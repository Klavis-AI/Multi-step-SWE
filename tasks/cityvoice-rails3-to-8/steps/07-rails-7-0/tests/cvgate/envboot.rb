# frozen_string_literal: true

# Does the application still boot in the environments nothing else boots?
#
#   bundle exec ruby /gates/cvgate/envboot.rb --app /app --out envboot.json
#
# `bootprobe.rb` and `depdrive.rb` boot `production`, and the spec suite boots
# `test` through rspec.  Nothing boots `development`, and nothing reads
# `config/environments/test.rb` for what it says -- rspec's own boot happens
# under a `RAILS_ENV` the suite sets and reports its findings as example
# failures, which a configuration deprecation is not.  So each release's
# renames and removals inside those two files are invisible to every other
# gate, and a tree can score full marks with a development environment that
# cannot boot at all.
#
# Measured, on this repository, on two steps of the ladder:
#
#   * 4.2 -> 5.0 drops `Rack::Lock` from the default middleware stack, and
#     `config/environments/development.rb` anchors rack-livereload to it with
#     `insert_before`.  Left alone, booting development on 5.0.7.2 raises
#     `RuntimeError: No such middleware to insert before: Rack::Lock` from
#     actionpack's `stack.rb:108` and the process exits 1.
#   * 3.2 -> 4.2 renames `config.serve_static_assets` to `serve_static_files`,
#     and 4.2 -> 5.0 renames it again to `config.public_file_server.enabled`
#     and `config.static_cache_control` to `public_file_server.headers`.  All
#     four of those calls live in `config/environments/test.rb`, whose
#     deprecations are raised while rspec boots and are attributed to that
#     file -- not to anything under `spec/`, which is what the suite's own
#     deprecation gate counts.
#
# PRODUCTION IS DELIBERATELY NOT BOOTED HERE.  Two probes already do it, and a
# third reading of the same environment would charge the same defect twice and
# make the gate weights a function of how many instruments happen to exist.
# `skipped` says so in the report rather than leaving the omission to be
# inferred from a missing key.
#
# ONE ENVIRONMENT FAILING MUST NOT LOSE THE OTHERS, and a Rails application is
# a singleton -- `Rails.application` is set once and `config/environment.rb`
# refuses to run twice -- so each environment is booted in its own process and
# reports through a file.  A child that dies without writing one is reported as
# a failure with its exit status, which is the one outcome a naive
# `JSON.parse(capture)` turns into a crash of the gate instead of a finding
# about the tree.
#
# `--expect` is why the report cannot be satisfied by deletion.  The scanner's
# pristine manifest covers `app/` and `lib/`, not `config/`, so "every
# environment this tree declares boots" is true of a tree with no
# `config/environments/development.rb` in it at all.  The two environments
# this repository shipped are named, and a named environment whose file is
# gone is a failure, not an absence.

require 'json'
require 'optparse'

require File.join(File.dirname(File.expand_path(__FILE__)), 'depdrive')
require File.join(File.dirname(File.expand_path(__FILE__)), 'bundledgems')

module CVGate
  module EnvBoot
    # The environments this repository ships besides production.  Named rather
    # than discovered: see the header.
    EXPECT = %w[development test].freeze

    # Booted elsewhere.  A name in here is reported, not read.
    SKIP = %w[production].freeze

    def self.safely
      yield
    rescue Exception => e # rubocop:disable Lint/RescueException
      # `Exception`: a framework reader that raises `NotImplementedError` or a
      # `SystemExit` from a half-loaded initializer would otherwise take the
      # whole report with it, which reads in the reward exactly like a tree
      # that did not boot.
      { 'error' => "#{e.class}: #{e.message}" }
    end

    def self.declared(app)
      Dir[File.join(app, 'config', 'environments', '*.rb')]
        .map { |path| File.basename(path, '.rb') }.sort
    end

    # Which of a boot's own deprecation call sites a gate states a requirement
    # about.  Restricted to the environment files because those are what this
    # probe reads that nothing else does, and because `config/application.rb`
    # carries one deprecation on this repository that belongs to a gem:
    # factory_girl 4.9 announces its own rename from inside `Bundler.require`,
    # and `blame` attributes it to the application line that did the requiring.
    # Holding a candidate to that would be holding them to which gems the
    # resolver happened to pick.
    #
    # Its own method so the selftest can drive it against canned call sites: it
    # is one `select` and it is the whole difference between a gate about this
    # application's configuration and a gate about its gem set.
    def self.env_file_only(paths)
      Array(paths).select { |where| where.start_with?('config/environments/') }
                  .uniq.sort
    end

    # The boot itself, in a process of its own.  Runs in the child.
    def self.child(app, env)
      report = { 'env' => env, 'booted' => false }
      records = []
      ENV['RAILS_ENV'] = env
      ENV['RACK_ENV'] = env

      # The collector goes in before the boot: most of what an environment
      # file deprecates, it deprecates while the framework is reading it, and
      # a collector installed afterwards reports a clean tree every time.
      installed = safely { CVGate::DepDrive.install(app, records) }
      report['collector_error'] = installed['error'] if installed.is_a?(Hash) && installed['error']

      boot_file = File.join(app, 'config', 'environment.rb')
      if File.exist?(boot_file)
        begin
          require boot_file
          report['booted'] = true
        rescue Exception => e # rubocop:disable Lint/RescueException
          report['error'] = "#{e.class}: #{e.message}"
          report['backtrace'] = Array(e.backtrace).first(20)
        end
      else
        report['error'] = "no config/environment.rb under #{app}"
      end

      # The reading `bootprobe.rb` takes in production, taken here in the two
      # environments nothing else boots.  A library the interpreter is about to
      # stop shipping is loaded by whoever loads it, and who that is differs by
      # environment: production eager-loads the application's own files, while
      # development and test reach the same code through `to_prepare` and
      # through the suite (measured).  Off the load
      # path, not off a warning -- see `cvgate/bundledgems.rb`.  Taken whether
      # or not the boot finished, because a boot that died half way still
      # loaded whatever it loaded.
      report['bundled_gem_stdlib'] = safely { CVGate::BundledGems.read }

      if report['booted']
        # What the application says it is, so that a boot which silently fell
        # back to another environment is visible.  A `development` boot that
        # reports `production` is not a reading of development.rb.
        report['rails_env'] = safely { Rails.env.to_s }
        report['rails'] = safely { Rails::VERSION::STRING }
        report['eager_load'] = safely { Rails.application.config.eager_load }
        # The built stack, not the list of operations queued against it: the
        # whole point on this step is that an operation naming a middleware
        # that is no longer in the stack raises while the stack is built.
        report['middleware'] =
          safely { Rails.application.middleware.map { |used| used.name.to_s } }
        # `show_exceptions`, read as the framework will act on it rather than
        # as the environment file spells it.  Rails 7.1 deprecated the boolean
        # values in favour of `:all`, `:rescuable` and `:none`; 7.2 REMOVED
        # them, and the removal did not restore an error -- actionpack-7.2's
        # `ExceptionWrapper#show?` simply dropped the `when true` and
        # `when false` arms, so `false` falls through to `else` and answers
        # TRUE.  A test environment that has said `= false` since 2014 keeps
        # parsing and starts meaning its opposite.  Both readings are kept:
        # the declaration, and what the wrapper answers for a request in this
        # environment, which is the one that moves.
        report['show_exceptions'] =
          safely { Rails.application.config.action_dispatch.show_exceptions.inspect }
        probe_request = lambda do
          require 'stringio'
          ActionDispatch::Request.new(
            Rails.application.env_config.merge(
              'REQUEST_METHOD' => 'GET', 'PATH_INFO' => '/',
              'rack.input' => StringIO.new
            )
          )
        end
        report['exceptions_shown'] = safely do
          ActionDispatch::ExceptionWrapper.new(nil, RuntimeError.new('probe'))
                                          .show?(probe_request.call)
        end
        # And the same question asked with an exception the framework has a
        # status for, because `:none` and `:rescuable` answer the first one
        # identically.  7.2's `show?` is a three-arm case: `:none` is false,
        # `:rescuable` is `rescue_responses.key?(exception.class.name)`, and
        # everything else -- including the two removed booleans -- is true
        # (actionpack-7.2.3.2 exception_wrapper.rb:179-196).  RuntimeError is
        # not a key, so it reads false under BOTH named values; RecordNotFound
        # is one, so it reads true under `:rescuable` and false under `:none`.
        # Measured on one tree, six header values.
        report['exceptions_shown_rescuable'] = safely do
          ActionDispatch::ExceptionWrapper.new(nil, ActiveRecord::RecordNotFound.new('probe'))
                                          .show?(probe_request.call)
        end
        # The three readings above taken as the one property a step can gate
        # on, and the reason no one of them will do.  `show?` answering false
        # for a plain exception is not evidence of a migrated tree: on 7.1 the
        # booleans 7.2 removed still work, so the tree the 7.2 step is HANDED
        # answers false too -- measured rather than reasoned: the step-08
        # golden tree, scored by the step-09 verifier on that reading alone,
        # collects four of eighteen gate points for having done nothing.  The
        # declaration alone is a gate on having typed a line.
        # And `:rescuable` is neither: it is a value this release does have,
        # so it clears the declaration half, and it answers the plain
        # exception exactly as `:none` does.  All three together say the
        # requirement and nothing else: the environment lets the exception
        # out, it lets out the ones the framework has a status for as well,
        # and it says so in a vocabulary the running release still
        # understands.
        report['exceptions_let_out'] =
          report['exceptions_shown'] == false &&
          report['exceptions_shown_rescuable'] == false &&
          !%w[true false].include?(report['show_exceptions'].to_s)
      end

      found = CVGate::DepDrive.summarise(records)
      report['deprecation_count'] = found['count']
      report['own_deprecation_count'] = found['own_count']
      report['own'] = found['own']
      report
    end

    def self.boot_one(app, env, dir)
      out = File.join(dir, "#{env}.json")
      if Process.respond_to?(:fork)
        pid = fork do
          result = child(app, env)
          File.write(out, JSON.generate(result))
          # `exit!`, not `exit`: Rails installs `at_exit` hooks, and one of
          # them raising after the report is written would turn a recorded
          # reading into a child that "failed".
          exit!(0)
        end
        _, status = Process.waitpid2(pid)
        code = status.exitstatus
      else
        ok = system(RbConfig.ruby, File.expand_path(__FILE__),
                    '--app', app, '--one', env, '--out', out)
        code = ok ? 0 : 1
      end

      return JSON.parse(File.read(out)) if File.exist?(out) && File.size(out) > 0

      { 'env' => env, 'booted' => false,
        'error' => "the boot process wrote no report and exited #{code.inspect}" }
    rescue JSON::ParserError => e
      { 'env' => env, 'booted' => false,
        'error' => "the boot process wrote an unreadable report: #{e.message}" }
    end

    def self.run(app, expect = EXPECT, skip = SKIP)
      here = declared(app)
      checked = ((expect + here) - skip).uniq.sort
      report = { 'app' => app, 'ruby' => RUBY_VERSION, 'declared' => here,
                 'expected' => expect.sort, 'skipped' => skip.sort,
                 'checked' => checked, 'envs' => {} }

      dir = File.join(ENV['TMPDIR'] || '/tmp', "cvenvboot-#{Process.pid}")
      require 'fileutils'
      FileUtils.mkdir_p(dir)
      begin
        checked.each do |env|
          report['envs'][env] =
            if File.exist?(File.join(app, 'config', 'environments', "#{env}.rb"))
              boot_one(app, env, dir)
            else
              # A named environment whose file is gone.  Reported as a failure
              # rather than skipped: "it does not boot" and "there is nothing
              # to boot" are the same finding about a tree that was asked to
              # keep it.
              { 'env' => env, 'booted' => false,
                'error' => "no config/environments/#{env}.rb" }
            end
        end
      ensure
        FileUtils.rm_rf(dir)
      end

      report['failed'] =
        checked.reject { |env| report['envs'][env]['booted'] }
      # `inject`, not `sum`: `Array#sum` arrived in Ruby 2.4 and this probe runs
      # inside the candidate's bundle on whichever interpreter the tree selects,
      # which is 2.1.7 at the bottom of this ladder.
      # A boot whose collector could not be installed counted nothing, and
      # nothing counted is not zero: with any such boot the two deprecation
      # readings below are absent (nil), which the gates read as not taken,
      # instead of a 0 and an empty list that read as a clean tree.
      report['collector_failed'] =
        checked.select { |env| report['envs'][env]['collector_error'] }
      blind = !report['collector_failed'].empty?
      report['own_deprecation_count'] =
        if blind
          nil
        else
          checked.inject(0) do |total, env|
            total + report['envs'][env]['own_deprecation_count'].to_i
          end
        end
      report['env_file_deprecations'] =
        if blind
          nil
        else
          env_file_only(
            checked.flat_map { |env| Array(report['envs'][env]['own']) }
          )
        end
      # The union over the environments that were booted, so that one gate can
      # ask this of development and test together rather than once per
      # environment -- the requirement is about the tree, and which of its
      # environments happens to load the library first is not the candidate's
      # business.  A child that could not take the reading contributes nothing
      # rather than a false finding: it reports an error object instead of an
      # array, and `failed` above is what says so.
      report['bundled_gem_stdlib'] =
        checked.flat_map do |env|
          found = report['envs'][env]['bundled_gem_stdlib']
          found.is_a?(Array) ? found : []
        end.uniq.sort
      report
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { app: '/app', out: nil, one: nil,
              expect: CVGate::EnvBoot::EXPECT, skip: CVGate::EnvBoot::SKIP }
  OptionParser.new do |opts|
    opts.on('--app PATH') { |v| options[:app] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
    opts.on('--one ENV') { |v| options[:one] = v }
    opts.on('--expect LIST') { |v| options[:expect] = v.split(',').reject(&:empty?) }
    opts.on('--skip LIST') { |v| options[:skip] = v.split(',').reject(&:empty?) }
  end.parse!(ARGV)

  app = File.expand_path(options[:app])
  Dir.chdir(app)
  result = if options[:one]
             # The no-fork fallback's worker.  Writes the same shape a forked
             # child writes, so the two paths cannot drift.
             CVGate::EnvBoot.child(app, options[:one])
           else
             CVGate::EnvBoot.run(app, options[:expect], options[:skip])
           end
  payload = options[:one] ? JSON.generate(result) : JSON.pretty_generate(result)
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
  # Always zero, for the reason depdrive.rb exits zero: the finding is the
  # report, and a non-zero exit here would make a tree whose development
  # environment is broken indistinguishable from a gate that failed to run.
  exit 0
end
