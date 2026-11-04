# The construct rules, as data.
#
# Each rule names a property in the language's terms -- "a controller filter
# declared with the pre-5.0 `*_filter` spelling" -- and the step that gates on
# it states that property in its instruction.  The rule name, this file, and
# the list a given step selects are internal to the verifier.
#
# A rule is a hash:
#
#   kind:      :call | :call_key | :const | :gvar | :superclass |
#              :superclass_version | :assign | :lex
#   names:     the identifiers it matches
#   detail:    a short human-readable statement of the property
#
# `:call` counts call *expressions*, so six calls in one file are six.  The
# only thing that reduces a count is removing calls.

module CVGate
  RULES = {
    # -- Rails 4.x -> 4.2 -------------------------------------------------
    'attr_accessible' => {
      kind: :call, names: %w[attr_accessible attr_protected],
      detail: 'model-level mass-assignment declaration (protected_attributes)'
    },
    # Rails 4.1 introduced `config/secrets.yml` and 4.2 reads `secret_key_base`
    # from it.  The property the instruction states is that nothing in the tree
    # assigns that key by hand any more -- so what is counted is assignment,
    # not mention: `Rails.application.config.secret_key_base` read back in a
    # view or an initializer is not a finding, and neither is the string in
    # `config/secrets.yml`, which is YAML and is never scanned.
    'secret_key_base_assignment' => {
      kind: :assign, names: %w[secret_key_base],
      detail: 'assignment to secret_key_base (Rails 4.1 reads it from config/secrets.yml)'
    },
    # `rails_email_preview` and `style-guide` have no release that works here,
    # so the step removes them from the bundle -- and a constant they defined,
    # left behind in a route or an initializer, is a `NameError` the moment the
    # environment that loads it is loaded.  Dropping the gem without dropping
    # its references is the half-migration this counts.
    'dead_engine_ref' => {
      kind: :const, names: %w[RailsEmailPreview StyleGuide],
      detail: 'reference to a retired development engine'
    },
    'scope_without_lambda' => {
      kind: :scope_nonlambda, names: %w[scope],
      detail: 'scope whose body is not callable (removed in Rails 4.1)'
    },
    # There is deliberately no rule for `Mail::Message#deliver`.  This
    # application defines its own `SubscriberNotifier#deliver`, and no static
    # rule can tell that call apart from a mailer's without guessing at the
    # receiver's runtime class.  Rails 5.0 *removes* the mailer method, so a
    # tree that still uses it raises `NoMethodError` and the recorded mail
    # cases fail -- unambiguously, at runtime, which is the better instrument
    # for a construct that announces itself.

    # -- Rails 5.0 --------------------------------------------------------
    'before_filter' => {
      kind: :call,
      names: %w[before_filter after_filter around_filter skip_before_filter
                skip_after_filter skip_filter append_before_filter
                prepend_before_filter filter_parameter_logging],
      detail: 'controller filter declared with the pre-5.0 `*_filter` spelling'
    },
    'unversioned_migration' => {
      kind: :superclass, names: %w[ActiveRecord::Migration],
      detail: 'migration class without the Rails-version suffix'
    },
    # The suffix is not decoration.  `ActiveRecord::Migration[x]` runs the
    # migration's body through release x's compatibility shim, so the same
    # `create_table` writes a different schema depending on the number: 4.2
    # leaves `t.timestamps` nullable and `add_reference` unindexed, 5.0 does
    # neither.  These files are the ones this repository has always had, and
    # they were written and run under 4.2; the version they carry is therefore
    # 4.2 for the rest of the ladder, not the version of whatever release the
    # tree happens to run on.  Stamping them forward is the cheap way to
    # satisfy `unversioned_migration` while silently changing what they do,
    # which is why the complement is counted too.
    'misversioned_migration' => {
      kind: :superclass_version,
      names: { 'ActiveRecord::Migration' => '4.2' },
      detail: 'migration declaring a schema version other than the 4.2 it was written against'
    },
    'redirect_to_back' => {
      kind: :call_arg_symbol, names: { 'redirect_to' => 'back' },
      detail: '`redirect_to :back` (removed in Rails 5.0)'
    },
    'alias_method_chain' => {
      kind: :call, names: %w[alias_method_chain],
      detail: '`alias_method_chain` (removed in Rails 5.0)'
    },

    # -- Rails 5.1 / 5.2 --------------------------------------------------
    'render_text' => {
      kind: :call_key, names: { 'render' => %w[text nothing] },
      detail: '`render text:`/`render nothing:` (removed in Rails 5.1)'
    },
    'secret_token' => {
      kind: :call, names: %w[secret_token],
      detail: '`secret_token` (removed in Rails 5.2, replaced by credentials)'
    },
    # Rails 5.1 unified `form_for` and `form_tag` under `form_with`.  Both
    # sides are counted, and deliberately as two rules rather than one
    # difference: "no old builder left" is true of a tree that deleted the
    # partial the form was in, and only the pair -- none of the old, as many of
    # the new as there were forms -- says the forms were converted.  The
    # counting is of call expressions in Ruby and in compiled ERB alike, so a
    # form built in a helper reads the same as one built in a template.
    #
    # `form_tag` is counted under the same name as `form_for` because it is the
    # same requirement and because leaving it out would leave a way to satisfy
    # the gate without doing the work: `form_for` rewritten as `form_tag` is a
    # form that is still not built by `form_with`, and 5.1 soft-deprecates the
    # two together.  This application ships neither `form_tag` nor `form_for`
    # under any other name, so adding the spelling moves no count on the tree
    # the candidate is handed -- it closes a spelling, it does not add a
    # requirement.
    'form_for' => {
      kind: :call, names: %w[form_for form_tag],
      detail: '`form_for`/`form_tag` (Rails 5.1 unifies the form builders under `form_with`)'
    },
    'form_with' => {
      kind: :call, names: %w[form_with],
      detail: '`form_with`, the form builder Rails 5.1 introduced'
    },
    # The 5.1 declaration itself.  There is no runtime reading of it on 5.1:
    # `Rails::Application::Configuration#load_defaults` (railties-5.1.7
    # lib/rails/application/configuration.rb:57-83) is a `case` over the
    # argument that flips switches and records nothing, and the
    # `loaded_config_version` reader Rails 5.2 adds does not exist yet.  So the
    # declaration is read where it is written.
    #
    # Two rules, and only the second of them is scored.  `load_defaults` counts
    # the declaration however it is spelled, which is what tells a reader of a
    # console log "declared the wrong release" apart from "declared nothing";
    # `load_defaults_5_1` counts the ones that name this step's release, which
    # is the sentence the instruction actually writes.  The scored reading is
    # the positive one on purpose: the complement --
    # "no declaration names anything but 5.1" -- is vacuously true of the
    # handed-on tree, which has no declaration at all, so it can only be a
    # guard, and as a guard it costs a tree that names 5.0 the entire gates
    # family while a tree that names nothing keeps almost all of it.
    #
    # Both are scoped to `config/`, because that is where the declaration takes
    # effect: `load_defaults 5.1` written in a spec, a seed file or a rake task
    # declares nothing, and counting it would let a tree collect the gate
    # without the application adopting anything.  `via` reads the same
    # declaration spelled through a dispatch method.
    'load_defaults' => {
      kind: :call, names: %w[load_defaults], only_in: %w[config/],
      detail: '`config.load_defaults`, the framework-defaults declaration Rails 5.1 introduced'
    },
    'load_defaults_5_1' => {
      kind: :call_arg_version, names: { 'load_defaults' => '5.1' },
      only_in: %w[config/], via: %w[send public_send __send__],
      detail: '`load_defaults` naming the 5.1 defaults this step adopts'
    },
    # The per-switch file the 5.0 generator writes, read by its contents rather
    # than by its name.  These are the seven identifiers
    # `new_framework_defaults.rb` assigns and no others: the instruction's
    # sentence is that *that file's* contents have not moved somewhere else, so
    # the list is the file's, not "every framework default there is".
    # `assets.unknown_asset_fallback` is deliberately absent -- 5.1 introduces
    # it and the 5.0 file never listed it, so a tree that reads "the fallback
    # is off" and writes the assignment by hand has not moved the file's
    # contents anywhere, and the setting itself is read at runtime by the
    # `asset_strict_*` gates rather than here.
    #
    # A tree that renames the file, or copies its body into
    # `config/application.rb` or an initializer of its own, is still carrying
    # the switches one at a time, which is the thing the declaration replaces.
    # Assignment and not mention: `config.ssl_options` read back somewhere is
    # not a finding, and neither is a switch named in a comment.
    'framework_defaults_switch' => {
      kind: :assign,
      names: %w[per_form_csrf_tokens forgery_protection_origin_check
                raise_on_unfiltered_parameters to_time_preserves_timezone
                belongs_to_required_by_default
                halt_callback_chains_on_return_false ssl_options],
      detail: 'a framework-defaults switch assigned by hand (the generated per-switch file, under any name)'
    },
    # Action Mailer 5.1's parameterisation moves the arguments off the action,
    # so `NotificationMailer.confirmation_email(x)` becomes
    # `NotificationMailer.with(location_subscription: x).confirmation_email`.
    # The receiver of the surviving call is the object `with` returns, not the
    # constant, so the old convention is exactly "one of this mailer's actions
    # called on the mailer class itself".  Counted statically because the
    # callers that matter most here are the ones nothing runs: a preview class
    # is not eager-loaded and has no spec, so a runtime reading cannot see it.
    #
    # `via` covers the same call reached through a dispatch method:
    # `NotificationMailer.send(:confirmation_email, x)` is the old convention
    # spelled differently, and the rule is about spellings.  What it still
    # does not see is a receiver that is not the constant itself --
    # `m = NotificationMailer; m.confirmation_email(x)`, or
    # `described_class.confirmation_email(x)` in a spec -- which would need the
    # scanner to track assignments.  The residual is bounded: a positional
    # call to a parameterized action sends mail with empty `params`, so a
    # caller anything runs is caught by the behaviour family or by the suite.
    # What escapes is a caller nothing runs, which in this tree means a mailer
    # preview class.
    'mailer_positional' => {
      kind: :call, names: %w[confirmation_email weekly_activity],
      receivers: %w[NotificationMailer],
      via: %w[send public_send __send__],
      detail: 'a mailer action called on the mailer class, the pre-5.1 positional convention'
    },

    # -- Rails 6.1 --------------------------------------------------------
    'update_attributes' => {
      kind: :call, names: %w[update_attributes update_attributes!],
      detail: '`update_attributes` (deprecated in Rails 6.0, removed in 6.1)'
    },
    # The marker half of 6.1's preloading requirement.  `strict_loading` on a
    # relation makes a lazily-loaded association raise instead of issuing the
    # query, which is what keeps a preload that is there today from quietly
    # going away tomorrow -- so it is counted where it is written, and the
    # preloading itself is read where it happens, as a query count, by
    # `depdrive`'s fourth pass.  Two readings of one requirement and not two
    # requirements: neither half is worth anything alone.  A count and not a
    # per-file rule because the relation it belongs on is the controller's
    # choice: a tree that moved the query into a model scope has done the work
    # in a different file and is not wrong for it.
    # Confined to app/ and to the relation-returning form.  Unconfined it
    # would count `strict_loading!` on a loaded record and any call under
    # spec/, so three lines in a spec file would collect a gate whose
    # requirement is "the relations these controllers build carry the
    # marker" -- a source count satisfiable with dead code.  The global
    # assignment `strict_loading_by_default = true` is still not a call and
    # so is still not counted, which is the other half of "not globally".
    'strict_loading' => {
      kind: :call, names: %w[strict_loading], only_in: %w[app/],
      detail: '`strict_loading`, the Rails 6.1 relation marker that makes a lazy load raise'
    },

    # -- Rails 7.2 --------------------------------------------------------
    # Rails 7.2 makes `ActiveRecord::Base.connection` a *permanent* checkout
    # the application has to ask for by name, and
    # `config.active_record.permanent_connection_checkout` decides what a
    # caller that has not asked gets: `:allowed` (7.1's behaviour),
    # `:deprecated`, or `:disallowed`, which raises.  A tree that sets
    # `:disallowed` and still holds one of these call sites is a tree that
    # raises the first time that line runs.
    #
    # `bootprobe`'s fork-hook reading catches the call site this application
    # actually has -- unicorn's `before_fork` -- because it runs the hook and
    # measures the pool afterwards.  It cannot catch a call site nothing runs
    # at boot: a rake task, a `db/seeds.rb` branch, a mailer preview, an
    # admin-only controller action.  Those are exactly the lines a migration
    # forgets, and they raise in production rather than in the suite.  So the
    # accessor is also counted where it is written, over the whole tree.
    #
    # Receiver-qualified, because `connection` alone is a method name that
    # belongs to plenty of objects that are not the Active Record class --
    # `pool.connection`, a `Net::HTTP` wrapper, a fixture double.  What the
    # receiver list does not see is the accessor reached through a model
    # (`Location.connection`) or through a local (`ar = ActiveRecord::Base`).
    # That residual is bounded the same way rule `mailer_positional`'s is: a
    # line that runs raises under `:disallowed`, so what escapes the count is
    # a line nothing runs *and* nothing names directly.
    'ar_base_connection' => {
      kind: :call, names: %w[connection],
      receivers: %w[ActiveRecord::Base ApplicationRecord],
      detail: '`ActiveRecord::Base.connection`, the permanent checkout Rails 7.2 asks an application to stop taking'
    },

    # -- annotation tooling -----------------------------------------------
    # The API of the `annotate` gem, which cannot come to Rails 8: its last
    # release caps `activerecord < 8.0`, and asked to resolve against 8.0
    # bundler does not report a conflict -- it walks the gem back through its
    # own history to a 2014 release with no cap, and installs that.  The
    # maintained fork is `annotaterb`, whose constant is `AnnotateRb`.
    #
    # Counted because of where the calls are.  `lib/tasks/
    # auto_annotate_models.rake` is what the old gem's installer wrote, and
    # rake loads every file under `lib/tasks/` before it runs anything, so the
    # file left in place beside the fork is not a broken annotate task: it is
    # `NameError: uninitialized constant Annotate` raised while the task tree
    # is built, taking `db:migrate` and `assets:precompile` with it, in
    # development only, where that file's own `if Rails.env.development?`
    # guard puts it.  Boot, the three environments and the whole suite stay
    # green (measured).
    #
    # Receiver-qualified, and both names are needed: the 2014 file calls
    # `Annotate.set_defaults` with its options and then `Annotate.load_tasks`,
    # and a tree half way through the swap has one without the other.
    # `Rails.application.load_tasks` is a different receiver and is not
    # counted; neither is anything on `AnnotateRb`.
    #
    # `via`, for the reason `mailer_positional` has it and
    # `secrets_reader` has it: `Annotate.send(:set_defaults, options)` is the
    # same call spelled so that a receiver-and-name matcher walks past it, and
    # a rule that a one-word edit defeats is a rule that scores the edit
    # instead of the migration.  Same residual as there: a receiver held in a
    # local is still not tracked.
    'annotate_legacy_api' => {
      kind: :call, names: %w[set_defaults load_tasks], receivers: %w[Annotate],
      via: %w[send public_send __send__],
      detail: '`Annotate.set_defaults`/`Annotate.load_tasks`, the API of the gem Rails 8 leaves behind'
    },

    # -- asset pipeline ---------------------------------------------------
    'sprockets_directive' => {
      kind: :sprockets,
      detail: 'Sprockets manifest directive (`//= require`) in an asset'
    },

    # -- Ruby 2.4 -> 3.4 --------------------------------------------------
    'fixnum' => {
      kind: :const, names: %w[Fixnum Bignum NIL TRUE FALSE],
      detail: 'integer/boolean constant removed in Ruby 3.2'
    },
    'taint' => {
      kind: :call, names: %w[taint untaint tainted? trust untrust untrusted?],
      detail: 'object taint API (removed in Ruby 3.2)'
    },
    'safe_global' => {
      kind: :gvar, names: %w[$SAFE $=],
      detail: 'global removed in Ruby 3.0'
    },
    'uri_escape' => {
      kind: :call, names: %w[escape unescape],
      receivers: %w[URI], detail: '`URI.escape`/`URI.unescape` (removed in Ruby 3.0)'
    },
    'exists_predicate' => {
      kind: :call, names: %w[exists?], receivers: %w[File Dir FileTest],
      detail: '`File.exists?`/`Dir.exists?` (deprecated since Ruby 2.1)'
    },
    'random_default' => {
      kind: :const_path, names: %w[Random::DEFAULT],
      detail: '`Random::DEFAULT` (removed in Ruby 3.2)'
    },

    # -- suppressions -----------------------------------------------------
    'suppression' => {
      kind: :lex,
      detail: 'comment that hides a finding from a tool'
    }
  }.freeze

  # A comment is a suppression if it *directs a tool*.  Matched against the
  # comment token's own text, which `Ripper.lex` hands over already separated
  # from string content -- so the word `rubocop:disable` inside a heredoc in
  # this project's own documentation is not a suppression.
  SUPPRESSION_PATTERNS = [
    [/\brubocop\s*:\s*(disable|todo)\b/, 'rubocop:disable'],
    [/\bbrakeman\s*:\s*ignore\b/, 'brakeman:ignore'],
    [/^\s*:?nocov:?\s*$/, ':nocov:'],
    [/\bzeitwerk\s*:\s*ignore\b/, 'zeitwerk:ignore'],
    [/\bsorbet\s*:\s*(false|ignore)\b/, 'sorbet:ignore'],
    [/\bsteep\s*:\s*ignore\b/, 'steep:ignore'],
    [/\bstandard\s*:\s*disable\b/, 'standard:disable'],
    [/\berblint\s*:\s*disable\b/, 'erblint:disable']
  ].freeze
end
