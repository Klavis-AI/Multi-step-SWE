# OpenSSL differential interoperation. Sourced by test.sh; not executable alone.
#
# BoGo asks "does this implementation behave correctly against a test harness
# that is deliberately hostile". This layer asks a different question: "do two
# independent production TLS implementations agree". OpenSSL is a peer that
# shares no code with the implementation under test, so an agreement here is
# evidence about the protocol rather than about a shared assumption.
#
# It also reaches observables BoGo has no opinion about - most importantly how
# many NewSessionTickets a server sends, which is one of the behaviours that
# changes partway up this ladder and is visible to any client.
#
# Every function prints `key=value` facts on stdout and asserts nothing. The
# per-step check file decides what the facts should be, so the same driver
# serves a step where one ticket is correct and a step where four are.
#
# The frozen PKI, not the candidate's, is used throughout: the certificates live
# under $TESTS/data/test-ca and are valid for twenty years from authoring time.

# Pick a port nothing is listening on. Racy in principle; in a single-tenant
# grading container with one probe at a time, not in practice. The range avoids
# the ephemeral range so the kernel does not hand the same port to an outbound
# socket between the check and the bind.
_free_port() {
    local p
    for _ in $(seq 1 50); do
        p=$(( 20000 + RANDOM % 20000 ))
        if ! (exec 3<>"/dev/tcp/127.0.0.1/$p") 2>/dev/null; then echo "$p"; return 0; fi
        # Braces, not a bare `exec 3<&- 2>/dev/null`. An `exec` with no command
        # applies its redirections to the shell itself and never undoes them,
        # so the bare form silences this shell's stderr for the rest of the
        # run -- every later [group] line goes to /dev/null while the scores
        # still reach reward.json. The group list simply stops mid-way and the
        # one failure you were trying to read is the first thing lost. The
        # braces scope the redirection to the group and leave fd 2 alone.
        { exec 3<&-; } 2>/dev/null || true
    done
    echo 24711
}

# _wait_port <port> [pid] [tries]
#
# Waits for something to accept on $port. If a pid is given, gives up the moment
# that process dies rather than burning the whole budget: a server that exited
# on a bind error is never going to come up, and the distinction between "died"
# and "slow" is the difference between a real finding and a flake.
#
# The budget is deliberately generous. A debug-profile Rust binary starting on a
# loaded machine can take seconds to reach its listen call, and a probe that
# times out early turns into a scored group that fails for reasons that have
# nothing to do with the candidate.
_wait_port() {
    local port="$1" pid="${2:-}" tries="${3:-300}"
    for _ in $(seq 1 "$tries"); do
        if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
            # Braces for the same reason as in _free_port above: a bare
            # `exec 3<&- 2>/dev/null` here would redirect the caller's stderr
            # permanently, and this one runs in the main shell.
            { exec 3<&-; } 2>/dev/null || true
            # Same still-alive guard the unbuffered-server probe below carries,
            # for the same reason. This test reads the kernel's answer, not our
            # server's: a foreign listener that won the race for a random port
            # answers it just as readily while the server we started is dying of
            # EADDRINUSE, and the probe would then grade whatever else is on the
            # port. Confirming the pid we started is still up turns that into a
            # startup failure, which _retry_probe re-rolls onto a fresh port.
            if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then return 1; fi
            return 0
        fi
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then return 1; fi
        sleep 0.1
    done
    return 1
}

# _retry_probe <fact-to-retry-on> <function> [args...]
#
# Runs a probe and, if it reports the named startup failure, runs it once more.
# Only startup is retried: a probe that got as far as a handshake reports what
# it saw, pass or fail, and is never re-rolled until it says something nicer.
_retry_probe() {
    local key="$1"; shift
    local out
    out="$("$@")"
    if printf '%s\n' "$out" | grep -q "^${key}=no$"; then
        out="$("$@")"
    fi
    printf '%s\n' "$out"
}

# Kill a process we started, by the exact pid we captured. Never by name and
# never by pattern: this container is not the only thing running on the host.
_kill_pid() {
    local pid="${1:-}"
    [ -n "$pid" ] || return 0
    kill "$pid" 2>/dev/null || true
    for _ in $(seq 1 20); do kill -0 "$pid" 2>/dev/null || return 0; sleep 0.1; done
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

# find_example_bin <role>   (role is "client" or "server")
#
# Resolves the mio example binary for this tree. Upstream moved these examples
# from the `rustls-mio` sub-crate to a workspace `examples` crate partway up the
# ladder, and renamed them in the process, so there is no single path that is
# correct at every step. Rather than freeze one layout and fail every candidate
# that chose the other, the verifier searches a fixed, documented list and takes
# the first hit; TEST_INTERFACE.md publishes exactly this list, in this order.
#
# Prints the path on stdout and returns 0, or returns 1 having printed nothing.
find_example_bin() {
    local role="$1" c
    for c in \
        "target/debug/tls${role}-mio" \
        "target/debug/examples/tls${role}-mio" \
        "target/debug/tls${role}" \
        "target/debug/examples/tls${role}" \
        "target/release/tls${role}-mio" \
        "target/release/examples/tls${role}-mio" \
        "target/release/tls${role}" \
        "target/release/examples/tls${role}"
    do
        if [ -x "$c" ]; then printf '%s\n' "$c"; return 0; fi
    done
    return 1
}

# _hold_open <s_client-output-file>
#
# Keeps a probe connection open while post-handshake messages land, and returns
# as soon as they have stopped landing: never before _TICKET_HOLD_MIN seconds,
# never after _TICKET_HOLD_MAX, and in between as soon as a full second has
# passed with no new NewSessionTicket line. Nothing here knows how many tickets
# are correct -- that is the per-step check's business; this only decides when
# there is no point waiting for another one.
#
# `stdbuf -o0` on the s_client side is what makes the report readable while the
# process is still running: s_client writes through stdio, which block-buffers
# onto a file, so without it nothing appears until the process exits and every
# poll below reads zero. Where stdbuf is missing this degrades to waiting out
# the maximum -- slower, and it reads the same count.
_TICKET_HOLD_MIN=1
_TICKET_HOLD_MAX=12
_hold_open() {
    local f="$1" i n last=-1 stable=0
    for i in $(seq 1 $(( _TICKET_HOLD_MAX * 4 ))); do
        sleep 0.25
        if [ "$i" -ge $(( _TICKET_HOLD_MIN * 4 )) ]; then
            n=$(grep -c 'New Session Ticket arrived' "$f" 2>/dev/null || true)
            n=${n:-0}
            if [ "$n" -gt 0 ] && [ "$n" = "$last" ]; then
                stable=$(( stable + 1 ))
                if [ "$stable" -ge 4 ]; then return 0; fi
            else
                stable=0
            fi
            last="$n"
        fi
    done
    return 0
}

# ossl_probe_server <server-binary> <ca-dir> [extra server args...]
#
# Runs the candidate's example server in echo mode and connects to it with
# `openssl s_client`. Emits:
#   started=yes|no   handshake=yes|no   proto=TLSv1.x   tickets=N
#   echo=ok|bad      (resumption is not reported by this probe)
_ossl_probe_server_once() {
    local bin="$1" ca="$2"; shift 2
    local port out pid rc=0
    port="$(_free_port)"

    run_unprivileged "$bin" --certs "$ca/end.fullchain" --key "$END_KEY" \
        --port "$port" "$@" echo > "$WORK/dsrv.log" 2>&1 &
    pid=$!
    if ! _wait_port "$port" "$pid"; then
        echo "started=no"; _kill_pid "$pid"; return 1
    fi
    echo "started=yes"

    # Post-handshake NewSessionTickets arrive after the handshake has already
    # finished, so the connection has to be held open to receive them -- and
    # how long "long enough" is belongs to the machine, not to the protocol.
    # A fixed sleep would make a scored group (the ticket count) a function of
    # load: with `sleep 3`, a server that sends its fourth ticket 3.1 seconds in
    # would be recorded as having sent three, and the group would fail for a
    # reason that has nothing to do with the candidate. _hold_open watches
    # s_client's own running report instead and closes the connection once the
    # count has stopped moving, so a prompt server costs about a second and a
    # half and a slow one is given up to _TICKET_HOLD_MAX seconds before the
    # count is read.
    local sc="$WORK/dsrv-sclient.out" sb=""
    command -v stdbuf > /dev/null 2>&1 && sb="stdbuf -o0"
    : > "$sc"
    { printf 'PING\n'; _hold_open "$sc"; } | timeout 40 $sb openssl s_client \
              -connect "127.0.0.1:$port" -CAfile "$ca/ca.cert" \
              -servername testserver.com > "$sc" 2>&1 || rc=$?
    out="$(cat "$sc" 2>/dev/null)"
    _kill_pid "$pid"

    if printf '%s' "$out" | grep -q 'Cipher *: *(NONE)\|no peer certificate available'; then
        echo "handshake=no"; return 0
    fi
    echo "handshake=yes"
    echo "proto=$(printf '%s' "$out" | sed -n 's/^ *Protocol *: *\(.*\)$/\1/p' | head -1)"
    echo "tickets=$(printf '%s' "$out" | grep -c 'New Session Ticket arrived')"
    if printf '%s' "$out" | grep -q '^PING$'; then echo "echo=ok"; else echo "echo=bad"; fi
    return 0
}

# ossl_probe_client <client-binary> <ca-dir> [extra client args...]
#
# Runs `openssl s_server` and points the candidate's example client at it.
# Emits: served=yes|no  connected=yes|no  client_rc=N  got_response=yes|no
_ossl_probe_client_once() {
    local bin="$1" ca="$2"; shift 2
    local port pid rc=0
    port="$(_free_port)"

    # Deliberately NOT -naccept 1. The readiness probe below opens and closes a
    # TCP connection to find out whether the listener is up, and s_server counts
    # that as its one permitted connection and exits -- so the client that
    # actually matters then gets ECONNREFUSED and the group fails for a reason
    # that has nothing to do with the candidate. The server is stopped by pid
    # when the probe finishes.
    # -cert reads only the FIRST certificate in the file, so pointing it at a
    # fullchain silently serves a bare end-entity cert and any correct candidate
    # client rejects the chain as UnknownIssuer. The intermediates have to be
    # handed over separately with -cert_chain.
    if [ "${OSSL_CLIENT_CHAIN:-yes}" = yes ]; then
        timeout 60 openssl s_server -cert "$ca/end.cert" -key "$END_KEY" \
            -cert_chain "$ca/end.chain" \
            -accept "$port" -www -quiet > "$WORK/dcli-srv.log" 2>&1 &
    else
        # Same leaf, no -cert_chain: the intermediate never reaches the client,
        # so no path to the frozen root can be built and a client that verifies
        # anything at all must refuse the handshake. This is the negative leg --
        # see ossl_probe_client_nochain below.
        timeout 60 openssl s_server -cert "$ca/end.cert" -key "$END_KEY" \
            -accept "$port" -www -quiet > "$WORK/dcli-srv.log" 2>&1 &
    fi
    pid=$!
    if ! _wait_port "$port" "$pid"; then
        echo "served=no"; _kill_pid "$pid"; return 1
    fi
    echo "served=yes"

    # Long-form flags only. Upstream moved these examples from docopt to clap
    # partway up the ladder and the clap version does not accept the short -p,
    # so a probe written with -p reports "this client is broken" from step 11 on
    # when nothing is broken but the probe.
    run_unprivileged timeout 30 "$bin" --cafile "$ca/ca.cert" --port "$port" --http \
        "$@" localhost > "$WORK/dcli.log" 2>&1 || rc=$?
    _kill_pid "$pid"

    echo "client_rc=$rc"
    if [ "$rc" = 0 ]; then echo "connected=yes"; else echo "connected=no"; fi
    if grep -qi '^HTTP/1\|s_server' "$WORK/dcli.log" 2>/dev/null; then
        echo "got_response=yes"
    else
        echo "got_response=no"
    fi
    return 0
}

# Public entry points. Each retries once if the peer never came up at all.
ossl_probe_server() { _retry_probe started "_ossl_probe_server_once" "$@"; }
ossl_probe_client() { _retry_probe served  "_ossl_probe_client_once" "$@"; }

# The negative mirror of ossl_probe_client. Identical in every respect except
# that s_server is not given the intermediate, so the chain the client receives
# does not reach the frozen root. Emits the same keys; the caller asserts on
# client_rc being non-zero instead of zero.
#
# `local` is dynamically scoped in bash, so the assignment is visible to
# _ossl_probe_client_once through _retry_probe and is gone when this returns --
# a later positive probe is unaffected.
ossl_probe_client_nochain() {
    local OSSL_CLIENT_CHAIN=no
    _retry_probe served "_ossl_probe_client_once" "$@"
}

# --- a peer named by its IP address ---------------------------------------
#
# rustls refuses to verify the certificate of a peer named by an IP address
# until 0.21.0, where the name is checked against the certificate's iPAddress
# subjectAltName instead. Neither the frozen leaf nor anything else in the
# fixture PKI carries such a name, so one is minted here, at run time, from the
# frozen intermediate. Minting rather than shipping it keeps the leaf in step
# with the frozen PKI: a shipped IP leaf would be signed by a CA that a
# regenerated PKI replaces, and would then fail for every candidate at once.

# _mint_ip_leaf <ca-dir>
#
# Prints the path to an end-entity certificate for 127.0.0.1, signed by the
# frozen intermediate and carrying that address as its only subjectAltName. It
# reuses $END_KEY, so the same -key the other probes pass to s_server matches
# it. Minted once per run and cached; returns 1 having printed nothing if
# openssl cannot produce it.
_mint_ip_leaf() {
    local ca="$1" d="$WORK/ip-leaf"
    if [ -s "$d/ip.cert" ]; then printf '%s\n' "$d/ip.cert"; return 0; fi
    mkdir -p "$d" || return 1
    printf 'basicConstraints = critical,CA:false\nkeyUsage = nonRepudiation, digitalSignature, keyEncipherment\nextendedKeyUsage = serverAuth\nsubjectKeyIdentifier = hash\nsubjectAltName = IP:127.0.0.1\n' \
        > "$d/ip.ext" || return 1
    openssl req -new -key "$END_KEY" -subj '/CN=127.0.0.1' -sha256 \
        -out "$d/ip.req" > "$d/mint.log" 2>&1 || return 1
    openssl x509 -req -in "$d/ip.req" -out "$d/ip.cert" \
        -CA "$ca/inter.cert" -CAkey "$ca/inter.key" \
        -sha256 -days 3650 -set_serial 4127 -extfile "$d/ip.ext" \
        >> "$d/mint.log" 2>&1 || return 1
    printf '%s\n' "$d/ip.cert"
}

# ossl_probe_ip_client <client-binary> <ca-dir> [extra client args...]
#
# The mirror of ossl_probe_client for a peer the client is given an IP address
# as the name of. Two things differ from that probe and both matter: the final
# argument is `127.0.0.1` rather than `localhost`, and the certificate OpenSSL
# serves carries that address in an iPAddress subjectAltName instead of the DNS
# names the frozen leaf carries. Emits the same keys.
#
# With OSSL_IP_LEAF=none the ordinary DNS-only leaf is served instead. That is
# the negative leg: the address is not on the certificate, so a client that
# checks the name against it must refuse, and a client that merely stopped
# checking names once the name was an address cannot pass both legs.
_ossl_probe_ip_client_once() {
    local bin="$1" ca="$2"; shift 2
    local port pid rc=0 cert
    if [ "${OSSL_IP_LEAF:-ip}" = ip ]; then
        cert="$(_mint_ip_leaf "$ca")" || { echo "served=no"; echo "reason=ip-leaf-mint-failed"; return 1; }
    else
        cert="$ca/end.cert"
    fi
    port="$(_free_port)"

    # -naccept is omitted, -cert_chain is given and the flags are long-form for
    # exactly the reasons spelled out on _ossl_probe_client_once above.
    timeout 60 openssl s_server -cert "$cert" -key "$END_KEY" \
        -cert_chain "$ca/end.chain" \
        -accept "$port" -www -quiet > "$WORK/dip-srv.log" 2>&1 &
    pid=$!
    if ! _wait_port "$port" "$pid"; then
        echo "served=no"; _kill_pid "$pid"; return 1
    fi
    echo "served=yes"
    echo "leaf=${OSSL_IP_LEAF:-ip}"

    run_unprivileged timeout 30 "$bin" --cafile "$ca/ca.cert" --port "$port" --http \
        "$@" 127.0.0.1 > "$WORK/dip-cli.log" 2>&1 || rc=$?
    _kill_pid "$pid"

    echo "client_rc=$rc"
    if [ "$rc" = 0 ]; then echo "connected=yes"; else echo "connected=no"; fi
    if grep -qi '^HTTP/1\|s_server' "$WORK/dip-cli.log" 2>/dev/null; then
        echo "got_response=yes"
    else
        echo "got_response=no"
    fi
    return 0
}

ossl_probe_ip_client() { _retry_probe served "_ossl_probe_ip_client_once" "$@"; }

# The negative mirror. `local` is dynamically scoped in bash, so the assignment
# reaches _ossl_probe_ip_client_once through _retry_probe and is gone when this
# returns -- see ossl_probe_client_nochain, which does the same thing.
ossl_probe_ip_client_mismatch() {
    local OSSL_IP_LEAF=none
    _retry_probe served "_ossl_probe_ip_client_once" "$@"
}


# --- 0-RTT early data -----------------------------------------------------
#
# Two things about a server's early-data configuration are visible to an
# ordinary client and neither needs a hook into the library:
#
#   * the limit itself. A TLS 1.3 server that is willing to read early data
#     says so in every NewSessionTicket, as the `early_data` extension whose
#     body is `max_early_data_size` (RFC 8446 section 4.6.1). `openssl
#     s_client` prints it as `Max Early Data: N` in the session dump it writes
#     for each ticket that arrives, and prints `0` where the extension is
#     absent -- so "off" and "on with a number" are the same observable read
#     twice, not two different ones.
#   * whether early data is actually read. A second connection resuming that
#     ticket and given `-early_data` reports `Early data was accepted` or
#     `Early data was rejected`; where the ticket advertised 0 a conforming
#     client does not offer any and it reports `Early data was not sent`,
#     which is the correct answer for that leg rather than a failure of it.
#
# Both need two connections in one probe run, because a ticket has to come
# from this server before it can be resumed against it.
#
# ossl_probe_early_data <server-binary> <ca-dir> [cap]
#
# With a cap the server is started with `--max-early-data <cap>` added to the
# command line ossl_probe_server uses; without one it is started with exactly
# that command line, which is the early-data-off default. Emits:
#   started=yes|no  handshake=yes|no  max_early=N  resumed=yes|no
#   early=accepted|rejected|not-sent|unknown
_ossl_probe_early_data_once() {
    local bin="$1" ca="$2" cap="${3:-}"
    local port pid tag sess ed one two out
    port="$(_free_port)"
    tag="ed-${cap:-off}"
    sess="$WORK/$tag-sess.pem"; ed="$WORK/$tag-payload.bin"
    one="$WORK/$tag-first.out"; two="$WORK/$tag-second.out"
    rm -f "$sess"
    # Sixteen bytes, and deliberately far below every cap this ladder
    # configures: the payload is here to make "was it read" answerable, not to
    # test the bound. Why the bound itself is not asked here is written down on
    # early_data_group in checks-lib.sh.
    printf 'EARLY-DATA-PING\n' > "$ed"

    if [ -n "$cap" ]; then
        run_unprivileged "$bin" --certs "$ca/end.fullchain" --key "$END_KEY" \
            --port "$port" --max-early-data "$cap" echo \
            > "$WORK/$tag-srv.log" 2>&1 &
    else
        run_unprivileged "$bin" --certs "$ca/end.fullchain" --key "$END_KEY" \
            --port "$port" echo > "$WORK/$tag-srv.log" 2>&1 &
    fi
    pid=$!
    if ! _wait_port "$port" "$pid"; then
        echo "started=no"
        # A server that will not come up fails the group, and without this the
        # log says only that it failed. `--max-early-data` is a flag the
        # earlier releases' example server does not have, and "Unknown flag"
        # on this line is the whole finding.
        log "[verifier] early-data probe server (cap=${cap:-off}) did not come up: $(tail -n 3 "$WORK/$tag-srv.log" 2>/dev/null | tr '\n' ' ')"
        _kill_pid "$pid"; return 1
    fi
    echo "started=yes"

    # First connection: take a ticket. Held open exactly as ossl_probe_server
    # holds its connection open, because NewSessionTicket is post-handshake and
    # closing at once collects nothing to resume.
    local sb=""
    command -v stdbuf > /dev/null 2>&1 && sb="stdbuf -o0"
    : > "$one"
    { printf 'PING\n'; _hold_open "$one"; } | timeout 40 $sb openssl s_client \
        -connect "127.0.0.1:$port" -CAfile "$ca/ca.cert" \
        -servername testserver.com -sess_out "$sess" > "$one" 2>&1 || true
    out="$(cat "$one" 2>/dev/null)"
    if printf '%s' "$out" | grep -q 'Cipher *: *(NONE)\|no peer certificate available'; then
        echo "handshake=no"; _kill_pid "$pid"; return 0
    fi
    echo "handshake=yes"
    # Every ticket carries the same limit, so the distinct values are printed
    # rather than the first: a server that advertises two different numbers is
    # a finding, and reading only one of them would hide it. Joined with `/`
    # so the fact stays a single whitespace-free token.
    echo "max_early=$(printf '%s\n' "$out" | sed -n 's/^ *Max Early Data: *\([0-9]*\).*$/\1/p' \
        | sort -u | paste -sd/ -)"
    echo "tickets=$(printf '%s' "$out" | grep -c 'New Session Ticket arrived')"

    if [ ! -s "$sess" ]; then
        echo "resumed=no"; echo "early=unknown"; _kill_pid "$pid"; return 0
    fi

    # Second connection: resume, offering early data. `-early_data` makes
    # s_client write the file in its first flight where the ticket permits it.
    : > "$two"
    { printf 'PING\n'; sleep 2; } | timeout 40 openssl s_client \
        -connect "127.0.0.1:$port" -CAfile "$ca/ca.cert" \
        -servername testserver.com -sess_in "$sess" -early_data "$ed" \
        > "$two" 2>&1 || true
    out="$(cat "$two" 2>/dev/null)"
    _kill_pid "$pid"

    if printf '%s' "$out" | grep -q '^Reused, TLS'; then echo "resumed=yes"
    else echo "resumed=no"; fi
    case "$out" in
        *"Early data was accepted"*) echo "early=accepted" ;;
        *"Early data was rejected"*) echo "early=rejected" ;;
        *"Early data was not sent"*) echo "early=not-sent" ;;
        *) echo "early=unknown" ;;
    esac
    return 0
}

ossl_probe_early_data() { _retry_probe started "_ossl_probe_early_data_once" "$@"; }

# --------------------------------------------------------------------------
# Single-shot example servers.
#
# _wait_port's readiness test opens a TCP connection and closes it again. That
# is harmless for a server written to loop over complete connections, and it is
# fatal for the two single-shot examples driven below. rustls' unbuffered-server
# reads the empty connection as a client that never sent a hello and panics out
# of its accept loop after MAX_ITERATIONS; server_acceptor calls
# `acceptor.read_tls()` on a socket already at EOF and spins. Either way the
# readiness probe kills the thing it was checking on, and the scored group then
# reports "did not start" about a server that started perfectly.
#
# So readiness is read out of the kernel's own listening table instead of being
# probed for. Nothing connects to these servers except the one connection the
# check is actually about.
# --------------------------------------------------------------------------

# _kill_tree <pid>
#
# Kills a backgrounded server *and its descendants*. Needed because
# `run_unprivileged foo &` backgrounds a shell **function**, so `$!` names the
# forked subshell and not the binary underneath it -- `kill $!` reaps the
# subshell and leaves the server running. Every probe that draws a fresh random
# port survives that leak unnoticed; the unbuffered-server probe below cannot,
# because upstream's example hardcodes its port, so a leaked server from the
# first attempt is what the retry would then be grading.
#
# /proc is read directly rather than shelling out per level: the children file
# is one read and it is exactly the list wanted. pgrep is the fallback.
_kill_tree() {
    local pid="${1:-}" kids="" k
    [ -n "$pid" ] || return 0
    if [ -r "/proc/$pid/task/$pid/children" ]; then
        kids="$(cat "/proc/$pid/task/$pid/children" 2>/dev/null || true)"
    else
        kids="$(pgrep -P "$pid" 2>/dev/null || true)"
    fi
    for k in $kids; do _kill_tree "$k"; done
    _kill_pid "$pid"
}

# _wait_listen <port> [pid] [tries]
#
# Waits for a LISTEN socket on <port> without opening a connection to it. Gives
# up early if a pid is given and that process dies, exactly as _wait_port does.
_wait_listen() {
    local port="$1" pid="${2:-}" tries="${3:-300}" hex i files=""
    hex="$(printf '%04X' "$port")"
    [ -r /proc/net/tcp ]  && files="$files /proc/net/tcp"
    [ -r /proc/net/tcp6 ] && files="$files /proc/net/tcp6"
    if [ -z "$files" ]; then
        # No procfs view of the listening table. Fall back to a fixed wait
        # rather than to a connect, which would kill the server being probed.
        sleep 5; return 0
    fi
    for i in $(seq 1 "$tries"); do
        # Field 2 is the local address, field 4 the state; 0A is TCP_LISTEN.
        # Matching on field 2 only is what keeps an *outbound* socket to the
        # same port number from reading as a listener.
        if awk -v p=":$hex" '$2 ~ p"$" && $4 == "0A" { found = 1 }
                             END { exit !found }' $files 2>/dev/null; then
            return 0
        fi
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then return 1; fi
        sleep 0.1
    done
    return 1
}

# find_named_bin <name>
#
# The same idea as find_example_bin, for an example that has one name rather
# than a role. Four locations, in the order cargo writes them; TEST_INTERFACE.md
# publishes this list wherever a check depends on it.
find_named_bin() {
    local name="$1" c
    for c in "target/debug/$name" "target/debug/examples/$name" \
             "target/release/$name" "target/release/examples/$name"
    do
        if [ -x "$c" ]; then printf '%s\n' "$c"; return 0; fi
    done
    return 1
}

# ossl_probe_unbuffered_server <ca-dir>
#
# Drives the candidate's *unbuffered-API* example server -- a different code
# path from the mio example every other server-side probe uses, and the only
# one this ladder can reach from outside the library. Upstream's example takes
# its chain and key as two positional arguments, listens on a fixed port and
# answers one HTTP request per connection, so that is what is asked for and
# what TEST_INTERFACE.md publishes.
#
# Emits: found=yes|no  started=yes|no  handshake=yes|no  proto=TLSv1.x
#        response=yes|no
_UNBUFFERED_PORT=1443
_ossl_probe_unbuffered_server_once() {
    local ca="$1" bin port pid out sc rc=0
    bin="$( cd /app 2>/dev/null && find_named_bin unbuffered-server )" || {
        echo "found=no"; echo "started=no"; return 1; }
    echo "found=yes"
    port="$_UNBUFFERED_PORT"

    run_unprivileged "/app/$bin" "$ca/end.fullchain" "$END_KEY" \
        > "$WORK/unbuf-srv.log" 2>&1 &
    pid=$!
    if ! _wait_listen "$port" "$pid" 300; then
        echo "started=no"
        log "[verifier] unbuffered example server did not listen on $port: $(tail -n 3 "$WORK/unbuf-srv.log" 2>/dev/null | tr '\n' ' ')"
        _kill_tree "$pid"; return 1
    fi
    # still-alive guard. _wait_listen reads the kernel's table rather than the
    # candidate's process, so a foreign listener on the fixed port would look
    # like a successful start while our server was dying of EADDRINUSE -- and
    # the check would then grade whatever else is on 1443. Confirming the pid
    # we started is the process that is up closes that.
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "started=no"
        log "[verifier] unbuffered example server exited before the probe; port $port may be in use: $(tail -n 3 "$WORK/unbuf-srv.log" 2>/dev/null | tr '\n' ' ')"
        _kill_tree "$pid"; return 1
    fi
    echo "started=yes"

    # One connection, and only one: the example handles a single connection at
    # a time and exits its loop on the first one it cannot complete.
    sc="$WORK/unbuf-sclient.out"
    : > "$sc"
    { printf 'GET / HTTP/1.0\r\n\r\n'; sleep 3; } | timeout 40 openssl s_client \
        -connect "127.0.0.1:$port" -CAfile "$ca/ca.cert" \
        -servername testserver.com > "$sc" 2>&1 || rc=$?
    out="$(cat "$sc" 2>/dev/null)"
    _kill_tree "$pid"

    if printf '%s' "$out" | grep -q 'Cipher *: *(NONE)\|no peer certificate available'; then
        echo "handshake=no"; return 0
    fi
    echo "handshake=yes"
    echo "proto=$(printf '%s' "$out" | sed -n 's/^ *Protocol *: *\(.*\)$/\1/p' | head -1)"
    # Any HTTP/1 status line counts. What the body says is the example's own
    # choice and is not read.
    if printf '%s' "$out" | grep -qa '^HTTP/1'; then echo "response=yes"
    else echo "response=no"; fi
    return 0
}

ossl_probe_unbuffered_server() {
    _retry_probe started "_ossl_probe_unbuffered_server_once" "$@"
}

# probe_acceptor_first_record
#
# Starts the candidate's `server_acceptor` example and sends it a ClientHello
# whose server_name is not a legal hostname, then reads the *first record* that
# comes back -- the same question server_sni_group asks of the mio server, put
# to the acceptor API instead. The acceptor decides whether to accept the hello
# before any ServerConfig exists, so what it can send when it refuses is
# whatever the accept call hands back; a release whose accept call hands back
# nothing can only close the socket.
#
# The example writes its own throw-away PKI into its working directory, so it
# is run from the unprivileged scratch and never from /app.
#
# Emits: found=yes|no  started=yes|no  first=<kind>:<detail>
_acceptor_probe_once() {
    local bin port pid out dir
    bin="$( cd /app 2>/dev/null && find_named_bin server_acceptor )" || {
        echo "found=no"; echo "started=no"; return 1; }
    echo "found=yes"
    port="$(_free_port)"
    dir="$UNPRIV/acceptor"
    rm -rf "$dir"; mkdir -p "$dir"
    chown "$SCORE_UID:$SCORE_GID" "$dir" 2>/dev/null || true

    # Only --port. The example's other flags name where it writes its own
    # throw-away PKI, and their defaults are relative to the working directory,
    # which is the scratch above; the contract names one flag, not five.
    ( cd "$dir" && run_unprivileged "/app/$bin" --port "$port" ) \
        > "$WORK/acceptor-srv.log" 2>&1 &
    pid=$!
    if ! _wait_listen "$port" "$pid" 300; then
        echo "started=no"
        log "[verifier] acceptor example did not listen on $port: $(tail -n 3 "$WORK/acceptor-srv.log" 2>/dev/null | tr '\n' ' ')"
        _kill_tree "$pid"; return 1
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "started=no"
        log "[verifier] acceptor example exited before the probe: $(tail -n 3 "$WORK/acceptor-srv.log" 2>/dev/null | tr '\n' ' ')"
        _kill_tree "$pid"; return 1
    fi
    echo "started=yes"

    out="$(run_unprivileged python3 "$TESTS/bin/send-clienthello.py" \
        --port "$port" --sni 'ab@cd.com' --timeout 30 2>/dev/null)"
    _kill_tree "$pid"
    printf 'first=%s\n' "$(printf '%s' "${out#result=}" | sed 's/ detail=/:/')"
    return 0
}

probe_acceptor_first_record() {
    _retry_probe started "_acceptor_probe_once" "$@"
}
