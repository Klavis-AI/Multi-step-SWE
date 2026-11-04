// This program does benchmarking of the functions in verify.rs,
// that do certificate chain validation and signature verification.
//
// Note: we don't use any of the standard 'cargo bench', 'test::Bencher',
// etc. because it's unstable at the time of writing.

use std::time::{Duration, Instant};

use crate::anchors;
use crate::error::TLSError;
use crate::key;
use crate::verify;
use crate::verify::ServerCertVerifier;
use webpki;

use webpki_roots;

fn duration_nanos(d: Duration) -> u64 {
    unimplemented!()
}

fn bench<Fsetup, Ftest, S>(count: usize, name: &'static str, f_setup: Fsetup, f_test: Ftest)
where
    Fsetup: Fn() -> S,
    Ftest: Fn(S),
{
    unimplemented!()
}

fn fixed_time() -> Result<webpki::Time, TLSError> {
    unimplemented!()
}

static V: &'static verify::WebPKIVerifier = &verify::WebPKIVerifier { time: fixed_time };

#[test]
fn test_reddit_cert() {
    unimplemented!()
}

#[test]
fn test_github_cert() {
    unimplemented!()
}

#[test]
fn test_arstechnica_cert() {
    unimplemented!()
}

#[test]
fn test_servo_cert() {
    unimplemented!()
}

#[test]
fn test_twitter_cert() {
    unimplemented!()
}

#[test]
fn test_wikipedia_cert() {
    unimplemented!()
}

#[test]
fn test_google_cert() {
    unimplemented!()
}

#[test]
fn test_hn_cert() {
    unimplemented!()
}

#[test]
fn test_stackoverflow_cert() {
    unimplemented!()
}

#[test]
fn test_duckduckgo_cert() {
    unimplemented!()
}

#[test]
fn test_rustlang_cert() {
    unimplemented!()
}

#[test]
fn test_wapo_cert() {
    unimplemented!()
}
