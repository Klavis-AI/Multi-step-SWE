use crate::client;
use crate::error::TLSError;
use crate::key;
use crate::msgs::enums::SignatureScheme;
use crate::sign;

use std::collections;
use std::sync::{Arc, Mutex};

/// An implementor of `StoresClientSessions` which does nothing.
pub struct NoClientSessionStorage {}

impl client::StoresClientSessions for NoClientSessionStorage {
    fn put(&self, _key: Vec<u8>, _value: Vec<u8>) -> bool {
        unimplemented!()
    }

    fn get(&self, _key: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
}

/// An implementor of `StoresClientSessions` that stores everything
/// in memory.  It enforces a limit on the number of entries
/// to bound memory usage.
pub struct ClientSessionMemoryCache {
    cache: Mutex<collections::HashMap<Vec<u8>, Vec<u8>>>,
    max_entries: usize,
}

impl ClientSessionMemoryCache {
    /// Make a new ClientSessionMemoryCache.  `size` is the
    /// maximum number of stored sessions.
    pub fn new(size: usize) -> Arc<ClientSessionMemoryCache> {
        unimplemented!()
    }

    fn limit_size(&self) {
        unimplemented!()
    }
}

impl client::StoresClientSessions for ClientSessionMemoryCache {
    fn put(&self, key: Vec<u8>, value: Vec<u8>) -> bool {
        unimplemented!()
    }

    fn get(&self, key: &[u8]) -> Option<Vec<u8>> {
        unimplemented!()
    }
}

pub struct FailResolveClientCert {}

impl client::ResolvesClientCert for FailResolveClientCert {
    fn resolve(
        &self,
        _acceptable_issuers: &[&[u8]],
        _sigschemes: &[SignatureScheme],
    ) -> Option<sign::CertifiedKey> {
        unimplemented!()
    }

    fn has_certs(&self) -> bool {
        unimplemented!()
    }
}

pub struct AlwaysResolvesClientCert(sign::CertifiedKey);

impl AlwaysResolvesClientCert {
    pub fn new(
        chain: Vec<key::Certificate>,
        priv_key: &key::PrivateKey,
    ) -> Result<AlwaysResolvesClientCert, TLSError> {
        unimplemented!()
    }
}

impl client::ResolvesClientCert for AlwaysResolvesClientCert {
    fn resolve(
        &self,
        _acceptable_issuers: &[&[u8]],
        _sigschemes: &[SignatureScheme],
    ) -> Option<sign::CertifiedKey> {
        unimplemented!()
    }

    fn has_certs(&self) -> bool {
        unimplemented!()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::StoresClientSessions;

    #[test]
    fn test_noclientsessionstorage_drops_put() {
        unimplemented!()
    }

    #[test]
    fn test_noclientsessionstorage_denies_gets() {
        unimplemented!()
    }

    #[test]
    fn test_clientsessionmemorycache_accepts_put() {
        unimplemented!()
    }

    #[test]
    fn test_clientsessionmemorycache_persists_put() {
        unimplemented!()
    }

    #[test]
    fn test_clientsessionmemorycache_overwrites_put() {
        unimplemented!()
    }

    #[test]
    fn test_clientsessionmemorycache_drops_to_maintain_size_invariant() {
        unimplemented!()
    }
}
