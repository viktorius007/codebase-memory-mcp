#!/usr/bin/env python3
"""Offline rustdoc format-61 method-name inventory; never certifies absence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TOOLCHAIN = Path.home() / '.rustup/toolchains/nightly-2026-08-10-aarch64-apple-darwin'
REASON = ('open world: erased generic arguments; blanket bounds not evaluated; '
          'reference/unsizing candidates and imported traits not exhaustive; '
          'edition, feature and target coverage not certified')


class Rustdoc:
    """Resolve crate-local IDs through rustdoc paths, never by a leaf name."""
    def __init__(self, directory):
        self.docs = {}
        self.items = {}
        self.hashes = {}
        for crate in ('alloc', 'core', 'std'):
            raw = (directory / (crate + '.json')).read_bytes()
            doc = json.loads(raw)
            if doc['format_version'] != 61:
                raise ValueError(f'{crate}: format_version {doc["format_version"]}, expected 61')
            self.docs[crate] = doc
            self.hashes[crate] = hashlib.sha256(raw).hexdigest()
            for ident, path in doc['paths'].items():
                item = doc['index'].get(ident)
                if item and item['crate_id'] == 0:
                    self.items['.'.join(path['path'])] = (crate, item)

    def path(self, crate, ident):
        info = self.docs[crate]['paths'].get(str(ident))
        return '.'.join(info['path']) if info else None

    def item(self, crate, ident):
        local = self.docs[crate]['index'].get(str(ident))
        if local and local['crate_id'] == 0:
            return crate, local
        return self.items.get(self.path(crate, ident))

    def head(self, crate, ty):
        if 'borrowed_ref' in ty:
            return self.head(crate, ty['borrowed_ref']['type'])
        if 'resolved_path' in ty:
            return self.path(crate, ty['resolved_path']['id'])
        if 'primitive' in ty:
            return 'core.' + ty['primitive']
        if 'slice' in ty:
            return 'core.slice'
        return None

    def methods(self, crate, ids):
        result = {}
        for ident in ids:
            found = self.item(crate, ident)
            if not found:
                continue
            owner, item = found
            fn = item['inner'].get('function')
            if not fn or not fn['sig']['inputs'] or fn['sig']['inputs'][0][0] != 'self':
                continue
            source = f'{owner}.json#/index/{item["id"]}'
            result[item['name']] = source
        return result

    def trait_methods(self, crate, trait, visited=None):
        visited = set() if visited is None else visited
        path = self.path(crate, trait['id'])
        if path in visited:
            return {}
        visited.add(path)
        found = self.item(crate, trait['id'])
        if not found:
            return {}
        owner, item = found
        body = item['inner'].get('trait')
        if not body:
            return {}
        result = self.methods(owner, body['items'])
        for bound in body['bounds']:
            if 'trait_bound' in bound:
                result.update(self.trait_methods(owner, bound['trait_bound']['trait'], visited))
        return result

    def collect(self, path, visited=None):
        visited = set() if visited is None else visited
        if path in visited or path not in self.items:
            return {}
        visited.add(path)
        crate, item = self.items[path]
        body = next(iter(item['inner'].values()))
        if 'type_alias' in item['inner']:
            return self.collect(self.head(crate, body['type']), visited)
        rows = {}
        if 'trait' in item['inner']:
            for method, source in self.trait_methods(crate, {'id': item['id']}).items():
                rows.setdefault(method, (f'trait:{path}', source))
        impls = set(body.get('impls', []))
        # rustdoc's attached list does not necessarily contain reference impls.
        for ident, candidate in self.docs[crate]['index'].items():
            inner = candidate['inner']
            impl = inner.get('impl') if isinstance(inner, dict) else None
            if impl and 'borrowed_ref' in impl['for'] and self.head(crate, impl['for']) == path:
                impls.add(int(ident))
        for ident in sorted(impls):
            impl = self.docs[crate]['index'][str(ident)]['inner']['impl']
            if impl['is_negative']:
                continue
            trait = impl['trait']
            tr_path = self.path(crate, trait['id']) if trait else None
            reach = ('blanket:' if impl['blanket_impl'] else 'trait:') + str(tr_path) if trait else 'inherent'
            if 'borrowed_ref' in impl['for']:
                reach = 'autoref:' + reach
            methods = self.methods(crate, impl['items'])
            if trait:
                methods.update(self.trait_methods(crate, trait))
            for method, source in methods.items():
                rows.setdefault(method, (reach, source))
            if trait and trait['path'].split('::')[-1] == 'Deref':
                self.collect_deref(crate, impl, visited, rows)
        return rows

    def collect_deref(self, crate, impl, visited, rows):
        for ident in impl['items']:
            item = self.docs[crate]['index'][str(ident)]
            assoc = item['inner'].get('assoc_type')
            if item['name'] != 'Target' or not assoc or not assoc['type']:
                continue
            target = self.head(crate, assoc['type'])
            for method, (_, source) in self.collect(target, visited.copy()).items():
                rows.setdefault(method, (f'deref:{target}', source))


def render(doc, receivers, version):
    rows = []
    stamps = []
    for receiver, path in sorted(receivers.items()):
        if path is None:
            stamps.append('CBM_STD_STAMP(' + ', '.join(map(json.dumps, (receiver, 'incomplete', 'not traversed: no audited receiver-to-rustdoc definition mapping; no absence verdict'))) + ')')
            continue
        methods = doc.collect(path)
        if not methods:
            raise ValueError(f'{receiver}: no callable methods found at {path}')
        for method, (reach, source) in sorted(methods.items()):
            rows.append('CBM_STD_METHOD(' + ', '.join(map(json.dumps, (receiver, method, reach, source, version))) + ')')
        stamps.append('CBM_STD_STAMP(' + ', '.join(map(json.dumps, (receiver, 'incomplete', REASON))) + ')')
    provenance = '\n'.join(f' * {crate}.json sha256 {digest}' for crate, digest in sorted(doc.hashes.items()))
    prefix = f'''/* Generated by tools/std-inventory/generate.py; captured 2026-10-14.
 * Toolchain: {version}; rustdoc JSON format 61.
{provenance}
 * {len(rows)} method rows; 0 complete, {len(stamps)} incomplete stamps.
 * Reachability is a conditional name union, not proof of bound satisfaction.
 * See tools/std-inventory/README.md before interpreting absence. */
#if !defined(CBM_STD_METHOD) && !defined(CBM_STD_STAMP)
#ifndef CBM_RUST_STD_INVENTORY_DATA
#define CBM_RUST_STD_INVENTORY_DATA
#define CBM_STD_METHOD(receiver, method, reach, source, version) {{receiver, method, reach, source, version}},
#define CBM_STD_STAMP(receiver, status, reason)
const char *const cbm_rust_std_inventory[][5] = {{
#include "rust_std_inventory.h"
}};
#undef CBM_STD_METHOD
#undef CBM_STD_STAMP
#define CBM_STD_METHOD(receiver, method, reach, source, version)
#define CBM_STD_STAMP(receiver, status, reason) {{receiver, status, reason}},
const char *const cbm_rust_std_inventory_stamps[][3] = {{
#include "rust_std_inventory.h"
}};
#undef CBM_STD_METHOD
#undef CBM_STD_STAMP
#endif
#else
#ifdef CBM_STD_METHOD
'''
    return prefix + '\n'.join(rows) + '\n#endif\n#ifdef CBM_STD_STAMP\n' + '\n'.join(stamps) + '\n#endif\n#endif\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=DEFAULT_TOOLCHAIN)
    parser.add_argument('--output', type=Path, default=ROOT / 'internal/cbm/lsp/rust_std_inventory.h')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    version = subprocess.check_output([str(args.toolchain / 'bin/rustc'), '--version'], text=True).strip()
    doc = Rustdoc(args.toolchain / 'share/doc/rust/json')
    receivers = json.loads((Path(__file__).with_name('receivers.json')).read_text())
    output = render(doc, receivers, version).encode()
    if args.check:
        if args.output.read_bytes() != output:
            print(f'FAIL: {args.output} differs from generated inventory', file=sys.stderr)
            return 1
        print('PASS: committed inventory equals generated output')
    else:
        args.output.write_bytes(output)
        print(f'PASS: generated {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
