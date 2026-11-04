# Step 12 — Ruby 3.2 → 3.4 on Rails 8.1

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 8.1 under Ruby 3.2, taking its framework defaults through a single
`load_defaults` declaration, serving its static pages from a route drawn with
keywords. Move it to **Ruby 3.4**, and leave it behaving as it does today.

The framework does not move. Rails 8.1 is the release this tree runs and the
release it still runs when you hand it back; 8.1 supports 3.4 and this is a
milestone about the interpreter underneath it. Nothing in the dependency graph
asked for the move either — the framework's floor is still Ruby 3.2 and the
tree has cleared it since it arrived on 3.2. The reason is the one a migration
ends on: 3.2 is two minor versions behind the interpreter the current framework
is developed against, and a tree that has just reached the current framework
should not be standing on an interpreter that is not.

So the work is not repair, and it is not a resolution either. On the version
change by itself the whole set still resolves and the framework still loads.
What 3.4 takes away is the thing this step is about: it is the release where
Ruby finished moving libraries out of the set the interpreter hands out for
free and into the set a `Gemfile` has to name, and a program started under
Bundler may require only what its `Gemfile` names.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Ruby 3.4 — the version of the process that boots it,
  not a version named in a file.
- One version, named in all three places. The `Gemfile`'s `ruby` directive,
  `.ruby-version` and the `RUBY VERSION` line in `Gemfile.lock` all name the
  same interpreter, and it is 3.4. Two out of three is not partway there: it
  is a tree that boots on one interpreter and tells the next machine to use
  another. The lockfile's copy is the one nothing will complain about, and it
  is the one a deployment reads.
- The framework does not move. The application still runs on Rails 8.1, its
  declaration still names 8.1, and it is 8.1 the running process loads.
- The whole dependency set installs on Ruby 3.4 and resolves against Rails 8.1
  — not just the gems this step has to name. Advice rather than a condition: a
  resolution that drags the whole lockfile forward because it was asked to move
  one pin is a pile of behaviour changes nobody asked for, and the tree still
  has to boot, behave and pass its suite with every one of them in it. There is
  a conservative resolution of this Gemfile in which the gems this step names
  arrive, one gem already in the lock moves, and nothing else moves that you
  did not move on purpose.
- A lockfile is a resolver's output, and hand-editing it does not resolve
  anything. The file records, under each gem, the dependencies that gem
  declares; a version written in by hand carries the *other* version's
  dependency list with it, and a container that already has the gem unpacked
  will install a lockfile that no clean machine can. Resolve the set with the
  resolver, and check what you hand back by installing it into an empty bundle
  path rather than by reading it.
- A lockfile that resolved is not evidence the set works. Booting the
  application in each environment, driving it, and running its suite is.

**The libraries the interpreter stops shipping.** Ruby has been moving standard
library files into gems for several releases, and 3.4 is where that reaches
this application. A file that used to be there for anyone who required it is
now a gem like any other, and under Bundler an undeclared gem is not on the
load path at all.

- Every library this application's code, its dependencies or its suite requires
  is available to the process that requires it, in every environment. Where 3.4
  no longer supplies one, this tree's `Gemfile` names it and its lockfile
  resolves it.
- There are three of these here and they fail in three different places, so a
  tree that only boots is not a tree that has found them all:
  - one is required by the application's own code, at the top of a model and at
    the top of each of the three importers under `lib/`;
  - one is required by the test framework the suite loads, and is therefore
    invisible to anything that only reads or boots the application;
  - one is required by the factory library the suite loads, and has the same
    shape.
  Each raises on `require`, so each is findable by running the thing that needs
  it. Find them by running the application and its suite, not by reading this
  list — it is here to tell you how many places to look.
- Pin each to the version the interpreter itself was going to hand out, unless
  you have a reason to do otherwise. Naming the code that was already going to
  be loaded is a declaration; choosing a different release of it is a change
  this milestone did not ask for.
- Nothing about this is a licence to rewrite what requires them. Deleting the
  `require` and the code under it, or vendoring a copy into the tree, is not a
  migration of this application.

**And one that does not raise.** A fourth library is on its way out of the
default set but has not left yet: it still loads on 3.4, and all it does is say
that it will not be there forever. It is an ordinary interpreter warning and
not a deprecation warning, so nothing that collects deprecations sees it and
the suite stays green with it firing.

- No library on its way out of the default set is loaded out of the
  interpreter's own copy of it. Not in `production`, and not in the
  environments that boot beside it. The condition is on the outcome and not on
  how you reach it: naming the library in the `Gemfile` clears it, because the
  copy that loads is then the one the bundle resolved, and so does no longer
  requiring the library at all. This application uses it in exactly one place,
  so both are real migrations of it and neither is preferred here.
- The interpreter says so while it happens, once per library, wherever warnings
  are on. That sentence is how you find it. It is not the condition, and a tree
  that stops the sentence being printed has not moved the library.
- Where it is reached from is worth knowing before you decide: the require sits
  at the top of a model, and an initializer names that model's constant inside
  a `to_prepare` block, so it is reached on every boot in every environment —
  including the two that do not eager-load. "It only happens in production" is
  not true of this one.

**A pin eleven steps old.** One gem in the lockfile has been sitting at its
2019 release since the Rails 4.2 baseline: nothing in this tree ever named it,
two dependencies bring it in, and a conservative resolution never had cause to
move it. It is a C extension that defines an allocator for a class the
interpreter allocates itself, and every interpreter since 3.2 prints a warning
about that on every boot that loads it -- `warning: undefining the allocator of
T_DATA class <Class>`, with the class the extension defines in the place of
`<Class>`. This tree has printed it since the interpreter moved to 3.2 and no step has answered it.

- That gem is off its 2019 release in the tree you hand back, on a release from
  its current major line. Which one is yours to choose; the interpreter you are
  moving to bundles one, and naming that one is the smallest change that works.
- Adding it to the `Gemfile` is how a gem nothing declares gets a version you
  chose. It is still a dependency of the two gems that ask for it; declaring it
  does not take it out of their hands, it takes it out of the resolver's.
- It is not the only C extension in this tree that prints that warning. Another
  gem, one the `Gemfile` has pinned since the repository was written, prints the
  same sentence about a class of its own, and that one is not this pin and is
  not part of this step. The class the warning names is what tells them apart,
  and the gem this one is about is the one nothing in the tree declares.

**Deprecations.** Three conditions, unchanged from the previous step:

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
them. The same goes for the interpreter's own warnings and the library
conditions above: lowering the warning level, reassigning `$VERBOSE`, or
prepending something to `Warning` hides the sentence and changes nothing about
the load path it is describing.

**Integrity.** This step is not a rewrite, and everything the previous steps
established is still true of the tree you hand back. Every item on this list must hold, and so must the deprecation conditions above.

- Every Ruby and ERB file in the tree parses, under the interpreter the tree
  names.
- `rake` loads. Asked for the whole task list, in `development`, the tree
  produces it and exits cleanly.
- The application is still there: every file the repository shipped under
  `app/models`, `app/controllers`, `app/helpers`, `lib` and `db/migrate` is
  still at its path. The one exception is already made and is not yours to
  make again: the rake file that configured the retired annotation gem was
  removed by an earlier step, and restoring it is not part of this one. This step
  adds no file to that set and removes none.
- The application still takes its framework defaults through a single
  declaration and that declaration still names 8.1, with no
  `new_framework_defaults` file in the tree — of any version — and no switch
  such a file used to list set by hand anywhere (the seven that 5.0's generated file listed are
  `per_form_csrf_tokens`, `forgery_protection_origin_check`,
  `raise_on_unfiltered_parameters`, `to_time_preserves_timezone`,
  `belongs_to_required_by_default`, `halt_callback_chains_on_return_false`
  and `ssl_options`). Every default the release
  brings is still in force: the Unicode separators still unescaped in JSON, the
  direct JSON render still unescaped, a path-relative redirect still raising,
  an unordered order-dependent finder still raising, the template dependency
  tracker still the parser rather than the regular expression, and the
  framework's hidden inputs still rendered without the autofill attribute. The
  option this release withdrew is still unset, in either direction.
- The route named `page` is still there, it still goes to
  `high_voltage/pages#show`, its path is still exactly `/pages/*id`, and it is
  still not drawn by passing a positional Hash to a verb helper. The rest of
  the routing table is expected to be exactly what it is today: same routes,
  same names, same paths.
- The schema comment at the top of each of the seven model files is still
  there, with every column it lists today still listed in it. Re-generating
  those comments with the tool the tree carries is fine; losing them, or
  leaving a header with the columns stripped out of it, is not.
- The annotation tooling is still the maintained fork an earlier step swapped
  in, at a release of its 4.x line or above, and the capped gem it replaced is
  in neither the Gemfile nor the lockfile. Nothing in the tree calls the
  retired gem's configuration API — its `set_defaults` and its `load_tasks`,
  written out anywhere under the repository, including through `send`,
  `public_send` or `__send__`.
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
- The test environment still lets exceptions out of the request cycle: when an
  example drives a request that raises, the exception still reaches the
  example rather than being turned into a rendered error page, and *every*
  exception is let out on the same terms, including the ones the framework has
  a status for.
- The master process still forks without holding a database connection, and the
  hook that does it still does not take one out in order to throw it away. The
  deprecated accessor that hands out a permanent connection checkout still appears nowhere: not written out on `ActiveRecord::Base` or on `ApplicationRecord`, anywhere under the repository. The hook that runs *after* a worker
  forks is still not part of this step and need not change.
- An untyped date read back from the database is still a date object rather
  than a string, on the live connection this application opens.
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
- `belongs_to` is still required by default; the one association this
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

**What is allowed to change.** The interpreter the tree names, the `Gemfile`
lines and lockfile entries that follow from it, and — if you take that route
for the library that only warns — the single place this application uses it.
Nothing else: every page, every IVR callback, every mail, every redirect, every
recorded answer and every row the database ends up holding is expected to be
exactly what it is today.

3.4 ships things this milestone does **not** take. Its frozen string literal
machinery, its new `it` block parameter and the rest of the language's own
additions are not migrations of this application, and rewriting working code to
use them is a behaviour change nobody asked for. This is an upgrade, not a
modernisation.

Two more, named because they are the first things a reader of the release notes
reaches for. The just-in-time compiler is not asked for and is not a condition
here: whether an interpreter has one is a property of how it was built, not of
this tree, and a tree that turns it on where it is absent has changed nothing
except a line it now has to keep. The parser 3.4 compiles with is likewise the
interpreter's business; what this tree owes is that every file in it parses
under the interpreter it names, which is a condition above. And raising the
interpreter's own warning level -- for deprecated features or anything else --
is not one of the conditions above: the conditions on this page are about what the tree loads, raises and does, not about what any stream shows.

## 2. Read these local references

**Normative semantics.** Ruby's own release documentation defines what "on Ruby
3.4" means and every change listed above:

- Ruby 3.4.0 release notes — https://www.ruby-lang.org/en/news/2024/12/25/ruby-3-4-0-released/
- Ruby 3.4 NEWS, as shipped with the interpreter you install
- Bundler's documentation on the `ruby` directive —
  https://bundler.io/guides/gemfile_ruby.html
- Ruby on Rails 8.1 release notes, for the framework this tree stays on —
  https://guides.rubyonrails.org/8_1_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Each gem's own source, gemspec and CHANGELOG, as installed under your bundle
  path. The libraries this step is about ship as gems now, and each of them has
  one; the interpreter's own `lib/ruby/gems` directory says which versions it
  was going to hand out.

**In the repository.** `README.md` and `code-base-overview.md` describe what the
application does and how it is run; `Procfile` gives the web command;
`config/database.yml.example`, `sample.env` and `app.json` describe the
configuration it expects; `spec/` is the application's own test suite;
`lib/tasks/` is where this application keeps the rake tasks it added itself.

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
- Interpreter warnings are off in the processes that run the application and
  its suite. Nothing on this page is a condition about what is printed, so
  nothing on this page is changed by what a process does or does not print.
- The interpreter a process in this tree gets is the one rbenv selects from the
  tree's own `.ruby-version`. A version this tree declares and does not run on
  is not the milestone, and neither is a version it runs on and does not
  declare.
- A tree that does not boot is not partly done: almost everything above is a property of the running application, so a tree that cannot start has none of it. Leave yourself time to boot, drive and
  test what you hand back.
