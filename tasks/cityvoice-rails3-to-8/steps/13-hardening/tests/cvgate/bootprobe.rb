# Boot the application and read its own account of itself.
#
#   cd /app && bundle exec ruby /gates/cvgate/bootprobe.rb --out /logs/boot.json
#
# Everything here is a runtime fact, asked of the framework rather than inferred
# from source.  "Rails 7.1 is in use" is `Rails::VERSION::STRING`, not a line in
# a Gemfile; "the defaults are loaded" is
# `Rails.application.config.loaded_config_version`, not the presence of a
# `load_defaults` call that might be dead; "Zeitwerk is the autoloader" is
# `Rails.autoloaders.zeitwerk_enabled?`; "Sprockets is gone" is the absence of
# `config.assets` on a booted application and of `sprockets` in
# `Gem.loaded_specs`.
#
# ONE PROBE FAILING MUST NOT LOSE THE OTHERS.  Each readout is wrapped, and a
# probe that raises records its exception in place of its value.  A gate then
# distinguishes "this application says X" from "this application could not be
# asked", which are very different findings and which a bare crash would merge.
#
# Boot failure itself is the loudest possible result: `booted: false` with the
# exception, and every later probe absent.  It is never a zero.

require 'json'
require 'optparse'

require File.join(File.dirname(File.expand_path(__FILE__)), 'bundledgems')

module CVGate
  class BootProbe
    # `outside` is the list of directories, relative to the application root,
    # that this step asks `undefined_outside_app` about on top of whatever the
    # booted application declares.  It is named by the caller because the probe
    # cannot know it: a tree that has taken `lib` off `autoload_paths`
    # altogether declares nothing to enumerate, and reading only what the tree
    # declares would let "stop autoloading it and never require it" answer the
    # question by deleting it.
    def initialize(app_root, outside = [])
      @app = File.expand_path(app_root)
      @outside = outside
      @report = { 'app' => @app, 'ruby' => RUBY_VERSION,
                  'ruby_platform' => RUBY_PLATFORM, 'probes' => {} }
      @warnings = []
    end

    # Which libraries the interpreter says it is about to stop handing out.
    #
    # Ruby 3.4 prints, on the first `require` of a library that has left the
    # default set or is leaving it, a line rubygems writes through `Kernel#warn`:
    #
    #   /opt/rubies/3.4.10/lib/ruby/3.4.0/ostruct.rb was loaded from the
    #   standard library, but will no longer be part of the default gems
    #   starting from Ruby 4.0.0.  You can add ostruct to your Gemfile or
    #   gemspec to silence this warning.
    #
    # Nothing else on this ladder sees it.  It is a warning and not a
    # deprecation, so `depdrive` does not collect it; it breaks nothing, so the
    # boot is green and the suite passes with it; and it is printed by the
    # `require` rather than by the application, so no backtrace names a file
    # under app/.  On this repository the library is `ostruct`, required at
    # `app/models/app_content_set.rb:1` and reached on every boot in every
    # environment -- not by eager loading, which is off in development and
    # test, but by `config/initializers/app_content_set.rb:10`, which names the
    # constant from inside a `to_prepare` block (measured).
    #
    # The NAMES are reported and the lines are not.  A raw line carries the
    # interpreter's own install prefix, which would make the reading a fact
    # about the image rather than about the tree, and it names the release the
    # library is leaving in, which moves between interpreters.  `warnings`
    # still carries the full text for a reader; this key is what a check can
    # be written against.
    #
    # Its own method, and a class method, so the selftest can drive it against
    # canned lines on every interpreter in the ladder -- including the ones
    # where `Warning` does not exist and the list is always empty.
    BUNDLED_GEM_WARNING = /
      ([A-Za-z0-9_-]+)\.rb\ was\ loaded\ from\ the\ standard\ library,
      \ but\ will\ no\ longer\ be\ part\ of\ the\ default\ gems
    /x.freeze

    def self.bundled_gem_only(lines)
      Array(lines).map { |line| line.to_s[BUNDLED_GEM_WARNING, 1] }
                  .compact.uniq.sort
    end

    def probe(name)
      @report['probes'][name] = yield
    rescue Exception => e # rubocop:disable Lint/RescueException
      # `Exception`, not `StandardError`: a missing constant during eager load
      # arrives as `NameError` (a StandardError) but a bad `require` can arrive
      # as `LoadError` or `SystemExit`, and losing the whole report to one of
      # those is exactly the silent-zero shape this file exists to avoid.
      @report['probes'][name] = { 'error' => "#{e.class}: #{e.message}",
                                  'backtrace' => Array(e.backtrace).first(8) }
    end

    def run
      capture_warnings
      boot
      # Read before the early return, and read off the load path rather than
      # off the warning list: a boot that died half way still loaded whatever
      # it loaded, and this reading is the one `$VERBOSE` cannot move.  See
      # `cvgate/bundledgems.rb`; the warning list is still reported below, as
      # evidence for a reader rather than as the thing a gate stands on.
      @report['bundled_gem_stdlib'] = CVGate::BundledGems.read
      @report['bundled_gem_list_known'] = CVGate::BundledGems.known?
      return @report unless @report['booted']

      probe('versions') { versions }
      probe('load_defaults') { load_defaults }
      probe('autoloader') { autoloader }
      probe('eager_load') { eager_load }
      probe('zeitwerk_check') { zeitwerk_check }
      probe('asset_pipeline') { asset_pipeline }
      probe('audio') { audio }
      probe('mailers') { mailers }
      probe('loaded_gems') { loaded_gems }
      probe('framework_defaults_files') { framework_defaults_files }
      probe('models') { models }
      probe('framework_defaults') { framework_defaults }
      probe('defaults_5_2') { defaults_5_2 }
      probe('defaults_6_0') { defaults_6_0 }
      probe('defaults_6_1') { defaults_6_1 }
      probe('defaults_7_0') { defaults_7_0 }
      probe('defaults_7_1') { defaults_7_1 }
      probe('defaults_7_2') { defaults_7_2 }
      probe('defaults_8_0') { defaults_8_0 }
      probe('defaults_8_1') { defaults_8_1 }
      probe('autoload_lib') { autoload_lib }
      probe('encryption') { encryption }
      probe('async_queries') { async_queries }
      probe('host_authorization') { host_authorization }
      probe('parameters') { parameters }
      probe('database') { database }
      probe('deprecation') { deprecation }
      probe('secrets') { secrets }
      probe('bootsnap') { bootsnap }
      probe('csp') { csp }
      probe('routes') { routes }
      # Last, and deliberately: it disconnects the connection pool, which is
      # what the hook it runs is for.  Every probe above wants a connection.
      probe('fork_hooks') { fork_hooks }
      @report['warnings'] = @warnings.uniq.first(200)
      @report['bundled_gem_warnings'] = BootProbe.bundled_gem_only(@warnings)
      @report
    end

    private

    # "Which release is this?", asked once and answered the same way
    # everywhere, because three probes narrow on it and a string compare
    # would put 8.10 below 8.9.  False, not an exception, when Rails is not
    # loaded at all: this file runs inside a selftest that has no Rails, and a
    # probe that cannot see the framework has not seen a NEW framework.
    def rails_at_least?(version)
      return false unless defined?(Rails::VERSION::STRING)

      Gem::Version.new(Rails::VERSION::STRING) >= Gem::Version.new(version)
    rescue StandardError
      false
    end

    # `Warning.warn` is the hook Ruby routes `warn`, `$VERBOSE` output and most
    # library deprecation notices through.  It arrived in 2.4; on 2.3 there is
    # nothing to hook and the report simply carries no `warnings` key's worth of
    # them.
    #
    # The override writes to `$stderr` itself rather than calling `super`.  From
    # Ruby 3.0 the real `Warning.warn` takes a `category:` keyword, and a
    # `*args` override that forwards with `super(*args)` turns that keyword into
    # a positional hash and raises `ArgumentError` on every warning -- which
    # would abort a boot that was otherwise fine, in the one method whose job is
    # to observe quietly.
    def capture_warnings
      collector = @warnings
      # The verifier image sets `RUBYOPT=-W0` for every process it starts, and
      # `-W0` is `$VERBOSE = nil`, which is not merely "fewer warnings": it
      # silences rubygems' notice that a library was loaded from the standard
      # library and will not be a default gem much longer.  Measured on 3.4.10:
      # under `-W0` a `require 'ostruct'` from a bundle that does not name it
      # prints nothing, and `$VERBOSE = false` -- the interpreter's own default
      # level, not `-w` -- brings the notice back.
      #
      # So the level is restored here, in this process only.  Nothing else in
      # the verifier changes: `test.sh` starts each instrument separately, the
      # recorded behaviour baseline is produced by rake and rspec runs that
      # still have `-W0`, and this probe's own stdout is not scored -- it
      # writes a JSON report to a file.  What it buys is that `warnings`, and
      # the key below it, read what the application would say on a machine
      # nobody had quieted.  `nil` is the only value replaced: a verifier that
      # someday runs with `-w` should keep `-w`.
      $VERBOSE = false if $VERBOSE.nil?
      return unless defined?(Warning) && Warning.respond_to?(:warn)

      Warning.singleton_class.prepend(Module.new do
        define_method(:warn) do |*args|
          text = args.first.to_s
          collector << text.strip
          $stderr.write(text)
          nil
        end
      end)
    end

    def boot
      environment = File.join(@app, 'config', 'environment.rb')
      unless File.exist?(environment)
        @report['booted'] = false
        @report['boot_error'] = "no config/environment.rb under #{@app}"
        return
      end

      started = Time.now
      begin
        require environment
        @report['booted'] = true
      rescue Exception => e # rubocop:disable Lint/RescueException
        @report['booted'] = false
        @report['boot_error'] = "#{e.class}: #{e.message}"
        @report['boot_backtrace'] = Array(e.backtrace).first(20)
      end
      @report['boot_seconds'] = (Time.now - started).round(2)
    end

    def versions
      {
        'rails' => (Rails::VERSION::STRING if defined?(Rails::VERSION::STRING)),
        'rack' => (Rack.release.to_s if defined?(Rack)),
        'env' => Rails.env.to_s,
        'app_class' => Rails.application.class.name
      }
    end

    # `load_defaults 7.1` records the version it was given.  Rails 5.x has no
    # such reader, so `nil` there means "this Rails cannot be asked", which is
    # reported as its own value rather than as 0.
    def load_defaults
      config = Rails.application.config
      value = config.respond_to?(:loaded_config_version) ? config.loaded_config_version : nil
      {
        'loaded_config_version' => value && value.to_s,
        'reader_exists' => config.respond_to?(:loaded_config_version)
      }
    end

    def autoloader
      out = { 'config' => nil, 'zeitwerk_enabled' => nil, 'loaders' => [] }
      config = Rails.application.config
      out['config'] = config.autoloader.to_s if config.respond_to?(:autoloader)
      if Rails.respond_to?(:autoloaders)
        loaders = Rails.autoloaders
        out['zeitwerk_enabled'] = loaders.zeitwerk_enabled? if loaders.respond_to?(:zeitwerk_enabled?)
        out['zeitwerk_enabled'] = true if out['zeitwerk_enabled'].nil? && loaders.respond_to?(:main)
        out['loaders'] = loaders.map { |loader| loader.tag.to_s } if loaders.respond_to?(:map)
      end
      out['classic_available'] = defined?(ActiveSupport::Dependencies::Loadable) ? true : false
      out
    end

    # Eager loading is the real check that every file in `app/` is loadable and
    # that its constant is named the way its path says.  It is what
    # `zeitwerk:check` does plus the rest of the eager-load paths.
    #
    # `autoload_only` is the other half of that: from Rails 5.0 a booted
    # production process stops autoloading, so a directory the application put
    # on `autoload_paths` and nowhere else has its constants defined in
    # development and undefined in production, and `eager_load!` returns
    # cleanly either way.  Measured on this repository rather than assumed --
    # `lib/` on `autoload_paths` alone, booted in production under 5.0.7.2,
    # raises `NameError: uninitialized constant AppContentRenderer`; the same
    # tree with `config.eager_load_paths << Rails.root.join('lib')` resolves it
    # and reports no autoload-only path at all.
    #
    # Only paths inside the application are reported.  An engine's or a gem's
    # own autoload path is not this application's to move, and a report that
    # named them would make the reading depend on the bundle.
    #
    # `undefined_outside_app` is the same question asked of the answer rather
    # than of the configuration.  `autoload_only` names a path that is set up
    # wrongly; this names a constant that is not there, which is the thing that
    # actually breaks.  The two can disagree in both directions: a tree that
    # puts `lib/` on `eager_load_paths` but has moved `lib/question_presenter.rb`
    # somewhere the path does not cover reports no autoload-only path and an
    # undefined `QuestionPresenter`, and a tree that reaches its constants by
    # `require` from an initializer reports neither.  A gate that asks only the
    # configuration question is asking about a means; this asks about the end.
    def eager_load
      started = Time.now
      Rails.application.eager_load!
      config = Rails.application.config
      eager = config.eager_load_paths.map(&:to_s)
      declared = (config.autoload_paths + config.eager_load_paths +
                  config.autoload_once_paths).map(&:to_s)
      { 'ok' => true, 'seconds' => (Time.now - started).round(2),
        'paths' => eager.map { |p| p.sub(@app + '/', '') },
        'autoload_only' => (declared - eager)
                           .select { |p| p.start_with?(@app + '/') }
                           .map { |p| p.sub(@app + '/', '') }.sort,
        'undefined_outside_app' => undefined_outside_app(declared) }
    end

    # Every `.rb` the application autoloads or eager-loads from outside `app/`
    # whose constant is not defined now that `eager_load!` has returned.
    #
    # The constant is the camelization of the file's path below the autoload
    # root, which is the rule both autoloaders apply -- `lib/question_presenter.rb`
    # is `QuestionPresenter` and `lib/foo/bar.rb` is `Foo::Bar`.  Measured
    # against the repository rather than assumed: all seven `lib/*.rb` files it
    # ships define exactly the camelization of their own filename, and the only
    # other things under `lib/` are `lib/assets/.keep` and two `.rake` files,
    # none of which is a `.rb` and so none of which is asked about.
    #
    # `app/` is excluded because `eager_load!` already raises on a mismatch
    # there under Zeitwerk and this reading would only repeat `ok`.  A path
    # that does not exist contributes nothing rather than an error: pointing
    # `autoload_paths` at a missing directory is `autoload_only`'s business.
    def undefined_outside_app(declared)
      asked = @outside.map { |rel| File.join(@app, rel) }
      roots = (declared + asked).select { |p| p.start_with?(@app + '/') }
              .reject { |p| p == @app + '/app' || p.start_with?(@app + '/app/') }
              .select { |p| File.directory?(p) }.uniq
      roots.flat_map do |root|
        # `map` + `compact` rather than `filter_map`: this runs under the
        # candidate's interpreter, and the earliest rung of the ladder this
        # probe has to work on is 2.3.8, where `filter_map` does not exist.
        Dir.glob(File.join(root, '**', '*.rb')).sort.map do |file|
          name = file.sub(/\A#{Regexp.escape(root)}\//, '').sub(/\.rb\z/, '')
          constant = ActiveSupport::Inflector.camelize(name)
          next if const_there?(constant)

          "#{file.sub(@app + '/', '')} (#{constant})"
        end.compact
      end.uniq.sort
    end

    # `const_defined?` raises NameError on a name that is not a constant at
    # all, which a file called `lib/2020_report.rb` would produce.  That is an
    # undefined constant for this reading's purpose, not a crashed probe.
    def const_there?(name)
      Object.const_defined?(name)
    rescue NameError
      false
    end

    # `Zeitwerk::Loader.eager_load_all` walks the loaders in
    # `Zeitwerk::Registry`, so on a tree where Zeitwerk is not the autoloader
    # the registry is empty and the call succeeds by having nothing to do.
    # Measured, not assumed: the golden step-05 tree with
    # `config.autoloader = :classic` added after `load_defaults 6.0` boots with
    # `Rails.autoloaders.zeitwerk_enabled? == false` and
    # `Zeitwerk::Registry.loaders == []`, and a probe that asked only
    # `defined?(Zeitwerk)` -- true there because railties requires the gem
    # either way -- would answer `ok => true` on it.  So the question the probe
    # has to ask is whether Zeitwerk is in force, not whether it is loadable.
    #
    # `zeitwerk_enabled?` is the reading where the release offers it.  A release
    # that has dropped the classic autoloader altogether drops the question with
    # it, and there `Rails.autoloaders` responding to `main` is the corroborating
    # reading that the Zeitwerk loaders are the application's -- the same pair
    # the `autoloader` probe above uses.  Neither available is "cannot be asked",
    # which is reported rather than passed.
    def zeitwerk_check
      return { 'skipped' => 'Rails.autoloaders not available' } unless Rails.respond_to?(:autoloaders)

      loaders = Rails.autoloaders
      enabled = if loaders.respond_to?(:zeitwerk_enabled?)
                  loaders.zeitwerk_enabled?
                elsif loaders.respond_to?(:main)
                  true
                end
      return { 'skipped' => 'Rails.autoloaders cannot be asked which autoloader is in force' } if enabled.nil?

      unless enabled
        return { 'zeitwerk_enabled' => false,
                 'config_autoloader' => autoloader_config_value,
                 'registered_loaders' => registered_zeitwerk_loaders }
      end
      return { 'error' => 'zeitwerk is enabled but the Zeitwerk constant is undefined' } unless defined?(Zeitwerk)

      Zeitwerk::Loader.eager_load_all
      { 'ok' => true, 'loaders' => (loaders.map { |loader| loader.tag.to_s } if loaders.respond_to?(:map)) }
    end

    def autoloader_config_value
      config = Rails.application.config
      config.autoloader.to_s if config.respond_to?(:autoloader)
    end

    def registered_zeitwerk_loaders
      return nil unless defined?(Zeitwerk::Registry) && Zeitwerk::Registry.respond_to?(:loaders)

      Zeitwerk::Registry.loaders.map { |loader| loader.tag.to_s }
    end

    def asset_pipeline
      config = Rails.application.config
      {
        'config_assets' => config.respond_to?(:assets),
        # `sprockets-rails` installs `config.assets`; Propshaft installs it too
        # but with a different class, so the class name is what separates them.
        'assets_class' => (config.assets.class.name if config.respond_to?(:assets)),
        # `config.assets.unknown_asset_fallback` is the one setting
        # `load_defaults "5.1"` adds on top of `load_defaults "5.0"`
        # (railties `application/configuration.rb`), and sprockets-rails 3.x
        # installs it as `true` in its own railtie.  Three readings are
        # distinguishable and all three matter: `true` is the pipeline still
        # falling back to the public path for a name it does not know, `false`
        # is 5.1's answer, and `nil` is a pipeline that has no such setting at
        # all (sprockets-rails 2.x, Propshaft).
        #
        # The second `respond_to?` is the whole difference between a reading
        # and a lost probe: sprockets-rails 2.x installs `config.assets`
        # without this setting, so asking for it there raises NoMethodError,
        # and because every reading in this method shares one `probe` rescue,
        # that one raise would replace `config_assets`, `assets_class` and the
        # four `*_loaded` flags with a single `error` string.
        'unknown_asset_fallback' =>
          (if config.respond_to?(:assets) &&
              config.assets.respond_to?(:unknown_asset_fallback)
             config.assets.unknown_asset_fallback
           end),
        'sprockets_loaded' => Gem.loaded_specs.key?('sprockets'),
        'propshaft_loaded' => Gem.loaded_specs.key?('propshaft'),
        'importmap_loaded' => Gem.loaded_specs.key?('importmap-rails'),
        'webpacker_loaded' => Gem.loaded_specs.key?('webpacker'),
        'coffee_loaded' => Gem.loaded_specs.key?('coffee-rails') ||
          Gem.loaded_specs.key?('coffee-script')
      }
    end

    # The name asked about below.  It is not one of the repository's audios,
    # and a tree that made it one would be answering a different question.
    MISS_NAME = 'cv-probe-not-an-asset'

    # What this application's own audio-URL paths answer for a name that is not
    # a pipeline asset, and what they raise or warn on the way.
    #
    # Every question and error this application plays over the phone is reached
    # through an audio URL whose name comes from the data -- a question's
    # `short_name`, a `TwilioSessionError`'s message -- so those names have
    # never been pipeline assets.  With sprockets-rails' `unknown_asset_fallback`
    # on, `audio_url` answers them from the public path and warns, through
    # `ActiveSupport::Deprecation`, that the fallback is going away and that
    # `skip_pipeline: true` is how to keep it (`sprockets/rails/helper.rb`,
    # `compute_asset_path`).  With it off, the same call raises
    # `Sprockets::Rails::Helper::AssetNotFound`.
    #
    # So neither half of the obvious reading separates the two arrangements:
    # both answer `/audios/<name>.mp3`, and "it raises" separates them the
    # wrong way round.  What separates them is whether the answer arrives
    # *quietly* -- a tree that asks for the public path explicitly is the only
    # one of the three that neither raises nor warns.
    def audio
      # Read here rather than taken from `asset_pipeline` above, because the
      # question each `_strict` field below answers is a single one -- "the
      # pipeline no longer falls back to the public folder, *and* the
      # application's own data-driven audio lookups still answer" -- and it is
      # only worth asking of a tree where the first half holds.
      config = Rails.application.config
      fallback =
        (config.assets.unknown_asset_fallback if config.respond_to?(:assets))
      readings = {
        'miss_name' => MISS_NAME,
        'fallback' => fallback,
        'helper_module' => (defined?(VoiceFeedbackHelper) ? 'VoiceFeedbackHelper' : nil),
        'error_class' => (defined?(TwilioSessionError) ? 'TwilioSessionError' : nil)
      }
      readings.update(watch('helper') do
        view_helpers.voice_file_path(MISS_NAME)
      end)
      readings.update(watch('error') do
        TwilioSessionError.new(MISS_NAME).voice_file
      end)
      # Not "it answered something" but "it answered the public path": the
      # directory the repository keeps these files in, and the one its own
      # suite pins.
      %w[helper error].each do |label|
        readings["#{label}_public_path"] =
          readings["#{label}_answer"].to_s.include?("/audios/#{MISS_NAME}.mp3")
        readings["#{label}_clean"] =
          readings["#{label}_ok"] && readings["#{label}_public_path"]
        readings["#{label}_strict"] =
          (fallback == false) && readings["#{label}_clean"]
      end
      readings
    end

    # The application's own helpers, reachable the way a view reaches them.
    #
    # `ActionController::Base.helpers` is *not* that: it is a view context
    # carrying the framework's helpers and `ActionController::Base._helpers`,
    # which for the abstract base class is empty, so a call to one of this
    # application's helpers raises `NoMethodError` there.  `ApplicationController`
    # is the class that has `include_all_helpers` applied to it, and its proxy
    # is the one that answers.
    def view_helpers
      if defined?(ApplicationController) && ApplicationController.respond_to?(:helpers)
        ApplicationController.helpers
      elsif defined?(ActionController::Base)
        ActionController::Base.helpers
      end
    end

    # Parameterised mailers, which Action Mailer 5.1 introduced.
    #
    # Two halves, and both are read: the framework supports `with` at all, and
    # each of this application's own mailer actions takes no arguments -- a
    # parameterised action reads `params`, and one that still takes its subject
    # as an argument has an arity that says so.  Asked of the class rather than
    # of a delivery, so a mailer whose body would raise is still read.
    def mailers
      return { 'action_mailer' => false } unless defined?(ActionMailer::Base)

      own = ActionMailer::Base.descendants.reject do |klass|
        klass.name.nil? || klass.name.start_with?('ActionMailer::')
      end
      actions = {}
      declared = {}
      own.sort_by { |klass| klass.name }.each do |klass|
        klass.action_methods.to_a.sort.each do |name|
          method = begin
            klass.instance_method(name)
          rescue NameError
            nil
          end
          key = "#{klass.name}##{name}"
          actions[key] = method && method.arity
          declared[key] = actions[key] if method && method.owner == klass
        end
      end
      {
        'action_mailer' => true,
        'with_supported' => ActionMailer::Base.respond_to?(:with),
        'mailers' => own.map { |klass| klass.name }.sort,
        # Everything Action Mailer would route to, which on this application is
        # more than the mail it sends: `NotificationMailer` includes a helper
        # module, and a module's public instance methods become action methods
        # of the class that includes them.  That is the framework's own answer
        # and is reported as it stands, but it is not what this reads, because
        # a helper's arity is the helper's business.
        'action_arity' => actions,
        # The mail this application actually sends: the action methods the
        # mailer class itself defines.
        'declared_action_arity' => declared,
        'all_parameterized' =>
          !declared.empty? && declared.values.all? { |arity| arity == 0 } &&
          ActionMailer::Base.respond_to?(:with)
      }
    end

    # The deprecation collector, borrowed from `depdrive.rb` so that "what did
    # the framework complain about, and where" has one implementation.  It is
    # installed lazily and after boot: `install` prepends to
    # `ActiveSupport::Deprecation`, which the booted application has already
    # loaded and configured, and a prepend survives every form of silencing the
    # tree can ask for -- `behavior = :silence`, `silenced = true`, a `silence`
    # block -- because all of it is downstream of the method being prepended.
    def deprecations
      return @deprecations if defined?(@deprecations)

      require_relative 'depdrive'
      @deprecations = []
      CVGate::DepDrive.install(@app, @deprecations)
      @deprecations
    end

    # Run a block and report what it answered, what it raised, and which
    # deprecations it raised while doing so.  `_ok` is the three together: an
    # answer, no exception, and nothing warned about.
    def watch(label)
      seen = deprecations
      mark = seen.length
      value = nil
      raised = nil
      begin
        value = yield
      rescue Exception => e # rubocop:disable Lint/RescueException
        raised = "#{e.class}: #{e.message}"
      end
      warned = (seen[mark..-1] || []).map { |record| record['message'].to_s[0, 200] }
      {
        "#{label}_answer" => (value.nil? ? nil : value.to_s),
        "#{label}_raised" => raised,
        "#{label}_deprecations" => warned,
        "#{label}_ok" => raised.nil? && warned.empty? && !value.to_s.empty?
      }
    end

    # The runtime counterpart to the lockfile: what actually got activated.
    def loaded_gems
      Hash[Gem.loaded_specs.map { |name, spec| [name, spec.version.to_s] }.sort]
    end

    def framework_defaults_files
      Dir.glob(File.join(@app, 'config', 'initializers', 'new_framework_defaults*'))
         .map { |p| p.sub(@app + '/', '') }.sort
    end

    # Which class the application's own models actually descend from, and which
    # of their `belongs_to` the framework treats as required.
    #
    # `ApplicationRecord` is a file a grep can find whether or not anything
    # inherits from it, so the question is asked of the constant graph after the
    # eager_load probe has populated it: every model's real superclass, and the
    # ones still reaching `ActiveRecord::Base` directly.  Rails' own internal
    # records are namespaced under `ActiveRecord::` and are dropped by that
    # prefix; a model named `ActiveRecordThing` keeps its place, because the
    # `::` is part of what is matched.
    def models
      return { 'active_record' => false } unless defined?(ActiveRecord::Base)

      own = ActiveRecord::Base.descendants.reject do |klass|
        klass.name.nil? || klass.name.start_with?('ActiveRecord::')
      end
      direct = own.select { |klass| klass.superclass == ActiveRecord::Base }
                  .map { |klass| klass.name }.sort
      belongs_to = Hash[own.flat_map { |klass|
        klass.reflect_on_all_associations(:belongs_to).map do |ref|
          ["#{klass.name}##{ref.name}", !optional_reflection?(ref)]
        end
      }.sort]
      {
        'active_record' => true,
        'application_record' => (defined?(ApplicationRecord) ? 'ApplicationRecord' : nil),
        'application_record_abstract' =>
          (ApplicationRecord.abstract_class? if defined?(ApplicationRecord)),
        'application_record_parent' =>
          (ApplicationRecord.superclass.name if defined?(ApplicationRecord)),
        'count' => own.length,
        'direct_base_subclasses' => direct,
        # The gate reads this one: `ApplicationRecord` is *supposed* to sit
        # directly under `ActiveRecord::Base`, so it is not a stray.
        'stray_base_subclasses' => direct - ['ApplicationRecord'],
        'parents' => Hash[own.map { |klass| [klass.name, klass.superclass.name] }.sort],
        'belongs_to' => belongs_to,
        # How many of them the framework ends up treating as optional.  The
        # application-wide default is a separate reading; this is the
        # difference between naming the one association whose parent is not
        # known when the row is created and marking every association
        # optional, which leaves the default switched on and gives up
        # everything it was switched on for.
        'belongs_to_optional_count' => belongs_to.values.count { |req| !req },
        # Validators whose `:if`/`:unless` is a String.  Rails 5.1 deprecates a
        # String condition -- it is `instance_eval`ed against the record -- and
        # 5.2 removes it.  Read off the validator objects the class is holding
        # rather than off the source, so a condition written in a concern, or
        # built by a loop, or passed through `validates_with`, is read the same
        # way as one written in the model.
        'string_conditions' => string_conditions(own)
      }
    end

    def string_conditions(own)
      own.flat_map { |klass|
        next [] unless klass.respond_to?(:validators)

        klass.validators.flat_map do |validator|
          options = validator.options || {}
          [:if, :unless].flat_map do |key|
            Array(options[key]).select { |cond| cond.is_a?(String) }
                               .map { |cond| "#{klass.name} #{key}: #{cond}" }
          end
        end
      }.sort
    end

    # Is this `belongs_to` optional?  An explicit `optional:`/`required:` on the
    # association wins; otherwise it is the application-wide default, which does
    # not exist before Rails 5.0 and is read as "optional" there.
    def optional_reflection?(ref)
      options = ref.options
      return options[:optional] ? true : false if options.key?(:optional)
      return options[:required] ? false : true if options.key?(:required)
      !(ActiveRecord::Base.respond_to?(:belongs_to_required_by_default) &&
        ActiveRecord::Base.belongs_to_required_by_default)
    end

    # The Rails 5.0 upgrade defaults, read from the booted application rather
    # than from the initializer that is meant to set them.  The generator ships
    # `new_framework_defaults.rb` with every line commented out, so "the file is
    # there" and "the defaults are on" are different facts and only the second
    # one is worth a gate.  Each value is read from the class that consumes it
    # where there is one, and from `config` only as a fallback.
    def framework_defaults
      config = Rails.application.config
      controller = defined?(ActionController::Base) ? ActionController::Base : nil
      {
        'belongs_to_required_by_default' =>
          (ActiveRecord::Base.belongs_to_required_by_default if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:belongs_to_required_by_default)),
        'per_form_csrf_tokens' =>
          setting(controller, :per_form_csrf_tokens, config.action_controller),
        'forgery_protection_origin_check' =>
          setting(controller, :forgery_protection_origin_check, config.action_controller),
        # 8.1 deprecates this reader and raises the notice AS THE READING
        # (see `defaults_8_0` below for the measurement).  This probe must not
        # be the source of a deprecation in a job whose whole business is
        # counting them -- the reason `halt_callback_chains_on_return_false`
        # two lines down is read the way it is -- so the probe stops asking
        # above 8.0 and says why in the evidence rather than dropping the key.
        # `'withdrawn'` is not the 5.0 value either, so `adoption` still lists
        # it under `not_adopted`, and the guard leaves this key out of its
        # `absent_keys` from step 10 on.
        'to_time_preserves_timezone' =>
          if rails_at_least?('8.1')
            'withdrawn'
          elsif ActiveSupport.respond_to?(:to_time_preserves_timezone)
            ActiveSupport.to_time_preserves_timezone
          end,
        'halt_callback_chains_on_return_false' => halts_callback_chain_on_false,
        'ssl_options' => config.ssl_options.inspect
      }.tap { |readings| readings.update(adoption(readings)) }
    end

    # Which of the five defaults `rails new` writes into
    # `config/initializers/new_framework_defaults.rb` -- other than
    # `belongs_to_required_by_default`, which is asked about on its own because
    # it is the one with a visible consequence for this application -- are at
    # their Rails 5.0 value.
    #
    # `not_adopted` and not just a count: a gate that reads a number tells its
    # reader that four of five are right, and a gate that reads this tells them
    # which one is not.  Both are reported, because the count is the thing that
    # has to move and the list is the thing that has to be acted on.
    #
    # A `nil` reading is "the framework has no such setting", which is what
    # every one of these is on 4.2.  It is not adopted, and it is not an error.
    # Does a callback that returns `false` still halt the chain?
    #
    # Asked of the framework's own callback machinery and not of
    # `ActiveSupport.halt_callback_chains_on_return_false`, because that reader
    # is a different object in every release that has it: on 5.0 it is the
    # switch; on 5.1 it is a deprecated no-op that returns `nil` and raises a
    # deprecation when it is *read*; from 5.2 it is gone.  Reading it would
    # report "not adopted" for a release that made the adoption unconditional,
    # and would make this probe the source of a deprecation warning in a job
    # whose whole business is counting them.
    #
    # The behaviour is the same question in every release.  Rails 5.0 installs
    # the false-halting terminator on Active Model's and Active Record's
    # callback chains specifically (`active_model/validations/callbacks.rb`,
    # `active_record/transactions.rb`), not on `ActiveSupport::Callbacks` in
    # general, so the probe asks an Active Model validation chain and not a
    # bare one: a `before_validation` that returns `false`, and whether the
    # validations after it ran.
    def halts_callback_chain_on_false
      return nil unless defined?(ActiveModel::Validations::Callbacks)

      probe = Class.new do
        include ActiveModel::Validations
        include ActiveModel::Validations::Callbacks
        before_validation { false }
        def self.name
          'CVHaltProbe'
        end
      end
      !probe.new.valid?
    rescue StandardError, ScriptError
      nil
    end

    def adoption(readings)
      wanted = { 'per_form_csrf_tokens' => true,
                 'forgery_protection_origin_check' => true,
                 'to_time_preserves_timezone' => true,
                 'halt_callback_chains_on_return_false' => false }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      # `ssl_options` is a hash and is compared on the one member the template
      # sets, so that an application which adds its own SSL options beside the
      # HSTS one still counts as having adopted it.
      ssl = Rails.application.config.ssl_options
      hsts = ssl.is_a?(Hash) ? ssl[:hsts] : nil
      missed << 'ssl_options' unless hsts.is_a?(Hash) && hsts[:subdomains] == true
      { 'not_adopted' => missed.sort,
        'adopted' => (wanted.length + 1) - missed.length }
    end

    # Which of the settings `load_defaults "5.2"` throws are at their 5.2 value.
    #
    # Read out of railties-5.2.8.1's own `load_defaults` body rather than off
    # the upgrade guide: the `"5.2"` branch is the `"5.1"` branch plus exactly
    # six assignments, and the six are below.
    #
    # `form_with_generates_ids` is measured but deliberately not in `wanted`.
    # It is the one of the six with a visible consequence for this
    # application's two forms, so it is asked about on its own -- the same
    # arrangement `framework_defaults` uses for `belongs_to_required_by_default`.
    #
    # Two of the six do not have the reader their `config` name suggests, and
    # a gate written against the obvious one would read `nil` on a correct
    # tree:
    #
    #   * `use_sha1_digests` has no reader at all.  The observable is which
    #     class `ActiveSupport::Digest` delegates to -- `Digest::MD5` before
    #     the switch and `Digest::SHA1` after it.
    #   * `form_with_generates_ids` is assigned onto
    #     `ActionView::Helpers::FormHelper` by an Action View initializer, so
    #     `config.action_view.form_with_generates_ids` is `nil` even when the
    #     flag is on.  The module-level accessor is the reading; `config` is
    #     kept only as the fallback, for a release that has not run the
    #     initializer.
    #
    # A `nil` reading is "the framework has no such setting", which is what
    # every one of these is on 5.1.  It is not adopted, and it is not an error.
    def defaults_5_2
      config = Rails.application.config
      controller = defined?(ActionController::Base) ? ActionController::Base : nil
      form_helper = defined?(ActionView::Helpers::FormHelper) ? ActionView::Helpers::FormHelper : nil
      encryptor = defined?(ActiveSupport::MessageEncryptor) ? ActiveSupport::MessageEncryptor : nil
      readings = {
        'cache_versioning' =>
          (ActiveRecord::Base.cache_versioning if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:cache_versioning)),
        'use_authenticated_cookie_encryption' =>
          setting(nil, :use_authenticated_cookie_encryption, config.action_dispatch),
        'use_authenticated_message_encryption' =>
          setting(encryptor, :use_authenticated_message_encryption, config.active_support),
        'hash_digest_class' =>
          (ActiveSupport::Digest.hash_digest_class.name if
            defined?(ActiveSupport::Digest) &&
            ActiveSupport::Digest.respond_to?(:hash_digest_class)),
        'default_protect_from_forgery' =>
          setting(controller, :default_protect_from_forgery, config.action_controller),
        'form_with_generates_ids' =>
          setting(form_helper, :form_with_generates_ids, config.action_view)
      }
      wanted = { 'cache_versioning' => true,
                 'use_authenticated_cookie_encryption' => true,
                 'use_authenticated_message_encryption' => true,
                 'hash_digest_class' => 'Digest::SHA1',
                 'default_protect_from_forgery' => true }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      readings.merge('not_adopted' => missed.sort,
                     'adopted' => wanted.length - missed.length)
    end

    # The four settings `load_defaults 6.0` throws that this gate reads.  The
    # release's `"6.0"` branch is `"5.2"` plus `self.autoloader = :zeitwerk`
    # and nine assignments (railties 6.0.6.1,
    # `lib/rails/application/configuration.rb`).  The autoloader switch is read
    # by the `autoloader` probe above, because "zeitwerk is the autoloader" has
    # a reader of its own in `Rails.autoloaders` and is this release's
    # headline.  Of the five assignments this gate does not read, three belong
    # to Active Storage (`queues.analysis`, `queues.purge`,
    # `replace_on_assign_to_many`) and one to Active Job
    # (`return_false_on_aborted_enqueue`), and neither framework is in this
    # application's `config/application.rb` -- reading a setting off a
    # framework that was never required would be reading the OrderedOptions bag
    # the assignment went into, which says that `load_defaults` ran and nothing
    # about whether anything adopted it.  The fifth,
    # `action_dispatch.return_only_media_type_on_content_type = false`, IS
    # readable on this application -- `ActionDispatch::Response` carries it as a
    # cattr and the Action Dispatch railtie defaults it to true -- and is left
    # out deliberately: the declaration itself is gated separately on
    # `loaded_config_version`, which only the framework sets, so a fifth
    # reading of the same declaration buys no discrimination the fourth did not
    # already buy.
    #
    # `default_enforce_utf8` is wanted FALSE, which is why the comparison is
    # against the wanted value and not against truthiness: `nil` is a release
    # that has no such setting, and a release that cannot be asked has not
    # adopted anything.
    def defaults_6_0
      config = Rails.application.config
      form_tag = defined?(ActionView::Helpers::FormTagHelper) ? ActionView::Helpers::FormTagHelper : nil
      readings = {
        'default_enforce_utf8' =>
          setting(form_tag, :default_enforce_utf8, config.action_view),
        # No class-level reader on 6.0: `ActionDispatch::Cookies` takes it off
        # the request env, which the railtie fills from this setting.
        'use_cookies_with_metadata' =>
          setting(nil, :use_cookies_with_metadata, config.action_dispatch),
        'collection_cache_versioning' =>
          (ActiveRecord::Base.collection_cache_versioning if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:collection_cache_versioning)),
        # A String on 6.0 and a class on later releases; `to_s` is the same
        # answer either way, and `nil.to_s` would be `''` rather than the name,
        # so the reading is only taken when there is something to read.
        'delivery_job' =>
          (if defined?(ActionMailer::Base) &&
              ActionMailer::Base.respond_to?(:delivery_job)
             ActionMailer::Base.delivery_job.to_s
           end)
      }
      wanted = { 'default_enforce_utf8' => false,
                 'use_cookies_with_metadata' => true,
                 'collection_cache_versioning' => true,
                 'delivery_job' => 'ActionMailer::MailDeliveryJob' }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      readings.merge('not_adopted' => missed.sort,
                     'adopted' => wanted.length - missed.length)
    end

    # The seven settings `load_defaults 6.1` throws that this gate reads.  The
    # release's `"6.1"` branch is `"6.0"` plus sixteen assignments (railties
    # 6.1.7.10, `lib/rails/application/configuration.rb:160-207`), and the
    # nine this gate leaves out are left out for reasons already argued at
    # `defaults_6_0`:
    #
    #   track_variants, queues.analysis, queues.purge        Active Storage
    #   queues.incineration, queues.routing                  Action Mailbox
    #     Neither framework is required by this application's
    #     `config/application.rb`, and `ActiveStorage` is not even a defined
    #     constant on the booted tree -- the readings would come back off the
    #     `OrderedOptions` bag the assignment went into, which says that
    #     `load_defaults` ran and nothing about whether anything adopted it.
    #
    #   retry_jitter, skip_after_callbacks_if_terminated     Active Job
    #   deliver_later_queue_name                             Action Mailer
    #     Active Job is on the load path because Action Mailer drags it there,
    #     not because this application asked for it, and this application
    #     enqueues nothing: `NotificationMailer` is delivered inline.
    #     `deliver_later_queue_name` is additionally the one setting in the
    #     branch whose WANTED value is `nil`, which is also what a reading that
    #     cannot be taken returns -- a gate that cannot tell adoption from
    #     absence is not a gate.
    #
    #   ssl_default_redirect_status
    #     Consumed as a constructor keyword by `ActionDispatch::SSL`
    #     (`default_middleware_stack.rb:20`), stored in an ivar with no reader
    #     (`ssl.rb:70`), and the middleware is only in the stack when
    #     `config.force_ssl` is on, which it is not here.  The only reading
    #     available is off `config.action_dispatch`, i.e. a restatement of the
    #     declaration `loaded_config_version` already gates.
    #
    # Of the seven that are read, five have a class-level reader at the place
    # the framework really consults them, and the reader is used rather than
    # the config bag because Action View's railtie DELETES its two keys as it
    # assigns them (`actionview/lib/action_view/railtie.rb`) and Action
    # Record's and Action Controller's leave the bag holding `nil` on a 6.0
    # tree while the class already answers `true`/`false`.  Measured both ways
    # on this application: `config.action_view.preload_links_header` is `nil`
    # under 6.0 AND under 6.1, so the obvious reading discriminates nothing.
    #
    # `cookies_same_site_protection` has no class-level reader on 6.1 either:
    # `ActionDispatch::Cookies` takes it off the request env
    # (`cookies.rb:72,434`), which the railtie fills from this setting.  Same
    # shape as `use_cookies_with_metadata` at `defaults_6_0`.
    def defaults_6_1
      config = Rails.application.config
      form_helper = defined?(ActionView::Helpers::FormHelper) ? ActionView::Helpers::FormHelper : nil
      asset_tag = defined?(ActionView::Helpers::AssetTagHelper) ? ActionView::Helpers::AssetTagHelper : nil
      readings = {
        'has_many_inversing' =>
          (ActiveRecord::Base.has_many_inversing if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:has_many_inversing)),
        'legacy_connection_handling' =>
          (ActiveRecord::Base.legacy_connection_handling if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:legacy_connection_handling)),
        'form_with_generates_remote_forms' =>
          setting(form_helper, :form_with_generates_remote_forms, config.action_view),
        'preload_links_header' =>
          setting(asset_tag, :preload_links_header, config.action_view),
        'urlsafe_csrf_tokens' =>
          (ActionController::Base.urlsafe_csrf_tokens if
            defined?(ActionController::Base) &&
            ActionController::Base.respond_to?(:urlsafe_csrf_tokens)),
        'cookies_same_site_protection' =>
          setting(nil, :cookies_same_site_protection, config.action_dispatch).to_s,
        'utc_to_local_returns_utc_offset_times' =>
          (ActiveSupport.utc_to_local_returns_utc_offset_times if
            ActiveSupport.respond_to?(:utc_to_local_returns_utc_offset_times))
      }
      # Three of the seven are wanted FALSE, so every comparison is against the
      # wanted value rather than against truthiness: `nil` is a release that
      # has no such setting, and a release that cannot be asked has not adopted
      # anything.
      wanted = { 'has_many_inversing' => true,
                 'legacy_connection_handling' => false,
                 'form_with_generates_remote_forms' => false,
                 'preload_links_header' => true,
                 'urlsafe_csrf_tokens' => true,
                 'cookies_same_site_protection' => 'lax',
                 'utc_to_local_returns_utc_offset_times' => true }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      readings.merge('not_adopted' => missed.sort,
                     'adopted' => wanted.length - missed.length)
    end

    # The "7.0" branch of railties-7.0.10
    # lib/rails/application/configuration.rb:280-330, restricted to the
    # frameworks this application loads.  Active Storage's five and Active
    # Job's are not read: neither railtie is required by
    # `config/application.rb`, so `respond_to?(:active_storage)` is false and
    # the branch never assigns them -- a reading there would be a reading of
    # the framework's own absence, which `loaded_gems` already reports.
    #
    # Fourteen readings, each taken where the setting is really consulted
    # rather than off `config.*`, for the reason `defaults_6_0` records: a
    # railtie that DELETES the key as it assigns it leaves the obvious reader
    # nil on a tree that has the default.  Two of the fourteen are read
    # through their effect rather than through a flag, because 7.0 offers no
    # flag to read:
    #
    #   disable_to_s_conversion            `TimeWithZone#to_s` loses the
    #                                      `format` parameter, so the ARITY is
    #                                      the setting.  This is the release's
    #                                      one hard break in this application
    #                                      -- two views call `to_s(:db)`-style
    #                                      formats -- and the reading has to
    #                                      survive being asked of 6.1, where
    #                                      the method takes a splat (-1).
    #   remove_deprecated_time_with_zone_name
    #                                      `TimeWithZone.name` answers "Time"
    #                                      on 6.1 and its own name on 7.0.
    #
    # `cookies_serializer` and `action_mailer.smtp_timeout` were measured and
    # DROPPED: both read nil through `config.*` on 7.0 as well as on 6.1, so a
    # gate on either would be a gate on a reader that answers the same on both
    # sides of the step.
    # `return_only_request_media_type_on_content_type` is dropped for the
    # opposite reason -- 7.0 renames the 6.1 setting, so the reading changes
    # spelling rather than value and says nothing about adoption.
    def defaults_7_0
      config = Rails.application.config
      url_helper = defined?(ActionView::Helpers::UrlHelper) ? ActionView::Helpers::UrlHelper : nil
      asset_tag = defined?(ActionView::Helpers::AssetTagHelper) ? ActionView::Helpers::AssetTagHelper : nil
      headers = (config.action_dispatch.default_headers if
                   config.action_dispatch.respond_to?(:default_headers)) || {}
      readings = {
        'hash_digest_class' =>
          (ActiveSupport::Digest.hash_digest_class.to_s if
            defined?(ActiveSupport::Digest) &&
            ActiveSupport::Digest.respond_to?(:hash_digest_class)),
        'key_generator_hash_digest_class' =>
          (ActiveSupport::KeyGenerator.hash_digest_class.to_s if
            defined?(ActiveSupport::KeyGenerator) &&
            ActiveSupport::KeyGenerator.respond_to?(:hash_digest_class)),
        'cache_format_version' =>
          (ActiveSupport.cache_format_version if
            ActiveSupport.respond_to?(:cache_format_version)),
        'use_rfc4122_namespaced_uuids' =>
          (Digest::UUID.use_rfc4122_namespaced_uuids if
            defined?(Digest::UUID) &&
            Digest::UUID.respond_to?(:use_rfc4122_namespaced_uuids)),
        'time_with_zone_name' =>
          (ActiveSupport::TimeWithZone.name if defined?(ActiveSupport::TimeWithZone)),
        'to_s_arity' =>
          (ActiveSupport::TimeWithZone.instance_method(:to_s).arity if
            defined?(ActiveSupport::TimeWithZone)),
        'button_to_generates_button_tag' =>
          setting(url_helper, :button_to_generates_button_tag, config.action_view),
        'apply_stylesheet_media_default' =>
          setting(asset_tag, :apply_stylesheet_media_default, config.action_view),
        'automatic_scope_inversing' =>
          (ActiveRecord::Base.automatic_scope_inversing if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:automatic_scope_inversing)),
        'partial_inserts' =>
          (ActiveRecord::Base.partial_inserts if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:partial_inserts)),
        'verify_foreign_keys_for_fixtures' =>
          (ActiveRecord.verify_foreign_keys_for_fixtures if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:verify_foreign_keys_for_fixtures)),
        'raise_on_open_redirects' =>
          (ActionController::Base.raise_on_open_redirects if
            defined?(ActionController::Base) &&
            ActionController::Base.respond_to?(:raise_on_open_redirects)),
        'action_on_open_redirect' =>
          (ActionController::Base.action_on_open_redirect.to_s if
            defined?(ActionController::Base) &&
            ActionController::Base.respond_to?(:action_on_open_redirect)),
        'wrap_parameters_by_default' =>
          setting(nil, :wrap_parameters_by_default, config.action_controller),
        'x_xss_protection' => headers['X-XSS-Protection']
      }
      # Three of the fourteen are wanted FALSE or wanted a number, so as in
      # `defaults_6_1` every comparison is against the wanted value and never
      # against truthiness.
      wanted = { 'hash_digest_class' => 'OpenSSL::Digest::SHA256',
                 'key_generator_hash_digest_class' => 'OpenSSL::Digest::SHA256',
                 'cache_format_version' => 7.0,
                 'use_rfc4122_namespaced_uuids' => true,
                 'time_with_zone_name' => 'ActiveSupport::TimeWithZone',
                 'to_s_arity' => 0,
                 'button_to_generates_button_tag' => true,
                 'apply_stylesheet_media_default' => false,
                 'automatic_scope_inversing' => true,
                 'partial_inserts' => false,
                 'verify_foreign_keys_for_fixtures' => true,
                 'wrap_parameters_by_default' => true,
                 'x_xss_protection' => '0' }
      # The fourteenth is asked for under the name the RUNNING release keeps it
      # under, which is not the same name on both sides of 8.1.  Rails 8.1
      # replaces the boolean `raise_on_open_redirects` with the enum
      # `action_on_open_redirect` (`:log` / `:raise`) and rewrites its own "7.0"
      # branch to assign `action_on_open_redirect = :raise`
      # (railties-8.1.3.1 application/configuration.rb:266); the boolean is left
      # in place as a `mattr_accessor` defaulting to FALSE
      # (actionpack-8.1.3.1 action_controller/metal/redirecting.rb:29) and is
      # consulted first by `_allow_other_host` (:262), so the behaviour of a
      # correctly-migrated tree is unchanged and only the place it is written
      # down has moved.  Read the boolean alone and every Rails 8.1 tree reports
      # this 7.0 default as not adopted, which would fail the inherited
      # `defaults_7_0_adopted` guard from step 11 up and zero 0.55 of the reward
      # on a tree that did nothing wrong.  Both readings are reported either way
      # -- the one the release does not have answers nil -- and `renamed` says
      # which one was compared, so "the release moved this" and "the tree
      # declined this" cannot read the same.  Reading the boolean is silent on
      # 8.1: the deprecation in that release's `action_controller.open_redirects`
      # initializer (railtie.rb:114-119) fires on an application that SETS
      # `config.action_controller.raise_on_open_redirects`, which this one does
      # not.  Measured on three trees.
      renamed = rails_at_least?('8.1')
      if renamed
        wanted['action_on_open_redirect'] = 'raise'
      else
        wanted['raise_on_open_redirects'] = true
      end
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      # Two more readings about `to_s_arity`, reported and not compared,
      # because that one default is the only one of the fourteen whose effect
      # depends on WHEN a file was required.  railties
      # application/bootstrap.rb:17 turns
      # `config.active_support.disable_to_s_conversion` into
      # `ENV['RAILS_DISABLE_DEPRECATED_TO_S_CONVERSION']`, and activesupport
      # core_ext/time.rb:7 reads that ENV at require time -- so a tree that
      # loads `active_support/core_ext/time` during `Bundler.require`, which
      # runs before the bootstrap initializer, keeps the format-argument
      # `to_s` even though it declared the defaults.  The arity stays the
      # gated reading, because a tree where the conversion still works is a
      # tree where the default is not in force whatever the flag says; these
      # two are what tell the two apart in the evidence rather than leaving a
      # bare `-1`.
      readings.merge(
        'not_adopted' => missed.sort,
        'adopted' => wanted.length - missed.length,
        'renamed' =>
          (renamed ? { 'raise_on_open_redirects' => 'action_on_open_redirect' } : {}),
        'disable_to_s_conversion' =>
          setting(nil, :disable_to_s_conversion, config.active_support),
        'deprecated_to_s_loaded' =>
          $LOADED_FEATURES.any? { |f|
            f.end_with?('time/deprecated_conversions.rb')
          }
      )
    end

    # The "7.1" branch of railties-7.1.6
    # lib/rails/application/configuration.rb:337-425, restricted to what this
    # application can be asked about and to what actually MOVES between the
    # 7.0 defaults and the 7.1 ones on this tree.  Both restrictions matter.
    # The branch assigns some forty keys; on this application eighteen of them
    # are readable at runtime AND answer differently under the two
    # declarations, and those eighteen are what is here.  The rest were
    # measured and dropped (35 readings, 21 differ):
    #
    #   ERR NoMethodError on 7.1.6 -- the setting exists in the guide and not
    #   in this release's objects, so a reading would be a reading of the
    #   framework's own absence:
    #       ar_belongs_to_required_validates_foreign_key
    #       ar_query_log_tags_format
    #       ar_run_commit_callbacks_on_first_saved_instances_in_transaction
    #       as_use_message_serializer_for_metadata
    #       enc_support_sha1_for_non_deterministic_encryption
    #   the same under both declarations, so they say nothing about adoption:
    #       nokogiri_html5 (true either way -- the gem is in the bundle, the
    #                       sanitizer vendor is what the declaration moves)
    #   read elsewhere, deliberately:
    #       loaded_config_version   -- `load_defaults_7_1`, which is the
    #                                  declaration and not the adoption
    #       default_headers         -- 7.1 drops X-Download-Options, and that
    #                                  is the one change of the eighteen that
    #                                  reaches the wire, so it is read off a
    #                                  served response in `depdrive.headers`
    #                                  rather than off the class here
    #       lib_in_autoload_paths   -- that belongs to `config.autoload_lib`,
    #                                  which is a separate requirement that
    #                                  `load_defaults 7.1` does not imply
    #
    # Active Storage's and Active Job's keys are absent for `defaults_7_0`'s
    # reason: neither railtie is required by config/application.rb.
    def defaults_7_1
      config = Rails.application.config
      readings = {
        # Autoloading: 7.1 stops putting the autoload paths on $LOAD_PATH.
        # Safe on this tree -- nothing `require`s a lib/ constant by bare name
        # -- and it is the setting `config.autoload_lib` is designed to sit
        # beside, which is why the two requirements are adjacent in the
        # instruction and separate in the profile.
        'add_autoload_paths_to_load_path' =>
          (config.add_autoload_paths_to_load_path if
            config.respond_to?(:add_autoload_paths_to_load_path)),
        'precompile_filter_parameters' =>
          (config.precompile_filter_parameters if
            config.respond_to?(:precompile_filter_parameters)),

        # The HTML5 pivot.  Two readings and not one: the sanitizer the
        # helpers use and the parser rails-dom-testing compares with are
        # separate settings that 7.1 moves together, and a tree that pins
        # rails-html-sanitizer back would move one without the other.
        'av_sanitizer_vendor' =>
          (ActionView::Helpers::SanitizeHelper.sanitizer_vendor.to_s if
            defined?(ActionView::Helpers::SanitizeHelper) &&
            ActionView::Helpers::SanitizeHelper.respond_to?(:sanitizer_vendor)),
        'dom_testing_default_html_version' =>
          (config.dom_testing_default_html_version.to_s if
            config.respond_to?(:dom_testing_default_html_version)),

        # Active Record.  All read off the `ActiveRecord` module or
        # `ActiveRecord::Base` where the framework itself consults them.
        'ar_commit_transaction_on_non_local_return' =>
          (ActiveRecord.commit_transaction_on_non_local_return if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:commit_transaction_on_non_local_return)),
        'ar_allow_deprecated_singular_associations_name' =>
          (ActiveRecord.allow_deprecated_singular_associations_name if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:allow_deprecated_singular_associations_name)),
        'ar_raise_on_assign_to_attr_readonly' =>
          (ActiveRecord.raise_on_assign_to_attr_readonly if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:raise_on_assign_to_attr_readonly)),
        'ar_before_committed_on_all_records' =>
          (ActiveRecord.before_committed_on_all_records if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:before_committed_on_all_records)),
        'ar_run_after_transaction_callbacks_in_order_defined' =>
          (ActiveRecord.run_after_transaction_callbacks_in_order_defined if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:run_after_transaction_callbacks_in_order_defined)),
        # `.inspect` and not `.to_s`: the 7.1 value is nil and `nil.to_s` is
        # the empty string, which is also what a reading that RAISED and was
        # swallowed would look like.  "nil" is a value; "" is an accident.
        'ar_default_column_serializer' =>
          (ActiveRecord::Base.default_column_serializer.inspect if
            defined?(ActiveRecord::Base) &&
            ActiveRecord::Base.respond_to?(:default_column_serializer)),
        'ar_marshalling_format_version' =>
          (ActiveRecord::Marshalling.format_version if
            defined?(ActiveRecord::Marshalling) &&
            ActiveRecord::Marshalling.respond_to?(:format_version)),
        'ar_generate_secure_token_on' =>
          (ActiveRecord.generate_secure_token_on.to_s if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:generate_secure_token_on)),
        # Read off the live encryption config rather than off `config.*`: this
        # application encrypts a column from step 07 on, so this one is not a
        # setting nothing consults.
        'enc_hash_digest_class' =>
          (ActiveRecord::Encryption.config.hash_digest_class.to_s if
            defined?(ActiveRecord::Encryption) &&
            ActiveRecord::Encryption.respond_to?(:config)),

        # Action Dispatch / Action Controller / Active Support.
        'debug_exception_log_level' =>
          (config.action_dispatch.debug_exception_log_level.to_s if
            config.action_dispatch.respond_to?(:debug_exception_log_level)),
        'as_cache_format_version' =>
          (ActiveSupport.cache_format_version if
            ActiveSupport.respond_to?(:cache_format_version)),
        'as_message_serializer' =>
          setting(nil, :message_serializer, config.active_support).to_s,
        'as_raise_on_invalid_cache_expiration_time' =>
          (ActiveSupport::Cache::Store.raise_on_invalid_cache_expiration_time if
            defined?(ActiveSupport::Cache::Store) &&
            ActiveSupport::Cache::Store.respond_to?(:raise_on_invalid_cache_expiration_time)),
        'ac_allow_deprecated_parameters_hash_equality' =>
          (ActionController::Parameters.allow_deprecated_parameters_hash_equality if
            defined?(ActionController::Parameters) &&
            ActionController::Parameters.respond_to?(:allow_deprecated_parameters_hash_equality))
      }
      # Five of the eighteen are wanted FALSE and three are wanted a
      # particular string or number, so as in `defaults_6_1` and
      # `defaults_7_0` every comparison is against the wanted value and never
      # against truthiness.  Each value is the one MEASURED on a tree that
      # declares 7.1, beside the same tree declaring 7.0, not the one a guide
      # states.
      wanted = { 'add_autoload_paths_to_load_path' => false,
                 'precompile_filter_parameters' => true,
                 'av_sanitizer_vendor' => 'Rails::HTML5::Sanitizer',
                 'dom_testing_default_html_version' => 'html5',
                 'ar_commit_transaction_on_non_local_return' => true,
                 'ar_allow_deprecated_singular_associations_name' => false,
                 'ar_raise_on_assign_to_attr_readonly' => true,
                 'ar_before_committed_on_all_records' => true,
                 'ar_run_after_transaction_callbacks_in_order_defined' => true,
                 'ar_default_column_serializer' => 'nil',
                 'ar_marshalling_format_version' => 7.1,
                 'ar_generate_secure_token_on' => 'initialize',
                 'enc_hash_digest_class' => 'OpenSSL::Digest::SHA256',
                 'debug_exception_log_level' => 'error',
                 'as_cache_format_version' => 7.1,
                 'as_message_serializer' => 'json_allow_marshal',
                 'as_raise_on_invalid_cache_expiration_time' => true,
                 'ac_allow_deprecated_parameters_hash_equality' => false }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      readings.merge('not_adopted' => missed.sort,
                     'adopted' => wanted.length - missed.length)
    end

    # `config.load_defaults 7.2`: the five assignments railties-7.2.3.2 makes
    # in its "7.2" branch (lib/rails/application/configuration.rb:321-337).
    # Four of the five are readable on this application and the fifth cannot
    # be, which is recorded here rather than left as a silent gap:
    #
    #   active_storage.web_image_content_types
    #       config/application.rb requires the active_record,
    #       action_controller, action_mailer and sprockets railties and no
    #       others, so `respond_to?(:active_storage)` is false, the assignment
    #       never runs, and both `ActiveStorage.web_image_content_types` and
    #       the config key raise -- on the tree that has adopted 7.2 as well as
    #       on the one that has not.  A reading that is an error on both sides
    #       measures nothing.
    #
    # `yjit` is read as the CONFIGURATION and not as `RubyVM::YJIT.enabled?`.
    # The interpreter these images carry is built without YJIT --
    # `defined?(RubyVM::YJIT)` is nil on this 3.2.11 -- so the runtime state is
    # false on a tree that has done the work perfectly.  The runtime
    # availability is reported beside the setting so the two are never
    # confused, and the reading is false at every rung: no image on this
    # ladder installs a Rust compiler, so no interpreter it builds has YJIT.
    # That is a decision rather than an accident -- bookworm's own rustc does
    # build a YJIT-capable 3.4.10, measured.
    #
    # It is REPORTED AND NOT COUNTED IN `not_adopted`, which is the one of the
    # five this probe deliberately does not hold anyone to.  The other three
    # are each read off a framework object the setting reached -- Active Job's
    # own class, Active Record's module reader, the config key the PostgreSQL
    # adapter consults -- so each of them can catch a tree that declared 7.2
    # and then assigned the old value back.  `config.yjit` is read off the
    # very configuration object `load_defaults` writes to and nothing here
    # consults it: on this image it cannot mean anything, because the
    # interpreter has no YJIT to enable.  Counting it would make
    # `not_adopted` shorter by a reading whose only content is "the
    # `load_defaults` line was typed", which `load_defaults.loaded_config_version`
    # already says more directly.
    #
    # `postgresql_adapter_decode_dates` is the only one of the five that is
    # behaviour rather than declaration, and it is read twice on purpose: here
    # as the setting, and in `database` as what an untyped query actually
    # answers with.  The setting has no class-level reader on this release --
    # `ActiveRecord.respond_to?(:postgresql_adapter_decode_dates)` is false --
    # so the config key is the only place the declaration itself can be seen.
    def defaults_7_2
      config = Rails.application.config
      readings = {
        'yjit' => (config.yjit if config.respond_to?(:yjit)),
        'yjit_runtime_available' => !defined?(RubyVM::YJIT).nil?,
        # Read off `ActiveJob::Base` and not off `config.active_job`: the
        # activejob railtie DELETES the key as it assigns it (the same shape
        # step 05 met with `default_enforce_utf8`), so the config reading is
        # nil on both sides while the class reading moves from `never` to
        # `default`.  Active Job is loaded here even though this application
        # does not require its railtie -- Action Mailer, which it does
        # require, requires Active Job for `deliver_later`.
        'aj_enqueue_after_transaction_commit' =>
          (ActiveJob::Base.enqueue_after_transaction_commit.to_s if
            defined?(ActiveJob::Base) &&
            ActiveJob::Base.respond_to?(:enqueue_after_transaction_commit)),
        'ar_validate_migration_timestamps' =>
          (ActiveRecord.validate_migration_timestamps if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:validate_migration_timestamps)),
        'ar_postgresql_adapter_decode_dates' =>
          (config.active_record.postgresql_adapter_decode_dates if
            config.active_record.respond_to?(:postgresql_adapter_decode_dates))
      }
      # Each value is the one MEASURED on a tree that declares 7.2, beside the
      # same tree declaring 7.1, not the one a guide states.
      wanted = { 'aj_enqueue_after_transaction_commit' => 'default',
                 'ar_validate_migration_timestamps' => true,
                 'ar_postgresql_adapter_decode_dates' => true }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      readings.merge('not_adopted' => missed.sort,
                     'adopted' => wanted.length - missed.length)
    end

    # The three settings `config.load_defaults 8.0` turns on, each read off the
    # framework object the 8.0 railtie assigned it to rather than off the
    # configuration object the railtie was handed.  Measured on BOTH sides --
    # the same tree, the same 8.0 gem set, the declaration the only
    # difference:
    #
    #   reading                          declares 7.2   declares 8.0
    #   as_to_time_preserves_timezone    "offset"       "zone"
    #   ad_strict_freshness              false          true
    #   regexp_timeout                   nil            1.0
    #
    # All three move, and all three are READABLE on both sides rather than
    # raising on one -- which is what disqualified
    # `active_storage.web_image_content_types` from `defaults_7_2`.  So this
    # probe, unlike that one, holds a tree to every reading it takes and keeps
    # nothing out of `not_adopted`.
    #
    # `to_time_preserves_timezone` is the one of the three the release will not
    # let a tree decline quietly: activesupport 8.0 deprecates the old value
    # and attributes the warning to the application file that initialises the
    # framework, so a tree that declares 8.0 and assigns `:offset` back fails
    # the deprecation conditions as well as this one.  Note the SHAPE change --
    # on 7.2 the reader answered `true`, on 8.0 it answers `:offset` or `:zone`
    # -- which is why `framework_defaults`, the step-02 probe that has counted
    # this setting since the 5.0 rung and compares it against `true`, reports
    # it as not adopted on a tree that has done everything right.  That is the
    # release changing the shape of an answer, not the tree giving up a
    # default; the ladder narrows the guard rather than move this reading.
    # `.to_s` so the reading is one type on every rung.
    #
    # `ad_strict_freshness` is a DECLARATION on this application and is not
    # sold as behaviour.  actionpack-8.0.5.1 consults it inside
    # `ActionDispatch::Http::Cache::Request#fresh?`, whose only callers are
    # `ActionController::ConditionalGet#fresh_when` and `#stale?`, and this
    # application writes neither (zero hits under `app/`).  The conditional GET
    # Rack performs for every response is `Rack::ConditionalGet`, which has its
    # own AND-ing logic and never reaches this flag.  Read off the class the
    # railtie wrote to, which is the same status step 09 gave
    # `form_with_generates_ids`.
    #
    # `regexp_timeout` is the INTERPRETER's and not the framework's: the 8.0
    # railtie runs `Regexp.timeout ||= 1` and the reading comes straight back
    # from Ruby.  It is readable here only because the rungs below already
    # moved the interpreter to a release that has the setting at all, and `||=`
    # is why a tree that set its own timeout first keeps it -- 1.0 is what this
    # application gets, and `wanted` says so rather than accepting any value.
    # The `1` the railtie assigns comes back as the Float `1.0`; `==` compares
    # them equal either way.
    #
    # ---------------------------------------------------------------------
    # 8.1 withdraws the first of the three, and reading it is what costs
    # ---------------------------------------------------------------------
    #
    # activesupport-8.1.3.1/CHANGELOG.md:121 removes the old `to_time`
    # behaviour outright and :126 deprecates the setting that selected it, and
    # railties-8.1.3.1/lib/rails/application/configuration.rb no longer
    # assigns it under `when "8.0"` -- that branch went from three assignments
    # to two.  What is left is a reader that answers `nil` and deprecates on
    # the way out.  Measured:
    #
    #   DEPRECATION WARNING: `config.active_support.to_time_preserves_timezone`
    #     is deprecated and will be removed in Rails 8.2 (called from <main> at -e:12)
    #   "8.0/as_to_time_preserves_timezone": ""
    #
    # Both halves of that matter here.  `""` is not `'zone'`, so a probe that
    # kept asking would put the setting in `not_adopted` on a tree that has
    # done nothing wrong -- step 10's guard, inherited by every rung above it,
    # would fail on step 11's own golden.  And the warning is raised BY THE
    # READING, so such a probe would also manufacture a deprecation and then,
    # on a tree whose collector blames the first application frame, hand it to
    # the tree.
    #
    # So the probe stops asking above 8.0.  `'withdrawn'` on both sides of the
    # comparison rather than dropping the key: "the release took this away" and
    # "the tree declined this" must not read the same, and a key that vanishes
    # from the report is a key no reader can tell apart from a probe that
    # crashed.  `respond_to?` is deliberately NOT the test -- the method is
    # still there on 8.1, it just deprecates -- and neither is rescuing the
    # warning, which would still have been raised.  The version is.
    def defaults_8_0
      withdrawn = rails_at_least?('8.1')
      readings = {
        'as_to_time_preserves_timezone' =>
          if withdrawn
            'withdrawn'
          elsif defined?(ActiveSupport) &&
                ActiveSupport.respond_to?(:to_time_preserves_timezone)
            ActiveSupport.to_time_preserves_timezone.to_s
          end,
        'ad_strict_freshness' =>
          (ActionDispatch::Http::Cache::Request.strict_freshness if
            defined?(ActionDispatch::Http::Cache::Request) &&
            ActionDispatch::Http::Cache::Request.respond_to?(:strict_freshness)),
        'regexp_timeout' => (Regexp.timeout if Regexp.respond_to?(:timeout))
      }
      wanted = { 'as_to_time_preserves_timezone' =>
                   (withdrawn ? 'withdrawn' : 'zone'),
                 'ad_strict_freshness' => true,
                 'regexp_timeout' => 1.0 }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      readings.merge('withdrawn' => (withdrawn ? ['as_to_time_preserves_timezone'] : []),
                     'not_adopted' => missed.sort,
                     'adopted' => wanted.length - missed.length)
    end

    # The 8.1 rung.  `railties-8.1.3.1/lib/rails/application/configuration.rb`
    # assigns SEVEN things under `when "8.1"`; six are here and the seventh is
    # reported beside them without being wanted.
    #
    # The six, with the value `load_defaults "8.1"` gives them, each read off
    # the object the railtie wrote to rather than off `config` -- the same rule
    # `defaults_8_0` follows, because `config.x.y` answers whatever was
    # assigned whether or not any railtie ever consumed it:
    #
    #   ac_escape_json_responses                            false
    #   ad_action_on_path_relative_redirect                 :raise
    #   ar_raise_on_missing_required_finder_order_columns   true
    #   as_escape_js_separators_in_json                     false
    #   av_render_tracker                                   :ruby
    #   av_remove_hidden_field_autocomplete                 true
    #
    # Measured on both probe trees in both environments, all six move
    # with the declaration and only with the declaration, and all six are
    # readable on a tree that declares 8.0 -- so, unlike
    # `active_storage.web_image_content_types` at the 7.2 rung, nothing here is
    # kept out of `not_adopted` for fear of raising.
    #
    # Two readings need their spelling justified, because the obvious one is
    # wrong and answers `nil` on a tree that has adopted everything:
    #
    #   * `ActionView.render_tracker`, not `ActionView::Base.render_tracker`.
    #   * `ActionView::Base.remove_hidden_field_autocomplete`, not
    #     `ActionView::Helpers::FormTagHelper.remove_hidden_field_autocomplete`.
    #
    # `.to_s` on the two whose answers are Symbols, for the reason
    # `as_to_time_preserves_timezone` has it: one type on every rung, and a
    # JSON round-trip that does not turn `:raise` into `"raise"` behind the
    # profile's back.
    #
    # The seventh is `config.yjit`, and it is REPORTED AND NOT WANTED.  It is
    # the first default on this ladder whose value is a function of the
    # environment it is read in -- `self.yjit = !Rails.env.local?` -- so it
    # says nothing about the tree unless it also says which environment
    # answered.  Measured: declaring 8.0 gives `true` in both test and
    # production; declaring 8.1 gives `false` in test and `true` in
    # production.  This probe boots production, where the two declarations
    # agree, so as a discriminator it is worth exactly nothing and as a
    # reading it is worth having next to `yjit_env`.  Anything above that
    # wants to gate it has to gate it per environment and say so.
    def defaults_8_1
      readings = {
        'ac_escape_json_responses' =>
          (ActionController::Base.escape_json_responses if
            defined?(ActionController::Base) &&
            ActionController::Base.respond_to?(:escape_json_responses)),
        'ad_action_on_path_relative_redirect' =>
          (ActionController::Base.action_on_path_relative_redirect.to_s if
            defined?(ActionController::Base) &&
            ActionController::Base.respond_to?(:action_on_path_relative_redirect)),
        'ar_raise_on_missing_required_finder_order_columns' =>
          (ActiveRecord.raise_on_missing_required_finder_order_columns if
            defined?(ActiveRecord) &&
            ActiveRecord.respond_to?(:raise_on_missing_required_finder_order_columns)),
        'as_escape_js_separators_in_json' =>
          (ActiveSupport.escape_js_separators_in_json if
            defined?(ActiveSupport) &&
            ActiveSupport.respond_to?(:escape_js_separators_in_json)),
        'av_render_tracker' =>
          (ActionView.render_tracker.to_s if
            defined?(ActionView) && ActionView.respond_to?(:render_tracker)),
        'av_remove_hidden_field_autocomplete' =>
          (ActionView::Base.remove_hidden_field_autocomplete if
            defined?(ActionView::Base) &&
            ActionView::Base.respond_to?(:remove_hidden_field_autocomplete))
      }
      wanted = { 'ac_escape_json_responses' => false,
                 'ad_action_on_path_relative_redirect' => 'raise',
                 'ar_raise_on_missing_required_finder_order_columns' => true,
                 'as_escape_js_separators_in_json' => false,
                 'av_render_tracker' => 'ruby',
                 'av_remove_hidden_field_autocomplete' => true }
      missed = wanted.reject { |name, value| readings[name] == value }.keys
      config = (Rails.application.config if defined?(Rails) && Rails.application)
      readings.merge(
        'yjit' => (config.yjit if config.respond_to?(:yjit)),
        'yjit_env' => (Rails.env.to_s if defined?(Rails) && Rails.respond_to?(:env)),
        'not_adopted' => missed.sort,
        'adopted' => wanted.length - missed.length
      )
    end

    # `config.autoload_lib(ignore:)`, which 7.1 adds and which is NOT implied
    # by `config.load_defaults 7.1` -- it is a separate call, and the line it
    # replaces here is `config.eager_load_paths << Rails.root.join('lib')`.
    #
    # The discriminator is `lib_in_autoload_paths`, because the two spellings
    # agree everywhere else: railties 7.1's `autoload_lib` pushes lib onto
    # BOTH `autoload_paths` and `eager_load_paths`, while the line it replaces
    # pushes it onto `eager_load_paths` only.  So `lib_in_eager_load_paths` is
    # true on the tree this step is handed as well as on the migrated one and
    # is reported rather than gated, and the autoload-paths reading is the one
    # that moves.
    #
    # The `ignore:` argument IS readable, which matters, because
    # `in_autoload_paths` alone is also satisfied by
    # `config.autoload_paths << Rails.root.join('lib')` -- the other hand-rolled
    # spelling, which leaves lib/assets and lib/tasks as autoload roots.  The
    # public reader is spelt `__ignores?`, zeitwerk's convention for a method
    # that is public because Rails has to call it and internal by name;
    # `ignores?` does not exist.  `ignored_paths` is the set behind it and is
    # the more useful shape here, because it names what was ignored rather
    # than answering one path at a time.  Measured both ways on both trees:
    # `[]` and false on a tree that has not called it, the two paths and true
    # on one that has.
    #
    # Relative to `Rails.root` so the reading does not carry the checkout
    # path, and sorted so it does not carry the order `ignore` happened to be
    # called in.
    #
    # The two counts are how "declared once" is read.  `autoload_lib` pushes
    # lib onto each list exactly once, so a tree that calls it and ALSO keeps
    # the `config.eager_load_paths << Rails.root.join('lib')` line it replaces
    # has lib on the eager-load paths twice -- and every boolean above is true
    # on that tree, because both spellings are in force.  Counted and not
    # de-duplicated: the two entries are a `String` and a `Pathname`
    # (railties' `autoload_lib` pushes `lib.to_s`, the hand-rolled line pushes
    # the `Pathname` that `Rails.root.join` returns), so `uniq` on the raw
    # list would not collapse them either, and `.to_s` here is what makes them
    # comparable at all.
    def autoload_lib
      lib = Rails.root.join('lib').to_s
      config = Rails.application.config
      autoload = config.autoload_paths.map(&:to_s)
      eager = config.eager_load_paths.map(&:to_s)
      { 'lib' => lib,
        'in_autoload_paths' => autoload.include?(lib),
        'in_eager_load_paths' => eager.include?(lib),
        'autoload_path_entries' => autoload.count { |path| path == lib },
        'eager_load_path_entries' => eager.count { |path| path == lib },
        'ignored' => autoload_ignored_paths,
        'zeitwerk_version' => (Zeitwerk::VERSION if defined?(Zeitwerk::VERSION)) }
    end

    # Every guard here is a release this ladder still passes through: there is
    # no `Rails.autoloaders` before 6.0, no `main` loader under the classic
    # autoloader, and `ignored_paths` is zeitwerk's own and could be renamed
    # under this ladder's feet without the framework noticing.
    def autoload_ignored_paths
      return [] unless defined?(Rails.autoloaders)

      main = Rails.autoloaders.respond_to?(:main) ? Rails.autoloaders.main : nil
      return [] unless main.respond_to?(:ignored_paths, true)

      root = Rails.root.to_s
      main.send(:ignored_paths).to_a.map { |path|
        path.to_s.sub(%r{\A#{Regexp.escape(root)}/?}, '')
      }.sort
    rescue StandardError, NoMethodError
      []
    end

    # Active Record encryption, as the application DECLARES it.  Reported and
    # very nearly not gated, for `host_authorization`'s reason: `encrypts` in
    # a model is a declaration, and a column that holds ciphertext is the
    # behaviour, and the two come apart in both directions --
    # `support_unencrypted_data` leaves a declared-encrypted column full of
    # the plaintext it always held, and a row written before the declaration
    # stays readable forever.  `depdrive` holds the gates that write a row and
    # read the column back; this is the evidence that says WHICH way one of
    # those failed.
    #
    # The one reading here a gate does gate is `key_source`, and it is here
    # rather than in `depdrive` because it is not observable from behaviour at
    # all: keys read out of `ENV` and keys read out of the encrypted
    # credentials encrypt identically.  The difference is that the first
    # spelling puts the key in the process environment of every `ps` on the
    # box and in whatever the platform logs at deploy, which is the thing
    # `no_plaintext_secrets` has been guarding since step 04.  So the reading
    # is: do the three keys the application is using equal the three that are
    # in the encrypted credentials file?
    def encryption
      return { 'available' => false } unless defined?(ActiveRecord::Encryption)

      config = ActiveRecord::Encryption.config
      creds = begin
        Rails.application.credentials.config[:active_record_encryption] || {}
      rescue StandardError => err
        { '_error' => "#{err.class}: #{err.message}" }
      end
      keys = { 'primary_key' => config.primary_key,
               'deterministic_key' => config.deterministic_key,
               'key_derivation_salt' => config.key_derivation_salt }
      from_credentials = keys.all? do |name, value|
        !value.nil? && !value.to_s.empty? &&
          value.to_s == creds[name.to_sym].to_s
      end
      # activerecord railtie.rb:330-335 passes `credentials.dig(...)` first and
      # then splats `config.active_record.encryption` OVER it, so the
      # application's own configuration bag is the channel by which a key that
      # came from anywhere else gets used.  railtie.rb:18 creates that bag
      # empty, and nothing in 7.0 puts the three key names into it, so a tree
      # that leaves the keys to the credentials file leaves it empty.  Without
      # this reading a tree that lifts the keys out of `ENV` and assigns them
      # in `config/application.rb` answers `credentials`, as long as the values
      # match the file -- which is exactly the spelling the requirement exists
      # to refuse, because it is the one that puts the key in the process
      # environment of every `ps` on the box.
      overridden = begin
        (Rails.application.config.active_record.encryption || {}).to_h
      rescue StandardError
        {}
      end
      overrides_a_key = keys.keys.any? { |name| overridden.key?(name.to_sym) }
      { 'available' => true,
        # The keys themselves are never reported -- this file's output is
        # kept as run evidence.  Presence and provenance are the readings.
        'keys_present' => keys.map { |name, value|
          [name, !value.nil? && !value.to_s.empty?]
        }.to_h,
        'config_override_keys' => overridden.keys.map(&:to_s).sort,
        'key_source' => if overrides_a_key
                          'elsewhere'
                        elsif from_credentials
                          'credentials'
                        elsif keys.values.any? { |value| !value.to_s.empty? }
                          'elsewhere'
                        else
                          'none'
                        end,
        'credentials_keys' => (creds.keys.map(&:to_s).sort rescue []),
        'support_unencrypted_data' => config.support_unencrypted_data,
        'extend_queries' => config.extend_queries,
        'attributes' => encrypted_attributes }
    end

    # Per-model, per-attribute: what `encrypts` was given.  `type_for_attribute`
    # is asked rather than the `encrypted_attributes` set alone, because the
    # set answers "yes, encrypted" for a non-deterministic declaration too and
    # this application's `find_or_create_by!` needs the deterministic one.
    def encrypted_attributes
      return {} unless defined?(ActiveRecord::Base)

      ActiveRecord::Base.descendants.reject { |klass|
        klass.name.nil? || klass.name.start_with?('ActiveRecord::') ||
          klass.abstract_class?
      }.flat_map { |klass|
        declared = if klass.respond_to?(:encrypted_attributes)
                     Array(klass.encrypted_attributes)
                   else
                     []
                   end
        declared.map do |attribute|
          type = klass.type_for_attribute(attribute.to_s)
          [ "#{klass.name}##{attribute}",
            { 'deterministic' => (type.deterministic? if type.respond_to?(:deterministic?)),
              'downcase' => (type.downcase? if type.respond_to?(:downcase?)),
              # The declared column, so a tree that encrypts a column it never
              # widened is visible here as well as in the round trip
              # `depdrive` drives.
              'sql_type' => (klass.columns_hash[attribute.to_s].sql_type if
                               klass.columns_hash.key?(attribute.to_s)),
              'limit' => (klass.columns_hash[attribute.to_s].limit if
                            klass.columns_hash.key?(attribute.to_s)) } ]
        end
      }.sort.to_h
    end

    # Whether an executor is configured for `.load_async`.  Reported, not
    # gated, and for a sharper reason than `host_authorization`'s: a relation
    # that calls `.load_async` with NO executor configured does not raise and
    # does not warn -- `ActiveRecord::Relation#load_async` falls straight
    # through to a synchronous load and `scheduled?` stays false (measured
    # both ways).  So the executor and the
    # call site are each necessary and neither is sufficient, and the honest
    # gate is the one `depdrive` takes downstream of both: an
    # `sql.active_record` event whose payload carries `async: true`.
    def async_queries
      return { 'available' => false } unless defined?(ActiveRecord)

      { 'available' => true,
        'executor' => (ActiveRecord.async_query_executor.to_s if
                        ActiveRecord.respond_to?(:async_query_executor)),
        'concurrency' => (ActiveRecord.global_executor_concurrency if
                           ActiveRecord.respond_to?(:global_executor_concurrency)) }
    end

    # What `config.hosts` holds and whether the middleware that reads it is in
    # the stack.  Reported and not gated: the gate is `depdrive`'s, because a
    # list is a declaration and a refused request is the behaviour, and the two
    # come apart -- an empty list leaves `ActionDispatch::HostAuthorization` in
    # the stack and makes it step aside (`return @app.call(env) if
    # @permissions.empty?`), so "the middleware is there" is true of every 6.0
    # application including the one that checks nothing.  This is the evidence
    # that says WHICH way a failing host gate failed.
    def host_authorization
      config = Rails.application.config
      hosts = config.hosts if config.respond_to?(:hosts)
      # `used.name`, the same reading `envboot.rb` takes of the same stack: a
      # `Middleware` entry answers `klass` only once it has been built, and
      # `name` is the spelling that works on both sides of that.
      middleware = begin
        Rails.application.middleware.map { |used| used.name.to_s }
      rescue StandardError
        []
      end
      { 'reader_exists' => config.respond_to?(:hosts),
        # `inspect` rather than the values: the list may hold Regexps and
        # IPAddrs, and a gate never reads this -- a human does.
        'hosts' => Array(hosts).map(&:inspect),
        'count' => Array(hosts).length,
        'middleware_present' =>
          middleware.include?('ActionDispatch::HostAuthorization') }
    end

    # Bootsnap, read as work done rather than as a gem named in a Gemfile.
    #
    # Three readings because there are three distinct states and a gate that
    # collapsed them would pass the wrong one:
    #
    #   * `defined` -- the constant is there, which says only that something
    #     required the gem;
    #   * `setup_called` -- `Bootsnap::LoadPathCache.load_path_cache` is `nil`
    #     until `Bootsnap.setup` runs and is a `Cache` afterwards, so this is
    #     the reading that tells a `require "bootsnap"` from a
    #     `require "bootsnap/setup"`.  Measured against bootsnap 1.4.9, the
    #     last release that installs on Ruby 2.4;
    #   * `boot_file_setup` / `setup_from_boot_file` -- whether the setup the
    #     reading above found came from `config/boot.rb`.  5.2 asks for
    #     `require "bootsnap/setup"` there, after `bundler/setup` and before
    #     anything else is required, and the position is the whole point of
    #     the gem: an initializer that calls `Bootsnap.setup` runs after the
    #     framework, the application's own code and every gem have already
    #     been loaded, so it caches nothing that mattered.  `setup_called`
    #     alone cannot tell those apart -- both leave a `Cache` behind -- so
    #     the file is read as well;
    #   * `store_path` / `store_written` / `store_bytes` -- what it actually
    #     wrote, asked of bootsnap rather than of the tree.  The load path
    #     cache's `Store` carries the file it is writing to in `@store_path`,
    #     and `CompileCache::ISeq.cache_dir` carries the other one; both are
    #     read back here and the sizes are taken of those exact paths.
    #     `cache_files` and `cache_bytes` are a wider count -- any file under
    #     `tmp/` whose path contains the string `bootsnap`, which
    #     `touch tmp/bootsnap` satisfies -- kept because it is useful in a
    #     log; nothing is gated on them.
    #
    # `Gem.loaded_specs` is deliberately not the reading: it is true of a gem
    # that Bundler put on the load path and that nothing ever required.
    def bootsnap
      cache = Dir.glob(File.join(@app, 'tmp', '**', '*'), File::FNM_DOTMATCH)
                 .select { |path| File.file?(path) && path.include?('bootsnap') }
      lpc = if defined?(Bootsnap::LoadPathCache) &&
               Bootsnap::LoadPathCache.respond_to?(:load_path_cache)
              Bootsnap::LoadPathCache.load_path_cache
            end
      store = lpc.instance_variable_get(:@store) if lpc
      store_path = store.instance_variable_get(:@store_path).to_s if store
      compile_dir = if defined?(Bootsnap::CompileCache::ISeq) &&
                       Bootsnap::CompileCache::ISeq.respond_to?(:cache_dir)
                      Bootsnap::CompileCache::ISeq.cache_dir.to_s
                    end
      boot_rb = File.join(@app, 'config', 'boot.rb')
      boot_source = (File.read(boot_rb) if File.file?(boot_rb)).to_s
      # `String#match?` is Ruby 2.4 and this probe also runs on the 2.3 tree,
      # and a literal \b in a Ruby regexp is a backspace, not a boundary.
      boot_file_setup = !(boot_source =~ %r{require\s*['"]bootsnap/setup['"]}).nil? ||
                        !(boot_source =~ /Bootsnap\.setup(?![A-Za-z0-9_])/).nil?
      store_rel = relative(store_path)
      store_written = !store_path.to_s.empty? &&
                      File.file?(store_path.to_s) &&
                      File.size(store_path.to_s).positive?
      {
        'defined' => defined?(Bootsnap) ? true : false,
        'version' => (Bootsnap::VERSION if defined?(Bootsnap::VERSION)),
        'setup_called' => !lpc.nil?,
        'load_path_cache_class' => lpc.class.name,
        'boot_file_setup' => boot_file_setup,
        'setup_from_boot_file' => !lpc.nil? && boot_file_setup,
        'store_path' => store_rel,
        'store_written' => store_written,
        # `store_written` says a file with bytes in it exists where bootsnap
        # says it is writing.  It does not say that file is inside the tree:
        # `Bootsnap.setup(cache_dir: ENV['HOME'] + '/bs')` writes a real store
        # outside the application and satisfies it.  The instruction asks for a
        # cache left "under the application's own `tmp/`", so this is the
        # reading that answers the instruction -- kept as its own key rather
        # than folded into `store_written`, so the evidence still shows both
        # halves on a tree where they disagree.
        'store_in_tree' => self.class.store_in_tree?(store_rel, store_written),
        'store_bytes' => (File.size(store_path.to_s) if
                           !store_path.to_s.empty? && File.file?(store_path.to_s)).to_i,
        'compile_cache_dir' => relative(compile_dir),
        'cache_files' => cache.length,
        # `inject` and not `sum`: `Enumerable#sum` is 2.4, and step 01's
        # verifier runs 2.3.8, where `sum` would take this whole probe down
        # with a NoMethodError.
        'cache_bytes' => cache.map { |path| File.size(path) }.inject(0, :+),
        'in_lockfile' => Gem.loaded_specs.key?('bootsnap')
      }
    end

    # The whole of the `store_in_tree` verdict as a pure function of the two
    # values the probe reports, so `selftest_tools.rb` can rehearse the cases
    # the stub application cannot produce: it never loads bootsnap, so its
    # store path is always nil.  `relative` returns a path it cannot root
    # UNCHANGED, so an absolute store path outside the tree keeps its leading
    # `/` and fails the prefix on its own.  `=~` and not `match?`, which is
    # 2.4: step 01's verifier runs 2.3.8.
    def self.store_in_tree?(store_rel, store_written)
      store_written && !(store_rel.to_s =~ %r{\Atmp/}).nil?
    end

    # Paths are reported relative to the application root wherever the probe
    # reports one at all: the absolute form carries the harness's own mount
    # point, which is not a property of the tree and would put a fixture in
    # the position of asserting where the scoring container put `/app`.
    def relative(path)
      return nil if path.nil? || path.to_s.empty?
      path.to_s.sub(/\A#{Regexp.escape(@app)}\/?/, '')
    end

    # The Content Security Policy as the booted application holds it.
    #
    # `config.content_security_policy` does not exist before 5.2, so
    # `reader_exists` is itself a version signal and `configured` cannot be
    # accidentally true on an older tree.  Whether the header reaches a
    # response is a different question and is not asked here -- see
    # `depdrive.rb`, which drives the application and reads what came back.
    def csp
      config = Rails.application.config
      policy = (config.content_security_policy if
                 config.respond_to?(:content_security_policy))
      generator = (config.content_security_policy_nonce_generator if
                    config.respond_to?(:content_security_policy_nonce_generator))
      {
        'reader_exists' => config.respond_to?(:content_security_policy),
        'configured' => !!(defined?(ActionDispatch::ContentSecurityPolicy) &&
                           policy.is_a?(ActionDispatch::ContentSecurityPolicy)),
        'policy_class' => policy.class.name,
        'directives' => (policy.directives.keys.sort if policy.respond_to?(:directives)),
        'report_only' => (config.content_security_policy_report_only if
                           config.respond_to?(:content_security_policy_report_only)),
        'nonce_generator' => !generator.nil?,
        'initializer_present' =>
          File.exist?(File.join(@app, 'config', 'initializers',
                                'content_security_policy.rb'))
      }
    end

    # The routing table, as a projection, plus the two readings the 8.1 rung
    # needs from it.
    #
    # Why a route probe exists at all, on a ladder that has read configuration
    # for ten rungs and never read routes.  actionpack 8.1 gave the HTTP verb
    # helpers a keyword signature and left the old positional Hash working
    # behind a deprecation (`action_dispatch/routing/mapper.rb:737`, warning
    # from `assign_deprecated_option` at `:667`).  The application's own
    # `config/routes.rb` is not affected -- Ruby 3 turns a bare trailing hash
    # literal into keywords, so `get '/export' => '...'` arrives in `**mapping`
    # and never touches the deprecated path.  A Hash held in a VARIABLE cannot
    # be converted, and `high_voltage-3.0.0/config/routes.rb:8` passes exactly
    # that: `get HighVoltage.route_drawer.match_attributes`.
    #
    # Two consequences, and the second is the one no warning mentions.
    #
    #   1. Two deprecations per suite run, blamed -- because the route set is
    #      drawn inside the first request -- to `spec/`, which is what
    #      `rspec.suite_deprecation_sites` reads.  Measured.
    #   2. `assign_deprecated_option` warns only when the value it deleted is
    #      truthy, so the drawer's `:format => false` is deleted, answers
    #      `nil`, and the optional `(.:format)` segment comes BACK.  Measured
    #      on three trees: 8.0 gives `/pages/*id`, 8.1 with the gem drawing it
    #      gives `/pages/*id(.:format)`, 8.1 with the application drawing it
    #      gives `/pages/*id` again.
    #
    # So `page_spec` separates a tree that only pinned the version from a tree
    # that dealt with the drawer.  What it does NOT say is whether the drawer
    # was dealt with, because the floor -- Rails 8.0, the gem drawing the route
    # -- reads `/pages/*id` too: there the same positional call is simply not
    # warned about yet.  `drawn_by_positional_hash` is the half that separates
    # those, and it is a fact about the code rather than about the release, so
    # it is TRUE on the floor as well as on a tree that only pinned, and false
    # only once the drawing itself has moved.
    #
    # There are TWO ways to stop drawing the route positionally and the reading
    # has to accept both, because both are real migrations of this application:
    # move the `high_voltage` pin to 5.0.0, where the gem itself splats, or
    # turn the gem's drawer off and draw the route in `config/routes.rb`.  The
    # first is the smaller change and the second is the one an application has
    # to make if the gem is unmaintained; a profile that gated
    # `high_voltage_draws_routes == false` would fail the better answer
    # (measured on a fourth tree).
    #
    # `source_location` would have been the general way to ask "whose file
    # declared this", and it is not available: the reader exists from 7.1 but
    # answers `nil` unless the application is running with
    # `config.consider_all_requests_local`, which production is not.  Measured
    # on both probe trees.  Route ORDER is the other tempting proxy -- an
    # engine's routes are appended after the application's, so a moved route
    # sorts before `root` -- and it is rejected here because a candidate who
    # writes the line after `root` has still done the migration correctly and
    # must not be failed for where on the page they put it.
    #
    # The whole table is reported, name and spec in declaration order, so that
    # a reader of the evidence can see a route that went missing rather
    # than only the one this rung happens to ask about.  Nineteen routes on
    # every tree of this ladder that boots.
    def routes
      set = (Rails.application.routes if defined?(Rails) && Rails.application)
      return { 'available' => false } if set.nil?

      table = set.routes.map do |route|
        { 'name' => route.name, 'spec' => route.path.spec.to_s }
      end
      page = set.named_routes[:page]
      page_spec = (page.path.spec.to_s if page)
      page_endpoint = (if page
                         [page.defaults[:controller],
                          page.defaults[:action]].compact.join('#')
                       end)
      draws = (HighVoltage.routes if defined?(HighVoltage) &&
                                     HighVoltage.respond_to?(:routes))
      spec = (Gem.loaded_specs['high_voltage'] if defined?(Gem) &&
                                                  Gem.respond_to?(:loaded_specs))
      version = (spec.version.to_s if spec)
      # `5.0.0` is a measured fact about this gem and not a guess at what a
      # release number means: high_voltage 3.0.0 and 4.0.0 draw the page route
      # with `get HighVoltage.route_drawer.match_attributes` and 5.0.0 draws it
      # with `get **HighVoltage.route_drawer.match_attributes`
      # (`config/routes.rb:8` of each gem, unpacked and read).  The double
      # splat is the whole
      # difference: the Hash reaches actionpack 8.1's `get` as keywords, the
      # deprecation does not fire, and `:format => false` is not swallowed.
      # A gem that is not in the bundle at all cannot be drawing anything
      # positionally, so the safe answer is `true` there.
      keyword_safe = if version.nil?
                       true
                     else
                       Gem::Version.new(version) >= Gem::Version.new('5.0.0')
                     end
      positional = (draws == true) && !keyword_safe
      {
        'available' => true,
        'count' => table.length,
        'table' => table,
        'named' => set.named_routes.names.map(&:to_s).sort,
        'page_named' => !page.nil?,
        'page_spec' => page_spec,
        'page_endpoint' => page_endpoint,
        'page_format_segment' => (page_spec.include?('(.:format)') if page),
        'high_voltage_draws_routes' => draws,
        'high_voltage_version' => version,
        'high_voltage_drawer_keyword_safe' => keyword_safe,
        'drawn_by_positional_hash' => positional,
        # The compound the profile gates, reported with its parts beside it so
        # the evidence says WHICH half a tree failed.  `/pages/*id` and
        # `high_voltage/pages#show` are the route this repository has had since
        # it was written; the migration has to get past the deprecation without
        # moving either, and both ways of getting past it (upgrade the gem, or
        # turn the drawer off and draw the route in the application) land here.
        'page_route_migrated' =>
          (!positional && !page.nil? && page_spec == '/pages/*id' &&
            page_endpoint == 'high_voltage/pages#show')
      }
    end

    # `nil` from this means "nothing here can be asked", which is a different
    # answer from `false` and is left distinguishable.
    def setting(object, name, fallback = nil)
      return object.public_send(name) if object && object.respond_to?(name)
      return fallback[name] if fallback.respond_to?(:[])

      nil
    end

    # From Rails 5.0 `ActionController::Parameters` is no longer a `Hash`, which
    # is the change that breaks `params.slice(...).merge(...)`-style code that
    # type-checks clean.  It is read from the class rather than inferred from
    # the Rails version so that a shim putting `Hash` back is visible.
    def parameters
      return { 'defined' => false } unless defined?(ActionController::Parameters)

      klass = ActionController::Parameters
      {
        'defined' => true,
        'ancestors' => klass.ancestors.first(6).map { |mod| mod.to_s },
        'is_hash' => klass.ancestors.include?(Hash),
        'permit_all_parameters' => klass.permit_all_parameters,
        'action_on_unpermitted' => klass.action_on_unpermitted_parameters.inspect
      }
    end

    # `respond_to?(:postgresql_version)` alone answers false on Rails 4.0, where
    # the method exists but is private, so asking only that would report a
    # null server version against a PostgreSQL the probe is talking to
    # perfectly well.  The private form is asked for explicitly; `schema_format`
    # likewise lives on `ActiveRecord::Base` and only appears on
    # `config.active_record` once the application has set it.
    def database
      # `lease_connection` is the spelling Rails 7.2 and later want; asking for
      # `connection` there works but emits the deprecation the probe is
      # supposed to be watching for, which would have the instrument
      # manufacture its own finding.
      connection = if ActiveRecord::Base.respond_to?(:lease_connection)
                     ActiveRecord::Base.lease_connection
                   else
                     ActiveRecord::Base.connection
                   end

      version = (connection.send(:postgresql_version) if
        connection.respond_to?(:postgresql_version, true))

      active_record = Rails.application.config.active_record
      configured = active_record[:schema_format] if active_record.respond_to?(:[])

      # `ActiveRecord::Base.schema_format` is gone on Rails 7.2 -- the module
      # level accessor is the only one left -- and `configured` is nil on a
      # tree that has never named the setting, so asking the class there would
      # take the WHOLE database probe down with a NoMethodError on every 7.2
      # boot.  That would be an instrument failing on a release, not a tree
      # failing a requirement.  The module reader exists from 7.0 on and
      # answers the same value, so it is asked first and no earlier step's
      # reading moves.
      schema_format = if ActiveRecord.respond_to?(:schema_format)
                        ActiveRecord.schema_format
                      elsif ActiveRecord::Base.respond_to?(:schema_format)
                        ActiveRecord::Base.schema_format
                      end

      migrations = if connection.table_exists?('schema_migrations')
                     connection.select_values('SELECT count(*) FROM schema_migrations').first.to_i
                   end

      # The database this probe is connected to was built by `db:schema:load`
      # from the tree's own `db/schema.rb`, which is how nine of the ten
      # recorded worlds and the application's release step build it.  So the
      # two readings below are a single question asked of one database: what
      # width of key would this framework hand a table that does not name one,
      # and what width did this application's tables actually get.  They are
      # only worth asking together -- a tree whose framework still defaults to
      # the narrow key answers the second one correctly for free.
      default_key = begin
        connection.native_database_types[:primary_key].to_s
      rescue StandardError
        nil
      end

      internal = %w[schema_migrations ar_internal_metadata]
      id_types = {}
      (connection.tables.to_a - internal).sort.each do |table|
        column = begin
          connection.columns(table).find { |c| c.name == 'id' }
        rescue StandardError
          nil
        end
        id_types[table] = column.sql_type.to_s if column
      end
      widths = self.class.primary_key_widths(default_key, id_types)

      {
        'adapter' => connection.adapter_name,
        'server_version' => version,
        'schema_format' => (configured || schema_format).to_s,
        'migrations_applied' => migrations,
        'default_primary_key' => default_key,
        'id_sql_types' => id_types,
        # `postgresql_adapter_decode_dates`, read as what it DOES rather than
        # as what it is set to.  7.2's adapter registers a date decoder, so a
        # query that does not pass through an attribute type stops answering
        # with a String: `select_value("SELECT CURRENT_DATE")` is a `Date` on
        # a tree that has adopted the 7.2 defaults and a `String` on one that
        # has not.  Nothing in
        # this application's own `app/` or `lib/` issues raw SQL -- there is no
        # `select_all`, `select_value`, `select_rows`, `find_by_sql` or
        # `execute` anywhere in the tree -- so the whole recorded baseline is
        # blind to this, which is exactly why it is asked here.  The class
        # name, not the value: the value is the date the faked clock reports
        # and would make the reading a fact about the clock.
        'untyped_date_class' => begin
          connection.select_value('SELECT CURRENT_DATE').class.to_s
        rescue StandardError => e
          "#{e.class}"
        end
      }.merge(widths)
    end

    # unicorn's fork hooks, run rather than read.
    #
    # `config/unicorn.rb` is not dead configuration on this ladder: the
    # Procfile's web line is `bundle exec unicorn -p $PORT -c
    # ./config/unicorn.rb` and the harness boots the Procfile's own web line,
    # so `before_fork` runs in every recorded world.  What it does there --
    # drop the master process's database connection before forking workers --
    # is written `ActiveRecord::Base.connection.disconnect!` in this
    # repository, and 7.2 deprecates `.connection` in favour of
    # `.lease_connection` / `.with_connection` (connection_handling.rb:261-283).
    #
    # The deprecation is SOFT: `ActiveRecord.permanent_connection_checkout`
    # defaults to `true`, and while it is true the old spelling works and says
    # nothing, which is why neither the boot nor the 504 examples mention it.
    # So this probe sets the flag to `:disallowed` -- the setting's own name
    # for "a permanent checkout is an error" -- runs the hook, and reports
    # whether it came back clean.  Measured both ways on this tree:
    # `ActiveRecord::Base.connection.disconnect!` raises
    # `ActiveRecord::ActiveRecordError: Called deprecated
    # `ActiveRecord::Base.connection` method`, and
    # `ActiveRecord::Base.connection_pool.disconnect!` does not.
    #
    # The configurator DSL is stubbed rather than required: unicorn's own
    # `Unicorn::Configurator` opens listeners and rewrites the process, which
    # is not something a scoring probe may do.  The stub answers every method
    # and keeps the two blocks.
    #
    # THREE things are put back afterwards, because this probe has side
    # effects and every other reading in this file is taken before it:
    # the checkout flag, the `TERM` handler the hook installs, and the
    # connection the hook disconnects.
    def fork_hooks
      path = File.join(@app, 'config', 'unicorn.rb')
      return { 'config_file' => nil } unless File.exist?(path)

      stub = Class.new do
        attr_reader :hooks
        def initialize; @hooks = {}; end
        def before_fork(&block); @hooks[:before_fork] = block; end
        def after_fork(&block); @hooks[:after_fork] = block; end
        def method_missing(*); nil; end
        def respond_to_missing?(*); true; end
      end.new
      stub.instance_eval(File.read(path), path)

      supported = defined?(ActiveRecord) &&
                  ActiveRecord.respond_to?(:permanent_connection_checkout=)
      previous_flag = ActiveRecord.permanent_connection_checkout if supported
      previous_term = Signal.trap('TERM', 'DEFAULT')
      server = Struct.new(:config).new(nil)

      # WHAT THE HOOK IS FOR, measured rather than inferred from the absence of
      # one method name.  `:disallowed` makes the deprecated accessor raise, so
      # a hook that still calls it is caught -- but a hook that calls NOTHING,
      # and a hook that reaches the same object by a name the flag does not
      # cover, both come back clean from that reading alone.  The property the
      # step actually states is about the pool the master leaves behind: no
      # connection open in it, and none leased on the way there.  So the pool
      # is seeded, read before, and read again the moment `before_fork`
      # returns.
      #
      # Seeded because an empty pool would let a hook that does nothing report
      # what a hook that disconnects reports.  A `preload_app` master has
      # booted the application and holds an open connection; this is that.
      # Seeded BEFORE the flag goes on, because the seeding is the probe's own
      # checkout and not the hook's.
      pool = nil
      pool_before = pool_after = leased_before = leased_after = nil
      if supported
        pool = ActiveRecord::Base.connection_pool
        begin
          pool.with_connection(&:verify!)
          pool_before = pool.connections.size
          leased_before = pool.connections.count(&:in_use?)
        rescue StandardError => e
          pool_before = "seed failed: #{e.class}: #{e.message.to_s.lines.first.to_s.strip}"
        end
      end

      results = {}
      begin
        ActiveRecord.permanent_connection_checkout = :disallowed if supported
        %i[before_fork after_fork].each do |name|
          block = stub.hooks[name]
          results[name.to_s] =
            if block.nil?
              'absent'
            elsif !supported
              # A release without the flag cannot be asked this question, and
              # the answer is 'unsupported' whatever the hook does -- so the
              # hook is not run.  Steps 01-08 ship this same file: running a
              # `before_fork` that disconnects a live connection on eight
              # trees, to reach a reading none of them gates on, is a side
              # effect with nothing on the other side of it.
              'not asked'
            else
              # In a FRESH THREAD, and this is the whole reading rather than a
              # detail of it.  `ActiveRecord::Base.connection` only consults
              # `permanent_connection_checkout` when the calling thread holds
              # no lease yet -- `ConnectionPool#permanent_lease?` -- and every
              # probe above this one has already leased a connection on the
              # main thread, which would make the deprecated call sail through
              # and this probe report a clean hook on a tree that has not
              # touched it (measured: `permanent_lease?` is true on a fresh
              # thread, false on the main one after `lease_connection`).  A
              # thread that has leased nothing is also the truer model of the
              # unicorn master, which boots the
              # application and then forks without having taken a connection
              # through the legacy API.
              Thread.new do
                begin
                  results["#{name}_permanent_lease"] =
                    ActiveRecord::Base.connection_pool
                                      .respond_to?(:permanent_lease?) &&
                    ActiveRecord::Base.connection_pool.permanent_lease?
                  block.call(server, server)
                  'ok'
                rescue Exception => e # rubocop:disable Lint/RescueException
                  # Truncated: a `NoMethodError` names the receiver, and an
                  # inspected ConnectionPool is forty kilobytes of schema
                  # cache.  This string lands in `evidence.json`.
                  "raised: #{e.class}: " \
                    "#{e.message.to_s.lines.first.to_s.strip[0, 160]}"
                end
              end.value
            end
          # Read the pool the hook left, before `after_fork` re-establishes it
          # and while the thread that ran the hook is the only one that could
          # have leased anything.  A lease the hook took and did not give back
          # is still owned by that (now dead) thread: nothing returns it, which
          # is exactly why leasing in a master is the bug.
          if supported && name == :before_fork
            begin
              pool_after = pool.connections.size
              leased_after = pool.connections.count(&:in_use?)
            rescue StandardError => e
              pool_after = leased_after = "unreadable: #{e.class}"
            end
          end
          # `after_fork` re-establishes the connection the hook before it drops,
          # and it may not be permitted to while the flag is set.
          ActiveRecord.permanent_connection_checkout = previous_flag if
            supported && name == :before_fork
        end
      ensure
        ActiveRecord.permanent_connection_checkout = previous_flag if supported
        Signal.trap('TERM', previous_term)
        begin
          ActiveRecord::Base.establish_connection
        rescue StandardError
          nil
        end
      end

      # One reading a gate can stand on: `ok` only when the flag this probe
      # needs exists AND the hook came back clean AND the pool it left behind
      # is the one the hook is for.  A release without the flag cannot answer
      # the question, and "could not be asked" must not read like "asked and
      # passed".  Nor must "called no forbidden method": each clause below is a
      # tree that a raise-only reading lets through --
      #
      #   kept   the disconnect deleted, or `clear_active_connections!`, which
      #          hands leases back and leaves the sockets open for the children
      #          to inherit -- the pool still holds what the master had
      #   held    `lease_connection.disconnect!` or `with_connection`, the
      #          rename rather than the fix: a connection is taken out in order
      #          to be thrown away, and only that one is
      #
      # `leased_after > leased_before` rather than `leased_after > 0` because
      # the main thread may hold a lease of its own from the probes above; the
      # question is what THIS hook took, not what the process was already
      # holding.
      status = if !supported
                 'unsupported'
               elsif results['before_fork'] != 'ok'
                 results['before_fork']
               elsif !pool_before.is_a?(Integer) || pool_before < 1
                 "pool unseeded: #{pool_before.inspect}"
               elsif !pool_after.is_a?(Integer) || !leased_after.is_a?(Integer)
                 "pool unreadable: #{pool_after.inspect}"
               elsif leased_after > leased_before.to_i
                 "held: #{leased_after - leased_before.to_i} leased and not returned"
               elsif pool_after.positive?
                 "kept: #{pool_after} still open"
               else
                 'ok'
               end
      { 'config_file' => 'config/unicorn.rb',
        'hooks' => stub.hooks.keys.map(&:to_s).sort,
        'checkout_flag_supported' => !!supported,
        'before_fork_status' => status,
        'before_fork' => results['before_fork'],
        'after_fork' => results['after_fork'],
        'pool_connections_before' => pool_before,
        'pool_connections_after' => pool_after,
        'pool_leased_before' => leased_before,
        'pool_leased_after' => leased_after,
        'before_fork_permanent_lease' => results['before_fork_permanent_lease'],
        'after_fork_permanent_lease' => results['after_fork_permanent_lease'] }
    end

    # The width classification, kept out of `database` so that it can be
    # rehearsed without a database: `selftest_tools.rb` calls it directly with
    # the readings the three recorded fixtures produced and with the spellings
    # those fixtures happen not to contain.
    #
    # Three named sets rather than "integer and everything else".  The adapter
    # is free to hand back any of its own spellings for the same width --
    # PostgreSQL's catalogue calls a 4-byte key `int4` and an auto-incrementing
    # one `serial`, and `column.sql_type` has reported more than one of those
    # over the versions of the adapter this ladder spans -- so a reading that
    # said "wide unless the string is exactly `integer`" would call a narrow
    # key wide the first time the adapter changed its mind, and fail a step for
    # a property the tree still had.  A width this probe does not recognise
    # (`uuid`, a domain type) is neither narrow nor wide: it gets its own list,
    # so that it shows up as a finding instead of being quietly filed under one
    # of the two answers.
    NARROW_KEY_TYPES = %w[integer int int4 serial serial4].freeze
    WIDE_KEY_TYPES = %w[bigint int8 bigserial serial8].freeze

    def self.primary_key_widths(default_key, id_types)
      wide_default = default_key.to_s.include?('bigserial') ||
                     default_key.to_s.include?('bigint')
      narrow_ids = id_types.select { |_, t| NARROW_KEY_TYPES.include?(t.to_s) }.keys.sort
      wide_ids = id_types.select { |_, t| WIDE_KEY_TYPES.include?(t.to_s) }.keys.sort
      odd_ids = (id_types.keys - narrow_ids - wide_ids).sort

      {
        'wide_default_primary_key' => wide_default,
        'narrow_id_tables' => narrow_ids,
        'wide_id_tables' => wide_ids,
        'odd_id_tables' => odd_ids,
        # "the framework would now hand out a wide key, and every table in this
        # database still has a narrow one".  Both halves are required: on a
        # framework that still defaults narrow the second half is true for
        # free, and an empty database answers it for free as well.
        'pk_pinned' => wide_default && !id_types.empty? &&
                       wide_ids.empty? && odd_ids.empty?
      }
    end

    # Two readouts, because they answer different questions.  `configured` is
    # the symbol the application asked for (`:raise`, `:stderr`) and is the one
    # a gate can state a requirement about.  `behavior_classes` is what the
    # deprecator is actually holding, which is how a stray initializer that
    # overrides the config shows up.
    #
    # `_instance` rather than the class methods: from Rails 7.1 the class-level
    # accessors are themselves deprecated in favour of
    # `Rails.application.deprecators`, and calling them here would have the
    # probe emit the very warning it is trying to observe.
    def deprecation
      config = Rails.application.config
      active_support = config.respond_to?(:active_support) ? config.active_support : nil
      configured = if active_support.respond_to?(:deprecation)
                     active_support.deprecation
                   end

      behaviors = []
      if defined?(ActiveSupport::Deprecation)
        target = if ActiveSupport::Deprecation.respond_to?(:_instance)
                   ActiveSupport::Deprecation._instance
                 else
                   ActiveSupport::Deprecation
                 end
        behaviors = Array(target.behavior).map { |b| b.class.name } if target.respond_to?(:behavior)
      end

      {
        'configured' => configured && configured.to_s,
        'behavior_classes' => behaviors,
        'deprecators' => deprecator_count
      }
    end

    # Where the application's signing secret comes from.
    #
    # The question a gate wants to ask is "has this tree stopped setting
    # `config.secret_key_base` from an initializer and started letting the
    # framework read it?", and the observable that answers it is
    # `Rails.application.secrets`: that reader exists from 4.1 and is populated
    # from `config/secrets.yml` alone.  An initializer assigning
    # `config.secret_key_base` gives a booted application with a key and an
    # *empty* `secrets`, so the two arrangements are distinguishable without
    # reading a line of the tree's source.
    #
    # The key itself is never reported, only whether there is one and how long
    # it is: the report is written into the job log.
    def secrets
      app = Rails.application
      from_yaml = nil
      if app.respond_to?(:secrets)
        begin
          from_yaml = app.secrets[:secret_key_base]
        rescue StandardError, NoMethodError
          from_yaml = nil
        end
      end
      # `config.secret_key_base` is *not* the key the application signs with
      # from 4.1 on -- `Rails.application.secrets.secret_key_base` is, and it
      # falls back to `config.secret_key_base` when the YAML has nothing for
      # this environment (`railties/lib/rails/application.rb`, `secrets`).  So
      # this field is not "the effective key"; it is "what an initializer
      # assigned", which is a diagnostic and never the gate.
      assigned = begin
        app.config.secret_key_base
      rescue StandardError, NoMethodError
        nil
      end
      path = secrets_yml_path
      from_file = secrets_yml_value(path)
      encrypted_path = File.join(@app, 'config', 'secrets.yml.enc')
      key_path = File.join(@app, 'config', 'secrets.yml.key')
      from_encrypted = encrypted_secrets_value(encrypted_path)
      credentials_path = File.join(@app, 'config', 'credentials.yml.enc')
      master_key_path = File.join(@app, 'config', 'master.key')
      from_credentials = credentials_value(credentials_path, master_key_path)
      effective = effective_secret_key_base
      {
        'reader_exists' => app.respond_to?(:secrets),
        # The file itself, not just the value it would supply.  The
        # instruction says `config/secrets.yml` supplies the key; a tree that
        # has deleted the file and set the key some other way satisfies
        # `from_secrets_yml` and not this.
        'secrets_yml_present' => !path.nil? && File.exist?(path),
        'secrets_yml_path' => path,
        'from_secrets_yml' => !(from_yaml.nil? || from_yaml.to_s.empty?),
        'secrets_yml_length' => from_yaml.to_s.length,
        'assigned_length' => assigned.to_s.length,
        'yml_file_length' => from_file.to_s.length,
        # The gate's question is not "is `secrets` populated?" but "is the key
        # the application boots with the one *that file* supplies?".  Those are
        # different questions from 4.1 on, because `secrets.secret_key_base`
        # falls back to `config.secret_key_base`: a tree that ships an empty
        # `config/secrets.yml` and keeps an initializer assigning the key has a
        # populated `secrets` reader and a file that supplied nothing.  So the
        # file is read a second time, independently, and the two are compared
        # by value.  Only the answer is reported; the key never reaches the log.
        'effective_from_secrets_yml' =>
          !(from_file.nil? || from_file.to_s.empty?) &&
          from_file.to_s == from_yaml.to_s,
        # Rails 5.1's encrypted secrets.  `read_encrypted_secrets` is an
        # accessor on `Rails::Application::Configuration` from 5.1 and does not
        # exist before it, so `nil` here is "this release cannot be asked" and
        # `false` is "it was asked and the answer is no" -- and only the second
        # is a tree that shipped the encrypted file and left it unread.
        #
        # railties 7.2 adds a third state.  configuration.rb:357-363 replaces
        # the accessor with a pair of deprecation stubs -- the getter warns and
        # returns the warning TEXT, the setter warns and discards -- and 8.0
        # removes both.  From 7.2 there is therefore no setting to read and no
        # way for an application to turn the store on, so a reading that is
        # neither true nor false is reported as 'unsupported' rather than as
        # prose in a boolean field.  `nil` keeps its meaning: the accessor is
        # not there at all, which is every release below 5.1 and every release
        # from 8.0 up.
        'read_encrypted_secrets' => begin
          if app.config.respond_to?(:read_encrypted_secrets)
            value = app.config.read_encrypted_secrets
            value == true || value == false ? value : 'unsupported'
          end
        end,
        'encrypted_present' => File.exist?(encrypted_path),
        # Asked of the path and not of `secrets_yml_path` above, which is the
        # framework's own answer and from 5.1 resolves the glob
        # `{secrets.yml,secrets.yml.enc}` -- so on a tree that has moved to the
        # encrypted file it names the encrypted file, and `secrets_yml_present`
        # is true of a tree with no plaintext secrets at all.  This is the
        # plaintext file itself, by name.
        'plaintext_present' =>
          File.exist?(File.join(@app, 'config', 'secrets.yml')),
        # ...and the same question asked of the whole tree, because the file
        # can be moved.  See `plaintext_secret_files`.
        'plaintext_secret_files' => plaintext_secret_files,
        # The key itself is never reported, only that there is one and where it
        # came from.  `RAILS_MASTER_KEY` is the other place the framework looks.
        'encrypted_key_file_present' => File.exist?(key_path),
        'encrypted_key_from_env' => !ENV['RAILS_MASTER_KEY'].to_s.empty?,
        'encrypted_length' => from_encrypted.to_s.length,
        # The same reasoning as `effective_from_secrets_yml`: a populated
        # `secrets` reader is evidence that *something* set the key, and this
        # is evidence that the encrypted file did.  Decrypted a second time,
        # independently, and compared by value.
        'effective_from_encrypted' =>
          !(from_encrypted.nil? || from_encrypted.to_s.empty?) &&
          from_encrypted.to_s == from_yaml.to_s,
        # Rails 5.2's credentials.  The same reasoning as
        # `effective_from_encrypted` one more time, with one difference that
        # matters: the comparison is against `Rails.application.secret_key_base`
        # and not against `Rails.application.secrets.secret_key_base`, because
        # on 5.2 those are different questions.  `secrets` does not consult
        # credentials at all (`railties/lib/rails/application.rb`, `secrets`);
        # `secret_key_base` is `ENV["SECRET_KEY_BASE"] || credentials
        # .secret_key_base || secrets.secret_key_base`, so it is the only
        # reader that says which store the application is actually signing
        # from.  A tree that ships a credentials file the application never
        # reads satisfies `credentials_present` and not this.
        #
        # The third clause is there because `EncryptedConfiguration` runs no
        # ERB -- it is `YAML.load` of the decrypted bytes and nothing else --
        # so a credentials file written as
        # `secret_key_base: <%= ENV["SECRET_TOKEN"] %>` decrypts to that
        # sentence as a *string*, the application signs with the sentence, and
        # the first two clauses are both satisfied by a tree that moved no
        # key at all.  Without this clause the reading answers "credentials
        # are wired up" for a tree whose credentials hold a template; with it
        # the reading is what its name says.  `credentials_template` below
        # reports the same observation on its own so a log can tell the two
        # failures apart.
        'effective_from_credentials' =>
          !(from_credentials.nil? || from_credentials.to_s.empty?) &&
          from_credentials.to_s == effective.to_s &&
          !erb_template?(from_credentials),
        # True when the decrypted value is an unevaluated ERB tag rather than
        # a key.  Reported separately and not gated on: it is a reason for
        # `effective_from_credentials`, not a requirement of its own.
        'credentials_template' => erb_template?(from_credentials),
        'credentials_length' => from_credentials.to_s.length,
        'effective_key_length' => effective.to_s.length,
        'credentials_key_file_present' => File.exist?(master_key_path),
        'credentials_key_from_env' => !ENV['RAILS_MASTER_KEY'].to_s.empty?,
        'credentials_file_present' => File.exist?(credentials_path),
        # Every file still carrying the 5.1 encrypted-secrets store, by
        # basename and anywhere in the tree -- `config/secrets.yml`,
        # `config/secrets.yml.enc`, `config/secrets.yml.key`, and the same
        # three moved somewhere else.  See `legacy_secret_store_files`.
        'legacy_secret_store_files' => legacy_secret_store_files,
        # 5.2+ only; nil below that, which is itself the version signal.
        'credentials_present' =>
          if app.respond_to?(:credentials)
            begin
              !app.credentials.secret_key_base.to_s.empty?
            rescue StandardError, NoMethodError
              false
            end
          end
      }
    end

    # Whether a value is an unevaluated ERB tag.  Deliberately loose about
    # what is inside the tag and strict about the shape: anything carrying
    # `<%` ... `%>` is a template that something was expected to evaluate and
    # nothing did, whatever it interpolates.
    def erb_template?(value)
      !(value.to_s =~ /<%.*%>/m).nil?   # `match?` is 2.4; the 2.3 tree runs this too
    end

    # What `config/credentials.yml.enc` itself supplies, decrypted
    # independently of the booted application and with the framework's own
    # reader rather than a reimplementation -- the same arrangement
    # `encrypted_secrets_value` uses for 5.1's store, and for the same reason.
    #
    # `ActiveSupport::EncryptedConfiguration` takes absolute paths, so unlike
    # `Rails::Secrets` there is no global root to point at the tree and put
    # back.
    #
    # One measured difference from `Rails::Secrets` decides how this step's
    # instruction is written: **ERB does not run**.  `Rails::Secrets.parse`
    # pipes the plaintext through `ERB` before `YAML`; `EncryptedConfiguration`
    # does not (measured: `other: <%= 1 + 1 %>` reads back as the literal
    # string).  This tree's `secrets.yml.enc` holds
    # `secret_key_base: <%= ENV["SECRET_TOKEN"] %>`, so a candidate who copies
    # the YAML across verbatim ends up signing with a 25-character template
    # instead of a key.  The gate reads that as a mismatch, which is what it
    # is.
    def credentials_value(path, key_path)
      return nil unless defined?(ActiveSupport::EncryptedConfiguration)
      return nil unless File.exist?(path)

      config = ActiveSupport::EncryptedConfiguration.new(
        config_path: path, key_path: key_path,
        env_key: 'RAILS_MASTER_KEY', raise_if_missing_key: true
      )
      parsed = config.config
      return nil unless parsed.respond_to?(:[])

      parsed[:secret_key_base] || parsed['secret_key_base']
    rescue StandardError, SyntaxError
      nil
    end

    # The key the application is actually signing with, asked of the
    # application.
    #
    # From 4.1 this raises rather than returning nil when nothing supplies a
    # key in a non-development environment, so the rescue is the reading: a
    # tree with no key at all reports `nil` here and fails every gate that
    # compares against it, which is correct and is not a probe crash.
    def effective_secret_key_base
      Rails.application.secret_key_base
    rescue StandardError, NoMethodError
      nil
    end

    # Every file still carrying Rails 5.1's encrypted-secrets store.
    #
    # Matched on basename and not on the `config/` path, for the reason
    # `plaintext_secret_files` gives about renames: the property is "the old
    # store is gone", and a tree that moved `secrets.yml.enc` to
    # `config/legacy/` has not removed it, it has hidden it.  The key file is
    # 32 hex characters and the encrypted file is base64 ciphertext that
    # mentions nothing, so neither of them trips `plaintext_secret_files` --
    # that probe answers "is a key lying around in the clear", this one
    # answers "is the superseded store still here", and at this step both are
    # asked.
    def legacy_secret_store_files
      found = []
      walk_tree(@app) do |rel, _full|
        found << rel if File.basename(rel).start_with?('secrets.yml')
      end
      found.sort.first(50)
    end

    # Every file in the tree that carries a signing key in the clear -- not
    # just `config/secrets.yml`.
    #
    # The property is "the key this application boots with is not also lying
    # around in plaintext", and a tree that renamed the old file to
    # `config/secrets.yml.bak`, or copied it to `config/secrets.dev.yml`, or
    # left it in a fixture under `spec/`, satisfies "there is no
    # `config/secrets.yml`" while still shipping the key.  One path cannot be
    # the reading; the tree has to be.
    #
    # Two ways a file lands on this list, because either alone is wrong:
    #
    #   * by NAME -- anything matching `config/secrets.yml*` other than the two
    #     files that are supposed to be there (`.enc` and `.key`).  This
    #     catches a rename whose contents this probe declined to read.
    #   * by CONTENT -- a file that mentions `secret_key_base` and also carries
    #     a run of 64 or more hex characters.  64 is what `rake secret`
    #     produces; the encryption key in `config/secrets.yml.key` is 32, so
    #     the file that is meant to be there cannot reach this threshold, and
    #     a `secrets.yml` whose value is an ERB call to `ENV` does not either
    #     -- which is correct, since that tree is not carrying a key.
    #
    # The walk skips the directories that are build products or history rather
    # than source (`.git`, `tmp`, `log`, `node_modules`, `coverage`,
    # `public/assets`, `vendor/bundle`), and files over 1 MiB; a key is 64
    # bytes and does not need a megabyte to hide in.  Binary files are read as
    # binary and simply will not match.
    PLAINTEXT_KEY_RUN = /[0-9a-f]{64,}/
    PLAINTEXT_SKIP_DIRS = %w[.git tmp log node_modules coverage .bundle].freeze
    PLAINTEXT_SKIP_PATHS = %w[public/assets vendor/bundle].freeze
    PLAINTEXT_ALLOWED = %w[config/secrets.yml.enc config/secrets.yml.key].freeze

    def plaintext_secret_files
      found = []
      walk_tree(@app) do |rel, full|
        next if PLAINTEXT_ALLOWED.include?(rel)

        if rel.start_with?('config/secrets.yml')
          found << rel
          next
        end
        next if File.size(full) > 1_048_576

        body = begin
          IO.read(full, mode: 'rb')
        rescue StandardError
          nil
        end
        next if body.nil?

        text = body.force_encoding('ASCII-8BIT')
        found << rel if text.include?('secret_key_base') && text =~ PLAINTEXT_KEY_RUN
      end
      found.sort.first(50)
    end

    # A depth-first walk of the tree under test, yielding `[relative, absolute]`
    # for every regular file that is not inside a skipped directory.  Symlinks
    # are not followed: a link into the gem home would turn this into a walk of
    # the whole image.
    def walk_tree(root, rel = '', &block)
      Dir.entries(File.join(root, rel)).sort.each do |entry|
        next if entry == '.' || entry == '..'
        next if PLAINTEXT_SKIP_DIRS.include?(entry)

        child = rel.empty? ? entry : File.join(rel, entry)
        next if PLAINTEXT_SKIP_PATHS.include?(child)

        full = File.join(root, child)
        next if File.symlink?(full)

        if File.directory?(full)
          walk_tree(root, child, &block)
        elsif File.file?(full)
          block.call(child, full)
        end
      end
    rescue StandardError
      nil
    end

    # Where this application's `config/secrets.yml` is, asked of the
    # application rather than assumed.
    #
    # `Rails.application.paths['config/secrets']` is the framework's own answer
    # and honours an application that moved the file; it exists from 4.1, which
    # is the release that introduced the file, and is `nil` before that.
    # `Rails.root` is the fallback, and the directory the probe was pointed at
    # is the fallback's fallback -- a probe that cannot ask still reports the
    # path it looked at rather than nil, so a `false` here is always traceable
    # to a path.
    # What `config/secrets.yml` itself supplies for the running environment,
    # read the way the framework reads it: ERB first, then the section named
    # for `Rails.env`.  This is deliberately a second, independent reading and
    # not a field off the booted application, because the booted application
    # cannot tell the two arrangements apart -- `Rails.application.secrets
    # .secret_key_base` is `||=`'d from `config.secret_key_base`, so a
    # populated reader is evidence that *something* set the key and not that
    # this file did.
    #
    # Any failure is nil, which fails the gate: a `secrets.yml` this cannot
    # read is not one the gate should pass on the strength of the reader
    # agreeing with itself.
    def secrets_yml_value(path)
      return nil if path.nil? || !File.exist?(path)

      require 'erb'
      require 'yaml'
      doc = YAML.load(ERB.new(IO.read(path)).result)
      return nil unless doc.is_a?(Hash)

      section = doc[Rails.env.to_s]
      return nil unless section.is_a?(Hash)

      section['secret_key_base'] || section[:secret_key_base]
    rescue StandardError, SyntaxError
      nil
    end

    # What `config/secrets.yml.enc` itself supplies for the running
    # environment, decrypted independently of the booted application.
    #
    # `Rails::Secrets` is the framework's own reader (railties 5.1,
    # `rails/secrets.rb`): it takes the key from `RAILS_MASTER_KEY` or from
    # `config/secrets.yml.key`, decrypts with aes-128-gcm, runs ERB over the
    # plaintext and parses the YAML.  Asking it rather than reimplementing it
    # makes the reading "the framework can read this file", which is the
    # property, rather than "this file has the shape I expected".
    #
    # `Rails::Secrets` resolves its key path against its own `root`, which
    # defaults to `File` -- that is, to the current directory.  The root is
    # pointed at the tree under test for the duration and put back afterwards,
    # and the memoised encryptor is dropped on both sides so neither reading
    # can be served from the other's key.
    def encrypted_secrets_value(path)
      return nil unless defined?(Rails::Secrets)
      return nil unless File.exist?(path)

      require 'pathname'
      previous = Rails::Secrets.instance_variable_get(:@root)
      begin
        Rails::Secrets.root = Pathname.new(@app)
        Rails::Secrets.instance_variable_set(:@encryptor, nil)
        parsed = Rails::Secrets.parse([path], env: Rails.env.to_s)
        parsed[:secret_key_base] || parsed['secret_key_base']
      ensure
        Rails::Secrets.root = previous
        Rails::Secrets.instance_variable_set(:@encryptor, nil)
      end
    rescue StandardError, SyntaxError
      nil
    end

    def secrets_yml_path
      app = Rails.application
      entry = (app.paths['config/secrets'] if app.respond_to?(:paths))
      if entry
        # `expanded` and not `existent`: the configured path is wanted whether
        # or not anything is at it, so that an absent file is reported as
        # `false` *at a path* rather than as a fallback guess.
        list = if entry.respond_to?(:expanded) then entry.expanded
               elsif entry.respond_to?(:existent) then entry.existent
               else Array(entry)
               end
        first = Array(list).first
        return first.to_s unless first.nil?
      end
      root = (Rails.root if Rails.respond_to?(:root))
      return File.join(root.to_s, 'config', 'secrets.yml') unless root.nil?

      File.join(@app, 'config', 'secrets.yml')
    rescue StandardError, NoMethodError
      File.join(@app, 'config', 'secrets.yml')
    end

    # Rails 7.1's `Rails.application.deprecators` is a registry of every
    # framework's own deprecator.  Counting them is a cheap way to see that the
    # registry exists at all, which is itself a version signal.
    def deprecator_count
      return nil unless Rails.application.respond_to?(:deprecators)

      registry = Rails.application.deprecators
      return nil unless registry.respond_to?(:each)

      count = 0
      registry.each { count += 1 }
      count
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { app: Dir.pwd }
  OptionParser.new do |opts|
    opts.on('--app PATH') { |v| options[:app] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
    opts.on('--outside LIST') { |v| options[:outside] = v.split(',').reject(&:empty?) }
  end.parse!(ARGV)

  report = CVGate::BootProbe.new(options[:app], options[:outside] || []).run
  payload = JSON.pretty_generate(report)
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
  # Always exit 0: the gate reads the report and decides.  A non-zero exit here
  # would turn "the app did not boot" into "the probe crashed", and the two
  # have to stay distinguishable.
  exit 0
end
