# Verificações do backend

O teste nativo usa pastas temporárias e não altera a biblioteca pessoal do usuário. Cobre gravação/reabertura, edição e remoção de jogos, identidades offline, troca de Perfil sem duplicar Jogos, nomes inválidos, importação de capas, falhas de gravação e preservação de JSON corrompido.

Com o SDK preparado, execute na raiz do projeto:

```powershell
.\scripts\build.ps1 -Test
```

O teste precisa de Qt Core/Gui, incluindo o plugin WebP para verificar esse formato; não precisa de rede, conta Microsoft nem Minecraft instalado.

Também é possível configurar apenas `tests/` em um terminal de desenvolvimento C++:

```powershell
cmake -S tests -B build-tests -G Ninja -DCMAKE_PREFIX_PATH=".tools/Qt/6.8.3/msvc2022_64"
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

O CTest prepara o caminho das DLLs Qt no Windows. Arquivos gerados ficam em `build-tests/`, fora do Git.
