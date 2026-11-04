//! Group `preempt_config`: the configuration section that drives preemptive
//! circuit construction.
//!
//! `instruction.md` section 1: "Preemptive circuits built from port
//! prediction".  The knobs that describe that prediction are
//! `tor_circmgr::PreemptiveCircuitConfig`; every field is private, so each case
//! observes it through `Eq`, through `Default`, or through the deserializer.

use std::time::Duration;
use tor_circmgr::{PreemptiveCircuitConfig, PreemptiveCircuitConfigBuilder};

/// The values that `TEST_INTERFACE.md` documents as the defaults.
fn documented_defaults() -> PreemptiveCircuitConfig {
    PreemptiveCircuitConfig::builder()
        .disable_at_threshold(12)
        .initial_predicted_ports(vec![80, 443])
        .prediction_lifetime(Duration::from_secs(3600))
        .min_exit_circs_for_port(2)
        .build()
        .expect("builder rejected the documented default values")
}

#[test]
fn preempt_config__default_matches_documented_values() {
    assert_eq!(PreemptiveCircuitConfig::default(), documented_defaults());
    assert_eq!(
        PreemptiveCircuitConfigBuilder::default()
            .build()
            .expect("empty builder must succeed: every field has a default"),
        documented_defaults()
    );
}

#[test]
fn preempt_config__every_field_is_observable_through_equality() {
    let d = PreemptiveCircuitConfig::default();

    let threshold = PreemptiveCircuitConfig::builder()
        .disable_at_threshold(13)
        .build()
        .expect("build");
    let ports = PreemptiveCircuitConfig::builder()
        .initial_predicted_ports(vec![80])
        .build()
        .expect("build");
    let lifetime = PreemptiveCircuitConfig::builder()
        .prediction_lifetime(Duration::from_secs(3601))
        .build()
        .expect("build");
    let min_circs = PreemptiveCircuitConfig::builder()
        .min_exit_circs_for_port(3)
        .build()
        .expect("build");

    assert_ne!(threshold, d);
    assert_ne!(ports, d);
    assert_ne!(lifetime, d);
    assert_ne!(min_circs, d);

    // ... and each of those differs from the others, so no field is aliasing
    // another.
    let all = [&d, &threshold, &ports, &lifetime, &min_circs];
    for (i, a) in all.iter().enumerate() {
        for (j, b) in all.iter().enumerate() {
            assert_eq!(i == j, a == b, "configs {} and {} compared wrongly", i, j);
        }
    }
}

#[test]
fn preempt_config__deserializes_every_field_from_toml() {
    let cfg: PreemptiveCircuitConfig = toml::from_str(
        r#"
disable_at_threshold = 7
initial_predicted_ports = [22, 9001]
prediction_lifetime = "45m"
min_exit_circs_for_port = 5
"#,
    )
    .expect("could not deserialize a fully-populated preemptive section");

    let expected = PreemptiveCircuitConfig::builder()
        .disable_at_threshold(7)
        .initial_predicted_ports(vec![22, 9001])
        .prediction_lifetime(Duration::from_secs(45 * 60))
        .min_exit_circs_for_port(5)
        .build()
        .expect("build");

    assert_eq!(cfg, expected);
}

#[test]
fn preempt_config__prediction_lifetime_is_a_humantime_duration() {
    let one_hour: PreemptiveCircuitConfig =
        toml::from_str("prediction_lifetime = \"1h\"").expect("1h");
    let ninety_s: PreemptiveCircuitConfig =
        toml::from_str("prediction_lifetime = \"90s\"").expect("90s");
    let json: PreemptiveCircuitConfig =
        serde_json::from_str(r#"{"prediction_lifetime": "2h"}"#).expect("json 2h");

    assert_eq!(
        one_hour,
        PreemptiveCircuitConfig::builder()
            .prediction_lifetime(Duration::from_secs(3600))
            .build()
            .expect("build")
    );
    assert_eq!(
        ninety_s,
        PreemptiveCircuitConfig::builder()
            .prediction_lifetime(Duration::from_secs(90))
            .build()
            .expect("build")
    );
    assert_eq!(
        json,
        PreemptiveCircuitConfig::builder()
            .prediction_lifetime(Duration::from_secs(7200))
            .build()
            .expect("build")
    );
    // A bare integer is not a humantime duration.
    assert!(toml::from_str::<PreemptiveCircuitConfig>("prediction_lifetime = 3600").is_err());
}

#[test]
fn preempt_config__missing_fields_take_their_defaults() {
    let cfg: PreemptiveCircuitConfig =
        toml::from_str("disable_at_threshold = 4").expect("partial section");
    let expected = PreemptiveCircuitConfig::builder()
        .disable_at_threshold(4)
        .build()
        .expect("build");
    assert_eq!(cfg, expected);

    let empty: PreemptiveCircuitConfig = toml::from_str("").expect("empty section");
    assert_eq!(empty, PreemptiveCircuitConfig::default());
}

#[test]
fn preempt_config__unknown_fields_are_rejected() {
    assert!(toml::from_str::<PreemptiveCircuitConfig>("disable_at_thresold = 4").is_err());
    assert!(toml::from_str::<PreemptiveCircuitConfig>(
        "disable_at_threshold = 4\nnumber_of_circuits = 9\n"
    )
    .is_err());
    assert!(serde_json::from_str::<PreemptiveCircuitConfig>(r#"{"lifetime": "1h"}"#).is_err());
}

#[test]
fn preempt_config__converts_back_into_a_builder() {
    let cfg = PreemptiveCircuitConfig::builder()
        .disable_at_threshold(3)
        .initial_predicted_ports(vec![9050, 9150])
        .prediction_lifetime(Duration::from_secs(120))
        .min_exit_circs_for_port(4)
        .build()
        .expect("build");

    let mut builder: PreemptiveCircuitConfigBuilder = cfg.clone().into();
    assert_eq!(builder.build().expect("rebuild"), cfg);

    // The round-tripped builder is a real builder: it can still be overridden.
    let changed = builder
        .min_exit_circs_for_port(11)
        .build()
        .expect("rebuild after override");
    assert_ne!(changed, cfg);
    assert_eq!(
        changed,
        PreemptiveCircuitConfig::builder()
            .disable_at_threshold(3)
            .initial_predicted_ports(vec![9050, 9150])
            .prediction_lifetime(Duration::from_secs(120))
            .min_exit_circs_for_port(11)
            .build()
            .expect("build")
    );
}

#[test]
fn preempt_config__initial_predicted_ports_keeps_order_and_duplicates() {
    let cfg: PreemptiveCircuitConfig =
        toml::from_str("initial_predicted_ports = [443, 80, 443]").expect("ports");
    assert_eq!(
        cfg,
        PreemptiveCircuitConfig::builder()
            .initial_predicted_ports(vec![443, 80, 443])
            .build()
            .expect("build")
    );
    assert_ne!(
        cfg,
        PreemptiveCircuitConfig::builder()
            .initial_predicted_ports(vec![80, 443, 443])
            .build()
            .expect("build")
    );
    // Ports are 16-bit.
    assert!(toml::from_str::<PreemptiveCircuitConfig>("initial_predicted_ports = [65536]").is_err());
}
