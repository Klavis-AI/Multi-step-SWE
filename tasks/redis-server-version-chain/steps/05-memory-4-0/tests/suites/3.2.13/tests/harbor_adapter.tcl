# Harbor scoring adapter for the Redis Tcl test suite.
#
# Sourced at the very bottom of tests/test_helper.tcl, after every support file
# and after option parsing, so it can wrap the harness procs without changing
# any pass/fail semantics. It adds exactly three things:
#
#   1. exact-name test selection from $env(HARBOR_ONLY_FILE) (the upstream
#      --only option is a glob match, and upstream test names contain glob
#      metacharacters),
#   2. a machine-readable per-test result log at $env(HARBOR_RESULT_LOG),
#   3. durability: a non-assertion exception inside a test, or anywhere in a
#      test file, is recorded and the run continues instead of aborting the
#      whole job (upstream --durable, which older harness versions lack).
#
# It never changes what makes a test pass.

set ::harbor_only [dict create]
if {[info exists ::env(HARBOR_ONLY_FILE)] && $::env(HARBOR_ONLY_FILE) ne ""} {
    set __hf [open $::env(HARBOR_ONLY_FILE) r]
    foreach __line [split [read $__hf] "\n"] {
        set __line [string trim $__line]
        if {$__line ne "" && [string index $__line 0] ne "#"} {
            dict set ::harbor_only $__line 1
        }
    }
    close $__hf
}

set ::harbor_log ""
if {[info exists ::env(HARBOR_RESULT_LOG)]} {
    set ::harbor_log "$::env(HARBOR_RESULT_LOG).[pid]"
}

proc harbor_record {status name} {
    if {$::harbor_log eq ""} return
    set f ""
    catch {set f $::curfile}
    if {[catch {
        set fh [open $::harbor_log a]
        fconfigure $fh -translation binary
        puts $fh "$status\t$name\t$f"
        close $fh
    }]} {}
}


# Explicit file list (upstream --single accepts a single file on older harness
# versions, and the file list differs per rung).
if {[info exists ::env(HARBOR_FILES)] && $::env(HARBOR_FILES) ne ""} {
    set ::all_tests {}
    foreach __f [split [string trim $::env(HARBOR_FILES)] " \n"] {
        if {$__f ne ""} { lappend ::all_tests $__f }
    }
}

# Redis internals are out of scope for this task (see the task line's authoring
# note): assert_encoding inspects OBJECT ENCODING / DEBUG OBJECT, so it is a
# no-op here. Every other assertion is untouched.
if {[llength [info procs assert_encoding]] > 0} {
    rename assert_encoding __harbor_orig_assert_encoding
    proc assert_encoding {enc key} { return }
}


# The harness's patience for "is the server up yet" is a C-era constant: the
# 2.8-6.x harness gives server_is_up 100 x 50ms = 5s and then *silently skips
# the whole file* (start_server_error returns rather than raising). A JVM on a
# loaded box needs longer than that, and every harness from 7.0 already waits
# two minutes, so raise the old one to the same order. Nothing about what makes
# a test pass changes.
if {[llength [info procs server_is_up]] > 0} {
    rename server_is_up __harbor_orig_server_is_up
    proc server_is_up {host port retrynum} {
        return [__harbor_orig_server_is_up $host $port [expr {$retrynum < 1200 ? 1200 : $retrynum}]]
    }
}
# Make a silently skipped file visible in the result log instead of vanishing.
if {[llength [info procs start_server_error]] > 0} {
    rename start_server_error __harbor_orig_start_server_error
    proc start_server_error {config_file error} {
        harbor_record startfail "start_server failed"
        __harbor_orig_start_server_error $config_file $error
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
    rename wait_for_condition __harbor_orig_wait_for_condition
    proc wait_for_condition {maxtries delay e _else_ elsescript} {
        if {$delay > 0} {
            set floor [expr {int(ceil(20000.0 / $delay))}]
            if {$maxtries < $floor} { set maxtries $floor }
        }
        uplevel 1 [list __harbor_orig_wait_for_condition $maxtries $delay $e \
                       $_else_ $elsescript]
    }
}

# Determinism: never silently drop to one client because the host looked slow.
proc is_a_slow_computer {} { return 0 }

rename test __harbor_orig_test
set ::harbor_test_nargs [llength [info args __harbor_orig_test]]
proc test {args} {
    set name [lindex $args 0]
    if {[dict size $::harbor_only] > 0 && ![dict exists $::harbor_only $name]} {
        return
    }
    set fwd [lrange $args 0 [expr {$::harbor_test_nargs - 1}]]
    set __p0 $::num_passed
    set __rc [catch {uplevel 1 [list __harbor_orig_test {*}$fwd]} __err]
    if {$__rc} {
        harbor_record err $name
        incr ::num_failed
        catch {send_data_packet $::test_server_fd err "$name (exception)\n$__err"}
    } elseif {$::num_passed > $__p0} {
        harbor_record ok $name
    } else {
        harbor_record err $name
    }
}

# File-level durability: an exception outside any test block (a failed
# start_server, a helper proc blowing up) must not abort the whole run.
foreach __p {execute_test_file execute_tests} {
    if {[llength [info procs $__p]] > 0} {
        rename $__p __harbor_orig_$__p
        proc $__p {name} [format {
            if {[catch {__harbor_orig_%s $name} e]} {
                harbor_record fileerr $name
                catch {send_data_packet $::test_server_fd err "file $name aborted\n$e"}
                catch {send_data_packet $::test_server_fd done "$name"}
            }
        } $__p]
    }
}

# Authoring aid: keep tests/tmp for inspection. Never set in the grader.
if {[info exists ::env(HARBOR_KEEP_TMP)] && [llength [info procs cleanup]] > 0} {
    rename cleanup __harbor_orig_cleanup
    proc cleanup {} { puts "harbor: tmp preserved" }
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
