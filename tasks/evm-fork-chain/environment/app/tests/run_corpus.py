#!/usr/bin/env python3
"""Drive an `evm` binary through a corpus of Ethereum test cases.

Usage: run_corpus.py <corpus-dir> <evm-binary> [--json out.json]
Environment: JOBS (parallel workers), CASE_TIMEOUT (seconds per case),
MEM_LIMIT_MB (address-space ceiling for the binary under test).

Every invocation of the binary under test gets a wall-clock limit and an address-space
limit.  The binary must work within CASE_TIMEOUT=90 and MEM_LIMIT_MB=1536, running as an
unprivileged user.  CASE_TIMEOUT defaults to 90 here and MEM_LIMIT_MB to 8192, so set
MEM_LIMIT_MB=1536 to run under both limits.  A case that exceeds either limit fails.

Every case runs the binary through the go-ethereum `evm` tooling CLI and compares
the output with the expectation recorded in cases.jsonl.  Prints a per-bucket
summary and, with --json, writes the per-case results.
"""
import json, os, subprocess, sys, tempfile, shutil, collections
from concurrent.futures import ProcessPoolExecutor

TIMEOUT = int(os.environ.get("CASE_TIMEOUT", "90"))
MEM_LIMIT_MB = int(os.environ.get("MEM_LIMIT_MB", "8192"))
SANDBOX_UID = os.environ.get("SANDBOX_UID")   # run the binary under test as this uid, if set

def _limits():
    """Cap the address space of the binary under test, so one runaway case cannot take
    the machine down with it.  Inherited across the exec that follows."""
    try:
        import resource
        lim = MEM_LIMIT_MB * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (lim, lim))
    except Exception:
        pass

def wrap(cmd):
    """Optionally drop privileges before running the binary under test."""
    if SANDBOX_UID and os.geteuid() == 0:
        return ["setpriv", "--reuid=" + SANDBOX_UID, "--regid=" + SANDBOX_UID,
                "--clear-groups", "--"] + cmd
    return cmd

def norm_hex(x):
    if x is None: return None
    if isinstance(x, str) and x.startswith("0x"):
        try: return "0x%x" % int(x, 16)
        except ValueError: return x.lower()
    return x

def norm_alloc(a):
    out = {}
    for k, v in (a or {}).items():
        st = {}
        for sk, sv in (v.get("storage") or {}).items():
            if int(sv, 16) != 0:
                st["0x%064x" % int(sk, 16)] = "0x%064x" % int(sv, 16)
        out[k.lower()] = {
            "balance": "0x%x" % int(v.get("balance", "0x0") or "0x0", 16),
            "nonce": "0x%x" % int(v.get("nonce", "0x0") or "0x0", 16),
            "code": (v.get("code") or "0x").lower(),
            "storage": st,
        }
    return out

def run(cmd, cwd=None):
    try:
        return subprocess.run(wrap(cmd), capture_output=True, text=True, timeout=TIMEOUT,
                              cwd=cwd, preexec_fn=_limits)
    except subprocess.TimeoutExpired:
        return None

def copy_in(data, t):
    for f in os.listdir(data):
        dst = os.path.join(t, f)
        shutil.copy(os.path.join(data, f), dst)
        os.chmod(dst, 0o644)

def case_t8n(evm, data, rec, t):
    copy_in(data, t)
    txfile = rec.get("txfile", "txs.rlp")
    cmd = [evm, "t8n", f"--input.alloc={t}/alloc.json", f"--input.env={t}/env.json",
           f"--input.txs={t}/{txfile}", f"--output.basedir={t}",
           "--output.result=out_result.json", "--output.alloc=out_alloc.json",
           f"--state.fork={rec['fork']}", f"--state.chainid={rec['chainid']}",
           f"--state.reward={rec.get('reward','-1')}"]
    p = run(cmd)
    if p is None: return False, "timeout"
    if p.returncode != 0: return False, "exit %d" % p.returncode
    try:
        r = json.load(open(f"{t}/out_result.json"))
    except Exception as e:
        return False, "unreadable result.json (%s)" % e
    exp = rec["expect"]
    for k in ("stateRoot", "logsHash", "txRoot", "receiptsRoot", "logsBloom",
              "withdrawalsRoot", "requestsHash"):
        if k in exp and (r.get(k) or "").lower() != exp[k]:
            return False, "%s=%s want %s" % (k, r.get(k), exp[k])
    for k in ("gasUsed", "currentDifficulty", "currentBaseFee",
              "blobGasUsed", "currentExcessBlobGas"):
        if k in exp and norm_hex(r.get(k)) != exp[k]:
            return False, "%s=%s want %s" % (k, r.get(k), exp[k])
    if "currentDifficultyNull" in exp:
        isnull = r.get("currentDifficulty") is None
        if isnull != exp["currentDifficultyNull"]:
            return False, "currentDifficulty null=%s want %s" % (isnull, exp["currentDifficultyNull"])
    if "rejected" in exp:
        got = sorted(x.get("index") for x in (r.get("rejected") or []))
        if got != exp["rejected"]:
            return False, "rejected=%s want %s" % (got, exp["rejected"])
    if "receipts" in exp:
        got = []
        for rc in (r.get("receipts") or []):
            got.append({
                "root": (rc.get("root") or "").lower(),
                "status": "0x%x" % int(rc.get("status", "0x0"), 16),
                "cumulativeGasUsed": "0x%x" % int(rc.get("cumulativeGasUsed", "0x0"), 16),
                "gasUsed": "0x%x" % int(rc.get("gasUsed", "0x0"), 16),
                "contractAddress": (rc.get("contractAddress") or "").lower(),
                "transactionHash": (rc.get("transactionHash") or "").lower(),
                "nlogs": len(rc.get("logs") or []),
            })
        if got != exp["receipts"]:
            bad = [i for i in range(max(len(got), len(exp["receipts"])))
                   if (got[i] if i < len(got) else None) != (exp["receipts"][i] if i < len(exp["receipts"]) else None)]
            return False, "receipt %s differs" % bad[:2]
    if "alloc" in exp:
        try: a = json.load(open(f"{t}/out_alloc.json"))
        except Exception as e: return False, "unreadable alloc.json (%s)" % e
        if norm_alloc(a) != exp["alloc"]:
            ga, ea = norm_alloc(a), exp["alloc"]
            diff = [k for k in set(ga) | set(ea) if ga.get(k) != ea.get(k)]
            return False, "post alloc differs at %s" % diff[:3]
    return True, ""

def case_t9n(evm, data, rec, t):
    copy_in(data, t)
    cmd = [evm, "t9n", f"--input.txs={t}/txs.rlp",
           f"--state.fork={rec['fork']}", f"--state.chainid={rec['chainid']}"]
    p = run(cmd)
    if p is None: return False, "timeout"
    if p.returncode != 0: return False, "exit %d" % p.returncode
    try:
        got = json.loads(p.stdout)
    except Exception as e:
        return False, "stdout is not JSON (%s)" % e
    exp = rec["expect"]
    if not isinstance(got, list) or len(got) != len(exp):
        return False, "expected %d results, got %s" % (len(exp), type(got).__name__)
    for g, e in zip(got, exp):
        if bool(g.get("error")) != e["error"]:
            return False, "error flag %s want %s" % (bool(g.get("error")), e["error"])
        if "address" in e and not e["error"] and (g.get("address") or "").lower() != e["address"]:
            return False, "address %s want %s" % (g.get("address"), e["address"])
        if "hash" in e and (g.get("hash") or "").lower() != e["hash"]:
            return False, "hash %s want %s" % (g.get("hash"), e["hash"])
        if "intrinsicGas" in e and norm_hex(g.get("intrinsicGas")) != e["intrinsicGas"]:
            return False, "intrinsicGas %s want %s" % (g.get("intrinsicGas"), e["intrinsicGas"])
    return True, ""

def case_b11r(evm, data, rec, t):
    copy_in(data, t)
    cmd = [evm, "b11r", f"--input.header={t}/header.json", f"--input.txs={t}/txs.rlp",
           f"--output.basedir={t}", "--output.block=out_block.json"]
    if rec.get("has_ommers"): cmd.append(f"--input.ommers={t}/ommers.json")
    if rec.get("has_withdrawals"): cmd.append(f"--input.withdrawals={t}/withdrawals.json")
    p = run(cmd)
    if p is None: return False, "timeout"
    if p.returncode != 0: return False, "exit %d" % p.returncode
    try: b = json.load(open(f"{t}/out_block.json"))
    except Exception as e: return False, "unreadable block.json (%s)" % e
    if (b.get("hash") or "").lower() != rec["expect"]["hash"]:
        return False, "hash %s want %s" % (b.get("hash"), rec["expect"]["hash"])
    if (b.get("rlp") or "").lower() != rec["expect"]["rlp"]:
        return False, "block rlp differs"
    return True, ""

def case_chain(evm, data, rec, t):
    alloc = json.load(open(os.path.join(data, "alloc.json")))
    blocks = json.load(open(os.path.join(data, "chain.json")))
    for i, b in enumerate(blocks):
        for fn_, obj in (("alloc.json", alloc), ("env.json", b["env"]), ("txs.rlp", b["txs"])):
            json.dump(obj, open(f"{t}/{fn_}", "w"))
            os.chmod(f"{t}/{fn_}", 0o644)
        cmd = [evm, "t8n", f"--input.alloc={t}/alloc.json", f"--input.env={t}/env.json",
               f"--input.txs={t}/txs.rlp", f"--output.basedir={t}",
               "--output.result=out_result.json", "--output.alloc=out_alloc.json",
               f"--state.fork={rec['fork']}", f"--state.chainid={rec['chainid']}",
               f"--state.reward={rec.get('reward','-1')}"]
        p = run(cmd)
        if p is None: return False, "block %d: timeout" % i
        if p.returncode != 0: return False, "block %d: exit %d" % (i, p.returncode)
        try:
            r = json.load(open(f"{t}/out_result.json"))
            alloc = json.load(open(f"{t}/out_alloc.json"))
        except Exception as e:
            return False, "block %d: unreadable output (%s)" % (i, e)
        for k, want in b["expect"].items():
            got = r.get(k)
            if k in ("gasUsed", "blobGasUsed"):
                if norm_hex(got) != norm_hex(want): return False, "block %d: %s=%s want %s" % (i, k, got, want)
            else:
                if (got or "").lower() != want.lower(): return False, "block %d: %s=%s want %s" % (i, k, got, want)
    return True, ""

def case_cli_exit(evm, data, rec, t):
    copy_in(data, t)
    cmd = [evm, "t8n", f"--input.alloc={t}/alloc.json", f"--input.env={t}/env.json",
           f"--input.txs={t}/{rec['txfile']}", f"--output.basedir={t}",
           "--output.result=out_result.json", "--output.alloc=out_alloc.json",
           f"--state.fork={rec['fork']}", f"--state.chainid={rec['chainid']}",
           f"--state.reward={rec.get('reward','-1')}"]
    p = run(cmd)
    if p is None: return False, "timeout"
    if p.returncode != rec["expect"]["exit"]:
        return False, "exit %d want %d" % (p.returncode, rec["expect"]["exit"])
    return True, ""

def case_cli_body(evm, data, rec, t):
    copy_in(data, t)
    cmd = [evm, "t8n", f"--input.alloc={t}/alloc.json", f"--input.env={t}/env.json",
           f"--input.txs={t}/{rec['txfile']}", f"--output.basedir={t}",
           "--output.result=out_result.json", "--output.alloc=out_alloc.json",
           "--output.body=out_body.rlp",
           f"--state.fork={rec['fork']}", f"--state.chainid={rec['chainid']}",
           f"--state.reward={rec.get('reward','-1')}"]
    p = run(cmd)
    if p is None: return False, "timeout"
    if p.returncode != 0: return False, "exit %d" % p.returncode
    try: body = json.load(open(f"{t}/out_body.rlp"))
    except Exception as e: return False, "unreadable body (%s)" % e
    if (body or "").lower() != rec["expect"]["body"]:
        return False, "body rlp differs"
    return True, ""

def case_cli_stdout(evm, data, rec, t):
    copy_in(data, t)
    cmd = [evm, "t8n", f"--input.alloc={t}/alloc.json", f"--input.env={t}/env.json",
           f"--input.txs={t}/{rec['txfile']}", f"--output.basedir={t}",
           "--output.result=stdout", "--output.alloc=stdout",
           f"--state.fork={rec['fork']}", f"--state.chainid={rec['chainid']}",
           f"--state.reward={rec.get('reward','-1')}"]
    p = run(cmd)
    if p is None: return False, "timeout"
    if p.returncode != 0: return False, "exit %d" % p.returncode
    try: d = json.loads(p.stdout)
    except Exception as e: return False, "stdout is not JSON (%s)" % e
    if "result" not in d or "alloc" not in d:
        return False, "stdout object has keys %s" % sorted(d)[:6]
    got = (d["result"].get("stateRoot") or "").lower()
    if got != rec["expect"]["stateRoot"]:
        return False, "stateRoot %s want %s" % (got, rec["expect"]["stateRoot"])
    return True, ""

HANDLERS = {"t8n": case_t8n, "t9n": case_t9n, "b11r": case_b11r, "chain": case_chain,
            "cli_exit": case_cli_exit, "cli_body": case_cli_body, "cli_stdout": case_cli_stdout}

def run_case(args):
    corpus, evm, rec = args
    data = os.path.join(corpus, "data", rec["id"])
    fn = HANDLERS.get(rec["family"])
    if fn is None: return rec["id"], rec.get("bucket", "?"), False, "unknown family"
    with tempfile.TemporaryDirectory() as t:
        try:
            os.chmod(t, 0o777)
            ok, msg = fn(evm, data, rec, t)
        except Exception as e:
            ok, msg = False, "harness: %r" % (e,)
    return rec["id"], rec.get("bucket", "?"), ok, msg

def main():
    corpus, evm = sys.argv[1], sys.argv[2]
    jobs = int(os.environ.get("JOBS", "7"))
    outjson = None
    if "--json" in sys.argv: outjson = sys.argv[sys.argv.index("--json") + 1]
    recs = [json.loads(l) for l in open(os.path.join(corpus, "cases.jsonl"))]
    results = []

    def dump():
        if not outjson: return
        by = collections.defaultdict(lambda: [0, 0])
        for cid, bucket, ok, msg in results:
            by[bucket][1] += 1
            if ok: by[bucket][0] += 1
        json.dump({"results": [{"id": c, "bucket": b, "ok": o, "msg": m} for c, b, o, m in results],
                   "by_bucket": {k: v for k, v in by.items()},
                   "total": len(recs), "ran": len(results),
                   "passed": sum(1 for _, _, o, _ in results if o)},
                  open(outjson, "w"), indent=1)

    with ProcessPoolExecutor(max_workers=jobs) as ex:
        for i, r in enumerate(ex.map(run_case, [(corpus, evm, rec) for rec in recs], chunksize=1)):
            results.append(r)
            if i % 50 == 49: dump()        # keep a partial result if we are cut short
    dump()
    by = collections.defaultdict(lambda: [0, 0])
    for cid, bucket, ok, msg in results:
        by[bucket][1] += 1
        if ok: by[bucket][0] += 1
    total_ok = sum(v[0] for v in by.values())
    total = sum(v[1] for v in by.values())
    for b in sorted(by): print("  %-16s %4d/%4d" % (b, by[b][0], by[b][1]))
    print("TOTAL %d/%d = %.4f" % (total_ok, total, total_ok / total if total else 0.0))
    fails = [(b, m) for c, b, o, m in results if not o]
    for b, m in fails[:int(os.environ.get("SHOW", "10"))]: print("FAIL", b, m[:160])

if __name__ == "__main__":
    main()
