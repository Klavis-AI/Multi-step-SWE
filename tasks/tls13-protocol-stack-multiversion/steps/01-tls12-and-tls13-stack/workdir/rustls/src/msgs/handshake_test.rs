use super::base::{Payload, PayloadU8, PayloadU16, PayloadU24};
use super::codec::{put_u16, Codec, Reader};
use super::enums::*;
use super::handshake::*;
use crate::key::Certificate;
use webpki::DNSNameRef;

use std::mem;

#[test]
fn rejects_short_random() {
    unimplemented!()
}

#[test]
fn reads_random() {
    unimplemented!()
}

#[test]
fn rejects_truncated_sessionid() {
    unimplemented!()
}

#[test]
fn rejects_sessionid_with_bad_length() {
    unimplemented!()
}

#[test]
fn sessionid_with_different_lengths_are_unequal() {
    unimplemented!()
}

#[test]
fn accepts_short_sessionid() {
    unimplemented!()
}

#[test]
fn accepts_empty_sessionid() {
    unimplemented!()
}

#[test]
fn can_roundtrip_unknown_client_ext() {
    unimplemented!()
}

#[test]
fn can_roundtrip_single_sni() {
    unimplemented!()
}

#[test]
fn can_roundtrip_other_sni_name_types() {
    unimplemented!()
}

#[test]
fn get_single_hostname_returns_none_for_other_sni_name_types() {
    unimplemented!()
}

#[test]
fn can_roundtrip_multiname_sni() {
    unimplemented!()
}

#[test]
fn rejects_truncated_sni() {
    unimplemented!()
}

#[test]
fn can_roundtrip_psk_identity() {
    unimplemented!()
}

#[test]
fn can_roundtrip_psk_offer() {
    unimplemented!()
}

#[test]
fn can_roundtrip_certstatusreq_for_ocsp() {
    unimplemented!()
}

#[test]
fn can_roundtrip_certstatusreq_for_other() {
    unimplemented!()
}

#[test]
fn can_roundtrip_multi_proto() {
    unimplemented!()
}

#[test]
fn can_roundtrip_single_proto() {
    unimplemented!()
}

#[test]
fn decomposed_signature_scheme_has_correct_mappings() {
    unimplemented!()
}

fn get_sample_clienthellopayload() -> ClientHelloPayload {
    unimplemented!()
}

#[test]
fn can_print_all_clientextensions() {
    unimplemented!()
}

#[test]
fn can_clone_all_clientextensions() {
    unimplemented!()
}

#[test]
fn client_has_duplicate_extensions_works() {
    unimplemented!()
}

#[test]
fn test_truncated_psk_offer() {
    unimplemented!()
}

#[test]
fn test_truncated_client_hello_is_detected() {
    unimplemented!()
}

#[test]
fn test_truncated_client_extension_is_detected() {
    unimplemented!()
}

fn test_client_extension_getter(typ: ExtensionType, getter: fn(&ClientHelloPayload) -> bool) {
    unimplemented!()
}

#[test]
fn client_get_sni_extension() {
    unimplemented!()
}

#[test]
fn client_get_sigalgs_extension() {
    unimplemented!()
}

#[test]
fn client_get_namedgroups_extension() {
    unimplemented!()
}

#[test]
fn client_get_ecpoints_extension() {
    unimplemented!()
}

#[test]
fn client_get_alpn_extension() {
    unimplemented!()
}

#[test]
fn client_get_quic_params_extension() {
    unimplemented!()
}

#[test]
fn client_get_versions_extension() {
    unimplemented!()
}

#[test]
fn client_get_keyshare_extension() {
    unimplemented!()
}

#[test]
fn client_get_psk() {
    unimplemented!()
}

#[test]
fn client_get_psk_modes() {
    unimplemented!()
}

#[test]
fn test_truncated_helloretry_extension_is_detected() {
    unimplemented!()
}

fn test_helloretry_extension_getter(typ: ExtensionType, getter: fn(&HelloRetryRequest) -> bool) {
    unimplemented!()
}

#[test]
fn helloretry_get_requested_key_share_group() {
    unimplemented!()
}

#[test]
fn helloretry_get_cookie() {
    unimplemented!()
}

#[test]
fn helloretry_get_supported_versions() {
    unimplemented!()
}

#[test]
fn test_truncated_server_extension_is_detected() {
    unimplemented!()
}

fn test_server_extension_getter(typ: ExtensionType, getter: fn(&ServerHelloPayload) -> bool) {
    unimplemented!()
}

#[test]
fn server_get_key_share() {
    unimplemented!()
}

#[test]
fn server_get_psk_index() {
    unimplemented!()
}

#[test]
fn server_get_ecpoints_extension() {
    unimplemented!()
}

#[test]
fn server_get_sct_list() {
    unimplemented!()
}

#[test]
fn server_get_supported_versions() {
    unimplemented!()
}

fn test_cert_extension_getter(typ: ExtensionType, getter: fn(&CertificateEntry) -> bool) {
    unimplemented!()
}

#[test]
fn certentry_get_ocsp_response() {
    unimplemented!()
}

#[test]
fn certentry_get_scts() {
    unimplemented!()
}

fn get_sample_serverhellopayload() -> ServerHelloPayload {
    unimplemented!()
}

#[test]
fn can_print_all_serverextensions() {
    unimplemented!()
}

#[test]
fn can_clone_all_serverextensions() {
    unimplemented!()
}

fn get_sample_helloretryrequest() -> HelloRetryRequest {
    unimplemented!()
}

fn get_sample_certificatepayloadtls13() -> CertificatePayloadTLS13 {
    unimplemented!()
}

fn get_sample_serverkeyexchangepayload_ecdhe() -> ServerKeyExchangePayload {
    unimplemented!()
}

fn get_sample_serverkeyexchangepayload_unknown() -> ServerKeyExchangePayload {
    unimplemented!()
}

fn get_sample_certificaterequestpayload() -> CertificateRequestPayload {
    unimplemented!()
}

fn get_sample_certificaterequestpayloadtls13() -> CertificateRequestPayloadTLS13 {
    unimplemented!()
}

fn get_sample_newsessionticketpayload() -> NewSessionTicketPayload {
    unimplemented!()
}

fn get_sample_newsessionticketpayloadtls13() -> NewSessionTicketPayloadTLS13 {
    unimplemented!()
}

fn get_sample_encryptedextensions() -> EncryptedExtensions {
    unimplemented!()
}

fn get_sample_certificatestatus() -> CertificateStatus {
    unimplemented!()
}

fn get_all_tls12_handshake_payloads() -> Vec<HandshakeMessagePayload> {
    unimplemented!()
}

#[test]
fn can_roundtrip_all_tls12_handshake_payloads() {
    unimplemented!()
}

#[test]
fn can_detect_truncation_of_all_tls12_handshake_payloads() {
    unimplemented!()
}

fn get_all_tls13_handshake_payloads() -> Vec<HandshakeMessagePayload> {
    unimplemented!()
}

#[test]
fn can_roundtrip_all_tls13_handshake_payloads() {
    unimplemented!()
}

fn put_u24(u: u32, b: &mut [u8]) {
    unimplemented!()
}

#[test]
fn can_detect_truncation_of_all_tls13_handshake_payloads() {
    unimplemented!()
}

#[test]
fn cannot_read_messagehash_from_network() {
    unimplemented!()
}

#[test]
fn cannot_decode_huge_certificate() {
    unimplemented!()
}
