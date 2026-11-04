# Shared assertion vocabulary for the per-step checks. Sourced by test.sh after
# differential.sh; used by data/step-checks.sh.
#
# Every function here takes the group name as its first argument and records
# exactly one pass or fail for it. Nothing here decides *what* the right answer
# is -- the per-step file does that, because which observables exist and what
# they should say is precisely what changes along this ladder.
#
# The probes are run at most once each and their facts cached, so a step that
# scores three groups off the server probe pays for one handshake, and all three
# groups see the same handshake rather than three that might disagree.

_facts_server="" ; _facts_client="" ; _facts_ch="" ; _facts_ch_ip=""
_facts_client_nochain="" ; _facts_ch_tls12=""
_facts_ip_client="" ; _facts_ip_client_mismatch=""

# Resolve the candidate's example binaries once. A missing binary is a fact
# about the candidate, not an error here: the groups that need it fail with a
# reason that names the paths that were searched.
_cand_bin() { ( cd /app 2>/dev/null && find_example_bin "$1" ); }

_server_facts() {
    [ -n "$_facts_server" ] && { printf '%s\n' "$_facts_server"; return 0; }
    local b; b="$(_cand_bin server)" || { _facts_server="started=no reason=no-server-binary"; \
        printf '%s\n' "$_facts_server"; return 0; }
    _facts_server="$( cd /app && ossl_probe_server "./$b" "$CA" 2>/dev/null | tr '\n' ' ' )"
    printf '%s\n' "$_facts_server"
}

_client_facts() {
    [ -n "$_facts_client" ] && { printf '%s\n' "$_facts_client"; return 0; }
    local b; b="$(_cand_bin client)" || { _facts_client="served=no reason=no-client-binary"; \
        printf '%s\n' "$_facts_client"; return 0; }
    _facts_client="$( cd /app && ossl_probe_client "./$b" "$CA" 2>/dev/null | tr '\n' ' ' )"
    printf '%s\n' "$_facts_client"
}

# The same client against a server that does not send the intermediate. Cached
# separately from _client_facts because it is a different handshake with a
# different expected outcome, and only the steps that ask for it pay for it.
_client_facts_nochain() {
    [ -n "$_facts_client_nochain" ] && { printf '%s\n' "$_facts_client_nochain"; return 0; }
    local b; b="$(_cand_bin client)" || { _facts_client_nochain="served=no reason=no-client-binary"; \
        printf '%s\n' "$_facts_client_nochain"; return 0; }
    _facts_client_nochain="$( cd /app && ossl_probe_client_nochain "./$b" "$CA" 2>/dev/null | tr '\n' ' ' )"
    printf '%s\n' "$_facts_client_nochain"
}

_ip_client_facts() {
    [ -n "$_facts_ip_client" ] && { printf '%s\n' "$_facts_ip_client"; return 0; }
    local b; b="$(_cand_bin client)" || { _facts_ip_client="served=no reason=no-client-binary"; \
        printf '%s\n' "$_facts_ip_client"; return 0; }
    _facts_ip_client="$( cd /app && ossl_probe_ip_client "./$b" "$CA" 2>/dev/null | tr '\n' ' ' )"
    printf '%s\n' "$_facts_ip_client"
}

# The same client against a certificate that does not name the address it
# connected to. Cached separately for the same reason _client_facts_nochain is:
# it is a different handshake with a different expected outcome.
_ip_client_facts_mismatch() {
    [ -n "$_facts_ip_client_mismatch" ] && { printf '%s\n' "$_facts_ip_client_mismatch"; return 0; }
    local b; b="$(_cand_bin client)" || { _facts_ip_client_mismatch="served=no reason=no-client-binary"; \
        printf '%s\n' "$_facts_ip_client_mismatch"; return 0; }
    _facts_ip_client_mismatch="$( cd /app && ossl_probe_ip_client_mismatch "./$b" "$CA" 2>/dev/null | tr '\n' ' ' )"
    printf '%s\n' "$_facts_ip_client_mismatch"
}

_fact() { printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1; }

# --- OpenSSL differential -------------------------------------------------

# diff_server_group <group>
# The candidate's server must complete a TLS 1.3 handshake with OpenSSL and
# echo what it was sent. This is the "does it speak TLS to a peer that shares
# no code with it" question, and it is an obligation at every step.
diff_server_group() {
    local g="$1" f; f="$(_server_facts)"
    if [ "$(_fact "$f" started)" != yes ]; then fail "$g" "server did not start: $f"; return; fi
    if [ "$(_fact "$f" handshake)" != yes ]; then fail "$g" "no handshake with openssl s_client: $f"; return; fi
    if [ "$(_fact "$f" proto)" != TLSv1.3 ]; then fail "$g" "negotiated $(_fact "$f" proto), expected TLSv1.3"; return; fi
    if [ "$(_fact "$f" echo)" != ok ]; then fail "$g" "application data not echoed: $f"; return; fi
    pass "$g" "$f"
}

# diff_client_group <group> [reject-unverifiable]
# The mirror: the candidate's client must verify a real chain served by OpenSSL
# and read back a response. The chain is the frozen 20-year PKI, and the
# intermediate is served separately, so a correct client must build the path.
#
# With `reject-unverifiable`, a second leg runs the same client against the same
# leaf certificate served WITHOUT its intermediate, and requires a non-zero exit.
# Without it, "exits 0 on success" is the only thing ever asserted about the
# client's exit status, and a client that returns 0 unconditionally -- or that
# verifies nothing -- passes the positive leg exactly as a correct one does.
# It is an argument rather than a default because it costs a second handshake
# and because the two legs are separately meaningful; every step that scores
# this group should be asking for it.
diff_client_group() {
    local g="$1" neg="${2:-}" f nf
    f="$(_client_facts)"
    if [ "$(_fact "$f" served)" != yes ]; then fail "$g" "openssl s_server did not come up: $f"; return; fi
    if [ "$(_fact "$f" connected)" != yes ]; then fail "$g" "client exited $(_fact "$f" client_rc): $f"; return; fi
    if [ "$(_fact "$f" got_response)" != yes ]; then fail "$g" "client connected but read no response: $f"; return; fi
    if [ "$neg" = reject-unverifiable ]; then
        nf="$(_client_facts_nochain)"
        if [ "$(_fact "$nf" served)" != yes ]; then
            fail "$g" "openssl s_server without the intermediate did not come up: $nf"; return; fi
        if [ "$(_fact "$nf" client_rc)" = 0 ]; then
            fail "$g" "client exited 0 against a chain it cannot verify: $nf"; return; fi
        pass "$g" "$f| rejected-unverifiable rc=$(_fact "$nf" client_rc)"
        return
    fi
    pass "$g" "$f"
}

# ticket_count_group <group> <expected>
# How many NewSessionTickets the candidate's server sends on a fresh TLS 1.3
# connection. This is a library default that changes twice along the ladder
# (1 -> 4 at step 04, 4 -> 2 at step 11), which is why the expected value is an
# argument and not a constant.
ticket_count_group() {
    local g="$1" want="$2" f n; f="$(_server_facts)"
    if [ "$(_fact "$f" handshake)" != yes ]; then fail "$g" "no handshake, cannot count tickets: $f"; return; fi
    n="$(_fact "$f" tickets)"
    if [ "$n" = "$want" ]; then pass "$g" "sent $n NewSessionTicket(s)"
    else fail "$g" "sent ${n:-no} NewSessionTicket(s), expected $want"; fi
}

# ip_peer_cert_group <group>
# The candidate's client, pointed at a peer it is given an **IP address** as
# the name of rather than a hostname. Two legs, and both are needed:
#
#   positive -- the certificate carries 127.0.0.1 as an iPAddress
#               subjectAltName, and the client must complete the handshake and
#               read the response;
#   negative -- the same client against the ordinary fixture leaf, which names
#               only DNS hosts, where it must refuse.
#
# The positive leg alone would be passed by a client that simply stopped
# checking the name once the name was an address; the negative leg alone is
# passed by every release that cannot talk to an IP-named peer at all. Together
# they say: the address was checked against the certificate.
#
# Distinct from ch_ip_peer_group, which asks the earlier question -- whether a
# ClientHello is produced for an IP-named peer at all.
ip_peer_cert_group() {
    local g="$1" f nf
    f="$(_ip_client_facts)"
    if [ "$(_fact "$f" served)" != yes ]; then
        fail "$g" "openssl s_server with an IP-SAN certificate did not come up: $f"; return; fi
    if [ "$(_fact "$f" connected)" != yes ]; then
        fail "$g" "client exited $(_fact "$f" client_rc) against a peer named 127.0.0.1 whose certificate carries that address: $f"; return; fi
    if [ "$(_fact "$f" got_response)" != yes ]; then
        fail "$g" "client reached an IP-named peer but read no response: $f"; return; fi
    nf="$(_ip_client_facts_mismatch)"
    if [ "$(_fact "$nf" served)" != yes ]; then
        fail "$g" "openssl s_server with the DNS-only certificate did not come up: $nf"; return; fi
    if [ "$(_fact "$nf" client_rc)" = 0 ]; then
        fail "$g" "client exited 0 against an IP-named peer whose certificate names only DNS hosts: $nf"; return; fi
    pass "$g" "$f| refused-dns-only-leaf rc=$(_fact "$nf" client_rc)"
}

# --- ClientHello wire observation ----------------------------------------

# _ch_facts <hostname> <connections> [extra client flags ...]
# Runs a listener that parses the ClientHello and points the candidate's client
# at it the requested number of times. Prints the observer's key=value report.
#
# The trailing arguments are appended to the client's command line before
# `--http <host>`. They exist so that a step can ask what the client offers
# under a *configuration* rather than only under its default -- the same
# binary, told to speak one version, writes a different hello. They are
# folded into the output filename so two sweeps with different argv never
# overwrite each other's report.
_ch_facts() {
    local host="$1" n="$2"; shift 2
    local b port out tag
    b="$(_cand_bin client)" || { echo "error=no-client-binary"; return 0; }
    port="$(_free_port)"
    tag="$(printf '%s' "$*" | tr -c 'A-Za-z0-9' '-')"
    out="$WORK/ch-$host-$n$tag.out"
    run_unprivileged python3 "$TESTS/bin/observe-clienthello.py" \
        --port "$port" --count "$n" --timeout 30 --summary \
        > "$out" 2> "$WORK/ch-$host-$n$tag.err" &
    local op=$!
    # The observer announces its listening socket on stderr before it accepts,
    # so the client is never started against a socket that does not exist yet.
    local i
    for i in $(seq 1 100); do
        grep -q listening "$WORK/ch-$host-$n$tag.err" 2>/dev/null && break
        sleep 0.1
    done
    for i in $(seq 1 "$n"); do
        # 30s, because TEST_INTERFACE.md sec 5 promises each probe 30 seconds
        # and this is a probe. This is an upper bound and not a cost: the
        # observer closes each connection as soon as it has the hello, so a
        # client that works returns in well under a second and only one that
        # has hung ever reaches the bound.
        ( cd /app && run_unprivileged timeout 30 "./$b" --cafile "$CA/ca.cert" \
            --port "$port" "$@" --http "$host" ) > /dev/null 2>&1 || true
    done
    wait $op 2>/dev/null || true
    cat "$out" 2>/dev/null
}

# ch_extension_order_group <group> <stable|randomised>
# Whether the extension ordering in the ClientHello is byte-stable across
# repeated connections. rustls randomises it from 0.23.0, so "stable" is the
# correct answer below that step and "randomised" at and above it.
ch_extension_order_group() {
    local g="$1" want="$2" f n orders
    [ -n "$_facts_ch" ] || _facts_ch="$(_ch_facts localhost 6)"
    f="$_facts_ch"
    n="$(printf '%s\n' "$f" | sed -n 's/^samples: *//p' | head -1)"
    orders="$(printf '%s\n' "$f" | sed -n 's/^distinct_extension_orders: *//p' | head -1)"
    if [ "${n:-0}" -lt 6 ]; then
        fail "$g" "only ${n:-0} of 6 connections produced a ClientHello: $(printf '%s' "$f" | tr '\n' ' ')"
        return
    fi
    case "$want" in
      stable)
        if [ "$orders" -eq 1 ]; then pass "$g" "one extension order across $n connections"
        else fail "$g" "$orders distinct extension orders across $n connections, expected 1"; fi ;;
      randomised)
        # Six samples. A fixed order gives exactly 1; a shuffled one gives 1
        # only if six independent permutations of eleven extensions all
        # coincide, so observing more than 1 here is not a coin flip.
        if [ "$orders" -gt 1 ]; then pass "$g" "$orders distinct extension orders across $n connections"
        else fail "$g" "extension order identical in all $n connections, expected it to vary"; fi ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}

# ch_extension_group <group> <absent|present> <id> <name>
# Whether a numbered ClientHello extension is offered, over the same six-
# connection sweep the other ClientHello groups read. This asks only "is the
# extension there", never what is inside it or where in the list it sits --
# those are not graded anywhere on this ladder, and a group that depended on
# them would break every time the client reordered its hello.
#
# The two expectations deliberately read two different aggregates, because
# they have opposite failure modes. `present` reads the intersection over all
# six hellos, so one hello that carries the extension cannot satisfy it.
# `absent` reads the union, so one hello that still carries it is enough to
# fail. Either way a client cannot pass by being inconsistent.
ch_extension_group() {
    local g="$1" want="$2" id="$3" name="$4" f n list
    [ -n "$_facts_ch" ] || _facts_ch="$(_ch_facts localhost 6)"
    f="$_facts_ch"
    n="$(printf '%s\n' "$f" | sed -n 's/^samples: *//p' | head -1)"
    if [ "${n:-0}" -lt 6 ]; then
        fail "$g" "only ${n:-0} of 6 connections produced a ClientHello: $(printf '%s' "$f" | tr '\n' ' ')"
        return
    fi
    case "$want" in
      absent)  list="$(printf '%s\n' "$f" | sed -n 's/^extensions_any: *//p'   | head -1)" ;;
      present) list="$(printf '%s\n' "$f" | sed -n 's/^extensions_every: *//p' | head -1)" ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'"; return ;;
    esac
    # The observer prints both lines whenever it parsed at least one hello, so
    # a missing line means the report is not the one this function was written
    # against. Say that rather than reading an empty list as "not offered",
    # which would pass `absent` for the wrong reason.
    if ! printf '%s\n' "$f" | grep -q '^extensions_any:'; then
        fail "$g" "observer reported no extension summary: $(printf '%s' "$f" | tr '\n' ' ')"
        return
    fi
    local hit=no
    case ",$list," in *",$id,"*) hit=yes ;; esac
    if [ "$want" = absent ]; then
        # An empty union would make "offered in none" vacuously true for every
        # extension id at once, so a report that saw no extension at all is a
        # failure of the observation rather than a pass.
        if [ -z "$list" ]; then
            fail "$g" "no extension was seen in any of $n ClientHellos, so 'extension $id absent' is vacuous: $(printf '%s' "$f" | tr '\n' ' ')"
            return
        fi
        if [ "$hit" = no ]; then pass "$g" "extension $id ($name) offered in none of $n ClientHellos; saw [$list]"
        else fail "$g" "extension $id ($name) still offered; extensions seen across $n ClientHellos: [$list]"; fi
    else
        if [ "$hit" = yes ]; then pass "$g" "extension $id ($name) offered in all $n ClientHellos; saw [$list]"
        else fail "$g" "extension $id ($name) not offered in every ClientHello; common extensions across $n: [$list]"; fi
    fi
}

# ch_session_id_group <group> <tls13|tls12> <empty|random>
# The length of legacy_session_id in the client's ClientHello, under a stated
# version configuration. TLS 1.3 tells a client to put a 32-byte value there
# so the handshake looks like a resumption to middleboxes (RFC 8446 4.1.2,
# "middlebox compatibility mode", D.4); a client that has been configured out
# of TLS 1.3 altogether has nothing to be compatible with and the field is
# free to be empty again. Which of those two a TLS-1.2-only configuration
# produces is a visible property of the client and it changes along this
# ladder, which is why both the configuration and the expectation are
# arguments.
#
# `tls12` runs a second, separately cached sweep with `--protover 1.2` on the
# client's command line; `tls13` reads the default sweep every other
# ClientHello group already pays for.
ch_session_id_group() {
    local g="$1" cfg="$2" want="$3" f n lens
    case "$cfg" in
      tls13)
        [ -n "$_facts_ch" ] || _facts_ch="$(_ch_facts localhost 6)"
        f="$_facts_ch" ;;
      tls12)
        [ -n "$_facts_ch_tls12" ] || _facts_ch_tls12="$(_ch_facts localhost 3 --protover 1.2)"
        f="$_facts_ch_tls12" ;;
      *) fail "$g" "checks-lib: unknown client configuration '$cfg'"; return ;;
    esac
    n="$(printf '%s\n' "$f" | sed -n 's/^samples: *//p' | head -1)"
    lens="$(printf '%s\n' "$f" | sed -n 's/^session_id_lens: *//p' | head -1)"
    if [ "${n:-0}" -lt 1 ] || [ -z "$lens" ]; then
        fail "$g" "no ClientHello observed from the $cfg client: $(printf '%s' "$f" | tr '\n' ' ')"
        return
    fi
    # `lens` is every distinct length seen, so a single value means all of the
    # connections agreed. A client that is sometimes empty is not "empty".
    case "$want" in
      empty)
        if [ "$lens" = 0 ]; then pass "$g" "legacy_session_id empty in all $n $cfg hello(s)"
        else fail "$g" "legacy_session_id length(s) $lens across $n $cfg hello(s), expected 0"; fi ;;
      random)
        if [ "$lens" = 32 ]; then pass "$g" "legacy_session_id 32 bytes in all $n $cfg hello(s)"
        else fail "$g" "legacy_session_id length(s) $lens across $n $cfg hello(s), expected 32"; fi ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}

# --- server-side wire probes ----------------------------------------------
#
# These drive the candidate's own server with hand-built ClientHellos. That is
# the only way to ask questions no conforming client will ask: an SNI holding
# an IP literal, or a record carrying a handshake message that should not be
# there. One server is started for the whole run and reused.

_probe_port="" ; _probe_pid=""

# Whether the candidate's client can be pointed at a peer named by an IP
# address literal at all. The early releases have no address form in the name
# type their client API accepts, so the name is rejected before a socket is
# opened and the observer sees nothing; once the form exists a ClientHello is
# produced and reaches the peer. This group asserts exactly that and nothing
# more -- what that hello carries, and in particular whether it carries a
# server_name extension for an IP peer, is a separate question, is the same at
# every level from here on, and is named as unscored.
ch_ip_peer_group() {
    local g="$1" want="$2" f n
    [ -n "$_facts_ch_ip" ] || _facts_ch_ip="$(_ch_facts 127.0.0.1 1)"
    f="$_facts_ch_ip"
    n="$(printf '%s\n' "$f" | sed -n 's/^samples: *//p' | head -1)"
    case "$want" in
      hello)
        if [ "${n:-0}" -ge 1 ]; then pass "$g" "client sent a ClientHello to an IP-literal peer"
        else fail "$g" "no ClientHello reached an IP-literal peer: $(printf '%s' "$f" | tr '\n' ' ')"; fi ;;
      no-hello)
        if [ "${n:-0}" -lt 1 ]; then pass "$g" "client sent no ClientHello to an IP-literal peer"
        else fail "$g" "client sent $n ClientHello(s) to an IP-literal peer, expected none"; fi ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}

_probe_server_up() {
    if [ -n "$_probe_pid" ] && kill -0 "$_probe_pid" 2>/dev/null; then return 0; fi
    local b; b="$(_cand_bin server)" || return 1
    # Three attempts, each on a freshly drawn port, because `_free_port` tests
    # the port and the server binds it a moment later and nothing holds it in
    # between; losing that window shows up as `cannot listen on port: Os {
    # code: 98, kind: AddrInUse }`. A scored group that goes red because the
    # kernel handed a port to something else in the microsecond between the
    # check and the bind is measuring the machine, not the candidate. Retrying
    # the *startup* only: once the server is listening, a probe reports what it
    # saw and is never re-rolled.
    local attempt
    for attempt in 1 2 3; do
        _probe_port="$(_free_port)"
        # Backgrounded directly, the way differential.sh starts its server, and
        # not through a wrapping subshell. `exec` does not look up shell
        # functions, so `exec run_unprivileged` would be a guaranteed 127. And
        # even without the exec, $! would name the subshell rather than the
        # server, which is what _wait_port's died-vs-slow test and the kill in
        # cleanup both need. The binary path is absolute so no `cd /app` is
        # needed; the server reads nothing else relative to it.
        run_unprivileged "/app/$b" --certs "$CA/end.fullchain" \
            --key "$END_KEY" --port "$_probe_port" echo \
            > "$WORK/probe-server.log" 2>&1 &
        _probe_pid=$!
        if _wait_port "$_probe_port" "$_probe_pid" 300; then return 0; fi
        # Whatever it said on the way down. A probe server that will not come
        # up fails several groups at once, and without this the verifier log
        # says only that it failed.
        log "[verifier] probe server did not come up (attempt $attempt/3): $(tail -n 3 "$WORK/probe-server.log" 2>/dev/null | tr '\n' ' ')"
        _kill_pid "$_probe_pid"; _probe_pid=""
    done
    return 1
}

checks_lib_cleanup() {
    [ -n "$_probe_pid" ] && _kill_pid "$_probe_pid"
    _probe_pid=""
}

# _send_hello <sni-or-empty> <mode> -> "kind:detail"
_send_hello() {
    local sni="$1" mode="$2" out
    if [ -z "$sni" ]; then
        out="$(run_unprivileged python3 "$TESTS/bin/send-clienthello.py" \
            --port "$_probe_port" --mode "$mode" --timeout 30 2>/dev/null)"
    else
        out="$(run_unprivileged python3 "$TESTS/bin/send-clienthello.py" \
            --port "$_probe_port" --sni "$sni" --mode "$mode" --timeout 30 2>/dev/null)"
    fi
    printf '%s' "${out#result=}" | sed 's/ detail=/:/'
}

# server_sni_group <group> <accept|reject> <sni> [<sni> ...]
# What the candidate's server does with each given server_name value. "accept"
# means it answers with a ServerHello -- it parsed the extension and carried on;
# "reject" means it refuses with a fatal alert. Every listed name must give the
# same answer or the group fails, so a server that merely stopped parsing
# server_name altogether cannot collect the accept case for free: the reject
# case is graded alongside it at every step.
server_sni_group() {
    local g="$1" want="$2"; shift 2
    if ! _probe_server_up; then fail "$g" "candidate server did not start"; return; fi
    local sni r bad=""
    for sni in "$@"; do
        r="$(_send_hello "$sni" normal)"
        case "$want:$r" in
            accept:handshake:*) ;;
            reject:alert:fatal*) ;;
            *) bad="$bad [$sni -> $r]" ;;
        esac
    done
    if [ -n "$bad" ]; then fail "$g" "expected every name to be ${want}ed;$bad"
    else pass "$g" "$# name(s) ${want}ed as required"; fi
}

# _send_hello_ack <sni-or-empty> -> "kind:detail"
# One TLS 1.2 hello, read as far as the ServerHello, reporting whether that
# ServerHello carries a server_name extension. TLS 1.2 because the
# acknowledgement is in the clear there; see send-clienthello.py.
_send_hello_ack() {
    local sni="$1" out
    if [ -z "$sni" ]; then
        out="$(run_unprivileged python3 "$TESTS/bin/send-clienthello.py" \
            --port "$_probe_port" --sni-ack --timeout 30 2>/dev/null)"
    else
        out="$(run_unprivileged python3 "$TESTS/bin/send-clienthello.py" \
            --port "$_probe_port" --sni "$sni" --sni-ack --timeout 30 2>/dev/null)"
    fi
    printf '%s' "${out#result=}" | sed 's/ detail=/:/'
}

# server_sni_ack_group <group> <hostname> <ignored-name> [<ignored-name> ...]
# Whether the server *used* each name or disregarded it.
#
# Accepting a name and acting on it are different things, and the first record
# cannot tell them apart: a server that dispatched on the name and a server
# that threw the name away both answer with a ServerHello. RFC 6066 section 3
# closes the gap -- a server that based its choice on the name "SHALL include
# an extension of type server_name" in its hello, with an empty body -- so the
# acknowledgement is the observable, and this group requires it to be present
# for <hostname> and absent for each <ignored-name>.
#
# The reference point is the same server answering a hello that carries no
# server_name at all: that must come back unacknowledged too, and it is
# checked here rather than assumed, because a server that had simply stopped
# emitting the acknowledgement would otherwise collect every "ignored" case
# for free while failing the rule this group exists to state.
server_sni_ack_group() {
    local g="$1" host="$2"; shift 2
    if ! _probe_server_up; then fail "$g" "candidate server did not start"; return; fi
    local r bad="" seen=""

    # The baseline: no extension sent, so nothing to acknowledge.
    r="$(_send_hello_ack "")"
    case "$r" in
        sni-ack:not-acknowledged*) ;;
        *) bad="$bad [no-server_name -> $r]" ;;
    esac

    # The name the server is expected to use.
    r="$(_send_hello_ack "$host")"
    case "$r" in
        sni-ack:acknowledged*) seen="$seen $host=used" ;;
        *) bad="$bad [$host -> $r, expected the name to be acknowledged]" ;;
    esac

    # The names it must parse and then disregard.
    local name
    for name in "$@"; do
        r="$(_send_hello_ack "$name")"
        case "$r" in
            sni-ack:not-acknowledged*) seen="$seen $name=disregarded" ;;
            sni-ack:acknowledged*) bad="$bad [$name -> acknowledged, expected it to be disregarded]" ;;
            *) bad="$bad [$name -> $r]" ;;
        esac
    done

    if [ -n "$bad" ]; then fail "$g" "server_name acknowledgement wrong;$bad"
    else pass "$g" "acknowledgement follows use:$seen"; fi
}

# _send_hello_psk_nomodes -> "kind:detail"
# One hello offering pre_shared_key with psk_key_exchange_modes left out. The
# framing is ordinary; it is the extension set that is the question, so this
# does not go through the `--mode` table.
_send_hello_psk_nomodes() {
    local out
    out="$(run_unprivileged python3 "$TESTS/bin/send-clienthello.py" \
        --port "$_probe_port" --sni localhost --psk-without-modes --timeout 30 2>/dev/null)"
    printf '%s' "${out#result=}" | sed 's/ detail=/:/'
}

# server_psk_modes_group <group> <abort|accept>
# What the candidate's server does with a ClientHello that offers
# "pre_shared_key" and no "psk_key_exchange_modes". RFC 8446 4.2.9: a client
# "MUST provide" the latter if it offers the former, and "servers MUST abort
# the handshake if [it] is not present". `abort` requires a fatal alert as the
# first record; `accept` requires a ServerHello, which is what an
# implementation that never checked the pairing produces.
#
# Only the level of the alert is read, never its description: which
# description a peer chooses here is its own decision and is not what this
# group is about.
server_psk_modes_group() {
    local g="$1" want="$2" r
    if ! _probe_server_up; then fail "$g" "candidate server did not start"; return; fi
    r="$(_send_hello_psk_nomodes)"
    case "$want:$r" in
      abort:alert:fatal*)   pass "$g" "pre_shared_key without psk_key_exchange_modes refused: $r" ;;
      accept:handshake:*)   pass "$g" "pre_shared_key without psk_key_exchange_modes answered: $r" ;;
      abort:*)  fail "$g" "expected a fatal alert for pre_shared_key without psk_key_exchange_modes, got $r" ;;
      accept:*) fail "$g" "expected a ServerHello for pre_shared_key without psk_key_exchange_modes, got $r" ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}

# server_framing_group <group> <accept|reject|refuse> <mode> [mode ...]
# What the candidate's server does with given record framings of a ClientHello.
# All listed modes must give the same answer. The three expectations are not
# interchangeable:
#
#   accept  the first record back is a handshake record -- the hello was read.
#   reject  the first record back is a fatal alert.
#   refuse  the hello is not answered and the peer is not left waiting: a fatal
#           alert or a connection close both count, silence does not. Use this
#           where the graded obligation is "do not hang", and which of the two
#           refusals an implementation picks is its own choice rather than
#           something the contract should pin. `send-clienthello.py` reports
#           the two apart in its detail string -- "server closed without
#           sending a record" against "no reply within Ns" -- so the
#           distinction is measurable and is the whole point of the
#           expectation.
#
# Most modes vary only the framing of one well-formed hello. `huge-hs` is the
# exception: it varies the *declared length* in the handshake header, which is
# what makes a buffer bound observable.
server_framing_group() {
    local g="$1" want="$2"; shift 2
    if ! _probe_server_up; then fail "$g" "candidate server did not start"; return; fi
    local mode r bad="" good="" verb="${want}ed"
    case "$want" in refuse) verb="refused" ;; esac
    for mode in "$@"; do
        r="$(_send_hello localhost "$mode")"
        case "$want:$r" in
            accept:handshake:*) good="$good $mode" ;;
            reject:alert:fatal*) good="$good $mode" ;;
            refuse:alert:fatal*) good="$good $mode" ;;
            "refuse:none:server closed"*) good="$good $mode" ;;
            *) bad="$bad [$mode -> $r]" ;;
        esac
    done
    if [ -n "$bad" ]; then fail "$g" "expected every framing to be $verb;$bad"
    else pass "$g" "$verb:$good"; fi
}

# flight_shape_group <group> <coalesced|separate>
# Whether the server packs its encrypted handshake flight into one record or
# emits a record per message. Both are legal; which one an implementation does
# is a visible choice that changed when the flight gained an output buffer.
flight_shape_group() {
    local g="$1" want="$2" shape n_app
    if ! _probe_server_up; then fail "$g" "candidate server did not start"; return; fi
    shape="$(run_unprivileged python3 "$TESTS/bin/send-clienthello.py" \
        --port "$_probe_port" --sni localhost --flight --timeout 30 2>/dev/null)"
    shape="${shape#result=flight detail=}"
    # Count ContentType::ApplicationData records -- the encrypted part of the
    # flight. ServerHello and the compatibility CCS are always separate.
    n_app="$(printf '%s' "$shape" | tr ',' '\n' | grep -c '^23/' || true)"
    if [ "${n_app:-0}" -eq 0 ]; then fail "$g" "no encrypted flight observed; records=$shape"; return; fi
    case "$want" in
      coalesced)
        if [ "$n_app" -eq 1 ]; then pass "$g" "flight in one record; records=$shape"
        else fail "$g" "$n_app encrypted records, expected the flight coalesced into 1; records=$shape"; fi ;;
      separate)
        if [ "$n_app" -gt 1 ]; then pass "$g" "$n_app encrypted records; records=$shape"
        else fail "$g" "flight coalesced into 1 record, expected one per message; records=$shape"; fi ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}


# --- 0-RTT early data -----------------------------------------------------

_facts_ed_off="" ; _facts_ed_cap=""

# early_data_group <group> <cap>
#
# Whether the candidate's server can be told to read 0-RTT early data, and
# whether the limit it then advertises is the one it was given. Two legs, and
# both are needed:
#
#   off  -- the server started with the ordinary command line of section 3 must
#           advertise `max_early_data_size` **0** in its tickets, and a client
#           resuming one of them must still complete the handshake. Early data
#           is off unless it is switched on; a server that reads it
#           unconditionally is a server that accepts replayable application
#           data from anyone holding a ticket, and it fails here.
#   on   -- the same server started with `--max-early-data <cap>` must
#           advertise **exactly <cap>**, and must then actually read the early
#           data a resuming client sends -- `Early data was accepted`, with the
#           connection resumed.
#
# Either leg alone is passable without the capability. The `off` leg is passed
# by every release that has no early-data support at all, which is why it is
# never scored on its own; the `on` leg alone would be passed by a server that
# advertises a limit it has no intention of honouring, since nothing in it
# would have to read a byte of early data. Together they say: the limit is
# configurable, it is what it was configured to be, and data arriving under it
# is delivered.
#
# What this group does **not** ask is what happens when a client sends *more*
# than the cap. That is the sharper half of the behaviour and it is graded --
# by BoGo's early-data cases, which drive a shim that can be made to overrun
# deliberately. It cannot be asked here: `openssl s_client` enforces the
# advertised limit on its own side and refuses to write past it, so an over-cap
# probe built on it measures OpenSSL rather than the candidate. Section 6 of
# TEST_INTERFACE.md says so rather than leaving the gap to be inferred.
early_data_group() {
    local g="$1" cap="$2" b off on
    b="$(_cand_bin server)" || { fail "$g" "no server binary found under /app"; return; }
    [ -n "$_facts_ed_off" ] || _facts_ed_off="$( cd /app && ossl_probe_early_data "/app/$b" "$CA" 2>/dev/null | tr '\n' ' ' )"
    off="$_facts_ed_off"
    if [ "$(_fact "$off" handshake)" != yes ]; then
        fail "$g" "no handshake against the server started without an early-data flag: $off"; return; fi
    if [ "$(_fact "$off" max_early)" != 0 ]; then
        fail "$g" "server advertised max_early_data_size $(_fact "$off" max_early) with no --max-early-data flag given; early data must be off by default: $off"; return; fi
    if [ "$(_fact "$off" resumed)" != yes ]; then
        fail "$g" "client could not resume a ticket from the default server: $off"; return; fi

    [ -n "$_facts_ed_cap" ] || _facts_ed_cap="$( cd /app && ossl_probe_early_data "/app/$b" "$CA" "$cap" 2>/dev/null | tr '\n' ' ' )"
    on="$_facts_ed_cap"
    if [ "$(_fact "$on" started)" != yes ]; then
        fail "$g" "server did not start with --max-early-data $cap; see the verifier log for what it said: $on"; return; fi
    if [ "$(_fact "$on" handshake)" != yes ]; then
        fail "$g" "no handshake against the server started with --max-early-data $cap: $on"; return; fi
    if [ "$(_fact "$on" max_early)" != "$cap" ]; then
        fail "$g" "server advertised max_early_data_size $(_fact "$on" max_early), expected exactly $cap: $on"; return; fi
    if [ "$(_fact "$on" resumed)" != yes ]; then
        fail "$g" "client could not resume a ticket from the server started with --max-early-data $cap: $on"; return; fi
    if [ "$(_fact "$on" early)" != accepted ]; then
        fail "$g" "early data was $(_fact "$on" early), expected accepted, under a cap of $cap: $on"; return; fi
    pass "$g" "default max_early_data_size 0 and resumption works| --max-early-data $cap advertised as $(_fact "$on" max_early) and early data accepted"
}

# --- ClientHello key-exchange offer ---------------------------------------

# kx_offer_group <group> <name> <absent|offered-last|preferred-with-share>
# Where a named key-exchange group sits in the client's offer. Presence alone
# is not the rule: a hybrid group added "at low negotiation priority" must be
# offered without being preferred and without spending a key share on it, and
# when it later becomes the default preference it must arrive with a share.
kx_offer_group() {
    local g="$1" name="$2" want="$3" f groups shares first
    [ -n "$_facts_ch" ] || _facts_ch="$(_ch_facts localhost 6)"
    f="$_facts_ch"
    groups="$(printf '%s\n' "$f" | sed -n 's/^supported_groups: *//p' | head -1)"
    shares="$(printf '%s\n' "$f" | sed -n 's/^key_share_groups: *//p' | head -1)"
    if [ -z "$groups" ]; then fail "$g" "no ClientHello observed"; return; fi
    first="$(printf '%s' "$groups" | sed 's/[][]//g; s/,.*//; s/[\"'"'"' ]//g')"
    case "$want" in
      absent)
        if printf '%s' "$groups" | grep -qi "$name"; then
            fail "$g" "$name must not be offered yet; groups=$groups"
        else pass "$g" "$name not offered; groups=$groups"; fi ;;
      offered-last)
        if ! printf '%s' "$groups" | grep -qi "$name"; then
            fail "$g" "$name not offered; groups=$groups"
        elif printf '%s' "$first" | grep -qi "$name"; then
            fail "$g" "$name is the first preference; it must be offered but not preferred. groups=$groups"
        elif printf '%s' "$shares" | grep -qi "$name"; then
            fail "$g" "$name carries a key share; a low-priority group must not. shares=$shares"
        else pass "$g" "$name offered, not preferred, no key share; groups=$groups"; fi ;;
      preferred-with-share)
        if ! printf '%s' "$first" | grep -qi "$name"; then
            fail "$g" "$name is not the first preference; groups=$groups"
        elif ! printf '%s' "$shares" | grep -qi "$name"; then
            fail "$g" "$name is preferred but sends no key share; shares=$shares"
        else pass "$g" "$name preferred and carries a key share; groups=$groups shares=$shares"; fi ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}


# kx_hybrid_share_group <group> <well-formed|absent>
# Whether the key share the client spends on X25519MLKEM768 is the object the
# draft defines, or merely a slot with the right code point on it.
#
# This is a different question from the one kx_offer_group asks, and the
# difference is why it exists. `kx_offer_group ... preferred-with-share` reads
# group *names*: that X25519MLKEM768 is the first entry of supported_groups and
# that some key_share entry carries its code point. Neither of those reads a
# byte of the share itself, so a client that put an X25519 point in that entry,
# or a zero-filled buffer, or the two halves the other way round, passes it --
# and none of those three would survive contact with a real server.
#
# draft-ietf-tls-ecdhe-mlkem section 4.1 fixes both the size and the order:
# 1216 bytes, the 1184-byte ML-KEM-768 encapsulation key first and the 32-byte
# X25519 public value second. The same section requires the server to run the
# encapsulation-key check on what it receives and to abort with
# illegal_parameter when it fails, so a malformed share is not a cosmetic
# defect. The observer decodes the ML-KEM half as ByteEncode_12 output and
# reports whether every coefficient is reduced below q; that is a bounds test
# on a public encoding, not cryptography, and nothing below the socket is read.
#
# It costs no extra handshakes. The six-connection sweep is the one
# ch_extension_order_group and kx_offer_group already pay for, and this reads
# two more lines out of the same report.
kx_hybrid_share_group() {
    local g="$1" want="$2" f v sizes
    [ -n "$_facts_ch" ] || _facts_ch="$(_ch_facts localhost 6)"
    f="$_facts_ch"
    v="$(printf '%s\n' "$f" | sed -n 's/^mlkem768_client_share: *//p' | head -1)"
    sizes="$(printf '%s\n' "$f" | sed -n 's/^key_share_sizes: *//p' | head -1)"
    if [ -z "$v" ]; then fail "$g" "no ClientHello observed"; return; fi
    case "$want" in
      well-formed)
        case "$v" in
          ok) pass "$g" "X25519MLKEM768 share is 1216 bytes and its ML-KEM-768 half decodes as an encapsulation key; sizes=$sizes" ;;
          absent) fail "$g" "no X25519MLKEM768 key share in any of the six hellos; sizes=$sizes" ;;
          inconsistent:*) fail "$g" "the X25519MLKEM768 share is not the same shape on every connection: $v; sizes=$sizes" ;;
          size=*) fail "$g" "X25519MLKEM768 share is the wrong size; draft-ietf-tls-ecdhe-mlkem sec 4.1 fixes it at 1216 bytes (1184 ML-KEM-768 + 32 X25519), observed $v; sizes=$sizes" ;;
          *) fail "$g" "X25519MLKEM768 share is 1216 bytes but its first 1184 are not an ML-KEM-768 encapsulation key -- sec 4.1 puts the ML-KEM half first and the X25519 half second ($v); sizes=$sizes" ;;
        esac ;;
      absent)
        case "$v" in
          absent) pass "$g" "no X25519MLKEM768 key share offered; sizes=$sizes" ;;
          *) fail "$g" "an X25519MLKEM768 key share is offered ($v); sizes=$sizes" ;;
        esac ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}


# --- hybrid key exchange: usable, or merely listed ------------------------

# The two groups below drive `bin/kx-hybrid-probe.py`, which asks a
# HelloRetryRequest question instead of reading a list. The distinction is the
# point. `kx_offer_group` reads the names a client puts in `supported_groups`,
# and a name there costs nothing to add: a peer advertising a group it cannot
# perform is indistinguishable from one that can, right up until something
# insists on that group and nothing else. RFC 8446 sec 4.1.4 is the mechanism
# for insisting, and it asks no cryptography of us -- the answer arrives in
# cleartext, in the second message each side sends, and neither probe ever
# completes a handshake.

# kx_server_retry_group <group> <name> <selects|refuses>
# Whether the candidate's *server* can actually use a named group. It is sent
# a hello offering that group alone and carrying an empty `client_shares`,
# which RFC 8446 sec 4.2.8 permits in terms and which is precisely how a
# client asks to be told what to send. Two answers are then available to it
# and no others: a HelloRetryRequest naming the group, or a fatal alert saying
# it has no group in common with us. That is what makes this a decision the
# server has to make rather than an inference we draw.
kx_server_retry_group() {
    local g="$1" name="$2" want="$3" facts res sel
    if ! _probe_server_up; then fail "$g" "candidate server did not start"; return; fi
    facts="$(run_unprivileged python3 "$TESTS/bin/kx-hybrid-probe.py" server \
        --port "$_probe_port" --group "$name" --timeout 30 2>/dev/null | tr '\n' ' ')"
    [ -n "$facts" ] || facts="result=error detail=probe-produced-no-output"
    res="$(_fact "$facts" result)"
    sel="$(_fact "$facts" group)"
    case "$want" in
      selects)
        # The probe reports the code point the HelloRetryRequest selects, and
        # the obligation is that it names *this* group rather than answering
        # with some other one, so the selected value is compared to the code
        # point of the group the case offers.
        local want_cp
        case "$name" in
          X25519MLKEM768)       want_cp="0x11ec" ;;
          SecP256r1MLKEM768)    want_cp="0x11eb" ;;
          X25519Kyber768Draft00) want_cp="0x6399" ;;
          *)                    want_cp="" ;;
        esac
        if [ "$res" = hello-retry ] && [ -n "$sel" ] && [ "$sel" != none ] \
           && { [ -z "$want_cp" ] || [ "$sel" = "$want_cp" ]; }; then
            pass "$g" "hello offering only $name drew a HelloRetryRequest selecting $sel; $facts"
        else
            fail "$g" "a server that supports $name must answer a hello offering it alone, with no key share, with a HelloRetryRequest naming it; got: $facts"
        fi ;;
      refuses)
        if [ "$res" = refused ]; then
            pass "$g" "server refused a hello offering only $name; $facts"
        else
            fail "$g" "$name must not be usable at this step; got: $facts"
        fi ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}

# kx_retry_share_group <group> <name> <bytes>
# The size of the key share the candidate's *client* produces for a named
# group once it is asked, by HelloRetryRequest, to use that group. A share of
# the size the group's own definition fixes can only come from a real key
# generation for that group; a client that has the name and not the
# implementation has nothing to put there. draft-ietf-tls-ecdhe-mlkem sec 4.1
# fixes 1216 bytes for X25519MLKEM768 -- a 1184-byte ML-KEM-768 encapsulation
# key, then 32 bytes of X25519, in that order.
#
# This asks about a client that offers the group *without* a share. One that
# already spends a share on it cannot be asked to retry with it at all: RFC
# 8446 sec 4.2.8 makes that request illegal and a correct client rejects it.
# So `already-shared` is reported as its own outcome rather than provoked. A
# step where the group is the preference reads the share straight out of the
# first hello instead, with `kx_hybrid_share_group`, and has no need of a
# retry to provoke one.
kx_retry_share_group() {
    local g="$1" name="$2" want="$3" b port of ef facts res len op i
    b="$(_cand_bin client)" || { fail "$g" "no client binary"; return; }
    port="$(_free_port)"
    of="$WORK/kx-retry-$g.out" ; ef="$WORK/kx-retry-$g.err"
    run_unprivileged python3 "$TESTS/bin/kx-hybrid-probe.py" client \
        --port "$port" --group "$name" --timeout 30 > "$of" 2> "$ef" &
    op=$!
    # The listener announces its socket on stderr before it accepts, exactly
    # as _ch_facts' observer does, so the client is never started against a
    # port nothing is listening on yet.
    for i in $(seq 1 100); do
        grep -q listening "$ef" 2>/dev/null && break
        sleep 0.1
    done
    # 30s for the same reason _ch_facts gives: TEST_INTERFACE.md sec 5
    # promises each probe 30 seconds. It is a bound, not a cost -- the probe
    # closes as soon as it has the second hello.
    ( cd /app && run_unprivileged timeout 30 "./$b" --cafile "$CA/ca.cert" \
        --port "$port" --http localhost ) > /dev/null 2>&1 || true
    wait $op 2>/dev/null || true
    facts="$(tr '\n' ' ' < "$of" 2>/dev/null)"
    [ -n "$facts" ] || facts="result=error detail=probe-produced-no-output"
    res="$(_fact "$facts" result)"
    len="$(_fact "$facts" share_len)"
    case "$res" in
      retried)
        if [ "$len" = "$want" ]; then
            pass "$g" "client asked to retry with $name sent a $len-byte share; $facts"
        else
            fail "$g" "client retried with $name but sent a ${len:-absent}-byte share, expected $want; $facts"
        fi ;;
      not-offered)
        fail "$g" "client does not offer $name, so it cannot be asked to retry with it; $facts" ;;
      already-shared)
        fail "$g" "client already spends a key share on $name, so a retry request for it would be the illegal request of RFC 8446 sec 4.2.8; score that step with kx_hybrid_share_group instead. $facts" ;;
      *)
        fail "$g" "no second ClientHello to measure; $facts" ;;
    esac
}

# --- example binaries that are not the mio pair ---------------------------

# unbuffered_server_group <group>
# The candidate's *unbuffered-API* example server, driven with `openssl
# s_client` exactly as diff_server_group drives the mio one. Three things have
# to hold together and all three are read off the wire: the example exists and
# listens, the handshake completes at TLS 1.3 against an independent
# implementation, and an HTTP/1 request made over it is answered.
#
# Distinct from diff_server_group, which asks the same questions of the
# buffered mio server. The point of asking them twice is that the two go
# through different halves of the library: the mio example hands its socket to
# rustls, and this one owns the buffers and feeds records in and out itself.
# A release that has no unbuffered API cannot answer at all.
unbuffered_server_group() {
    local g="$1" f
    f="$(ossl_probe_unbuffered_server "$CA" 2>/dev/null | tr '\n' ' ')"
    if [ "$(_fact "$f" found)" != yes ]; then
        fail "$g" "no unbuffered example server found under /app; searched target/{debug,release}/[examples/]unbuffered-server"
        return
    fi
    if [ "$(_fact "$f" started)" != yes ]; then
        fail "$g" "unbuffered example server did not come up: $f"; return; fi
    if [ "$(_fact "$f" handshake)" != yes ]; then
        fail "$g" "openssl s_client did not complete a handshake with the unbuffered example server: $f"; return; fi
    case "$(_fact "$f" proto)" in
      TLSv1.3) ;;
      *) fail "$g" "unbuffered example server negotiated $(_fact "$f" proto), expected TLSv1.3: $f"; return ;;
    esac
    if [ "$(_fact "$f" response)" != yes ]; then
        fail "$g" "unbuffered example server completed a handshake but answered no HTTP request: $f"; return; fi
    pass "$g" "$f"
}

# acceptor_alert_group <group> <alert|silent>
# What the candidate's `server_acceptor` example puts on the wire when the
# acceptor refuses the ClientHello it was given -- here, one whose server_name
# is not a legal hostname.
#
#   alert   the first record back is a fatal alert: the refusal was reported
#           to the peer.
#   silent  nothing is sent and the connection closes: the refusal was not
#           reportable, which is what a release whose accept call returns only
#           an error can do.
#
# Only the *level* of the alert is read, never its description. Which
# description fits an unparseable name is the sending implementation's choice
# and nothing here should pin it.
acceptor_alert_group() {
    local g="$1" want="$2" f r
    f="$(probe_acceptor_first_record 2>/dev/null | tr '\n' ' ')"
    if [ "$(_fact "$f" found)" != yes ]; then
        fail "$g" "no acceptor example found under /app; searched target/{debug,release}/[examples/]server_acceptor"
        return
    fi
    if [ "$(_fact "$f" started)" != yes ]; then
        fail "$g" "acceptor example did not come up: $f"; return; fi
    r="$(_fact "$f" first)"
    case "$want:$r" in
      alert:alert:fatal*)
        pass "$g" "acceptor answered an illegal server_name with $r" ;;
      "silent:none:server closed"*)
        pass "$g" "acceptor closed without a record: $r" ;;
      alert:*)
        fail "$g" "expected a fatal alert as the first record from the acceptor, got ${r:-nothing}" ;;
      silent:*)
        fail "$g" "expected the acceptor to close without sending a record, got ${r:-nothing}" ;;
      *) fail "$g" "checks-lib: unknown expectation '$want'" ;;
    esac
}

# --- the CertificateRequest issuer hint list -------------------------------
#
# A TLS server that asks for a client certificate may tell the client which
# issuers it would accept. At TLS 1.2 that is the mandatory
# `certificate_authorities` field of CertificateRequest (RFC 5246 7.4.4), so an
# empty list is a zero-length field; at TLS 1.3 it is the
# `certificate_authorities` *extension* (RFC 8446 4.2.4), which a server with
# nothing to say is free to leave out entirely. Those are different observables
# and the groups below keep them apart.
#
# The example server in this workspace never asks for a client certificate, so
# the only server here that can be driven into sending a CertificateRequest at
# all is the interoperability shim. It is launched with documented flags, and
# what is read is the bytes it puts on a socket. bin/certreq-ca-probe.py does
# the reading.
_shim_keydir=""

_shim_bin() {
    local p="${SHIM:-}" c
    if [ -n "$p" ] && [ -x "$p" ]; then printf '%s\n' "$p"; return 0; fi
    # The same candidate list test.sh resolved SHIM from, re-read here so this
    # helper still answers if it is reached before that ran.
    while IFS= read -r c; do
        [ -n "$c" ] || continue
        if [ -x "/app/$c" ]; then printf '%s\n' "/app/$c"; return 0; fi
    done < "$TESTS/data/shim-path.txt"
    return 1
}

# A scratch copy of the two BoGo key fixtures, handed to the unprivileged uid.
# The shim reads `cert.pem` and `key.pem` from its own working directory, so it
# needs a directory it can read; copied rather than shared for the same reason
# test.sh copies the rest of the fixtures, so that nothing the candidate's code
# can write survives the run.
_shim_keys() {
    [ -n "$_shim_keydir" ] && { printf '%s\n' "$_shim_keydir"; return 0; }
    local d="$UNPRIV/shim-keys"
    rm -rf "$d"
    mkdir -p "$d" || return 1
    cp "$TESTS/data/bogo-keys/cert.pem" "$TESTS/data/bogo-keys/key.pem" "$d/" \
        2>/dev/null || return 1
    chown -R "$SCORE_UID:$SCORE_GID" "$d" 2>/dev/null || true
    _shim_keydir="$d"
    printf '%s\n' "$d"
}

_certreq_probe() {
    local mode="$1" val="$2" shim keys out
    shim="$(_shim_bin)" || {
        printf 'failed:no shim binary to drive\n'; return 0; }
    keys="$(_shim_keys)" || {
        printf 'failed:the shim key fixtures could not be staged\n'; return 0; }
    if [ "$val" = - ]; then
        out="$(run_unprivileged python3 "$TESTS/bin/certreq-ca-probe.py" \
            --shim "$shim" --keydir "$keys" --mode "$mode" \
            --timeout 30 2>/dev/null)"
    else
        out="$(run_unprivileged python3 "$TESTS/bin/certreq-ca-probe.py" \
            --shim "$shim" --keydir "$keys" --mode "$mode" \
            --ca-list "$val" --timeout 30 2>/dev/null)"
    fi
    printf '%s' "${out#result=}" | sed 's/ detail=/:/'
}

# _certreq_why <probe result> <expectation> -> empty when it matched, else why
#
# Expectations, and nothing else, are what the per-step file supplies:
#   absent          no `certificate_authorities` extension at all (TLS 1.3)
#   empty           the field is present and names no issuer  (TLS 1.2)
#   names:<hex,...> exactly these DER-encoded issuer names, in this order
_certreq_why() {
    local r="$1" want="$2" k n ids exp cnt
    k="${r%%:*}"
    n="$(printf '%s' "$r" | sed -n 's/.*count=\([0-9]*\).*/\1/p')"
    ids="$(printf '%s' "$r" | sed -n 's/.*ids=//p')"
    case "$want" in
      absent)
        [ "$k" = no-authorities ] && return 0
        if [ "$k" = names ]; then
            printf 'expected no certificate_authorities extension at all, got count=%s ids=%s' \
                "${n:-?}" "$ids"
        else
            printf 'the probe reported: %s' "${r:-nothing}"
        fi ;;
      empty)
        if [ "$k" = names ] && [ "${n:-x}" = 0 ] && [ -z "$ids" ]; then return 0; fi
        if [ "$k" = names ]; then
            printf 'expected the issuer list to name nobody, got count=%s ids=%s' \
                "${n:-?}" "$ids"
        else
            printf 'the probe reported: %s' "${r:-nothing}"
        fi ;;
      names:*)
        exp="${want#names:}"
        cnt="$(printf '%s' "$exp" | tr ',' '\n' | grep -c .)"
        if [ "$k" = names ] && [ "${n:-x}" = "$cnt" ] && [ "$ids" = "$exp" ]; then
            return 0
        fi
        if [ "$k" = names ]; then
            printf 'expected the issuer list to be exactly %s, got count=%s ids=%s' \
                "$exp" "${n:-?}" "$ids"
        else
            printf 'the probe reported: %s' "${r:-nothing}"
        fi ;;
      *) printf 'checks-lib: unknown expectation %s' "$want" ;;
    esac
    return 1
}

# shim_certreq_group <group> <tls12|tls13> <-use-client-ca-list value|-> <expect>
#
# One observation, one record. `-` means the flag is not passed at all, which is
# the configuration every release on this ladder can be driven into: it asks
# what a server with no issuer hints to offer puts on the wire.
shim_certreq_group() {
    local g="$1" mode="$2" val="$3" want="$4" r why
    r="$(_certreq_probe "$mode" "$val")"
    why="$(_certreq_why "$r" "$want")"
    if [ -n "$why" ]; then
        fail "$g" "the $mode CertificateRequest: $why"
        return
    fi
    pass "$g" "the $mode CertificateRequest carried the issuer list this step requires ($want)"
}

# shim_ca_hint_group <group> <tls12|tls13> <hint as hex DER> <expect when empty>
#
# Two legs, both required, because either one alone is passable by a server that
# ignores the configuration entirely. Configured with one issuer hint, the
# CertificateRequest must name exactly that one; configured with an explicitly
# empty list, it must say what the per-step file's fourth argument says. A
# server that always sends the hint it was given passes the first leg and fails
# the second; one that never sends any passes the second and fails the first.
shim_ca_hint_group() {
    local g="$1" mode="$2" hint="$3" empty_want="$4" r why
    r="$(_certreq_probe "$mode" "$hint")"
    why="$(_certreq_why "$r" "names:$hint")"
    if [ -n "$why" ]; then
        fail "$g" "with one issuer hint configured, the $mode CertificateRequest: $why"
        return
    fi
    r="$(_certreq_probe "$mode" '<NULL>')"
    why="$(_certreq_why "$r" "$empty_want")"
    if [ -n "$why" ]; then
        fail "$g" "with the issuer hint list explicitly empty, the $mode CertificateRequest: $why"
        return
    fi
    pass "$g" "the $mode CertificateRequest named exactly the configured issuer, and $empty_want when the list was empty"
}

# server_reassembly_alignment_group <group>
# The encryption-level alignment fix (GHSA-2mjx-qc3c-rqvc) on the record layer's
# *reassembly* path, which the single-record `server_framing_group <g> reject
# packed` shape does not reach. Two legs, both required, so the group states
# what is refused rather than passing for a server that refuses every
# multi-record hello:
#
#   accept  a ClientHello split across two records must still be answered with
#           a ServerHello -- the fix must not turn "a handshake message may span
#           records" into a blanket refusal.
#   reject  the same split hello, whose closing fragment also carries a
#           *complete* trailing handshake message, must draw a fatal alert.
#
# The refused case reaches the alignment check through the deframer's coalesce
# path: a partial span is completed by a later record that also appends a whole
# message, so after the hello is taken a complete message is still buffered.
# The 0.23.31 definition that only inspects the record last read answers it; the
# 0.23.45 definition that covers every buffered-but-unprocessed message refuses
# it. Only the level of the alert is read, never its description.
server_reassembly_alignment_group() {
    local g="$1" acc rej
    if ! _probe_server_up; then fail "$g" "candidate server did not start"; return; fi
    acc="$(_send_hello localhost fragmented)"
    case "$acc" in
        handshake:*) ;;
        *) fail "$g" "a ClientHello reassembled from two records must be answered; got [$acc]"; return ;;
    esac
    rej="$(_send_hello localhost packed-split)"
    case "$rej" in
        alert:fatal*) pass "$g" "reassembled hello answered [$acc]; reassembled hello with a trailing handshake message refused [$rej]" ;;
        *) fail "$g" "a reassembled record whose closing fragment carries a complete trailing handshake message must draw a fatal alert; got [$rej]" ;;
    esac
}
