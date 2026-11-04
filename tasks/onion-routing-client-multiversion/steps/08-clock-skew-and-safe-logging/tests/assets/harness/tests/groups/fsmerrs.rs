//! Group `fsmerrs`: the shape of the errors a filesystem-permission check
//! produces -- the message a user sees, the path it blames, and how several
//! problems are aggregated into one value.
//!
//! Traces to instruction.md section 1: "filesystem-permission checking".
//! Interface: `fs_mistrust::Error` and its `path`, `errors` and
//! `FromIterator` implementations.

use fs_mistrust::Error;
use std::error::Error as _;
use std::path::PathBuf;

/// A convenient fixed path; nothing is ever read from it.
fn p(s: &str) -> PathBuf {
    PathBuf::from(s)
}

#[test]
fn fsmerrs__a_permission_error_names_the_path_and_spells_out_the_bits() {
    assert_eq!(
        Error::BadPermission(p("/var/lib/arti"), 0o022).to_string(),
        "Incorrect permissions on file or directory /var/lib/arti:  g=w o=w"
    );
    assert_eq!(
        Error::BadPermission(p("/x"), 0o066).to_string(),
        "Incorrect permissions on file or directory /x:  g=rw o=rw"
    );
}

#[test]
fn fsmerrs__each_access_class_is_rendered_separately() {
    let render = |bits: u32| {
        let s = Error::BadPermission(p("/x"), bits).to_string();
        s.strip_prefix("Incorrect permissions on file or directory /x:  ")
            .expect("unexpected message prefix")
            .to_owned()
    };
    assert_eq!(render(0o777), "u=rwx g=rwx o=rwx");
    assert_eq!(render(0o020), "g=w");
    assert_eq!(render(0o007), "o=rwx");
    assert_eq!(render(0o400), "u=r");
    assert_eq!(render(0o055), "g=rx o=rx");
}

#[test]
fn fsmerrs__no_forbidden_bits_renders_as_nothing() {
    assert_eq!(
        Error::BadPermission(p("/x"), 0).to_string(),
        "Incorrect permissions on file or directory /x:  "
    );
}

#[test]
fn fsmerrs__only_some_errors_are_about_a_particular_path() {
    assert_eq!(Error::NotFound(p("/a")).path(), Some(p("/a").as_path()));
    assert_eq!(
        Error::BadPermission(p("/b"), 0o022).path(),
        Some(p("/b").as_path())
    );
    assert_eq!(Error::BadOwner(p("/c"), 17).path(), Some(p("/c").as_path()));
    assert_eq!(Error::BadType(p("/d")).path(), Some(p("/d").as_path()));

    assert_eq!(Error::StepsExceeded.path(), None);
    assert_eq!(Error::InvalidSubdirectory.path(), None);
    assert_eq!(
        Error::Multiple(vec![Box::new(Error::BadType(p("/e")))]).path(),
        None
    );
}

#[test]
fn fsmerrs__a_single_error_iterates_over_itself() {
    let e = Error::BadType(p("/a"));
    let items: Vec<&Error> = e.errors().collect();
    assert_eq!(items.len(), 1);
    assert_eq!(items[0].path(), Some(p("/a").as_path()));

    let e = Error::Multiple(vec![
        Box::new(Error::BadType(p("/a"))),
        Box::new(Error::NotFound(p("/b"))),
        Box::new(Error::BadPermission(p("/c"), 0o022)),
    ]);
    let paths: Vec<Option<&std::path::Path>> = e.errors().map(|e| e.path()).collect();
    assert_eq!(
        paths,
        vec![
            Some(p("/a").as_path()),
            Some(p("/b").as_path()),
            Some(p("/c").as_path())
        ]
    );
}

#[test]
fn fsmerrs__collecting_errors_folds_zero_one_and_many() {
    let none: Option<Error> = Vec::new().into_iter().collect();
    assert!(none.is_none(), "an empty run of checks has no error");

    let one: Option<Error> = vec![Error::BadType(p("/a"))].into_iter().collect();
    let one = one.expect("one error should be preserved");
    assert!(matches!(one, Error::BadType(_)), "got {:?}", one);
    assert_eq!(one.path(), Some(p("/a").as_path()));

    let many: Option<Error> = vec![
        Error::BadType(p("/a")),
        Error::NotFound(p("/b")),
        Error::StepsExceeded,
    ]
    .into_iter()
    .collect();
    let many = many.expect("three errors should be preserved");
    assert!(matches!(many, Error::Multiple(_)), "got {:?}", many);
    assert_eq!(many.errors().count(), 3);
    assert_eq!(many.to_string(), "Multiple errors found");
}

#[test]
fn fsmerrs__a_content_error_defers_to_the_error_it_wraps() {
    let inner = Error::BadPermission(p("/dir/file"), 0o066);
    let outer = Error::Content(Box::new(inner.clone()));

    assert_eq!(outer.to_string(), "Invalid directory content");
    assert_eq!(outer.path(), Some(p("/dir/file").as_path()));
    assert_eq!(outer.errors().count(), 1);
    assert_eq!(
        outer
            .source()
            .expect("a content error has a source")
            .to_string(),
        inner.to_string()
    );
}
