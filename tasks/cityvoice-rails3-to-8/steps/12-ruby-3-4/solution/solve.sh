#!/usr/bin/env bash
# Reference solution -- step 12, "the last interpreter move".
#
# The tree this runs against is step 11's finished tree -- Rails 8.1.3.1 on
# Ruby 3.2.11, `config.load_defaults 8.1`, high_voltage 5.0.0 drawing
# `/pages/*id`, annotaterb in place of the capped `annotate`.  The first block
# below says so out loud: every count it takes is a fact about *step 11's*
# output, so running this against anything else stops on the first line
# instead of producing a tree that is neither.
#
# No framework pin moves here.  `rails` stays at 8.1.3.1 and so does every
# other line of the Gemfile that names a third-party gem; what moves is the
# interpreter underneath them, 3.2.11 -> 3.4.10, for the last time on this
# ladder.  Nothing in the dependency graph asked for it -- railties 8.1.3.1
# wants Ruby >= 3.2.0 and the tree has cleared that since step 08 -- so the
# reason is the one a migration ends on: 3.2 is two minor versions behind the
# release the framework is developed against, and a tree that has just reached
# the current framework should not be standing on an interpreter that is not.
#
# Nothing about the arriving tree is broken.  rbenv reads `.ruby-version` out
# of the working tree, every interpreter this ladder names was compiled into
# the image once, and so the tree that arrives runs on the 3.2.11 it declares.
# The pressure comes from the other end: the image the tests run in carries a
# single interpreter, so a tree that still says 3.2.11 when it is handed over
# finds no interpreter there at all.  That is what makes `.ruby-version` an
# artefact on this rung rather than a preference, and the second block reads
# both interpreters rather than describing them.
#
# What the move costs is not a resolution.  It is the end of the default-gem
# era: Ruby 3.4 finished moving libraries out of the set the interpreter hands
# out for free and into the set a Gemfile has to name, and a program started
# under `bundler/setup` may require only what the Gemfile names.  Three of the
# libraries that moved are ones this bundle loads, and they fail in three
# different places:
#
#   csv       the application's own code.  `app/models/answer_counts.rb:1` and
#             each of the three importers under lib/ open with `require 'csv'`.
#             `AnswerCounts` is a model the recorded behaviour suite exercises
#             and any eager load reaches, so this one is fatal wherever the
#             application is actually used -- production boots eager-loaded,
#             and development and test reach it the moment the constant is
#             named.
#   mutex_m   minitest-5.15.0/lib/minitest.rb:3.  No file in this tree names
#             it.  rspec-rails loads minitest, so it is reached only when the
#             suite starts.
#   observer  factory_bot-5.2.0/lib/factory_bot/evaluation.rb:1.  Same shape:
#             invisible to anything that reads the application and fatal to
#             anything that runs its tests.
#
# and two more pins that are not LoadErrors at all.  `bigdecimal` has been locked
# at 1.4.4 since the 4.2 baseline because no Gemfile ever named it and
# `bundle lock --conservative` never had cause to move it; that 2019 C
# extension defines an allocator for a T_DATA class, which every interpreter
# since 3.2 has warned about on every boot.  3.1.8 is what 3.4.10 ships.
#
# `ostruct` is the fifth and the quietest: it still loads, because 4.0.0 is the
# release it leaves in rather than this one, and all it does here is print on
# the first `require` that it will not be there forever.  It is a plain
# `warning:` and not a `DEPRECATION WARNING:`, so nothing that collects
# deprecations sees it; `app/models/app_content_set.rb:1` requires it and
# `config/initializers/app_content_set.rb:10` names the constant inside a
# `to_prepare` block, so every boot in every environment prints one.
#
# All five are pinned to the versions this interpreter itself ships, so each
# declaration names the code that was already going to be loaded rather than
# choosing different code.  Four specs arrive in the lockfile -- 149 gems
# become 153 -- and `bigdecimal` moves in place.
#
# Two artefacts too long to write inline -- the Gemfile and its lockfile --
# ship as files beside this script.  Every edit states how many lines it
# expects to touch: a `sed` that matched nothing exits 0 and prints exactly
# what a successful one prints.
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


# ---------------------------------------------------------------------------
say "the tree this expects to find"
# ---------------------------------------------------------------------------
expect "rails 8.1.3.1 in the Gemfile"     1 "$(count "gem 'rails', '8\.1\.3\.1'" Gemfile)"
expect "load_defaults 8.1, from 11"       1 "$(count 'config.load_defaults 8\.1' config/application.rb)"
expect "high_voltage 5.0.0, from 11"      1 "$(count "gem 'high_voltage', '5\.0\.0'" Gemfile)"
expect "the interpreter it declares"      1 "$(count '^3\.2\.11$' .ruby-version)"
expect "the same one in the Gemfile"      1 "$(count "^ruby '3\.2\.11'" Gemfile)"
expect "and in the lockfile"              1 "$(count '^   ruby 3\.2\.11p' Gemfile.lock)"
expect "csv is named nowhere"             0 "$(count "^gem 'csv'" Gemfile)"
expect "nor mutex_m"                      0 "$(count "^gem 'mutex_m'" Gemfile)"
expect "nor observer"                     0 "$(count "^gem 'observer'" Gemfile)"
expect "nor bigdecimal"                   0 "$(count "^gem 'bigdecimal'" Gemfile)"
expect "nor ostruct"                      0 "$(count "^gem 'ostruct'" Gemfile)"
expect "but bigdecimal is locked, at 1.4" 1 "$(count '^    bigdecimal (1\.4\.4)$' Gemfile.lock)"
expect "149 gems in the lock"           149 "$(count '^    [a-z]' Gemfile.lock)"

# ---------------------------------------------------------------------------
say "the interpreter this tree selects, and the one waiting beside it"
# ---------------------------------------------------------------------------
# `ruby` here is an rbenv shim and the tree's own `.ruby-version` is what it
# reads, so the reading below is a fact about the tree and not about the box:
# step 11's output runs on step 11's interpreter.  3.4.10 needs no building --
# step 08 compiled every version this ladder names into the image in one pass,
# which is why no rung after it spends its time on a build.
#
# So there is no refusal to assert and no rescue to perform.  What the change
# below buys is read at the far end: the image the tests run in installs one
# interpreter, the one this rung declares, and a tree still naming 3.2.11 finds
# nothing there to run it.
expect "arriving on step 11's interpreter" 1 "$(ruby -e 'print RUBY_VERSION.start_with?("3.2.") ? 1 : 0')"
expect "and 3.4.10 already built beside it" 1 "$(files /opt/rubies/3.4.10/bin/ruby -maxdepth 0)"

# ---------------------------------------------------------------------------
say "the declaration, in the three files that have to agree"
# ---------------------------------------------------------------------------
# The reason step 05 gave, and every interpreter rung since has repeated:
# rbenv reads `.ruby-version`, Bundler reads the `ruby` directive, and
# `bundle install` refuses a lockfile whose RUBY VERSION the running
# interpreter does not satisfy.  Whichever two of the three disagree,
# something downstream picks the interpreter the third named.
#
# The lockfile shipped beside this script is `bundle lock --conservative` run
# under 3.4.10 against the Gemfile shipped with it -- not a hand edit.  Its
# whole delta against step 11's is the four specs named below, the five
# DEPENDENCIES lines that go with them -- `bigdecimal` gains one as well as
# moving -- and the RUBY VERSION trailer, which bundler rewrites from the
# interpreter that ran it.
printf '3.4.10\n' > .ruby-version
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "the interpreter the tree selects" 1 "$(count '^3\.4\.10$' .ruby-version)"
expect "the same one in the Gemfile"      1 "$(count "^ruby '3\.4\.10'" Gemfile)"
expect "and in the lockfile"              1 "$(count '^   ruby 3\.4\.10p' Gemfile.lock)"
# The Gemfile's own comments name the version this rung leaves behind, and a
# comment is not a declaration.  What has to be gone is 3.2.11 as a *value*, so
# the pattern refuses a `#` anywhere to its left rather than reading the file
# whole, which would count those two comments.
expect "no 3.2.11 declared anywhere"     0 "$(count '^[^#]*3\.2\.11' .ruby-version Gemfile Gemfile.lock)"
expect "csv declared"                     1 "$(count "^gem 'csv', '3\.3\.2'" Gemfile)"
expect "mutex_m declared"                 1 "$(count "^gem 'mutex_m', '0\.3\.0'" Gemfile)"
expect "observer declared"                1 "$(count "^gem 'observer', '0\.1\.2'" Gemfile)"
expect "ostruct declared"                 1 "$(count "^gem 'ostruct', '0\.6\.1'" Gemfile)"
expect "bigdecimal declared"              1 "$(count "^gem 'bigdecimal', '3\.1\.8'" Gemfile)"
expect "and locked off the 2019 release"  1 "$(count '^    bigdecimal (3\.1\.8)$' Gemfile.lock)"
expect "153 gems in the lock"           153 "$(count '^    [a-z]' Gemfile.lock)"
expect "the framework has not moved"      1 "$(count '^    rails (8\.1\.3\.1)$' Gemfile.lock)"

bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "8.1.3.1"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("3.4.")'

# ---------------------------------------------------------------------------
say "what the declarations buy, read under bundler/setup"
# ---------------------------------------------------------------------------
# The point of the four lines is that these requires resolve at all, and the
# only place that can be read honestly is inside a process bundler has set up:
# a bare `ruby -e 'require "csv"'` succeeds on this interpreter whether or not
# the Gemfile names it, because the file is still on the box.  What `bundler/
# setup` does is restrict the load path to the bundle, and that is what turns
# an unnamed bundled gem into a LoadError.
#
# Each of the five is asked for by name and answered with the version the
# interpreter ships, so this also checks the pins are the shipped ones rather
# than a newer release that would have had to be downloaded.
bundle exec ruby -e '
  %w[csv mutex_m observer bigdecimal ostruct].each do |name|
    require name
    puts "LOADED #{name}=#{Gem.loaded_specs.fetch(name).version}"
  end
' > /tmp/solve-12-loaded.txt 2> /tmp/solve-12-loaded.err
cat /tmp/solve-12-loaded.txt
expect "csv loads, at the shipped version" 1 "$(count '^LOADED csv=3\.3\.2$' /tmp/solve-12-loaded.txt)"
expect "mutex_m loads"                     1 "$(count '^LOADED mutex_m=0\.3\.0$' /tmp/solve-12-loaded.txt)"
expect "observer loads"                    1 "$(count '^LOADED observer=0\.1\.2$' /tmp/solve-12-loaded.txt)"
expect "bigdecimal loads, off 1.4.4"       1 "$(count '^LOADED bigdecimal=3\.1\.8$' /tmp/solve-12-loaded.txt)"
expect "ostruct loads"                     1 "$(count '^LOADED ostruct=0\.6\.1$' /tmp/solve-12-loaded.txt)"
# Declared, so the copy that loads is the one in the bundle and the notice
# about leaving the default set is not printed at all.
expect "and none of them says it is leaving" 0 "$(count 'no longer be part of the default gems' /tmp/solve-12-loaded.err)"

# ---------------------------------------------------------------------------
say "the boot, eager-loaded, and what it no longer warns about"
# ---------------------------------------------------------------------------
# `csv` is the one of the three that the application itself requires, so the
# reading that matters is a boot that actually loads every model:
# `AnswerCounts` is what opens with `require 'csv'`, and only `eager_load!`
# guarantees the file is read.  The three importers under lib/ are autoloaded
# the same way.
#
# The same process is where two warnings used to be printed and neither is any
# more: the allocator notice from bigdecimal 1.4.4, and the default-gem notice
# from ostruct.  A third `T_DATA` warning survives this step and is not its
# business: redcarpet 3.1.1 is a third-party C extension no rung on this ladder
# repins, and it warned identically at 3.2.11 -- it is handed to this step, not
# created by it.  It is also not printed by a boot, only by the suite, which is
# why the assertion below can ask this process for silence on the two and say
# nothing about the third.
#
# `config/database.yml` is not in this tree and is not supposed to be -- the
# configuration is copied into place from `config/database.yml.example`, and a
# tree that ships the real file fails an inherited condition.  But an eager
# load reaches `ApplicationRecord`, which reaches `ActiveRecord::Base`, which
# runs the railtie hook that reads the configuration, so the boot below cannot
# happen without one.  So the example is copied in for the length of this one
# reading and removed again whatever the reading says; `rm` is unconditional
# rather than after the assertions, because `set -e` would otherwise leave the
# file in the tree on the first failing `expect`.  No connection is opened:
# eager loading defines the classes, it does not query.
cp config/database.yml.example config/database.yml
set +e
RAILS_ENV=test bundle exec ruby -e '
  require File.expand_path("config/environment", Dir.pwd)
  Rails.application.eager_load!
  puts "EAGER_OK models=#{ActiveRecord::Base.descendants.size}"
  puts "CSV=#{defined?(CSV) ? CSV::VERSION : "missing"}"
  puts "BIGDECIMAL=#{BigDecimal("1.5") + 1}"
' > /tmp/solve-12-boot.txt 2> /tmp/solve-12-boot.err
boot_rc=$?
set -e
rm -f config/database.yml
cat /tmp/solve-12-boot.txt
expect "the eager-loaded boot exits clean" 0 "$boot_rc"
expect "and database.yml is gone again"   0 "$(files config/database.yml -maxdepth 0)"
expect "the tree boots eager-loaded"      1 "$(count '^EAGER_OK models=[0-9]' /tmp/solve-12-boot.txt)"
expect "csv is reachable from the app"    1 "$(count '^CSV=3\.3\.2$' /tmp/solve-12-boot.txt)"
expect "bigdecimal still does its sums"   1 "$(count '^BIGDECIMAL=2\.5$' /tmp/solve-12-boot.txt)"
expect "nothing is deprecated booting"    0 "$(count 'DEPRECATION' /tmp/solve-12-boot.err)"
expect "and BigDecimal stops warning"     0 "$(count 'T_DATA class BigDecimal' /tmp/solve-12-boot.err)"
expect "and ostruct stops warning"        0 "$(count 'no longer be part of the default gems' /tmp/solve-12-boot.err)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.  This is also the first rung where
# the compiler doing it is Prism rather than parse.y, which is the other half
# of what 3.4 changed: a file that only the old parser accepted would fail
# here rather than in the suite.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
