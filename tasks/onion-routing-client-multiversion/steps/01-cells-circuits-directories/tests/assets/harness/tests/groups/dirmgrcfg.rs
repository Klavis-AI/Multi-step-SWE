//! Group `dirmgrcfg`: the directory manager's configuration — which
//! authorities and caches to trust, and what to override in the consensus.

use std::collections::BTreeSet;

use tor_checkable::{SelfSigned, Timebound};
use tor_dirmgr::{
    Authority, DirMgrConfig, DirMgrConfigBuilder, DownloadScheduleConfig, Error, NetworkConfig,
};
use tor_linkspec::ChanTarget;
use tor_llcrypto::pk::rsa::RsaIdentity;
use tor_netdir::fallback::FallbackDir;
use tor_netdir::{MdReceiver, PartialNetDir};
use tor_netdoc::doc::authcert::AuthCert;

use crate::common::{fixture_text, load_consensus, load_mds};

/// Recover the `tor_dirmgr::Error` behind a configuration failure.
fn cfg_err(e: anyhow::Error) -> Error {
    match e.downcast::<Error>() {
        Ok(e) => e,
        Err(other) => panic!("not a tor-dirmgr error: {}", other),
    }
}

/// An authority with a given 20-byte v3 identity.
fn authority(name: &str, id: [u8; 20]) -> Authority {
    Authority::builder()
        .name(name)
        .v3ident(RsaIdentity::from_bytes(&id).unwrap())
        .build()
        .expect("well-formed authority")
}

/// A fallback directory with fixed identities.
fn fallback(b: u8) -> FallbackDir {
    FallbackDir::builder()
        .rsa_identity([b; 20].into())
        .ed_identity([b; 32].into())
        .orport(format!("10.0.0.{}:9001", b).parse().unwrap())
        .build()
        .expect("well-formed fallback")
}

#[test]
fn dirmgrcfg__a_cache_path_is_required_and_the_rest_defaults() {
    let e = cfg_err(DirMgrConfigBuilder::new().build().unwrap_err());
    assert!(matches!(e, Error::BadNetworkConfig(_)), "{:?}", e);

    let dir = tempfile::tempdir().unwrap();
    let mut builder = DirMgrConfig::builder();
    builder.cache_path(dir.path());
    let cfg = builder.build().expect("a cache path is all that is needed");

    // With no network configured, the defaults are the public Tor network.
    assert_eq!(cfg.authorities().len(), NetworkConfig::default().authorities().len());
    assert_eq!(cfg.fallbacks().len(), NetworkConfig::default().fallbacks().len());
    assert!(cfg.override_net_params().iter().next().is_none());
    assert_eq!(cfg.schedule().microdesc_parallelism(), 4);
}

#[test]
fn dirmgrcfg__naming_authorities_opts_out_of_the_built_in_fallbacks() {
    let dir = tempfile::tempdir().unwrap();

    // A network with its own authorities has no default fallbacks, so a
    // configuration built from it alone is incomplete.
    let mut net = NetworkConfig::builder();
    net.authority(authority("alpha", [0x11; 20]));
    let net = net.build().unwrap();
    assert_eq!(net.authorities().len(), 1);
    assert!(net.fallbacks().is_empty());

    let mut builder = DirMgrConfigBuilder::new();
    builder.cache_path(dir.path()).network_config(net);
    let e = cfg_err(builder.build().unwrap_err());
    assert!(matches!(e, Error::BadNetworkConfig(_)), "{:?}", e);

    // Naming a fallback completes it.
    let mut net = NetworkConfig::builder();
    net.authority(authority("alpha", [0x11; 20]))
        .authority(authority("beta", [0x22; 20]))
        .fallback(fallback(9));
    let mut builder = DirMgrConfigBuilder::new();
    builder.cache_path(dir.path()).network_config(net.build().unwrap());
    let cfg = builder.build().expect("authorities plus a fallback");
    let idents: Vec<Vec<u8>> = cfg
        .authorities()
        .iter()
        .map(|a| a.v3ident().as_bytes().to_vec())
        .collect();
    assert_eq!(idents, vec![vec![0x11; 20], vec![0x22; 20]]);
    assert_eq!(cfg.fallbacks().len(), 1);
    assert_eq!(cfg.fallbacks()[0].rsa_identity().as_bytes(), &[9u8; 20][..]);

    // A network that names only fallbacks keeps the default authorities.
    let mut net = NetworkConfig::builder();
    net.fallback(fallback(8));
    let net = net.build().unwrap();
    assert_eq!(net.authorities().len(), 9);
    assert_eq!(net.fallbacks().len(), 1);
}

#[test]
fn dirmgrcfg__an_authority_is_matched_by_the_identity_it_names() {
    let text = fixture_text("authcert.txt");
    let cert: AuthCert = AuthCert::parse(&text)
        .expect("fixture must parse")
        .dangerously_assume_wellsigned()
        .dangerously_assume_timely();
    let id = *cert.id_fingerprint();

    let auth = authority("fixture", *<&[u8; 20]>::try_from(id.as_bytes()).unwrap());
    assert_eq!(auth.v3ident(), &id);
    assert!(auth.matches_cert(&cert));
    assert!(auth.matches_keyid(cert.key_ids()));

    // The signing-key fingerprint is not the authority identity.
    let other = authority("wrong", *<&[u8; 20]>::try_from(cert.sk_fingerprint().as_bytes()).unwrap());
    assert!(!other.matches_cert(&cert));
    assert!(!other.matches_keyid(cert.key_ids()));
}

#[test]
fn dirmgrcfg__the_built_in_network_is_the_public_tor_directory() {
    let net = NetworkConfig::default();
    // The nine long-lived v3 directory authorities.
    assert_eq!(net.authorities().len(), 9);
    let idents: BTreeSet<String> = net
        .authorities()
        .iter()
        .map(|a| hex::encode_upper(a.v3ident().as_bytes()))
        .collect();
    assert_eq!(idents.len(), 9, "authority identities must be distinct");
    for known in [
        "D586D18309DED4CD6D57C18FDB97EFA96D330566", // moria1
        "14C131DFC5C6F93646BE72FA1401C02A8DF2E8B4", // tor26
        "E8A9C45EDE6D711294FADF8E7951F4DE6CA56B58", // dizum
    ] {
        assert!(idents.contains(known), "missing authority {}", known);
    }

    // The built-in fallback caches carry both identities and an ORPort.
    let fallbacks = net.fallbacks();
    assert!(
        fallbacks.len() >= 50,
        "expected a real fallback list, got {}",
        fallbacks.len()
    );
    for f in fallbacks.iter().take(10) {
        assert_eq!(f.rsa_identity().as_bytes().len(), 20);
        assert_eq!(f.ed_identity().as_bytes().len(), 32);
    }
}

#[test]
fn dirmgrcfg__parameter_overrides_reach_the_directory_they_configure() {
    let dir = tempfile::tempdir().unwrap();
    let mut builder = DirMgrConfigBuilder::new();
    builder
        .cache_path(dir.path())
        .override_net_param("circwindow".to_string(), 250)
        .override_net_param("NotAParameterAtAll".to_string(), 7);
    let cfg = builder.build().unwrap();

    let over = cfg.override_net_params();
    assert_eq!(over.get("circwindow"), Some(&250));
    assert_eq!(over.get("NotAParameterAtAll"), Some(&7));
    assert_eq!(over.get("circuit_window"), None);

    // These are exactly the parameters a directory is constructed with.
    let mut partial = PartialNetDir::new(load_consensus("consensus-md.txt"), Some(over));
    for md in load_mds("mds-all.txt") {
        assert!(partial.add_microdesc(md));
    }
    let netdir = partial.unwrap_if_sufficient().expect("sufficient");
    // The consensus says 888; the override says 250.
    assert_eq!(netdir.params().circuit_window.get(), 250);

    // The download schedule is part of the same configuration.
    let mut sched = DownloadScheduleConfig::builder();
    sched.microdesc_parallelism(7);
    assert_eq!(sched.build().unwrap().microdesc_parallelism(), 7);
    sched.microdesc_parallelism(0);
    assert_eq!(
        sched.build().unwrap().microdesc_parallelism(),
        1,
        "at least one download must be allowed"
    );
}
