#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Publish a bounded, static CI canary from a completed trusted workflow run."""
import base64
import io
import json
import os
import re
import subprocess
import sys
import zipfile

from PIL import Image

MAX_ZIP = 50 * 1024 * 1024
MAX_PNG = 10 * 1024 * 1024
MAX_ENTRIES = 128
CANARY_SIZE = (1398, 1584)
MARKER = '<!-- jelli-engine-canary -->'


def api(path, method='GET', body=None, raw=False):
    args = ['gh', 'api', '--method', method, path]
    payload = None
    if body is not None:
        args += ['--input', '-']
        payload = json.dumps(body).encode()
    result = subprocess.run(args, input=payload, capture_output=True, check=True).stdout
    return result if raw else json.loads(result)


def extract_png(data):
    if len(data) > MAX_ZIP:
        raise ValueError('artifact archive exceeds size limit')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = archive.infolist()
        if len(members) > MAX_ENTRIES:
            raise ValueError('artifact contains too many files')
        entries = [entry for entry in members if entry.filename == 'canary.png']
        if len(entries) != 1 or entries[0].file_size > MAX_PNG or entries[0].file_size < 33:
            raise ValueError('artifact must contain one bounded root canary.png')
        png = archive.read(entries[0])
    try:
        with Image.open(io.BytesIO(png)) as image:
            image.verify()
        with Image.open(io.BytesIO(png)) as image:
            if image.format != 'PNG' or image.size != CANARY_SIZE:
                raise ValueError(f'canary must be a {CANARY_SIZE[0]}x{CANARY_SIZE[1]} PNG')
            image.load()
    except (OSError, SyntaxError) as error:
        raise ValueError('invalid or truncated PNG') from error
    return png


def current_pr(repo, number, sha):
    pr = api(f'repos/{repo}/pulls/{number}')
    return (pr['state'] == 'open' and pr['head']['sha'] == sha and
            pr['head']['repo'] is not None and pr['head']['repo']['full_name'].lower() == repo.lower())


def find_existing_comment(comments, run_id):
    matches = [c for c in comments if c['user']['login'] == 'github-actions[bot]' and MARKER in c['body']]
    numbered = []
    for comment in matches:
        match = re.search(r'<!-- jelli-engine-canary-run:(\d+) -->', comment['body'])
        if match:
            numbered.append((int(match.group(1)), comment))
    if numbered and max(number for number, _ in numbered) > run_id:
        return None, True
    newest = max(numbered, key=lambda item: item[0])[1] if numbered else (matches[-1] if matches else None)
    return newest, False


def main():
    repo = os.environ['GITHUB_REPOSITORY']
    event = json.load(open(os.environ['GITHUB_EVENT_PATH'], encoding='utf-8'))
    run = event['workflow_run']
    if run['event'] != 'pull_request' or run['conclusion'] != 'success' or run['head_repository']['full_name'].lower() != repo.lower():
        print('Not a successful same-repository pull request run; skipping.')
        return
    prs = run.get('pull_requests', [])
    if len(prs) != 1:
        print('Run does not identify exactly one pull request; skipping.')
        return
    number, sha, run_id = prs[0]['number'], run['head_sha'], run['id']
    if not isinstance(number, int) or not re.fullmatch(r'[0-9a-f]{40}', sha) or not current_pr(repo, number, sha):
        print('Pull request has moved or closed; skipping.')
        return
    artifacts = api(f'repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100')['artifacts']
    matches = [a for a in artifacts if a['name'].startswith('engine-evidence-') and not a['expired']]
    if len(matches) != 1 or matches[0]['size_in_bytes'] > MAX_ZIP:
        raise ValueError('expected one bounded engine evidence artifact')
    artifact = matches[0]
    png = extract_png(api(f"repos/{repo}/actions/artifacts/{artifact['id']}/zip", raw=True))
    comments = []
    for page in range(1, 101):
        batch = api(f'repos/{repo}/issues/{number}/comments?per_page=100&page={page}')
        comments.extend(batch)
        if len(batch) < 100:
            break
    existing, newer_published = find_existing_comment(comments, run_id)
    if newer_published:
        print('A newer run already published; skipping.')
        return
    # The publisher never checks out the PR or interprets its contents as code.
    blob = api(f'repos/{repo}/git/blobs', 'POST', {'content': base64.b64encode(png).decode(), 'encoding': 'base64'})['sha']
    tree = api(f'repos/{repo}/git/trees', 'POST', {'tree': [{'path': 'canary.png', 'mode': '100644', 'type': 'blob', 'sha': blob}]})['sha']
    base = api(f'repos/{repo}/git/ref/heads/main')['object']['sha']
    commit = api(f'repos/{repo}/git/commits', 'POST', {'message': f'CI canary for PR {number} {sha}', 'tree': tree, 'parents': [base]})['sha']
    if not current_pr(repo, number, sha):
        print('Pull request moved during publication; skipping.')
        return
    branch = f'ci-evidence/pr-{number}'
    # refs are per PR, and the workflow serializes publishers across all runs.
    try:
        api(f'repos/{repo}/git/refs', 'POST', {'ref': f'refs/heads/{branch}', 'sha': commit})
    except subprocess.CalledProcessError:
        api(f'repos/{repo}/git/refs/heads/{branch}', 'PATCH', {'sha': commit, 'force': True})
    image = f'https://raw.githubusercontent.com/{repo}/{commit}/canary.png'
    run_url = f'https://github.com/{repo}/actions/runs/{run_id}'
    body = f'{MARKER}\n<!-- jelli-engine-canary-run:{run_id} -->\n### Engine canary\n\nRendered from PR head `{sha}`. [Download full captures and provenance]({run_url}).\n\n![Nine engine canary scenarios]({image})'
    if not current_pr(repo, number, sha):
        print('Pull request moved before commenting; skipping.')
        return
    if existing:
        api(f"repos/{repo}/issues/comments/{existing['id']}", 'PATCH', {'body': body})
    else:
        api(f'repos/{repo}/issues/{number}/comments', 'POST', {'body': body})
    print(f'Published canary for PR {number} at {commit}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(f'Canary publisher: {error}', file=sys.stderr)
        sys.exit(1)
