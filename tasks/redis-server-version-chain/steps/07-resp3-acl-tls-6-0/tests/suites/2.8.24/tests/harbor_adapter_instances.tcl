# Harbor scoring adapter for redis's multi-instance harness (tests/instances.tcl),
# which drives the Sentinel and Cluster suites. Sourced at the end of
# instances.tcl, after its own `proc test` and `proc run_tests` are defined.
#
# Adds exact-name selection, a machine-readable result log, and per-unit
# durability. It never changes what makes a test pass.

set ::harbor_only [dict create]
if {[info exists ::env(HARBOR_ONLY_FILE)] && $::env(HARBOR_ONLY_FILE) ne ""} {
    set __hf [open $::env(HARBOR_ONLY_FILE) r]
    foreach __line [split [read $__hf] "\n"] {
        set __line [string trim $__line]
        if {$__line ne "" && [string index $__line 0] ne "#"} { dict set ::harbor_only $__line 1 }
    }
    close $__hf
}
set ::harbor_log ""
if {[info exists ::env(HARBOR_RESULT_LOG)]} { set ::harbor_log "$::env(HARBOR_RESULT_LOG).[pid]" }
set ::harbor_unit ""

proc harbor_record {status name} {
    if {$::harbor_log eq ""} return
    catch {
        set fh [open $::harbor_log a]
        fconfigure $fh -translation binary
        puts $fh "$status\t$name\t$::harbor_unit"
        close $fh
    }
}

# Same patience fix as the single-server harness: spawn_instance gives
# server_is_up 100 x 50ms = 5s and aborts the whole run on timeout. A JVM on a
# loaded box needs longer; nothing about pass/fail changes.
if {[llength [info procs server_is_up]] > 0} {
    rename server_is_up __harbor_orig_server_is_up
    proc server_is_up {host port retrynum} {
        return [__harbor_orig_server_is_up $host $port [expr {$retrynum < 1200 ? 1200 : $retrynum}]]
    }
}

# And the same patience fix for the condition loops, which the single-server
# adapter already has and this one did not.
#
# The cluster units that failed the determinism pass are `03-failover-loop` and
# `04-resharding` -- killing a node, waiting for a replica to be promoted with a
# new epoch, resharding 50000 keys. Those waits are written as
# `wait_for_condition <tries> <delay-ms>` with totals as short as five seconds,
# which is fine on an idle machine and not fine on one running sixty containers:
# a failover that takes eight seconds instead of four is a correct failover, and
# the harness would call it a failure.
#
# The floor here is 120 s rather than the single-server adapter's 20 s, because a
# cluster failover legitimately takes tens of seconds -- the node timeout alone
# is 3000 ms by default, the election adds rank delay on top, and the box these
# runs share is at 2.6x oversubscription -- at 60 s the failover loop still
# failed 9 of 192 where three consecutive calibration runs had passed 192/192.
# Only the number
# of retries changes; the condition, and therefore what makes a test pass, does
# not.
if {[llength [info procs wait_for_condition]] > 0} {
    rename wait_for_condition __harbor_orig_wait_for_condition
    proc wait_for_condition {maxtries delay e _else_ elsescript} {
        if {$delay > 0} {
            set floor [expr {int(ceil(120000.0 / $delay))}]
            if {$maxtries < $floor} { set maxtries $floor }
        }
        uplevel 1 [list __harbor_orig_wait_for_condition $maxtries $delay $e $_else_ $elsescript]
    }
}

rename test __harbor_orig_test
proc test {descr code} {
    if {[dict size $::harbor_only] > 0 && ![dict exists $::harbor_only $descr]} { return }
    # The original reports an assertion failure by returning -code continue and
    # re-raises anything else; nothing else about the outcome is observable.
    set __rc [catch {uplevel 1 [list __harbor_orig_test $descr $code]} __e]
    if {$__rc == 0} {
        harbor_record ok $descr
        return
    }
    harbor_record err $descr
    catch {incr ::failed}
    if {$__rc == 4} { return -code continue }
    return
}

rename run_tests __harbor_orig_run_tests
proc run_tests {} {
    set tests [lsort [glob ../tests/*]]
    foreach t $tests {
        if {$::run_matching ne {} && [string match $::run_matching $t] == 0} { continue }
        if {[file isdirectory $t]} continue
        set ::harbor_unit [file tail $t]
        puts "Testing unit: $::harbor_unit"
        if {[catch {uplevel #0 [list source $t]} e]} {
            harbor_record fileerr $::harbor_unit
            puts "unit $::harbor_unit aborted: $e"
        }
    }
}

# The candidate's server is exec'd by this harness and would otherwise inherit
# the grader's environment, including the path of the result log. Strip every
# HARBOR_* variable around any spawn so a server cannot write its own score.
proc harbor_scrub_env {} {
    set saved [dict create]
    foreach k [array names ::env HARBOR_*] {
        dict set saved $k $::env($k)
        unset ::env($k)
    }
    return $saved
}
proc harbor_restore_env {saved} {
    dict for {k v} $saved { set ::env($k) $v }
}
foreach __sp {spawn_server exec_instance restart_server} {
    if {[llength [info procs $__sp]] > 0} {
        rename $__sp __harbor_orig_$__sp
        proc $__sp {args} [format {
            set __s [harbor_scrub_env]
            set __rc [catch {uplevel 1 [list __harbor_orig_%s {*}$args]} __r __o]
            harbor_restore_env $__s
            if {$__rc} { return -options $__o $__r }
            return $__r
        } $__sp]
    }
}

# ...and around EVERY exec, which is the only wrap that actually fires here.
#
# Round-2 review of step 4 found the hole: `tests/support/server.tcl` starts the
# candidate with a bare
#
#     set pid [exec src/redis-server $config_file > $stdout 2> $stderr &]
#
# There is no `spawn_server` and no `restart_server` in the single-server
# harness, so the three wraps above never fire on components A, B or B0. The
# JVM therefore inherited HARBOR_ONLY_FILE -- every scored test name -- and
# HARBOR_RESULT_LOG, whose shards `run.py` unions and treats as the score. A
# server that wrote `ok\t<name>` for each line of the only-file would have
# taken 1.0 on three quarters of a step without implementing a command.
#
# Wrapping `exec` itself is the fix: it is the primitive every spawn goes
# through, in both harnesses and in any future one, and nothing this harness
# execs needs a HARBOR_* variable -- they are read once when this file is
# sourced and kept in Tcl variables from then on. `{*}$args` preserves
# redirections, `&`, and exec's own flags.
# The harness re-execs ITSELF to start its test clients
# (`exec $tclsh [info script] ... --client`), and those clients need the two
# variables to know what to score and where to log. So the scrub applies to
# every exec except one whose program is the Tcl interpreter: the harness may
# hand its own children the grader's environment, and nothing else may have it.
proc harbor_is_tcl_exec {args} {
    foreach a $args {
        if {[string index $a 0] eq "-"} { continue }
        set b [file tail $a]
        return [expr {[string match "tclsh*" $b] || $a eq [info nameofexecutable]}]
    }
    return 0
}
if {[llength [info procs __harbor_orig_exec]] == 0} {
    rename exec __harbor_orig_exec
    proc exec {args} {
        if {[harbor_is_tcl_exec {*}$args]} {
            return [uplevel 1 [list __harbor_orig_exec {*}$args]]
        }
        set __s [harbor_scrub_env]
        set __rc [catch {uplevel 1 [list __harbor_orig_exec {*}$args]} __r __o]
        harbor_restore_env $__s
        if {$__rc} { return -options $__o $__r }
        return $__r
    }
}
