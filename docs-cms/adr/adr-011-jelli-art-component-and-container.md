---
title: Version and ship Jelli Art as its own component and container
status: Proposed
created: 2026-10-10T02:40:00Z
deciders: Jacob Repp
tags: [ci, containers, jelli-art, releases, tooling]
id: adr-011
project_id: jelligotchi
doc_uuid: 5bf7ce28-f3d3-453e-b78f-6fd69678dbc4
---

# Context

Jelli Art (`tools/jelli-art/`, `make jelli-art`) is a small web app for painting and
reviewing the slice artwork. The user asked for a Docker container, a separate
semantic version for the web app, and hosting on nuc at
`jelli-art.home.jrepp.com`, reachable over Tailscale. ADR-008 versions the whole
repository as one game release, so a change to the web app would otherwise bump
the game.

# Decision

- `tools/jelli-art` is a second Release Please component (`jelli-art`), with its
  own `VERSION`, `CHANGELOG.md`, and `jelli-art-vX.Y.Z` tags. The root game
  package excludes that path. The same Conventional Commit rules apply, so use a
  `feat(jelli-art):` or `fix(jelli-art):` scope for studio changes.
  `scripts/release-validate` checks that the component, the exclusion, and the
  version file agree.
- `tools/jelli-art/Containerfile` builds the image from the repository root. The
  base image is pinned by digest, Pillow by version, and `.dockerignore`
  allowlists the build context. The image holds only the studio code. The art is
  a git checkout on the `/work` volume, cloned on first start.
- `.github/workflows/jelli-art.yml` builds and smoke-tests the image on pull
  requests. When the component is released (called from Release Please), it
  pushes `ghcr.io/jrepp/jelli-art` tags `X.Y.Z`, `X.Y`, `X`, and `sha-<commit>`.
  It uses `docker` commands directly, so no new third-party actions need pins.
- Hosted mode sets `JELLI_STUDIO_BRANCH` and `JELLI_GIT_PUSH`. Every save is
  committed with a `Painted-by:` trailer and pushed with a write deploy key to a
  working branch (`art/studio`). Art reaches `main` only through a reviewed pull
  request. Local runs never write to git.
- Hosting on nuc is owned by t1-hosting (branch `feat/jelli-art-nuc`, memo-039):
  a rootless Quadlet following the major-version channel with
  `podman auto-update`.

# Consequences

## Positive

- Studio releases and game releases move independently, and each has its own
  changelog.
- Hosts can follow a major channel. Exact tags and SHAs remain available for
  pinning and rollback.
- Artists' work stays reviewable git history, attributed per save.

## Negative

- Changes to the shared review page (`tools/assets/compare.html`,
  `compare_slice.py`) fall under the game component, not the studio's. To ship
  one as a studio release, include a `jelli-art`-scoped change or note it in the
  release PR.
- The hosted app has no login; tailnet membership is the access boundary. The
  deploy key can push to the repository and must be limited to that use on nuc.
- The first studio release must exist (`jelli-art-v0.1.0`), and the GHCR package
  must be public or the host given pull credentials, before nuc can run it.

# Approval basis

On 2026-10-10 the user asked for a containerized web app with its own semantic
version, published to nuc over Tailscale, with saves committed and pushed to a
branch. The component layout, tag format, image name, and hosting details are
agent proposals awaiting review.
