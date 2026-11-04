//! Group `cfg_builderr` - the error type every configuration builder reports.
//!
//! instruction.md section 1: "a rewritten configuration system built around
//! builders". Builders need one error vocabulary, and section names have to be
//! attached to errors raised by the section's own builder.
//!
//! Graded items: `tor_config::ConfigBuildError` (its three variants and their
//! messages), `ConfigBuildError::within`, and the two variants as they are
//! raised by a real configuration builder
//! (`arti_client::config::dir::FallbackDir::builder`).

use tor_config::ConfigBuildError;

#[test]
fn cfg_builderr__missing_field_message() {
    let e = ConfigBuildError::MissingField {
        field: "lettuce".to_owned(),
    };
    assert_eq!(e.to_string(), "Field was not provided: lettuce");

    // A real configuration builder raises exactly this variant when a
    // mandatory field is left unset: `FallbackDir` requires an RSA identity.
    let err = arti_client::config::dir::FallbackDir::builder()
        .build()
        .expect_err("a FallbackDir with no fields set must not build");
    assert_eq!(err.to_string(), "Field was not provided: rsa_identity");
}

#[test]
fn cfg_builderr__invalid_field_message() {
    let e = ConfigBuildError::Invalid {
        field: "tomato".to_owned(),
        problem: "too crunchy".to_owned(),
    };
    assert_eq!(e.to_string(), "Value of tomato was incorrect: too crunchy");

    // A real configuration builder raises exactly this variant when a field is
    // set to a value it cannot use: a fallback directory needs at least one
    // ORPort.
    let err = arti_client::config::dir::FallbackDir::builder()
        .orports(Vec::new())
        .build()
        .expect_err("a FallbackDir with an empty ORPort list must not build");
    assert_eq!(err.to_string(), "Value of orport was incorrect: list was empty");
}

#[test]
fn cfg_builderr__inconsistent_fields_message() {
    let e = ConfigBuildError::Inconsistent {
        fields: vec!["mayo".to_owned(), "avocado".to_owned()],
        problem: "pick one".to_owned(),
    };
    assert_eq!(
        e.to_string(),
        r#"Fields ["mayo", "avocado"] are inconsistent: pick one"#
    );
}

#[test]
fn cfg_builderr__within_prefixes_a_single_field() {
    let missing = ConfigBuildError::MissingField {
        field: "lettuce".to_owned(),
    };
    assert_eq!(
        missing.within("sandwich").to_string(),
        "Field was not provided: sandwich.lettuce"
    );

    let invalid = ConfigBuildError::Invalid {
        field: "tomato".to_owned(),
        problem: "too crunchy".to_owned(),
    };
    assert_eq!(
        invalid.within("sandwich").to_string(),
        "Value of sandwich.tomato was incorrect: too crunchy"
    );
}

#[test]
fn cfg_builderr__within_prefixes_every_inconsistent_field() {
    let e = ConfigBuildError::Inconsistent {
        fields: vec!["mayo".to_owned(), "avocado".to_owned()],
        problem: "pick one".to_owned(),
    };
    assert_eq!(
        e.within("sandwich").to_string(),
        r#"Fields ["sandwich.mayo", "sandwich.avocado"] are inconsistent: pick one"#
    );

    // Nesting composes: the outermost section is applied last and so appears
    // leftmost.
    assert_eq!(
        e.within("sandwich").within("lunch").to_string(),
        r#"Fields ["lunch.sandwich.mayo", "lunch.sandwich.avocado"] are inconsistent: pick one"#
    );
}
