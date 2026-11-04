# TEST_INTERFACE.md — the test interface

This contract is **fixed for all seven steps**. The verifier drives your tool
through exactly this surface and nothing else. Later steps add forks and
subcommands; they never change the spelling of what is already here.

## Build

`/app/build.sh` must produce the executable **`/app/build/t8n`**. The verifier
runs `/app/build.sh` first and checks nothing if it does not produce that path.
The first build attempt runs with `GOPROXY=off` against a prewarmed module
cache, and the prewarmed graph covers every dependency the task's own `go.mod`
names, so a tree that has not added a dependency builds with no network at all.
A build that does need the network is retried with a proxy reachable rather
than treated as a failure: what is required is that `/app/build/t8n` exists
afterwards, not which attempt produced it.

The verifier then runs **`/app/build/t8n t8n --help`** as a liveness check and
checks nothing if that exits non-zero. Restructure the tree however you like;
keep the executable at that path, and keep `t8n --help` answering.

## Command surface

```
/app/build/t8n t8n  --input.alloc=F --input.env=F --input.txs=F \
                    --state.fork=NAME --state.chainid=N --state.reward=N \
                    --output.basedir=D --output.result=F --output.alloc=F
# (t9n is introduced at step 02)
# (b11r is introduced at step 02)
```

`--input.txs` accepts **either** a JSON array of transaction objects **or**, when
the filename ends in `.rlp`, an RLP list of EIP-2718-enveloped transactions. The
tool decodes the envelope itself; the input does not say which transaction type
each element is.

A `.rlp` file is **still a JSON document**. Its entire content is one JSON
*string literal* whose value is the `0x`-prefixed hex encoding of the RLP list —
not raw binary, and not bare hex text. A one-transaction file therefore begins

```
"0xf90703b9070001f906fc01800a8301faa094...
```

with the double quote and the `0x` both part of the file. Parse it as JSON and
hex-decode the string that yields; hex-decoding the file's own bytes fails,
because the first byte is `"`. An empty transaction list is the file `"0xc0"` —
the six characters `"`, `0`, `x`, `c`, `0`, `"`.

In the JSON array form each transaction object carries the following keys.
All values are `0x`-prefixed hex strings.

| Type | Keys |
| --- | --- |
| 0 (legacy) | `nonce`, `gasPrice`, `gas`, `to`, `value`, `input`, `v`, `r`, `s` |
| 1 (EIP-2930) | type-0 keys plus `type` (`"0x1"`), `chainId`, `accessList` |
| 2 (EIP-1559) | `nonce`, `maxFeePerGas`, `maxPriorityFeePerGas`, `gas`, `to`, `value`, `input`, `accessList`, `chainId`, `type` (`"0x2"`), `v`, `r`, `s` |

For types 1 and 2 each entry in `accessList` is an object with an `address` key
and a `storageKeys` key (a JSON array of 32-byte hex strings).

The `v` key in the JSON form holds the parity as an integer (`"0x0"` or `"0x1"`).
In a raw EIP-2718 RLP envelope the same field is spelled `yParity` in the EIP
text; the tool uses the JSON spelling `v` throughout its JSON input form.

A transaction object may also carry a `secretKey` field, and does not have to
carry a usable signature: when `v`, `r` and `s` are absent or zero, the tool
signs the transaction with `secretKey` instead, and the object still needs the
key list above. `v`, `r`, `s` are required fields of the input form; a
`secretKey` transaction supplies them as `"0x0"`.

`--state.reward=-1` means *no block reward accounting*. Every case passes
`-1`; it is the only value the conformance fixtures agree with at every fork.

`--output.basedir` is the directory the output files are written into;
`--output.result`, `--output.alloc` and `--output.block` are filenames relative
to it.

The document named by `--output.alloc` is the **post-transition world state**,
in the same shape `--input.alloc` is read in: a JSON object keyed by account
address, each value an object carrying that account's `balance`, `nonce`,
`code` and `storage` (a map from slot to value). A field that is zero or empty
may be omitted, and an account the transition deleted may be omitted entirely.
The verifier canonicalises both sides before comparing, so none of the following is
a difference: `0x0` against `0x00` against `0`; `""` against `"0x"`; an address
or storage key written at a different width; a storage slot holding zero
against an absent slot; an account with no balance, no nonce, no code and no
storage against an absent account. Only values differ, never spellings.

`t9n` takes **no output flags**. It writes its result to **stdout** and nothing
else to stdout: a JSON array with one entry per input transaction, in input
order, each carrying the recovered sender `address`, the transaction `hash` and
its `intrinsicGas` when the transaction is valid, and an `error` describing why
when it is not.

`b11r` writes the block it assembles to `--output.block`, as a JSON object with
exactly two keys:

```
{"rlp": "0x...", "hash": "0x..."}
```

`rlp` is the complete RLP encoding of the assembled block and `hash` is the
keccak-256 hash of its header. `b11r` takes **no `--state.fork` and no
`--state.chainid`**: it assembles a block and performs no state transition, so
it is fork-agnostic.

## `--input.env`

The block environment the transition runs against: a JSON object whose quantity
values are `0x`-prefixed hex strings. A field the selected fork does not use is
ignored rather than refused — a `currentRandom` in a London env, or the
`currentDifficulty` every Shanghai env carries, is well-formed, and the tool
must not require its absence. Where a fork instead makes a carried field's
*value* significant, that fork's own instruction says so.

| field | from |
| --- | --- |
| `currentCoinbase`, `currentDifficulty`, `currentGasLimit`, `currentNumber`, `currentTimestamp` | every fork |
| `blockHashes`, `ommers` | every fork |
| `currentBaseFee` | London, and **required** from there |
| `currentRandom` | the Merge |
| `withdrawals` | Shanghai |
| `parentBeaconBlockRoot`, `currentExcessBlobGas` | Cancun |

A block-derived case may also carry a `parent*` set — `parentUncleHash`,
`parentDifficulty`, `parentTimestamp`, `parentGasLimit`, `parentGasUsed`,
`parentBaseFee`, and from Cancun `parentBlobGasUsed` and
`parentExcessBlobGas` — naming the parent block a block-level field is computed
against.

`blockHashes` is keyed by the **decimal spelling of the block number** —
`{"63": "0x…"}`, beside a `currentNumber` that is hex, each value a 32-byte hash
— and it supplies those blocks to `BLOCKHASH`. A reader that builds its own
lookup key must spell it in decimal.

`ommers` is an array of `{"address": "0x…", "delta": N}`, with `N` a JSON number
— the uncle's distance, which the fork's reward accounting uses. Every case
passes `--state.reward=-1`, so it moves no balance at these forks.

`currentBaseFee` is **required** from London onward: an env for a fork that has
a base fee and omits the field is a malformed input document, not a base fee of
zero.

## Run limits

Every candidate invocation is bounded. A corpus case has 30 s, and its group a
clock of `90 + ceil(cases / 4) × 30` seconds; the probe groups are bounded by
their own fixed caps, which are at least as generous. An invocation killed by
its clock is a failure, and a group whose clock expires reports a failure for
every case in it — so a tool that hangs on one case does not hold up the rest of
its group, but a group that hangs everywhere fails only that group, not the whole run.

## Fork vocabulary

`Berlin`, `London`, `Merge`, `Shanghai`, `Cancun`, `Prague`, `Osaka`.

Spelled exactly that way, as the value of `--state.fork`. A step is checked at
its own fork **and at every earlier fork**, so a fork name that stops being
accepted is a regression.

## `--output.result` schema

Always present:

| field | meaning |
| --- | --- |
| `stateRoot` | post-transition state root |
| `txRoot` | transactions trie root |
| `receiptsRoot` | receipts trie root (**not** `receiptRoot`) |
| `logsHash` | hash of the RLP-encoded log list |
| `logsBloom` | 256-byte bloom filter |
| `gasUsed` | total gas used by the block's transactions |
| `receipts` | array, one entry per *included* transaction |

Present when non-empty:

| field | meaning |
| --- | --- |
| `rejected` | array of `{"index": N, "error": "..."}` for transactions the fork rejects |

`receipts` and `rejected` together account for every transaction in
`--input.txs`: a transaction is either included (one `receipts` entry, in input
order) or rejected (one `rejected` entry carrying its input index).

### A `receipts[]` entry

Each entry is an Ethereum transaction receipt in its usual JSON spelling. Only
one of its fields is checked:

| field | meaning |
| --- | --- |
| `cumulativeGasUsed` | gas used by this transaction and every earlier included one; the **last** entry's value is therefore the block total and must equal the top-level `gasUsed` |

The other receipt fields a receipt customarily carries — `status`, `gasUsed`,
`logs`, `logsBloom`, `transactionHash`, `transactionIndex`, `type`,
`contractAddress`, `root`, `blockHash` — may be present and none of them is
checked.

### When no transaction is included

With an empty transaction list, or one in which every transaction is rejected,
`txRoot` and `receiptsRoot` are both the root of an empty trie,
`0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421`;
`logsHash` is the hash of the empty log list,
`0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347`;
`logsBloom` is 256 zero bytes; `gasUsed` is `0x0`; and `receipts` is `[]`, not
absent and not null.

Present from the fork that defines them:

| field | from |
| --- | --- |
| `currentDifficulty` | the Merge (step 03); absent before it |
| `withdrawalsRoot` | Shanghai |
| `blobGasUsed`, `currentExcessBlobGas` | Cancun |
| `requestsHash` | Prague |

`currentBaseFee` is an **`--input.env` field, not a result field**. The tool
reads it and never echoes it back; emitting it is neither required nor checked.

## Exit status

- **0** — the transition ran. This includes a run where every transaction was
  rejected: a transaction the fork refuses is recorded in `rejected[]`, not
  reported as a tool error.
- **non-zero** — an *input document* was malformed or could not be read
  (unparseable JSON, a bad RLP payload, a fork name the tool does not know, an
  `--input.alloc` that is missing, unreadable, or not a well-formed alloc). On a
  non-zero exit the tool must not leave a partial `--output.result` file behind.

That distinction is checked. Turning a rejected transaction into a non-zero exit,
or a malformed input into a zero exit with an empty result, both fail.
