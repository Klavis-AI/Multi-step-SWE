#!/usr/bin/env bash
# Reference solution -- step 08, "reach Rails 7.1 on Ruby 3.2".
#
# The tree this runs against is step 07's finished tree -- Rails 7.0.10 on Ruby
# 3.1.7, `config.load_defaults 7.0`, a deterministic case-folding `encrypts` on
# `subscribers.email` with its keys in the credentials, and three relations in
# /export.csv loaded through the async executor.  The first block below says so
# out loud: every count it takes is a fact about *step 07's* output, so running
# this against anything else stops on the first line instead of producing a
# tree that is neither.
#
# Two pins move and only one of them has to.  7.1 needs Ruby 3.1 at a minimum
# and the tree arrives on 3.1.7, so the framework could be taken alone here.
# The interpreter comes with it because steps 09, 10 and 11 all want 3.2 and
# 7.1 is the last release on this ladder that runs on both sides of that move;
# taking it later would put a two-pin rung further up, where the framework is
# also changing more.
#
# Six pieces of work, in the order this file does them:
#
#   1. The floor.  Ruby 3.2 removed `File.exists?`, and this tree calls it in
#      three places -- one of them config/boot.rb, which runs before anything
#      else -- and so does sass 3.2.19, which is pinned here and cannot be
#      kept.  Moving sass moves foundation-rails with it, for the reason the
#      old pin was written down.  rspec-rails 4 cannot see Action View 7.1.
#   2. `load_defaults 7.1`, which breaks nothing in this application and turns
#      on eighteen settings that are read.
#   3. `config.autoload_lib(ignore: %w[assets tasks])`, replacing the
#      `eager_load_paths <<` line this ladder wrote at step 05.
#   4. Strict locals on the ten partials this application renders -- and on
#      those ten only, because four of the fourteen on disk have no call site.
#   5. The two deprecations 7.1 raises inside the spec suite --
#      `ActiveRecord::Migration.check_pending!` and `config.fixture_path=` --
#      both of which the release schedules for removal in 7.2, which is the
#      next rung.
#   6. Nothing for the headers: 7.1 drops `X-Download-Options` on its own once
#      the defaults are adopted, and this file does not touch them.  It is
#      listed here because a reader who finds a header gate in the profile and
#      no header edit in the solution should find out why from the solution.
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

# prepend <file> <line> -- put <line> at the very top of <file>.  Refuses a
# file that already begins with an ERB comment, because the one thing a strict
# locals declaration has to be is FIRST: Action View reads the magic comment
# out of the head of the template and a second one below it is a comment.
prepend() {
    ruby -e '
      path, line = ARGV
      src = File.read(path)
      abort("prepend: #{path}: already starts with #{src.lines.first.strip}") \
        if src.start_with?("<%#")
      File.write(path, line + "\n" + src)
    ' "$@"
    printf '   ok  %-46s %s\n' "$(printf '%s' "$1" | sed 's|app/views/||')" "$2"
}


# ---------------------------------------------------------------------------
say "the tree this expects to find"
# ---------------------------------------------------------------------------
expect "rails 7.0.10 in the Gemfile"      1 "$(count "gem 'rails', '7.0.10'" Gemfile)"
expect "ruby 3.1.7"                       1 "$(count '^3\.1\.7$' .ruby-version)"
expect "load_defaults 7.0"                1 "$(count 'config.load_defaults 7\.0' config/application.rb)"
expect "lib on the eager-load paths"      1 "$(count "config.eager_load_paths << Rails.root.join('lib')" config/application.rb)"
expect "sass held at 3.2.19 since step 05" 1 "$(count "gem 'sass', '3.2.19'" Gemfile)"
expect "foundation held with it"          1 "$(count "gem 'foundation-rails', '5.2.2.0'" Gemfile)"
expect "rspec-rails on the 4.x line"      1 "$(count "gem 'rspec-rails', '~> 4.0'" Gemfile)"
expect "File.exists? call sites"          3 "$(count 'File\.exists?' config spec)"
expect "the encrypted column, from 07"    1 "$(count 'encrypts :email' app/models/subscriber.rb)"
expect "the async relations, from 07"     3 "$(count '\.load_async$' app/controllers/numerical_answers_controller.rb)"
expect "the deprecated pending check"      1 "$(count 'Migration.check_pending!' spec/spec_helper.rb)"
expect "the singular fixture path"        1 "$(count 'config.fixture_path = ' spec/spec_helper.rb)"
expect "partials on disk"                14 "$(files app/views -name '_*.html.erb')"
expect "and none of them declares locals" 0 "$(count '^<%# locals:' app/views/*/_*.html.erb)"

# ---------------------------------------------------------------------------
say "the floor: three gems the interpreter moves, and a File method it removed"
# ---------------------------------------------------------------------------
# `bundle lock --conservative --update rails` against the two pins -- not
# `bundle update`, which floats seventy-odd gems -- moves seventeen gems and
# adds fourteen.  Thirteen of the seventeen are the framework's own list; the
# other four are the sanitizer chain 7.1 requires a newer version of
# (loofah, rails-dom-testing, rails-html-sanitizer) and reline.  Nothing is
# removed, which has not been true of any other rung.  The fourteen additions
# are all libraries Ruby 3.2 and 3.4 are moving out of the standard library and
# that railties 7.1 now names as gems.
#
# That resolution installs, and `require "rails"` answers 7.1.6.  It does not
# boot.  Three more moves, each of them measured rather than read off a guide:
#
#   sass 3.2.19            version.rb:115 calls `File.exists?`, from
#                          `sass-rails.rb:1`, inside `Bundler.require` -- so
#                          the whole application dies at second 0 and no
#                          amount of application code can answer for it.
#   foundation-rails 5.2.2.0
#                          comes with sass, because sass 3.3 changed `index()`
#                          to answer `null` rather than `false` for a miss and
#                          5.2's module guard tests for `false`.  5.5.3.2 is
#                          the last 5.x, so the stylesheet keeps the major
#                          version it was written against.
#   rspec-rails 4.x        actionview 7.1's `PathSet#typecast` (path_set.rb:79)
#                          refuses anything that is not a String, a Pathname
#                          or an `ActionView::Resolver`, and rspec-rails 4's
#                          `EmptyTemplateResolver::ResolverDecorator` is none
#                          of the three.  57 of the 504 examples raise
#                          TypeError before asserting anything.
#
# Three files have to name the same interpreter, for the reason step 05 gave:
# rbenv reads .ruby-version, Bundler reads the `ruby` directive, and `bundle
# install` refuses a lockfile whose RUBY VERSION the running interpreter does
# not satisfy.
printf '3.2.11\n' > .ruby-version
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "the interpreter the tree selects" 1 "$(count '^3\.2\.11$' .ruby-version)"
expect "the same one in the Gemfile"      1 "$(count "^ruby '3.2.11'" Gemfile)"
expect "and in the lockfile"              1 "$(count '^   ruby 3\.2\.11p' Gemfile.lock)"
expect "rails pinned to 7.1"              1 "$(count "gem 'rails', '7.1.6'" Gemfile)"
expect "framework locked at 7.1"          1 "$(count '^    rails (7\.1\.6)$' Gemfile.lock)"
expect "sass moved off the 3.2 line"      1 "$(count '^    sass (3\.4\.25)$' Gemfile.lock)"
expect "foundation moved with it"         1 "$(count '^    foundation-rails (5\.5\.3\.2)$' Gemfile.lock)"
expect "rspec-rails that can see 7.1"     1 "$(count '^    rspec-rails (6\.1\.5)$' Gemfile.lock)"

# `File.exists?` was deprecated in Ruby 2.1 and removed in 3.2.  Three call
# sites, and the first of them is the one that matters: config/boot.rb runs
# before the framework, before the initializers and before anything that could
# report a better error, so on 3.2 this tree prints a NoMethodError about
# File:Class and nothing else -- in all three environments, and for `rake` and
# `rspec` alike.  The other two are in the spec support file that writes the
# JavaScript fixtures.
rewrite config/boot.rb 1 \
"require 'bundler/setup' if File.exists?(ENV['BUNDLE_GEMFILE'])" \
"require 'bundler/setup' if File.exist?(ENV['BUNDLE_GEMFILE'])"
rewrite spec/support/save_fixture.rb 1 \
'    FileUtils.mkdir_p fixture_directory unless File.exists?(fixture_directory)' \
'    FileUtils.mkdir_p fixture_directory unless File.exist?(fixture_directory)'
rewrite spec/support/save_fixture.rb 1 \
'    return if File.exists?(ignore_file)' \
'    return if File.exist?(ignore_file)'
expect "File.exists? call sites left"     0 "$(count 'File\.exists?' app lib config spec db)"

bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "7.1.6"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("3.2.")'

# ---------------------------------------------------------------------------
say "framework defaults: 7.0 becomes 7.1, and lib/ gets the name 7.1 gives it"
# ---------------------------------------------------------------------------
# `load_defaults "7.1"` is the 7.0 branch plus twenty-eight settings of its own
# (railties-7.1.6 lib/rails/application/configuration.rb, the `when "7.1"`
# branch).  Eighteen of them mean something to this application; the rest
# configure Active Storage, Active Job, SQLite and the query-log tags, none of
# which this application has.  Read on a booted process, at the place each is
# really consulted, 7.0 -> 7.1:
#
#   add_autoload_paths_to_load_path               true  -> false
#   precompile_filter_parameters                  false -> true
#   SanitizeHelper.sanitizer_vendor               Rails::HTML4 -> Rails::HTML5
#   Rails::Dom::Testing.default_html_version      :html4 -> :html5
#   commit_transaction_on_non_local_return        false -> true
#   allow_deprecated_singular_associations_name   true  -> false
#   raise_on_assign_to_attr_readonly              false -> true
#   before_committed_on_all_records               false -> true
#   run_after_transaction_callbacks_in_order_defined false -> true
#   ActiveRecord::Base.default_column_serializer  YAML  -> nil
#   marshalling_format_version                    6.1   -> 7.1
#   generate_secure_token_on                      :create -> :initialize
#   Encryption.config.hash_digest_class           SHA1  -> SHA256
#   debug_exception_log_level                     :fatal -> :error
#   ActiveSupport.cache_format_version            7.0   -> 7.1
#   message_serializer                            :marshal -> :json_allow_marshal
#   raise_on_invalid_cache_expiration_time        false -> true
#   allow_deprecated_parameters_hash_equality     true  -> false
#
# None of the eighteen breaks anything here, which is what makes this rung
# different from step 07: the suite is 504 green before the line moves and 504
# green after it, and the nine recorded pages are byte-identical apart from the
# CSP nonce.  What the line does change and nothing above lists is the response
# header set: 7.1's `action_dispatch.default_headers` drops
# `X-Download-Options: noopen`, which IE8 read and nothing has read since.
# That is a real change to every HTML response this application serves and it
# needs no edit here -- it arrives with the declaration.
#
# `config.autoload_lib` is the other half of this block and it is a 7.1 method
# rather than a 7.1 default.  The line it replaces has been here since step 05
# and works by a side effect: eager-load paths are also autoload paths, so
# pushing lib/ onto one gets the other.  `autoload_lib` says the thing itself,
# puts lib/ on BOTH lists, and takes the `ignore:` the side effect never
# could -- lib/assets and lib/tasks hold no Ruby, and leaving them as autoload
# roots is a trap for the first `.rb` anyone puts in either.
rewrite config/application.rb 1 \
'    # One declaration of the release whose defaults the application takes.
    # "7.0" is the 6.1 branch plus SHA256 where the digests were SHA1, the 7.0
    # cache entry format, RFC 4122 namespaced UUIDs, a `TimeWithZone` that
    # answers its own name and a `to_s` that takes no format argument,
    # `button_to` generating a <button>, stylesheet tags without `media="all"`,
    # scopes Active Record can invert on its own, partial inserts off,
    # foreign keys verified for fixtures, open redirects raising rather than
    # redirecting, parameter wrapping on by default, and `X-XSS-Protection: 0`.
    config.load_defaults 7.0' \
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
    config.load_defaults 7.1'
rewrite config/application.rb 1 \
"    # Rails 5.0 stops autoloading once the application has booted when
    # config.eager_load is on, so an autoload-only path leaves lib/ constants
    # undefined in production.  eager_load_paths is also an autoload path, so
    # this keeps development working and makes production load them up front.
    config.eager_load_paths << Rails.root.join('lib')" \
'    # What the `eager_load_paths <<` line here has meant since step 05, said
    # in the method 7.1 added for it: lib/ on the autoload paths AND the
    # eager-load paths, so development resolves its constants lazily and
    # production loads them at boot.  `ignore:` is the part the old line could
    # not express -- lib/assets and lib/tasks hold no Ruby, and an autoloader
    # that treats them as roots will try to define constants for the first
    # `.rb` anyone puts in either.
    config.autoload_lib(ignore: %w[assets tasks])'
expect "the declaration moved"            1 "$(count 'config.load_defaults 7\.1' config/application.rb)"
expect "and there is only one of it"      1 "$(count 'config.load_defaults' config/application.rb)"
expect "lib declared the 7.1 way"         1 "$(count 'config.autoload_lib' config/application.rb)"
expect "and the old line is gone"         0 "$(count 'config\.eager_load_paths' config/application.rb)"

# ---------------------------------------------------------------------------
say "strict locals on the ten partials this application renders"
# ---------------------------------------------------------------------------
# A `<%# locals: (...) %>` magic comment on the first line of a partial turns
# its locals into a method signature: an undeclared local raises instead of
# arriving as nil, a declared-but-unpassed one raises unless it has a default,
# and `local_assigns` stops being an open bag.  It is 7.1's answer to the
# oldest hazard in a Rails view, which is that `render partial:` with a
# misspelt key renders a page with a hole in it and no error anywhere.
#
# TEN partials, not fourteen.  The four left alone --
# locations/_subscribe_form, numerical_answers/_vote,
# numerical_answers/_vote_meter and voice_answers/_voice_answers -- have no
# call site anywhere in this application, which was established by rendering
# every page and every IVR path with a subscriber on
# `render_partial.action_view` rather than by grepping for `render`.  A
# declaration on a partial nothing renders is a declaration against no caller:
# it cannot be wrong, it cannot be right, and it cannot be measured.  They are
# dead code and this step does not ask anyone to delete them.
#
# The signatures are what the call sites actually pass, which is not always
# what the partial's body reads:
#
#   application/_map           ()            rendered with no locals at all;
#                                            `()` is stricter than omitting
#                                            the comment and stricter than
#                                            `(**nil)` is obvious -- it means
#                                            this partial takes none.
#   application/_header        (content:)    the three layout partials each
#   application/_footer        (content:)    take the AppContentSet section
#   application/_feedback      (content:)    they render.
#   landing/_answer            (answer:)
#   locations/_voice_message   (answer:)
#   voice_answers/_answer      (answer:)
#   locations/_email_notifications  (location:)
#   locations/_numerical_response   (numerical_response:)
#   landing/_info_block        (title: nil, html_class: nil)
#
# The last one is the only one that is not mechanical, and it is the trap in
# this requirement.  `_info_block` is rendered from two call sites that pass
# different sets -- one passes both locals, one passes neither -- and its body
# tests for them with `defined?(title)`.  Under strict locals `defined?` is
# ALWAYS true, because the local is now a method parameter and exists whether
# it was passed or not, so a declaration of `(title:, html_class:)` raises on
# the call site that passes neither and a declaration of `(title:, ...)` with
# `defined?` still in the body renders `nil.downcase` on it.  Defaults of nil
# plus truthiness tests is the spelling that is correct at both call sites,
# and the two `defined?` guards have to go with it.
prepend app/views/application/_map.html.erb                '<%# locals: () %>'
prepend app/views/application/_header.html.erb             '<%# locals: (content:) %>'
prepend app/views/application/_footer.html.erb             '<%# locals: (content:) %>'
prepend app/views/application/_feedback.html.erb           '<%# locals: (content:) %>'
prepend app/views/landing/_answer.html.erb                 '<%# locals: (answer:) %>'
prepend app/views/locations/_voice_message.html.erb        '<%# locals: (answer:) %>'
prepend app/views/voice_answers/_answer.html.erb           '<%# locals: (answer:) %>'
prepend app/views/locations/_email_notifications.html.erb  '<%# locals: (location:) %>'
prepend app/views/locations/_numerical_response.html.erb   '<%# locals: (numerical_response:) %>'

rewrite app/views/landing/_info_block.html.erb 1 \
"<div class=\"city-voice-info-block <%= title.downcase.gsub(' ', '-') if defined?(title) %> <%= html_class if defined?(html_class) %>\">" \
"<div class=\"city-voice-info-block <%= title.downcase.gsub(' ', '-') if title %> <%= html_class %>\">"
rewrite app/views/landing/_info_block.html.erb 1 \
'  <% if defined?(title)  %>' \
'  <% if title %>'
prepend app/views/landing/_info_block.html.erb '<%# locals: (title: nil, html_class: nil) %>'

expect "partials declaring locals"       10 "$(count '^<%# locals:' app/views/*/_*.html.erb)"
expect "and the four dead ones left be"   4 "$(($(files app/views -name '_*.html.erb') - $(count '^<%# locals:' app/views/*/_*.html.erb)))"
expect "no defined? left in _info_block"  0 "$(count 'defined?' app/views/landing/_info_block.html.erb)"
expect "no defined? anywhere in a view"   0 "$(count 'defined?' app/views)"

# ---------------------------------------------------------------------------
say "the two deprecations 7.1 raises inside the spec suite"
# ---------------------------------------------------------------------------
# Both are in spec/spec_helper.rb, both are announced by the release that is
# being adopted here, and both are scheduled for removal in 7.2 -- so a rung
# that adopts 7.1 and leaves them is a rung that hands step 09 a suite that
# does not run.
#
#   ActiveRecord::Migration.check_pending!   (spec/spec_helper.rb:19)
#       activerecord-7.1.6 lib/active_record/migration.rb:679-694.  The method
#       still raises PendingMigrationError and still means what it meant; its
#       first statement is now `ActiveRecord.deprecator.warn`, naming
#       `check_all_pending!` at :696-710 as the replacement.  The new one asks
#       every database configuration in the environment instead of the single
#       connection the caller happened to be holding -- this application has
#       one database, so the answer is the same, and what actually changes is
#       that the question no longer needs a connection opened first.  The 7.0
#       file has neither: activerecord-7.0.10 migration.rb:662 is
#       `def check_pending!(connection = Base.connection)` with no deprecator
#       call, and there is no `check_all_pending!` anywhere in it.
#       It is raised through
#       `ActiveRecord.deprecator`, so the collector the harness prepends to
#       ActiveSupport::Deprecation inside the suite's own process records it,
#       and `suite_deprecations` -- a guard, so an unweighted zero for the
#       whole gate family -- fails with `["spec/spec_helper.rb"]`.
#
#   config.fixture_path =                    (spec/spec_helper.rb:38)
#       activerecord-7.1.6 lib/active_record/test_fixtures.rb:65-67,
#       "TestFixtures.fixture_path= is deprecated and will be removed in Rails
#       7.2.  Use .fixture_paths= instead", against :31
#       `class_attribute :fixture_paths, instance_writer: false, default: []`.
#       rspec-rails 6.1.5 lib/rspec/rails/configuration.rb:167-187 defines its
#       own reader and writer behind `if ::Rails::VERSION::STRING >= "7.1.0"`,
#       calls `RSpec.deprecate` and forwards to the plural -- which is why this
#       one appears in rspec's own "Deprecation Warnings:" summary and NOT in
#       the ActiveSupport collector, and so is not what `suite_deprecations`
#       reads.  It has a reading of its own instead: `cvgate/rspecdep.rb`
#       prepends to `RSpec.deprecate` as well, and `rspec_own_deprecations`
#       gates the count at zero.  One rspec deprecation
#       is deliberately not in that count -- rspec-expectations warns once a
#       run about this suite's own 22 `.should` call sites, which no step of
#       this ladder asks anyone to rewrite; it is reported separately as
#       `inherited_rspec_deprecations`.
rewrite spec/spec_helper.rb 1 \
'ActiveRecord::Migration.check_pending! if defined?(ActiveRecord::Migration)' \
'ActiveRecord::Migration.check_all_pending! if defined?(ActiveRecord::Migration)'
rewrite spec/spec_helper.rb 1 \
'  config.fixture_path = "#{::Rails.root}/spec/fixtures"' \
'  config.fixture_paths = ["#{::Rails.root}/spec/fixtures"]'
expect "check_pending! call sites left"   0 "$(count 'check_pending!' spec)"
expect "the plural fixture path"          1 "$(count 'config.fixture_paths = ' spec/spec_helper.rb)"
expect "and the singular one is gone"     0 "$(count 'config.fixture_path = ' spec/spec_helper.rb)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"

# The ten declarations are NOT compiled here.  Compiling one template outside a
# booted application means building an `ActionView::Base` by hand, and on 7.1
# that pulls in the whole helper chain and raises before it reads a line of
# ERB.  A malformed magic comment is caught
# three times over by things that do boot: the suite renders these partials in
# its view and controller examples, the recorded pages render all ten, and the
# step's own `strict_locals_enforced` gate compiles each one against the real
# Action View and renders it with a local it does not declare.  A cheap
# reimplementation of Action View's declaration regex here would be a fourth
# reading that agrees with the other three until the day it does not.
echo "solve.sh: done"
