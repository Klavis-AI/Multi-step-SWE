# frozen_string_literal: true

# The suite's deprecation collector, loaded into the rspec process itself:
#
#   CV_RSPEC_DEP_OUT=/tmp/gates/rspec_deprecations.jsonl \
#     RUBYOPT=-r/tests/cvgate/rspecdep bundle exec rspec
#
# Why this file exists.  Counting the `DEPRECATION WARNING:` banner in the
# rspec log is not a reading of the suite's deprecations.  A tree that writes
# `config.active_support.deprecation = :silence` into its environment files
# raises exactly as many deprecations as before and prints none of them, so the
# banner count falls to zero and a gate on it passes.  Measured with that one
# setting changed in the three environment files and nothing else touched at
# all: 306 suite deprecations read as 0.  Suppressed findings are findings.
#
# So the reading is taken where it cannot be silenced, the same way
# `depdrive.rb` takes its own: by prepending a `warn` to
# `ActiveSupport::Deprecation` that records and *then* calls `super`.  Every
# form of suppression the framework offers -- `behavior = :silence`, `silenced
# = true`, a `Deprecation.silence { }` block, a `disallowed_warnings` entry --
# is downstream of that method, so all of it still runs and none of it changes
# what was written down.  The one thing this file does not see is a
# deprecation the tree stopped *raising*, which is the thing the step is asking
# for.
#
# ---------------------------------------------------------------------------
# The one suppression that is not the tree's: `silence` opened by the framework
# ---------------------------------------------------------------------------
#
# Recording upstream of `return if silenced` also records the deprecations
# *Rails itself* raises inside its own `ActiveSupport::Deprecation.silence`
# blocks, and on this repository there are two of them, both on the golden.
# Measured on the step-02 golden tree by recording the whole callstack of every
# `warn` the suite raised (three events, each seen twice -- once at the
# singleton, once at the instance, because the class method delegates):
#
#   message                         opened `silence`            first /app frame
#   factory_girl is deprecated      -- (printed)                config/application.rb
#   #table_exists? ... views        activerecord migration.rb   spec/spec_helper.rb:19
#   #table_exists? ... views        activerecord schema_migr.   spec/spec_helper.rb:19
#
# The second and third are `ActiveRecord::Migration.check_pending!`, which the
# suite calls from `spec_helper.rb` and which asks a deprecated question about
# its own schema table with the warning explicitly silenced.  Nothing the
# candidate can write removes them, they never reach the log, and blamed to the
# first application frame they would be blamed on `spec/` -- charging a
# correct tree for the framework's own check.
#
# So the record is skipped when a `silence` block is open and *every* block
# that is open was opened from outside the tree.  `silence` is prepended for
# exactly that: it notes its own caller on the way in and drops it on the way
# out.  The rule in one sentence: a `silence` the framework opened around its
# own internal check is the framework's business; a `silence` the tree opened
# is the tree's, and is recorded.  `silenced = true` set as a flag, with no
# block anywhere, silences nothing here -- there is no caller to credit it to.
#
# Records are appended one JSON object per line as they arrive rather than
# dumped from an `at_exit`: a suite killed by the step's timeout, or one whose
# last example calls `exit!`, still leaves behind everything it raised up to
# the moment it died, and `at_exit` ordering is a thing the candidate's
# `spec_helper.rb` can get in front of.  The skipped ones are written down too,
# as `event: framework_silenced`, so that "the instrument saw nothing" and "the
# instrument saw two and knew whose they were" are different readings.
#
# `test.sh` loads this file two ways, because neither alone reaches every
# tree.  Measured with bundler 1.17.3 on Ruby 2.4.10, a `-r` file and a
# `bundle exec` of a ruby-shebang script:
#
#   bundle exec ./probe                            -> loaded once, before
#                                                     `Bundler.setup`
#   BUNDLE_DISABLE_EXEC_LOAD=true bundle exec ...  -> loaded twice: once
#                                                     before, once after
#
# `bundle exec` does not exec a script with a ruby shebang; it `Kernel.load`s
# it into the process bundler is already running in, and that process read
# RUBYOPT at startup -- before the bundle existed.  So RUBYOPT alone installs
# nothing (see `ready?`), and `test.sh` sets `BUNDLE_DISABLE_EXEC_LOAD` to get
# the second load back.  It also passes `rspec --require` as a second way in,
# because a bundler that stops honouring that setting must not silently turn
# the gate off: `--require` is processed by rspec itself, always after
# `Bundler.setup`, and the double load is harmless -- `require` of the same
# absolute path twice is a no-op, and `install` refuses to run twice anyway.

require 'json'
require_relative 'depdrive'

module CVGate
  module RspecDep
    # The sink the collector pushes into.  It asks for `<<` and nothing else,
    # so this appends a line instead of growing an array.
    class Sink
      def initialize(path); @path = path; end

      def <<(record)
        File.open(@path, 'a') do |handle|
          handle.write(JSON.generate(record)); handle.write("\n")
        end
        self
      rescue StandardError
        self
      end
    end

    # Whether this process is far enough along to install.  `require
    # 'active_support'` inside the pre-`Bundler.setup` load activates a system
    # gem and makes bundler abort the run with "you have already activated
    # activesupport"; the whole run would be lost to the instrument.  So the
    # early load is refused, and the one after `Bundler.setup` -- which the
    # bundle's own activesupport is on the load path for -- is taken.
    def self.ready?
      return true if defined?(::ActiveSupport::Deprecation)
      $LOADED_FEATURES.any? { |feature| feature.end_with?('bundler/setup.rb') }
    end

    @installed = false
    @rspec_installed = false
    @app = nil
    @sink = nil
    @silencers = []

    class << self
      attr_reader :installed
      attr_reader :rspec_installed
      # The open `silence` blocks, innermost last, as the call site that opened
      # each.  Exposed for the selftest, which drives them directly.
      attr_reader :silencers
      # `app` is a reader with a default rather than an `attr_reader`, below:
      # the rspec hook can record before `install` has set one.
    end

    # The frame a `silence` was opened from, reduced to the path it names.
    # `caller` frames are "<path>:<line>:in `<label>'" and the label can hold
    # anything, so the cut is at the first ":<digits>:" and not at the last
    # colon.
    def self.path_of(frame)
      text = frame.to_s
      cut = text.index(/:\d+(?::|\z)/)
      cut ? text[0, cut] : text
    end

    def self.under_app?(frame)
      return false if @app.nil? || @app.empty?
      path = path_of(frame)
      path.start_with?(@app + '/') && !path.include?('/vendor/bundle/')
    end

    def self.enter_silence(frame)
      @silencers.push(frame.to_s)
    end

    def self.leave_silence
      @silencers.pop
    end

    # True when the only reason this warning is being suppressed is a `silence`
    # block the tree did not open.  A flag set with no block open is not a
    # reason: `@silencers` is empty and the warning is recorded.
    def self.framework_silenced?
      return false if @silencers.empty?
      !@silencers.any? { |frame| under_app?(frame) }
    end

    def self.observe(message, callstack)
      if framework_silenced?
        # The outermost open block and not the innermost: the class method
        # delegates to the instance, so a `silence` the framework opened is
        # pushed twice and the inner frame is always the delegator.  The one
        # that decided is the first.
        @sink << { 'event' => 'framework_silenced',
                   'by' => path_of(@silencers.first) }
        return
      end
      @sink << { 'message' => message.to_s.strip,
                 'where' => CVGate::DepDrive.blame(callstack, @app) }
    rescue StandardError
      nil
    end

    # The collector as a value, so the selftest can prepend it to a class of
    # its own and check that a `warn` really does land on disk and really does
    # still reach the method underneath.  A collector that is only ever
    # exercised against the real Rails is a collector nobody has checked: it
    # would report zero deprecations on a clean tree and zero on a broken
    # install, and those look identical in the reward.
    #
    # `*args` and not the 5.0 signature, because this module is prepended to
    # whatever `ActiveSupport::Deprecation` the tree ships and the arity of
    # both methods has to be allowed to move between 5.0 and 8.0.  Bare
    # `super` forwards the arguments and the block exactly as they arrived.
    module Collector
      def warn(*args)
        CVGate::RspecDep.observe(args[0], args[1] || caller)
        super
      end

      def silence(*_args)
        CVGate::RspecDep.enter_silence(Array(caller(1, 1)).first)
        begin
          super
        ensure
          CVGate::RspecDep.leave_silence
        end
      end
    end

    # -------------------------------------------------------------------
    # RSpec's own deprecations
    # -------------------------------------------------------------------
    #
    # `ActiveSupport::Deprecation` is not the only thing that deprecates in a
    # suite, and from Rails 7.1 it is not the one that matters most here.
    # rspec-rails 6.1.5 keeps `config.fixture_path=` working on 7.1 by
    # defining a shim that calls `RSpec.deprecate` and then assigns the plural
    # setting (lib/rspec/rails/configuration.rb:179-187).  `RSpec.deprecate`
    # reports to rspec's own reporter and never touches
    # `ActiveSupport::Deprecation`, so the collector above sees nothing, the
    # suite passes, and `suite_deprecations` reads zero on a tree that still
    # writes the singular spelling.  A requirement no instrument can see is a
    # requirement nobody has to meet.
    #
    # The hook is on `RSpec.deprecate` and `RSpec.warn_deprecation`, the two
    # module methods rspec raises through, rather than on the
    # `Reporter#deprecation` they both call: the reporter is built lazily and
    # a warning raised while `spec_helper` is still being read reaches it
    # before there is a formatter to print it, and `RSpec::Core::Reporter` is
    # not necessarily loaded at the moment this hook goes in.
    #
    # The call site is rspec's own answer.  `RSpec.deprecate` puts
    # `CallerFilter.first_non_rspec_line` in the payload, so a deprecation
    # raised from the tree's own `spec/` is attributed there and one raised
    # from inside a gem is attributed to the gem -- which is the partition
    # `test.sh` needs, and the same one `suite_deprecations` makes.
    module RSpecCollector
      # `*args`, for the reason `Collector` gives: this is prepended to
      # whatever rspec the tree ships, and the arity of both methods has to be
      # allowed to move.
      def deprecate(*args)
        CVGate::RspecDep.observe_rspec(args[0], args[1])
        super
      end

      def warn_deprecation(*args)
        CVGate::RspecDep.observe_rspec(args[0], args[1])
        super
      end
    end

    # The sink, built on demand.  `install` above refuses to run before
    # `Bundler.setup` because touching `ActiveSupport::Deprecation` there
    # would activate a system gem and lose the whole run; the rspec hook has
    # no such problem -- it touches nothing until rspec itself is loaded --
    # so it must be able to write without waiting for that install.
    def self.sink
      @sink ||= begin
        path = ENV['CV_RSPEC_DEP_OUT']
        path.nil? || path.empty? ? nil : Sink.new(path)
      end
    end

    # The tree the suite is being run against.  `install` sets it from its own
    # argument; until then it is what the environment says, so that a
    # deprecation recorded before the Rails collector installed is still
    # blamed against the right root.
    def self.app
      @app ||= ENV['CV_RSPEC_DEP_APP'] || '/app'
    end

    # `/app/spec/spec_helper.rb:38` -> `spec/spec_helper.rb:38`, so that a
    # site inside the tree is relative and a site inside a gem stays absolute.
    # That is the partition `test.sh` reads: what the tree wrote is what the
    # step is asking about, and what a gem does to itself is the gem's.
    def self.relative(where)
      text = where.to_s
      root = app
      return text if root.nil? || root.empty?
      text.start_with?(root + '/') ? text[(root.length + 1)..-1] : text
    end

    def self.observe_rspec(deprecated, options)
      target = sink
      return nil if target.nil?
      opts = options.is_a?(Hash) ? options : {}
      site = opts[:call_site] || opts['call_site']
      site = CVGate::DepDrive.blame(caller, app) if site.nil? || site.to_s.empty?
      target << { 'event' => 'rspec_deprecation',
                  'message' => deprecated.to_s.strip,
                  'where' => relative(site) }
      nil
    rescue StandardError
      nil
    end

    # True when a suite helper is already loaded by the time the hook goes in
    # -- the one ordering in which an `RSpec.configure` block could have
    # deprecated something before there was anything to record it.  This
    # repository has no `.rspec` and every spec file requires `spec_helper`
    # itself, so rspec's `--require` list is processed first and this is
    # false; a tree that adds `--require spec_helper` to a `.rspec` of its own
    # would load it from `file_options`, which rspec merges BEFORE the command
    # line, and this would be true.  Written down rather than assumed, because
    # "the instrument saw nothing" and "the instrument arrived after the
    # answer" must not read the same way.
    def self.loaded_suite_helper?
      $LOADED_FEATURES.any? do |feature|
        base = File.basename(feature.to_s)
        base == 'spec_helper.rb' || base == 'rails_helper.rb'
      end
    end

    def self.install_rspec
      return false if @rspec_installed
      return false unless defined?(::RSpec) && ::RSpec.respond_to?(:deprecate)
      target = sink
      return false if target.nil?
      ::RSpec.singleton_class.prepend(RSpecCollector)
      @rspec_installed = true
      # `rspec/core.rb` defines `RSpec.deprecate` before it has required its
      # own version file, so the constant is usually still missing at the
      # moment the watch fires -- measured: without this fallback the report
      # carries `"rspec":null`.  The gemspec is already activated by then, so
      # bundler's copy answers instead.
      version = if defined?(::RSpec::Core::Version::STRING)
                  ::RSpec::Core::Version::STRING
                elsif (spec = Gem.loaded_specs['rspec-core'])
                  spec.version.to_s
                end
      target << { 'event' => 'rspec_install', 'pid' => Process.pid,
                  'rspec' => version, 'late' => loaded_suite_helper? }
      true
    rescue Exception => e # rubocop:disable Lint/RescueException
      Kernel.warn("cvgate/rspecdep: rspec hook not installed: #{e.class}: #{e.message}")
      false
    end

    # rspec does not exist in the process when this file is first loaded:
    # RUBYOPT puts it in before bundler has set up a load path at all.  So the
    # hook cannot simply be installed here and cannot wait for rspec's own
    # `--require` either -- by then a `.rspec` could already have loaded the
    # suite helper.  A `TracePoint` on `:end`, which fires as each class or
    # module body closes, puts it in the moment `RSpec.deprecate` is defined:
    # before rspec has read an option, let alone a spec file.  It disables
    # itself on the way in, so the suite itself runs untraced.
    def self.watch_for_rspec
      return true if @rspec_installed
      return true if install_rspec
      @rspec_watch ||= TracePoint.new(:end) do
        @rspec_watch.disable if install_rspec
      end
      @rspec_watch.enable unless @rspec_watch.enabled?
      true
    rescue Exception => e # rubocop:disable Lint/RescueException
      Kernel.warn("cvgate/rspecdep: rspec watch not enabled: #{e.class}: #{e.message}")
      false
    end

    def self.install(path = ENV['CV_RSPEC_DEP_OUT'],
                     app = ENV['CV_RSPEC_DEP_APP'] || '/app')
      return false if @installed
      return false if path.nil? || path.empty?
      return false unless ready?
      unless defined?(::ActiveSupport::Deprecation)
        # `logger` first, for the reason `depdrive.rb` `install` gives: on
        # activesupport 6.0/6.1 with concurrent-ruby >= 1.3.5 this require
        # otherwise raises NameError ahead of the tree's own `config/boot.rb`.
        require 'logger'
        require 'active_support'
        require 'active_support/deprecation'
      end
      @app = app
      @sink = Sink.new(path)
      # Both, for the reason `depdrive.rb` gives: the class method is its own
      # method and delegates to the instance, and from 7.1 each framework
      # carries an instance of its own.
      ActiveSupport::Deprecation.prepend(Collector)
      ActiveSupport::Deprecation.singleton_class.prepend(Collector)
      version = defined?(::ActiveSupport::VERSION::STRING) ? ::ActiveSupport::VERSION::STRING : nil
      @installed = true
      @sink << { 'event' => 'install', 'pid' => Process.pid,
                 'ruby' => RUBY_VERSION, 'active_support' => version }
      true
    rescue Exception => e # rubocop:disable Lint/RescueException
      Kernel.warn("cvgate/rspecdep: not installed: #{e.class}: #{e.message}")
      false
    end
  end
end

CVGate::RspecDep.install
CVGate::RspecDep.watch_for_rspec
