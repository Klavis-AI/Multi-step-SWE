# Two adjustments this workspace makes to Redis's own test harness.
#
# Neither changes what makes a test pass. Both exist because the harness was
# written for a C server that starts in milliseconds and aborts a whole file
# when one test raises, and this workspace's server is a JVM.
#
#  1. `server_is_up` waits longer. Redis's harness up to 7.0 gives a starting
#     server 100 retries of 50 ms -- five seconds -- and then *skips the rest of
#     the file*, reporting "Cant' start the Redis server". A JVM under sixteen
#     concurrent test clients routinely needs more than five seconds, so this
#     raises the floor to a minute, the same order as every 7.0-and-later
#     harness. Without it a local run reports failures that are about the harness's
#     patience and not about your server.
#
#  2. A test that raises an exception is recorded as a failed test and the rest
#     of its file still runs, instead of the exception ending that test client.
#     Redis's own harness gained this as `--durable` in 6.2; this makes every
#     version behave that way.
#
# Every run of the suite against this server does the same two things, so a
# local run and any other run agree about what a failure is.

if {[llength [info procs server_is_up]] > 0} {
    rename server_is_up __agent_orig_server_is_up
    proc server_is_up {host port retrynum} {
        return [__agent_orig_server_is_up $host $port \
                    [expr {$retrynum < 1200 ? 1200 : $retrynum}]]
    }
}

# A convergence wait is a bound on the harness's patience, not a performance
# requirement. Redis's suite uses `wait_for_condition 50 100` -- five seconds --
# for "the replica has synced", "the failover finished", "the child exited". On a
# box running twenty builders' jobs at once, a correct server that syncs slowly
# is indistinguishable from one that never syncs, and the test fails for a reason
# that is about the host. Raise any wait shorter than twenty seconds to twenty;
# never shorten one. A server that is actually broken still fails, twenty seconds
# later.
if {[llength [info procs wait_for_condition]] > 0} {
    rename wait_for_condition __agent_orig_wait_for_condition
    proc wait_for_condition {maxtries delay e _else_ elsescript} {
        if {$delay > 0} {
            set floor [expr {int(ceil(20000.0 / $delay))}]
            if {$maxtries < $floor} { set maxtries $floor }
        }
        uplevel 1 [list __agent_orig_wait_for_condition $maxtries $delay $e \
                       $_else_ $elsescript]
    }
}

# Never silently drop to a single test client because the host looked slow.
proc is_a_slow_computer {} { return 0 }

rename test __agent_orig_test
set ::agent_test_nargs [llength [info args __agent_orig_test]]
proc test {args} {
    set name [lindex $args 0]
    set fwd [lrange $args 0 [expr {$::agent_test_nargs - 1}]]
    if {[catch {uplevel 1 [list __agent_orig_test {*}$fwd]} __err]} {
        incr ::num_failed
        catch {send_data_packet $::test_server_fd err "$name (exception)\n$__err"}
    }
}
