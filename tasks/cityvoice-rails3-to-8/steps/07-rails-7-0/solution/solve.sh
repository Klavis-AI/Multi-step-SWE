#!/usr/bin/env bash
# Reference solution -- step 07, "reach Rails 7.0".
#
# The tree this runs against is step 06's finished tree -- Rails 6.1.7.10 on
# Ruby 3.0.7, `config.load_defaults 6.1`, three preloaded collection pages with
# `strict_loading` markers on them, credentials and a master key, a host
# allow-list in production.  The first block below says so out loud: every
# count it takes is a fact about *step 06's* output, so running this against
# anything else stops on the first line instead of producing a tree that is
# neither.
#
# Two pins move, and for the last time on this ladder: Rails 7.0 runs on the
# Ruby 3.0 this tree arrives with and is the last release that does, while the
# next rung's 7.1 will not take anything below 3.1.  What is unusual about this
# rung is what the pins cost, which is nothing: the resolution names no new
# floor, and on the two pins alone the tree boots in all three environments and
# runs its whole suite green.  That is the reason the step's substance comes
# from what 7.0 OFFERS rather than from what it breaks.
#
# Four pieces of work, in the order this file does them:
#
#   1. `load_defaults 7.0`, which breaks exactly one thing here -- the two
#      views that call `Date#to_s(:year_month_date)`.  7.0 deprecates that
#      spelling before the defaults are adopted and removes the argument once
#      they are, so the deprecation and the removal are one switch apart.
#   2. Active Record encryption on `subscribers.email`: deterministic, because
#      the application looks that column up by value in two places;
#      case-folding, because the uniqueness validator has always been
#      case-insensitive there; and on a column wide enough for the ciphertext,
#      which is what the ladder's first new migration is for.
#   3. The keys for it, in the credentials this tree has carried since step 04
#      rather than in the environment.
#   4. `load_async` on the three independent queries /export.csv builds, with
#      the executor that makes the call mean something.
#
# Three artefacts too long to write inline -- the resolved Gemfile, its
# lockfile, and the schema dump `rake db:migrate` produces -- ship as files
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
expect "rails 6.1.7.10 in the Gemfile"    1 "$(count "gem 'rails', '6.1.7.10'" Gemfile)"
expect "ruby 3.0.7"                       1 "$(count '^3\.0\.7$' .ruby-version)"
expect "load_defaults 6.1"                1 "$(count 'config.load_defaults 6\.1' config/application.rb)"
expect "credentials, since step 04"       1 "$(files config -maxdepth 1 -name credentials.yml.enc)"
expect "and the key that opens them"      1 "$(files config -maxdepth 1 -name master.key)"
expect "the 43 migrations it shipped"    43 "$(files db/migrate -name '*.rb')"
expect "email is a string column"         1 "$(count 't.string "email"' db/schema.rb)"
expect "the address looked up by value"   1 "$(count 'find_or_create_by!' app/controllers/subscription_controller.rb)"
expect "and validated case-insensitively" 1 "$(count 'case_sensitive: false' app/models/subscriber.rb)"
expect "nothing encrypted yet"            0 "$(count 'encrypts ' app/models)"
expect "the two to_s(:...) views"         2 "$(count 'to_s(:year_month_date)' app/views)"
expect "the export relation preloaded"    1 "$(count 'includes(:location, :answers)' app/controllers/numerical_answers_controller.rb)"
expect "no relation loaded async yet"     0 "$(count 'load_async' app)"

# ---------------------------------------------------------------------------
say "the framework, the interpreter, and no floor at all"
# ---------------------------------------------------------------------------
# `bundle lock --conservative --update rails` against the two pins -- not
# `bundle update`, which floats seventy-odd gems -- moves fifteen gems and adds
# four.  Thirteen of the fifteen are the framework's own list; annotate and
# net-imap are the other two; the four additions are libraries activesupport
# 7.0 now names because Ruby is moving them out of the standard library.  Not
# one floor had to be named, which has not been true of any other rung: steps
# 02 through 06 each found between two and six gems that resolve cleanly and
# then fail at runtime.
#
# Three files have to name the same interpreter, for the reason step 05 gave:
# rbenv reads .ruby-version, Bundler reads the `ruby` directive, and `bundle
# install` refuses a lockfile whose RUBY VERSION the running interpreter does
# not satisfy.  Two out of three agreeing does not stop anything; it just runs
# the application on an interpreter no one chose.
printf '3.1.7\n' > .ruby-version
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "the interpreter the tree selects" 1 "$(count '^3\.1\.7$' .ruby-version)"
expect "the same one in the Gemfile"      1 "$(count "^ruby '3.1.7'" Gemfile)"
expect "and in the lockfile"              1 "$(count '^   ruby 3\.1\.7p' Gemfile.lock)"
expect "rails pinned to 7.0"              1 "$(count "gem 'rails', '7.0.10'" Gemfile)"
expect "framework locked at 7.0"          1 "$(count '^    rails (7\.0\.10)$' Gemfile.lock)"
bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "7.0.10"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("3.1.")'

# ---------------------------------------------------------------------------
say "framework defaults: 6.1 becomes 7.0, and the async executor beside it"
# ---------------------------------------------------------------------------
# `load_defaults "7.0"` is the 6.1 branch plus nineteen settings of its own
# (railties-7.0.10 lib/rails/application/configuration.rb, the `when "7.0"`
# branch).  Fourteen of them mean something to this application; the other five
# configure Active Storage, which this application does not use.  Read on a
# booted production process, at the place each is really consulted, 6.1 -> 7.0:
#
#   ActiveSupport::Digest.hash_digest_class       SHA1  -> SHA256
#   key_generator_hash_digest_class               SHA1  -> SHA256
#   ActiveSupport.cache_format_version            6.1   -> 7.0
#   Digest::UUID.use_rfc4122_namespaced_uuids     false -> true
#   ActiveSupport::TimeWithZone.name              "Time" -> its own name
#   TimeWithZone#to_s arity                       -1    -> 0
#   UrlHelper.button_to_generates_button_tag      false -> true
#   AssetTagHelper.apply_stylesheet_media_default true  -> false
#   ActiveRecord::Base.automatic_scope_inversing  false -> true
#   ActiveRecord::Base.partial_inserts            true  -> false
#   verify_foreign_keys_for_fixtures              false -> true
#   ActionController::Base.raise_on_open_redirects false -> true
#   wrap_parameters_by_default                    false -> true
#   default_headers["X-XSS-Protection"]           "1; mode=block" -> "0"
#
# Two of the fourteen are read through their effect rather than off a flag,
# because 7.0 keeps no flag for either: `disable_to_s_conversion` shows in the
# ARITY of `TimeWithZone#to_s` and `remove_deprecated_time_with_zone_name` in
# what `TimeWithZone.name` answers.
#
# The executor is not part of `load_defaults` and is the other half of the
# `load_async` work below.  `async_query_executor` is nil out of the box on
# 7.0, and a relation that calls `load_async` with it nil does not raise and
# does not warn: it loads the relation inline and `scheduled?` stays false.  So
# the setting and the call site are one requirement written in two files, and
# neither of them does anything alone.  Four threads because /export.csv builds
# three independent relations and the pool is process-wide.
rewrite config/application.rb 1 \
'    # One declaration of the release whose defaults the application takes.
    # "6.1" is the 6.0 branch plus `has_many` associations that hand the parent
    # back to each child, the new Active Record connection handling in place of
    # the legacy one, `form_with` generating ordinary rather than remote forms,
    # a `Link: rel=preload` header on the assets a page needs, url-safe CSRF
    # tokens, cookies written with `SameSite=Lax`, and `Time#to_time` keeping
    # its own offset instead of converting to the system zone.
    config.load_defaults 6.1' \
'    # One declaration of the release whose defaults the application takes.
    # "7.0" is the 6.1 branch plus SHA256 where the digests were SHA1, the 7.0
    # cache entry format, RFC 4122 namespaced UUIDs, a `TimeWithZone` that
    # answers its own name and a `to_s` that takes no format argument,
    # `button_to` generating a <button>, stylesheet tags without `media="all"`,
    # scopes Active Record can invert on its own, partial inserts off,
    # foreign keys verified for fixtures, open redirects raising rather than
    # redirecting, parameter wrapping on by default, and `X-XSS-Protection: 0`.
    config.load_defaults 7.0

    # The other half of the `load_async` call sites in
    # NumericalAnswersController#export.  With no executor configured,
    # `Relation#load_async` loads the relation inline and reports
    # `scheduled? == false`; with one, the query is handed to this pool and the
    # thread that asked for it carries on.  Process-wide rather than
    # per-connection, and four threads for the three relations that page builds.
    config.active_record.async_query_executor = :global_thread_pool
    config.active_record.global_executor_concurrency = 4'
expect "load_defaults declared"   1 "$(count 'config.load_defaults 7\.0' config/application.rb)"
expect "the 6.1 declaration gone" 0 "$(count 'config.load_defaults 6\.1' config/application.rb)"
expect "an executor to run on"    1 "$(count 'async_query_executor = :global_thread_pool' config/application.rb)"
expect "and a size for it"        1 "$(count 'global_executor_concurrency = 4' config/application.rb)"

# ---------------------------------------------------------------------------
say "the one thing load_defaults 7.0 breaks"
# ---------------------------------------------------------------------------
# `active_support.disable_to_s_conversion = true` makes `TimeWithZone#to_s`
# take no argument at all, and two views pass it one.  Before the defaults are
# adopted 7.0 deprecates the same call instead of breaking it, so the tree that
# arrives here prints ten
#   DEPRECATION WARNING: TimeWithZone#to_s(:year_month_date) is deprecated.
#   Please use TimeWithZone#to_fs(:year_month_date) instead.
# and the tree that adopts them fails the same ten examples with
#   ArgumentError: wrong number of arguments (given 1, expected 0)
# out of active_support/time_with_zone.rb:210.
#
# `to_fs` is the rename; `to_formatted_s`, which a third view and the three
# specs that assert these dates already use, is the same method under the name
# it has always also had.  `Time::DATE_FORMATS[:year_month_date]` is set in
# config/initializers/time_formats.rb and does not move.
rewrite app/views/landing/_answer.html.erb 1 \
'    <small><%= answer.created_at.to_s(:year_month_date) %></small>' \
'    <small><%= answer.created_at.to_fs(:year_month_date) %></small>'
rewrite app/views/locations/_voice_message.html.erb 1 \
'    <small>date:</small> <%= answer.created_at.to_s(:year_month_date) %>' \
'    <small>date:</small> <%= answer.created_at.to_fs(:year_month_date) %>'
expect "views formatting the old way" 0 "$(count 'to_s(:year_month_date)' app/views)"
expect "views formatting the new way" 2 "$(count 'to_fs(:year_month_date)' app/views)"

# ---------------------------------------------------------------------------
say "encryption keys, in the credentials rather than the environment"
# ---------------------------------------------------------------------------
# activerecord-7.0.10 lib/active_record/railtie.rb:330-335 reads
# `credentials.active_record_encryption.{primary_key,deterministic_key,
# key_derivation_salt}`.  This tree has carried `config/credentials.yml.enc`
# and `config/master.key` since step 04, so the three keys go where step 04 put
# the signing key and nowhere else.
#
# `ENV` would work identically and is the wrong answer.  `no_plaintext_secrets`
# has guarded this tree since step 04 for the reason that applies here twice
# over: a key in the process environment is in every `ps` on the box and in
# whatever the platform logs at deploy, and an encryption key sitting beside
# the ciphertext it opens is the whole of the protection undone.
#
# The file is REWRITTEN IN PLACE rather than shipped beside this script.
# Step 04's solution writes `config/master.key` with
# `ActiveSupport::EncryptedFile.generate_key`, which is RANDOM PER RUN, and a
# ciphertext baked from one run's key opens with that key and no other.  When
# the solutions are applied one after another to a fresh clone (as
# `harbor run --agent oracle` does), step 04 makes a different key, and a
# shipped file would make step 07's first boot die in
# `ActiveSupport::MessageEncryptor::InvalidMessage` inside
# activerecord railtie.rb:332, before anything this step does is visible.
#
# So: open the tree's own credentials with the tree's own key, APPEND the
# `active_record_encryption` mapping as text, and write it back through the
# same `EncryptedFile`, which re-encrypts with that key.  Appending rather
# than `YAML.dump`ing the parsed hash keeps step 04's four comment lines and
# its `secret_key_base` byte-for-byte, which is also the cheapest way to be
# sure the rewrite added a key and changed nothing else.
#
# The three key values are fixed rather than generated, and they are the ones
# the Rails guide prints.  Deterministic encryption means the key decides the
# ciphertext, so a generated key would make `subscribers.email` a different
# 122 bytes on every run, and fixed keys keep the reference tree
# reproducible.  A real deployment generates them.
bundle exec ruby -e '
  require "active_support"
  require "active_support/encrypted_file"
  require "yaml"
  file = ActiveSupport::EncryptedFile.new(
    content_path: "config/credentials.yml.enc", key_path: "config/master.key",
    env_key: "RAILS_MASTER_KEY", raise_if_missing_key: true)
  before = file.read
  abort("credentials already carry encryption keys") if before.include?("active_record_encryption")
  secret = YAML.safe_load(before)["secret_key_base"].to_s
  abort("no secret_key_base to preserve") unless secret.length >= 64
  file.write(before.sub(/\n*\z/, "\n") + <<~YAML)
    active_record_encryption:
      primary_key: EGY8WhulUOXixybod7ZWwMIL68R9o5kC
      deterministic_key: aPA5XyALhf75NNnMzaspW7akTfZp0lPY
      key_derivation_salt: xEY0dt6TZcAMg52K7O84wYzkjvbA62Hz
  YAML
  after = file.read
  creds = YAML.safe_load(after)
  keys = creds.fetch("active_record_encryption", {}).keys.sort
  want = %w[deterministic_key key_derivation_salt primary_key]
  abort("credentials carry #{keys.inspect}, wanted #{want.inspect}") unless keys == want
  abort("secret_key_base did not survive") unless creds["secret_key_base"].to_s == secret
  abort("the rewrite changed more than it added") unless after.start_with?(before.sub(/\n*\z/, "\n"))
  creds["active_record_encryption"].each do |name, value|
    abort("#{name} is #{value.to_s.length} characters") unless value.to_s.length >= 32
  end
  puts "   ok  three keys added to the credentials step 04 wrote, secret_key_base intact"
'
expect "no key set in the tree"       0 "$(count 'active_record.encryption' app config)"
expect "and none in sample.env"       0 "$(count 'ENCRYPTION' sample.env)"

# ---------------------------------------------------------------------------
say "encryption on the column the application looks up by value"
# ---------------------------------------------------------------------------
# `deterministic: true` is not a preference here.  `SubscriptionController`
# does `Subscriber.find_or_create_by!(email: ...)` and the model validates the
# uniqueness of the same column; both are `WHERE email = $1`.  Under the
# non-deterministic default every ciphertext differs, so both stop matching --
# and they stop matching SILENTLY: no error, just a lookup that never finds and
# a new subscriber row every time somebody subscribes.
#
# `downcase: true` is what replaces `case_sensitive: false`, and the validation
# has to be rewritten at the same time rather than left alone.  Left alone it
# emits, against an encrypted column,
#     SELECT 1 AS one FROM "subscribers" WHERE LOWER("subscribers"."email") = LOWER($1)
# which PASSES -- both sides are mangled the same way -- while lower-casing
# base64, refusing every index, and colliding for two ciphertexts that differ
# only in the case of their encoding.  With the case folded before encryption
# the predicate becomes `WHERE "subscribers"."email" = $1` and the column keeps
# the case-insensitive identity it has had since the application was written.
rewrite app/models/subscriber.rb 1 \
'  has_many :location_subscriptions' \
'  has_many :location_subscriptions

  # Deterministic because two call sites look this column up by value, and
  # downcasing because the uniqueness this column has always had is
  # case-insensitive -- folded here, before encryption, rather than by a
  # `LOWER()` the database can no longer make sense of.
  encrypts :email, deterministic: true, downcase: true'
rewrite app/models/subscriber.rb 1 \
'  validates :email, uniqueness: { case_sensitive: false }' \
'  validates :email, uniqueness: true'
expect "the column encrypted"            1 "$(count 'encrypts :email, deterministic: true, downcase: true' app/models/subscriber.rb)"
expect "the LOWER() validation gone"     0 "$(count 'case_sensitive' app/models/subscriber.rb)"
expect "uniqueness still validated"      1 "$(count 'validates :email, uniqueness: true' app/models/subscriber.rb)"

# ---------------------------------------------------------------------------
say "the column has to be wide enough for the ciphertext"
# ---------------------------------------------------------------------------
# `subscribers.email` has been `varchar(255)` since the 2013 migration that
# made it.  Encrypted, the stored value is base64 JSON: measured on this
# application at about 1.33 bytes per plaintext byte plus a fixed 73, so
#     a@b.co (6)  -> 78        user@example.com (16) -> 94
#     someone.with.a.long.name+tagged@subdomain.example.org (53) -> 142
#     ...(135) -> 250          ...(201) -> 283
# and a legal address over about 137 characters stops fitting.  That is not a
# hypothetical: the format validation this model carries accepts them, so the
# migration turns an address the application took yesterday into an
# `ActiveRecord::ValueTooLong` today.
#
# This is the first file this ladder adds to db/migrate, and it declares
# `ActiveRecord::Migration[7.0]` because that is the release it was written
# against -- the same rule that keeps the other 43 declaring `[4.2]`.  The
# timestamp is the one `bin/rails generate migration` produces in this
# application's world.
cat > db/migrate/20160119120000_widen_subscribers_email.rb <<'RUBY'
class WidenSubscribersEmail < ActiveRecord::Migration[7.0]
  def change
    change_column :subscribers, :email, :text
  end
end
RUBY
expect "one new migration"          44 "$(files db/migrate -name '*.rb')"
expect "written against 7.0"         1 "$(count 'ActiveRecord::Migration\[7\.0\]' db/migrate)"
expect "the 43 others untouched"    43 "$(count 'ActiveRecord::Migration\[4\.2\]' db/migrate)"

# db/schema.rb is a committed file and `rake db:migrate` re-dumps it, so the
# dump has to move with the migration -- and it is `db:schema:load`, not the
# migrations, that builds the database everything downstream runs against.  The
# file below is that dump, taken from `rake db:migrate` over these 44
# migrations under 7.0.10: the same seven tables, `subscribers.email` now
# `t.text`, the schema version at the new migration, and the header Rails 7.0's
# dumper writes in place of the 4.2 one.
install -m 0644 "$FILES/schema.rb" db/schema.rb
expect "the column widened in the dump" 1 "$(count 't.text "email"' db/schema.rb)"
expect "no string column left"          0 "$(count 't.string "email"' db/schema.rb)"
expect "the dump at the new version"    1 "$(count 'ActiveRecord::Schema\[7\.0\].define(version: 2016_01_19_120000)' db/schema.rb)"
expect "the seven tables"               7 "$(count 'force: :cascade' db/schema.rb)"
expect "each still pinning its key"     7 "$(count 'id: :serial, force: :cascade' db/schema.rb)"

# ---------------------------------------------------------------------------
say "load_async on the three queries /export.csv does not need in order"
# ---------------------------------------------------------------------------
# The export builds three relations that do not depend on one another -- the
# numerical questions, the voice question, and every call that has a location
# with its location and its answers -- and then walks them.  Until 7.0 the only
# way to overlap them was to run them in threads the application manages
# itself.  `Relation#load_async` hands the query to the executor configured in
# config/application.rb and returns immediately; the relation blocks on first
# use, so the two question lists are already in flight while the calls query is
# being sent.
#
# The three declarations move to the top of the method for that reason.  Where
# they were, each relation was consumed on the line after it was built, so
# there was nothing for the executor to overlap: `load_async` on a relation
# that is iterated immediately is `load` with extra steps.
#
# `strict_loading` stays where step 06 put it and `includes` stays where step
# 06 put it.  All three compose: the marked, preloaded relation is scheduled,
# every record it returns still answers `strict_loading?`, and the page still
# costs five queries rather than one per row.
rewrite app/controllers/numerical_answers_controller.rb 1 \
'    data_table = Array.new
    header_row = Array.new
    header_row << "Call ID"
    header_row << "Location"
    numerical_questions = Question.numerical.order(:id)
    numerical_questions.each do |question|
      header_row << question.question_text
    end
    voice_questions = Question.where(feedback_type: "voice_file")
    voice_questions.each do |question|
      header_row << question.question_text
    end
    data_table << header_row
    calls = Call.where.not(location_id: nil)
                .includes(:location, :answers)
                .strict_loading
                .order(:id)' \
'    # Three queries this page does not need in any particular order, sent
    # before any of them is read.  `load_async` needs the executor in
    # config/application.rb to do anything at all: without it the relation
    # loads inline and `scheduled?` stays false, with no error and no warning.
    numerical_questions = Question.numerical.order(:id).load_async
    voice_questions = Question.where(feedback_type: "voice_file").load_async
    calls = Call.where.not(location_id: nil)
                .includes(:location, :answers)
                .strict_loading
                .order(:id)
                .load_async

    data_table = Array.new
    header_row = Array.new
    header_row << "Call ID"
    header_row << "Location"
    numerical_questions.each do |question|
      header_row << question.question_text
    end
    voice_questions.each do |question|
      header_row << question.question_text
    end
    data_table << header_row'
expect "relations loaded async"      3 "$(count '\.load_async$' app/controllers/numerical_answers_controller.rb)"
expect "the preload still declared"  1 "$(count 'includes(:location, :answers)' app/controllers/numerical_answers_controller.rb)"
expect "the markers still declared"  3 "$(count 'strict_loading' app)"
expect "nothing else loads async"    0 "$(count 'load_async' app/models app/views)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
