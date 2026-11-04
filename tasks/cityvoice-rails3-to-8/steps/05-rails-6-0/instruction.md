# Step 5 — Rails 5.2 → 6.0 on Ruby 2.7

## 1. Implement this milestone

`/app` is CityVoice, a civic IVR application, as the previous step left it: on
Rails 5.2 under Ruby 2.4, taking its framework defaults through a single
`load_defaults` declaration, holding its signing key in encrypted credentials
with the key beside them, booting through bootsnap, and serving a content
security policy whose nonce is minted per request. Move it to **Rails 6.0 on
Ruby 2.7**, and leave it behaving as it does today.

The interpreter moves in this step, for the first time since the step that
took it to 2.4. Rails 6.0
does not install on Ruby 2.4, and 2.7 is the newest interpreter 6.0 supports,
so in this step the two pins move together or neither of them moves.

The tree you hand back must have all of these properties.

**Runtime and framework.**

- The application runs on Ruby 2.7 and on Rails 6.0.
- The Gemfile declares the Ruby the application runs on, `.ruby-version` names
  that same version, and `Gemfile.lock` records that same Ruby alongside the
  resolved gem set — one version, named in all three places. Two of the three
  agreeing is the failure worth naming, because it stops nothing: it leaves the
  application running on an interpreter nobody chose.
- The whole dependency set installs on Ruby 2.7 and resolves against Rails 6.0
  — not just the framework gems. A dependency whose current pin caps the
  framework below 6.0, which has no release that loads on this interpreter, or
  whose native extension no longer builds against it, has moved. A pin that a
  *previous* interpreter forced and this one does not is free to go back to
  where the application had it; a pin that is still doing work stays. Not all
  of this shows up at resolution: `bundle install` can succeed against a set
  the framework then fails to load, because a dependency's current release
  stopped requiring a standard library for you, or dropped a method or a
  keyword this framework still calls it with. A lockfile that resolved is not
  evidence the set works; booting the application is.

**Framework defaults.** The declaration that names the release whose defaults
this application takes moves one version on:

- The declaration names 6.0.
- And the application actually gets what 6.0 throws with it. Naming the version
  and then configuring the new behaviours back off is not the milestone; each
  of these is in force in the tree you hand back:
  - The application's own classes are loaded by the autoloader this release
    introduces, not the one it replaces.
  - Encrypted cookies carry their own metadata — the purpose the value was
    written for and when it expires — so a cookie lifted out of one slot
    cannot be replayed into another.
  - A cache key built for a collection carries a version taken from the
    collection, instead of baking the collection's state into the key itself.
  - Action Mailer's deferred delivery goes through the delivery job this
    release introduces, rather than the one it supersedes.
  - The form builder stops emitting the hidden `utf8` parameter it has been
    adding to every form since it was needed by a browser this application no
    longer serves.

**The autoloader.** 6.0's headline change is that the application's own
constants are loaded by Zeitwerk rather than by the framework's
constant-missing hook, and this application moves across:

- The application's constants are loaded by the new autoloader. The release
  keeps the old one available behind a setting, and selecting it back is not a
  way to satisfy this line.
- The tree satisfies the new autoloader's own rule, which the old one did not
  enforce: every file under the autoload paths defines the constant its path
  names. The release ships a check that walks those paths and reports the files
  that do not, and it reports nothing against the tree you hand back.
- And nothing the application's own initialization autoloads one of the
  application's own constants. 6.0 reports every one it finds, and warns that
  autoloading during initialization is going to be an error condition in a
  future release. The complaint is not about noise: an initializer
  runs once and a reloaded class does not, so a value an initializer built out
  of an autoloaded constant is a stale object from the first reload onwards.
  Whatever you do about it, that is the property: initialization finishes
  without having autoloaded a constant of the application's own.

**Host authorization.** 6.0 adds a middleware that refuses any request whose
`Host` header the application does not recognise, and this application
configures it:

- The `production` environment names the hosts the application answers for.
  Development and test are given a default list of loopback names; production
  is given an empty one, and an empty list makes the middleware step aside — so
  until that list is written down, the DNS-rebinding protection the release
  advertises is off in the one environment that faces the internet.
- A request carrying the application's own deployed name is served.
- A request carrying a name the application does not serve is refused, with the
  status the release refuses with.
- A request whose `Host` merely *contains* the deployed name — the deployment's
  name with another domain appended to it — is refused. That is a hostname an
  attacker can register precisely because it contains the real one, and it is
  served by any entry that asks whether the deployed name appears *somewhere*
  in the `Host` rather than whether it is the whole of it — a matcher of your
  own writing that does a substring or prefix test. The names the release
  itself knows how to anchor it will anchor for you, and a pattern you hand it
  unanchored it re-anchors; what it takes at its word is an entry it can do
  neither of those to — one it can only ask, one `Host` at a time, whether that
  host is one of yours.
- And the address the deployment is itself reached on keeps being served. This
  application runs behind a proxy and under a platform health check, both of
  which connect to the container's own loopback address, so the `Host` header
  they send is that address and not the deployed name. A list naming only the
  public name refuses them, which takes the application off the air in a way
  that no page in a browser will show you. What the list needs is the host
  itself: the header those callers send carries the port they reached it on,
  and matching the port is the release's business rather than yours.
- Removing the middleware, emptying the list, or permitting everything is not a
  way to satisfy any of the four requirements above.

**Logging.** The application's production log still goes to the process's own
standard output, where the platform that runs it reads it, and its lines still
carry the message and nothing else. It goes there today because of a gem, and
that gem is one of this step's casualties: what it set is the application's own
configuration now.

**Deprecations.**

- Driving the application raises no deprecation warning that comes from the
  application's own code. Warnings raised from inside a third-party gem's own
  source are out of scope; anything the application itself triggers is in
  scope. This includes warnings raised from the application's own
  configuration. Like the suite condition below, this one allows not one: a drive that raises one has not met it any more than a drive that raises all of them.
- Booting it raises none either, in any of the three environments, and from this step that applies across the whole tree rather than only to `config/environments/`. A gem that announces on every boot that the
  application is naming it by a name it no longer has is the application's
  doing, and so is a gem whose last release predates the release that
  deprecated what it does: neither of them has a version to upgrade to, and
  both are named in the application's own Gemfile. A boot of this application
  in each of `production`, `development` and `test` raises nothing. The
  production boot is part of driving the application, so a deprecation raised while production boots falls under that condition, which allows not one; a gem the Gemfile puts in the `production` group
  is a gem only that boot loads.
- Running the spec suite raises no deprecation warning from a file under
  `spec/`. 6.0 deprecates the persistence method this application updates a
  record with — the one whose name says `attributes` — and removes it in the
  release after; the suite exercises it. As in the previous step, this one allows not one: a suite with one such call left in it has not met it any more than a suite that fixed none of them.

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
  still at its path. The migration removes what the framework removed, not the
  application's own code.
- The application's own test suite is still there — every file the repository
  shipped under `spec/`, each still asserting something — and still passes.
  One support file is the exception, and only because this step renames the
  library it configures; every other path stays.
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
- The 5.0, 5.1 and 5.2 defaults stay adopted, and stay adopted the same way:
  through the version declaration, with no `new_framework_defaults` file in the
  tree and no switch it used to list set by hand anywhere (the seven that 5.0's generated file listed are
  `per_form_csrf_tokens`, `forgery_protection_origin_check`,
  `raise_on_unfiltered_parameters`, `to_time_preserves_timezone`,
  `belongs_to_required_by_default`, `halt_callback_chains_on_return_false`
  and `ssl_options`). `belongs_to` is
  still required by default; the one association this application legitimately
  creates before its parent is known is still the only one marked optional, and
  it is still marked on the association itself.
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
- No association or validation passes SQL or a condition as a String to be
  evaluated in the model's context.
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
tasks), the mail it sends — is unchanged, with three exceptions. Adopt all
three, and do not write code to restore the old answer:

1. **A request whose `Host` header the application does not recognise is
   refused.** That is the milestone above doing what it was asked to do: a 5.2
   that had never heard of the header served such a request, and a 6.0 that has
   been configured refuses it. It is the only endpoint answer this step is
   expected to change, and the request it changes for is the one carrying
   neither the deployed name nor the address the process is reached on.
2. **The line the framework logs when it delivers mail names the message
   rather than the recipient.** Action Mailer's log subscriber was rewritten
   for 6.0 and now reports the delivery by the message's own identifier. What
   is sent, to whom, and everything else in the mail is unchanged.
3. **The line the framework logs when it finishes rendering a template reports
   two numbers where it reported one.** Action View's render instrumentation
   gained an allocation count for 6.0 and its log subscriber now labels both
   that and the duration. The template rendered, and the order the lines come
   in, are unchanged.

Both of the log lines above are the framework's own wording, written by the
framework's own subscribers; nothing you write produces either spelling, and
restoring the 5.2 spelling by subclassing or patching a log subscriber is
writing code to restore the old answer. Outside those two lines, everything
else the application logs is unchanged.

Nothing else about the application's behaviour is up for renegotiation. Two of
this step's changes are the kind that break everything and show nothing if they
are done carelessly: replacing the autoloader changes how every constant in the
application is found, and configuring the host list is a change whose failure
mode is a working application that answers 403 to the only client that ever
reaches it. Drive the application after each of them rather than after all of
them.

Where a release changed a default in a way that would alter behaviour you are
asked to preserve, the old behaviour still holds.

## 2. Read these local references

**Normative semantics.** The framework's own release documentation defines what
"on Rails 6.0" means and every change listed above:

- Ruby on Rails 6.0 release notes — https://guides.rubyonrails.org/6_0_release_notes.html
- Upgrading Ruby on Rails — https://guides.rubyonrails.org/upgrading_ruby_on_rails.html
- Autoloading and Reloading Constants —
  https://guides.rubyonrails.org/autoloading_and_reloading_constants.html
- Configuring Rails Applications, on `config.hosts` —
  https://guides.rubyonrails.org/configuring.html
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
  spelling wrong — a matcher library that renames itself, a matcher that learns
  about a default an earlier step adopted, a factory definition whose syntax a
  major version drops. No expectation in this suite has a subject this step
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
is supplied from outside the tree — in particular, no key material is: neither
the signing key nor the key that decrypts the application's credentials is set
in the environment, so whatever the application needs in order to read its own
secrets is in the tree or it is not there at all.
Beyond that the surroundings are fixed, and the tree must work in them as they
are:

- `PORT` names the port to listen on, and the process is reached at that port
  on the container's own address as well as by the name in `APP_URL`.
- `DISABLE_DATABASE_ENVIRONMENT_CHECK` is set. It is Rails' own documented
  escape hatch for the guard that refuses a destructive `db:` task against a
  database the framework believes is production, and it is set because those tasks get run — not to change what any of them does.
- The system clock may be set to the repository's own era; nothing may depend
  on the current date.
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
