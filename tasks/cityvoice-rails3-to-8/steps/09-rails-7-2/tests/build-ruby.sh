#!/usr/bin/env bash
# build-ruby.sh <version> <openssl-prefix|system> <bundler-version>
#
# Builds one CRuby into /opt/rubies/<version>, installs the requested Bundler,
# and proves the interpreter can complete an HTTPS request -- the failure mode
# an old interpreter built against the wrong OpenSSL shows only at run time.
set -euo pipefail

version="$1"
ssl="$2"
bundler="$3"
series="${version%.*}"
prefix="/opt/rubies/${version}"

ssl_opt=""
if [ "$ssl" != "system" ]; then
    ssl_opt="--with-openssl-dir=${ssl}"
fi

cd /tmp
curl -sSfL --retry 5 -O "https://cache.ruby-lang.org/pub/ruby/${series}/ruby-${version}.tar.gz"
tar xzf "ruby-${version}.tar.gz"
cd "ruby-${version}"
# shellcheck disable=SC2086
./configure --prefix="$prefix" --disable-install-doc $ssl_opt >/dev/null
make -j"$(nproc)" >/dev/null 2>&1
make install >/dev/null
cd /
rm -rf "/tmp/ruby-${version}" "/tmp/ruby-${version}.tar.gz"

"${prefix}/bin/gem" install bundler -v "$bundler" --no-document

"${prefix}/bin/ruby" -ropenssl -e '
  require "net/https"
  puts "ruby #{RUBY_VERSION} #{OpenSSL::OPENSSL_VERSION}"
  code = Net::HTTP.get_response(URI("https://rubygems.org/api/v1/versions/rake.json")).code
  abort("https check failed: #{code}") unless code == "200"
'
"${prefix}/bin/bundle" -v
