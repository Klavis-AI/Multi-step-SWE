# Step 4 — Rails 5.1 → 5.2

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 5.1 under Ruby 2.4, taking its framework defaults through a single
`load_defaults` declaration, building its forms with the 5.1 form builder,
sending its mail through a parameterized mailer, and holding its secrets in
5.1's encrypted-secrets store. Move it to **Rails 5.2**, and leave it behaving
as it does today.

The interpreter does not move in this step. Ruby 2.4 is the interpreter before
and after.

The tree you hand back must have all of these properties.

**Framework.**

- The application runs on Rails 5.2.
- The whole dependency set resolves against Rails 5.2 and installs on Ruby 2.4
  — not just the framework gems. A dependency whose current pin caps the
  framework below 5.2, or which 5.2 no longer loads, has moved.

**Framework defaults.** The previous step moved this application onto the
declaration that names the release whose defaults it takes. This step moves
the version it names:

- The declaration names 5.2.
- And the application actually gets what 5.2 throws with it. Naming the version
  and then configuring the new behaviours back off is not the milestone; each
  of these is in force in the tree you hand back:
  - Active Record cache keys carry a version alongside the key, instead of
    baking the record's timestamp into the key itself.
  - Encrypted cookies are written with an authenticated cipher rather than the
    older encrypt-then-sign pair.
  - Everything else the framework encrypts is written the same way.
  - The framework's own digests — the ones behind cache keys and ETags — are
    SHA-1 rather than MD5.
  - Controllers get forgery protection by default, rather than only the ones
    that ask for it. (This application's controllers already ask for it; the
    default is still expected to be on.)
  - The form builder gives its fields `id` attributes again. 5.1's builder
    stopped emitting them — which is why two fields in this tree carry an
    `id:` written by hand — and 5.2 offers them back as a default, which this
    application takes rather than leaving it off. The hand-written attributes
    may stay where they are: nothing on this page asks for a view to be
    churned.

**Credentials.** 5.2 replaces 5.1's encrypted secrets with encrypted
credentials, and this application moves across:

- The signing key the application actually uses at boot comes out of the tree's
  encrypted credentials. Not merely present in the file — *used*: the value the
  framework hands to everything that signs and encrypts is the value that file
  holds.
- The key that decrypts the credentials ships in the tree. This is a deliberate
  departure from what you would do for a real deployment, where that key is
  held outside the repository: the tree is run as it stands, with no secret
  store and no key in its environment, so a tree whose credentials cannot be
  decrypted is a tree that does not boot.
- The value that moves across has to be a key. 5.1's store was evaluated as a
  template before it was parsed; 5.2's credentials are not, so a secret written
  as a template that reads an environment variable arrives on the other side as
  the text of the template. Whatever the application signs with, it is a
  secret and not a spelling.
- 5.1's store is gone from the tree — the encrypted file, its key, and the
  plaintext file they replaced, by name and anywhere in the tree. Moving one of
  them under `lib/`, or to a `.bak`, is not removing it. Two stores that can
  disagree about the application's signing key is the defect this step closes.
- The configuration that told 5.1 to read that store is gone with it. A
  setting that names a file the tree no longer has is a setting that will be
  read by the next person as though it did.

**Bootsnap.** 5.2's generator puts bootsnap in the Gemfile and requires its
setup from `config/boot.rb`, and this application adopts it:

- Bootsnap is in the bundle and is set up from the tree's own boot file, so
  that it is in effect for everything the application loads — not merely
  required somewhere later, which leaves the constant defined and the caches
  switched off.
- And it is really caching: a boot leaves its cache behind under the
  application's own `tmp/`.

**A Content Security Policy.** 5.2 adds one, with a generator that writes a
commented-out initializer; this application adopts it for real:

- The application declares a content security policy.
- Every HTML page it serves carries that policy as an enforced header. Not the
  report-only header — that one is the failure that looks like success, since
  the browser reports and blocks nothing — and not on some pages.
- The policy's default source is the application's own origin, written the way
  the security guide below writes it: `default-src` names `'self'`, the source
  keyword, rather than a host this deployment happens to answer on.
- The pages still work under it. This application serves inline `<script>`
  blocks, and a policy that names no source for them breaks every page that has
  one. Each inline `<script>` the application serves carries a nonce, that
  nonce is different on every response, and the same response's policy names
  that exact nonce as an allowed script source — which means the policy states
  a script source of its own, and the nonce is generated per request rather
  than written into a view as a constant. A literal `nonce="..."` in a template
  is not a nonce, and neither is a value that is merely unique per visitor:
  the nonce comes from the policy's own nonce generator —
  `content_security_policy_nonce_generator`, which the 5.2 generator leaves
  commented out beside the policy — and that generator returns a fresh random
  value each time it is called. A generator that returns the session id, or
  anything else that stays the same for the length of a browsing session,
  serves one nonce on two responses and does not satisfy the sentence above.
- The inline scripts stay. They are application content — the map bootstrap,
  the analytics call, the audio player wiring — and deleting them is not a way
  to have nothing left to nonce.

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
  `spec/`. 5.2 deprecates the response predicate this suite asserts with, and
  the suite reaches it through RSpec's `be_success` matcher — twelve sites
  across seven controller specs, beside thirty-five calls to the longer
  matcher the same suite already uses elsewhere. Every site in the suite that uses it has moved, not most of them: a suite with one such call left in it has not met this condition any more than a suite that fixed none of them.

Both of these are about what the tree *raises*, not about what it prints.
Turning the warnings off — `config.active_support.deprecation = :silence`,
`ActiveSupport::Deprecation.silenced = true`, a `silence` block around the
call — leaves the deprecated call exactly where it was, and satisfies neither
of them.

**Integrity.** This step is not a rewrite, and everything the previous steps
established is still true of the tree you hand back. Every item on this list must hold, and so must the two deprecation conditions above.

- The interpreter is still Ruby 2.4.10.
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
  `ActiveRecord::Base` directly, and every migration still declares the schema
  version it was written against — which stays the version it was written
  against, not whichever release the tree now runs. `db/schema.rb` still pins the primary-key type it was dumped with — the 32-bit key the
  tables have always had, while the release's own default for a table that
  names no key type is left as the release sets it rather than configured
  back to the narrow key application-wide — so a database built by loading the
  dump still has the same primary keys as one built by running the migrations.
- The 5.0 and 5.1 defaults stay adopted, and stay adopted the same way: through
  the version declaration, with no `new_framework_defaults` file in the tree
  and no switch it used to list set by hand anywhere (the seven that 5.0's generated file listed are
  `per_form_csrf_tokens`, `forgery_protection_origin_check`,
  `raise_on_unfiltered_parameters`, `to_time_preserves_timezone`,
  `belongs_to_required_by_default`, `halt_callback_chains_on_return_false`
  and `ssl_options`). `belongs_to` is still
  required by default; the one association this application legitimately
  creates before its parent is known is still the only one marked optional, and
  it is still marked on the association itself.
- The asset helpers still raise on a name the pipeline has no asset for, rather
  than emitting a path under `public/`, and the application's own audio lookups
  still answer as they do today.
- The forms are still built with the 5.1 builder — the tree calls neither of
  the two builders it replaced, anywhere.
- The mailer is still parameterized: every action it declares takes no
  positional argument, and every caller in the tree hands its arguments over
  the same way.
- No association or validation passes SQL or a condition as a String to be
  evaluated in the model's context.
- No file anywhere in the tree carries a signing key in the clear — under that
  name or any other. The key that unlocks the credentials is not a signing key
  and is expected to be there.
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
tasks), the mail it sends — is unchanged, with one exception. It is a change
Rails itself made between 5.1 and 5.2; adopt it, and do not write code to
restore the old answer:

1. **The migration log stops naming the adapter.** The compatibility layer
   these migrations declare themselves against used to ask the migration
   object itself which adapter it was running on, and a question asked that
   way announces itself in the log ahead of the statement it was asked about.
   From 5.2 the layer asks the connection directly, so the announcements are
   gone — two of them ahead of each table these migrations create. The
   statements themselves, their arguments, their order and the schema they
   build are unchanged, and so is everything else `db:migrate` prints.

Nothing else about the application's behaviour is up for renegotiation. Two of
this step's changes are the kind that break everything and show nothing if they
are done carelessly: replacing the store the signing key comes from changes
what every signed and encrypted value in the application is derived from, and
rewriting `config/boot.rb` edits the file whose failure mode is that nothing
boots at all. Drive the application after each of them rather than after all of
them.

Where a release changed a default in a way that would alter behaviour you are
asked to preserve, the old behaviour still holds.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 5.2" means and every change listed above:

- Ruby on Rails 5.2 release notes — https://guides.rubyonrails.org/5_2_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Securing Rails Applications, on the content security policy —
  https://guides.rubyonrails.org/security.html
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
  spelling wrong — an expectation whose predicate this release renames. No
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
on, and a `config/database.yml` copied into place from the third. Nothing else
is supplied from outside the tree — in particular, no key material is: neither
the signing key nor the key that decrypts the application's credentials is set
in the environment, so whatever the application needs in order to read its own
secrets is in the tree or it is not there at all.
Beyond that the surroundings are fixed, and the tree must work in them as they
are:

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
- Interpreter warnings are off in the processes that run the application and
  its suite. Nothing on this page is a condition about what is printed, so
  nothing on this page is changed by what a process does or does not print.
- The interpreter is the one rbenv selects from the tree's own
  `.ruby-version`.
