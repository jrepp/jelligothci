# Builds and releases

Run commands from the repository root unless stated otherwise.

## Versions and releases

This project follows the [auth project](https://github.com/jrepp/auth) pattern
(no local sibling checkout is required): Conventional Commits feed Release
Please, which opens a release PR updating `VERSION`, `CHANGELOG.md`, and
`.release-please-manifest.json`. `fix:` means patch, `feat:` means minor, and
`!` or a `BREAKING CHANGE:` footer means major. These rules also apply at 0.x.
Use the same convention for PR titles, since squash merges become commits.

Merging the release PR creates `vX.Y.Z` and a GitHub Release. The reusable
release workflow validates metadata, builds/tests the core on all three desktop
platforms, runs SDL sanitizers and C/docs checks, then packages macOS and Windows x64 game ZIPs with bundled SDL, all authored PNGs, manifest, preview page, and hot-reload
tools. It also publishes an editable source-art ZIP, standalone
preview HTML (also zipped), source archive, and `SHA256SUMS`. Game archives name
the actual build architecture. Packaging verifies each source PNG byte-for-byte
and requires all nine unique present images before publishing. Builds are ad-hoc signed, not Apple notarized;
macOS may require approval in Privacy & Security. Releases never flash hardware.
Opening the app starts an unsaved session; use the bundled CLI with `--save` for persistence.

To reproduce packages on macOS after `make test`, build the preview with
`./scripts/uv run --python 3.12 tools/assets/build_slice.py`, then run
`python3 scripts/package-release.py`. Downloads appear in `build/release/`.
Manual publication also accepts an existing `dev-` tag for development releases,
without changing the Release Please-managed semantic version.
The release record is created before checks finish; verify the release workflow
is green and its assets are present before using a release.

```sh
./scripts/release-validate  # metadata plus core CMake build/test
# Revalidate a candidate; omit publish to avoid changing release assets.
gh workflow run release.yml -f ref=main
# Retry asset publication for an existing release after investigating failure.
gh workflow run release.yml -f ref=v0.1.0 -F publish=true
```

The default GitHub token cannot trigger CI through bot-created PR/tag events.
The automation explicitly dispatches release-PR validation and calls release
validation after tagging, without a stored personal access token. The repository
must allow GitHub Actions to create pull requests. CMake and ESP-IDF both read
`VERSION`; generated build metadata therefore follows the release version.

Private Linux core jobs install the pinned CMake package through repository-local
uv. The shared runner image provides the compiler and Make.

External workflow actions use full commit SHAs with release-version comments.
Hosted OS labels are explicit: Ubuntu 24.04, macOS 15, Windows 2025. GitHub still
updates those hosted images; these are OS selections, not immutable VM images.
`python3 scripts/check-workflow-pins.py` rejects floating actions and OS aliases.
