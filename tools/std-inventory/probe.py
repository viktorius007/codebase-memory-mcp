#!/usr/bin/env python3
"""Prove std inventory completeness with stable rustc method-call probes."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import subprocess
import sys
import time

from probe_specs import ALIAS, POINTEE_SPECS, S


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GENERATOR_TOOLCHAIN = (
    Path.home() / '.rustup/toolchains/nightly-2026-08-10-aarch64-apple-darwin'
)
EXCLUDED_ERRORS = {'[E0599]', '[E0624]', '[E0658]'}
KEYWORDS = set('''
as break const continue crate else enum extern false fn for if impl in let loop
match mod move mut pub ref return self Self static struct super trait true type
unsafe use where while async await dyn abstract become box do final macro override
priv typeof unsized virtual yield try gen
'''.split())
PRELUDE = '''
use std::any::Any; use std::error::Error; use std::future::Future;
use std::mem::MaybeUninit; use std::path::{Path, PathBuf};
use std::ffi::{OsStr, OsString, CString, CStr}; use std::pin::Pin;
use std::rc::Rc; use std::sync::Arc; use std::cell::*; use std::sync::*;
use std::collections::*; use std::borrow::Cow; use std::num::*;
use std::ops::*; use std::sync::atomic::*; use std::fs::*;
use std::io::{BufReader, BufWriter, Lines, StdoutLock}; use std::net::*;
use std::process::*; use std::os::fd::OwnedFd; use std::os::unix::net::*;
use std::time::*; use std::thread; use std::alloc::Layout;
use std::any::TypeId; use std::panic::Location; use std::marker::PhantomData;
use std::ptr::NonNull; use std::mem::ManuallyDrop; use std::iter::*;
pub struct P;
'''
POINTEE_IDENTITIES = {
    'str': 'core.str', '[u8]': 'core.slice', '[MaybeUninit<u8>]': 'core.slice',
    '[String]': 'core.slice', 'Mutex<u8>': 'RUST_STD_MUTEX',
    'String': 'RUST_STD_STRING', 'Vec<u8>': 'RUST_STD_VEC',
    'Path': 'RUST_STD_PATH', 'OsStr': 'std.ffi.OsStr',
    'Option<u8>': 'RUST_STD_OPTION', 'u8': 'u8',
    'RefCell<u8>': 'RUST_STD_REFCELL', 'dyn Any': 'RUST_STD_ANY',
    'dyn Any+Send': 'RUST_STD_ANY', 'dyn Any+Send+Sync': 'RUST_STD_ANY',
    'dyn Error': 'RUST_STD_ERROR', 'dyn Error+Send+Sync': 'RUST_STD_ERROR',
    'dyn Iterator<Item=u8>': 'core.iter.Iterator',
    'dyn Future<Output=u8>': 'RUST_STD_FUTURE',
}
EXPECTED_UNPROBED = {
    'core.option.Option.Inner', 'core.option.Option.Item',
    'std.os.windows.io.BorrowedHandle', 'std.os.windows.io.OwnedHandle',
}


def read_inventory(header):
    rows = defaultdict(set)
    stamps = {}
    for line in header.read_text().splitlines():
        method = re.match(r'CBM_STD_METHOD\("([^"]*)", "([^"]*)"', line)
        if method:
            rows[method.group(1)].add(method.group(2))
            continue
        stamp = re.match(r'CBM_STD_STAMP\("([^"]*)", "([^"]*)", "([^"]*)"', line)
        if stamp:
            stamps[stamp.group(1)] = (stamp.group(2), stamp.group(3))
    return rows, stamps


def scan_self_methods(source, names):
    for match in re.finditer(r'\bfn\s+([A-Za-z_]\w*)', source):
        offset = match.end()
        while offset < len(source) and source[offset].isspace():
            offset += 1
        if offset < len(source) and source[offset] == '<':
            depth = 0
            while offset < len(source):
                char = source[offset]
                if char == '<':
                    depth += 1
                elif char == '>' and source[offset - 1] != '-':
                    depth -= 1
                offset += 1
                if depth == 0:
                    break
        while offset < len(source) and source[offset].isspace():
            offset += 1
        if offset >= len(source) or source[offset] != '(':
            continue
        receiver = source[offset + 1:offset + 81]
        receiver = re.sub(r'//[^\n]*', '', receiver)
        receiver = re.sub(r'#\[[^\]]*\]', '', receiver).lstrip()
        if re.match(r"(&\s*('\w+\s+)?(mut\s+)?)?(mut\s+)?self\b", receiver):
            names.add(match.group(1))


def candidate_names(stable_source, json_directory, rows):
    names = set()
    for crate in ('core', 'alloc', 'std'):
        for path in (stable_source / crate / 'src').rglob('*.rs'):
            scan_self_methods(path.read_text(errors='replace'), names)
        doc = json.loads((json_directory / f'{crate}.json').read_text())
        if doc['format_version'] != 61:
            raise ValueError(f'{crate}: format_version {doc["format_version"]}, expected 61')
        for item in doc['index'].values():
            inner = item['inner']
            function = inner.get('function') if isinstance(inner, dict) else None
            inputs = function['sig']['inputs'] if function else []
            if inputs and inputs[0][0] == 'self' and item['name']:
                names.add(item['name'])
    for methods in rows.values():
        names.update(methods)
    return sorted(name for name in names if name not in KEYWORDS and not name.startswith('r#'))


def build_source(spec, names, trait_imports):
    kind, receiver = spec
    lines = ['#![allow(warnings)]', trait_imports, PRELUDE]
    first_line = len('\n'.join(lines).split('\n')) + 1
    for index, name in enumerate(names):
        if kind == 'ty':
            lines.append(f'pub fn f{index}(x: {receiver}) {{ let _ = x.{name}(); }}')
        else:
            generics, parameter = receiver
            lines.append(
                f'pub fn f{index}{generics}(x: {parameter}) {{ let _ = x.{name}(); }}'
            )
    return '\n'.join(lines) + '\n', first_line


def compile_spec(rustc, work_directory, tag, spec, names, trait_imports):
    source, first_line = build_source(spec, names, trait_imports)
    source_path = work_directory / f'{tag}.rs'
    source_path.write_text(source)
    command = [
        str(rustc), '--edition', '2021', '--crate-type', 'lib', '--emit=metadata',
        '-o', str(work_directory / f'{tag}.rmeta'), '--error-format=short',
        str(source_path),
    ]
    process = subprocess.run(command, capture_output=True, text=True, check=False)
    errors = defaultdict(set)
    prefix = re.escape(str(source_path))
    for line in process.stderr.splitlines():
        match = re.match(prefix + r':(\d+):\d+: error(\[E\d+\])?', line)
        if match:
            errors[int(match.group(1))].add(match.group(2) or 'E????')
    global_errors = [
        line for line in process.stderr.splitlines()
        if line.startswith('error') and not line.startswith('error: aborting')
    ]
    result = {}
    for index, name in enumerate(names):
        codes = sorted(errors.get(first_line + index, set()))
        if '[E0599]' not in codes:
            result[name] = codes
    return {'bad': global_errors[:5], 'r': result}


def result_key(identity, spec):
    return identity + ' || ' + json.dumps(spec)


def write_results(path, results):
    temporary = path.with_suffix(path.suffix + '.new')
    temporary.write_text(json.dumps(results, sort_keys=True))
    temporary.replace(path)


def compile_matrix(rustc, work_directory, names, trait_imports, result_path):
    results = json.loads(result_path.read_text()) if result_path.exists() else {}
    jobs = [(identity, spec) for identity, specs in S.items() for spec in specs]
    for index, (identity, spec) in enumerate(jobs):
        key = result_key(identity, spec)
        if key in results:
            continue
        results[key] = compile_spec(
            rustc, work_directory, f'receiver-{index}', spec, names, trait_imports
        )
        write_results(result_path, results)
        print(f'compiler probe receiver {index + 1}/{len(jobs)}', file=sys.stderr)
    return results


def compile_pointees(rustc, work_directory, names, trait_imports, result_path):
    results = json.loads(result_path.read_text()) if result_path.exists() else {}
    jobs = [
        (pointee, spec)
        for pointee, specs in POINTEE_SPECS.items()
        for spec in specs
    ]
    for index, (pointee, spec) in enumerate(jobs):
        key = result_key(pointee, spec)
        if key in results:
            continue
        results[key] = compile_spec(
            rustc, work_directory, f'pointee-{index}', spec, names, trait_imports
        )
        write_results(result_path, results)
        print(f'compiler probe pointee {index + 1}/{len(jobs)}', file=sys.stderr)
    return results


def callable_methods(results):
    per_identity = defaultdict(set)
    bad = []
    for key, result in results.items():
        identity = key.split(' || ', 1)[0]
        bad.extend((key, error) for error in result['bad'])
        for name, errors in result['r'].items():
            if not set(errors) & EXCLUDED_ERRORS:
                per_identity[identity].add(name)
    return per_identity, bad


def pointee_of(receiver):
    match = re.match(r"^(?:&)?(\w+(?:::\w+)*)<(?:'static,)?(.*)>$", receiver)
    if not match:
        return None
    wrapper, pointee = match.groups()
    if wrapper == 'LazyCell' and ',' in pointee and not pointee.startswith('['):
        pointee = pointee.split(',', 1)[0]
    if wrapper == 'Pin':
        pointee = re.sub(r'^&(mut )?', '', pointee)
        nested = re.match(r'^(Box|Arc|Rc)<(.*)>$', pointee)
        if nested:
            pointee = nested.group(2)
    if pointee.startswith('(') and pointee.endswith(')'):
        pointee = pointee[1:-1]
    return pointee


def except_deref_misses(results, pointee_results, rows, stamps):
    pointee_callable, bad = callable_methods(pointee_results)
    receiver_callable, receiver_bad = callable_methods(results)
    misses = defaultdict(set)
    ignored = defaultdict(set)
    for key, result in results.items():
        identity, encoded = key.split(' || ', 1)
        if stamps.get(identity, ('',))[0] != 'complete_except_deref':
            continue
        spec = json.loads(encoded)
        receiver = spec[1] if spec[0] == 'ty' else 'GEN<P>'
        pointee = pointee_of(receiver) if spec[0] == 'ty' else 'P'
        for name, errors in result['r'].items():
            if set(errors) & EXCLUDED_ERRORS or name in rows[identity]:
                continue
            if name in pointee_callable.get(pointee, set()):
                continue
            target = POINTEE_IDENTITIES.get(pointee)
            if target and name in rows[target]:
                continue
            if not target and pointee in {'P', 'CStr'}:
                ignored[identity].add(name)
                continue
            misses[identity].add(name)
    return misses, ignored, bad + receiver_bad, receiver_callable


def verify_results(results, pointee_results, rows, stamps):
    callable_per_identity, bad = callable_methods(results)
    for alias, source in ALIAS.items():
        callable_per_identity[alias] = callable_per_identity[source]
    misses = defaultdict(set)
    for identity, methods in callable_per_identity.items():
        if stamps.get(identity, ('',))[0] == 'complete':
            unlisted = methods - rows[identity]
            if unlisted:
                misses[identity].update(unlisted)
    deref_misses, ignored, deref_bad, _ = except_deref_misses(
        results, pointee_results, rows, stamps
    )
    for identity, methods in deref_misses.items():
        misses[identity].update(methods)
    probed = set(callable_per_identity)
    unprobed = set(stamps) - probed
    errors = []
    if bad or deref_bad:
        errors.append(f'unattributed compiler errors: {(bad + deref_bad)[:5]}')
    if unprobed != EXPECTED_UNPROBED:
        errors.append(f'unexpected unprobed identities: {sorted(unprobed)}')
    if misses:
        errors.append('callable-and-unlisted: ' + json.dumps(
            {identity: sorted(methods) for identity, methods in sorted(misses.items())}
        ))
    return errors, misses, ignored, len(probed)


def rustc_path(toolchain):
    sysroot = subprocess.check_output(
        ['rustup', 'run', toolchain, 'rustc', '--print', 'sysroot'], text=True
    ).strip()
    return Path(sysroot) / 'bin/rustc', Path(sysroot)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--header', type=Path,
                        default=ROOT / 'internal/cbm/lsp/rust_std_inventory.h')
    parser.add_argument('--toolchain', default='stable')
    parser.add_argument('--generator-toolchain', type=Path,
                        default=DEFAULT_GENERATOR_TOOLCHAIN)
    parser.add_argument('--results-in', type=Path)
    parser.add_argument('--pointee-results-in', type=Path)
    args = parser.parse_args()
    started = time.monotonic()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    rustc, sysroot = rustc_path(args.toolchain)
    version = subprocess.check_output([str(rustc), '--version'], text=True).strip()
    if not version.startswith('rustc 1.99.0 '):
        print(f'FAIL: compiler probe requires stable rustc 1.99.0, got {version}')
        return 1
    rows, stamps = read_inventory(args.header)
    json_directory = args.generator_toolchain / 'share/doc/rust/json'
    names = candidate_names(sysroot / 'lib/rustlib/src/rust/library', json_directory, rows)
    trait_imports = (Path(__file__).with_name('probe_traits.rs')).read_text()
    result_path = args.results_in or args.work_dir / 'results.json'
    pointee_path = args.pointee_results_in or args.work_dir / 'pointee.json'
    results = (json.loads(result_path.read_text()) if args.results_in else
               compile_matrix(rustc, args.work_dir, names, trait_imports, result_path))
    pointee_results = (json.loads(pointee_path.read_text()) if args.pointee_results_in else
                       compile_pointees(
                           rustc, args.work_dir, names, trait_imports, pointee_path
                       ))
    expected_results = len({
        result_key(identity, spec)
        for identity, specs in S.items()
        for spec in specs
    })
    expected_pointees = sum(map(len, POINTEE_SPECS.values()))
    if len(results) != expected_results or len(pointee_results) != expected_pointees:
        print('FAIL: compiler probe result set is incomplete')
        return 1
    errors, misses, ignored, types_probed = verify_results(
        results, pointee_results, rows, stamps
    )
    calls = (len(results) + len(pointee_results)) * len(names)
    elapsed = time.monotonic() - started
    if errors:
        print('FAIL: compiler probe found an incomplete inventory')
        for error in errors:
            print(error)
        return 1
    print(f'PASS: {calls} compiler-probed calls; 0 callable-and-unlisted')
    print(f'types={types_probed}/{len(stamps)} names={len(names)} specs={len(results)} '
          f'pointees={len(pointee_results)} rustc={version!r} seconds={elapsed:.1f}')
    if ignored:
        print('deferred no-identity pointee methods=' + json.dumps(
            {identity: sorted(methods) for identity, methods in sorted(ignored.items())}
        ))
    return 0


if __name__ == '__main__':
    sys.exit(main())
