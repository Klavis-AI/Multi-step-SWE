#!/usr/bin/env bash
# Reference solution -- step 04, "reach Rails 5.2".
#
# The tree this runs against is step 03's finished tree -- Rails 5.1.7 on Ruby
# 2.4.10, `config.load_defaults 5.1`, the secrets encrypted into
# config/secrets.yml.enc with the key beside them, form_with, parameterized
# mailers.  The first block below says so out loud: every count it takes is a
# fact about *step 03's* output, so running this against anything else stops on
# the first line instead of producing a tree that is neither.
#
# This is the second step running that does not move the interpreter.  Ruby
# 2.4.10 is the interpreter before and after, and `.ruby-version` is not
# touched.
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
expect "rails 5.1.7 in the Gemfile"       1 "$(count "gem 'rails', '5.1.7'" Gemfile)"
expect "ruby 2.4.10"                      1 "$(count '^2\.4\.10$' .ruby-version)"
expect "load_defaults 5.1"                1 "$(count 'config.load_defaults 5\.1' config/application.rb)"
expect "encrypted secrets configured"     1 "$(count 'config.read_encrypted_secrets = true' config/application.rb)"
expect "encrypted secrets store"          1 "$(files config -maxdepth 1 -name secrets.yml.enc)"
expect "its key"                          1 "$(files config -maxdepth 1 -name secrets.yml.key)"
expect "no plaintext secrets"             0 "$(files config -maxdepth 1 -name secrets.yml)"
expect "no credentials yet"               0 "$(files config -maxdepth 1 -name 'credentials.yml.enc' -o -maxdepth 1 -name master.key)"
expect "no bootsnap yet"                  0 "$(count 'bootsnap' Gemfile config/boot.rb)"
expect "no content security policy yet"   0 "$(count 'content_security_policy' config app)"
expect "inline script blocks"             4 "$(count '<script>' app/views)"
expect "be_success expectations"         12 "$(count 'should be_success }' spec)"

# ---------------------------------------------------------------------------
say "the framework"
# ---------------------------------------------------------------------------
# 5.2 is one minor release on from 5.1 and this application's pins do not stand
# in its way.  The resolved pair ships beside this script so the tree is
# reproducible without a resolver run; `files/Gemfile` says what moved and why.
install -m 0644 "$FILES/Gemfile" Gemfile
install -m 0644 "$FILES/Gemfile.lock" Gemfile.lock
expect "rails pinned to 5.2"      1 "$(count "gem 'rails', '5.2.8.1'" Gemfile)"
expect "ruby directive unmoved"   1 "$(count "^ruby '2.4.10'" Gemfile)"
expect "framework locked at 5.2"  1 "$(count '^    rails (5\.2\.8\.1)$' Gemfile.lock)"
bundle install
bundle exec ruby -e 'require "rails"; abort("wrong rails") unless Rails::VERSION::STRING == "5.2.8.1"'
bundle exec ruby -e 'abort("wrong ruby") unless RUBY_VERSION.start_with?("2.4.")'

# ---------------------------------------------------------------------------
say "framework defaults: 5.1 becomes 5.2"
# ---------------------------------------------------------------------------
# `load_defaults "5.2"` is the 5.1 branch plus six settings of its own
# (railties-5.2.8.1 lib/rails/application/configuration.rb, the `when "5.2"`
# branch): cache versioning on Active Record, authenticated cookie encryption
# on Action Dispatch, authenticated message encryption and SHA-1 digests on
# Active Support, forgery protection on by default on Action Controller, and
# `form_with_generates_ids` on Action View.  A seventh is not a setting at all
# but an `on_load(:active_record_sqlite3adapter)` hook, which never fires here:
# this application runs on PostgreSQL.
#
# `read_encrypted_secrets` goes in the same edit.  It pointed at
# config/secrets.yml.enc, which the next block deletes, and 5.2's own default
# for it is false -- leaving the line would be configuration naming a file that
# is not there.
rewrite config/application.rb 1 \
'    # Rails 5.1 replaces the generated new_framework_defaults.rb with one
    # declaration of the release whose defaults the application takes.  "5.1"
    # is the five settings the 5.0 branch sets plus
    # assets.unknown_asset_fallback = false.
    config.load_defaults 5.1

    # The secrets live in config/secrets.yml.enc, decrypted with
    # config/secrets.yml.key (or ENV["RAILS_MASTER_KEY"]).  Set here rather
    # than in config/environments/production.rb because development and test
    # read their secret_key_base from the same file.
    config.read_encrypted_secrets = true' \
'    # One declaration of the release whose defaults the application takes.
    # "5.2" is the 5.1 branch plus cache versioning, authenticated cookie and
    # message encryption, SHA-1 digests, forgery protection on by default, and
    # form_with generating field ids again.
    config.load_defaults 5.2'
expect "load_defaults declared"        1 "$(count 'config.load_defaults 5\.2' config/application.rb)"
expect "read_encrypted_secrets gone"   0 "$(count 'read_encrypted_secrets' config)"

# ---------------------------------------------------------------------------
say "secrets become credentials"
# ---------------------------------------------------------------------------
# 5.2 replaces Rails::Secrets with ActiveSupport::EncryptedConfiguration:
# config/credentials.yml.enc, decrypted with config/master.key or
# ENV["RAILS_MASTER_KEY"], and read through `Rails.application.credentials`.
# In production `secret_key_base` is
# `ENV["SECRET_KEY_BASE"] || credentials.secret_key_base || secrets.secret_key_base`
# (railties-5.2.8.1 lib/rails/application.rb), so moving the value into
# credentials is what puts it in force.
#
# Two things do not carry over, and both are deliberate.
#
# EncryptedConfiguration does not run ERB where Rails::Secrets did, so the
# production entry -- `secret_key_base: <%= ENV["SECRET_TOKEN"] %>` -- cannot
# travel: written into credentials it would be a 25-character template, and
# every cookie the application signed would be signed with the string
# "<%= ENV[\"SECRET_TOKEN\"] %>".  The key is regenerated instead, which is
# what rotating away from an environment variable means.
#
# Credentials are not per-environment on 5.2 either.  Development and test do
# not read them: `Rails.application.secret_key_base` generates and caches a
# secret under tmp/ for those two environments, which is why the old file's
# development and test entries have nowhere to go and are not missed.
bundle exec ruby -e '
  require "securerandom"
  require "active_support"
  require "active_support/encrypted_configuration"

  File.write("config/master.key", ActiveSupport::EncryptedFile.generate_key + "\n")
  File.chmod(0644, "config/master.key")

  body = <<~YAML
    # Read with `Rails.application.credentials`, decrypted with
    # config/master.key or ENV["RAILS_MASTER_KEY"].  Unlike the secrets file
    # this replaces, it is not evaluated as ERB and is not split by
    # environment: development and test generate their own key under tmp/.
    secret_key_base: #{SecureRandom.hex(64)}
  YAML

  config = ActiveSupport::EncryptedConfiguration.new(
    config_path: "config/credentials.yml.enc",
    key_path: "config/master.key",
    env_key: "RAILS_MASTER_KEY",
    raise_if_missing_key: true
  )
  config.write(body)
  File.chmod(0644, "config/credentials.yml.enc")
'
# Read it back in a second process, so the key comes off disk the way the
# application will take it, and check the ciphertext is really ciphertext.
bundle exec ruby -e '
  require "active_support"
  # EncryptedConfiguration#config deep_symbolize_keys the parsed YAML, and
  # outside a booted application the core extension that defines it is not
  # loaded.
  require "active_support/core_ext/hash"
  require "active_support/encrypted_configuration"
  config = ActiveSupport::EncryptedConfiguration.new(
    config_path: "config/credentials.yml.enc",
    key_path: "config/master.key",
    env_key: "RAILS_MASTER_KEY",
    raise_if_missing_key: true
  )
  key = config.config[:secret_key_base]
  abort("credentials did not round-trip") unless key.is_a?(String) && key.length == 128
  ciphertext = File.binread("config/credentials.yml.enc")
  abort("plaintext in the ciphertext") if ciphertext.include?("secret_key_base")
  abort("key in the ciphertext") if ciphertext.include?(key)
  printf("   ok  %-46s %s\n", "credentials written and read back", ciphertext.bytesize)
'
rm config/secrets.yml.enc config/secrets.yml.key
expect "superseded secret store"  0 "$(files config -maxdepth 1 -name 'secrets.yml*')"
expect "credentials"              1 "$(files config -maxdepth 1 -name credentials.yml.enc)"
expect "master key shipped"       1 "$(files config -maxdepth 1 -name master.key)"

# ---------------------------------------------------------------------------
say "bootsnap"
# ---------------------------------------------------------------------------
# 5.2's generated config/boot.rb requires bootsnap/setup after bundler/setup,
# which is the whole of enabling it: the file computes a cache directory from
# its own caller -- hence tmp/cache under this application -- and calls
# Bootsnap.setup with the load-path cache, the autoload-path cache and the
# iseq and YAML compile caches all on.  Requiring the gem without this does
# nothing at all, which is why the Gemfile entry is `require: false`.
rewrite config/boot.rb 1 \
"require 'bundler/setup' if File.exists?(ENV['BUNDLE_GEMFILE'])" \
"require 'bundler/setup' if File.exists?(ENV['BUNDLE_GEMFILE'])

# Caches the load path and the compiled bytecode of everything required below,
# under tmp/cache.  Must come after bundler/setup and before anything else is
# required, and the gem is loaded here rather than by Bundler.require.
require 'bootsnap/setup'"
expect "bootsnap enabled from boot" 1 "$(count "require 'bootsnap/setup'" config/boot.rb)"
expect "bootsnap in the Gemfile"    1 "$(count "gem 'bootsnap'" Gemfile)"

# ---------------------------------------------------------------------------
say "a content security policy, and the nonces the pages need to satisfy it"
# ---------------------------------------------------------------------------
# 5.2 adds ActionDispatch::ContentSecurityPolicy and a middleware that turns
# the configured policy into a Content-Security-Policy header on every HTML
# response.  The generated initializer is entirely commented out, and
# uncommenting it is not enough here: this application serves four inline
# <script> blocks, and a policy whose script-src is `'self' https:` blocks all
# four in any browser that enforces it.
#
# The nonce is the framework's answer.  `content_security_policy_nonce_generator`
# makes Action Dispatch mint a value per request and name it in script-src --
# NONCE_DIRECTIVES is %w[script-src] on 5.2 -- and
# `content_security_policy_nonce` hands the same value to the view, so each
# inline block carries the nonce of the response it is part of.  A hard-coded
# attribute would satisfy neither half.
cat > config/initializers/content_security_policy.rb <<'CSP'
# Rails 5.2 serves this policy as a Content-Security-Policy header on every
# HTML response (ActionDispatch::ContentSecurityPolicy::Middleware).
#
# `:https` is wider than `:self` because this application loads map tiles from
# OpenStreetMap, audio from S3 and, when GOOGLE_ANALYTICS_ID is set, the
# analytics snippet from Google.  Inline styles are allowed because Foundation
# and the map both write them; inline scripts are not -- they carry a nonce
# instead, minted per request below.
Rails.application.config.content_security_policy do |policy|
  policy.default_src :self, :https
  policy.font_src    :self, :https, :data
  policy.img_src     :self, :https, :data
  policy.media_src   :self, :https
  policy.object_src  :none
  policy.script_src  :self, :https
  policy.style_src   :self, :https, :unsafe_inline
end

# One value per request, named in script-src by the framework and handed to the
# views by the `content_security_policy_nonce` helper.
Rails.application.config.content_security_policy_nonce_generator =
  ->(_request) { SecureRandom.base64(16) }
CSP
expect "the policy"        1 "$(count 'content_security_policy do' config/initializers/content_security_policy.rb)"
expect "the nonce source"  1 "$(count 'content_security_policy_nonce_generator' config/initializers/content_security_policy.rb)"

# Three of the four blocks sit at column 0; the fourth is inside the static
# page's markup.  Each becomes a tag carrying this response's own nonce.
for view in app/views/locations/show.html.erb \
            app/views/landing/index.html.erb \
            app/views/application/_header.html.erb; do
    rewrite "$view" 1 '<script>' \
      '<script nonce="<%= content_security_policy_nonce %>">'
done
rewrite app/views/pages/messages.html.erb 1 \
'            <script>' \
'            <script nonce="<%= content_security_policy_nonce %>">'
expect "inline scripts with a nonce" 4 "$(count 'content_security_policy_nonce' app/views)"
expect "inline scripts without one"  0 "$(count '<script>$' app/views)"

# ---------------------------------------------------------------------------
say "the suite: response.success? is deprecated"
# ---------------------------------------------------------------------------
# 5.2 deprecates ActionDispatch::TestResponse#success?, #missing? and #error?
# in favour of #successful?, #not_found? and #server_error?
# (actionpack-5.2.8.1 lib/action_dispatch/testing/test_response.rb).  RSpec's
# `be_success` matcher calls the predicate, so each of these twelve lines
# deprecates when the example runs.  Nine of the suite's controller specs were
# already on the new spelling; these are the twelve that were not.
ruby -e '
  changed = 0
  Dir.glob("spec/**/*.rb").sort.each do |path|
    src = File.read(path)
    next unless src.include?("should be_success }")
    changed += src.scan("should be_success }").size
    File.write(path, src.gsub("should be_success }", "should be_successful }"))
  end
  abort("expected 12 be_success sites, changed #{changed}") unless changed == 12
  printf("   ok  %-46s %s\n", "be_success -> be_successful", changed)
'
expect "be_success left"      0 "$(count 'should be_success }' spec)"
expect "be_successful sites" 47 "$(count 'be_successful' spec)"

# ---------------------------------------------------------------------------
say "the tree still parses"
# ---------------------------------------------------------------------------
# `ruby -c` reads one script and treats the rest of its arguments as ARGV, so
# every file is compiled by name instead.
parsed=$(find app lib config spec db -name '*.rb' -print0 |
    xargs -0 ruby -W0 -e 'ARGV.each { |f| RubyVM::InstructionSequence.compile_file(f) }; print ARGV.size')
expect "ruby files that parse" "$(files app lib config spec db -name '*.rb')" "$parsed"
echo "solve.sh: done"
