//! Group `dirreq`: the HTTP requests the directory client issues over a
//! circuit.

use std::time::{Duration, SystemTime};

use tor_dirclient::request::{
    AuthCertRequest, ConsensusRequest, MicrodescRequest, Requestable, RouterDescRequest,
};
use tor_llcrypto::pk::rsa::RsaIdentity;
use tor_netdoc::doc::authcert::AuthCertKeyIds;
use tor_netdoc::doc::netstatus::ConsensusFlavor;

/// Build the request and return its method and path.
fn line<R: Requestable>(r: &R) -> (String, String) {
    let req = r.make_request().expect("request should be well-formed");
    (req.method().to_string(), req.uri().to_string())
}

/// Look up one header of a built request.
fn header<R: Requestable>(r: &R, name: &str) -> Option<String> {
    let req = r.make_request().expect("request should be well-formed");
    req.headers()
        .get(name)
        .map(|v| v.to_str().unwrap().to_string())
}

fn rsa(b: u8) -> RsaIdentity {
    RsaIdentity::from_bytes(&[b; 20]).unwrap()
}

#[test]
fn dirreq__consensus_requests_name_the_flavour_in_the_path() {
    let (method, uri) = line(&ConsensusRequest::new(ConsensusFlavor::Microdesc));
    assert_eq!(method, "GET");
    assert_eq!(uri, "/tor/status-vote/current/consensus-microdesc.z");

    // The unflavoured consensus has no suffix.
    let (_, uri) = line(&ConsensusRequest::new(ConsensusFlavor::Ns));
    assert_eq!(uri, "/tor/status-vote/current/consensus.z");

    // A microdescriptor consensus is what a client asks for by default.
    let (_, uri) = line(&ConsensusRequest::default());
    assert_eq!(uri, "/tor/status-vote/current/consensus-microdesc.z");

    // Every request says which encodings the client can decode.
    let enc = header(&ConsensusRequest::default(), "accept-encoding")
        .expect("accept-encoding header");
    assert!(
        enc.starts_with("deflate, identity"),
        "unexpected accept-encoding: {}",
        enc
    );
    assert!(!ConsensusRequest::default().partial_docs_ok());
}

#[test]
fn dirreq__consensus_requests_list_authority_identities_in_sorted_order() {
    let mut r = ConsensusRequest::new(ConsensusFlavor::Microdesc);
    r.push_authority_id(rsa(0xCC));
    r.push_authority_id(rsa(0x22));
    assert_eq!(r.authority_ids().count(), 2);

    let (_, uri) = line(&r);
    let cc = "cc".repeat(20);
    let two = "22".repeat(20);
    assert_eq!(
        uri,
        format!("/tor/status-vote/current/consensus-microdesc/{}+{}.z", two, cc)
    );
}

#[test]
fn dirreq__consensus_requests_carry_an_if_modified_since_and_diff_header() {
    let mut r = ConsensusRequest::new(ConsensusFlavor::Microdesc);
    assert_eq!(r.last_consensus_date(), None);
    assert_eq!(header(&r, "if-modified-since"), None);
    assert_eq!(header(&r, "x-or-diff-from-consensus"), None);

    let when = SystemTime::UNIX_EPOCH + Duration::from_secs(1_624_492_800);
    r.set_last_consensus_date(when);
    assert_eq!(r.last_consensus_date(), Some(when));
    assert_eq!(
        header(&r, "if-modified-since").as_deref(),
        Some("Thu, 24 Jun 2021 00:00:00 GMT")
    );

    r.push_old_consensus_digest([0xEE; 32]);
    r.push_old_consensus_digest([0x11; 32]);
    assert_eq!(r.old_consensus_digests().count(), 2);
    assert_eq!(
        header(&r, "x-or-diff-from-consensus").as_deref(),
        Some(&format!("{}, {}", "11".repeat(32), "ee".repeat(32))[..])
    );
    // The digests travel in a header, not in the path.
    let (_, uri) = line(&r);
    assert_eq!(uri, "/tor/status-vote/current/consensus-microdesc.z");
}

#[test]
fn dirreq__microdescriptor_requests_are_sorted_base64_digests() {
    let mut r = MicrodescRequest::new();
    let mut d1 = [0u8; 32];
    d1[0] = 0xFF;
    let d2 = [0x01u8; 32];
    r.push(d1);
    r.push(d2);
    assert_eq!(r.digests().count(), 2);

    let b64 = |d: &[u8; 32]| base64::encode_config(d, base64::STANDARD_NO_PAD);
    let (method, uri) = line(&r);
    assert_eq!(method, "GET");
    assert_eq!(uri, format!("/tor/micro/d/{}-{}.z", b64(&d2), b64(&d1)));

    // Several documents in one request: a partial answer is still useful.
    assert!(r.partial_docs_ok());
    assert!(!MicrodescRequest::new().partial_docs_ok());
    assert_eq!(r.max_response_len(), 2 * 8 * 1024);
}

#[test]
fn dirreq__authority_certificate_requests_pair_identity_and_signing_keys() {
    let mut r = AuthCertRequest::new();
    r.push(AuthCertKeyIds {
        id_fingerprint: rsa(0xBB),
        sk_fingerprint: rsa(0x01),
    });
    r.push(AuthCertKeyIds {
        id_fingerprint: rsa(0x0A),
        sk_fingerprint: rsa(0x02),
    });
    assert_eq!(r.keys().count(), 2);

    let (method, uri) = line(&r);
    assert_eq!(method, "GET");
    assert_eq!(
        uri,
        format!(
            "/tor/keys/fp-sk/{}-{}+{}-{}.z",
            "0a".repeat(20),
            "02".repeat(20),
            "bb".repeat(20),
            "01".repeat(20)
        )
    );
    assert!(r.partial_docs_ok());
    assert_eq!(r.max_response_len(), 2 * 16 * 1024);
}

#[test]
fn dirreq__router_descriptor_requests_use_hex_digests_or_ask_for_all() {
    let mut r = RouterDescRequest::new();
    r.push([0x99; 20]);
    r.push([0x0F; 20]);
    let (_, uri) = line(&r);
    assert_eq!(
        uri,
        format!("/tor/server/d/{}+{}.z", "0f".repeat(20), "99".repeat(20))
    );

    let mut all = RouterDescRequest::all();
    // Once we are asking for everything, single digests add nothing.
    all.push([0x99; 20]);
    assert_eq!(all.digests().count(), 0);
    let (method, uri) = line(&all);
    assert_eq!(method, "GET");
    assert_eq!(uri, "/tor/server/all.z");
    assert!(all.partial_docs_ok());
}
