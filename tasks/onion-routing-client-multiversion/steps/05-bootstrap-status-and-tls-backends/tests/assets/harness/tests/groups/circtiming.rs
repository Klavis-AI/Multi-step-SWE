//! Group `circtiming` (inherited from step 4) — circuit timing configuration.
//!
//! Same shape as `preemptive`: private fields, so the graded observables are
//! equality, the derived `Debug` rendering, and TOML deserialization.

use std::time::Duration;

use tor_circmgr::{CircuitTiming, CircuitTimingBuilder};

fn defaults() -> CircuitTiming {
    CircuitTiming::default()
}

#[test]
fn circtiming__defaults_match_the_documented_values() {
    assert_eq!(
        format!("{:?}", defaults()),
        "CircuitTiming { max_dirtiness: 600s, request_timeout: 60s, \
         request_max_retries: 32, request_loyalty: 50ms }"
    );
}

#[test]
fn circtiming__builder_default_equals_default() {
    assert_eq!(
        CircuitTiming::builder().build().expect("build"),
        defaults()
    );
    assert_eq!(
        CircuitTimingBuilder::default().build().expect("build"),
        defaults()
    );
}

#[test]
fn circtiming__individual_fields_can_be_overridden() {
    let cfg = CircuitTiming::builder()
        .max_dirtiness(Duration::from_secs(5))
        .request_max_retries(2)
        .build()
        .expect("build");
    assert_ne!(cfg, defaults());
    let text = format!("{cfg:?}");
    assert!(text.contains("max_dirtiness: 5s"), "{text}");
    assert!(text.contains("request_max_retries: 2"), "{text}");
    // Untouched fields keep their defaults.
    assert!(text.contains("request_timeout: 60s"), "{text}");
    assert!(text.contains("request_loyalty: 50ms"), "{text}");
}

#[test]
fn circtiming__config_deserializes_from_toml() {
    let cfg: CircuitTiming = toml::from_str(
        r#"
            max_dirtiness = "10 minutes"
            request_timeout = "60 seconds"
            request_loyalty = "50ms"
        "#,
    )
    .expect("valid configuration");
    assert_eq!(cfg, defaults());

    let empty: CircuitTiming = toml::from_str("").expect("empty configuration");
    assert_eq!(empty, defaults());
}

#[test]
fn circtiming__unknown_fields_are_rejected() {
    let err = toml::from_str::<CircuitTiming>("request_timeou = \"1s\"")
        .err()
        .expect("a misspelled key must not be silently ignored");
    assert!(
        err.to_string().contains("request_timeou"),
        "the error should name the offending key: {err}"
    );
}

#[test]
fn circtiming__config_round_trips_through_its_builder() {
    let original = CircuitTiming::builder()
        .max_dirtiness(Duration::from_secs(120))
        .request_timeout(Duration::from_secs(30))
        .request_max_retries(4)
        .request_loyalty(Duration::from_millis(250))
        .build()
        .expect("build");
    let rebuilt = CircuitTimingBuilder::from(original.clone())
        .build()
        .expect("rebuild");
    assert_eq!(rebuilt, original);
}
