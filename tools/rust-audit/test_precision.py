"""Caller ownership is exact even when SCIP has no declaration for the caller."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('precision.py')


class NestedCaller(unittest.TestCase):
    def score(self, outer_has_call):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'f.rs').write_text('fn same() {\n fn same() { target(); }\n target();\n}\nfn target() {}\n')
            (root / 'g_nodes.tsv').write_text(
                '1\tFunction\tsame\tf.rs\t1\t4\n2\tFunction\ttarget\tf.rs\t5\t5\n')
            (root / 'g_calls.tsv').write_text('1\t2\tfixture\n')
            (root / 'refs.tsv').write_text(
                'D\tf.rs\t1\tfn\tsame\tsame().\n'
                'D\tf.rs\t2\tfn\tsame\tsame().same().\n'
                'D\tf.rs\t5\tfn\ttarget\ttarget().\n')
            # Nested caller deliberately precedes its outer caller in the oracle.
            truth = 'f.rs\t2\tpath\tf.rs\t2\tsame\trust-analyzer cargo fixture 0 other().\tf.rs\t9\t-\t-\n'
            if outer_has_call:
                truth += 'f.rs\t3\tpath\tf.rs\t1\tsame\trust-analyzer cargo fixture 0 target().\tf.rs\t5\t-\t-\n'
            (root / 'truth.tsv').write_text(truth)
            result = subprocess.run(['python3', str(SCRIPT)],
                                    env=dict(os.environ, AUDIT_DIR=tmp, AUDIT_CORPUS=tmp),
                                    capture_output=True, text=True, check=True)
            return result.stdout, (root / 'wrong_pairs.tsv').read_text()

    def test_outer_caller_selected_before_nested_homonym(self):
        output, wrong = self.score(True)
        self.assertIn('correct: explicit call', output)
        self.assertEqual(wrong, '')

    def test_outer_without_calls_does_not_borrow_nested_truth(self):
        output, wrong = self.score(False)
        self.assertIn('caller has no oracle data', output)
        self.assertEqual(wrong, '')

    def test_truth_caller_without_scip_declaration_keeps_only_own_calls(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'f.rs').write_text(
                '#[tokio::test]\nasync fn same() {\n fn same() { inner(); }\n'
                ' target();\n wrapper!(macro_target());\n}\n'
                'fn target() {}\nfn macro_target() {}\nfn inner() {}\n')
            (root / 'g_nodes.tsv').write_text(
                '1\tFunction\tsame\tf.rs\t2\t6\n'
                '2\tFunction\ttarget\tf.rs\t7\t7\n'
                '3\tFunction\tmacro_target\tf.rs\t8\t8\n'
                '4\tFunction\tinner\tf.rs\t9\t9\n')
            (root / 'g_calls.tsv').write_text('1\t2\tfixture\n1\t3\tfixture\n1\t4\tfixture\n')
            # SCIP records callees but neither source caller declaration after expansion.
            (root / 'refs.tsv').write_text(
                'D\tf.rs\t7\tfn\ttarget\ttarget().\n'
                'D\tf.rs\t8\tfn\tmacro_target\tmacro_target().\n'
                'D\tf.rs\t9\tfn\tinner\tinner().\n')
            (root / 'truth.tsv').write_text(
                'f.rs\t3\tpath\tf.rs\t3\tsame\trust-analyzer cargo fixture 0 inner().\tf.rs\t9\t-\t-\n'
                'f.rs\t4\tpath\tf.rs\t2\tsame\trust-analyzer cargo fixture 0 target().\tf.rs\t7\t-\t-\n'
                'f.rs\t5\tref_in_macro\tf.rs\t2\tsame\trust-analyzer cargo fixture 0 macro_target().\tf.rs\t8\t-\t-\n')
            result = subprocess.run(['python3', str(SCRIPT)],
                                    env=dict(os.environ, AUDIT_DIR=tmp, AUDIT_CORPUS=tmp),
                                    capture_output=True, text=True, check=True)
            self.assertRegex(result.stdout, r'1\s+33\.3%\s+correct: explicit call')
            self.assertRegex(result.stdout, r'1\s+33\.3%\s+correct: call inside macro arguments')
            wrong = (root / 'wrong_pairs.tsv').read_text()
            self.assertEqual(wrong, 'NOT IN TRUTH\tfixture\tf.rs:2 same\tf.rs:9 inner\n')


if __name__ == '__main__':
    unittest.main()
