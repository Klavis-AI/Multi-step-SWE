use crate::key;
use crate::msgs::base::{Payload, PayloadU8, PayloadU16, PayloadU24};
use crate::msgs::codec;
use crate::msgs::codec::{Codec, Reader};
use crate::msgs::enums::ECCurveType;
use crate::msgs::enums::PSKKeyExchangeMode;
use crate::msgs::enums::{CertificateStatusType, ClientCertificateType};
use crate::msgs::enums::{CipherSuite, Compression, ECPointFormat, ExtensionType};
use crate::msgs::enums::{HandshakeType, ProtocolVersion};
use crate::msgs::enums::{HashAlgorithm, ServerNameType, SignatureAlgorithm};
use crate::msgs::enums::{KeyUpdateRequest, NamedGroup, SignatureScheme};

#[cfg(feature = "logging")]
use crate::log::warn;

use std::collections;
use std::fmt;
use std::io::Write;
use std::mem;
use webpki;

macro_rules! declare_u8_vec(
  ($name:ident, $itemtype:ty) => {
    pub type $name = Vec<$itemtype>;

    impl Codec for $name {
      fn encode(&self, bytes: &mut Vec<u8>) {
        codec::encode_vec_u8(bytes, self);
      }

      fn read(r: &mut Reader) -> Option<$name> {
        codec::read_vec_u8::<$itemtype>(r)
      }
    }
  }
);

macro_rules! declare_u16_vec(
  ($name:ident, $itemtype:ty) => {
    pub type $name = Vec<$itemtype>;

    impl Codec for $name {
      fn encode(&self, bytes: &mut Vec<u8>) {
        codec::encode_vec_u16(bytes, self);
      }

      fn read(r: &mut Reader) -> Option<$name> {
        codec::read_vec_u16::<$itemtype>(r)
      }
    }
  }
);

declare_u16_vec!(VecU16OfPayloadU8, PayloadU8);
declare_u16_vec!(VecU16OfPayloadU16, PayloadU16);

#[derive(Debug, PartialEq, Clone)]
pub struct Random([u8; 32]);

static HELLO_RETRY_REQUEST_RANDOM: Random = Random([
    0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
    0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c,
]);

static ZERO_RANDOM: Random = Random([0u8; 32]);

impl Codec for Random {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<Random> {
        unimplemented!()
    }
}

impl Random {
    pub fn from_slice(bytes: &[u8]) -> Random {
        unimplemented!()
    }

    pub fn write_slice(&self, mut bytes: &mut [u8]) {
        unimplemented!()
    }
}

#[derive(Copy, Clone)]
pub struct SessionID {
    len: usize,
    data: [u8; 32],
}

impl fmt::Debug for SessionID {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        unimplemented!()
    }
}

impl PartialEq for SessionID {
    fn eq(&self, other: &Self) -> bool {
        unimplemented!()
    }
}

impl Codec for SessionID {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<SessionID> {
        unimplemented!()
    }
}

impl SessionID {
    pub fn new(bytes: &[u8]) -> SessionID {
        unimplemented!()
    }

    pub fn empty() -> SessionID {
        unimplemented!()
    }

    pub fn len(&self) -> usize {
        unimplemented!()
    }

    pub fn is_empty(&self) -> bool {
        unimplemented!()
    }
}

#[derive(Clone, Debug)]
pub struct UnknownExtension {
    pub typ: ExtensionType,
    pub payload: Payload,
}

impl UnknownExtension {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(typ: ExtensionType, r: &mut Reader) -> Option<UnknownExtension> {
        unimplemented!()
    }
}

declare_u8_vec!(ECPointFormatList, ECPointFormat);

pub trait SupportedPointFormats {
    fn supported() -> ECPointFormatList;
}

impl SupportedPointFormats for ECPointFormatList {
    fn supported() -> ECPointFormatList {
        unimplemented!()
    }
}

declare_u16_vec!(NamedGroups, NamedGroup);

declare_u16_vec!(SupportedSignatureSchemes, SignatureScheme);

pub trait DecomposedSignatureScheme {
    fn sign(&self) -> SignatureAlgorithm;
    fn make(alg: SignatureAlgorithm, hash: HashAlgorithm) -> SignatureScheme;
}

impl DecomposedSignatureScheme for SignatureScheme {
    fn sign(&self) -> SignatureAlgorithm {
        unimplemented!()
    }

    fn make(alg: SignatureAlgorithm, hash: HashAlgorithm) -> SignatureScheme {
        unimplemented!()
    }
}

#[derive(Clone, Debug)]
pub enum ServerNamePayload {
    HostName(webpki::DNSName),
    Unknown(Payload),
}

impl ServerNamePayload {
    fn read_hostname(r: &mut Reader) -> Option<ServerNamePayload> {
        unimplemented!()
    }

    fn encode_hostname(name: webpki::DNSNameRef, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }
}

#[derive(Clone, Debug)]
pub struct ServerName {
    pub typ: ServerNameType,
    pub payload: ServerNamePayload,
}

impl Codec for ServerName {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ServerName> {
        unimplemented!()
    }
}

declare_u16_vec!(ServerNameRequest, ServerName);

pub trait ConvertServerNameList {
    fn has_duplicate_names_for_type(&self) -> bool;
    fn get_single_hostname(&self) -> Option<webpki::DNSNameRef>;
}

impl ConvertServerNameList for ServerNameRequest {
    /// RFC6066: "The ServerNameList MUST NOT contain more than one name of the same name_type."
    fn has_duplicate_names_for_type(&self) -> bool {
        unimplemented!()
    }

    fn get_single_hostname(&self) -> Option<webpki::DNSNameRef> {
        unimplemented!()
    }
}

pub type ProtocolNameList = VecU16OfPayloadU8;

pub trait ConvertProtocolNameList {
    fn from_slices(names: &[&[u8]]) -> Self;
    fn to_slices(&self) -> Vec<&[u8]>;
    fn as_single_slice(&self) -> Option<&[u8]>;
}

impl ConvertProtocolNameList for ProtocolNameList {
    fn from_slices(names: &[&[u8]]) -> ProtocolNameList {
        unimplemented!()
    }

    fn to_slices(&self) -> Vec<&[u8]> {
        unimplemented!()
    }

    fn as_single_slice(&self) -> Option<&[u8]> {
        unimplemented!()
    }
}

// --- TLS 1.3 Key shares ---
#[derive(Clone, Debug)]
pub struct KeyShareEntry {
    pub group: NamedGroup,
    pub payload: PayloadU16,
}

impl KeyShareEntry {
    pub fn new(group: NamedGroup, payload: &[u8]) -> KeyShareEntry {
        unimplemented!()
    }
}

impl Codec for KeyShareEntry {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<KeyShareEntry> {
        unimplemented!()
    }
}

// --- TLS 1.3 PresharedKey offers ---
#[derive(Clone, Debug)]
pub struct PresharedKeyIdentity {
    pub identity: PayloadU16,
    pub obfuscated_ticket_age: u32,
}

impl PresharedKeyIdentity {
    pub fn new(id: Vec<u8>, age: u32) -> PresharedKeyIdentity {
        unimplemented!()
    }
}

impl Codec for PresharedKeyIdentity {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<PresharedKeyIdentity> {
        unimplemented!()
    }
}

declare_u16_vec!(PresharedKeyIdentities, PresharedKeyIdentity);
pub type PresharedKeyBinder = PayloadU8;
pub type PresharedKeyBinders = VecU16OfPayloadU8;

#[derive(Clone, Debug)]
pub struct PresharedKeyOffer {
    pub identities: PresharedKeyIdentities,
    pub binders: PresharedKeyBinders,
}

impl PresharedKeyOffer {
    /// Make a new one with one entry.
    pub fn new(id: PresharedKeyIdentity, binder: Vec<u8>) -> PresharedKeyOffer {
        unimplemented!()
    }
}

impl Codec for PresharedKeyOffer {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<PresharedKeyOffer> {
        unimplemented!()
    }
}

// --- RFC6066 certificate status request ---
type ResponderIDs = VecU16OfPayloadU16;

#[derive(Clone, Debug)]
pub struct OCSPCertificateStatusRequest {
    pub responder_ids: ResponderIDs,
    pub extensions: PayloadU16,
}

impl Codec for OCSPCertificateStatusRequest {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<OCSPCertificateStatusRequest> {
        unimplemented!()
    }
}

#[derive(Clone, Debug)]
pub enum CertificateStatusRequest {
    OCSP(OCSPCertificateStatusRequest),
    Unknown((CertificateStatusType, Payload)),
}

impl Codec for CertificateStatusRequest {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificateStatusRequest> {
        unimplemented!()
    }
}

impl CertificateStatusRequest {
    pub fn build_ocsp() -> CertificateStatusRequest {
        unimplemented!()
    }
}

// ---
// SCTs

pub type SCTList = VecU16OfPayloadU16;

// ---

declare_u8_vec!(PSKKeyExchangeModes, PSKKeyExchangeMode);
declare_u16_vec!(KeyShareEntries, KeyShareEntry);
declare_u8_vec!(ProtocolVersions, ProtocolVersion);

#[derive(Clone, Debug)]
pub enum ClientExtension {
    ECPointFormats(ECPointFormatList),
    NamedGroups(NamedGroups),
    SignatureAlgorithms(SupportedSignatureSchemes),
    ServerName(ServerNameRequest),
    SessionTicketRequest,
    SessionTicketOffer(Payload),
    Protocols(ProtocolNameList),
    SupportedVersions(ProtocolVersions),
    KeyShare(KeyShareEntries),
    PresharedKeyModes(PSKKeyExchangeModes),
    PresharedKey(PresharedKeyOffer),
    Cookie(PayloadU16),
    ExtendedMasterSecretRequest,
    CertificateStatusRequest(CertificateStatusRequest),
    SignedCertificateTimestampRequest,
    TransportParameters(Vec<u8>),
    EarlyData,
    Unknown(UnknownExtension),
}

impl ClientExtension {
    pub fn get_type(&self) -> ExtensionType {
        unimplemented!()
    }
}

impl Codec for ClientExtension {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ClientExtension> {
        unimplemented!()
    }
}

fn trim_hostname_trailing_dot_for_sni(dns_name: webpki::DNSNameRef) -> webpki::DNSName {
    unimplemented!()
}

impl ClientExtension {
    /// Make a basic SNI ServerNameRequest quoting `hostname`.
    pub fn make_sni(dns_name: webpki::DNSNameRef) -> ClientExtension {
        unimplemented!()
    }
}

#[derive(Clone, Debug)]
pub enum ServerExtension {
    ECPointFormats(ECPointFormatList),
    ServerNameAck,
    SessionTicketAck,
    RenegotiationInfo(PayloadU8),
    Protocols(ProtocolNameList),
    KeyShare(KeyShareEntry),
    PresharedKey(u16),
    ExtendedMasterSecretAck,
    CertificateStatusAck,
    SignedCertificateTimestamp(SCTList),
    SupportedVersions(ProtocolVersion),
    TransportParameters(Vec<u8>),
    EarlyData,
    Unknown(UnknownExtension),
}

impl ServerExtension {
    pub fn get_type(&self) -> ExtensionType {
        unimplemented!()
    }
}

impl Codec for ServerExtension {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ServerExtension> {
        unimplemented!()
    }
}

impl ServerExtension {
    pub fn make_alpn(proto: &[&[u8]]) -> ServerExtension {
        unimplemented!()
    }

    pub fn make_empty_renegotiation_info() -> ServerExtension {
        unimplemented!()
    }

    pub fn make_sct(sctl: Vec<u8>) -> ServerExtension {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct ClientHelloPayload {
    pub client_version: ProtocolVersion,
    pub random: Random,
    pub session_id: SessionID,
    pub cipher_suites: Vec<CipherSuite>,
    pub compression_methods: Vec<Compression>,
    pub extensions: Vec<ClientExtension>,
}

impl Codec for ClientHelloPayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ClientHelloPayload> {
        unimplemented!()
    }
}

impl ClientHelloPayload {
    /// Returns true if there is more than one extension of a given
    /// type.
    pub fn has_duplicate_extension(&self) -> bool {
        unimplemented!()
    }

    pub fn find_extension(&self, ext: ExtensionType) -> Option<&ClientExtension> {
        unimplemented!()
    }

    pub fn get_sni_extension(&self) -> Option<&ServerNameRequest> {
        unimplemented!()
    }

    pub fn get_sigalgs_extension(&self) -> Option<&SupportedSignatureSchemes> {
        unimplemented!()
    }

    pub fn get_namedgroups_extension(&self) -> Option<&NamedGroups> {
        unimplemented!()
    }

    pub fn get_ecpoints_extension(&self) -> Option<&ECPointFormatList> {
        unimplemented!()
    }

    pub fn get_alpn_extension(&self) -> Option<&ProtocolNameList> {
        unimplemented!()
    }

    pub fn get_quic_params_extension(&self) -> Option<Vec<u8>> {
        unimplemented!()
    }

    pub fn get_ticket_extension(&self) -> Option<&ClientExtension> {
        unimplemented!()
    }

    pub fn get_versions_extension(&self) -> Option<&ProtocolVersions> {
        unimplemented!()
    }

    pub fn get_keyshare_extension(&self) -> Option<&KeyShareEntries> {
        unimplemented!()
    }

    pub fn has_keyshare_extension_with_duplicates(&self) -> bool {
        unimplemented!()
    }

    pub fn get_psk(&self) -> Option<&PresharedKeyOffer> {
        unimplemented!()
    }

    pub fn check_psk_ext_is_last(&self) -> bool {
        unimplemented!()
    }

    pub fn get_psk_modes(&self) -> Option<&PSKKeyExchangeModes> {
        unimplemented!()
    }

    pub fn psk_mode_offered(&self, mode: PSKKeyExchangeMode) -> bool {
        unimplemented!()
    }


    pub fn set_psk_binder(&mut self, binder: Vec<u8>) {
        unimplemented!()
    }

    pub fn ems_support_offered(&self) -> bool {
        unimplemented!()
    }

    pub fn early_data_extension_offered(&self) -> bool {
        unimplemented!()
    }
}

#[derive(Debug)]
pub enum HelloRetryExtension {
    KeyShare(NamedGroup),
    Cookie(PayloadU16),
    SupportedVersions(ProtocolVersion),
    Unknown(UnknownExtension),
}

impl HelloRetryExtension {
    pub fn get_type(&self) -> ExtensionType {
        unimplemented!()
    }
}

impl Codec for HelloRetryExtension {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<HelloRetryExtension> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct HelloRetryRequest {
    pub legacy_version: ProtocolVersion,
    pub session_id: SessionID,
    pub cipher_suite: CipherSuite,
    pub extensions: Vec<HelloRetryExtension>,
}

impl Codec for HelloRetryRequest {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<HelloRetryRequest> {
        unimplemented!()
    }
}

impl HelloRetryRequest {
    /// Returns true if there is more than one extension of a given
    /// type.
    pub fn has_duplicate_extension(&self) -> bool {
        unimplemented!()
    }

    pub fn has_unknown_extension(&self) -> bool {
        unimplemented!()
    }

    fn find_extension(&self, ext: ExtensionType) -> Option<&HelloRetryExtension> {
        unimplemented!()
    }

    pub fn get_requested_key_share_group(&self) -> Option<NamedGroup> {
        unimplemented!()
    }

    pub fn get_cookie(&self) -> Option<&PayloadU16> {
        unimplemented!()
    }

    pub fn get_supported_versions(&self) -> Option<ProtocolVersion> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct ServerHelloPayload {
    pub legacy_version: ProtocolVersion,
    pub random: Random,
    pub session_id: SessionID,
    pub cipher_suite: CipherSuite,
    pub compression_method: Compression,
    pub extensions: Vec<ServerExtension>,
}

impl Codec for ServerHelloPayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    // minus version and random, which have already been read.
    fn read(r: &mut Reader) -> Option<ServerHelloPayload> {
        unimplemented!()
    }
}

impl HasServerExtensions for ServerHelloPayload {
    fn get_extensions(&self) -> &[ServerExtension] {
        unimplemented!()
    }
}

impl ServerHelloPayload {
    pub fn get_key_share(&self) -> Option<&KeyShareEntry> {
        unimplemented!()
    }

    pub fn get_psk_index(&self) -> Option<u16> {
        unimplemented!()
    }

    pub fn get_ecpoints_extension(&self) -> Option<&ECPointFormatList> {
        unimplemented!()
    }

    pub fn ems_support_acked(&self) -> bool {
        unimplemented!()
    }

    pub fn get_sct_list(&self) -> Option<&SCTList> {
        unimplemented!()
    }

    pub fn get_supported_versions(&self) -> Option<ProtocolVersion> {
        unimplemented!()
    }
}

pub type CertificatePayload = Vec<key::Certificate>;

impl Codec for CertificatePayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificatePayload> {
        unimplemented!()
    }
}

// TLS1.3 changes the Certificate payload encoding.
// That's annoying. It means the parsing is not
// context-free any more.

#[derive(Debug)]
pub enum CertificateExtension {
    CertificateStatus(CertificateStatus),
    SignedCertificateTimestamp(SCTList),
    Unknown(UnknownExtension),
}

impl CertificateExtension {
    pub fn get_type(&self) -> ExtensionType {
        unimplemented!()
    }

    pub fn make_sct(sct_list: Vec<u8>) -> CertificateExtension {
        unimplemented!()
    }

    pub fn get_cert_status(&self) -> Option<&Vec<u8>> {
        unimplemented!()
    }

    pub fn get_sct_list(&self) -> Option<&SCTList> {
        unimplemented!()
    }
}

impl Codec for CertificateExtension {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificateExtension> {
        unimplemented!()
    }
}

declare_u16_vec!(CertificateExtensions, CertificateExtension);

#[derive(Debug)]
pub struct CertificateEntry {
    pub cert: key::Certificate,
    pub exts: CertificateExtensions,
}

impl Codec for CertificateEntry {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificateEntry> {
        unimplemented!()
    }
}

impl CertificateEntry {
    pub fn new(cert: key::Certificate) -> CertificateEntry {
        unimplemented!()
    }

    pub fn has_duplicate_extension(&self) -> bool {
        unimplemented!()
    }

    pub fn has_unknown_extension(&self) -> bool {
        unimplemented!()
    }

    pub fn get_ocsp_response(&self) -> Option<&Vec<u8>> {
        unimplemented!()
    }

    pub fn get_scts(&self) -> Option<&SCTList> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct CertificatePayloadTLS13 {
    pub context: PayloadU8,
    pub entries: Vec<CertificateEntry>,
}

impl Codec for CertificatePayloadTLS13 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificatePayloadTLS13> {
        unimplemented!()
    }
}

impl CertificatePayloadTLS13 {
    pub fn new(entries: Vec<CertificateEntry>) -> CertificatePayloadTLS13 {
        unimplemented!()
    }

    pub fn any_entry_has_duplicate_extension(&self) -> bool {
        unimplemented!()
    }

    pub fn any_entry_has_unknown_extension(&self) -> bool {
        unimplemented!()
    }

    pub fn any_entry_has_extension(&self) -> bool {
        unimplemented!()
    }

    pub fn get_end_entity_ocsp(&self) -> Vec<u8> {
        unimplemented!()
    }

    pub fn get_end_entity_scts(&self) -> Option<SCTList> {
        unimplemented!()
    }

    pub fn convert(&self) -> CertificatePayload {
        unimplemented!()
    }
}

#[derive(Debug)]
pub enum KeyExchangeAlgorithm {
    BulkOnly,
    DH,
    DHE,
    RSA,
    ECDH,
    ECDHE,
}

// We don't support arbitrary curves.  It's a terrible
// idea and unnecessary attack surface.  Please,
// get a grip.
#[derive(Debug)]
pub struct ECParameters {
    pub curve_type: ECCurveType,
    pub named_group: NamedGroup,
}

impl Codec for ECParameters {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ECParameters> {
        unimplemented!()
    }
}

#[derive(Debug, Clone)]
pub struct DigitallySignedStruct {
    pub scheme: SignatureScheme,
    pub sig: PayloadU16,
}

impl DigitallySignedStruct {
    pub fn new(scheme: SignatureScheme, sig: Vec<u8>) -> DigitallySignedStruct {
        unimplemented!()
    }
}

impl Codec for DigitallySignedStruct {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<DigitallySignedStruct> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct ClientECDHParams {
    pub public: PayloadU8,
}

impl Codec for ClientECDHParams {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ClientECDHParams> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct ServerECDHParams {
    pub curve_params: ECParameters,
    pub public: PayloadU8,
}

impl ServerECDHParams {
    pub fn new(named_group: NamedGroup, pubkey: &[u8]) -> ServerECDHParams {
        unimplemented!()
    }
}

impl Codec for ServerECDHParams {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ServerECDHParams> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct ECDHEServerKeyExchange {
    pub params: ServerECDHParams,
    pub dss: DigitallySignedStruct,
}

impl Codec for ECDHEServerKeyExchange {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ECDHEServerKeyExchange> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub enum ServerKeyExchangePayload {
    ECDHE(ECDHEServerKeyExchange),
    Unknown(Payload),
}

impl Codec for ServerKeyExchangePayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ServerKeyExchangePayload> {
        unimplemented!()
    }
}

impl ServerKeyExchangePayload {
    pub fn unwrap_given_kxa(&self, kxa: &KeyExchangeAlgorithm) -> Option<ServerKeyExchangePayload> {
        unimplemented!()
    }

    pub fn encode_params(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    pub fn get_sig(&self) -> Option<DigitallySignedStruct> {
        unimplemented!()
    }
}

// -- EncryptedExtensions (TLS1.3 only) --
declare_u16_vec!(EncryptedExtensions, ServerExtension);

pub trait HasServerExtensions {
    fn get_extensions(&self) -> &[ServerExtension];

    /// Returns true if there is more than one extension of a given
    /// type.
    fn has_duplicate_extension(&self) -> bool {
        unimplemented!()
    }

    fn find_extension(&self, ext: ExtensionType) -> Option<&ServerExtension> {
        unimplemented!()
    }

    fn get_alpn_protocol(&self) -> Option<&[u8]> {
        unimplemented!()
    }

    fn get_quic_params_extension(&self) -> Option<Vec<u8>> {
        unimplemented!()
    }

    fn early_data_extension_offered(&self) -> bool {
        unimplemented!()
    }
}

impl HasServerExtensions for EncryptedExtensions {
    fn get_extensions(&self) -> &[ServerExtension] {
        unimplemented!()
    }
}

// -- CertificateRequest and sundries --
declare_u8_vec!(ClientCertificateTypes, ClientCertificateType);
pub type DistinguishedName = PayloadU16;
pub type DistinguishedNames = VecU16OfPayloadU16;

#[derive(Debug)]
pub struct CertificateRequestPayload {
    pub certtypes: ClientCertificateTypes,
    pub sigschemes: SupportedSignatureSchemes,
    pub canames: DistinguishedNames,
}

impl Codec for CertificateRequestPayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificateRequestPayload> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub enum CertReqExtension {
    SignatureAlgorithms(SupportedSignatureSchemes),
    AuthorityNames(DistinguishedNames),
    Unknown(UnknownExtension),
}

impl CertReqExtension {
    pub fn get_type(&self) -> ExtensionType {
        unimplemented!()
    }
}

impl Codec for CertReqExtension {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertReqExtension> {
        unimplemented!()
    }
}

declare_u16_vec!(CertReqExtensions, CertReqExtension);

#[derive(Debug)]
pub struct CertificateRequestPayloadTLS13 {
    pub context: PayloadU8,
    pub extensions: CertReqExtensions,
}

impl Codec for CertificateRequestPayloadTLS13 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificateRequestPayloadTLS13> {
        unimplemented!()
    }
}

impl CertificateRequestPayloadTLS13 {
    pub fn find_extension(&self, ext: ExtensionType) -> Option<&CertReqExtension> {
        unimplemented!()
    }

    pub fn get_sigalgs_extension(&self) -> Option<&SupportedSignatureSchemes> {
        unimplemented!()
    }

    pub fn get_authorities_extension(&self) -> Option<&DistinguishedNames> {
        unimplemented!()
    }
}

// -- NewSessionTicket --
#[derive(Debug)]
pub struct NewSessionTicketPayload {
    pub lifetime_hint: u32,
    pub ticket: PayloadU16,
}

impl NewSessionTicketPayload {
    pub fn new(lifetime_hint: u32, ticket: Vec<u8>) -> NewSessionTicketPayload {
        unimplemented!()
    }
}

impl Codec for NewSessionTicketPayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<NewSessionTicketPayload> {
        unimplemented!()
    }
}

// -- NewSessionTicket electric boogaloo --
#[derive(Debug)]
pub enum NewSessionTicketExtension {
    EarlyData(u32),
    Unknown(UnknownExtension),
}

impl NewSessionTicketExtension {
    pub fn get_type(&self) -> ExtensionType {
        unimplemented!()
    }
}

impl Codec for NewSessionTicketExtension {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<NewSessionTicketExtension> {
        unimplemented!()
    }
}

declare_u16_vec!(NewSessionTicketExtensions, NewSessionTicketExtension);

#[derive(Debug)]
pub struct NewSessionTicketPayloadTLS13 {
    pub lifetime: u32,
    pub age_add: u32,
    pub nonce: PayloadU8,
    pub ticket: PayloadU16,
    pub exts: NewSessionTicketExtensions,
}

impl NewSessionTicketPayloadTLS13 {
    pub fn new(
        lifetime: u32,
        age_add: u32,
        nonce: Vec<u8>,
        ticket: Vec<u8>,
    ) -> NewSessionTicketPayloadTLS13 {
        unimplemented!()
    }

    pub fn find_extension(&self, ext: ExtensionType) -> Option<&NewSessionTicketExtension> {
        unimplemented!()
    }

    pub fn get_max_early_data_size(&self) -> Option<u32> {
        unimplemented!()
    }
}

impl Codec for NewSessionTicketPayloadTLS13 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<NewSessionTicketPayloadTLS13> {
        unimplemented!()
    }
}

// -- RFC6066 certificate status types

/// Only supports OCSP
#[derive(Debug)]
pub struct CertificateStatus {
    pub ocsp_response: PayloadU24,
}

impl Codec for CertificateStatus {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<CertificateStatus> {
        unimplemented!()
    }
}

impl CertificateStatus {
    pub fn new(ocsp: Vec<u8>) -> CertificateStatus {
        unimplemented!()
    }

    pub fn take_ocsp_response(&mut self) -> Vec<u8> {
        unimplemented!()
    }
}

#[derive(Debug)]
pub enum HandshakePayload {
    HelloRequest,
    ClientHello(ClientHelloPayload),
    ServerHello(ServerHelloPayload),
    HelloRetryRequest(HelloRetryRequest),
    Certificate(CertificatePayload),
    CertificateTLS13(CertificatePayloadTLS13),
    ServerKeyExchange(ServerKeyExchangePayload),
    CertificateRequest(CertificateRequestPayload),
    CertificateRequestTLS13(CertificateRequestPayloadTLS13),
    CertificateVerify(DigitallySignedStruct),
    ServerHelloDone,
    EarlyData,
    EndOfEarlyData,
    ClientKeyExchange(Payload),
    NewSessionTicket(NewSessionTicketPayload),
    NewSessionTicketTLS13(NewSessionTicketPayloadTLS13),
    EncryptedExtensions(EncryptedExtensions),
    KeyUpdate(KeyUpdateRequest),
    Finished(Payload),
    CertificateStatus(CertificateStatus),
    MessageHash(Payload),
    Unknown(Payload),
}

impl HandshakePayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct HandshakeMessagePayload {
    pub typ: HandshakeType,
    pub payload: HandshakePayload,
}

impl Codec for HandshakeMessagePayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<HandshakeMessagePayload> {
        unimplemented!()
    }
}

impl HandshakeMessagePayload {
    pub fn length(&self) -> usize {
        unimplemented!()
    }

    pub fn read_version(r: &mut Reader, vers: ProtocolVersion) -> Option<HandshakeMessagePayload> {
        unimplemented!()
    }

    pub fn build_key_update_notify() -> HandshakeMessagePayload {
        unimplemented!()
    }

    pub fn get_encoding_for_binder_signing(&self) -> Vec<u8> {
        unimplemented!()
    }

    pub fn build_handshake_hash(hash: &[u8]) -> HandshakeMessagePayload {
        unimplemented!()
    }
}
