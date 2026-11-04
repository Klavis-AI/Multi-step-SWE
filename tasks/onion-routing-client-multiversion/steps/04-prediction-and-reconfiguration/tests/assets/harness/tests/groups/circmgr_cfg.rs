//! Group `circmgr_cfg`: the circuit manager's configuration object now carries
//! a preemptive-circuit section alongside the path rules and circuit timing.
//!
//! `instruction.md` section 1 requires preemptive circuits and live
//! reconfiguration; `CircMgrConfig` is the object both of those are expressed
//! in, and `CircMgr::new`/`CircMgr::reconfigure` destructure exactly these
//! three sections.

use std::time::Duration;
use tor_circmgr::{
    CircMgrConfig, CircMgrConfigBuilder, CircuitTiming, PathConfig, PreemptiveCircuitConfig,
};

#[test]
fn circmgr_cfg__default_is_the_three_default_sections() {
    let built = CircMgrConfig::builder()
        .path_rules(PathConfig::default())
        .circuit_timing(CircuitTiming::default())
        .preemptive_circuits(PreemptiveCircuitConfig::default())
        .build()
        .expect("all three sections given");
    assert_eq!(built, CircMgrConfig::default());
    assert_eq!(
        CircMgrConfigBuilder::default()
            .build()
            .expect("every section has a default"),
        CircMgrConfig::default()
    );
}

#[test]
fn circmgr_cfg__preemptive_section_is_part_of_the_config() {
    let changed = CircMgrConfig::builder()
        .preemptive_circuits(
            PreemptiveCircuitConfig::builder()
                .disable_at_threshold(30)
                .build()
                .expect("build"),
        )
        .build()
        .expect("build");
    assert_ne!(changed, CircMgrConfig::default());

    // Setting it back to the default value restores equality: the section is
    // compared by value, not by identity.
    let restored = CircMgrConfig::builder()
        .preemptive_circuits(PreemptiveCircuitConfig::default())
        .build()
        .expect("build");
    assert_eq!(restored, CircMgrConfig::default());
}

#[test]
fn circmgr_cfg__path_rules_section_is_part_of_the_config() {
    let changed = CircMgrConfig::builder()
        .path_rules(
            PathConfig::builder(&PathConfig::default())
                .ipv4_subnet_family_prefix(24)
                .build()
                .expect("build"),
        )
        .build()
        .expect("build");
    assert_ne!(changed, CircMgrConfig::default());
    assert_ne!(
        changed,
        CircMgrConfig::builder()
            .preemptive_circuits(
                PreemptiveCircuitConfig::builder()
                    .disable_at_threshold(30)
                    .build()
                    .expect("build")
            )
            .build()
            .expect("build")
    );
}

#[test]
fn circmgr_cfg__circuit_timing_section_is_part_of_the_config() {
    let changed = CircMgrConfig::builder()
        .circuit_timing(
            CircuitTiming::builder()
                .max_dirtiness(Duration::from_secs(1))
                .build()
                .expect("build"),
        )
        .build()
        .expect("build");
    assert_ne!(changed, CircMgrConfig::default());
}

#[test]
fn circmgr_cfg__path_config_defaults_are_sixteen_and_thirtytwo() {
    let documented = PathConfig::builder(&PathConfig::default())
        .ipv4_subnet_family_prefix(16)
        .ipv6_subnet_family_prefix(32)
        .build()
        .expect("build");
    assert_eq!(PathConfig::default(), documented);

    let v4 = PathConfig::builder(&PathConfig::default())
        .ipv4_subnet_family_prefix(17)
        .build()
        .expect("build");
    let v6 = PathConfig::builder(&PathConfig::default())
        .ipv6_subnet_family_prefix(33)
        .build()
        .expect("build");
    assert_ne!(v4, PathConfig::default());
    assert_ne!(v6, PathConfig::default());
    assert_ne!(v4, v6);

    let parsed: PathConfig =
        toml::from_str("ipv4_subnet_family_prefix = 17").expect("partial path rules");
    assert_eq!(parsed, v4);
}

#[test]
fn circmgr_cfg__circuit_timing_defaults_are_unchanged_from_step_three() {
    let documented = CircuitTiming::builder()
        .max_dirtiness(Duration::from_secs(600))
        .request_timeout(Duration::from_secs(60))
        .request_max_retries(32)
        .request_loyalty(Duration::from_millis(50))
        .build()
        .expect("build");
    assert_eq!(CircuitTiming::default(), documented);

    let parsed: CircuitTiming = toml::from_str(
        r#"
max_dirtiness = "10m"
request_timeout = "1m"
request_max_retries = 32
request_loyalty = "50ms"
"#,
    )
    .expect("timing section");
    assert_eq!(parsed, documented);
}
