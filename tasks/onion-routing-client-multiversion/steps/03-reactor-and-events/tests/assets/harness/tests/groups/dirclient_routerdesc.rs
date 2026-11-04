//! Group `dirclient_routerdesc` (gate, inherited) - router-descriptor requests.
//!
//! instruction.md section 1: "router-descriptor requests in `tor-dirclient`"
//! are among the surfaces that moved behind a cargo feature at this
//! checkpoint. What moved is the gating, not the behaviour: `RouterDescRequest`
//! and every item below already exist, ungated, at step 02, and this step only
//! puts them behind `tor-dirclient`'s `routerdesc` feature - which the driver
//! requests, so the surface under test is identical either side of the move.
//!
//! That makes this a GATE group rather than a scored one. Nothing here is new
//! capability, so it must earn nothing; it was scored `peripheral 4` until a
//! no-op continuation passed all four cases against step 02's tree and
//! collected the credit (A69). It vetoes instead: these requests must keep
//! building the same URLs after the feature move as before it.
//!
//! Graded items: `tor_dirclient::request::RouterDescRequest::{all, new, push,
//! digests}`, its `Default` and `FromIterator<[u8; 20]>` implementations, and
//! its `tor_dirclient::request::Requestable` implementation.
//!
//! The URL shapes come from dir-spec (see SPEC_NOTES.md): descriptors by digest
//! are fetched from `/tor/server/d/<F1>+<F2>+...`, all of them from
//! `/tor/server/all`, and the `.z` suffix asks for a compressed reply.

use tor_dirclient::request::{Requestable, RouterDescRequest};

/// Twenty-byte descriptor digests, chosen so that their sort order is not
/// their insertion order.
const D1: [u8; 20] = [0x11; 20];
const D2: [u8; 20] = [0x22; 20];

#[test]
fn dirclient_routerdesc__request_for_all_descriptors() {
    let req = RouterDescRequest::all();
    let http = req.make_request().expect("make_request failed");
    assert_eq!(http.method(), "GET");
    assert_eq!(http.uri(), "/tor/server/all.z");
    assert!(
        req.partial_docs_ok(),
        "a request for every descriptor can be answered partially"
    );
    assert_eq!(req.max_response_len(), 64 * 1024 * 1024);

    let enc = http
        .headers()
        .get(http::header::ACCEPT_ENCODING)
        .expect("no accept-encoding header")
        .to_str()
        .expect("accept-encoding is not ascii");
    assert!(
        enc.starts_with("deflate, identity"),
        "unexpected accept-encoding {:?}",
        enc
    );
}

#[test]
fn dirclient_routerdesc__request_for_one_descriptor() {
    let mut req = RouterDescRequest::new();
    req.push(D1);
    let http = req.make_request().expect("make_request failed");
    assert_eq!(http.method(), "GET");
    assert_eq!(
        http.uri(),
        "/tor/server/d/1111111111111111111111111111111111111111.z"
    );
    assert!(
        !req.partial_docs_ok(),
        "a request for a single descriptor must not accept a partial answer"
    );
    assert_eq!(req.max_response_len(), 8 * 1024);
}

#[test]
fn dirclient_routerdesc__digests_are_sorted_and_joined() {
    // Pushed in descending order; the URL must list them in ascending order.
    let mut req = RouterDescRequest::new();
    req.push(D2);
    req.push(D1);
    let http = req.make_request().expect("make_request failed");
    assert_eq!(
        http.uri(),
        "/tor/server/d/\
         1111111111111111111111111111111111111111+\
         2222222222222222222222222222222222222222.z"
    );
    assert!(req.partial_docs_ok());
    assert_eq!(req.max_response_len(), 16 * 1024);
}

#[test]
fn dirclient_routerdesc__empty_default_and_from_iterator() {
    // `new` and `default` agree, and neither asks for anything yet.
    let empty = RouterDescRequest::default();
    assert_eq!(empty.digests().count(), 0);
    assert!(!empty.partial_docs_ok());
    assert_eq!(empty.max_response_len(), 0);

    // Collecting digests keeps them in the order they arrived.
    let collected: RouterDescRequest = vec![D2, D1].into_iter().collect();
    let got: Vec<[u8; 20]> = collected.digests().copied().collect();
    assert_eq!(got, vec![D2, D1]);

    // A request for everything ignores individual digests.
    let mut all = RouterDescRequest::all();
    all.push(D1);
    assert_eq!(all.digests().count(), 0);
    assert_eq!(
        all.make_request().expect("make_request failed").uri(),
        "/tor/server/all.z"
    );
}
