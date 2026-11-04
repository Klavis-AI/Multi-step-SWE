//! A first-party mock *managed* pluggable transport proxy.
//!
//! Scope decision S5 says individual pluggable transports are out of scope and
//! that step 12 grades the *manager* protocol instead.  This binary is the far
//! end of that protocol: it speaks the managed-proxy IPC of pt-spec.txt v1 on
//! its stdout, and nothing else.  It never opens a socket and never touches the
//! network, so every case built on it is deterministic.
//!
//! `argv[1]` selects a scripted conversation.  Whatever the script, the process
//! first records the environment it was handed into `$TOR_PT_STATE_LOCATION/env.txt`
//! (one `KEY=VALUE` per line, sorted, `TOR_PT_*` only) so that a test can check
//! the manager's side of pt-spec.txt section 3.2 without any IPC at all, and
//! then exits of its own accord.  Exiting rather than blocking matters: the
//! manager's reader thread owns our stdin and only releases it when our stdout
//! reaches EOF.

use std::io::Write;

/// Print one IPC line to stdout.
fn say(line: &str) {
    let mut out = std::io::stdout();
    let _ = writeln!(out, "{}", line);
    let _ = out.flush();
}

/// The transports the manager asked us for, in the order it asked.
fn requested() -> Vec<String> {
    match std::env::var("TOR_PT_CLIENT_TRANSPORTS") {
        Ok(s) if !s.is_empty() => s.split(',').map(|s| s.to_string()).collect(),
        _ => Vec::new(),
    }
}

/// Record the `TOR_PT_*` environment into the state directory.
fn dump_env() {
    let dir = match std::env::var("TOR_PT_STATE_LOCATION") {
        Ok(d) => d,
        Err(_) => return,
    };
    let mut vars: Vec<(String, String)> = std::env::vars()
        .filter(|(k, _)| k.starts_with("TOR_PT_"))
        .collect();
    vars.sort();
    let mut body = String::new();
    for (k, v) in vars {
        body.push_str(&k);
        body.push('=');
        body.push_str(&v);
        body.push('\n');
    }
    let _ = std::fs::create_dir_all(&dir);
    let _ = std::fs::write(std::path::Path::new(&dir).join("env.txt"), body);
}

/// Announce every requested transport as a working SOCKS5 endpoint.
fn announce_all(protocol: &str) {
    for (i, t) in requested().iter().enumerate() {
        say(&format!("CMETHOD {} {} 127.0.0.1:{}", t, protocol, 19000 + i));
    }
}

fn main() {
    dump_env();
    let script = std::env::args().nth(1).unwrap_or_else(|| "ok".to_string());
    match script.as_str() {
        // A clean version-1 startup that launches everything it was asked for.
        "ok" => {
            say("VERSION 1");
            announce_all("socks5");
            say("CMETHODS DONE");
        }
        // Same, but the transport speaks SOCKS4.
        "ok_socks4" => {
            say("VERSION 1");
            announce_all("socks4");
            say("CMETHODS DONE");
        }
        // A clean startup that also confirms the upstream proxy we were given.
        "ok_proxy" => {
            say("VERSION 1");
            say("PROXY DONE");
            announce_all("socks5");
            say("CMETHODS DONE");
        }
        // Chatter that the manager must tolerate and skip over.
        "ok_noisy" => {
            say("VERSION 1");
            say("LOG SEVERITY=notice MESSAGE=\"starting up\"");
            say("this is not a command at all");
            if let Some(t) = requested().first() {
                say(&format!("STATUS TRANSPORT={} CONNECT=SUCCESS", t));
            }
            announce_all("socks5");
            say("CMETHODS DONE");
        }
        // pt-spec.txt: no mutually supported IPC version.
        "version_error" => say("VERSION-ERROR no-version"),
        // A version we did not ask for.
        "version_two" => say("VERSION 2"),
        // pt-spec.txt: the environment was unusable.
        "env_error" => say("ENV-ERROR no TOR_PT_STATE_LOCATION"),
        // PROXY DONE when no TOR_PT_PROXY was supplied.
        "unasked_proxy" => {
            say("VERSION 1");
            say("PROXY DONE");
            announce_all("socks5");
            say("CMETHODS DONE");
        }
        // The upstream proxy could not be used.
        "proxy_error" => {
            say("VERSION 1");
            say("PROXY-ERROR unsupported proxy scheme");
        }
        // One transport reported a startup failure.
        "cmethod_error" => {
            say("VERSION 1");
            let t = requested().first().cloned().unwrap_or_default();
            say(&format!("CMETHOD-ERROR {} no bridges configured", t));
        }
        // Finished without ever launching what we asked for.
        "no_cmethods" => {
            say("VERSION 1");
            say("CMETHODS DONE");
        }
        // Launched a transport nobody asked for.
        "extra_transport" => {
            say("VERSION 1");
            say("CMETHOD gopher socks5 127.0.0.1:19999");
            say("CMETHODS DONE");
        }
        // A SOCKS dialect the manager does not implement.
        "bad_protocol" => {
            say("VERSION 1");
            let t = requested().first().cloned().unwrap_or_default();
            say(&format!("CMETHOD {} socks6 127.0.0.1:19000", t));
            say("CMETHODS DONE");
        }
        // Stay alive, saying nothing, for longer than any configured deadline.
        "silent" => std::thread::sleep(std::time::Duration::from_secs(3)),
        other => say(&format!("LOG SEVERITY=error MESSAGE=\"unknown script {}\"", other)),
    }
}
