use std::fmt;

/// Alternative implementation of `fmt::Debug` for byte slice.
///
/// Standard `Debug` implementation for `[u8]` is comma separated
/// list of numbers. Since large amount of byte strings are in fact
/// ASCII strings or contain a lot of ASCII strings (e. g. HTTP),
/// it is convenient to print strings as ASCII when possible.
///
/// This struct wraps `&[u8]` just to override `fmt::Debug`.
///
/// `BsDebug` is not a part of public API of bytes crate.
pub struct BsDebug<'a>(pub &'a [u8]);

impl<'a> fmt::Debug for BsDebug<'a> {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        unimplemented!()
    }
}

#[cfg(test)]
mod test {
    use super::BsDebug;

    #[test]
    fn debug() {
        unimplemented!()
    }
}
