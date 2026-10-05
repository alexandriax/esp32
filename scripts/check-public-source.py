#!/usr/bin/env python3
"""Reject accidental local artifacts and private material in the public tree.

Gitleaks separately scans secret formats and Git history. This small check adds
project-specific path rules and home-directory privacy checks, without printing
matched content. Third-party license notices are preserved.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PRIVATE_DIRS = {'.tools', '.cache', 'artifacts', 'backups', 'build', '.aws', '.ssh'}
PRIVATE_SUFFIXES = {'.p12', '.pfx', '.p8', '.key', '.pem', '.keychain', '.keychain-db',
                    '.mobileprovision', '.provisionprofile', '.log', '.elf', '.bin'}
PRIVATE_HOME = re.compile(rb'/(?:Users|home)/(?!runner/|builduser/|example/)[A-Za-z0-9_.-]+/')
PRIVATE_KEY = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----')


def problems(name, data):
    path = Path(name)
    found = []
    if PRIVATE_DIRS.intersection(path.parts) or path.suffix.lower() in PRIVATE_SUFFIXES:
        found.append('local artifact or credential file')
    if path.name.startswith('.env') and path.name != '.env.example':
        found.append('environment file')
    if path.name in {'.DS_Store', 'feedforward-firmware', 'core'}:
        found.append('local machine artifact')
    if PRIVATE_KEY.search(data):
        found.append('private key material')
    if PRIVATE_HOME.search(data):
        found.append('personal home-directory path')
    return found


def main():
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).split(b'\0')
    failures = []
    checked = 0
    for raw in names:
        if not raw:
            continue
        name = raw.decode('utf-8')
        path = ROOT / name
        if path.is_symlink():
            failures.append((name, 'symlinks require explicit publication review'))
            continue
        if not path.is_file():
            continue
        checked += 1
        failures.extend((name, reason) for reason in problems(name, path.read_bytes()))
    for name, reason in failures:
        print(f'{name}: {reason}', file=sys.stderr)
    if failures:
        raise SystemExit(1)
    print(f'Public source guard passed: {checked} tracked files; matched values are never logged.')


if __name__ == '__main__':
    main()
