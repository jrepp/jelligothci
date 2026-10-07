---
id: adr-005
title: Keep structured project memory in docs-cms with pinned Docuchango
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [documentation, process, tooling]
project_id: jelligotchi
doc_uuid: 9bc7f841-f51c-4e4d-8788-f66e0c7ed963
---

# Context

Project decisions and implementation findings need to survive the current chat.
Both developers and coding agents need a versioned place to find that context.

# Decision

Store structured Markdown under `docs-cms/`, with ADRs for decisions, RFCs for
proposals, PRDs for requirements, and memos for findings. Use Docuchango's
generated config, schema, and templates. Track these files with the code.

Pin uv and the Docuchango PyPI package in `toolchain.env`. Use `scripts/uvx`
through `scripts/docs` to honor those pins. Bootstrap prints the setup guide;
`init` creates the documentation tree. The initial pins are uv 0.12.23 and
Docuchango 1.19.0; future upgrades follow ADR-004.

Point `AGENTS.md` at this project memory. Preserve each document's ID, UUID, and
created date. Mark new proposals as such; only record a decision as accepted
when approval is supported by the user's instructions.

Run read-only validation in CI and provide a local repair command. Review
repairs before committing. The current setup validates Markdown without a
Docusaurus website; publishing a site is not required by this decision.

# Approval basis

Jacob requested `AGENTS.md` and a docs-cms bootstrap with pinned tooling, then
clarified that the package was `docuchango`. He subsequently requested these
initial ADRs based on the session. These are explicit documentation choices.

# Consequences

- Durable decisions and findings are reviewable alongside code changes.
- Schema and link checks catch common metadata and reference mistakes.
- Authors must maintain frontmatter and validate documentation changes.
- Templates and an empty tree alone do not count as a successful validation.
- Validation checks structure; it does not establish that technical claims are
  true or that a proposed decision has been approved.

# Alternatives

Chat history alone is hard to find from a checkout. A README alone does not
separate decisions, proposals, requirements, and findings. A Docusaurus website
would add a separate build and publishing concern beyond the requested CMS.

# References

- [Documentation index](../README.md)
- [Agent instructions](../../AGENTS.md)
- [Pinned docs wrapper](../../scripts/docs)
- [Documentation CI](../../.github/workflows/docs.yml)
- [Repository-local toolchains](adr-004-repository-local-pinned-toolchains.md)
