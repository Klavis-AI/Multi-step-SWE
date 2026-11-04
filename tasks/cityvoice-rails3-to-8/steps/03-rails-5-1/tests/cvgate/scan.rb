# Apply the rules of `rules.rb` to the sources of `ast.rb`.
#
#   ruby -I/gates /gates/cvgate/scan.rb --root /app --rules attr_accessible,fixnum
#
# Prints JSON: per-rule counts, every finding with file and line, the number of
# files scanned, and the files that could not be parsed.  A step's verifier
# selects the rules it gates on; the scan itself always walks the whole tree.
#
# `--pristine <manifest>` additionally checks the tree against a list of paths
# the repository shipped with -- its own spec suite, and the application code
# under `app/`, `lib/` and `db/migrate/`.  Every step's instruction forbids
# deleting the repository's own tests, and deleting them is otherwise the
# cheapest way to satisfy a construct gate: nine `before_filter` calls live in
# `spec/`, and `rm -rf spec` removes nine findings.  A count of files scanned
# cannot see that -- the candidate is also *adding* files -- so the paths
# themselves are shipped with the verifier and looked for by name.

require 'digest'
require 'json'
require 'optparse'
require_relative 'ast'
require_relative 'rules'

module CVGate
  class Scanner
    LAMBDA_CALLS = %w[lambda proc].freeze

    # One column row of an annotating gem's schema comment:
    #
    #   #  id         :integer          not null, primary key
    #
    # The name and the type are all that is matched.  Everything after the
    # type is the adapter's own wording and moves between releases of the
    # database, of the adapter and of the annotating tool; the row itself is
    # the reading, and it is what a stripped comment block no longer has.
    SCHEMA_COLUMN = /\A#\s+\w+\s+:\w+/.freeze

    def initialize(root, rule_names, pristine = nil)
      @root = File.expand_path(root)
      unknown = rule_names - RULES.keys
      raise ArgumentError, "unknown rules: #{unknown.join(',')}" if unknown.any?

      @rule_names = rule_names
      @pristine_path = pristine
      @pristine = pristine.nil? ? nil : load_pristine(pristine)
      @findings = []
      @parse_errors = []
      @migrations = {}
      @files = 0
      @erb_files = 0
      # Compiled ERB carries a wrapper line; every finding's line number has
      # this subtracted so it points into the template the reader has open.
      @line_offset = 0
    end

    attr_reader :findings, :parse_errors

    # A manifest that is missing or malformed raises rather than reporting
    # nothing missing.  The failure mode this guards against is the option
    # silently not arriving -- a renamed file, a copy step that forgot to
    # place it -- which would otherwise read as two empty lists, which reads as
    # a pass.  Raising means no `--out` is written, the evidence is a stub, and
    # the guards that depend on it fail.
    def load_pristine(path)
      raise ArgumentError, "pristine manifest not found: #{path}" unless File.file?(path)

      data = JSON.parse(File.read(path, encoding: 'UTF-8'))
      keys = %w[tests core tests_substantive]
      lists = data.is_a?(Hash) && keys.all? { |k| data[k].is_a?(Array) }
      raise ArgumentError, "pristine manifest needs #{keys.join(', ')} lists: #{path}" unless lists

      data
    end

    # Manifest paths with nothing at them now.  A path is looked for by
    # `File.exist?` and not by `File.file?`: a spec replaced by a directory of
    # the same name is a question for a human, not a silent miss.
    def missing_from(list)
      list.reject { |rel| File.exist?(File.join(@root, rel)) }.sort
    end

    # An example declaration, as RSpec spells them in this suite: `it`, the
    # one-liner `its`, `specify`, and the shared-example forms.  The word
    # boundary matters -- `it_behaves_like` is an example and `iterate` is not,
    # and neither is `foo.it`.
    EXAMPLE = /(^|[^\w.])(it|its|specify|scenario|example|it_behaves_like|
                 it_should_behave_like|include_examples|shared_examples)[\s({]/x

    # Manifest spec files that are still at their path but no longer declare a
    # single example.  Existence alone is not enough to say the suite survived:
    # truncating a spec to `require 'spec_helper'`, or to nothing at all, keeps
    # the path and removes every expectation in it, and that is the cheap way
    # to make a failing suite stop failing.  Only `*_spec.rb` is judged this
    # way; the manifest's fixtures, factories and helpers legitimately contain
    # no examples.
    def gutted_from(list)
      list.select { |rel| rel.end_with?('_spec.rb') }.reject do |rel|
        path = File.join(@root, rel)
        next true unless File.file?(path)

        body = begin
          File.read(path, encoding: 'UTF-8')
        rescue StandardError
          ''
        end
        !(body =~ EXAMPLE).nil?
      end.sort
    end

    # A line that asserts something, in every spelling this suite uses: the
    # rspec-2 `subject.should`, the bare `should` of a one-liner, the rspec-3
    # `expect`/`is_expected`, and `assert*` for anything left from Test::Unit.
    # Matched against a line with its comments already removed, so the word in
    # a comment does not count.
    ASSERTION = /\b(expect|is_expected|should|should_not|assert\w*)\b/

    # How many DISTINCT assertion lines the shipped spec files still contain.
    #
    # `gutted_tests` asks whether a file still *declares* an example.  This
    # asks whether the examples still *assert* anything, and the two are
    # different attacks.  A suite of five hundred examples each reading
    # `it { expect(true).to eq true }` keeps every path, every declaration and
    # the example count, runs green, and preserves not one inherited
    # expectation.  Counting DISTINCT lines rather than occurrences is what
    # makes that visible: five hundred copies of one tautology are one line,
    # and to reach a floor in the hundreds a tree has to write hundreds of
    # textually different assertions, which is the work it was trying to avoid.
    #
    # The line is assembled from `Ripper.lex` tokens rather than from the raw
    # text, for one reason: a trailing comment is a free distinct suffix.
    # `expect(true).to eq true # 1`, `# 2`, `# 3` would otherwise be three
    # distinct lines and the count would measure the attacker's patience.  The
    # lexer separates comments from string content, so `# ` inside a string is
    # kept and a real comment is dropped.  Whitespace inside a token is
    # collapsed, and a run of whitespace BETWEEN two tokens becomes exactly one
    # space, so reindenting a spec does not move the number.
    #
    # One space and not none.  Joining the tokens with nothing welds the words
    # either side of the space into one, and a welded word has no boundary in
    # the middle: `response.status.should eq(200)` comes out `response . status
    # . should eq ( 200 )` with the separator, and with `shouldeq` without it
    # -- which `\bshould\b` does not match.  Every `x.should eq(...)`,
    # `page.should have_content ...` and `should_not be_nil` in the repository's
    # own suite would read as a line with no assertion on it, which is most of
    # a 2013 RSpec suite: the union is 401 lines, and 212 without the
    # separator.  Collapsing the run to one space is what keeps reindentation
    # invisible without deleting the boundary.
    # Per file, so that the two readings below are one pass and cannot disagree
    # about what an assertion line is.  The value is the file's own distinct
    # assertion lines; the union of the values is the tree's.
    def assertion_lines_by_file(list)
      out = {}
      list.select { |rel| rel.end_with?('_spec.rb') }.each do |rel|
        path = File.join(@root, rel)
        next unless File.file?(path)

        text = begin
          File.read(path, encoding: 'UTF-8')
        rescue StandardError
          next
        end
        by_line = Hash.new { |hash, key| hash[key] = [] }
        begin
          Ripper.lex(text).each do |row|
            next if %i[on_comment on_sp on_nl on_ignored_nl].include?(row[1])

            by_line[row[0][0]] << row[2].to_s.gsub(/\s+/, ' ')
          end
        rescue StandardError
          next
        end
        out[rel] = by_line.values.map { |tokens| tokens.join(' ') }
                          .select { |line| !(line =~ ASSERTION).nil? }.uniq
      end
      out
    end

    def assertion_lines_from(list)
      seen = {}
      assertion_lines_by_file(list).each_value do |lines|
        lines.each { |line| seen[line] = true }
      end
      seen.length
    end

    # The files that still declare examples and no longer assert anything.
    #
    # The tree-wide count above is a floor over the union, and a union has a
    # shape the floor cannot see: one spec file carrying every surviving
    # assertion and fifty carrying none reaches the same number as fifty files
    # that each kept their own.  This is the shape.  A file the repository
    # shipped, which is still there and still declares examples, and whose
    # examples assert nothing, is named -- and the guard is that the list is
    # empty, so the count and the distribution are both stated.
    #
    # A file that is gone is `missing_tests`' finding and a file that declares
    # no example at all is `gutted_tests`', so neither is repeated here: this
    # asks only about files that are present and still look like a suite.
    def hollow_from(list)
      lines = assertion_lines_by_file(list)
      declared = list.select { |rel| rel.end_with?('_spec.rb') } - gutted_from(list)
      declared.select { |rel| lines.key?(rel) && lines[rel].empty? }.sort
    end

    # A line that is neither blank nor a line comment, in the two comment
    # spellings this suite uses.  Deliberately the same rule as the manifest
    # generator's: the two are a pair, and a file counts as emptied only if the
    # generator counted it as filled by the identical test.
    def substantive?(body)
      body.each_line.any? do |line|
        stripped = line.strip
        !stripped.empty? && !stripped.start_with?('#') && !stripped.start_with?('//')
      end
    end

    # Manifest test files that had content of their own in the baseline and
    # have none now.
    #
    # `gutted_tests` and `hollow_tests` both judge `*_spec.rb` only, because an
    # example declaration and an assertion are things only a spec file has.
    # That leaves 27 of the 78 shipped test paths -- the factories, the
    # fixtures, `spec_helper.rb`, the Teaspoon JavaScript -- judged by nothing
    # but existence, so `> /app/spec/factories/locations.rb` keeps the path,
    # keeps the count, and removes the fixtures the suite builds on.  This is
    # the check those files get: whatever the file is, it still says something.
    #
    # The comparison is against `tests_substantive` rather than `tests` because
    # one shipped file -- `spec/javascripts/spec_helper.js` -- is comment and
    # nothing else in the baseline.  Asking all 78 for a substantive line would
    # fail the untouched repository; asking the 77 that had one asks about a
    # change the candidate made.
    def emptied_from(list)
      list.reject do |rel|
        path = File.join(@root, rel)
        # Rejected, so not reported: a file that is not there is
        # `missing_tests`' finding, and the two must not double-count.
        next true unless File.file?(path)

        body = begin
          File.read(path, encoding: 'UTF-8')
        rescue StandardError
          ''
        end
        substantive?(body)
      end.sort
    end

    # db/migrate/*.rb, and the schema version each one declares.
    #
    # `unversioned_migration` and `misversioned_migration` are counts of
    # *findings*, and a count of findings is zero on a tree with nothing to be
    # wrong about.  For the second of those that is not a corner case, it is the
    # tree this step is handed: a migration with no `[x.y]` suffix at all is an
    # `unversioned` finding and never a `misversioned` one, so `misversioned ==
    # 0` holds on 43 migrations that declare no version whatsoever.  A gate on
    # it is a free point.
    #
    # This is the positive reading.  The value is a histogram of what the
    # migrations actually declare, with `.` replaced by `_` in the keys so that
    # a version is one segment of a gate's dotted path, plus the count that
    # declare nothing.  A gate can then ask for "43 of them say 4.2", which no
    # tree satisfies by leaving the files alone.
    #
    # The histogram is reported twice: once over everything under `db/migrate/`
    # and once over the files the manifest says shipped with the repository.
    # The second exists because the first counts any file put there.  A tree
    # that stamps the 43 shipped migrations `[5.0]` and drops in 43 dummy
    # `[4.2]` classes alongside them satisfies "43 say 4.2" on the whole
    # directory while failing the thing that sentence is about.  Restricted to
    # the shipped list the dummies are not counted and the stamped files are.
    #
    # `shipped` is nil when no manifest was read, and every key under it is
    # then nil or empty rather than zero: a predicate over an absent value is
    # false, so a gate on it fails when the manifest did not arrive, which is
    # the direction an unverifiable reading should fail in.
    def migrations_h(shipped)
      all = histogram(@migrations.keys)
      own = shipped && histogram(shipped)
      { 'total' => @migrations.length,
        'unversioned' => all[0],
        'versions' => all[1],
        'shipped_total' => shipped && shipped.length,
        'shipped_unversioned' => own && own[0],
        'shipped_versions' => own ? own[1] : {} }
    end

    # [how many of these files declare no version, what the rest declare].
    # A path with no entry in `@migrations` -- a shipped migration that has
    # been deleted -- contributes to neither, so deleting a file lowers the
    # count a gate asks for rather than moving it into the unversioned bucket.
    def histogram(files)
      versions = Hash.new(0)
      unversioned = 0
      files.each do |file|
        next unless @migrations.key?(file)

        if @migrations[file].nil?
          unversioned += 1
        else
          versions[@migrations[file].to_s.tr('.', '_')] += 1
        end
      end
      [unversioned, Hash[versions.sort]]
    end

    # One migration file's declared version: the `x.y` of
    # `ActiveRecord::Migration[x.y]`, nil for a bare `ActiveRecord::Migration`,
    # and the string 'not_a_literal' for a suffix that is not a literal --
    # `[Rails::VERSION::STRING.to_f]` is a version this scanner has not read
    # rather than one it has checked, and the two must not share a bucket.
    def note_migration(walker, sexp, file)
      return unless file.start_with?('db/migrate/')

      @migrations[file] = nil unless @migrations.key?(file)
      walker.class_defs(sexp).each do |_klass, superclass, _line|
        next if superclass.nil?

        if superclass.is_a?(Array) && superclass[0] == :aref
          next unless walker.const_name(superclass[1]) == 'ActiveRecord::Migration'

          @migrations[file] = walker.aref_index(superclass) || 'not_a_literal'
        else
          next unless walker.const_name(superclass) == 'ActiveRecord::Migration'

          @migrations[file] = nil
        end
      end
    end

    def run
      ruby_files, erb_files = CVGate.sources(@root)
      ruby_files.each do |path|
        @files += 1
        @line_offset = 0
        scan_text(File.read(path, encoding: 'UTF-8'), rel(path), lex: true)
      end
      erb_files.each do |path|
        @erb_files += 1
        raw = File.read(path, encoding: 'UTF-8')
        src = CVGate.erb_source(raw)
        if src.nil?
          @parse_errors << { 'file' => rel(path), 'why' => 'erb would not compile' }
          next
        end
        @line_offset = ERBCompile::LINE_OFFSET
        # `lex: true` still finds Ruby comments inside `<% %>` code tags, which
        # survive compilation.  ERB's *own* comment tag (`<%# ... %>`) carries
        # no code and is therefore absent from the compiled source, so it is
        # read off the template separately -- a suppression directive in a view
        # is still a suppression directive.
        scan_text(src, rel(path), lex: true)
        scan_erb_comments(raw, rel(path)) if @rule_names.include?('suppression')
        @line_offset = 0
      end
      self
    end

    def scan_erb_comments(raw, file)
      rule = RULES.fetch('suppression')
      ERBCompile.comments(raw).each do |line, text|
        SUPPRESSION_PATTERNS.each do |pattern, label|
          next unless pattern =~ text

          add('suppression', file, line + @line_offset,
              "#{rule[:detail]}: #{label}")
        end
      end
    end

    # A rule this scanner cannot evaluate -- `sprockets_directive`, whose
    # subject is an asset file and whose count is `assets.rb`'s to report --
    # must not appear in `counts`.  A zero there is indistinguishable from a
    # zero that was measured, and a later step's profile gating it would be
    # gating a constant: passing, always, on every tree, for nothing.  It is
    # named in `not_evaluated` instead, so that a reader of the evidence can
    # see the rule was asked for and see where its answer lives.
    def unevaluated
      @rule_names.select { |name| RULES.fetch(name)[:kind] == :sprockets }
    end

    def to_h
      skipped = unevaluated
      counts = Hash[(@rule_names - skipped).map { |r| [r, 0] }]
      @findings.each { |f| counts[f.rule] = counts.fetch(f.rule, 0) + 1 }
      {
        'root' => @root,
        'rules' => @rule_names,
        'not_evaluated' => skipped,
        # The manifest that was read, or nil.  Reported so that "nothing is
        # missing" and "nothing was looked for" are different answers.
        'pristine' => @pristine_path,
        'missing_tests' => @pristine ? missing_from(@pristine['tests']) : [],
        'missing_core' => @pristine ? missing_from(@pristine['core']) : [],
        'gutted_tests' => @pristine ? gutted_from(@pristine['tests']) : [],
        'assertion_lines' => @pristine ? assertion_lines_from(@pristine['tests']) : nil,
        'hollow_tests' => @pristine ? hollow_from(@pristine['tests']) : [],
        'emptied_tests' => @pristine ? emptied_from(@pristine['tests_substantive']) : [],
        'migrations' => migrations_h(
          @pristine && @pristine['core'].select { |rel| rel.start_with?('db/migrate/') }
        ),
        'files_scanned' => @files,
        'erb_scanned' => @erb_files,
        'annotations' => annotations_h,
        'counts' => counts,
        'total' => @findings.length,
        'parse_errors' => @parse_errors,
        'findings' => @findings.map(&:to_h)
                               .sort_by { |f| [f['file'], f['line'], f['rule']] }
      }
    end

    private

    def rel(path)
      path.sub(/\A#{Regexp.escape(@root)}\/?/, '')
    end

    # The schema comment an annotating gem writes above each model class.
    #
    # This is a property of the REPOSITORY and not of any framework release:
    # seven of this application's models carry the block, it has been there
    # since the baseline, and it is the thing that would quietly disappear if
    # the annotating gem were dropped rather than replaced -- with every other
    # reading in this verifier still green, because nothing else reads a
    # comment.
    #
    # WHAT IS COUNTED IS COLUMN ROWS, not bytes.  A digest of the block is
    # reported per model, but the count is what a step gates or guards on,
    # because re-generating the comments with a maintained tool is a legitimate
    # thing to do and moves bytes: measured on this repository, the fork's own
    # CLI rewrites fourteen files.  A row per column survives that; a stripped
    # block does not.
    def annotations_h
      models = {}
      columns = 0
      lines_total = 0
      Dir.glob(File.join(@root, 'app/models/**/*.rb')).sort.each do |path|
        # Same shape as every other read in this file: a file that cannot be
        # read, or whose bytes are not the encoding it claims -- `include?`
        # raises on an invalid sequence -- is a file with no block, not an
        # exception that takes the whole scan report down with it.
        lines = begin
          File.read(path, encoding: 'UTF-8').split("\n", -1)
        rescue StandardError
          []
        end
        start = begin
          lines.index { |ln| ln.include?('== Schema Information') }
        rescue StandardError
          nil
        end
        next if start.nil?
        stop = start
        stop += 1 while stop < lines.length && lines[stop].lstrip.start_with?('#')
        block = lines[start...stop]
        cols = block.count { |ln| SCHEMA_COLUMN.match(ln) }
        models[rel(path)] = { 'lines' => block.length, 'columns' => cols,
                              'digest' => Digest::SHA256.hexdigest(
                                block.join("\n"))[0, 12] }
        columns += cols
        lines_total += block.length
      end
      { 'models' => models,
        'count' => models.length,
        'columns' => columns,
        'lines' => lines_total,
        'digest' => Digest::SHA256.hexdigest(
          models.map { |k, v| "#{k} #{v['digest']}" }.join("\n"))[0, 12] }
    end

    def scan_text(text, file, lex:)
      sexp = Ripper.sexp(text)
      if sexp.nil?
        # A file Ruby cannot parse is a finding in its own right.  It is also
        # the one case where the construct counts below are not trustworthy
        # for that file, so it is reported separately and loudly rather than
        # folded into a zero.
        @parse_errors << { 'file' => file, 'why' => 'ruby would not parse' }
        return
      end

      walker = Walker.new(text, file)
      note_migration(walker, sexp, file)
      @rule_names.each do |name|
        apply(RULES.fetch(name), name, walker, sexp, file, text, lex)
      end
    end

    def apply(rule, name, walker, sexp, file, text, lex)
      # `only_in` scopes a rule to the part of the tree where the construct it
      # reads actually means something.  A declaration that has to be in
      # `config/` to take effect is not counted anywhere else: the same call
      # written in a spec, a seed script or a rake task names the method
      # without making the application declare anything, and a count that
      # included those would be answering a question the instruction did not
      # ask.  Paths are the relative ones `rel` produces.
      return if rule[:only_in] &&
                rule[:only_in].none? { |prefix| file.start_with?(prefix) }

      case rule[:kind]
      when :call            then rule_call(rule, name, walker, sexp, file)
      when :call_key        then rule_call_key(rule, name, walker, sexp, file)
      when :call_arg_symbol then rule_call_arg_symbol(rule, name, walker, sexp, file)
      when :call_arg_version
        rule_call_arg_version(rule, name, walker, sexp, file)
      when :const           then rule_const(rule, name, walker, sexp, file)
      when :const_path      then rule_const_path(rule, name, walker, sexp, file)
      when :assign          then rule_assign(rule, name, walker, sexp, file)
      when :gvar            then rule_gvar(rule, name, walker, sexp, file)
      when :superclass      then rule_superclass(rule, name, walker, sexp, file)
      when :superclass_version
        rule_superclass_version(rule, name, walker, sexp, file)
      when :scope_nonlambda then rule_scope(rule, name, walker, sexp, file)
      when :lex             then rule_lex(rule, name, file, text) if lex
      when :sprockets       then nil # asset files, handled by `assets.rb`
      else raise ArgumentError, "rule #{name} has no kind"
      end
    end

    def add(name, file, line, detail)
      @findings << Finding.new(name, file, [(line || 0) - @line_offset, 0].max, detail)
    end

    def rule_call(rule, name, walker, sexp, file)
      wanted = rule[:names]
      receivers = rule[:receivers]
      # `via` names the dispatch methods that reach the same call by its name
      # rather than by writing it: `X.send(:confirmation_email, y)` is the same
      # call as `X.confirmation_email(y)`, and the rules are about spellings
      # of the same requirement, not about how it was typed.  Only meaningful
      # with `receivers`, because an unqualified `send(:foo)` says nothing about
      # who receives it.
      via = rule[:via]
      walker.calls(sexp).each do |called, line, node, parent|
        dispatched = nil
        if !wanted.include?(called)
          next unless via && receivers && via.include?(called)

          args = walker.call_args(node, parent)
          dispatched = args ? (symbol_args(args) & wanted).first : nil
          next unless dispatched
        end

        if receivers
          # `URI.escape` is a finding; a local `escape(x)` is not.  The
          # receiver is read from the node rather than from the text.
          recv = %i[call command_call field].include?(node[0]) ? node[1] : nil
          next unless recv && receivers.include?(walker.const_name(recv))
        end
        shown = dispatched ? "#{called} :#{dispatched}" : called
        add(name, file, line, "#{rule[:detail]}: #{shown}")
      end
    end

    def rule_call_key(rule, name, walker, sexp, file)
      walker.calls(sexp).each do |called, _line, node, parent|
        keys = rule[:names][called]
        next unless keys

        args = walker.call_args(node, parent)
        next unless args

        walker.call_keys(args).each do |key, line|
          next unless keys.include?(key)

          add(name, file, line, "#{rule[:detail]}: #{called} #{key}:")
        end
      end
    end

    def rule_call_arg_symbol(rule, name, walker, sexp, file)
      walker.calls(sexp).each do |called, line, node, parent|
        wanted = rule[:names][called]
        next unless wanted

        args = walker.call_args(node, parent)
        next unless args
        next unless symbol_args(args).include?(wanted)

        add(name, file, line, "#{rule[:detail]}: #{called} :#{wanted}")
      end
    end

    # A call whose literal arguments include the wanted version.
    #
    # The match and not the complement.  The complement -- "no `load_defaults`
    # names anything but 5.1" -- reads 0 on a tree that declares 5.1, 1 on a
    # tree that declares 5.0, and 0 again on a tree that declares nothing at
    # all, because there is nothing to be wrong.  A reading that is vacuously
    # clean on the tree the step is handed can only be a guard, and as a guard
    # it costs a tree that named the wrong release its whole gates family while
    # a tree that named no release keeps almost all of it: the near miss scored
    # below the skip.  The match reads 1, 0 and 0 on those same three trees, so
    # it is a gate that the handed-on tree fails, and every way of not saying
    # 5.1 costs the same three points.
    #
    # A call whose argument is not a literal -- `load_defaults(TARGET)` -- does
    # not count: the declaration is supposed to be readable, and an indirection
    # through a constant is a version this instrument cannot confirm.
    #
    # `via` reads the same declaration reached through a dispatch method:
    # `config.send(:load_defaults, 5.1)` declares exactly what
    # `config.load_defaults 5.1` declares, and the rules are about spellings
    # of the same requirement.  The dispatched name is taken from the symbol
    # argument and the version from the literal ones, so both have to be there.
    def rule_call_arg_version(rule, name, walker, sexp, file)
      via = rule[:via]
      walker.calls(sexp).each do |called, line, node, parent|
        wanted = rule[:names][called]
        args = walker.call_args(node, parent)
        if wanted.nil?
          next unless via && via.include?(called)

          dispatched = args ? (symbol_args(args) & rule[:names].keys).first : nil
          next unless dispatched

          wanted = rule[:names][dispatched]
          called = "#{called} :#{dispatched}"
        end

        got = args ? literal_args(args) : []
        next unless got.include?(wanted)

        add(name, file, line, "#{rule[:detail]}: #{called} #{wanted}")
      end
    end

    # Literal arguments, as the strings they are written as.  `5.1` is a Float
    # token and `"5.1"` a string literal, and Rails takes either -- it calls
    # `target_version.to_s` -- so both are read back as `"5.1"`.
    def literal_args(node, out = [])
      return out unless node.is_a?(Array)

      case node[0]
      when :@float, :@int
        out << node[1]
        return out
      when :string_literal
        text = string_text(node)
        out << text if text
        return out
      end
      node.each { |child| literal_args(child, out) if child.is_a?(Array) }
      out
    end

    # The content of a string literal, or nil when any part of it is
    # interpolated -- `"#{a}.1"` names no version this instrument can read.
    def string_text(node, parts = [])
      return nil unless node.is_a?(Array)

      case node[0]
      when :@tstring_content then parts << node[1]
      when :string_embexpr then return nil
      else
        node.each do |child|
          next unless child.is_a?(Array)
          return nil if string_text(child, parts).nil?
        end
      end
      parts.join
    end

    # Symbols AND single-part string literals, because `send` takes either and
    # `X.send('set_defaults')` is the same call as `X.send(:set_defaults)`.
    # Reading only the symbol form would leave a rule that one quotation mark
    # defeats, and a spelling of the same requirement is the requirement.
    #
    # `string_literal` with exactly one `tstring_content` part, and no
    # interpolation: `"set_#{name}"` is not a literal and the scanner does not
    # guess at what it evaluates to.  A string in a dispatch position that
    # does not name one of the rule's own methods is discarded by the
    # intersection above it either way, so widening here cannot invent a
    # finding out of an unrelated string argument.
    def symbol_args(node, out = [])
      return out unless node.is_a?(Array)

      if node[0] == :symbol_literal && node[1].is_a?(Array) && node[1][0] == :symbol
        token = node[1][1]
        out << token[1] if token.is_a?(Array)
      elsif node[0] == :string_literal
        out.concat(string_literal_parts(node))
      end
      node.each { |child| symbol_args(child, out) if child.is_a?(Array) }
      out
    end

    # `[:string_literal, [:string_content, [:@tstring_content, "x", [1, 1]]]]`
    # -> `["x"]`; anything with a second part, or with an `:string_embexpr`
    # among them, -> `[]`.
    def string_literal_parts(node)
      content = node[1]
      return [] unless content.is_a?(Array) && content[0] == :string_content

      parts = content[1..-1] || []
      return [] unless parts.length == 1

      part = parts[0]
      return [] unless part.is_a?(Array) && part[0] == :@tstring_content

      [part[1]]
    end

    def rule_const(rule, name, walker, sexp, file)
      walker.constants(sexp).each do |const, line|
        next unless rule[:names].include?(const)

        add(name, file, line, "#{rule[:detail]}: #{const}")
      end
    end

    def rule_const_path(rule, name, walker, sexp, file)
      find_const_paths(walker, sexp).each do |path, line|
        next unless rule[:names].include?(path)

        add(name, file, line, "#{rule[:detail]}: #{path}")
      end
    end

    def find_const_paths(walker, sexp, out = [])
      return out unless sexp.is_a?(Array)

      if %i[const_path_ref const_path_field].include?(sexp[0])
        path = walker.const_name(sexp)
        out << [path, walker.first_line(sexp)] if path
      end
      sexp.each { |child| find_const_paths(walker, child, out) if child.is_a?(Array) }
      out
    end

    def rule_assign(rule, name, walker, sexp, file)
      walker.assign_targets(sexp).each do |target, line|
        next unless rule[:names].include?(target)

        add(name, file, line, "#{rule[:detail]}: #{target} =")
      end
    end

    def rule_gvar(rule, name, walker, sexp, file)
      walker.globals(sexp).each do |gvar, line|
        next unless rule[:names].include?(gvar)

        add(name, file, line, "#{rule[:detail]}: #{gvar}")
      end
    end

    def rule_superclass(rule, name, walker, sexp, file)
      walker.class_defs(sexp).each do |_klass, superclass, line|
        next if superclass.nil?
        # `ActiveRecord::Migration[7.1]` parses as an :aref around the constant
        # path.  That is exactly the versioned form, so it is the *absence* of
        # the :aref that is the finding.
        next if superclass.is_a?(Array) && superclass[0] == :aref

        path = walker.const_name(superclass)
        next unless rule[:names].include?(path)

        add(name, file, line, "#{rule[:detail]}: #{path}")
      end
    end

    # `unversioned_migration`'s complement: the suffix is there, and it is the
    # wrong one.  The two rules are deliberately separate counts -- a tree that
    # never versioned its migrations and a tree that versioned them at the
    # release it is moving to are different mistakes, and a single count would
    # report them as the same number.
    def rule_superclass_version(rule, name, walker, sexp, file)
      walker.class_defs(sexp).each do |_klass, superclass, line|
        next unless superclass.is_a?(Array) && superclass[0] == :aref

        path = walker.const_name(superclass[1])
        want = rule[:names][path]
        next if want.nil?

        got = walker.aref_index(superclass)
        next if got == want

        add(name, file, line,
            "#{rule[:detail]}: #{path}[#{got || 'not a literal'}]")
      end
    end

    def rule_scope(rule, name, walker, sexp, file)
      walker.calls(sexp).each do |called, line, node, parent|
        next unless rule[:names].include?(called)

        args = walker.call_args(node, parent)
        next unless args.is_a?(Array) && args[0] == :args_add_block

        body = args[1].is_a?(Array) ? args[1][1] : nil
        next if body.nil?      # `scope :x` alone takes no body to judge
        next if callable?(body)

        add(name, file, line, rule[:detail])
      end
    end

    # A scope body Rails 4.1 accepts is one that responds to `call`.
    def callable?(node)
      return false unless node.is_a?(Array)
      return true if node[0] == :lambda

      if node[0] == :method_add_block
        inner = node[1]
        inner = inner[1] if inner.is_a?(Array) && inner[0] == :method_add_arg
        if inner.is_a?(Array) && %i[fcall vcall].include?(inner[0])
          return LAMBDA_CALLS.include?(inner[1][1])
        end
        # `Proc.new { }`
        if inner.is_a?(Array) && inner[0] == :call
          return Walker.new('', '').const_name(inner[1]) == 'Proc'
        end
      end
      false
    end

    def rule_lex(rule, name, file, text)
      # `Ripper.lex` hands back comments already separated from string content,
      # so the word `rubocop:disable` inside a heredoc -- in this very file's
      # own documentation, for instance -- is not counted as a suppression.
      #
      # `Ripper.lex` yields 3-element rows on Ruby 2.3 and 4-element rows from
      # 2.6, so the row is taken apart positionally rather than destructured
      # into a fixed arity.  `pattern =~ token` rather than `pattern.match?`:
      # `Regexp#match?` arrived in Ruby 2.4, and the step-1 verifier runs 2.3.
      # Nothing is rescued here -- an exception from the lexer is a fact about
      # the file worth crashing over, and rescuing it would turn exactly that
      # kind of `NoMethodError` into a silent zero.
      Ripper.lex(text).each do |row|
        next unless row[1] == :on_comment

        line = row[0][0]
        token = row[2]
        SUPPRESSION_PATTERNS.each do |pattern, label|
          next unless pattern =~ token

          add(name, file, line, "#{rule[:detail]}: #{label}")
        end
      end
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { root: '/app', rules: CVGate::RULES.keys - ['sprockets_directive'] }
  OptionParser.new do |opts|
    opts.on('--root ROOT') { |v| options[:root] = v }
    opts.on('--rules LIST') { |v| options[:rules] = v.split(',').map(&:strip).reject(&:empty?) }
    opts.on('--pristine PATH') { |v| options[:pristine] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
  end.parse!(ARGV)

  scanner = CVGate::Scanner.new(options[:root], options[:rules],
                                options[:pristine]).run
  payload = JSON.pretty_generate(scanner.to_h)
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
end
