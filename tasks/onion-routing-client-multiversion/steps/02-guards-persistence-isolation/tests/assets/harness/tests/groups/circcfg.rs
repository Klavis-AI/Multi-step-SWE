//! Group `circcfg` - the circuit manager's configuration objects.
//!
//! Graded through `tor_circmgr::{CircMgrConfig, CircMgrConfigBuilder,
//! CircuitTiming, CircuitTimingBuilder, PathConfig, PathConfigBuilder,
//! RequestTiming, RequestTimingBuilder}`. These carry the retry, loyalty and
//! dirtiness limits the milestone names; each is observable through `Debug`
//! and each must be settable from a builder and from a configuration file.

use std::time::Duration;
use tor_circmgr::{
    CircMgrConfig, CircMgrConfigBuilder, CircuitTiming, CircuitTimingBuilder, PathConfig,
    PathConfigBuilder, RequestTiming, RequestTimingBuilder,
};

/// Assert that a `Debug` rendering mentions each of `wanted`.
fn shows(what: &str, rendered: &str, wanted: &[&str]) {
    for w in wanted {
        assert!(
            rendered.contains(w),
            "{} did not report {:?}; got {}",
            what,
            w,
            rendered
        );
    }
}

#[test]
fn circcfg__request_timing_defaults_bound_retries_and_loyalty() {
    let d = format!("{:?}", RequestTiming::default());
    shows("RequestTiming::default()", &d, &["60s", "32", "50ms"]);

    // The `Default` impl and an empty builder must agree.
    let b = format!(
        "{:?}",
        RequestTimingBuilder::default()
            .build()
            .expect("an empty RequestTimingBuilder failed to build")
    );
    assert_eq!(d, b, "RequestTiming::default() disagreed with its builder");
}

#[test]
fn circcfg__request_timing_is_settable_from_a_builder() {
    let rt = RequestTimingBuilder::default()
        .request_timeout(Duration::from_secs(90))
        .request_max_retries(7)
        .request_loyalty(Duration::from_millis(275))
        .build()
        .expect("RequestTimingBuilder::build failed");
    let d = format!("{:?}", rt);
    shows("an overridden RequestTiming", &d, &["90s", "7", "275ms"]);
    assert!(
        !d.contains("60s") && !d.contains("50ms"),
        "the builder kept a default it was told to replace: {}",
        d
    );
}

#[test]
fn circcfg__request_timing_reads_human_readable_durations() {
    let rt: RequestTiming = serde_json::from_str(
        r#"{"request_timeout":"90s","request_max_retries":7,"request_loyalty":"250ms"}"#,
    )
    .expect("RequestTiming did not deserialize from its documented field names");
    shows("a deserialized RequestTiming", &format!("{:?}", rt), &["90s", "7", "250ms"]);

    // A duration that is not a duration is a configuration error, not a zero.
    let bad: Result<RequestTiming, _> = serde_json::from_str(
        r#"{"request_timeout":"whenever","request_max_retries":7,"request_loyalty":"250ms"}"#,
    );
    assert!(bad.is_err(), "an unparsable duration was accepted");
}

#[test]
fn circcfg__circuit_timing_limits_how_long_a_circuit_stays_dirty() {
    let d = format!("{:?}", CircuitTiming::default());
    shows("CircuitTiming::default()", &d, &["600s"]);

    let ct = CircuitTimingBuilder::default()
        .max_dirtiness(Duration::from_secs(90))
        .build()
        .expect("CircuitTimingBuilder::build failed");
    shows("an overridden CircuitTiming", &format!("{:?}", ct), &["90s"]);

    let parsed: CircuitTiming = serde_json::from_str(r#"{"max_dirtiness":"45s"}"#)
        .expect("CircuitTiming did not deserialize");
    shows("a deserialized CircuitTiming", &format!("{:?}", parsed), &["45s"]);
}

#[test]
fn circcfg__the_path_configuration_carries_the_subnet_distance() {
    // The default path rule is the default subnet rule.
    let d = format!("{:?}", PathConfig::default());
    let from_builder = format!(
        "{:?}",
        PathConfigBuilder::default()
            .build()
            .expect("an empty PathConfigBuilder failed to build")
    );
    assert_eq!(d, from_builder);
    shows("PathConfig::default()", &d, &["16", "32"]);

    let narrowed: PathConfig = serde_json::from_str(
        r#"{"enforce_distance":{"subnets_family_v4":24,"subnets_family_v6":64}}"#,
    )
    .expect("PathConfig did not deserialize");
    shows("a deserialized PathConfig", &format!("{:?}", narrowed), &["24", "64"]);
}

#[test]
fn circcfg__the_circuit_manager_configuration_composes_the_three_parts() {
    let d = format!("{:?}", CircMgrConfig::default());
    shows(
        "CircMgrConfig::default()",
        &d,
        &["60s", "32", "50ms", "600s", "16"],
    );

    let cfg = CircMgrConfigBuilder::default()
        .circuit_timing(
            CircuitTimingBuilder::default()
                .max_dirtiness(Duration::from_secs(120))
                .build()
                .expect("CircuitTimingBuilder::build failed"),
        )
        .request_timing(
            RequestTimingBuilder::default()
                .request_max_retries(3)
                .build()
                .expect("RequestTimingBuilder::build failed"),
        )
        .build()
        .expect("CircMgrConfigBuilder::build failed");
    let d = format!("{:?}", cfg);
    shows("an overridden CircMgrConfig", &d, &["120s", "3", "60s"]);
    assert!(
        !d.contains("600s"),
        "the circuit timing override was discarded: {}",
        d
    );
}
