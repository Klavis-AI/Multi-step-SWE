#!/usr/bin/env bash
# Reference solution -- step 06, "reach Rails 6.1".
#
# The tree this runs against is step 05's finished tree -- Rails 6.0.6.1 on
# Ruby 2.7.8, `config.load_defaults 6.0`, the zeitwerk autoloader with nothing
# autoloaded during initialization, a host allow-list in production, a logger
# on STDOUT where rails_12factor used to put one, factory_bot, shoulda-matchers
# 4.  The first block below says so out loud: every count it takes is a fact
# about *step 05's* output, so running this against anything else stops on the
# first line instead of producing a tree that is neither.
#
# Two pins move, and unlike step 05's pair neither forces the other: Rails 6.1
# runs on 2.7 and Ruby 3.0 runs 6.0.  It is the ladder that pairs them, because
# 6.1 is the last release that runs on both interpreters and the next two rungs
# each raise the floor.  What that buys is that the four gem floors Ruby 3.0
# imposes -- every one of them a `Proc.new`-shaped or a bytecode-shaped runtime
# failure the resolver is perfectly happy with -- are paid here rather than on
# top of a framework change that is doing real work.
#
# The step's own work is preloading.  6.1 adds `strict_loading`, which makes a
# lazily-loaded association raise instead of quietly issuing a query, and this
# application has three collection pages that issue one query per row.  The two
# halves go together: the `includes` is what makes the page cheap, and the
# marker is what keeps it cheap after the next edit.
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
# argument list or an ERB block mean themselves.
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
expect "rails 6.0.6.1 in the Gemfile"     1 "$(count "gem 'rails', '6.0.6.1'" Gemfile)"
expect "ruby 2.7.8"                       1 "$(count '^2\.7\.8$' .ruby-version)"
expect "load_defaults 6.0"                1 "$(count 'config.load_defaults 6\.0' config/application.rb)"
expect "pg on the 0.x line"               1 "$(count "gem 'pg', '~> 0.21'" Gemfile)"
expect "bootsnap 1.4.9"                   1 "$(count "gem 'bootsnap', '1.4.9'" Gemfile)"
expect "capybara on the 2.x line"         1 "$(count "gem 'capybara', '~> 2.18'" Gemfile)"
expect "a host allow-list"                1 "$(count 'config.hosts = ' config/environments/production.rb)"
expect "the value built in to_prepare"    1 "$(count 'config.to_prepare do' config/initializers/app_content_set.rb)"
expect "OpenStruct used and not required" 1 "$(count 'OpenStruct' app lib config)"
expect "nothing requires ostruct"         0 "$(count 'require .ostruct.' app lib config)"
expect "the landing collection"           1 "$(count 'Answer.includes(:location)' app/controllers/landing_controller.rb)"
expect "the voice collection"             1 "$(count 'Answer.voice_messages' app/controllers/voice_answers_controller.rb)"
expect "the export collection"            1 "$(count 'Call.where.not(location_id: nil).order(:id)' app/controllers/numerical_answers_controller.rb)"
expect "no relation marked yet"           0 "$(count 'strict_loading' app config lib spec)"

# ---------------------------------------------------------------------------
say "the interpreter, and the four gems it breaks"
# ---------------------------------------------------------------------------
# Rails 6.1 does not need Ruby 3.0 and Ruby 3.0 does not need Rails 6.1.  They
# move together because 6.1 is the last release that runs on 2.7 and on 3.0
# both, the next rung's Rails 7.0 will not take anything below 2.7, and the
# rung after that will not take anything below 3.1 -- so an interpreter that
# does not move here has to move later, under a framework that also has an
# opinion about it.
#
# Three files have to name the same interpreter, for the reason step 05 gave:
# rbenv reads .ruby-version, Bundler reads the `ruby` directive, and `bundle
# install` refuses a lockfile whose RUBY VERSION the running interpreter does
# not satisfy.  Two out of three agreeing does not stop anything; it just runs
# the application on an interpreter no one chose.
#
# The four floors are in files/Gemfile with their reasons.  What they have in
# common is worth saying once: the resolver accepts every one of the old pins
# on Ruby 3.0.  bootsnap 1.4 fails when `RubyVM::InstructionSequence#to_binary`
# meets thor's `def attr_reader(*)`, pg 0.21 fails when Active Record 6.1's
# adapter asks for `~> 1.1`, jbuilder 2.9 and capybara 2.18 fail on the
# `Proc.new`-with-no-block that 3.0 removed.  None of that is visible to
# `bundle lock`; all of it is visible the first time the tree is run.
printf '3.0.7\n' > .ruby-version
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "the interpreter the tree selects" 1 "$(count '^3\.0\.7$' .ruby-version)"
expect "the same one in the Gemfile"      1 "$(count "^ruby '3.0.7'" Gemfile)"
expect "and in the lockfile"              1 "$(count '^   ruby 3\.0\.7p220$' Gemfile.lock)"
expect "rails pinned to 6.1"              1 "$(count "gem 'rails', '6.1.7.10'" Gemfile)"
expect "framework locked at 6.1"          1 "$(count '^    rails (6\.1\.7\.10)$' Gemfile.lock)"
expect "pg above Active Record's floor"   1 "$(count "gem 'pg', '1.4.6'" Gemfile)"
expect "bootsnap that survives 3.0"       1 "$(count "gem 'bootsnap', '1.9.3'" Gemfile)"
expect "jbuilder without Proc.new"        1 "$(count "gem 'jbuilder', '~> 2.10'" Gemfile)"
expect "capybara without Proc.new"        1 "$(count "gem 'capybara', '~> 3.0'" Gemfile)"
bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "6.1.7.10"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("3.0.")'

# ---------------------------------------------------------------------------
say "framework defaults: 6.0 becomes 6.1"
# ---------------------------------------------------------------------------
# `load_defaults "6.1"` is the 6.0 branch plus sixteen settings of its own
# (railties-6.1.7.10 lib/rails/application/configuration.rb, the `when "6.1"`
# branch).  Seven of them mean something to this application: `has_many`
# associations hand the parent back to each child instead of loading it again,
# Active Record uses the new connection handling rather than the legacy one,
# `form_with` stops generating remote forms, the asset helpers add a `Link:
# rel=preload` header, the masked CSRF token becomes url-safe base64, cookies
# are written with `SameSite=Lax`, and `Time#to_time` keeps its own offset
# instead of converting to the system zone.  The rest set up Active Storage,
# Action Mailbox and Active Job, which this application does not use.
#
# `form_with_generates_remote_forms = false` is the one that shows: the forms
# this application renders lose their `data-remote="true"` attribute.  That is
# the change, not a break -- the forms were never handled by a remote handler.
rewrite config/application.rb 1 \
'    # One declaration of the release whose defaults the application takes.
    # "6.0" is the 5.2 branch plus the zeitwerk autoloader, cookies that
    # carry their own purpose and expiry, collection cache versioning, Action
    # Mailer'"'"'s delivery job, and form builders that stop emitting the hidden
    # `utf8` field.
    config.load_defaults 6.0' \
'    # One declaration of the release whose defaults the application takes.
    # "6.1" is the 6.0 branch plus `has_many` associations that hand the parent
    # back to each child, the new Active Record connection handling in place of
    # the legacy one, `form_with` generating ordinary rather than remote forms,
    # a `Link: rel=preload` header on the assets a page needs, url-safe CSRF
    # tokens, cookies written with `SameSite=Lax`, and `Time#to_time` keeping
    # its own offset instead of converting to the system zone.
    config.load_defaults 6.1'
expect "load_defaults declared"   1 "$(count 'config.load_defaults 6\.1' config/application.rb)"
expect "the 6.0 declaration gone" 0 "$(count 'config.load_defaults 6\.0' config/application.rb)"

# ---------------------------------------------------------------------------
say "ostruct: a default gem this application never required"
# ---------------------------------------------------------------------------
# AppContentSet has built an OpenStruct since the Rails 3 original and has
# never required `ostruct`.  It worked because something else in the bundle
# required it first -- jbuilder 2.9 did, at lib/jbuilder.rb:6 -- and jbuilder
# 2.10 does not.  So a pin that moved for an unrelated reason takes a constant
# out from under a file three directories away, and 38 examples fail with
# `NameError: uninitialized constant AppContentSet::OpenStruct`.
#
# The fix is the require the file should always have carried.  A library a file
# uses is a library that file names: leaving it to the bundle is how one gem's
# version becomes another file's runtime error.
rewrite app/models/app_content_set.rb 1 \
'class AppContentSet < Struct.new(:content_hash)' \
'require "ostruct"

class AppContentSet < Struct.new(:content_hash)'
expect "ostruct required where it is used" 1 "$(count 'require "ostruct"' app/models/app_content_set.rb)"

# ---------------------------------------------------------------------------
say "capybara 3 normalises the text it searches"
# ---------------------------------------------------------------------------
# capybara 2.18's matcher proxy calls `Proc.new` with no block, which Ruby 3.0
# removed, so every `within '...' do` in a feature spec raises -- the pin has to
# cross a major version.  Capybara 3 collapses whitespace in `page.text`, and
# the export feature spec compares a CSV body against `have_content`, so the
# newlines between its rows stop matching.
#
# One line, and the fix is to assert against the bytes the endpoint served
# rather than against the browser's idea of their text.  That is the stricter
# of the two assertions, not the weaker one: `include` on the raw body still
# fails if a row moves, a separator changes or a newline goes missing, all of
# which `have_content` on normalised text would now let through.
rewrite spec/features/export_spec.rb 1 \
'    expect(page).to have_content(export_data)' \
'    expect(page.body).to include(export_data)'
expect "the export body asserted" 1 "$(count 'expect(page.body).to include(export_data)' spec)"

# ---------------------------------------------------------------------------
say "preloading, and the marker that keeps it"
# ---------------------------------------------------------------------------
# This application has three pages that walk a collection and read associations
# off every row, and two of them pay a query per row for it.  Measured on a
# development database with twelve voice answers: /voice_answers costs 37
# queries and /export.csv costs 19.  With the associations named, 4 and 5.
#
# `strict_loading` is the 6.1 half.  Marking a relation makes any association
# it did not load raise ActiveRecord::StrictLoadingViolationError instead of
# issuing the query, which turns the next `includes` somebody deletes from a
# page that quietly gets slower into a page that fails.  It goes on the
# relation rather than on the application: `config.active_record.
# strict_loading_by_default` exists and is a different, much larger decision --
# it makes every lazy load in the suite raise too, which is 51 of this suite's
# 504 examples, none of them about preloading.
#
# The landing page already names its association; it gets the marker so that
# stays true.
rewrite app/controllers/landing_controller.rb 1 \
'    @most_recent_messages = Answer.includes(:location)' \
'    @most_recent_messages = Answer.strict_loading
                                  .includes(:location)'
expect "the landing relation marked" 1 "$(count 'Answer.strict_loading' app/controllers/landing_controller.rb)"

# /voice_answers renders one row per voice message and each row reads the
# location, the call and the caller -- `obscured_phone_number` goes through
# `has_one :caller, through: :call`, so the caller has to be named too or the
# marked relation raises on it.
rewrite app/controllers/voice_answers_controller.rb 1 \
'    @messages = Answer.voice_messages' \
'    @messages = Answer.voice_messages
                      .includes(:location, :call, :caller)
                      .strict_loading'
expect "the voice relation preloaded" 1 "$(count 'includes(:location, :call, :caller)' app/controllers/voice_answers_controller.rb)"

# The CSV export walks every call that has a location and reads the location's
# name and the call's answers for each one.
rewrite app/controllers/numerical_answers_controller.rb 1 \
'    calls = Call.where.not(location_id: nil).order(:id)' \
'    calls = Call.where.not(location_id: nil)
                .includes(:location, :answers)
                .strict_loading
                .order(:id)'
expect "the export relation preloaded" 1 "$(count 'includes(:location, :answers)' app/controllers/numerical_answers_controller.rb)"
expect "relations marked in all"       3 "$(count 'strict_loading' app)"
expect "no application-wide switch"    0 "$(count 'strict_loading_by_default' config)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
