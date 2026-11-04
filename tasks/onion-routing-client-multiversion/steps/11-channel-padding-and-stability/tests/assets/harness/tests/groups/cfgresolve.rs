//! Group `cfgresolve`: resolving a parsed configuration into typed settings.
//!
//! This milestone introduces the two-stage configuration model that the new
//! configuration directory feeds: a deserializable *builder* per consumer,
//! resolved into the built settings with `tor_config::load::resolve`. Because
//! several consumers share one configuration tree, a key that one consumer
//! does not recognize may still be another's, so "unrecognized" is decided by
//! intersection across all consumers and reported rather than being fatal.
//!
//! The types below stand in for a consumer's configuration: the trait
//! implementations, not the fields, are what is being graded.
#![allow(non_snake_case)]

use serde::Deserialize;

use tor_config::config_crate as config;
use tor_config::load::{
    resolve, resolve_ignore_warnings, resolve_return_results, Builder, ConfigResolveError, TopLevel,
};
use tor_config::ConfigBuildError;

/// One consumer's settings, as they appear in a configuration file.
#[derive(Debug, Clone, Default, Deserialize)]
struct PaddingSettingsBuilder {
    /// How much padding: "none", "reduced" or "normal".
    padding: Option<String>,
    /// How many times to retry; must be under 100.
    retries: Option<u32>,
    /// A nested table, so that nested key paths are exercised too.
    sub: Option<SubSettingsBuilder>,
}

/// The nested part of one consumer's settings.
#[derive(Debug, Clone, Default, Deserialize)]
struct SubSettingsBuilder {
    /// A recognized key inside the nested table.
    known: Option<u32>,
}

/// One consumer's settings, built and validated.
#[derive(Debug, Clone, Eq, PartialEq)]
struct PaddingSettings {
    /// How much padding.
    padding: String,
    /// How many times to retry.
    retries: u32,
    /// The nested key's value.
    sub_known: u32,
}

impl Builder for PaddingSettingsBuilder {
    type Built = PaddingSettings;
    fn build(&self) -> Result<PaddingSettings, ConfigBuildError> {
        let retries = self.retries.unwrap_or(3);
        if retries >= 100 {
            return Err(ConfigBuildError::Invalid {
                field: "retries".into(),
                problem: "must be less than 100".into(),
            });
        }
        Ok(PaddingSettings {
            padding: self.padding.clone().unwrap_or_else(|| "normal".into()),
            retries,
            sub_known: self.sub.as_ref().and_then(|s| s.known).unwrap_or(0),
        })
    }
}

impl TopLevel for PaddingSettings {
    type Builder = PaddingSettingsBuilder;
    const DEPRECATED_KEYS: &'static [&'static str] = &["padding_enabled"];
}

/// A second, unrelated consumer sharing the same configuration tree.
#[derive(Debug, Clone, Default, Deserialize)]
struct LogSettingsBuilder {
    /// Log verbosity.
    verbosity: Option<String>,
}

/// The second consumer's built settings.
#[derive(Debug, Clone, Eq, PartialEq)]
struct LogSettings {
    /// Log verbosity.
    verbosity: String,
}

impl Builder for LogSettingsBuilder {
    type Built = LogSettings;
    fn build(&self) -> Result<LogSettings, ConfigBuildError> {
        Ok(LogSettings {
            verbosity: self.verbosity.clone().unwrap_or_else(|| "warn".into()),
        })
    }
}

impl TopLevel for LogSettings {
    type Builder = LogSettingsBuilder;
}

/// Parse TOML into the untyped configuration tree that `resolve` consumes.
fn parse(toml: &str) -> config::Config {
    config::Config::builder()
        .add_source(config::File::from_str(toml, config::FileFormat::Toml))
        .build()
        .expect("the fixture TOML in this file is well-formed")
}

/// Resolution deserializes into the builder and then builds, so defaults
/// declared in `build` apply to keys the file left out.
#[test]
fn cfgresolve__builds_settings_and_applies_defaults() {
    let got: PaddingSettings = resolve(parse(r#"padding = "reduced""#)).expect("a valid config");
    assert_eq!(
        got,
        PaddingSettings {
            padding: "reduced".into(),
            retries: 3,
            sub_known: 0,
        }
    );

    let got: PaddingSettings = resolve(parse("")).expect("an empty config is valid");
    assert_eq!(got.padding, "normal", "the builder's own default");
    assert_eq!(got.retries, 3);
}

/// An unrecognized key is reported by path, not treated as an error.
#[test]
fn cfgresolve__unrecognized_keys_are_reported_not_fatal() {
    let results = resolve_return_results::<PaddingSettings>(parse(
        r#"
            padding = "none"
            wombat = 3
            [sub]
            known = 7
            other = 1
        "#,
    ))
    .expect("unrecognized keys are not a failure");

    assert_eq!(results.value.padding, "none");
    assert_eq!(results.value.sub_known, 7, "the recognized nested key was read");
    let keys: Vec<String> = results.unrecognized.iter().map(|k| k.to_string()).collect();
    assert_eq!(
        keys,
        vec!["sub.other".to_string(), "wombat".to_string()],
        "reported by dotted path, and only the keys no consumer claimed"
    );

    // The same input, resolved without reporting, still succeeds.
    let quiet: PaddingSettings = resolve_ignore_warnings(parse(r#"padding = "none""#))
        .expect("a valid config");
    assert_eq!(quiet.padding, "none");
}

/// A key the consumer has declared deprecated is reported separately, and only
/// when it actually appears.
#[test]
fn cfgresolve__deprecated_keys_are_reported_when_present() {
    let results = resolve_return_results::<PaddingSettings>(parse("padding_enabled = true"))
        .expect("a deprecated key is not a failure");
    let deprecated: Vec<String> = results.deprecated.iter().map(|k| k.to_string()).collect();
    assert_eq!(deprecated, vec!["padding_enabled".to_string()]);

    let results = resolve_return_results::<PaddingSettings>(parse(r#"padding = "none""#))
        .expect("a valid config");
    assert!(
        results.deprecated.is_empty(),
        "nothing deprecated was used: {:?}",
        results.deprecated
    );
}

/// The two failure modes are distinguished: bad syntax for the *type* fails in
/// deserialization, and a value the consumer rejects fails in `build`.
#[test]
fn cfgresolve__build_and_deserialize_failures_are_distinguished() {
    let err = resolve::<PaddingSettings>(parse("retries = 1000"))
        .expect_err("1000 retries is rejected by build()");
    assert!(
        matches!(&err, ConfigResolveError::Build(ConfigBuildError::Invalid { field, .. }) if field == "retries"),
        "expected a build error naming the field, got {err:?}"
    );
    assert_eq!(err.to_string(), "Config semantically incorrect");

    let err = resolve::<PaddingSettings>(parse(r#"retries = "lots""#))
        .expect_err("a string is not a u32");
    assert!(
        matches!(&err, ConfigResolveError::Deserialize(_)),
        "expected a deserialize error, got {err:?}"
    );
    assert_eq!(err.to_string(), "Config contents not as expected");
}

/// Several consumers resolve from one tree at once, and a key is unrecognized
/// only if no consumer claims it.
#[test]
fn cfgresolve__multiple_consumers_share_one_tree() {
    let results = resolve_return_results::<(PaddingSettings, LogSettings)>(parse(
        r#"
            padding = "reduced"
            verbosity = "debug"
            wombat = 3
        "#,
    ))
    .expect("both consumers resolve");

    let (padding, logs) = &results.value;
    assert_eq!(padding.padding, "reduced");
    assert_eq!(logs.verbosity, "debug");

    let keys: Vec<String> = results.unrecognized.iter().map(|k| k.to_string()).collect();
    assert_eq!(
        keys,
        vec!["wombat".to_string()],
        "each consumer ignores the other's keys, so only the intersection is reported"
    );
}
