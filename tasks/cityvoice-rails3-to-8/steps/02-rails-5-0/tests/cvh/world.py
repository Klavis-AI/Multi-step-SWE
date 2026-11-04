"""The application's outside world, as a process instead of as a library.

`cvh.runner` starts `Sink(587)` and `Stub(80)` itself, because it drives the
application and also has to switch the stub's mode mid-scenario.  The gates do
not: they only need the world to be *there*, because two of the application's
own surfaces reach out of the container and both of them raise without it --

    StatusController#twilio_response_okay?  POST http://www.southbendvoices.com
    SubscriptionConfirmer#confirm           SMTP  smtp.sendgrid.net:587

-- and a surface that raises is a surface whose deprecations go uncounted. The
deprecation gate sees the application's `#deliver` call only while this is
running, so this is not a tidiness measure.

    LD_PRELOAD= python3 -m cvh.world &

Blocks until killed.  Binds the same ports as the runner, so the two never run
at the same time; the verifier runs the recorded suite first and the gates
after.

`LD_PRELOAD=` because the frozen clock belongs to the application under test
and not to its scenery -- see the sleep below for what happens when libfaketime
gets hold of this process.
"""

from __future__ import annotations

import signal
import sys

from .sink import Sink
from .stub import Stub


def main(argv=None):
    sink = Sink(port=587).start()
    stub = Stub(port=80).start()
    print("world up: smtp 587, http 80", flush=True)

    def bye(_signum, _frame):
        stub.stop()
        sink.stop()
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, bye)
    signal.signal(signal.SIGINT, bye)
    # `signal.pause()` and not `time.sleep()`.  Both servers run on daemon
    # threads, so this call is the only thing keeping the process alive, and
    # under libfaketime `time.sleep` returns EINVAL immediately -- at 100000
    # seconds and at 60 alike.  The process would then exit, the threads would
    # go with it, and the only symptom would be ECONNREFUSED in the
    # application's log two minutes later.  `signal.pause` is `sigsuspend`, which libfaketime does
    # not wrap, and it is what the SIGTERM handler above is for.
    while True:
        signal.pause()


if __name__ == "__main__":
    sys.exit(main())
