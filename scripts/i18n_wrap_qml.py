"""Marks the interface strings of qml/*.qml for translation (wraps them in qsTr()).

Run once when adding screens:  python scripts/i18n_wrap_qml.py [--dry]
A literal is interface text when it reads like a phrase (has a letter and a space,
or an accented letter, or is a capitalised word). Identifiers, keys, paths, colours,
font names and strings compared against values are left alone.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
# Contexts (text right before the literal) where a string is data, not text.
SKIP_BEFORE = re.compile(
    r'(===|!==|==|!=|\bcase|indexOf\(|lastIndexOf\(|startsWith\(|endsWith\(|split\(|replace\(|play\(|setGameDefault\(|'
    r'option\(|setOption\(|brandSource\(|coverSource\(|\bname:|iconName:|source:|objectName:|font\.family:|'
    r'Qt\.locale\(|[\w\]]\[|setPreference\(|setServerProperty\(|setProp\(|prop\(|playerList\(|openFolder\(|'
    r'gameDefaults\[|\.value\(|key:|id:|value:|values:|state:|kind:|cmd:|property string \w+:|Behavior on|'
    r'console\.|Qt\.openUrlExternally\(|sendCommand\(|command\(|sequence:|category:|remove:|"\w+":)\s*$')
NAMES = {"Vanilla", "Fabric", "Forge", "NeoForge", "Modrinth", "Microsoft", "OmniLauncher", "Minecraft", "Discord",
         "Consolas", "Manrope", "Steve", "Herobrine", "Java", "Iris", "Oculus", "GitHub", "CurseForge"}


def is_text(value: str) -> bool:
    if not re.search(r'[A-Za-zÀ-ÿ]', value):
        return False
    if re.match(r'^(https?:|file:|qrc:|#|key\.|minecraft:|/|M[\d.]+ )', value):
        return False
    if value in NAMES:
        return False
    if re.search(r'[À-ÿ]', value):
        return True
    if ' ' in value.strip():
        return True
    # A lone capitalised word ("Instalar", "Jogos"), not an identifier.
    return bool(re.fullmatch(r"[A-Z][a-z]+[!.…]?", value))


def wrap(text: str):
    out, last, found = [], 0, []
    for match in LITERAL.finditer(text):
        start, end = match.span()
        value = match.group(1)
        before = text[max(0, start - 40):start]
        line_start = text.rfind('\n', 0, start) + 1
        line = text[line_start:start]
        if (before.endswith('qsTr(') or not is_text(value) or SKIP_BEFORE.search(before)
                or line.lstrip().startswith('//') or line.lstrip().startswith('import ')):
            continue
        out.append(text[last:start])
        out.append('qsTr(' + match.group(0) + ')')
        last = end
        found.append(value)
    out.append(text[last:])
    return ''.join(out), found


def main():
    dry = '--dry' in sys.argv
    total = 0
    for path in sorted((ROOT / 'qml').glob('*.qml')):
        text = path.read_text(encoding='utf-8')
        new, found = wrap(text)
        total += len(found)
        if dry:
            for value in found:
                print(f'{path.name}: {value}')
        elif new != text:
            path.write_text(new, encoding='utf-8', newline='')
    print(f'{total} strings', file=sys.stderr)


if __name__ == '__main__':
    main()
