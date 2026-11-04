//! Group `futureproof` - forward compatibility of persisted documents.
//!
//! Persistent state is shared with future versions of the client, so
//! `tor_persist::Futureproof<T>` deserializes an unrecognised document into a
//! generic JSON value instead of failing. Graded on both arms of the untagged
//! representation and on the fact that the wrapper is transparent on the wire.

use serde::{Deserialize, Serialize};
use tor_persist::{Futureproof, JsonValue};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
struct Entry {
    id: String,
    confirmed: u32,
}

#[test]
fn futureproof__a_recognised_document_becomes_understandable() {
    let text = r#"{"id":"$AAAA","confirmed":7}"#;
    let got: Futureproof<Entry> = serde_json::from_str(text).expect("deserialize failed");
    assert_eq!(
        got,
        Futureproof::Understandable(Entry {
            id: "$AAAA".to_string(),
            confirmed: 7,
        })
    );
    assert_eq!(
        got.into_option(),
        Some(Entry {
            id: "$AAAA".to_string(),
            confirmed: 7,
        })
    );
}

#[test]
fn futureproof__an_unrecognised_document_is_kept_as_json_not_rejected() {
    // A document written by a newer version: the field `id` is gone.
    let text = r#"{"identity":"$AAAA","confirmed":7,"bandwidth":900}"#;
    let got: Futureproof<Entry> = serde_json::from_str(text).expect("deserialize must not fail");
    match &got {
        Futureproof::Unknown(v) => {
            let expected: JsonValue = serde_json::from_str(text).expect("bad fixture");
            assert_eq!(v, &expected, "the unknown document was not preserved");
        }
        other => panic!("expected Futureproof::Unknown, got {:?}", other),
    }
    assert_eq!(got.into_option(), None);
}

#[test]
fn futureproof__the_wrapper_is_transparent_when_serialized() {
    let inner = Entry {
        id: "$BBBB".to_string(),
        confirmed: 1,
    };
    let wrapped = Futureproof::Understandable(inner.clone());
    assert_eq!(
        serde_json::to_string(&wrapped).expect("serialize failed"),
        serde_json::to_string(&inner).expect("serialize failed"),
        "Futureproof added a tag to the serialized form"
    );

    let unknown: Futureproof<Entry> =
        Futureproof::Unknown(serde_json::json!({"whatever": [1, 2, 3]}));
    assert_eq!(
        serde_json::to_string(&unknown).expect("serialize failed"),
        r#"{"whatever":[1,2,3]}"#
    );
}

#[test]
fn futureproof__from_wraps_a_value_as_understandable() {
    let inner = Entry {
        id: "$CCCC".to_string(),
        confirmed: 0,
    };
    let wrapped: Futureproof<Entry> = inner.clone().into();
    assert_eq!(wrapped, Futureproof::Understandable(inner.clone()));
    assert_eq!(Futureproof::from(inner.clone()).into_option(), Some(inner));
}
