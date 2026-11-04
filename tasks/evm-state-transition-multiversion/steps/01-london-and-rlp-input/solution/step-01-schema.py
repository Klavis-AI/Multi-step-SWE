#!/usr/bin/env python3
"""Step-01 Oracle: bring v1.10.7's t8n result document up to the documented
contract.  Both edits are backports of what upstream itself did later --
go-ethereum v1.10.23's ExecutionResult carries `json:"receiptsRoot"` and a
`GasUsed math.HexOrDecimal64 json:"gasUsed"` field -- so the Oracle is a
faithful forward port, not an invention of this task.
"""
import re, sys, pathlib

p = pathlib.Path(sys.argv[1]) / "cmd/evm/internal/t8ntool/execution.go"
s = p.read_text()

before = s
s = s.replace('ReceiptRoot common.Hash    `json:"receiptRoot"`',
              'ReceiptRoot common.Hash    `json:"receiptsRoot"`')
assert s != before, "receiptRoot json tag not found"

before = s
s = s.replace('\tRejected    []*rejectedTx  `json:"rejected,omitempty"`\n}',
              '\tRejected    []*rejectedTx  `json:"rejected,omitempty"`\n'
              '\tGasUsed     math.HexOrDecimal64 `json:"gasUsed"`\n}')
assert s != before, "Rejected field not found"

before = s
s = s.replace('\t\tRejected:    rejectedTxs,\n\t}',
              '\t\tRejected:    rejectedTxs,\n'
              '\t\tGasUsed:     (math.HexOrDecimal64)(gasUsed),\n\t}')
assert s != before, "ExecutionResult construction not found"

p.write_text(s)
print("patched", p)
