use crate::cipher::{Iv, IvLen};
use crate::error::TLSError;
use crate::msgs::base::PayloadU8;
use crate::KeyLog;
/// Key schedule maintenance for TLS1.3
use ring::{
    aead, digest,
    hkdf::{self, KeyType as _},
    hmac,
};

/// The kinds of secret we can extract from `KeySchedule`.
#[derive(Debug, Clone, Copy, PartialEq)]
enum SecretKind {
    ResumptionPSKBinderKey,
    ClientEarlyTrafficSecret,
    ClientHandshakeTrafficSecret,
    ServerHandshakeTrafficSecret,
    ClientApplicationTrafficSecret,
    ServerApplicationTrafficSecret,
    ExporterMasterSecret,
    ResumptionMasterSecret,
    DerivedSecret,
}

impl SecretKind {
    fn to_bytes(self) -> &'static [u8] {
        unimplemented!()
    }

    fn log_label(self) -> Option<&'static str> {
        unimplemented!()
    }
}

/// This is the TLS1.3 key schedule.  It stores the current secret and
/// the type of hash.  This isn't used directly; but only through the
/// typestates.
struct KeySchedule {
    current: hkdf::Prk,
    algorithm: ring::hkdf::Algorithm,
}

// We express the state of a contained KeySchedule using these
// typestates.  This means we can write code that cannot accidentally
// (eg) encrypt application data using a KeySchedule solely constructed
// with an empty or trivial secret, or extract the wrong kind of secrets
// at a given point.

/// KeySchedule for early data stage.
pub struct KeyScheduleEarly {
    ks: KeySchedule,
}

impl KeyScheduleEarly {
    pub fn new(algorithm: hkdf::Algorithm, secret: &[u8]) -> KeyScheduleEarly {
        unimplemented!()
    }

    pub fn client_early_traffic_secret(
        &self,
        hs_hash: &[u8],
        key_log: &dyn KeyLog,
        client_random: &[u8; 32],
    ) -> hkdf::Prk {
        unimplemented!()
    }

    pub fn resumption_psk_binder_key_and_sign_verify_data(&self, hs_hash: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    pub fn into_handshake(mut self, secret: &[u8]) -> KeyScheduleHandshake {
        unimplemented!()
    }
}

/// KeySchedule for skipping early data stage.  No secrets can be extracted
/// (since there are none), but the handshake secret can be input.
pub struct KeyScheduleNonSecret {
    ks: KeySchedule,
}

impl KeyScheduleNonSecret {
    pub fn new(algorithm: hkdf::Algorithm) -> KeyScheduleNonSecret {
        unimplemented!()
    }

    pub fn into_handshake(mut self, secret: &[u8]) -> KeyScheduleHandshake {
        unimplemented!()
    }
}

/// KeySchedule during handshake.
pub struct KeyScheduleHandshake {
    ks: KeySchedule,
    current_client_traffic_secret: Option<hkdf::Prk>,
    current_server_traffic_secret: Option<hkdf::Prk>,
}

impl KeyScheduleHandshake {
    pub fn client_handshake_traffic_secret(
        &mut self,
        hs_hash: &[u8],
        key_log: &dyn KeyLog,
        client_random: &[u8; 32],
    ) -> hkdf::Prk {
        unimplemented!()
    }

    pub fn server_handshake_traffic_secret(
        &mut self,
        hs_hash: &[u8],
        key_log: &dyn KeyLog,
        client_random: &[u8; 32],
    ) -> hkdf::Prk {
        unimplemented!()
    }

    pub fn sign_server_finish(&self, hs_hash: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    pub fn into_traffic_with_client_finished_pending(
        mut self,
    ) -> KeyScheduleTrafficWithClientFinishedPending {
        unimplemented!()
    }
}

/// KeySchedule during traffic stage, retaining the ability to calculate the client's
/// finished verify_data, and incrementally generate the first traffic keys.
pub struct KeyScheduleTrafficWithClientFinishedPending {
    ks: KeySchedule,
    handshake_client_traffic_secret: hkdf::Prk,
    current_client_traffic_secret: Option<hkdf::Prk>,
    current_server_traffic_secret: Option<hkdf::Prk>,
    current_exporter_secret: Option<hkdf::Prk>,
}

impl KeyScheduleTrafficWithClientFinishedPending {
    pub fn sign_client_finish(&self, hs_hash: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    pub fn server_application_traffic_secret(
        &mut self,
        hs_hash: &[u8],
        key_log: &dyn KeyLog,
        client_random: &[u8; 32],
    ) -> hkdf::Prk {
        unimplemented!()
    }

    pub fn client_application_traffic_secret(
        &mut self,
        hs_hash: &[u8],
        key_log: &dyn KeyLog,
        client_random: &[u8; 32],
    ) -> hkdf::Prk {
        unimplemented!()
    }

    pub fn exporter_master_secret(
        &mut self,
        hs_hash: &[u8],
        key_log: &dyn KeyLog,
        client_random: &[u8; 32],
    ) {
        unimplemented!()
    }

    pub fn into_traffic(self) -> KeyScheduleTraffic {
        unimplemented!()
    }
}

/// KeySchedule during traffic stage.  All traffic & exporter keys are guaranteed
/// to be available.
pub struct KeyScheduleTraffic {
    ks: KeySchedule,
    current_client_traffic_secret: hkdf::Prk,
    current_server_traffic_secret: hkdf::Prk,
    current_exporter_secret: hkdf::Prk,
}

impl KeyScheduleTraffic {
    pub fn next_server_application_traffic_secret(&mut self) -> hkdf::Prk {
        unimplemented!()
    }

    pub fn next_client_application_traffic_secret(&mut self) -> hkdf::Prk {
        unimplemented!()
    }

    pub fn resumption_master_secret_and_derive_ticket_psk(
        &self,
        hs_hash: &[u8],
        nonce: &[u8],
    ) -> Vec<u8> {
        unimplemented!()
    }

    pub fn export_keying_material(
        &self,
        out: &mut [u8],
        label: &[u8],
        context: Option<&[u8]>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }
}

impl KeySchedule {
    fn new(algorithm: hkdf::Algorithm, secret: &[u8]) -> KeySchedule {
        unimplemented!()
    }

    #[inline]
    fn algorithm(&self) -> hkdf::Algorithm {
        unimplemented!()
    }

    fn new_with_empty_secret(algorithm: hkdf::Algorithm) -> KeySchedule {
        unimplemented!()
    }

    /// Input the empty secret.
    fn input_empty(&mut self) {
        unimplemented!()
    }

    /// Input the given secret.
    fn input_secret(&mut self, secret: &[u8]) {
        unimplemented!()
    }

    /// Derive a secret of given `kind`, using current handshake hash `hs_hash`.
    fn derive<T, L>(&self, key_type: L, kind: SecretKind, hs_hash: &[u8]) -> T
    where
        T: for<'a> From<hkdf::Okm<'a, L>>,
        L: hkdf::KeyType,
    {
        unimplemented!()
    }

    fn derive_logged_secret(
        &self,
        kind: SecretKind,
        hs_hash: &[u8],
        key_log: &dyn KeyLog,
        client_random: &[u8; 32],
    ) -> hkdf::Prk {
        unimplemented!()
    }

    /// Derive a secret of given `kind` using the hash of the empty string
    /// for the handshake hash.  Useful only for
    /// `SecretKind::ResumptionPSKBinderKey` and
    /// `SecretKind::DerivedSecret`.
    fn derive_for_empty_hash<T>(&self, kind: SecretKind) -> T
    where
        T: for<'a> From<hkdf::Okm<'a, hkdf::Algorithm>>,
    {
        unimplemented!()
    }

    /// Sign the finished message consisting of `hs_hash` using a current
    /// traffic secret.
    fn sign_finish(&self, base_key: &hkdf::Prk, hs_hash: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    /// Sign the finished message consisting of `hs_hash` using the key material
    /// `base_key`.
    fn sign_verify_data(&self, base_key: &hkdf::Prk, hs_hash: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    /// Derive the next application traffic secret, returning it.
    fn derive_next(&self, base_key: &hkdf::Prk) -> hkdf::Prk {
        unimplemented!()
    }

    /// Derive the PSK to use given a resumption_master_secret and
    /// ticket_nonce.
    fn derive_ticket_psk(&self, rms: &hkdf::Prk, nonce: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    fn export_keying_material(
        &self,
        current_exporter_secret: &hkdf::Prk,
        out: &mut [u8],
        label: &[u8],
        context: Option<&[u8]>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }
}

pub(crate) fn hkdf_expand<T, L>(secret: &hkdf::Prk, key_type: L, label: &[u8], context: &[u8]) -> T
where
    T: for<'a> From<hkdf::Okm<'a, L>>,
    L: hkdf::KeyType,
{
    unimplemented!()
}

fn hkdf_expand_info<F, T, L>(
    secret: &hkdf::Prk,
    key_type: L,
    label: &[u8],
    context: &[u8],
    f: F,
) -> T
where
    F: for<'b> FnOnce(hkdf::Okm<'b, L>) -> T,
    L: hkdf::KeyType,
{
    unimplemented!()
}

pub(crate) struct PayloadU8Len(pub(crate) usize);
impl hkdf::KeyType for PayloadU8Len {
    fn len(&self) -> usize {
        unimplemented!()
    }
}

impl From<hkdf::Okm<'_, PayloadU8Len>> for PayloadU8 {
    fn from(okm: hkdf::Okm<PayloadU8Len>) -> Self {
        unimplemented!()
    }
}

pub fn derive_traffic_key(
    secret: &hkdf::Prk,
    aead_algorithm: &'static aead::Algorithm,
) -> aead::UnboundKey {
    unimplemented!()
}

pub(crate) fn derive_traffic_iv(secret: &hkdf::Prk) -> Iv {
    unimplemented!()
}

#[cfg(test)]
mod test {
    use super::{derive_traffic_iv, derive_traffic_key, KeySchedule, SecretKind};
    use crate::KeyLog;
    use ring::{aead, hkdf};

    #[test]
    fn test_vectors() {
        unimplemented!()
    }

    fn assert_traffic_secret(
        ks: &KeySchedule,
        kind: SecretKind,
        hash: &[u8],
        expected_traffic_secret: &[u8],
        expected_key: &[u8],
        expected_iv: &[u8],
    ) {
        unimplemented!()
    }

    fn seal_zeroes(key: aead::UnboundKey) -> Vec<u8> {
        unimplemented!()
    }
}
