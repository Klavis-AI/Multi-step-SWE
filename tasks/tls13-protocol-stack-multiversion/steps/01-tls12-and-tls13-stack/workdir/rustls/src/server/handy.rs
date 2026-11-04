use crate::error::TLSError;
use crate::key;
use crate::server;
use crate::server::ClientHello;
use crate::sign;
use webpki;

use std::collections;
use std::sync::{Arc, Mutex};

/// Something which never stores sessions.
pub struct NoServerSessionStorage {}

impl server::StoresServerSessions for NoServerSessionStorage {
    fn put(&self, _id: Vec<u8>, _sec: Vec<u8>) -> bool {
        unimplemented!()
    }
    fn get(&self, _id: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
    fn take(&self, _id: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
}

/// An implementor of `StoresServerSessions` that stores everything
/// in memory.  If enforces a limit on the number of stored sessions
/// to bound memory usage.
pub struct ServerSessionMemoryCache {
    cache: Mutex<collections::HashMap<Vec<u8>, Vec<u8>>>,
    max_entries: usize,
}

impl ServerSessionMemoryCache {
    /// Make a new ServerSessionMemoryCache.  `size` is the maximum
    /// number of stored sessions.
    pub fn new(size: usize) -> Arc<ServerSessionMemoryCache> {
        unimplemented!()
    }

    fn limit_size(&self) {
        unimplemented!()
    }
}

impl server::StoresServerSessions for ServerSessionMemoryCache {
    fn put(&self, key: Vec<u8>, value: Vec<u8>) -> bool {
        unimplemented!()
    }

    fn get(&self, key: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }

    fn take(&self, key: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
}

/// Something which never produces tickets.
pub struct NeverProducesTickets {}

impl server::ProducesTickets for NeverProducesTickets {
    fn enabled(&self) -> bool {
        unimplemented!()
    }
    fn get_lifetime(&self) -> u32 {
        unimplemented!()
    }
    fn encrypt(&self, _bytes: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
    fn decrypt(&self, _bytes: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
}

/// Something which never resolves a certificate.
pub struct FailResolveChain {}

impl server::ResolvesServerCert for FailResolveChain {
    fn resolve(&self, _client_hello: ClientHello) -> Option<sign::CertifiedKey> {
        unimplemented!()
    }
}

/// Something which always resolves to the same cert chain.
pub struct AlwaysResolvesChain(sign::CertifiedKey);

impl AlwaysResolvesChain {
    /// Creates an `AlwaysResolvesChain`, auto-detecting the underlying private
    /// key type and encoding.
    pub fn new(
        chain: Vec<key::Certificate>,
        priv_key: &key::PrivateKey,
    ) -> Result<AlwaysResolvesChain, TLSError> {
        unimplemented!()
    }

    /// Creates an `AlwaysResolvesChain`, auto-detecting the underlying private
    /// key type and encoding.
    ///
    /// If non-empty, the given OCSP response and SCTs are attached.
    pub fn new_with_extras(
        chain: Vec<key::Certificate>,
        priv_key: &key::PrivateKey,
        ocsp: Vec<u8>,
        scts: Vec<u8>,
    ) -> Result<AlwaysResolvesChain, TLSError> {
        unimplemented!()
    }
}

impl server::ResolvesServerCert for AlwaysResolvesChain {
    fn resolve(&self, _client_hello: ClientHello) -> Option<sign::CertifiedKey> {
        unimplemented!()
    }
}

/// Something that resolves do different cert chains/keys based
/// on client-supplied server name (via SNI).
pub struct ResolvesServerCertUsingSNI {
    by_name: collections::HashMap<String, sign::CertifiedKey>,
}

impl ResolvesServerCertUsingSNI {
    /// Create a new and empty (ie, knows no certificates) resolver.
    pub fn new() -> ResolvesServerCertUsingSNI {
        unimplemented!()
    }

    /// Add a new `sign::CertifiedKey` to be used for the given SNI `name`.
    ///
    /// This function fails if `name` is not a valid DNS name, or if
    /// it's not valid for the supplied certificate, or if the certificate
    /// chain is syntactically faulty.
    pub fn add(&mut self, name: &str, ck: sign::CertifiedKey) -> Result<(), TLSError> {
        unimplemented!()
    }
}

impl server::ResolvesServerCert for ResolvesServerCertUsingSNI {
    fn resolve(&self, client_hello: ClientHello) -> Option<sign::CertifiedKey> {
        unimplemented!()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::server::ProducesTickets;
    use crate::server::ResolvesServerCert;
    use crate::StoresServerSessions;

    #[test]
    fn test_noserversessionstorage_drops_put() {
        unimplemented!()
    }

    #[test]
    fn test_noserversessionstorage_denies_gets() {
        unimplemented!()
    }

    #[test]
    fn test_noserversessionstorage_denies_takes() {
        unimplemented!()
    }

    #[test]
    fn test_serversessionmemorycache_accepts_put() {
        unimplemented!()
    }

    #[test]
    fn test_serversessionmemorycache_persists_put() {
        unimplemented!()
    }

    #[test]
    fn test_serversessionmemorycache_overwrites_put() {
        unimplemented!()
    }

    #[test]
    fn test_serversessionmemorycache_drops_to_maintain_size_invariant() {
        unimplemented!()
    }

    #[test]
    fn test_neverproducestickets_does_nothing() {
        unimplemented!()
    }

    #[test]
    fn test_failresolvechain_does_nothing() {
        unimplemented!()
    }

    #[test]
    fn test_resolvesservercertusingsni_requires_sni() {
        unimplemented!()
    }

    #[test]
    fn test_resolvesservercertusingsni_handles_unknown_name() {
        unimplemented!()
    }
}
