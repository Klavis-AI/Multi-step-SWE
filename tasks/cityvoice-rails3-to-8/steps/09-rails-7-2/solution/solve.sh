#!/usr/bin/env bash
# Reference solution -- step 09, "reach Rails 7.2".
#
# The tree this runs against is step 08's finished tree -- Rails 7.1.6 on Ruby
# 3.2.11, `config.load_defaults 7.1`, `config.autoload_lib(ignore: ...)`, ten
# partials carrying strict-locals declarations, and a spec helper that calls
# `check_all_pending!` and `fixture_paths=`.  The first block below says so out
# loud: every count it takes is a fact about *step 08's* output, so running this
# against anything else stops on the first line instead of producing a tree that
# is neither.
#
# One pin moves, and it is the only pin on this ladder that moves alone with
# nothing underneath it: 7.2 keeps railties' `required_ruby_version` at 3.1.0,
# nothing is capped or floored, and on the Gemfile line by itself this tree
# installs, boots all three environments and runs its whole suite green.
# That is what makes this rung an adoption
# rather than a repair, and it is why three of the four remaining pieces of
# work are things the pin does NOT do by itself.
#
# Four pieces of work, in the order this file does them:
#
#   1. The pin.  `rails 7.2.3.2`, resolved conservatively: the framework's own
#      thirteen gems move, `useragent` arrives, `mutex_m` leaves.
#   2. `load_defaults 7.2`, which turns on five settings.  Four are readable on
#      this application and one of the four -- the PostgreSQL date decoder --
#      is a real change to what a query answers with.
#   3. `config.action_dispatch.show_exceptions` in the TEST environment.  7.1
#      deprecated the booleans and 7.2 removed them, and the removal did not
#      restore an error: `false` now falls through to the `else` arm and means
#      `true`.  The line this tree has said since 2014 keeps parsing and starts
#      meaning its own opposite.
#   4. The last `ActiveRecord::Base.connection` in the tree, in unicorn's
#      `before_fork` hook -- a hook that runs in every recorded world, because
#      the Procfile's web line is what the harness boots.
#
# Two artefacts too long to write inline -- the resolved Gemfile and its
# lockfile -- ship as files beside this script.  Every edit states how many
# lines it expects to touch: a `sed` that matched nothing exits 0 and prints
# exactly what a successful one prints.
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
expect "rails 7.1.6 in the Gemfile"       1 "$(count "gem 'rails', '7.1.6'" Gemfile)"
expect "ruby 3.2.11"                      1 "$(count '^3\.2\.11$' .ruby-version)"
expect "load_defaults 7.1"                1 "$(count 'config.load_defaults 7\.1' config/application.rb)"
expect "lib declared the 7.1 way, from 08" 1 "$(count 'config.autoload_lib' config/application.rb)"
expect "strict locals on ten partials"   10 "$(count '^<%# locals:' app/views/*/_*.html.erb)"
expect "the plural fixture path, from 08"  1 "$(count 'config.fixture_paths = ' spec/spec_helper.rb)"
expect "check_all_pending!, from 08"      1 "$(count 'Migration.check_all_pending!' spec/spec_helper.rb)"
expect "show_exceptions still a boolean"  1 "$(count 'config.action_dispatch.show_exceptions = false' config/environments/test.rb)"
expect "one .connection call site left"   1 "$(count 'ActiveRecord::Base\.connection\.' app lib config spec db)"
expect "and it is unicorn's before_fork"  1 "$(count 'ActiveRecord::Base\.connection\.disconnect!' config/unicorn.rb)"

# ---------------------------------------------------------------------------
say "the pin: one line, and nothing underneath it"
# ---------------------------------------------------------------------------
# `bundle lock --conservative --update rails` from the step-08 lockfile with
# only the rails line changed -- not `bundle update`, which floats seventy-odd
# gems.  149 gems either side:
#
#   changed (13)  the framework's own thirteen        7.1.6 -> 7.2.3.2
#   added   (1)   useragent        actionpack 7.2 depends on it for
#                                  `allow_browser`, which this step does not
#                                  turn on.
#   removed (1)   mutex_m          activesupport 7.2 stopped naming the
#                                  backport; Ruby 3.2 has Mutex_m in its own
#                                  standard library.
#
# Nothing else.  No cap, no floor, no held version -- the first rung on this
# ladder where that is true, and the reason the three edits below are the whole
# of the work: they are the changes the new framework does NOT make for you.
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "rails pinned to 7.2"              1 "$(count "gem 'rails', '7.2.3.2'" Gemfile)"
expect "framework locked at 7.2"          1 "$(count '^    rails (7\.2\.3\.2)$' Gemfile.lock)"
expect "useragent locked in"              1 "$(count '^    useragent (' Gemfile.lock)"
expect "mutex_m gone from the lockfile"   0 "$(count '^    mutex_m' Gemfile.lock)"
expect "the interpreter does not move"    1 "$(count '^3\.2\.11$' .ruby-version)"
expect "and the lockfile still says so"   1 "$(count '^   ruby 3\.2\.11p' Gemfile.lock)"

bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "7.2.3.2"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("3.2.")'

# ---------------------------------------------------------------------------
say "framework defaults: 7.1 becomes 7.2"
# ---------------------------------------------------------------------------
# `load_defaults "7.2"` is the 7.1 branch plus five assignments (railties-7.2.3.2
# lib/rails/application/configuration.rb:321-337), three of them guarded by a
# `respond_to?` on the framework that owns them.  Read on a booted process, at
# the place each is really consulted, 7.1 -> 7.2:
#
#   config.yjit                                        false -> true
#   ActiveJob::Base.enqueue_after_transaction_commit   :never -> :default
#   ActiveRecord.validate_migration_timestamps         false -> true
#   an untyped `select_value` of a date column         String -> Date
#   ActiveStorage.web_image_content_types              not in this app at all
#
# Three notes, all measured rather than read off the release guide:
#
#   * `config.yjit = true` is a declaration here and not a runtime state: this
#     image's 3.2.11 is built without YJIT, `defined?(RubyVM::YJIT)` is nil, and
#     nothing reads the setting.  No interpreter on this ladder has YJIT: none
#     of the images installs a Rust compiler, at any rung.  So the step's gate
#     reads the configuration, not
#     `YJIT.enabled?`.
#   * `postgresql_adapter_decode_dates` is the one that is really behaviour.
#     The adapter registers a date decoder, and a query that does not go through
#     an attribute type -- `select_value("select current_date")` -- stops
#     answering with a String.  No `app/` or `lib/` code in this application
#     issues raw SQL, so nothing in the suite or the recorded pages moves; it is
#     still a change to what the database layer returns, and the step gates it
#     on a live connection.
#   * `validate_migration_timestamps` refuses a migration whose version is more
#     than a day in the future.  The newest migration here is 20160119120000 and
#     the harness's clock is fixed at 2016-01-19 12:00:00, so `rake db:migrate`
#     is unaffected -- checked, not assumed.
rewrite config/application.rb 1 \
'    # One declaration of the release whose defaults the application takes.
    # "7.1" is the 7.0 branch plus autoload paths off $LOAD_PATH, filter
    # parameters precompiled at boot, the HTML5 sanitizer and HTML5 dom
    # testing, `default_column_serializer` nil so an unannotated column is
    # never silently YAML, secure tokens generated on initialize rather than
    # on create, SHA256 where the encryption digest was SHA1, transaction
    # callbacks that run in the order they were defined and commit callbacks
    # that see every record, the 7.1 cache entry format, a message serializer
    # that writes JSON and still reads Marshal, an invalid cache expiry that
    # raises rather than passing, `params == hash` no longer comparing equal,
    # and `X-Download-Options` gone from the default response headers.
    config.load_defaults 7.1' \
'    # One declaration of the release whose defaults the application takes.
    # "7.2" is the 7.1 branch plus five settings: YJIT enabled where the
    # interpreter has it, Active Job enqueues deferred to after the
    # transaction commits, migration timestamps validated against the clock,
    # Active Storage'"'"'s web image content types -- which this application does
    # not load the framework for -- and a PostgreSQL adapter that decodes
    # dates, so an untyped query answers with a Date rather than a String.
    config.load_defaults 7.2'
expect "the declaration moved"            1 "$(count 'config.load_defaults 7\.2' config/application.rb)"
expect "and there is only one of it"      1 "$(count 'config.load_defaults' config/application.rb)"

# ---------------------------------------------------------------------------
say "show_exceptions: the line that kept parsing and changed its meaning"
# ---------------------------------------------------------------------------
# `config/environments/test.rb` has said `show_exceptions = false` since this
# repository was written, and it meant "let the exception out of the request so
# the example sees it".  Rails 7.1 deprecated the booleans in favour of
# `:all`, `:rescuable` and `:none`; 7.2 removed them.  The removal is not a
# NoMethodError and not an ArgumentError -- actionpack-7.2.3.2
# lib/action_dispatch/middleware/exception_wrapper.rb:179-192 simply deleted
# the `when true` and `when false` arms of the case, so `false` now falls
# through to `else` and `show?` answers TRUE.  Measured on this tree:
#
#     rails=7.1.6   config=false  show?=false   + DEPRECATION WARNING
#     rails=7.2.3.2 config=false  show?=true    <- the same line, inverted
#     rails=7.2.3.2 config=:none  show?=false
#
# So a migration that leaves this line alone silently turns the test
# environment from "raise" into "render the 500 page", and no test can catch it
# -- nothing in the 504 examples raises through a request, which is also why
# the 7.1 deprecation never fired during step 08.  `:none` is the spelling that
# preserves what the file has always meant.
rewrite config/environments/test.rb 1 \
'  # Raise exceptions instead of rendering exception templates.
  config.action_dispatch.show_exceptions = false' \
'  # Raise exceptions instead of rendering exception templates.  7.2 removed
  # the booleans this line used to take, and `false` now falls through to the
  # `else` arm of ExceptionWrapper#show? -- which answers true.  `:none` is
  # the spelling that still means "do not render, let it out".
  config.action_dispatch.show_exceptions = :none'
expect "show_exceptions declared :none"   1 "$(count 'config.action_dispatch.show_exceptions = :none' config/environments/test.rb)"
expect "and no boolean left anywhere"     0 "$(count 'show_exceptions = false\|show_exceptions = true' config)"

# ---------------------------------------------------------------------------
say "the last ActiveRecord::Base.connection in the tree"
# ---------------------------------------------------------------------------
# activerecord 7.2 deprecates `.connection` in favour of `.lease_connection`
# and `.with_connection` (connection_handling.rb:261-283).  It is soft: the
# warning is only raised when `ActiveRecord.permanent_connection_checkout` is
# set away from its default, which is why nothing in the boot or the suite says
# a word about the one call site this tree has.
#
# That call site is `config/unicorn.rb`'s `before_fork`, and it is not dead
# code: the Procfile's web line is `bundle exec unicorn -p $PORT -c
# ./config/unicorn.rb` and the harness boots the Procfile's own web line, so
# the hook runs in every recorded world.
#
# `connection_pool.disconnect!` is the replacement rather than
# `lease_connection`, because the line is not trying to use a connection -- it
# is trying to make sure the master process holds none before it forks.  The
# old spelling leased one into the master in order to throw it away, and
# disconnected only that one instead of the pool.
#
# `after_fork`'s `ActiveRecord::Base.establish_connection` is not deprecated
# and is left exactly as it is.
rewrite config/unicorn.rb 1 \
'    ActiveRecord::Base.connection.disconnect!' \
'    ActiveRecord::Base.connection_pool.disconnect!'
expect "the pool is disconnected"         1 "$(count 'ActiveRecord::Base\.connection_pool\.disconnect!' config/unicorn.rb)"
expect "no .connection call site left"    0 "$(count 'ActiveRecord::Base\.connection\.' app lib config spec db)"
expect "establish_connection untouched"   1 "$(count 'ActiveRecord::Base\.establish_connection' config/unicorn.rb)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
