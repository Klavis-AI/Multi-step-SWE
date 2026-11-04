//! Group `logcfg`: the operator-facing switch that turns sensitive logging on,
//! and its safe default.
//!
//! Traces to instruction.md section 1 ("safe logging on by default") and to the
//! migration note ("unless sensitive logging is explicitly enabled").
//! Interface: `arti::logging::{LoggingConfig, LoggingConfigBuilder}` and the
//! configuration key `log_sensitive_information`.

use arti::{LoggingConfig, LoggingConfigBuilder};

/// Build a `LoggingConfig` from a JSON fragment of the builder.
fn from_json(text: &str) -> LoggingConfig {
    let builder: LoggingConfigBuilder =
        serde_json::from_str(text).expect("the fragment should deserialize");
    builder.build().expect("the fragment should build")
}

#[test]
fn logcfg__the_default_keeps_sensitive_information_out_of_logs() {
    let explicitly_safe = LoggingConfig::builder()
        .log_sensitive_information(false)
        .build()
        .expect("builder should build");
    assert_eq!(LoggingConfig::default(), explicitly_safe);

    let unsafe_logging = LoggingConfig::builder()
        .log_sensitive_information(true)
        .build()
        .expect("builder should build");
    assert_ne!(LoggingConfig::default(), unsafe_logging);
}

#[test]
fn logcfg__the_key_is_named_log_sensitive_information() {
    let enabled = LoggingConfig::builder()
        .log_sensitive_information(true)
        .build()
        .expect("builder should build");
    assert_eq!(from_json(r#"{"log_sensitive_information": true}"#), enabled);
    assert_eq!(
        from_json(r#"{"log_sensitive_information": false}"#),
        LoggingConfig::default()
    );
}

#[test]
fn logcfg__omitting_the_key_yields_the_safe_default() {
    assert_eq!(from_json("{}"), LoggingConfig::default());
}

#[test]
fn logcfg__the_key_takes_a_boolean() {
    let bad = serde_json::from_str::<LoggingConfigBuilder>(
        r#"{"log_sensitive_information": "yes"}"#,
    );
    assert!(
        bad.is_err(),
        "a string should not be accepted for a boolean flag"
    );
}

#[test]
fn logcfg__the_flag_is_independent_of_the_other_logging_options() {
    let quiet_safe = LoggingConfig::builder()
        .console("info")
        .log_sensitive_information(false)
        .build()
        .expect("builder should build");
    let quiet_unsafe = LoggingConfig::builder()
        .console("info")
        .log_sensitive_information(true)
        .build()
        .expect("builder should build");
    assert_ne!(quiet_safe, quiet_unsafe);
    // Changing the console filter alone does not turn sensitive logging on.
    assert_ne!(quiet_safe, LoggingConfig::default());
    assert_ne!(quiet_unsafe, LoggingConfig::default());
}
