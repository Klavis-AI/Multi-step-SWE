//! Certificate *generation* is opt-in: the `encode` feature and the optional
//! builder dependency it turns on.

#![allow(non_snake_case)]

use crate::common;

#[test]
fn certgenfeat__encode_feature_turns_on_the_builder_dependency() {
    common::assert_feature_is("tor-cert", "encode", &["derive_builder"]);
}

#[test]
fn certgenfeat__experimental_implies_encode() {
    common::assert_feature_contains("tor-cert", "experimental", "encode");
}

#[test]
fn certgenfeat__certificate_generation_is_off_by_default() {
    let default = common::feature_members("tor-cert", "default")
        .expect("tor-cert must declare a `default` feature list");
    assert!(
        default.is_empty(),
        "generating certificates must not be on by default; got {:?}",
        default
    );
}

#[test]
fn certgenfeat__the_builder_dependency_is_optional() {
    let spec = common::dependency_spec("tor-cert", "derive_builder")
        .expect("tor-cert must depend on derive_builder");
    assert!(
        spec.contains("optional = true"),
        "derive_builder must be an optional dependency; got {:?}",
        spec
    );
}
