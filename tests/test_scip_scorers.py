#!/usr/bin/env python3
"""Occurrence- and definition-exact contracts for the Rust SCIP scorers."""

import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import stat
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SITE_SCORER = ROOT / "scripts" / "scip-site-join.py"
HARNESS_SCORER = ROOT / "scripts" / "scip-harness-join.py"


def varint(value):
    encoded = bytearray()
    while value >= 0x80:
        encoded.append((value & 0x7F) | 0x80)
        value >>= 7
    encoded.append(value)
    return bytes(encoded)


def field(number, wire, payload):
    if wire == 0:
        return varint((number << 3) | wire) + varint(payload)
    return varint((number << 3) | wire) + varint(len(payload)) + payload


def packed(values):
    return b"".join(varint(value) for value in values)


def occurrence(rng, symbol, roles=0, enclosing=None):
    encoded = field(1, 2, packed(rng)) + field(2, 2, symbol.encode())
    if roles:
        encoded += field(3, 0, roles)
    if enclosing is not None:
        encoded += field(7, 2, packed(enclosing))
    return field(2, 2, encoded)


def write_scip(path, relative_path, occurrences):
    document = field(1, 2, relative_path.encode()) + b"".join(occurrences)
    path.write_bytes(field(2, 2, document))


def symbol_information(identity, kind, display_name):
    encoded = field(1, 2, identity.encode()) + field(5, 0, kind)
    encoded += field(6, 2, display_name.encode())
    return field(3, 2, encoded)


def write_identity_scip(path, relative_path, occurrences, symbols):
    document = field(1, 2, relative_path.encode())
    document += b"".join(occurrences) + b"".join(symbols)
    path.write_bytes(field(2, 2, document))


def line_col(source, offset):
    prefix = source[:offset]
    return prefix.count("\n"), len(prefix.rsplit("\n", 1)[-1])


def byte_span(source, needle, ordinal=0):
    start = -1
    for _ in range(ordinal + 1):
        start = source.index(needle, start + 1)
    return start, start + len(needle)


def range_for(source, needle, ordinal=0):
    start, end = byte_span(source, needle, ordinal)
    start_line, start_col = line_col(source, start)
    end_line, end_col = line_col(source, end)
    return [start_line, start_col, end_line, end_col]


def line_range(source, line_number):
    line = source.splitlines()[line_number]
    return [line_number, 0, line_number, len(line)]


def symbol(descriptor):
    return f"rust-analyzer cargo fixture 0.1.0 {descriptor}"


def make_executable(path, contents):
    path.write_text(contents, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def load_identity_module():
    path = ROOT / "scripts" / "scip_rust_identity.py"
    spec = importlib.util.spec_from_file_location("scip_rust_identity", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def create_identity_database(path, nodes):
    connection = sqlite3.connect(path)
    connection.execute(
        "create table nodes (id integer primary key, project text, label text, "
        "name text, qualified_name text, file_path text, start_line integer, "
        "end_line integer, properties text)"
    )
    connection.executemany("insert into nodes values (?,?,?,?,?,?,?,?,?)", nodes)
    connection.commit()
    connection.close()


class ScorerFixture:
    def __init__(self, temporary, source):
        self.root = Path(temporary)
        self.corpus = self.root / "corpus"
        (self.corpus / "src").mkdir(parents=True)
        (self.corpus / "src" / "lib.rs").write_text(source, encoding="utf-8")
        (self.corpus / "Cargo.toml").write_text(
            '[package]\nname="fixture"\nversion="0.1.0"\nedition="2021"\n',
            encoding="utf-8",
        )
        self.source = source
        self.scip = self.root / "fixture.scip"
        self.dump = self.root / "dump.tsv"
        self.database = self.root / "graph.db"
        self.detail = self.root / "detail"
        self.detail.mkdir()
        self.tools = self.root / "tools"
        self.tools.mkdir()
        make_executable(
            self.tools / "cargo",
            "#!/bin/sh\nprintf '%s\\n' '{\"packages\":[{\"name\":\"fixture\"}]}'\n",
        )

    def create_database(self, nodes):
        connection = sqlite3.connect(self.database)
        connection.execute(
            "create table nodes (qualified_name text, file_path text, name text, "
            "label text, start_line integer, end_line integer)"
        )
        connection.executemany("insert into nodes values (?,?,?,?,?,?)", nodes)
        connection.commit()
        connection.close()

    def write_dump(self, calls):
        rows = []
        for needle, ordinal, target, strategy, verdict in calls:
            start, end = byte_span(self.source, needle, ordinal)
            rows.append(
                "\t".join(
                    (
                        "C", "src/lib.rs", str(start), str(end), "fixture.caller",
                        needle[:-2], target, strategy, "1.0", verdict, "-", "-",
                    )
                )
            )
        self.dump.write_text("\n".join(rows) + "\n", encoding="utf-8")

    def run_site(self):
        environment = os.environ.copy()
        environment["PATH"] = f"{self.tools}{os.pathsep}{environment['PATH']}"
        return subprocess.run(
            (
                "python3", str(SITE_SCORER), "--corpus", str(self.corpus),
                "--dump", str(self.dump), "--db", str(self.database),
                "--scip", str(self.scip),
            ),
            text=True,
            capture_output=True,
            env=environment,
            check=False,
        )


class ScipScorerTests(unittest.TestCase):
    def test_long_function_maps_from_its_own_name_occurrence(self):
        source = "fn outer() {\n    let before = 1;\n    fn nested() {}\n    let after = 2;\n}\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            outer = symbol("fixture/outer().")
            nested = symbol("fixture/outer/nested().")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "outer"), outer, 1, [0, 0, 4, 1]),
                    occurrence(range_for(source, "nested"), nested, 1, line_range(source, 2)),
                ),
                (
                    symbol_information(outer, 17, "outer"),
                    symbol_information(nested, 17, "nested"),
                ),
            )
            create_identity_database(
                database,
                ((1, "fixture", "Function", "outer", "fixture.outer", "src/lib.rs", 1, 5, "{}"),),
            )

            identities = load_identity_module().map_rust_graph_nodes(database, scip)

            self.assertEqual(identities[1].symbol, outer)

    def test_nested_functions_each_map_from_their_own_name_occurrence(self):
        source = "fn outer() {\n    fn left() {}\n    fn right() {}\n}\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            left = symbol("fixture/outer/left().")
            right = symbol("fixture/outer/right().")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "left"), left, 1, line_range(source, 1)),
                    occurrence(range_for(source, "right"), right, 1, line_range(source, 2)),
                ),
                (
                    symbol_information(left, 17, "left"),
                    symbol_information(right, 17, "right"),
                ),
            )
            create_identity_database(
                database,
                (
                    (
                        1, "fixture", "Function", "left", "fixture.outer.left",
                        "src/lib.rs", 2, 2, "{}",
                    ),
                    (
                        2, "fixture", "Function", "right", "fixture.outer.right",
                        "src/lib.rs", 3, 3, "{}",
                    ),
                ),
            )

            identities = load_identity_module().map_rust_graph_nodes(database, scip)

            self.assertEqual((identities[1].symbol, identities[2].symbol), (left, right))

    def test_same_named_methods_in_different_trait_impls_map_apart(self):
        source = (
            "impl First for Thing {\n    fn run(&self) {}\n}\n"
            "impl Second for Thing {\n    fn run(&self) {}\n}\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            first = symbol("fixture/impl#[Thing][First]run().")
            second = symbol("fixture/impl#[Thing][Second]run().")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "run", 0), first, 1, line_range(source, 1)),
                    occurrence(range_for(source, "run", 1), second, 1, line_range(source, 4)),
                ),
                (
                    symbol_information(first, 26, "run"),
                    symbol_information(second, 26, "run"),
                ),
            )
            create_identity_database(
                database,
                (
                    (
                        1, "fixture", "Method", "run", "fixture.Thing.run[First]",
                        "src/lib.rs", 2, 2, "{}",
                    ),
                    (
                        2, "fixture", "Method", "run", "fixture.Thing.run[Second]",
                        "src/lib.rs", 5, 5, "{}",
                    ),
                ),
            )

            identities = load_identity_module().map_rust_graph_nodes(database, scip)

            self.assertEqual((identities[1].symbol, identities[2].symbol), (first, second))

    def test_inherent_and_trait_methods_with_the_same_name_map_apart(self):
        source = (
            "impl Thing {\n    fn run(&self) {}\n}\n"
            "impl Trait for Thing {\n    fn run(&self) {}\n}\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            inherent = symbol("fixture/impl#[Thing]run().")
            trait_method = symbol("fixture/impl#[Thing][Trait]run().")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "run", 0), inherent, 1, line_range(source, 1)),
                    occurrence(
                        range_for(source, "run", 1), trait_method, 1, line_range(source, 4)
                    ),
                ),
                (
                    symbol_information(inherent, 26, "run"),
                    symbol_information(trait_method, 26, "run"),
                ),
            )
            create_identity_database(
                database,
                (
                    (1, "fixture", "Method", "run", "fixture.Thing.run", "src/lib.rs", 2, 2, "{}"),
                    (
                        2, "fixture", "Method", "run", "fixture.Thing.run[Trait]",
                        "src/lib.rs", 5, 5, "{}",
                    ),
                ),
            )

            identities = load_identity_module().map_rust_graph_nodes(database, scip)

            self.assertEqual(
                (identities[1].symbol, identities[2].symbol), (inherent, trait_method)
            )

    def test_derive_generated_method_maps_to_explicit_generated_identity(self):
        source = "#[derive(Clone)]\nstruct Thing;\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            owner = symbol("fixture/Thing#")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (occurrence(range_for(source, "Thing"), owner, 1, line_range(source, 1)),),
                (symbol_information(owner, 49, "Thing"),),
            )
            create_identity_database(
                database,
                ((
                    1, "fixture", "Method", "clone", "fixture.Thing.clone[Clone]",
                    "src/lib.rs", 1, 2,
                    json.dumps({"rust_derive": "Clone", "rust_derive_owner": "fixture.Thing"}),
                ),),
            )

            identity = load_identity_module()
            identities = identity.map_rust_graph_nodes(database, scip)

            self.assertEqual(
                identities[1],
                identity.IdentityOutcome(
                    status="generated_by_derive",
                    reason="generated by derive Clone for fixture.Thing",
                    derive="Clone",
                    owner="fixture.Thing",
                ),
            )

    def test_multiple_oracle_symbols_at_one_site_are_ambiguous(self):
        source = "fn target() {}\nfn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            first = symbol("fixture/one/target().")
            second = symbol("fixture/two/target().")
            call_range = range_for(source, "target", 2)
            write_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), first, 1, line_range(source, 0)),
                    occurrence(range_for(source, "target", 1), second, 1, line_range(source, 1)),
                    occurrence(call_range, first),
                    occurrence(call_range, second),
                ),
            )
            fixture.create_database(
                (("fixture.src.lib.target", "src/lib.rs", "target", "Function", 2, 2),)
            )
            fixture.write_dump((("target()", 2, "fixture.src.lib.target", "exact", "admitted"),))

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("('exact', 'admitted', 'ambiguous', 'ambiguous_oracle')", result.stdout)
            self.assertNotIn("correct=      1", result.stdout)

    def test_trait_method_truth_does_not_match_inherent_method_definition(self):
        source = "fn trait_run() {}\nfn inherent_run() {}\nfn caller() { run(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/impl#[Thing][Trait]run().")
            inherent = symbol("fixture/impl#[Thing]run().")
            write_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "trait_run"), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "inherent_run"), inherent, 1, line_range(source, 1)),
                    occurrence(range_for(source, "run", 2), truth),
                ),
            )
            fixture.create_database(
                (("fixture.src.lib.Thing.run", "src/lib.rs", "run", "Method", 2, 2),)
            )
            fixture.write_dump((("run()", 2, "fixture.src.lib.Thing.run", "method", "admitted"),))

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("('method', 'admitted', 'internal', 'wrong_definition')", result.stdout)
            self.assertNotIn("correct=      1", result.stdout)

    def test_uppercase_module_name_is_not_treated_as_method_owner_identity(self):
        source = "fn method_run() {}\nfn module_run() {}\nfn caller() { run(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/impl#[Thing]run().")
            module_function = symbol("fixture/Thing/run().")
            write_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "method_run"), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "module_run"), module_function, 1, line_range(source, 1)),
                    occurrence(range_for(source, "run", 2), truth),
                ),
            )
            fixture.create_database(
                (("fixture.src.lib.Thing.run", "src/lib.rs", "run", "Function", 2, 2),)
            )
            fixture.write_dump((("run()", 2, "fixture.src.lib.Thing.run", "owner", "admitted"),))

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("('owner', 'admitted', 'internal', 'wrong_definition')", result.stdout)
            self.assertNotIn("correct=      1", result.stdout)

    def test_recall_counts_sibling_call_occurrences_separately(self):
        source = "fn target() {}\nfn caller() { target(); target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/target().")
            write_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "target", 1), truth),
                    occurrence(range_for(source, "target", 2), truth),
                ),
            )
            fixture.create_database(
                (("fixture.src.lib.target", "src/lib.rs", "target", "Function", 1, 1),)
            )
            fixture.write_dump(
                (
                    ("target()", 1, "fixture.src.lib.target", "free", "admitted"),
                    ("target()", 2, "-", "none", "closed"),
                )
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("truth sites 2 found correct 1 recall=50.00%", result.stdout)

    def test_harness_compares_full_definition_identity_not_short_names(self):
        source = "fn target() {}\nfn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/right/target().")
            wrong = symbol("fixture/wrong/target().")
            caller = symbol("fixture/caller().")
            write_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "target", 1), wrong, 1, line_range(source, 1)),
                    occurrence(range_for(source, "caller"), caller, 1, line_range(source, 2)),
                    occurrence(range_for(source, "target", 2), truth),
                ),
            )
            expected = fixture.root / "expected.txt"
            expected.write_text(
                "calls_matched=0\ncalls_graph_only_referenced=0\n"
                "calls_oracle_only=1\nimports=0\n",
                encoding="utf-8",
            )
            make_executable(
                fixture.tools / "cbm",
                """#!/usr/bin/env python3
import json
import sys
query = sys.argv[sys.argv.index('--query') + 1]
if ':IMPORTS' in query:
    rows = []
elif 'a.qualified_name' in query:
    rows = [['src/lib.rs', '3', '3', 'fixture.src.lib.caller',
             'src/lib.rs', '2', '2', 'fixture.src.lib.wrong.target']]
else:
    rows = [['src/lib.rs', 'caller', 'target']]
print(json.dumps({'rows': rows, 'has_more': False}))
""",
            )

            result = subprocess.run(
                (
                    "python3", str(HARNESS_SCORER), "--cbm", str(fixture.tools / "cbm"),
                    "--project", "fixture", "--corpus", str(fixture.corpus),
                    "--scip", str(fixture.scip), "--cargo", str(fixture.tools / "cargo"),
                    "--expected", str(expected), "--detail-dir", str(fixture.detail),
                ),
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("matched=0", result.stdout, result.stderr)
            self.assertIn("fabricated=1", result.stdout, result.stderr)
            self.assertIn("oracle_only=1", result.stdout, result.stderr)


if __name__ == "__main__":
    unittest.main()
