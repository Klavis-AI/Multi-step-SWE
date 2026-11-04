//! Group `errtaxonomy`: the structured error taxonomy — `tor_error::ErrorKind`
//! plus the `HasKind` trait that classifies every error in the workspace.
//!
//! Inherited tier.  The taxonomy was introduced at step 06; step 07's
//! instruction says it must be *preserved*, so every case here is written
//! against behaviour that already held at the previous checkpoint and must
//! still hold on the cumulative tree.  Nothing here depends on a kind or a
//! constructor that step 07 adds, deliberately: this group is the floor a
//! no-op continuation earns, and a floor that needs new code is not a floor.
//!
//! Graded items: `tor_error::{ErrorKind, HasKind, Bug, internal!,
//! bad_api_usage!, into_internal!}`, `tor_config::{ConfigBuildError,
//! ReconfigureError, Reconfigure}`, `tor_socksproto::Error`.

use std::error::Error as _;

use tor_config::{ConfigBuildError, Reconfigure, ReconfigureError};
use tor_error::{Bug, ErrorKind, HasKind};
use tor_socksproto::SocksHandshake;

#[test]
fn errtaxonomy__internal_bug_reports_kind_internal_and_carries_its_message() {
    let bug: Bug = tor_error::internal!("could not {} the {}", "wobble", "device");

    // Classification is programmatic, through `HasKind`.
    assert_eq!(bug.kind(), ErrorKind::Internal);

    // The rendered form names the kind and repeats the formatted message, so a
    // human reading a log gets more than the classification.
    let rendered = bug.to_string();
    assert!(
        rendered.starts_with("internal error (bug) at "),
        "unexpected rendering: {rendered}"
    );
    assert!(
        rendered.contains("could not wobble the device"),
        "message was dropped: {rendered}"
    );
    // A bare `internal!` has no underlying cause.
    assert!(bug.source().is_none());
    // And it is a real `std::error::Error`, not just a Display type.
    let as_std: &dyn std::error::Error = &bug;
    assert!(as_std.to_string().contains("could not wobble the device"));
}

#[test]
fn errtaxonomy__bad_api_usage_is_a_separate_kind_from_internal() {
    let misuse: Bug = tor_error::bad_api_usage!("no targets supplied");
    let internal: Bug = tor_error::internal!("no targets supplied");

    assert_eq!(misuse.kind(), ErrorKind::BadApiUsage);
    assert_eq!(internal.kind(), ErrorKind::Internal);
    assert_ne!(misuse.kind(), internal.kind());

    // The two kinds are distinguishable in their rendered form as well.
    assert!(
        misuse.to_string().starts_with("bad API usage (bug) at "),
        "unexpected rendering: {}",
        misuse
    );
    assert_ne!(
        format!("{}", ErrorKind::BadApiUsage),
        format!("{}", ErrorKind::Internal)
    );
}

#[test]
fn errtaxonomy__into_internal_preserves_the_underlying_error_as_a_source() {
    // The documented way to lift a foreign error into the taxonomy: a closure
    // suitable for `Result::map_err`.
    let utf8_err = std::str::from_utf8(&[0xff_u8, 0xfe]).expect_err("not valid UTF-8");
    let expected_source = utf8_err.to_string();

    let lifted: Bug = std::str::from_utf8(&[0xff_u8, 0xfe])
        .map_err(tor_error::into_internal!("bad bytes from {}", "the cache"))
        .expect_err("conversion must preserve the failure");

    assert_eq!(lifted.kind(), ErrorKind::Internal);
    assert!(
        lifted.to_string().contains("bad bytes from the cache"),
        "context was dropped: {lifted}"
    );
    let source = lifted.source().expect("the foreign error is kept as source");
    assert_eq!(source.to_string(), expected_source);
}

#[test]
fn errtaxonomy__configuration_build_failures_are_invalid_config() {
    let missing = ConfigBuildError::MissingField {
        field: "socks_port".to_owned(),
    };
    let invalid = ConfigBuildError::Invalid {
        field: "socks_port".to_owned(),
        problem: "out of range".to_owned(),
    };
    let inconsistent = ConfigBuildError::Inconsistent {
        fields: vec!["authorities".to_owned(), "fallbacks".to_owned()],
        problem: "override both or neither".to_owned(),
    };

    for e in [&missing, &invalid, &inconsistent] {
        assert_eq!(e.kind(), ErrorKind::InvalidConfig, "for {e:?}");
    }

    // Each variant renders the offending field, and `within` re-roots the
    // field name under a section so a nested builder can report a full path.
    assert_eq!(
        missing.to_string(),
        "Field was not provided: socks_port".to_owned()
    );
    assert_eq!(
        missing.within("proxy").to_string(),
        "Field was not provided: proxy.socks_port".to_owned()
    );
    assert_eq!(
        invalid.within("proxy").to_string(),
        "Value of proxy.socks_port was incorrect: out of range".to_owned()
    );
    assert_eq!(
        inconsistent.within("tor_network").to_string(),
        r#"Fields ["tor_network.authorities", "tor_network.fallbacks"] are inconsistent: override both or neither"#
            .to_owned()
    );
    // Re-rooting does not change the classification.
    assert_eq!(missing.within("proxy").kind(), ErrorKind::InvalidConfig);
}

#[test]
fn errtaxonomy__runtime_reconfiguration_failures_are_a_distinct_kind() {
    // `AllOrNothing` and `CheckAllOrNothing` refuse a field that cannot change
    // on a running client; `WarnOnFailures` accepts it and logs instead.
    for how in [Reconfigure::AllOrNothing, Reconfigure::CheckAllOrNothing] {
        let err = how
            .cannot_change("storage.cache_path")
            .expect_err("a frozen field must be refused");
        match &err {
            ReconfigureError::CannotChange { field } => {
                assert_eq!(field, "storage.cache_path");
            }
            other => panic!("unexpected variant: {other:?}"),
        }
        assert_eq!(err.kind(), ErrorKind::InvalidConfigTransition);
        assert_ne!(err.kind(), ErrorKind::InvalidConfig);
        assert_eq!(
            err.to_string(),
            "Cannot change storage.cache_path on a running client.".to_owned()
        );
    }
    assert!(Reconfigure::WarnOnFailures
        .cannot_change("storage.cache_path")
        .is_ok());
}

#[test]
fn errtaxonomy__a_malformed_socks_greeting_is_a_local_protocol_violation() {
    // Version 9 is not a SOCKS version: the handshake rejects it rather than
    // asking for more bytes, and the failure is classified as a violation of
    // the protocol spoken to us locally — not as a Tor-network problem.
    let mut hs = SocksHandshake::new();
    let outcome = hs
        .handshake(&[9, 1, 0])
        .expect("three bytes are enough to see the version");
    let err = outcome.expect_err("version 9 must be rejected");

    assert!(
        matches!(err, tor_socksproto::Error::BadProtocol(9)),
        "unexpected error: {err:?}"
    );
    assert_eq!(err.kind(), ErrorKind::LocalProtocolViolation);
    assert_ne!(err.kind(), ErrorKind::TorProtocolViolation);

    // A feature we do not implement is a different kind again.
    assert_eq!(
        tor_socksproto::Error::NotImplemented.kind(),
        ErrorKind::NotImplemented
    );
}

#[test]
fn errtaxonomy__has_kind_is_object_safe_and_dispatches_dynamically() {
    // The whole point of the taxonomy: an application holds erased errors and
    // still classifies them.
    let erased: Vec<Box<dyn HasKind>> = vec![
        Box::new(tor_error::internal!("a bug")),
        Box::new(tor_error::bad_api_usage!("caller error")),
        Box::new(ConfigBuildError::MissingField {
            field: "dns_port".to_owned(),
        }),
        Box::new(ReconfigureError::CannotChange {
            field: "network.authorities".to_owned(),
        }),
    ];
    let kinds: Vec<ErrorKind> = erased.iter().map(|e| e.kind()).collect();
    assert_eq!(
        kinds,
        vec![
            ErrorKind::Internal,
            ErrorKind::BadApiUsage,
            ErrorKind::InvalidConfig,
            ErrorKind::InvalidConfigTransition,
        ]
    );
}

#[test]
fn errtaxonomy__every_kind_in_the_step_06_taxonomy_renders_distinctly() {
    // `ErrorKind` is a classification an application matches on, so the
    // variants must be mutually unequal, and each must have its own
    // human-readable label.  Debug and Display are different renderings: the
    // former names the variant, the latter describes it.
    let kinds = [
        ("TorAccessFailed", ErrorKind::TorAccessFailed),
        ("BootstrapRequired", ErrorKind::BootstrapRequired),
        ("DirectoryExpired", ErrorKind::DirectoryExpired),
        (
            "PersistentStateAccessFailed",
            ErrorKind::PersistentStateAccessFailed,
        ),
        ("CacheCorrupted", ErrorKind::CacheCorrupted),
        ("ReactorShuttingDown", ErrorKind::ReactorShuttingDown),
        ("ArtiShuttingDown", ErrorKind::ArtiShuttingDown),
        ("InvalidConfig", ErrorKind::InvalidConfig),
        ("InvalidConfigTransition", ErrorKind::InvalidConfigTransition),
        ("NoHomeDirectory", ErrorKind::NoHomeDirectory),
        ("NotImplemented", ErrorKind::NotImplemented),
        ("FeatureDisabled", ErrorKind::FeatureDisabled),
        ("LocalProtocolViolation", ErrorKind::LocalProtocolViolation),
        ("TorProtocolViolation", ErrorKind::TorProtocolViolation),
        ("LocalNetworkError", ErrorKind::LocalNetworkError),
        ("RelayIdMismatch", ErrorKind::RelayIdMismatch),
        ("CircuitCollapse", ErrorKind::CircuitCollapse),
        ("TorNetworkTimeout", ErrorKind::TorNetworkTimeout),
        ("TorDirectoryError", ErrorKind::TorDirectoryError),
        ("RemoteStreamClosed", ErrorKind::RemoteStreamClosed),
        ("RemoteStreamReset", ErrorKind::RemoteStreamReset),
        ("RemoteConnectionRefused", ErrorKind::RemoteConnectionRefused),
        ("ExitPolicyRejected", ErrorKind::ExitPolicyRejected),
        ("ExitTimeout", ErrorKind::ExitTimeout),
        ("RemoteNetworkFailed", ErrorKind::RemoteNetworkFailed),
        ("RemoteHostNotFound", ErrorKind::RemoteHostNotFound),
        ("RemoteProtocolFailed", ErrorKind::RemoteProtocolFailed),
        ("RelayTooBusy", ErrorKind::RelayTooBusy),
        ("InvalidStreamTarget", ErrorKind::InvalidStreamTarget),
        ("ForbiddenStreamTarget", ErrorKind::ForbiddenStreamTarget),
        ("TransientFailure", ErrorKind::TransientFailure),
        ("BadApiUsage", ErrorKind::BadApiUsage),
        ("CircuitRefused", ErrorKind::CircuitRefused),
        ("NoPath", ErrorKind::NoPath),
        ("NoExit", ErrorKind::NoExit),
        ("Internal", ErrorKind::Internal),
    ];

    let mut labels: Vec<String> = Vec::new();
    for (name, kind) in kinds.iter() {
        // Debug names the variant exactly.
        assert_eq!(&format!("{:?}", kind), name);
        // Display is a separate, non-empty, human-readable label.
        let label = format!("{}", kind);
        assert!(!label.is_empty(), "{name} has an empty Display");
        assert_ne!(&label, name, "{name}: Display must not just be the variant");
        labels.push(label);
    }

    let mut sorted = labels.clone();
    sorted.sort();
    sorted.dedup();
    assert_eq!(
        sorted.len(),
        kinds.len(),
        "two kinds share a label; labels were {labels:?}"
    );

    // Copy + Eq: a kind can be stashed and compared later.
    let k = ErrorKind::NoExit;
    let copy = k;
    assert_eq!(k, copy);
    assert_ne!(k, ErrorKind::NoPath);
}
