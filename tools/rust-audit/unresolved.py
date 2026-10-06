#!/usr/bin/env python3
"""Check Rust oracle sites against published links and unresolved occurrences.

Usage: python3 tools/rust-audit/unresolved.py SCORE_DIR
No compiler or language server is invoked. At the harness's pair-scoring level,
publication is a caller/target pair; unresolved occurrence keys use file/line/leaf
because the frozen truth does not include byte offsets. Uncovered rows are written
verbatim to unresolved-neither.tsv. In-corpus gaps fail the check; an external
oracle target does not prove the resolver can identify it as external.
"""
import bisect
import collections
import json
import os
import pathlib
import re
import sqlite3
import sys


def leaf(name):
    return re.split(r"::|\.", name)[-1].strip().removeprefix("r#")


def main():
    directory = pathlib.Path(sys.argv[1])
    connection = sqlite3.connect(f"file:{directory / 'cache/score.db'}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    try:
        sites = list(connection.execute("SELECT * FROM unresolved_sites"))
    except sqlite3.OperationalError as error:
        print(f"FAIL: unresolved-site recording unavailable: {error}")
        return 1
    invalid = [dict(row) for row in sites if not row['file_path'] or not row['caller'] or not row['name'] or
               not row['reason'] or row['line'] < 1 or
               row['site_end_byte'] <= row['site_start_byte']]
    print(f"records: {len(sites)}; invalid required fields: {len(invalid)}")
    invalid_path = directory / 'unresolved-invalid.json'
    if invalid:
        invalid_path.write_text(json.dumps(invalid, indent=2))
    else:
        invalid_path.unlink(missing_ok=True)
    by_site = collections.defaultdict(list)
    for row in sites:
        for spelling in set(part.strip().removeprefix('r#') for part in row['name'].split('::')):
            by_site[(row['file_path'], row['line'], spelling)].append(row)
        by_site[(row['file_path'], row['line'], leaf(row['name']))].append(row)
    by_line = collections.defaultdict(list)
    for row in sites:
        by_line[row['file_path'], row['line']].append(row)
    nodes = {row['id']: row for row in connection.execute('SELECT * FROM nodes')}
    by_file = collections.defaultdict(list)
    by_name = collections.defaultdict(list)
    for row in nodes.values():
        by_file[row['file_path']].append(row)
        by_name[row['file_path'], leaf(row['name'])].append(row)
    published_lines = set()
    published_spellings = collections.defaultdict(list)
    for row in connection.execute("SELECT e.source_id,e.target_id,CAST(e.properties AS BLOB) AS properties FROM edges e WHERE e.type='CALLS'"):
        source = nodes.get(row['source_id'])
        properties = json.loads((row['properties'] or b'{}').decode('utf-8', errors='replace'))
        line = properties.get('line', 0)
        callee = str(properties.get('callee', '')).replace('\\n', '\n')
        if source:
            spelling = leaf(callee)
            if re.fullmatch(r'[A-Za-z_]\w*', spelling):
                published_spellings[source['file_path']].append((source, spelling))
            target = nodes.get(row['target_id'])
            if target and target['label'] in {'Function', 'Method'}:
                published_spellings[source['file_path']].append((source, leaf(target['name'])))
            if line:
                # CALLS coordinates begin at the receiver, before its leaf.
                published_lines.add((source['file_path'], line + callee.count('\n')))
    corpus = pathlib.Path(os.environ.get('CBM_RUST_CORPUS',
                         str(pathlib.Path.home() / '.cache/cbm-rust-bench/codex/codex-rs')))
    aliases = {}
    source_lines = {}
    occurrence_lines = collections.defaultdict(list)
    by_source = collections.defaultdict(list)
    for row in sites:
        if row['kind'] == 'call':
            by_source[row['file_path']].append(row)
    for path, file_sites in by_source.items():
        data = (corpus / path).read_bytes()
        breaks = [i for i, byte in enumerate(data) if byte == 10]
        for row in file_sites:
            first = bisect.bisect_left(breaks, row['site_start_byte']) + 1
            last = bisect.bisect_left(breaks, row['site_end_byte'] - 1) + 1
            for site_line in range(first, last + 1):
                occurrence_lines[path, site_line].append(row)
    reexports = collections.defaultdict(set)
    for source_path in corpus.rglob('*.rs'):
        text = source_path.read_text(errors='replace')
        for module, original, alias in re.findall(
                r'\bpub(?:\([^)]*\))?\s+use\s+(?:[\w]+::)*([\w]+)::([\w]+)\s+as\s+([\w]+)\s*;', text):
            reexports[module, original].add(alias)

    def written_aliases(path, name):
        if path not in aliases:
            source = corpus / path
            sources = {source}
            # Rust file modules can inherit names from an enclosing module.
            # Keep these as spelling candidates, never as resolution proof.
            directory_path = source.parent
            while directory_path.is_relative_to(corpus):
                sources.update(p for p in (directory_path / 'mod.rs',
                                           directory_path.with_suffix('.rs')) if p.is_file())
                if (directory_path / 'Cargo.toml').exists():
                    break
                directory_path = directory_path.parent
            mapping = collections.defaultdict(set)
            for candidate in sources:
                for declaration in re.findall(r'\buse\s+[^;]+;', candidate.read_text(errors='replace')):
                    for original, alias in re.findall(r'\b(\w+)\s+as\s+(\w+)\b', declaration):
                        mapping[original].add(alias)
            aliases[path] = mapping
        return aliases[path].get(leaf(name), ())
    operators = {'index': '[', 'index_mut': '[', 'mul': '*', 'div': '/',
                 'rem': '%', 'add': '+', 'sub': '-', 'neg': '-', 'not': '!',
                 'bitand': '&', 'bitor': '|', 'bitxor': '^', 'shl': '<<', 'shr': '>>',
                 'eq': '==', 'ne': '!=', 'partial_cmp': '<', 'cmp': '<', 'deref': '*'}
    other_publications = []
    publication_rows = collections.defaultdict(list)
    edges = collections.defaultdict(set)
    for row in connection.execute("SELECT source_id,target_id,type,CAST(properties AS BLOB) AS properties FROM edges "
                                   "WHERE type IN ('CALLS','USAGE','CALL_REFERENCE')"):
        edges[row['type']].add((row['source_id'], row['target_id']))
        properties = json.loads((row['properties'] or b'{}').decode('utf-8', errors='replace'))
        strategy = properties.get('strategy', properties.get('rule', 'unreported'))
        publication_rows[nodes[row['source_id']]['file_path'], leaf(nodes[row['target_id']]['name'])].append((nodes[row['source_id']], nodes[row['target_id']], row['type'], strategy))

    def find(path, line, name, labels=None):
        candidates = [row for row in by_name[path, leaf(name)]
                      if row['start_line'] <= int(line) <= row['end_line'] and
                      leaf(row['name']) == leaf(name) and
                      (labels is None or row['label'] in labels)]
        return min(candidates, key=lambda row: row['end_line'] - row['start_line'], default=None)

    counts = collections.Counter()
    neither = []
    required_gaps = collections.Counter()

    def classify(family, path, line, name, caller, target, types, original, external=False):
        published = caller is not None and target is not None and any(
            (caller['id'], target['id']) in edges[kind] for kind in types)
        if name == '-':
            if (path, int(line)) in published_lines:
                published = True
            if path not in source_lines:
                source_lines[path] = (corpus / path).read_text(errors='replace').splitlines()
            written = source_lines[path][int(line) - 1]
            # CALLS is pair-deduplicated: the stored line is the last invocation.
            # For oracle rows lacking a target, use an actual written call and
            # its enclosing published pair at the harness's file/line granularity.
            published |= any(source['start_line'] <= int(line) <= source['end_line'] and
                             re.search(r'\b' + re.escape(spelling) + r'\s*(?:\(|::<)', written)
                             for source, spelling in published_spellings[path])
        matching = list(by_site.get((path, int(line), leaf(name)), ()))
        for alias in written_aliases(path, name):
            matching.extend(by_site.get((path, int(line), alias), ()))
        if target is not None:
            for alias in reexports[pathlib.Path(target['file_path']).stem, leaf(name)]:
                matching.extend(by_site.get((path, int(line), alias), ()))
        if family == 'call' and leaf(name) in operators:
            matching.extend(row for row in by_site.get((path, int(line), operators[leaf(name)]), ())
                            if row['reason'] == 'macro_token_not_resolved')
        if family == 'type_or_field' and target is not None and caller is not None and \
                caller['qualified_name'].startswith(target['qualified_name'] + '.'):
            matching.extend(by_site.get((path, int(line), 'Self'), ()))
        if name == '-':
            matching = occurrence_lines.get((path, int(line)), ())
        def enclosing_item(row):
            return any(node['label'] in {'Module', 'File', 'Function', 'Method'} and
                       node['qualified_name'] in {row['caller'], row['caller'] + '#module'} and
                       (node['label'] == 'File' or node['start_line'] <= int(line) <= node['end_line'])
                       for node in by_file[path])
        matching = [row for row in matching
                    if (family != 'call' or row['kind'] in {'call', 'reference'}) and
                    (caller is None or row['caller'] == caller['qualified_name'] or
                     ((caller['label'] in {'Module', 'File'} or
                       row['reason'] in {'macro_invocation_not_proven', 'macro_token_not_resolved'} or
                       not caller['start_line'] <= int(line) <= caller['end_line']) and
                      enclosing_item(row)))]
        if published:
            verdict = 'published'
        elif matching:
            verdict = 'recorded'
        else:
            candidates = [entry for entry in publication_rows[path, leaf(name)]
                          if entry[0]['file_path'] == path and entry[2] in types and
                          leaf(entry[1]['name']) == leaf(name) and
                          (caller is None or entry[0]['qualified_name'] == caller['qualified_name'])]
            if candidates:
                # The omission check concerns sites, while join.py scores exact
                # oracle pairs. Preserve this distinction and expose the actual
                # successful resolver result for review; do not invent a stop.
                counts[family, 'published_other_target'] += 1
                source, destination, edge_kind, strategy = candidates[0]
                other_publications.append((family, path, line, source['qualified_name'],
                                           destination['qualified_name'], edge_kind, strategy, original))
                return
            verdict = 'neither'
            reason = 'caller_not_mapped'  if caller is None else 'target_not_mapped' \
                if target is None else 'no_published_pair_or_occurrence_record'
            neither.append(f'{family}\t{reason}\t{original}')
            if not external:
                required_gaps[family, reason] += 1
        counts[family, verdict] += 1

    for raw in (directory / 'truth.tsv').read_text(errors='replace').splitlines():
        path, line, kind, cf, cl, cn, symbol, df, dl, _shape, _wr = raw.split('\t')
        if kind == 'ref_not_call':
            continue
        callee = symbol.rsplit('().', 1)[0]
        callee = callee[max(callee.rfind(']'), callee.rfind('/'), callee.rfind('#'), callee.rfind(' ')) + 1:]
        # Calls without an oracle target are still accounted for, separately.
        caller = None if cf == '-' else find(cf, cl, cn, {'Function', 'Method'})
        target = None if df in {'ext', '-'} else find(df, dl, callee, {'Function', 'Method'})
        types = ['CALLS', 'CALL_REFERENCE', 'USAGE'] if kind.startswith('ref_') else ['CALLS']
        classify('call', path, line, callee, caller, target, types, raw, df == 'ext')
    for raw in (directory / 'refs.tsv').read_text(errors='replace').splitlines():
        parts = raw.split('\t')
        if parts[0] != 'R':
            continue
        path, line, context, el, en, df, dl, kind, name = parts[1:]
        if context == 'use' and el == '-':
            continue
        target = find(df, dl, name)
        if kind != 'type' and not (target is not None and target['label'] == 'Field'):
            continue
        if el != '-':
            caller = find(path, el, en, {'Function', 'Method'})
        else:
            candidates = [row for row in by_file[path] if row['label'] in {'File', 'Module'} and
                          (row['label'] == 'File' or
                           row['start_line'] <= int(line) <= row['end_line'])]
            # families.py credits any enclosing Module/File publication.
            caller = next((row for row in candidates if target is not None and any(
                (row['id'], target['id']) in edges[k] for k in ('USAGE', 'CALLS', 'CALL_REFERENCE'))), None)
            if caller is None:
                caller = min(candidates, key=lambda row: (row['label'] == 'File', row['end_line'] - row['start_line']), default=None)
        classify('type_or_field', path, line, name, caller, target,
                 ['USAGE', 'CALLS', 'CALL_REFERENCE'], raw, df == 'ext')
    (directory / 'unresolved-other-publication.tsv').write_text(
        '\n'.join('\t'.join(map(str, row)) for row in other_publications) + '\n')
    (directory / 'unresolved-neither.tsv').write_text('\n'.join(neither) + '\n')
    for family in ('call', 'type_or_field'):
        totals = {verdict: counts[family, verdict] for verdict in ('published', 'published_other_target', 'recorded', 'neither')}
        print(f'{family}: {json.dumps(totals)}; accounted: {sum(totals.values())}')
    reasons = collections.Counter(row['reason'] for row in sites)
    print('stop reasons: ' + json.dumps(dict(sorted(reasons.items()))))
    for (family, reason), count in sorted(required_gaps.items()):
        print(f'GAP: {family} {reason}: {count}')
    failed = bool(invalid or required_gaps)
    print(('FAIL: uncovered in-corpus sites; see unresolved-neither.tsv' if failed else
           'PASS: every in-corpus site is published or recorded'))
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
