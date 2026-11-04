#![allow(non_snake_case)]
//! `tor_config::resolve` and friends: building several consumers' configuration
//! from one configuration tree, and reporting the keys nobody recognized.
//!
//! The types below are the documented downstream pattern: a `Built` type, a
//! deserializable builder, `Builder` to go from one to the other, and `TopLevel`
//! to mark the type as resolvable at the root of the config file.
use tor_config::config_crate as config;
use tor_config::load::{Builder, ConfigResolveError, TopLevel};
use tor_config::ConfigBuildError;
use tor_config::{resolve, resolve_ignore_unrecognized, resolve_return_unrecognized};

/// Consumer recognizing a single integer key `alpha`.
#[derive(Debug, Clone, PartialEq)]
struct AConfig {
    alpha: i32,
}
#[derive(Debug, Default, Clone, serde::Deserialize)]
struct AConfigBuilder {
    alpha: Option<i32>,
}
impl Builder for AConfigBuilder {
    type Built = AConfig;
    fn build(&self) -> Result<AConfig, ConfigBuildError> {
        Ok(AConfig {
            alpha: self.alpha.unwrap_or(0),
        })
    }
}
impl TopLevel for AConfig {
    type Builder = AConfigBuilder;
}

/// Consumer recognizing a single string key `beta`.
#[derive(Debug, Clone, PartialEq)]
struct BConfig {
    beta: String,
}
#[derive(Debug, Default, Clone, serde::Deserialize)]
struct BConfigBuilder {
    beta: Option<String>,
}
impl Builder for BConfigBuilder {
    type Built = BConfig;
    fn build(&self) -> Result<BConfig, ConfigBuildError> {
        Ok(BConfig {
            beta: self.beta.clone().unwrap_or_default(),
        })
    }
}
impl TopLevel for BConfig {
    type Builder = BConfigBuilder;
}

/// Consumer recognizing nothing at all: every key in the config is unrecognized
/// by it, and it ignores each one at the top level.
#[derive(Debug, Clone, PartialEq)]
struct NoneConfig;
#[derive(Debug, Default, Clone, serde::Deserialize)]
struct NoneConfigBuilder {}
impl Builder for NoneConfigBuilder {
    type Built = NoneConfig;
    fn build(&self) -> Result<NoneConfig, ConfigBuildError> {
        Ok(NoneConfig)
    }
}
impl TopLevel for NoneConfig {
    type Builder = NoneConfigBuilder;
}

/// Consumer that descends into `outer` and into `list`, recognizing only
/// `outer.known` and `list[_].keep`. Used to show that ignoring happens at the
/// depth the consumer actually reached.
#[derive(Debug, Clone, PartialEq)]
struct DeepConfig;
#[derive(Debug, Default, Clone, serde::Deserialize)]
struct DeepConfigBuilder {
    outer: Option<Known>,
    list: Option<Vec<Known>>,
}
#[derive(Debug, Default, Clone, PartialEq, serde::Deserialize)]
struct Known {
    known: Option<i32>,
    keep: Option<i32>,
}
impl Builder for DeepConfigBuilder {
    type Built = DeepConfig;
    fn build(&self) -> Result<DeepConfig, ConfigBuildError> {
        Ok(DeepConfig)
    }
}
impl TopLevel for DeepConfig {
    type Builder = DeepConfigBuilder;
}

/// Consumer whose builder always refuses to build.
#[derive(Debug, Clone, PartialEq)]
struct PickyConfig;
#[derive(Debug, Default, Clone, serde::Deserialize)]
struct PickyConfigBuilder {
    n: Option<i32>,
}
impl Builder for PickyConfigBuilder {
    type Built = PickyConfig;
    fn build(&self) -> Result<PickyConfig, ConfigBuildError> {
        Err(ConfigBuildError::Invalid {
            field: "n".into(),
            problem: "always bad".into(),
        })
    }
}
impl TopLevel for PickyConfig {
    type Builder = PickyConfigBuilder;
}

/// Build a `config::Config` from a list of dotted-key overrides.
fn cfg<V: Into<config::Value> + Clone>(pairs: &[(&str, V)]) -> config::Config {
    let mut b = config::Config::builder();
    for (k, v) in pairs {
        b = b.set_override(*k, v.clone().into()).unwrap();
    }
    b.build().unwrap()
}

/// Render unrecognized keys as the strings a user would be shown.
fn shown(keys: &[tor_config::load::UnrecognizedKey]) -> Vec<String> {
    keys.iter().map(|k| k.to_string()).collect()
}

#[test]
fn cfgresolve__single_consumer_from_tuple() {
    // `Resolvable` is implemented for tuples of `TopLevel` types, so even a lone
    // consumer is resolved as a one-element tuple.
    let (a,): (AConfig,) = resolve(cfg(&[("alpha", 42)])).unwrap();
    assert_eq!(a, AConfig { alpha: 42 });
}

#[test]
fn cfgresolve__two_consumers_share_one_config() {
    // Each consumer deserializes the whole tree independently and takes the keys
    // it knows; this is the alternative to `#[serde(flatten)]`.
    let cfg = config::Config::builder()
        .set_override("alpha", 1)
        .unwrap()
        .set_override("beta", "two")
        .unwrap()
        .build()
        .unwrap();
    let (a, b): (AConfig, BConfig) = resolve(cfg).unwrap();
    assert_eq!(a, AConfig { alpha: 1 });
    assert_eq!(
        b,
        BConfig {
            beta: "two".to_string()
        }
    );
}

#[test]
fn cfgresolve__deserialize_error_names_contents() {
    // A type mismatch is a deserialization failure, distinct from a build failure.
    let r: Result<(AConfig,), ConfigResolveError> = resolve(cfg(&[("alpha", "not-an-integer")]));
    match r {
        Err(e @ ConfigResolveError::Deserialize(_)) => assert!(
            e.to_string().contains("config contents not as expected"),
            "{}",
            e
        ),
        other => panic!("expected Deserialize, got {:?}", other.map(|_| ())),
    }
}

#[test]
fn cfgresolve__build_error_from_builder() {
    // A builder that refuses is a build failure, and carries the builder's own message.
    let r: Result<(PickyConfig,), ConfigResolveError> = resolve(cfg(&[("n", 1)]));
    match r {
        Err(e @ ConfigResolveError::Build(_)) => {
            assert!(
                e.to_string().contains("config semantically incorrect"),
                "{}",
                e
            );
            assert!(e.to_string().contains("always bad"), "{}", e);
        }
        other => panic!("expected Build, got {:?}", other.map(|_| ())),
    }
}

#[test]
fn cfgresolve__unrecognized_keys_are_reported_sorted() {
    // Unrecognized keys come back in sorted order, not in config order.
    let (_, keys) =
        resolve_return_unrecognized::<(NoneConfig,)>(cfg(&[("zeta", 1), ("alpha", 2)])).unwrap();
    assert_eq!(shown(&keys), vec!["alpha".to_string(), "zeta".to_string()]);
}

#[test]
fn cfgresolve__unrecognized_key_display_is_toml_path() {
    // Map entries render dotted; array elements render with a bracketed index.
    let mut table = std::collections::HashMap::new();
    table.insert("known".to_string(), config::Value::from(1));
    table.insert("surprise".to_string(), config::Value::from(2));

    let mut nested = std::collections::HashMap::new();
    nested.insert("inner".to_string(), config::Value::from(1));

    let cfg = config::Config::builder()
        .set_override("outer", config::Value::from(nested))
        .unwrap()
        .set_override("list", vec![config::Value::from(table)])
        .unwrap()
        .build()
        .unwrap();

    let (_, keys) = resolve_return_unrecognized::<(DeepConfig,)>(cfg).unwrap();
    assert_eq!(
        shown(&keys),
        vec!["list[0].surprise".to_string(), "outer.inner".to_string()]
    );
}

#[test]
fn cfgresolve__unrecognized_key_resolves_to_deepest_ignore() {
    // `NoneConfig` ignores all of `outer`; `DeepConfig` descends and ignores only
    // `outer.inner`. The report names the deeper key, because that is the only
    // setting no consumer used.
    let cfg = || {
        config::Config::builder()
            .set_override("outer.inner", 1)
            .unwrap()
            .build()
            .unwrap()
    };
    let (_, shallow_only) = resolve_return_unrecognized::<(NoneConfig,)>(cfg()).unwrap();
    assert_eq!(shown(&shallow_only), vec!["outer".to_string()]);

    let (_, both) = resolve_return_unrecognized::<(NoneConfig, DeepConfig)>(cfg()).unwrap();
    assert_eq!(shown(&both), vec!["outer.inner".to_string()]);
}

#[test]
fn cfgresolve__key_recognized_by_any_consumer_is_not_unrecognized() {
    // The report is the intersection across consumers: `alpha` is unknown to B and
    // `beta` is unknown to A, but only `gamma` is unknown to both.
    let cfg = config::Config::builder()
        .set_override("alpha", 1)
        .unwrap()
        .set_override("beta", "x")
        .unwrap()
        .set_override("gamma", 3)
        .unwrap()
        .build()
        .unwrap();
    let (_, keys) = resolve_return_unrecognized::<(AConfig, BConfig)>(cfg).unwrap();
    assert_eq!(shown(&keys), vec!["gamma".to_string()]);
}

#[test]
fn cfgresolve__ignore_unrecognized_succeeds() {
    // The ignoring variant returns the value alone and never reports keys.
    let _: (NoneConfig,) = resolve_ignore_unrecognized(cfg(&[("something_unknown", 1)])).unwrap();
}
