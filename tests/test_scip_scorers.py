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
SITES_INVENTORY = ROOT / "scripts" / "scip_rust_sites.py"


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


def inventory_records(source, needle, target, ordinals=(0,)):
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        corpus = root / "corpus"
        (corpus / "src").mkdir(parents=True)
        (corpus / "src" / "lib.rs").write_text(source, encoding="utf-8")
        scip = root / "fixture.scip"
        anchor = symbol("fixture/package_anchor().")
        write_scip(
            scip,
            "src/lib.rs",
            (
                occurrence(
                    range_for(source, "package_anchor"),
                    anchor,
                    1,
                    line_range(source, 0),
                ),
                *(
                    occurrence(range_for(source, needle, ordinal), target)
                    for ordinal in ordinals
                ),
            ),
        )
        spec = importlib.util.spec_from_file_location("scip_rust_sites", SITES_INVENTORY)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.call_sites(corpus, scip)


def load_identity_module():
    path = ROOT / "scripts" / "scip_rust_identity.py"
    spec = importlib.util.spec_from_file_location("scip_rust_identity", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def create_identity_database(path, nodes, edges=()):
    connection = sqlite3.connect(path)
    connection.execute(
        "create table nodes (id integer primary key, project text, label text, "
        "name text, qualified_name text, file_path text, start_line integer, "
        "end_line integer, properties text)"
    )
    connection.executemany("insert into nodes values (?,?,?,?,?,?,?,?,?)", nodes)
    if edges:
        connection.execute(
            "create table edges (source_id integer, target_id integer, type text)"
        )
        connection.executemany(
            "insert into edges values (?, ?, 'CALLS')",
            edges,
        )
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

    def create_database(self, nodes, edges=(), with_edges=True):
        connection = sqlite3.connect(self.database)
        connection.execute(
            "create table nodes (id integer primary key, project text, label text, "
            "name text, qualified_name text, file_path text, start_line integer, "
            "end_line integer, properties text)"
        )
        expanded = (
            (index, "fixture", label, name, qualified_name, file_path, start, end, "{}")
            for index, (qualified_name, file_path, name, label, start, end)
            in enumerate(nodes, 1)
        )
        connection.executemany("insert into nodes values (?,?,?,?,?,?,?,?,?)", expanded)
        if with_edges:
            connection.execute(
                "create table edges (source_id integer, target_id integer, type text)"
            )
            node_ids = {
                qualified_name: index
                for index, (qualified_name, *_rest) in enumerate(nodes, 1)
            }
            connection.executemany(
                "insert into edges values (?, ?, 'CALLS')",
                ((node_ids[source], node_ids[target]) for source, target in edges),
            )
        connection.commit()
        connection.close()

    def write_dump(self, calls):
        rows = []
        for needle, ordinal, target, strategy, verdict in calls:
            start, end = byte_span(self.source, needle, ordinal)
            callee_start = start
            callee_end = start + len(needle) - 2
            rows.append(
                "\t".join(
                    (
                        "C", "src/lib.rs", str(start), str(end), "fixture.caller",
                        needle[:-2], target, strategy, "1.0", verdict, "-", "-",
                        str(callee_start), str(callee_end),
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
                "--scip", str(self.scip), "--out", str(self.detail),
            ),
            text=True,
            capture_output=True,
            env=environment,
            check=False,
        )


class ScipScorerTests(unittest.TestCase):
    def test_external_oracle_target_is_compared_with_published_target(self):
        source = "fn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            external = "rust-analyzer cargo dependency 1.0.0 dependency/target()."
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 1),
                    ),
                    occurrence(range_for(source, "target", 1), external),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 2, 2),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=1 unscorable=0", result.stdout
            )
            self.assertIn("target_identity_mismatch", result.stdout)

    def test_callable_oracle_caller_does_not_use_source_function_fallback(self):
        source = "#[rstest]\nfn caller() { target(); }\nfn target() {}\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            generated = symbol("fixture/generated_case().")
            target = symbol("fixture/target().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "caller"), generated, 1,
                        line_range(source, 1),
                    ),
                    occurrence(
                        range_for(source, "target", 1), target, 1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "target", 0), target),
                ),
                (
                    symbol_information(generated, 17, "generated_case"),
                    symbol_information(target, 17, "target"),
                ),
            )
            create_identity_database(
                fixture.database,
                (
                    (
                        1, "fixture", "Function", "caller", "fixture.caller",
                        "src/lib.rs", 1, 2,
                        json.dumps({"decorators": ["#[rstest]"]}),
                    ),
                    (
                        2, "fixture", "Function", "target", "fixture.target",
                        "src/lib.rs", 3, 3, "{}",
                    ),
                ),
                ((1, 2),),
            )
            fixture.write_dump(
                (("target()", 0, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=0 unscorable=1", result.stdout
            )
            self.assertIn(
                "unscorable_category=caller_oracle_gap:proc_macro_output count=1",
                result.stdout,
            )
            self.assertIn(
                "truth relations 1 found correct 0 recall=0.00%", result.stdout
            )
            self.assertIn(
                "truth sites 1 found correct 0 recall=0.00%", result.stdout
            )
            self.assertIn(
                "caller_category=caller_gap:proc_macro_output "
                "outcome=missed count=1",
                result.stdout,
            )

    def test_active_call_with_unmapped_callable_caller_stays_in_recall_truth(self):
        source = "fn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 1),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    (
                        "fixture.caller", "src/lib.rs", "caller",
                        "Function", 2, 2,
                    ),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "truth relations 1 found correct 0 recall=0.00%", result.stdout
            )
            self.assertIn(
                "truth sites 1 found correct 0 recall=0.00%", result.stdout
            )
            self.assertIn(
                "caller_category=caller_gap:oracle_definition_missing "
                "outcome=missed count=1",
                result.stdout,
            )

    def test_callable_caller_without_identity_is_wrong_when_source_disagrees(self):
        source = "fn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 1),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    (
                        "fixture.caller", "src/lib.rs", "not_caller", "Function",
                        2, 2,
                    ),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=1 unscorable=0", result.stdout
            )
            self.assertIn("caller_source_mismatch", result.stdout)

    def test_inactive_cfg_caller_is_unscorable_and_absent_from_recall(self):
        source = "#[cfg(any())]\nfn caller() { target(); }\nfn target() {}\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 1), target, 1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "target", 0), target),
                ),
                (symbol_information(target, 17, "target"),),
            )
            create_identity_database(
                fixture.database,
                (
                    (
                        1, "fixture", "Function", "caller", "fixture.caller",
                        "src/lib.rs", 1, 2,
                        json.dumps({"decorators": ["#[cfg(any())]"]}),
                    ),
                    (
                        2, "fixture", "Function", "target", "fixture.target",
                        "src/lib.rs", 3, 3, "{}",
                    ),
                ),
                ((1, 2),),
            )
            fixture.write_dump(
                (("target()", 0, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=0 unscorable=1", result.stdout
            )
            self.assertIn(
                "unscorable_category=caller_cfg_inactive count=1", result.stdout
            )
            self.assertIn(
                "truth sites 0 found correct 0 recall=0.00%", result.stdout
            )

    def test_noncallable_oracle_caller_uses_source_function_for_recall(self):
        source = "fn target() {}\nfn caller() { static CELL: i32 = target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            cell = symbol("fixture/caller/CELL.")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 1),
                    ),
                    occurrence(
                        range_for(source, "CELL"), cell, 1,
                        [
                            1, source.splitlines()[1].index("static"),
                            1, len(source.splitlines()[1]) - 2,
                        ],
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                    symbol_information(cell, 37, "CELL"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 2, 2),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=1 wrong=0 unscorable=0", result.stdout
            )
            self.assertIn(
                "label_category=source_caller:non_callable_oracle count=1",
                result.stdout,
            )
            self.assertIn(
                "truth relations 1 found correct 1 recall=100.00%", result.stdout
            )
            self.assertIn(
                "truth sites 1 found correct 1 recall=100.00%", result.stdout
            )

    def test_source_function_caller_is_carried_into_relation_truth(self):
        source = "fn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (symbol_information(target, 17, "target"),),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 2, 2),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "truth relations 1 found correct 1 recall=100.00%", result.stdout
            )
            self.assertIn(
                "caller_category=source_function_item outcome=found count=1",
                result.stdout,
            )

    def test_reference_truth_without_target_definition_is_not_wrong(self):
        source = "fn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 1),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 2, 2),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=0 unscorable=1", result.stdout
            )
            self.assertIn(
                "unscorable_category=target_oracle_gap:oracle_definition_missing count=1",
                result.stdout,
            )

    def test_resolved_oracle_target_rejects_nonsemantic_graph_target(self):
        source = "fn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            caller = symbol("fixture/caller().")
            external = "rust-analyzer cargo dependency 1.0.0 dependency/target()."
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 0),
                    ),
                    occurrence(range_for(source, "target"), external),
                ),
                (symbol_information(caller, 17, "caller"),),
            )
            fixture.create_database(
                (
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 1, 1),
                    ("__route__ANY__/target", "", "target", "Route", 0, 0),
                ),
                edges=(("fixture.caller", "__route__ANY__/target"),),
            )
            fixture.write_dump(
                (("target()", 0, "__route__ANY__/target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=1 unscorable=0", result.stdout
            )
            self.assertIn("target_identity_mismatch", result.stdout)
            self.assertIn("non_rust_target_edges=1", result.stdout)
            self.assertIn("integrity_errors=0", result.stdout)

    def test_edge_without_provenance_is_unscorable_and_in_totals(self):
        source = "fn target() {}\nfn caller() {}\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target"), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 1),
                    ),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 2, 2),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(())

            result = fixture.run_site()

            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=0 unscorable=1", result.stdout
            )
            self.assertIn("integrity_error=edge_without_dump count=1", result.stdout)

    def test_pair_label_is_correct_while_wrong_occurrence_is_counted(self):
        source = "fn target() {}\nfn other() {}\nfn caller() { target(); other(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            other = symbol("fixture/other().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "other", 0), other, 1,
                        line_range(source, 1),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                    occurrence(range_for(source, "other", 1), other),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(other, 17, "other"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 3, 3),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (
                    ("target()", 1, "fixture.target", "direct", "admitted"),
                    ("other()", 1, "fixture.target", "direct", "admitted"),
                )
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=1 wrong=0 unscorable=0", result.stdout
            )
            self.assertIn("wrong_occurrences=1", result.stdout)

    def test_pair_label_is_wrong_when_contradicted_and_unscorable(self):
        source = "fn target() {}\nfn other() {}\nfn caller() { other(); ghost(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            other = symbol("fixture/other().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target"), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "other", 0), other, 1,
                        line_range(source, 1),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "other", 1), other),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(other, 17, "other"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 3, 3),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (
                    ("other()", 1, "fixture.target", "direct", "admitted"),
                    ("ghost()", 0, "fixture.target", "direct", "admitted"),
                )
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=1 unscorable=0", result.stdout
            )
            self.assertIn("target_identity_mismatch", result.stdout)

    def test_missing_edges_table_is_a_named_integrity_error(self):
        source = "fn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 1),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 2, 2),
                ),
                with_edges=False,
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("integrity_error=missing_edges_table count=1", result.stdout)
            self.assertNotIn("correct=1", result.stdout)

    def test_graph_and_dump_mismatches_are_distinct_integrity_errors(self):
        source = "fn target() {}\nfn other() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            other = symbol("fixture/other().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "other"), other, 1,
                        line_range(source, 1),
                    ),
                    occurrence(
                        range_for(source, "caller"), caller, 1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(other, 17, "other"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.other", "src/lib.rs", "other", "Function", 2, 2),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 3, 3),
                ),
                edges=(("fixture.caller", "fixture.other"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("integrity_error=edge_without_dump count=1", result.stdout)
            self.assertIn("integrity_error=dump_without_edge count=1", result.stdout)
            self.assertNotIn("correct=1", result.stdout)

    def test_wrong_caller_edge_is_scored_wrong(self):
        source = (
            "fn target() {}\n"
            "fn real_caller() { target(); }\n"
            "fn wrong_caller() {}\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            real_caller = symbol("fixture/real_caller().")
            wrong_caller = symbol("fixture/wrong_caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 0), target, 1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "real_caller"), real_caller, 1,
                        line_range(source, 1),
                    ),
                    occurrence(
                        range_for(source, "wrong_caller"), wrong_caller, 1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(real_caller, 17, "real_caller"),
                    symbol_information(wrong_caller, 17, "wrong_caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 1, 1),
                    (
                        "fixture.real_caller", "src/lib.rs", "real_caller",
                        "Function", 2, 2,
                    ),
                    (
                        "fixture.wrong_caller", "src/lib.rs", "wrong_caller",
                        "Function", 3, 3,
                    ),
                ),
                edges=(("fixture.wrong_caller", "fixture.target"),),
            )
            start, end = byte_span(source, "target", 1)
            fixture.dump.write_text(
                "\t".join(
                    (
                        "C", "src/lib.rs", str(start), str(end + 2),
                        "fixture.wrong_caller", "target", "fixture.target",
                        "direct", "1.0", "admitted", "-", "-", str(start),
                        str(end),
                    )
                ) + "\n",
                encoding="utf-8",
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=1 unscorable=0", result.stdout
            )
            self.assertIn(
                "caller_identity_mismatch",
                (fixture.detail / "wrong-edges.tsv").read_text(encoding="utf-8"),
            )

    def test_published_wrong_edge_is_scored_wrong(self):
        source = 'fn target(_: &str) {}\nfn wrong() {}\nfn caller() { target("("); }\n'
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/target().")
            wrong = symbol("fixture/wrong().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "wrong"), wrong, 1, line_range(source, 1)),
                    occurrence(range_for(source, "caller"), caller, 1, line_range(source, 2)),
                    occurrence(range_for(source, "target", 1), truth),
                ),
                (
                    symbol_information(truth, 17, "target"),
                    symbol_information(wrong, 17, "wrong"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.wrong", "src/lib.rs", "wrong", "Function", 2, 2),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 3, 3),
                ),
                edges=(("fixture.caller", "fixture.wrong"),),
            )
            site_start, site_end = byte_span(source, 'target("(")')
            callee_start, callee_end = byte_span(source, "target", 1)
            fixture.dump.write_text(
                "\t".join(
                    (
                        "C", "src/lib.rs", str(site_start), str(site_end),
                        "fixture.caller", "target", "fixture.wrong", "direct",
                        "1.0", "admitted", "-", "-", str(callee_start),
                        str(callee_end),
                    )
                ) + "\n",
                encoding="utf-8",
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=1 unscorable=0", result.stdout
            )
            wrong_rows = (fixture.detail / "wrong-edges.tsv").read_text(encoding="utf-8")
            self.assertIn("fixture.wrong", wrong_rows)
            self.assertIn(truth, wrong_rows)

    def test_closed_resolution_is_absent_from_precision(self):
        source = "fn target() {}\nfn wrong() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/target().")
            wrong = symbol("fixture/wrong().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "wrong"), wrong, 1, line_range(source, 1)),
                    occurrence(range_for(source, "target", 1), truth),
                ),
                (
                    symbol_information(truth, 17, "target"),
                    symbol_information(wrong, 17, "wrong"),
                ),
            )
            fixture.create_database(
                (("fixture.wrong", "src/lib.rs", "wrong", "Function", 2, 2),)
            )
            fixture.write_dump(
                (("target()", 1, "fixture.wrong", "direct", "closed"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=0 correct=0 wrong=0 unscorable=0", result.stdout
            )

    def test_oracle_call_without_indexer_record_is_missed(self):
        source = "fn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/target().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "target", 1), truth),
                ),
                (symbol_information(truth, 17, "target"),),
            )
            fixture.create_database(())
            fixture.write_dump(())

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("truth sites 1 found correct 0 recall=0.00%", result.stdout)
            missed = (fixture.detail / "missed-occurrences.tsv").read_text(encoding="utf-8")
            self.assertIn(truth, missed)

    def test_long_function_with_nested_function_scores_correct(self):
        source = (
            "fn package_anchor() {}\n"
            "fn target() {\n"
            "    let before = 1;\n"
            "    fn nested() {}\n"
            "}\n"
            "fn caller() { target(); }\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            target = symbol("fixture/target().")
            nested = symbol("fixture/target/nested().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), target, 1, line_range(source, 1)),
                    occurrence(range_for(source, "nested"), nested, 1, line_range(source, 3)),
                    occurrence(range_for(source, "caller"), caller, 1, line_range(source, 5)),
                    occurrence(range_for(source, "target", 1), target),
                ),
                (
                    symbol_information(target, 17, "target"),
                    symbol_information(nested, 17, "nested"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.target", "src/lib.rs", "target", "Function", 2, 5),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 6, 6),
                ),
                edges=(("fixture.caller", "fixture.target"),),
            )
            fixture.write_dump(
                (("target()", 1, "fixture.target", "direct", "admitted"),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=1 wrong=0 unscorable=0", result.stdout
            )

    def test_inventory_uses_rust_syntax_for_complex_calls(self):
        source = (
            "fn package_anchor() {}\n"
            "fn target<T>() {}\n"
            "fn caller() {\n"
            "    target::<{ 2 > 1 }>();\n"
            "    target::<fn() -> u8>();\n"
            '    sink("(", target());\n'
            "}\n"
        )
        target = symbol("fixture/target().")
        spans = [byte_span(source, "target", ordinal) for ordinal in (1, 2, 3)]

        records = inventory_records(source, "target", target, (1, 2, 3))

        self.assertEqual(
            [
                ("src/lib.rs", start, end, target, "plain_path")
                for start, end in spans
            ],
            [tuple(record) for record in records],
        )

    def test_inventory_finds_parenthesized_callee(self):
        source = "fn package_anchor() {}\nfn caller() { (target)(); }\n"
        start, end = byte_span(source, "target")

        records = inventory_records(source, "target", symbol("fixture/target()."))

        self.assertEqual(
            [("src/lib.rs", start, end, symbol("fixture/target()."), "plain_path")],
            [tuple(record) for record in records],
        )

    def test_inventory_excludes_function_reference_that_is_not_invoked_leaf(self):
        source = (
            "fn package_anchor() {}\n"
            "fn caller(values: Values) { "
            "values.iter().map(Type::target).collect(); }\n"
        )

        records = inventory_records(
            source, "target", symbol("fixture/Type/target().")
        )

        self.assertEqual([], records)

    def test_inventory_includes_tuple_constructor_as_constructor_call(self):
        source = (
            "fn package_anchor() {}\n"
            "fn caller(value: Value) { RemoteState::Ice(value); }\n"
        )
        start, end = byte_span(source, "Ice")
        target = symbol("fixture/RemoteState#Ice#")

        records = inventory_records(source, "Ice", target)

        self.assertEqual(
            [("src/lib.rs", start, end, target, "constructor")],
            [tuple(record) for record in records],
        )

    def test_inventory_resolves_self_tuple_constructor_to_type_constructor(self):
        source = (
            "fn package_anchor() {}\n"
            "struct Wrapper(u8);\n"
            "impl Wrapper { fn new(value: u8) -> Self { Self(value) } }\n"
        )
        start, end = byte_span(source, "Self", 1)

        records = inventory_records(
            source, "Self", symbol("fixture/impl#[Wrapper]"), (0, 1)
        )

        self.assertEqual(
            [
                (
                    "src/lib.rs",
                    start,
                    end,
                    symbol("fixture/Wrapper#"),
                    "constructor",
                )
            ],
            [tuple(record) for record in records],
        )

    def test_inventory_counts_static_function_pointer_as_call_through_value(self):
        source = (
            "fn package_anchor() {}\n"
            "static CALLBACK: fn(Value) = target;\n"
            "fn caller(value: Value) { CALLBACK(value); }\n"
        )
        start, end = byte_span(source, "CALLBACK", 1)

        records = inventory_records(
            source, "CALLBACK", symbol("fixture/CALLBACK."), (1,)
        )

        self.assertEqual(
            [("src/lib.rs", start, end, "call_through_value", "plain_path")],
            [tuple(record) for record in records],
        )

    def test_inventory_keeps_same_name_external_package_external(self):
        source = "fn package_anchor() {}\nfn caller() { target(); }\n"
        start, end = byte_span(source, "target")
        external = "rust-analyzer cargo fixture 9.9.9 dependency/target()."

        records = inventory_records(source, "target", external)

        self.assertEqual(
            [("src/lib.rs", start, end, "external", "plain_path")],
            [tuple(record) for record in records],
        )

    def test_inventory_classifies_method_call(self):
        source = "fn package_anchor() {}\nfn caller(value: Thing) { value.run(); }\n"
        start, end = byte_span(source, "run")

        records = inventory_records(source, "run", symbol("fixture/impl#[Thing]run()."))

        self.assertEqual(
            [("src/lib.rs", start, end, symbol("fixture/impl#[Thing]run()."), "method")],
            [tuple(record) for record in records],
        )

    def test_inventory_classifies_type_qualified_method_call(self):
        source = "fn package_anchor() {}\nfn caller() { Type::method(); }\n"
        start, end = byte_span(source, "method")

        records = inventory_records(source, "method", symbol("fixture/impl#[Type]method()."))

        self.assertEqual(
            [
                (
                    "src/lib.rs",
                    start,
                    end,
                    symbol("fixture/impl#[Type]method()."),
                    "qualified_path",
                )
            ],
            [tuple(record) for record in records],
        )

    def test_inventory_classifies_trait_qualified_method_call(self):
        source = "fn package_anchor() {}\nfn caller() { <T as Trait>::method(); }\n"
        start, end = byte_span(source, "method")
        target = symbol("fixture/impl#[T][Trait]method().")

        records = inventory_records(source, "method", target)

        self.assertEqual(
            [("src/lib.rs", start, end, target, "qualified_path")],
            [tuple(record) for record in records],
        )

    def test_inventory_classifies_call_inside_format_argument(self):
        source = 'fn package_anchor() {}\nfn caller() { format!("{}", target()); }\n'
        start, end = byte_span(source, "target")

        records = inventory_records(source, "target", symbol("fixture/target()."))

        self.assertEqual(
            [("src/lib.rs", start, end, symbol("fixture/target()."), "macro_argument")],
            [tuple(record) for record in records],
        )

    def test_inventory_includes_call_inside_attribute_arguments(self):
        source = (
            "fn package_anchor() {}\n"
            "#[tracing::instrument(fields(value = target()))]\n"
            "fn caller() {}\n"
        )
        start, end = byte_span(source, "target")
        target = symbol("fixture/target().")

        records = inventory_records(source, "target", target)

        self.assertEqual(
            [("src/lib.rs", start, end, target, "plain_path")],
            [tuple(record) for record in records],
        )

    def test_inventory_recovers_call_from_complex_macro_arguments(self):
        source = (
            "fn package_anchor() {}\n"
            "fn caller() { let expected = json!({\n"
            '    "account": {"type": "chatgpt", "planType": plan_type},\n'
            '    "workspaceRouting": routing("selected", "https://example.com")\n'
            "}); }\n"
        )
        start, end = byte_span(source, "routing")
        target = symbol("fixture/routing().")

        records = inventory_records(source, "routing", target)

        self.assertEqual(
            [("src/lib.rs", start, end, target, "macro_argument")],
            [tuple(record) for record in records],
        )

    def test_inventory_excludes_tuple_patterns(self):
        source = (
            "fn package_anchor() {}\n"
            "enum Event { Done(u8) }\n"
            "fn caller(event: Event) {\n"
            "    let constructed = Event::Done(1);\n"
            "    let Event::Done(from_let) = event else { return; };\n"
            "    if let Event::Done(from_if_let) = event {}\n"
            "    match event { Event::Done(from_match) => {} }\n"
            "    let matched = matches!(event, Event::Done(_));\n"
            "}\n"
        )
        target = symbol("fixture/Event#Done#")
        construction = byte_span(source, "Done", 1)

        records = inventory_records(
            source, "Done", target, (1, 2, 3, 4, 5)
        )

        self.assertEqual(
            [
                (
                    "src/lib.rs",
                    construction[0],
                    construction[1],
                    target,
                    "constructor",
                )
            ],
            [tuple(record) for record in records],
        )

    def test_inventory_excludes_tuple_pattern_in_custom_macro(self):
        source = (
            "fn package_anchor() {}\n"
            "enum Event { Done(u8) }\n"
            "fn caller(event: Event) { check_pattern!(event, Event::Done(_)); }\n"
        )

        records = inventory_records(
            source, "Done", symbol("fixture/Event#Done#"), (1,)
        )

        self.assertEqual([], records)

    def test_literal_tuple_macro_argument_is_undetermined_not_truth(self):
        source = (
            "fn package_anchor() {}\n"
            "enum Event { Done(u8) }\n"
            "fn caller(event: Event) { check_pattern!(event, Event::Done(1)); }\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            corpus = root / "corpus"
            (corpus / "src").mkdir(parents=True)
            (corpus / "src" / "lib.rs").write_text(source, encoding="utf-8")
            scip = root / "fixture.scip"
            write_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "package_anchor"),
                        symbol("fixture/package_anchor()."),
                        1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "Done", 1),
                        symbol("fixture/Event#Done#"),
                    ),
                ),
            )
            spec = importlib.util.spec_from_file_location(
                "scip_rust_sites", SITES_INVENTORY
            )
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            records, completeness = module._call_inventory(corpus, scip)

            self.assertEqual([], records)
            self.assertEqual(
                1,
                dict(completeness)[
                    "excluded_macro_argument_tuple_undetermined"
                ],
            )

    def test_binding_tuple_macro_argument_is_undetermined_not_truth(self):
        source = (
            "fn package_anchor() {}\n"
            "enum Event { Done(u8) }\n"
            "fn caller(event: Event) { "
            "check_pattern!(event, Event::Done(value)); }\n"
        )

        records = inventory_records(
            source, "Done", symbol("fixture/Event#Done#"), (1,)
        )

        self.assertEqual([], records)

    def test_completeness_reports_tuple_pattern_as_pattern_not_parse_failure(self):
        source = (
            "fn package_anchor() {}\n"
            "enum Event { Done(u8) }\n"
            "fn caller(event: Event) { match event { Event::Done(_) => {} } }\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            corpus = root / "corpus"
            (corpus / "src").mkdir(parents=True)
            (corpus / "src" / "lib.rs").write_text(source, encoding="utf-8")
            scip = root / "fixture.scip"
            write_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "package_anchor"),
                        symbol("fixture/package_anchor()."),
                        1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "Done", 1),
                        symbol("fixture/Event#Done#"),
                    ),
                ),
            )
            spec = importlib.util.spec_from_file_location(
                "scip_rust_sites", SITES_INVENTORY
            )
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            _, completeness = module._call_inventory(corpus, scip)

            counts = dict(completeness)
            self.assertEqual(1, counts["excluded_pattern"])
            self.assertEqual(0, counts["excluded_call_parser_failure"])

    def test_completeness_names_non_call_reference_categories(self):
        source = (
            "fn package_anchor() {}\n"
            "struct Thing;\n"
            "fn target() {}\n"
            "pub use crate::Thing as Exported;\n"
            "fn caller(value: Thing, left: Thing, right: Thing) {\n"
            "    let callback: fn() = target;\n"
            "    if left == (right) {}\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            corpus = root / "corpus"
            (corpus / "src").mkdir(parents=True)
            (corpus / "src" / "lib.rs").write_text(source, encoding="utf-8")
            scip = root / "fixture.scip"
            write_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "package_anchor"),
                        symbol("fixture/package_anchor()."),
                        1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "Thing", 0),
                        symbol("fixture/Thing#"),
                        1,
                        line_range(source, 1),
                    ),
                    occurrence(
                        range_for(source, "target", 0),
                        symbol("fixture/target()."),
                        1,
                        line_range(source, 2),
                    ),
                    occurrence(
                        range_for(source, "Thing", 1),
                        symbol("fixture/Thing#"),
                    ),
                    occurrence(
                        range_for(source, "Thing", 2),
                        symbol("fixture/Thing#"),
                    ),
                    occurrence(
                        range_for(source, "target", 1),
                        symbol("fixture/target()."),
                    ),
                    occurrence(
                        range_for(source, "=="),
                        symbol("fixture/impl#[Thing][PartialEq]eq()."),
                    ),
                ),
            )
            spec = importlib.util.spec_from_file_location(
                "scip_rust_sites", SITES_INVENTORY
            )
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            _, completeness = module._call_inventory(corpus, scip)

            counts = dict(completeness)
            self.assertEqual(1, counts["excluded_import_or_reexport"])
            self.assertEqual(1, counts["excluded_type_or_variant_reference"])
            self.assertEqual(1, counts["excluded_function_reference_not_invoked"])
            self.assertEqual(1, counts["excluded_operator_resolution_reference"])

    def test_completeness_counts_self_constructor_inventory_reference(self):
        source = (
            "fn package_anchor() {}\n"
            "struct Wrapper(u8);\n"
            "impl Wrapper { fn new(value: u8) -> Self { Self(value) } }\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            corpus = root / "corpus"
            (corpus / "src").mkdir(parents=True)
            (corpus / "src" / "lib.rs").write_text(source, encoding="utf-8")
            scip = root / "fixture.scip"
            write_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "package_anchor"),
                        symbol("fixture/package_anchor()."),
                        1,
                        line_range(source, 0),
                    ),
                    occurrence(
                        range_for(source, "Self", 1),
                        symbol("fixture/impl#[Wrapper]"),
                    ),
                ),
            )
            spec = importlib.util.spec_from_file_location(
                "scip_rust_sites", SITES_INVENTORY
            )
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            records, completeness = module._call_inventory(corpus, scip)

            self.assertEqual(1, len(records))
            self.assertEqual(1, dict(completeness)["inventory_found"])
            self.assertEqual(1, sum(count for _, count in completeness))

    def test_inventory_parses_each_file_in_isolation(self):
        source = b"fn caller() { target(); }\n"
        spec = importlib.util.spec_from_file_location(
            "scip_rust_sites", SITES_INVENTORY
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        callees = module._rust_call_callees(
            {"broken.rs": b"/*", "valid.rs": source}
        )

        start = source.index(b"target")
        self.assertIn((start, start + len(b"target"), False), callees["valid.rs"])

    def test_caller_functions_parse_each_file_in_isolation(self):
        source = b"fn caller() { target(); }\n"
        spec = importlib.util.spec_from_file_location(
            "scip_rust_sites", SITES_INVENTORY
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        functions = module._rust_function_items(
            {"broken.rs": b"/*", "valid.rs": source}
        )

        self.assertEqual(
            [(0, len(source) - 1, "caller", 1, 1)], functions["valid.rs"]
        )

    def test_inventory_keeps_ordinary_call_after_invalid_macro_rewrite(self):
        source = (
            "fn package_anchor() {}\n"
            "fn caller() {\n"
            "    tokio::select! { value = async {} => {} }\n"
            "    target();\n"
            "}\n"
        )
        start, end = byte_span(source, "target")

        records = inventory_records(source, "target", symbol("fixture/target()."))

        self.assertEqual(
            [("src/lib.rs", start, end, symbol("fixture/target()."), "plain_path")],
            [tuple(record) for record in records],
        )

    def test_inventory_finds_macro_argument_call_when_another_file_is_invalid(self):
        source = b"fn caller() { assert!(target()); }\n"
        spec = importlib.util.spec_from_file_location(
            "scip_rust_sites", SITES_INVENTORY
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        callees = module._rust_call_callees(
            {"broken.rs": b"/*", "valid.rs": source}
        )

        start = source.index(b"target")
        self.assertIn((start, start + len(b"target"), False), callees["valid.rs"])

    def test_inventory_marks_local_closure_call(self):
        source = (
            "fn package_anchor() {}\n"
            "fn caller() { let closure = || (); closure(); }\n"
        )
        start, end = byte_span(source, "closure", 1)

        records = inventory_records(source, "closure", "local 42", (1,))

        self.assertEqual(
            [("src/lib.rs", start, end, "call_through_value", "plain_path")],
            [tuple(record) for record in records],
        )

    def test_inventory_finds_nested_function_call_inside_long_function(self):
        padding = "".join(f"    let value_{index} = {index};\n" for index in range(120))
        source = (
            "fn package_anchor() {}\n"
            "fn outer() {\n"
            f"{padding}"
            "    fn nested() { target(); }\n"
            "}\n"
        )
        start, end = byte_span(source, "target")

        records = inventory_records(source, "target", symbol("fixture/target()."))

        self.assertEqual(
            [("src/lib.rs", start, end, symbol("fixture/target()."), "plain_path")],
            [tuple(record) for record in records],
        )

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

    def test_nested_same_name_definition_does_not_identify_enclosing_node(self):
        source = "#[instrument]\nfn target() {\n    fn target() {}\n    target();\n}\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            nested = symbol("fixture/target/target().")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target", 1), nested, 1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "target", 2), nested),
                ),
                (symbol_information(nested, 17, "target"),),
            )
            create_identity_database(
                database,
                ((
                    1, "fixture", "Function", "target", "fixture.target",
                    "src/lib.rs", 1, 5,
                    json.dumps({"decorators": ["#[instrument]"]}),
                ),),
            )

            identity = load_identity_module()
            identities = identity.map_rust_graph_nodes(database, scip)

            self.assertEqual(
                identities[1],
                identity.IdentityOutcome(
                    status="oracle_gap", reason="proc_macro_output"
                ),
            )

    def test_duplicate_symbol_target_in_wrong_document_is_wrong(self):
        source_a = "fn helper() {}\nfn caller() { helper(); }\n"
        source_b = "fn helper() {}\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source_a)
            (fixture.corpus / "src" / "a.rs").write_text(
                source_a, encoding="utf-8"
            )
            (fixture.corpus / "src" / "b.rs").write_text(
                source_b, encoding="utf-8"
            )
            shared = symbol("fixture/helper().")
            caller = symbol("fixture/caller().")
            document_a = field(1, 2, b"src/a.rs")
            document_a += b"".join((
                occurrence(
                    range_for(source_a, "helper", 0), shared, 1,
                    line_range(source_a, 0),
                ),
                occurrence(
                    range_for(source_a, "caller"), caller, 1,
                    line_range(source_a, 1),
                ),
                occurrence(range_for(source_a, "helper", 1), shared),
                symbol_information(shared, 17, "helper"),
                symbol_information(caller, 17, "caller"),
            ))
            document_b = field(1, 2, b"src/b.rs")
            document_b += b"".join((
                occurrence(
                    range_for(source_b, "helper"), shared, 1,
                    line_range(source_b, 0),
                ),
                symbol_information(shared, 17, "helper"),
            ))
            fixture.scip.write_bytes(
                field(2, 2, document_a) + field(2, 2, document_b)
            )
            target = "fixture.b.helper"
            fixture.create_database(
                (
                    (target, "src/b.rs", "helper", "Function", 1, 1),
                    ("fixture.a.caller", "src/a.rs", "caller", "Function", 2, 2),
                ),
                edges=(("fixture.a.caller", target),),
            )
            start, end = byte_span(source_a, "helper", 1)
            fixture.dump.write_text(
                "\t".join((
                    "C", "src/a.rs", str(start), str(end + 2),
                    "fixture.a.caller", "helper", target, "direct", "1.0",
                    "admitted", "-", "-", str(start), str(end),
                )) + "\n",
                encoding="utf-8",
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=0 wrong=1 unscorable=0", result.stdout
            )
            self.assertIn("target_identity_mismatch", result.stdout)

    def test_file_nodes_are_excluded_from_identity_outcomes(self):
        source = "fn target() {}\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            target = symbol("fixture/target().")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "target"), target, 1, line_range(source, 0)
                    ),
                ),
                (symbol_information(target, 17, "target"),),
            )
            create_identity_database(
                database,
                (
                    (
                        1, "fixture", "File", "lib.rs", "fixture.src.lib",
                        "src/lib.rs", 1, 1, "{}",
                    ),
                    (
                        2, "fixture", "Function", "target", "fixture.target",
                        "src/lib.rs", 1, 1, "{}",
                    ),
                ),
            )

            identities = load_identity_module().map_rust_graph_nodes(database, scip)

            self.assertEqual(set(identities), {2})

    def test_path_named_module_maps_to_scip_module_definition(self):
        source = "mod network_rule_saved {}\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scip = root / "fixture.scip"
            database = root / "graph.db"
            module_symbol = symbol("fixture/network_rule_saved/")
            write_identity_scip(
                scip,
                "src/network_rule_saved.rs",
                (
                    occurrence(
                        range_for(source, "network_rule_saved"),
                        module_symbol,
                        1,
                        line_range(source, 0),
                    ),
                ),
                (symbol_information(module_symbol, 29, "network_rule_saved"),),
            )
            create_identity_database(
                database,
                ((
                    1, "fixture", "Module", "src/network_rule_saved.rs",
                    "fixture.src.network_rule_saved", "src/network_rule_saved.rs",
                    1, 1, "{}",
                ),),
            )

            identities = load_identity_module().map_rust_graph_nodes(database, scip)

            self.assertEqual(identities[1].symbol, module_symbol)

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
        source = (
            "#[derive(Clone)]\n"
            "struct Thing;\n"
            "fn use_clone(value: &Thing) { value.clone(); }\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            owner = symbol("fixture/Thing#")
            derived = symbol("fixture/impl#[Thing][Clone]clone().")
            caller = symbol("fixture/use_clone().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "Thing"), owner, 1, line_range(source, 1)),
                    occurrence(
                        range_for(source, "use_clone"),
                        caller,
                        1,
                        line_range(source, 2),
                    ),
                    occurrence(range_for(source, "clone", 1), derived),
                ),
                (
                    symbol_information(owner, 49, "Thing"),
                    symbol_information(caller, 17, "use_clone"),
                ),
            )
            create_identity_database(
                fixture.database,
                (
                    (
                        1, "fixture", "Struct", "Thing", "fixture.Thing",
                        "src/lib.rs", 1, 2, "{}",
                    ),
                    (
                        2, "fixture", "Method", "clone", "fixture.Thing.clone[Clone]",
                        "src/lib.rs", 1, 2,
                        json.dumps(
                            {"rust_derive": "Clone", "rust_derive_owner": "fixture.Thing"}
                        ),
                    ),
                    (
                        3, "fixture", "Function", "use_clone", "fixture.caller",
                        "src/lib.rs", 3, 3, "{}",
                    ),
                ),
                ((3, 2),),
            )

            identity = load_identity_module()
            identities = identity.map_rust_graph_nodes(fixture.database, fixture.scip)

            self.assertEqual(
                identities[2],
                identity.IdentityOutcome(
                    status="matched",
                    symbol=derived,
                    derive="Clone",
                    owner="fixture.Thing",
                ),
            )
            fixture.write_dump(
                ((
                    "clone()", 0, "fixture.Thing.clone[Clone]", "derive",
                    "admitted",
                ),)
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "published_edges=1 correct=1 wrong=0 unscorable=0",
                result.stdout,
            )

    def test_missing_definitions_use_closed_specific_reasons(self):
        source = "fn actual() {}\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            corpus = root / "corpus"
            (corpus / "src").mkdir(parents=True)
            (corpus / "src" / "root.rs").write_text(
                'include!("bindings.rs");\n', encoding="utf-8"
            )
            (corpus / "src" / "bindings.rs").write_text(
                "pub type Binding = i32;\n", encoding="utf-8"
            )
            scip = root / "fixture.scip"
            database = root / "graph.db"
            actual = symbol("fixture/actual().")
            write_identity_scip(
                scip,
                "src/lib.rs",
                (
                    occurrence(
                        range_for(source, "actual"), actual, 1, line_range(source, 0)
                    ),
                ),
                (symbol_information(actual, 17, "actual"),),
            )
            create_identity_database(
                database,
                (
                    (
                        1, "fixture", "Function", "expected", "fixture.expected",
                        "src/lib.rs", 1, 1, "{}",
                    ),
                    (
                        2, "fixture", "Macro", "generated", "fixture.generated",
                        "src/lib.rs", 1, 1, "{}",
                    ),
                    (
                        3, "fixture", "Function", "inactive", "fixture.inactive",
                        "src/inactive.rs", 1, 1,
                        json.dumps({"decorators": ["#[cfg(any())]"]}),
                    ),
                    (
                        4, "fixture", "Function", "generated_test",
                        "fixture.generated_test",
                        "src/proc.rs", 1, 1,
                        json.dumps({"decorators": ["#[tokio::test]"]}),
                    ),
                    (
                        5, "fixture", "Type", "Binding", "fixture.Binding",
                        "src/bindings.rs", 1, 1, "{}",
                    ),
                    (
                        6, "fixture", "Function", "missing", "fixture.missing",
                        "src/missing.rs", 1, 1, "{}",
                    ),
                    (
                        7, "fixture", "Function", "active", "fixture.active",
                        "src/active.rs", 1, 1,
                        json.dumps({"decorators": ["#[cfg(all())]"]}),
                    ),
                ),
            )
            connection = sqlite3.connect(database)
            connection.execute("create table projects (name text, root_path text)")
            connection.execute(
                "insert into projects values (?, ?)", ("fixture", str(corpus))
            )
            connection.commit()
            connection.close()

            identity = load_identity_module()
            identities = identity.map_rust_graph_nodes(database, scip)

            self.assertEqual(
                {
                    node_id: (outcome.status, outcome.reason)
                    for node_id, outcome in identities.items()
                },
                {
                    1: ("oracle_gap", "name_mismatch"),
                    2: ("oracle_gap", "unsupported_node_kind"),
                    3: ("not_in_oracle_configuration", "cfg_inactive"),
                    4: ("oracle_gap", "proc_macro_output"),
                    5: ("oracle_gap", "included_source"),
                    6: ("oracle_gap", "oracle_document_missing"),
                    7: ("oracle_gap", "oracle_document_missing"),
                },
            )
            self.assertTrue(
                all(
                    outcome.reason in identity.IDENTITY_REASONS
                    for outcome in identities.values()
                )
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
                (
                    ("fixture.src.lib.target", "src/lib.rs", "target", "Function", 2, 2),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 3, 3),
                ),
                edges=(("fixture.caller", "fixture.src.lib.target"),),
            )
            fixture.write_dump((("target()", 2, "fixture.src.lib.target", "exact", "admitted"),))

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "strategy=exact call_form=plain_path correct=0 wrong=0 unscorable=1",
                result.stdout,
            )
            self.assertIn("unscorable_reason=oracle_ambiguous count=1", result.stdout)

    def test_trait_method_truth_does_not_match_inherent_method_definition(self):
        source = "fn trait_run() {}\nfn inherent_run() {}\nfn caller() { run(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/impl#[Thing][Trait]run().")
            inherent = symbol("fixture/impl#[Thing]run().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "trait_run"), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "inherent_run"), inherent, 1, line_range(source, 1)),
                    occurrence(range_for(source, "run", 2), truth),
                ),
                (
                    symbol_information(truth, 26, "run"),
                    symbol_information(inherent, 26, "run"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.src.lib.Thing.run", "src/lib.rs", "run", "Method", 2, 2),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 3, 3),
                ),
                edges=(("fixture.caller", "fixture.src.lib.Thing.run"),),
            )
            fixture.write_dump((("run()", 2, "fixture.src.lib.Thing.run", "method", "admitted"),))

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "strategy=method call_form=plain_path correct=0 wrong=1 unscorable=0",
                result.stdout,
            )

    def test_uppercase_module_name_is_not_treated_as_method_owner_identity(self):
        source = "fn method_run() {}\nfn module_run() {}\nfn caller() { run(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/impl#[Thing]run().")
            module_function = symbol("fixture/Thing/run().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "method_run"), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "module_run"), module_function, 1, line_range(source, 1)),
                    occurrence(range_for(source, "run", 2), truth),
                ),
                (
                    symbol_information(truth, 26, "run"),
                    symbol_information(module_function, 17, "run"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.src.lib.Thing.run", "src/lib.rs", "run", "Function", 2, 2),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 3, 3),
                ),
                edges=(("fixture.caller", "fixture.src.lib.Thing.run"),),
            )
            fixture.write_dump((("run()", 2, "fixture.src.lib.Thing.run", "owner", "admitted"),))

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "strategy=owner call_form=plain_path correct=0 wrong=1 unscorable=0",
                result.stdout,
            )

    def test_recall_counts_repeated_same_pair_call_as_found(self):
        source = "fn target() {}\nfn caller() { target(); target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/target().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "caller"), caller, 1, line_range(source, 1)),
                    occurrence(range_for(source, "target", 1), truth),
                    occurrence(range_for(source, "target", 2), truth),
                ),
                (
                    symbol_information(truth, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    ("fixture.src.lib.target", "src/lib.rs", "target", "Function", 1, 1),
                    ("fixture.caller", "src/lib.rs", "caller", "Function", 2, 2),
                ),
                edges=(("fixture.caller", "fixture.src.lib.target"),),
            )
            fixture.write_dump(
                (
                    ("target()", 1, "fixture.src.lib.target", "free", "admitted"),
                    ("target()", 2, "-", "none", "closed"),
                )
            )

            result = fixture.run_site()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "truth relations 1 found correct 1 recall=100.00%", result.stdout
            )
            self.assertIn(
                "truth sites 2 found correct 2 recall=100.00%", result.stdout
            )

    def test_harness_compares_full_definition_identity_not_short_names(self):
        source = "fn target() {}\nfn target() {}\nfn caller() { target(); }\n"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = ScorerFixture(temporary, source)
            truth = symbol("fixture/right/target().")
            wrong = symbol("fixture/wrong/target().")
            caller = symbol("fixture/caller().")
            write_identity_scip(
                fixture.scip,
                "src/lib.rs",
                (
                    occurrence(range_for(source, "target", 0), truth, 1, line_range(source, 0)),
                    occurrence(range_for(source, "target", 1), wrong, 1, line_range(source, 1)),
                    occurrence(range_for(source, "caller"), caller, 1, line_range(source, 2)),
                    occurrence(range_for(source, "target", 2), truth),
                ),
                (
                    symbol_information(truth, 17, "target"),
                    symbol_information(wrong, 17, "target"),
                    symbol_information(caller, 17, "caller"),
                ),
            )
            fixture.create_database(
                (
                    (
                        "fixture.src.lib.wrong.target", "src/lib.rs", "target",
                        "Function", 2, 2,
                    ),
                    (
                        "fixture.caller", "src/lib.rs", "caller",
                        "Function", 3, 3,
                    ),
                ),
                edges=(("fixture.caller", "fixture.src.lib.wrong.target"),),
            )
            fixture.write_dump(
                ((
                    "target()", 2, "fixture.src.lib.wrong.target", "direct",
                    "admitted",
                ),)
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
                    "--db", str(fixture.database), "--dump", str(fixture.dump),
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
            self.assertIn(
                "precision strategy=direct call_form=plain_path "
                "outcome=wrong reason=target_identity_mismatch count=1",
                result.stdout,
            )
            self.assertIn(
                "fixture.src.lib.wrong.target",
                (fixture.detail / "wrong-edges.tsv").read_text(encoding="utf-8"),
            )
            self.assertIn(
                truth,
                (fixture.detail / "missed-occurrences.tsv").read_text(
                    encoding="utf-8"
                ),
            )


if __name__ == "__main__":
    unittest.main()
