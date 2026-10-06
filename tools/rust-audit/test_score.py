#!/usr/bin/env python3
"""Run the public rescore entry point on graph tables with known scoring outcomes."""
import os
from pathlib import Path
import subprocess
import shutil
import sqlite3
import tempfile
import unittest

ROOT = Path(os.environ.get('SCORE_TEST_ROOT', Path(__file__).resolve().parents[2]))


class ScoreSummary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        base = Path(cls.temp.name)
        bench = base / 'bench'
        corpus = bench / 'codex' / 'codex-rs'
        corpus.mkdir(parents=True)
        (corpus / 'Cargo.toml').write_text('[package]\nname="fixture"\nversion="0.1.0"\n')
        # Match the entry point's frozen-corpus guard without running an indexer.
        for i in range(4696):
            (corpus / f'empty{i}.rs').touch()
        (corpus / 'f.rs').write_text('\n'.join([''] * 50))
        (corpus / 'd.rs').write_text('\n'.join([''] * 70))
        cls.out = base / 'score'
        cls.out.mkdir()
        files = {
            'g_nodes.tsv': (
                '1\tFile\tf.rs\tf.rs\t0\t0\n'
                '2\tModule\tf.rs\tf.rs\t1\t50\n'
                '3\tStruct\tT\td.rs\t10\t10\n'
                '4\tFunction\tcallback\td.rs\t20\t20\n'
                '5\tFunction\tf\tf.rs\t20\t35\n'
                '6\tField\tr#type\td.rs\t40\t40\n'
                '7\tModule\tnarrow\tf.rs\t1\t5\n'
                '8\tStruct\tU\td.rs\t45\t45\n'
                '9\tFile\td.rs\td.rs\t0\t0\n'
                '10\tFunction\tuncompiled\td.rs\t60\t65\n'
                '11\tFunction\tcfg\tf.rs\t40\t45\n'
                '12\tFunction\tvalue\td.rs\t30\t30\n'
                '13\tEnumVariant\tV\td.rs\t46\t46\n'
                '14\tMacro\tm\td.rs\t47\t47\n'
                '15\tModule\tinner\td.rs\t48\t48\n'
                '16\tAssociatedConst\tC\td.rs\t49\t49\n'),
            'g_calls.tsv': '5\t4\tfixture\n10\t4\tfixture\n11\t4\tfixture\n',
            'g_USAGE.tsv': '2\t3\n1\t3\n7\t3\n2\t8\n5\t6\n2\t12\n',
            'g_CALL_REFERENCE.tsv': '5\t12\n5\t4\n',
            'g_IMPORTS.tsv': ''.join(f'1\t{900+i}\n' for i in range(10)),
            'g_DEFINES_METHOD.tsv': '3\t4\n',
            'g_IMPLEMENTS.tsv': '',
            'edge_types.tsv': 'CALLS\t3\nCALL_REFERENCE\t2\n',
        }
        for name, text in files.items():
            (cls.out / name).write_text(text)
        (bench / 'refs.tsv').write_text(
            'D\tf.rs\t20\tfn\tf\tf().\n'
            'D\td.rs\t10\ttype\tT\tT#\n'
            'D\td.rs\t20\tfn\tcallback\tcallback().\n'
            'D\td.rs\t30\tfn\tvalue\tvalue().\n'
            'D\td.rs\t40\tterm\ttype\tT#type.\n'
            'D\td.rs\t45\ttype\tU\tU#\n'
            'R\tf.rs\t10\tref\t-\t-\td.rs\t10\ttype\tT\n'
            'R\tf.rs\t11\tref\t-\t-\td.rs\t10\ttype\tT\n'
            'R\tf.rs\t3\tuse\t-\t-\td.rs\t45\ttype\tU\n'
            'R\tf.rs\t21\tref\t20\tf\td.rs\t40\tterm\ttype\n'
            'R\tf.rs\t22\tref\t20\tf\td.rs\t30\tfn\tvalue\n'
            'R\tf.rs\t23\tref\t20\tf\td.rs\t20\tfn\tcallback\n'
            'R\tf.rs\t24\tref\t20\tf\td.rs\t46\ttype\tV\n'
            'R\tf.rs\t25\tref\t20\tf\td.rs\t47\tmacro\tm\n'
            'R\tf.rs\t26\tref\t20\tf\td.rs\t48\tmodule\tinner\n'
            'R\tf.rs\t27\tref\t20\tf\td.rs\t49\tterm\tC\n')
        (bench / 'truth.tsv').write_text(''.join(
            f'f.rs\t{21+i}\t{kind}\tf.rs\t20\tf\trust-analyzer cargo fixture 0 callback().\td.rs\t20\t-\t-\n'
            for i, kind in enumerate(['path', 'path', 'method', 'ref_in_macro'])) + ''.join(
            f'f.rs\t{30+i}\t{kind}\tf.rs\t20\tf\trust-analyzer cargo fixture 0 value().\td.rs\t30\t-\t-\n'
            for i, kind in enumerate(['path', 'method', 'ref_in_macro'])))
        env = dict(os.environ, CBM_RUST_BENCH=str(bench))
        cls.env = env
        result = subprocess.run(
            ['bash', str(ROOT / 'scripts/rust-score.sh'), '--rescore', str(cls.out)],
            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            details = '\n'.join(p.read_text() for p in cls.out.glob('*.txt'))
            raise RuntimeError(result.stdout + details)
        cls.summary = result.stdout
        cls.families = (cls.out / 'families.txt').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_all_unverifiable_reasons_are_counted(self):
        self.assertRegex(self.summary, r'calls unverifiable pairs .*: 2\n')

    def test_reference_kinds_without_published_usage_are_visible(self):
        for kind in ('EnumVariant', 'Macro', 'Module', 'AssociatedConst'):
            with self.subTest(kind=kind):
                self.assertRegex(self.summary, rf'\n  {kind}\s+0\s+0\s+0\s+0 / 1\s+0.0')

    def test_only_non_use_module_references_inside_source_span_confirm_usage(self):
        self.assertIn('source Module/File span: 2\n', self.summary)
        self.assertRegex(self.families, r'\n  Struct\s+2\s+2\s+0\n')
        self.assertRegex(self.families, r'\n  Function\s+0\s+1\s+0\n')

    def test_reference_edges_credit_function_values_once(self):
        self.assertRegex(self.families, r'    Function\s+2 / 2\s+100.0%')

    def test_raw_identifier_matches_the_oracle_name(self):
        self.assertRegex(self.families, r'    Field\s+1 / 1\s+100.0%')
        self.assertRegex(self.families, r'\n  Field\s+1\s+0\s+0\n')

    def test_all_shared_pair_sites_are_disclosed(self):
        self.assertIn('another credited site: 4 / 4\n', self.summary)

    def test_definition_rows_do_not_become_reference_kinds(self):
        self.assertNotRegex(self.summary, r'\n  type\s+0\s+0\s+0')
        self.assertLessEqual(len(self.summary.splitlines()), 36)

    def test_legacy_rescore_recovers_reference_edges_from_frozen_database(self):
        legacy = Path(self.temp.name) / 'legacy'
        shutil.copytree(self.out, legacy)
        (legacy / 'g_CALL_REFERENCE.tsv').unlink()
        (legacy / 'cache').mkdir()
        with sqlite3.connect(legacy / 'cache/score.db') as db:
            db.execute('CREATE TABLE edges(source_id INT, target_id INT, type TEXT)')
            db.executemany('INSERT INTO edges VALUES (?,?,?)',
                           [(5, 12, 'CALL_REFERENCE'), (5, 4, 'CALL_REFERENCE')])
        result = subprocess.run(
            ['bash', str(ROOT / 'scripts/rust-score.sh'), '--rescore', str(legacy)],
            env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual((legacy / 'g_CALL_REFERENCE.tsv').read_text(), '5\t12\n5\t4\n')

    def test_missing_reference_table_and_database_is_an_error(self):
        missing = Path(self.temp.name) / 'missing'
        shutil.copytree(self.out, missing)
        (missing / 'g_CALL_REFERENCE.tsv').unlink()
        result = subprocess.run(
            ['bash', str(ROOT / 'scripts/rust-score.sh'), '--rescore', str(missing)],
            env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn('no g_CALL_REFERENCE.tsv or cache/score.db', result.stdout)


class MacroOracleGap(unittest.TestCase):
    def test_unresolved_import_alias_is_an_oracle_gap(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'f.rs').write_text(
                'use dependency::target as renamed;\nfn caller() { let target = renamed(); other(); }\n'
                'fn target() {}\n')
            (root / 'g_nodes.tsv').write_text(
                '1\tFunction\tcaller\tf.rs\t2\t2\n2\tFunction\ttarget\tf.rs\t3\t3\n')
            (root / 'g_calls.tsv').write_text('1\t2\tfixture\n')
            (root / 'refs.tsv').write_text('D\tf.rs\t3\tfn\ttarget\ttarget().\n')
            (root / 'truth.tsv').write_text(
                'f.rs\t2\tpath_unresolved\t-\t-\t-\t-\t-\t-\t-\t-\n'
                'f.rs\t2\tpath\tf.rs\t2\tcaller\trust-analyzer cargo fixture 0 other().\text\t-\t-\t-\n')
            result = subprocess.run(['python3', str(ROOT / 'tools/rust-audit/precision.py')],
                                    env=dict(os.environ, AUDIT_DIR=tmp, AUDIT_CORPUS=tmp),
                                    check=True, capture_output=True, text=True)
            self.assertIn('unverifiable: oracle left a source call site unresolved', result.stdout)
            self.assertEqual((root / 'wrong_pairs.tsv').read_text(), '')

    def test_missing_macro_target_is_unverifiable_despite_a_resolved_namesake(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'f.rs').write_text(
                'fn caller() { let target = 0;\n assert_eq!(local.target(), other());\n external.target();\n}\n'
                'fn target() {}\nfn other() {}\n')
            (root / 'g_nodes.tsv').write_text(
                '1\tFunction\tcaller\tf.rs\t1\t4\n'
                '2\tFunction\ttarget\tf.rs\t5\t5\n')
            (root / 'g_calls.tsv').write_text('1\t2\tfixture\n')
            (root / 'refs.tsv').write_text('D\tf.rs\t5\tfn\ttarget\ttarget().\n')
            (root / 'truth.tsv').write_text(
                'f.rs\t2\tref_in_macro\tf.rs\t1\tcaller\trust-analyzer cargo fixture 0 implicit().\text\t-\t-\t-\n'
                'f.rs\t3\tmethod\tf.rs\t1\tcaller\trust-analyzer cargo dependency 0 target().\text\t-\t-\t-\n')
            result = subprocess.run(['python3', str(ROOT / 'tools/rust-audit/precision.py')],
                                    env=dict(os.environ, AUDIT_DIR=tmp, AUDIT_CORPUS=tmp),
                                    check=True, capture_output=True, text=True)
            self.assertIn('unverifiable: oracle has no target for the named call in macro arguments',
                          result.stdout)
            self.assertEqual((root / 'wrong_pairs.tsv').read_text(), '')


if __name__ == '__main__':
    unittest.main()
