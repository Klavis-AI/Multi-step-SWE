//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-initial-keys` is `fn cap_initial_keys()`.
//! The driver runs each one on its own with `--exact` and asserts that
//! exactly one test executed, so a renamed, `#[ignore]`d or accidentally
//! deleted function scores zero for that group rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::collections::BTreeMap;
use std::collections::BTreeSet;
use std::path::Path;
use std::time::Duration;

#[allow(unused_imports)]
use quiche::h3::NameValue;
#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// --- the smallest JSON reader that can answer a qlog question --------------

/// A parsed JSON value. Enough of JSON to walk a qlog record; no more.
#[derive(Debug, Clone, PartialEq)]
enum Json {
    Null,
    Bool(bool),
    Num(f64),
    Str(String),
    Arr(Vec<Json>),
    Obj(BTreeMap<String, Json>),
}

impl Json {
    fn get(&self, key: &str) -> Option<&Json> {
        match self {
            Json::Obj(m) => m.get(key),
            _ => None,
        }
    }

    #[allow(dead_code)]
    fn path(&self, keys: &[&str]) -> Option<&Json> {
        let mut cur = self;
        for k in keys {
            cur = cur.get(k)?;
        }
        Some(cur)
    }

    fn as_str(&self) -> Option<&str> {
        match self {
            Json::Str(s) => Some(s),
            _ => None,
        }
    }

    fn as_u64(&self) -> Option<u64> {
        match self {
            Json::Num(n) if *n >= 0.0 && n.fract() == 0.0 => Some(*n as u64),
            _ => None,
        }
    }

    fn as_arr(&self) -> Option<&Vec<Json>> {
        match self {
            Json::Arr(a) => Some(a),
            _ => None,
        }
    }

    #[allow(dead_code)]
    fn keys(&self) -> BTreeSet<String> {
        match self {
            Json::Obj(m) => m.keys().cloned().collect(),
            _ => BTreeSet::new(),
        }
    }
}

fn json_parse(s: &str) -> Result<Json, String> {
    let b: Vec<char> = s.chars().collect();
    let mut i = 0usize;
    let v = json_value(&b, &mut i)?;
    json_ws(&b, &mut i);
    if i != b.len() {
        return Err(format!("trailing bytes at char {i}"));
    }
    Ok(v)
}

fn json_ws(b: &[char], i: &mut usize) {
    while *i < b.len() && b[*i].is_whitespace() {
        *i += 1;
    }
}

fn json_value(b: &[char], i: &mut usize) -> Result<Json, String> {
    json_ws(b, i);
    let c = *b.get(*i).ok_or("unexpected end of input")?;
    match c {
        '{' => {
            *i += 1;
            let mut m = BTreeMap::new();
            json_ws(b, i);
            if b.get(*i) == Some(&'}') {
                *i += 1;
                return Ok(Json::Obj(m));
            }
            loop {
                json_ws(b, i);
                let k = match json_value(b, i)? {
                    Json::Str(s) => s,
                    other => return Err(format!("object key is not a string: {other:?}")),
                };
                json_ws(b, i);
                if b.get(*i) != Some(&':') {
                    return Err(format!("expected ':' at char {i}"));
                }
                *i += 1;
                let v = json_value(b, i)?;
                m.insert(k, v);
                json_ws(b, i);
                match b.get(*i) {
                    Some(',') => *i += 1,
                    Some('}') => {
                        *i += 1;
                        return Ok(Json::Obj(m));
                    },
                    _ => return Err(format!("expected ',' or '}}' at char {i}")),
                }
            }
        },
        '[' => {
            *i += 1;
            let mut a = Vec::new();
            json_ws(b, i);
            if b.get(*i) == Some(&']') {
                *i += 1;
                return Ok(Json::Arr(a));
            }
            loop {
                a.push(json_value(b, i)?);
                json_ws(b, i);
                match b.get(*i) {
                    Some(',') => *i += 1,
                    Some(']') => {
                        *i += 1;
                        return Ok(Json::Arr(a));
                    },
                    _ => return Err(format!("expected ',' or ']' at char {i}")),
                }
            }
        },
        '"' => {
            *i += 1;
            let mut s = String::new();
            loop {
                let c = *b.get(*i).ok_or("unterminated string")?;
                *i += 1;
                match c {
                    '"' => return Ok(Json::Str(s)),
                    '\\' => {
                        let e = *b.get(*i).ok_or("unterminated escape")?;
                        *i += 1;
                        match e {
                            'n' => s.push('\n'),
                            't' => s.push('\t'),
                            'r' => s.push('\r'),
                            'b' => s.push('\u{8}'),
                            'f' => s.push('\u{c}'),
                            'u' => {
                                let hex: String = b[*i..(*i + 4).min(b.len())].iter().collect();
                                *i += 4;
                                let n = u32::from_str_radix(&hex, 16)
                                    .map_err(|_| format!("bad \\u escape {hex}"))?;
                                s.push(char::from_u32(n).unwrap_or('\u{fffd}'));
                            },
                            other => s.push(other),
                        }
                    },
                    other => s.push(other),
                }
            }
        },
        't' | 'f' | 'n' => {
            let rest: String = b[*i..(*i + 5).min(b.len())].iter().collect();
            if rest.starts_with("true") {
                *i += 4;
                Ok(Json::Bool(true))
            } else if rest.starts_with("false") {
                *i += 5;
                Ok(Json::Bool(false))
            } else if rest.starts_with("null") {
                *i += 4;
                Ok(Json::Null)
            } else {
                Err(format!("unknown literal at char {i}: {rest}"))
            }
        },
        _ => {
            let start = *i;
            while *i < b.len() && (b[*i].is_ascii_digit() || "+-.eE".contains(b[*i])) {
                *i += 1;
            }
            let t: String = b[start..*i].iter().collect();
            t.parse::<f64>().map(Json::Num).map_err(|_| format!("bad number {t:?}"))
        },
    }
}

/// Every record of a JSON-SEQ qlog that parses, in file order.
///
/// Step 06 reads a finished qlog and is strict about RFC 7464 framing, because
/// the framing is what that group is about. Here the log belongs to a server
/// that is still running behind a buffered writer, so a flush that lands
/// mid-record leaves a partial trailing line. That is not the candidate's
/// fault and must not read as one: a record that does not parse is skipped,
/// the group waits for the records it needs to appear, and if they never do it
/// prints what it did see.
fn sqlog_records_so_far(raw: &[u8]) -> Vec<Json> {
    String::from_utf8_lossy(raw)
        .split('\u{1e}')
        .filter_map(|r| json_parse(r.trim()).ok())
        .collect()
}

// --- reading HTTP/3 out of a qlog ------------------------------------------

/// True when `ev` is the qlog event `name`, on `stream_id`, carrying a frame
/// of type `ty`.
fn qlog_is(ev: &Json, name: &str, stream_id: u64, ty: &str) -> bool {
    ev.get("name").and_then(Json::as_str) == Some(name)
        && ev.get("data").and_then(|d| d.get("stream_id")).and_then(Json::as_u64)
            == Some(stream_id)
        && ev
            .get("data")
            .and_then(|d| d.get("frame"))
            .and_then(|f| f.get("frame_type"))
            .and_then(Json::as_str)
            == Some(ty)
}

/// The `headers` list of an HTTP/3 qlog frame event, as `(name, value)`.
fn qlog_headers(ev: &Json) -> Vec<(String, String)> {
    ev.get("data")
        .and_then(|d| d.get("frame"))
        .and_then(|f| f.get("headers"))
        .and_then(Json::as_arr)
        .map(|a| {
            a.iter()
                .filter_map(|h| {
                    Some((
                        h.get("name")?.as_str()?.to_string(),
                        h.get("value")?.as_str()?.to_string(),
                    ))
                })
                .collect()
        })
        .unwrap_or_default()
}

/// The `data.length` of a qlog frame event.
fn qlog_length(ev: &Json) -> Option<u64> {
    ev.get("data").and_then(|d| d.get("length")).and_then(Json::as_u64)
}

/// Every `http:*` event in `recs`, one per line, for an assert message.
fn http_events(recs: &[Json]) -> String {
    let mut out: Vec<String> = Vec::new();
    for ev in recs {
        let name = match ev.get("name").and_then(Json::as_str) {
            Some(n) if n.starts_with("http:") => n,
            _ => continue,
        };
        let sid = ev
            .get("data")
            .and_then(|d| d.get("stream_id"))
            .and_then(Json::as_u64);
        let ty = ev
            .get("data")
            .and_then(|d| d.get("frame"))
            .and_then(|f| f.get("frame_type"))
            .and_then(Json::as_str)
            .unwrap_or("-");
        let hdrs = qlog_headers(ev);
        if hdrs.is_empty() {
            out.push(format!("{name} stream={sid:?} frame={ty} length={:?}", qlog_length(ev)));
        } else {
            out.push(format!(
                "{name} stream={sid:?} frame={ty} [{}]",
                hdrs.iter().map(|(n, v)| format!("{n}: {v}")).collect::<Vec<_>>().join(", ")
            ));
        }
    }
    if out.is_empty() {
        return "(the server's qlog has no http:* events at all)".to_string();
    }
    out.join("\n")
}

/// The one `prefix*.sqlog` in `dir`, or `None` while it is not there yet.
fn maybe_sqlog(dir: &Path, prefix: &str) -> Option<Vec<u8>> {
    let mut hits: Vec<std::path::PathBuf> = std::fs::read_dir(dir)
        .ok()?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| {
            p.file_name()
                .and_then(|n| n.to_str())
                .map(|n| n.starts_with(prefix) && n.ends_with(".sqlog"))
                .unwrap_or(false)
        })
        .collect();
    hits.sort();
    if hits.len() != 1 {
        return None;
    }
    std::fs::read(&hits[0]).ok()
}

// --- reading HTTP/3 off a `pair::H3Pair` -----------------------------------

/// One HTTP/3 event, flattened so that a whole exchange can be compared and
/// printed as a list.
#[derive(Debug, Clone, PartialEq)]
enum Seen {
    /// A header section: stream, its `(name, value)` pairs, `more_frames`.
    Headers(u64, Vec<(String, String)>, bool),
    /// A body: stream, the bytes that were read out of it.
    Data(u64, Vec<u8>),
    /// The stream is finished.
    Finished(u64),
    /// Anything else, debug-formatted, so that an unexpected event is
    /// reported rather than dropped.
    Other(u64, String),
}

/// `(name, value)` pairs of a header list, as strings.
fn hdr_pairs(list: &[quiche::h3::Header]) -> Vec<(String, String)> {
    list.iter()
        .map(|h| {
            (
                String::from_utf8_lossy(h.name()).into_owned(),
                String::from_utf8_lossy(h.value()).into_owned(),
            )
        })
        .collect()
}

/// The value of `name` in a header list, if it has one.
fn header_of(hdrs: &[(String, String)], name: &str) -> Option<String> {
    hdrs.iter().find(|(n, _)| n == name).map(|(_, v)| v.clone())
}

/// A one-line-per-event rendering of an exchange, for an assert message.
fn shape(evs: &[Seen]) -> String {
    if evs.is_empty() {
        return "(nothing: poll returned Done straight away)".to_string();
    }
    evs.iter()
        .map(|e| match e {
            Seen::Headers(s, h, more) => format!(
                "Headers(stream={s}, more_frames={more}) [{}]",
                h.iter().map(|(n, v)| format!("{n}: {v}")).collect::<Vec<_>>().join(", ")
            ),
            Seen::Data(s, b) => format!(
                "Data(stream={s}, {} bytes) {}",
                b.len(),
                testkit::hex(&b[..b.len().min(16)])
            ),
            Seen::Finished(s) => format!("Finished(stream={s})"),
            Seen::Other(s, d) => format!("{d} (stream={s})"),
        })
        .collect::<Vec<_>>()
        .join("\n")
}

/// Poll the client's HTTP/3 connection until `Done`, reading every body.
///
/// The bodies have to be read here rather than left for the caller: `poll`
/// keeps returning `Data` for a stream whose body has not been taken, so a
/// drain that ignored it would not terminate.
fn drain_client(h: &mut pair::H3Pair) -> Vec<Seen> {
    let mut out = Vec::new();
    for _ in 0..256 {
        match h.poll_client() {
            Ok((s, quiche::h3::Event::Headers { list, more_frames })) =>
                out.push(Seen::Headers(s, hdr_pairs(&list), more_frames)),
            Ok((s, quiche::h3::Event::Data)) => {
                let mut body = Vec::new();
                let mut buf = [0u8; 4096];
                while let Ok(n) = h.client.recv_body(&mut h.t.client, s, &mut buf) {
                    if n == 0 {
                        break;
                    }
                    body.extend_from_slice(&buf[..n]);
                }
                out.push(Seen::Data(s, body));
            },
            Ok((s, quiche::h3::Event::Finished)) => out.push(Seen::Finished(s)),
            Ok((s, ev)) => out.push(Seen::Other(s, format!("{ev:?}"))),
            Err(quiche::h3::Error::Done) => return out,
            Err(e) => {
                out.push(Seen::Other(u64::MAX, format!("poll returned Err({e:?})")));
                return out;
            },
        }
    }
    out
}

/// [`drain_client`], on the server's HTTP/3 connection.
fn drain_server(h: &mut pair::H3Pair) -> Vec<Seen> {
    let mut out = Vec::new();
    for _ in 0..256 {
        match h.poll_server() {
            Ok((s, quiche::h3::Event::Headers { list, more_frames })) =>
                out.push(Seen::Headers(s, hdr_pairs(&list), more_frames)),
            Ok((s, quiche::h3::Event::Data)) => {
                let mut body = Vec::new();
                let mut buf = [0u8; 4096];
                while let Ok(n) = h.server.recv_body(&mut h.t.server, s, &mut buf) {
                    if n == 0 {
                        break;
                    }
                    body.extend_from_slice(&buf[..n]);
                }
                out.push(Seen::Data(s, body));
            },
            Ok((s, quiche::h3::Event::Finished)) => out.push(Seen::Finished(s)),
            Ok((s, ev)) => out.push(Seen::Other(s, format!("{ev:?}"))),
            Err(quiche::h3::Error::Done) => return out,
            Err(e) => {
                out.push(Seen::Other(u64::MAX, format!("poll returned Err({e:?})")));
                return out;
            },
        }
    }
    out
}

/// An ordinary GET request header list for `path`.
fn get_request(path: &'static [u8]) -> Vec<quiche::h3::Header> {
    vec![
        quiche::h3::Header::new(b":method", b"GET"),
        quiche::h3::Header::new(b":scheme", b"https"),
        quiche::h3::Header::new(b":authority", b"quic.test"),
        quiche::h3::Header::new(b":path", path),
    ]
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-h3i-replay  (T1)
// ---------------------------------------------------------------------------

/// The authority the action file names and the SNI `h3i` is given. They are
/// the same string on purpose: `--replay-host-override` exists because they
/// can differ, and a group that needed it would be testing the override.
const REPLAY_AUTHORITY: &str = "harbor.example";
/// The path the action file names, and the name of the file in the docroot.
const REPLAY_PATH: &str = "/harbor-12";

/// Write the verifier's two-record JSON-SEQ action file and return its path.
///
/// The verifier writes this, not the candidate: the point of the group is
/// that `h3i` can read a qlog somebody else produced. Record one is the qlog
/// header, record two is one `http:frame_created` carrying a HEADERS frame on
/// stream 0 with `fin_stream` set, which is the shape
/// `WORKSPACE_HANDOFF.md` fixes. Both records are `0x1E`-prefixed and
/// newline-terminated, per RFC 7464.
fn replay_actions(dir: &Path) -> std::path::PathBuf {
    let p = dir.join("actions.qlog");
    let mut out: Vec<u8> = Vec::new();

    out.push(0x1e);
    out.extend_from_slice(
        br#"{"qlog_version": "0.3", "qlog_format": "JSON-SEQ", "title": "h3i replay fixture", "trace": {"vantage_point": {"type": "client"}, "configuration": {"time_offset": 0.0}}}"#,
    );
    out.push(b'\n');

    out.push(0x1e);
    out.extend_from_slice(
        format!(
            r#"{{"time": 0.0, "name": "http:frame_created", "data": {{"stream_id": 0, "frame": {{"frame_type": "headers", "headers": [{{"name": ":method", "value": "GET"}}, {{"name": ":scheme", "value": "https"}}, {{"name": ":authority", "value": "{REPLAY_AUTHORITY}"}}, {{"name": ":path", "value": "{REPLAY_PATH}"}}]}}}}, "fin_stream": true}}"#
        )
        .as_bytes(),
    );
    out.push(b'\n');

    std::fs::write(&p, &out)
        .unwrap_or_else(|e| panic!("cannot write the action file {}: {e}", p.display()));
    p
}

/// `cap-h3i-replay`: the new workspace member replays a qlog the verifier
/// wrote, against the candidate's own server, and the server's qlog says what
/// arrived.
///
/// Three processes' worth of work in one group, and the assertions are split
/// across two of them on purpose. `h3i` exiting 0 is necessary and nowhere
/// near sufficient -- its `main` logs a client error and still returns
/// `Ok(())`, so a binary that connected to nothing would exit 0 too. What
/// makes this a capability observation is the *server's* `.sqlog`: the
/// request has to have arrived on stream 0 with the `:path` the action file
/// named, and a response has to have gone back on the same stream.
///
/// The server's qlog rather than `h3i`'s stderr, because a `.sqlog` is an
/// emitted artefact with a schema this package already reads at step 06,
/// while `h3i`'s stderr is a `log`-crate format that nothing in this step
/// declares. A group that grepped it would be scoring a logging convention.
///
/// Both idle timeouts are short and neither is asserted on. `h3i` has no
/// other way to finish -- it replays its actions and then waits for the
/// connection to die -- and the server has to let its connection go before
/// its buffered qlog writer is dropped and flushed. The group waits for the
/// records it needs to appear rather than for a duration.
#[test]
fn cap_h3i_replay() {
    let h3i_bin = testkit::need_bin(&["h3i"]);
    let server_bin = testkit::server_bin();

    let dir = testkit::scratch("cap-h3i-replay");
    let want = testkit::body(512, 0x12a0_5eed);
    let root = testkit::docroot(&dir, &[("harbor-12", want.clone())]);
    let qlogdir = dir.join("qlogs");
    std::fs::create_dir_all(&qlogdir)
        .unwrap_or_else(|e| panic!("cannot create {}: {e}", qlogdir.display()));
    let tls = testkit::tls();
    let port = testkit::free_port();
    let actions = replay_actions(&dir);

    let mut sc = testkit::cmd(&server_bin);
    sc.env("QLOGDIR", &qlogdir)
        .arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--no-retry")
        .arg("--idle-timeout")
        .arg("6000");
    let mut server = testkit::spawn_server(&mut sc, port, &dir, "h3i-server", LONG);

    // `--connect-to` keeps the authority in the replayed headers and the
    // address the socket goes to separate, which is the whole reason the flag
    // exists. The action file is passed as an absolute path because the
    // reference answers a missing `--qlog-input` with a panic rather than a
    // diagnostic.
    let mut hc = testkit::cmd(&h3i_bin);
    hc.current_dir(&dir)
        .env_remove("QLOGDIR")
        .arg("--qlog-input")
        .arg(&actions)
        .arg("--no-verify")
        .arg("--connect-to")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--idle-timeout")
        .arg("2000")
        .arg(format!("{REPLAY_AUTHORITY}:{port}"));
    let run = testkit::run(&mut hc, LONG, &dir, "h3i");

    let slog = server.log();
    assert!(
        !run.timed_out,
        "expected {} to replay {} against 127.0.0.1:{port} and exit within {LONG:?}; observed: \
         still running at the deadline, killed.\n--- h3i stderr ---\n{}\n--- server log ---\n{slog}",
        h3i_bin.display(),
        actions.display(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after replaying {}; observed status {:?}.\n\
         --- h3i stderr ---\n{}\n--- server log ---\n{slog}",
        h3i_bin.display(),
        actions.display(),
        run.status,
        run.stderr
    );

    // Wait for the server's qlog to carry the exchange. The writer is
    // buffered and is flushed when the connection is dropped, which happens
    // when the server's own idle timeout fires after h3i has gone.
    let mut raw: Vec<u8> = Vec::new();
    let have = testkit::until(Duration::from_secs(30), || {
        match maybe_sqlog(&qlogdir, "server-") {
            Some(r) => {
                let recs = sqlog_records_so_far(&r);
                let ok = recs.iter().any(|e| qlog_is(e, "http:frame_parsed", 0, "headers"))
                    && recs.iter().any(|e| qlog_is(e, "http:frame_created", 0, "headers"));
                raw = r;
                ok
            },
            None => false,
        }
    });
    let log = server.log();
    let alive = server.alive();
    let recs = sqlog_records_so_far(&raw);
    let seen = http_events(&recs);
    let ctx = format!(
        "--- the server's http:* qlog events ---\n{seen}\n\
         --- h3i stderr ---\n{}\n--- server log (still running: {alive}) ---\n{log}",
        run.stderr
    );
    assert!(
        have,
        "{} exited 0, but 30s later the server at 127.0.0.1:{port} had not logged both a \
         request and a response on stream 0. Exiting 0 is not enough on its own: h3i's own \
         main logs a client error and still returns Ok(()), so this is what a replay that \
         reached nobody looks like.\n{ctx}",
        h3i_bin.display()
    );

    // The request.
    let req = recs
        .iter()
        .find(|e| qlog_is(e, "http:frame_parsed", 0, "headers"))
        .expect("checked by the wait above");
    let rh = qlog_headers(req);
    assert_eq!(
        header_of(&rh, ":path").as_deref(),
        Some(REPLAY_PATH),
        "expected the HEADERS frame the server parsed on stream 0 to carry the :path the \
         action file named; observed {:?}.\n{ctx}",
        header_of(&rh, ":path")
    );
    assert_eq!(
        header_of(&rh, ":method").as_deref(),
        Some("GET"),
        "expected :method GET on the replayed request; observed {:?}.\n{ctx}",
        header_of(&rh, ":method")
    );
    assert_eq!(
        header_of(&rh, ":authority").as_deref(),
        Some(REPLAY_AUTHORITY),
        "expected the authority the action file named to reach the server unchanged; observed \
         {:?}. The verifier writes the same string into the action file and into h3i's \
         host:port argument, so nothing here needs --replay-host-override.\n{ctx}",
        header_of(&rh, ":authority")
    );

    // The response.
    let resp = recs
        .iter()
        .find(|e| qlog_is(e, "http:frame_created", 0, "headers"))
        .expect("checked by the wait above");
    let sh = qlog_headers(resp);
    assert_eq!(
        header_of(&sh, ":status").as_deref(),
        Some("200"),
        "expected the server to answer {REPLAY_PATH} with :status 200 on stream 0; observed \
         {:?}.\n{ctx}",
        header_of(&sh, ":status")
    );

    let data = recs.iter().find(|e| qlog_is(e, "http:frame_created", 0, "data"));
    assert!(
        data.is_some(),
        "expected a DATA frame on stream 0 carrying the {} byte file the replayed request \
         asked for; observed no http:frame_created data event on that stream.\n{ctx}",
        want.len()
    );
    assert_eq!(
        qlog_length(data.unwrap()),
        Some(want.len() as u64),
        "expected the DATA frame on stream 0 to be the whole {} byte file; observed a length \
         of {:?}.\n{ctx}",
        want.len(),
        qlog_length(data.unwrap())
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-live-idle-timeout  (T3)
// ---------------------------------------------------------------------------

/// A client and a server config that both declare a 30s idle timeout.
fn idle_configs() -> (quiche::Config, quiche::Config) {
    let mut c = pair::client_config(pair::ALPN_HQ);
    let mut s = pair::server_config(pair::ALPN_HQ);
    c.set_max_idle_timeout(30_000);
    s.set_max_idle_timeout(30_000);
    (c, s)
}

/// `cap-live-idle-timeout`: `Connection::set_max_idle_timeout` overrides what
/// the `Config` said, and the peer sees the override.
///
/// Two pairs that differ in exactly one line. Both configure 30s on both
/// ends; in the first, and only there, the client is told 7s on the
/// *connection*, after `quiche::connect` and before a single datagram exists.
/// Three separate things are pinned by the four numbers that come back. That
/// the server sees 7000 is the feature. That the client's view of the server
/// stays 30000 in the same pair is what stops a candidate from having moved a
/// shared default. That the baseline pair reports 30000 on the same code path
/// is what stops the first row from being satisfied by a connection that
/// ignores its config altogether.
///
/// The value is read through `peer_transport_params()`, which the previous
/// milestone already provided, so the group is about the new setter and not
/// about a new way of looking.
///
/// Nothing here waits for a connection to time out. Idle expiry is a
/// wall-clock observation and has no place in a case that has to be
/// deterministic; what is asserted is the transport parameter that went out
/// on the wire.
#[test]
fn cap_live_idle_timeout() {
    // --- the live call ----------------------------------------------------
    let (mut cc, mut sc) = idle_configs();
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);

    // The return type is load-bearing, so it is bound and propagated rather
    // than discarded: a candidate whose method returns `()` does not compile
    // past this line.
    let set: quiche::Result<()> = p.client.set_max_idle_timeout(7_000);
    set.unwrap_or_else(|e| {
        panic!(
            "expected Connection::set_max_idle_timeout(7000) to succeed on a connection that \
             has not sent a packet yet; observed Err({e:?})"
        )
    });

    p.handshake();

    let server_sees = p.server.peer_transport_params().map(|t| t.max_idle_timeout);
    let client_sees = p.client.peer_transport_params().map(|t| t.max_idle_timeout);

    assert_eq!(
        server_sees,
        Some(7_000),
        "expected the server's view of the client's max_idle_timeout to be the 7000 the live \
         call set, not the 30000 the Config declared; observed {server_sees:?}. The call was \
         made immediately after quiche::connect, so the value had to be re-encoded into the \
         transport parameters before the first flight went out."
    );
    assert_eq!(
        client_sees,
        Some(30_000),
        "expected the client's view of the *server's* max_idle_timeout to be unchanged at \
         30000 in the same pair; observed {client_sees:?}. Setting the local value on one \
         connection must not move the peer's."
    );

    // --- the baseline, same code path, no live call -----------------------
    let (mut cc2, mut sc2) = idle_configs();
    let mut q = pair::Pair::with_configs(&mut cc2, &mut sc2);
    q.handshake();

    let baseline = q.server.peer_transport_params().map(|t| t.max_idle_timeout);
    assert_eq!(
        baseline,
        Some(30_000),
        "expected the server's view of the client's max_idle_timeout to be the 30000 the \
         Config declared when no live call was made; observed {baseline:?}. Without this leg, \
         a connection that ignored its config entirely and always announced 7000 would pass \
         the first assertion."
    );
}

// ---------------------------------------------------------------------------
// cap-unknown-tp-tracking-enabled  (T3)
// ---------------------------------------------------------------------------

/// `cap-unknown-tp-tracking-enabled`: the record a config asks for exists,
/// declares the capacity it asked for, and is absent on the end that did not
/// ask.
///
/// One handshake, both answers. The server's config calls
/// `enable_track_unknown_transport_parameters(128)`; the client's does not.
/// `None` and an empty `Some` are different answers to different questions,
/// and a candidate that returns `Default::default()` for both, or `Some`
/// for both, fails one half or the other.
///
/// The record is allocated while the peer's parameter block is being decoded,
/// from the configuration alone and before any parameter has been examined,
/// which is why its existence and its declared capacity are observable even
/// though neither end of this ladder ever sends an unrecognised transport
/// parameter. A tracked parameter that actually arrived is not observable
/// anywhere in this step: both endpoints are the candidate, and the
/// verifier's support crate has no TLS and so cannot complete a handshake to
/// send one.
#[test]
fn cap_unknown_tp_tracking_enabled() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    sc.enable_track_unknown_transport_parameters(128);

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let on = p.server.peer_transport_params().map(|t| t.unknown_params.clone());
    match &on {
        Some(Some(u)) => {
            assert_eq!(
                u.capacity, 128,
                "expected the server's record of the client's unknown transport parameters to \
                 declare the 128 bytes of capacity its Config asked for; observed {}. The \
                 record is sized from the configuration before decoding begins, so the \
                 capacity is the number that was asked for and not what is left after \
                 anything was stored.",
                u.capacity
            );
            assert!(
                u.parameters.is_empty(),
                "expected the record to be empty: the peer is the candidate's own client and \
                 it sends nothing this stack does not recognise. Observed {} entries: {:?}",
                u.parameters.len(),
                u.parameters
            );
        },
        other => panic!(
            "expected the server, whose Config called \
             enable_track_unknown_transport_parameters(128), to have a record of the client's \
             unknown transport parameters after the handshake; observed {other:?}"
        ),
    }

    let off = p.client.peer_transport_params().map(|t| t.unknown_params.clone());
    assert_eq!(
        off,
        Some(None),
        "expected the client, whose Config did not enable tracking, to have no record at all \
         -- Some(None), a decoded parameter set with nothing tracked in it; observed {off:?}. \
         An empty record and no record are different answers, and only the end that asked for \
         one may have it."
    );
}

// ---------------------------------------------------------------------------
// cap-unknown-tp-capacity  (T3)
// ---------------------------------------------------------------------------

/// `cap-unknown-tp-capacity`: the accounting the public type does, driven
/// directly.
///
/// No connection, no handshake, nothing timed. Both fields of
/// `UnknownTransportParameters` are public, so the verifier builds one with
/// 20 bytes of capacity and pushes into it.
///
/// The third push is the one that matters. The admission test is a strict
/// `<`, so an entry whose charge exactly equals the remaining capacity is
/// refused: a candidate that wrote `<=` passes the first two rows and fails
/// that one. It also shows that the eight bytes charged for the identifier
/// are charged even for an empty value.
///
/// Both refusals must leave `capacity` and `parameters` exactly as they were.
/// A silent drop at this API would be a different contract from the quiet
/// drop the protocol layer performs, so the state is asserted after each
/// refusal and not only the return value.
#[test]
fn cap_unknown_tp_capacity() {
    let mut u = quiche::UnknownTransportParameters { capacity: 20, parameters: vec![] };

    let r1 = u.push(quiche::UnknownTransportParameter { id: 31, value: &b"abcd"[..] });
    assert!(
        r1.is_ok(),
        "expected pushing a 4-byte value into 20 bytes of capacity to be admitted: the charge \
         is 4 + 8 = 12 and 12 < 20. Observed {r1:?}"
    );
    assert_eq!(
        u.capacity, 8,
        "expected the 12 byte charge -- 4 bytes of value plus 8 for the identifier -- to be \
         taken out of the 20 bytes of capacity, leaving 8; observed {}",
        u.capacity
    );
    assert_eq!(
        u.parameters.len(),
        1,
        "expected the admitted entry to have been stored; observed {} entries",
        u.parameters.len()
    );

    let r2 = u.push(quiche::UnknownTransportParameter { id: 62, value: &b"abcd"[..] });
    assert!(
        matches!(r2, Err(quiche::Error::BufferTooShort)),
        "expected a second 12 byte charge against the 8 bytes that are left to be refused with \
         BufferTooShort; observed {r2:?}"
    );
    assert_eq!(
        (u.capacity, u.parameters.len()),
        (8, 1),
        "expected a refused push to leave the record exactly as it was -- 8 bytes of capacity \
         and one entry; observed capacity {} and {} entries",
        u.capacity,
        u.parameters.len()
    );

    let r3 = u.push(quiche::UnknownTransportParameter { id: 63, value: &b""[..] });
    assert!(
        matches!(r3, Err(quiche::Error::BufferTooShort)),
        "expected an empty value to be refused too: it still costs the 8 bytes charged for the \
         identifier, and the admission test is a strict `<`, so 8 < 8 is false. Observed \
         {r3:?}. A candidate that wrote `<=` admits this one."
    );
    assert_eq!(
        (u.capacity, u.parameters.len()),
        (8, 1),
        "expected the second refusal to leave the record alone as well; observed capacity {} \
         and {} entries",
        u.capacity,
        u.parameters.len()
    );

    let seen: Vec<(u64, Vec<u8>)> =
        (&u).into_iter().map(|e| (e.id, e.value.clone())).collect();
    assert_eq!(
        seen,
        vec![(31u64, b"abcd".to_vec())],
        "expected iterating &UnknownTransportParameters to yield the one entry that was \
         admitted, identifier and value intact; observed {seen:?}. A candidate that accounted \
         correctly but stored nothing fails here."
    );
}

// ---------------------------------------------------------------------------
// cap-tp-reserved-ids  (T3)
// ---------------------------------------------------------------------------

/// `cap-tp-reserved-ids`: `UnknownTransportParameter::is_reserved` over a
/// fixed table, and nothing else.
///
/// RFC 9000 Section 18.1 reserves the identifiers `31 * N + 27` for
/// parameters that have no meaning and must always be ignored. Five of the
/// fourteen rows are reserved -- the first four members of the family and one
/// well above where an off-by-one in a loop would still agree -- and the
/// unreserved ones sit immediately on either side of a reserved one, so a
/// predicate that is off by one in either direction fails.
///
/// **Identifiers below 27 are deliberately absent, and this is not an
/// oversight.** The reference computes `(id - 27) / 31` on a `u64`, which
/// underflows for every identifier under 27 and panics outright in the debug
/// profile the verifier builds. A candidate writing the obvious
/// non-underflowing form answers `false` there instead. RFC 9000 defines the
/// reserved set only upward from 27 and says nothing about what lies below,
/// so both readings are defensible and neither is this milestone's
/// capability. The table stops at 27, where the two forms agree on every row.
///
/// The value type is `()`: the predicate is about the identifier alone, and
/// saying so in the type is cheaper than allocating fourteen empty vectors.
#[test]
fn cap_tp_reserved_ids() {
    const TABLE: &[(u64, bool)] = &[
        (27, true),
        (28, false),
        (31, false),
        (57, false),
        (58, true),
        (62, false),
        (88, false),
        (89, true),
        (90, false),
        (119, false),
        (120, true),
        (121, false),
        (1000, false),
        (7963, true),
    ];

    let got: Vec<(u64, bool)> = TABLE
        .iter()
        .map(|(id, _)| {
            (*id, quiche::UnknownTransportParameter { id: *id, value: () }.is_reserved())
        })
        .collect();
    let want: Vec<(u64, bool)> = TABLE.to_vec();

    assert_eq!(
        got,
        want,
        "expected is_reserved to be true for exactly the identifiers 31 * N + 27. The rows \
         that disagree: {:?}",
        got.iter()
            .zip(want.iter())
            .filter(|(g, w)| g != w)
            .map(|((id, g), (_, w))| format!("id {id}: expected {w}, observed {g}"))
            .collect::<Vec<_>>()
    );
}

// ---------------------------------------------------------------------------
// cap-h3-informational  (T3)
// ---------------------------------------------------------------------------

/// `cap-h3-informational`: a response in two header sections, and a client
/// that is refused the same thing.
///
/// The informational section is the stream's *initial* header section, so it
/// goes out through the ordinary `send_response`; the final response is the
/// *additional* one. The client must see both, in order, with nothing between
/// them, and both are asserted by content rather than merely counted -- a
/// candidate that delivered the same section twice, or coalesced the two into
/// one, fails on the `:status` values.
///
/// The refusal leg is the other half. A request cannot contain an
/// informational response, and the reference rejects a client's
/// `is_trailer_section: false` before it looks at the stream at all. It is
/// checked here on a stream whose initial HEADERS has already been sent, so
/// the refusal is about the direction and not about the state; the
/// state-machine refusals belong to `cap-h3-trailers`.
///
/// Nothing is asserted about what a 1xx status code means, about
/// `Retry-After`, or about Early Hints semantics. The contract is positional.
#[test]
fn cap_h3_informational() {
    let mut h = pair::H3Pair::new();
    let req = get_request(b"/informational");

    let sid = h
        .client
        .send_request(&mut h.t.client, &req, false)
        .expect("h3::Connection::send_request on the client");

    // A client may not send an informational section, and this is not about
    // stream state: the initial HEADERS has already gone out.
    let refused = h.client.send_additional_headers(
        &mut h.t.client,
        sid,
        &[quiche::h3::Header::new(b":status", b"103")],
        false,
        false,
    );
    assert!(
        matches!(refused, Err(quiche::h3::Error::FrameUnexpected)),
        "expected a client calling send_additional_headers with is_trailer_section: false to \
         be refused with FrameUnexpected -- informational sections are a response construct \
         and a request cannot contain one; observed {refused:?}. The stream had already had \
         its initial HEADERS sent, so this is about the direction, not the state."
    );

    h.settle();

    let got_server = drain_server(&mut h);
    assert!(
        matches!(got_server.first(), Some(Seen::Headers(s, _, _)) if *s == sid),
        "expected the server to see the request on stream {sid} before anything else; \
         observed:\n{}",
        shape(&got_server)
    );

    h.server
        .send_response(
            &mut h.t.server,
            sid,
            &[
                quiche::h3::Header::new(b":status", b"103"),
                quiche::h3::Header::new(b"link", b"</s.css>"),
            ],
            false,
        )
        .expect("h3::Connection::send_response with the 103 informational section");
    h.server
        .send_additional_headers(
            &mut h.t.server,
            sid,
            &[
                quiche::h3::Header::new(b":status", b"200"),
                quiche::h3::Header::new(b"content-length", b"5"),
            ],
            false,
            false,
        )
        .expect("h3::Connection::send_additional_headers with the final response");
    h.server
        .send_body(&mut h.t.server, sid, b"hello", true)
        .expect("h3::Connection::send_body");

    h.settle();

    let got = drain_client(&mut h);
    let want = vec![
        Seen::Headers(
            sid,
            vec![
                (":status".into(), "103".into()),
                ("link".into(), "</s.css>".into()),
            ],
            true,
        ),
        Seen::Headers(
            sid,
            vec![
                (":status".into(), "200".into()),
                ("content-length".into(), "5".into()),
            ],
            true,
        ),
        Seen::Data(sid, b"hello".to_vec()),
        Seen::Finished(sid),
    ];
    assert_eq!(
        got,
        want,
        "expected the client to see the informational section, then the final response, then \
         the body, then the end of the stream, with nothing between them.\n\
         --- expected ---\n{}\n--- observed ---\n{}",
        shape(&want),
        shape(&got)
    );
}

// ---------------------------------------------------------------------------
// cap-h3-trailers  (T3)
// ---------------------------------------------------------------------------

/// `cap-h3-trailers`: a trailer section after a body, and the three ways the
/// same method must refuse.
///
/// The positive leg turns on `more_frames`: true on the response's own
/// header section, false on the trailer, because the trailer closed the
/// stream. That transition is how a receiver knows the trailer was a trailer,
/// so it is asserted rather than assumed.
///
/// The three refusals are all `FrameUnexpected`, and each is a different
/// reason: a stream that has not had its initial header section sent, a
/// stream that has already had its trailer, and a stream the connection has
/// never seen. The last must be the same error and not a panic or a
/// different one.
///
/// A client *is* allowed to send trailers, and that asymmetry is why this
/// group and `cap-h3-informational` are separate: `is_trailer_section: true`
/// is the one combination a client may use, and `false` is the one it may not.
#[test]
fn cap_h3_trailers() {
    let mut h = pair::H3Pair::new();
    let req = get_request(b"/trailers");
    let body = b"trailers-body";

    let sid = h
        .client
        .send_request(&mut h.t.client, &req, true)
        .expect("h3::Connection::send_request on the client");
    h.settle();

    let got_server = drain_server(&mut h);
    assert!(
        matches!(got_server.first(), Some(Seen::Headers(s, _, _)) if *s == sid),
        "expected the server to see the request on stream {sid}; observed:\n{}",
        shape(&got_server)
    );

    // 1. Before the initial header section. The stream exists on the server
    //    -- the request arrived on it -- but nothing has been sent on it yet.
    let before = h.server.send_additional_headers(
        &mut h.t.server,
        sid,
        &[quiche::h3::Header::new(b"x-checksum", b"deadbeef")],
        true,
        false,
    );
    assert!(
        matches!(before, Err(quiche::h3::Error::FrameUnexpected)),
        "expected send_additional_headers on a stream whose initial header section has not \
         been sent to be refused with FrameUnexpected; observed {before:?}. The server has \
         received the request on stream {sid} but has not called send_response, so there is \
         nothing for a trailer to trail."
    );

    // 2. An unknown stream id. Never opened, never seen.
    let unknown = h.server.send_additional_headers(
        &mut h.t.server,
        400,
        &[quiche::h3::Header::new(b"x-checksum", b"deadbeef")],
        true,
        false,
    );
    assert!(
        matches!(unknown, Err(quiche::h3::Error::FrameUnexpected)),
        "expected send_additional_headers on stream 400, which this connection has never \
         seen, to be refused with the same FrameUnexpected as a stream in the wrong state -- \
         not a panic and not a different error; observed {unknown:?}"
    );

    // The positive leg.
    h.server
        .send_response(
            &mut h.t.server,
            sid,
            &[
                quiche::h3::Header::new(b":status", b"200"),
                quiche::h3::Header::new(b"content-length", b"13"),
            ],
            false,
        )
        .expect("h3::Connection::send_response");
    h.server
        .send_body(&mut h.t.server, sid, body, false)
        .expect("h3::Connection::send_body without fin, so a trailer can follow");
    h.server
        .send_additional_headers(
            &mut h.t.server,
            sid,
            &[quiche::h3::Header::new(b"x-checksum", b"deadbeef")],
            true,
            true,
        )
        .expect("h3::Connection::send_additional_headers with the trailer section");

    // 3. A second trailer section on the same stream.
    let again = h.server.send_additional_headers(
        &mut h.t.server,
        sid,
        &[quiche::h3::Header::new(b"x-checksum", b"cafebabe")],
        true,
        true,
    );
    assert!(
        matches!(again, Err(quiche::h3::Error::FrameUnexpected)),
        "expected a second trailer section on stream {sid} to be refused with \
         FrameUnexpected: once a trailer has gone out the stream is finished with header \
         sections. Observed {again:?}"
    );

    h.settle();

    let got = drain_client(&mut h);
    let want = vec![
        Seen::Headers(
            sid,
            vec![
                (":status".into(), "200".into()),
                ("content-length".into(), "13".into()),
            ],
            true,
        ),
        Seen::Data(sid, body.to_vec()),
        Seen::Headers(
            sid,
            vec![("x-checksum".into(), "deadbeef".into())],
            false,
        ),
        Seen::Finished(sid),
    ];
    assert_eq!(
        got,
        want,
        "expected the client to see the response, the body byte for byte, then the trailer \
         section with more_frames false because it closed the stream, then the end of the \
         stream.\n--- expected ---\n{}\n--- observed ---\n{}",
        shape(&want),
        shape(&got)
    );
}

// ---------------------------------------------------------------------------
// cap-h3-stats  (T2/T3)
// ---------------------------------------------------------------------------

/// Every client-initiated unidirectional stream the client opened, as
/// `(stream_id, first byte)`, read off the wire.
///
/// T2: the client's 1-RTT datagrams are decrypted with keys the verifier
/// derived from `CLIENT_TRAFFIC_SECRET_0` in the client's own keylog, their
/// STREAM frames are decoded by the support crate, and every
/// client-initiated unidirectional stream (`stream_id % 4 == 2`) that carries
/// `offset == 0` gives up its HTTP/3 stream type byte.
fn client_uni_stream_types(h: &mut pair::H3Pair, keys: &support::PacketKeys) -> Vec<(u64, u8)> {
    let mut largest = 0u64;
    let mut found: Vec<(u64, u8)> = Vec::new();
    for d in h.t.client_out.iter() {
        let opened = match pair::open_1rtt(d, pair::SERVER_CID.len(), keys, Some(largest)) {
            Some(o) => o,
            None => continue,
        };
        largest = opened.packet_number;
        for f in pair::decoded_frames(&opened.payload) {
            if let support::Frame::Stream { stream_id, offset, data, .. } = f {
                if stream_id % 4 == 2 && offset == 0 && !data.is_empty() {
                    found.push((stream_id, data[0]));
                }
            }
        }
    }
    found
}

/// `cap-h3-stats`: the two QPACK byte counters, driven by bytes the verifier
/// writes onto the candidate's own QPACK streams.
///
/// An ordinary request and response leaves both counters at zero -- this
/// stack's QPACK encoder emits only literal field lines, so no instruction
/// ever reaches the peer's encoder stream -- and a group that read them after
/// a GET would assert `0 == 0` and pass on a `Default::default()`. So the
/// verifier writes the bytes itself, which is what the reference's own unit
/// test for these counters does.
///
/// Finding the streams is T2 and deliberately makes no assumption: the type
/// byte is read off the wire rather than the ids hard-coded, so a candidate
/// that opens its unidirectional streams in a different order still passes.
/// What it may not do is fail to open them.
///
/// The client's own counters staying at zero throughout is what makes the
/// reading per-connection and per-direction rather than a connection-wide
/// total; the second write is what makes it cumulative rather than a
/// high-water mark or a last-read size. Both endpoints must still be open
/// afterwards: QPACK instructions on a stack with no dynamic table are
/// discarded, and discarding them must not close a critical stream.
///
/// The byte counts are safe to assert exactly because the verifier chose
/// them. Nothing here asserts on bytes the *candidate's* encoder wrote, which
/// is an encoder policy and not this step's contract.
#[test]
fn cap_h3_stats() {
    let mut cc = pair::client_config(pair::ALPN_H3);
    let mut sc = pair::server_config(pair::ALPN_H3);
    cc.log_keys();

    let (t, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    let mut h = pair::H3Pair::over(t);

    let log = keylog.contents();
    let secret = pair::keylog_secret(&log, "CLIENT_TRAFFIC_SECRET_0").unwrap_or_else(|| {
        panic!(
            "the client wrote no CLIENT_TRAFFIC_SECRET_0 into the keylog its Config was given, \
             so the verifier cannot read the client's own datagrams back off the wire. The \
             keylog held {} bytes.",
            log.len()
        )
    });
    let keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret)
        .expect("deriving generation-0 1-RTT keys from CLIENT_TRAFFIC_SECRET_0");

    let found = client_uni_stream_types(&mut h, &keys);
    let enc = found.iter().find(|(_, ty)| *ty == 0x02).map(|(id, _)| *id);
    let dec = found.iter().find(|(_, ty)| *ty == 0x03).map(|(id, _)| *id);

    let enc = enc.unwrap_or_else(|| {
        panic!(
            "expected the client to have opened a QPACK encoder stream -- a client-initiated \
             unidirectional stream whose first byte is the HTTP/3 stream type 0x02. The \
             client-initiated unidirectional streams seen on the wire, as (stream_id, type \
             byte), were {found:?}"
        )
    });
    let dec = dec.unwrap_or_else(|| {
        panic!(
            "expected the client to have opened a QPACK decoder stream -- type byte 0x03. The \
             client-initiated unidirectional streams seen on the wire, as (stream_id, type \
             byte), were {found:?}"
        )
    });
    assert_ne!(
        enc, dec,
        "expected the QPACK encoder and decoder streams to be two different streams; both \
         read as {enc}. Streams seen: {found:?}"
    );

    // Twenty bytes on the encoder stream, twelve on the decoder stream.
    h.t.client
        .stream_send(enc, &[0u8; 20], false)
        .unwrap_or_else(|e| panic!("writing 20 bytes onto the client's QPACK encoder stream {enc}: {e:?}"));
    h.t.client
        .stream_send(dec, &[0u8; 12], false)
        .unwrap_or_else(|e| panic!("writing 12 bytes onto the client's QPACK decoder stream {dec}: {e:?}"));
    h.settle();
    let after_first = drain_server(&mut h);

    let s = h.server.stats();
    assert_eq!(
        (s.qpack_encoder_stream_recv_bytes, s.qpack_decoder_stream_recv_bytes),
        (20, 12),
        "expected the server to have counted the 20 bytes the verifier wrote on the client's \
         QPACK encoder stream ({enc}) and the 12 on its decoder stream ({dec}); observed \
         encoder {} and decoder {}. Server events while the bytes crossed:\n{}",
        s.qpack_encoder_stream_recv_bytes,
        s.qpack_decoder_stream_recv_bytes,
        shape(&after_first)
    );

    let c = h.client.stats();
    assert_eq!(
        (c.qpack_encoder_stream_recv_bytes, c.qpack_decoder_stream_recv_bytes),
        (0, 0),
        "expected the *client's* counters to stay at zero: it received nothing on either \
         QPACK stream, and the reading is per-connection and per-direction rather than a \
         connection-wide total. Observed encoder {} and decoder {}.",
        c.qpack_encoder_stream_recv_bytes,
        c.qpack_decoder_stream_recv_bytes
    );

    // Five more on the encoder stream: cumulative on the one that moved,
    // untouched on the one that did not.
    h.t.client
        .stream_send(enc, &[0u8; 5], false)
        .unwrap_or_else(|e| panic!("writing 5 more bytes onto the QPACK encoder stream {enc}: {e:?}"));
    h.settle();
    let after_second = drain_server(&mut h);

    let s2 = h.server.stats();
    assert_eq!(
        (s2.qpack_encoder_stream_recv_bytes, s2.qpack_decoder_stream_recv_bytes),
        (25, 12),
        "expected the encoder counter to accumulate to 25 after five more bytes and the \
         decoder counter to stay at 12; observed encoder {} and decoder {}. A high-water mark \
         or a last-read size would read 20 and 12, or 5 and 12. Server events while the bytes \
         crossed:\n{}",
        s2.qpack_encoder_stream_recv_bytes,
        s2.qpack_decoder_stream_recv_bytes,
        shape(&after_second)
    );

    assert!(
        !h.t.client.is_closed() && !h.t.server.is_closed(),
        "expected both ends to still be open after the QPACK instructions were discarded: \
         this stack has no dynamic table, so the bytes go nowhere, but a QPACK stream is a \
         critical stream and discarding what arrives on one must not close the connection. \
         Observed client closed: {}, server closed: {}. Client error: {:?}. Server error: \
         {:?}.",
        h.t.client.is_closed(),
        h.t.server.is_closed(),
        h.t.client.peer_error(),
        h.t.server.peer_error()
    );
}
