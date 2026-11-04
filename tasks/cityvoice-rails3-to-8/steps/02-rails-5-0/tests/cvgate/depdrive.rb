# frozen_string_literal: true

# Every framework deprecation the application raises while it is being used.
#
#   bundle exec ruby -I/gates /gates/cvgate/depdrive.rb --app /app --out dep.json
#
# Run from inside the application's bundle, because the point is to observe
# *its* framework, not one this file chose.
#
# Why a runtime instrument and not a scan.  "Nothing in this tree uses a
# construct the new release deprecated" is a property of what the code does,
# not of how it is spelled: `send(:before_filter, :x)`, a filter declared in a
# concern, and a mailer delivered through a variable all read differently and
# deprecate identically.  Rails already computes the answer -- every
# deprecation in the framework goes through one method -- so the gate asks it
# instead of re-deriving it.
#
# The collector is installed by *prepending* to `ActiveSupport::Deprecation`
# rather than by assigning `behavior`. Three reasons:
#
#   * Rails assigns `behavior` itself, in the `active_support.deprecation_
#     behavior` initializer. A `behavior =` set before boot is overwritten
#     during boot, so every deprecation raised while the application was
#     loading -- which on this step is most of them -- would be silently lost.
#   * From Rails 7.1 each framework carries its own deprecator instance and
#     `ActiveSupport::Deprecation.behavior =` touches only one of them. Every
#     instance is an instance of this class, so the prepend catches all of
#     them and keeps catching them as more are added.
#   * `behavior = :raise` aborts on the first finding, which reports one
#     deprecation and hides the rest. The collector records and returns, so a
#     tree with nine of them says nine.
#
# `super` is always called: the application's configured behaviour still runs,
# so nothing about the run changes except that someone is writing it down.

require 'json'
require 'optparse'
# For `strict_local_names`, which reads a strict-locals declaration by parsing
# it rather than by scanning it.  Stdlib on every Ruby in this ladder.
require 'ripper'

module CVGate
  module DepDrive
    RECORDS = []

    # Rails' own frames are stripped from the call site so the report names the
    # application file that has to change, not the framework file that
    # complained.  `caller` order is innermost-first, so the first frame under
    # the application root is the one to keep.
    def self.blame(callstack, app)
      frames = Array(callstack).map(&:to_s)
      hit = frames.find do |frame|
        frame.start_with?(app) && !frame.include?('/vendor/bundle/')
      end
      # Anchored, because the fallback frame is a framework path and a plain
      # `sub("#{app}/", '')` would cut "/app/" out of the *middle* of
      # "/bundle/gems/rack-1.5.2/lib/app/x.rb" and report a path that does not
      # exist.
      (hit || frames.first || '(no call site)')
        .sub(/\A#{Regexp.escape(app)}\//, '')
    end

    # The collector itself, as a value, so the selftest can prepend it to a
    # class of its own and check that a `warn` really does land in `records`
    # and really does still reach the method underneath.  A collector that is
    # only ever exercised against the real Rails is a collector nobody has
    # checked: it would report zero deprecations on a clean tree and zero on a
    # broken install, and those look identical in the reward.
    def self.collector(app, records)
      Module.new do
        define_method(:warn) do |message = nil, callstack = nil, *rest|
          records << {
            'message' => message.to_s.strip,
            'where' => CVGate::DepDrive.blame(callstack || caller, app)
          }
          super(message, callstack, *rest)
        end
      end
    end

    def self.install(app, records = RECORDS)
      # Only when it is not already there: `rspecdep.rb` installs into a
      # process that has booted its own Rails, and a selftest installs into one
      # that has no activesupport to require at all.
      unless defined?(::ActiveSupport::Deprecation)
        # `logger` first, because this runs before the application's own boot
        # file.  activesupport 6.0 and 6.1 read `::Logger` while they load and
        # rely on a dependency to have required it; concurrent-ruby 1.3.5
        # stopped doing that.  A tree that answers it with `require 'logger'`
        # in `config/boot.rb` -- the fix Rails itself gives, as good as pinning
        # concurrent-ruby -- boots, but this collector is loaded ahead of that
        # file and would raise NameError, losing every reading built on it.
        # Every activesupport requires `logger` itself, so what is loaded
        # afterwards is unchanged.
        require 'logger'
        require 'active_support'
        require 'active_support/deprecation'
      end
      ActiveSupport::Deprecation.prepend(collector(app, records))
      # 7.1+ hands each framework its own instance; they are all instances of
      # the class above, but `Deprecation.warn` at the class level is its own
      # method there and delegates elsewhere.  Prepending the singleton too
      # costs nothing and closes that door.
      ActiveSupport::Deprecation.singleton_class.prepend(collector(app, records))
    end

    # The drive: the application's own surface, in the order a real use of it
    # happens.  Each entry records its own outcome, and an entry that raises
    # does not stop the others -- a deprecation report that stopped at the
    # first 500 would under-count on exactly the trees that need counting.
    GETS = %w[
      / /locations /locations.json /locations/1 /locations/1.json
      /voice_answers /numerical_answers /numerical_answers.csv /export.csv
      /.well-known/status
    ].freeze

    # The pages the CSP pass adds to what the drive above already observed.
    # `/pages/*` are this application's three static pages, and the last of
    # them carries the audio player's inline `<script>`; the drive above does
    # not fetch them because it is a deprecation drive and they are the same
    # controller three times.  `CSP_REPEAT` is fetched twice in the same pass:
    # "a nonce that is different on every response" is not a property any one
    # response has, and the only way to read it is to ask the same page twice.
    CSP_GETS = %w[/pages/about /pages/contact /pages/messages].freeze
    CSP_REPEAT = '/'.freeze

    # The three Host headers of the host-authorization pass.  `HOSTS_KNOWN` is
    # the name the rest of this file drives with, which is the name the
    # application is deployed under; `HOSTS_LOOPBACK` is the address a proxy or
    # a health check connects to, carrying a port so that a list that matches
    # on the header verbatim is told apart from one that matches on the host;
    # `HOSTS_UNKNOWN` is a name the deployment has never heard of, which is
    # what a DNS-rebinding request looks like from inside Rack.
    HOSTS_PATH = '/'.freeze
    HOSTS_LOOPBACK = '127.0.0.1:3000'.freeze
    HOSTS_UNKNOWN = 'evil.example.com'.freeze
    # The near miss, and the reason there are four headers and not three: a
    # list written as an unanchored Regexp, or as a substring match of the
    # deployment's own name, serves this host.  It is the hostname an attacker
    # registers, and it contains the real one.
    HOSTS_SUFFIX = 'cityvoice.test.evil.example.com'.freeze

    TWILIO = {
      'CallSid' => 'CAdepdrive0000000000000000000000',
      'From' => '+15745551212',
      'To' => '+15745550000',
      'AccountSid' => 'ACdepdrive0000000000000000000000'
    }.freeze

    # `Rack::MockRequest` has no cookie jar, and without one the session cookie
    # never comes back, so `protect_from_forgery` rejects every form POST with
    # a 422 before the controller runs.  The subscription form and the mailer
    # behind it are exactly where the deprecated delivery API lives, so a drive
    # that cannot post a form is a drive that cannot see it.
    class Session
      HOST = 'cityvoice.test'

      # `mock` is injectable so the selftest can drive the jar and the token
      # scraper against canned responses without a Rack in the load path.
      def initialize(app, mock = nil)
        @mock = mock || begin
          require 'rack/mock'
          Rack::MockRequest.new(app)
        end
        @cookies = {}
      end

      def get(path, extra = {})
        absorb(@mock.get(path, env(extra)))
      end

      def post(path, params, extra = {})
        absorb(@mock.post(path, env(extra).merge(params: params)))
      end

      # The token Rails embeds in the page it just served.  Either spelling:
      # the meta tag the layout emits and the hidden field `form_for` emits are
      # the same token, and which one is present has changed between releases.
      def csrf(response)
        body = response.body.to_s
        body[/name="csrf-token"[^>]*content="([^"]*)"/, 1] ||
          body[/name="authenticity_token"[^>]*value="([^"]*)"/, 1] ||
          body[/value="([^"]*)"[^>]*name="authenticity_token"/, 1]
      end

      private

      def env(extra)
        base = { 'HTTP_HOST' => HOST }
        unless @cookies.empty?
          base['HTTP_COOKIE'] = @cookies.map { |k, v| "#{k}=#{v}" }.join('; ')
        end
        base.merge(extra)
      end

      # Rack 1.x joins Set-Cookie with newlines and Rack 3 hands back an array;
      # both are flattened to one line per cookie here so the jar does not
      # depend on which Rack this step's application resolved.
      def absorb(response)
        raw = response.headers['Set-Cookie'] || response.headers['set-cookie']
        Array(raw).flat_map { |line| line.to_s.split("\n") }.each do |line|
          name, value = line.split(';').first.to_s.split('=', 2)
          @cookies[name.strip] = value if name && value
        end
        response
      end
    end

    def self.drive(app, report)
      require 'rack/mock'
      session = Session.new(Rails.application)

      report['steps'] = []
      report['csp'] = csp_summary([])
      report['headers'] = headers_summary([])
      report['strict_locals'] = strict_locals_summary({})
      report['hosts'] = hosts_summary({})
      # Seeded here for the reason the two above are: `encryption_pass` runs
      # near the end of the drive, and a drive that raises before it must
      # leave a reading a gate fails on rather than a key a gate cannot find.
      report['encryption'] = { 'ran' => false, 'no_plaintext_at_rest' => false,
                               'find_by_finds' => false,
                               'case_variant_finds_same_row' => false,
                               'find_or_create_by_reuses' => false,
                               'long_address_round_trips' => false }
      seen = []
      step(report, 'eager_load') { Rails.application.eager_load! }

      # The repository's own import tasks run first so that every page below
      # has rows to render.  A drive against an empty database reaches the
      # empty branch of each view and leaves the populated one -- the one with
      # the associations, the pagination and the CSV serialisation in it --
      # unobserved, which is where a deprecation is most likely to be.
      require 'rake'
      Rails.application.load_tasks
      rake(report, 'import:locations')
      rake(report, 'import:questions')

      # The location page is fetched first and kept: it is the page carrying
      # the subscription form, so it is where the CSRF token below comes from.
      token = nil
      GETS.each do |path|
        step(report, "GET #{path}") do
          response = session.get(path)
          csp_observe(path, response, seen)
          token ||= session.csrf(response) if path == '/locations/1'
          response.status
        end
      end
      call_id = nil
      step(report, 'POST /calls') do
        response = session.post('/calls', TWILIO)
        call_id = response.body[%r{/calls/(\d+)/}, 1]
        response.status
      end
      if call_id
        step(report, 'POST /calls/:id/location') do
          session.post("/calls/#{call_id}/location?attempts=1",
                       TWILIO.merge('Digits' => '1')).status
        end
        step(report, 'POST /calls/:id/consent') do
          session.post("/calls/#{call_id}/consent?attempts=1",
                       TWILIO.merge('Digits' => '1')).status
        end
        step(report, 'POST /calls/:id/questions') do
          session.post("/calls/#{call_id}/questions", TWILIO).status
        end
        step(report, 'POST /calls/:id/questions/0/answer') do
          session.post("/calls/#{call_id}/questions/0/answer?attempts=1",
                       TWILIO.merge('Digits' => '3')).status
        end
      end

      report['csrf_token_found'] = !token.nil?
      step(report, 'POST /subscription') do
        form = { 'subscription' => { 'email' => 'depdrive@example.com',
                                     'location_id' => '1' } }
        form['authenticity_token'] = token if token
        session.post('/subscription', form).status
      end

      # The mailer is where `#deliver` lives, and it is only reached by the
      # application's own task.  It runs last, after the subscription above
      # gave it something to send.
      rake(report, 'notifications:send')

      csp_pass(report, session, seen)
      report['csp'] = csp_summary(seen)
      report['headers'] = headers_summary(seen)

      # After the pages, because it renders the same partials the pages just
      # rendered and a failure here should be attributable to the partial and
      # not to a half-built page.
      strict_locals_pass(report)

      # Before the host pass, because the host pass is the one that expects a
      # 403 and the pages below expect to be served.
      queries_pass(report, session)

      # After the query pass, because it writes and deletes subscriber rows
      # and the query pass counts queries against a fixed row count.
      encryption_pass(report)

      # Last, because it is the only pass that sends a header the application
      # is expected to reject, and a 403 leaves no session cookie to absorb.
      hosts_pass(report, session)
    end

    # The second pass: three more HTML pages, and one of the pages the drive
    # already fetched asked for again.  Nothing here is read for deprecations
    # and that is deliberate.  Every step ships this same file, and the
    # `own_deprecations` gate of steps 01, 02 and 03 is a reading of the drive
    # above; adding pages to the drive would silently move the number those
    # steps are scored on.  So the records this pass produces are cut back off
    # the shared list and reported on their own line instead, where a later
    # step can gate them.
    def self.csp_pass(report, session, seen)
      mark = RECORDS.length
      (CSP_GETS + [CSP_REPEAT]).each do |path|
        step(report, "CSP GET #{path}") do
          response = session.get(path)
          csp_observe(path, response, seen)
          response.status
        end
      end
      extra = RECORDS[mark..-1] || []
      RECORDS.slice!(mark..-1) if RECORDS.length > mark
      report['csp_pass_deprecations'] = extra.length
      report['csp_pass_deprecation_sites'] =
        extra.map { |r| r['where'] }.uniq.sort.first(10)
    end

    # ERB strict locals -- ActionView 7.1's `<%# locals: (...) %>` magic
    # comment, which turns a partial's locals into a real Ruby keyword
    # signature.
    #
    # Read by RENDERING and not by reading the comment, for the reason every
    # other pass in this file exists: `<%# locals: (content:) %>` in a file is
    # a comment, and ActionView only compiles a signature from it when it
    # parses it as a declaration -- which it does not if the comment is not
    # the first thing in the template, if the release predates 7.1, or if the
    # handler is not the ERB one.  A grep cannot tell those apart from the
    # real thing.  `Template#strict_locals!` is asked first, because it is the
    # framework's own answer to "did you parse this", and then the partial is
    # handed one local it did not declare and the exception is the reading.
    #
    # Nothing here is step-specific: every ERB partial in the tree is probed
    # and the result is a list.  Which partials MUST be strict is named in the
    # step's profile, because that is a scope decision -- this application has
    # four ERB partials with no render call site anywhere, and requiring a
    # declaration on a template nothing renders would be requiring a comment.
    STRICT_PROBE_LOCAL = :cv_depdrive_strict_probe

    # `title: nil, html_class: nil` -> [:title, :html_class]; `**nil` -> [].
    # Only the names are wanted: every one is passed nil, because the point of
    # the call is to reach the signature and an exception raised inside the
    # body is an exception raised AFTER the locals were accepted, which is the
    # answer this pass is looking for.
    #
    # What arrives here is what `Template#strict_locals!` returns, and that is
    # the text INSIDE the parentheses and not the parenthesised list:
    # actionview's own STRICT_LOCALS_REGEX (template.rb:11) is
    # `/\#\s+locals:\s+\((.*)\)/` and the capture is group 1.  So
    # `<%# locals: (content:) %>` hands over `content:`, and `()` hands over
    # `**nil` (template.rb:350, which rewrites a blank declaration).
    #
    # The names are taken by parsing that text as the parameter list it is --
    # with Ripper, the way `ast.rb` reads every other construct in this
    # project, and the same grammar actionview compiles it with at
    # template.rb:429 -- rather than by scanning it for `name:`.  A default
    # value can hold commas and colons of its own (`a: {b: 1, c: 2}`), and a
    # name invented from one of those would be passed as an undeclared local
    # and would make the probe report a refusal it did not earn.  Ripper
    # parses and does not evaluate, so no default value's expression is run
    # here.
    def self.strict_local_names(declaration)
      text = declaration.to_s
      return [] if text.strip.empty?
      sexp = Ripper.sexp("def cv_strict_locals_probe(#{text}); end")
      return [] if sexp.nil?
      params = find_params(sexp)
      return [] if params.nil?
      # Ripper's `:params` node is
      # [:params, req, opt, rest, post, keywords, kwrest, block]; keywords are
      # [[[:@label, "title:", ...], default], ...] and a label carries its own
      # trailing colon.
      Array(params[5]).map do |(label, _default)|
        next unless label.is_a?(Array) && label[0] == :@label
        label[1].to_s.sub(/:\z/, '').to_sym
      end.compact
    rescue Exception # rubocop:disable Lint/RescueException
      []
    end

    # The parameter list of the first `def` in the tree, which is the one
    # written above.  Stopping there matters: a default value that is itself a
    # lambda -- `a: ->(x) { x }` -- carries a `:params` node of its own, and
    # `x` is not a local this template accepts.
    def self.find_params(node)
      return nil unless node.is_a?(Array)
      if node[0] == :def
        list = node[2]
        list = list[1] if list.is_a?(Array) && list[0] == :paren
        return list.is_a?(Array) && list[0] == :params ? list : nil
      end
      node.each do |child|
        found = find_params(child)
        return found if found
      end
      nil
    end

    def self.strict_probe(logical)
      prefix, name = logical.include?('/') ? logical.split('/', 2) : ['', logical]
      controller = ApplicationController.new
      controller.request = ActionDispatch::TestRequest.create
      template = controller.lookup_context.find_template(name, [prefix], true, [])
      declaration = (template.strict_locals! if template.respond_to?(:strict_locals!))
      locals = strict_local_names(declaration).each_with_object({}) { |k, h| h[k] = nil }
      refused = begin
        ApplicationController.render(partial: logical,
                                     locals: locals.merge(STRICT_PROBE_LOCAL => 1))
        nil
      rescue Exception => e # rubocop:disable Lint/RescueException
        "#{e.class}: #{e.message.to_s.split("\n").first}"[0, 200]
      end
      { 'strict' => !declaration.nil?,
        'declaration' => declaration.to_s,
        'probe_error' => refused,
        'refuses_unknown' => !refused.nil? && refused =~ STRICT_REFUSAL ? true : false }
    rescue Exception => e # rubocop:disable Lint/RescueException
      { 'strict' => false, 'declaration' => '',
        'lookup_error' => "#{e.class}: #{e.message}"[0, 200],
        'refuses_unknown' => false }
    end

    # The two messages ActionView 7.1 raises when a strict-locals signature
    # turns away a local it does not declare, wrapped in Template::Error.
    # Matched on the text because the class is the same
    # `ActionView::Template::Error` a broken partial raises -- it is the
    # message that says the refusal came from the signature and not from the
    # body.  Both are actionview's own words for Ruby's: `base.rb:249-260`
    # rewrites `unknown keyword:` and `no keywords accepted` as it re-raises.
    #
    # `missing local:` is deliberately NOT one of them, and this is the whole
    # reason `strict_local_names` has to get the declaration right.  That
    # message means the probe failed to supply a local the partial requires,
    # so the signature refused the CALL before it could refuse the undeclared
    # name -- the reading would be "this template has a strict signature",
    # which is what `strict` already says, and not "and it turns an undeclared
    # local away", which is the requirement.
    STRICT_REFUSAL = /unknown local|no locals accepted/.freeze

    def self.strict_locals_pass(report)
      mark = RECORDS.length
      results = {}
      step(report, 'strict locals') do
        Dir.glob(Rails.root.join('app/views/**/_*.html.erb').to_s).sort.each do |path|
          logical = path.sub(%r{\A#{Regexp.escape(Rails.root.join('app/views').to_s)}/}, '')
                        .sub(/\A_|\/_/) { |m| m == '_' ? '' : '/' }
                        .sub(/\.html\.erb\z/, '')
          results[logical] = strict_probe(logical)
        end
        results.length
      end
      # Cut back for `csp_pass`'s reason: this pass renders templates the
      # drive above already rendered, and a deprecation raised twice must not
      # move the count steps 01 to 03 are scored on.
      RECORDS.slice!(mark..-1) if RECORDS.length > mark
      report['strict_locals'] = strict_locals_summary(results)
    end

    # `partials` keeps every reading for the evidence; the four lists beside
    # it are what a profile can state a requirement about.  `refuses_unknown`
    # and not `strict` is the one to gate: `strict` is "the framework parsed
    # the comment", `refuses_unknown` is "and the signature it compiled really
    # turns an undeclared local away", and the second is the requirement.
    def self.strict_locals_summary(results)
      { 'probed' => results.length,
        'partials' => results,
        'strict' => results.select { |_, v| v['strict'] }.keys.sort,
        'not_strict' => results.reject { |_, v| v['strict'] }.keys.sort,
        'refuses_unknown' => results.select { |_, v| v['refuses_unknown'] }.keys.sort,
        'accepts_unknown' => results.reject { |_, v| v['refuses_unknown'] }.keys.sort }
    end

    # The third pass: one page, three Host headers.  Rails 6.0 puts
    # `ActionDispatch::HostAuthorization` in the default stack and reads
    # `config.hosts` for the list it allows -- and an empty list, which is what
    # production is given, makes the middleware step aside.  So "this
    # application refuses a Host it does not serve" is not a property of the
    # release; it is a property of the configuration, and the only way to read
    # it is to send the header and look at the status.
    #
    # Three statuses and not one, because each of the three ways to get this
    # wrong shows up in a different one of them: a list that was never stated
    # serves the unknown host, a list of exactly `[APP_URL]` stops the
    # loopback, and a list built from a variable that is not set in this
    # environment stops everything.
    #
    # Like `csp_pass`, the records this pass produces are cut back off the
    # shared list: steps 01 to 04 are scored on a reading of the drive above,
    # and a page fetched here must not be able to move it.
    def self.hosts_pass(report, session)
      mark = RECORDS.length
      statuses = {}
      { 'known' => Session::HOST,
        'loopback' => HOSTS_LOOPBACK,
        'unknown' => HOSTS_UNKNOWN,
        'suffix' => HOSTS_SUFFIX }.each do |name, host|
        step(report, "HOST #{host}") do
          statuses[name] = session.get(HOSTS_PATH, 'HTTP_HOST' => host).status
        end
      end
      extra = RECORDS[mark..-1] || []
      RECORDS.slice!(mark..-1) if RECORDS.length > mark
      report['hosts_pass_deprecations'] = extra.length
      report['hosts'] = hosts_summary(statuses)
    end

    # What the four statuses mean, as a value, so the selftest can put canned
    # statuses through it without a Rack in the load path.  `serves_known`
    # takes both of the hosts the deployment really uses: a gate that asked
    # only about the unknown one would pass a tree that refuses every request.
    def self.hosts_summary(statuses)
      known = statuses['known']
      loopback = statuses['loopback']
      unknown = statuses['unknown']
      suffix = statuses['suffix']
      { 'known' => known,
        'loopback' => loopback,
        'unknown' => unknown,
        'suffix' => suffix,
        'serves_known' => known == 200 && loopback == 200,
        'blocks_unknown' => unknown == 403,
        'blocks_suffix' => suffix == 403 }
    end

    # The fourth pass: how many queries a page costs, measured against how
    # many rows it renders.
    #
    # An N+1 is not a spelling and it is not a configuration; it is a count.
    # `answers.each { |a| a.location.name }` and `render partial:` over the
    # same collection read differently, scan identically, and both issue one
    # query per row -- so the only instrument that answers "is this page
    # preloaded" is one that counts what the page sent to PostgreSQL.  Rails
    # publishes every one of them on `sql.active_record`, so this pass
    # subscribes, drives the two collection pages the application has, and
    # writes down the number.
    #
    # The rows are made here rather than taken as they come.  The drive above
    # leaves one call and one answer behind, and one row cannot tell an N+1
    # from a fixed page: both cost about the same.  `PRELOAD_ROWS` rows is
    # enough that the two are not close -- measured on the step 06 tree at 4
    # queries preloaded against 37 not, for the same twelve answers.
    #
    # The judgement is `queries_summary`'s and it is a comparison, not a
    # threshold: a page is preloaded when it costs FEWER queries than it
    # renders rows.  A threshold would be a number somebody chose, and it
    # would have to be re-chosen every time a release adds a query of its own;
    # `queries < rows` is the shape of the fault itself, and it stays true
    # about a page whose fixed cost moves.  The status is carried into the
    # same boolean because a page that raises issues almost no queries, and a
    # reading that only counted them would score a 500 as well preloaded.
    #
    # Like `csp_pass` and `hosts_pass`, the records this pass produces are cut
    # back off the shared list: steps 01 to 05 are scored on a reading of the
    # drive, and a page fetched here must not move it.  The rows are left
    # in the database on purpose -- `envboot` and `pkdrift` read schema and
    # not contents, and `rspec` is given a database of its own by `test.sh` --
    # so that a failed drive does not also have to unwind itself.
    # Keyed by a name and not by the path, because `/export.csv` has a dot in
    # it and a gate's `path` is split on dots: `depdrive.queries./export.csv`
    # would be read as four keys.  The path is carried inside the reading, so
    # nothing about which page was driven is lost.
    PRELOAD_ROWS = 12
    PRELOAD_GETS = [%w[voice_answers /voice_answers],
                    %w[export_csv /export.csv]].freeze

    def self.queries_pass(report, session)
      mark = RECORDS.length
      rows = {}
      step(report, 'preload rows') { seed_rows(rows) }
      readings = {}
      count = [0, 0]
      handle = subscribe_sql(count)
      begin
        PRELOAD_GETS.each do |name, path|
          step(report, "QUERIES GET #{path}") do
            count[0] = 0
            count[1] = 0
            status = session.get(path).status
            readings[name] = { 'status' => status, 'queries' => count[0],
                               'async_queries' => count[1] }
            "#{status} in #{count[0]} queries (#{count[1]} async)"
          end
        end
      ensure
        ActiveSupport::Notifications.unsubscribe(handle) if handle
      end
      extra = RECORDS[mark..-1] || []
      RECORDS.slice!(mark..-1) if RECORDS.length > mark
      report['queries_pass_deprecations'] = extra.length
      report['queries'] = queries_summary(rows, readings)
    end

    # One `Caller`, one `Call` and one voice `Answer` per row, spread over the
    # locations the import task made, because `/export.csv` walks calls and
    # `/voice_answers` walks answers and both reach `location` from their own
    # row.  `create!` and not a fixture file: the application's own validations
    # are the only description of a valid row that is correct on all eight
    # releases this file has to run under.
    def self.seed_rows(rows)
      voice = Question.where(feedback_type: 'voice_file').first
      locations = Location.order(:id).to_a
      raise 'no voice question' if voice.nil?
      raise 'no locations' if locations.empty?
      PRELOAD_ROWS.times do |i|
        caller_row = Caller.create!(phone_number: format('+1574555%04d', i))
        call = Call.create!(caller: caller_row,
                            location: locations[i % locations.length],
                            consented_to_callback: true,
                            source: 'depdrive')
        Answer.create!(call: call, question: voice,
                       voice_file_url: "http://depdrive.invalid/a#{i}")
      end
      rows['voice_answers'] = Answer.where.not(voice_file_url: nil).count
      rows['export_csv'] = Call.where.not(location_id: nil).count
      "#{rows['voice_answers']} answers, #{rows['export_csv']} calls"
    end

    # The five-argument block form, which every Active Support from 3.0 to 8.0
    # accepts; `ActiveSupport::Notifications::Event.new(*args)` does not have
    # one signature across that range.  `SCHEMA` and `TRANSACTION` are the
    # framework's own bookkeeping and `:cached` is a query that was not sent,
    # so none of the three is a row's cost.
    UNCOUNTED_SQL = %w[SCHEMA TRANSACTION CACHE].freeze

    # `count` is a two-slot array: [0] every query the page sent, [1] the ones
    # Active Record ran on a background thread.  The second slot is the only
    # honest reading of `.load_async` there is.  A relation that calls
    # `.load_async` with no executor configured does not raise and does not
    # warn -- it loads synchronously and `scheduled?` stays false -- and an
    # executor configured with no call site schedules nothing, so neither the
    # source nor the configuration answers the question on its own.
    # `payload[:async]` is set by Active Record 7.0's
    # `FutureResult#execute_or_wait` and is absent on every earlier release,
    # where `nil` counts as not async and the slot honestly reports 0.
    def self.subscribe_sql(count)
      ActiveSupport::Notifications
        .subscribe('sql.active_record') do |_name, _start, _finish, _id, payload|
          counted, async = sql_counts(payload)
          next unless counted

          count[0] += 1
          count[1] += 1 if async
        end
    rescue StandardError
      nil
    end

    # The decision the block above makes, as a value, so the selftest can put
    # canned payloads through it without an Active Support in the load path.
    def self.sql_counts(payload)
      return [false, false] if payload[:cached] ||
                              UNCOUNTED_SQL.include?(payload[:name].to_s)

      [true, !payload[:async].nil? && payload[:async] != false]
    end

    # What the counts mean, as a value, so the selftest can put canned pairs
    # through it without a database.  A path the pass never reached reports
    # `preloaded => false` rather than being left out: "the page did not answer"
    # and "the page answered badly" are both failures of the same requirement,
    # and a gate reading a missing key would pass on the first.
    def self.queries_summary(rows, readings)
      out = {}
      PRELOAD_GETS.each do |name, path|
        reading = readings[name] || {}
        row_count = rows[name]
        queries = reading['queries']
        out[name] = {
          'path' => path,
          'status' => reading['status'],
          'queries' => queries,
          # 0 and not nil for a page the pass never reached, for the reason
          # the comment above gives about `preloaded`: a gate reading a
          # missing key would pass on a page that never answered.
          'async_queries' => reading['async_queries'].to_i,
          'rows' => row_count,
          'preloaded' => (reading['status'] == 200 &&
                          !queries.nil? && !row_count.nil? &&
                          queries < row_count)
        }
      end
      out
    end

    # Whether the column the application stores subscriber addresses in holds
    # ciphertext, and whether it is still usable once it does.  Six readings,
    # every one of them a round trip through the model rather than a look at
    # the source, because `encrypts :email` in a model file and an encrypted
    # column are different claims: `support_unencrypted_data` leaves the
    # plaintext readable and writable, and a column declared encrypted after
    # the rows were written stays plaintext for those rows forever.
    #
    # The address is mixed case on BOTH sides of the `@`, and there are four
    # of them, because there are four distinct ways to get this wrong and a
    # single reading would let three through:
    #
    #   no_plaintext_at_rest   the requirement.  Read with a raw SELECT, not
    #                          through the model, which would decrypt it back
    #                          and report success about a column full of
    #                          plaintext.
    #   find_by_finds          NON-deterministic encryption satisfies the
    #                          reading above and breaks every `WHERE email =`
    #                          in the application, including the uniqueness
    #                          validator and the controller's
    #                          `find_or_create_by!`.  This is the reading that
    #                          separates the two schemes.
    #   case_variant_finds_same_row / find_or_create_by_reuses
    #                          `validates :email, uniqueness: { case_sensitive:
    #                          false }` gave this column case-insensitive
    #                          identity before the step, and deterministic
    #                          encryption alone takes it away silently -- the
    #                          validator's `LOWER(email) = LOWER($1)` still
    #                          runs, still passes, and now lower-cases base64.
    #                          `downcase: true` is what keeps the identity, and
    #                          these two are what read it: one asks the
    #                          finder, one asks the controller's own call.
    #   long_address_round_trips
    #                          ciphertext is base64 JSON: measured on this
    #                          application at 1.33 bytes per plaintext byte
    #                          plus a fixed 73 (37 chars -> 122, 53 -> 142,
    #                          135 -> 250, 201 -> 283),
    #                          so a legal address the application accepted
    #                          yesterday overflows `varchar(255)` today.  The
    #                          reading is a store-and-read-back of an address
    #                          that fits before encryption and does not after,
    #                          which is the fault itself; the column's declared
    #                          type is reported by `bootprobe.encryption` as
    #                          the evidence that says which way it failed.
    #
    #                          `ENC_LONG` is 201 characters of varied text and
    #                          not 200 of one, because
    #                          `Encryptor::THRESHOLD_TO_JUSTIFY_COMPRESSION` is
    #                          140 bytes and a repeated character compresses to
    #                          nothing: `'e' * 200` stores in 111 bytes, which
    #                          fits `varchar(255)` comfortably and would make
    #                          this reading pass on the tree it exists to fail.
    #                          Measured both ways:
    #                          283 bytes on `text`, and
    #                          `ActiveRecord::ValueTooLong` on
    #                          `varchar(255)` -- while the short address above
    #                          still stores fine there, so the reading is of
    #                          the width and not of the encryption.
    #
    #                          The round trip is CONJOINED with a stored size
    #                          over 255 bytes, because 201 characters fit
    #                          `varchar(255)` and the round trip on its own is
    #                          therefore true of the column this step is
    #                          handed: the requirement is that the column now
    #                          holds more than the old width could, and the
    #                          byte count is the direct reading of exactly
    #                          that.
    #
    # Nothing here raises: a release with no encryption at all answers
    # `ran => true` with `no_plaintext_at_rest => false`, which is the true
    # thing to say about it, and any exception is recorded in `error` and
    # leaves the readings false.  The rows are removed again, unlike
    # `queries_pass`'s, because `Subscriber.count` is a number the mailer task
    # and the suite both read.
    ENC_ADDRESS = 'Depdrive.Encryption.Probe@Example.COM'.freeze
    ENC_VARIANT = 'depdrive.encryption.probe@example.com'.freeze
    ENC_LONG = ('depdrive.encryption.width.probe.neighbourhood.association.of.' \
                'south.bend.west.side.volunteer.coordinator.and.newsletter.' \
                'editor.emeritus+cityvoice-2026-q3-digest-plaintext-copy@' \
                'mail.subdomain.example.org').freeze

    def self.encryption_pass(report)
      mark = RECORDS.length
      out = { 'ran' => false, 'no_plaintext_at_rest' => false,
              'find_by_answers' => false, 'find_by_finds' => false,
              'case_variant_finds_same_row' => false,
              'find_or_create_by_reuses' => false,
              'long_round_trip_answers' => false,
              'long_address_round_trips' => false, 'errors' => {} }
      made = []
      step(report, 'encryption round trip') do
        begin
          out['ran'] = true
          # Each reading gets its own rescue, so that the FIRST requirement a
          # tree misses does not take the diagnosis of the other four down
          # with it.  A tree that encrypts deterministically but without
          # `downcase` raises RecordInvalid at `find_or_create_by!` -- the
          # case-insensitive uniqueness validator refuses the second row --
          # and one shared rescue would have reported "the column is too
          # narrow" about a tree whose column is fine.
          short = attempt(out, 'setup') { Subscriber.create!(email: ENC_ADDRESS) }
          made << short.id if short
          if short
            raw = raw_column('subscribers', 'email', short.id).to_s
            out['stored_bytes'] = raw.bytesize
            # Every case form of the address, because a column that stored it
            # folded is still a column that stored it.
            out['no_plaintext_at_rest'] =
              !raw.downcase.include?(ENC_VARIANT) && raw != ENC_ADDRESS
            # Both halves of the reading are kept: `find_by_answers` is what
            # the finder did, and `find_by_finds` is the requirement, which is
            # that reading CONJOINED with `no_plaintext_at_rest`.  Read alone,
            # "the finder still finds the address it wrote" is true of the
            # plaintext `varchar(255)` column this step is handed -- three of
            # the thirty gate points for doing nothing.  "Still" is a word
            # about the ciphertext: it only means anything once the column
            # holds some.
            out['find_by_answers'] = attempt(out, 'find_by_finds') {
              Subscriber.find_by(email: ENC_ADDRESS).try(:id) == short.id
            } || false
            out['find_by_finds'] =
              out['find_by_answers'] && out['no_plaintext_at_rest']
            out['case_variant_finds_same_row'] =
              attempt(out, 'case_variant_finds_same_row') {
                Subscriber.find_by(email: ENC_VARIANT).try(:id) == short.id
              } || false
            out['find_or_create_by_reuses'] =
              attempt(out, 'find_or_create_by_reuses') {
                before = Subscriber.count
                reused = Subscriber.find_or_create_by!(email: ENC_VARIANT)
                made << reused.id
                reused.id == short.id && Subscriber.count == before
              } || false
          end
          # What the column is declared as, reported rather than gated: a
          # reading of `sql_type` is a reading of the schema, and the schema
          # is the thing the migration is one way of writing.  The gate below
          # reads the store instead.
          out['email_sql_type'] = attempt(out, 'email_sql_type') {
            Subscriber.columns_hash['email'].sql_type
          }
          out['email_limit'] = attempt(out, 'email_limit') {
            Subscriber.columns_hash['email'].limit
          }
          out['long_round_trip_answers'] =
            attempt(out, 'long_address_round_trips') {
              long = Subscriber.create!(email: ENC_LONG)
              made << long.id
              out['long_stored_bytes'] =
                raw_column('subscribers', 'email', long.id).to_s.bytesize
              Subscriber.find(long.id).email == ENC_LONG
            } || false
          # ENC_LONG is 201 characters, which FITS the `varchar(255)` this
          # step is handed, so the round trip alone succeeds on a column
          # nobody widened -- two more gate points for doing nothing.  The
          # requirement is that the column now holds MORE than the old width
          # could, and the stored byte count is the direct reading of that:
          # 283 on the migrated tree, 201 on an unencrypted one, and
          # `ValueTooLong` (no reading at all) on an encrypted-but-unwidened
          # one.
          out['long_address_round_trips'] =
            out['long_round_trip_answers'] && out['long_stored_bytes'].to_i > 255
        ensure
          begin
            Subscriber.where(id: made.compact.uniq).delete_all
          rescue StandardError # rubocop:disable Lint/SuppressedException
          end
        end
        "at rest #{out['no_plaintext_at_rest']}, " \
        "find_by #{out['find_by_finds']}, " \
        "case #{out['case_variant_finds_same_row']}, " \
        "width #{out['long_address_round_trips']} " \
        "(#{out['long_stored_bytes'].inspect} bytes in " \
        "#{out['email_sql_type'].inspect})"
      end
      extra = RECORDS[mark..-1] || []
      RECORDS.slice!(mark..-1) if RECORDS.length > mark
      report['encryption_pass_deprecations'] = extra.length
      report['encryption'] = out
    end

    # One reading, and the exception it raised if it raised one.  `nil` is the
    # answer for a reading that could not be taken, which every caller turns
    # into `false`: "the tree refused to do this" and "the tree did this
    # wrongly" are both failures of the same requirement.
    def self.attempt(out, name)
      yield
    rescue StandardError, NotImplementedError => err
      out['errors'][name] = "#{err.class}: #{err.message.to_s[0, 200]}"
      nil
    end

    # One column of one row, without the model in the way.  `lease_connection`
    # is the spelling Rails 7.2 and later want; asking for `connection` there
    # works and emits the deprecation this file exists to watch for, which
    # would have the instrument manufacture its own finding.
    def self.raw_column(table, column, id)
      connection = if ActiveRecord::Base.respond_to?(:lease_connection)
                     ActiveRecord::Base.lease_connection
                   else
                     ActiveRecord::Base.connection
                   end
      quoted = "#{connection.quote_table_name(table)}." \
               "#{connection.quote_column_name(column)}"
      connection.select_value(
        "SELECT #{quoted} FROM #{connection.quote_table_name(table)} " \
        "WHERE #{connection.quote_column_name('id')} = #{connection.quote(id)}"
      )
    end

    CSP_HEADER = 'Content-Security-Policy'.freeze
    CSP_REPORT_ONLY = 'Content-Security-Policy-Report-Only'.freeze
    # A `<script>` start tag, and whether it has a `src` and a `nonce`.  This
    # is a tag scan and not an HTML parse on purpose: the probe runs inside the
    # candidate's own bundle, and requiring Nokogiri there would make the
    # reading a function of which parser the candidate's resolver happened to
    # pick.
    SCRIPT_TAG = /<script\b([^>]*)>/i
    SRC_ATTR = /\bsrc\s*=/i
    NONCE_ATTR = /\bnonce\s*=\s*(["'])(.*?)\1/i

    # What one response says about the policy.  Recorded rather than judged:
    # the judging is `csp_summary`'s, so that the selftest can drive canned
    # responses through it without a Rack in the load path.
    def self.csp_observe(path, response, sink)
      headers = response.headers || {}
      lookup = lambda do |name|
        value = headers[name] || headers[name.downcase]
        Array(value).flat_map { |line| line.to_s.split("\n") }.first
      end
      type = lookup.call('Content-Type').to_s
      return sink unless type.include?('text/html')

      # Only responses the application itself produced.  Rails 5.2 inserts
      # ContentSecurityPolicy::Middleware inside ShowExceptions, so a request
      # that raises -- this application answers `/locations` with 406 Not
      # Acceptable, which is `ActionController::UnknownFormat` reaching
      # PublicExceptions -- is rendered by a middleware the policy never sees.
      # No tree can put the header on that page, so asking for it there would
      # be asking for something the framework does not offer.
      return sink unless (200..299).cover?(response.status.to_i)

      body = response.body.to_s
      inline = body.scan(SCRIPT_TAG).map { |(attrs)| attrs.to_s }
                   .reject { |attrs| attrs =~ SRC_ATTR }
      sink << {
        'path' => path,
        'status' => response.status,
        'header' => lookup.call(CSP_HEADER),
        'report_only' => lookup.call(CSP_REPORT_ONLY),
        'inline_nonces' => inline.map { |attrs| (attrs[NONCE_ATTR, 2] if attrs =~ NONCE_ATTR) },
        # Taken here rather than in a pass of its own because this is already
        # the filter the question wants: a 2xx HTML response the application
        # itself produced.  `default_headers` is a class attribute and a gate
        # could read it off the class, but what a browser gets is what the
        # middleware stack put on the wire, and those come apart -- a
        # `response.headers.delete` in a controller, an after_action, a rack
        # middleware, or a page served by PublicExceptions instead of the
        # application all change one without the other.
        'security_headers' =>
          SECURITY_HEADERS.each_with_object({}) { |name, out| out[name] = lookup.call(name) }
      }
      sink
    end

    # The six headers `ActionDispatch::Response.default_headers` has carried
    # since 5.2 on this application.  7.1 drops X-Download-Options from the
    # list -- it was an IE8 affordance and IE8 is gone -- and that is the ONE
    # change `load_defaults 7.1` makes to this application that reaches the
    # wire.  It does not reach the behaviour baseline: `cvh/norm.py`'s
    # `_KEEP_HEADERS` is a whitelist of the headers the application decides,
    # and this family is deliberately not in it, so the recorded cases are
    # blind to the whole set by design.  A gate is the only place it can be
    # read, which is why it is read.
    SECURITY_HEADERS = %w[X-Frame-Options X-XSS-Protection X-Content-Type-Options
                          X-Download-Options X-Permitted-Cross-Domain-Policies
                          Referrer-Policy].freeze

    # Three lists, because "the header is gone" and "the headers are gone" are
    # different answers and only one of them is the requirement.
    #
    #   always     name -> value, for headers on EVERY observed HTML response
    #              with the same value each time
    #   sometimes  on some and not others, or differing values -- always a
    #              finding, never a pass: a security header the application
    #              sets on four pages and not the fifth is the shape of a
    #              middleware inserted in the wrong place
    #   seen       every name observed at least once, which is what "gone"
    #              is the absence from
    #
    # An empty observation reports `html_responses` 0 and leaves all three
    # empty, so a gate reading `seen` for an absence would pass a drive that
    # measured nothing.  That is what `html_responses` is for, and it is
    # gated beside the absence rather than instead of it.
    def self.headers_summary(observed)
      values = Hash.new { |h, k| h[k] = [] }
      observed.each do |row|
        SECURITY_HEADERS.each do |name|
          values[name] << (row['security_headers'] || {})[name]
        end
      end
      total = observed.length
      always = {}
      sometimes = {}
      seen = []
      values.each do |name, list|
        present = list.compact.reject { |v| v.to_s.empty? }
        next if present.empty?
        seen << name
        if present.length == total && present.uniq.length == 1
          always[name] = present.first
        else
          sometimes[name] = { 'on' => present.length, 'of' => total,
                              'values' => present.uniq.sort }
        end
      end
      { 'html_responses' => total,
        'always' => always,
        'sometimes' => sometimes,
        'seen' => seen.sort }
    end

    # The two scalars the step-04 gates read, and the counts that make a
    # failure traceable without the header ever reaching the log twice.
    #
    # `problems` is "the policy reached the wire": a header on every HTML
    # response, under the enforcing name and not the report-only one, with a
    # `default-src` that names `'self'`.  Report-Only is called out
    # separately because it is the failure that looks like success -- the
    # header is there, the browser reports and enforces nothing.
    #
    # `nonce_problems` is "the policy does not break the pages this
    # application serves, and the nonce is a nonce": every inline `<script>`
    # carries one, the nonce it carries is one the same response's
    # `script-src` names, and the page fetched twice does not come back with
    # the same nonce both times.
    #
    # The third reading is there because the first two are not enough.  A tree
    # that writes `nonce="abc"` into the layout and `'nonce-abc'` into a
    # static `script-src` satisfies both of them and has no generator at all.
    # A value that is the same on two responses is not a nonce -- `number used
    # once` is the whole of the word -- so the repeat fetch is the reading that
    # tells the two apart, and it
    # is taken of the served page rather than of the configuration so that a
    # generator which is configured and not reached is caught too.
    #
    # An empty observation is a problem and not a pass.  A drive that reached
    # no successful HTML response has measured nothing, and a page with no
    # inline `<script>` left on it is a tree that answered the requirement by
    # deleting the thing it was asked about.
    def self.csp_summary(seen)
      html = seen.length
      problems = []
      nonce_problems = []
      inline_total = 0
      problems << 'no successful HTML response observed' if html.zero?
      seen.each do |row|
        path = row['path']
        header = row['header'].to_s
        directives = csp_directives(header)
        if header.empty?
          problems << if row['report_only'].to_s.empty?
                        "#{path}: no #{CSP_HEADER} header"
                      else
                        "#{path}: #{CSP_REPORT_ONLY} only"
                      end
        elsif !Array(directives['default-src']).include?("'self'")
          problems << "#{path}: default-src does not name 'self'"
        end
        allowed = directives['script-src'] || directives['default-src'] || []
        nonces = Array(row['inline_nonces'])
        inline_total += nonces.length
        nonces.each do |nonce|
          if nonce.nil? || nonce.empty?
            nonce_problems << "#{path}: inline <script> with no nonce"
          elsif !allowed.include?("'nonce-#{nonce}'")
            nonce_problems << "#{path}: inline <script> nonce not named by script-src"
          end
        end
      end
      if html.positive? && inline_total.zero?
        nonce_problems << 'no inline <script> observed in any HTML response'
      end

      # The repeat fetch.  Grouped by path, because that is what makes the two
      # readings comparable: the same page, rendered twice in the same process,
      # a few milliseconds apart.  A page whose inline scripts carry no nonce
      # at all is already named above and is skipped here so that one fault
      # does not report twice.
      seen.group_by { |row| row['path'] }.each do |path, rows|
        next if rows.length < 2
        sets = rows.map do |row|
          Array(row['inline_nonces']).compact.reject(&:empty?).uniq.sort
        end
        next if sets.any?(&:empty?)
        next unless sets.uniq.length == 1
        nonce_problems <<
          "#{path}: the same nonce on two responses -- not minted per request"
      end
      { 'html_responses' => html,
        'inline_scripts' => inline_total,
        'with_header' => seen.count { |row| !row['header'].to_s.empty? },
        'report_only' => seen.count { |row| !row['report_only'].to_s.empty? },
        'directives' => csp_directives(seen.map { |r| r['header'] }.compact.first.to_s).keys.sort,
        'problems' => problems.uniq.sort.first(50),
        'nonce_problems' => nonce_problems.uniq.sort.first(50) }
    end

    # `"default-src 'self' https:; script-src 'self' 'nonce-abc'"` ->
    # `{"default-src" => ["'self'", "https:"], "script-src" => [...]}`.
    def self.csp_directives(header)
      header.to_s.split(';').each_with_object({}) do |chunk, out|
        parts = chunk.strip.split(/\s+/)
        name = parts.shift
        out[name.downcase] = parts unless name.nil? || name.empty?
      end
    end

    # `Rake::Task#invoke` rather than a subprocess: a subprocess would boot a
    # second, uninstrumented copy of the application and its deprecations
    # would go uncounted.
    def self.rake(report, task)
      step(report, "rake #{task}") do
        Rake::Task[task].reenable
        Rake::Task[task].invoke
        'ok'
      end
    end

    def self.step(report, name)
      report['steps'] << { 'step' => name, 'result' => yield.to_s }
    rescue Exception => e # rubocop:disable Lint/RescueException
      report['steps'] << { 'step' => name, 'error' => "#{e.class}: #{e.message}" }
    end

    def self.run(app)
      report = { 'app' => app, 'ruby' => RUBY_VERSION }
      install(app)
      begin
        require File.join(app, 'config', 'environment')
        report['booted'] = true
      rescue Exception => e # rubocop:disable Lint/RescueException
        report['booted'] = false
        report['boot_error'] = "#{e.class}: #{e.message}"
        report['boot_backtrace'] = Array(e.backtrace).first(20)
      end

      drive(app, report) if report['booted']

      report.merge!(summarise(RECORDS))
      report
    end

    # Grouped by message *and* call site so a deprecation raised once per
    # request is one finding rather than eleven, and counted by group: "how
    # many distinct deprecated things does this tree do" is the number a gate
    # can state a requirement about.  `raised` is kept beside it because a
    # count that collapses is a count that can be gamed by raising the same
    # thing from one place instead of fixing it.
    def self.summarise(records)
      groups = records.group_by { |r| [r['message'], r['where']] }
      deprecations = groups.map do |(message, where), rows|
        { 'message' => message, 'where' => where, 'times' => rows.length }
      end.sort_by { |r| [-r['times'], r['message'], r['where']] }
      # `blame` returns a path relative to the application root when it found
      # an application frame and an absolute one when it did not, so the two
      # are told apart by the leading slash.  `own` is the number a gate states
      # a requirement about: a deprecation raised from inside a dependency is
      # that dependency's to fix, and holding the candidate to it would make
      # the gate a function of which gems the resolver happened to pick.  The
      # unfiltered `count` is reported beside it so a run where a dependency
      # started shouting is still visible in the log.
      own = deprecations.reject { |d| d['where'].start_with?('/') }
      { 'deprecations' => deprecations,
        'count' => deprecations.length,
        'own_count' => own.length,
        'own' => own.map { |d| d['where'] },
        'raised' => records.length }
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { app: '/app', out: nil }
  OptionParser.new do |opts|
    opts.on('--app PATH') { |v| options[:app] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
  end.parse!(ARGV)

  app = File.expand_path(options[:app])
  Dir.chdir(app)
  result = CVGate::DepDrive.run(app)
  payload = JSON.pretty_generate(result)
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
  # Always zero: the finding is the file, and a non-zero exit here would make a
  # tree with deprecations indistinguishable from a gate that failed to run.
  exit 0
end
