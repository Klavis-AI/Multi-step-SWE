# frozen_string_literal: true

# Does `rake` still load?
#
#   bundle exec ruby /gates/cvgate/raketasks.rb --app /app --out rake.json
#
# Every other instrument here reads the application through `config/
# environment.rb` -- `bootprobe.rb` and `depdrive.rb` boot production,
# `envboot.rb` boots development and test, rspec boots test.  None of them
# loads `Rakefile`, and `Rakefile` is a second entry point into this tree with
# a loader of its own: `Rails.application.load_tasks` reads EVERY file under
# `lib/tasks/` before rake runs anything at all.  So a file in there that names
# a constant the bundle no longer defines is not a broken task.  It is a broken
# task tree -- `db:migrate`, `db:schema:load` and `assets:precompile` included
# -- on a tree that boots in all three environments, passes its whole suite and
# serves every page.
#
# Measured on this repository, at the 8.0 rung: `lib/tasks/
# auto_annotate_models.rake` is what the `annotate` gem's installer wrote in
# 2014, and it calls `Annotate.set_defaults` and `Annotate.load_tasks`.  That
# gem's last release caps `activerecord < 8.0`, so the step replaces it with
# the maintained fork, whose constant is `AnnotateRb`.  Leave the 2014 file
# where it is and `rake -T` dies with `NameError: uninitialized constant
# Annotate`, while every other reading in this verifier stays green -- the file
# guards itself with `if Rails.env.development?`, and nothing else here is
# development.
#
# DEVELOPMENT IS THE ENVIRONMENT ON PURPOSE.  It is the one a rake file is most
# likely to guard itself into, it is the environment a person runs `rake -T` in,
# and it is where this repository's own guard puts the file above.
#
# WHAT IS ASKED IS THE WHOLE LIST, not a task.  `rake -T` loads the Rakefile,
# the application and every task file, prints what it found and exits; it
# touches no database and writes nothing.  Running an actual task would measure
# the task, and would put a migration between this reading and the ones after
# it.
#
# `ok` is the exit status AND a non-empty list, because both halves fail
# differently and both are the same finding.  A rake whose loader raised exits
# non-zero with an empty list; a `Rakefile` deleted to make the error go away
# exits zero with one.  Neither tree can produce its task list.
#
# Exits zero whatever it finds, for the reason `depdrive.rb` and `envboot.rb`
# do: the finding is the report.  A non-zero exit here would make a tree whose
# task tree is broken indistinguishable from an instrument that failed to run,
# and the second is a defect in this verifier rather than a property of the
# tree.

require 'json'
require 'optparse'
require 'open3'

module CVGate
  module RakeTasks
    ENVIRONMENT = 'development'

    # `rake -T` prints `rake <task>[ <args>]   # <description>`, one per line,
    # for the tasks that have a description.  Anything else on stdout -- a
    # warning from a gem, the `bigdecimal.so` allocator notice this image's
    # 2.x interpreters print -- is not a task and is left out rather than
    # counted.  Its own method so the selftest can drive it against canned
    # output: the difference between "50 tasks" and "50 lines" is the whole
    # reading.
    def self.parse(stdout)
      Array(stdout.to_s.split("\n")).map do |line|
        match = /\Arake\s+(\S+)/.match(line)
        match && match[1]
      end.compact
    end

    # The verdict, separated from the running of it for the same reason.
    # `status` is the child's exit status as an Integer, or nil if it never
    # produced one.
    def self.summarize(status, stdout, stderr)
      tasks = parse(stdout)
      { 'exit_status' => status,
        'ok' => status == 0 && !tasks.empty?,
        'task_count' => tasks.length,
        # Sorted and capped: the evidence is that the list exists and what is
        # in it, not the order rake happened to print it in.  200 is four
        # times what this repository has ever declared.
        'tasks' => tasks.sort.uniq.first(200),
        # Two readings this step cares about, reported rather than counted.
        # The first is the task the broken loader takes down with it; the
        # second is what the retired gem contributed and what the fork
        # contributes in its place, which a step that swapped one for the
        # other can be read for without gating on a task name.
        #
        # `annotat` and not `annotate`: the retired gem describes three tasks
        # and one of them is `remove_annotation`, so the longer spelling
        # counts two of the three and reads like a tree half way through the
        # swap (measured).
        'has_db_migrate' => tasks.include?('db:migrate'),
        'annotate_tasks' => tasks.count { |name| name.include?('annotat') },
        # First lines only.  A `NameError` from the loader arrives with a
        # backtrace through the whole of rake, and the useful part is the top.
        'stderr_head' => Array(stderr.to_s.split("\n")).reject(&:empty?).first(12) }
    end

    def self.run(app, environment = ENVIRONMENT)
      command = %w[bundle exec rake -T]
      report = { 'app' => app, 'env' => environment,
                 'command' => command.join(' '), 'ruby' => RUBY_VERSION }
      begin
        stdout, stderr, status = Open3.capture3(
          { 'RAILS_ENV' => environment, 'RACK_ENV' => environment },
          *command, chdir: app
        )
        report.merge!(summarize(status.exitstatus, stdout, stderr))
        report['signalled'] = !status.exitstatus
      rescue StandardError, SystemCallError => e
        # `rake` not on the path, `app` not a directory, the fork failing.
        # A report rather than a crash, and `ok` false either way.
        report.merge!(summarize(nil, '', ''))
        report['error'] = "#{e.class}: #{e.message}"
      end
      report
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { app: '/app', out: nil, env: CVGate::RakeTasks::ENVIRONMENT }
  OptionParser.new do |opts|
    opts.on('--app PATH') { |v| options[:app] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
    opts.on('--env NAME') { |v| options[:env] = v }
  end.parse!(ARGV)

  result = CVGate::RakeTasks.run(File.expand_path(options[:app]), options[:env])
  payload = JSON.pretty_generate(result)
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
  exit 0
end
