/// These tests are intended to provide coverage and
/// check panic-safety of relatively unused values.
use super::codec::Codec;
use super::enums::*;

fn get8<T: Codec>(enum_value: &T) -> u8 {
    unimplemented!()
}

fn get16<T: Codec>(enum_value: &T) -> u16 {
    unimplemented!()
}

fn test_enum16<T: Codec>(first: T, last: T) {
    unimplemented!()
}

fn test_enum8<T: Codec>(first: T, last: T) {
    unimplemented!()
}

#[test]
fn test_enums() {
    unimplemented!()
}
