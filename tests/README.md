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

`catalog-smoke` também usa Qt Network, com respostas simuladas sem acessar a internet. Verifica os filtros do catálogo, paginação com projetos repetidos, respostas atrasadas, categorias por tipo de conteúdo e a compatibilidade dos detalhes com o jogo de destino. Para executar somente essa verificação: `ctest --test-dir build-tests -R catalog-smoke --output-on-failure`.

`nbt-smoke` monta um `level.dat` em memória e confere o leitor de NBT (sem compressão, gzip e zlib), a recusa de dados cortados, a lista de mundos da aba Mundos (com o mundo de um servidor hospedado, achado pelo `server.properties`) e a malha 3D de um chunk sintético (a superfície da água na altura certa, e o corte de Y escondendo o que fica acima) e os blocos e a luz ao vivo de um quadro `Blocks` por cima dos salvos, que somem quando o ao vivo é desligado. Para executar somente essa verificação: `ctest --test-dir build-tests -R nbt-smoke --output-on-failure`.

`link-smoke` confere a ponta do launcher na conexão com o Omni agent: uma conexão sem o token do lançamento é derrubada, e um quadro `State` escrito como o agent escreve (`DataOutputStream` do Java), mesmo chegando em pedaços, vira jogador e entidades. Um quadro `Blocks` fica guardado pelo chunk e é esquecido quando o jogo desconecta, e um quadro `Model` é anunciado com o id do mob. Um `State` de servidor (`@localhost:25567`) só casa com um servidor deste computador naquela porta. Para executar somente essa verificação: `ctest --test-dir build-tests -R link-smoke --output-on-failure`. Para conferir fora do jogo que todos os nomes que o agent lê existem num client: `java -cp "build\omni-agent.jar;<jar do client>;<bibliotecas>" omni.agent.Check [arquivo de mappings]`.
