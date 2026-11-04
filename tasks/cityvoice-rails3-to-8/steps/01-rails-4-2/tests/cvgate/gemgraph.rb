# The resolved dependency graph, read from the bundler's own metafile.
#
#   ruby -I/gates /gates/cvgate/gemgraph.rb --lock /app/Gemfile.lock
#
# The gate reads the bundler's metafile, the resolved dependency graph, not a
# grep over the Gemfile.  The difference is the whole point of the gate: a
# candidate can delete `gem 'sass-rails'` from the Gemfile and still have
# sprockets in the tree, because some other gem depends on it; and a candidate
# can leave a `gem` line commented out in the Gemfile while the lockfile has
# already moved on.  `Gemfile.lock` is what `bundle exec` actually installs, so
# it is what gets measured.
#
# `Bundler::LockfileParser` is bundler's own parser and is used when it can be
# loaded.  It cannot always be: the gate has to read a lockfile written by a
# bundler far newer than the one the verifier's Ruby ships, and a
# `BUNDLED WITH 2.7.x` header makes old bundlers raise.  The fallback is a
# parser for the lockfile's documented grammar, and the two are checked against
# each other on every lockfile this task ships -- see `selftest_tools.rb`.

require 'json'
require 'optparse'
require 'set'
require_relative 'ast'

module CVGate
  module GemGraph
    # `    rails (7.1.3)` / `      actionpack (= 7.1.3)`
    SPEC_RE = /\A    ([a-zA-Z0-9\-_.!]+) \(([^)]*)\)\z/.freeze
    DEP_RE  = /\A      ([a-zA-Z0-9\-_.!]+)(?: \((.*)\))?\z/.freeze
    SECTION_RE = /\A(\S.*)\z/.freeze

    # Parse without bundler.  Returns
    #   { 'specs' => {name => version}, 'edges' => {name => [dep, ...]},
    #     'direct' => [name, ...], 'ruby' => '3.4.10' or nil,
    #     'bundled_with' => '2.6.9' or nil, 'sources' => [...] }
    def self.parse(text)
      specs = {}
      edges = Hash.new { |h, k| h[k] = [] }
      direct = []
      sources = []
      ruby = nil
      bundled_with = nil
      section = nil
      current = nil
      expect_bundled_with = false
      expect_ruby = false

      text.each_line do |raw|
        line = raw.chomp
        if line.empty?
          current = nil
          next
        end

        if SECTION_RE.match(line) && !line.start_with?(' ')
          section = line.strip
          expect_bundled_with = (section == 'BUNDLED WITH')
          expect_ruby = (section == 'RUBY VERSION')
          current = nil
          next
        end

        if expect_bundled_with
          bundled_with = line.strip
          expect_bundled_with = false
          next
        end
        if expect_ruby
          ruby = line.strip.sub(/\Aruby\s+/, '')
          expect_ruby = false
          next
        end

        case section
        when 'GEM', 'GIT', 'PATH', 'PLUGIN SOURCE'
          if (match = SPEC_RE.match(line))
            current = match[1]
            specs[current] = match[2]
            edges[current] ||= []
          elsif (match = DEP_RE.match(line)) && current
            edges[current] << match[1]
          elsif line.strip.start_with?('remote:')
            sources << line.strip.sub(/\Aremote:\s*/, '')
          end
        when 'DEPENDENCIES'
          name = line.strip.split(/[\s(]/).first
          direct << name.sub(/!\z/, '') if name
        end
      end

      { 'specs' => specs, 'edges' => edges, 'direct' => direct.uniq.sort,
        'ruby' => ruby, 'bundled_with' => bundled_with,
        'sources' => sources.uniq.sort }
    end

    # Parse with bundler when bundler can read this lockfile, for cross-checking.
    #
    # `BUNDLE_GEMFILE` is pointed at a throwaway empty Gemfile first.  Bundler
    # 2.1 through 2.4 reach `Bundler.settings` while building a
    # `LazySpecification`, and `Bundler.settings` walks up from the working
    # directory looking for a Gemfile -- so parsing a lockfile *as a string*
    # raised `Bundler::GemfileNotFound` whenever the scan happened to run
    # outside an application, which is precisely where a gate runs.  Reading a
    # file should not depend on the reader's working directory.
    def self.parse_with_bundler(text)
      require 'bundler'
      require 'tmpdir'

      Dir.mktmpdir do |dir|
        File.write(File.join(dir, 'Gemfile'), "source 'https://rubygems.org'\n")
        previous = ENV['BUNDLE_GEMFILE']
        begin
          ENV['BUNDLE_GEMFILE'] = File.join(dir, 'Gemfile')
          Bundler.reset! if Bundler.respond_to?(:reset!)
          return parse_with_bundler!(text)
        ensure
          ENV['BUNDLE_GEMFILE'] = previous
          Bundler.reset! if Bundler.respond_to?(:reset!)
        end
      end
    rescue StandardError, LoadError => e
      { 'error' => "#{e.class}: #{e.message}" }
    end

    def self.parse_with_bundler!(text)
      parsed = Bundler::LockfileParser.new(text)
      specs = {}
      edges = {}
      parsed.specs.each do |spec|
        specs[spec.name] = spec.version.to_s
        edges[spec.name] = spec.dependencies.map(&:name).sort
      end
      { 'specs' => specs, 'edges' => edges,
        'direct' => parsed.dependencies.keys.sort }
    end

    # Every gem reachable from the direct dependencies, and how.  The path is
    # what makes a finding actionable: "sprockets is still here" is not a bug
    # report, "sass-rails -> sprockets" is.
    def self.reachable(graph)
      seen = {}
      queue = graph['direct'].map { |name| [name, [name]] }
      until queue.empty?
        name, path = queue.shift
        next if seen.key?(name)

        seen[name] = path
        (graph['edges'][name] || []).each do |dep|
          queue << [dep, path + [dep]] unless seen.key?(dep)
        end
      end
      seen
    end

    # ------------------------------------------------------------------
    # The Ruby the tree declares, in all three of the places it declares it
    # ------------------------------------------------------------------
    #
    # A lockfile `RUBY VERSION` stanza on its own is not the requirement the
    # instruction states.  Bundler reads the `ruby` directive out of the
    # Gemfile, rbenv reads `.ruby-version`, and the lockfile records whichever
    # interpreter resolved the set; a tree can satisfy any one of them and
    # still be inconsistent with the other two.  So all three are read from
    # the tree and reported separately, and `ruby_declared` is the version
    # they agree on -- or nil.

    # `.ruby-version`, stripped, with rbenv's `ruby-` prefix removed.  nil when
    # the file is absent or empty: an absent declaration is not a declaration,
    # and reporting it as the empty string would let it compare equal to
    # another absent one.
    def self.version_file(path)
      return nil unless File.file?(path)

      value = File.read(path, encoding: 'UTF-8').strip.sub(/\Aruby-/, '')
      value.empty? ? nil : value
    end

    # The Gemfile's `ruby` directive, or nil when it has none.
    #
    # Ripper and not a regexp, for the reason `ast.rb` gives: the word `ruby`
    # in a comment, in a string, or as the name of a block parameter is not a
    # directive, and no regular expression has to be taught the difference.
    #
    # Two spellings are accepted because bundler accepts two:
    #
    #   ruby "2.3.8"
    #   ruby file: ".ruby-version"    # bundler 2.2 and later
    #
    # The second defers to the same file rbenv reads, which is a legitimate
    # way to make the two agree rather than a way to dodge the requirement --
    # so it is resolved to that file's contents and compared like any other
    # declaration.  A named file that is not there resolves to nil.
    def self.gemfile_ruby(path, root)
      return nil unless File.file?(path)

      text = File.read(path, encoding: 'UTF-8')
      sexp = Ripper.sexp(text)
      return nil if sexp.nil?

      walker = Walker.new(text, path)
      walker.calls(sexp).each do |called, _line, node, parent|
        next unless called == 'ruby'

        args = walker.call_args(node, parent)
        next unless args

        strings = string_literals(args)
        if walker.call_keys(args).any? { |key, _line| key == 'file' }
          named = strings.first
          return named && version_file(File.expand_path(named, root))
        end
        return strings.first if strings.first
      end
      nil
    end

    # Every string literal in an argument list, in source order.  A literal
    # carrying interpolation is skipped rather than half-read: `ruby "2.#{x}"`
    # is not a version this gate can resolve, and reporting the fragment it
    # could read would be worse than reporting nothing.
    def self.string_literals(node, out = [])
      return out unless node.is_a?(Array)

      if node[0] == :string_literal
        parts = []
        interpolated = false
        collect = lambda do |inner|
          return unless inner.is_a?(Array)

          interpolated = true if inner[0] == :string_embexpr
          parts << inner[1] if inner[0] == :@tstring_content
          inner.each { |child| collect.call(child) if child.is_a?(Array) }
        end
        collect.call(node)
        out << parts.join unless interpolated
        return out
      end

      node.each { |child| string_literals(child, out) if child.is_a?(Array) }
      out
    end

    # `2.3.8` from `ruby-2.3.8`, and from `2.3.8p459` when the value came from
    # the lockfile.  Bundler writes the patchlevel it read off the interpreter
    # that resolved the set; neither the Gemfile directive nor `.ruby-version`
    # ever carries one, so stripping it is what makes the three comparable.
    def self.normalize_ruby(value, patchlevel: false)
      return nil if value.nil?

      text = value.to_s.strip.sub(/\Aruby[-\s]+/, '')
      text = text.sub(/p\d+\z/, '') if patchlevel
      text.empty? ? nil : text
    end

    # The one version all three declarations name, or nil.
    #
    # nil covers both failures on purpose -- a declaration that is missing and
    # three declarations that disagree are the same answer to "which Ruby does
    # this tree say it runs on?", which is: it does not say.
    def self.agreed_ruby(version_file, gemfile, lock)
      declared = [normalize_ruby(version_file), normalize_ruby(gemfile),
                  normalize_ruby(lock, patchlevel: true)]
      return nil if declared.any?(&:nil?)
      return nil unless declared.uniq.length == 1

      declared.first
    end

    # The tree's root defaults to the directory the lockfile sits in, which
    # is where `.ruby-version` and the Gemfile sit in every real application.
    def self.report(path, root = nil)
      text = File.read(path, encoding: 'UTF-8')
      graph = parse(text)
      cross = parse_with_bundler(text)
      reach = reachable(graph)
      root = File.expand_path(root || File.dirname(File.expand_path(path)))
      from_file = version_file(File.join(root, '.ruby-version'))
      from_gemfile = gemfile_ruby(File.join(root, 'Gemfile'), root)
      {
        'lockfile' => path,
        'root' => root,
        'ruby' => graph['ruby'],
        'ruby_version_file' => from_file,
        'gemfile_ruby' => from_gemfile,
        'ruby_declared' => agreed_ruby(from_file, from_gemfile, graph['ruby']),
        'bundled_with' => graph['bundled_with'],
        'sources' => graph['sources'],
        'direct' => graph['direct'],
        'specs' => graph['specs'],
        'reachable' => reach.keys.sort,
        'paths' => reach,
        # Present only when bundler could read the file at all.  An
        # unreachable second opinion is reported as one, not swallowed.
        'bundler_agrees' => cross['error'] ? nil : (cross['specs'] == graph['specs']),
        'bundler_error' => cross['error'],
        # A resolved gem nothing depends on -- a lockfile bundler would prune.
        'orphans' => (graph['specs'].keys - reach.keys).sort,
        # A gem depended on but not resolved.  `bundler` itself is normally the
        # only one (Rails declares a dependency on it; it is never locked), and
        # anything *else* here means the lockfile is inconsistent.
        'unresolved' => (reach.keys - graph['specs'].keys).sort
      }
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { lock: '/app/Gemfile.lock' }
  OptionParser.new do |opts|
    opts.on('--lock PATH') { |v| options[:lock] = v }
    opts.on('--root PATH') { |v| options[:root] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
  end.parse!(ARGV)

  payload = JSON.pretty_generate(
    CVGate::GemGraph.report(options[:lock], options[:root])
  )
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
end
