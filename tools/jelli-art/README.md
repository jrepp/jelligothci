# Jelli Art

Browser pixel editor and before/after review for the slice artwork. Artist
instructions: [Paint in Jelli Art](../../docs/artwork.md#paint-in-jelli-art).
Style rules: [pixel art guide](../../docs/pixel-art-guide.md).

## Run locally

```sh
make jelli-art                      # http://127.0.0.1:8765/, edits this checkout
./scripts/uv run --python 3.12 tools/jelli-art/jelli_art.py --help
```

Local runs only write PNGs, `assets.json` bounds, and `source/hand-painted.json`.
Commit the changes yourself.

## Container

```sh
podman build -f tools/jelli-art/Containerfile -t jelli-art .     # or docker build
# Serve an existing checkout (read-only review):
podman run --rm -p 127.0.0.1:8765:8765 -v "$PWD:/work/jelligotchi:ro" jelli-art
```

| Variable | Meaning |
| --- | --- |
| `JELLI_REPO` | Checkout path inside `/work` (default `/work/jelligotchi`) |
| `JELLI_GIT_URL` | Clone URL when the checkout is missing (public HTTPS by default) |
| `JELLI_BASE_BRANCH` | Branch a new studio branch starts from (default `main`) |
| `JELLI_STUDIO_BRANCH` | Commit every save to this branch; unset means no git writes |
| `JELLI_GIT_PUSH` | `1` pushes the studio branch in the background |
| `JELLI_PUSH_URL` | SSH push URL, for example `git@github.com:jrepp/jelligothci.git` |
| `JELLI_DEPLOY_KEY` | Private key path (default `/run/secrets/jelli-art-deploy-key`) |
| `GIT_AUTHOR_NAME`, `GIT_AUTHOR_EMAIL` | Commit identity (default `Jelli Art`) |

The container listens on 8765 and answers `GET /healthz`; `healthcheck.py` is
the probe. On start, the studio branch resets to the base branch once its art
has landed there, whether by merge or squash. Unmerged art is kept.

## Versioning and releases

Jelli Art has its own version (`VERSION`, tags `jelli-art-vX.Y.Z`); see ADR-011.
Release Please opens the release PR. When it merges,
`.github/workflows/jelli-art.yml` publishes `ghcr.io/jrepp/jelli-art` as `X.Y.Z`,
`X.Y`, `X`, and `sha-<commit>`.

## Hosting

`jelli-art.home.jrepp.com` on nuc, over Tailscale, is defined in t1-hosting
(memo-039, `jelli-art/` and `scripts/jelli-art-deploy`).
