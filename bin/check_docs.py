#!/usr/bin/env python3
"""Check local Markdown links and HTML image sources; never makes network calls."""
from pathlib import Path
import re
import subprocess
import sys
import unicodedata
from urllib.parse import unquote, urlsplit

root = Path(__file__).resolve().parents[1]
# Include new documentation without traversing downloaded tool/dependency trees.
files = [root / 'README.md', *sorted((root / 'docs').rglob('*.md')),
         root / 'cyd/README.md', root / 'experiments/radio-smoke/README.md']
# Local/private notes were never public documentation.
tracked = set(subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard'],
                                     cwd=root, text=True).splitlines())
errors = []
count = 0
for path in files:
    if path.relative_to(root).as_posix() not in tracked:
        continue
    text = re.sub(r'```.*?```', '', path.read_text(), flags=re.S)
    links = re.findall(r'!?\[[^\]]*\]\(([^\s)]+)(?:\s+[^)]*)?\)', text)
    links += re.findall(r'<img\b[^>]*\bsrc=["\']([^"\']+)', text)
    for link in links:
        parts = urlsplit(link)
        if parts.scheme or parts.netloc or not parts.path:
            continue
        target = (path.parent / unquote(parts.path)).resolve()
        count += 1
        if not target.exists():
            errors.append(f'{path.relative_to(root)}: missing {link}')
        elif parts.fragment and target.suffix == '.md':
            headings = re.findall(r'^#{1,6} (.+)$', target.read_text(), flags=re.M)
            def slug(value):
                value = re.sub(r'<[^>]*>', '', value).replace('`', '').lower()
                return ''.join(c for c in value if c in '-_ ' or unicodedata.category(c)[0] in 'LN').replace(' ', '-')
            anchors = {slug(h) for h in headings}
            if unquote(parts.fragment) not in anchors:
                errors.append(f'{path.relative_to(root)}: missing heading {link}')
for error in errors:
    print(error, file=sys.stderr)
print(f'Checked {count} local documentation links/assets; {len(errors)} missing.')
sys.exit(bool(errors))
