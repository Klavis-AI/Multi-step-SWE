#![allow(non_snake_case)]
use std::time::Duration;
use tor_proto::ClockSkew;

#[test]
fn skewinfer__none_display() {
    let sk = ClockSkew::None;
    let s = format!("{:?}", sk);
    assert!(s.contains("None"));
}

#[test]
fn skewinfer__fast_display() {
    let sk = ClockSkew::Fast(Duration::from_secs(10));
    let s = format!("{:?}", sk);
    assert!(s.contains("Fast"));
}

#[test]
fn skewinfer__slow_display() {
    let sk = ClockSkew::Slow(Duration::from_secs(5));
    let s = format!("{:?}", sk);
    assert!(s.contains("Slow"));
}

#[test]
fn skewinfer__magnitude() {
    let sk = ClockSkew::Fast(Duration::from_secs(42));
    assert_eq!(sk.magnitude(), Duration::from_secs(42));
}

#[test]
fn skewinfer__as_secs_f64_fast() {
    let sk = ClockSkew::Fast(Duration::from_secs(10));
    let f = sk.as_secs_f64();
    assert!(f > 9.0 && f < 11.0);
}

#[test]
fn skewinfer__as_secs_f64_slow_negative() {
    let sk = ClockSkew::Slow(Duration::from_secs(5));
    let f = sk.as_secs_f64();
    assert!(f < -4.0 && f > -6.0);
}

#[test]
fn skewinfer__ord() {
    let slow = ClockSkew::Slow(Duration::from_secs(10));
    let none = ClockSkew::None;
    let fast = ClockSkew::Fast(Duration::from_secs(10));
    assert!(slow < none);
    assert!(none < fast);
}
