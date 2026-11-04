# Step 3 — Rails 5.0 → 5.1

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 5.0 under Ruby 2.4, with its models on `ApplicationRecord`, its
migrations versioned, and the 5.0 framework defaults adopted through the file
`rails new` generates for that purpose. Move it to **Rails 5.1**, and leave it
behaving as it does today.

The interpreter does not move in this step. Ruby 2.4 is the interpreter before
and after.

The tree you hand back must have all of these properties.

**Framework.**

- The application runs on Rails 5.1.
- The whole dependency set resolves against Rails 5.1 and installs on Ruby 2.4
  — not just the framework gems. A dependency whose current pin caps the
  framework below 5.1, or which 5.1 no longer loads, has moved.

**Framework defaults.** The previous step adopted 5.0's new behaviours by
editing the generated file that lists them one switch at a time. 5.1 offers a
single declaration that names the release whose defaults the application wants,
and that is how this application now says it:

- The application declares the framework version whose defaults it takes, in
  its own configuration, and the version it names is 5.1.
- The generated per-switch file is gone from the tree, and its contents have
  not moved somewhere else under another name: no switch it listed is set by
  hand anywhere. It exists to be deleted once its contents have been adopted,
  and keeping it alongside the declaration means two places state the same
  thing and can disagree.
- Every default the previous step adopted is still in force. Moving from the
  file to the declaration must not quietly give one of them back: `belongs_to`
  is still required by default, per-form CSRF tokens and origin-checking
  forgery protection are still on, `to_time` still preserves its receiver's
  timezone, a callback chain is still not halted by a callback returning
  `false`, and the SSL options still enable HSTS with subdomains.
- `belongs_to` in particular is still adopted the way the previous step
  adopted it, and the declaration does not become a second way of satisfying
  it. The association this application legitimately creates before its parent
  is known still says so on the association itself; the ones whose parent is
  known by the time the row is written stay required. Turning the default back
  off, application-wide, is not a way to keep this line; neither is marking
  every association optional one at a time, and neither is leaving the default
  off and hand-writing a presence validation in its place.

**Assets.** Among 5.1's defaults is one this application will feel. Up to 5.0
the asset pipeline, asked for a path it has no asset for, quietly answered with
a path under `public/`; from 5.1 that fallback is off and the same request
raises instead. Both halves of the following are required, and neither on its
own is the milestone:

- The fallback is off — that default is in force, not switched back on.
- The application's own audio lookups still answer, and still answer with the
  same paths they answer with today. This application names audio files from
  its data: a question's short name, an error's message. Those names have never
  been pipeline assets and are not becoming any; the files they name are served
  from the application's own audio directory. Every place the application turns
  such a name into a URL keeps working, and a name that is a pipeline asset is
  still resolved through the pipeline.

**Forms.** The form helper this application uses to build its subscription form
is soft-deprecated in 5.1, along with its URL-backed counterpart, in favour of
the one builder that replaces the pair of them. The tree you hand back builds
its forms with the 5.1 builder, and calls neither of the two builders it
replaces anywhere — swapping one of them for the other is not the migration.
The form's action, its method and its field names are part of the behaviour
that must not change, so the replacement names them explicitly rather than
inferring a different set.

**Secrets.** 5.1 adds encrypted secrets, and this application adopts them:

- The application's secrets are held encrypted in the tree, in the file 5.1
  reads them from, and no file anywhere in the tree still carries the signing
  key in the clear — under that name or any other.
- The application is configured to read them in the environment it is run in.
- The key that decrypts them ships in the tree. This is a deliberate departure
  from what you would do for a real deployment, where the key is held outside
  the repository: the tree is run as it stands, with no secret store, so a
  tree whose secrets cannot be decrypted is a tree that does not boot.
  Everything the application reads out of its secrets today it still reads
  out of them, and from the encrypted copy.

**Mailers.** 5.1 adds a way to hand a mailer its arguments that does not change
the arity of the action. This application's mailer adopts it: every action the
mailer class itself declares takes no positional argument, and takes what it
needs from the arguments the caller supplied. Every caller in the tree — the
application's own code and its test suite alike — hands them over the same way,
and the mail that is sent is unchanged: same recipients, same subjects, same
bodies.

**Model conditions.** A validation conditioned on a String to be evaluated in
the model's context is deprecated in 5.1, at the moment the class is defined.
Conditions in this application are stated in a form the framework does not have
to evaluate as code.

**The schema dump.** From 5.1 a table created without being told what kind of
primary key to give it gets a 64-bit one. This application's migrations are
insulated from that: they declare the schema version they were written
against — which stays the version they were actually written against, not
whichever release the tree now runs — and the compatibility layer keeps
building the 32-bit key they have always built. `db/schema.rb` declares no
version and is not insulated. The tree you hand back is one where **a database
built by loading the schema dump has the same primary keys as one built by
running the migrations, and both are the 32-bit keys this application's tables
have today** — this application's own foreign key columns were declared to
match them, and a schema dump that silently starts issuing wider ids than its
foreign keys can hold is a defect that appears years later and not in any test.

The insulation is the compatibility layer, and it is the only thing doing this
job. 5.1's own answer for a table that does not say what key it wants stays
5.1's answer: the release's new default is adopted along with the rest of them
and is not configured back to the narrow key application-wide. A tree that
reaches the two matching schemas by telling the framework to hand out 32-bit
keys again has adopted 5.1's defaults in name only, and the next table anyone
adds — which will not be written against a 4.2 compatibility layer — gets the
key 5.1 stopped giving out rather than the one it now gives.

The application's tables and their contents are otherwise unchanged; nothing
here is a data migration.

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
  `spec/`.

Both of these are about what the tree *raises*, not about what it prints.
Turning the warnings off — `config.active_support.deprecation = :silence`,
`ActiveSupport::Deprecation.silenced = true`, a `silence` block around the
call — leaves the deprecated call exactly where it was, and satisfies neither
of them.

**Integrity.** This step is not a rewrite, and everything the previous steps
established is still true of the tree you hand back. Every item on this list must hold, and so must the two deprecation conditions above.

- Every Ruby and ERB file in the tree parses, under the interpreter the tree
  names.
- The application is still there: every file the repository shipped under
  `app/models`, `app/controllers`, `app/helpers`, `lib` and `db/migrate` is
  still at its path. The migration removes what the framework removed, not the
  application's own code.
- The application's own test suite is still there — every file the repository
  shipped under `spec/`, each still asserting something — and still passes.
- The application still boots in `production` with eager loading on, and in
  `development` and `test`, each in a process of its own, with every file under
  the eager-load paths loading without raising, and with the code the
  application keeps outside `app/` available in production.
- The models still descend from `ApplicationRecord`, nothing inherits from
  `ActiveRecord::Base` directly, and every migration still declares its schema
  version.
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
tasks), the mail it sends — is unchanged, with two exceptions. Both are changes
Rails itself made between 5.0 and 5.1; adopt them, and do not write code to
restore the old answers:

1. **The subscription form is submitted by script.** The 5.1 form builder
   builds a remote form by default, so the `<form>` element the subscription
   partial renders carries one extra attribute saying so. Every page that
   renders that form therefore serves one extra attribute. The form's action,
   its method and its two field names are unchanged, and the behaviour of a
   submitted subscription is unchanged.
2. **The migration log names the adapter and the primary key.** Running the
   migrations logs an adapter lookup ahead of each schema statement, and logs
   the primary key type the statement was given rather than leaving it
   unstated. The schema those migrations build is the same schema.

Nothing else about the application's behaviour is up for renegotiation. Where a
release changed a default in a way that would alter behaviour you are asked to
preserve, the old behaviour still holds.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 5.1" means and every change listed above:

- Ruby on Rails 5.1 release notes — https://guides.rubyonrails.org/5_1_release_notes.html
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
  spelling wrong — a mailer invocation whose calling convention this step
  changes, an expectation whose subject the framework now provides instead of
  the application. No expectation in this suite has a subject this step
  removes, so nothing in it need be dropped: the suite you are handed reports
  at least 495 examples, and the suite you hand on is expected to still report
  at least that many. Merging surviving examples into fewer of them is not a
  way to meet that number, and an example that no longer asserts anything is
  not an example. Across the spec files the repository shipped, at least 220
  distinct assertion lines survive, and no one of those files is left declaring
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
on, and a `config/database.yml` copied into place from the third. Nothing else
is supplied from outside the tree — in particular, no key material is: whatever
the application needs in order to read its own secrets is in the tree or it is
not there at all. Beyond that the surroundings are fixed, and the tree must
work in them as they are:

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
