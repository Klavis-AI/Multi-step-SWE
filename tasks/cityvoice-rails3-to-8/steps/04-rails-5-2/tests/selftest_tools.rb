# Rehearse the three non-AST gates -- the dependency graph, the Sprockets
# backstop and the boot probe -- against known answers.
#
# The construct scanner has its own selftest next door; this one covers the
# gates whose subject is not Ruby source:
#
#   * `gemgraph.rb` reads the bundler's metafile.  Its fixtures are two
#     lockfiles with a known shape -- one before the migration and one after --
#     because a gate that reports "sprockets is gone" is worth nothing unless it
#     reported "sprockets is here" on the tree next door.
#   * `assets.rb` reads Sprockets directives.  Its fixtures come in pairs that
#     differ only in *where* the directive sits, since the whole subtlety of the
#     format is that a directive is a directive only inside the leading comment
#     block.
#   * `bootprobe.rb` asks a booted application about itself.  Its fixtures are
#     two stub applications, one migrated and one not, plus applications that do
#     not boot at all -- run as subprocesses, the way a verifier runs them, so
#     the exit status and the `--out` file are covered too.
#   * `envboot.rb` boots the environments nothing else boots.  Its fixtures are
#     stub applications whose `config/environment.rb` behaves differently
#     depending on the `RAILS_ENV` it is given, because the whole point of the
#     probe is that one environment can fail while the others do not -- and one
#     that kills itself without writing a report, because a child that dies is
#     the outcome a naive reader turns into a crash of the gate.
#   * `depdrive.rb` counts the deprecations a running application raises.  It
#     needs a Rails to do that, which no fixture here can supply, so what is
#     rehearsed is everything around the Rails: the collector really records a
#     `warn` and really calls through to it, the call site really names the
#     application file, the grouping really collapses repeats, and the session
#     really carries a cookie -- that last one because without it every form
#     POST comes back 422 and the application's own `#deliver` deprecation goes
#     unseen.
#   * `audit.rb` resolves a lockfile against a copy of ruby-advisory-db.  Its
#     fixtures are a four-gem database built here, in a tmpdir, rather than the
#     real 1245-advisory one: the questions are "does a two-constraint
#     requirement string parse", "does `unaffected_versions` win", "is a
#     missing database a failure or a free pass", and each of those is a fixture
#     of two or three files.  Whether the same code reproduces bundler-audit
#     0.9.3's own answer on the real database is not a unit test and is not
#     checked here.
#   * `logpii.rb` reads what the application wrote to its own production log.
#     Its fixtures are hand-written log files, because the whole subtlety is
#     positional -- the same telephone number is a different finding inside a
#     `Parameters:` line and outside one -- and because the floor counts
#     (`Started`, `Parameters:`, `Completed`) have to be shown to move
#     independently of the findings, or a candidate who silences the log
#     entirely would score as a candidate who filtered it.
#   * `rspecdep.rb` carries the same collector into the suite's own process.
#     Its fixtures are the sink that writes a line per warning, the guard that
#     keeps it out of the `bundle` process that has not set up a bundle yet,
#     and -- against a stub standing in for `ActiveSupport::Deprecation` -- the
#     whole path from a `warn` to a line on disk.
#
#   ruby -I. selftest_tools.rb

require 'fileutils'
require 'tmpdir'
require 'json'
require 'rbconfig'
require_relative 'cvgate/gemgraph'
require_relative 'cvgate/assets'
require_relative 'cvgate/depdrive'
require_relative 'cvgate/envboot'
require_relative 'cvgate/rspecdep'
require_relative 'cvgate/bootprobe'
require_relative 'cvgate/bundledgems'
require_relative 'cvgate/pkdrift'
require_relative 'cvgate/raketasks'
require_relative 'cvgate/audit'
require_relative 'cvgate/logpii'

$checks = 0
$failures = []
$names = {}
$notes = []

MIN_CHECKS = 309

def check(name)
  raise "duplicate check name: #{name}" if $names[name]

  $names[name] = true
  $checks += 1
  begin
    yield
  rescue StandardError => e
    $failures << "#{name}: raised #{e.class}: #{e.message}\n    #{Array(e.backtrace).first}"
  end
end

def fail!(name, message)
  $failures << "#{name}: #{message}"
end

def want(name, got, expected)
  fail!(name, "want #{expected.inspect}, got #{got.inspect}") unless got == expected
end

# ---------------------------------------------------------------------------
# gemgraph: the resolved dependency graph
# ---------------------------------------------------------------------------

# The shape that matters: a direct dependency pulling in, two levels down, a gem
# the Gemfile never names.  `sass-rails -> sprockets-rails -> sprockets` is the
# real case from this codebase, reduced to its bones.
DIRTY_LOCK = <<~LOCK
  GEM
    remote: https://rubygems.org/
    specs:
      actionpack (4.0.0)
        rack (~> 1.5.2)
      multi_json (1.15.0)
      rack (1.5.5)
      rails (4.0.0)
        actionpack (= 4.0.0)
      sass-rails (4.0.3)
        sprockets-rails (~> 2.0)
      sprockets (2.11.0)
        multi_json (~> 1.0)
      sprockets-rails (2.0.1)
        sprockets (~> 2.8)

  PLATFORMS
    ruby

  DEPENDENCIES
    rails (= 4.0.0)
    sass-rails (~> 4.0.3)

  RUBY VERSION
     ruby 2.1.7p400

  BUNDLED WITH
     1.17.3
LOCK

# The same application afterwards: no sass-rails, no sprockets, and the
# transitive edge that carried sprockets gone with it.
CLEAN_LOCK = <<~LOCK
  GEM
    remote: https://rubygems.org/
    specs:
      actionpack (8.0.1)
        rack (>= 2.2.4)
      propshaft (1.1.0)
        actionpack (>= 7.0.0)
      rack (3.1.8)
      rails (8.0.1)
        actionpack (= 8.0.1)

  PLATFORMS
    ruby

  DEPENDENCIES
    propshaft
    rails (= 8.0.1)

  RUBY VERSION
     ruby 3.4.10p0

  BUNDLED WITH
     2.6.9
LOCK

def lockfile(text)
  Dir.mktmpdir do |dir|
    path = File.join(dir, 'Gemfile.lock')
    File.write(path, text)
    yield CVGate::GemGraph.report(path)
  end
end

# A whole little tree, not just a lockfile: the Ruby is declared in three
# places and `report` reads all three.  `nil` means the file is absent, which
# is a different answer from a file that says something else.
def ruby_tree(lock, version_file: nil, gemfile: nil, root: nil, extra: {})
  Dir.mktmpdir do |dir|
    File.write(File.join(dir, 'Gemfile.lock'), lock)
    File.write(File.join(dir, '.ruby-version'), version_file) if version_file
    File.write(File.join(dir, 'Gemfile'), gemfile) if gemfile
    extra.each { |file, body| File.write(File.join(dir, file), body) }
    yield CVGate::GemGraph.report(File.join(dir, 'Gemfile.lock'), root), dir
  end
end

# The lockfile with its `RUBY VERSION` stanza removed -- which is the state the
# baseline of this task is actually in, and the reason `ruby_declared` is not a
# free point.
NO_RUBY_LOCK = DIRTY_LOCK.sub(/RUBY VERSION\n[^\n]*\n/, '').freeze

check 'gemgraph: three agreeing declarations report the one version' do
  name = 'gemgraph: three agreeing declarations report the one version'
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n", gemfile: "ruby '2.1.7'\n") do |report, _dir|
    want(name,
         [report['ruby_version_file'], report['gemfile_ruby'], report['ruby'],
          report['ruby_declared']],
         ['2.1.7', '2.1.7', '2.1.7p400', '2.1.7'])
  end
end

check "gemgraph: rbenv's ruby- prefix is not a disagreement" do
  ruby_tree(DIRTY_LOCK, version_file: "ruby-2.1.7\n", gemfile: "ruby '2.1.7'\n") do |report, _dir|
    want("gemgraph: rbenv's ruby- prefix is not a disagreement",
         [report['ruby_version_file'], report['ruby_declared']], ['2.1.7', '2.1.7'])
  end
end

check 'gemgraph: a lockfile with no RUBY VERSION agrees on nothing' do
  # The untouched baseline.  Two of the three artifacts declare 2.1.7 and the
  # third says nothing, and "two out of three" is not what the instruction says.
  name = 'gemgraph: a lockfile with no RUBY VERSION agrees on nothing'
  ruby_tree(NO_RUBY_LOCK, version_file: "2.1.7\n", gemfile: "ruby '2.1.7'\n") do |report, _dir|
    want(name, [report['ruby'], report['gemfile_ruby'], report['ruby_declared']],
         [nil, '2.1.7', nil])
  end
end

check 'gemgraph: an absent .ruby-version agrees on nothing' do
  ruby_tree(DIRTY_LOCK, gemfile: "ruby '2.1.7'\n") do |report, _dir|
    want('gemgraph: an absent .ruby-version agrees on nothing',
         [report['ruby_version_file'], report['ruby_declared']], [nil, nil])
  end
end

check 'gemgraph: an empty .ruby-version is not a declaration' do
  ruby_tree(DIRTY_LOCK, version_file: "\n", gemfile: "ruby '2.1.7'\n") do |report, _dir|
    want('gemgraph: an empty .ruby-version is not a declaration',
         [report['ruby_version_file'], report['ruby_declared']], [nil, nil])
  end
end

check 'gemgraph: a Gemfile with no ruby directive agrees on nothing' do
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n",
            gemfile: "source 'https://rubygems.org'\ngem 'rails'\n") do |report, _dir|
    want('gemgraph: a Gemfile with no ruby directive agrees on nothing',
         [report['gemfile_ruby'], report['ruby_declared']], [nil, nil])
  end
end

check 'gemgraph: three declarations that disagree report nothing' do
  # The failure this gate exists for: a candidate who moved one file and not
  # the others.  Every artifact is present and the answer is still nil.
  name = 'gemgraph: three declarations that disagree report nothing'
  ruby_tree(DIRTY_LOCK, version_file: "2.3.8\n", gemfile: "ruby '2.1.7'\n") do |report, _dir|
    want(name, [report['ruby_version_file'], report['gemfile_ruby'],
                report['ruby_declared']], ['2.3.8', '2.1.7', nil])
  end
end

check 'gemgraph: the lockfile patchlevel is not a disagreement' do
  # `ruby 2.1.7p400` is what bundler writes; neither of the other two artifacts
  # ever carries a patchlevel, so comparing them raw would never agree.
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n", gemfile: "ruby '2.1.7'\n") do |report, _dir|
    want('gemgraph: the lockfile patchlevel is not a disagreement',
         report['ruby'].end_with?('p400') && report['ruby_declared'] == '2.1.7', true)
  end
end

check 'gemgraph: `ruby file:` resolves to the file rbenv reads' do
  # Bundler 2.2's spelling.  Deferring to `.ruby-version` is a legitimate way
  # to make the declarations agree, not a way to dodge the requirement.
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n",
            gemfile: "ruby file: '.ruby-version'\n") do |report, _dir|
    want('gemgraph: `ruby file:` resolves to the file rbenv reads',
         [report['gemfile_ruby'], report['ruby_declared']], ['2.1.7', '2.1.7'])
  end
end

check 'gemgraph: `ruby file:` naming a file that is not there declares nothing' do
  name = 'gemgraph: `ruby file:` naming a file that is not there declares nothing'
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n",
            gemfile: "ruby file: '.no-such-version'\n") do |report, _dir|
    want(name, [report['gemfile_ruby'], report['ruby_declared']], [nil, nil])
  end
end

check 'gemgraph: the word ruby in a comment or a string is not a directive' do
  # Ripper and not a regexp.  A grep for /^\s*ruby\b/ reads the first of these.
  name = 'gemgraph: the word ruby in a comment or a string is not a directive'
  gemfile = <<~GEMFILE
    # ruby '9.9.9'
    NOTE = "ruby '8.8.8'"
    gem 'rails', platform: :ruby
    ruby '2.1.7'
  GEMFILE
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n", gemfile: gemfile) do |report, _dir|
    want(name, [report['gemfile_ruby'], report['ruby_declared']], ['2.1.7', '2.1.7'])
  end
end

check 'gemgraph: an interpolated ruby directive resolves to nothing' do
  # A version this gate cannot read is reported as unread, not as the fragment
  # it could see.
  name = 'gemgraph: an interpolated ruby directive resolves to nothing'
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n",
            gemfile: %(V = "1.7"\nruby "2.\#{V}"\n)) do |report, _dir|
    want(name, [report['gemfile_ruby'], report['ruby_declared']], [nil, nil])
  end
end

check 'gemgraph: the root defaults to the lockfile directory' do
  ruby_tree(DIRTY_LOCK, version_file: "2.1.7\n", gemfile: "ruby '2.1.7'\n") do |report, dir|
    want('gemgraph: the root defaults to the lockfile directory',
         report['root'], File.expand_path(dir))
  end
end

check 'gemgraph: an explicit root reads that tree and not the lockfile one' do
  # The two can legitimately differ -- a verifier may hold the lockfile
  # somewhere else -- and a report that silently read the wrong `.ruby-version`
  # would be indistinguishable from one that read the right one.
  name = 'gemgraph: an explicit root reads that tree and not the lockfile one'
  Dir.mktmpdir do |locks|
    Dir.mktmpdir do |tree|
      File.write(File.join(locks, 'Gemfile.lock'), DIRTY_LOCK)
      File.write(File.join(locks, '.ruby-version'), "3.4.10\n")
      File.write(File.join(tree, '.ruby-version'), "2.1.7\n")
      File.write(File.join(tree, 'Gemfile'), "ruby '2.1.7'\n")
      report = CVGate::GemGraph.report(File.join(locks, 'Gemfile.lock'), tree)
      want(name, [report['ruby_version_file'], report['ruby_declared']],
           ['2.1.7', '2.1.7'])
    end
  end
end

NAME = 'gemgraph: every spec and its version is read'.freeze
check NAME do
  graph = CVGate::GemGraph.parse(DIRTY_LOCK)
  want(NAME, graph['specs'].length, 7)
  want(NAME, graph['specs']['sprockets'], '2.11.0')
end

check 'gemgraph: dependency edges drop the version constraint' do
  want('gemgraph: dependency edges drop the version constraint',
       CVGate::GemGraph.parse(DIRTY_LOCK)['edges']['sprockets-rails'], ['sprockets'])
end

check 'gemgraph: DEPENDENCIES becomes the direct set' do
  want('gemgraph: DEPENDENCIES becomes the direct set',
       CVGate::GemGraph.parse(DIRTY_LOCK)['direct'], %w[rails sass-rails])
end

check 'gemgraph: a pinned dependency keeps its name without the bang' do
  text = DIRTY_LOCK.sub("  sass-rails (~> 4.0.3)\n", "  sass-rails!\n")
  want('gemgraph: a pinned dependency keeps its name without the bang',
       CVGate::GemGraph.parse(text)['direct'], %w[rails sass-rails])
end

check 'gemgraph: RUBY VERSION is read' do
  want('gemgraph: RUBY VERSION is read',
       CVGate::GemGraph.parse(DIRTY_LOCK)['ruby'], '2.1.7p400')
end

check 'gemgraph: BUNDLED WITH is read' do
  want('gemgraph: BUNDLED WITH is read',
       CVGate::GemGraph.parse(DIRTY_LOCK)['bundled_with'], '1.17.3')
end

check 'gemgraph: the remote source is read' do
  want('gemgraph: the remote source is read',
       CVGate::GemGraph.parse(DIRTY_LOCK)['sources'], ['https://rubygems.org/'])
end

check 'gemgraph: a platform-suffixed version is not truncated' do
  text = DIRTY_LOCK.sub('rack (1.5.5)', 'rack (1.5.5-x86_64-linux)')
  want('gemgraph: a platform-suffixed version is not truncated',
       CVGate::GemGraph.parse(text)['specs']['rack'], '1.5.5-x86_64-linux')
end

GIT_SECTION = <<~SECTION
  GIT
    remote: https://github.com/example/widget.git
    revision: 0123456789abcdef0123456789abcdef01234567
    specs:
      widget (0.1.0)
        rack
SECTION

check 'gemgraph: a GIT section contributes specs like a GEM section' do
  graph = CVGate::GemGraph.parse(DIRTY_LOCK + "\n" + GIT_SECTION)
  want('gemgraph: a GIT section contributes specs like a GEM section',
       [graph['specs']['widget'], graph['edges']['widget']], ['0.1.0', ['rack']])
end

check 'gemgraph: reachability records the path, not just the name' do
  reach = CVGate::GemGraph.reachable(CVGate::GemGraph.parse(DIRTY_LOCK))
  want('gemgraph: reachability records the path, not just the name',
       reach['sprockets'], %w[sass-rails sprockets-rails sprockets])
end

check 'gemgraph: the unmigrated lockfile reaches sprockets' do
  lockfile(DIRTY_LOCK) do |report|
    want('gemgraph: the unmigrated lockfile reaches sprockets',
         report['reachable'].include?('sprockets'), true)
  end
end

# The paired failure fixture: the same gate and the same command on a tree where
# the thing really has been removed.  The number has to move.
check 'gemgraph: the migrated lockfile reaches no sprockets' do
  lockfile(CLEAN_LOCK) do |report|
    want('gemgraph: the migrated lockfile reaches no sprockets',
         [report['reachable'].include?('sprockets'),
          report['reachable'].include?('sass-rails'),
          report['reachable'].include?('propshaft')],
         [false, false, true])
  end
end

check 'gemgraph: an unreferenced spec is reported as an orphan' do
  text = DIRTY_LOCK.sub("    multi_json (1.15.0)\n",
                        "    leftover (9.9.9)\n    multi_json (1.15.0)\n")
  lockfile(text) do |report|
    want('gemgraph: an unreferenced spec is reported as an orphan',
         report['orphans'], ['leftover'])
  end
end

check 'gemgraph: a dependency with no spec is reported as unresolved' do
  text = DIRTY_LOCK.sub("    rack (1.5.5)\n", '')
  lockfile(text) do |report|
    want('gemgraph: a dependency with no spec is reported as unresolved',
         report['unresolved'], ['rack'])
  end
end

check 'gemgraph: a consistent lockfile has neither orphans nor unresolved' do
  lockfile(CLEAN_LOCK) do |report|
    want('gemgraph: a consistent lockfile has neither orphans nor unresolved',
         [report['orphans'], report['unresolved']], [[], []])
  end
end

# Bundler is a default gem only from Ruby 2.6.  On 2.3 and 2.4 the cross-check
# is simply unavailable unless the gem is installed -- which it is in a verifier
# image, since the application cannot be run without it, but is not in a bare
# interpreter image.  That is reported as a NOTE and not as a pass: a check that
# quietly does nothing on two of the seven Rubies is the shape of instrument
# failure this file exists to avoid, and the note makes it visible in the
# ladder's output.
BUNDLER_ABSENT = /LoadError: cannot load such file -- bundler/.freeze

check "gemgraph: bundler's own parser agrees on an old lockfile" do
  name = "gemgraph: bundler's own parser agrees on an old lockfile"
  lockfile(DIRTY_LOCK) do |report|
    error = report['bundler_error']
    if error.nil?
      want(name, report['bundler_agrees'], true)
    elsif BUNDLER_ABSENT =~ error
      $notes << "#{name}: NOT EXERCISED (bundler is not installed on ruby #{RUBY_VERSION})"
    else
      fail!(name, "bundler could not read BUNDLED WITH 1.17.3: #{error}")
    end
  end
end

# The newer `BUNDLED WITH` is the case the hand-written parser exists for: an
# old bundler may refuse the file outright.  Either it agrees or it could not
# look.  It must never disagree.
check 'gemgraph: bundler never disagrees on a newer lockfile' do
  lockfile(CLEAN_LOCK) do |report|
    if report['bundler_agrees'] == false
      fail!('gemgraph: bundler never disagrees on a newer lockfile',
            "hand parser says #{report['specs'].inspect}")
    end
  end
end

# ---------------------------------------------------------------------------
# assets: the Sprockets directive backstop
# ---------------------------------------------------------------------------

def assets(files)
  Dir.mktmpdir do |dir|
    files.each do |name, body|
      path = File.join(dir, name)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, body)
    end
    yield CVGate::Assets.scan(dir)
  end
end

def expect_directives(name, files, expected)
  check(name) do
    assets(files) do |report|
      got = report['total']
      if got != expected
        fail!(name, "want #{expected} directives, got #{got}: " \
                    "#{report['findings'].map { |f| "#{f['file']}:#{f['line']}" }.join(' ')}")
      end
    end
  end
end

MANIFEST_JS = <<~JS
  //= require jquery
  //= require jquery_ujs
  //= require_tree .
  $(function () {});
JS

MANIFEST_CSS = <<~CSS
  /*
   *= require_self
   *= require leaflet
   */
  body { color: red; }
CSS

MANIFEST_COFFEE = <<~COFFEE
  #= require_tree ./models
  x = 1
COFFEE

# The boundary rule, in its two halves.  Sprockets stops reading at the first
# line that is neither blank nor a comment, so the same eighteen characters are
# a build instruction in one file and dead text in the other.
LATE_JS = "var x = 1;\n//= require jquery\n".freeze
EARLY_JS = "//= require jquery\nvar x = 1;\n".freeze
SPACED_JS = "//= require jquery\n\n//= require_tree .\nvar x = 1;\n".freeze

expect_directives 'assets: a leading directive block is counted line by line',
                  { 'app/assets/javascripts/application.js' => MANIFEST_JS }, 3

expect_directives 'assets: the CSS comment form is counted',
                  { 'app/assets/stylesheets/application.css.scss' => MANIFEST_CSS }, 2

expect_directives 'assets: the hash form is counted',
                  { 'app/assets/javascripts/models.js.coffee' => MANIFEST_COFFEE }, 1

expect_directives 'assets: a directive after code is not a directive',
                  { 'app/assets/javascripts/late.js' => LATE_JS }, 0

expect_directives 'assets: the same directive before the code is a directive',
                  { 'app/assets/javascripts/early.js' => EARLY_JS }, 1

expect_directives 'assets: a blank line does not end the leading block',
                  { 'app/assets/javascripts/spaced.js' => SPACED_JS }, 2

expect_directives 'assets: a file with no directives scores zero',
                  { 'app/assets/javascripts/plain.js' => "var x = 1;\n" }, 0

expect_directives 'assets: vendor/assets is scanned too',
                  { 'vendor/assets/javascripts/vendored.js' => "//= require foo\n" }, 1

expect_directives 'assets: a file outside the asset roots is not scanned',
                  { 'app/models/thing.js' => "//= require foo\n" }, 0

expect_directives 'assets: a non-asset suffix is not scanned',
                  { 'app/assets/images/logo.svg' => "//= require foo\n" }, 0

check 'assets: the reported line is the line in the file' do
  assets('app/assets/stylesheets/application.css' => MANIFEST_CSS) do |report|
    want('assets: the reported line is the line in the file',
         report['findings'].map { |f| [f['file'], f['line']] },
         [['app/assets/stylesheets/application.css', 2],
          ['app/assets/stylesheets/application.css', 3]])
  end
end

check 'assets: the scanned-file count is reported' do
  assets('app/assets/javascripts/a.js' => '',
         'app/assets/javascripts/b.js' => '',
         'app/assets/images/c.png' => '') do |report|
    want('assets: the scanned-file count is reported', report['assets_scanned'], 2)
  end
end

# ---------------------------------------------------------------------------
# bootprobe: the booted application's account of itself
# ---------------------------------------------------------------------------

GATES = File.expand_path(File.dirname(__FILE__))
PROBE = File.join(GATES, 'cvgate', 'bootprobe.rb')

# A stub `config/environment.rb` standing in for a booted Rails: only the
# surface the probe touches, with the two stubs differing in exactly the answers
# a migration is supposed to change.  `MIGRATED` is a plain constant so that the
# whole template is one uninterpolated heredoc: interpolated method bodies risk
# a stray `#{}` in a squiggly heredoc, which quietly changes the dedent of
# everything below it.
#
# `secrets:` selects which of the three arrangements of the signing key the
# stub presents, because telling them apart is the whole job of the `secrets`
# probe:
#
#   :none     no `Rails.application.secrets` reader at all -- Rails 4.0
#   :empty    the reader exists but `config/secrets.yml` did not supply a key,
#             so an initializer is still doing it -- an unmigrated 4.2
#   :present  the key comes from `config/secrets.yml` -- the migrated shape
#
# The middle one is the one a careless probe gets wrong: the application boots,
# it has a working `secret_key_base`, and only `secrets` is empty.
def stub_environment(migrated, secrets: (migrated ? :present : :none),
                     secrets_path: nil)
  <<~RUBY
    MIGRATED = #{migrated ? 'true' : 'false'}
    SECRETS = #{secrets.inspect}
    SECRETS_PATH = #{secrets_path.inspect}
    ROOT = File.expand_path(File.join(File.dirname(__FILE__), '..'))

    module Rails
      module VERSION
        STRING = MIGRATED ? '8.0.1' : '4.0.0'
      end

      Loader = Struct.new(:tag)

      class ActiveSupportConfig
        def deprecation; MIGRATED ? :raise : :log; end
      end

      class ActiveRecordConfig
        def schema_format; :ruby; end
      end

      class Config
        def loaded_config_version; MIGRATED ? '8.0' : nil; end
        def autoloader; MIGRATED ? :zeitwerk : :classic; end
        def eager_load_paths; [File.join(ROOT, 'app/models')]; end

        # The autoload-only paths, read from a file the fixture may write, so
        # that a tree with one and a tree without one are the same stub with a
        # different `extra:` and not two stubs.  Real Rails always answers all
        # three; a stub that answered only `eager_load_paths` would let the
        # probe raise and report nothing.
        def autoload_paths
          file = File.join(ROOT, 'config', 'autoload_paths.txt')
          return [] unless File.exist?(file)

          File.readlines(file).map(&:strip).reject(&:empty?)
              .map { |rel| File.join(ROOT, rel) }
        end

        def autoload_once_paths; []; end
        def active_support; ActiveSupportConfig.new; end
        def active_record; ActiveRecordConfig.new; end
        # Set whatever the arrangement, because every one of the three boots.
        def secret_key_base; 'k' * 128; end
      end

      class Application
        def config; @config ||= Config.new; end
        def eager_load!; @eager_loaded = true; end
      end

      unless SECRETS == :none
        Application.send(:define_method, :secrets) do
          { secret_key_base: (SECRETS == :present ? ('s' * 64) : nil) }
        end
      end

      # From 4.1 the framework knows where `config/secrets.yml` is and will
      # say so, including when the application moved it.  A stub that never
      # answers would leave the probe's fallback as the only path ever tested.
      unless SECRETS_PATH.nil?
        PathEntry = Struct.new(:expanded)
        Application.send(:define_method, :paths) do
          { 'config/secrets' => PathEntry.new([File.join(ROOT, SECRETS_PATH)]) }
        end
      end

      class Autoloaders
        include Enumerable
        def zeitwerk_enabled?; MIGRATED; end
        def main; @main ||= Loader.new('rails.main'); end
        def each; yield main; end
      end

      def self.application; @application ||= Application.new; end
      def self.env; 'production'; end
      def self.autoloaders; @autoloaders ||= Autoloaders.new; end
    end

    # An unmigrated application still carries `config.assets`; a migrated one
    # must not respond to it at all, which is what the probe actually asks.
    unless MIGRATED
      Rails::Config.send(:define_method, :assets) do
        # sprockets-rails 3 installs `unknown_asset_fallback` on this object;
        # 2.x does not, and Propshaft has no such setting either.  Which of the
        # two this fixture is, is decided by a file the fixture may write, the
        # same way `autoload_paths` above is -- a probe that assumed the
        # setting was always there would raise NoMethodError on the older
        # pipeline and take every other asset reading down with it.
        file = File.join(ROOT, 'config', 'assets_fallback.txt')
        klass = Struct.new(:version) do
          if File.exist?(file)
            answer = File.read(file).strip == 'true'
            define_method(:unknown_asset_fallback) { answer }
          end
        end
        klass.new('1.0')
      end
    end
  RUBY
end

def run_probe(app_dir)
  out = File.join(app_dir, 'boot.json')
  ok = system(RbConfig.ruby, PROBE, '--app', app_dir, '--out', out,
              out: File::NULL, err: File::NULL)
  [ok, (JSON.parse(File.read(out)) if File.exist?(out))]
end

def with_app(migrated, extra = {}, secrets: nil, secrets_path: nil)
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'config', 'initializers'))
    FileUtils.mkdir_p(File.join(dir, 'app', 'models'))
    # Spelled out rather than built with `**`: an empty double-splat is not the
    # same call on every Ruby in the ladder.
    body = if secrets && secrets_path
             stub_environment(migrated, secrets: secrets, secrets_path: secrets_path)
           elsif secrets
             stub_environment(migrated, secrets: secrets)
           elsif secrets_path
             stub_environment(migrated, secrets_path: secrets_path)
           else
             stub_environment(migrated)
           end
    File.write(File.join(dir, 'config', 'environment.rb'), body)
    extra.each do |name, content|
      path = File.join(dir, name)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, content)
    end
    yield(*run_probe(dir))
  end
end

def with_broken_app(body)
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'config'))
    File.write(File.join(dir, 'config', 'environment.rb'), body)
    yield(*run_probe(dir))
  end
end

check 'bootprobe: a directory with no environment.rb reports why, and exits 0' do
  name = 'bootprobe: a directory with no environment.rb reports why, and exits 0'
  Dir.mktmpdir do |dir|
    ok, report = run_probe(dir)
    want(name, [ok, report['booted']], [true, false])
    unless report['boot_error'].to_s.include?('no config/environment.rb')
      fail!(name, report['boot_error'].inspect)
    end
  end
end

check 'bootprobe: an application that raises on boot is reported, not lost' do
  name = 'bootprobe: an application that raises on boot is reported, not lost'
  with_broken_app("raise NameError, 'uninitialized constant Widget'\n") do |ok, report|
    want(name, [ok, report['booted']], [true, false])
    unless report['boot_error'].to_s.include?('uninitialized constant Widget')
      fail!(name, report['boot_error'].inspect)
    end
    fail!(name, 'no backtrace recorded') if Array(report['boot_backtrace']).empty?
  end
end

check 'bootprobe: the exit status is 0 even when the application calls exit' do
  name = 'bootprobe: the exit status is 0 even when the application calls exit'
  with_broken_app("exit 3\n") do |ok, report|
    want(name, [ok, report && report['booted']], [true, false])
  end
end

check 'bootprobe: a booting application reports its Rails version' do
  name = 'bootprobe: a booting application reports its Rails version'
  with_app(true) do |_ok, report|
    want(name, [report['booted'], report['probes']['versions']['rails']],
         [true, '8.0.1'])
  end
end

check 'bootprobe: load_defaults is read from the application, not the source' do
  with_app(true) do |_ok, report|
    want('bootprobe: load_defaults is read from the application, not the source',
         report['probes']['load_defaults'],
         'loaded_config_version' => '8.0', 'reader_exists' => true)
  end
end

check 'bootprobe: an unmigrated application reports no loaded_config_version' do
  with_app(false) do |_ok, report|
    want('bootprobe: an unmigrated application reports no loaded_config_version',
         report['probes']['load_defaults'],
         'loaded_config_version' => nil, 'reader_exists' => true)
  end
end

check 'bootprobe: the autoloader is named by the application' do
  with_app(true) do |_ok, report|
    probe = report['probes']['autoloader']
    want('bootprobe: the autoloader is named by the application',
         [probe['config'], probe['zeitwerk_enabled'], probe['loaders']],
         ['zeitwerk', true, ['rails.main']])
  end
end

check 'bootprobe: a classic autoloader is reported as such' do
  with_app(false) do |_ok, report|
    probe = report['probes']['autoloader']
    want('bootprobe: a classic autoloader is reported as such',
         [probe['config'], probe['zeitwerk_enabled']], ['classic', false])
  end
end

check 'bootprobe: eager_load runs and reports its paths' do
  with_app(true) do |_ok, report|
    probe = report['probes']['eager_load']
    want('bootprobe: eager_load runs and reports its paths',
         [probe['ok'], probe['paths'], probe['autoload_only']],
         [true, ['app/models'], []])
  end
end

# The reading the gate actually scores.  `eager_load!` returns on a tree that
# left a directory autoloaded and not eager-loaded, and from Rails 5.0 that
# directory's constants are undefined in a booted production process -- so the
# difference between these two fixtures is the difference between an
# application whose `lib/` works in production and one whose `lib/` works only
# in development.  Both directions, because a reading that can only report
# trouble is as useless as one that can never report it.
check 'bootprobe: a path that is autoloaded and not eager-loaded is named' do
  with_app(true, { 'config/autoload_paths.txt' => "lib\n" }) do |_ok, report|
    probe = report['probes']['eager_load']
    want('bootprobe: a path that is autoloaded and not eager-loaded is named',
         [probe['ok'], probe['autoload_only']], [true, ['lib']])
  end
end

check 'bootprobe: a path that is on both lists is not autoload-only' do
  with_app(true, { 'config/autoload_paths.txt' => "app/models\n" }) do |_ok, report|
    probe = report['probes']['eager_load']
    want('bootprobe: a path that is on both lists is not autoload-only',
         [probe['ok'], probe['autoload_only']], [true, []])
  end
end

check 'bootprobe: the asset pipeline is read off the booted config' do
  with_app(true) do |_ok, report|
    probe = report['probes']['asset_pipeline']
    want('bootprobe: the asset pipeline is read off the booted config',
         [probe['config_assets'], probe['sprockets_loaded'], probe['propshaft_loaded']],
         [false, false, false])
  end
end

check 'bootprobe: an application still holding config.assets says so' do
  with_app(false) do |_ok, report|
    want('bootprobe: an application still holding config.assets says so',
         report['probes']['asset_pipeline']['config_assets'], true)
  end
end

# -- where bootsnap was set up, and what it wrote --
#
# The stub application never loads the gem, so `setup_called` is false in every
# one of these and `setup_from_boot_file` -- the reading step 04 gates on -- is
# false with it.  That is the point: what is rehearsed here is the other half,
# the one that reads `config/boot.rb` as text, on every Ruby in the ladder.
# Two mistakes in that reading are quiet: `String#match?`, which is 2.4 and so
# invisible on 3.4, and a `\b` written inside a double-quoted heredoc, which
# reaches the regexp as a backspace byte and matches nothing anywhere -- hidden
# behind the `require "bootsnap/setup"` alternative that the golden tree
# happens to use.
def bootsnap_probe(files)
  with_app(true, files) { |_ok, report| return report['probes']['bootsnap'] }
end

check 'bootprobe: the boot file that requires bootsnap/setup is recognised' do
  probe = bootsnap_probe('config/boot.rb' => <<~RUBY)
    ENV['BUNDLE_GEMFILE'] ||= File.expand_path('../Gemfile', __dir__)
    require 'bundler/setup'
    require 'bootsnap/setup'
  RUBY
  want('bootprobe: the boot file that requires bootsnap/setup is recognised',
       [probe['boot_file_setup'], probe['setup_called'], probe['setup_from_boot_file']],
       [true, false, false])
end

check 'bootprobe: the other spelling, a bare Bootsnap.setup call, is recognised' do
  probe = bootsnap_probe('config/boot.rb' => <<~RUBY)
    require 'bundler/setup'
    require 'bootsnap'
    Bootsnap.setup(cache_dir: 'tmp/cache', development_mode: false)
  RUBY
  want('bootprobe: the other spelling, a bare Bootsnap.setup call, is recognised',
       probe['boot_file_setup'], true)
end

check 'bootprobe: requiring the gem without setting it up is not setup' do
  probe = bootsnap_probe('config/boot.rb' => <<~RUBY)
    require 'bundler/setup'
    require 'bootsnap'
    Bootsnap.setup_later_maybe
  RUBY
  want('bootprobe: requiring the gem without setting it up is not setup',
       probe['boot_file_setup'], false)
end

check 'bootprobe: no boot file at all is not setup' do
  want('bootprobe: no boot file at all is not setup',
       bootsnap_probe({})['boot_file_setup'], false)
end

check 'bootprobe: the store is only written when bootsnap says where it is' do
  probe = bootsnap_probe('tmp/cache/bootsnap-load-path-cache' => 'xxxxx',
                         'tmp/cache/bootsnap-compile-cache/ab/cdef' => 'yy')
  want('bootprobe: the store is only written when bootsnap says where it is',
       [probe['store_path'], probe['store_written'], probe['store_bytes'],
        probe['cache_files'], probe['cache_bytes']],
       [nil, false, 0, 2, 7])
end

# `store_in_tree` is the reading step 04's `bootsnap_caches` gate scores, and
# the stub application above cannot produce its interesting cases: it never
# loads the gem, so `store_path` is nil on every tree built here and
# `store_written` is false with it.  The verdict is therefore rehearsed as the
# pure function it is written as.  The positive case is the step-04 golden's
# own reading: it reports exactly `tmp/cache/bootsnap-load-path-cache` with
# `store_written` true.
check 'bootprobe: a written store under tmp/ is in the tree' do
  want('bootprobe: a written store under tmp/ is in the tree',
       CVGate::BootProbe.store_in_tree?('tmp/cache/bootsnap-load-path-cache', true),
       true)
end

check 'bootprobe: a written store outside the tree is not in the tree' do
  # `relative` hands back a path it cannot root unchanged, so this is the shape
  # `Bootsnap.setup(cache_dir: ENV['HOME'] + '/bs')` actually reports.
  want('bootprobe: a written store outside the tree is not in the tree',
       [CVGate::BootProbe.store_in_tree?('/root/bs/bootsnap-load-path-cache', true),
        CVGate::BootProbe.store_in_tree?('var/bootsnap-load-path-cache', true),
        CVGate::BootProbe.store_in_tree?('tmpfoo/bootsnap-load-path-cache', true)],
       [false, false, false])
end

check 'bootprobe: a path under tmp/ with nothing written is not a cache' do
  want('bootprobe: a path under tmp/ with nothing written is not a cache',
       [CVGate::BootProbe.store_in_tree?('tmp/cache/bootsnap-load-path-cache', false),
        CVGate::BootProbe.store_in_tree?(nil, false),
        CVGate::BootProbe.store_in_tree?(nil, true)],
       [false, false, false])
end

# ---------------------------------------------------------------------------
# The tree-wide plaintext-key reading.
#
# Rehearsed against trees built here rather than against the application,
# because the interesting cases are the ones the application does not contain:
# the renamed file, the copy under `spec/`, the prose that mentions the setting
# without carrying a key.  The readings against the real trees are: gold 03
# `[]`, gold 02 `["config/secrets.yml"]`, the 3.2 pristine tree
# `["config/initializers/secret_token.rb"]`.
# ---------------------------------------------------------------------------

KEY64 = 'a1b2c3d4' * 8         # what `rake secret` produces
KEY32 = 'f0e1d2c3' * 4         # what `config/secrets.yml.key` holds

def plaintext_files(files)
  Dir.mktmpdir do |dir|
    files.each do |name, content|
      path = File.join(dir, name)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, content)
    end
    CVGate::BootProbe.new(dir).send(:plaintext_secret_files)
  end
end

check 'bootprobe: the two files that are meant to be there are not findings' do
  name = 'bootprobe: the two files that are meant to be there are not findings'
  want(name,
       plaintext_files('config/secrets.yml.enc' => "\x01\x02encrypted\x03",
                       'config/secrets.yml.key' => "#{KEY32}\n",
                       'config/initializers/note.rb' =>
                         "# secret_key_base now comes from the encrypted file\n"),
       [])
end

check 'bootprobe: the plaintext file is named when it is still there' do
  name = 'bootprobe: the plaintext file is named when it is still there'
  want(name,
       plaintext_files('config/secrets.yml' => "production:\n  secret_key_base: #{KEY64}\n",
                       'config/secrets.yml.enc' => 'x'),
       ['config/secrets.yml'])
end

check 'bootprobe: renaming the plaintext file does not remove it' do
  # The cheat this reading exists for: `git mv config/secrets.yml
  # config/secrets.yml.bak` satisfies "there is no config/secrets.yml".
  name = 'bootprobe: renaming the plaintext file does not remove it'
  want(name,
       plaintext_files('config/secrets.yml.bak' => "production:\n  secret_key_base: #{KEY64}\n",
                       'config/secrets.yml.key' => KEY32),
       ['config/secrets.yml.bak'])
end

check 'bootprobe: a copy anywhere else in the tree is found by its contents' do
  name = 'bootprobe: a copy anywhere else in the tree is found by its contents'
  want(name,
       plaintext_files('spec/support/legacy_secrets.yml' =>
                         "test:\n  secret_key_base: #{KEY64}\n",
                       'config/initializers/secret_token.rb' =>
                         "Rails.application.config.secret_key_base = '#{KEY64}'\n"),
       ['config/initializers/secret_token.rb', 'spec/support/legacy_secrets.yml'])
end

check 'bootprobe: a long hex run that is not a key is not a finding' do
  # A digest in a lockfile, a fixture checksum, a sprockets asset name.  The
  # reading needs both halves or it reports half the repository.
  name = 'bootprobe: a long hex run that is not a key is not a finding'
  want(name,
       plaintext_files('spec/fixtures/digest.txt' => "#{KEY64}\n",
                       'config/initializers/note.rb' =>
                         "# do not put secret_key_base in here\n"),
       [])
end

check 'bootprobe: build products and history are not part of the tree' do
  # `tmp/cache` holds compiled templates and `.git` holds every version of
  # every file, including the one that was just deleted.  Neither is what the
  # application ships, and reading them would make this gate unpassable.
  name = 'bootprobe: build products and history are not part of the tree'
  want(name,
       plaintext_files('tmp/cache/secrets.yml' => "secret_key_base: #{KEY64}\n",
                       '.git/ORIG_HEAD' => "secret_key_base: #{KEY64}\n",
                       'log/development.log' => "secret_key_base: #{KEY64}\n"),
       [])
end

# -- the superseded secret store ----------------------------------------------
#
# `plaintext_secret_files` above answers "is a signing key lying around in the
# clear".  This one answers a different question -- "is Rails 5.1's encrypted
# store still in the tree" -- and the pair below is the reason both are needed:
# `config/secrets.yml.enc` and `config/secrets.yml.key` are *allowed* by the
# first reading and are exactly what the second is looking for.

def legacy_store_files(files)
  Dir.mktmpdir do |dir|
    files.each do |name, content|
      path = File.join(dir, name)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, content)
    end
    CVGate::BootProbe.new(dir).send(:legacy_secret_store_files)
  end
end

check 'bootprobe: a tree on credentials carries no legacy store' do
  name = 'bootprobe: a tree on credentials carries no legacy store'
  want(name,
       legacy_store_files('config/credentials.yml.enc' => "\x01\x02encrypted\x03",
                          'config/master.key' => "#{KEY32}\n"),
       [])
end

check 'bootprobe: the encrypted store left behind is named' do
  name = 'bootprobe: the encrypted store left behind is named'
  want(name,
       legacy_store_files('config/credentials.yml.enc' => 'x',
                          'config/master.key' => KEY32,
                          'config/secrets.yml.enc' => "\x01\x02encrypted\x03",
                          'config/secrets.yml.key' => "#{KEY32}\n"),
       ['config/secrets.yml.enc', 'config/secrets.yml.key'])
end

check 'bootprobe: the plaintext store is a legacy store too' do
  name = 'bootprobe: the plaintext store is a legacy store too'
  want(name, legacy_store_files('config/secrets.yml' => "production:\n"),
       ['config/secrets.yml'])
end

check 'bootprobe: moving the store out of config does not remove it' do
  # The cheat: `git mv config/secrets.yml.enc config/legacy/` satisfies "there
  # is no config/secrets.yml.enc" while shipping the file.
  name = 'bootprobe: moving the store out of config does not remove it'
  want(name, legacy_store_files('config/legacy/secrets.yml.enc' => 'x'),
       ['config/legacy/secrets.yml.enc'])
end

check 'bootprobe: a file that merely mentions the old store is not the store' do
  name = 'bootprobe: a file that merely mentions the old store is not the store'
  want(name,
       legacy_store_files('config/initializers/note.rb' =>
                            "# config/secrets.yml.enc was removed in this step\n",
                          'db/seeds.rb' => "# secrets.yml is gone\n"),
       [])
end

check 'bootprobe: history and build products are not the tree here either' do
  name = 'bootprobe: history and build products are not the tree here either'
  want(name,
       legacy_store_files('.git/ORIG_HEAD' => 'config/secrets.yml.enc',
                          'tmp/cache/secrets.yml.enc' => 'x',
                          'log/secrets.yml' => 'x'),
       [])
end

# ---------------------------------------------------------------------------
# The primary-key width classification.
#
# `probes.database` needs a PostgreSQL to answer at all.  What follows replays
# the readings three trees booted against real databases recorded, plus the
# spellings they happen not to contain, against the classification itself -- so that the part of the reading that can change
# without a database is rehearsed on every Ruby in the ladder rather than once,
# by hand, on one.
# ---------------------------------------------------------------------------

def want_widths(name, default_key, id_types, expected)
  got = CVGate::BootProbe.primary_key_widths(default_key, id_types)
  want(name, expected.keys.map { |k| got[k] }, expected.values)
end

check 'bootprobe: 5.1 with a regenerated dump is the pinned reading' do
  # A regenerated dump: bigserial default, seven integer keys -- two of
  # them here, since the classification does not depend on how many there are.
  name = 'bootprobe: 5.1 with a regenerated dump is the pinned reading'
  want_widths(name, 'bigserial primary key',
              { 'locations' => 'integer', 'voice_responses' => 'integer' },
              'wide_default_primary_key' => true, 'wide_id_tables' => [],
              'odd_id_tables' => [],
              'narrow_id_tables' => %w[locations voice_responses],
              'pk_pinned' => true)
end

check 'bootprobe: 5.1 with a stale dump names the tables that went wide' do
  # The same tree, `db/schema.rb` left as 5.0 wrote it,
  # so `db:schema:load` builds every key at the 5.1 default width.
  name = 'bootprobe: 5.1 with a stale dump names the tables that went wide'
  want_widths(name, 'bigserial primary key',
              { 'locations' => 'bigint', 'voice_responses' => 'bigint' },
              'wide_id_tables' => %w[locations voice_responses],
              'narrow_id_tables' => [], 'pk_pinned' => false)
end

check 'bootprobe: the predecessor does not pass this for free' do
  # The Rails 5.0 tree: 5.0's adapter defaults to `serial primary key`,
  # so its keys are narrow without anyone having done anything -- which is the
  # whole reason the reading is conjunctive.
  name = 'bootprobe: the predecessor does not pass this for free'
  want_widths(name, 'serial primary key',
              { 'locations' => 'integer', 'voice_responses' => 'integer' },
              'wide_default_primary_key' => false, 'wide_id_tables' => [],
              'pk_pinned' => false)
end

check 'bootprobe: the adapter\'s other narrow spellings are still narrow' do
  # Not in any recorded fixture, and that is the point: `int4` and `serial`
  # name the same 4-byte key, and a classification that only knew `integer`
  # would report this tree as having gone wide.
  name = 'bootprobe: the adapter\'s other narrow spellings are still narrow'
  want_widths(name, 'bigserial primary key',
              { 'a' => 'int4', 'b' => 'serial', 'c' => 'integer' },
              'narrow_id_tables' => %w[a b c], 'wide_id_tables' => [],
              'odd_id_tables' => [], 'pk_pinned' => true)
end

check 'bootprobe: a width this probe does not know is a finding, not a pass' do
  name = 'bootprobe: a width this probe does not know is a finding, not a pass'
  want_widths(name, 'bigserial primary key',
              { 'a' => 'integer', 'b' => 'uuid' },
              'narrow_id_tables' => ['a'], 'wide_id_tables' => [],
              'odd_id_tables' => ['b'], 'pk_pinned' => false)
end

check 'bootprobe: a database with no tables is not pinned' do
  # An empty `id_sql_types` would satisfy "no table is wide" vacuously, and a
  # tree whose database failed to load its schema would score as migrated.
  name = 'bootprobe: a database with no tables is not pinned'
  want_widths(name, 'bigserial primary key', {},
              'pk_pinned' => false, 'wide_default_primary_key' => true)
end

# ---------------------------------------------------------------------------
# The agreement between the two databases.
#
# `pkdrift.rb` compares what `db:schema:load` built with what `db:migrate`
# built.  Every fixture here is a pair, because the failure the comparison
# exists for is a pair that disagrees -- and a comparison that has only ever
# been run against pairs that agree would answer "pinned" on the tree it was
# written for and "pinned" on the tree it was written against.  Those two look
# identical in the reward.
#
# The classification underneath is `BootProbe`'s and is rehearsed above; what
# is rehearsed here is the conjunction, the five ways of failing it, and the
# one difference between the two sides that is deliberately *not* a failure --
# the catalogue spelling a 32-bit key `serial` on one side and `integer` on the
# other.  That last case is the reason the comparison is by width: comparing
# `sql_type` strings would fail a tree that had done everything this step asks.
# ---------------------------------------------------------------------------

NARROW7 = { 'answers' => 'integer', 'callers' => 'integer', 'calls' => 'integer',
            'location_subscriptions' => 'integer', 'locations' => 'integer',
            'questions' => 'integer', 'subscribers' => 'integer' }.freeze

def want_drift(name, dump, migrated, expected,
               default_key: 'bigserial primary key', load_exit: 0, migrate_exit: 0)
  got = CVGate::PkDrift.compare(dump, migrated, default_key, load_exit, migrate_exit)
  want(name, expected.keys.map { |k| got[k] }, expected.values)
end

check 'pkdrift: two constructions agreeing at the narrow key is the pinned reading' do
  # The golden's own readings, both sides, taken from the step-03 calibration.
  name = 'pkdrift: two constructions agreeing at the narrow key is the pinned reading'
  want_drift(name, NARROW7, NARROW7,
             { 'pinned' => true, 'agree' => true, 'both_narrow' => true,
               'differing_tables' => {}, 'differing_spellings' => {},
               'schema_load_exit' => 0, 'problems' => [] })
end

check 'pkdrift: the same width spelled two ways is not a disagreement' do
  # PostgreSQL's catalogue has called a 4-byte key `integer`, `int4` and
  # `serial` across the adapters this ladder spans, and which one comes back
  # depends on how the column was built, not on how wide it is.  A comparison
  # by string would report the tree that did exactly what the step asks.
  name = 'pkdrift: the same width spelled two ways is not a disagreement'
  dump = NARROW7.merge('calls' => 'serial', 'locations' => 'int4')
  got = CVGate::PkDrift.compare(dump, NARROW7, 'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'], got['both_narrow'],
        got['differing_tables'], got['differing_spellings'].keys.sort,
        got['differing_spellings']['calls'], got['problems']],
       [true, true, true, {}, %w[calls locations],
        { 'dump' => 'serial', 'migrated' => 'integer',
          'dump_width' => 'narrow', 'migrated_width' => 'narrow' }, []])
end

check 'pkdrift: a spelling difference that is also a width difference is a finding' do
  # The near neighbour of the case above, and the reason "spelling" cannot be
  # a blanket exemption: `serial` and `bigserial` differ by one prefix and by
  # thirty-two bits.
  name = 'pkdrift: a spelling difference that is also a width difference is a finding'
  dump = NARROW7.merge('calls' => 'serial')
  migrated = NARROW7.merge('calls' => 'bigserial')
  got = CVGate::PkDrift.compare(dump, migrated, 'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'], got['both_narrow'],
        got['differing_tables'].keys, got['differing_spellings'],
        got['differing_tables']['calls']['dump_width'],
        got['differing_tables']['calls']['migrated_width']],
       [false, false, false, ['calls'], {}, 'narrow', 'wide'])
end

check 'pkdrift: a dump edited back narrow does not answer for the migrations' do
  # A tree built to slip past a dump-only reading: every migration still
  # declared `[4.2]`, `id: :bigint` written on the `create_table`s, and
  # `db/schema.rb` hand-edited to the narrow key.  Reading the dump alone
  # scores it; reading both does not.
  name = 'pkdrift: a dump edited back narrow does not answer for the migrations'
  migrated = NARROW7.merge('locations' => 'bigint', 'calls' => 'bigint')
  got = CVGate::PkDrift.compare(NARROW7, migrated, 'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'], got['both_narrow'],
        got['differing_tables'].keys.sort,
        got['differing_tables']['calls']],
       [false, false, false, %w[calls locations],
        { 'dump' => 'integer', 'migrated' => 'bigint',
          'dump_width' => 'narrow', 'migrated_width' => 'wide' }])
end

check 'pkdrift: a table only the dump has is a disagreement' do
  name = 'pkdrift: a table only the dump has is a disagreement'
  got = CVGate::PkDrift.compare(NARROW7, NARROW7.reject { |t, _| t == 'subscribers' },
                                'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'], got['both_narrow'],
        got['tables_only_in_dump'], got['tables_only_in_migrated']],
       [false, false, true, ['subscribers'], []])
end

check 'pkdrift: a table only the migrations build is a disagreement' do
  # The other direction, and it is not the same fault: a migration written but
  # never dumped leaves a development database the deploy does not build.
  name = 'pkdrift: a table only the migrations build is a disagreement'
  got = CVGate::PkDrift.compare(NARROW7, NARROW7.merge('audits' => 'integer'),
                                'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'],
        got['tables_only_in_dump'], got['tables_only_in_migrated']],
       [false, false, [], ['audits']])
end

check 'pkdrift: the predecessor does not pass this for free either' do
  # Rails 5.0's adapter still defaults to `serial primary key`, so the handed-on
  # tree's two constructions agree at the narrow key without anyone having done
  # anything.  A reading that stopped at `agree && both_narrow` would be a free
  # point that raised the floor.
  name = 'pkdrift: the predecessor does not pass this for free either'
  want_drift(name, NARROW7, NARROW7,
             { 'pinned' => false, 'agree' => true, 'both_narrow' => true,
               'wide_default_primary_key' => false },
             default_key: 'serial primary key')
end

check 'pkdrift: a migration run that failed is not a reading' do
  # A run that stopped half way leaves a database that is readable and smaller
  # than the dump.  Here it is the harder case: it stopped *after* the last
  # table, so the two sides agree and only the exit code knows.
  name = 'pkdrift: a migration run that failed is not a reading'
  want_drift(name, NARROW7, NARROW7,
             { 'pinned' => false, 'agree' => true, 'both_narrow' => true,
               'migrate_exit' => 1, 'problems' => ['db:migrate exited 1'] },
             migrate_exit: 1)
end

check 'pkdrift: a schema load that failed is not a reading either' do
  # The dump side's own exit code, and it needs its own conjunct: a
  # `db:schema:load` that died part way leaves whatever the *previous* task
  # built still standing, and the reading taken afterwards is of that.
  name = 'pkdrift: a schema load that failed is not a reading either'
  want_drift(name, NARROW7, NARROW7,
             { 'pinned' => false, 'agree' => true, 'both_narrow' => true,
               'schema_load_exit' => 1,
               'problems' => ['db:schema:load exited 1'] },
             load_exit: 1)
end

check 'pkdrift: two empty databases do not agree with each other' do
  # Both sides built nothing.  Set equality holds vacuously, which is exactly
  # the shape a tree whose schema failed to load produces.
  name = 'pkdrift: two empty databases do not agree with each other'
  want_drift(name, {}, {},
             { 'pinned' => false, 'agree' => false,
               'problems' => ['neither database has a table with an id column'] })
end

check 'pkdrift: a dump that could not be read is not a pass' do
  name = 'pkdrift: a dump that could not be read is not a pass'
  got = CVGate::PkDrift.compare(nil, NARROW7, 'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'], got['both_narrow'],
        got['problems'].include?('no reading from the dump side')],
       [false, false, false, true])
end

check 'pkdrift: an application that never booted is not a pass' do
  name = 'pkdrift: an application that never booted is not a pass'
  got = CVGate::PkDrift.compare(NARROW7, nil, nil, 0, 0)
  want(name,
       [got['pinned'], got['wide_default_primary_key'],
        got['problems'].include?('no reading from the migrated side')],
       [false, false, true])
end

check 'pkdrift: two databases that agree at the wide key are still not pinned' do
  # The other way to make the two sides agree: re-stamp the migrations and
  # re-dump.  Everything is consistent and every key has doubled, which is the
  # change this step is asking not to happen.
  name = 'pkdrift: two databases that agree at the wide key are still not pinned'
  wide = NARROW7.map { |t, _| [t, 'bigint'] }.to_h
  got = CVGate::PkDrift.compare(wide, wide, 'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'], got['both_narrow'],
        got['problems'].length],
       [false, true, false, 14])
end

check 'pkdrift: an unrecognised width is a finding on whichever side has it' do
  name = 'pkdrift: an unrecognised width is a finding on whichever side has it'
  odd = NARROW7.merge('locations' => 'uuid')
  got = CVGate::PkDrift.compare(odd, odd, 'bigserial primary key', 0, 0)
  want(name,
       [got['pinned'], got['agree'], got['both_narrow'],
        got['id_widths'].nil?, got['dump']['id_widths']['locations'],
        got['problems']],
       [false, true, false, true, 'unrecognised',
        ['dump:locations has a key width this probe does not recognise (uuid)',
         'migrated:locations has a key width this probe does not recognise (uuid)']])
end

# `read_side` is the other half, and its failures all have to be loud: every
# one of them would otherwise arrive as "this side has no tables", which
# compares equal to the other side also having none.
def drift_reading(body)
  Dir.mktmpdir do |dir|
    path = File.join(dir, 'pk-side.json')
    File.write(path, body)
    yield path
  end
end

check 'pkdrift: a side is read out of the reading this probe itself wrote' do
  name = 'pkdrift: a side is read out of the reading this probe itself wrote'
  drift_reading(JSON.generate('booted' => true, 'id_sql_types' => NARROW7,
                              'default_primary_key' => 'bigserial primary key')) do |path|
    types, reading, problem = CVGate::PkDrift.read_side(path, 'dump')
    want(name, [types, reading['default_primary_key'], problem],
         [NARROW7, 'bigserial primary key', nil])
  end
end

check 'pkdrift: no reading named is a problem, not an empty reading' do
  name = 'pkdrift: no reading named is a problem, not an empty reading'
  types, reading, problem = CVGate::PkDrift.read_side(nil, 'dump')
  want(name, [types, reading, problem], [nil, nil, 'no dump reading was named'])
end

check 'pkdrift: a reading that is not there is a problem' do
  name = 'pkdrift: a reading that is not there is a problem'
  types, _reading, problem = CVGate::PkDrift.read_side('/nonexistent/pk.json', 'migrated')
  want(name, [types, !problem.nil? && problem.include?('could not be read'),
              problem.to_s.start_with?('the migrated reading')],
       [nil, true, true])
end

check 'pkdrift: a reading that is not JSON is a problem' do
  name = 'pkdrift: a reading that is not JSON is a problem'
  drift_reading("Killed\n") do |path|
    types, _reading, problem = CVGate::PkDrift.read_side(path, 'dump')
    want(name, [types, !problem.nil? && problem.include?('could not be read')], [nil, true])
  end
end

check 'pkdrift: an application that did not boot is a problem, not no tables' do
  # What a tree whose `config/environment` raises leaves behind.  It has to
  # name the boot error, because the alternative reading -- "this side has no
  # tables" -- is what a *successful* run against an empty database looks like.
  name = 'pkdrift: an application that did not boot is a problem, not no tables'
  drift_reading(JSON.generate('booted' => false,
                              'boot_error' => 'NameError: uninitialized constant Foo')) do |path|
    types, _reading, problem = CVGate::PkDrift.read_side(path, 'dump')
    want(name, [types, problem&.include?('uninitialized constant Foo'),
                problem&.include?('did not boot')],
         [nil, true, true])
  end
end

check 'pkdrift: a boot with no reason recorded still reads as a problem' do
  name = 'pkdrift: a boot with no reason recorded still reads as a problem'
  drift_reading(JSON.generate('booted' => false)) do |path|
    types, _reading, problem = CVGate::PkDrift.read_side(path, 'migrated')
    want(name, [types, problem&.include?('no reason recorded')], [nil, true])
  end
end

check 'pkdrift: a database read that raised is a problem, not no tables' do
  name = 'pkdrift: a database read that raised is a problem, not no tables'
  drift_reading(JSON.generate('booted' => true,
                              'read_error' => 'PG::ConnectionBad: could not connect')) do |path|
    types, _reading, problem = CVGate::PkDrift.read_side(path, 'dump')
    want(name, [types, problem&.include?('PG::ConnectionBad')], [nil, true])
  end
end

check 'pkdrift: a reading that is not an object is a problem' do
  name = 'pkdrift: a reading that is not an object is a problem'
  drift_reading("[1,2,3]\n") do |path|
    types, _reading, problem = CVGate::PkDrift.read_side(path, 'dump')
    want(name, [types, problem&.include?('is not an object')], [nil, true])
  end
end

check 'pkdrift: the two halves join up -- unreadable sides reach the verdict' do
  # `run` is what the verifier actually calls, and the join is where a problem
  # found by `read_side` could be dropped on the floor.  Neither side exists
  # here, so the only thing that can carry the two problems into the report is
  # the `problems` argument `compare` takes.
  name = 'pkdrift: the two halves join up -- unreadable sides reach the verdict'
  got = CVGate::PkDrift.run('/nonexistent/dump.json', '/nonexistent/migrated.json', 0, 0)
  want(name,
       [got['pinned'], got['agree'],
        got['problems'].count { |p| p.include?('could not be read') },
        got['problems'].include?('no reading from the dump side'),
        got['problems'].include?('no reading from the migrated side')],
       [false, false, 2, true, true])
end

check 'pkdrift: run takes the default key from the migrated side it booted' do
  # The default key is a property of the adapter the *application* loaded, so
  # it comes from a side that booted -- and the migrated side is preferred
  # because it is the one whose reading was taken last.
  name = 'pkdrift: run takes the default key from the migrated side it booted'
  Dir.mktmpdir do |dir|
    dump = File.join(dir, 'pk-dump.json')
    migrated = File.join(dir, 'pk-migrated.json')
    File.write(dump, JSON.generate('booted' => true, 'id_sql_types' => NARROW7,
                                   'default_primary_key' => 'serial primary key'))
    File.write(migrated, JSON.generate('booted' => true, 'id_sql_types' => NARROW7,
                                       'default_primary_key' => 'bigserial primary key',
                                       'migrations_applied' => 43))
    got = CVGate::PkDrift.run(dump, migrated, 0, 0)
    want(name,
         [got['default_primary_key'], got['wide_default_primary_key'],
          got['pinned'], got['migrations_applied'], got['problems']],
         ['bigserial primary key', true, true, 43, []])
  end
end

check 'bootprobe: a pipeline with no unknown_asset_fallback reads nil, not an error' do
  name = 'bootprobe: a pipeline with no unknown_asset_fallback reads nil, not an error'
  with_app(false) do |_ok, report|
    probe = report['probes']['asset_pipeline']
    fail!(name, probe['error'].to_s) if probe['error']
    want(name, [probe['config_assets'], probe['unknown_asset_fallback']], [true, nil])
  end
end

check 'bootprobe: the pipeline setting 5.1 flips is read when the pipeline has it' do
  name = 'bootprobe: the pipeline setting 5.1 flips is read when the pipeline has it'
  with_app(false, { 'config/assets_fallback.txt' => "true\n" }) do |_ok, report|
    probe = report['probes']['asset_pipeline']
    fail!(name, probe['error'].to_s) if probe['error']
    want(name, probe['unknown_asset_fallback'], true)
  end
end

check 'bootprobe: that setting turned off reads false, not nil' do
  # `false` and `nil` are two different findings here -- `false` is 5.1's
  # answer and `nil` is a pipeline that has no such setting -- so a probe that
  # collapsed them would score a 5.0 application as a 5.1 one.
  name = 'bootprobe: that setting turned off reads false, not nil'
  with_app(false, { 'config/assets_fallback.txt' => "false\n" }) do |_ok, report|
    probe = report['probes']['asset_pipeline']
    fail!(name, probe['error'].to_s) if probe['error']
    got = probe['unknown_asset_fallback']
    fail!(name, got.inspect) unless got == false
  end
end

check 'bootprobe: the configured deprecation behaviour is read' do
  with_app(true) do |_ok, report|
    want('bootprobe: the configured deprecation behaviour is read',
         report['probes']['deprecation']['configured'], 'raise')
  end
end

check 'bootprobe: an unmigrated deprecation setting is read too' do
  with_app(false) do |_ok, report|
    want('bootprobe: an unmigrated deprecation setting is read too',
         report['probes']['deprecation']['configured'], 'log')
  end
end

check 'bootprobe: a leftover new_framework_defaults file is listed' do
  with_app(false, { 'config/initializers/new_framework_defaults_7_1.rb' => "# TODO\n" }) do |_ok, report|
    want('bootprobe: a leftover new_framework_defaults file is listed',
         report['probes']['framework_defaults_files'],
         ['config/initializers/new_framework_defaults_7_1.rb'])
  end
end

check 'bootprobe: no leftover defaults file is an empty list, not a missing key' do
  with_app(true) do |_ok, report|
    want('bootprobe: no leftover defaults file is an empty list, not a missing key',
         report['probes']['framework_defaults_files'], [])
  end
end

# The instrument's own failure mode, made visible.  `database` cannot work in a
# stub with no ActiveRecord, and the point of the check is that every *other*
# probe still reports: a gate that crashed here would lose nine good readouts to
# one missing constant.
check 'bootprobe: a failing probe records its error and the others survive' do
  name = 'bootprobe: a failing probe records its error and the others survive'
  with_app(true) do |_ok, report|
    database = report['probes']['database']
    unless database['error'].to_s.include?('ActiveRecord')
      fail!(name, "want an ActiveRecord error, got #{database.inspect}")
    end
    missing = %w[versions load_defaults autoloader eager_load zeitwerk_check
                 asset_pipeline loaded_gems framework_defaults_files deprecation] -
              report['probes'].keys
    want(name, missing, [])
  end
end

check 'bootprobe: loaded_gems is a name-to-version map' do
  with_app(true) do |_ok, report|
    gems = report['probes']['loaded_gems']
    unless gems.is_a?(Hash) && gems.values.all? { |v| v.is_a?(String) }
      fail!('bootprobe: loaded_gems is a name-to-version map', gems.inspect[0, 120])
    end
  end
end

check 'bootprobe: zeitwerk_check reports that it was skipped, not that it passed' do
  with_app(true) do |_ok, report|
    want('bootprobe: zeitwerk_check reports that it was skipped, not that it passed',
         report['probes']['zeitwerk_check'].key?('ok'), false)
  end
end

check 'bootprobe: --out writes a report with every probe key' do
  with_app(true) do |_ok, report|
    want('bootprobe: --out writes a report with every probe key',
         report.is_a?(Hash) && report.key?('probes') && report.key?('boot_seconds'), true)
  end
end

check 'bootprobe: an application with no secrets reader says so' do
  with_app(false) do |_ok, report|
    got = report['probes']['secrets']
    want('bootprobe: an application with no secrets reader says so',
         [got['reader_exists'], got['from_secrets_yml']], [false, false])
  end
end

check 'bootprobe: a key that came from secrets.yml is reported as such' do
  with_app(true) do |_ok, report|
    got = report['probes']['secrets']
    want('bootprobe: a key that came from secrets.yml is reported as such',
         [got['reader_exists'], got['from_secrets_yml'], got['secrets_yml_length']],
         [true, true, 64])
  end
end

# The case a careless probe gets wrong: the reader exists, the application has
# a working key, and `config/secrets.yml` supplied none of it -- an initializer
# did.  A gate on "there is a secret_key_base" passes here; the gate this probe
# supports must not.
#
# All five fields, not the two the gate reads.  The distinction this fixture
# exists for is between *where the key came from* and *whether there is one*,
# and only the lengths carry the second half: a probe that reported the whole
# arrangement as absent would satisfy `from_secrets_yml == false` while saying
# something false about the application.  `assigned_length` is the initializer's
# key -- 128 characters of it -- and `secrets_yml_length` and `yml_file_length`
# are both 0 because the YAML supplied nothing.
check 'bootprobe: a key from an initializer is not a key from secrets.yml' do
  with_app(true, {}, secrets: :empty) do |_ok, report|
    got = report['probes']['secrets']
    want('bootprobe: a key from an initializer is not a key from secrets.yml',
         [got['reader_exists'], got['from_secrets_yml'], got['secrets_yml_length'],
          got['assigned_length'], got['yml_file_length'],
          got['effective_from_secrets_yml']],
         [true, false, 0, 128, 0, false])
  end
end

# `secrets_yml_present` is about the file and `from_secrets_yml` is about the
# value, and the instruction states them as separate sentences because they
# fail separately: a tree can delete the file and set the key some other way,
# and a tree can ship the file with nothing useful in it.
check 'bootprobe: a config/secrets.yml in the tree is reported present' do
  name = 'bootprobe: a config/secrets.yml in the tree is reported present'
  with_app(true, { 'config/secrets.yml' => "production:\n  secret_key_base: x\n" }) do |_ok, report|
    got = report['probes']['secrets']
    want(name, [got['secrets_yml_present'], got['from_secrets_yml']], [true, true])
    unless got['secrets_yml_path'].to_s.end_with?('config/secrets.yml')
      fail!(name, "path is #{got['secrets_yml_path'].inspect}")
    end
  end
end

check 'bootprobe: no config/secrets.yml is reported absent, at a path' do
  # False *and* a path: a probe that answered nil here would leave "there is no
  # file" and "I could not work out where to look" as the same report.
  name = 'bootprobe: no config/secrets.yml is reported absent, at a path'
  with_app(true) do |_ok, report|
    got = report['probes']['secrets']
    want(name, got['secrets_yml_present'], false)
    unless got['secrets_yml_path'].to_s.end_with?('config/secrets.yml')
      fail!(name, "path is #{got['secrets_yml_path'].inspect}")
    end
  end
end

check 'bootprobe: the file being there is not the key coming from it' do
  name = 'bootprobe: the file being there is not the key coming from it'
  with_app(true, { 'config/secrets.yml' => "production:\n" }, secrets: :empty) do |_ok, report|
    got = report['probes']['secrets']
    want(name, [got['secrets_yml_present'], got['from_secrets_yml']], [true, false])
  end
end

check "bootprobe: the application's own configured secrets path is the one checked" do
  name = "bootprobe: the application's own configured secrets path is the one checked"
  with_app(true, { 'config/elsewhere/secrets.yml' => "production:\n" },
           secrets_path: 'config/elsewhere/secrets.yml') do |_ok, report|
    got = report['probes']['secrets']
    want(name, got['secrets_yml_present'], true)
    unless got['secrets_yml_path'].to_s.end_with?('config/elsewhere/secrets.yml')
      fail!(name, "path is #{got['secrets_yml_path'].inspect}")
    end
  end
end

check 'bootprobe: a configured secrets path with nothing at it is reported absent' do
  name = 'bootprobe: a configured secrets path with nothing at it is reported absent'
  with_app(true, {}, secrets_path: 'config/elsewhere/secrets.yml') do |_ok, report|
    got = report['probes']['secrets']
    want(name, got['secrets_yml_present'], false)
    unless got['secrets_yml_path'].to_s.end_with?('config/elsewhere/secrets.yml')
      fail!(name, "path is #{got['secrets_yml_path'].inspect}")
    end
  end
end

check 'bootprobe: the secret itself is never written into the report' do
  with_app(true) do |_ok, report|
    want('bootprobe: the secret itself is never written into the report',
         JSON.generate(report).include?('s' * 64), false)
  end
end

# ---------------------------------------------------------------------------
# depdrive: the deprecations a running application raises
# ---------------------------------------------------------------------------

DD = CVGate::DepDrive

# -- the call site -----------------------------------------------------------

FRAMEWORK = '/bundle/gems/activesupport-4.2.11.3/lib/active_support/x.rb:12'
APP_FRAME = '/app/app/models/subscription_confirmer.rb:4:in `confirm\''

check 'depdrive: the call site is the application frame, not the framework one' do
  want('depdrive: the call site is the application frame, not the framework one',
       DD.blame([FRAMEWORK, APP_FRAME, '/app/config/environment.rb:5'], '/app'),
       'app/models/subscription_confirmer.rb:4:in `confirm\'')
end

check 'depdrive: the innermost application frame wins, not the outermost' do
  want('depdrive: the innermost application frame wins, not the outermost',
       DD.blame(['/app/app/models/a.rb:1', '/app/app/models/b.rb:2'], '/app'),
       'app/models/a.rb:1')
end

# A gem unpacked under the application root is still the framework: the file to
# change is never inside `vendor/bundle`.
check 'depdrive: a vendored gem frame is not blamed' do
  want('depdrive: a vendored gem frame is not blamed',
       DD.blame(['/app/vendor/bundle/ruby/2.3.0/gems/rails/x.rb:9',
                 '/app/app/models/a.rb:1'], '/app'),
       'app/models/a.rb:1')
end

check 'depdrive: with no application frame the innermost frame is reported' do
  want('depdrive: with no application frame the innermost frame is reported',
       DD.blame([FRAMEWORK], '/app'), FRAMEWORK)
end

# The anchored strip: "/app/" also occurs inside this framework path, and an
# unanchored `sub` would turn it into a file name that does not exist.
check 'depdrive: the app prefix is stripped only from the front' do
  want('depdrive: the app prefix is stripped only from the front',
       DD.blame(['/bundle/gems/rack-1.5.2/lib/app/handler.rb:3'], '/app'),
       '/bundle/gems/rack-1.5.2/lib/app/handler.rb:3')
end

check 'depdrive: an empty callstack is reported, not raised on' do
  want('depdrive: an empty callstack is reported, not raised on',
       DD.blame([], '/app'), '(no call site)')
end

# -- the collector ------------------------------------------------------------

# Stands in for ActiveSupport::Deprecation: the only thing the collector needs
# from it is that `warn` exists to be prepended to.
class FakeDeprecator
  attr_reader :seen

  def initialize
    @seen = []
  end

  def warn(message = nil, callstack = nil, *_rest)
    @seen << message
    :underneath
  end
end

def with_collector
  records = []
  klass = Class.new(FakeDeprecator)
  klass.prepend(DD.collector('/app', records))
  yield klass.new, records
end

check 'depdrive: a warn through the collector is recorded' do
  with_collector do |dep, records|
    dep.warn('`#deliver` is deprecated', [APP_FRAME])
    want('depdrive: a warn through the collector is recorded',
         records.map { |r| r['message'] },
         ['`#deliver` is deprecated'])
  end
end

# The collector observes; it must not replace.  If `super` were dropped the
# application would stop printing its own deprecation warnings and, worse, a
# tree configured to raise on them would stop raising -- the run would change
# because it was being measured.
check 'depdrive: the method underneath still runs' do
  with_collector do |dep, _records|
    got = dep.warn('x', [APP_FRAME])
    want('depdrive: the method underneath still runs',
         [got, dep.seen], [:underneath, ['x']])
  end
end

check 'depdrive: the recorded call site comes from the passed callstack' do
  with_collector do |dep, records|
    dep.warn('x', [APP_FRAME])
    want('depdrive: the recorded call site comes from the passed callstack',
         records.first['where'],
         'app/models/subscription_confirmer.rb:4:in `confirm\'')
  end
end

# Rails 7.1 calls `warn` with one argument from some paths.  Without the
# `|| caller` fallback that is a nil callstack and the finding loses its site.
check 'depdrive: a warn with no callstack still gets a site' do
  with_collector do |dep, records|
    dep.warn('x')
    got = records.first['where']
    want('depdrive: a warn with no callstack still gets a site',
         got.is_a?(String) && !got.empty?, true)
  end
end

check 'depdrive: the message is stripped' do
  with_collector do |dep, records|
    dep.warn("  DEPRECATION WARNING: x \n", [APP_FRAME])
    want('depdrive: the message is stripped',
         records.first['message'], 'DEPRECATION WARNING: x')
  end
end

check 'depdrive: a nil message does not raise' do
  with_collector do |dep, records|
    dep.warn(nil, [APP_FRAME])
    want('depdrive: a nil message does not raise', records.first['message'], '')
  end
end

check 'depdrive: every raise is recorded, not just the first' do
  with_collector do |dep, records|
    3.times { dep.warn('x', [APP_FRAME]) }
    want('depdrive: every raise is recorded, not just the first',
         records.length, 3)
  end
end

# -- the summary --------------------------------------------------------------

def record(message, where)
  { 'message' => message, 'where' => where }
end

check 'depdrive: the same deprecation from the same site is one finding' do
  got = DD.summarise([record('a', 'x.rb:1')] * 4)
  want('depdrive: the same deprecation from the same site is one finding',
       [got['count'], got['raised'], got['deprecations'].first['times']],
       [1, 4, 4])
end

check 'depdrive: the same deprecation from two sites is two findings' do
  got = DD.summarise([record('a', 'x.rb:1'), record('a', 'y.rb:2')])
  want('depdrive: the same deprecation from two sites is two findings',
       [got['count'], got['raised']], [2, 2])
end

check 'depdrive: findings are ordered with the most frequent first' do
  got = DD.summarise([record('rare', 'y.rb:2')] + [record('common', 'x.rb:1')] * 3)
  want('depdrive: findings are ordered with the most frequent first',
       got['deprecations'].map { |d| d['message'] }, %w[common rare])
end

check 'depdrive: a clean tree summarises to zero, not to nothing' do
  got = DD.summarise([])
  want('depdrive: a clean tree summarises to zero, not to nothing',
       [got['count'], got['raised'], got['own_count']], [0, 0, 0])
end

# `own_count` is the number the gate states a requirement about.  A deprecation
# raised from inside a gem is that gem's to fix, and holding the candidate to
# it would make the gate a function of which versions the resolver picked.
check 'depdrive: a deprecation from a gem is counted but not owned' do
  got = DD.summarise([record('a', '/bundle/gems/foo-1.0/lib/foo.rb:3')])
  want('depdrive: a deprecation from a gem is counted but not owned',
       [got['count'], got['own_count']], [1, 0])
end

check 'depdrive: a deprecation from the application is owned' do
  got = DD.summarise([record('a', 'app/models/x.rb:3')])
  want('depdrive: a deprecation from the application is owned',
       [got['count'], got['own_count'], got['own']], [1, 1, ['app/models/x.rb:3']])
end

check 'depdrive: owned and unowned findings are separated, not mixed' do
  got = DD.summarise([record('a', 'app/models/x.rb:3'),
                      record('b', '/bundle/gems/foo-1.0/lib/foo.rb:3')])
  want('depdrive: owned and unowned findings are separated, not mixed',
       [got['count'], got['own_count']], [2, 1])
end

# -- the session --------------------------------------------------------------

FakeResponse = Struct.new(:status, :headers, :body)

# Records the env it was handed, so the checks below can assert on what the
# application would have received.
class FakeMock
  attr_reader :envs

  def initialize(responses)
    @responses = responses
    @envs = []
  end

  def get(path, env = {})
    @envs << [path, env]
    @responses.shift || FakeResponse.new(200, {}, '')
  end

  def post(path, env = {})
    @envs << [path, env]
    @responses.shift || FakeResponse.new(200, {}, '')
  end
end

def with_session(responses)
  mock = FakeMock.new(responses)
  yield DD::Session.new(nil, mock), mock
end

check 'depdrive: every request carries the host the application expects' do
  with_session([]) do |session, mock|
    session.get('/')
    want('depdrive: every request carries the host the application expects',
         mock.envs.first[1]['HTTP_HOST'], 'cityvoice.test')
  end
end

check 'depdrive: the first request carries no cookie header' do
  with_session([]) do |session, mock|
    session.get('/')
    want('depdrive: the first request carries no cookie header',
         mock.envs.first[1].key?('HTTP_COOKIE'), false)
  end
end

# The whole point: without this the session cookie never comes back,
# `protect_from_forgery` rejects the form POST with a 422, and the mailer
# behind it -- which is where `#deliver` lives -- is never reached.
check 'depdrive: a cookie set by one response is sent on the next request' do
  with_session([FakeResponse.new(200, { 'Set-Cookie' => '_cityvoice_session=abc; path=/; HttpOnly' }, '')]) do |session, mock|
    session.get('/locations/1')
    session.post('/subscription', {})
    want('depdrive: a cookie set by one response is sent on the next request',
         mock.envs.last[1]['HTTP_COOKIE'], '_cityvoice_session=abc')
  end
end

check 'depdrive: newline-joined Set-Cookie headers are all absorbed' do
  with_session([FakeResponse.new(200, { 'Set-Cookie' => "a=1; path=/\nb=2; path=/" }, '')]) do |session, mock|
    session.get('/')
    session.get('/')
    want('depdrive: newline-joined Set-Cookie headers are all absorbed',
         mock.envs.last[1]['HTTP_COOKIE'], 'a=1; b=2')
  end
end

check 'depdrive: an array of Set-Cookie headers is absorbed too' do
  with_session([FakeResponse.new(200, { 'set-cookie' => ['a=1; path=/', 'b=2'] }, '')]) do |session, mock|
    session.get('/')
    session.get('/')
    want('depdrive: an array of Set-Cookie headers is absorbed too',
         mock.envs.last[1]['HTTP_COOKIE'], 'a=1; b=2')
  end
end

check 'depdrive: a later value for the same cookie replaces the earlier one' do
  with_session([FakeResponse.new(200, { 'Set-Cookie' => 'a=1; path=/' }, ''),
                FakeResponse.new(200, { 'Set-Cookie' => 'a=2; path=/' }, '')]) do |session, mock|
    session.get('/')
    session.get('/')
    session.get('/')
    want('depdrive: a later value for the same cookie replaces the earlier one',
         mock.envs.last[1]['HTTP_COOKIE'], 'a=2')
  end
end

check 'depdrive: the POST body reaches the mock as params' do
  with_session([]) do |session, mock|
    session.post('/subscription', 'k' => 'v')
    want('depdrive: the POST body reaches the mock as params',
         mock.envs.last[1][:params], 'k' => 'v')
  end
end

# The token is spelled two ways depending on the release and on whether it came
# from the layout or from `form_for`; both have to be readable.
check 'depdrive: the csrf token is read from the meta tag' do
  with_session([]) do |session, _mock|
    page = FakeResponse.new(200, {}, '<meta name="csrf-token" content="TOK123" />')
    want('depdrive: the csrf token is read from the meta tag',
         session.csrf(page), 'TOK123')
  end
end

check 'depdrive: the csrf token is read from a hidden field' do
  with_session([]) do |session, _mock|
    page = FakeResponse.new(200, {},
                            '<input type="hidden" name="authenticity_token" value="TOK456" />')
    want('depdrive: the csrf token is read from a hidden field',
         session.csrf(page), 'TOK456')
  end
end

check 'depdrive: the csrf token is read with the attributes the other way round' do
  with_session([]) do |session, _mock|
    page = FakeResponse.new(200, {},
                            '<input value="TOK789" name="authenticity_token" />')
    want('depdrive: the csrf token is read with the attributes the other way round',
         session.csrf(page), 'TOK789')
  end
end

check 'depdrive: a page with no token reports none rather than a wrong one' do
  with_session([]) do |session, _mock|
    want('depdrive: a page with no token reports none rather than a wrong one',
         session.csrf(FakeResponse.new(200, {}, '<p>hello</p>')), nil)
  end
end

# -- the content security policy reading --------------------------------------
#
# Every check below comes in a pair: a tree that satisfies the requirement and
# the near miss that must not satisfy it.  The near misses are the ones this
# step expects to meet -- a policy declared and never served, a policy served
# Report-Only, a policy pasted from the generator with the inline scripts left
# bare, and a hard-coded `nonce="..."` attribute that no generator produced.
#
# The last of those is the subtle one.  A hand-written `nonce="abc"` in a view
# fails only while the header does not name it; paste `'nonce-abc'` into
# `script-src` beside it and every reading above passes -- and a near-miss
# fixture written with a nonce the header does not name tests the other rule
# instead.  The reading and its fixtures below turn on the property the
# requirement is actually about:
# two responses from the same path in the same drive must not carry the same
# nonce.  `SAME_NONCE` is the constant-generator tree, which is clean on
# everything else.

HTML = { 'Content-Type' => 'text/html; charset=utf-8' }.freeze
GOOD_CSP = "default-src 'self' https:; script-src 'self' 'nonce-abc123=='".freeze

def observe(responses)
  responses.each_with_object([]) do |(path, response), sink|
    DD.csp_observe(path, response, sink)
  end
end

def html_response(body, headers = {})
  FakeResponse.new(200, HTML.merge(headers), body)
end

check 'depdrive/csp: a header parses into directives and their sources' do
  got = DD.csp_directives("default-src 'self' https:; script-src 'self'")
  want('depdrive/csp: a header parses into directives and their sources',
       got, 'default-src' => ["'self'", 'https:'], 'script-src' => ["'self'"])
end

check 'depdrive/csp: a directive with no sources is a key with no sources' do
  want('depdrive/csp: a directive with no sources is a key with no sources',
       DD.csp_directives('upgrade-insecure-requests'),
       'upgrade-insecure-requests' => [])
end

check 'depdrive/csp: directive names are matched without case' do
  want('depdrive/csp: directive names are matched without case',
       DD.csp_directives("Default-Src 'self'").keys, ['default-src'])
end

check 'depdrive/csp: an empty header parses to no directives' do
  want('depdrive/csp: an empty header parses to no directives',
       DD.csp_directives('').keys, [])
end

check 'depdrive/csp: a response that is not HTML is not observed' do
  seen = observe([['/export.csv', FakeResponse.new(200, { 'Content-Type' => 'text/csv' }, 'a,b')]])
  want('depdrive/csp: a response that is not HTML is not observed', seen.length, 0)
end

check 'depdrive/csp: an HTML response is observed with its header' do
  seen = observe([['/', html_response('<p>hi</p>', 'Content-Security-Policy' => GOOD_CSP)]])
  want('depdrive/csp: an HTML response is observed with its header',
       [seen.length, seen.first['header']], [1, GOOD_CSP])
end

check 'depdrive/csp: the header is found under a lower-case name too' do
  seen = observe([['/', html_response('<p>hi</p>', 'content-security-policy' => GOOD_CSP)]])
  want('depdrive/csp: the header is found under a lower-case name too',
       seen.first['header'], GOOD_CSP)
end

check 'depdrive/csp: a script with a src is not an inline script' do
  seen = observe([['/', html_response('<script src="/a.js"></script>')]])
  want('depdrive/csp: a script with a src is not an inline script',
       seen.first['inline_nonces'], [])
end

check 'depdrive/csp: an inline script records the nonce it carries' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>')]])
  want('depdrive/csp: an inline script records the nonce it carries',
       seen.first['inline_nonces'], ['abc123=='])
end

check 'depdrive/csp: a bare inline script records no nonce' do
  seen = observe([['/', html_response('<script>x()</script>')]])
  want('depdrive/csp: a bare inline script records no nonce',
       seen.first['inline_nonces'], [nil])
end

check 'depdrive/csp: a served policy with nonced scripts has no problems' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>',
                                      'Content-Security-Policy' => GOOD_CSP)]])
  got = DD.csp_summary(seen)
  want('depdrive/csp: a served policy with nonced scripts has no problems',
       [got['problems'], got['nonce_problems']], [[], []])
end

check 'depdrive/csp: the counts are reported beside the problems' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>' \
                                      '<script src="/a.js"></script>',
                                      'Content-Security-Policy' => GOOD_CSP)],
                  ['/locations.json', FakeResponse.new(200, { 'Content-Type' => 'application/json' }, '{}')]])
  got = DD.csp_summary(seen)
  want('depdrive/csp: the counts are reported beside the problems',
       [got['html_responses'], got['inline_scripts'], got['with_header'], got['report_only']],
       [1, 1, 1, 0])
end

check 'depdrive/csp: a drive that reached no HTML response is a problem' do
  want('depdrive/csp: a drive that reached no HTML response is a problem',
       DD.csp_summary([])['problems'], ['no successful HTML response observed'])
end

# Rails 5.2 puts ContentSecurityPolicy::Middleware inside ShowExceptions, so a
# request that raises is answered by a middleware the policy never reaches.
# This application has one: `/locations` is 406 Not Acceptable.  The reading
# therefore watches responses the application itself produced, and the three
# checks below are the boundary -- 2xx observed, everything else not, and a
# drive whose only HTML was an error page measuring nothing rather than
# passing.

check 'depdrive/csp: a framework error page is not observed' do
  seen = observe([['/locations', FakeResponse.new(406, HTML, '<h1>Not Acceptable</h1>')]])
  want('depdrive/csp: a framework error page is not observed', seen.length, 0)
end

check 'depdrive/csp: a redirect is not observed either' do
  seen = observe([['/old', FakeResponse.new(302, HTML, '<html><body>You are being redirected.</body></html>')]])
  want('depdrive/csp: a redirect is not observed either', seen.length, 0)
end

check 'depdrive/csp: 299 is observed and 300 is not' do
  want('depdrive/csp: 299 is observed and 300 is not',
       [observe([['/a', FakeResponse.new(299, HTML, '<p>x</p>')]]).length,
        observe([['/b', FakeResponse.new(300, HTML, '<p>x</p>')]]).length],
       [1, 0])
end

check 'depdrive/csp: an error page cannot stand in for a served policy' do
  seen = observe([['/locations', FakeResponse.new(406, HTML, '<p>no</p>')]])
  want('depdrive/csp: an error page cannot stand in for a served policy',
       DD.csp_summary(seen)['problems'], ['no successful HTML response observed'])
end

check 'depdrive/csp: an error page beside a good page costs nothing' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>',
                                      'Content-Security-Policy' => GOOD_CSP)],
                  ['/locations', FakeResponse.new(406, HTML, '<p>no</p>')]])
  got = DD.csp_summary(seen)
  want('depdrive/csp: an error page beside a good page costs nothing',
       [got['problems'], got['nonce_problems'], got['html_responses']], [[], [], 1])
end

check 'depdrive/csp: a response with no header is named in the problems' do
  seen = observe([['/locations', html_response('<script nonce="q">x()</script>')]])
  want('depdrive/csp: a response with no header is named in the problems',
       DD.csp_summary(seen)['problems'],
       ['/locations: no Content-Security-Policy header'])
end

check 'depdrive/csp: a Report-Only policy is not a served policy' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>',
                                      'Content-Security-Policy-Report-Only' => GOOD_CSP)]])
  got = DD.csp_summary(seen)
  want('depdrive/csp: a Report-Only policy is not a served policy',
       [got['problems'], got['report_only']],
       [['/: Content-Security-Policy-Report-Only only'], 1])
end

check 'depdrive/csp: a default-src that does not name self is a problem' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>',
                                      'Content-Security-Policy' =>
                                        "default-src https:; script-src 'nonce-abc123=='")]])
  want('depdrive/csp: a default-src that does not name self is a problem',
       DD.csp_summary(seen)['problems'],
       ["/: default-src does not name 'self'"])
end

check 'depdrive/csp: a bare inline script under a good policy is a nonce problem' do
  seen = observe([['/', html_response('<script>x()</script>',
                                      'Content-Security-Policy' => GOOD_CSP)]])
  got = DD.csp_summary(seen)
  want('depdrive/csp: a bare inline script under a good policy is a nonce problem',
       [got['problems'], got['nonce_problems']],
       [[], ['/: inline <script> with no nonce']])
end

check 'depdrive/csp: a nonce the header does not name is a nonce problem' do
  seen = observe([['/', html_response('<script nonce="handwritten">x()</script>',
                                      'Content-Security-Policy' => GOOD_CSP)]])
  want('depdrive/csp: a nonce the header does not name is a nonce problem',
       DD.csp_summary(seen)['nonce_problems'],
       ['/: inline <script> nonce not named by script-src'])
end

check 'depdrive/csp: default-src carries the nonce when script-src is absent' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>',
                                      'Content-Security-Policy' =>
                                        "default-src 'self' 'nonce-abc123=='")]])
  want('depdrive/csp: default-src carries the nonce when script-src is absent',
       DD.csp_summary(seen)['nonce_problems'], [])
end

check 'depdrive/csp: a page with the inline scripts removed is not a pass' do
  seen = observe([['/', html_response('<script src="/a.js"></script>',
                                      'Content-Security-Policy' => GOOD_CSP)]])
  got = DD.csp_summary(seen)
  want('depdrive/csp: a page with the inline scripts removed is not a pass',
       [got['problems'], got['nonce_problems']],
       [[], ['no inline <script> observed in any HTML response']])
end

# -- the nonce is minted per request ------------------------------------------
#
# Fixtures for the pair.  `TWICE` is what a per-request generator produces: the
# same path fetched twice, two nonces, each named by its own response's policy.
# `SAME_NONCE` is `->(_request) { 'abc' }` -- or a `nonce="abc"` pasted into the
# template with `'nonce-abc'` pasted into the initializer beside it, which is
# indistinguishable from it on the wire.  Both are served, enforced, and name
# every nonce they carry; only the repeat separates them.

def nonced(nonce)
  html_response(%(<script nonce="#{nonce}">x()</script>),
                'Content-Security-Policy' =>
                  "default-src 'self' https:; script-src 'self' 'nonce-#{nonce}'")
end

TWICE = [['/', nonced('r1')], ['/', nonced('r2')]].freeze
SAME_NONCE = [['/', nonced('abc')], ['/', nonced('abc')]].freeze

check 'depdrive/csp: two fetches with two nonces are no problem' do
  got = DD.csp_summary(observe(TWICE.dup))
  want('depdrive/csp: two fetches with two nonces are no problem',
       [got['problems'], got['nonce_problems'], got['html_responses']], [[], [], 2])
end

check 'depdrive/csp: the same nonce on two fetches of one path is a problem' do
  got = DD.csp_summary(observe(SAME_NONCE.dup))
  want('depdrive/csp: the same nonce on two fetches of one path is a problem',
       [got['problems'], got['nonce_problems']],
       [[], ['/: the same nonce on two responses -- not minted per request']])
end

check 'depdrive/csp: one fetch of a path cannot be a repeat problem' do
  got = DD.csp_summary(observe([['/', nonced('abc')]]))
  want('depdrive/csp: one fetch of a path cannot be a repeat problem',
       [got['problems'], got['nonce_problems']], [[], []])
end

check 'depdrive/csp: two paths sharing a nonce is not the reading' do
  # Deliberately not a problem: this reading is about one path fetched twice.
  # Two paths could be one request in some other application, and the bare
  # `nonce_problems` above already catch a nonce nobody named.
  got = DD.csp_summary(observe([['/a', nonced('abc')], ['/b', nonced('abc')]]))
  want('depdrive/csp: two paths sharing a nonce is not the reading',
       got['nonce_problems'], [])
end

check 'depdrive/csp: a repeat with no nonce at all is the other problem' do
  seen = observe([['/', html_response('<script>x()</script>',
                                      'Content-Security-Policy' => GOOD_CSP)],
                  ['/', html_response('<script>x()</script>',
                                      'Content-Security-Policy' => GOOD_CSP)]])
  want('depdrive/csp: a repeat with no nonce at all is the other problem',
       DD.csp_summary(seen)['nonce_problems'],
       ['/: inline <script> with no nonce'])
end

check 'depdrive/csp: one bad response among good ones is still a problem' do
  seen = observe([['/', html_response('<script nonce="abc123==">x()</script>',
                                      'Content-Security-Policy' => GOOD_CSP)],
                  ['/locations', html_response('<script nonce="abc123==">x()</script>')]])
  got = DD.csp_summary(seen)
  want('depdrive/csp: one bad response among good ones is still a problem',
       [got['problems'], got['html_responses'], got['with_header']],
       [['/locations: no Content-Security-Policy header'], 2, 1])
end

# -- host authorization -------------------------------------------------------
#
# `hosts_summary` turns three statuses into the two facts a gate can state a
# requirement about.  The fixtures are the four configurations this application
# can be left in, written as the statuses each one produces:
#
#   OK          config.hosts names the deployment and the loopback
#   OPEN        config.hosts was never stated, so the middleware steps aside
#   NAME_ONLY   config.hosts names the deployment and nothing else
#   SHUT        config.hosts was built from a variable that is not set here
#   LOOSE       config.hosts holds an unanchored Regexp, or a substring of the
#               deployment's name, so a hostname that CONTAINS it is served
#
# Only the first is a pass, and no single status separates it from the other
# four -- which is why the probe sends four headers and not one.

HOSTS_OK = { 'known' => 200, 'loopback' => 200,
             'unknown' => 403, 'suffix' => 403 }.freeze
HOSTS_OPEN = { 'known' => 200, 'loopback' => 200,
               'unknown' => 200, 'suffix' => 200 }.freeze
HOSTS_NAME_ONLY = { 'known' => 200, 'loopback' => 403,
                    'unknown' => 403, 'suffix' => 403 }.freeze
HOSTS_SHUT = { 'known' => 403, 'loopback' => 403,
               'unknown' => 403, 'suffix' => 403 }.freeze
HOSTS_LOOSE = { 'known' => 200, 'loopback' => 200,
                'unknown' => 403, 'suffix' => 200 }.freeze

check 'depdrive/hosts: a stated list serves the deployment and refuses the rest' do
  got = DD.hosts_summary(HOSTS_OK)
  want('depdrive/hosts: a stated list serves the deployment and refuses the rest',
       [got['serves_known'], got['blocks_unknown'], got['blocks_suffix']],
       [true, true, true])
end

check 'depdrive/hosts: a list nobody stated serves the unknown host' do
  got = DD.hosts_summary(HOSTS_OPEN)
  want('depdrive/hosts: a list nobody stated serves the unknown host',
       [got['serves_known'], got['blocks_unknown'], got['blocks_suffix']],
       [true, false, false])
end

check 'depdrive/hosts: a list of one name stops the loopback' do
  got = DD.hosts_summary(HOSTS_NAME_ONLY)
  want('depdrive/hosts: a list of one name stops the loopback',
       [got['serves_known'], got['blocks_unknown']], [false, true])
end

check 'depdrive/hosts: a list built from an unset variable stops everything' do
  got = DD.hosts_summary(HOSTS_SHUT)
  want('depdrive/hosts: a list built from an unset variable stops everything',
       [got['serves_known'], got['blocks_unknown']], [false, true])
end

# The near miss the fourth header exists for.  Every other reading of this
# configuration is indistinguishable from the pass: it serves both of the hosts
# the deployment uses and refuses a name that has nothing in common with it.
check 'depdrive/hosts: a loose match refuses the stranger and serves the suffix' do
  got = DD.hosts_summary(HOSTS_LOOSE)
  want('depdrive/hosts: a loose match refuses the stranger and serves the suffix',
       [got['serves_known'], got['blocks_unknown'], got['blocks_suffix']],
       [true, true, false])
end

# 403 and not "anything but 200": a tree whose landing page raises under one
# Host header and serves under another has a bug, not a host list, and the two
# are indistinguishable in a reading that only asks whether the request failed.
check 'depdrive/hosts: a 500 on the unknown host is not a refusal' do
  got = DD.hosts_summary('known' => 200, 'loopback' => 200,
                         'unknown' => 500, 'suffix' => 500)
  want('depdrive/hosts: a 500 on the unknown host is not a refusal',
       [got['blocks_unknown'], got['blocks_suffix']], [false, false])
end

check 'depdrive/hosts: a pass that never ran is not a pass' do
  got = DD.hosts_summary({})
  want('depdrive/hosts: a pass that never ran is not a pass',
       [got['known'], got['serves_known'], got['blocks_unknown'],
        got['blocks_suffix']],
       [nil, false, false, false])
end

# The four statuses are carried through unchanged beside the judgement: when a
# gate fails, the number in the evidence is what says which of the five
# configurations above the tree is in.
check 'depdrive/hosts: the statuses are reported beside the judgement' do
  got = DD.hosts_summary(HOSTS_NAME_ONLY)
  want('depdrive/hosts: the statuses are reported beside the judgement',
       [got['known'], got['loopback'], got['unknown'], got['suffix']],
       [200, 403, 403, 403])
end

# `queries_summary` turns a query count and a row count into the one fact a
# gate can state a requirement about: this page does not pay per row.  The
# fixtures are the measurements taken on the step 06 tree, both ways round --
# /voice_answers at 4 queries for 12 answers with the preload in place, and at
# 37 without it, which is three associations per row plus the collection.
QUERIES_ROWS = { 'voice_answers' => 12, 'export_csv' => 13 }.freeze
QUERIES_FIXED = { 'voice_answers' => { 'status' => 200, 'queries' => 4 },
                  'export_csv' => { 'status' => 200, 'queries' => 5 } }.freeze
QUERIES_NPLUS1 = { 'voice_answers' => { 'status' => 200, 'queries' => 37 },
                   'export_csv' => { 'status' => 200, 'queries' => 19 } }.freeze

check 'depdrive/queries: a preloaded page costs fewer queries than it has rows' do
  got = DD.queries_summary(QUERIES_ROWS, QUERIES_FIXED)
  want('depdrive/queries: a preloaded page costs fewer queries than it has rows',
       [got['voice_answers']['preloaded'], got['export_csv']['preloaded']],
       [true, true])
end

check 'depdrive/queries: a page that pays per row is not preloaded' do
  got = DD.queries_summary(QUERIES_ROWS, QUERIES_NPLUS1)
  want('depdrive/queries: a page that pays per row is not preloaded',
       [got['voice_answers']['preloaded'], got['export_csv']['preloaded']],
       [false, false])
end

# The near miss, and the reason the rows are made by the pass rather than taken
# as they come: with one row in the table the two readings are the same
# reading.  A page costing 4 queries for 1 row is an N+1 and this rule calls it
# one, which is correct -- but a page costing 1 query for 1 row would pass with
# the association read per row, so the count of rows is what makes the
# comparison mean anything.
check 'depdrive/queries: one row cannot tell the two apart' do
  got = DD.queries_summary({ 'voice_answers' => 1 },
                           'voice_answers' => { 'status' => 200, 'queries' => 1 })
  want('depdrive/queries: one row cannot tell the two apart',
       got['voice_answers']['preloaded'], false)
end

# A page that raises issues almost no queries.  The cheapest possible reading
# of a 500 -- one query, or none -- is also the best possible reading of a
# preload, so the status is part of the judgement and not beside it.
check 'depdrive/queries: a 500 is not a well preloaded page' do
  got = DD.queries_summary(QUERIES_ROWS,
                           'voice_answers' => { 'status' => 500, 'queries' => 1 },
                           'export_csv' => { 'status' => 200, 'queries' => 5 })
  want('depdrive/queries: a 500 is not a well preloaded page',
       [got['voice_answers']['preloaded'], got['export_csv']['preloaded']],
       [false, true])
end

check 'depdrive/queries: a pass that never ran is not a pass' do
  got = DD.queries_summary({}, {})
  want('depdrive/queries: a pass that never ran is not a pass',
       [got['voice_answers']['status'], got['voice_answers']['queries'],
        got['voice_answers']['preloaded'], got['export_csv']['preloaded']],
       [nil, nil, false, false])
end

# Every path the pass drives is reported, whether or not it answered: a gate
# that read a missing key would score "the page was never fetched" as a pass.
check 'depdrive/queries: a path that did not answer is still reported' do
  got = DD.queries_summary(QUERIES_ROWS,
                           'voice_answers' => { 'status' => 200, 'queries' => 4 })
  want('depdrive/queries: a path that did not answer is still reported',
       [got.keys.sort, got['export_csv']['preloaded']],
       [['export_csv', 'voice_answers'], false])
end

# The counts are carried through beside the judgement, because when the gate
# fails the two numbers are what say whether one association was missed or all
# three.
check 'depdrive/queries: the counts are reported beside the judgement' do
  got = DD.queries_summary(QUERIES_ROWS, QUERIES_NPLUS1)
  want('depdrive/queries: the counts are reported beside the judgement',
       [got['voice_answers']['queries'], got['voice_answers']['rows'],
        got['export_csv']['queries'], got['export_csv']['rows']],
       [37, 12, 19, 13])
end

# The keys are names and the paths are values, because a gate's `path` is split
# on dots and `/export.csv` has one in it.  The page each reading is of is
# carried in the reading rather than in the key.
check 'depdrive/queries: the key is a name and the path is beside it' do
  got = DD.queries_summary(QUERIES_ROWS, QUERIES_FIXED)
  want('depdrive/queries: the key is a name and the path is beside it',
       [got.keys.none? { |k| k.include?('.') },
        got['voice_answers']['path'], got['export_csv']['path']],
       [true, '/voice_answers', '/export.csv'])
end


# `.load_async` is the one requirement in this ladder that no static reading
# can settle: the call site without an executor loads synchronously and says
# nothing, and the executor without a call site schedules nothing.  What
# separates them is an `sql.active_record` payload carrying `async: true`,
# published by Active Record 7.0's `FutureResult`, so the counter that reads
# it is rehearsed here against canned payloads -- including the two that are
# NOT async on a release that has no such key at all.
check 'depdrive/sql: an ordinary query counts once and is not async' do
  want('depdrive/sql: an ordinary query counts once and is not async',
       DD.sql_counts(name: 'Answer Load', sql: 'SELECT 1'), [true, false])
end

check 'depdrive/sql: an async query counts in both slots' do
  want('depdrive/sql: an async query counts in both slots',
       DD.sql_counts(name: 'Answer Load', async: true), [true, true])
end

# `async: false` is what Active Record 7.0 puts on a relation that was loaded
# synchronously -- the key is PRESENT and false.  A counter written as
# `if payload.key?(:async)` would score every query on a 7.0 tree as async and
# pass the gate on a tree that never wrote `.load_async` anywhere.
check 'depdrive/sql: a present-but-false async key is not async' do
  want('depdrive/sql: a present-but-false async key is not async',
       DD.sql_counts(name: 'Answer Load', async: false), [true, false])
end

# Every release before 7.0 has no `:async` key at all, and a missing key is
# not a finding -- it is 0 async queries, which is the true thing to say.
check 'depdrive/sql: a release with no async key reports none' do
  want('depdrive/sql: a release with no async key reports none',
       DD.sql_counts(name: 'Call Load'), [true, false])
end

# The framework's own bookkeeping is not a row's cost, async or not.  The
# second element still has to be false, because an uncounted query counted as
# async would make the gate pass on a tree whose only async query was a
# `SCHEMA` load the framework issued for itself.
check 'depdrive/sql: the bookkeeping queries are uncounted in both slots' do
  want('depdrive/sql: the bookkeeping queries are uncounted in both slots',
       [DD.sql_counts(name: 'SCHEMA'), DD.sql_counts(name: 'TRANSACTION'),
        DD.sql_counts(name: 'CACHE'), DD.sql_counts(name: 'SCHEMA', async: true)],
       [[false, false], [false, false], [false, false], [false, false]])
end

check 'depdrive/sql: a cached query is not a query the page sent' do
  want('depdrive/sql: a cached query is not a query the page sent',
       DD.sql_counts(name: 'Answer Load', cached: true, async: true),
       [false, false])
end

# The count is carried out of the summary beside the total, and a page that
# never answered reports 0 rather than nil, for the reason `preloaded` reports
# false: `at_least 1` against a missing reading is a gate that cannot fail on
# a page that was never fetched.
check 'depdrive/queries: the async count is reported beside the total' do
  got = DD.queries_summary(QUERIES_ROWS,
                           'voice_answers' => { 'status' => 200, 'queries' => 4,
                                                'async_queries' => 0 },
                           'export_csv' => { 'status' => 200, 'queries' => 5,
                                             'async_queries' => 3 })
  want('depdrive/queries: the async count is reported beside the total',
       [got['voice_answers']['async_queries'], got['export_csv']['async_queries']],
       [0, 3])
end

check 'depdrive/queries: a page that never answered reports 0 async, not nil' do
  got = DD.queries_summary(QUERIES_ROWS, {})
  want('depdrive/queries: a page that never answered reports 0 async, not nil',
       [got['voice_answers']['async_queries'], got['export_csv']['async_queries']],
       [0, 0])
end

# ---------------------------------------------------------------------------
# envboot: the environments no other gate boots
# ---------------------------------------------------------------------------

ENVPROBE = File.join(GATES, 'cvgate', 'envboot.rb')

# A stub `config/environment.rb` that reports which environment it was booted
# in, and fails in exactly the ones named.  `RAILS_ENV` is read from the
# process environment rather than from an argument because that is the only
# channel the probe has to the child: a probe that set a constant instead would
# be testing its own plumbing.
def stub_env_app(dir, envs, fails: [], kills: [])
  FileUtils.mkdir_p(File.join(dir, 'config', 'environments'))
  envs.each do |name|
    File.write(File.join(dir, 'config', 'environments', "#{name}.rb"), "# #{name}\n")
  end
  File.write(File.join(dir, 'config', 'environment.rb'), <<~RUBY)
    FAILS = #{fails.inspect}
    KILLS = #{kills.inspect}
    here = ENV['RAILS_ENV'].to_s

    # The one way a child can end without writing its report: straight out,
    # past every `ensure` and every `at_exit`, the way a segfault or an OOM
    # kill ends one.
    exit!(9) if KILLS.include?(here)
    raise "no such middleware for \#{here}" if FAILS.include?(here)

    module Rails
      module VERSION
        STRING = '5.0.7.2'
      end

      class Config
        def eager_load; false; end
      end

      Used = Struct.new(:name)

      class Stack
        def map; [Used.new('Rack::Sendfile'), Used.new('Rack::LiveReload')].map { |u| yield u }; end
      end

      class Application
        def config; @config ||= Config.new; end
        def middleware; @middleware ||= Stack.new; end
      end

      def self.application; @application ||= Application.new; end
      def self.env; ENV['RAILS_ENV'].to_s; end
    end
  RUBY
end

# Run it the way a verifier runs it: a subprocess, an `--out` file, and the
# exit status checked separately from the report.
def run_envprobe(dir, extra = [])
  out = File.join(dir, 'envboot.json')
  ok = system(RbConfig.ruby, ENVPROBE, '--app', dir, '--out', out, *extra,
              out: File::NULL, err: File::NULL)
  [ok, (JSON.parse(File.read(out)) if File.exist?(out))]
end

def with_env_app(envs, fails: [], kills: [], extra: [])
  Dir.mktmpdir do |dir|
    stub_env_app(dir, envs, fails: fails, kills: kills)
    yield(*run_envprobe(dir, extra))
  end
end

check 'envboot: the environments no other gate boots are the ones booted' do
  name = 'envboot: the environments no other gate boots are the ones booted'
  with_env_app(%w[development production test]) do |ok, report|
    want(name, ok, true)
    want("#{name} (declared)", report['declared'], %w[development production test])
    want("#{name} (checked)", report['checked'], %w[development test])
    want("#{name} (skipped)", report['skipped'], %w[production])
    want("#{name} (failed)", report['failed'], [])
  end
end

check 'envboot: production is named as skipped, not silently absent' do
  name = 'envboot: production is named as skipped, not silently absent'
  with_env_app(%w[development production test]) do |_ok, report|
    want(name, report['envs'].key?('production'), false)
    want("#{name} (said so)", report['skipped'].include?('production'), true)
  end
end

check 'envboot: each boot reports the environment it was actually given' do
  name = 'envboot: each boot reports the environment it was actually given'
  with_env_app(%w[development test]) do |_ok, report|
    want(name,
         report['envs'].map { |env, r| [env, r['rails_env']] }.sort,
         [%w[development development], %w[test test]])
  end
end

check 'envboot: the built middleware stack is recorded' do
  name = 'envboot: the built middleware stack is recorded'
  with_env_app(%w[development test]) do |_ok, report|
    want(name, report['envs']['development']['middleware'],
         ['Rack::Sendfile', 'Rack::LiveReload'])
  end
end

check 'envboot: one environment failing does not lose the others' do
  name = 'envboot: one environment failing does not lose the others'
  with_env_app(%w[development test], fails: %w[development]) do |ok, report|
    want(name, report['failed'], %w[development])
    want("#{name} (the other one booted)", report['envs']['test']['booted'], true)
    want("#{name} (exit status)", ok, true)
    unless report['envs']['development']['error'].to_s.include?('no such middleware')
      fail!(name, report['envs']['development']['error'].inspect)
    end
    if Array(report['envs']['development']['backtrace']).empty?
      fail!(name, 'no backtrace recorded')
    end
  end
end

check 'envboot: a child that dies without a report is a finding, not a crash' do
  name = 'envboot: a child that dies without a report is a finding, not a crash'
  with_env_app(%w[development test], kills: %w[test]) do |ok, report|
    want(name, ok, true)
    want("#{name} (failed)", report['failed'], %w[test])
    want("#{name} (booted)", report['envs']['test']['booted'], false)
    unless report['envs']['test']['error'].to_s.include?('wrote no report')
      fail!(name, report['envs']['test']['error'].inspect)
    end
    want("#{name} (the other one booted)",
         report['envs']['development']['booted'], true)
  end
end

# The deletion hole.  `scan.rb`'s pristine manifest covers `app/` and `lib/`,
# not `config/`, so "every environment this tree declares boots" is true of a
# tree with no development environment in it at all.
check 'envboot: an expected environment file that is gone is a failure' do
  name = 'envboot: an expected environment file that is gone is a failure'
  with_env_app(%w[test]) do |_ok, report|
    want(name, report['failed'], %w[development])
    want("#{name} (why)", report['envs']['development']['error'],
         'no config/environments/development.rb')
  end
end

check 'envboot: an environment the tree added is checked too' do
  name = 'envboot: an environment the tree added is checked too'
  with_env_app(%w[development staging test], fails: %w[staging]) do |_ok, report|
    want(name, report['checked'], %w[development staging test])
    want("#{name} (failed)", report['failed'], %w[staging])
  end
end

check 'envboot: --expect and --skip are what they say' do
  name = 'envboot: --expect and --skip are what they say'
  with_env_app(%w[development production test],
               extra: ['--expect', 'development', '--skip', 'test,production']) do |_ok, report|
    want(name, report['checked'], %w[development])
  end
end

check 'envboot: an application with no environment.rb reports why' do
  name = 'envboot: an application with no environment.rb reports why'
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'config', 'environments'))
    %w[development test].each do |env|
      File.write(File.join(dir, 'config', 'environments', "#{env}.rb"), "\n")
    end
    ok, report = run_envprobe(dir)
    want(name, [ok, report['failed']], [true, %w[development test]])
    unless report['envs']['test']['error'].to_s.include?('no config/environment.rb')
      fail!(name, report['envs']['test']['error'].inspect)
    end
  end
end

# The selection the gate reads, driven directly: it is one `select`, and it is
# the whole difference between a gate about this application's configuration
# and a gate about which gems the resolver picked.
check 'envboot: only the environment files are gated deprecations' do
  name = 'envboot: only the environment files are gated deprecations'
  want(name,
       CVGate::EnvBoot.env_file_only(
         ["config/application.rb:12:in `<top (required)>'",
          "config/environments/test.rb:16:in `block in <top (required)>'",
          "config/initializers/teaspoon.rb:3:in `<top (required)>'",
          "app/models/location.rb:9:in `<class:Location>'"]
       ),
       ["config/environments/test.rb:16:in `block in <top (required)>'"])
end

check 'envboot: the same call site twice is one finding' do
  name = 'envboot: the same call site twice is one finding'
  where = "config/environments/test.rb:16:in `block in <top (required)>'"
  want(name, CVGate::EnvBoot.env_file_only([where, where]), [where])
end

check 'envboot: a clean tree has nothing to report' do
  name = 'envboot: a clean tree has nothing to report'
  want(name,
       CVGate::EnvBoot.env_file_only(
         ["config/application.rb:12:in `<top (required)>'"]
       ),
       [])
end


# ---------------------------------------------------------------------------
# bootprobe: the libraries the interpreter is about to stop shipping
# ---------------------------------------------------------------------------
#
# Driven against canned lines rather than against a boot, for the same reason
# `env_file_only` is: the selection is the whole check, and it has to read the
# same on an interpreter that never emits one of these as on the one that does.
# The real lines below are copied from a measured boot.

OSTRUCT_WARNING =
  '/gems/bootsnap-1.9.3/lib/bootsnap/load_path_cache/core_ext/kernel_require.rb:23: ' \
  'warning: /opt/rubies/3.4.10/lib/ruby/3.4.0/ostruct.rb was loaded from the ' \
  'standard library, but will no longer be part of the default gems starting ' \
  'from Ruby 4.0.0.'

check 'bootprobe: a default-gem warning is reported by library, not by path' do
  name = 'bootprobe: a default-gem warning is reported by library, not by path'
  want(name, CVGate::BootProbe.bundled_gem_only([OSTRUCT_WARNING]), ['ostruct'])
end

check 'bootprobe: two libraries, deduplicated and sorted' do
  name = 'bootprobe: two libraries, deduplicated and sorted'
  csv = OSTRUCT_WARNING.gsub('ostruct', 'csv').gsub('4.0.0', '3.4.0')
  want(name,
       CVGate::BootProbe.bundled_gem_only([OSTRUCT_WARNING, csv, OSTRUCT_WARNING]),
       %w[csv ostruct])
end

check 'bootprobe: the allocator notice is not a default-gem warning' do
  # The near miss that matters on this ladder: redcarpet 3.6.1 is a C extension
  # that says this on every 3.4 boot that loads it, and no rung of this ladder
  # moves redcarpet.  A rule that fired on it would fail every correct tree.
  name = 'bootprobe: the allocator notice is not a default-gem warning'
  want(name,
       CVGate::BootProbe.bundled_gem_only(
         ['/x.rb:1: warning: undefining the allocator of T_DATA class ' \
          'Redcarpet::Markdown']
       ),
       [])
end

check 'bootprobe: a deprecation is not a default-gem warning' do
  name = 'bootprobe: a deprecation is not a default-gem warning'
  want(name,
       CVGate::BootProbe.bundled_gem_only(
         ['DEPRECATION WARNING: ostruct.rb is deprecated (called from x.rb:1)']
       ),
       [])
end

check 'bootprobe: nothing to read reads empty, not nil' do
  # Ruby 2.3 has no `Warning` module, so `capture_warnings` returns without
  # installing anything and the list stays empty for the whole run.  The key is
  # still written, and it is an Array on every interpreter in the ladder.
  name = 'bootprobe: nothing to read reads empty, not nil'
  want(name, CVGate::BootProbe.bundled_gem_only([]), [])
  want(name + ' (nil)', CVGate::BootProbe.bundled_gem_only(nil), [])
end


# ---------------------------------------------------------------------------
# bundledgems: the same question asked of the load path instead of the channel
# ---------------------------------------------------------------------------
#
# The gate this ladder actually stands on, and the reason it is not the block
# above: the sentences are printed by `Kernel#warn`, which a tree can turn off
# without changing a thing about what it loads (measured: zero lines collected
# on a tree still loading stdlib `ostruct`, under
# `RUBYOPT=-W0`, under `$VERBOSE = nil`, and behind a `Warning` prepend that
# does not call `super`).  `select` is a pure function of four arrays so that
# these fixtures read the same on the 2.3 end of the ladder, where there is no
# `Gem::BUNDLED_GEMS` at all, as on the 3.4 end where the gate is live.
#
# The paths are the ones measured on a real 3.4.10 boot, for the stdlib copy
# and for the gem copy.

LIBDIR = '/opt/rubies/3.4.10/lib/ruby/3.4.0'
ARCH = 'x86_64-linux'
SINCE_NAMES = %w[bigdecimal csv logger mutex_m net-smtp observer ostruct racc].freeze

def stdlib_only(features, activated = [], names = SINCE_NAMES)
  CVGate::BundledGems.select(features, LIBDIR, ARCH, names, activated)
end

check 'bundledgems: a library loaded out of the interpreter is reported' do
  name = 'bundledgems: a library loaded out of the interpreter is reported'
  want(name, stdlib_only(["#{LIBDIR}/ostruct.rb"]), ['ostruct'])
end

check 'bundledgems: the same library out of the bundle is not' do
  # The declared tree.  This is the fix the step asks for, and the reading has
  # to see it whether or not anything was printed while it happened.
  name = 'bundledgems: the same library out of the bundle is not'
  want(name,
       stdlib_only(['/app/vendor/bundle/ruby/3.4.0/gems/ostruct-0.6.1/lib/ostruct.rb']),
       [])
end

check 'bundledgems: declared and activated clears it even from the interpreter copy' do
  # `gem "ostruct"` with no version can resolve to the copy the interpreter
  # already ships, in which case the file is under `rubylibdir` and the name is
  # in `Gem.loaded_specs`.  That tree has done what the step asks.
  name = 'bundledgems: declared and activated clears it even from the interpreter copy'
  want(name, stdlib_only(["#{LIBDIR}/ostruct.rb"], %w[ostruct rails]), [])
end

check 'bundledgems: a dashed name is a directory on disk' do
  name = 'bundledgems: a dashed name is a directory on disk'
  want(name, stdlib_only(["#{LIBDIR}/net/smtp.rb"]), ['net-smtp'])
end

check 'bundledgems: a file inside a listed library is not the library' do
  # `racc` is on 3.4's list; `racc/parser.rb` is what psych loads and is not
  # the bundled gem.  The comparison is exact for that reason.
  name = 'bundledgems: a file inside a listed library is not the library'
  want(name, stdlib_only(["#{LIBDIR}/racc/parser.rb"]), [])
end

check 'bundledgems: a C extension under the arch directory is read by name' do
  name = 'bundledgems: a C extension under the arch directory is read by name'
  want(name, stdlib_only(["#{LIBDIR}/#{ARCH}/bigdecimal.so"]), ['bigdecimal'])
end

check 'bundledgems: anything outside the interpreter lib directory is not read' do
  name = 'bundledgems: anything outside the interpreter lib directory is not read'
  want(name, stdlib_only(['/usr/lib/ruby/3.4.0/ostruct.rb', '/app/lib/csv.rb']), [])
end

check 'bundledgems: two libraries, deduplicated and sorted' do
  name = 'bundledgems: two libraries, deduplicated and sorted'
  want(name,
       stdlib_only(["#{LIBDIR}/ostruct.rb", "#{LIBDIR}/csv.rb", "#{LIBDIR}/ostruct.rb"]),
       %w[csv ostruct])
end

check 'bundledgems: an interpreter with no list reads empty for every tree' do
  # 2.1 through 3.2 have no `Gem::BUNDLED_GEMS`.  The reading is the empty list
  # and not a crash and not a finding: nothing is leaving the default set on an
  # interpreter that has not started moving anything.
  name = 'bundledgems: an interpreter with no list reads empty for every tree'
  want(name, stdlib_only(["#{LIBDIR}/ostruct.rb"], [], nil), [])
end

check 'bundledgems: no library directory is no reading' do
  name = 'bundledgems: no library directory is no reading'
  want(name, CVGate::BundledGems.select(["#{LIBDIR}/ostruct.rb"], '', ARCH, SINCE_NAMES, []), [])
  want(name + ' (nil)',
       CVGate::BundledGems.select(["#{LIBDIR}/ostruct.rb"], nil, ARCH, SINCE_NAMES, []), [])
end

check 'bundledgems: this interpreter answers its own question' do
  # The live reading, on whichever interpreter is running this file.  What is
  # asserted is the shape and the closure, not a particular list: `names` is
  # nil below 3.3 and an Array of Strings from 3.3, `read` is always an Array,
  # and nothing can come out of `read` that the interpreter did not name.
  name = 'bundledgems: this interpreter answers its own question'
  names = CVGate::BundledGems.names
  unless names.nil? || (names.is_a?(Array) && names.all? { |n| n.is_a?(String) })
    fail!(name, "names is neither nil nor an Array of String: #{names.inspect}")
  end
  want(name + ' (known?)', CVGate::BundledGems.known?, !names.nil?)
  got = CVGate::BundledGems.read
  if got.is_a?(Array)
    want(name + ' (closed over the list)', got - Array(names), [])
  else
    fail!(name, "read did not answer with an Array: #{got.inspect}")
  end
end


# ---------------------------------------------------------------------------
# rspecdep: the same collector, inside the suite's process
# ---------------------------------------------------------------------------

RD = CVGate::RspecDep

def with_sink
  Dir.mktmpdir('cvgate-rspecdep') do |dir|
    path = File.join(dir, 'rspec_deprecations.jsonl')
    yield RD::Sink.new(path), path
  end
end

def lines_of(path)
  return [] unless File.file?(path)

  File.readlines(path).map(&:strip).reject(&:empty?).map { |l| JSON.parse(l) }
end

check 'rspecdep: a record becomes one JSON line' do
  with_sink do |sink, path|
    sink << { 'message' => 'x', 'where' => 'spec/a_spec.rb:1' }
    want('rspecdep: a record becomes one JSON line', lines_of(path),
         [{ 'message' => 'x', 'where' => 'spec/a_spec.rb:1' }])
  end
end

# Appended as they arrive and not dumped at exit, so that a suite killed by the
# step's timeout still leaves behind what it had already raised.
check 'rspecdep: records accumulate rather than overwrite' do
  with_sink do |sink, path|
    3.times { |i| sink << { 'message' => "m#{i}", 'where' => 'spec/a_spec.rb:1' } }
    want('rspecdep: records accumulate rather than overwrite',
         lines_of(path).map { |r| r['message'] }, %w[m0 m1 m2])
  end
end

# A newline inside the message must not become a second record: the reader
# parses one object per line and a torn object is a lost finding.
check 'rspecdep: a multi-line message stays on one line' do
  with_sink do |sink, path|
    sink << { 'message' => "one\ntwo", 'where' => 'spec/a_spec.rb:1' }
    want('rspecdep: a multi-line message stays on one line',
         [File.readlines(path).length, lines_of(path).first['message']],
         [1, "one\ntwo"])
  end
end

# The sink runs inside `warn`.  If it raised, every deprecation in the suite
# would become an exception in the example that triggered it -- the candidate
# charged for this file's bug, on a tree that did nothing wrong.
check 'rspecdep: an unwritable sink does not raise' do
  sink = RD::Sink.new('/nonexistent-directory-cvgate/out.jsonl')
  got = begin
    sink << { 'message' => 'x' }
    'returned'
  rescue StandardError => e
    "raised #{e.class}"
  end
  want('rspecdep: an unwritable sink does not raise', got, 'returned')
end

check 'rspecdep: the sink returns itself, the way an array would' do
  with_sink do |sink, _path|
    want('rspecdep: the sink returns itself, the way an array would',
         sink.<<({ 'message' => 'x' }).equal?(sink), true)
  end
end

# The guard.  `bundle exec` reads RUBYOPT in the process that has not called
# `Bundler.setup` yet, and requiring activesupport there activates whatever
# version the system holds -- after which bundler aborts the command.  Measured
# with bundler 1.17.3 on Ruby 2.4.10: a `-r` file is loaded once before setup
# under the default `kernel_load`, and twice (once before, once after) with
# `BUNDLE_DISABLE_EXEC_LOAD=true`.  So "ready" means the bundle exists.
check 'rspecdep: not ready before bundler/setup' do
  want('rspecdep: not ready before bundler/setup', RD.ready?, false)
end

check 'rspecdep: ready once bundler/setup has been loaded' do
  $LOADED_FEATURES << '/x/gems/bundler-1.17.3/lib/bundler/setup.rb'
  got = RD.ready?
  $LOADED_FEATURES.pop
  want('rspecdep: ready once bundler/setup has been loaded', got, true)
end

check 'rspecdep: install refuses without an output path' do
  want('rspecdep: install refuses without an output path',
       [RD.install(nil, '/app'), RD.install('', '/app')], [false, false])
end

check 'rspecdep: install refuses before the bundle exists' do
  with_sink do |_sink, path|
    want('rspecdep: install refuses before the bundle exists',
         [RD.install(path, '/app'), File.exist?(path)], [false, false])
  end
end

# From here on there is an `ActiveSupport::Deprecation` to prepend to, so
# `ready?` is true and the two fixtures above cannot be moved below this line.
module ActiveSupport
  class Deprecation
    class << self
      attr_reader :seen
      attr_accessor :silenced

      def warn(message = nil, _callstack = nil, *_rest)
        @seen ||= []
        @seen << message
        :underneath
      end

      # Rails' own: set the flag, yield, put the flag back.  The collector
      # prepends to this to find out *who* opened the block.
      def silence
        old = @silenced
        @silenced = true
        yield
      ensure
        @silenced = old
      end
    end

    def warn(message = nil, _callstack = nil, *_rest)
      :instance_underneath
    end

    def silence
      yield
    end
  end
end

INSTALL_LOG = File.join(Dir.mktmpdir('cvgate-rspecdep-install'), 'dep.jsonl')

check 'rspecdep: install writes an install line and reports itself' do
  want('rspecdep: install writes an install line and reports itself',
       [RD.install(INSTALL_LOG, '/app'),
        lines_of(INSTALL_LOG).map { |r| r['event'] }],
       [true, ['install']])
end

# The install line is what `test.sh` turns into "a collector was in the
# process".  Without it an empty report reads as a clean tree, which is the
# free point the null exists to refuse.
check 'rspecdep: the install line names the interpreter' do
  want('rspecdep: the install line names the interpreter',
       lines_of(INSTALL_LOG).first['ruby'], RUBY_VERSION)
end

check 'rspecdep: a warn after install lands on disk with its call site' do
  ActiveSupport::Deprecation.warn(
    '`#deliver` is deprecated', ['/app/spec/models/a_spec.rb:9:in `block\''])
  record = lines_of(INSTALL_LOG).find { |r| r['event'].nil? }
  want('rspecdep: a warn after install lands on disk with its call site',
       [record['message'], record['where']],
       ['`#deliver` is deprecated', 'spec/models/a_spec.rb:9:in `block\''])
end

check 'rspecdep: the method underneath still runs' do
  want('rspecdep: the method underneath still runs',
       ActiveSupport::Deprecation.seen.length >= 1, true)
end

# Loaded twice -- once from RUBYOPT, once from rspec's `--require` -- is the
# ordinary case, not an error, and the second load must not prepend a second
# collector or write a second install line.
check 'rspecdep: install is idempotent' do
  before = lines_of(INSTALL_LOG).length
  got = RD.install(INSTALL_LOG, '/app')
  want('rspecdep: install is idempotent',
       [got, lines_of(INSTALL_LOG).length - before], [false, 0])
end

# Silence attribution.  Recording upstream of `return if silenced` also
# records what Rails silences for itself: on the step-02 golden tree the two
# `#table_exists?` warnings `ActiveRecord::Migration.check_pending!` raises
# inside its own `Deprecation.silence`, from `spec_helper.rb:19`, which blamed
# to the first application frame are blamed on `spec/` and score the golden
# 0.979070.  So the question the collector asks is not "is anything silenced"
# but "who opened the block", and these fixtures are that question from both
# sides.

check 'rspecdep: a frame is cut at its line number, not at its last colon' do
  want('rspecdep: a frame is cut at its line number, not at its last colon',
       RD.path_of('/app/spec/a_spec.rb:19:in `block (2 levels) in <top>\''),
       '/app/spec/a_spec.rb')
end

check 'rspecdep: a frame with no line number is kept whole' do
  want('rspecdep: a frame with no line number is kept whole',
       RD.path_of('/app/spec/a_spec.rb'), '/app/spec/a_spec.rb')
end

check 'rspecdep: the tree owns its own files' do
  want('rspecdep: the tree owns its own files',
       RD.under_app?('/app/spec/spec_helper.rb:19:in `<top (required)>\''), true)
end

check 'rspecdep: the tree does not own a gem frame' do
  want('rspecdep: the tree does not own a gem frame',
       RD.under_app?('/bundle/gems/activerecord-5.0.7.2/lib/active_record/' \
                     'migration.rb:1029:in `get_all_versions\''), false)
end

# A vendored bundle lives *inside* the tree and is still not the tree: the same
# exclusion `DepDrive.blame` makes, for the same reason.
check 'rspecdep: a vendored gem is not the tree either' do
  want('rspecdep: a vendored gem is not the tree either',
       RD.under_app?('/app/vendor/bundle/ruby/2.4.0/gems/x-1.0/lib/x.rb:3:in `y\''),
       false)
end

check 'rspecdep: nothing open is not silence' do
  want('rspecdep: nothing open is not silence',
       [RD.silencers, RD.framework_silenced?], [[], false])
end

check 'rspecdep: a block only the framework opened is the framework\'s' do
  RD.enter_silence('/bundle/gems/activerecord-5.0.7.2/lib/active_record/' \
                   'migration.rb:1029:in `get_all_versions\'')
  got = RD.framework_silenced?
  RD.leave_silence
  want('rspecdep: a block only the framework opened is the framework\'s',
       [got, RD.silencers.length], [true, 0])
end

# The attack this rule has to survive: the tree wraps the suite in a silence of
# its own, and the framework opens one of its own inside it.  The innermost
# block is the framework's, and the finding is still the tree's.
check 'rspecdep: a block the tree opened is the tree\'s, however deeply nested' do
  RD.enter_silence('/app/spec/spec_helper.rb:3:in `block in <top (required)>\'')
  RD.enter_silence('/bundle/gems/activerecord-5.0.7.2/lib/active_record/' \
                   'migration.rb:1029:in `get_all_versions\'')
  got = RD.framework_silenced?
  2.times { RD.leave_silence }
  want('rspecdep: a block the tree opened is the tree\'s, however nested',
       [got, RD.silencers.length], [false, 0])
end

check 'rspecdep: a warning the framework silenced is counted, not recorded' do
  before = lines_of(INSTALL_LOG).length
  seen = ActiveSupport::Deprecation.seen.length
  ActiveSupport::Deprecation.silence do
    ActiveSupport::Deprecation.warn(
      '#table_exists? currently checks both tables and views',
      ['/bundle/gems/activerecord-5.0.7.2/lib/active_record/migration.rb:1030',
       '/app/spec/spec_helper.rb:19:in `<top (required)>\''])
  end
  added = lines_of(INSTALL_LOG)[before..-1]
  want('rspecdep: a warning the framework silenced is counted, not recorded',
       [added.map { |r| r['event'] },
        added.map { |r| r['message'] }.compact,
        ActiveSupport::Deprecation.seen.length - seen],
       [['framework_silenced'], [], 1])
end

# Named by the block that decided, which is the outermost one open: the class
# method delegates to the instance, so a framework `silence` is pushed twice
# and the innermost frame is always activesupport's own delegator.  Measured on
# the step-02 golden tree: reported as the delegator, the evidence for all four of the
# run's skipped records read `instance_delegator.rb` and said nothing about
# `ActiveRecord::Migration`.
check 'rspecdep: the skipped record names the block that decided' do
  before = lines_of(INSTALL_LOG).length
  RD.enter_silence('/bundle/gems/activerecord-5.0.7.2/lib/active_record/' \
                   'migration.rb:1029:in `get_all_versions\'')
  RD.enter_silence('/bundle/gems/activesupport-5.0.7.2/lib/active_support/' \
                   'deprecation/instance_delegator.rb:20:in `silence\'')
  ActiveSupport::Deprecation.warn('#table_exists?', ['/app/spec/spec_helper.rb:19'])
  2.times { RD.leave_silence }
  want('rspecdep: the skipped record names the block that decided',
       lines_of(INSTALL_LOG)[before..-1].map { |r| r['by'] },
       ['/bundle/gems/activerecord-5.0.7.2/lib/active_record/migration.rb'])
end

# The same warning, the same call site, the same `silence` -- opened by the
# tree.  Without this fixture the one above is satisfied by a collector that
# records nothing whenever anything is silenced, which is the hole this whole
# file exists to close.
check 'rspecdep: the same warning inside the tree\'s own silence is recorded' do
  before = lines_of(INSTALL_LOG).length
  RD.enter_silence('/app/spec/spec_helper.rb:3:in `block in <top (required)>\'')
  ActiveSupport::Deprecation.warn(
    '#table_exists? currently checks both tables and views',
    ['/bundle/gems/activerecord-5.0.7.2/lib/active_record/migration.rb:1030',
     '/app/spec/spec_helper.rb:19:in `<top (required)>\''])
  RD.leave_silence
  added = lines_of(INSTALL_LOG)[before..-1]
  want('rspecdep: the same warning inside the tree\'s own silence is recorded',
       [added.length, added.first['where']],
       [1, 'spec/spec_helper.rb:19:in `<top (required)>\''])
end

# `silenced = true` as a flag opens no block and credits no caller, so it
# suppresses nothing here -- the other door out of the same room.
check 'rspecdep: the silenced flag alone suppresses nothing' do
  before = lines_of(INSTALL_LOG).length
  ActiveSupport::Deprecation.silenced = true
  ActiveSupport::Deprecation.warn('`#deliver` is deprecated',
                                  ['/app/spec/models/a_spec.rb:9:in `block\''])
  ActiveSupport::Deprecation.silenced = nil
  added = lines_of(INSTALL_LOG)[before..-1]
  want('rspecdep: the silenced flag alone suppresses nothing',
       [added.length, added.first['where']],
       [1, 'spec/models/a_spec.rb:9:in `block\''])
end

# The stack has to come back empty however the block left, or one raising
# example turns every later deprecation in the run into the framework's.
check 'rspecdep: a silence that raises still leaves the stack empty' do
  begin
    ActiveSupport::Deprecation.silence { raise 'boom' }
  rescue RuntimeError # rubocop:disable Lint/SuppressedException
  end
  want('rspecdep: a silence that raises still leaves the stack empty',
       RD.silencers, [])
end

# ---------------------------------------------------------------------------
# depdrive: reading a strict-locals declaration
# ---------------------------------------------------------------------------
#
# `Template#strict_locals!` hands back the text BETWEEN the parentheses --
# actionview's STRICT_LOCALS_REGEX (template.rb:11) captures inside them -- so
# a reader that expects to see them reads no names at all, supplies no locals,
# and every probe of a partial with a required local comes back
# `missing local:` instead of `unknown local:`.  That is a refusal of the
# CALL, not of an undeclared name, and it would have scored a tree that
# declares nothing the same as one that declares everything.  So the names are
# parsed, with Ripper, out of the only thing a strict-locals declaration is:
# a method signature.

check 'depdrive: a required local is read out of the declaration' do
  want('depdrive: a required local is read out of the declaration',
       CVGate::DepDrive.strict_local_names('content:'), [:content])
end

check 'depdrive: optional locals keep their order and lose their defaults' do
  want('depdrive: optional locals keep their order and lose their defaults',
       CVGate::DepDrive.strict_local_names('title: nil, html_class: nil'),
       %i[title html_class])
end

# The declaration actionview compiles for `<%# locals: () %>`: a signature
# that accepts nothing, which is a strict template with no locals and not a
# template that failed to parse.
check 'depdrive: the empty signature accepts no locals' do
  want('depdrive: the empty signature accepts no locals',
       CVGate::DepDrive.strict_local_names('**nil'), [])
end

# A default value is an expression, and an expression can carry labels and
# parameters of its own.  Parsed rather than scanned, neither reaches the
# list -- a regex over `\w+:` reads `b` and `c` here.
check 'depdrive: a hash default does not contribute its keys' do
  want('depdrive: a hash default does not contribute its keys',
       CVGate::DepDrive.strict_local_names('a: {b: 1, c: 2}'), [:a])
end

check 'depdrive: a lambda default does not contribute its parameters' do
  want('depdrive: a lambda default does not contribute its parameters',
       CVGate::DepDrive.strict_local_names('a: ->(x) { x }, b:'), %i[a b])
end

check 'depdrive: nothing declared is no names' do
  want('depdrive: nothing declared is no names',
       [CVGate::DepDrive.strict_local_names(''),
        CVGate::DepDrive.strict_local_names('   '),
        CVGate::DepDrive.strict_local_names(nil)],
       [[], [], []])
end

# A partial whose comment is not a signature at all.  The probe still has to
# return a value: it runs against the candidate's tree, and an exception here
# would lose the whole pass rather than one partial's reading.
check 'depdrive: an unparseable declaration is no names, not an exception' do
  want('depdrive: an unparseable declaration is no names, not an exception',
       CVGate::DepDrive.strict_local_names('garbage ( ['), [])
end

# The two messages that mean the signature turned an undeclared local away,
# and the two that do not.  `missing local:` is the one that matters: it is
# what a partial with a required local says when the probe supplied nothing,
# and counting it would make `refuses_unknown` true for a tree whose
# declarations the probe never managed to read.
check 'depdrive: actionview\'s two refusals are refusals' do
  want('depdrive: actionview\'s two refusals are refusals',
       ['ActionView::Template::Error: unknown local: :cv_depdrive_strict_probe',
        'ActionView::Template::Error: no locals accepted for this template'
       ].map { |m| (m =~ CVGate::DepDrive::STRICT_REFUSAL ? true : false) },
       [true, true])
end

check 'depdrive: a missing local is not a refusal of an undeclared one' do
  want('depdrive: a missing local is not a refusal of an undeclared one',
       ['ActionView::Template::Error: missing local: :content',
        'ActionView::Template::Error: undefined local variable or method ' \
        '`content\' for an instance of ActionView::Base',
        'ActionView::Template::Error: undefined method `each\' for nil'
       ].map { |m| (m =~ CVGate::DepDrive::STRICT_REFUSAL ? true : false) },
       [false, false, false])
end


# ---------------------------------------------------------------------------
# rspecdep: rspec's own deprecations, which ActiveSupport never sees
# ---------------------------------------------------------------------------
#
# rspec-rails 6.1.5 keeps `config.fixture_path=` working on Rails 7.1 by
# raising `RSpec.deprecate` and assigning the plural setting for you
# (lib/rspec/rails/configuration.rb:167-188).  That never reaches
# `ActiveSupport::Deprecation`, so every reading above it is blind to it and a
# tree that never renames the setting scores as a tree that did.  These
# fixtures are the hook that closes it, exercised against a stand-in for rspec
# -- there is no rspec in this container, and a hook that has only ever run
# against the real gem is a hook nobody has checked.

check 'rspecdep: with no rspec in the process there is nothing to hook' do
  want('rspecdep: with no rspec in the process there is nothing to hook',
       [defined?(::RSpec) ? true : false, RD.install_rspec,
        lines_of(INSTALL_LOG).any? { |r| r['event'] == 'rspec_install' }],
       [false, false, false])
end

# The stand-in.  `deprecate` and `warn_deprecation` are the two module methods
# rspec raises through; both return a value of their own so that "the method
# underneath still ran" is observable rather than assumed.
module RSpec
  module Core
    module Version
      STRING = '3.13.6'
    end
  end

  @deprecated = []

  class << self
    attr_reader :deprecated

    def deprecate(message, options = {})
      @deprecated << [message, options]
      :underneath
    end

    def warn_deprecation(message, options = {})
      @deprecated << [message, options]
      :underneath
    end
  end
end

# Nothing above called `install_rspec`: the `TracePoint(:end)` armed when this
# file required `cvgate/rspecdep` put the hook in as the module body closed,
# which is the mechanism the real run depends on -- rspec does not exist when
# RUBYOPT loads the collector, and waiting for rspec's own `--require` would
# be too late for a tree with a `.rspec` of its own.
check 'rspecdep: the watch installs the hook the moment rspec appears' do
  installed = lines_of(INSTALL_LOG).select { |r| r['event'] == 'rspec_install' }
  want('rspecdep: the watch installs the hook the moment rspec appears',
       [installed.length, installed.first && installed.first['rspec'],
        installed.first && installed.first['late'],
        ::RSpec.singleton_class.ancestors.include?(RD::RSpecCollector)],
       [1, '3.13.6', false, true])
end

check 'rspecdep: a deprecation rspec raised lands on disk with its call site' do
  before = lines_of(INSTALL_LOG).length
  got = ::RSpec.deprecate('config.fixture_path = "/app/spec/fixtures"',
                          call_site: '/app/spec/spec_helper.rb:38')
  added = lines_of(INSTALL_LOG)[before..-1]
  want('rspecdep: a deprecation rspec raised lands on disk with its call site',
       [added.map { |r| r['event'] }, added.first['message'],
        added.first['where'], got],
       [['rspec_deprecation'], 'config.fixture_path = "/app/spec/fixtures"',
        'spec/spec_helper.rb:38', :underneath])
end

check 'rspecdep: the other door raises through is hooked too' do
  before = lines_of(INSTALL_LOG).length
  ::RSpec.warn_deprecation('fixture_path is deprecated',
                           call_site: '/app/spec/rails_helper.rb:4')
  want('rspecdep: the other door raises through is hooked too',
       lines_of(INSTALL_LOG)[before..-1].map { |r| r['where'] },
       ['spec/rails_helper.rb:4'])
end

# The partition `test.sh` scores on.  rspec computes the call site with
# `CallerFilter.first_non_rspec_line`, so a deprecation one gem raises about
# another is attributed to the gem -- absolute, outside the tree, and not the
# candidate's to fix.
check 'rspecdep: a call site inside a gem stays absolute' do
  before = lines_of(INSTALL_LOG).length
  ::RSpec.deprecate('RSpec::Rails::FixtureSupport',
                    call_site: '/bundle/gems/rspec-rails-6.1.5/lib/x.rb:82')
  want('rspecdep: a call site inside a gem stays absolute',
       lines_of(INSTALL_LOG)[before..-1].map { |r| r['where'] },
       ['/bundle/gems/rspec-rails-6.1.5/lib/x.rb:82'])
end

# No call site in the payload at all -- an older rspec, or a caller that built
# the options itself.  The record still has to name somewhere, and the
# somewhere is the same `blame` the Rails collector uses, so a frame outside
# the tree is reported outside the tree rather than blamed on `spec/`.
check 'rspecdep: a deprecation with no call site is still placed' do
  before = lines_of(INSTALL_LOG).length
  ::RSpec.deprecate('something with no payload')
  added = lines_of(INSTALL_LOG)[before..-1]
  want('rspecdep: a deprecation with no call site is still placed',
       [added.length, added.first['where'].to_s.empty?,
        added.first['where'].to_s.start_with?('/')],
       [1, false, true])
end

check 'rspecdep: the method underneath kept every message' do
  want('rspecdep: the method underneath kept every message',
       ::RSpec.deprecated.map(&:first),
       ['config.fixture_path = "/app/spec/fixtures"',
        'fixture_path is deprecated', 'RSpec::Rails::FixtureSupport',
        'something with no payload'])
end

# rspec 2.14 spells it `deprecate(method, alternate_method = nil)`: the second
# argument is a String and not an options hash.  Steps 01 through 04 run that
# rspec, and this file is shipped into every step's payload, so a hook that
# assumed a Hash would raise inside the first deprecation those suites raise.
check 'rspecdep: rspec 2 passes a String where rspec 3 passes options' do
  before = lines_of(INSTALL_LOG).length
  ::RSpec.deprecate('the old spelling', 'the new one')
  added = lines_of(INSTALL_LOG)[before..-1]
  want('rspecdep: rspec 2 passes a String where rspec 3 passes options',
       [added.length, added.first['message'],
        added.first['where'].to_s.start_with?('/')],
       [1, 'the old spelling', true])
end

# The watch fires on every class and module body that closes after rspec is
# loaded, and the suite defines a great many.  A second prepend would double
# every record.
check 'rspecdep: the rspec hook is installed once' do
  before = lines_of(INSTALL_LOG).length
  got = [RD.install_rspec, RD.watch_for_rspec]
  ::RSpec.deprecate('once', call_site: '/app/spec/a_spec.rb:1')
  added = lines_of(INSTALL_LOG)[before..-1]
  want('rspecdep: the rspec hook is installed once',
       [got, added.length], [[false, true], 1])
end

# The one ordering in which the reading would be a lie: a `.rspec` of the
# tree's own naming `spec_helper`, which rspec merges ahead of the command
# line, so the `RSpec.configure` block runs before the hook is in.  The
# install line carries the answer, and `test.sh` turns `late` into a null
# reading rather than a zero.  This repository has no `.rspec` at all.
check 'rspecdep: a suite helper already loaded is a late hook' do
  before = RD.loaded_suite_helper?
  $LOADED_FEATURES << '/app/spec/spec_helper.rb'
  during = RD.loaded_suite_helper?
  $LOADED_FEATURES.pop
  want('rspecdep: a suite helper already loaded is a late hook',
       [before, during, RD.loaded_suite_helper?], [false, true, false])
end

# ---------------------------------------------------------------------------
# raketasks: does the task tree load
# ---------------------------------------------------------------------------

# Real `rake -T` output, trimmed: the shape is `rake <task>[ <args>]   # <desc>`
# and the first line is the allocator warning this image's 2.x interpreters
# print on stdout, which is not a task.
RAKE_T = <<~OUT
  rake app:template                     # Apply the template supplied
  rake assets:precompile                # Compile all the assets
  rake db:migrate                       # Migrate the database
  rake db:migrate:status                # Display status of migrations
  rake import:locations[path]           # Import locations from a CSV
  rake annotate_models                  # Add schema information
  rake annotate_routes                  # Add the routes to routes.rb
  rake remove_annotation                # Remove schema information
OUT

# What rake prints when the loader dies: nothing on stdout, and the error on
# stderr.  Quoted from a run on the step-10 golden tree with the retired gem's
# 2014 rake file put back.
RAKE_ABORTED = <<~ERR
  rake aborted!
  NameError: uninitialized constant Annotate (NameError)
    Annotate.load_tasks
    ^^^^^^^^
  Did you mean?  AnnotateRb
  /app/lib/tasks/auto_annotate_models.rake:33:in `<main>'
ERR

check 'raketasks: a task line is parsed to its name' do
  want('raketasks: a task line is parsed to its name',
       CVGate::RakeTasks.parse(RAKE_T).first(3),
       %w[app:template assets:precompile db:migrate])
end

check 'raketasks: the argument brackets stay with the name' do
  want('raketasks: the argument brackets stay with the name',
       CVGate::RakeTasks.parse(RAKE_T).include?('import:locations[path]'), true)
end

check 'raketasks: a line that is not a task is not counted' do
  noise = "bigdecimal.so: warning: undefining the allocator\n" + RAKE_T
  want('raketasks: a line that is not a task is not counted',
       CVGate::RakeTasks.parse(noise).length, 8)
end

check 'raketasks: empty output parses to no tasks' do
  want('raketasks: empty output parses to no tasks',
       CVGate::RakeTasks.parse(''), [])
end

check 'raketasks: a listed task tree is ok' do
  report = CVGate::RakeTasks.summarize(0, RAKE_T, '')
  want('raketasks: a listed task tree is ok', report['ok'], true)
  want('raketasks: a listed task tree is ok (count)', report['task_count'], 8)
  want('raketasks: a listed task tree is ok (db:migrate)',
       report['has_db_migrate'], true)
end

check 'raketasks: the aborted loader is not ok' do
  report = CVGate::RakeTasks.summarize(1, '', RAKE_ABORTED)
  want('raketasks: the aborted loader is not ok', report['ok'], false)
  want('raketasks: the aborted loader is not ok (count)', report['task_count'], 0)
  want('raketasks: the aborted loader is not ok (db:migrate)',
       report['has_db_migrate'], false)
  want('raketasks: the aborted loader is not ok (stderr)',
       report['stderr_head'].first, 'rake aborted!')
end

# Both halves of `ok`, because they fail differently and are the same finding:
# a loader that raised exits non-zero with no list, and a `Rakefile` deleted to
# make the error go away exits zero with no list.
check 'raketasks: exit 0 with no tasks is not ok' do
  want('raketasks: exit 0 with no tasks is not ok',
       CVGate::RakeTasks.summarize(0, '', '')['ok'], false)
end

check 'raketasks: a non-zero exit with tasks listed is not ok' do
  want('raketasks: a non-zero exit with tasks listed is not ok',
       CVGate::RakeTasks.summarize(1, RAKE_T, '')['ok'], false)
end

check 'raketasks: a child that produced no status is not ok' do
  want('raketasks: a child that produced no status is not ok',
       CVGate::RakeTasks.summarize(nil, RAKE_T, '')['ok'], false)
end

# `annotat` and not `annotate`: the retired gem describes three tasks and one
# of them is `remove_annotation`.  The longer spelling counts two of the three,
# which reads exactly like a tree half way through the swap.
check 'raketasks: all three of the retired gem tasks are counted' do
  want('raketasks: all three of the retired gem tasks are counted',
       CVGate::RakeTasks.summarize(0, RAKE_T, '')['annotate_tasks'], 3)
end

check 'raketasks: the task list is sorted and capped' do
  many = (1..300).map { |n| "rake t:%03d  # n" % n }.join("\n")
  tasks = CVGate::RakeTasks.summarize(0, many, '')['tasks']
  want('raketasks: the task list is sorted and capped (length)', tasks.length, 200)
  want('raketasks: the task list is sorted and capped (sorted)',
       tasks == tasks.sort, true)
  want('raketasks: the task list is sorted and capped (count is not capped)',
       CVGate::RakeTasks.summarize(0, many, '')['task_count'], 300)
end

check 'raketasks: a tree that is not there is a report and not a crash' do
  report = CVGate::RakeTasks.run('/nonexistent-tree-for-the-selftest')
  want('raketasks: a tree that is not there is a report and not a crash',
       report['ok'], false)
  want('raketasks: a tree that is not there is a report and not a crash (error)',
       report.key?('error') || report['exit_status'] != 0, true)
end

# ---------------------------------------------------------------------------
# bootprobe: the version narrow that three probes share
# ---------------------------------------------------------------------------
#
# `rails_at_least?` is the whole reason `defaults_8_0` stops reading
# `to_time_preserves_timezone` above 8.0, so a wrong answer here silently
# un-narrows a guard that every rung above step 10 inherits -- and the two
# ways to get it wrong are both quiet.  A string compare puts "8.9.0" above
# "8.10", and an exception on a tree where Rails is not loaded at all would
# take the probe with it.  Both are checked, and the second is not
# hypothetical: this selftest process has no Rails.
#
# `Object.const_set` and not `module Rails`, because a module definition is
# not allowed in a block, and the constant is removed again so the checks that
# follow see the process as they found it.

def with_rails_version(string)
  rails = Module.new
  version = Module.new
  version.const_set(:STRING, string)
  rails.const_set(:VERSION, version)
  Object.const_set(:Rails, rails)
  yield
ensure
  Object.send(:remove_const, :Rails) if Object.const_defined?(:Rails)
end

check 'bootprobe: rails_at_least? is false when Rails is not loaded' do
  name = 'bootprobe: rails_at_least? is false when Rails is not loaded'
  probe = CVGate::BootProbe.new(Dir.pwd)
  want(name, Object.const_defined?(:Rails), false)
  want(name, probe.send(:rails_at_least?, '8.1'), false)
end

check 'bootprobe: rails_at_least? orders by version, not by string' do
  name = 'bootprobe: rails_at_least? orders by version, not by string'
  probe = CVGate::BootProbe.new(Dir.pwd)
  got = {}
  [['8.0.5.1', '8.1'], ['8.1.3.1', '8.1'], ['8.1.0', '8.1'],
   ['8.9.0', '8.10'], ['8.10.0', '8.9'], ['7.2.2.2', '8.1']].each do |have, want_at|
    with_rails_version(have) { got["#{have}>=#{want_at}"] = probe.send(:rails_at_least?, want_at) }
  end
  want(name, got,
       '8.0.5.1>=8.1' => false, '8.1.3.1>=8.1' => true, '8.1.0>=8.1' => true,
       '8.9.0>=8.10' => false, '8.10.0>=8.9' => true, '7.2.2.2>=8.1' => false)
end

check 'bootprobe: rails_at_least? survives a version string it cannot parse' do
  name = 'bootprobe: rails_at_least? survives a version string it cannot parse'
  probe = CVGate::BootProbe.new(Dir.pwd)
  with_rails_version('not a version') do
    want(name, probe.send(:rails_at_least?, '8.1'), false)
  end
  want(name, Object.const_defined?(:Rails), false)
end

# ---------------------------------------------------------------------------
# audit.rb -- the lockfile against the advisory database
# ---------------------------------------------------------------------------

# A database of four gems, written here so that the expected answers are
# arithmetic rather than a snapshot of what rubysec published this week.
def with_advisory_db
  Dir.mktmpdir do |dir|
    db = File.join(dir, 'db')
    write = lambda do |gem, file, body|
      path = File.join(db, 'gems', gem, file)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, body)
    end
    File.write(File.join(db, 'COMMIT'), "deadbeef\n") if FileUtils.mkdir_p(db)

    # Patched in 2.x and in 3.x: the two-constraint requirement string that
    # `Gem::Requirement.new` cannot take as one argument.  142 of the real
    # database's entries are written this way.
    write.call('alpha', 'CVE-2020-1111.yml', <<~YAML)
      ---
      gem: alpha
      cve: 2020-1111
      url: https://example.invalid/1
      title: alpha is careless
      date: 2020-01-01
      patched_versions:
        - ">= 2.12.5, < 3.0.0"
        - ">= 3.1.2"
    YAML
    # No patch exists.  bundler-audit calls every version vulnerable.
    write.call('beta', 'GHSA-aaaa-bbbb-cccc.yml', <<~YAML)
      ---
      gem: beta
      ghsa: aaaa-bbbb-cccc
      title: beta has no fix
      patched_versions: []
    YAML
    # Patched everywhere from 1.0, but 0.9.1 was never affected in the first
    # place -- `unaffected_versions` has to win over `patched_versions`.
    write.call('gamma', 'OSVDB-99999.yml', <<~YAML)
      ---
      gem: gamma
      osvdb: 99999
      title: gamma, except for one release
      patched_versions:
        - ">= 1.0.0"
      unaffected_versions:
        - "= 0.9.1"
    YAML
    # Two advisories against one gem, to separate "gems" from "advisories".
    write.call('delta', 'CVE-2021-2222.yml', <<~YAML)
      ---
      gem: delta
      cve: 2021-2222
      title: delta, once
      patched_versions:
        - ">= 9.9.9"
    YAML
    write.call('delta', 'CVE-2021-3333.yml', <<~YAML)
      ---
      gem: delta
      cve: 2021-3333
      title: delta, twice
      patched_versions:
        - ">= 9.9.9"
    YAML
    yield db, dir
  end
end

# A lockfile in the shape `gemgraph.rb` parses, with the versions the caller
# asks for.  `direct` decides the DEPENDENCIES section, which is what splits a
# finding into "declared" and "transitive".
def lockfile_for(specs, direct)
  body = +"GEM\n  remote: https://rubygems.org/\n  specs:\n"
  specs.each { |name, version| body << "    #{name} (#{version})\n" }
  body << "\nPLATFORMS\n  ruby\n\nDEPENDENCIES\n"
  direct.each { |name| body << "  #{name}\n" }
  body << "\nBUNDLED WITH\n   2.6.9\n"
  body
end

def audit_report(specs, direct, db, root: nil, &_blk)
  Dir.mktmpdir do |tree|
    File.write(File.join(tree, 'Gemfile.lock'), lockfile_for(specs, direct))
    yield tree if block_given?
    CVGate::Audit.report(lock: File.join(tree, 'Gemfile.lock'), db: db,
                         root: root || tree)
  end
end

check 'audit: a two-constraint requirement string is two constraints' do
  name = 'audit: a two-constraint requirement string is two constraints'
  req = CVGate::Audit.requirement('>= 2.12.5, < 3.0.0')
  want(name, [req.satisfied_by?(Gem::Version.new('2.12.5')),
              req.satisfied_by?(Gem::Version.new('2.99.0')),
              req.satisfied_by?(Gem::Version.new('3.0.0')),
              req.satisfied_by?(Gem::Version.new('2.12.4'))],
       [true, true, false, false])
end

check 'audit: a requirement string that means nothing is nil, not a raise' do
  name = 'audit: a requirement string that means nothing is nil, not a raise'
  want(name, [CVGate::Audit.requirement('not a requirement'),
              CVGate::Audit.requirement(nil)&.to_s],
       [nil, '>= 0'])
end

check 'audit: unaffected_versions wins over patched_versions' do
  name = 'audit: unaffected_versions wins over patched_versions'
  advisory = { 'patched_versions' => ['>= 1.0.0'],
               'unaffected_versions' => ['= 0.9.1'] }
  want(name, %w[0.9.1 0.9.0 1.0.0].map do |v|
    CVGate::Audit.vulnerable?(advisory, Gem::Version.new(v))
  end, [false, true, false])
end

check 'audit: an advisory with no patched version makes every version vulnerable' do
  name = 'audit: an advisory with no patched version makes every version vulnerable'
  want(name, [CVGate::Audit.vulnerable?({ 'patched_versions' => [] },
                                        Gem::Version.new('99.0')),
              CVGate::Audit.vulnerable?({}, Gem::Version.new('99.0'))],
       [true, true])
end

check 'audit: the identifier is CVE, then GHSA, then OSVDB' do
  name = 'audit: the identifier is CVE, then GHSA, then OSVDB'
  want(name, [CVGate::Audit.identifier('cve' => '2020-1', 'ghsa' => 'x', 'osvdb' => '9'),
              CVGate::Audit.identifier('ghsa' => 'x', 'osvdb' => '9'),
              CVGate::Audit.identifier('osvdb' => '9'),
              CVGate::Audit.identifier({})],
       ['CVE-2020-1', 'GHSA-x', 'OSVDB-9', 'UNIDENTIFIED'])
end

check 'audit: the database is counted off the filesystem, and COMMIT is read' do
  name = 'audit: the database is counted off the filesystem, and COMMIT is read'
  with_advisory_db do |db, _dir|
    meta = CVGate::Audit.database(db)
    want(name, [meta['present'], meta['gems'], meta['advisories'], meta['commit']],
         [true, 4, 5, 'deadbeef'])
  end
end

check 'audit: a directory that is not a database says so and counts nothing' do
  name = 'audit: a directory that is not a database says so and counts nothing'
  Dir.mktmpdir do |dir|
    meta = CVGate::Audit.database(dir)
    want(name, [meta['present'], meta['gems'], meta['advisories'], meta['commit'],
                meta['error'].to_s.include?('gems')],
         [false, 0, 0, nil, true])
  end
end

check 'audit: the vulnerable versions are found and the patched ones are not' do
  name = 'audit: the vulnerable versions are found and the patched ones are not'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'alpha' => '2.12.4', 'gamma' => '0.9.1' }, ['alpha'], db)
    patched = audit_report({ 'alpha' => '2.12.5', 'gamma' => '0.9.0' }, ['alpha'], db)
    want(name, [got['vulnerable_gems'], got['counts'],
                patched['vulnerable_gems'], patched['counts']],
         [['alpha'], { 'advisories' => 1, 'gems' => 1, 'declared' => 1, 'transitive' => 0 },
          ['gamma'], { 'advisories' => 1, 'gems' => 1, 'declared' => 0, 'transitive' => 1 }])
  end
end

check 'audit: two advisories against one gem are two advisories and one gem' do
  name = 'audit: two advisories against one gem are two advisories and one gem'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'delta' => '1.0.0' }, [], db)
    want(name, [got['counts']['advisories'], got['counts']['gems'],
                got['by_gem'], got['vulnerable'].map { |f| f['id'] }],
         [2, 1, { 'delta' => 2 }, ['CVE-2021-2222', 'CVE-2021-3333']])
  end
end

check 'audit: declared and transitive are split by DEPENDENCIES' do
  name = 'audit: declared and transitive are split by DEPENDENCIES'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'alpha' => '1.0.0', 'beta' => '1.0.0' }, ['alpha'], db)
    want(name, [got['vulnerable_declared'], got['vulnerable_transitive']],
         [['alpha'], ['beta']])
  end
end

check 'audit: a gem the database has never heard of contributes nothing' do
  name = 'audit: a gem the database has never heard of contributes nothing'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'rails' => '8.1.0', 'pg' => '1.6.2' }, ['rails'], db)
    want(name, [got['specs'], got['counts'], got['vulnerable_gems']],
         [2, { 'advisories' => 0, 'gems' => 0, 'declared' => 0, 'transitive' => 0 }, []])
  end
end

check 'audit: a platform-suffixed version is read as its version' do
  name = 'audit: a platform-suffixed version is read as its version'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'alpha' => '2.12.4-x86_64-linux' }, ['alpha'], db)
    want(name, [got['counts']['advisories'], got['unreadable']], [1, []])
  end
end

check 'audit: a version that is not a version is reported, not raised' do
  name = 'audit: a version that is not a version is reported, not raised'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'alpha' => 'not.a.version!' }, ['alpha'], db)
    want(name, [got['counts']['advisories'], got['unreadable']],
         [0, ['alpha (not.a.version!)']])
  end
end

check 'audit: an advisory file that will not parse is reported, not raised' do
  name = 'audit: an advisory file that will not parse is reported, not raised'
  with_advisory_db do |db, _dir|
    File.write(File.join(db, 'gems', 'alpha', 'BROKEN.yml'), "---\n  : : :\n")
    got = audit_report({ 'alpha' => '1.0.0' }, ['alpha'], db)
    want(name, [got['counts']['advisories'], got['unreadable'].length,
                got['unreadable'].first.to_s.include?('BROKEN.yml')],
         [1, 1, true])
  end
end

check 'audit: no database means no gem list at all, not an empty one' do
  name = 'audit: no database means no gem list at all, not an empty one'
  Dir.mktmpdir do |nodb|
    got = audit_report({ 'alpha' => '1.0.0' }, ['alpha'], nodb)
    # `score.py` reads a missing path as a failed check.  If any of these keys
    # were present-and-empty instead, a verifier built without the database
    # would hand out every advisory gate for free.
    present = %w[counts by_gem vulnerable vulnerable_gems vulnerable_declared
                 vulnerable_transitive].select { |k| got.key?(k) }
    want(name, [present, got['db']['present'], got['specs']], [[], false, 1])
  end
end

check 'audit: a lockfile that is not there is an error and not a clean tree' do
  name = 'audit: a lockfile that is not there is an error and not a clean tree'
  with_advisory_db do |db, dir|
    got = CVGate::Audit.report(lock: File.join(dir, 'nope', 'Gemfile.lock'), db: db)
    want(name, [got['error'].to_s.start_with?('no lockfile'), got.key?('counts')],
         [true, false])
  end
end

check 'audit: a .bundler-audit.yml is reported and not obeyed' do
  name = 'audit: a .bundler-audit.yml is reported and not obeyed'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'alpha' => '1.0.0' }, ['alpha'], db) do |tree|
      File.write(File.join(tree, '.bundler-audit.yml'),
                 "---\nignore:\n  - CVE-2020-1111\n")
    end
    # The finding survives the ignore list, and the ignore list is in the
    # evidence: a tree that tried to answer the question by suppressing it
    # says so in its own report.
    want(name, [got['counts']['advisories'], got['suppression']],
         [1, { 'files' => ['.bundler-audit.yml'], 'ignored_ids' => ['CVE-2020-1111'] }])
  end
end

check 'audit: a tree with no suppression file says so' do
  name = 'audit: a tree with no suppression file says so'
  with_advisory_db do |db, _dir|
    got = audit_report({ 'alpha' => '1.0.0' }, ['alpha'], db)
    want(name, got['suppression'], { 'files' => [], 'ignored_ids' => [] })
  end
end

# ---------------------------------------------------------------------------
# logpii.rb -- what the application wrote about its callers
# ---------------------------------------------------------------------------

PARAMS_LINE = '  Parameters: {"From" => "+15105550101", "To" => "+15551230000"}'.freeze

def with_logs(files)
  Dir.mktmpdir do |dir|
    files.each { |name, body| File.write(File.join(dir, name), body) }
    yield CVGate::LogPII.report(dir), dir
  end
end

check 'logpii: a directory that is not there is an error' do
  name = 'logpii: a directory that is not there is an error'
  got = CVGate::LogPII.report('/nonexistent/cvh')
  want(name, [got['error'].to_s.start_with?('no log directory'), got.key?('phone')],
       [true, false])
end

check 'logpii: an empty log directory is a reading about the tree, not an error' do
  name = 'logpii: an empty log directory is a reading about the tree, not an error'
  with_logs({}) do |got, _dir|
    # Zeroed rather than absent: the gates read `phone.lines`, the guards read
    # `started_lines`, and this tree fails the guards -- which says "nothing was
    # served" -- instead of failing the instrument.
    want(name, [got.key?('error'), got['files'], got['lines'],
                got['started_lines'], got['phone']['lines'], got['email']['lines']],
         [false, [], 0, 0, 0, 0])
  end
end

check 'logpii: only *-server.log is read' do
  name = 'logpii: only *-server.log is read'
  with_logs('empty-server.log' => "#{PARAMS_LINE}\n",
            'runner.log' => "  Parameters: {\"To\" => \"+15559999999\"}\n",
            'notes.txt' => "  Parameters: {\"To\" => \"+15558888888\"}\n") do |got, _d|
    # `runner.log` is the harness's own file, written by the harness in the
    # same directory.  Reading it would score the instrument.
    want(name, [got['files'], got['phone']['distinct'], got['phone']['lines']],
         [['empty-server.log'], 2, 1])
  end
end

check 'logpii: a number inside a Parameters line is found and located' do
  name = 'logpii: a number inside a Parameters line is found and located'
  with_logs('a-server.log' => "#{PARAMS_LINE}\n") do |got, _dir|
    want(name, [got['phone']['lines'], got['phone']['distinct'],
                got['phone']['outside_parameters'], got['parameter_lines']],
         [1, 2, 0, 1])
  end
end

check 'logpii: the same number outside a Parameters line is located differently' do
  name = 'logpii: the same number outside a Parameters line is located differently'
  with_logs('a-server.log' => "TwilioSessionError for +15105550101\n") do |got, _d|
    # Same finding, different place: a leak in an exception message is not
    # reachable by `filter_parameters`, and the report has to say which it is
    # or the instruction cannot name a fix that works.
    want(name, [got['phone']['lines'], got['phone']['outside_parameters'],
                got['parameter_lines']],
         [1, 1, 0])
  end
end

check 'logpii: lines and distinct values are counted separately' do
  name = 'logpii: lines and distinct values are counted separately'
  body = ([PARAMS_LINE] * 3).join("\n") + "\n" +
         '  Parameters: {"From" => "+15105550102"}' + "\n"
  with_logs('a-server.log' => body) do |got, _dir|
    want(name, [got['phone']['lines'], got['phone']['distinct'],
                got['phone']['samples']],
         [4, 3, ['+15105550101', '+15105550102', '+15551230000']])
  end
end

check 'logpii: an address and a number on one line are one line each' do
  name = 'logpii: an address and a number on one line are one line each'
  with_logs('a-server.log' =>
            '  Parameters: {"To" => "+15551230000", "subscription" => ' \
            '{"email" => "resident@example.com"}}' + "\n") do |got, _dir|
    want(name, [got['phone']['lines'], got['email']['lines'],
                got['email']['samples']],
         [1, 1, ['resident@example.com']])
  end
end

check 'logpii: the floors move independently of the findings' do
  name = 'logpii: the floors move independently of the findings'
  # The anti-cheat reading.  A candidate who sets `log_level = :warn` produces
  # a log with no findings AND no floors; a candidate who filters the
  # parameters produces one with the floors intact.  The gate is the first
  # column, the guards are the rest, and this fixture is the pair.
  filtered = <<~LOG
    Started POST "/voice" for 127.0.0.1 at 2016-01-19 12:00:00 +0000
      Parameters: {"From" => "[FILTERED]", "To" => "[FILTERED]"}
    Completed 200 OK in 12ms
  LOG
  silenced = "\n"
  with_logs('a-server.log' => filtered) do |got, _dir|
    with_logs('a-server.log' => silenced) do |quiet, _dir2|
      want(name,
           [[got['phone']['lines'], got['started_lines'], got['parameter_lines'],
             got['completed_lines'], got['filtered_markers']],
            [quiet['phone']['lines'], quiet['started_lines'],
             quiet['parameter_lines'], quiet['completed_lines']]],
           [[0, 1, 1, 1, 2], [0, 0, 0, 0]])
    end
  end
end

check 'logpii: per-file counts name the world that leaked' do
  name = 'logpii: per-file counts name the world that leaked'
  with_logs('ivr_happy-server.log' => "#{PARAMS_LINE}\n",
            'empty-server.log' => "Completed 200 OK in 3ms\n") do |got, _dir|
    want(name, got['per_file'],
         'empty-server.log' => { 'phone' => 0, 'email' => 0 },
         'ivr_happy-server.log' => { 'phone' => 1, 'email' => 0 })
  end
end

check 'logpii: a version number is not a telephone number' do
  name = 'logpii: a version number is not a telephone number'
  # The near miss.  `1.2.3`, `2016-01-19 12:00:00`, an eleven-digit id with no
  # `+`, and a `+` with nine digits after it are all things this log is full
  # of, and none of them is an E.164 number.
  body = <<~LOG
    Started GET "/locations/1" for 127.0.0.1 at 2016-01-19 12:00:00 +0000
      Rendered locations/show.html.erb (12.3ms) sprockets 3.7.2 rails 8.1.0
      Parameters: {"CallSid" => "CA00000000000000000000000000000001"}
      Completed 200 OK in 15505550101ms (Views: 1.2ms | ActiveRecord: 3.4ms)
      Parameters: {"short" => "+123456789"}
  LOG
  with_logs('a-server.log' => body) do |got, _dir|
    want(name, [got['phone']['lines'], got['phone']['distinct'],
                got['started_lines'], got['completed_lines']],
         [0, 0, 1, 1])
  end
end

check 'logpii: a masked value is still a value' do
  name = 'logpii: a masked value is still a value'
  # What a candidate reaches for first: keep writing the number, star out the
  # last four digits.  A partially masked number is still enough to identify
  # the caller when the log also carries the call, and a partially masked
  # address is still a name at a domain, so both count.
  body = <<~LOG
      Parameters: {"From" => "+1510555****", "email" => "r***@example.com"}
      Parameters: {"From" => "+1510555xxxx"}
  LOG
  with_logs('a-server.log' => body) do |got, _dir|
    want(name, [got['phone']['lines'], got['phone']['distinct'],
                got['email']['lines'], got['email']['samples']],
         [2, 2, 1, ['r***@example.com']])
  end
end

check 'logpii: a reformatted number is still a number' do
  name = 'logpii: a reformatted number is still a number'
  # A second dodge: a log subscriber that
  # REFORMATS the number rather than removing it.  Every one of these is the
  # caller's telephone number in clear text, and a reader that counts digits
  # rather than matching one spelling sees all four.
  body = <<~LOG
      Parameters: {"From" => "+1 510 555 0101"}
      Parameters: {"From" => "+1-510-555-0102"}
      Parameters: {"From" => "+1 (510) 555-0103"}
      Parameters: {"From" => "+1.510.555.0104"}
  LOG
  with_logs('a-server.log' => body) do |got, _dir|
    want(name, [got['phone']['lines'], got['phone']['distinct'],
                got['phone']['outside_parameters']],
         [4, 4, 0])
  end
end

check 'logpii: a timestamp offset is not a reformatted number' do
  name = 'logpii: a timestamp offset is not a reformatted number'
  # The other side of the same change.  Widening the separators is only safe
  # because the length bound is on the DIGITS: `+0000` has four, a duration in
  # milliseconds has no `+` in front of it, and a version has letters in it.
  # Every line here is one this application actually writes.
  body = <<~LOG
    Started POST "/voice" for 127.0.0.1 at 2016-01-19 12:00:00 +0000
      Rendered locations/show.html.erb (12.3 ms) rails +8.1.0-rc
      Completed 200 OK in 12ms (Views: 1.2ms | ActiveRecord: 3.4ms)
      Parameters: {"CallSid" => "CA0000000000000000000000000000001"}
  LOG
  with_logs('a-server.log' => body) do |got, _dir|
    want(name, [got['phone']['lines'], got['phone']['distinct'],
                got['started_lines'], got['completed_lines']],
         [0, 0, 1, 1])
  end
end

check 'logpii: a word with an at-sign is not an address' do
  name = 'logpii: a word with an at-sign is not an address'
  body = <<~LOG
      Parameters: {"handle" => "resident@example", "ivar" => "@question"}
      Rendered app/views/locations/show.html.erb within layouts/application
  LOG
  with_logs('a-server.log' => body) do |got, _dir|
    want(name, [got['email']['lines'], got['email']['distinct']], [0, 0])
  end
end

check 'logpii: a sample is cut at 64 characters and capped at five' do
  name = 'logpii: a sample is cut at 64 characters and capped at five'
  long = "a#{'x' * 80}@example.com"
  body = (1..7).map { |i| "  Parameters: {\"email\" => \"r#{i}@example.com\"}" }
                .join("\n") + "\n  Parameters: {\"email\" => \"#{long}\"}\n"
  with_logs('a-server.log' => body) do |got, _dir|
    want(name, [got['email']['lines'], got['email']['distinct'],
                got['email']['samples'].length,
                got['email']['samples'].first.length,
                got['email']['samples'].first.start_with?('axxx')],
         [8, 8, 5, 64, true])
  end
end

# ---------------------------------------------------------------------------

if $checks < MIN_CHECKS
  $failures << "only #{$checks} checks ran, expected at least #{MIN_CHECKS} " \
               '(a check was deleted or silently skipped)'
end

if $failures.empty?
  puts "tools selftest ok: #{$checks} checks"
  $notes.each { |n| puts "  note: #{n}" }
  exit 0
else
  warn "tools selftest FAILED: #{$failures.length} of #{$checks} checks"
  $failures.each { |f| warn "  - #{f}" }
  exit 1
end
