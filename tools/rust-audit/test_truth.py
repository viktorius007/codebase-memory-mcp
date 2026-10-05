#!/usr/bin/env python3
"""Exercise both SCIP extractors with colliding definitions and explicit target bodies.

Build tsdiff's truth and refs, then set TSDIFF_BIN_DIR to the binary directory.
The expected sites are fixture-owned, independent of the symbol/body matching code.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

BIN = Path(os.environ['TSDIFF_BIN_DIR'])
SYMBOL = 'rust-analyzer cargo fixture 0.0.0 target().'


def varint(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def field(number, value):
    if isinstance(value, int):
        return varint(number << 3) + varint(value)
    if isinstance(value, str):
        value = value.encode()
    return varint((number << 3) | 2) + varint(len(value)) + value


def occurrence(span, body=(), definition=False):
    return (field(1, b''.join(map(varint, span))) + field(2, SYMBOL)
            + field(3, int(definition)) + field(7, b''.join(map(varint, body))))


def document(path, occurrences):
    return field(2, field(1, path) + b''.join(field(2, o) for o in occurrences) + field(6, 1))


class CollidingDefinitions(unittest.TestCase):
    def extract(self, docs, sources):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for path, source in sources.items():
                (root / path).write_text(source)
            index = root / 'index.scip'
            index.write_bytes(b''.join(docs))
            results = {}
            for program in ('truth', 'refs'):
                output = root / (program + '.tsv')
                subprocess.run([str(BIN / program), str(index), str(root), str(output)],
                               check=True, capture_output=True, text=True)
                results[program] = [line.split('\t') for line in output.read_text().splitlines()]
            return results

    def assert_target(self, results, path, line):
        self.assertEqual([(r[7], r[8]) for r in results['truth'] if r[6] == SYMBOL],
                         [(path, str(line))])
        self.assertEqual([(r[6], r[7]) for r in results['refs'] if r[0] == 'R'],
                         [(path, str(line))])

    def test_sibling_targets_and_library_tests_keep_document_identity(self):
        source = 'fn target() {}\nfn caller() { target(); }\n'
        definition = occurrence([0, 3, 9], [0, 0, 14], True)
        reference = occurrence([1, 14, 20], [0, 0, 14])
        for paths in [('a.rs', 'b.rs'), ('lib.rs', 'test.rs')]:
            with self.subTest(paths=paths):
                docs = [document(paths[0], [definition]), document(paths[1], [definition, reference])]
                self.assert_target(self.extract(docs, dict.fromkeys(paths, source)), paths[1], 1)
                self.assert_target(self.extract(docs[::-1], dict.fromkeys(paths, source)), paths[1], 1)

    def test_cfg_twins_in_one_document_use_the_selected_body(self):
        source = '#[cfg(a)]\nfn target() {}\n#[cfg(b)]\nfn target() {}\nfn caller() { target(); }\n'
        definitions = [occurrence([1, 3, 9], [1, 0, 14], True),
                       occurrence([3, 3, 9], [3, 0, 14], True)]
        for line in (1, 3):
            with self.subTest(line=line):
                docs = [document('twins.rs', definitions + [occurrence([4, 14, 20], [line, 0, 14])])]
                self.assert_target(self.extract(docs, {'twins.rs': source}), 'twins.rs', line + 1)

    def test_cross_file_reexport_with_colliding_symbol_is_unresolved(self):
        source = 'fn target() {}\nfn caller() { target(); }\n'
        definition = occurrence([0, 3, 9], [0, 0, 14], True)
        docs = [document('a.rs', [definition]), document('b.rs', [definition]),
                document('caller.rs', [occurrence([1, 14, 20])])]
        result = self.extract(docs, dict.fromkeys(['a.rs', 'b.rs', 'caller.rs'], source))
        self.assertEqual(result['truth'][0][2], 'path_unresolved')
        self.assertEqual(result['truth'][0][7:9], ['-', '-'])
        self.assertFalse(any(r[0] == 'R' for r in result['refs']))

    def test_macro_definitions_sharing_a_body_do_not_pick_the_first(self):
        source = 'fn target() {} fn target() {}\nfn caller() { target(); }\n'
        body = [0, 0, 29]
        docs = [document('macro.rs', [occurrence([0, 3, 9], body, True),
                                     occurrence([0, 18, 24], body, True),
                                     occurrence([1, 14, 20], body)])]
        result = self.extract(docs, {'macro.rs': source})
        self.assertEqual(result['truth'][0][2], 'path_unresolved')
        self.assertFalse(any(r[0] == 'R' for r in result['refs']))

    def test_unique_cross_file_symbol_stays_resolved_without_a_body(self):
        source = 'fn target() {}\nfn caller() { target(); }\n'
        docs = [document('a.rs', [occurrence([0, 3, 9], [0, 0, 14], True)]),
                document('caller.rs', [occurrence([1, 14, 20])])]
        self.assert_target(self.extract(docs, dict.fromkeys(['a.rs', 'caller.rs'], source)), 'a.rs', 1)


if __name__ == '__main__':
    unittest.main()
