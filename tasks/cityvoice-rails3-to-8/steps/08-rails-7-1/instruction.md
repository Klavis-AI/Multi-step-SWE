# Step 8 — Rails 7.0 → 7.1 on Ruby 3.2

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 7.0 under Ruby 3.1, with the column that holds residents' email addresses
encrypted at rest under a deterministic, case-folding scheme keyed out of the
tree's own credentials, with the CSV export's three relations sent to the
database together, with its own constants loaded by Zeitwerk, and taking its
framework defaults through a single `load_defaults` declaration. Move it to
**Rails 7.1 on Ruby 3.2**, and leave it behaving as it does today.

Two pins move: the framework to 7.1 and the interpreter to 3.2. Rails 7.1
runs on both — on 3.1, which is what the tree arrives on, and on 3.2, which is
where it has to end up — and that is what makes the pair readable as one move.
Under a framework that runs on either side of the interpreter change, a
failure belongs to the interpreter and not to the release.

This step has a floor to clear before any of it can boot. Ruby 3.2 finally
removed a long-deprecated `File` predicate, and this application calls it —
including from the file that runs before the framework does, so the failure
arrives before anything that could report it well. Two of the Gemfile's own
pins are below that floor as well. `sass` is pinned on the 3.2 line, and that
line calls the same predicate while it is being required: the pin has to end
up off the 3.2 line, on a release of the same gem that does not call it.
`foundation-rails` is pinned where it is only because `sass` was — the
Gemfile writes the reason down in a comment above the pin — and it has to end
up on a release whose stylesheets compile under the `sass` that replaces it,
or the stylesheet stops compiling and every page that links it stops being
served. Neither of those two is swapped for a different gem here; they move.
And the test-framework integration on its current major version cannot see
Action View 7.1's template resolvers at all. None of that is the milestone. It is
what the milestone costs.

Above the floor, this step is about saying things the application has been
doing by side effect, and saying them in the vocabulary 7.1 adds: where the
application keeps the code it holds outside `app/`, and what each of its view
partials actually takes.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Ruby 3.2 and on Rails 7.1.
- The Gemfile declares the Ruby the application runs on, `.ruby-version` names
  that same version, and `Gemfile.lock` records that same Ruby alongside the
  resolved gem set — one version, named in all three places.
- The whole dependency set installs on Ruby 3.2 and resolves against Rails 7.1
  — not just the framework gems. That is the requirement and it is the whole
  of it: how much of the lockfile moved is not part of it. Advice
  rather than a condition: a resolution that drags the whole lockfile forward
  because it was asked to move one pin is a pile of behaviour changes nobody
  asked for, and the tree still has to boot, behave and pass its suite with
  every one of them in it.
- The method Ruby 3.2 removed appears nowhere in the application's own code.
  The method is `File.exists?`, and the same removal took the `exists?` on
  `Dir` and on `FileTest` with it. Not in what the suite exercises, and not in
  what runs before the framework loads. Defining it back onto `File` is not a way to satisfy this: it leaves
  every call site exactly where it was, and the gem that calls it is fixed by
  the pin above.
- A lockfile that resolved is not evidence the set works. Booting the
  application in each environment, driving it, and running its suite is.

**Framework defaults.** The declaration that names the release whose defaults
this application takes moves one version on:

- The declaration names 7.1.
- And the application actually gets what 7.1 brings with it. Naming the version
  and then configuring the new behaviours back off is not the milestone. The
  list below is the release's own, not a work plan: the declaration turns all
  of it on at once, and each of these is in force in the tree you hand back.
  - The autoload paths are no longer added to the interpreter's load path.
  - The parameters the framework filters out of its logs are compiled once when
    the application boots rather than re-derived at each use.
  - HTML sanitizing is done by this release's HTML5 vendor rather than by the
    HTML4 one.
  - Tests that compare markup parse it as HTML5.
  - A column with no serializer declared on it is not silently YAML. The
    release's default is no serializer at all.
  - A record's secure tokens are generated when it is initialized rather than
    when it is created.
  - The digest the encryption scheme this application adopted in the previous
    step uses for its own hashing is the 256-bit member of the SHA-2 family.
  - Commit callbacks run against every record saved in the transaction, not
    only the first instance of each.
  - A transaction whose block exits by a non-local return commits rather than
    rolls back.
  - Callbacks registered to run after a transaction run in the order they were
    defined.
  - Assigning to an attribute the model declares read-only raises.
  - The singular spelling of a `has_many` association's name is no longer
    accepted where the plural belongs.
  - Active Record objects are marshalled in this release's format.
  - Entries the framework writes to the cache use this release's own entry
    format.
  - The serializer behind the framework's own signed and encrypted messages --
    the cookie jar among them -- is the one that emits JSON and still reads
    what the old one wrote.
  - A cache expiry that is not a valid duration raises rather than being
    quietly ignored.
  - The request parameters object no longer compares equal to a plain hash.
  - An exception logged for debugging is logged at the error level rather than
    at the fatal one.
- And one of those reaches the wire. This release's default response headers
  drop the legacy download-options header. Every HTML response it serves stops
  carrying it — and keeps the other five default security headers it has been
  sending: the referrer policy, the content-type-options header, the
  frame-options header, the cross-domain-policies header, and the
  cross-site-scripting header the previous step switched off. Dropping the one
  is this release's doing and arrives with the declaration; dropping any of the
  other five is not this milestone, and adding the removed one back by hand is
  the declaration undone.
- The 5.0, 5.1, 5.2, 6.0, 6.1 and 7.0 defaults this application adopted in the
  previous steps are still in force. 7.1's defaults are 7.0's plus the list
  above, so a tree that reaches 7.1 by replacing the declaration keeps them; a
  tree that reaches it by adding a second declaration, or by deleting the
  first, does not.

**Where the application keeps the code it holds outside `app/`.** This
application has a `lib/` directory with code in it, and reaches that code
today through a side effect of one path assignment. Rails 7.1 adds a
declaration that says the thing itself.

- `lib` is on the application's autoload paths and on its eager-load paths, and
  it is declared once, by the method this release adds for exactly this.
- The declaration excludes the two directories under `lib` that hold no Ruby —
  the one that holds assets and the one that holds rake tasks. An autoloader
  told to treat either as a root will try to define a constant for the first
  `.rb` anyone ever puts there; the old side-effect spelling could not say so
  and this one can.
- The `lib` line the previous steps wrote is gone. Two declarations of the same
  directory is not this requirement met twice.

**What each view partial takes.** Rails 7.1 lets a partial declare its locals
as a real signature, in a magic comment on its first line, and this is the
step's own work.

- Every partial this application actually renders declares the locals it takes.
- And the framework really compiled the declaration, rather than reading a
  comment. A declaration that is not the first thing in the template, or is
  spelled where the framework does not look for it, is a comment: the
  application renders exactly as it did and nothing is declared. What separates the two is behavioural — hand such a partial a local it does
  not declare and it raises, where an undeclared partial takes it silently.
- The signatures say what the call sites actually pass, which is not always
  what the partial's body reads: a partial rendered from two call sites that
  pass different sets of locals cannot declare them all as required.
- Declaring the signature is not the whole of that requirement. Under a strict
  signature a declared local exists whether or not the caller passed it, so a
  body that decides what to render by asking whether one is *defined* is
  asking a question with a new answer. Both call sites still render what they
  render today.
- The partials no page, mail or IVR response in this application renders are
  out of scope — there are four of them, and they need not change. A
  signature on a partial nothing renders is a declaration against no caller: it cannot be wrong and it cannot be right. They are dead
  code and this step does not ask anyone to delete them.

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

Note where this bites on this step: for the first time it is the suite's own
setup and not the application's code. Two spellings in it are deprecated by
the release being adopted here.

- The check the suite runs before its examples, to refuse to run against a
  database with migrations outstanding, is deprecated in favour of one that
  asks every database configuration the environment declares instead of the one
  connection the caller happens to be holding. This application has one
  database, so the two ask the same question — move to the new spelling anyway:
  the condition above is about what the suite raises, and the old spelling does.
- The setting that tells the test framework where the fixtures are is now
  plural and takes a list. Give it a list.

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
  the one the previous step added is not one of them and is not held to 4.2.
  This step adds no migration and needs none.
- `db/schema.rb` still pins the primary-key type it was dumped with — the 32-bit key the
  tables have always had, while the release's own default for a table that
  names no key type is left as the release sets it rather than configured
  back to the narrow key application-wide — so a
  database built by loading the dump still has the same primary keys as one
  built by running the migrations, and the column the previous step widened is
  still the unbounded type it widened it to.
- The 5.0, 5.1, 5.2, 6.0, 6.1 and 7.0 defaults stay adopted, and stay adopted
  the same way: through the version declaration, with no `new_framework_defaults`
  file in the tree and no switch it used to list set by hand anywhere (the seven that 5.0's generated file listed are
  `per_form_csrf_tokens`, `forgery_protection_origin_check`,
  `raise_on_unfiltered_parameters`, `to_time_preserves_timezone`,
  `belongs_to_required_by_default`, `halt_callback_chains_on_return_false`
  and `ssl_options`).
  `belongs_to` is still required by default; the one association this
  application legitimately creates before its parent is known is still the only
  one marked optional, and it is still marked on the association itself.
- The three relations the earlier step marked as not being allowed to load an
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
- The application's production log still goes to the process's own standard
  output, at the level it has always used.
- No comment anywhere directs a tool to ignore, skip or suppress a finding.

**What is allowed to change.** This step is a behaviour-preserving move with
one exception, and the exception is not something you write:

- The two lines the framework prints while it is sending a weekly digest — the
  one that says it rendered the mail's template and the one that says it
  delivered the message — are no longer printed. Rails 7.1 gives its own log
  subscribers a per-event level and reclassifies both of these as debug
  diagnostics; this application logs production at the informational level, so
  the events are dropped before they are published. The mail is still built,
  still addressed to the same recipients and still carries the same content,
  and the task still finishes successfully and prints nothing at all. Adopt it,
  and do not write code to restore the old answer. In particular, turning the
  production log level down to debug to get those two lines back is not a way
  to satisfy anything here, and moving the level the other way is not a way to
  satisfy it either. The level this application logs production at is one of
  the properties above; the two lines go because the release reclassified the
  events that produce them, not because the level moved.

Everything else this application does — every page, every IVR callback, every
mail, every redirect, every recorded answer and every row the database ends up
holding — is expected to be exactly what it is today.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 7.1" means and every change listed above:

- Ruby on Rails 7.1 release notes — https://guides.rubyonrails.org/7_1_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Autoloading and Reloading Constants —
  https://guides.rubyonrails.org/autoloading_and_reloading_constants.html
- Action View Overview, on partials and their locals —
  https://guides.rubyonrails.org/action_view_overview.html
- Layouts and Rendering in Rails —
  https://guides.rubyonrails.org/layouts_and_rendering.html
- Configuring Rails Applications —
  https://guides.rubyonrails.org/configuring.html
- Ruby 3.2.0 release notes — https://www.ruby-lang.org/en/news/2022/12/25/ruby-3-2-0-released/
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
