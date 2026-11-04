#!/usr/bin/env bash
# Reference solution -- step 10, "reach Rails 8.0".
#
# The tree this runs against is step 09's finished tree -- Rails 7.2.3.2 on Ruby
# 3.2.11, `config.load_defaults 7.2`, `show_exceptions = :none` in the test
# environment, and unicorn's `before_fork` disconnecting the pool instead of
# leasing a connection to throw away.  The first block below says so out loud:
# every count it takes is a fact about *step 09's* output, so running this
# against anything else stops on the first line instead of producing a tree that
# is neither.
#
# The interpreter does not move.  8.0 requires Ruby >= 3.2.0 and this tree has
# been on 3.2.11 since step 08, so for the second rung in a row `.ruby-version`
# is left exactly as it is -- and that is a fact worth asserting rather than
# assuming, which is what the last check in the first block does.
#
# Three pieces of work, in the order this file does them:
#
#   1. The pin, and one gem that cannot come with it.  `rails 8.0.5.1`,
#      resolved conservatively.  `annotate`'s last release caps
#      `activerecord < 8.0`, and bundler's answer to that is not a conflict
#      report: it walks the gem back to 2.6.5, from 2014, and every signal
#      this task has stays green.  The replacement is `annotaterb`, the
#      maintained fork.
#   2. `load_defaults 8.0`, which is three settings, and this is the first rung
#      where all three are live on this box.  One of them the release forces:
#      without it Active Support deprecates the old `to_time` behaviour from
#      this application's own `config/environment.rb`.
#   3. The tooling the gem swap leaves behind.  `lib/tasks/auto_annotate_models.rake`
#      calls `Annotate.set_defaults` and `Annotate.load_tasks`; with annotaterb
#      in place and that file untouched, `rake -T` dies with
#      `NameError: uninitialized constant Annotate` and takes `db:migrate` and
#      `assets:precompile` down with it -- while boot and the suite stay green.
#
# Four artefacts too long to write inline -- the resolved Gemfile, its
# lockfile, the annotaterb configuration and its rake hook -- ship as files
# beside this script.  Every edit states how many lines it expects to touch: a
# `sed` that matched nothing exits 0 and prints exactly what a successful one
# prints.
set -euo pipefail

APP=${APP:-/app}
FILES=${FILES:-/solution/files}
cd "$APP"

say() { printf '\n== %s\n' "$*"; }

# count <pattern> <path...> -- matching lines across the given files.
count() {
    local pattern="$1"; shift
    grep -rn -- "$pattern" "$@" 2>/dev/null | wc -l | tr -d ' '
}

# files <find-expression...> -- how many paths match, under the current tree.
files() { find "$@" 2>/dev/null | wc -l | tr -d ' '; }

# expect <what> <want> <got>
expect() {
    if [ "$2" != "$3" ]; then
        echo "solve.sh: $1: expected $2, got $3" >&2
        exit 1
    fi
    printf '   ok  %-46s %s\n' "$1" "$3"
}

# rewrite <file> <count> <from> <to> -- replace <from> with <to> in <file>,
# exactly <count> times.  The match is anchored at the end of a line, and both
# strings are literal, never patterns, so the `:`, `{` and `|` in a Ruby
# argument list mean themselves.
rewrite() {
    ruby -e '
      path, want, from, to = ARGV
      src = File.read(path)
      got = src.scan(from + "\n").size
      unless got == Integer(want)
        abort("rewrite: #{path}: #{got} lines end with #{from.inspect}, " \
              "wanted #{want}")
      end
      File.write(path, src.gsub(from + "\n") { to + "\n" })
    ' "$@"
    printf '   ok  %-30s %s\n' "$(basename "$1")" "$(printf '%s' "$3" | head -1)"
}


# ---------------------------------------------------------------------------
say "the tree this expects to find"
# ---------------------------------------------------------------------------
expect "rails 7.2.3.2 in the Gemfile"     1 "$(count "gem 'rails', '7.2.3.2'" Gemfile)"
expect "ruby 3.2.11"                      1 "$(count '^3\.2\.11$' .ruby-version)"
expect "load_defaults 7.2"                1 "$(count 'config.load_defaults 7\.2' config/application.rb)"
expect "show_exceptions :none, from 09"   1 "$(count 'show_exceptions = :none' config/environments/test.rb)"
expect "the pool disconnected, from 09"   1 "$(count 'connection_pool\.disconnect!' config/unicorn.rb)"
expect "the annotate gem, unversioned"    1 "$(count "gem 'annotate'" Gemfile)"
expect "and its rake file, calling it"    2 "$(count 'Annotate\.' lib/tasks/auto_annotate_models.rake)"
expect "no annotaterb configuration yet"  0 "$(files . -maxdepth 1 -name '.annotaterb.yml')"

# ---------------------------------------------------------------------------
say "the pin, and the gem that cannot come with it"
# ---------------------------------------------------------------------------
# `bundle lock --conservative --update rails` from the step-09 lockfile with
# only the rails line changed, then the annotate line replaced and the lock
# re-resolved.  149 gems either side:
#
#   changed (13)  the framework's own thirteen        7.2.3.2 -> 8.0.5.1
#   added   (2)   annotaterb 4.25.0, and `uri 1.1.1` -- a default gem the 8.0
#                 dependency graph names outright where 7.2's did not
#   removed (2)   annotate 3.2.0, and `cgi 0.5.2` -- one it stopped naming
#
# The annotate line is the finding.  `annotate`'s last release is 3.2.0, its
# gemspec says `activerecord >= 3.2, < 8.0`, and asked conservatively for Rails
# 8 bundler REPORTS NO CONFLICT: it resolves `annotate 2.6.5`, published
# 2014-06-16, whose only constraint is `activerecord >= 2.3.0`.  The tree boots,
# the suite passes, `rake annotate_models` still annotates all seven models.
# A ten-year downgrade, and nothing in this task's own instruments sees it.
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "rails pinned to 8.0"              1 "$(count "gem 'rails', '8.0.5.1'" Gemfile)"
expect "framework locked at 8.0"          1 "$(count '^    rails (8\.0\.5\.1)$' Gemfile.lock)"
expect "annotaterb in the Gemfile"        1 "$(count "gem 'annotaterb'" Gemfile)"
expect "annotaterb locked in"             1 "$(count '^    annotaterb (' Gemfile.lock)"
expect "annotate gone from the Gemfile"   0 "$(count "gem 'annotate'" Gemfile)"
expect "annotate gone from the lockfile"  0 "$(count '^    annotate (' Gemfile.lock)"
expect "the interpreter does not move"    1 "$(count '^3\.2\.11$' .ruby-version)"
expect "and the lockfile still says so"   1 "$(count '^   ruby 3\.2\.11p' Gemfile.lock)"

bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "8.0.5.1"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("3.2.")'

# ---------------------------------------------------------------------------
say "framework defaults: 7.2 becomes 8.0"
# ---------------------------------------------------------------------------
# `load_defaults "8.0"` is the 7.2 branch plus three assignments
# (railties-8.0.5.1 lib/rails/application/configuration.rb, the `when "8.0"`
# arm).  Read on a booted process, at the place each is really consulted,
# 7.2 -> 8.0:
#
#   ActiveSupport.to_time_preserves_timezone                  true -> :zone
#   ActionDispatch::Http::Cache::Request.strict_freshness    false -> true
#   Regexp.timeout                                             nil -> 1.0
#
# Three notes, all measured rather than read off the release guide:
#
#   * `to_time_preserves_timezone` is the one the release FORCES.  Active
#     Support's setter warns unless the value is exactly `:zone`, and the
#     warning is attributed to `config/environment.rb` -- an application file --
#     so it lands in this task's `own_deprecation_count`, which every step since
#     05 holds at zero.  A tree that takes 8.0 and declines its defaults fails
#     that guard and scores no gates at all.  Nothing in this application calls
#     `to_time`: the only conversion in app/, lib/, config/ or db/ is
#     `answer.created_at.to_date` in the weekly-activity mailer, and `to_date`
#     is not affected, so the flip changes no recorded byte.
#   * `strict_freshness` is a declaration on this tree.  It
#     is consulted inside `ActionDispatch::Http::Cache::Request#fresh?`, whose
#     only callers are `fresh_when` and `stale?`; this application writes
#     neither, and the conditional GET that every response goes through is
#     Rack's, which never reaches the flag.  It is still read off the class the
#     railtie assigns it to rather than off `config.action_dispatch`, because
#     that is where a tree that declared 8.0 and then assigned `false` back is
#     caught.
#   * `Regexp.timeout` is `Regexp.timeout ||= 1` in railties and reads back as
#     the Float `1.0`.  It is a process-global of the interpreter, which is why
#     it can be read at all on this rung: Ruby 3.2 is where `Regexp.timeout`
#     exists.
rewrite config/application.rb 1 \
'    # One declaration of the release whose defaults the application takes.
    # "7.2" is the 7.1 branch plus five settings: YJIT enabled where the
    # interpreter has it, Active Job enqueues deferred to after the
    # transaction commits, migration timestamps validated against the clock,
    # Active Storage'"'"'s web image content types -- which this application does
    # not load the framework for -- and a PostgreSQL adapter that decodes
    # dates, so an untyped query answers with a Date rather than a String.
    config.load_defaults 7.2' \
'    # One declaration of the release whose defaults the application takes.
    # "8.0" is the 7.2 branch plus three settings, and this is the first rung
    # where every one of the three is really live on this box: `to_time` keeps
    # the receiver'"'"'s time zone instead of its offset -- which is also the one
    # the release forces, because without it Active Support deprecates the old
    # behaviour from this application'"'"'s own `config/environment.rb` -- an ETag
    # takes precedence over a `Last-Modified` when a conditional request sends
    # both, and Ruby'"'"'s global regular-expression timeout is one second.
    config.load_defaults 8.0'
expect "the declaration moved"            1 "$(count 'config.load_defaults 8\.0' config/application.rb)"
expect "and there is only one of it"      1 "$(count 'config.load_defaults' config/application.rb)"

# ---------------------------------------------------------------------------
say "the tooling the gem swap leaves behind"
# ---------------------------------------------------------------------------
# `lib/tasks/auto_annotate_models.rake` is the file the annotate gem's own
# installer wrote in 2014.  It calls `Annotate.set_defaults` with twenty-one
# options and then `Annotate.load_tasks`, and with annotaterb in the bundle
# that constant does not exist:
#
#     NameError: uninitialized constant Annotate
#       Annotate.load_tasks
#     Did you mean?  AnnotateRb
#
# Rake loads every file under `lib/tasks/` before it runs anything, so that is
# not a broken annotate task -- it is a broken `rake`, including `db:migrate`
# and `assets:precompile`, in development only, where the `if
# Rails.env.development?` guard on line 4 puts it.  Boot is fine, the suite is
# fine, and nothing this task measured before this step would have noticed.
#
# The replacement is what `rails g annotate_rb:install` writes: a rake hook
# that loads annotaterb's own tasks, and `.annotaterb.yml`, which carries the
# options as data.  The generator's file is taken as it comes.  Nothing scores
# it, and that is deliberate: annotaterb's own defaults already put the schema
# block above the class and list the indexes (annotate_rb/options.rb,
# DEFAULTS), which is where the 2014 file put it too, so every line in that
# yaml agrees with the value annotaterb would have used anyway.  A gate on it
# would pay for running a generator rather than for a property of the tree --
# the property that matters is the one below, that `rake -T` still loads.
rm -f lib/tasks/auto_annotate_models.rake
install -m 0644 "$FILES/annotate_rb.rake" lib/tasks/annotate_rb.rake
install -m 0644 "$FILES/annotaterb.yml" .annotaterb.yml
expect "the 2014 rake file is gone"       0 "$(files lib/tasks -name 'auto_annotate_models.rake')"
expect "no Annotate constant left"        0 "$(count 'Annotate\.' app lib config spec db)"
expect "the annotaterb hook is installed" 1 "$(count 'AnnotateRb::Core.load_rake_tasks' lib/tasks/annotate_rb.rake)"
expect "positions preserved"              5 "$(count '^:position_in_.*: before$' .annotaterb.yml)"
expect "and show_indexes with them"       1 "$(count '^:show_indexes: true$' .annotaterb.yml)"

# The whole point of the third piece of work, asserted the way it fails:
# `rake -T` in DEVELOPMENT, which is the environment the retired file guarded
# itself into.  Under RAILS_ENV=test the old file is skipped and the breakage
# is invisible.
say "rake still loads, in development"
RAILS_ENV=development bundle exec rake -T > /tmp/solve-rake.txt
expect "rake -T lists the framework tasks" 1 \
    "$(count '^rake db:migrate ' /tmp/solve-rake.txt)"
expect "and the annotate tasks are gone"   0 \
    "$(count 'annotate' /tmp/solve-rake.txt)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
