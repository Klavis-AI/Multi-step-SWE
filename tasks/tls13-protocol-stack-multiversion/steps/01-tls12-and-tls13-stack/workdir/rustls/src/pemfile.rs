use crate::key;
use base64;
use std::io;

/// Extract and decode all PEM sections from `rd`, which begin with `start_mark`
/// and end with `end_mark`.  Apply the functor `f` to each decoded buffer,
/// and return a Vec of `f`'s return values.
fn extract<A>(
    rd: &mut dyn io::BufRead,
    start_mark: &str,
    end_mark: &str,
    f: &dyn Fn(Vec<u8>) -> A,
) -> Result<Vec<A>, ()> {
    unimplemented!()
}

/// Extract all the certificates from rd, and return a vec of `key::Certificate`s
/// containing the der-format contents.
pub fn certs(rd: &mut dyn io::BufRead) -> Result<Vec<key::Certificate>, ()> {
    unimplemented!()
}

/// Extract all RSA private keys from rd, and return a vec of `key::PrivateKey`s
/// containing the der-format contents.
pub fn rsa_private_keys(rd: &mut dyn io::BufRead) -> Result<Vec<key::PrivateKey>, ()> {
    unimplemented!()
}

/// Extract all PKCS8-encoded private keys from rd, and return a vec of
/// `key::PrivateKey`s containing the der-format contents.
pub fn pkcs8_private_keys(rd: &mut dyn io::BufRead) -> Result<Vec<key::PrivateKey>, ()> {
    unimplemented!()
}
