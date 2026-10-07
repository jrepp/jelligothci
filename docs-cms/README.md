# Jelligotchi project memory

This docs-cms stores technical decisions, proposals, requirements, and findings
alongside the code. Start with [memo-001: Shapes MVP foundation and validation
status](memos/memo-001-shapes-mvp-foundation.md).

## Architecture decisions

These records capture explicit decisions from the 2026-10-07 project session.
Their Accepted status records the user's direction, not hardware verification.

| Record | Decision |
| --- | --- |
| [ADR-001](adr/adr-001-portable-c-core-and-host-adapters.md) | Share a portable C core across SDL and ESP32 hosts |
| [ADR-002](adr/adr-002-inject-time-and-keep-pacing-in-hosts.md) | Inject time; let hosts pace frames |
| [ADR-003](adr/adr-003-start-with-a-shapes-mvp.md) | Limit the first MVP to simple shapes |
| [ADR-004](adr/adr-004-repository-local-pinned-toolchains.md) | Keep pinned tools local and generated artifacts out of Git |
| [ADR-005](adr/adr-005-docs-cms-with-pinned-docuchango.md) | Use docs-cms and pinned Docuchango for project memory |
| [ADR-006](adr/adr-006-validate-core-portability-in-ci.md) | Build and test the core across desktop platforms in CI |
| [ADR-007](adr/adr-007-bounded-c-and-enforced-quality-checks.md) | Bound C resources and enforce quality checks |
| [ADR-008](adr/adr-008-semantic-versioning-and-release-validation.md) | Use Conventional Commits, Release Please, and release validation |

## Layout

```text
docs-cms/
├── docs-project.yaml         # Project metadata and validation rules
├── docs-project.schema.json  # Config schema for editors and agents
├── adr/                      # Architecture decisions
├── rfcs/                     # Proposals for discussion
├── memos/                    # Findings and status notes
├── prd/                      # Product requirements
└── templates/                # Starting point for each document type
```

## Write a document

1. Choose a type and copy its file from `templates/` into the matching folder.
2. Use the next available number and a descriptive slug, such as
   `memo-002-display-bring-up.md`; set `id: memo-002` to match.
3. Fill in the title, author or deciders, created date, and tags. Keep
   `project_id: jelligotchi` and generate a fresh UUID v4 for `doc_uuid`.
4. Replace the sample prose and links with content relevant to the project.
5. Run validation and review the diff before committing.

Use `Proposed` for a new ADR and `Draft` for a new RFC or PRD. Memos do not need
status. Record approval only when it has actually happened. Keep the UUID and
created date stable; later updates can be found in Git history.

## Validate

Run these commands from the repository root:

```sh
make docs-check  # read-only report
make docs-fix    # apply available repairs, then review the diff
make docs-guide
./scripts/docs bootstrap --guide agent
```

From this directory, use `../scripts/docs validate --dry-run --skip-build`.
The wrapper finds the repository root regardless of your current directory.

The [toolchain pins](../toolchain.env) select uv 0.12.23 and Docuchango 1.19.0.
The [docs wrapper](../scripts/docs) uses the local uvx wrapper to run the pinned
package. Downloaded binaries, Python tool environments, and caches stay under
ignored `.tools/`. No global Python package installation is needed.

Validation checks document metadata, IDs, links, code blocks, and formatting.
Repair runs are atomic by default: if any unresolved issue remains, all fixes
from that run are withheld. Resolve the reported issues and run again. CI runs
only the read-only check. The check must scan real documents; an empty tree
should not silently pass.

This repository currently uses the Markdown CMS without a Docusaurus site.
`--skip-build` skips that optional website check, not document validation.

## Configuration and references

Edit [docs-project.yaml](docs-project.yaml) to change metadata or document
folders. Its [JSON schema](docs-project.schema.json) supports editor validation.
Keep new durable findings here and update the [root README](../README.md) when
setup or user commands change.

- [Docuchango on PyPI](https://pypi.org/project/docuchango/1.19.0/)
- [Docuchango source](https://github.com/jrepp/docuchango)
- [uv tool execution](https://docs.astral.sh/uv/concepts/tools/)
