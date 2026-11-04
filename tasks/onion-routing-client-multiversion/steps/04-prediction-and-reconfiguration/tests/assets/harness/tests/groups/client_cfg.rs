//! Group `client_cfg`: the client facade's configuration gains a preemptive
//! circuit section and a stream-timeout section.
//!
//! `instruction.md` section 1 requires preemptive circuits and live
//! reconfiguration, and the migration note requires `connect` to split from
//! `connect_with_prefs`.  The settings those requests are governed by are
//! reachable from `arti_client::config::TorClientConfigBuilder`.

use std::time::Duration;

use arti_client::config::{
    circ, StreamTimeoutConfig, StreamTimeoutConfigBuilder, TorClientConfig, TorClientConfigBuilder,
};

#[test]
fn client_cfg__default_config_equals_an_untouched_builder() {
    assert_eq!(
        TorClientConfig::builder().build().expect("default build"),
        TorClientConfig::default()
    );
    assert_eq!(
        TorClientConfigBuilder::default()
            .build()
            .expect("default build"),
        TorClientConfig::default()
    );
}

#[test]
fn client_cfg__preemptive_circuits_section_is_reachable_from_the_builder() {
    let mut b = TorClientConfig::builder();
    b.preemptive_circuits()
        .disable_at_threshold(30)
        .initial_predicted_ports(vec![80, 443, 9001]);
    let changed = b.build().expect("build");
    assert_ne!(changed, TorClientConfig::default());

    // Writing the documented defaults back gives the default configuration.
    let mut b = TorClientConfig::builder();
    b.preemptive_circuits()
        .disable_at_threshold(12)
        .initial_predicted_ports(vec![80, 443])
        .prediction_lifetime(Duration::from_secs(3600))
        .min_exit_circs_for_port(2);
    assert_eq!(b.build().expect("build"), TorClientConfig::default());
}

#[test]
fn client_cfg__stream_timeout_defaults_are_ten_seconds_each() {
    let documented = StreamTimeoutConfig::builder()
        .connect_timeout(Duration::from_secs(10))
        .resolve_timeout(Duration::from_secs(10))
        .resolve_ptr_timeout(Duration::from_secs(10))
        .build()
        .expect("build");
    assert_eq!(StreamTimeoutConfig::default(), documented);
    assert_eq!(
        StreamTimeoutConfigBuilder::default()
            .build()
            .expect("build"),
        documented
    );
}

#[test]
fn client_cfg__each_stream_timeout_is_separately_observable() {
    let d = StreamTimeoutConfig::default();
    let connect = StreamTimeoutConfig::builder()
        .connect_timeout(Duration::from_secs(1))
        .build()
        .expect("build");
    let resolve = StreamTimeoutConfig::builder()
        .resolve_timeout(Duration::from_secs(1))
        .build()
        .expect("build");
    let ptr = StreamTimeoutConfig::builder()
        .resolve_ptr_timeout(Duration::from_secs(1))
        .build()
        .expect("build");

    let all = [&d, &connect, &resolve, &ptr];
    for (i, a) in all.iter().enumerate() {
        for (j, b) in all.iter().enumerate() {
            assert_eq!(i == j, a == b, "configs {} and {} compared wrongly", i, j);
        }
    }
}

#[test]
fn client_cfg__stream_timeouts_deserialize_as_humantime_durations() {
    let cfg: StreamTimeoutConfig = toml::from_str(
        r#"
connect_timeout = "30s"
resolve_timeout = "1m"
resolve_ptr_timeout = "500ms"
"#,
    )
    .expect("stream timeout section");
    assert_eq!(
        cfg,
        StreamTimeoutConfig::builder()
            .connect_timeout(Duration::from_secs(30))
            .resolve_timeout(Duration::from_secs(60))
            .resolve_ptr_timeout(Duration::from_millis(500))
            .build()
            .expect("build")
    );

    // Missing fields fall back to the defaults; unknown fields are refused.
    let partial: StreamTimeoutConfig =
        toml::from_str("connect_timeout = \"30s\"").expect("partial section");
    assert_eq!(
        partial,
        StreamTimeoutConfig::builder()
            .connect_timeout(Duration::from_secs(30))
            .build()
            .expect("build")
    );
    assert!(toml::from_str::<StreamTimeoutConfig>("connect_timout = \"30s\"").is_err());
}

#[test]
fn client_cfg__stream_timeouts_section_is_reachable_from_the_builder() {
    let mut b = TorClientConfig::builder();
    b.stream_timeouts()
        .connect_timeout(Duration::from_secs(7));
    let changed = b.build().expect("build");
    assert_ne!(changed, TorClientConfig::default());

    let mut b = TorClientConfig::builder();
    b.stream_timeouts()
        .connect_timeout(Duration::from_secs(10))
        .resolve_timeout(Duration::from_secs(10))
        .resolve_ptr_timeout(Duration::from_secs(10));
    assert_eq!(b.build().expect("build"), TorClientConfig::default());
}

#[test]
fn client_cfg__config_round_trips_through_its_builder() {
    let mut b = TorClientConfig::builder();
    b.preemptive_circuits().disable_at_threshold(21);
    b.stream_timeouts()
        .resolve_ptr_timeout(Duration::from_secs(3));
    let cfg = b.build().expect("build");

    let rebuilt = TorClientConfigBuilder::from(cfg.clone())
        .build()
        .expect("rebuild");
    assert_eq!(rebuilt, cfg);
    assert_ne!(rebuilt, TorClientConfig::default());

    // The `circ` re-export is the circuit manager's own type, so the two
    // crates agree on one preemptive configuration object.
    let from_circmgr: circ::PreemptiveCircuitConfig =
        tor_circmgr::PreemptiveCircuitConfig::builder()
            .disable_at_threshold(21)
            .build()
            .expect("build");
    assert_ne!(from_circmgr, circ::PreemptiveCircuitConfig::default());
}
