# Step 2 — Homestead

## 1. Implement this milestone

`/app/evm` holds a C implementation of go-ethereum's `evm` tooling binary. Running
`make` with no arguments in `/app/evm` must keep producing the executable
`/app/evm/evm`, an ELF executable, and that binary must implement the `t8n`,
`t9n` and `b11r` subcommands as `/app/SPEC/evm-tool-README.md` and
`/app/SPEC/evm-tool-usage.txt` specify them.

Add support for the fork name `Homestead`, that is the Homestead rules (EIP-606), so that `t8n`, `t9n` and `b11r`
produce the results those rules define:

- `DELEGATECALL` (EIP-7)
- the EIP-2 contract-creation rules: the intrinsic gas of a creating transaction becomes 53000, and a contract creation that runs out of gas while storing its code fails entirely instead of leaving an empty account
- the EIP-2 signature rule: a transaction whose `s` value is greater than `secp256k1n/2` is invalid
- the EIP-2 difficulty adjustment

A fork changes only what its own EIPs change: at `Homestead` everything the list above
does not mention — the rest of the gas schedule, the rest of the opcode set, the
transaction types, the refunds, the state-clearing rules, the difficulty and the
fee computations — is exactly what `Frontier` already does.

`t9n` performs the static transaction checks the README lists — intrinsic gas,
maximum values on integers (among them that the nonce is below 2^64 − 1 and that the
gas limit times the gas price fits in 256 bits), fee semantics and newer transaction
types on older
forks — and prints a JSON array on standard output with one entry per transaction,
carrying the transaction `hash` whenever the transaction decodes at all, the
sender `address` and its `intrinsicGas` when it is valid, and an `error` when it
is not — a transaction that is well formed but breaks a validation rule reports
both its `hash` and its `error`, and only one that fails to decode at all has
nothing but the `error` to report; an invalid transaction does not
change the exit status, which stays 0. `b11r` assembles a block from a
header object, an RLP transaction list and the optional lists the tool accepts, and
reports the block RLP and the block hash; each entry of the ommers input is
the RLP of the ommer's *block*, from which the tool keeps the header, even though
the one-line description of `--input.ommers` in `/app/SPEC/evm-tool-usage.txt`
calls it a header RLP. `t8n` accepts its transactions either as
the JSON array the README describes — including the `secretKey` form, which the tool
signs itself — or as the RLP transaction list in a file
whose name ends in `.rlp`; a JSON transaction uses the execution-apis field names,
so `input` for the calldata, and may carry `secretKey` and `protected` as
`/app/SPEC/evm-tool-testdata/` shows. A `secretKey` transaction that carries `"protected": false`
is signed in the original form, with `v` of 27 or 28, as
`/app/SPEC/evm-tool-testdata/23` shows; the tool then recovers its sender with the
signer of the selected fork, exactly as for a transaction that arrived signed. `t8n`
also honours `--output.basedir`, the
`stdout` output target, `--output.body`, `--state.reward` of zero (which applies a
reward of zero and so still touches the coinbase) and the README's exit codes: 3
for an unsupported fork name and 4 when `BLOCKHASH` needs a hash `env.blockHashes`
did not supply. In the result object every receipt carries both `root` and
`status`: at `Homestead` `root` is the intermediate post-transaction state root, while `status` always reports the outcome; and the
receipt of a creation transaction carries the derived contract address whether or
not the creation succeeded.

Everything the earlier fork names already do must keep working unchanged. The
earlier fork names — `Frontier` — remain in use
alongside `Homestead`, and every one of them must still produce that fork's own
results: its own gas schedule, its own opcode availability, its own transaction
types, its own refunds and its own state-clearing rules. A change that is correct
for `Homestead` but leaks into an earlier fork name is wrong.

## 2. Read these local references

**Normative semantics.** The EIPs this fork consists of are shipped verbatim: `/app/SPEC/eips/eip-606.md`, `/app/SPEC/eips/eip-2.md`, `/app/SPEC/eips/eip-7.md`.

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
python3 /app/tests/run_corpus.py /app/tests/self/02-homestead /app/evm/evm
```

The corpora under `/app/tests/self/` are representative samples of those public suites.
Passing them is necessary, not sufficient.

These outputs must be right, and nothing else in the output matters:

- `stateRoot`, the root of the post state trie
- `txRoot`, the root of the transaction trie
- `receiptsRoot`, the root of the receipt trie
- `logsHash`, the keccak of the RLP of every log the block produced
- `logsBloom`, the block's bloom filter over those logs
- `gasUsed` for the block
- `currentDifficulty`, the difficulty this fork's formula gives
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
