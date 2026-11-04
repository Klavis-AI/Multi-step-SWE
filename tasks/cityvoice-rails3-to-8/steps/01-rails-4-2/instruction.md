# Step 1 — Rails 4.0 → 4.2 on Ruby 2.3

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application running on Rails 4.0.0 under Ruby
2.1.7. Move it to **Rails 4.2 on Ruby 2.3**, and leave it behaving as it does
today.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Ruby 2.3 and on Rails 4.2.
- The Gemfile declares the Ruby the application runs on, `.ruby-version` names
  that same version, and `Gemfile.lock` records that same Ruby alongside the
  resolved gem set.
- The whole dependency set installs on Ruby 2.3 — not just the framework gems.
  Versions currently pinned in the lockfile that cannot be built or loaded on
  the newer interpreter have moved with it.
- The application boots in the `production` environment with eager loading on,
  and every file under the eager-load paths loads without raising.
- The application also boots in the `development` and `test` environments,
  each one in a process of its own, and each file under `config/environments/`
  configures the release the application now runs on: a setting that release
  renamed or dropped, or a middleware entry its default stack no longer
  carries, is not still being named there.

**Configuration.**

- `config/secrets.yml` supplies `secret_key_base` for each environment — the
  mechanism Rails 4.1 introduced and 4.2 reads.
- The secret the application boots with is the one that file supplies.
- The Rails 3 initializer that assigned `config.secret_key_base` by hand is
  gone, and nothing anywhere else in the tree assigns `secret_key_base`.
- `config/database.yml` is not in the repository, and the tree you hand back
  does not add it; the configuration is produced from
  `config/database.yml.example`, which must keep working as it stands against a
  local PostgreSQL server. Copying the example into place to run the
  application while you work is expected — that copy is a working file, not
  part of the tree you hand on.

**Mass assignment.**

- `attr_accessible` and `attr_protected` appear nowhere in the tree: no
  model-level mass-assignment API remains.
- The `protected_attributes` gem is not in the bundle — the shim that keeps
  that API working on Rails 4 is not a way to satisfy the line above.
- Attributes that arrive from outside the application are still filtered before
  a model is assigned from them, and the models keep accepting the same
  attributes from the same entry points they accept today.
- The CSV importers keep both of the header behaviours they have today, and the
  two are not the same behaviour. A header naming a column the model does not
  have is *ignored*: the row is imported without it, the task says nothing about
  that header and finishes successfully. A *required* header that is absent is
  still *reported*: the importer prints the same `... column is missing` line it
  prints today, and then reports each row that fails validation without it.
  Neither case raises, and neither aborts the task.

**Controllers.**

- Controller filters use the `*_action` spelling; no member of the `*_filter`
  family — `before_filter`, `skip_before_filter`, `around_filter` and the rest
  — appears anywhere in the tree.

**Deprecations.**

- Driving the application raises no deprecation warning that comes from the
  application's own code. Warnings raised from inside a third-party gem's own
  source are out of scope; anything the application itself triggers is in
  scope. This includes warnings raised from the application's own
  configuration, and booting it in `development` or `test` raises none from
  a file under `config/environments/` either. While the application is driven, a deprecation a gem raises while the application's own code is beneath it on the stack —
  the production boot's `require` of that gem, or a call the application
  made into it — belongs to that line of the application and is in scope;
  only what a gem raises with no application code beneath it is the gem's
  own.

This is about what the tree *raises*, not about what it prints. Turning the
warnings off — `config.active_support.deprecation = :silence`,
`ActiveSupport::Deprecation.silenced = true`, a `silence` block around the
call — leaves the deprecated call exactly where it was, and does not satisfy
it.

**Dependencies.**

- `rails_email_preview`, `compass-rails` and `style-guide` are gone from the
  Gemfile and the lockfile. None of them has a release that works here, and the
  first of them is what pins the framework below 4.2.
- Whatever in the tree exists only to serve them is gone with them: no
  reference to the `RailsEmailPreview` or `StyleGuide` constants is left in a
  route, an initializer or anywhere else.

**Integrity.** All three of these must hold of the tree:

- Every Ruby and ERB file in the tree parses.
- The application is still there: the migration removes what the framework
  removed, not the application's own code.
- No comment anywhere directs a tool to ignore, skip or suppress a finding. A
  requirement above is about the property, not about what some tool reports;
  silencing the report does not change the property.

**Behaviour that must not change.** The application's observable behaviour on
its own surfaces — the HTTP endpoints it serves, the Twilio webhooks it
answers, the rake tasks it ships (including the CSV importers and the migration
tasks), the mail it sends — is unchanged, with four exceptions. These four are
changes Rails itself made between 4.0 and 4.2; adopt them, and do not write
code to restore the old answers:

1. **Content negotiation drops unregistered media types.** Since the 4.2.11.1
   security release (CVE-2019-5418), the formats negotiated from an `Accept`
   header are filtered to the media types the application has registered, plus
   `*/*`; the 4.2 release the tree runs is one that has it. An `Accept`
   header naming only unregistered types therefore contributes no format at
   all, and the request is answered by whichever template the action can find
   — rather than failing because no template matched the type that was asked
   for.
2. **`t.string` has no implicit length limit on PostgreSQL.** Rails 4.2 stopped
   giving `string` columns a default `limit: 255` on this adapter, so a value
   the 4.0 schema rejected at the database is now stored as given.
3. **What follows from that in the application's own output.** A record the 4.0
   schema rejected and 4.2 stores appears in the locations index rendered as
   JSON, which `config/routes.rb` serves at
   `resources :locations, only: [:index, :show]`.
4. **`db:migrate:status` aborts on an unmigrated database.** Where 4.0 printed a
   notice and exited successfully, 4.2 aborts: the task exits non-zero and no
   longer prints the notice, or a status table, on standard output.

Nothing else about the application's behaviour is up for renegotiation. Where a
release changed a default in a way that would alter behaviour you are asked to
preserve, the old behaviour still holds.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 4.2" means and every change listed above:

- Ruby on Rails 4.1 release notes — https://guides.rubyonrails.org/4_1_release_notes.html
- Ruby on Rails 4.2 release notes — https://guides.rubyonrails.org/4_2_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Each gem's own source and CHANGELOG, as installed under your bundle path.

**In the repository.** `README.md` and `code-base-overview.md` describe what the
application does and how it is run; `Procfile` gives the web command;
`config/database.yml.example`, `sample.env` and `app.json` describe the
configuration it expects; `spec/` is the application's own test suite.

## 3. Meta requirements

- The implementation budget for this step is 7,200 seconds. Check
  `bash /usr/local/bin/timer.sh`; the configured timeout is authoritative
  and resets for this step.
- Work in `/app` and hand the whole tree on as it stands. Do not add a prose
  answer, a migration report, or an alternate entry point; the tree is the
  deliverable.
- The repository's own test suite is part of the tree you hand on, and it is
  expected to run and pass there. Do not delete, empty, skip or exclude it.
  Bringing a spec up to date is in scope where this step's own changes make its
  spelling wrong — a renamed callback, a matcher its testing library no longer
  provides — and an expectation whose only subject is a mechanism this step
  removes may go with it. Dropping an expectation that still has a subject is
  not, and neither is handing on a suite that has lost most of what it shipped
  with. Concretely: the suite ships 520 examples, of which 25 have the
  mass-assignment protection this step removes as their only subject, and the
  suite you hand on is expected to still report at least the remaining 495.
  Merging surviving examples into fewer of them is not a way to meet that
  number, and an example that no longer asserts anything is not an example.
  Across the spec files the repository shipped, at least 220 distinct
  assertion lines survive, and no one of those files is left declaring
  examples that assert nothing: each of them keeps at least one assertion of
  its own. The suite is more than its spec files — its factories, fixtures,
  helpers and support files ship with it, and every one of those that arrived
  with content of its own still has some in the tree you hand on.
- Do not cheat by using online solutions or hints specific to this task.

**How it is run.** The application is started in `production` from its own
`Procfile` web command. The configuration it is given is its own, as
`sample.env`, `config/environment.rb` and `config/database.yml.example`
describe it: `SECRET_TOKEN`, `APP_URL`, `MAPBOX_MAP_ID`,
`GOOGLE_ANALYTICS_ID` and `LOCK_CITYVOICE` from the first,
`SENDGRID_USERNAME` and `SENDGRID_PASSWORD` from the second,
`CITYVOICE_LOCK_USERNAME` and `CITYVOICE_LOCK_PASSWORD` wherever the lock is
on, and a `config/database.yml` copied into place from the third. Beyond that
the surroundings are fixed, and the tree must work in them as they are:

- `PORT` names the port to listen on.
- `DISABLE_DATABASE_ENVIRONMENT_CHECK` is set. It is Rails' own documented
  escape hatch for the guard that refuses a destructive `db:` task against a
  database the framework believes is production. Rails 4.2 has no such guard
  and does not read it; it is set because those tasks get run, not to
  change what any of them does.
- The system clock may be set to the repository's own era; nothing may depend
  on the current date.
- The process runs as an unprivileged user with a writable home directory
  outside the tree, and must not need to write anywhere but its own tree and
  that home.
- Bundler configuration kept inside the tree is not read (`/app/.bundle`).
- The interpreter is the one rbenv selects from the tree's own
  `.ruby-version`.
