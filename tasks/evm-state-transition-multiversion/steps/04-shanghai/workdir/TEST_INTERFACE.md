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
checks nothing if that exits non-zero. All three subcommands answer `--help`
with exit 0: `t8n --help` is the gate, and `t9n --help` and `b11r --help` are
in scope as part of the command surface. Restructure the tree however you like;
keep the executable at that path, and keep every subcommand's `--help`
answering.

## Command surface

```
/app/build/t8n t8n  --input.alloc=F --input.env=F --input.txs=F \
                    --state.fork=NAME --state.chainid=N --state.reward=N \
                    --output.basedir=D --output.result=F --output.alloc=F
/app/build/t8n t9n  --input.txs=F --state.fork=NAME --state.chainid=N
/app/build/t8n b11r --input.header=F --input.txs=F --input.ommers=F \
                    --output.basedir=D --output.block=F
```

`--input.txs` accepts **either** a JSON array of transaction objects **or**, when
the filename ends in `.rlp`, a hex string holding an RLP list of
EIP-2718-enveloped transactions. The tool decodes the envelope itself; the
input does not say which transaction type each element is.

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

`--state.reward=-1` means *no block reward accounting*. Every `t8n`
invocation passes `-1`; it is the only value the conformance fixtures agree
with at every fork. `t9n` and `b11r` take no reward flag: neither of them runs a
state transition.

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

`t9n` takes **no output flags**. Its result document *is* its standard output,
and nothing else may go there: a JSON array with one entry per input
transaction, in input order. An entry has exactly one of four shapes.

| shape | keys | when |
| --- | --- | --- |
| accepted | `address`, `hash`, `intrinsicGas` | the transaction decodes, its signature recovers, and its `gasLimit` covers its intrinsic gas |
| under-funded | `error`, `address`, `hash`, `intrinsicGas` | it decodes and recovers, but `gasLimit` is below the intrinsic gas the fork charges |
| unrecoverable | `error`, `hash` | it decodes, so it has a hash, but the signature does not recover a sender — its `s` value is above secp256k1n/2, which Homestead forbids — or the transaction is invalid for this fork or chain id |
| undecodable | `error` | the element is not a transaction this tool can decode at all — an unknown EIP-2718 envelope type, a malformed body — so there is no hash to report |

The last two shapes are the ones that separate an element from its container:
an element that does not decode does **not** end the run. `t9n` walks the input
list element by element, and an element it cannot decode becomes an entry
carrying `error` and nothing else, in that element's position, beside the
entries for its well-formed neighbours.

`address` is the recovered sender and `hash` the transaction hash — for a
legacy transaction the keccak-256 of its RLP, and for a typed one the
keccak-256 of `TransactionType || TransactionPayload`, the same bytes
`docs/fork/berlin/eip-2718.md` puts in the transactions trie.

`intrinsicGas` is the gas the fork charges before execution begins.
`docs/fork/london/eip-1559.md` states the whole of it for a call:

```
21000 + 16 * non-zero calldata bytes + 4 * zero calldata bytes
      + 1900 * access list storage keys + 2400 * access list addresses
```

A **contract creation** — a transaction with no `to` — is charged **32000
more**, for 53000 rather than 21000 on empty calldata. That surcharge is the
one term of the formula no vendored EIP file restates, so it is stated here.

`t9n` is checked at London at this step, and later forks reprice some of
these terms, so this number is not fixed for the life of the task.

`--state.chainid` is applied **per transaction**, not to the batch: a
replay-protected transaction signed for a different chain id becomes one
unrecoverable entry beside its accepted neighbours, never a tool-level error.

`b11r` assembles a block and takes **no fork and no chain id** — it is
fork-agnostic, and its flag surface is exactly `--input.header`,
`--input.txs`, `--input.ommers`, `--output.basedir`, `--output.block` and the
`--seal.*` family. It writes the document named by `--output.block`:

```json
{"rlp": "0x...", "hash": "0x..."}
```

`rlp` is the RLP of the whole block `[header, transactions, ommers]` and
`hash` is the keccak-256 of the header's RLP alone.

`--input.header` carries the header fields. **No case supplies
`sha3Uncles`**, so the tool must derive the uncle hash from `--input.ommers`
and write it into the header it builds. Each element of the `--input.ommers`
array is the RLP of a whole *block*, `[header, transactions, ommers]`, not of
a bare header; the tool keeps that block's header and discards the rest. The
kept header enters the assembled block as a **decoded header** — the nested
list of its fields, the same shape the block's own header has — not as the byte
string it was decoded from; and the uncle hash written into the header is
**keccak-256 of the RLP of the list of those headers**, in the order
`--input.ommers` gives them. Encoding them as byte strings puts a byte string
where the block's ommer list has a header, and no case accepts that.

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
| `currentDifficulty` | the block's difficulty — see below |

Present when non-empty:

| field | meaning |
| --- | --- |
| `rejected` | array of `{"index": N, "error": "..."}` for transactions the fork rejects |

Present from the fork that defines them:

| field | from |
| --- | --- |
| `withdrawalsRoot` | Shanghai |
| `blobGasUsed`, `currentExcessBlobGas` | Cancun |
| `requestsHash` | Prague |

`currentDifficulty` is present at **every** fork, not only from the Merge: the
Merge removes the quantity, not the key. Before the Merge it carries the
difficulty `--input.env` supplied; from the Merge (step 03) onward there is no
difficulty to report and the key carries `null`.

`currentBaseFee` is an **`--input.env` field, not a result field**. The tool
reads it and never echoes it back; emitting it is neither required nor checked.

## Exit status

- **0** — the transition ran. This includes a run where every transaction was
  rejected: a transaction the fork refuses is recorded in `rejected[]`, not
  reported as a tool error.
- **non-zero** — an *input document* was malformed (unparseable JSON, a bad RLP
  payload, a fork name the tool does not know). On a non-zero exit the tool must
  not leave a partial `--output.result` file behind.

That distinction is checked. Turning a rejected transaction into a non-zero exit,
or a malformed input into a zero exit with an empty result, both fail.

The line runs **around the input document, not around each item inside it**.
For an RLP transaction list that means:

- The *list framing* is the input document, and the framing rule runs around
  **what decodes**: a usable payload decodes as exactly one RLP list, and the
  input document is the **first** JSON value in the file, so bytes after the
  closing quote are not part of it. A payload that will not decode at all — a
  value that is not a list, or a list that is truncated before its end — is a
  malformed input document, and one rule covers all three subcommands. The one
  payload that is not an error is an empty but well-formed list: it carries
  zero transactions, and either an empty array or `null` is accepted for that
  report. The two inherited groups at this step split the step-02 carry-overs
  between them: the London corpus carries the `t9n` cases and the Berlin corpus
  the `b11r` ones, both passing their transaction lists this way.
- What happens to an element *inside* a well-formed list depends on what the
  subcommand is being asked for. `t9n` is asked for a report on each
  transaction, so an element it cannot decode is one of the findings it
  reports: it becomes an entry carrying `error` in that element's position,
  beside the entries for its well-formed neighbours, and the run exits **0**.
  `t8n` and `b11r` are asked for a state transition and a block, and an element
  they cannot decode leaves them nothing to compute; they exit non-zero.
