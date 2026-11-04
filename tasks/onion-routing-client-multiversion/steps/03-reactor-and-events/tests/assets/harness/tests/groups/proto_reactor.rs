//! Group `proto_reactor` - the `tor-proto` reactor refactor.
//!
//! instruction.md section 1: "a `tor-proto` reactor refactor that removes locks
//! from the data path".
//!
//! Graded items: `tor_proto::channel::CHANNEL_BUFFER_SIZE`,
//! `tor_proto::circuit::CIRCUIT_BUFFER_SIZE`, the shapes of
//! `tor_proto::channel::Channel::{poll_ready, close_circuit, terminate,
//! is_closing}` and `tor_proto::circuit::ClientCirc::{terminate, is_closing,
//! unique_id}`, `Channel`'s `futures::Sink<ChanCell>` implementation,
//! and `tor_proto::circuit::CircParameters`. Reaching a live channel also
//! grades `UnverifiedChannel::check` and `VerifiedChannel::finish`.
//!
//! Removing the mutex from the data path is visible as a shape change: handles
//! became cheap clonable senders into a reactor task, the shutdown entry points
//! stopped being `async` (they now just post a control message), and a bounded
//! queue length had to be named. The shapes are checked by coercing each
//! method to a plain `fn` pointer, which is what proves nothing is `async`.
//!
//! The `Channel` behaviour underneath those shapes is checked on a real
//! channel. `common::live_channel` runs the whole link handshake against a
//! scripted relay whose certificate chain genuinely verifies (TEST_INTERFACE
//! section 6.2), so `unique_id`, `is_closing`, `terminate`, `close_circuit`
//! and `poll_ready` are exercised on a channel that has a reactor attached,
//! not merely named. `ClientCirc` stays at the type level: building one needs
//! a relay that can answer a CREATE cell, which grading does not have.

// `live_channel` lives in `current`, not `common`: it is built on the v0.0.2
// shape of `VerifiedChannel::finish`, which no gate group may be made to
// compile. current.rs re-exports common, so this one path reaches both.
use crate::current as common;
use futures::sink::Sink;
use std::pin::Pin;
use std::task::Context;
use tor_cell::chancell::{msg, ChanCell, CircId};
use tor_proto::channel::{Channel, CHANNEL_BUFFER_SIZE};
use tor_proto::circuit::{CircParameters, ClientCirc};

/// Compile-time assertion that `T` is a cheap, shareable handle.
fn assert_handle<T: Clone + Send + Sync + std::fmt::Debug + 'static>() {}

#[test]
fn proto_reactor__channel_queue_length_is_declared() {
    assert_eq!(tor_proto::channel::CHANNEL_BUFFER_SIZE, 128);
}

#[test]
fn proto_reactor__circuit_queue_length_is_declared() {
    assert_eq!(tor_proto::circuit::CIRCUIT_BUFFER_SIZE, 128);
}

#[test]
fn proto_reactor__handles_are_clonable_and_shareable() {
    // Both handles talk to a reactor task instead of guarding shared state, so
    // both are `Clone` and may cross threads.
    assert_handle::<Channel>();
    assert_handle::<ClientCirc>();

    // The channel is itself a sink of cells; the reactor drains it.
    fn assert_cell_sink<T: Sink<ChanCell, Error = tor_proto::Error>>() {}
    assert_cell_sink::<Channel>();

    // On a real channel: cloning the handle is not cloning the channel, so
    // every clone reports the same identity...
    let (chan, _reactor) = common::live_channel();
    let clone = chan.clone();
    assert_eq!(
        chan.unique_id(),
        clone.unique_id(),
        "a cloned handle must report the same channel identity"
    );
    // ... and it is stable across reads, not minted on demand.
    assert_eq!(chan.unique_id(), chan.unique_id());

    // ... while a second, independently created channel is a different
    // channel and must say so.
    let (other, _other_reactor) = common::live_channel();
    assert_ne!(
        chan.unique_id(),
        other.unique_id(),
        "two channels must not share one identity"
    );
}

#[test]
fn proto_reactor__channel_exposes_reactor_flow_control() {
    // Readiness is polled against the reactor's queue rather than taken from a
    // lock, and needs `&mut` access to the handle.
    let _poll_ready: fn(&mut Channel, &mut Context<'_>) -> tor_proto::Result<bool> =
        Channel::poll_ready;
    // Closing one circuit is a control message, so it is not `async`.
    let _close_circuit: fn(&Channel, CircId) -> tor_proto::Result<()> = Channel::close_circuit;
    let _is_closing: fn(&Channel) -> bool = Channel::is_closing;

    let (chan, _reactor) = common::live_channel();
    assert!(!chan.is_closing(), "a freshly built channel is not closing");

    // Closing a circuit the channel never had is the desired end state, not
    // an error, and it does not close the channel.
    assert!(
        chan.close_circuit(CircId::from(9999)).is_ok(),
        "closing an unknown circuit must succeed"
    );
    assert!(
        !chan.is_closing(),
        "closing one circuit must not close the channel"
    );

    // Readiness is the state of a bounded queue that the reactor drains. No
    // reactor is running here, so nothing is drained: the queue fills, and
    // `poll_ready` has to start saying no. A no-op waker is enough because
    // nothing is ever awaited.
    let waker = futures::task::noop_waker();
    let mut cx = Context::from_waker(&waker);
    let mut sender = chan.clone();
    assert!(
        sender.poll_ready(&mut cx).expect("poll_ready on a live channel"),
        "an empty queue must accept a cell"
    );

    let limit = CHANNEL_BUFFER_SIZE * 2;
    let mut accepted = 0usize;
    while accepted < limit
        && sender.poll_ready(&mut cx).expect("poll_ready on a live channel")
    {
        let cell = ChanCell::new(
            CircId::from(1),
            msg::Create2::new(2, &b"handshake"[..]).into(),
        );
        Pin::new(&mut sender)
            .start_send(cell)
            .expect("a ready channel accepts a cell");
        accepted += 1;
    }
    assert!(
        accepted >= CHANNEL_BUFFER_SIZE,
        "the queue must hold at least CHANNEL_BUFFER_SIZE cells, held {}",
        accepted
    );
    assert!(
        !sender.poll_ready(&mut cx).expect("poll_ready on a live channel"),
        "the queue is bounded: it must stop reporting ready, and did not \
         after {} cells",
        accepted
    );
}

#[test]
fn proto_reactor__shutdown_is_synchronous() {
    // Shutting down posts a message to the reactor; it no longer has to take a
    // lock, so neither entry point is `async` any more.
    let _chan_terminate: fn(&Channel) = Channel::terminate;
    let _circ_terminate: fn(&ClientCirc) = ClientCirc::terminate;
    let _circ_is_closing: fn(&ClientCirc) -> bool = ClientCirc::is_closing;
    // The circuit's identity is readable without a lock and without `async`.
    let _circ_id: fn(&ClientCirc) -> tor_proto::circuit::UniqId = ClientCirc::unique_id;
    let _chan_id: fn(&Channel) -> tor_proto::channel::UniqId = Channel::unique_id;

    // On a real channel: `terminate` takes a shared handle, returns nothing,
    // and cannot be awaited, so the shutdown it asks for has to be carried
    // out by the reactor. Run the reactor to completion and the channel -
    // through any clone of the handle - reports itself closing.
    let (chan, reactor) = common::live_channel();
    let clone = chan.clone();
    assert!(!chan.is_closing(), "a freshly built channel is not closing");
    chan.terminate();
    reactor
        .run_to_completion()
        .expect("a terminated reactor stops without error");
    assert!(
        chan.is_closing(),
        "after terminate() and the reactor stopping, the channel is closing"
    );
    assert!(
        clone.is_closing(),
        "every clone of the handle sees the same shutdown"
    );

    // The parameters the reactor is handed when a circuit is created.
    let mut p = CircParameters::default();
    assert_eq!(p.initial_send_window(), 1000);
    assert_eq!(p.extend_by_ed25519_id(), true);
    p.set_initial_send_window(500).expect("500 is a legal window");
    assert_eq!(p.initial_send_window(), 500);
    assert!(
        p.set_initial_send_window(1001).is_err(),
        "a send window above 1000 must be refused"
    );
    assert_eq!(p.initial_send_window(), 500);
    p.set_extend_by_ed25519_id(false);
    assert_eq!(p.extend_by_ed25519_id(), false);
}
