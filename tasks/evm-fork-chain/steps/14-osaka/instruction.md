# Step 14 — Osaka

## 1. Implement this milestone

`/app/evm` holds a C implementation of go-ethereum's `evm` tooling binary. Running
`make` with no arguments in `/app/evm` must keep producing the executable
`/app/evm/evm`, an ELF executable, and that binary must implement the `t8n`,
`t9n` and `b11r` subcommands as `/app/SPEC/evm-tool-README.md` and
`/app/SPEC/evm-tool-usage.txt` specify them.

Add support for the fork name `Osaka`, that is the Osaka rules (EIP-7607, the Fusaka network upgrade), so that `t8n`, `t9n` and `b11r`
produce the results those rules define:

- `CLZ` (EIP-7939)
- the secp256r1 verification precompile at `0x100` (EIP-7951)
- EIP-7823: the `MODEXP` input size bounds, and EIP-7883: the `MODEXP` repricing
- EIP-7825: the per-transaction gas limit cap
- EIP-7918: the blob base-fee floor — when `BLOB_BASE_COST` times the parent base fee exceeds `GAS_PER_BLOB` times the parent blob base fee, the excess blob gas carried into this block is computed from the reserve-price branch instead of the plain subtraction
- EIP-7594: at most six blobs in one transaction, whatever the block allows

A fork changes only what its own EIPs change: at `Osaka` everything the list above
does not mention — the rest of the gas schedule, the rest of the opcode set, the
transaction types, the refunds, the state-clearing rules, the difficulty and the
fee computations — is exactly what `Prague` already does.

One collision rule holds at this fork and at every earlier one, although the Yellow
Paper's collision condition predates it: a contract creation — by a creation transaction,
`CREATE` or `CREATE2` — whose target address already has non-empty storage is an address
collision, exactly as one whose target has non-empty code or a non-zero nonce is
(EIP-7610, adopted retroactively back to Frontier; local copy:
`/opt/docs/eips/eip-7610.md`).
One transaction rule holds the same way at this fork and at every earlier one: a
transaction whose sender account already has code is invalid and is listed in
`rejected` (EIP-3607, which the tool applies at every fork name), except that from
`Prague` on, code that is an EIP-7702 delegation designator does not count.

`t9n` performs the static transaction checks the README lists — intrinsic gas,
maximum values on integers (among them that the nonce is below 2^64 − 1 and that the
gas limit times the gas price, and the gas limit times `maxFeePerGas`, each fit in
256 bits), fee semantics and newer transaction types on older
forks — and prints a JSON array on standard output with one entry per transaction,
carrying the transaction `hash` whenever the transaction decodes at all, the
sender `address` and its `intrinsicGas` when it is valid, and an `error` when it
is not — a transaction that is well formed but breaks a validation rule reports
both its `hash` and its `error`, and only one that fails to decode at all has
nothing but the `error` to report; an invalid transaction does not
change the exit status, which stays 0. `b11r` assembles a block from a
header object, an RLP transaction list and the optional lists the tool accepts, and
reports the block RLP and the block hash, whose header object also carries `withdrawalsRoot`, `blobGasUsed`, `excessBlobGas`, `parentBeaconBlockRoot` and `requestsHash`; each entry of the ommers input is
the RLP of the ommer's *block*, from which the tool keeps the header, even though
the one-line description of `--input.ommers` in `/app/SPEC/evm-tool-usage.txt`
calls it a header RLP. `t8n` accepts its transactions either as
the JSON array the README describes — including the `secretKey` form, which the tool
signs itself — or as the RLP transaction list in a file
whose name ends in `.rlp`; a JSON transaction uses the execution-apis field names,
so `input` for the calldata, and may carry `secretKey` and `protected` as
`/app/SPEC/evm-tool-testdata/` shows. How the tool signs a `secretKey` transaction does not
depend on the selected fork and is the same at every fork name: a typed transaction is
signed in the form its type defines, and a legacy one in the EIP-155 form for the chain
id `--state.chainid` gives, unless it carries `"protected": false`, which selects the
original form with `v` of 27 or 28 (as `/app/SPEC/evm-tool-testdata/23` shows). The
tool then recovers the sender with the signer of the selected fork, exactly as for a
transaction that arrived signed, so a signature that fork does not accept gets the
transaction rejected; at a fork name older than `EIP158`, for instance, a legacy
`secretKey` transaction that does not carry `"protected": false` is rejected. `t8n`
also honours `--output.basedir`, the
`stdout` output target, `--output.body`, `--state.reward` of zero (which applies a
reward of zero and so still touches the coinbase) and the README's exit codes: 3
for an unsupported fork name and 4 when `BLOCKHASH` needs a hash `env.blockHashes`
did not supply. In the result object every receipt carries both `root` and
`status`: at `Osaka` `root` is empty, while `status` always reports the outcome; and the
receipt of a creation transaction carries the derived contract address whether or
not the creation succeeded.
The environment may state a quantity the tool would otherwise derive, and when it
does the stated value wins: `env.currentBaseFee` stands in for the EIP-1559
computation from `env.parentBaseFee`, `env.parentGasUsed` and `env.parentGasLimit`,
and `env.currentExcessBlobGas` stands in for the computation from
`env.parentExcessBlobGas` and `env.parentBlobGasUsed`. Both derivations are still
required for the environments that leave the quantity out, and both derived values
are reported in the result.

Everything the earlier fork names already do must keep working unchanged. The
earlier fork names — `Frontier`, `Homestead`, `EIP150`, `EIP158`, `Byzantium`, `Constantinople`, `ConstantinopleFix`, `Istanbul`, `Berlin`, `London`, `Paris`, `Shanghai`, `Cancun`, `Prague` — remain in use
alongside `Osaka`, and every one of them must still produce that fork's own
results: its own gas schedule, its own opcode availability, its own transaction
types, its own refunds and its own state-clearing rules. A change that is correct
for `Osaka` but leaks into an earlier fork name is wrong.

## 2. Read these local references

**Normative semantics.** The EIPs this fork consists of are shipped verbatim: `/app/SPEC/eips/eip-7607.md`, `/app/SPEC/eips/eip-7594.md`, `/app/SPEC/eips/eip-7823.md`, `/app/SPEC/eips/eip-7825.md`, `/app/SPEC/eips/eip-7883.md`, `/app/SPEC/eips/eip-7918.md`, `/app/SPEC/eips/eip-7939.md`, `/app/SPEC/eips/eip-7951.md`.

**The interface, adopted unchanged from go-ethereum.** `/app/SPEC/evm-tool-README.md` is
the upstream specification of the `t8n`, `t9n` and `b11r` tools: their flags, the `alloc`,
`env`, `txs`, `result` and `header` object shapes, the output files, the `stdin`/`stdout`
targets, the ommer-reward rule and the exit codes. `/app/SPEC/evm-tool-usage.txt` is the
upstream `--help` output for all three subcommands and fixes every flag name and default.
`/app/SPEC/evm-tool-testdata/` is upstream's own fixture set for these tools: each numbered
directory holds inputs and the `exp*.json` files that upstream asserts against.

The binary must handle these command lines; only the files they name matter, and no
other part of the CLI is relied on:

```text
evm t8n  --input.alloc=<dir>/alloc.json --input.env=<dir>/env.json \
         --input.txs=<dir>/txs.rlp|<dir>/txs.json --output.basedir=<dir> \
         --output.result=out_result.json --output.alloc=out_alloc.json \
         --state.fork=<ForkName> --state.chainid=<N> --state.reward=<N|0|-1>

     ... the same invocation with --output.body=out_body.rlp added

     ... the same invocation with --output.result=stdout --output.alloc=stdout

evm t9n  --input.txs=<dir>/txs.rlp --state.fork=<ForkName> --state.chainid=<N>

evm b11r --input.header=<dir>/header.json --input.txs=<dir>/txs.rlp \
         [--input.ommers=<dir>/ommers.json] [--input.withdrawals=<dir>/withdrawals.json] \
         --output.basedir=<dir> --output.block=out_block.json
```
The binary must also handle three further patterns built from those command lines,
under the same requirements:

- a multi-block chain, where the `alloc` the tool writes becomes the next block's
  `--input.alloc`, so the post state travels through the run and one wrong
  intermediate state makes every later block of that chain wrong
- inputs that are deterministic variations of public test fixtures — changed gas
  limits, balances and storage slots — whose correct results are the ones the
  reference implementation gives rather than ones a published suite records; treat
  them as ordinary inputs of the fork you are implementing
- inputs run under the fork name this step adds and inputs of the same shape run
  under the fork name it follows, whose correct results are the ones the reference
  implementation gives at each fork name: each must produce that fork's own result,
  and the two differ wherever this fork's rules change the answer

Each invocation of your binary gets a 90-second wall-clock limit and a 1,536 MiB
address-space limit, and runs as an unprivileged user; an invocation that exceeds either fails.
A step's full set of inputs gets 1,700 seconds on 7 vCPU as a whole, and an input with no
result when that runs out has failed, so the binary has to be quick as well as right — the
reference implementation takes about 85 seconds for a step's whole corpus.

**Local checks.** `/app/tests/run_corpus.py` runs a corpus of public Ethereum test
fixtures against your binary through the interface above:

```sh
python3 /app/tests/run_corpus.py /app/tests/self/14-osaka /app/evm/evm
```

The corpora under `/app/tests/self/` are representative samples of those public suites.
Passing them is necessary, not sufficient.

These outputs must be right, and nothing else in the output matters:

- `stateRoot`, the root of the post state trie
- `txRoot`, the root of the transaction trie
- `receiptsRoot`, the root of the receipt trie
- `logsHash`, the keccak of the RLP of every log the block produced
- `logsBloom`, the block's bloom filter over those logs
- `withdrawalsRoot`, the root of the withdrawal trie
- `requestsHash`, the commitment over the block's execution-layer requests
- `gasUsed` for the block
- `currentDifficulty`, the difficulty this fork's formula gives
- `currentBaseFee`, the base fee this fork's formula gives
- `blobGasUsed` for the block
- `currentExcessBlobGas` carried to the next block
- `rejected`, the indices of the transactions this fork refuses
- every receipt's `root`, `status`, `cumulativeGasUsed`, `gasUsed`, `contractAddress`, `transactionHash` and its number of logs
- the whole post state written to `--output.alloc`: every account's balance, nonce, code and non-zero storage
  (when `--output.alloc=stdout` is used, the object on standard output must still carry that
  post state under its `alloc` key; in that invocation only the `stateRoot` of the companion
  `result` object matters, so the stdout object's shape is what it must get right)
- each `t9n` entry's transaction `hash`
- each valid `t9n` entry's sender `address`
- each valid `t9n` entry's `intrinsicGas`
- whether each `t9n` entry reports an `error` at all: an entry the fork's rules reject must
  carry one and an entry it accepts must not, so the set of transactions you refuse has to
  match exactly; the wording of the message itself does not matter
- the `b11r` block `rlp`
- the `b11r` block `hash`
- the process exit status
- the value written to `--output.body`: the RLP transaction list, hex-encoded and
  placed in a JSON string, which is the format the README's own `signed_txs.rlp` example
  shows and the format `--input.txs` accepts back

Every field this list names must be present; an omitted field is wrong.
## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source, documents and
  spec material. Leave the tree self-describing: what you write down in it is all that
  survives this session. The `Makefile` is part of what survives, so make sure it rebuilds
  what it needs to when a header changes: an object file left stale by a header edit is a
  bug that shows up only as wrong results, not as a build failure.
- The project must build from source in a clean container with the same C toolchain and the same
  `libssl-dev`, `libsecp256k1-dev` and `libgmp-dev` as this one, and with no Go, Rust, JVM or
  Node toolchain and no Ethereum software. Other tools this image carries for your own use
  are not promised there — `xxd`, `unzip`, `git` and `curl` are absent — so `make` must not
  need them. It does carry a Python 3 interpreter, but every
  interpreter in the image has its execute bit removed before your binary runs, and your
  binary runs as an unprivileged user, so the binary
  `make` produces is what has to do the work. Packages you install at run time are not part of
  that container, so vendor the source of any third-party library you use under `/app/evm/`
  rather than relying on it being installed or fetched while `make` runs. The build starts
  by deleting `/app/evm/evm` and every object file, archive and ELF binary under `/app`,
  then runs `make` as an unprivileged user with a 1,500-second limit. The build
  has to succeed from the sources you leave in `/app/evm` alone, and the binary it produces
  has to be one those sources describe: a product of a Go toolchain is not allowed,
  whether it is that binary or any other program left in `/app` or in a temporary
  directory once `make` has run.
- Do not search for, download, copy, wrap, or invoke an online
  implementation or task-specific answer; work only from this workspace, its
  documentation and /opt/docs, and do not search the system for other implementations
  or answers.
