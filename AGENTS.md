# Agent guidance

Keep work within the user's requested increment. A draft RFC describes a
proposal; it does not authorize every proposed system or approve gameplay
defaults. See [README.md](README.md) for implemented features and
[CONTRIBUTING.md](CONTRIBUTING.md) for setup and validation.

## Project skills

- For C changes, use [jelli-safe-c](.agents/skills/jelli-safe-c/SKILL.md).
- For firmware, board integration, or hardware work, use
  [jelli-embedded-development](.agents/skills/jelli-embedded-development/SKILL.md).
- For engine/host contracts, see [docs/development.md](docs/development.md).
- For repository settings, access, recovery, and release operations, see
  [memo-005](docs-cms/memos/memo-005-contributor-and-maintainer-handoff.md).

Load only the skill relevant to the change. These skills apply to code work,
not ordinary documentation edits.

## Project memory and documentation

Use [docs-cms](docs-cms/README.md) as durable project memory. Before changing
architecture or development workflow, read the relevant ADRs, RFCs, PRDs, and
memos. Treat dated measurements as historical evidence; rediscover current
ports and host availability before device work.

For build/release, runner onboarding, or hardware bring-up, read the relevant
sections and linked evidence in [memo-004](docs-cms/memos/memo-004-process-learnings-and-context-remediation.md);
do not load its full history by default.

Record decisions as ADRs, proposals as RFCs, requirements as PRDs, and findings
as memos. Follow the templates and metadata rules in the docs-cms guide. Mark
proposals `Proposed` or `Draft` as appropriate, record approval only after the
user approves, and preserve a document's UUID and created date when editing it.

Follow the [source-grounded documentation rules](CONTRIBUTING.md#keep-documentation-grounded-in-source):
link current implementation values and catalogs to their source, while retaining
numbers that express requirements, contracts, examples, or measured evidence.
After documentation changes, run `make docs-check`, then `make docs-fix` and
review its repairs. Report unresolved findings.

After a failure or deployment, record the observation, remedy, verification, and
remaining uncertainty in the relevant memo. Put reusable guidance in the owning
document or skill and link to the evidence. Report build, host-test, and physical
display/touch evidence separately; serial startup or a successful firmware build
does not establish physical behavior.

## Scope and validation

Use the checks for the changed layer described in
[CONTRIBUTING.md](CONTRIBUTING.md#make-and-review-a-change). Keep architecture,
ownership, and platform details in [docs/development.md](docs/development.md)
and the project skills rather than duplicating their full contracts here.

For Jelli Art authoring or PR failures, follow the
[studio packaging and PR validation guidance](CONTRIBUTING.md#jelli-art-packaging-and-pr-validation).
Inspect the latest run for the current PR revision; local passes and failures
reproduced on the base do not establish that the PR's required checks pass.

## Progress notifications

When a planned stage of multi-step work completes (a design approved, a PR
merged, a release deployed) or is blocked waiting on the owner, post a short
notice to the owner's [ntfy](https://ntfy.sh) topic:

```bash
curl -s -H "Title: <stage> done" -d "<what landed, links, what's next>" \
  https://ntfy.sh/jelli-roll-call
```

The topic is public, so never include secrets, tokens, or private host details.
