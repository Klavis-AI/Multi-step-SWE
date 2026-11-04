#!/usr/bin/env bash
# Reference solution -- step 02, "reach Rails 5.0 on Ruby 2.4".
#
# The tree this runs against is step 01's finished tree -- Rails 4.2.11.3 on
# Ruby 2.3.8 -- because Harbor runs the steps in one container and each step
# starts from the last one's end state.  The first block below says so out
# loud: every count it takes is a fact about *step 01's* output, so running
# this against the pristine repository stops on the first line instead of
# producing a tree that is neither.
#
# Those counts are deliberately exact where the instruction is not.  A
# candidate's step 01 is judged by step 01's gates -- Rails 4.2.x, Ruby 2.3.x,
# at least 495 examples -- so a legal predecessor tree need not be pinned to
# 4.2.11.3, and step 02's instruction describes what it is handed in those
# terms.  This script is never run against a candidate's tree: Harbor runs it
# only in the oracle pass, immediately after this repository's own step-01
# solution.  What it asserts is therefore the one handoff it actually gets, and
# a line that fails here means that handoff moved -- not that some other tree
# would have been acceptable.
#
# As in step 01, the two artefacts too long to write inline -- the resolved
# Gemfile and its lockfile -- ship as files beside this script, and every edit
# states how many lines it expects to touch.  A `sed` that matched nothing
# exits 0 and prints exactly what a successful one prints.
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
# exactly <count> times.
#
# The match is anchored at the end of a line: the pattern searched for is
# <from> followed by a newline.  That is what lets `get :show, token: token`
# name one call site in a file that also contains
# `get :show, token: token, all: 'do-it'` -- without the anchor the first
# would rewrite the head of the second and leave a line that parses and means
# something else.  Both strings are literal, never patterns, so the `:` and
# `{` in a Ruby argument list mean themselves.
#
# The count is required rather than defaulted for the same reason the `expect`
# calls exist.  This is the step that rewrites 17 assertions in 14 files by
# hand; a typo in any one of them is a `from` that matches nothing, and a
# silent no-op there is a spec file that still calls Rails 4's signature and a
# suite that fails for a reason nobody wrote down.
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
expect "rails 4.2.11.3 in the Gemfile"   1 "$(count "gem 'rails', '4.2.11.3'" Gemfile)"
expect "ruby 2.3.8"                      1 "$(count '^2\.3\.8$' .ruby-version)"
expect "models on ActiveRecord::Base"    7 "$(count '< ActiveRecord::Base' app/models)"
expect "no ApplicationRecord yet"        0 "$(files app/models -name application_record.rb)"
expect "unversioned migrations"         43 "$(count '< ActiveRecord::Migration$' db/migrate)"
expect "double-extension stylesheets"   14 "$(files app/assets/stylesheets -name '*.css.scss')"
expect "no new_framework_defaults yet"   0 "$(files config/initializers -name new_framework_defaults.rb)"
expect "positional controller calls"    19 "$(grep -rEn '^[[:space:]]*(get|post|put|patch|delete) :[a-z_]+,' spec | wc -l | tr -d ' ')"

# ---------------------------------------------------------------------------
say "interpreter and dependencies"
# ---------------------------------------------------------------------------
# 2.4 is the floor Rails 5.0 shares with this tree's gems: 5.0 supports 2.2
# upwards, but `new_framework_defaults` turns on `to_time_preserves_timezone`,
# which is a 2.4 change, and the release is the one 5.0.7 was maintained
# against.  Going further is step 03's business -- 5.0 does not support 2.5's
# successors, and moving two axes at once makes a failure unattributable.
printf '2.4.10\n' > .ruby-version
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "ruby directive"           1 "$(count "^ruby '2.4.10'" Gemfile)"
expect "rails pinned to 5.0"      1 "$(count "gem 'rails', '5.0.7.2'" Gemfile)"
# `rails-deprecated_sanitizer` was a transitive dependency of rails 4.2 and of
# nothing else; resolving against 5.0 drops it without anybody naming it.
expect "4.2 sanitizer shim gone"  0 "$(count 'rails-deprecated_sanitizer' Gemfile Gemfile.lock)"
# `assigns` and `assert_template` left ActionPack in 5.0.  This tree asserts on
# `assigns` in 23 places; the gem that keeps them is the documented landing
# place for exactly that, and it is the alternative to rewriting assertions
# this step was not asked to rewrite.
expect "assigns gem declared"     1 "$(count "^  gem 'rails-controller-testing'" Gemfile)"

# ---------------------------------------------------------------------------
say "models: ApplicationRecord"
# ---------------------------------------------------------------------------
# 5.0 generates models under an application-owned abstract base class instead
# of ActiveRecord::Base directly, so that an application-wide concern has one
# place to go.  The rename has to happen before the class itself is written:
# `ApplicationRecord` is the one class that does still inherit from
# ActiveRecord::Base, and a tree-wide substitution run afterwards would turn
# its own superclass into itself.
sed -i -E 's/^class ([A-Za-z0-9_:]+) < ActiveRecord::Base$/class \1 < ApplicationRecord/' app/models/*.rb
expect "models reparented"               7 "$(count '< ApplicationRecord' app/models)"
expect "models still on the framework"   0 "$(count '< ActiveRecord::Base' app/models)"

cat > app/models/application_record.rb <<'RUBY'
class ApplicationRecord < ActiveRecord::Base
  self.abstract_class = true
end
RUBY
expect "abstract base class"             1 "$(count 'self.abstract_class = true' app/models)"
expect "one class on ActiveRecord::Base" 1 "$(count '< ActiveRecord::Base' app/models)"

# ---------------------------------------------------------------------------
say "belongs_to is required by default"
# ---------------------------------------------------------------------------
# `config.active_record.belongs_to_required_by_default` is the 5.0 default and
# is switched on in new_framework_defaults.rb below.  Two of this tree's
# associations meet it differently.
#
# `Answer` wants it: the two presence validations it declares by hand are
# exactly what the framework now adds, and leaving both in place validates the
# same association twice and reports the failure twice.  They go.
expect "hand-written presence validations" 2 "$(count 'presence: true$' app/models/answer.rb)"
sed -i -E '/^[[:space:]]*validates :(call|question), presence: true$/d' app/models/answer.rb
# The blank line the pair left behind doubles up with the one above it;
# `cat -s` squeezes runs of blank lines to one.
cat -s app/models/answer.rb > app/models/answer.rb.tmp
mv app/models/answer.rb.tmp app/models/answer.rb
expect "presence validations gone"         0 "$(count 'presence: true$' app/models/answer.rb)"
expect "the associations remain"           2 "$(count '^  belongs_to :\(call\|question\)$' app/models/answer.rb)"

# `Call` does not: `CallsController#create` makes the row when the call
# connects, and the IVR only learns which location it is about several requests
# later.  Requiring it would reject every inbound call.
rewrite app/models/call.rb 1 \
'  belongs_to :location' \
'  # `belongs_to` is required by default from Rails 5.0, and a call row is
  # created by `CallsController#create` before the IVR has asked which
  # location it is about -- `Call::LocationAssigner` fills that in later.  The
  # schema agrees: `calls.location_id` has no NOT NULL.
  belongs_to :location, optional: true'
expect "the one optional association"      1 "$(count 'optional: true' app/models)"

# ---------------------------------------------------------------------------
say "migrations carry the schema version they were written against"
# ---------------------------------------------------------------------------
# From 5.0 a migration declares which release's behaviour it wants:
# `ActiveRecord::Migration[4.2]` keeps the semantics these 43 files were
# written and already run under -- `t.timestamps` without null:, string
# defaults, the 4.2 index API -- instead of silently acquiring 5.0's.  An
# unversioned `ActiveRecord::Migration` raises StandardError on 5.0 and takes
# `rake db:migrate` with it.
sed -i -E 's/< ActiveRecord::Migration$/< ActiveRecord::Migration[4.2]/' db/migrate/*.rb
expect "versioned migrations"           43 "$(count '< ActiveRecord::Migration\[4\.2\]$' db/migrate)"
expect "unversioned migrations left"     0 "$(count '< ActiveRecord::Migration$' db/migrate)"

# The schema dump moves with them: running the migrations under 5.0 re-dumps
# `db/schema.rb`, and the dump is a committed file.  Two things change in it --
# the magic comment 5.0 stopped emitting, and the `force:` value, which became
# `:cascade` when the dumper started dropping dependent objects with the table.
# Neither is load-bearing; the file is written here rather than left behind so
# that the committed dump is the one this application's own `rake db:migrate`
# produces, and the next step's diff is the next step's changes.
rewrite db/schema.rb 1 '# encoding: UTF-8
# This file is auto-generated from the current state of the database. Instead' \
'# This file is auto-generated from the current state of the database. Instead'
sed -i -E 's/, force: true do \|t\|$/, force: :cascade do |t|/' db/schema.rb
expect "tables dumped with cascade"      7 "$(count 'force: :cascade' db/schema.rb)"
expect "tables dumped the old way"       0 "$(count 'force: true' db/schema.rb)"

# ---------------------------------------------------------------------------
say "framework configuration"
# ---------------------------------------------------------------------------
# `config.autoload_paths` still exists on 5.0, but 5.0 is the release that made
# autoloading outside eager-loaded paths an error once the application has
# booted: with `config.eager_load = true`, a constant from lib/ that nothing
# referenced during boot is simply not there in production.  `eager_load_paths`
# is an autoload path as well, so this keeps development's behaviour and fixes
# production's.
rewrite config/application.rb 1 \
"    config.autoload_paths << Rails.root.join('lib')" \
"    # Rails 5.0 stops autoloading once the application has booted when
    # config.eager_load is on, so an autoload-only path leaves lib/ constants
    # undefined in production.  eager_load_paths is also an autoload path, so
    # this keeps development working and makes production load them up front.
    config.eager_load_paths << Rails.root.join('lib')"

# `serve_static_files` and `static_cache_control` are deprecated in 5.0 in
# favour of one `public_file_server` object.  The deprecation is raised from
# the application's own environment files, which is the difference between a
# warning the application can silence and one it cannot.
rewrite config/environments/production.rb 1 \
'  config.serve_static_files = true' \
'  config.public_file_server.enabled = true'
rewrite config/environments/test.rb 1 \
'  config.serve_static_files  = true
  config.static_cache_control = "public, max-age=3600"' \
"  config.public_file_server.enabled = true
  config.public_file_server.headers = { 'Cache-Control' => 'public, max-age=3600' }"
expect "deprecated static settings"      0 "$(count 'serve_static_files\|static_cache_control' config)"

# Rack::Lock left the default middleware stack in 5.0 -- it was only ever there
# to serialize a non-threadsafe development stack, and 5.0's is threadsafe.
# `insert_before` on a middleware that is not in the stack raises at boot, so
# this line stops `rails server` in development on a tree that is otherwise
# migrated.  ActionDispatch::Static is the first entry live-reload can usefully
# follow.
rewrite config/environments/development.rb 1 \
'  config.middleware.insert_before(::Rack::Lock, ::Rack::LiveReload, :min_delay => 500) if defined?(Rack::LiveReload)' \
'  # Rails 5.0 dropped Rack::Lock from the default stack, so the old anchor is
  # not in it any more and insert_before raises at boot.  ActionDispatch::Static
  # is the first entry rack-livereload can usefully follow.
  config.middleware.insert_after(::ActionDispatch::Static, ::Rack::LiveReload, :min_delay => 500) if defined?(Rack::LiveReload)'
# The name survives in the comment above; what must not survive is the
# anchor, so the count is of the call and not of the string.
expect "Rack::Lock anchored on"          0 "$(count 'insert_before(::Rack::Lock' config app lib)"

# The five defaults `rails new` writes for an application coming from 4.2,
# verbatim as railties 5.0.7.2 generates them for a new application (the
# template's `options[:update]` branch, which comments them out, is for an
# in-place `rails app:update` that has to keep the old behaviour running).
# Adopting them is the point of the step: each is a 5.0 behaviour that 5.0
# leaves off for compatibility, and leaving them off is arriving at the version
# number without the release.
cat > config/initializers/new_framework_defaults.rb <<'RUBY'
# Be sure to restart your server when you modify this file.
#
# This file contains migration options to ease your Rails 5.0 upgrade.
#
# Read the Guide for Upgrading Ruby on Rails for more info on each option.

Rails.application.config.action_controller.raise_on_unfiltered_parameters = true

# Enable per-form CSRF tokens. Previous versions had false.
Rails.application.config.action_controller.per_form_csrf_tokens = true

# Enable origin-checking CSRF mitigation. Previous versions had false.
Rails.application.config.action_controller.forgery_protection_origin_check = true

# Make Ruby 2.4 preserve the timezone of the receiver when calling `to_time`.
# Previous versions had false.
ActiveSupport.to_time_preserves_timezone = true

# Require `belongs_to` associations by default. Previous versions had false.
Rails.application.config.active_record.belongs_to_required_by_default = true

# Do not halt callback chains when a callback returns false. Previous versions had true.
ActiveSupport.halt_callback_chains_on_return_false = false

# Configure SSL options to enable HSTS with subdomains. Previous versions had false.
Rails.application.config.ssl_options = { hsts: { subdomains: true } }
RUBY
expect "new framework defaults set"      7 "$(count '^\(Rails.application.config\|ActiveSupport\)' config/initializers/new_framework_defaults.rb)"

# ---------------------------------------------------------------------------
say "stylesheets: one extension, not two"
# ---------------------------------------------------------------------------
# sprockets-rails 3 resolves `stylesheet_link_tag 'application'` through
# sprockets 3, which stopped treating `application.css.scss` as a source for
# the logical path `application.css` without complaint: it finds it through a
# compatibility path and deprecates it, from the view and the helper that asked
# -- `app/views/layouts/application.html.erb` and
# `app/helpers/voice_feedback_helper.rb`, which are this application's files.
# The deprecation is the application's own, so the rename is the fix and not a
# tidy-up.  `foundation_and_overrides.scss.erb` already has one extension and a
# preprocessor suffix, and stays.
expect "stylesheets to rename"          14 "$(files app/assets/stylesheets -name '*.css.scss')"
find app/assets/stylesheets -name '*.css.scss' -print0 |
    while IFS= read -r -d '' sheet; do mv "$sheet" "${sheet%.css.scss}.scss"; done
expect "double extensions left"          0 "$(files app/assets/stylesheets -name '*.css.scss')"
expect "single-extension sass"          14 "$(files app/assets/stylesheets -name '*.scss')"
# The names inside the manifest and the partial imports are extensionless
# already, so nothing refers to the old spelling.
expect "references to the old names"     0 "$(count '\.css\.scss' app lib config)"

# ---------------------------------------------------------------------------
say "controller specs: keyword arguments"
# ---------------------------------------------------------------------------
# From 5.0 the controller-test helpers take `params:`, `session:`, `format:`
# and the rest as keywords; a positional hash is deprecated in 5.0 and removed
# in 5.1, and -- more immediately -- every key in it is treated as a parameter,
# so `format:` passed positionally becomes a request parameter named "format"
# rather than a requested format.  Each call below is rewritten to say which
# of the two it meant.  The two remaining positional calls are
# `get :index, format: :json` and `get :index, format: :csv`: `format` is one
# of the keywords, so those lines are already in the new spelling.
rewrite spec/controllers/calls/answers_controller_spec.rb 1 \
'      post :create, {call_id: call.id, question_id: 0}.merge(params)' \
'      post :create, params: {call_id: call.id, question_id: 0}.merge(params)'
rewrite spec/controllers/calls/consents_controller_spec.rb 1 \
'      post :create, {call_id: call.id}.merge(params)' \
'      post :create, params: {call_id: call.id}.merge(params)'
rewrite spec/controllers/calls/locations_controller_spec.rb 1 \
'      post :create, {call_id: call.id}.merge(params)' \
'      post :create, params: {call_id: call.id}.merge(params)'
rewrite spec/controllers/calls/messages_controller_spec.rb 1 \
"      post :create, {call_id: call.id, 'From' => '+14155551212', 'To' => '+14157672676'}.merge(params)" \
"      post :create, params: {call_id: call.id, 'From' => '+14155551212', 'To' => '+14157672676'}.merge(params)"
rewrite spec/controllers/calls/playbacks_controller_spec.rb 1 \
'      post :create, {call_id: call.id, message_id: 0}.merge(params)' \
'      post :create, params: {call_id: call.id, message_id: 0}.merge(params)'
rewrite spec/controllers/calls/questions_controller_spec.rb 1 \
'      post :create, {call_id: call.id}.merge(params)' \
'      post :create, params: {call_id: call.id}.merge(params)'
rewrite spec/controllers/calls_controller_spec.rb 1 \
'      post :create, params' \
'      post :create, params: params'
rewrite spec/controllers/confirms_controller_spec.rb 1 \
'      get :show, token: token' \
'      get :show, params: { token: token }'
rewrite spec/controllers/locations_controller_spec.rb 1 \
'      get :show, id: location_id' \
'      get :show, params: { id: location_id }'
rewrite spec/controllers/locations_controller_spec.rb 1 \
'        get :show, id: location_id, format: :json' \
'        get :show, params: { id: location_id }, format: :json'
rewrite spec/controllers/subscription_controller_spec.rb 1 \
'      post :create, params' \
'      post :create, params: params'
rewrite spec/controllers/twilio_controller_spec.rb 1 \
'      get :index, call_id: call.id' \
'      get :index, params: { call_id: call.id }'
rewrite spec/controllers/twilio_controller_spec.rb 2 \
'      get :index, call_id: 0' \
'      get :index, params: { call_id: 0 }'
rewrite spec/controllers/unsubscribes_controller_spec.rb 1 \
'        get :show, token: token' \
'        get :show, params: { token: token }'
rewrite spec/controllers/unsubscribes_controller_spec.rb 1 \
"        get :show, token: token, all: 'do-it'" \
"        get :show, params: { token: token, all: 'do-it' }"
rewrite spec/controllers/voice_answers_controller_spec.rb 1 \
'      get :index, params' \
'      get :index, params: params'
expect "calls now passing params"       17 "$(count 'params:' spec/controllers)"
expect "positional calls left"           2 "$(grep -rEn '^[[:space:]]*(get|post|put|patch|delete) :[a-z_]+,' spec | grep -vc 'params:')"

# The two assertions that named a validation the model no longer declares.
# The property is unchanged -- an Answer without a call is still invalid -- so
# the assertions stay and are stated against the message the framework uses for
# a missing required association.
rewrite spec/models/answer_spec.rb 1 \
'  it { should validate_presence_of(:call) }
  it { should validate_presence_of(:question) }' \
"  # Rails 5.0 requires a \`belongs_to\` by default, and the message it adds is
  # \`:required\` (\"must exist\") rather than \`:blank\`.  The model no longer
  # declares the two presence validations itself; the property they asserted is
  # unchanged and is asserted here against the message the framework now uses.
  it { should validate_presence_of(:call).with_message('must exist') }
  it { should validate_presence_of(:question).with_message('must exist') }"

# ---------------------------------------------------------------------------
say "install"
# ---------------------------------------------------------------------------
bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "5.0.7.2"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("2.4.")'
echo "solve.sh: done"
