# Step 9 — Rails 7.1 → 7.2 on Ruby 3.2

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 7.1 under Ruby 3.2, declaring where it keeps the code it holds outside
`app/`, with every partial it renders declaring the locals it takes, with the
column that holds residents' email addresses encrypted at rest under a
deterministic, case-folding scheme keyed out of the tree's own credentials, and
taking its framework defaults through a single `load_defaults` declaration.
Move it to **Rails 7.2**, and leave it behaving as it does today.

One pin moves, and it moves alone. Rails 7.2 runs on the interpreter this tree
already has — the framework asks for 3.1 or above and the tree arrives on 3.2 —
so the interpreter stays exactly where it is, and changing it is not part of
this milestone. Nothing under the framework is capped, floored or held back
either: on the pin by itself the whole set installs, the application boots in
all three environments, and its suite runs green.

That is what makes this step different from the ones before it. There is no
floor to clear before anything can boot and no gem to move out of the way, so
the work is not repair. It is the part the new framework does not do for you:
one release's worth of new defaults, actually adopted; one line that kept
parsing across the upgrade and quietly started meaning its opposite; and the
tree's last call to a method this release deprecates, in the file that runs in
the master process before it forks.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Rails 7.2, and it is 7.2 that the running process
  loads — not 7.1, and not a release above it.
- The interpreter does not move. The application still runs on Ruby 3.2, the
  Gemfile still declares it, `.ruby-version` still names that same version, and
  `Gemfile.lock` still records it alongside the resolved gem set.
- The whole dependency set installs on Ruby 3.2 and resolves against Rails 7.2
  — not just the framework gems. Advice rather than a condition: a resolution
  that drags the whole lockfile forward because it was asked to move one pin is
  a pile of behaviour changes nobody asked for, and the tree still has to boot,
  behave and pass its suite with every one of them in it. There is a
  conservative resolution of this Gemfile in which the framework's own gems
  move, one new dependency arrives with them, one leaves, and nothing else
  changes at all.
- A lockfile is a resolver's output, and hand-editing a version number in one
  does not resolve anything: the file also records, under each gem, the
  dependencies that gem declares, and a version written in by hand carries the
  *other* version's dependency list with it. Bundler reads the real list out of
  the gem as it installs and refuses the lockfile when the two disagree — but
  only for a gem it actually has to fetch, so a container that already has the
  gem unpacked will install a lockfile that no clean machine can. Resolve the
  set with the resolver, and check what you hand back by installing it into an
  empty bundle path rather than by reading it.
- A lockfile that resolved is not evidence the set works. Booting the
  application in each environment, driving it, and running its suite is.

**Framework defaults.** The declaration that names the release whose defaults
this application takes moves one version on:

- The declaration names 7.2.
- And the application actually gets what 7.2 brings with it. Naming the version
  and then configuring the new behaviours back off is not the milestone. The
  release turns all of it on at once, and each of these is in force in the tree
  you hand back:
  - A job enqueued inside a transaction is enqueued after that transaction
    commits rather than while it is still open.
  - A migration whose version number is more than a day ahead of the clock is
    refused rather than run. Every migration this repository ships is older
    than that, so nothing in the tree is affected by it.
  - The PostgreSQL adapter decodes dates. A query that does not go through an
    attribute type — a date selected as a bare value — comes back as a date
    object rather than as a string. This one is not a setting with an echo on
    some configuration object: it is a decoder the adapter registers on the
    connection when the setting is on, so what matters is what a live connection answers with, not what the configuration says.
- The 5.0, 5.1, 5.2, 6.0, 6.1, 7.0 and 7.1 defaults this application adopted in
  the previous steps are still in force. 7.2's defaults are 7.1's plus the list
  above, so a tree that reaches 7.2 by replacing the declaration keeps them; a
  tree that reaches it by adding a second declaration, or by deleting the
  first, does not.
- Three of the 7.1 settings are themselves deprecated by this release and
  announced for removal in the next one. They are the three compatibility
  switches 7.1 added so that an application could keep the *old* behaviour, and
  this application never wanted the old behaviour: it is on the new one, which
  is where the removal leaves everybody. Nothing here asks you to do anything
  about them, and setting any of them by hand — to either value — is not part
  of this step.

**The test environment still lets exceptions out of the request cycle.** This
is the step's silent flip, and the reason this milestone is not "change the
pin".

- When an example drives a request that raises, the exception still comes out
  of the request and reaches the example. It is not caught and turned into a
  rendered error page. *Every* exception: the ones the framework has a status
  for — a record that was not found, a route that does not exist — are let out
  on the same terms as the ones it does not. Of the values this setting now
  takes, only one means that; a value that means "let out the ones I have no
  answer for" is a different environment and does not satisfy this.
- `config/environments/test.rb` has said so since this repository was written,
  and the line it says it with still parses on 7.2 — the release did not remove
  the setting, it removed the *values*. 7.1 deprecated the booleans this line
  takes in favour of three named ones; 7.2 dropped the two branches that
  understood them, and dropping them did not restore an error. The old value
  now falls through to the default branch and answers with the opposite of what
  it has always meant.
- So this is a property of the booted environment and not of the file. The question is the one the framework's own middleware asks: given a real request
  in that environment, does the exception get shown or let out. A stale value,
  a deleted line and the right value are three different answers rather than
  three spellings of one.
- Nothing else can see it. Nothing in the suite raises through a request, so
  the suite passes either way, and the environment the `Procfile`'s web command
  boots is production, so driving the running application does not reach the
  test environment at all. A tree that gets this wrong has a test environment
  that quietly swallows the errors its suite exists to surface.

**The master process forks without holding a database connection.** The
application is served by a forking web server whose configuration file runs
hooks in the master process, and one of those hooks is the tree's last call to
a method this release deprecates.

- No file in the tree calls the deprecated accessor that hands out a permanent
  connection checkout — not the hook below, and not a model, a rake task, a seed
  file or a mailer preview either. Active Record 7.2 replaces it with two
  methods that lease a connection for the duration of a block or a statement,
  and — where what is wanted is not a connection at all but the pool behind it —
  with the pool itself. The accessor written out on `ActiveRecord::Base` or on `ApplicationRecord` appears nowhere under the repository, and neither does the same method reached through a model class, a local or `send`; moving the call somewhere quieter is not removing it.
- The hook that runs before the master forks still leaves the master holding no
  connection, and it does so without taking one out first. Read it as what the
  hook is for rather than as a rename: the old line leased a connection into the
  master in order to throw it away, and threw away only that one rather than the
  pool.
- The hook that runs *after* a worker forks re-establishes the worker's own
  connection. That call is not deprecated and is not part of this step; it need
  not change.
- This is not dead code. The web command in `Procfile` is what runs the
  application, and that command loads this configuration file, so the hook runs
  every time the application is served.
- The deprecation is silent on this tree as it stands — the framework only
  raises it when a setting that defaults the other way is turned on — so
  nothing in a boot, a drive or a suite run will tell you the call is there.
  Find it by looking.

**Deprecations.** Three conditions, as before:

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
  `spec/` — from the framework's deprecation stream or from the test
  framework's own. This one allows not one either, with one inherited
  exception which is stated here rather than left for you to find: the
  expectation library warns once per run that this suite uses the
  `should`/`should_not` spelling, a syntax it was never explicitly told to
  enable. That warning is older than this step, rewriting those call sites is
  not part of this milestone, and that one warning is exempt from this condition.

All three conditions are about what the tree *raises*, not about what it prints.
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
- `lib` is still on the application's autoload paths and on its eager-load
  paths, still declared once and by the method 7.1 added for exactly that, and
  the declaration still excludes the two directories under `lib` that hold no
  Ruby — the one that holds assets and the one that holds rake tasks.
- Every partial this application renders still declares the locals it takes, and
  the framework still really compiled the declaration: handed a local it does
  not declare, such a partial still raises. The four partials nothing renders
  are still out of scope and need not change.
- The method Ruby 3.2 removed still appears nowhere in the application's own
  code — `File.exists?`, and the `exists?` the same removal took off `Dir` and
  `FileTest`. The stylesheet compiler, the `sass` gem, is still on a release that does
  not call it either.
- The email column is still encrypted at rest, still findable by address, still
  finds the same row for an address that differs only in capitalisation, still
  makes one subscriber out of two differently-capitalised subscriptions, and
  still round-trips the longest address this application's own validation
  accepts. The keys still come out of the tree's encrypted credentials, under the
  `active_record_encryption` key Active Record reads them from on its own; the
  application's own configuration does not assign them, and they are nowhere
  in the environment.
- The three relations the export page builds are still sent to the database
  without the request waiting for each in turn, and the executor that makes
  that mean anything is still configured and still sized for the page. That
  page still costs fewer queries than it renders rows, and so does the page
  that lists every recorded answer for a question.
- No association, validation or callback passes SQL or a condition as a String
  to be evaluated in the model's context.
- `config/database.yml` is not in the tree you hand on. The configuration the
  application is given is copied into place from
  `config/database.yml.example`, which is the file that ships.
- The models still descend from `ApplicationRecord`, and nothing inherits from
  `ActiveRecord::Base` directly.
- Every migration the repository shipped still declares the schema version it
  was written against — which stays the version it was written against, not
  whichever release the tree now runs. All forty-three of them still say 4.2;
  the one an earlier step added is not one of them and is not held to 4.2.
  This step adds no migration and needs none.
- `db/schema.rb` still pins the primary-key type it was dumped with — the 32-bit key the
  tables have always had, while the release's own default for a table that
  names no key type is left as the release sets it rather than configured
  back to the narrow key application-wide — so a
  database built by loading the dump still has the same primary keys as one
  built by running the migrations, and the column an earlier step widened is
  still the unbounded type it widened it to.
- The defaults every earlier step adopted stay adopted, and stay adopted the
  same way: through the version declaration, with no `new_framework_defaults`
  file in the tree and no switch it used to list set by hand anywhere (the seven that 5.0's generated file listed are
  `per_form_csrf_tokens`, `forgery_protection_origin_check`,
  `raise_on_unfiltered_parameters`, `to_time_preserves_timezone`,
  `belongs_to_required_by_default`, `halt_callback_chains_on_return_false`
  and `ssl_options`).
  `belongs_to` is still required by default; the one association this
  application legitimately creates before its parent is known is still the only
  one marked optional, and it is still marked on the association itself.
- The three relations an earlier step marked as not being allowed to load an
  association on their own still carry that marker, and it is still declared
  where each relation is built rather than globally.
- The signing key the application uses at boot still comes out of the tree's
  encrypted credentials, the key that decrypts them still ships in the tree,
  and neither the store 5.1 used nor a plaintext key is anywhere in the tree
  under any name.
- Bootsnap is still set up from the tree's own boot file, and still caching.
- The content security policy is still declared, still reaches every HTML
  response as an enforced header naming the application's own origin, and the
  inline scripts the application serves still carry a nonce that the policy's own nonce generator
  (`content_security_policy_nonce_generator`) mints afresh for each request,
  named by that same response's policy. The inline scripts stay.
- Every HTML response still carries the five default security headers this
  application has settled on — the referrer policy, the content-type-options
  header, the frame-options header, the cross-domain-policies header, and the
  cross-site-scripting header an earlier step switched off — and still does not
  carry the legacy download-options header 7.1's defaults dropped. Adding that
  one back by hand is a default undone.
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
- The application's production log still goes to the process's own standard
  output, at the level it has always used.
- No comment anywhere directs a tool to ignore, skip or suppress a finding.

**What is allowed to change.** Nothing. This step has no behaviour exception at all: every page, every IVR callback, every mail,
every redirect, every recorded answer and every row the database ends up
holding is expected to be exactly what it is today. The release reworks the
wording of two lines this application logs, and this application has already
stopped printing them, so even that does not reach anything.

7.2 also offers two opt-in features this milestone does **not** take — a filter
that refuses browsers below a named version, and per-controller request rate
limiting. Both change what the application answers, neither is required by the
upgrade, and turning either on is a behaviour change nobody asked for. The
same goes for the files a 7.2 generator would put in a *new* application: the
container definitions, the progressive-web-app manifest and its routes, and the
rewritten `bin/` scripts. This is an upgrade, not a regeneration.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 7.2" means and every change listed above:

- Ruby on Rails 7.2 release notes — https://guides.rubyonrails.org/7_2_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Configuring Rails Applications —
  https://guides.rubyonrails.org/configuring.html
- Active Record and PostgreSQL —
  https://guides.rubyonrails.org/active_record_postgresql.html
- Testing Rails Applications —
  https://guides.rubyonrails.org/testing.html
- Debugging Rails Applications —
  https://guides.rubyonrails.org/debugging_rails_applications.html
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
any key the encryption needs is set in the environment, so whatever the
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
