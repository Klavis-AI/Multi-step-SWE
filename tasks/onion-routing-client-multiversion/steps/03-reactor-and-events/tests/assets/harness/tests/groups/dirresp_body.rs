//! Group `dirresp_body` (peripheral) - constructing a directory response.
//!
//! instruction.md section 1: a directory reply "is handed back as a
//! `DirResponse`, which can now also be constructed directly from a body".
//! A body is what a directory stream produces once its bytes have been read
//! back, and `from_body` is the entry point that wraps one.
//!
//! Graded items: `tor_dirclient::DirResponse::{from_body,
//! status_code, is_partial, error, output, into_output, source}`.

use tor_dirclient::DirResponse;

#[test]
fn dirresp_body__from_body_is_a_complete_success() {
    let r = DirResponse::from_body(b"router example 127.0.0.1 9001 0 0\n");
    assert_eq!(r.status_code(), 200);
    assert_eq!(r.output(), b"router example 127.0.0.1 9001 0 0\n");
    assert_eq!(r.is_partial(), false);
    assert!(r.error().is_none());
    assert!(r.source().is_none());
}

#[test]
fn dirresp_body__into_output_yields_the_same_bytes() {
    let body = vec![0u8, 1, 2, 250, 251, 255];
    let r = DirResponse::from_body(&body);
    assert_eq!(r.output(), &body[..]);
    assert_eq!(r.into_output(), body);
}

#[test]
fn dirresp_body__accepts_any_byte_source() {
    // `impl AsRef<[u8]>` covers the shapes a caller is likely to have.
    let from_str = DirResponse::from_body("hello");
    let from_string = DirResponse::from_body("hello".to_string());
    let from_slice = DirResponse::from_body(&b"hello"[..]);
    let from_vec = DirResponse::from_body(b"hello".to_vec());
    for r in [&from_str, &from_string, &from_slice, &from_vec] {
        assert_eq!(r.output(), b"hello");
        assert_eq!(r.status_code(), 200);
    }

    let empty = DirResponse::from_body(b"");
    assert_eq!(empty.output(), b"");
    assert_eq!(empty.is_partial(), false);
    assert_eq!(empty.into_output(), Vec::<u8>::new());
}
