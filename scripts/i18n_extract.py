"""Lists every translatable string (qsTr in qml/, tr/translate in src/) and checks a translation file.

    python scripts/i18n_extract.py                     # prints the strings missing from assets/i18n/en.json
    python scripts/i18n_extract.py --all               # prints every string
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATTERNS = [
    (ROOT / 'qml', '*.qml', re.compile(r'qsTr\("((?:[^"\\\n]|\\.)*)"')),
    (ROOT / 'src', '**/*.cpp', re.compile(r'(?<![\w:])tr\("((?:[^"\\\n]|\\.)*)"|translate\("OmniLauncher", "((?:[^"\\\n]|\\.)*)"')),
]
# Labels passed to tr() through a variable (Wrapped's travel modes).
TABLES = [(ROOT / 'src/core/Wrapped.cpp', re.compile(r'\{"\w+_one_cm", "((?:[^"\\\n]|\\.)*)"\}'))]


def unescape(text: str) -> str:
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), text)


def strings():
    found = {}
    for folder, pattern, regex in PATTERNS:
        for path in sorted(folder.glob(pattern)):
            for match in regex.finditer(path.read_text(encoding='utf-8')):
                value = unescape(next(g for g in match.groups() if g is not None))
                found.setdefault(value, path.name)
    for path, regex in TABLES:
        for match in regex.finditer(path.read_text(encoding='utf-8')):
            found.setdefault(unescape(match.group(1)), path.name)
    return found


def main():
    found = strings()
    existing = {}
    target = ROOT / 'assets/i18n/en.json'
    if target.exists():
        existing = json.loads(target.read_text(encoding='utf-8'))
    show_all = '--all' in sys.argv
    missing = [s for s in found if show_all or s not in existing]
    sys.stdout.reconfigure(encoding='utf-8')
    for value in missing:
        print(json.dumps(value, ensure_ascii=False))
    unused = [s for s in existing if s not in found]
    print(f'{len(found)} strings, {len(missing)} listed, {len(unused)} unused in en.json', file=sys.stderr)


if __name__ == '__main__':
    main()
