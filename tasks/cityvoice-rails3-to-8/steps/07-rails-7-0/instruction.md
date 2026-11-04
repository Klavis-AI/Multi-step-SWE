# Step 7 — Rails 6.1 → 7.0 on Ruby 3.1

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 6.1 under Ruby 3.0, loading its own constants through Zeitwerk, taking
its framework defaults through a single `load_defaults` declaration, preloading
the two collection pages that used to query once per row, naming the hosts it
will answer to, and booting clean in every environment it declares. Move it to
**Rails 7.0 on Ruby 3.1**, and leave it behaving as it does today.

This step is unlike the six before it. Nothing in this application breaks on
Rails 7.0. The framework gems move together, the dependency set resolves with
no floor to name, the suite runs green and the application boots in all three
environments with nothing changed but the two version pins. So the milestone
here is not repair work. It is the two things 7.0 offers that this application
has been waiting for: the column it has always stored people's email addresses
in stops being plain text, and the page that exports the whole answer set stops
waiting for each of its queries in turn.

Both of those are the release's, and both of them are the kind of feature whose
declaration and whose behaviour come apart — a line that looks right and
achieves nothing is the failure mode of each. Read the requirements below as being about what the running application does.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Ruby 3.1 and on Rails 7.0.
- The Gemfile declares the Ruby the application runs on, `.ruby-version` names
  that same version, and `Gemfile.lock` records that same Ruby alongside the
  resolved gem set — one version, named in all three places.
- The whole dependency set installs on Ruby 3.1 and resolves against Rails 7.0
  — not just the framework gems. Ruby 3.1 continues what 3.0 started and stops
  shipping several more libraries as part of the standard library; Active
  Support 7.0 declares the ones it needs, and they have to be in the set that
  actually resolves rather than in the set that would resolve if the resolver
  were allowed to float everything. Move what this step moves and leave the
  rest of the lockfile alone. Nothing here is about how many lines changed — what matters is that the tree boots, behaves and passes its suite — but a
  resolution that drags seventy gems forward because it was asked to update
  one is not this milestone, and each of those gems is a behaviour change
  nobody asked for that you then own.
- A lockfile that resolved is not evidence the set works. Booting the
  application in each environment, driving it, and running its suite is.

**Framework defaults.** The declaration that names the release whose defaults
this application takes moves one version on:

- The declaration names 7.0.
- And the application actually gets what 7.0 brings with it. Naming the version
  and then configuring the new behaviours back off is not the milestone; each
  of these is in force in the tree you hand back:
  - The digest the framework uses for its own hashing is the 256-bit member of
    the SHA-2 family, not SHA-1. The release picked one; a different member of
    the same family is not it.
  - The generator that derives keys from the application's secret uses that
    same newer digest.
  - A time that has been converted into a zone reports its own class name
    rather than the name of the plain time class it has been impersonating.
  - Converting such a time to a string takes no format argument at all. This is
    the one place in this step where a 7.0 default removes something this
    application uses: two view partials ask for a named format that way, and
    the framework has a differently-spelled conversion that still takes one.
    Both of those partials have to move to it. The deprecation the framework
    raises before its defaults remove the old spelling names the replacement,
    and a third partial in this application is already written that way.
  - Entries the framework writes to the cache use this release's own entry
    format.
  - UUIDs derived from a namespace follow the RFC rather than the framework's
    older interpretation of it.
  - The helper that renders a one-button form renders a real button element.
  - Stylesheet tags stop carrying a media attribute that was never asked for.
  - An insert writes every column rather than only the ones that differ from
    their defaults.
  - A redirect to a host outside the application raises rather than quietly
    redirecting.
  - Parameter wrapping is on for controllers that do not say otherwise.
  - Fixtures loaded by a test are checked against the foreign keys the schema
    declares.
  - Active Record works out the inverse of a scoped association on its own.
  - The legacy cross-site-scripting response header is sent switched off rather
    than switched on, which is what every current browser wants.
- The 5.0, 5.1, 5.2, 6.0 and 6.1 defaults this application adopted in the
  previous steps are still in force. 7.0's defaults are 6.1's plus the list
  above, so a tree that reaches 7.0 by replacing the declaration keeps them; a
  tree that reaches it by adding a second declaration, or by deleting the
  first, does not.

**The column that holds people's email addresses.** This is the step's own work, and the largest requirement in it.

`subscribers.email` has been plain text in the database since the first
migration in this repository. This application is a civic feedback line: the
addresses in that column are residents who asked to hear about their own
neighbourhood, and the column is the only personal data it stores. Rails 7.0 is
the release that encrypts a column at rest without an extra gem. In the tree
you hand back:

- An address the application has written is ciphertext in the column. Reading
  the column directly — not through the model, which would decrypt it back and
  tell you nothing — does not yield the address.
- And the application can still find a subscriber by address. This application
  looks that column up by value in two places, so an encryption scheme under
  which every ciphertext for the same address differs breaks it. It breaks it
  silently: no error, just a lookup that never matches.
- And an address that differs only in capitalisation still finds the same row.
  Until this step the uniqueness declared on that column was explicitly
  case-insensitive, which is the identity this application has always given
  addresses. Encryption takes that away by default and leaves no sign that it
  has — the case-insensitive comparison the validator emits still runs and
  still passes, now folding the case of base64. The fold has to happen before
  the value is encrypted, not after.
- And subscribing twice under different capitalisation still makes one
  subscriber, through the application's own subscribe path, not just through a
  finder. The finder and the uniqueness validator ask the database different
  questions, so a tree that folds case in one and not the other satisfies the requirement above and raises on the form.
- And an address the application accepted before this step still round-trips
  after it. Ciphertext is longer than what it encrypts — about a third longer,
  plus a fixed overhead for the envelope — and this column has been
  `varchar(255)` since it was created, which is not wide enough for the
  encrypted form of every address this application's own format validation
  accepts. Widen it, and to a type with no length at all rather than to a
  longer bounded one: the next address is longer than the last guess.
  - Widen it with a migration, not by editing the schema dump. Add exactly one
    file, at `db/migrate/20160119120000_widen_subscribers_email.rb`, declaring
    `class WidenSubscribersEmail`. That path and that class name are part of
    the requirement: this application runs under a fixed clock, so a
    timestamp taken from the wall clock would be a different number every time,
    and both the migration listing the application prints and the schema
    version in the dump name this one. Its body is part of the requirement
    too, because what `db:migrate` and `db:rollback` print is part of this
    application's behaviour: a single `change` method holding one
    `change_column :subscribers, :email, :text` and nothing else — `text`,
    not an unbounded `string`, and no `up`/`down` pair — so it is a migration
    that cannot be reverted. Like every migration already in this tree, it
    declares a schema version in its superclass; the release the tree now runs
    is the natural one to declare, because that is when it was written.
  - `db/schema.rb` is the dump of the schema those migrations build, so it
    moves with them, and it still pins the primary-key type it has always
    pinned.
- And the keys that do the encrypting come out of the tree's own encrypted
  credentials, which is where the signing key already lives — under the
  `active_record_encryption` key that Active Record reads them from on its
  own. The application's own configuration does not assign them, not even to
  a value it reads out of the credentials. Not out of the environment: nothing is supplied to this application from outside its
  tree except the configuration listed at the end of this document, and an
  encryption key in the environment of a process whose database holds the
  ciphertext is the whole of the protection undone. It would also be in the
  process list of every login on the box.

**The export page's queries.** The second thing 7.0 offers this application.

- The page that exports the whole answer set as a CSV builds three independent
  relations before it renders anything: the numerical questions, the voice
  question, and the calls with their locations and answers. None of the three
  needs either of the others. At least two of them are sent to the database
  without the request waiting for the answer before it sends the next — which
  means the three are all set going before the first one is read, not asked
  for on the line that then immediately walks them. A relation asked to load
  that way and consumed on the next line loads inline anyway; the framework
  neither raises nor warns when that happens.
- A relation asked to load that way does nothing at all unless the application
  has been configured with something to run it on. With no such configuration
  the call raises nothing, warns nothing, and loads the relation inline — so
  the line is written, the page is unchanged, and the work has not been done.
  Configure it, and size it for the page.
- Everything the previous step won on that page stays won. It still costs fewer
  queries than it renders rows, and so does the page that lists every recorded
  answer for a question. A page made asynchronous by un-preloading it and
  firing its per-row queries concurrently is a worse page than the one you were
  handed.

**Deprecations.** Three conditions, as in the previous two steps:

- Driving the application raises no deprecation warning that comes from the
  application's own code or its own configuration. Warnings raised from inside
  a gem's own source are out of scope; anything the application itself triggers
  is in scope. This includes warnings raised from the application's own
  configuration. The condition allows not one: a drive that raises one has not met it any more than a drive that raises all of them.
- Booting it raises none either, in `development` and in `test`, each in a
  process of its own, and across the whole tree rather than only in `config/environments/`. The `production` boot is part of driving the application, so a deprecation raised while production boots falls under that condition, which allows not one. In both cases, a deprecation a gem raises while the application's own code is
  beneath it on the stack — the boot's `require` of that gem, or a call the
  application made into it — belongs to that line of the application and is in scope: the gem, and the release of it the tree is on, are the
  application's choice. Only what a gem raises with no application code
  beneath it is the gem's own.
- Running the spec suite raises no deprecation warning from a file under
  `spec/`. This one allows not one either.

Note where this bites on this step. Rails 7.0 deprecates the format-argument
conversion named above as soon as it is installed, and removes it when its
defaults are adopted — so a tree that moves the pins and stops has ten of these
coming out of its own views, and a tree that adopts the defaults without fixing
the views does not render those pages at all. The deprecation and the removal
are one release apart in this step, which is why both halves have to be done
together.

All three are about what the tree *raises*, not about what it prints.
Turning the warnings off — `config.active_support.deprecation = :silence`,
`ActiveSupport::Deprecation.silenced = true`, a `silence` block around the
call — leaves the deprecated call exactly where it was, and satisfies none of
them.

**Integrity.** This step is not a rewrite, and everything the previous steps
established is still true of the tree you hand back. Every item on this list must hold, and so must the deprecation conditions above.

- Every Ruby and ERB file in the tree parses, under the interpreter the tree
  names.
- The application is still there: every file the repository shipped under
  `app/models`, `app/controllers`, `app/helpers`, `lib` and `db/migrate` is
  still at its path.
- The application's own test suite is still there — every file the repository
  shipped under `spec/`, each still asserting something — and still passes.
- The application still boots in `production` with eager loading on, and in
  `development` and `test`, each in a process of its own, with every file under
  the eager-load paths loading without raising, and with the code the
  application keeps outside `app/` available in production.
- The application's own constants are still loaded by Zeitwerk, and the whole
  set still loads when the loader is asked to load all of it at once.
- No association, validation or callback passes SQL or a condition as a String
  to be evaluated in the model's context.
- `config/database.yml` is not in the tree you hand on. The configuration the
  application is given is copied into place from
  `config/database.yml.example`, which is the file that ships.
- The models still descend from `ApplicationRecord`, and nothing inherits from
  `ActiveRecord::Base` directly.
- Every migration the repository shipped still declares the schema version it
  was written against — which stays the version it was written against, not
  whichever release the tree now runs. All forty-three of them still say 4.2.
  The one this step adds is not one of the forty-three and is not held to 4.2.
- `db/schema.rb` still pins the primary-key type it was dumped with — the 32-bit key the
  tables have always had, while the release's own default for a table that
  names no key type is left as the release sets it rather than configured
  back to the narrow key application-wide — so a
  database built by loading the dump still has the same primary keys as one
  built by running the migrations.
- The 5.0, 5.1, 5.2, 6.0 and 6.1 defaults stay adopted, and stay adopted the
  same way: through the version declaration, with no `new_framework_defaults`
  file in the tree and no switch it used to list set by hand anywhere (the seven that 5.0's generated file listed are
  `per_form_csrf_tokens`, `forgery_protection_origin_check`,
  `raise_on_unfiltered_parameters`, `to_time_preserves_timezone`,
  `belongs_to_required_by_default`, `halt_callback_chains_on_return_false`
  and `ssl_options`).
  `belongs_to` is still required by default; the one association this
  application legitimately creates before its parent is known is still the only
  one marked optional, and it is still marked on the association itself.
- The three relations the previous step marked as not being allowed to load an
  association on their own still carry that marker, and it is still declared
  where each relation is built rather than globally.
- The signing key the application uses at boot still comes out of the tree's
  encrypted credentials, the key that decrypts them still ships in the tree,
  and neither the store 5.1 used nor a plaintext key is anywhere in the tree
  under any name, and the framework is no longer told to read that store.
- Bootsnap is still set up from the tree's own boot file, and still caching.
- The content security policy is still declared, still reaches every HTML
  response as an enforced header naming the application's own origin, and the
  inline scripts the application serves still carry a nonce that the policy's own nonce generator
  (`content_security_policy_nonce_generator`) mints afresh for each request,
  named by that same response's policy. The inline scripts stay.
- The asset helpers still raise on a name the pipeline has no asset for, rather
  than emitting a path under `public/`, and the application's own audio lookups
  still answer as they do today.
- The forms are still built with the 5.1 builder — the tree calls neither of
  the two builders it replaced, anywhere — and the field ids those forms carry
  are still there.
- The mailer is still parameterized: every action it declares takes no
  positional argument, and every caller in the tree hands its arguments over
  the same way.
- The hosts declaration still answers to the deployment and to the container's
  own address, and still refuses both an unknown name and a name that merely contains the
  deployment's — the deployment's name with another domain appended to it.
- No comment anywhere directs a tool to ignore, skip or suppress a finding.

**What is allowed to change.** This step is a behaviour-preserving move with
three exceptions, and one of the three is the point of the step rather than an
accident of the release:

- A subscription submitted with an address that differs only in capitalisation
  from one already on file used to be refused: the lookup missed, the
  application tried to create a second subscriber, the case-insensitive
  uniqueness rule caught it, and the request came back as a failed submission.
  In the tree you hand back that submission succeeds and reuses the subscriber
  who is already there — which is the case-identity requirement above, seen
  from the outside. One more subscription exists afterwards, one more
  confirmation goes out — which mints one more unsubscribe token, so the count
  of tokens the application has issued goes up by one too — and the digests
  that list a resident's locations list one more.
- The lines `db:migrate` prints for the application's own migrations change
  spelling again: an internal flag this release sets on a compatibility shim is
  no longer printed, and the type name this release uses for a timestamp column
  on this database differs from the one the previous release printed. The
  schema those migrations build is unchanged, and it must stay unchanged — the
  dump is the proof, and the only difference in it is the column this step
  widens.
- The migration listing, the reported schema version, and the class named in
  the message the framework prints when it cannot revert the most recent
  migration all move, because this step adds a migration and it is now the most
  recent one. The framework still refuses to revert it, still aborts loudly,
  and still leaves the schema where it was.

Everything else this application does — every page, every IVR callback, every
mail, every redirect, every recorded answer and every row the database ends up
holding — is expected to be exactly what it is today.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 7.0" means and every change listed above:

- Ruby on Rails 7.0 release notes — https://guides.rubyonrails.org/7_0_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Active Record Encryption — https://guides.rubyonrails.org/active_record_encryption.html
- Active Record Query Interface, on asynchronous queries and on eager loading —
  https://guides.rubyonrails.org/active_record_querying.html
- Configuring Rails Applications —
  https://guides.rubyonrails.org/configuring.html
- Securing Rails Applications, on credentials —
  https://guides.rubyonrails.org/security.html
- Ruby 3.1.0 release notes — https://www.ruby-lang.org/en/news/2021/12/25/ruby-3-1-0-released/
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
  spelling wrong. No expectation in this suite has a subject this step removes,
  so nothing in it need be dropped: the suite you are handed reports at least
  495 examples, and the suite you hand on is expected to still report at least
  that many. Merging surviving examples into fewer of them is not a way to meet
  that number, and an example that no longer asserts anything is not an
  example. Across the spec files the repository shipped, at least 220 distinct
  assertion lines survive, and no one of those files is left declaring examples
  that assert nothing: each of them keeps at least one assertion of its own.
  The suite is more than its spec files — its factories, fixtures, helpers and
  support files ship with it, and every one of those that arrived with content
  of its own still has some in the tree you hand on.
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
the signing key, nor the key that decrypts the application's credentials, nor
any key this step's encryption needs is set in the environment, so whatever the
application needs in order to read its own secrets is in the tree or it is not
there at all.
Beyond that the surroundings are fixed, and the tree must work in them as they
are:

- `PORT` names the port to listen on, and the process is reached at that port
  on the container's own address as well as by the name in `APP_URL`.
- `DISABLE_DATABASE_ENVIRONMENT_CHECK` is set. It is Rails' own documented
  escape hatch for the guard that refuses a destructive `db:` task against a
  database the framework believes is production, and it is set because those tasks get run — not to change what any of them does.
- The system clock is set to the repository's own era; nothing may depend on
  the current date, and a file named after the wall clock is a file named
  after a number that will not be there next time.
- The process runs as an unprivileged user with a writable home directory
  outside the tree, and must not need to write anywhere but its own tree and
  that home.
- Bundler configuration kept inside the tree is not read (`/app/.bundle`).
- Interpreter warnings are off; the deprecation conditions above are about what driving the application, booting it in each environment and running its suite raise, not about what reaches stderr.
- The interpreter is the one rbenv selects from the tree's own
  `.ruby-version`, and the image this step runs in carries exactly one
  interpreter: the one this milestone names.
- A tree that does not boot is not partly done: almost everything above is a property of the running application, so a tree that cannot start has none of it. Leave yourself time to boot, drive and
  test what you hand back.
