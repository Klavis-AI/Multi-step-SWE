# The in-scope set for this milestone

Exactly these 2 functions, one per line, in the harness's own spelling. They are
what `/app/BENCHMARK.md` calls "the in-scope set"; `taskset -c 0 /tmp/kbench
samples 1 all` prints one line for each of them.

A function with no optimized implementation is measured at 1.0, the same
speed as the tree you inherited, so neither of them is optional.

```
join.decode.selected.fixed
join.decode.selected.varlen
```
