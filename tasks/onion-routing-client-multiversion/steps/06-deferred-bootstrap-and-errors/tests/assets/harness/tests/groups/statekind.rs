//! Group `statekind`: stable kinds for persistent state and configuration.
//!
//! instruction.md: "a structured error taxonomy", "Configuration-file
//! watching". Watching a configuration file only makes sense if a bad reload
//! can be reported precisely: which field, and whether the change was merely
//! wrong or simply not changeable while running. Persistent state has the same
//! need: "cannot reach the file" and "the file is nonsense" call for different
//! responses.

use std::sync::Arc;

use tor_config::{ConfigBuildError, ReconfigureError};
use tor_error::{ErrorKind, HasKind};
use tor_persist::Error;

/// A genuine `serde_json::Error` from a failed parse.
fn json_err() -> Arc<serde_json::Error> {
    Arc::new(serde_json::from_str::<u32>("{not json}").expect_err("this is not JSON"))
}

#[test]
fn statekind__unreachable_state_is_an_access_failure() {
    let e = Error::IoError(Arc::new(std::io::Error::new(
        std::io::ErrorKind::PermissionDenied,
        "denied",
    )));
    assert_eq!(e.kind(), ErrorKind::PersistentStateAccessFailed);
    assert_eq!(e.to_string(), "IO error");
}

#[test]
fn statekind__saving_without_the_lock_is_bad_api_usage() {
    assert_eq!(Error::NoLock.kind(), ErrorKind::BadApiUsage);
    assert_eq!(Error::NoLock.to_string(), "Storage not locked");
}

#[test]
fn statekind__unreadable_state_is_corruption_but_unwritable_state_is_our_bug() {
    // We wrote the state, so failing to serialize it is our fault; failing to
    // read it back means what is on disk is not what we wrote.
    assert_eq!(Error::Deserialize(json_err()).kind(), ErrorKind::PersistentStateCorrupted);
    assert_eq!(Error::Serialize(json_err()).kind(), ErrorKind::Internal);
}

#[test]
fn statekind__a_rejected_setting_is_an_invalid_configuration() {
    let missing = ConfigBuildError::MissingField {
        field: "state_dir".into(),
    };
    let invalid = ConfigBuildError::Invalid {
        field: "socks_port".into(),
        problem: "not a port number".into(),
    };
    let inconsistent = ConfigBuildError::Inconsistent {
        fields: vec!["cache_dir".into(), "state_dir".into()],
        problem: "must differ".into(),
    };
    for e in [&missing, &invalid, &inconsistent] {
        assert_eq!(e.kind(), ErrorKind::InvalidConfig);
    }
}

#[test]
fn statekind__a_rejected_setting_names_the_field() {
    assert_eq!(
        ConfigBuildError::MissingField {
            field: "state_dir".into()
        }
        .to_string(),
        "Field was not provided: state_dir"
    );
    assert_eq!(
        ConfigBuildError::Invalid {
            field: "socks_port".into(),
            problem: "not a port number".into()
        }
        .to_string(),
        "Value of socks_port was incorrect: not a port number"
    );
    assert_eq!(
        ConfigBuildError::Inconsistent {
            fields: vec!["cache_dir".into(), "state_dir".into()],
            problem: "must differ".into()
        }
        .to_string(),
        r#"Fields ["cache_dir", "state_dir"] are inconsistent: must differ"#
    );
}

#[test]
fn statekind__a_nested_section_prefixes_the_field_name() {
    // This is how a builder for one section reports the failure of a builder
    // for a section inside it.
    let e = ConfigBuildError::MissingField {
        field: "cache_dir".into(),
    };
    assert_eq!(
        e.within("storage").to_string(),
        "Field was not provided: storage.cache_dir"
    );
    assert_eq!(
        e.within("storage").within("arti").to_string(),
        "Field was not provided: arti.storage.cache_dir"
    );
    let inconsistent = ConfigBuildError::Inconsistent {
        fields: vec!["a".into(), "b".into()],
        problem: "pick one".into(),
    };
    assert_eq!(
        inconsistent.within("section").to_string(),
        r#"Fields ["section.a", "section.b"] are inconsistent: pick one"#
    );
    // `within` does not consume or mutate the original.
    assert_eq!(e.to_string(), "Field was not provided: cache_dir");
}

#[test]
fn statekind__a_setting_that_cannot_change_while_running_is_its_own_kind() {
    let e = ReconfigureError::CannotChange {
        field: "storage.state_dir".into(),
    };
    assert_eq!(e.kind(), ErrorKind::InvalidConfigTransition);
    assert_eq!(
        e.to_string(),
        "Cannot change storage.state_dir on a running client."
    );
    // A change that is merely invalid is not the same as a change that is
    // impossible while running.
    assert_ne!(ErrorKind::InvalidConfigTransition, ErrorKind::InvalidConfig);
}
