#!/usr/bin/env bash
# Reference solution -- step 11, "reach Rails 8.1".
#
# The tree this runs against is step 10's finished tree -- Rails 8.0.5.1 on Ruby
# 3.2.11, `config.load_defaults 8.0`, annotaterb in place of the capped
# `annotate`, and `high_voltage 3.0.0` still drawing the static-page route.  The
# first block below says so out loud: every count it takes is a fact about
# *step 10's* output, so running this against anything else stops on the first
# line instead of producing a tree that is neither.
#
# The interpreter does not move.  railties 8.1.3.1 still asks for
# `required_ruby_version >= 3.2.0` and this tree has been on 3.2.11 since step
# 08, so for the third rung in a row `.ruby-version` is left exactly as it is --
# and that is a fact worth asserting rather than assuming, which is what the
# last check in the first block does.  Ruby moves at step 12.
#
# Three pieces of work, in the order this file does them:
#
#   1. The pin.  `rails 8.1.3.1`, resolved conservatively: the framework's own
#      thirteen gems, plus `action_text-trix` arriving and `benchmark` leaving,
#      149 gems either side.  On that line alone the tree installs, boots all
#      three environments and runs its 504 examples green.
#   2. `load_defaults 8.1`, which is six settings -- and a seventh, `yjit`,
#      which is about the interpreter rather than the framework.  None of the
#      six changes a byte this application emits; the declaration is the work.
#   3. The route the release stops accepting.  actionpack 8.1 gives the routing
#      verbs keyword signatures and deprecates a positionally-passed Hash;
#      `high_voltage 3.0.0` draws `/pages/*id` with `get
#      HighVoltage.route_drawer.match_attributes`, a Hash in a variable, which
#      Ruby 3 does not convert.  The warning is raised while the suite draws its
#      routes and is blamed to a file under spec/, so it is not cosmetic here:
#      it fails the `suite_deprecations` guard this ladder has held since step
#      02.  `high_voltage 5.0.0` splats the attributes and that is the whole
#      fix -- one line, no runtime dependencies, and the route keeps its name,
#      its path and its endpoint.
#
# The trap in the third piece is that the deprecation is not the damage.
# `assign_deprecated_option` warns only when the value it deleted was truthy,
# so the drawer's `:format => false` is dropped in SILENCE and the optional
# `(.:format)` segment comes back -- `/pages/about.html` stops being the page
# `about.html` and becomes the page `about` in a format this application has no
# template for.  No warning says so and no example fails.  The last block below
# is what asserts the route came through the release unchanged.
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
expect "rails 8.0.5.1 in the Gemfile"     1 "$(count "gem 'rails', '8.0.5.1'" Gemfile)"
expect "ruby 3.2.11"                      1 "$(count '^3\.2\.11$' .ruby-version)"
expect "load_defaults 8.0"                1 "$(count 'config.load_defaults 8\.0' config/application.rb)"
expect "annotaterb, from 10"              1 "$(count "gem 'annotaterb'" Gemfile)"
expect "and no annotate with it"          0 "$(count "gem 'annotate'" Gemfile)"
expect "high_voltage on its 2015 pin"     1 "$(count "gem 'high_voltage', '3\.0\.0'" Gemfile)"
expect "the app draws no page route"      0 "$(count 'pages' config/routes.rb)"
expect "and no high_voltage initializer"  0 "$(files config/initializers -name 'high_voltage.rb')"

# ---------------------------------------------------------------------------
say "the pin, and the gem whose route the release stops accepting"
# ---------------------------------------------------------------------------
# `bundle lock --conservative --update rails` from the step-10 lockfile with
# only the rails line changed, then the high_voltage line changed and the lock
# updated conservatively again.  149 gems either side:
#
#   changed (14)  the framework's own thirteen        8.0.5.1 -> 8.1.3.1
#                 high_voltage                          3.0.0 -> 5.0.0
#   added   (1)   action_text-trix 2.1.19 -- 8.1 extracts the Trix editor from
#                 actiontext into a gem of its own, and actiontext depends on it
#   removed (1)   benchmark 0.5.0 -- activesupport 8.1 stops requiring it
#
# `benchmark` leaving is worth noticing rather than only recording: it is a
# default gem on Ruby 3.2 and a BUNDLED gem from 3.4, so this is one fewer
# thing for the interpreter step above to answer for.
#
# high_voltage 5.0.0 is the second pin.  It is the first release to ask for
# Ruby >= 3.1 -- satisfied here since step 08 -- and it declares no runtime
# dependencies at all, so `bundle lock --conservative --update high_voltage`
# moves exactly the one spec.  What comes with it was measured rather than read
# off a changelog: `show` passes `current_page` to the template as a local,
# `Page#valid?` asks ActionView which handlers are registered instead of
# matching `\.html`, and `PageFinder` refuses a traversal by comparing
# cleanpaths rather than by deleting the characters it dislikes.  All three page
# views render the same bytes and both adversarial page requests still raise
# `ActionController::RoutingError`.
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "rails pinned to 8.1"              1 "$(count "gem 'rails', '8\.1\.3\.1'" Gemfile)"
expect "framework locked at 8.1"          1 "$(count '^    rails (8\.1\.3\.1)$' Gemfile.lock)"
expect "high_voltage pinned to 5.0.0"     1 "$(count "gem 'high_voltage', '5\.0\.0'" Gemfile)"
expect "and locked there"                 1 "$(count '^    high_voltage (5\.0\.0)$' Gemfile.lock)"
expect "the 8.0 pin is gone"              0 "$(count "gem 'rails', '8\.0\.5\.1'" Gemfile)"
expect "action_text-trix arrives"         1 "$(count '^    action_text-trix (' Gemfile.lock)"
expect "benchmark leaves"                 0 "$(count '^    benchmark (' Gemfile.lock)"
expect "the interpreter does not move"    1 "$(count '^3\.2\.11$' .ruby-version)"
expect "and the lockfile still says so"   1 "$(count '^   ruby 3\.2\.11p' Gemfile.lock)"

bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "8.1.3.1"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("3.2.")'

# ---------------------------------------------------------------------------
say "framework defaults: 8.0 becomes 8.1"
# ---------------------------------------------------------------------------
# `load_defaults "8.1"` is the 8.0 branch plus six assignments and one about
# the interpreter (railties-8.1.3.1 lib/rails/application/configuration.rb, the
# `when "8.1"` arm).  Read on a booted process, at the object the railtie
# really assigns each to, 8.0 -> 8.1:
#
#   ActionController::Base.escape_json_responses            true -> false
#   ActionController::Base.action_on_path_relative_redirect :ignore -> :raise
#   ActiveRecord.raise_on_missing_required_finder_order_columns
#                                                          false -> true
#   ActiveSupport.escape_js_separators_in_json              true -> false
#   ActionView::Base.render_tracker                       :regexp -> :ruby
#   ActionView::Base.remove_hidden_field_autocomplete      false -> true
#
# and `self.yjit = !Rails.env.local?`, which reads true in production either
# way: the 8.0 branch assigns `true` outright and 8.1's assigns the negation of
# `Rails.env.local?`, which is true in production.  The interpreter has no YJIT
# to enable at either rung -- these images carry no Rust compiler -- so the
# setting is a declaration on both sides and the framework's own
# `enable_yjit` initializer checks `defined?(RubyVM::YJIT.enable)` before
# acting on it (railties-8.1.3.1 application/finisher.rb:231).
#
# None of the six moves a byte this application emits:
#
#   * the application writes no `redirect_to` anywhere in app/, lib/ or
#     config/, so nothing can be path-relative;
#   * it writes no `find_each` and no `in_batches`, so no finder can be missing
#     a required order column;
#   * `escape_json_responses` governs the renderer `render json:` uses, and
#     this application never calls it: every JSON byte it serves comes from a
#     `.json.jbuilder` template, whose entity escaping is
#     `ActiveSupport::JSON::Encoding.escape_html_entities_in_json`, which 8.1
#     leaves true.  Measured on both sides, a location named `A<b>&` serves
#     `A\u003cb\u003e\u0026` either way;
#   * `escape_js_separators_in_json` DOES reach those templates and is the one
#     setting here that can move a served byte: a string holding U+2028 or
#     U+2029 is emitted raw at 8.1 where 8.0 escaped it.  No world in
#     `cvh/scenarios.py` names anything containing either character, and the
#     recording parses a JSON body before projecting it, so the reading is the
#     same -- but "no fixture has one" is the honest reason rather than
#     "nothing changes";
#   * `render_tracker` picks the parser the template dependency tracker uses to
#     find nested renders; it changes which cache key a view digest gets, not
#     what the view renders;
#   * `remove_hidden_field_autocomplete` drops `autocomplete="off"` from the
#     hidden fields `form_for` writes, and the behaviour recording projects a
#     form field onto its name, its type and its scrubbed value, so the
#     attribute is not in the reading either way.
#
# The declaration is the work, which is why the gate on this page asks whether
# the six are at their 8.1 values and not whether the application behaves
# differently.
rewrite config/application.rb 1 \
'    # One declaration of the release whose defaults the application takes.
    # "8.0" is the 7.2 branch plus three settings, and this is the first rung
    # where every one of the three is really live on this box: `to_time` keeps
    # the receiver'"'"'s time zone instead of its offset -- which is also the one
    # the release forces, because without it Active Support deprecates the old
    # behaviour from this application'"'"'s own `config/environment.rb` -- an ETag
    # takes precedence over a `Last-Modified` when a conditional request sends
    # both, and Ruby'"'"'s global regular-expression timeout is one second.
    config.load_defaults 8.0' \
'    # One declaration of the release whose defaults the application takes.
    # "8.1" is the 8.0 branch plus six settings and one about the interpreter.
    # The `render json:` renderer stops HTML-escaping what it writes and the
    # JavaScript line separators stop being escaped in JSON generally, a
    # path-relative `redirect_to` raises instead of resolving against the
    # current path, a required-order finder with no
    # order column raises instead of guessing one, the view dependency tracker
    # reads templates with Ruby'"'"'s own parser rather than with a regular
    # expression, and a hidden field stops carrying `autocomplete="off"`.
    # Five of the six cannot move a byte this application emits: it writes no
    # `redirect_to` at all and no batch finder, it renders no `json:`, and
    # nothing reads a hidden field'"'"'s attributes.  The sixth, the separator
    # escaping, reaches its jbuilder templates but only for a string holding
    # U+2028 or U+2029, which no fixture has.  So the line is a declaration
    # and not a repair.  The seventh setting, `yjit`, is about the interpreter
    # and reads the same either way.
    config.load_defaults 8.1'
expect "the declaration moved"            1 "$(count 'config.load_defaults 8\.1' config/application.rb)"
expect "and there is only one of it"      1 "$(count 'config.load_defaults' config/application.rb)"

# ---------------------------------------------------------------------------
say "the page route, drawn and intact"
# ---------------------------------------------------------------------------
# The assertion the step is really about, taken the way it fails: boot the test
# environment, draw the route set, and read the `page` route back off the
# router rather than off any source file.  Two things have to be true at once
# and only one of them warns.
#
#   * nothing is deprecated while the routes are drawn.  With high_voltage
#     3.0.0 this is two lines on stderr -- `get received a hash argument
#     /pages/*id` and `... a hash argument as` -- and in the suite they are
#     blamed to `spec/controllers/status_controller_spec.rb:16`, the first
#     example that issues a request, because that is when the route set is
#     drawn.
#   * and the route still has the shape it had.  `/pages/*id`, named `page`,
#     answered by `high_voltage/pages#show`, with NO `(.:format)` segment:
#     that segment is what `:format => false` was suppressing, and 8.1 deletes
#     that key without a word when the value is false.
#
# `as: :page` is load-bearing rather than decoration --
# `app/views/application/_footer.html.erb` calls `page_path('contact')` and
# `page_path('about')` on every page this application renders -- so a fix that
# drops the name takes the whole site down.
RAILS_ENV=test bundle exec ruby -e '
  require File.expand_path("config/environment", Dir.pwd)
  Rails.application.reload_routes!
  route = Rails.application.routes.named_routes[:page]
  abort("no page route") if route.nil?
  puts "PAGE_SPEC=#{route.path.spec}"
  puts "PAGE_ENDPOINT=#{route.defaults[:controller]}##{route.defaults[:action]}"
  puts "HIGH_VOLTAGE=#{HighVoltage::VERSION} drawer=#{HighVoltage.routes}"
' > /tmp/solve-routes.txt 2> /tmp/solve-routes.err
cat /tmp/solve-routes.txt
expect "the page route is /pages/*id"     1 "$(count '^PAGE_SPEC=/pages/\*id$' /tmp/solve-routes.txt)"
expect "answered by high_voltage"         1 "$(count '^PAGE_ENDPOINT=high_voltage/pages#show$' /tmp/solve-routes.txt)"
expect "drawn by the gem, at 5.0.0"       1 "$(count '^HIGH_VOLTAGE=5\.0\.0 drawer=true$' /tmp/solve-routes.txt)"
expect "nothing deprecated drawing them"  0 "$(count 'hash argument' /tmp/solve-routes.err)"
expect "and nothing else either"          0 "$(count 'DEPRECATION' /tmp/solve-routes.err)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
