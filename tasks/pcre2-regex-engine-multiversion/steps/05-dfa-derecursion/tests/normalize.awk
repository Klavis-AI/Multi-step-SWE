# Normalises pcre2test output before comparison, by deleting the regions that
# expose the internal compiled representation or an implementation-defined
# quantity. Applied identically to the expected file and to the candidate's
# output, so it can never make a correct implementation fail; what it does is
# stop the grade depending on an encoding choice that no shipped document
# specifies.
#
# Deleted:
#   * the byte-code dump emitted by the `B`/`bincode`/`fullbincode` modifiers,
#     which is a listing of the internal opcode stream;
#   * `Memory allocation (code space)`, the compiled pattern's size in bytes;
#   * `Memory allocation (JIT code)`, which is 0 in this task's JIT-less build
#     but is still an implementation-defined quantity;
#   * the match-frame size reported by the `framesize` modifier;
#   * the integers printed by the `find_limits` modifier. The release ladder
#     renames these lines: through 10.23 they are `Minimum match limit` and
#     `Minimum recursion limit`; 10.30 renames the second to `Minimum depth
#     limit` and adds `Minimum heap limit` above it. pcre2api.3 describes the
#     quantity they count only as "a function called `match()`, which it calls
#     repeatedly" -- one particular decomposition's step/frame accounting --
#     and pcre2test.1 notes that the numbers vary between systems, so a matcher
#     that decomposes the search differently produces different numbers for the
#     same pattern. The LINES are still required and still compared; only the
#     numbers on them are dropped.
#
# Everything else -- match results, capture values, error messages and offsets,
# callout traces, substitution output, and every pcre2_pattern_info value -- is
# compared byte for byte.

# No interval expression anywhere below. `{60,}` is POSIX ERE, but mawk --
# which is /usr/bin/awk on a stock Debian image -- silently ignores intervals
# unless built with -W repetitions, so a rule written that way matches nothing
# and the filter becomes a no-op that still looks correct. A length test plus
# `/^-+$/` says the same thing in the subset every awk agrees on.
length($0) >= 60 && /^-+$/ { in_dump = 1 - in_dump; next }
in_dump                    { next }
/^Memory allocation \(code space\): [0-9]+$/ { next }
/^Memory allocation \(JIT code\): [0-9]+$/   { next }
/^Frame size for pcre2_match\(\): [0-9]+$/   { next }
# `find_limits` counts entries to the matcher's own step/frame accounting, so
# the number is a property of how the implementation decomposes the search and
# not of anything the manual pages specify. Keep the labels -- they are what the
# release ladder renames -- and erase the counts.
/^Minimum (heap|depth|match|recursion) limit = [0-9]+$/ {
    sub(/= [0-9]+$/, "= N"); print; next
}
                           { print }
