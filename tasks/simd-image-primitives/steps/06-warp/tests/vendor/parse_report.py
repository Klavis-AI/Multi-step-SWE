#!/usr/bin/env python3
"""Parse a Simd `Test -m=a -ot=<file>` performance report.

The report is a `Test::Table` render.  Only the LAST header of a column group
carries a `|` separator (`Table::SetHeader(..., separator, ...)`,
TestPerformance.cpp:337-359), so a rendered line has fewer `|`-separated cells
than it has columns: the statistic columns API, Base, Sse41, Avx2 render as one
cell, then two ratio groups.  Split on the `|` positions of the *header* line,
then split each group on whitespace; the flat column-name order is
API, Base, Sse41, Avx2, Avx5b, Vnni, Amx, Neon, Sve2, Hvx followed by the
derived ratio columns, and each enabled column carries exactly one value.

`API` is the dispatched entry point (`Simd<Fn>`), `Base` the scalar reference
(`Simd::Base::<Fn>`); both are timed by the same harness, in the same process,
on the same input.

Usage: parse_report.py <report.txt> [<row-prefix> ...]
       -> "<row>\t<api_ms>\t<base_ms>\t<speedup>" per matched row
"""
import sys


def _split_groups(line):
    """Cells between the '|' separators of a rendered table line."""
    parts = line.rstrip('\n').split('|')
    # drop the empty strings produced by the leading/trailing '|'
    if parts and parts[0].strip() == '':
        parts = parts[1:]
    if parts and parts[-1].strip() == '':
        parts = parts[:-1]
    return parts


def parse(path):
    header_names = None
    groups = 0
    rows = []
    for line in open(path, errors='replace'):
        if not line.startswith('|'):
            continue
        cells = _split_groups(line)
        if header_names is None:
            header_names = []
            for c in cells:
                header_names.extend(c.split())
            groups = len(cells)
            continue
        if set(line.strip()) <= set('|- '):
            continue
        if len(cells) != groups:
            continue
        values = []
        for c in cells:
            values.extend(c.split())
        row = {}
        for name, val in zip(header_names, values):
            row.setdefault(name, val)
        row['_row'] = cells[0].strip()
        rows.append(row)
    return header_names, rows


def main():
    path = sys.argv[1]
    prefixes = sys.argv[2:]
    names, rows = parse(path)
    if names is None:
        sys.exit('no table found in %s' % path)
    for r in rows:
        name = r['_row']
        if name.startswith('Common,'):
            continue
        if prefixes and not any(name.startswith(p) for p in prefixes):
            continue
        api, base = r.get('API', ''), r.get('Base', '')
        try:
            api_f, base_f = float(api), float(base)
        except ValueError:
            print('%s\t%s\t%s\tNaN' % (name, api, base))
            continue
        sp = (base_f / api_f) if api_f > 0 else float('nan')
        print('%s\t%.6f\t%.6f\t%.4f' % (name, api_f, base_f, sp))


if __name__ == '__main__':
    main()
