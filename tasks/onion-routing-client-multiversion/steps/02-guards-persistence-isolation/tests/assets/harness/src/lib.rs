//! Support library for the step-02 conformance driver.
//!
//! The scored cases live in `tests/groups/`; this crate exists so the driver
//! package has a library target and so that the generated `[[test]]` targets
//! have something stable to link against. It intentionally contains no logic
//! that duplicates anything under test.
