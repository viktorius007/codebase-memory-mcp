#!/usr/bin/env python3
"""Generate a conservative method-name superset from offline rustdoc format 61."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TOOLCHAIN = Path.home() / '.rustup/toolchains/nightly-2026-08-10-aarch64-apple-darwin'
TYPE_KINDS = {'primitive', 'struct', 'enum', 'union', 'trait', 'type_alias'}
# These erased containers expose a generic element, even without implementing Deref.
ELEMENT_CONTAINERS = {
    'core.cell.Cell', 'core.cell.RefCell', 'core.cell.UnsafeCell',
    'core.cell.once.OnceCell', 'core.cell.lazy.LazyCell', 'std.sync.once_lock.OnceLock',
    'std.sync.lazy_lock.LazyLock', 'std.sync.poison.mutex.Mutex',
    'std.sync.poison.rwlock.RwLock', 'alloc.rc.Weak', 'alloc.sync.Weak',
    'core.ptr.non_null.NonNull', 'core.ops.deref.Deref', 'core.ops.deref.DerefMut',
}
# Rustdoc JSON omits the inherent dyn-trait blocks, not merely their impl bounds.
MISSING_CLASSES = {
    'core.any.Any': 'trait-object inherent impls: dyn Any is/downcast_ref absent from JSON',
    'core.error.Error': 'trait-object inherent impls: dyn Error downcast_ref absent from JSON',
}


class Rustdoc:
    """Resolve crate-local IDs and retain every conditional reachability witness."""
    def __init__(self, directory):
        self.docs = {}
        self.items = {}
        self.hashes = {}
        self.impls = defaultdict(set)
        self.witnesses = defaultdict(lambda: defaultdict(set))
        for crate in ('alloc', 'core', 'std'):
            raw = (directory / (crate + '.json')).read_bytes()
            doc = json.loads(raw)
            if doc['format_version'] != 61:
                raise ValueError(f'{crate}: format_version {doc["format_version"]}, expected 61')
            self.docs[crate] = doc
            self.hashes[crate] = hashlib.sha256(raw).hexdigest()
            for ident, path in doc['paths'].items():
                item = doc['index'].get(ident)
                if item and item['crate_id'] == 0 and path['kind'] in TYPE_KINDS:
                    name = self.path(crate, ident)
                    self.items.setdefault(name, (crate, item))
        for crate, doc in self.docs.items():
            for ident, item in doc['index'].items():
                inner = item['inner']
                impl = inner.get('impl') if isinstance(inner, dict) else None
                if impl and not impl['is_negative']:
                    head = self.head(crate, impl['for'])
                    if head or self.generic_receiver(impl['for']):
                        self.impls[head].add((crate, int(ident)))

    def path(self, crate, ident):
        info = self.docs[crate]['paths'].get(str(ident))
        if not info:
            return None
        path = '.'.join(info['path'])
        if info['kind'] == 'primitive':
            return 'core.' + info['path'][-1]
        return path

    def item(self, crate, ident):
        local = self.docs[crate]['index'].get(str(ident))
        if local:
            return crate, local
        return self.items.get(self.path(crate, ident))

    def head(self, crate, ty):
        if 'borrowed_ref' in ty:
            return self.head(crate, ty['borrowed_ref']['type'])
        if 'resolved_path' in ty:
            return self.path(crate, ty['resolved_path']['id'])
        if 'primitive' in ty:
            return 'core.' + ty['primitive']
        if 'slice' in ty or 'array' in ty:
            return 'core.slice'
        if 'tuple' in ty:
            return 'core.tuple'
        if 'dyn_trait' in ty:
            traits = ty['dyn_trait']['traits']
            return self.path(crate, traits[0]['trait']['id']) if traits else None
        return None

    def generic_receiver(self, ty):
        if 'borrowed_ref' in ty:
            return self.generic_receiver(ty['borrowed_ref']['type'])
        return 'generic' in ty

    def methods(self, crate, ids, issues):
        result = {}
        for ident in ids:
            found = self.item(crate, ident)
            if not found:
                issues.add(f'method item unavailable: {crate}.json#/index/{ident}')
                continue
            owner, item = found
            inner = item['inner']
            fn = inner.get('function') if isinstance(inner, dict) else None
            if not fn or not fn['sig']['inputs'] or fn['sig']['inputs'][0][0] != 'self':
                continue
            source = f'{owner}.json#/index/{item["id"]}'
            result[item['name']] = source
        return result

    def trait_methods(self, crate, trait, issues, visited=None):
        visited = set() if visited is None else visited
        key = (crate, trait['id'])
        if key in visited:
            return {}
        visited.add(key)
        found = self.item(crate, trait['id'])
        if not found:
            issues.add(f'trait definition unavailable: {crate}.json#/index/{trait["id"]}')
            return {}
        owner, item = found
        body = item['inner']['trait']
        result = self.methods(owner, body['items'], issues)
        for bound in body['bounds']:
            if 'trait_bound' in bound:
                result.update(self.trait_methods(owner, bound['trait_bound']['trait'], issues, visited))
        return result

    def collect(self, path, visited=None):
        visited = set() if visited is None else visited
        if path in visited:
            return {}, set(), set()
        if path not in self.items:
            return {}, {f'receiver definition unavailable: {path}'}, set()
        visited.add(path)
        crate, item = self.items[path]
        body = next(iter(item['inner'].values()))
        if 'type_alias' in item['inner']:
            return self.collect(self.head(crate, body['type']), visited)
        rows, issues, deferred = defaultdict(dict), set(), set()
        if path in MISSING_CLASSES:
            issues.add(MISSING_CLASSES[path])
        if path in ELEMENT_CONTAINERS:
            deferred.add(f'generic element: {crate}.json#/index/{item["id"]}')
        if 'trait' in item['inner']:
            self.witnesses[path][f'trait-impl:{path}'].add(f'{crate}.json#/index/{item["id"]}')
            rows[f'trait-impl:{path}'].update(self.trait_methods(crate, {'id': item['id']}, issues))
        impls = set(self.impls[path]) | self.impls[None]
        impls.update((crate, ident) for ident in body.get('impls', []))
        for owner, ident in sorted(impls):
            impl = self.docs[owner]['index'][str(ident)]['inner']['impl']
            if impl['is_negative']:
                continue
            trait = impl['trait']
            tr_path = self.path(owner, trait['id']) if trait else None
            reach = ('blanket:' if impl['blanket_impl'] is not None or
                     self.generic_receiver(impl['for']) else 'trait-impl:') + str(tr_path) if trait else 'inherent'
            if 'borrowed_ref' in impl['for']:
                reach = 'reference-impl:' + reach
            methods = self.methods(owner, impl['items'], issues)
            if trait:
                methods.update(self.trait_methods(owner, trait, issues))
            rows[reach].update(methods)
            self.witnesses[path][reach].add(f'{owner}.json#/index/{ident}')
            if tr_path == 'core.ops.deref.Deref' and (owner, ident) not in self.impls[None]:
                self.collect_deref(path, owner, impl, visited, rows, issues, deferred)
        return dict(rows), issues, deferred

    def collect_deref(self, path, crate, impl, visited, rows, issues, deferred):
        for ident in impl['items']:
            item = self.docs[crate]['index'][str(ident)]
            assoc = item['inner'].get('assoc_type')
            if item['name'] != 'Target' or not assoc or not assoc['type']:
                continue
            ty = assoc['type']
            target = self.head(crate, ty)
            if target is None:
                deferred.add(f'generic Deref::Target: {crate}.json#/index/{ident}')
                continue
            self.witnesses[path][f'deref:{target}'].add(f'{crate}.json#/index/{ident}')
            nested, missing, generic = self.collect(target, visited.copy())
            issues.update(missing)
            deferred.update(generic)
            for methods in nested.values():
                rows[f'deref:{target}'].update(methods)


def inventory(doc, receivers):
    rows, stamps, audit = [], [], {}
    priority = {'inherent': 0, 'trait-impl': 1, 'blanket': 2, 'reference-impl': 3, 'deref': 4}
    for receiver, path in sorted(receivers.items()):
        if path is None:
            classes, issues, deferred = {}, {'erased associated receiver: no concrete JSON definition'}, set()
        else:
            classes, issues, deferred = doc.collect(path)
        chosen = {}
        for reach in sorted(classes, key=lambda r: (priority[r.split(':')[0]], r)):
            for method, source in sorted(classes[reach].items()):
                chosen.setdefault(method, (reach, source))
        status = 'incomplete' if issues else 'complete_except_deref' if deferred else 'complete'
        reason = '; '.join(sorted(issues or deferred)) if issues or deferred else 'all JSON receiver candidate classes enumerated as a superset'
        stamps.append((receiver, status, reason))
        rows.extend((receiver, method, reach, source) for method, (reach, source) in sorted(chosen.items()))
        definition = path
        while definition in doc.items and 'type_alias' in doc.items[definition][1]['inner']:
            owner, alias = doc.items[definition]
            definition = doc.head(owner, alias['inner']['type_alias']['type'])
        audit[receiver] = {'path': path, 'resolved_path': definition, 'status': status, 'reason': reason,
                           'rows': len(chosen), 'classes': classes,
                           'impl_witnesses': {reach: sorted(ids) for reach, ids in doc.witnesses[definition].items()},
                           'retained': dict(Counter(reach for reach, _ in chosen.values()))}
    return rows, stamps, audit


def render(doc, rows, stamps, version):
    method_lines = ['CBM_STD_METHOD(' + ', '.join(map(json.dumps, (*row, version))) + ')' for row in rows]
    stamp_lines = ['CBM_STD_STAMP(' + ', '.join(map(json.dumps, stamp)) + ')' for stamp in stamps]
    counts = Counter(stamp[1] for stamp in stamps)
    provenance = '\n'.join(f' * {crate}.json sha256 {digest}' for crate, digest in sorted(doc.hashes.items()))
    prefix = f'''/* Generated by tools/std-inventory/generate.py; captured 2026-10-14.
 * Toolchain: {version}; rustdoc JSON format 61.
{provenance}
 * {len(rows)} method rows; {counts['complete']} complete, {counts['complete_except_deref']} complete_except_deref, {counts['incomplete']} incomplete stamps.
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
    return prefix + '\n'.join(method_lines) + '\n#endif\n#ifdef CBM_STD_STAMP\n' + '\n'.join(stamp_lines) + '\n#endif\n#endif\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=DEFAULT_TOOLCHAIN)
    parser.add_argument('--output', type=Path, default=ROOT / 'internal/cbm/lsp/rust_std_inventory.h')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--audit-out', type=Path, help='write per-class witnesses and counts as JSON')
    args = parser.parse_args()
    version = subprocess.check_output([str(args.toolchain / 'bin/rustc'), '--version'], text=True).strip()
    doc = Rustdoc(args.toolchain / 'share/doc/rust/json')
    receivers = json.loads((Path(__file__).with_name('receivers.json')).read_text())
    rows, stamps, audit = inventory(doc, receivers)
    output = render(doc, rows, stamps, version).encode()
    if args.audit_out:
        args.audit_out.write_text(json.dumps(audit, sort_keys=True, indent=2) + '\n')
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
