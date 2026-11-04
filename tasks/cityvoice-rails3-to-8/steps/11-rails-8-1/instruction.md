# Step 11 — Rails 8.0 → 8.1 on Ruby 3.2

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 8.0 under Ruby 3.2, taking its framework defaults through a single
`load_defaults` declaration, and annotating its models with the maintained fork
of a tool that could not come to Rails 8. Move it to **Rails 8.1**, and leave
it behaving as it does today.

The interpreter does not move. Rails 8.1 runs on the one this tree already has
— the framework asks for 3.2 or above and the tree arrives on 3.2 — so the
interpreter stays exactly where it is, and changing it is not part of this
milestone. On the framework pin by itself the whole set installs, the
application boots in all three environments, and its suite runs green.

So, as in the two previous steps, the work is not repair. It is the part the new
framework does not do for you, and in this step that is two things: one
release's worth of new defaults, actually adopted; and one route this
application serves, whose drawing this release deprecates — and whose shape it
has already quietly changed by the time it says so.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Rails 8.1, and it is 8.1 that the running process
  loads — not 8.0, and not a release above it.
- The interpreter does not move. The application still runs on Ruby 3.2, the
  Gemfile still declares it, `.ruby-version` still names that same version, and
  `Gemfile.lock` still records it alongside the resolved gem set.
- The whole dependency set installs on Ruby 3.2 and resolves against Rails 8.1
  — not just the framework gems. Advice rather than a condition: a resolution
  that drags the whole lockfile forward because it was asked to move one pin is
  a pile of behaviour changes nobody asked for, and the tree still has to boot,
  behave and pass its suite with every one of them in it. There is a
  conservative resolution of this Gemfile in which the framework's own gems
  move, one dependency of the framework's graph arrives, one leaves, and
  nothing else moves that you did not move on purpose.
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

- The declaration names 8.1.
- And the application actually gets what 8.1 brings with it. Naming the version
  and then configuring the new behaviours back off is not the milestone. The
  release turns all of it on at once, and each of these six is in force in the
  tree you hand back:
  - JSON no longer has the Unicode line and paragraph separators escaped in it.
    U+2028 and U+2029 are emitted as themselves rather than as `\u2028` and
    `\u2029`. This one could move a byte of a JSON body this application
    serves — its map endpoints render JSON out of recorded location names —
    and on the data this application actually carries it moves none, because
    none of that text contains either character. HTML entity
    escaping inside JSON is a different setting, is not part of this release's
    defaults, and is not this step's concern.
  - A response rendered by handing an object straight to the renderer as JSON
    is no longer HTML-escaped on the way out. That is a change to a rendering
    path this application does not use: its JSON comes out of templates. It is
    still expected to be at its 8.1 value in the tree you hand back.
  - A redirect to a path-relative location — one with no leading slash — raises
    instead of being logged and followed. Nothing in this application's own
    controllers redirects at all, so this one is a declaration and not a
    behaviour here.
  - An order-dependent finder called on a relation with no order, against a
    model with no order column to fall back on, raises rather than warning.
    Every model in this application has a primary key, so every such finder has
    something to fall back on and the suite stays green with this in force.
  - The view layer works out which templates a template renders with a Ruby
    parser rather than with a regular expression. It is an internal of template
    dependency tracking and nothing this application serves depends on it.
  - The hidden inputs the framework's form builder renders stop carrying the
    attribute that told the browser not to autofill them. The email
    notifications partial the location page renders builds its form with that
    builder and declares a hidden field of its own, so this one changes the
    HTML the application serves: an attribute disappears from a field the
    application itself asked for. A second view declares the same field the
    same way and nothing renders it; it is out of scope and need not change,
    like the other partials nothing renders.
- The 5.0, 5.1, 5.2, 6.0, 6.1, 7.0, 7.1, 7.2 and 8.0 defaults this application
  adopted in the previous steps are still in force, with the one exception the
  release itself makes and which is stated here rather than left for you to
  find. 8.1's defaults are 8.0's plus the list above, so a tree that reaches
  8.1 by replacing the declaration keeps them; a tree that reaches it by adding
  a second declaration, or by deleting the first, does not.
- The exception, and it is the same behaviour the previous step asked you to
  adopt. The option that decided whether converting a time to a `Time` keeps
  the receiver's time zone is deprecated outright in this release: the
  conversion keeps the zone unconditionally now, the framework no longer
  assigns the option as part of anybody's defaults, the class that answers for
  it answers `nil`, and merely *reading* it raises a deprecation warning. The
  behaviour the previous step adopted is still what this tree does — the release
  took the switch away rather than the behaviour — and that is the release's
  decision, not a regression here. Nothing asks you to do anything about it,
  and setting it by hand, to either value, is worse than doing nothing: the
  warning is attributed to the application file where the framework is
  configured, so a tree that sets it fails the deprecation conditions below.
  The other two settings 8.0 brought, the entity tag deciding a conditional
  request on its own and the one-second global regular-expression timeout, are
  unaffected and are still expected to be in force.
- One more thing the release decides for itself, stated here so that you do not
  go looking for it: whether the interpreter's just-in-time compiler is enabled
  is now a framework default, and the value it takes depends on the environment
  the application is running in. Nothing is asked of you about it, in either
  direction.

**The static page route.** This application serves its static pages —
`/pages/about`, `/pages/contact`, `/pages/messages` — from a route it does not
draw itself: a gem in the bundle draws it, out of the gem's own four-line
routes file. 8.1 changes what that drawing does, twice, and warns about only
one of the two.

- The release gave the HTTP verb helpers a keyword signature and left the old
  positional Hash working behind a deprecation. This application's own
  `config/routes.rb` is unaffected by that, hash rockets and all: Ruby turns a
  bare trailing hash literal into keywords before the helper ever sees it, so
  `get '/foo' => 'bar#baz'` arrives as keywords and never touches the
  deprecated path. A Hash held in a *variable* cannot be converted that way,
  and passing one is exactly what the gem does.
- The first cost is two deprecation warnings per suite run. The route set is
  drawn lazily, inside the first request the suite makes, so the frame the
  framework attributes them to is a file under `spec/` — a file that has
  nothing wrong with it. Under the suite condition below those two warnings are in scope, and editing the file that gets named will not remove them.
- The second cost is the one nothing warns about. The Hash the gem passes
  carries `:format => false` in it, the deprecated path drops that key on the
  way through, and the optional format segment comes back. Handed the pin
  alone, this application's `page` route stops being `/pages/*id` and becomes
  `/pages/*id(.:format)`. The suite does not notice, and no warning mentions
  it.
- So the condition, and it is about the route rather than about the gem. In the
  tree you hand back the route named `page` is still there, it still goes to
  `high_voltage/pages#show`, its path is still exactly `/pages/*id`, and it is
  no longer drawn by passing a positional Hash to a verb helper.
- There are two ways to reach that and **either will do**, because both are
  real migrations of this application: move that gem's pin to a release that
  splats the Hash into keywords itself, or turn the gem's route drawer off and
  draw the route in this application's own `config/routes.rb`. The first is the
  smaller change; the second is what an application has to do when the gem is
  unmaintained. Nothing here prefers one, and it does not matter which file the route ends up declared in or where in that file it sits.
- If you draw it yourself, draw the route that is there.
  `get '/pages/*id' => 'high_voltage/pages#show', as: :page` is not it: a glob
  segment picks the optional format segment up unless it is told not to, so
  that line reproduces the exact `/pages/*id(.:format)` the pin alone produces.
  The gem's own routes file is four lines long and is in your bundle path;
  read what it passes before you replace it.
- The rest of the routing table is expected to be exactly what it is today:
  same routes, same names, same paths, in the tree you hand back.

**A rake task disappears, and nothing is asked of you about it.** The release
removes the deprecated `stats` task. The list of tasks this application can
produce is one entry shorter than it was, and that is the release's own
decision rather than damage in this tree: what matters here, as in the previous step, is that the whole list can still be produced in `development` and that
the process producing it exits cleanly. Re-implementing the removed task is not
part of this milestone.

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
  not part of this milestone, and that one warning is exempt from this condition. The two warnings the route drawer raises
  are a different matter: they are new in this step, they are named under the static page route above, they fall under this condition, and what to do about them is said there.

All three conditions are about what the tree *raises*, not about what it prints.
Turning the warnings off — `config.active_support.deprecation = :silence`,
`ActiveSupport::Deprecation.silenced = true`, a `silence` block around the
call — leaves the deprecated call exactly where it was, and satisfies none of
them.

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
  removed by the previous step, and restoring it is not part of this one. This
  step adds no file to that set and removes none.
- The schema comment at the top of each of the seven model files is still
  there, with every column it lists today still listed in it. Re-generating
  those comments with the tool the tree now carries is fine; losing them, or
  leaving a header with the columns stripped out of it, is not.
- The annotation tooling is still the maintained fork the previous step swapped
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
- The defaults every earlier step adopted stay adopted, and stay adopted the
  same way: through the version declaration, with no `new_framework_defaults`
  file in the tree — of any version, including the one this release generates
  for applications adopting its defaults one at a time — and no switch such a
  file used to list set by hand anywhere (the seven that 5.0's generated file listed are
  `per_form_csrf_tokens`, `forgery_protection_origin_check`,
  `raise_on_unfiltered_parameters`, `to_time_preserves_timezone`,
  `belongs_to_required_by_default`, `halt_callback_chains_on_return_false`
  and `ssl_options`), with the one exception named under
  framework defaults above, which is a setting this release withdrew rather
  than one the tree gave up. `belongs_to` is still required by default; the one
  association this application legitimately creates before its parent is known
  is still the only one marked optional, and it is still marked on the
  association itself.
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

**What is allowed to change.** The gem that draws the static page route, or
where that route is drawn — one of the two, as above. Nothing else: every page,
every IVR callback, every mail, every redirect, every recorded answer and every
row the database ends up holding is expected to be exactly what it is today.

8.1 also ships things for *new* applications that this milestone does **not**
take. The local test runner it now generates — `bin/ci` and its configuration
file — and the dependency audit beside it are generator output rather than part
of `load_defaults 8.1`, and this application has a suite of its own and its own
way of being run. Each of those is new infrastructure rather than a migration,
and adding any of them here is a behaviour change nobody asked for. This is an
upgrade, not a regeneration.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 8.1" means and every change listed above:

- Ruby on Rails 8.1 release notes — https://guides.rubyonrails.org/8_1_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Configuring Rails Applications —
  https://guides.rubyonrails.org/configuring.html
- Rails Routing from the Outside In —
  https://guides.rubyonrails.org/routing.html
- The Rails Command Line and rake tasks —
  https://guides.rubyonrails.org/command_line.html
- Each gem's own source, gemspec and CHANGELOG, as installed under your bundle
  path — including the one that draws a route this application serves, whose
  own `config/routes.rb` is four lines long and is the reason this step has a
  second subject. Its newer releases are on the gem host like any other, and
  reading what one of them changed is reading a dependency, not a hint.

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
- Interpreter warnings are off; the deprecation conditions above are about what driving the application, booting it in each environment and running its suite raise, not about what reaches stderr.
- The interpreter is the one rbenv selects from the tree's own
  `.ruby-version`, and the image this step runs in carries exactly one
  interpreter: the one this milestone names.
- A tree that does not boot is not partly done: almost everything above is a property of the running application, so a tree that cannot start has none of it. Leave yourself time to boot, drive and
  test what you hand back.
