use crate::rand;
use crate::server::ProducesTickets;

use ring::aead;
use std::mem;
use std::sync::{Arc, Mutex};
use std::time;

/// The timebase for expiring and rolling tickets and ticketing
/// keys.  This is UNIX wall time in seconds.
pub fn timebase() -> u64 {
    unimplemented!()
}

/// This is a `ProducesTickets` implementation which uses
/// any *ring* `aead::Algorithm` to encrypt and authentication
/// the ticket payload.  It does not enforce any lifetime
/// constraint.
pub struct AEADTicketer {
    alg: &'static aead::Algorithm,
    key: aead::LessSafeKey,
    lifetime: u32,
}

impl AEADTicketer {
    /// Make a new `AEADTicketer` using the given `alg`, `key` material
    /// and advertised `lifetime_seconds`.  Note that `lifetime_seconds`
    /// does not affect the lifetime of the key.  `key` must be the
    /// right length for `alg` or this will panic.
    pub fn new_custom(
        alg: &'static aead::Algorithm,
        key: &[u8],
        lifetime_seconds: u32,
    ) -> AEADTicketer {
        unimplemented!()
    }

    /// Make a ticketer with recommended configuration and a random key.
    pub fn new() -> AEADTicketer {
        unimplemented!()
    }
}

impl ProducesTickets for AEADTicketer {
    fn enabled(&self) -> bool {
        unimplemented!()
    }
    fn get_lifetime(&self) -> u32 {
        unimplemented!()
    }

    /// Encrypt `message` and return the ciphertext.
    fn encrypt(&self, message: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }

    /// Decrypt `ciphertext` and recover the original message.
    fn decrypt(&self, ciphertext: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
}

struct TicketSwitcherState {
    current: Box<dyn ProducesTickets>,
    previous: Option<Box<dyn ProducesTickets>>,
    next_switch_time: u64,
}

/// A ticketer that has a 'current' sub-ticketer and a single
/// 'previous' ticketer.  It creates a new ticketer every so
/// often, demoting the current ticketer.
pub struct TicketSwitcher {
    generator: fn() -> Box<dyn ProducesTickets>,
    lifetime: u32,
    state: Mutex<TicketSwitcherState>,
}

impl TicketSwitcher {
    /// `lifetime` is in seconds, and is how long the current ticketer
    /// is used to generate new tickets.  Tickets are accepted for no
    /// longer than twice this duration.  `generator` produces a new
    /// `ProducesTickets` implementation.
    pub fn new(lifetime: u32, generator: fn() -> Box<dyn ProducesTickets>) -> TicketSwitcher {
        unimplemented!()
    }

    /// If it's time, demote the `current` ticketer to `previous` (so it
    /// does no new encryptions but can do decryptions) and make a fresh
    /// `current` ticketer.
    ///
    /// Calling this regularly will ensure timely key erasure.  Otherwise,
    /// key erasure will be delayed until the next encrypt/decrypt call.
    pub fn maybe_roll(&self) {
        unimplemented!()
    }
}

impl ProducesTickets for TicketSwitcher {
    fn get_lifetime(&self) -> u32 {
        unimplemented!()
    }

    fn enabled(&self) -> bool {
        unimplemented!()
    }

    fn encrypt(&self, message: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }

    fn decrypt(&self, ciphertext: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
}

/// A concrete, safe ticket creation mechanism.
pub struct Ticketer {}

fn generate_inner() -> Box<dyn ProducesTickets> {
    unimplemented!()
}

impl Ticketer {
    /// Make the recommended Ticketer.  This produces tickets
    /// with a 12 hour life and randomly generated keys.
    ///
    /// The encryption mechanism used in Chacha20Poly1305.
    pub fn new() -> Arc<dyn ProducesTickets> {
        unimplemented!()
    }
}

#[test]
fn basic_pairwise_test() {
    unimplemented!()
}
