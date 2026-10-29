"""Marks user-facing Portuguese messages in src/ for translation: QStringLiteral("…") → tr("…").

Run once when adding messages:  python scripts/i18n_wrap_cpp.py [--dry]
Code outside a QObject member (free functions) uses QCoreApplication::translate("OmniLauncher", …)
instead; the compiler points those out.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FILES = ['src/app/AppModel.cpp', 'src/core/GameRunner.cpp', 'src/core/ModManager.cpp', 'src/core/AuthManager.cpp',
         'src/core/UpdateManager.cpp', 'src/core/ServerManager.cpp', 'src/core/PackCloud.cpp', 'src/core/Wrapped.cpp',
         'src/core/DiscordPresence.cpp', 'src/core/Downloader.cpp']
LITERAL = re.compile(r'QStringLiteral\("((?:[^"\\\n]|\\.)*)"\)')
PORTUGUESE = re.compile(r'[À-ÿ]|\b(não|de|do|da|dos|das|um|uma|o|a|os|as|em|no|na|com|para|por|seu|sua|jogo|mundo|servidor|'
                        r'Baixando|Lendo|Buscando|Instalando|Preparando|Enviando|Publicando|Copiando|Criando|Iniciando|'
                        r'Abrindo|Verificando|Pronto|Carregando|Salvando)\b', re.IGNORECASE)


def is_message(value: str) -> bool:
    if not re.search(r'[A-Za-zÀ-ÿ]', value) or ' ' not in value.strip() and not re.search(r'[À-ÿ]', value):
        return False
    if re.match(r'^(https?:|file:|[-/]|\$|\*|%\d$)', value) or 'OmniLauncher/' in value:
        return False
    return bool(PORTUGUESE.search(value))


def main():
    dry = '--dry' in sys.argv
    total = 0
    for name in FILES:
        path = ROOT / name
        text = path.read_text(encoding='utf-8')
        found = []

        def replace(match):
            value = match.group(1)
            if not is_message(value):
                return match.group(0)
            found.append(value)
            return 'tr("' + value + '")'

        new = LITERAL.sub(replace, text)
        total += len(found)
        if dry:
            for value in found:
                print(f'{name}: {value}')
        elif new != text:
            path.write_text(new, encoding='utf-8', newline='')
    print(f'{total} strings', file=sys.stderr)


if __name__ == '__main__':
    main()
