#!/usr/bin/env python3
"""Check this repository's explicit container layout without building or importing it.

Supports literal COPY paths and the current literal-path/* dockerignore policy.
Unsupported packaging syntax fails closed rather than approximating Docker.
This checks repository dependencies, not third-party packages or runtime behavior.
"""
import ast
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import sys


class PackagingError(ValueError):
    pass


def context_rules(root):
    rules = []
    for line in (root / '.dockerignore').read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        include = line.startswith('!')
        pattern = line.lstrip('!').strip('/')
        if pattern != '*' and (any(c in pattern for c in '*?[]\\') or '..' in pattern.split('/')):
            raise PackagingError(f'Unsupported .dockerignore rule: {line}; extend the checker before using it')
        rules.append((pattern, include))
    return rules


def included(path, rules):
    result = True
    for pattern, include in rules:
        if pattern == '*' or path == pattern or path.startswith(pattern + '/'):
            result = include
    return result


def image_files(root, instructions, rules):
    files = {}
    for instruction in instructions:
        if not instruction.upper().startswith('COPY '):
            continue
        args = shlex.split(instruction)[1:]
        if len(args) < 2 or any(c in arg for arg in args for c in '*?[]$') or any(arg.startswith('--') for arg in args):
            raise PackagingError(f'Unsupported COPY syntax: {instruction}')
        destination = PurePosixPath(args[-1])
        if not destination.is_absolute() or not args[-1].endswith('/'):
            raise PackagingError('COPY destinations must be absolute directories ending in /')
        for name in args[:-1]:
            relative = PurePosixPath(name)
            if relative.is_absolute() or '..' in relative.parts:
                raise PackagingError(f'COPY source escapes context: {name}')
            source = root / name
            if not source.exists():
                raise PackagingError(f'COPY source does not exist: {name}')
            candidates = sorted(source.rglob('*')) if source.is_dir() else [source]
            copied = 0
            for path in candidates:
                if not included(path.relative_to(root).as_posix(), rules):
                    continue
                if path.is_symlink():
                    raise PackagingError(f'COPY symlink requires explicit checker support: {path}')
                if not path.is_file():
                    continue
                target = destination / (path.relative_to(source) if source.is_dir() else path.name)
                files[target] = path
                copied += 1
            if not copied:
                raise PackagingError(f'COPY source excluded or empty: {name}')
    return files


def check(root):
    instructions = [line.strip() for line in re.sub(r'\\\n', ' ', (root / 'tools/jelli-art/Containerfile').read_text()).splitlines()
                    if line.strip() and not line.lstrip().startswith('#')]
    files = image_files(root, instructions, context_rules(root))
    errors = []
    if sum(line.upper().startswith('FROM ') for line in instructions) > 1:
        raise PackagingError('Multi-stage builds require explicit checker support')
    for command in ('ENTRYPOINT ', 'HEALTHCHECK '):
        if sum(line.upper().startswith(command) for line in instructions) != 1:
            errors.append(f'Expected exactly one {command.strip()} instruction')
    # These are the server's script directory and shared-assets import search path.
    search = [PurePosixPath('/app/tools/jelli-art'), PurePosixPath('/app/tools/assets')]
    local = {p.stem: p for folder in ('tools/jelli-art', 'tools/assets') for p in (root / folder).glob('*.py')}
    for target, source in files.items():
        if source.suffix != '.py' or source.name.startswith('test_'):
            continue
        tree = ast.parse(source.read_text(), filename=str(source))
        for node in ast.walk(tree):
            names = [a.name for a in node.names] if isinstance(node, ast.Import) else (
                [node.module] if isinstance(node, ast.ImportFrom) and not node.level and node.module else [])
            for name in names:
                module = name.split('.')[0]
                if module in local and not any(folder / f'{module}.py' in files for folder in search):
                    errors.append(f'{source.relative_to(root)}:{node.lineno}: local import {module!r} missing from image; check COPY and .dockerignore')
        # The server bundles literal browser files through HERE / "file.js".
        for node in ast.walk(tree):
            if (isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div)
                    and isinstance(node.left, ast.Name) and node.left.id == 'HERE'
                    and isinstance(node.right, ast.Constant) and isinstance(node.right.value, str)
                    and node.right.value.endswith(('.js', '.html', '.css'))
                    and target.parent / node.right.value not in files):
                errors.append(f'{source.relative_to(root)}:{node.lineno}: bundled file missing: {node.right.value}')
    for instruction in instructions:
        if instruction.upper().startswith(('ENTRYPOINT ', 'HEALTHCHECK ')):
            match = re.search(r'\[.*\]', instruction)
            if not match:
                raise PackagingError(f'Expected JSON command: {instruction}')
            for arg in json.loads(match.group()):
                if isinstance(arg, str) and arg.startswith('/app/'):
                    source = files.get(PurePosixPath(arg))
                    if source is None:
                        errors.append(f'Container command file missing: {arg}')
                    elif instruction.startswith('ENTRYPOINT ') and not source.stat().st_mode & 0o111:
                        errors.append(f'Entrypoint is not executable: {arg}')
    for required in ('/app/tools/assets/compare.html', '/app/tools/jelli-art/jelli_art.py'):
        if PurePosixPath(required) not in files:
            errors.append(f'Required studio file missing from image: {required}')
    if errors:
        raise PackagingError('\n'.join(sorted(set(errors))))
    return len(files)


def main():
    try:
        count = check(Path(__file__).resolve().parent.parent)
    except (PackagingError, OSError, SyntaxError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f'Studio container layout valid ({count} files; no container build)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
