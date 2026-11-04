# Step 2 — Rails 4.2 → 5.0 on Ruby 2.4

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it:
on Rails 4.2 under Ruby 2.3, with mass assignment gone and its secret coming
from `config/secrets.yml`. Move it to **Rails 5.0 on Ruby 2.4**, and leave it
behaving as it does today.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Ruby 2.4 and on Rails 5.0.
- The Gemfile declares the Ruby the application runs on, `.ruby-version` names
  that same version, and `Gemfile.lock` records that same Ruby alongside the
  resolved gem set.
- The whole dependency set installs on Ruby 2.4 and resolves against Rails 5.0
  — not just the framework gems. A dependency whose current pin caps the
  framework below 5.0, or which has no release that loads on this interpreter,
  has moved.
- The application boots in the `production` environment with eager loading on,
  and every file under the eager-load paths loads without raising. Code the
  application keeps outside `app/` is available in production, not only in
  development.
- The application also boots in the `development` and `test` environments,
  each one in a process of its own, and each file under `config/environments/`
  configures the release the application now runs on: a setting that release
  renamed or dropped, or a middleware entry its default stack no longer
  carries, is not still being named there.

**Models.**

- The application's models descend from an application-owned abstract base
  class, `ApplicationRecord`, as Rails 5.0 generates them — one place for a
  concern that belongs to every model of this application rather than to every
  model of every application in the process.
- No model inherits from `ActiveRecord::Base` directly any more; that class is
  the superclass of `ApplicationRecord` and of nothing else in the tree.

**Migrations.**

- Every migration declares the schema version it was written against. These
  migrations were written and run under 4.2 semantics and must keep them; an
  undeclared migration is deprecated on 5.0, and `rake db:migrate` warns about
  each one it runs.

**Framework defaults.** Rails 5.0 leaves several of its own new behaviours
switched off for an application arriving from 4.2, and lists them in the file
`rails new` generates for exactly this purpose. This application adopts them:

- `belongs_to` associations are required by default. An association this
  application legitimately creates before its parent is known must say so on
  the association itself, and the ones whose parent is known by the time the
  row is written stay required. Turning the default back off, application-wide,
  is not a way to satisfy this line; neither is marking every association
  optional one at a time, and neither is leaving the default off and
  hand-writing a presence validation in its place.
- Per-form CSRF tokens are on.
- Origin-checking CSRF mitigation is on.
- `to_time` preserves the timezone of its receiver.
- A callback chain is no longer halted by a callback returning `false`.
- The SSL options enable HSTS with subdomains.

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
- Running the spec suite raises no deprecation warning from a file under
  `spec/`. The suite is the application's code too, and it exercises paths that
  driving the running application does not reach.

Both of these are about what the tree *raises*, not about what it prints.
Turning the warnings off — `config.active_support.deprecation = :silence`,
`ActiveSupport::Deprecation.silenced = true`, a `silence` block around the
call — leaves the deprecated call exactly where it was, and satisfies neither
of them.

**Integrity.** All three of these must hold of the tree:

- Every Ruby and ERB file in the tree parses.
- The application is still there: the migration removes what the framework
  removed, not the application's own code.
- No comment anywhere directs a tool to ignore, skip or suppress a finding. A
  requirement above is about the property, not about what some tool reports;
  silencing the report does not change the property.

**Configuration.** `config/database.yml` is not in the repository, and the tree
you hand back does not add it; the configuration is produced from
`config/database.yml.example`, which must keep working as it stands against a
local PostgreSQL server. Copying the example into place to run the application
while you work is expected — that copy is a working file, not part of the tree
you hand on.

**Behaviour that must not change.** The application's observable behaviour on
its own surfaces — the HTTP endpoints it serves, the Twilio webhooks it
answers, the rake tasks it ships (including the CSV importers and the migration
tasks), the mail it sends — is unchanged, with six exceptions. These six are
changes Rails itself made between 4.2 and 5.0; adopt them, and do not write
code to restore the old answers:

1. **A generated submit button disables itself on submit.** Rails 5.0 gives
   `submit_tag` a `data-disable-with` attribute by default, carrying the
   button's own label. Every page this application renders with a form built
   that way therefore serves one extra attribute.
2. **A format with no template is refused, not an error.** Where 4.2 answered a
   request for a format the action has no template for with a 500, 5.0 answers
   `406 Not Acceptable`: an action that renders nothing for the negotiated
   format is now a refusal to negotiate rather than a missing-template
   exception.
3. **A JSON error body reports its status as a number.** The `status` member of
   the JSON error document the public exceptions app serves is an integer where
   it used to be a string.
4. **`db:create` and `db:drop` say what they did.** Both tasks now print a line
   naming the database they created or dropped; 4.2 printed nothing on success.
5. **The migration log names the options each schema statement was given.**
   Running a migration logs the options hash alongside the statement's other
   arguments — the table a `create_table` creates, the reference an
   `add_reference` adds — and logs it when it is empty as readily as when it is
   not.
6. **A render is announced before it is reported.** Each rendered template is
   logged twice: once when rendering starts and once, as before, with the time
   it took. Anything that writes that log gains the first line, a rake task
   that renders a template as much as a request that does.

Nothing else about the application's behaviour is up for renegotiation. Where a
release changed a default in a way that would alter behaviour you are asked to
preserve, the old behaviour still holds.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 5.0" means and every change listed above:

- Ruby on Rails 5.0 release notes — https://guides.rubyonrails.org/5_0_release_notes.html
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
  spelling wrong — a test helper whose signature the framework changed, an
  assertion whose subject the framework now provides instead of the model. No
  expectation in this suite has a subject this step removes, so nothing in it
  need be dropped: the suite you are handed reports at least 495 examples, and
  the suite you hand on is expected to still report at least that many. Merging
  surviving examples into fewer of them is not a way to meet that number, and
  an example that no longer asserts anything is not an example. Across the spec
  files the repository shipped, at least 220 distinct assertion lines survive,
  and no one of those files is left declaring examples that assert nothing:
  each of them keeps at least one assertion of its own. The suite is more than
  its spec files — its factories, fixtures, helpers and support files ship with
  it, and every one of those that arrived with content of its own still has
  some in the tree you hand on.
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
  database the framework believes is production, and it is set because those tasks get run — not to change what any of them does.
- The system clock may be set to the repository's own era; nothing may depend
  on the current date.
- The process runs as an unprivileged user with a writable home directory
  outside the tree, and must not need to write anywhere but its own tree and
  that home.
- Bundler configuration kept inside the tree is not read (`/app/.bundle`).
- The interpreter is the one rbenv selects from the tree's own
  `.ruby-version`.
