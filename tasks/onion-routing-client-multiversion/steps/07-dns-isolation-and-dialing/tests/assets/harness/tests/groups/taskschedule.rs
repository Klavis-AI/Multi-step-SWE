//! Group `taskschedule`: the remotely-controllable recurring-task trigger that
//! the directory manager and the dormant-mode machinery drive.  A schedule is
//! a stream of "do it now" events; a handle can bring the next event forward,
//! cancel it, or -- when every handle is gone -- shut the task down.
//!
//! Graded items: `tor_rtcompat::scheduler::{TaskSchedule, TaskHandle}`.

use std::time::Duration;

use futures::{FutureExt, StreamExt};
use tor_rtcompat::scheduler::TaskSchedule;
use tor_rtcompat::tokio::TokioNativeTlsRuntime;
use tor_rtcompat::BlockOn;

/// Create a runtime for one case.
fn runtime() -> TokioNativeTlsRuntime {
    TokioNativeTlsRuntime::create().expect("a runtime can be created")
}

/// A duration long enough that it will never elapse during a test.
const FOREVER: Duration = Duration::from_secs(86_400);

#[test]
fn taskschedule__a_new_schedule_fires_once_immediately() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let (mut sch, _hdl) = TaskSchedule::new(rt);
        // Ready straight away, so a periodic task runs once at startup ...
        assert_eq!(sch.next().now_or_never(), Some(Some(())));
        // ... and then only once.
        assert!(sch.next().now_or_never().is_none());
    });
}

#[test]
fn taskschedule__a_schedule_waiting_on_a_deadline_is_not_ready() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let (mut sch, _hdl) = TaskSchedule::new(rt);
        assert_eq!(sch.next().now_or_never(), Some(Some(())));

        sch.fire_in(FOREVER);
        assert!(
            sch.next().now_or_never().is_none(),
            "a schedule waiting for a deadline must not be ready"
        );
        // Asking for an immediate firing overrides the pending deadline.
        sch.fire();
        assert_eq!(sch.next().now_or_never(), Some(Some(())));
        assert!(sch.next().now_or_never().is_none());
    });
}

#[test]
fn taskschedule__a_handle_can_bring_the_next_firing_forward() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let (mut sch, hdl) = TaskSchedule::new(rt);
        assert_eq!(sch.next().now_or_never(), Some(Some(())));
        sch.fire_in(FOREVER);
        assert!(sch.next().now_or_never().is_none());

        assert!(hdl.fire(), "the schedule is still alive");
        assert_eq!(sch.next().now_or_never(), Some(Some(())));
        // One request produces exactly one firing.
        assert!(sch.next().now_or_never().is_none());
    });
}

#[test]
fn taskschedule__a_handle_can_cancel_a_pending_firing() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let (mut sch, hdl) = TaskSchedule::new(rt);
        assert_eq!(sch.next().now_or_never(), Some(Some(())));

        assert!(hdl.fire());
        assert!(hdl.cancel());
        assert!(
            sch.next().now_or_never().is_none(),
            "a cancelled firing must not be delivered"
        );

        // Cancelling also discards a deadline set on the schedule itself.
        sch.fire_in(FOREVER);
        assert!(hdl.cancel());
        assert!(sch.next().now_or_never().is_none());
        // The schedule is still usable afterwards.
        assert!(hdl.fire());
        assert_eq!(sch.next().now_or_never(), Some(Some(())));
    });
}

#[test]
fn taskschedule__the_stream_ends_when_every_handle_is_dropped() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let (mut sch, hdl) = TaskSchedule::new(rt.clone());
        let clone = hdl.clone();
        drop(hdl);
        // One handle remains, so the schedule keeps running.
        assert_eq!(sch.next().now_or_never(), Some(Some(())));
        assert!(sch.next().now_or_never().is_none());

        drop(clone);
        // End of stream: the driven task should shut itself down.
        assert_eq!(sch.next().now_or_never(), Some(None));

        // A schedule whose handle never existed ends before its first firing.
        let (mut sch, hdl) = TaskSchedule::new(rt);
        drop(hdl);
        assert_eq!(sch.next().now_or_never(), Some(None));
    });
}

#[test]
fn taskschedule__handles_report_whether_the_schedule_still_exists() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let (sch, hdl) = TaskSchedule::new(rt);
        let clone = hdl.clone();
        assert!(hdl.fire());
        assert!(clone.fire_at(std::time::Instant::now()));
        assert!(clone.cancel());

        drop(sch);
        // Every operation now reports that the schedule is gone, instead of
        // panicking or silently succeeding.
        assert!(!hdl.fire());
        assert!(!hdl.fire_at(std::time::Instant::now()));
        assert!(!hdl.cancel());
        assert!(!clone.fire());
    });
}
