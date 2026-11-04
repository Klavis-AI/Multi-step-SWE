#!/usr/bin/env bash
# Reference solution -- step 13, "the hardening pass".
#
# The tree this runs against is step 12's finished tree: Rails 8.1.3.1 on Ruby
# 3.4.10, 153 gems resolved, every rung of the ladder below it already walked.
# The first block says so out loud, in counts, so that running this against
# anything else stops on the first line instead of producing a tree that is
# neither.
#
# Nothing here moves a framework version, and that is the point of ending
# here rather than one rung earlier.  A migration that stops at "it boots on
# the new version" has left the two things a migration is usually *for*
# undone:
#
#   * the dependency graph is now resolvable against current gems, and until
#     somebody actually resolves it the tree is still holding the versions
#     the old framework pinned it to -- including seven gems with published
#     advisories against them, four of which the Gemfile is pinning by hand;
#   * the application has spent twelve rungs being driven by a behaviour
#     suite that posts real-shaped Twilio webhooks at it, and every one of
#     those requests has been writing the caller's telephone number into the
#     production log in clear text, because Rails logs the parameter hash at
#     :info and this application has filtered exactly one key since 2013.
#
# Both are facts about the tree that arrives here, both are measured rather
# than asserted, and neither is a version bump.
#
# ---- the seven gems -------------------------------------------------------
#
# Measured against rubysec/ruby-advisory-db at commit 44784c29 -- the snapshot
# in /opt/ruby-advisory-db, which is what `bundle-audit` in this image reads
# and what the scorer reads too -- step 12's tree carries 31 advisories over
# 7 gems:
#
#   concurrent-ruby 1.3.4    3    declared in the Gemfile
#   msgpack 1.4.2            1    declared
#   redcarpet 3.1.1          2    declared
#   uglifier 2.5.0           1    declared
#   jwt 2.3.0                1    transitive, through twilio-ruby
#   nokogiri 1.15.7         17    transitive
#   rexml 3.2.5              6    transitive
#
# The four declared ones are pinned with `=` in the Gemfile, so no resolver
# will ever move them: they are moved by editing the Gemfile.  The three
# transitive ones are not pinned anywhere -- they are held where they are by
# the lockfile alone, which is the whole reason a lockfile exists -- so they
# move when the resolver is asked to move them and not before.
#
# `bundle lock --conservative --update <the seven>` is what produces the
# lockfile shipped beside this script.  Conservative because the question
# here is "are these seven gems current enough to be clean", not "what does
# this application resolve to today": a bare `bundle update` would also walk
# the other 146 specs forward, which is a different change with a different
# risk, and one no gate here asks for.  The result moves exactly the seven,
# adds one spec edge (`jwt 3.3.0` depends on `base64`, which the lockfile
# already carries as a bundled gem at 0.3.0) and drops one (`uglifier 4.2.1`
# no longer depends on `json`).  153 specs before, 153 after.
#
# ---- the log --------------------------------------------------------------
#
# `config.filter_parameters` is a list of key fragments, matched case
# insensitively as substrings and descended into nested hashes.  This
# application's list has had one entry, `:password`, since it was generated --
# and it has no password field, so the list has never filtered anything.
#
# Meanwhile, in the ten production logs the behaviour suite produced from step
# 12's tree: 132 lines carrying an E.164 telephone number, 12 distinct numbers,
# and 10 lines carrying an e-mail address.  Every one of them inside a
# `Parameters:` line, which is the good case -- it means the leak is reachable
# by the mechanism Rails provides, rather than by an exception message or a
# hand-rolled `Rails.logger.info` that would have to be rewritten instead.
#
# Three entries close it: `From` and `To`, which is what Twilio calls the
# caller and the line they dialled, and `email`, which the subscription form
# posts nested under `subscription`.
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

# files <find args> -- how many paths match.
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
expect "rails 8.1.3.1, from 11"           1 "$(count "gem 'rails', '8\.1\.3\.1'" Gemfile)"
expect "ruby 3.4.10, from 12"             1 "$(count '^3\.4\.10$' .ruby-version)"
expect "the same in the Gemfile"          1 "$(count "^ruby '3\.4\.10'" Gemfile)"
expect "and in the lockfile"              1 "$(count '^   ruby 3\.4\.10p' Gemfile.lock)"
expect "bigdecimal current, from 12"      1 "$(count '^    bigdecimal (3\.' Gemfile.lock)"
expect "153 gems in the lock"           153 "$(count '^    [a-z]' Gemfile.lock)"
expect "the four pins, as 12 left them"   4 "$(count "^gem '\(concurrent-ruby\|msgpack\|redcarpet\|uglifier\)', '\(1\.3\.4\|1\.4\.2\|3\.1\.1\|2\.5\.0\)'" Gemfile)"
expect "one filtered parameter, since 2013" 1 "$(count 'filter_parameters += \[:password\]' config/initializers/filter_parameter_logging.rb)"

# ---------------------------------------------------------------------------
say "what the advisory database says about it, before anything moves"
# ---------------------------------------------------------------------------
# `bundle-audit` in this image is pinned to the snapshot in /opt and told not
# to update, because a hardening step whose expected answer moves when
# somebody merges a pull request upstream is not a hardening step.  Its exit
# status is 1 when it finds something, so the
# `|| true` is load-bearing under `set -e`.
bundle-audit check --no-update > /tmp/audit-before.txt 2>&1 || true
expect "advisories against this tree"    31 "$(count '^Name:' /tmp/audit-before.txt)"
expect "gems carrying them"               7 "$(grep '^Name:' /tmp/audit-before.txt | sort -u | wc -l | tr -d ' ')"

# ---------------------------------------------------------------------------
say "the four declared pins, and the lockfile that follows from them"
# ---------------------------------------------------------------------------
# The Gemfile shipped beside this script differs from the arriving one in
# exactly four lines, and the lockfile is `bundle lock --conservative --update
# concurrent-ruby msgpack redcarpet uglifier nokogiri rexml jwt` run under
# 3.4.10 with Bundler 2.6.9 against it -- not a hand edit.  Installing both is
# the same move step 12 made: the resolution is reproduced here rather than
# re-run, so that the golden tree is the same tree every time it is built.
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock

expect "concurrent-ruby moved"            1 "$(count "^gem 'concurrent-ruby', '1\.3\.7'" Gemfile)"
expect "msgpack moved"                    1 "$(count "^gem 'msgpack', '1\.8\.2'" Gemfile)"
expect "redcarpet moved"                  1 "$(count "^gem 'redcarpet', '3\.6\.1'" Gemfile)"
expect "uglifier moved"                   1 "$(count "^gem 'uglifier', '4\.2\.1'" Gemfile)"
expect "jwt followed"                     1 "$(count '^    jwt (3\.' Gemfile.lock)"
expect "nokogiri followed"                1 "$(count '^    nokogiri (1\.19\.' Gemfile.lock)"
expect "rexml followed"                   1 "$(count '^    rexml (3\.4\.' Gemfile.lock)"
expect "still 153 gems"                 153 "$(count '^    [a-z]' Gemfile.lock)"
expect "still on 3.4.10"                  1 "$(count '^   ruby 3\.4\.10p' Gemfile.lock)"
expect "still bundler 2.6.9"              1 "$(count '^   2\.6\.9$' Gemfile.lock)"

bundle install

# ---------------------------------------------------------------------------
say "the same question, asked again"
# ---------------------------------------------------------------------------
bundle-audit check --no-update > /tmp/audit-after.txt 2>&1 || true
expect "advisories against this tree"     0 "$(count '^Name:' /tmp/audit-after.txt)"
tail -1 /tmp/audit-after.txt

# ---------------------------------------------------------------------------
say "the parameters the log may keep"
# ---------------------------------------------------------------------------
install -m 0644 "$FILES/filter_parameter_logging.rb" \
        config/initializers/filter_parameter_logging.rb
expect "password stays"                   1 "$(count '^Rails\.application\.config\.filter_parameters += \[:password,' config/initializers/filter_parameter_logging.rb)"
expect "From, To and email join it"       1 "$(count 'filter_parameters += \[:password, :From, :To, :email\]$' config/initializers/filter_parameter_logging.rb)"

# The filter itself, exercised rather than described: `ParameterFilter` is what
# `ActionController`'s log subscriber runs the parameter hash through, and the
# three entries have to cover the nested `subscription[email]` as well as the
# two flat Twilio keys.  Asking the class directly keeps this a fact about the
# configuration and not about whether a request happened to be logged.
bundle exec ruby -e '
  require "./config/environment"
  filter = ActiveSupport::ParameterFilter.new(Rails.application.config.filter_parameters)
  got = filter.filter(
    "From" => "+15105550101", "To" => "+15551230000",
    "CallSid" => "CA0001", "subscription" => { "email" => "resident@example.com" })
  want = { "From" => "[FILTERED]", "To" => "[FILTERED]",
           "CallSid" => "CA0001", "subscription" => { "email" => "[FILTERED]" } }
  abort("filter_parameters did not cover the webhook: #{got.inspect}") unless got == want
  puts "   ok  the webhook hash, filtered               #{got.inspect}"
' 2>&1 | grep -v '^Don.t run Bundler as root'

# ---------------------------------------------------------------------------
say "the tree still parses, and still boots"
# ---------------------------------------------------------------------------
# The same closing check every rung makes.  `filter_parameter_logging.rb` is an
# initializer, so a syntax error in it is a boot failure rather than a warning,
# and a boot failure discovered by the scorer is a rung's worth of gates lost.
bundle exec ruby -c config/initializers/filter_parameter_logging.rb

# `config/database.yml` is not in this tree and is not supposed to be: the
# configuration is copied into place from `config/database.yml.example`, and a
# tree that hands on the real file fails an inherited condition.  The boot
# below needs one, so the example is copied in for the length of that one
# reading and removed again whatever the reading says -- `rm` unconditional and
# before the assertion, because `set -e` would otherwise leave the file in the
# tree on a failing boot and quietly cost the whole gates family.
cp config/database.yml.example config/database.yml
set +e
bundle exec ruby -e 'require "./config/environment"; puts "BOOT_OK #{Rails::VERSION::STRING} on #{RUBY_VERSION}"' \
  > /tmp/solve-13-boot.txt 2> /tmp/solve-13-boot.err
boot_rc=$?
set -e
rm -f config/database.yml
grep -v '^Don.t run Bundler as root' /tmp/solve-13-boot.err >&2 || true
expect "the boot exits clean"             0 "$boot_rc"
expect "and database.yml is gone again"   0 "$(files config/database.yml -maxdepth 0)"
expect "booted on 8.1.3.1 and 3.4.10"     1 "$(count '^BOOT_OK 8\.1\.3\.1 on 3\.4\.10$' /tmp/solve-13-boot.txt)"
