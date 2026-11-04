# Coverage for Ruby 1.8.7

This milestone covers 744 programs.  Each one is run as
`ruby <switches> <program>` from a scratch directory.  A program passes
when its standard output is byte-for-byte what CRuby 1.8.7 produces, its
exit status matches and -- for a non-zero status -- the exception class
named on standard error matches (a program rejected as a syntax error
counts as `SyntaxError` however the message is phrased); programs in the
`warn` stratum must also agree about whether a warning was printed.
The areas below are exactly the areas those programs cover, with the
number of programs in each.

| stratum | programs |
| --- | --- |
| `core` | 684 |
| `warn` | 60 |

| area | programs |
| --- | --- |
| `core/array` | 21 |
| `core/basicobject` | 3 |
| `core/class` | 8 |
| `core/comparable` | 2 |
| `core/exception` | 4 |
| `core/false` | 3 |
| `core/float` | 18 |
| `core/hash` | 13 |
| `core/integer` | 30 |
| `core/kernel` | 2 |
| `core/math` | 13 |
| `core/module` | 8 |
| `core/nil` | 5 |
| `core/numeric` | 3 |
| `core/other` | 22 |
| `core/range` | 19 |
| `core/regexp` | 14 |
| `core/string` | 14 |
| `core/struct` | 13 |
| `core/symbol` | 4 |
| `core/systemexit` | 1 |
| `core/true` | 2 |
| `dual/containers` | 32 |
| `dual/frozen-literals` | 12 |
| `dual/keyword-arguments` | 5 |
| `dual/objects` | 33 |
| `dual/other` | 63 |
| `dual/rejected-construct` | 20 |
| `dual/strings` | 43 |
| `dual/warning-behaviour` | 36 |
| `language/and` | 5 |
| `language/array` | 5 |
| `language/assignments` | 5 |
| `language/case` | 5 |
| `language/def` | 5 |
| `language/ensure` | 5 |
| `language/hash` | 1 |
| `language/if` | 5 |
| `language/loop` | 5 |
| `language/method` | 4 |
| `language/module` | 3 |
| `language/not` | 5 |
| `language/numbers` | 5 |
| `language/or` | 2 |
| `language/order` | 3 |
| `language/precedence` | 5 |
| `language/proc` | 5 |
| `language/range` | 3 |
| `language/redo` | 3 |
| `language/regexp` | 6 |
| `language/retry` | 2 |
| `language/string` | 5 |
| `language/symbol` | 5 |
| `language/unless` | 3 |
| `language/until` | 5 |
| `language/variables` | 5 |
| `language/while` | 5 |
| `ruby-test/array` | 10 |
| `ruby-test/assignment` | 5 |
| `ruby-test/bignum` | 1 |
| `ruby-test/call` | 1 |
| `ruby-test/case` | 1 |
| `ruby-test/class` | 5 |
| `ruby-test/clone` | 2 |
| `ruby-test/dup` | 3 |
| `ruby-test/econv` | 4 |
| `ruby-test/enum` | 5 |
| `ruby-test/exception` | 9 |
| `ruby-test/fixnum` | 6 |
| `ruby-test/float` | 5 |
| `ruby-test/ifunless` | 3 |
| `ruby-test/inlinecache` | 1 |
| `ruby-test/integer` | 2 |
| `ruby-test/integer_comb` | 1 |
| `ruby-test/literal` | 9 |
| `ruby-test/math` | 1 |
| `ruby-test/method` | 5 |
| `ruby-test/method_cache` | 1 |
| `ruby-test/nomethod_error` | 2 |
| `ruby-test/not` | 2 |
| `ruby-test/object` | 7 |
| `ruby-test/other` | 21 |
| `ruby-test/parse` | 1 |
| `ruby-test/path` | 2 |
| `ruby-test/proc` | 1 |
| `ruby-test/range` | 9 |
| `ruby-test/regexp` | 7 |
| `ruby-test/signal` | 1 |
| `ruby-test/sprintf` | 5 |
| `ruby-test/string` | 8 |
| `ruby-test/stringchar` | 2 |
| `ruby-test/struct` | 1 |
| `ruby-test/super` | 4 |
| `ruby-test/transcode` | 4 |
| `ruby-test/whileuntil` | 1 |
| `ruby-test/yield` | 5 |
