# pm-lsif-2026-09-17 — gold-standard evidence bundle

Ground-truth reference index for the two ISSUES.md entries dated 2026-09-17
(fabricated semantic edges; missing type/import reference edges).

- `pm-lsif.json.gz` — LSIF index of the project-management workspace.
  Built with `rust-analyzer lsif .` — rust-analyzer 1.95.0
  (59807616 2026-04-14), 77 s wall — at
  `/Users/viktor/Projects/project-management`, commit **`dd7ec1b2e`**,
  clean tracked tree. Uncompressed: 220,455,585 bytes.
- `compare.py` — self-contained reproduction: parses the LSIF, queries the
  live codebase-memory index (project
  `Users-viktor-Projects-project-management`), prints the per-crate
  module-pair confusion sets and total recall/precision. The measured
  numbers (recall 20%, precision 38% over 182 LSIF / 96 CBM pairs) hold
  only when the CBM index is built from the same commit; the index that
  produced them was built 2026-09-16T23:27Z from that tree.

Full analysis with adjudicated false-positive classes:
`/Users/viktor/Projects/project-management/docs/research/codebase-memory-vs-lsif-2026-09-17.md`.
