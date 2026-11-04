//! Group `preemptive` (inherited from step 4) — the preemptive-circuit
//! configuration must survive this step's changes intact.
//!
//! The fields are private, so the observables are: equality against a
//! builder-built value, the derived `Debug` rendering, and TOML
//! deserialization including its rejection of unknown keys.

use std::time::Duration;

use tor_circmgr::{PreemptiveCircuitConfig, PreemptiveCircuitConfigBuilder};

fn defaults() -> PreemptiveCircuitConfig {
    PreemptiveCircuitConfig::default()
}

#[test]
fn preemptive__defaults_match_the_documented_values() {
    assert_eq!(
        format!("{:?}", defaults()),
        "PreemptiveCircuitConfig { disable_at_threshold: 12, \
         initial_predicted_ports: [80, 443], prediction_lifetime: 3600s, \
         min_exit_circs_for_port: 2 }"
    );
}

#[test]
fn preemptive__builder_default_equals_default() {
    let built = PreemptiveCircuitConfig::builder()
        .build()
        .expect("hardwired defaults must build");
    assert_eq!(built, defaults());
    let via_type = PreemptiveCircuitConfigBuilder::default()
        .build()
        .expect("builder defaults must build");
    assert_eq!(via_type, defaults());
}

#[test]
fn preemptive__threshold_can_be_overridden() {
    let cfg = PreemptiveCircuitConfig::builder()
        .disable_at_threshold(3)
        .build()
        .expect("build");
    assert_ne!(cfg, defaults());
    assert!(format!("{cfg:?}").contains("disable_at_threshold: 3"));
    // Only the field that was set changes.
    assert!(format!("{cfg:?}").contains("min_exit_circs_for_port: 2"));
}

#[test]
fn preemptive__predicted_ports_can_be_overridden() {
    let cfg = PreemptiveCircuitConfig::builder()
        .initial_predicted_ports(vec![9050, 9150])
        .min_exit_circs_for_port(5)
        .build()
        .expect("build");
    assert!(format!("{cfg:?}").contains("initial_predicted_ports: [9050, 9150]"));
    assert!(format!("{cfg:?}").contains("min_exit_circs_for_port: 5"));
}

#[test]
fn preemptive__config_deserializes_from_toml() {
    // Durations are written the way a human writes them, and an omitted key
    // falls back to the documented default.
    let cfg: PreemptiveCircuitConfig = toml::from_str(
        r#"
            disable_at_threshold = 4
            prediction_lifetime = "1 hour"
        "#,
    )
    .expect("valid configuration");
    let want = PreemptiveCircuitConfig::builder()
        .disable_at_threshold(4)
        .prediction_lifetime(Duration::from_secs(3600))
        .build()
        .expect("build");
    assert_eq!(cfg, want);

    let empty: PreemptiveCircuitConfig = toml::from_str("").expect("empty configuration");
    assert_eq!(empty, defaults());
}

#[test]
fn preemptive__unknown_fields_are_rejected() {
    let err = toml::from_str::<PreemptiveCircuitConfig>("disable_at_threhsold = 4")
        .err()
        .expect("a misspelled key must not be silently ignored");
    assert!(
        err.to_string().contains("disable_at_threhsold"),
        "the error should name the offending key: {err}"
    );
}

#[test]
fn preemptive__config_round_trips_through_its_builder() {
    let original = PreemptiveCircuitConfig::builder()
        .disable_at_threshold(7)
        .initial_predicted_ports(vec![22])
        .prediction_lifetime(Duration::from_secs(90))
        .min_exit_circs_for_port(1)
        .build()
        .expect("build");
    let rebuilt = PreemptiveCircuitConfigBuilder::from(original.clone())
        .build()
        .expect("rebuild");
    assert_eq!(rebuilt, original);
}
