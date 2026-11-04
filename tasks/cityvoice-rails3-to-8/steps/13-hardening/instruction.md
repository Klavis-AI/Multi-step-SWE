# Step 13 — Known vulnerabilities, and what the log keeps

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
**Rails 8.1 under Ruby 3.4**, twelve releases and eight interpreters from where
it started. The migration is over. Nothing moves a release in this milestone
and nothing in the framework changes.

What is left is the reason the migration was undertaken. A tree can arrive at
the current release of everything and still be carrying the two things an
upgrade is supposed to leave behind:

- its resolved dependency set contains gems with **published security
  advisories** against exactly the versions it resolved; and
- driving it writes the **telephone numbers and e-mail addresses of members of
  the public** into its production log, in plain text.

Read your own tree for both before you change anything. What is true of the
tree in front of you is the work; a property it turns out not to have is not
work to invent, and a property it has that this page does not mention is still
yours to leave alone.

Both are properties of the tree rather than of any tool, both show in the application you hand back when it is driven, and neither is a version bump with a
CHANGELOG to read. The first is a resolution problem with decisions inside it.
The second is a configuration problem that takes one line to fix and rather
longer to be sure of.

The tree you hand back must have all of these properties.

**The dependency set carries no known vulnerabilities.**

- No gem in `Gemfile.lock` has a published security advisory against the
  version the lockfile resolved. Not one that the `Gemfile` declares, and not
  one that arrived only because something else asked for it.
- The affected gems are not all the same kind of problem, and the two kinds have to be handled separately. Some are **declared by name in the `Gemfile`**, several
  of them pinned to an exact version, so each of those is a decision about what
  to move it to. The rest are **nobody's declared dependency**: they arrive
  underneath something else, and they move only when whatever asks for them is
  allowed to move. A tree that answers the declared ones and never looks at
  what moved underneath them has done part of the work, and `bundle install`
  will succeed without mentioning the rest. How many there are of each kind,
  and which gems they are, is a property of the lockfile you were handed rather
  than a number to take from this page — read your own `Gemfile.lock` against
  the snapshot and find out.
- The advisory data is in the environment, local and pinned: a snapshot of
  the community Ruby advisory database at `/opt/ruby-advisory-db`, and
  `bundle-audit` is installed and already pointed at it, so
  `bundle-audit check --no-update` answers the question as it stands today.
  Use it or do not; the condition is on the tree, and it holds against that same snapshot however you arrive at it.
- **An advisory that is ignored is not an advisory that is fixed.** A
  `.bundler-audit.yml`, an ignore list, a suppression comment or a flag that
  drops findings from a report changes nothing about what the tree resolved.
  Do not hand back any such file: it would not be honoured, and its presence in the tree is itself a defect.
- Move what has to move, and no more. There is a conservative resolution of
  this `Gemfile` in which the gems that have to move arrive, what depends on
  them follows, and nothing else moves that you did not move on purpose. A
  resolution that drags the whole lockfile forward because it was asked to move
  a few pins is a pile of behaviour changes nobody asked for, and the tree still
  has to boot, behave and pass its suite with every one of them in it.
- One of them may not be an ordinary library. The minifier the asset pipeline
  runs every piece of JavaScript this application serves through — in
  `production`, at boot — is among the gems that can carry an advisory here. If
  it is one of yours, moving it moves what the pipeline emits: the minified
  output will not be byte-for-byte what it is today, and that is a consequence
  of the move rather than something to avoid. What the assets *do* is what has
  to be unchanged. Check that they still do it.
- A lockfile is a resolver's output, and hand-editing it does not resolve
  anything. A version written in by hand carries the *other* version's
  dependency list with it, and a container that already has the gem unpacked
  will install a lockfile that no clean machine can. Resolve the set with the
  resolver, and check what you hand back by installing it into an empty bundle path outside the tree (anything installed
  under `/app` is part of the tree you hand back) rather than by reading it.
- Removing the feature that uses a gem is not a migration of this
  application: everything it does today it still does.

**The log does not record the callers.**

This application is an IVR. Every request the telephone provider makes to it
carries the number of the member of the public who rang the survey line, and
the number they dialled to get there. The neighbourhood-subscription form
carries an e-mail address, nested inside the form's own parameter. The
framework's request logger writes the whole parameter hash at the level
`production` runs at, so all of it is in the log file, in the clear, for
anything that later reads or ships that file.

- Driving the application in `production` writes **no telephone number** into
  its log. Not one line.
- And **no e-mail address**. Not one line.
- What matters is the log the application writes while it is driven, not any file in `config/`. A tree that declares the right thing and is not running it meets neither condition.
- The log still records what it records today, in the framework's own words:
  a `Started <VERB> "<path>"` line as each request arrives, a `Parameters:`
  line carrying what that request was given, and a `Completed <status>` line as
  the response goes out. All three of them, and as often as today. Raising the
  log level, turning the request logger off, sending the log somewhere else or
  dropping parameter logging altogether makes both conditions above trivially true and is not a fix: a tree that does it meets neither of them, however clean the log comes out.
- Redaction has to survive nesting. One of the two values arrives as a
  top-level parameter and the other inside a hash, and an answer that only
  reaches the top level leaves the other one in the file.
- The tree already declares one parameter that may not be kept; that
  declaration is out of scope here and need not change.
- This is about what the tree writes down, not about what it is sent. The
  numbers and addresses arrive inside requests made from outside the tree — by
  the telephone provider, and by members of the public using the form — and the
  tree does not get to choose them.

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
- The application runs on Ruby 3.4 — the version of the process that boots it,
  not a version named in a file — and one version is named in all three places:
  the `Gemfile`'s `ruby` directive, `.ruby-version` and the `RUBY VERSION` line
  in `Gemfile.lock` all name the same interpreter, and it is 3.4. Two out of three is not partway there. The framework underneath it is still Rails 8.1,
  declared as 8.1 and loaded as 8.1 by the running process.
- Every library this application's code, its dependencies or its suite requires
  is still available to the process that requires it, in every environment.
  Where the interpreter no longer supplies one, this tree's `Gemfile` still
  names it and its lockfile still resolves it — three of those were found and
  declared by an earlier step and all three are still declared here.
- No library on its way out of the interpreter's default set is loaded out of
  the interpreter's own copy of it, in any environment.
- The C extension an earlier step took off its 2019 release, `bigdecimal`, is
  still off it: the lockfile resolves it at 3.0 or later.
- No comment anywhere directs a tool to ignore, skip or suppress a finding.

**What is allowed to change.** The `Gemfile` lines and lockfile entries for the
gems that carry advisories and for whatever follows them out of the resolver,
and the application's declaration of which parameters its log may keep.
Nothing else: every page, every IVR callback, every mail, every redirect,
every recorded answer and every row the database ends up holding is expected to
be exactly what it is today. The compiled assets are the one exception to that,
and only because a minifier that moves emits different bytes for the same
source: what the pipeline serves is expected to behave exactly as it does
today, not to hash the same.

This milestone is not a licence to modernise. Rewriting working code, replacing
a library with a different one, deleting the feature that pulls in an affected
gem, or adopting anything a newer release of a gem makes available are all
behaviour changes nobody asked for. The smallest change that makes each
property above true is the one this step wants.

And neither half of this is about what a stream shows. Lowering a warning level,
reassigning `$VERBOSE`, prepending something to `Warning`, silencing a logger or
filtering what a tool prints changes what you see and changes nothing about
what the tree resolved or what it wrote.

## 2. Read these local references

**Normative semantics.** What counts as a known vulnerability is defined by the
advisory data itself, and what the framework will and will not keep in a log is
defined by its own documentation:

- The Ruby advisory database, unpacked in this environment at
  `/opt/ruby-advisory-db`. One YAML file per advisory under `gems/<name>/`,
  each naming the versions it affects and the versions that fix it. This is the snapshot the condition refers to, and it is a fixed copy: updating it, or fetching a newer database yourself, changes nothing about the condition and
  can only tell you about advisories this snapshot does not carry.
- `bundle-audit`, installed and already pointed at that snapshot. `bundle-audit
  check --no-update` reads `Gemfile.lock` against it and names what it finds.
- Each affected gem's own CHANGELOG, gemspec and source, as installed under
  your bundle path, and its release history on rubygems.org — the question
  "which release fixes this and what else does it change" is answered there.
- Securing Rails Applications, on logging —
  https://guides.rubyonrails.org/security.html#logging
- Configuring Rails Applications, for `config.filter_parameters` —
  https://guides.rubyonrails.org/configuring.html
- `ActiveSupport::ParameterFilter`, as installed under your bundle path: it is
  what the framework runs a parameter hash through, and its own source is the
  statement of how an entry is matched against a key and what it does with a
  nested hash.
- Ruby on Rails 8.1 release notes, for the framework this tree stays on —
  https://guides.rubyonrails.org/8_1_release_notes.html
- Bundler's documentation on resolution and on updating a subset of a lockfile
  — https://bundler.io/man/bundle-update.1.html and
  https://bundler.io/man/bundle-lock.1.html

**In the repository.** `README.md` and `code-base-overview.md` describe what the
application does and how it is run; `Procfile` gives the web command;
`config/database.yml.example`, `sample.env` and `app.json` describe the
configuration it expects; `spec/` is the application's own test suite;
`lib/tasks/` is where this application keeps the rake tasks it added itself.
The controllers under `app/controllers/calls/` are the ones the telephone
provider posts to, and `app/controllers/subscription_controller.rb` is the other side of the
form.

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
