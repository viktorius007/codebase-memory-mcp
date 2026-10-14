#!/usr/bin/env python3
"""Check gap coverage and measure inventory membership without changing the graph."""
import argparse
import json
from pathlib import Path
import re
import sqlite3

ROOT = Path(__file__).resolve().parents[2]


def read_rows(header, macro):
    rows = []
    for line in header.read_text().splitlines():
        if line.startswith(macro + '('):
            rows.append(json.loads('[' + line[len(macro) + 1:-1] + ']'))
    return rows


def quote(value):
    return "'" + value.replace("'", "''") + "'"


def measurement_sql(methods, stamps, aliases):
    tables = [('inventory', '(receiver,method)', [(r[0], r[1]) for r in methods]),
              ('stamps', '(receiver,status)', [(r[0], r[1]) for r in stamps]),
              ('aliases', '(qn,receiver)', sorted(aliases.items()))]
    lines = []
    for table, columns, rows in tables:
        lines.append(f'CREATE TEMP TABLE {table} {columns};')
        values = ',\n'.join('(' + ','.join(map(quote, row)) + ')' for row in rows)
        lines.append(f'INSERT INTO {table} VALUES\n{values};')
    lines.append('''WITH sites AS (
  SELECT substr(name,1,length(name)-length(j.value)-1) AS qn,
         j.value AS method
  FROM unresolved_sites,
       json_each('["' || replace(name,'.','","') || '"]') AS j
  WHERE kind='call' AND reason='unrecognised_std_method'
    AND j.key=json_array_length('["' || replace(name,'.','","') || '"]')-1
), classified AS (
 SELECT CASE
   WHEN inventory.method IS NOT NULL THEN 'listed'
   WHEN stamps.status='complete' THEN 'unlisted-complete'
   WHEN stamps.status='incomplete' THEN 'unlisted-incomplete'
   ELSE 'not-covered' END AS class
 FROM sites LEFT JOIN aliases USING(qn)
 LEFT JOIN stamps USING(receiver)
 LEFT JOIN inventory USING(receiver,method)
)
SELECT labels.class,count(classified.class)
FROM (SELECT 'listed' AS class UNION ALL SELECT 'unlisted-complete'
      UNION ALL SELECT 'unlisted-incomplete' UNION ALL SELECT 'not-covered') AS labels
LEFT JOIN classified USING(class)
GROUP BY labels.class ORDER BY labels.class;''')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('db', type=Path)
    parser.add_argument('--sql-out', type=Path, required=True)
    args = parser.parse_args()
    header = ROOT / 'internal/cbm/lsp/rust_std_inventory.h'
    methods = read_rows(header, 'CBM_STD_METHOD')
    stamps = read_rows(header, 'CBM_STD_STAMP')
    aliases = {r[0]: r[0] for r in stamps}
    family = (ROOT / 'internal/cbm/lsp/rust_lsp.c').read_text()
    for ident, qn in re.findall(r'\{(RUST_STD_\w+),\s*"([\w.]+)"\}', family):
        if ident in aliases:
            aliases[qn] = ident
    sql = measurement_sql(methods, stamps, aliases)
    args.sql_out.write_text(sql)
    with sqlite3.connect(args.db.resolve().as_uri() + '?mode=ro', uri=True) as db:
        db.executescript(sql)
        # executescript discards SELECT results; execute the final SELECT separately.
        query = sql[sql.index('WITH sites AS'):]
        counts = dict(db.execute(query))
        gaps = db.execute((ROOT / 'tools/rust-audit/std-method-gaps.sql').read_text()).fetchall()
    covered = {r[0] for r in stamps}
    missing = sorted({r[0] for r in gaps if aliases.get(r[0]) not in covered})
    status = 'FAIL' if missing or counts.get('not-covered', 0) else 'PASS'
    print(f'{status}: top-20 receiver coverage; missing={missing}; recovery={counts}')
    print('receiver_type\tmethod\tsites')
    for row in gaps:
        print('\t'.join(map(str, row)))
    return int(status == 'FAIL')


if __name__ == '__main__':
    raise SystemExit(main())
