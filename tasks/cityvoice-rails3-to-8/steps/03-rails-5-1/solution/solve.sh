#!/usr/bin/env bash
# Reference solution -- step 03, "reach Rails 5.1".
#
# The tree this runs against is step 02's finished tree -- Rails 5.0.7.2 on
# Ruby 2.4.10, models on ApplicationRecord, 43 versioned migrations, the five
# 5.0 framework defaults adopted in config/initializers/new_framework_defaults.rb.
# The first block below says so out loud: every count it takes is a fact about
# *step 02's* output, so running this against anything else stops on the first
# line instead of producing a tree that is neither.
#
# This is the first step that does not move the interpreter.  Ruby 2.4.10 is
# the interpreter before and after, and `.ruby-version` is not touched.
#
# Three artefacts too long to write inline -- the resolved Gemfile, its
# lockfile, and the schema dump Rails 5.1 produces from this application's own
# migrations -- ship as files beside this script.  Every edit states how many
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
expect "rails 5.0.7.2 in the Gemfile"     1 "$(count "gem 'rails', '5.0.7.2'" Gemfile)"
expect "ruby 2.4.10"                      1 "$(count '^2\.4\.10$' .ruby-version)"
expect "new_framework_defaults.rb"        1 "$(files config/initializers -name new_framework_defaults.rb)"
expect "no load_defaults yet"             0 "$(count 'load_defaults' config)"
expect "form_for call sites"              2 "$(count 'form_for(:subscription' app/views)"
expect "no form_with yet"                 0 "$(count 'form_with' app lib)"
expect "plaintext secrets.yml"            1 "$(files config -maxdepth 1 -name secrets.yml)"
expect "no encrypted secrets yet"         0 "$(files config -maxdepth 1 -name 'secrets.yml.*')"
expect "mailer actions take arguments"    2 "$(count '^  def [a-z_]*(' app/mailers/notification_mailer.rb)"
expect "String validation conditions"     2 "$(count "if: '" app/models)"
expect "schema tables"                    7 "$(count 'force: :cascade' db/schema.rb)"
expect "schema pins no primary key"       0 "$(count 'id: :serial' db/schema.rb)"

# ---------------------------------------------------------------------------
say "the framework"
# ---------------------------------------------------------------------------
# 5.1 is one minor release on from 5.0 and this application's pins do not
# stand in its way: `bundle update rails` against the step-02 Gemfile moves
# arel, erubis -> erubi, rack-test and the framework, and nothing else.  The
# resolved pair ships beside this script so the tree is reproducible without a
# resolver run.
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "rails pinned to 5.1"      1 "$(count "gem 'rails', '5.1.7'" Gemfile)"
expect "ruby directive unmoved"   1 "$(count "^ruby '2.4.10'" Gemfile)"
expect "framework locked at 5.1"  1 "$(count '^    rails (5\.1\.7)$' Gemfile.lock)"
bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "5.1.7"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("2.4.")'

# ---------------------------------------------------------------------------
say "framework defaults: one declaration instead of seven assignments"
# ---------------------------------------------------------------------------
# `config.load_defaults` arrives in 5.1 (railties 5.1.7
# lib/rails/application/configuration.rb:57; 5.0.7.2 has no such method) and is
# the framework's own replacement for the generated file step 02 filled in.
# The "5.1" branch is `load_defaults "5.0"` -- per-form CSRF tokens, the origin
# check, `to_time` preserving its receiver's zone, `belongs_to` required, HSTS
# with subdomains -- plus one setting of its own,
# `assets.unknown_asset_fallback = false`.  That is every line of the
# initializer except `raise_on_unfiltered_parameters`, which 5.1 dropped along
# with the deprecated behaviour it guarded.
#
# `read_encrypted_secrets` is set here rather than in
# config/environments/production.rb because all three environments read their
# secret_key_base from the same file, and from this step on that file is the
# encrypted one.
rewrite config/application.rb 1 \
'    config.eager_load_paths << Rails.root.join('"'"'lib'"'"')' \
'    # Rails 5.1 replaces the generated new_framework_defaults.rb with one
    # declaration of the release whose defaults the application takes.  "5.1"
    # is the five settings the 5.0 branch sets plus
    # assets.unknown_asset_fallback = false.
    config.load_defaults 5.1

    # The secrets live in config/secrets.yml.enc, decrypted with
    # config/secrets.yml.key (or ENV["RAILS_MASTER_KEY"]).  Set here rather
    # than in config/environments/production.rb because development and test
    # read their secret_key_base from the same file.
    config.read_encrypted_secrets = true

    config.eager_load_paths << Rails.root.join('"'"'lib'"'"')'
expect "load_defaults declared"       1 "$(count 'config.load_defaults 5\.1' config/application.rb)"
expect "encrypted secrets configured" 1 "$(count 'config.read_encrypted_secrets = true' config/application.rb)"

# The file exists to be deleted once its contents have been adopted; keeping it
# alongside the declaration means two places state the same thing.
rm config/initializers/new_framework_defaults.rb
expect "new_framework_defaults.rb gone" 0 "$(files config/initializers -name new_framework_defaults.rb)"

# ---------------------------------------------------------------------------
say "assets: the pipeline stops covering for names that were never assets"
# ---------------------------------------------------------------------------
# `assets.unknown_asset_fallback = false` is the one setting `load_defaults
# "5.1"` adds.  Until 5.0, sprockets-rails answered a request for a logical
# path it has no asset for with the public path instead
# (sprockets-rails-3.2.2 lib/sprockets/rails/helper.rb:76-97); from 5.1 the
# same call raises Sprockets::Rails::Helper::AssetNotFound.
#
# This application asks for audio by names that come out of its data -- a
# question's short name, an error's message -- and those files are served from
# app/assets/audios, not compiled from it.  5.1 gives the caller the explicit
# spelling for that, `skip_pipeline: true` (added in actionview 5.1; the
# constant does not exist in 5.0.7.2), so each call site asks the pipeline
# first and falls through to the public path itself.  Switching the fallback
# back on would answer the same URLs and give up the setting.
rewrite app/helpers/voice_feedback_helper.rb 1 \
'    audio_url("#{short_name}.mp3")' \
'    audio_url("#{short_name}.mp3")
  rescue Sprockets::Rails::Helper::AssetNotFound
    # Names that come from the data -- a question'"'"'s short name, an error'"'"'s
    # message -- have never been pipeline assets.  Up to Rails 5.0 the
    # pipeline quietly handed those to the public path; from 5.1 it raises
    # instead, so ask for the public path explicitly.
    audio_url("#{short_name}.mp3", skip_pipeline: true)'
rewrite lib/twilio_session_error.rb 1 \
'    ActionController::Base.helpers.audio_url("#{message}.mp3")' \
'    ActionController::Base.helpers.audio_url("#{message}.mp3")
  rescue Sprockets::Rails::Helper::AssetNotFound
    ActionController::Base.helpers.audio_url("#{message}.mp3", skip_pipeline: true)'
expect "audio lookups with a fallback" 2 "$(count 'skip_pipeline: true' app lib)"

# ---------------------------------------------------------------------------
say "forms: form_with"
# ---------------------------------------------------------------------------
# 5.1 soft-deprecates form_for and form_tag in favour of form_with, which
# unifies the model-backed and URL-backed builders.  `scope:` is form_with's
# spelling of the symbol form_for took positionally, and the two field ids are
# named explicitly because form_with does not generate them: the JavaScript
# that reads this form (app/assets/javascripts/cityvoice/, and the feature
# spec) addresses both fields by the id form_for used to emit.
#
# The remote default is form_with's own -- ActionView::Helpers::FormHelper sets
# `local: false` unless told otherwise -- and it is adopted: the <form> element
# gains data-remote="true", which is the one behaviour change this step makes
# on a served page.
rewrite app/views/locations/_email_notifications.html.erb 1 \
"  <%= form_for(:subscription, url: subscription_path, html: { class: 'row collapse' } ) do |f| %>
    <%= f.hidden_field :location_id, value: location.id %>" \
"  <%= form_with(scope: :subscription, url: subscription_path, class: 'row collapse') do |f| %>
    <%= f.hidden_field :location_id, value: location.id, id: 'subscription_location_id' %>"
rewrite app/views/locations/_email_notifications.html.erb 1 \
"      <%= f.text_field :email, placeholder: 'example@gmail.com' %>" \
"      <%= f.text_field :email, placeholder: 'example@gmail.com', id: 'subscription_email' %>"
rewrite app/views/locations/_subscribe_form.html.erb 1 \
"<%= form_for(:subscription, url: subscription_path) do |f| %>
  <%= f.hidden_field :location_id, value: @location.id %>
  <%= f.text_field :email, placeholder: 'example@gmail.com' %>" \
"<%= form_with(scope: :subscription, url: subscription_path) do |f| %>
  <%= f.hidden_field :location_id, value: @location.id, id: 'subscription_location_id' %>
  <%= f.text_field :email, placeholder: 'example@gmail.com', id: 'subscription_email' %>"
expect "form_with call sites"  2 "$(count 'form_with(scope: :subscription' app/views)"
expect "form_for call sites"   0 "$(count 'form_for' app lib)"

# ---------------------------------------------------------------------------
say "secrets: encrypted, with the key in the tree"
# ---------------------------------------------------------------------------
# Rails::Secrets arrives in railties 5.1 and reads config/secrets.yml.enc with
# the key from config/secrets.yml.key or ENV["RAILS_MASTER_KEY"]; the paths are
# widened to the glob `secrets.yml{,.enc}` in the same release
# (railties-5.1.7 lib/rails/application/configuration.rb:112).  The plaintext
# file is encrypted as it stands -- the same three environments, the same
# production line reading SECRET_TOKEN out of the environment -- so what the
# application reads out of its secrets does not change, only where from.
#
# The key ships in the tree, mode 0644.  That is not what a deployment would
# do, and the instruction says so: the tree is run as it stands by a harness
# with no secret store and as an unprivileged user, so a key that is absent or
# unreadable is a tree that cannot boot.
expect "secrets.yml is the only one"  1 "$(files config -maxdepth 1 -name 'secrets.yml*')"
bundle exec ruby -e '
  require "securerandom"
  require "openssl"
  require "fileutils"
  require "rails/secrets"
  File.write("config/secrets.yml.key", Rails::Secrets.generate_key + "\n")
  File.chmod(0644, "config/secrets.yml.key")
  Rails::Secrets.write(File.binread("config/secrets.yml"))
  File.chmod(0644, "config/secrets.yml.enc")
'
# Read it back in a second process, so the key comes off disk the way the
# application will take it, while the plaintext is still there to compare with.
bundle exec ruby -e '
  require "openssl"
  require "fileutils"
  require "rails/secrets"
  abort("secrets did not round-trip") unless Rails::Secrets.read == File.binread("config/secrets.yml")
  printf("   ok  %-46s %s\n", "secrets encrypted, read back with the key file",
         File.size("config/secrets.yml.enc"))
'
rm config/secrets.yml
expect "plaintext secrets gone"   0 "$(files config -maxdepth 1 -name secrets.yml)"
expect "encrypted secrets"        1 "$(files config -maxdepth 1 -name secrets.yml.enc)"
expect "key shipped"              1 "$(files config -maxdepth 1 -name secrets.yml.key)"

# ---------------------------------------------------------------------------
say "mailers: parameterization"
# ---------------------------------------------------------------------------
# ActionMailer::Base.with arrives in 5.1: the caller hands the mailer a hash,
# the action reads it out of `params`, and the action itself takes no
# positional argument.  The mail is unchanged -- same recipients, same
# subjects, same bodies -- so the three call sites move with the two actions
# and nothing else does.
rewrite app/mailers/notification_mailer.rb 1 \
'  def confirmation_email(location_subscription)
    @location_subscription = location_subscription
    mail(to: location_subscription.subscriber.email, subject: '"'"'Confirm to get notifications'"'"')' \
'  def confirmation_email
    @location_subscription = params[:location_subscription]
    mail(to: @location_subscription.subscriber.email, subject: '"'"'Confirm to get notifications'"'"')'
rewrite app/mailers/notification_mailer.rb 1 \
'  def weekly_activity(subscriber)
    @subscriber = subscriber
    @unsubscribe_all_token = subscriber.location_subscriptions.first.auth_token
    mail(to: subscriber.email, subject: '"'"'New Activity on CityVoice!'"'"')' \
'  def weekly_activity
    @subscriber = params[:subscriber]
    @unsubscribe_all_token = @subscriber.location_subscriptions.first.auth_token
    mail(to: @subscriber.email, subject: '"'"'New Activity on CityVoice!'"'"')'
expect "mailer actions take arguments" 0 "$(count '^  def [a-z_]*(' app/mailers/notification_mailer.rb)"

rewrite app/models/subscriber_notifier.rb 1 \
'    NotificationMailer.weekly_activity(subscriber).deliver_now' \
'    NotificationMailer.with(subscriber: subscriber).weekly_activity.deliver_now'
rewrite app/models/subscription_confirmer.rb 1 \
'    @confirmation_email ||= NotificationMailer.confirmation_email(location_subscription)' \
'    @confirmation_email ||= NotificationMailer.with(location_subscription: location_subscription).confirmation_email'
# The suite calls the mailer directly in two places; the spelling is the one
# this step changes, so the expectations stay and the calls move.
rewrite spec/mailers/notification_mailer_spec.rb 1 \
'    subject(:mail) { NotificationMailer.confirmation_email(location_subscription) }' \
'    subject(:mail) { NotificationMailer.with(location_subscription: location_subscription).confirmation_email }'
rewrite spec/mailers/notification_mailer_spec.rb 1 \
'    let(:mail) { NotificationMailer.weekly_activity(subscriber) }' \
'    let(:mail) { NotificationMailer.with(subscriber: subscriber).weekly_activity }'
expect "callers parameterized"  4 "$(count 'NotificationMailer.with(' app spec)"
expect "positional callers"     0 "$(count 'NotificationMailer.\(confirmation_email\|weekly_activity\)(' app spec)"

# ---------------------------------------------------------------------------
say "model conditions: not a String to be evaluated"
# ---------------------------------------------------------------------------
# 5.1 deprecates a String :if/:unless at the moment the class is defined, so
# this pair deprecates three times -- `validates :numerical_response` declares
# two validators -- before the application has served anything.  A lambda is
# evaluated in the same context and says the same thing.
rewrite app/models/answer.rb 1 \
"  validates :numerical_response, presence: true, inclusion: [1, 2], if: 'question.try(:numerical_response?)'
  validates :voice_file_url, presence: true, if: 'question.try(:voice_file?)'" \
'  validates :numerical_response, presence: true, inclusion: [1, 2], if: -> { question.try(:numerical_response?) }
  validates :voice_file_url, presence: true, if: -> { question.try(:voice_file?) }'
expect "String conditions left"  0 "$(count "if: '" app/models)"
expect "lambda conditions"       2 "$(count 'if: -> {' app/models)"

# ---------------------------------------------------------------------------
say "the schema dump pins the primary key the migrations build"
# ---------------------------------------------------------------------------
# From 5.1 a create_table that does not name its primary key gets a 64-bit one
# on PostgreSQL.  The 43 migrations are insulated: they are declared
# ActiveRecord::Migration[4.2], and the compatibility layer forces
# `id: :integer` on every create_table it replays (activerecord-5.1.7
# lib/active_record/migration/compatibility.rb, V5_0#create_table).  db/schema.rb
# declares no version and is not insulated, and it is the file nine of this
# application's ten recorded worlds, its test suite and its own Procfile
# release step build the database from.  Left as Rails 4.2 dumped it, the two
# paths build different databases: bigint ids from the dump, integer ids from
# the migrations, against foreign key columns declared t.integer.
#
# The fix is to re-dump on 5.1, which writes the key it finds.  The file below
# is that dump, taken from `rake db:migrate` over these 43 migrations under
# 5.1.7: the same seven tables at the same schema version, each one now naming
# `id: :serial`.
install -m 0644 "$FILES/schema.rb" db/schema.rb
expect "schema version"           1 "$(count 'ActiveRecord::Schema.define(version: 20140401151212)' db/schema.rb)"
expect "tables"                   7 "$(count 'force: :cascade' db/schema.rb)"
expect "tables pinning their key" 7 "$(count 'id: :serial, force: :cascade' db/schema.rb)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
