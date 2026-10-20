---
title: Generated asset translation unit final newline regression
author: Codex
created: 2026-10-20
tags: [assets, ci, memo, validation]
id: memo-084
project_id: jelligotchi
doc_uuid: e20b558d-f6b6-4a6f-8d06-5d5baccb6883
---

# Observation

PR #73 at `cecb303` failed the macOS release `package` job in
[run 39470645823](https://github.com/jrepp/jelligothci/actions/runs/39470645823).
The compiler rejected the generated `jelli_assets.c` with `-Werror,-Wnewline-eof`.
Appending the scene table made its count declaration the final chunk, and that
chunk did not carry a trailing newline. Other reported checks, including the
Windows package and firmware profiles, passed on this revision.

# Remedy and verification

[The asset generator](../../tools/assets/embed_slice.py) now terminates the complete
translation unit explicitly rather than relying on the last contributing section.
Keep this responsibility at the output writer when adding future generated sections;
do not relax compiler warnings to accommodate malformed generated text.

[The scene suite](../../tools/jelli-art/test_scenes.py) now generates the actual asset
translation unit and checks its final byte. This test failed before the fix (`;`
instead of newline) and passed afterward; all nine scene tests passed. A fresh local
Release configuration and full build using the package job's CMake commands passed.
An explicit `cc -std=c11 -Wnewline-eof -Werror -fsyntax-only` check of the generated
translation unit also passed. No runtime behavior or firmware interfaces changed.

These results establish local regression coverage and compilation. Confirmation of
the replacement macOS package job remains pending until CI runs the corrected commit.
No release assets were published or physical device checks performed by this fix.
