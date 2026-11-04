use ring::digest;
use ring::hmac;

use std::io::Write;

fn convert_digest_to_hmac_alg(hash: &'static digest::Algorithm) -> hmac::Algorithm {
    unimplemented!()
}

fn concat_sign(key: &hmac::Key, a: &[u8], b: &[u8]) -> hmac::Tag {
    unimplemented!()
}

fn p(out: &mut [u8], hashalg: &'static digest::Algorithm, secret: &[u8], seed: &[u8]) {
    unimplemented!()
}

fn concat(a: &[u8], b: &[u8]) -> Vec<u8> {
    unimplemented!()
}

pub fn prf(
    out: &mut [u8],
    hashalg: &'static digest::Algorithm,
    secret: &[u8],
    label: &[u8],
    seed: &[u8],
) {
    unimplemented!()
}

#[cfg(test)]
mod tests {
    use ring::digest::{SHA256, SHA512};

    #[test]
    fn check_sha256() {
        unimplemented!()
    }

    #[test]
    fn check_sha512() {
        unimplemented!()
    }
}
