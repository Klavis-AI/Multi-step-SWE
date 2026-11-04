# Which resolved gems have a published advisory against them.
#
#   ruby /tests/cvgate/audit.rb --lock /app/Gemfile.lock \
#        --db /opt/ruby-advisory-db --out gates/audit.json
#
# The lockfile is read with `gemgraph.rb`'s parser -- the same one the
# dependency gates use, so "which gems are in this tree, at which versions"
# has one answer in this package and not two.  The advisories are read out of
# a COPY OF rubysec/ruby-advisory-db unpacked into the verifier image at a
# recorded commit, and the matching is done here rather than by shelling out
# to `bundler-audit`.  Three reasons:
#
#   * `bundle-audit` reads `.bundler-audit.yml` FROM THE DIRECTORY IT SCANS.
#     A candidate who writes `ignore: [CVE-...]` into the application's own
#     root takes the count down without touching a gem -- measured, 31 -> 30.
#     A gate that shells out to it has to defend against that file; a gate
#     that never reads it cannot be affected by it, and this one never reads
#     it.  (It is still REPORTED, under `suppression`, because a tree that
#     tried it is worth seeing in the evidence.)
#   * The database is a git repository that moves daily.  Pinning the
#     directory pins the reading, and a probe that does its own matching
#     cannot be surprised by a change in how a future bundler-audit decides
#     what "patched" means.
#   * `bundle-audit`'s exit status is 1 both for "found advisories" and for
#     "could not read the database".  Here the two are different keys.
#
# The matching rule is bundler-audit 0.9.3's own, restated
# (`Bundler::Audit::Advisory#vulnerable?`): a version is vulnerable unless it
# satisfies one of `unaffected_versions`, and -- when the advisory names any
# `patched_versions` at all -- unless it satisfies one of those.  An advisory
# with NO patched versions is an unfixed one and matches every version of its
# gem.  Each entry in either list may carry more than one constraint in one
# string (`">= 2.12.5, < 3.0.0"`; 142 of the 1245 do), so each is split on
# `, ` before `Gem::Requirement` sees it -- which is what bundler-audit does,
# and what a single `Gem::Requirement.new(str)` would get wrong by raising.
#
# On the two reference trees this implementation gives the numbers
# `bundle-audit 0.9.3` itself produced: 31 advisories over 7 gems on step 12's
# tree, 0 on the resolved one.  `selftest_tools.rb` covers the matching rule
# case by case.
require 'json'
require 'optparse'
require 'yaml'
# Explicitly, not as a side effect of `yaml`: on 2.3, 2.4 and 3.4 Psych
# happens to pull `date` in and `Date` resolves; on 2.7, 3.0, 3.1 and 3.2 it
# does not, and `PERMITTED = [Date, Time]` raises NameError at require time --
# so without this line the file would die before reading a single advisory on
# four of the seven interpreters this ladder runs it under.
require 'date'
require_relative 'gemgraph'

module CVGate
  module Audit
    # `date:` is a YAML date and `safe_load` refuses it by default.  The
    # database is the verifier's own file and not the candidate's, but it is
    # still parsed under `safe_load`: an advisory file is data, and a probe
    # that ran `unsafe_load` over 1245 of them would be instantiating objects
    # named by a file to answer a question about a lockfile.
    PERMITTED = [Date, Time].freeze

    # `YAML.safe_load`'s allow-list arguments moved.  Psych 3.1 (Ruby 2.6) made
    # them KEYWORDS; before that they were POSITIONAL, and on the old signature
    # the keyword hash simply becomes the `whitelist_classes` argument -- a
    # Hash, whose `include?` is a key lookup, so `Date` is not permitted, every
    # advisory carrying a `date:` raises `Psych::DisallowedClass`, each one is
    # swallowed into `unreadable`, and THE REPORT COMES BACK CLEAN.  Silently
    # clean, on rung 01's Ruby 2.3.8, which is the interpreter `test.sh` runs
    # this file under at step 01.  That is the third instance of this class of
    # defect in this directory -- `Regexp#match?` and `ERB.new(trim_mode:)`
    # are the other two, both recorded in `gates/check.sh`'s header -- and it
    # is why that script runs the whole ladder instead of the newest Ruby.
    #
    # Decided by ASKING rather than by version arithmetic: parse a document
    # that needs `Date` permitted, the new way, once at load, and keep the
    # answer.  A packaging surprise in either direction gives the right call.
    KEYWORD_SAFE_LOAD = begin
      YAML.safe_load("---\nd: 2020-01-01\n", permitted_classes: [Date])
      true
    rescue StandardError
      false
    end

    def self.load_yaml(text, filename = nil)
      if KEYWORD_SAFE_LOAD
        YAML.safe_load(text, permitted_classes: PERMITTED, aliases: true)
      else
        YAML.safe_load(text, PERMITTED, [], true, filename)
      end
    end

    def self.requirement(entry)
      Gem::Requirement.new(*entry.to_s.split(', '))
    rescue ArgumentError, Gem::Requirement::BadRequirementError
      nil
    end

    def self.satisfied?(list, version)
      Array(list).any? do |entry|
        req = requirement(entry)
        req && req.satisfied_by?(version)
      end
    end

    # bundler-audit 0.9.3's `Advisory#vulnerable?`, restated.
    def self.vulnerable?(advisory, version)
      return false if satisfied?(advisory['unaffected_versions'], version)
      return true if Array(advisory['patched_versions']).empty?

      !satisfied?(advisory['patched_versions'], version)
    end

    # The identifier bundler-audit prints, and in its order of preference.
    def self.identifier(advisory)
      return "CVE-#{advisory['cve']}" if advisory['cve']
      return "GHSA-#{advisory['ghsa']}" if advisory['ghsa']
      return "OSVDB-#{advisory['osvdb']}" if advisory['osvdb']

      'UNIDENTIFIED'
    end

    def self.database(path)
      gems_dir = File.join(path, 'gems')
      unless File.directory?(gems_dir)
        return { 'path' => path, 'present' => false, 'gems' => 0,
                 'advisories' => 0, 'commit' => nil,
                 'error' => "no #{gems_dir}" }
      end
      # Counted off the filesystem rather than off what was parsed: the whole
      # point of the count is to be a floor under "the database arrived", and
      # a parse failure must not be able to lower it silently.
      { 'path' => path, 'present' => true,
        # `entries - %w[. ..]` rather than `Dir.children`: the latter arrived in
        # Ruby 2.5 and this file runs under the interpreter each STEP targets,
        # which is 2.3.8 on rung 01.  This line runs only when a database is
        # present, which is step 13's verifier and the selftest, so the twelve
        # lower rungs take the `present => false` branch above and never
        # exercise it.  scan.rb's `rule_lex` and logpii.rb's `report` carry the
        # same note.
        'gems' => (Dir.entries(gems_dir) - %w[. ..])
                  .count { |n| File.directory?(File.join(gems_dir, n)) },
        'advisories' => Dir.glob(File.join(gems_dir, '*', '*.yml')).length,
        'commit' => begin
          File.read(File.join(path, 'COMMIT'), encoding: 'UTF-8').strip
        rescue StandardError
          nil
        end,
        'error' => nil }
    end

    # Only the directories named by the lockfile are parsed -- 153 gems of the
    # database's 470 on this application -- because parsing the other 317 would
    # be 900 YAML files read to answer no question.
    def self.findings(specs, path)
      out = []
      unreadable = []
      specs.each do |name, version|
        dir = File.join(path, 'gems', name)
        next unless File.directory?(dir)

        begin
          locked = Gem::Version.new(version.to_s.split('-').first)
        rescue ArgumentError
          unreadable << "#{name} (#{version})"
          next
        end
        Dir.glob(File.join(dir, '*.yml')).sort.each do |file|
          begin
            advisory = load_yaml(File.read(file, encoding: 'UTF-8'), file)
          rescue StandardError => e
            unreadable << "#{file}: #{e.class}"
            next
          end
          next unless advisory.is_a?(Hash)
          next unless vulnerable?(advisory, locked)

          out << { 'gem' => name, 'version' => version.to_s,
                   'id' => identifier(advisory),
                   'cve' => advisory['cve'], 'ghsa' => advisory['ghsa'],
                   'title' => advisory['title'].to_s.gsub(/\s+/, ' ').strip,
                   'url' => advisory['url'],
                   'patched_versions' => Array(advisory['patched_versions']),
                   'cvss_v3' => advisory['cvss_v3'] }
        end
      end
      [out.sort_by { |f| [f['gem'], f['id']] }, unreadable]
    end

    # A `.bundler-audit.yml` cannot change anything above.  It is read anyway,
    # and reported, so that a tree that tried to answer the question by
    # suppressing it says so in its own evidence.
    def self.suppression(root)
      files = []
      ignored = []
      %w[.bundler-audit.yml .bundler-audit.yaml].each do |name|
        file = File.join(root, name)
        next unless File.file?(file)

        files << name
        begin
          config = load_yaml(File.read(file, encoding: 'UTF-8'), file)
          ignored.concat(Array(config['ignore'])) if config.is_a?(Hash)
        rescue StandardError
          nil
        end
      end
      { 'files' => files, 'ignored_ids' => ignored.map(&:to_s).sort }
    end

    def self.report(lock:, db:, root: nil)
      root = File.expand_path(root || File.dirname(File.expand_path(lock)))
      meta = database(db)
      unless File.file?(lock)
        return { 'lockfile' => lock, 'root' => root, 'db' => meta,
                 'error' => "no lockfile at #{lock}" }
      end

      specs = CVGate::GemGraph.parse(File.read(lock, encoding: 'UTF-8'))
      declared = specs['direct'] || []
      if meta['present']
        found, unreadable = findings(specs['specs'], db)
      else
        found = nil
        unreadable = []
      end

      result = { 'lockfile' => lock, 'root' => root, 'db' => meta,
                 'specs' => specs['specs'].length,
                 'suppression' => suppression(root) }
      # Every key below is omitted when the database could not be read, and
      # that is the whole defence against an empty report being read as a
      # clean one.  `score.py` fails a gate whose path is missing,
      # so a verifier built without the database scores these gates 0 -- which
      # is a broken instrument saying so, rather than four free points.
      return result unless found

      by_gem = Hash.new(0)
      found.each { |f| by_gem[f['gem']] += 1 }
      names = by_gem.keys.sort
      result.merge(
        'vulnerable' => found,
        'by_gem' => by_gem,
        'vulnerable_gems' => names,
        'vulnerable_declared' => (names & declared),
        'vulnerable_transitive' => (names - declared),
        'unreadable' => unreadable,
        'counts' => { 'advisories' => found.length, 'gems' => names.length,
                      'declared' => (names & declared).length,
                      'transitive' => (names - declared).length }
      )
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { lock: '/app/Gemfile.lock', db: '/opt/ruby-advisory-db' }
  OptionParser.new do |opts|
    opts.on('--lock PATH') { |v| options[:lock] = v }
    opts.on('--db PATH') { |v| options[:db] = v }
    opts.on('--root PATH') { |v| options[:root] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
  end.parse!(ARGV)

  payload = JSON.pretty_generate(
    CVGate::Audit.report(lock: options[:lock], db: options[:db],
                         root: options[:root])
  )
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
end
