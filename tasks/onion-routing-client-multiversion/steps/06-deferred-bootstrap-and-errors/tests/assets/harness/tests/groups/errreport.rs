//! Group `errreport`: `tor_error::Report`, the wrapper that renders a whole
//! error chain in one line.
//!
//! instruction.md: "a structured error taxonomy". `Report` is the taxonomy's
//! presentation half: it walks `std::error::Error::source()` and joins the
//! chain, suppressing a link whose text the previous link already contained.

use std::error::Error as StdError;

use tor_error::Report;

/// A leaf error carrying a fixed message.
#[derive(Debug)]
struct Leaf(&'static str);

impl std::fmt::Display for Leaf {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.0)
    }
}
impl StdError for Leaf {}

/// An error with a message of its own and an underlying cause.
#[derive(Debug)]
struct Wrap {
    /// This link's own text.
    text: String,
    /// The link underneath.
    source: Box<dyn StdError + 'static>,
}

impl std::fmt::Display for Wrap {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.text)
    }
}
impl StdError for Wrap {
    fn source(&self) -> Option<&(dyn StdError + 'static)> {
        Some(self.source.as_ref())
    }
}

/// Wrap `text` around `source`.
fn wrap(text: &str, source: Box<dyn StdError + 'static>) -> Box<dyn StdError + 'static> {
    Box::new(Wrap {
        text: text.to_string(),
        source,
    })
}

/// Render `e` through `Report`.
fn report(e: Box<dyn StdError + 'static>) -> String {
    Report(e).to_string()
}

#[test]
fn errreport__prefixes_a_single_error_with_error() {
    assert_eq!(report(Box::new(Leaf("shallow"))), "error: shallow");
}

#[test]
fn errreport__joins_the_source_chain_with_colons() {
    assert_eq!(
        report(wrap("terse", Box::new(Leaf("shallow")))),
        "error: terse: shallow"
    );
}

#[test]
fn errreport__skips_a_source_already_named_by_its_parent() {
    // "verbose - shallow" already contains "shallow", so the source link is
    // not repeated.
    assert_eq!(
        report(wrap("verbose - shallow", Box::new(Leaf("shallow")))),
        "error: verbose - shallow"
    );
}

#[test]
fn errreport__walks_a_three_level_chain() {
    let chain = wrap("outer", wrap("middle", Box::new(Leaf("inner"))));
    assert_eq!(report(chain), "error: outer: middle: inner");
}

#[test]
fn errreport__unwraps_an_io_error_without_repeating_it() {
    // io::Error::new renders as its inner error, so the chain collapses.
    let io = std::io::Error::new(std::io::ErrorKind::Other, Leaf("shallow"));
    assert_eq!(report(Box::new(io)), "error: shallow");
}

#[test]
fn errreport__is_copy_and_debug() {
    let boxed: Box<dyn StdError + 'static> = Box::new(Leaf("shallow"));
    let r = Report(&boxed);
    let copy = r; // Report<&T> is Copy: this must not move `r`.
    assert_eq!(r.to_string(), "error: shallow");
    assert_eq!(copy.to_string(), "error: shallow");
    assert!(format!("{:?}", r).contains("Report"));
}
