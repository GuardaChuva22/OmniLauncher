# Aba Mundos e Omni agent: plano

> Plano escrito em 30 de outubro de 2026, antes da implementação. Ele descreve a aba **Mundos** (ver os mundos do jogador como uma maquete inclinada, ao vivo quando o jogo está aberto) e o **Omni agent**, que roda dentro do jogo sem depender de mod ou loader e depois também vai servir de base para um client in-game do Omni.

## Objetivo

Uma aba **Mundos** na Sidebar. Nela o jogador escolhe um mundo de qualquer jogo da biblioteca e vê uma visão isométrica, navegável, do terreno, dos mobs e do próprio player.

- **Com o jogo fechado:** o mundo como foi salvo, completo.
- **Com o jogo aberto:** o mapa se atualiza ao vivo. Os blocos mudam na hora, e os mobs e o player andam.
- **Leve de verdade:** com nada mudando, o mapa não gasta CPU nem GPU. A UI do launcher nunca trava.
- **Funciona no vanilla:** nada de mod, nada na pasta `mods`, e o jogador não instala nada.

## Decisões tomadas

| Decisão | Motivo |
|---|---|
| A base é ler os arquivos do mundo do disco | Funciona com o jogo fechado, em qualquer instância e sem código no jogo |
| O visual é feito de **tiles oblíquos 2.5D** pré-desenhados, e não de um renderer 3D com voxels | É bem mais leve: cada tile é uma `Image` no QML. A inspiração é o [Diorama](https://github.com/SunkenInTime/diorama) (MIT). Uma visão 3D livre fica como extra opcional |
| O modo ao vivo usa um **Java agent** (`-javaagent`) injetado pelo launcher | Funciona no vanilla e com qualquer loader, com um build só. Um mod por loader e por versão foi descartado |
| O agent é uma **plataforma** (core + módulos), não um gancho só para o mapa | O mesmo agent vai hospedar o client in-game do Omni |
| Suporte inicial: **1.18+**, agent compilado para **Java 17** | É o mesmo formato de mundo do leitor de `.mca`. Deixa de fora o pré-1.17 (Java 8) e o formato antigo de chunk |

Opções descartadas: um mod OmniLink por loader (custo de manutenção), um proxy de rede (só serve para multiplayer e o protocolo muda a cada versão), e rodar o mundo num servidor dedicado local (muda a experiência do singleplayer).

## Os três estados do mapa

| Estado | Fonte | O que mostra |
|---|---|---|
| Jogo fechado | arquivos do disco | mundo completo, mobs e player parados na posição salva |
| Jogo aberto, sem agent | arquivos + `QFileSystemWatcher` | atualiza a cada autosave (~5 min) e quando o jogador sai do mundo |
| Jogo aberto, com agent | agent | tudo ao vivo |

A troca entre os estados é automática. Se o agent não liga ou não reconhece a versão, o mapa fica no modo arquivo e nada quebra.

---

## Andamento

- **Fase 1: pronta.** `Nbt.{h,cpp}`, `Worlds.{h,cpp}` e `qml/WorldsPage.qml`.
- **Fase 2: pronta.** Maquete oblíqua por região (`WorldMap.{h,cpp}`, `WorldTiles.h`, `qml/WorldMapView.qml`) com 4 rotações (botões ou Q/E), pan, zoom, corte de Y para ver cavernas, dimensões, spawn, jogador e mobs salvos (vermelho para monstros). As cores saem do jar do cliente e de cada jar de mod, lidos direto do zip, com o tom de cada bioma. O vanilla sempre vence quando um mod redefine um bloco do jogo. Tudo fica em cache: `cache/worlds/palette` (um arquivo por jar) e `cache/worlds/tiles`. Os mundos 26.x guardam as dimensões em `dimensions/<ns>/<nome>/region`, o jogador em `players/data/<uuid>.dat`, estados de bloco como `{id, properties}` e listas de tipos misturados como compounds `{"": valor}`, e tudo isso já é lido. Ficou de fora: o rosto da skin no marcador do jogador e o ícone de cada mob.
- **Fase 3: pronta.** `Worlds::watch` observa a pasta da dimensão e cada `.mca`/`.dat` do mundo aberto (no Windows a observação da pasta não vê edições nos arquivos), espera 2 s as gravações em rajada assentarem e avisa `worldChanged`; enquanto um jogo roda também compara datas a cada 10 s, e confere uma última vez quando o jogo fecha. O mapa recarrega só as regiões cujo carimbo mudou (a região inteira é redesenhada, não só o chunk) e os mobs; fora da tela, recarrega quando volta. Não testado ainda com um jogo aberto de verdade.
- **Fase 4: pronta, falta testar com o jogo aberto.** O agent fica em `agent/src/omni/agent/` (`Agent`, `Names`, `Link`, `Telemetry` e `Check`). Ele é compilado pelo CMake com `add_jar` (`--release 17`) em vez de Gradle, e vai para `build/omni-agent.jar` e para o pacote. A CI instala um JDK para isso; sem JDK, o launcher é compilado sem o agent. Ele só lê, por reflexão, sem ASM: a cópia é feita na thread do jogo, entregue como tarefa ao `Minecraft`, que é um `Executor`, então não precisa de gancho. Por isso a camada `bridge` ainda não foi necessária. O protocolo ficou com `Hello`, `Status` e `State` (jogador e entidades juntos, 10 vezes por segundo, num raio de 160 blocos, e só quando algo muda). O launcher (`OmniLink`) escuta só em 127.0.0.1, gera um token novo a cada lançamento, baixa o `client_mappings` das versões vanilla ofuscadas e injeta o `-javaagent` no vanilla, no Fabric e no NeoForge; o Forge antigo fica de fora. Uma instância com nomes próprios (Fabric 1.21.1) não acha a classe do jogo, e o agent desiste depois de 3 min. O `"liveMap": false` nas configurações desliga o agent. `Check` confirmou todos os nomes no 26.3, no 1.21.1 e no 1.20.1. O mapa mostra "Ao vivo" e usa o jogador e os mobs do agent quando o jogo aberto está nesse mundo.
- **Mapa em 3D de verdade (troca da maquete da fase 2): primeira etapa pronta.** A maquete oblíqua em imagens deu lugar a um renderizador na GPU (`WorldView3D.{h,cpp}`, um `QQuickRhiItem` com `shaders/world.{vert,frag}`), com câmera isométrica ortográfica de giro livre (botão direito ou Q/E). Os blocos são montados com os modelos do próprio jogo (`BlockModels.{h,cpp}`: blockstates com variantes e multipart, a cadeia de `parent`, elementos girados, tinta de bioma e texturas num atlas com mipmaps). `ChunkMesher.{h,cpp}` gera a malha de cada chunk em threads, esconde faces encostadas em blocos opacos e pula cavernas fechadas. Água e vidro ficam numa passada translúcida. Baús, camas e caixas de shulker são caixas da textura de partícula. `WorldData.{h,cpp}` reúne a leitura de regiões e jars; `WorldTiles.h` e o cache de tiles saíram. Os saves 26.x omitem as propriedades que estão no padrão (`"minecraft:grass_block"` em vez de `snowy=false`), e o leitor completa com o valor padrão provável. A segunda etapa também está pronta. A luz vem da que o jogo salvou em cada seção (`SkyLight` e `BlockLight`). Uma seção sem luz salva conta como céu aberto acima do chão e escuro abaixo. Cada canto de uma face junta a luz das quatro células à frente (luz suave) com a oclusão de ambiente; a luz de tochas é mais quente que a do céu. Com um corte de Y, a luz mínima sobe para as cavernas ficarem legíveis. A tinta de bioma é a média das 5×5 colunas em volta, como no jogo. Cada malha agora olha os 8 chunks vizinhos. Ainda falta: modelos de mobs.
- **Fase 5, primeira parte: blocos ao vivo, pronta, falta testar com o jogo aberto.** `Blocks.java` no agent, ainda sem ASM: na mesma tarefa da thread do jogo que copia o jogador, ele percorre os chunks a até 6 chunks do jogador. Cada chunk vai para o launcher uma vez quando entra no alcance e de novo sempre que um bloco nele muda. A mudança é vista pela marca `unsaved` do chunk, que o jogo liga a cada `setBlockState`; no cliente ninguém salva, então o agent desliga a marca depois de olhar. São no máximo 2 chunks por tarefa, os alterados primeiro. O quadro `Blocks` (tipo 4) leva a paleta de cada seção como o jogo escreve o estado (`Block{minecraft:oak_log}[axis=y]`) e 4096 índices. O `OmniLink` guarda os chunks do mundo aberto (até 2048, por dimensão) e o `WorldView3D` os põe por cima dos salvos (`ChunkSource::setLive`), mantendo biomas e luz do save. Acima da superfície nova, tudo é céu aberto. Cada chunk é refeito junto com os 4 vizinhos. Quando o jogo fecha, o mapa volta para o que foi salvo. `Names` agora acha métodos com parâmetros nos mappings, e o `Check` confirmou os nomes no 26.3, no 1.21.1 e no 1.20.1. Ainda falta: luz ao vivo (uma tocha posta agora só acende depois do save) e o resto da fase 5.
- **Fase 5, segunda parte: luz ao vivo e modelos reais dos mobs, prontos, falta testar com o jogo aberto.** Cada seção do quadro `Blocks` agora leva a luz do céu e dos blocos como o jogo tem (`LevelLightEngine`, por seção: nenhuma, um nível só, ou os 2048 bytes). O que o jogo não tem (céu aberto, quase sempre) vem do save ou da superfície. Um chunk alterado e os 8 vizinhos são mandados de novo 1,5 s depois, quando a luz já se espalhou. Os mobs: o novo `Models.java` pega o renderer de cada tipo de mob na primeira vez que o vê, percorre as partes do modelo em repouso (posição, giro z-y-x e escala de cada parte) e lê a textura pelos recursos do jogo, então resource packs e mobs de mod saem como no jogo. Vai num quadro `Model` (tipo 5), um por tipo por conexão. O launcher guarda em `cache/worlds/entities` (o mapa usa mesmo com o jogo fechado), junta as texturas num atlas próprio e desenha cada mob virado pelo yaw do corpo, que agora vem no `State` (protocolo 2) e no save (`Rotation`). Tipo sem modelo continua caixa. Testado fora do jogo: uma vaca do 26.3 montada com a mesma lógica do agent aparece em pé e texturizada no mapa; o `Check` passa no 26.3, 1.21.1 e 1.20.1. Também: a crafting table do visualworkbench (`visualworkbench:minecraft/crafting_table`, sem assets no jar) agora usa o modelo da vanilla.
- **Mundos dos servidores hospedados no launcher.** A aba Mundos lista também o mundo de cada servidor de `servers.json`, na pasta que o `server.properties` indica (`level-name`), com o selo "Servidor" e o nome do servidor. Os blocos vêm do jogo de onde o servidor foi criado (mesmos mods e resource packs) ou, sem ele, dos mods do próprio servidor; o `WorldView3D` ganhou a propriedade `instance` para isso. Ao vivo: fora do singleplayer, o agent manda no `State` o endereço em que o jogo entrou (`@localhost:25567`), e o mapa liga o ao vivo quando o endereço é deste computador (localhost, loopback ou um IP das placas de rede) e a porta é a do servidor. Com o servidor rodando, o mapa salvo também confere as datas dos arquivos de tempos em tempos. Servidores de outras pessoas ficam de fora, porque o mundo não está neste PC. Ainda falta: "Ir ao jogador" com o jogo fechado usa só o `level.dat`, e num servidor a posição do jogador fica em `playerdata/`.
- **O mapa não descarrega mais o que já fez.** Antes, um chunk a mais de 4 chunks da tela era jogado fora, e voltar o fazia de novo. Agora o mapa faz também uma faixa de 8 chunks em volta da tela (depois do que está nela) e guarda tudo o que já fez, onde quer que a câmera vá. Só solta os mais longe da tela quando passa de 3,2 milhões de quads na GPU (uns 300 MB). Um chunk guardado com outro corte de altura é jogado fora, para não mostrar blocos errados.

## Fase 1: aba Mundos

**Entrega:** um item novo na Sidebar e uma `qml/WorldsPage.qml` que lista os mundos de todas as instâncias.

- Percorrer `instances/<id>/saves/*`. O [Wrapped.cpp](../src/core/Wrapped.cpp) (perto da linha 573) já percorre essas pastas e pode ser reaproveitado.
- Ler o `level.dat` (NBT com gzip): `LevelName`, seed (`WorldGenSettings.seed`, ou `RandomSeed` em versões antigas), `GameType`, `LastPlayed`, `Version.Name` e `hardcore`.
- Mostrar o `icon.png` do mundo quando existir.
- Escrever um leitor de NBT pequeno em C++ (`src/core/Nbt.{h,cpp}`) que vai ser reaproveitado na fase 2. Para descompactar, usar zlib ou miniz. O `qUncompress` também serve se o cabeçalho for ajustado.
- Seguir o padrão de i18n do projeto (`assets/i18n/*.json`) e o visual das outras páginas.

**Pronto quando:** a lista mostra os mundos reais de várias instâncias, com nome, ícone, modo de jogo e data, e o teste smoke do leitor de NBT passa em `tests/`.

## Fase 2: leitor de `.mca` + tiles oblíquos

**Entrega:** abrir um mundo da lista mostra o mapa inclinado, com pan, zoom e rotação de 90 em 90°.

- Leitor de Anvil (`region/r.X.Z.mca`): a tabela de offsets de 4 KiB, cada chunk comprimido com zlib (tipo 2), às vezes com gzip ou sem compressão.
- Formato **1.18+**: `sections[]` com `block_states` (uma `palette` mais um array `data` de longs empacotados) e `biomes`, com Y de -64 a 320. Rejeitar formatos mais antigos com uma mensagem clara.
- **Cor por bloco:** calcular uma vez a cor média de cada textura de bloco a partir do `.jar` do cliente que o launcher já baixou, e guardar o resultado em cache por versão. Blocos de mods sem textura conhecida recebem uma cor padrão.
- **Tiles oblíquos:** cada tile (por exemplo, 1 região ou 1 grupo de chunks) é desenhado num worker (`QThreadPool`) como uma imagem em projeção oblíqua, com sombreamento por altura, faces de penhascos e sombras. O desenho tem 4 orientações, uma para cada rotação de 90°.
- Cache dos tiles no disco, separado por mundo e dimensão, e invalidado pelo timestamp do chunk.
- **Corte de Y** (slider de altura) para ver cavernas, e um seletor de dimensão (Overworld, Nether, End).
- Os arquivos são abertos só para leitura. Um chunk corrompido ou pela metade (o jogo pode estar gravando) é ignorado, e a leitura é tentada de novo depois.
- **Entidades no modo arquivo:** ler `entities/*.mca` (1.17+) e desenhar os mobs na posição salva. O player vem do `level.dat` (`Data.Player`) ou de `playerdata/<uuid>.dat`, e aparece com o rosto da skin (o `ProfileAvatar` já tem).
- O mapa só redesenha quando algo muda, e para tudo quando a aba não está visível.

**Pronto quando:** um mundo 1.18+ grande abre suave, sem travar a UI, com pouca memória, e a rotação, o zoom e o corte de Y funcionam.

## Fase 3: atualização pelos arquivos

- `QFileSystemWatcher` em `region/`, `entities/` e no `level.dat` do mundo que está aberto na tela.
- Quando um arquivo muda, reler só os chunks cujo timestamp mudou e redesenhar só os tiles afetados.

**Pronto quando:** com o jogo aberto, depois de um autosave (ou de sair do mundo), o mapa reflete as mudanças sem precisar reabrir a aba.

## Fase 4: Omni agent v0 (prova da ligação jogo → launcher)

**Entrega:** com o jogo aberto pelo launcher, o mapa mostra o player e os mobs andando ao vivo. Nesta fase o agent só roda no vanilla, só lê e não usa ASM.

### Estrutura

Projeto Gradle/Java em `agent/` neste repositório. O jar vai junto com o launcher.

```
omni-agent.jar
├─ core
│  ├─ bootstrap   → descobre a versão e o ambiente (vanilla / Fabric / NeoForge / Forge)
│  ├─ names       → resolve nomes de classes e métodos para esse ambiente
│  ├─ hooks (ASM) → poucos ganchos, cada um isolado e opcional (a partir da fase 5)
│  ├─ bridge      → injeta o código dos módulos no classloader do jogo
│  └─ link        → conexão com o launcher (127.0.0.1 + token)
└─ modules
   ├─ telemetry   → mapa ao vivo
   └─ client      → HUD, overlay e UI in-game (fase 6)
```

### Lado do launcher

- O [GameRunner.cpp](../src/core/GameRunner.cpp) monta os argumentos da JVM perto da linha 878 (`QStringList arguments{"-cp", ...}`). Quando o "Mapa ao vivo" estiver ligado, colocar antes do `-cp`:
  `-javaagent:<caminho>/omni-agent.jar=port=<p>,token=<t>,mappings=<arquivo>`
- Uma classe nova `src/core/OmniLink.{h,cpp}` com um `QTcpServer` só em `127.0.0.1`. Ela gera um **token aleatório a cada vez que o jogo abre** e recusa qualquer conexão sem esse token.
- O protocolo é binário, com mensagens enquadradas, versionado e nos dois sentidos: `Hello{version, env}`, `PlayerState`, `EntityBatch`, `ChunkData`, `BlockDelta` e, mais tarde, comandos do launcher para o jogo.
- O launcher baixa e passa o mapeamento certo de nomes (veja "Nomes por ambiente" abaixo).

### Regras do agent

- **Nunca derrubar o jogo:** capturar `Throwable` em tudo. Um transformer que falha devolve a classe sem mudança, e um gancho que não acha o alvo se desliga e avisa o launcher.
- **Não travar a thread do jogo:** ler o estado no fim do tick do cliente, numa cópia rápida. Converter para bytes e mandar pela rede acontece em outra thread.
- Entidades e player são enviados de 10 a 20 vezes por segundo, só dentro de um raio.
- O ASM vem embutido no jar com o pacote renomeado (`omni.shadow.asm`), porque Fabric e Forge trazem o seu próprio ASM.

**Pronto quando:** num jogo vanilla 1.21.x aberto pelo launcher, o player e os mobs se mexem no mapa em tempo real. Se o agent for removido ou falhar, o jogo abre normal e o mapa volta para o modo arquivo.

## Fase 5: Omni agent v1

- Gancho ASM para pegar mudança de bloco na hora (`BlockDelta`) e chunk carregado (`ChunkData`). Com isso o mapa ao vivo não depende mais de autosave.
- Suporte a nomes de **Fabric** e **NeoForge**, e a camada `bridge` funcionando com Knot e ModLauncher.
- **Cache de mundos de servidor:** guardar os chunks que o jogador viu em cada servidor, num cache local por servidor (a mesma ideia da exploração persistente do Diorama). Na aba Mundos, mostrar "Servidores visitados", que dá para ver com o jogo fechado.
- **Regra de multiplayer:** o radar de mobs e players fica ligado só no singleplayer ou em servidores que permitem. Respeitar códigos de fair-play, no estilo do Xaero.
- Um script que abre cada versão e loader suportado e confere se o agent responde ao `Hello`.

## Fase 6: client in-game do Omni (futuro)

- Um módulo `client` em cima do mesmo core: HUD, overlay e telas.
- Precisa de ganchos de **renderização** e **input**. A API de GUI do Minecraft muda muito entre versões, então a UI do Omni conversa só com um adaptador fino por faixa de versão.
- Como a conexão já funciona nos dois sentidos, coisas do launcher podem aparecer no jogo: convites de amigo da Steam (o `SteamTunnel` já existe), notificações e "entrar no mundo do amigo".

---

## Nomes por ambiente (a pegadinha principal do agent)

| Ambiente | Nomes em tempo de execução | De onde vem o mapeamento |
|---|---|---|
| Vanilla até 1.21.x | ofuscados | `client_mappings` oficial da Mojang (vem no JSON da versão) |
| Fabric | intermediary | intermediary do maven do Fabric, mais os mappings da Mojang |
| NeoForge 1.20.5+ | nomes da Mojang | nenhum extra |
| Forge antigo | SRG | deixar para depois (o Diorama suporta só Forge 1.20.1) |
| 26.1+ | sem ofuscação | nenhum (confirmar ao implementar) |

O launcher sabe qual instância está abrindo. Ele baixa o mapeamento certo e passa para o agent, e o agent monta a tabela só com os nomes que realmente usa.

## Classloaders

No vanilla, as classes do jogo ficam no classpath do sistema, junto com o agent. No Fabric (Knot) e no Forge/NeoForge (ModLauncher), elas ficam em outro classloader, que o agent não enxerga diretamente. Por isso o core é fino e o código dos módulos é definido **dentro do classloader do jogo** (a camada `bridge`). Isso vale fazer certo desde a fase 4, mesmo que só o vanilla seja testado nela.

## Riscos

1. **Manutenção por versão:** a parte de renderização do client in-game (fase 6) é a mais frágil. Os ganchos da telemetria devem ser poucos.
2. **Blocos de mods:** a cor tem que sair dos jars dos mods, ou cair numa cor padrão.
3. **Jogo que travou:** o disco fica sem o que aconteceu depois do último autosave. O mapa mostra o mesmo que o jogo vai carregar, o que está certo.
4. **Licenças:** o Diorama é MIT. Código reaproveitado precisa manter o aviso. Conferir a licença do mcmap antes de usar qualquer coisa dele.

## Referências

- [Diorama](https://github.com/SunkenInTime/diorama): mapa oblíquo dentro do jogo (MIT). Referência para o visual, os ícones de mobs e a exploração persistente.
- [mcmap](https://github.com/spoutn1k/mcmap): renderizador isométrico em C++ a partir dos arquivos. Referência para o formato.
- Formato de chunk e região: páginas "Anvil file format", "Chunk format" e "NBT format" do Minecraft Wiki.
