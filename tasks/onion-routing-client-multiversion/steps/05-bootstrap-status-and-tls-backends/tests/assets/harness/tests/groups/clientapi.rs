//! Group `clientapi` — the status and cloning surface added to `TorClient`.
//!
//! `TorClient` cannot be constructed offline (it needs a state directory, a
//! directory cache and a network), so these accessors are graded by pinning
//! their exact public signatures and the types they hand back. That is the
//! whole of their contract: `TEST_INTERFACE.md` lists each one.

use arti_client::status::{BootstrapEvents, BootstrapStatus};
use arti_client::{StreamPrefs, TorClient};
use tor_rtcompat::tokio::TokioNativeTlsRuntime;

type Rt = TokioNativeTlsRuntime;

fn assert_stream_of<S, I>()
where
    S: futures::Stream<Item = I>,
{
}

#[test]
fn clientapi__clone_with_prefs_takes_owned_preferences() {
    // Owned, not borrowed: the returned client keeps the preferences.
    let f: fn(&TorClient<Rt>, StreamPrefs) -> TorClient<Rt> = TorClient::clone_with_prefs;
    let _ = f;
}

#[test]
fn clientapi__bootstrap_status_returns_an_owned_status() {
    let f: fn(&TorClient<Rt>) -> BootstrapStatus = TorClient::bootstrap_status;
    let _ = f;
    // A snapshot, so it must be independent of the client that produced it.
    crate::common::assert_clone::<BootstrapStatus>();
    crate::common::assert_send_sync::<BootstrapStatus>();
}

#[test]
fn clientapi__bootstrap_events_returns_a_status_stream() {
    let f: fn(&TorClient<Rt>) -> BootstrapEvents = TorClient::bootstrap_events;
    let _ = f;
    assert_stream_of::<BootstrapEvents, BootstrapStatus>();
    crate::common::assert_clone::<BootstrapEvents>();
}

#[test]
fn clientapi__isolated_client_returns_a_new_client() {
    let f: fn(&TorClient<Rt>) -> TorClient<Rt> = TorClient::isolated_client;
    let _ = f;
}

#[test]
fn clientapi__client_is_cloneable() {
    crate::common::assert_clone::<TorClient<Rt>>();
    crate::common::assert_send_sync::<TorClient<Rt>>();
}

#[test]
fn clientapi__bootstrap_events_item_type_is_bootstrap_status() {
    assert_eq!(
        std::any::type_name::<<BootstrapEvents as futures::Stream>::Item>(),
        std::any::type_name::<BootstrapStatus>()
    );
    crate::common::assert_debug::<BootstrapEvents>();
    // And the item type is the same one the query returns, so a caller can
    // use one interchangeably with the other.
    let item: <BootstrapEvents as futures::Stream>::Item = BootstrapStatus::default();
    assert_eq!(
        item.to_string(),
        "0%: connecting to the internet; directory is fetching a consensus"
    );
}
