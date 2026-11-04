#!/usr/bin/env bash
# Reference solution -- step 05, "reach Rails 6.0".
#
# The tree this runs against is step 04's finished tree -- Rails 5.2.8.1 on
# Ruby 2.4.10, `config.load_defaults 5.2`, credentials with the master key
# beside them, bootsnap wired into config/boot.rb, a content security policy
# with per-request nonces.  The first block below says so out loud: every
# count it takes is a fact about *step 04's* output, so running this against
# anything else stops on the first line instead of producing a tree that is
# neither.
#
# This is the first step since step 02 that moves the interpreter, and it
# moves it and the framework in the same breath, because on this rung neither
# can move without the other: Rails 6.0 will not install on Ruby 2.4, and Ruby
# 2.7 is the newest interpreter 6.0 supports.
#
# Two artefacts too long to write inline -- the resolved Gemfile and its
# lockfile -- ship as files beside this script.  Every edit states how many
# lines it expects to touch: a `sed` that matched nothing exits 0 and prints
# exactly what a successful one prints.
set -euo pipefail

APP=${APP:-/app}
FILES=${FILES:-/solution/files}
cd "$APP"

say() { printf '\n== %s\n' "$*"; }

# count <pattern> <path...> -- matching lines across the given files.
count() {
    local pattern="$1"; shift
    grep -rn -- "$pattern" "$@" 2>/dev/null | wc -l | tr -d ' '
}

# files <find-expression...> -- how many paths match, under the current tree.
files() { find "$@" 2>/dev/null | wc -l | tr -d ' '; }

# expect <what> <want> <got>
expect() {
    if [ "$2" != "$3" ]; then
        echo "solve.sh: $1: expected $2, got $3" >&2
        exit 1
    fi
    printf '   ok  %-46s %s\n' "$1" "$3"
}

# rewrite <file> <count> <from> <to> -- replace <from> with <to> in <file>,
# exactly <count> times.  The match is anchored at the end of a line, and both
# strings are literal, never patterns, so the `:`, `{` and `|` in a Ruby
# argument list or an ERB block mean themselves.
rewrite() {
    ruby -e '
      path, want, from, to = ARGV
      src = File.read(path)
      got = src.scan(from + "\n").size
      unless got == Integer(want)
        abort("rewrite: #{path}: #{got} lines end with #{from.inspect}, " \
              "wanted #{want}")
      end
      File.write(path, src.gsub(from + "\n") { to + "\n" })
    ' "$@"
    printf '   ok  %-30s %s\n' "$(basename "$1")" "$(printf '%s' "$3" | head -1)"
}

# ---------------------------------------------------------------------------
say "the tree this expects to find"
# ---------------------------------------------------------------------------
expect "rails 5.2.8.1 in the Gemfile"     1 "$(count "gem 'rails', '5.2.8.1'" Gemfile)"
expect "ruby 2.4.10"                      1 "$(count '^2\.4\.10$' .ruby-version)"
expect "load_defaults 5.2"                1 "$(count 'config.load_defaults 5\.2' config/application.rb)"
expect "credentials and their key"        2 "$(files config -maxdepth 1 -name credentials.yml.enc -o -maxdepth 1 -name master.key)"
expect "bootsnap enabled from boot"       1 "$(count "require 'bootsnap/setup'" config/boot.rb)"
expect "a content security policy"        1 "$(count 'content_security_policy do' config/initializers/content_security_policy.rb)"
expect "nokogiri pinned for ruby 2.4"     1 "$(count "gem 'nokogiri', '1.10.10'" Gemfile)"
expect "rails_12factor"                   1 "$(count 'rails_12factor' Gemfile)"
expect "factory_girl in the Gemfile"      1 "$(count 'factory_girl_rails' Gemfile)"
expect "factory_girl in the suite"        8 "$(count 'FactoryGirl' spec)"
expect "shoulda-matchers 2.8"             1 "$(count "shoulda-matchers', '~> 2.8'" Gemfile)"
expect "no matcher configuration"         0 "$(count 'Shoulda::Matchers' spec)"
expect "update_attributes"                1 "$(count 'update_attributes' app)"
expect "an initializer that autoloads"    1 "$(count '^Rails.application.config.app_content_set = ' config/initializers/app_content_set.rb)"
expect "no autoloader configured"         0 "$(count 'autoloader' config)"
expect "no host authorization"            0 "$(count 'config.hosts' config)"

# ---------------------------------------------------------------------------
say "the interpreter, and the framework that needs it"
# ---------------------------------------------------------------------------
# Rails 6.0 requires Ruby 2.5 or newer, and 2.7 is the newest interpreter it
# supports -- so the one pin cannot move without the other, and this is the
# rung that takes both.  Three files have to name the same interpreter,
# because three different readers consult them: rbenv reads .ruby-version,
# Bundler reads the `ruby` directive in the Gemfile, and `bundle install`
# refuses a lockfile whose RUBY VERSION the running interpreter does not
# satisfy.  Two out of three agreeing is the failure mode worth guarding
# against: it does not stop anything, it just runs the application on an
# interpreter no one chose.
#
# The resolved pair ships beside this script so the tree is reproducible
# without a resolver run; `files/Gemfile` says what moved and why.
printf '2.7.8\n' > .ruby-version
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "the interpreter the tree selects" 1 "$(count '^2\.7\.8$' .ruby-version)"
expect "the same one in the Gemfile"      1 "$(count "^ruby '2.7.8'" Gemfile)"
expect "and in the lockfile"              1 "$(count '^   ruby 2\.7\.8p225$' Gemfile.lock)"
expect "rails pinned to 6.0"              1 "$(count "gem 'rails', '6.0.6.1'" Gemfile)"
expect "framework locked at 6.0"          1 "$(count '^    rails (6\.0\.6\.1)$' Gemfile.lock)"
expect "ruby 2.4's nokogiri cap gone"     0 "$(count "gem 'nokogiri'" Gemfile)"
expect "rails_12factor unresolved"        0 "$(count 'rails_12factor' Gemfile.lock)"
bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "6.0.6.1"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("2.7.")'

# ---------------------------------------------------------------------------
say "framework defaults: 5.2 becomes 6.0"
# ---------------------------------------------------------------------------
# `load_defaults "6.0"` is the 5.2 branch plus five settings of its own
# (railties-6.0.6.1 lib/rails/application/configuration.rb, the `when "6.0"`
# branch): the zeitwerk autoloader, cookies that carry their own purpose and
# expiry, cache keys that version themselves on a collection, Action Mailer's
# delivery job, and form builders that stop emitting the hidden `utf8` field.
# A sixth sets ActiveStorage's queues, which this application does not use.
#
# The declaration also moves to the top of the class body, above the
# eager_load_paths line and the comment that explains it.  It had been wedged
# between that comment and its statement since step 04.
rewrite config/application.rb 1 \
'    # Rails 5.0 stops autoloading once the application has booted when
    # config.eager_load is on, so an autoload-only path leaves lib/ constants
    # undefined in production.  eager_load_paths is also an autoload path, so
    # this keeps development working and makes production load them up front.
    # One declaration of the release whose defaults the application takes.
    # "5.2" is the 5.1 branch plus cache versioning, authenticated cookie and
    # message encryption, SHA-1 digests, forgery protection on by default, and
    # form_with generating field ids again.
    config.load_defaults 5.2

    config.eager_load_paths << Rails.root.join('"'"'lib'"'"')' \
'    # One declaration of the release whose defaults the application takes.
    # "6.0" is the 5.2 branch plus the zeitwerk autoloader, cookies that
    # carry their own purpose and expiry, collection cache versioning, Action
    # Mailer'"'"'s delivery job, and form builders that stop emitting the hidden
    # `utf8` field.
    config.load_defaults 6.0

    # Rails 5.0 stops autoloading once the application has booted when
    # config.eager_load is on, so an autoload-only path leaves lib/ constants
    # undefined in production.  eager_load_paths is also an autoload path, so
    # this keeps development working and makes production load them up front.
    config.eager_load_paths << Rails.root.join('"'"'lib'"'"')'
expect "load_defaults declared"   1 "$(count 'config.load_defaults 6\.0' config/application.rb)"
expect "the 5.2 declaration gone" 0 "$(count 'config.load_defaults 5\.2' config/application.rb)"

# ---------------------------------------------------------------------------
say "the autoloader: what initialization may no longer autoload"
# ---------------------------------------------------------------------------
# `load_defaults 6.0` turns on zeitwerk, and with it the finisher's
# :warn_if_autoloaded initializer, which reports every application constant
# that initialization autoloaded (railties-6.0.6.1
# lib/rails/application/finisher.rb).  Rails 7 raises on the same condition.
#
# This application has exactly one: config/initializers/app_content_set.rb
# calls AppContentSet.load! while the initializers run, which autoloads
# AppContentSet and, through it, AppContentSetImporter.  The complaint is not
# about noise.  An initializer runs once and a reloaded class does not, so
# from the first reload onwards the config value is an instance of a class
# that no longer exists.  `to_prepare` is the hook that runs after the
# autoloader is in place and again after every reload, and it is where a value
# built out of an autoloaded constant belongs.
#
# Nothing downstream moves: the stylesheet that interpolates `header_color`
# is compiled well after the prepare callbacks have run, so the assets this
# application serves are byte for byte what they were.
cat > config/initializers/app_content_set.rb <<'ACS'
# Rails 6.0 reports, and a later release refuses, any application constant that
# initialization autoloads.  An initializer runs once; a reloaded class does
# not, so whatever the initializer held on to is a stale object from the first
# reload onwards.  `to_prepare` is the hook that runs once the autoloader is in
# place and again after every reload, which is where a value built out of an
# autoloaded constant belongs.  The stylesheet that interpolates
# `header_color` is compiled after this has run, so the assets are unchanged.
Rails.application.config.to_prepare do
  Rails.application.config.app_content_set =
    AppContentSet.load!(Rails.root.join('data/app_content_set.csv'))
end
ACS
expect "the value built in to_prepare" 1 "$(count 'config.to_prepare do' config/initializers/app_content_set.rb)"
expect "nothing autoloaded up front"   0 "$(count '^Rails.application.config.app_content_set = ' config/initializers/app_content_set.rb)"

# ---------------------------------------------------------------------------
say "host authorization"
# ---------------------------------------------------------------------------
# 6.0 adds ActionDispatch::HostAuthorization, which answers 403 to any request
# whose Host header is not on `config.hosts`.  Development and test get a
# default list of loopback names; production gets an empty one, and an empty
# list makes the middleware step aside -- so the DNS-rebinding protection the
# release advertises is off in the one environment that faces the internet
# until the list is written down.
#
# Three entries, and the two loopback ones are not padding.  The framework
# strips the port before matching and compares a String permission with `===`,
# so "cityvoice.test" answers for that name and refuses
# "cityvoice.test.evil.example.com"; and the proxy and the platform's health
# check both connect to the container's own address, which means the Host
# header they send is 127.0.0.1 and not the deployment's name.  A list with
# only ENV["APP_URL"] on it would 403 every one of those.
rewrite config/environments/production.rb 1 \
'  config.action_controller.perform_caching = true' \
'  config.action_controller.perform_caching = true

  # Rails 6.0 refuses any request whose Host header is not on this list
  # (ActionDispatch::HostAuthorization).  Development and test are given a
  # default list of loopback names; production is given an empty one, and an
  # empty list means the middleware steps aside -- so unless the list is
  # stated here the DNS-rebinding protection the release added is off in the
  # one environment that faces the internet.
  #
  # APP_URL is the name this application already builds mail links with, below.
  # The two loopback entries are how the deployment reaches the process: the
  # proxy and the platform'"'"'s health check connect to the container'"'"'s own
  # address, and the Host header they send is that address and not the name.
  config.hosts = [ENV["APP_URL"], "localhost", "127.0.0.1"].reject(&:blank?)'
expect "the permitted hosts" 1 "$(count 'config.hosts = ' config/environments/production.rb)"

# ---------------------------------------------------------------------------
say "the logger rails_12factor used to install"
# ---------------------------------------------------------------------------
# rails_12factor is two gems in a trenchcoat, and both of them have to go.
# rails_serve_static_assets turns the file server on, which this file has set
# for itself since step 01; rails_stdout_logging replaces the logger with one
# writing to STDOUT, and it builds that logger with `include ::LoggerSilence`,
# which Rails 6.0 deprecates on every boot.  Neither gem has had a release
# since 2016, so there is no version of them to upgrade to -- the way to stop
# the deprecation is to stop using the gem and set what it set.
#
# ActiveSupport::Logger's own formatter prints the message and nothing else,
# which is the format this log has always had: `config.log_formatter` further
# down is applied by railties only to the logger it builds when the
# application supplies none, and rails_stdout_logging supplied one.
rewrite config/environments/production.rb 1 \
'  # config.logger = ActiveSupport::TaggedLogging.new(SyslogLogger.new)' \
'  # config.logger = ActiveSupport::TaggedLogging.new(SyslogLogger.new)

  # This is rails_stdout_logging written out.  The platform reads the process'"'"'s
  # own output, so the log goes to STDOUT rather than to log/production.log,
  # and the gem that used to arrange that is no longer in the Gemfile.
  #
  # The formatter is the one ActiveSupport::Logger comes with, which prints the
  # message and nothing else -- the format this log has always had, because
  # `config.log_formatter` below is applied only to the logger railties builds
  # when the application supplies none.  TaggedLogging is what the gem wrapped
  # its logger in and what `config.log_tags` above would need.
  STDOUT.sync = true
  config.logger =
    ActiveSupport::TaggedLogging.new(ActiveSupport::Logger.new(STDOUT))'
expect "a logger on STDOUT" 1 "$(count 'ActiveSupport::Logger.new(STDOUT)' config/environments/production.rb)"

# ---------------------------------------------------------------------------
say "update_attributes is deprecated"
# ---------------------------------------------------------------------------
# 6.0 deprecates ActiveRecord::Persistence#update_attributes and
# #update_attributes! in favour of #update and #update!
# (activerecord-6.0.6.1 lib/active_record/persistence.rb); 6.1 removes them.
# One call site, reached by the subscription confirmation link.
rewrite app/models/location_subscription.rb 1 \
'    self.update_attributes(confirmed: true)' \
'    self.update(confirmed: true)'
expect "update_attributes left" 0 "$(count 'update_attributes' app)"

# ---------------------------------------------------------------------------
say "factory_girl becomes factory_bot"
# ---------------------------------------------------------------------------
# factory_girl_rails 4.9 is a shim over factory_bot_rails that prints a
# deprecation when it loads, and the Gemfile above swaps it for the gem it
# points at.  factory_bot 5 also stops accepting a static value as an
# attribute definition -- `confirmed true` has to become `confirmed { true }`
# -- which is four traits here, the rest of the file having been written with
# blocks already.
mv spec/support/factory_girl.rb spec/support/factory_bot.rb
ruby -e '
  changed = 0
  Dir.glob("spec/**/*.rb").sort.each do |path|
    src = File.read(path)
    next unless src.include?("FactoryGirl")
    changed += src.scan("FactoryGirl").size
    File.write(path, src.gsub("FactoryGirl", "FactoryBot"))
  end
  abort("expected 8 FactoryGirl sites, changed #{changed}") unless changed == 8
  printf("   ok  %-46s %s\n", "FactoryGirl -> FactoryBot", changed)
'
rewrite spec/factories.rb 1 "      feedback_type 'numerical_response'" \
                            "      feedback_type { 'numerical_response' }"
rewrite spec/factories.rb 1 "      feedback_type 'voice_file'" \
                            "      feedback_type { 'voice_file' }"
rewrite spec/factories.rb 1 '      confirmed true'  '      confirmed { true }'
rewrite spec/factories.rb 1 '      bulk_added true' '      bulk_added { true }'
expect "factory_girl left in the suite" 0 "$(count 'FactoryGirl' spec)"
expect "factory_girl support file"      0 "$(files spec/support -name 'factory_girl*')"
expect "factory_bot support file"       1 "$(files spec/support -name 'factory_bot.rb')"

# ---------------------------------------------------------------------------
say "the suite: shoulda-matchers 4"
# ---------------------------------------------------------------------------
# shoulda-matchers 2.8 calls `BigDecimal.new` while its matchers load and Ruby
# 2.7 removed that constructor, so the version has to move; 4.5 is the release
# line that supports Rails 6.0.
#
# Two things changed on the way.  From version 3 the gem stopped including
# itself into the example group and asks to be configured instead, without
# which `should belong_to(...)` is an undefined method.  And its association
# matcher learned about Rails 5's required-by-default `belongs_to`: a
# `belongs_to :location, optional: true` -- which is what step 02 wrote on
# Call, because a call row exists before its location is known -- no longer
# satisfies a bare `belong_to(:location)`.  The matcher is told what the model
# says.
cat >> spec/spec_helper.rb <<'SM'
# shoulda-matchers 3 stopped including itself into the example group.  Without
# this block `should belong_to(...)` is an undefined method.
Shoulda::Matchers.configure do |config|
  config.integrate do |with|
    with.test_framework :rspec
    with.library :rails
  end
end

SM
rewrite spec/models/call_spec.rb 1 \
'  it { should belong_to(:location) }' \
'  it { should belong_to(:location).optional }'
expect "matchers configured"          1 "$(count 'Shoulda::Matchers.configure' spec)"
expect "the optional association"     1 "$(count 'belong_to(:location).optional' spec)"
expect "required ones left alone"     1 "$(count 'belong_to(:location) }' spec)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
