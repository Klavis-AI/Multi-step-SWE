#!/usr/bin/env bash
# Reference solution -- step 01, "reach Rails 4.2 on Ruby 2.3".
#
# Harbor uploads the whole `solution/` directory to /solution and runs this
# file, so the three artefacts that are too long to write inline -- the
# resolved Gemfile pair and the new config/secrets.yml -- ship as files beside
# it.  Shipping the lockfile rather than re-resolving keeps the oracle
# deterministic: `bundle lock` against a live index is a different answer on a
# different day, and an oracle whose gem versions move is an oracle whose score
# moves.
#
# Every edit below is asserted.  A `sed` that matched nothing exits 0 and
# prints exactly what a successful one prints, so each change states how many
# lines it expects to touch and stops if the file did not look the way it
# should.  (The same reason the verifier counts rather than greps.)
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

# insert_before <file> <anchor substring> -- place the block arriving on stdin
# immediately before the one line that contains <anchor substring>.  Substring
# and not regexp, and exactly one match or it stops: the same discipline as
# `expect` below, for the edits that add lines rather than change them.
insert_before() {
    ruby -e '
      path, anchor = ARGV
      block = STDIN.read
      lines = File.readlines(path)
      at = lines.each_index.select { |i| lines[i].include?(anchor) }
      unless at.size == 1
        abort("insert_before: #{path}: #{at.size} lines contain #{anchor.inspect}")
      end
      lines.insert(at.first, block)
      File.write(path, lines.join)
    ' "$1" "$2"
}

# replace_run <file> <substring> -- delete every line containing <substring>
# and put the block arriving on stdin where the first of them stood.  For the
# edit `insert_before` cannot express: a run of expectations that all go at
# once, replaced by one statement of what is left of them.  Zero matches is an
# error for the same reason a `sed` that matched nothing is.
replace_run() {
    ruby -e '
      path, needle = ARGV
      block = STDIN.read
      lines = File.readlines(path)
      at = lines.each_index.select { |i| lines[i].include?(needle) }
      abort("replace_run: #{path}: no line contains #{needle.inspect}") if at.empty?
      first = at.first
      kept = lines.each_with_index.reject { |_, i| at.include?(i) }.map(&:first)
      kept.insert(first, block)
      File.write(path, kept.join)
    ' "$1" "$2"
}

# expect <what> <want> <got>
expect() {
    if [ "$2" != "$3" ]; then
        echo "solve.sh: $1: expected $2, got $3" >&2
        exit 1
    fi
    printf '   ok  %-46s %s\n' "$1" "$3"
}

# ---------------------------------------------------------------------------
say "interpreter and dependencies"
# ---------------------------------------------------------------------------
# 2.1.7 is out of the picture: Rails 4.2 supports 2.3, and 2.3 is the newest
# interpreter 4.2 supports.  The version file and the Gemfile's `ruby`
# directive have to agree, or `bundle` refuses to run.
printf '2.3.8\n' > .ruby-version
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "ruby directive"          1 "$(count "^ruby '2.3.8'" Gemfile)"
expect "rails pinned to 4.2"     1 "$(count "gem 'rails', '4.2.11.3'" Gemfile)"
expect "protected_attributes gone" 0 "$(count "protected_attributes" Gemfile Gemfile.lock)"
expect "rails_email_preview gone"  0 "$(count "rails_email_preview" Gemfile Gemfile.lock)"
expect "compass-rails gone"        0 "$(count "compass-rails" Gemfile Gemfile.lock)"
expect "style-guide gone"          0 "$(count "^gem 'style-guide'" Gemfile)"

# ---------------------------------------------------------------------------
say "mass assignment: protected_attributes -> strong parameters"
# ---------------------------------------------------------------------------
# Every assignment site in this application is already either an explicit
# literal hash (`Caller.find_or_create_by(phone_number: ...)`), an association
# build, or a `params.require(...).permit(...)` -- SubscriptionController has
# used strong parameters since before this migration.  So the model-level
# declarations are the whole of the work: with the gem gone, `attr_accessible`
# is an undefined method and the class bodies raise at load.
before=$(count 'attr_accessible' app/models)
expect "attr_accessible before" 7 "$before"
sed -i -E '/^\s*attr_accessible\b/d' app/models/*.rb
# The blank line the declaration left behind is removed with it where it would
# otherwise double up; `cat -s` squeezes runs of blank lines to one.
for f in app/models/*.rb; do cat -s "$f" > "$f.tmp" && mv "$f.tmp" "$f"; done
expect "attr_accessible after" 0 "$(count 'attr_accessible' app/models app/controllers lib)"

# ---------------------------------------------------------------------------
say "mass assignment: the filtering attr_accessible also did"
# ---------------------------------------------------------------------------
# `attr_accessible` was a security control, and it was also -- incidentally --
# how the two CSV importers coped with a file whose columns they do not
# recognise.  `Location.new(row.to_hash)` used to be sanitized against the
# whitelist first, so an unknown column was dropped and `rake import:locations`
# went on to report "Name column is missing" and exit 0.  With
# protected_attributes gone, ActiveRecord assigns every key it is handed and
# raises ActiveRecord::UnknownAttributeError on the first one it has no writer
# for: the same file now ends the task with a backtrace and no diagnostics.
#
# That is a regression in the application's own recorded behaviour, not a
# framework semantic this step is adopting, so the filtering moves to the two
# places that mass-assign untrusted keys.  The lists are the ones the models
# already declared.
insert_before app/models/location.rb 'validates :name, presence: true' <<'RUBY'
  # The attributes an import may set.  `attr_accessible` used to say this, and
  # with it gone the filtering has to be explicit: strong parameters is the
  # Rails 4 replacement at the controller boundary, and this is the other
  # boundary -- a CSV whose columns came from outside the application.
  IMPORTABLE_ATTRIBUTES =
    [:name, :lat, :long, :description, :most_recent_activity].freeze

RUBY
insert_before app/models/question.rb 'validates_presence_of :short_name' <<'RUBY'
  # See Location::IMPORTABLE_ATTRIBUTES.
  IMPORTABLE_ATTRIBUTES = [:short_name, :feedback_type, :question_text].freeze

RUBY
expect "location whitelist"  1 "$(count 'IMPORTABLE_ATTRIBUTES =' app/models/location.rb)"
expect "question whitelist"  1 "$(count 'IMPORTABLE_ATTRIBUTES =' app/models/question.rb)"

# The two assignment sites.
ruby -e '
  edits = {
    "lib/location_importer.rb" => [
      "    csv_entries.map(&:to_hash).map { |p| Location.new(p) }\n",
      "    csv_entries.map(&:to_hash)\n" \
      "               .map { |p| Location.new(p.slice(*Location::IMPORTABLE_ATTRIBUTES)) }\n",
    ],
    "lib/question_presenter.rb" => [
      "    question.attributes = row\n",
      "    question.attributes = row.slice(*Question::IMPORTABLE_ATTRIBUTES)\n",
    ],
  }
  edits.each do |path, (from, to)|
    src = File.read(path)
    abort("#{path}: #{src.scan(from).size} matches for the assignment") unless src.scan(from).size == 1
    File.write(path, src.sub(from, to))
  end
'
expect "filtered assignment sites" 2 "$(count 'IMPORTABLE_ATTRIBUTES)' lib)"

# ---------------------------------------------------------------------------
say "controller callbacks: *_filter -> *_action"
# ---------------------------------------------------------------------------
# `before_action` has been the spelling since Rails 4.0; `before_filter` is
# deprecated from 5.0 and gone in 5.1.  Renaming here costs nothing and takes
# the tree out of the way of both.
before=$(count 'before_filter' app lib spec config)
expect "before_filter before" 9 "$before"
# Tree-wide, not just app/controllers: the Twilio controller spec declares the
# filter on an anonymous controller of its own, and a rename that stopped at
# app/ would leave the spelling in the tree for Rails 5.1 to remove under it.
sed -i -E 's/\bbefore_filter\b/before_action/g' $(grep -rl 'before_filter' app lib spec config)
expect "before_filter after" 0 "$(count '_filter\b' app lib spec config)"
# Ten, not nine: TwilioController already spelled its callback
# `skip_before_action`, so the tree arrives with one of these and leaves with
# the nine renamed ones beside it.
expect "before_action after" 10 "$(count '_action\b' app lib spec config)"

# ---------------------------------------------------------------------------
say "mail delivery: Mail::Message#deliver -> #deliver_now"
# ---------------------------------------------------------------------------
# Only the two calls whose receiver is a mailer message change.  The third
# `deliver` in this tree is SubscriberNotifier's own method -- the application
# defines it, Rails does not deprecate it, and renaming it would change this
# application's API for no reason.
sed -i 's/confirmation_email\.deliver$/confirmation_email.deliver_now/' \
    app/models/subscription_confirmer.rb
sed -i 's/NotificationMailer\.weekly_activity(subscriber)\.deliver$/NotificationMailer.weekly_activity(subscriber).deliver_now/' \
    app/models/subscriber_notifier.rb
expect "mailer deliver_now"   2 "$(count 'deliver_now' app/models)"
expect "own #deliver kept"    2 "$(count '\.deliver$\|def deliver' app/models/subscriber_notifier.rb)"

# ---------------------------------------------------------------------------
say "static files: serve_static_assets -> serve_static_files"
# ---------------------------------------------------------------------------
sed -i -E 's/\bconfig\.serve_static_assets\b/config.serve_static_files/' \
    config/environments/production.rb config/environments/test.rb
expect "serve_static_assets gone" 0 "$(count 'serve_static_assets' config)"
expect "serve_static_files"       2 "$(count 'serve_static_files' config)"

# ---------------------------------------------------------------------------
say "secrets: config/initializers/secret_token.rb -> config/secrets.yml"
# ---------------------------------------------------------------------------
# The production key is still read from SECRET_TOKEN, so a deployment that has
# one keeps signing its cookies with the same value.  The hand-written
# "SECRET_TOKEN is not set" guard goes: Rails 4.2 raises that itself when
# `secret_key_base` resolves empty in production.
install -m 0644 "$FILES/secrets.yml" config/secrets.yml
rm -f config/initializers/secret_token.rb
expect "secret_token.rb gone"  0 "$(ls config/initializers/secret_token.rb 2>/dev/null | wc -l | tr -d ' ')"
expect "secrets.yml production" 1 "$(count 'ENV\["SECRET_TOKEN"\]' config/secrets.yml)"
expect "secret_key_base assignment gone" 0 "$(count 'secret_key_base *=' config app lib)"

# ---------------------------------------------------------------------------
say "retired development engines: rails_email_preview, style-guide"
# ---------------------------------------------------------------------------
# Both were development-only engines behind `if defined?` guards, so the guards
# are already false once the gems are gone -- but a guard that can never be
# true is dead code that still names a dependency, and the previews and the
# style-guide views it points at are unreachable.
rm -f config/initializers/rails_email_preview.rb config/initializers/style_guide.rb
rm -rf app/mailer_previews app/views/style-guide
sed -i '/RailsEmailPreview::Engine/d;/StyleGuide::Engine/d' config/routes.rb
sed -i '/config\.style_guide\.paths/d' config/application.rb
expect "RailsEmailPreview references" 0 "$(count 'RailsEmailPreview' config app lib)"
expect "StyleGuide references"        0 "$(count 'StyleGuide' config app lib)"

# ---------------------------------------------------------------------------
say "stylesheets: compass mixins -> the CSS they expand to"
# ---------------------------------------------------------------------------
# compass-rails has no release that works with sass-rails 5, and the whole of
# this application's use of it is one `@import` and three transition mixins
# whose expansion is the plain property of the same name.
sed -i '/@import "compass\/css3\/transition";/d' \
    app/assets/stylesheets/application.css.scss
sed -i -E 's/@include transition-(property|duration|delay)\((.*)\);/transition-\1: \2;/' \
    app/assets/stylesheets/navigation/feedback.css.scss
expect "compass imports"  0 "$(count 'compass' app/assets/stylesheets)"
expect "transition mixins" 0 "$(count '@include transition-' app/assets/stylesheets)"
expect "transition properties" 3 "$(count '^\s*transition-' app/assets/stylesheets/navigation/feedback.css.scss)"

# ---------------------------------------------------------------------------
say "the inherited spec suite, on rspec-rails 3"
# ---------------------------------------------------------------------------
# rspec-rails 2 cannot load against Rails 4.2 (its gemspec caps activesupport
# below 4.2), so the suite moves to 3 with it.  Three things changed in that
# major release and this suite uses all three.
sed -i "/require 'rspec\/autorun'/d" spec/spec_helper.rb
sed -i "s|require 'rspec/rails'|require 'rspec/rails'\nrequire 'rspec/its'|" spec/spec_helper.rb
expect "rspec/autorun gone" 0 "$(count 'rspec/autorun' spec)"
expect "rspec/its required" 1 "$(count 'rspec/its' spec/spec_helper.rb)"

# rspec-rails 3 stopped inferring a spec's type from the directory it lives in.
# Without it the controller specs load as plain example groups and all nine die
# on `controller` before their first expectation -- "errors occurred outside of
# examples", which is what an inherited suite looks like when it is quietly not
# running at all.
sed -i '/^RSpec\.configure do |config|$/a\  # rspec-rails 3 no longer infers a spec type from its directory.\n  config.infer_spec_type_from_file_location!\n' spec/spec_helper.rb
expect "spec type inference" 1 "$(count 'infer_spec_type_from_file_location' spec/spec_helper.rb)"

# `be_true`/`be_false` asserted "not nil and not false"; rspec 3 renamed them
# to `be_truthy`/`be_falsey` and gave the old names the strict meaning.
sed -i -E 's/\bbe_true\b/be_truthy/g; s/\bbe_false\b/be_falsey/g' \
    $(grep -rl 'be_true\|be_false' spec)
expect "be_true/be_false" 0 "$(count '\bbe_true\b\|\bbe_false\b' spec)"

# `obj.stub(...)` is rspec-mocks' old syntax.
sed -i 's/Rails\.env\.stub(production?: true)/allow(Rails.env).to receive(:production?).and_return(true)/' \
    spec/lib/app_content_renderer_spec.rb
sed -i "s/ENV\.stub(:\[\] => 'meats')/allow(ENV).to receive(:[]).and_return('meats')/" \
    spec/controllers/application_controller_spec.rb
expect "old mock syntax" 0 "$(count '\.stub(' spec)"

# ffaker 2 dropped `Faker`, the top-level alias it had carried for `FFaker`
# since 1.x.  The Gemfile asks for `ffaker` with no constraint -- 1.24 was the
# lockfile's answer, never the Gemfile's -- so re-resolving moves it, and the
# factories are the one place in the tree that spells the old constant.
sed -i 's/\bFaker::/FFaker::/g' spec/factories.rb
expect "old Faker constant"  0 "$(count '\bFaker::' spec)"
expect "FFaker constant"    11 "$(count 'FFaker::' spec/factories.rb)"

# `have(n).things` was rspec-expectations' collection matcher.  rspec 3 removed
# it -- the team's own position is that `expect(x.size).to eq(n)` says the same
# thing without a DSL -- and left it behind in `rspec-collection_matchers` for
# suites that want it back.  Six expectations here use it, and each is rewritten
# to ask the collection for its size rather than to install a gem whose only
# purpose is to keep a matcher that was retired on purpose.  The subject and the
# number are untouched; only the spelling moves.
sed -i -E "s/its\(:([a-z_]+)\) \{ should have\(([0-9]+)\)\.[a-z_]+ \}/its('\1.size') { should eq(\2) }/" \
    spec/models/location_spec.rb
sed -i -E 's/expect\((.+)\)\.to have\(([0-9]+)\)\.[a-z_]+$/expect((\1).size).to eq(\2)/' \
    spec/controllers/locations_controller_spec.rb \
    spec/controllers/landing_controller_spec.rb \
    spec/controllers/voice_answers_controller_spec.rb
# `(` is a literal in a BRE and `\(` opens a group -- `have\([0-9]` matches
# `have1` and never `have(1`, which is a check that passes on the file it is
# meant to catch.
expect "collection matchers" 0 "$(count 'to have([0-9]\|should have([0-9]' spec)"
expect "size expectations"   6 "$(count '\.size).to eq(\|\.size.) { should eq(' spec)"

# `allow_mass_assignment_of` is shoulda-matchers' reader for `attr_accessible`:
# it asks the model for its `active_authorizer`, which is a method
# protected_attributes adds.  With that gem gone the matcher raises rather than
# fails, and the property it asserted is now true of every attribute of every
# model, so restating it name by name states nothing.  What the
# `attr_accessible` list meant survives in two halves, and both are written out
# per model in its place: the attributes are still settable from a hash, and
# the set an *untrusted* hash may set is now named where that hash arrives.
#
# Two of the twenty-five names turn out not to be attributes at all --
# `Caller#consented_to_callback` and `LocationSubscription#subscription` --
# which is its own small argument about the matcher: it asked a list of names,
# not the table.
replace_run spec/models/location_spec.rb allow_mass_assignment_of <<'SPEC'
  describe 'assignment from a hash' do
    let(:attributes) do
      { name: 'Elsewhere', lat: '1.5', long: '-2.5', description: 'somewhere',
        most_recent_activity: Time.utc(2016, 1, 19, 12) }
    end

    it 'sets every attribute the hash names' do
      assigned = Location.new(attributes)

      attributes.each do |attribute, value|
        expect(assigned.public_send(attribute)).to eq(value)
      end
    end

    it 'permits exactly those attributes on import' do
      expect(Location::IMPORTABLE_ATTRIBUTES).to match_array(attributes.keys)
    end
  end
SPEC

replace_run spec/models/question_spec.rb allow_mass_assignment_of <<'SPEC'
  describe 'assignment from a hash' do
    let(:attributes) do
      { short_name: 'extra_key', feedback_type: 'numerical_response',
        question_text: 'Does the extra key survive?' }
    end

    it 'sets every attribute the hash names' do
      assigned = Question.new(attributes)

      attributes.each do |attribute, value|
        expect(assigned.public_send(attribute)).to eq(value)
      end
    end

    it 'permits exactly those attributes on import' do
      expect(Question::IMPORTABLE_ATTRIBUTES).to match_array(attributes.keys)
    end
  end
SPEC

replace_run spec/models/call_spec.rb allow_mass_assignment_of <<'SPEC'
  describe 'assignment from a hash' do
    let(:attributes) do
      { caller: create(:caller), location: create(:location),
        source: '+15555550123', consented_to_callback: true }
    end

    it 'sets every attribute the hash names' do
      assigned = Call.new(attributes)

      attributes.each do |attribute, value|
        expect(assigned.public_send(attribute)).to eq(value)
      end
    end
  end
SPEC

replace_run spec/models/answer_spec.rb allow_mass_assignment_of <<'SPEC'
  describe 'assignment from a hash' do
    let(:attributes) do
      { call: create(:call), question: create(:question),
        voice_file_url: 'http://example.com/answer.mp3',
        numerical_response: 3 }
    end

    it 'sets every attribute the hash names' do
      assigned = Answer.new(attributes)

      attributes.each do |attribute, value|
        expect(assigned.public_send(attribute)).to eq(value)
      end
    end
  end
SPEC

replace_run spec/models/caller_spec.rb allow_mass_assignment_of <<'SPEC'
  # Only one of the two names the matcher listed here is an attribute:
  # `consented_to_callback` is a column on `calls`, not on `callers`.
  describe 'assignment from a hash' do
    it 'sets the phone number' do
      expect(Caller.new(phone_number: '+15555550123').phone_number)
        .to eq('+15555550123')
    end
  end
SPEC

replace_run spec/models/subscriber_spec.rb allow_mass_assignment_of <<'SPEC'
  describe 'assignment from a hash' do
    it 'sets the email address' do
      expect(Subscriber.new(email: 'user@example.com').email)
        .to eq('user@example.com')
    end
  end
SPEC

replace_run spec/models/location_subscription_spec.rb allow_mass_assignment_of <<'SPEC'
  # `subscription` is not an attribute of this model -- the association is
  # `subscriber` -- and the old matcher passed for it regardless.
  describe 'assignment from a hash' do
    let(:attributes) do
      { confirmed: true, bulk_added: true,
        last_email_sent_at: Time.utc(2016, 1, 19, 12),
        confirmation_sent_at: Time.utc(2016, 1, 19, 11),
        location: create(:location) }
    end

    it 'sets every attribute the hash names' do
      assigned = LocationSubscription.new(attributes)

      attributes.each do |attribute, value|
        expect(assigned.public_send(attribute)).to eq(value)
      end
    end
  end
SPEC

expect "mass-assignment matchers" 0 "$(count 'allow_mass_assignment_of' spec)"
expect "assignment blocks"        7 "$(count 'assignment from a hash' spec)"

# ---------------------------------------------------------------------------
say "install"
# ---------------------------------------------------------------------------
bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "4.2.11.3"'
echo "solve.sh: done"
