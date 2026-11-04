use crate::msgs::codec;
/// The single place where we generate random material
/// for our own use.  These functions never fail,
/// they panic on error.
use ring::rand::{SecureRandom, SystemRandom};

/// Fill the whole slice with random material.
pub fn fill_random(bytes: &mut [u8]) {
    unimplemented!()
}

/// Make a Vec<u8> of the given size
/// containing random material.
pub fn random_vec(len: usize) -> Vec<u8> {
    unimplemented!()
}

/// Return a uniformly random u32.
pub fn random_u32() -> u32 {
    unimplemented!()
}
