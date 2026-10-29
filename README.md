# OmniLauncher

**OmniLauncher** is a desktop launcher for **Minecraft: Java Edition** on Windows (C++20, Qt 6 Quick). It keeps a personal library of game installations (Vanilla, Fabric, Forge, NeoForge), installs mods and modpacks from Modrinth, and starts the game with a Microsoft account or a local offline profile. Personal project, shared with friends.

### Microsoft sign-in and privacy

- Sign-in happens in the system browser (OAuth 2.0 authorization code with PKCE, loopback redirect). The launcher never sees or stores the Microsoft password.
- The chain is Microsoft → Xbox Live → XSTS → Minecraft services, used only to confirm game ownership, read the player profile/skin and start the game.
- Only the Microsoft refresh token is stored, locally, in the Windows Credential Manager. Minecraft access tokens stay in memory. Nothing is sent to any server other than Microsoft, Xbox Live, Mojang/Minecraft services and the public mod catalogs (Modrinth).
- No analytics, accounts or backend of our own.

---


Launcher de Minecraft orientado pela experiência: uma biblioteca pessoal com presença de console, acabamento cuidadoso e resposta de aplicativo desktop.

**Etapa atual:** interface da Home com a identidade **01 Modular** aplicada. Jogos **Vanilla** instalam e iniciam de verdade com Perfil offline. Fabric/Forge/NeoForge, catálogos e login Microsoft são as próximas etapas funcionais.

Atualizado em 28 de outubro de 2026.

## Identidade visual

A direção Modular foi escolhida pelo usuário. O [brandbook v01](branding/modular-v01/OmniLauncher-brandbook-v01.pdf) e o [guia editável de identidade](branding/modular-v01/README.md) reúnem símbolo, assinatura, proposta de paleta rosa/grafite, tipografia, respiro e aplicações. Arquivos da marca e proveniência ficam em `branding/modular-v01/`. A página de aplicação é um estudo visual; a Home ainda não foi alterada com a nova marca.

## O que já está definido pelo pedido

- UI, UX, estética e sensação de uso são a prioridade do projeto.
- Acabamento premium, composição limpa e facilidade de uso são critérios obrigatórios. Evitar aparência genérica de interface gerada por IA.
- Referências: home do PS5 e acabamento Apple, com vidro, luz, profundidade e desfoque.
- Jogos com capas verticais individuais. Hover animado na capa e fundo que recebe uma versão desfocada da imagem sob o cursor. Transição de iniciar contínua e suave.
- Sidebar para navegação entre Home, Modpacks e Configurações. Catálogo Modrinth para instalar modpacks como jogos e adicionar mods aos jogos. Criação de jogo com escolha de loader, incluindo Fabric, Forge e NeoForge.
- Leveza importa, junto da qualidade visual.
- Conectar conta oficial do Minecraft faz parte do objetivo.
- Perfis offline locais para testes são essenciais desde o primeiro protótipo. Seletor de Perfis no canto inferior esquerdo, com avatar circular: rosto da skin para perfil Microsoft e Steve para perfil offline. Vários perfis salvos e troca rápida entre eles.
- Uso pessoal primeiro, compartilhado com amigos. Código em repositório privado no GitHub inicialmente; criação do repositório e publicação de código ainda não executadas.
- Pensar em ideias, funcionalidades e estrutura antes de começar a implementação.

**Decisões confirmadas:** Windows como primeira plataforma, Minecraft Java Edition, mouse e teclado; C++20 + Qt 6 Quick/QML + CMake. Direção visual escura, capas coloridas, fundo desfocado e vidro seletivo. Primeira entrega: protótipo navegável com Jogos, Perfis, criação/edição local e estados simulados de instalação/início. Versões e loaders nos formulários são dados locais para provar a UI, ainda sem resolver compatibilidade nem instalar componentes.

## Desenvolvimento local

O protótipo usa Qt 6.8.3 e MSVC x64. Os scripts reutilizam as ferramentas de compilação instaladas e mantêm o SDK Qt em `.tools/`, fora do Git.

```powershell
# Executar na pasta do projeto. Requer Visual Studio Build Tools com C++.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-qt.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run.ps1
```

Para distribuir: `scripts/package.ps1` gera `dist/OmniLauncher-<versão>-win64.zip`, com Qt e o runtime do Visual C++ ao lado do exe. Basta extrair e abrir `OmniLauncher.exe`; nada precisa ser instalado. Os dados ficam em `%APPDATA%\OmniLauncher` (versões anteriores usavam `%APPDATA%\OmniLauncher\OmniLauncher`, migrada automaticamente).

Dados de Jogos e Perfis são locais, fora da pasta de fonte. Para testes separados, `scripts/run.ps1` aceita `-DataDir caminho`. O primeiro acesso cria quatro Jogos de exemplo e nenhum Perfil; esses Jogos representam a biblioteca do protótipo, sem jogo instalado. Perfis offline podem ser criados, selecionados e removidos. “Jogar” em um jogo Vanilla baixa versão, bibliotecas, assets e o Java indicado pela Mojang (com verificação SHA-1), e inicia o Minecraft com o Perfil offline ativo. Arquivos compartilhados ficam uma vez na pasta de dados (`versions/`, `libraries/`, `assets/`, `runtimes/`); cada jogo tem sua pasta em `instances/<id>`. Memória, resolução e argumentos da JVM são salvos por jogo; memória padrão e o comportamento do launcher ao iniciar ficam em Configurações.

As quatro capas são artes originais geradas com ImageGen built-in. Arquivos, prompts e proveniência em `assets/ARTWORK.md`. Imagens próprias podem ser importadas no editor de jogo e copiadas para a pasta de dados, preservando o original.

## Modpacks customizados (aba Modpacks → Customizado)

Modpacks seus ou de amigos, assinados por link. O link aponta para um manifesto JSON; publicar uma versão nova no mesmo link faz o launcher oferecer a atualização (só arquivos novos ou alterados são baixados; mundos e arquivos pessoais ficam).

```json
{
  "pack": { "name": "Vanilla Ultra Enhanced", "version": "1.1.0" },
  "minecraft": "26.2",
  "fabric": { "loader": "0.19.3" },
  "files": [
    { "path": "mods/exemplo.jar", "url": "https://…", "size": 123456, "sha256": "…" }
  ],
  "overrides": { "url": "https://…/configs.zip", "sha256": "…" }
}
```

`fabric` pode ser `forge` ou `neoforge` (com `"loader": "<versão>"`). `files[].sha1` também é aceito. `overrides` é opcional: um zip com `config/`, `defaultconfigs/` etc., aplicado sobre o jogo a cada instalação ou atualização — inclua aqui as configs que o pack precisa para funcionar.

## OmniLauncher Cloud (modpacks compartilhados por código)

Na aba Customizado, "Criar modpack" transforma um jogo da biblioteca num modpack com um código de 6 letras. Quem entra com o código recebe cada versão nova como atualização. Não há contas: quem cria recebe uma chave de editor, guardada no próprio launcher; o "link de editor" passa essa chave para outra pessoa ou outro PC.

- Mods encontrados no Modrinth entram só como referência (URL e hash do CDN do Modrinth).
- As configs (`config/`, `defaultconfigs/`, `kubejs/`) viram um zip enviado ao pack, assim como jars que não estão no Modrinth (até 24 MB cada, 40 arquivos por pack).
- O servidor é `cloud/worker.js`, um Cloudflare Worker com um namespace KV (`PACKS`). Ele só aceita arquivos do CDN do Modrinth ou enviados ao próprio pack.

Publicar o servidor (uma vez, numa conta Cloudflare só do OmniLauncher):

```bash
cd cloud
npm install
npx wrangler login
npx wrangler kv namespace create PACKS   # cole o id em wrangler.toml
npx wrangler deploy                      # imprime a URL do Worker
```

A URL vai em `OMNI_CLOUD_URL` no CMake (ou na variável de ambiente de mesmo nome, para testar com `npx wrangler dev`). Sem ela, a aba mostra que a nuvem ainda não está no ar.

## Nomenclatura — decisão confirmada

| Termo na interface | Significado | Exemplos |
| --- | --- | --- |
| Jogos | Instalações independentes que você cria e inicia, com capa, versão de Minecraft, loader, mods e arquivos próprios | “Vanilla”, “Create com amigos”, “Sobrevivência” |
| Perfis | Identidades de jogador conectadas a uma conta Microsoft ou criadas localmente para uso offline | Seu nome Microsoft, “CaioTeste” offline, outro perfil salvo |

Escolher um **Perfil** e iniciar um **Jogo**. Manter vários de cada e trocar o perfil ativo sem duplicar jogos. Textos do produto: **Criar jogo**, **Editar jogo**, **Adicionar mods**, **Instalar modpack**, **Perfis**, **Conectar Microsoft**, **Criar perfil offline**. Modpack instalado aparece em Jogos.

Reservar “perfil” exclusivamente à identidade de jogador em toda a interface. “Instalação”, “instância” e “conta” podem aparecer na documentação técnica quando necessários, sem criar nomes concorrentes na navegação. Não chamar uma instalação com mods de perfil.

## Contrato de design — requisito confirmado

A qualidade deve aparecer na composição, na escolha das imagens, na tipografia, nos materiais e na resposta dos controles. Cada elemento precisa justificar seu espaço. Esta orientação vale para todas as telas, inclusive configurações, erros e primeiro acesso.

- **Texto só quando ajuda:** nomes e ações diretos. Sem taglines recorrentes, frases de efeito, subtítulos redundantes ou selos decorativos como “Premium” e “Sua experiência definitiva”.
- **Poucos planos visuais:** usar alinhamento, espaço e hierarquia para organizar o conteúdo. Sem painel dentro de painel, caixas envolvendo cada informação ou grades de cards por padrão. Capas são objetos da biblioteca; rótulos e controles podem existir diretamente na composição.
- **Materiais com intenção:** vidro, brilho e desfoque entram onde criam profundidade e preservam a leitura. Não aplicar glow, bordas luminosas, pílulas e transparência a todos os elementos.
- **Uso óbvio e acabamento consistente:** ação principal clara, controles secundários discretos e encontráveis, espaçamentos e alinhamentos precisos, ícones coerentes e estados de interação completos. Reduzir ruído sem esconder funções essenciais.
- **Movimento responsivo:** transições curtas e contínuas, sem introduções obrigatórias, delays decorativos ou controles que mudam de lugar durante o uso.

Na home proposta, os elementos essenciais são cenário, seleção de instalações, identificação da seleção, Jogar e acesso discreto à navegação/conta. Não adicionar estatísticas, notícias, recomendações, caixas de boas-vindas ou blocos explicativos para preencher espaço. Painéis de gerenciamento só aparecem quando solicitados, com conteúdo direto e sem recipientes aninhados.

Revisão de cada tela: o que pode ser removido sem prejudicar o uso? O olhar encontra a ação principal? A interface continua legível com os efeitos reduzidos? O mesmo cuidado aparece em hover, foco, carregamento e erro? O resultado deve ser avaliado visualmente e em interação, não apenas por cumprir uma lista de efeitos.

## Proposta de experiência

Abrir o OmniLauncher deve dar vontade de escolher uma experiência e jogar. O usuário precisa reconhecer imediatamente o jogo selecionado, o perfil ativo e o que acontecerá ao apertar Jogar.

Uma instalação tem nome, versão do jogo, loader quando houver, arquivos separados e identidade visual própria. Pode se chamar “Sobrevivência”, “Create com amigos” ou “Vanilla”. A capa representa essa instalação. Abrir diretamente um mundo ou servidor seria um recurso separado, sujeito ao suporte da versão e do ambiente; não deve ser prometido implicitamente pelo layout.

### Navegação — estrutura acordada; composição proposta

Sidebar estreita à esquerda, com ícones e rótulos curtos. Testar uma superfície integrada ao fundo com contraste estável, sem criar uma caixa flutuante adicional dentro de outra caixa. Seleção discreta e clara; Perfis no rodapé da navegação. Em janela pequena, uma variante compacta mantém nomes acessíveis. A largura e o tratamento exatos serão avaliados visualmente.

| Destino | Conteúdo |
| --- | --- |
| Home | Biblioteca pessoal de capas, criar jogo e iniciar jogo |
| Modpacks | Busca e navegação pelos catálogos, detalhes e instalação como novo jogo |
| Configurações | Aparência, movimento, armazenamento e padrões globais de Java/memória |

Mods pertencem ao jogo em edição. Abrir um jogo revela sua área de gerenciamento, com Mods e Ajustes locais, sem adicionar uma página global ambígua à sidebar. Configurações globais fornecem padrões; escolhas específicas do jogo prevalecem. A navegação preserva a posição da biblioteca, seleção, busca e filtros ao voltar. Downloads continuam durante a troca de página e têm um indicador discreto de acesso ao progresso.

### Seletor de Perfis — requisito confirmado

No canto inferior esquerdo da sidebar, um botão circular mostra a cabeça/rosto da skin do perfil ativo. Preservar os pixels da imagem na ampliação e usar cache local; falha na busca do avatar não pode bloquear o uso. Para perfil offline, usar Steve no launcher. Esse avatar local não promete uma skin específica dentro do jogo.

Com espaço, exibir nome e estado em texto discreto ao lado do avatar. Na sidebar compacta, manter nome e estado acessíveis por foco/tooltip e descrição acessível. Evitar depender só de um ponto colorido para distinguir Microsoft e Offline.

Ao clicar, abrir um único menu “Perfis”, com vários perfis salvos, seus avatares e indicação do ativo. Trocar diretamente ao selecionar uma linha, sem confirmação intermediária. A seleção persiste entre aberturas. As ações são “Conectar Microsoft” e “Criar perfil offline”. Gerenciar/remover fica nesse fluxo ou em Perfis nas configurações, sem criar uma página adicional na sidebar. O menu permite teclado e fecha com Escape, devolvendo o foco ao botão.

Perfis representam identidades de jogador; Jogos representam as instalações. A seleção do perfil é global na proposta inicial, sem duplicar jogos. Fixar a identidade no momento de iniciar; mudar o perfil ativo no launcher não altera a identidade de uma sessão do jogo que já está rodando.

Criar um perfil offline pede apenas o nome do jogador, com validação compatível com o jogo. Ele aparece no seletor e pode ser escolhido sem completar o login Microsoft. Nenhuma senha ou token é necessário para essa identidade local. Sem perfis cadastrados, oferecer as duas opções no primeiro acesso; não tornar o login uma barreira para testar a UI.

Estados a desenhar e implementar: sem perfil, perfil offline ativo, perfil Microsoft pronto, autenticação em andamento, sessão que exige novo login e avatar indisponível. Uma falha Microsoft não deve trocar silenciosamente para um perfil offline. Diferenciar “perfil offline” de “computador sem internet”.

### Biblioteca de jogos — direção confirmada; detalhes propostos

A biblioteca de jogos é o centro da experiência. Cada jogo corresponde a uma instalação independente: nome próprio e capa vertical personalizável. Testar inicialmente proporção **2:3**, como escolha de composição a avaliar visualmente, sem copiar a identidade ou a tarja de uma plataforma. A arte ocupa a capa; metadados ficam discretos e fora dela, sem empilhar badges.

Uma fileira de capas ocupa a composição principal, com acesso discreto à criação de jogo, busca e Perfis. No fundo, uma ampliação desfocada da capa em hover preenche a janela, com tratamento de contraste e cor. Essa imagem derivada da capa é a direção padrão; cenários independentes podem ser uma personalização posterior.

Abaixo da seleção ficam nome, versão, loader e um botão Jogar em posição estável, sem uma caixa envolvendo o conjunto. Gerenciar a instalação pode abrir um único painel lateral sob demanda, com opções e detalhes organizados por espaço e tipografia. Progresso de preparação aparece junto da ação iniciada, com etapas compreensíveis.

O carrossel funciona como vitrine dos favoritos. Uma biblioteca em grade ou lista, com busca, oferece acesso rápido a muitas instalações. Mouse, roda e teclado precisam funcionar sem depender de hover para descobrir ações.

### Comportamento das capas e do fundo

- **Repouso:** composição estável, fundo do jogo selecionado e nenhuma animação contínua obrigatória.
- **Hover:** a capa cresce e se eleva levemente, com sombra e realce refinados. O fundo mistura gradualmente a imagem atual com a versão ampliada e desfocada daquela capa. Evitar inclinação exagerada e efeitos competindo entre si.
- **Seleção e teclado:** proposta de clique para selecionar; hover é uma prévia visual, sem trocar silenciosamente o alvo de Jogar. Foco de teclado oferece a mesma prévia. Ao sair da área de capas, o fundo retorna suavemente ao jogo selecionado. Sem seleção inicial, mostrar a capa de abertura da biblioteca; definir esse caso no protótipo.
- **Trocas rápidas:** uma nova interação redireciona a transição a partir do estado visual atual. Sem fila de animações, flashes, saltos de escala ou retorno brusco ao estado inicial. Deslocar o cursor com as capas paradas e mover a fileira sob um cursor parado precisam ter comportamento consistente.
- **Iniciar:** fixar explicitamente o jogo acionado, encerrar a prévia de outro jogo e levar o destaque para a capa correta. Proposta de capa e fundo assumirem a composição enquanto os controles secundários desaparecem com suavidade; o progresso surge no mesmo contexto. O estado de iniciar responde imediatamente. Falha ou cancelamento recuperam a biblioteca sem perder posição e seleção.

Criar os fundos desfocados a partir das imagens e reutilizá-los; a transição pode misturar texturas prontas. Reservar blur dinâmico mais caro para onde trouxer diferença perceptível. Pré-carregar as capas próximas e manter uma imagem de fallback. O efeito precisa funcionar sem imagens piscando ao passar o mouse.

Os tempos e curvas são parâmetros para ajuste em interação, não decisões irrevogáveis. Testar entrada/saída rápida do cursor, nomes longos, repetição de comandos, imagens claras/escuras e redução de movimento. O hover deve manter a área clicável estável e não deslocar as capas vizinhas.

### Criação de jogo — primeiro fluxo a prototipar

Uma ação discreta “Criar jogo” abre uma única superfície com nome, capa, versão do Minecraft e tipo de instalação: Vanilla, Fabric, Forge ou NeoForge. Outros loaders podem ser acrescentados conforme suporte real. A lista de versões do loader acompanha a versão de Minecraft escolhida, mantendo uma opção recomendada visível. Opções indisponíveis precisam ter motivo claro.

A prévia da capa responde às mudanças. Ajuste simples de enquadramento para imagens enviadas pelo usuário e arte padrão para quem não escolher uma. Criar insere a capa na biblioteca e seleciona o novo jogo. “Adicionar mods” leva ao catálogo contextual desse jogo, também acessível depois; não obrigar a montar uma lista de mods antes de criar. Opções avançadas ficam recolhidas.

Na primeira prova de UI, versões e preparação podem ser dados simulados, identificados como tal na documentação do protótipo. Login, downloads e execução reais virão na etapa funcional. A transição visual do launcher pode ser controlada; o tempo de abertura e o desenho da janela do Minecraft dependem também do processo do jogo, sem prometer controle sobre sua tela de carregamento.

### Modpacks e mods — fluxos a prototipar

**Instalar modpack:** abrir Modpacks, escolher fonte, buscar e filtrar, abrir detalhes, escolher uma versão do pack e instalar. A versão do pack determina Minecraft e loader; mostrar esse resumo, sem oferecer combinações arbitrárias que alterem o pack. A instalação cria um jogo com nome e capa editáveis, identidade da origem e progresso. Ao concluir, ele fica pronto para iniciar pela Home.

**Adicionar mods:** no jogo, abrir Mods e usar “Adicionar mods”. O catálogo já conhece Minecraft e loader daquele jogo. Reutilizar a linguagem visual da página Modpacks, com origem visível e ação de adicionar. Usar listas claras para os mods instalados, com ativar/desativar, remover e atualização quando disponível. Não transformar cada linha de mod em um painel.

Antes de instalar, filtrar a versão específica do arquivo por jogo, loader e ambiente de cliente, e resolver dependências obrigatórias declaradas. Metadados de compatibilidade não garantem ausência de conflitos entre todos os mods. Preservar identificadores de projeto, versão e arquivo junto da fonte; não tratar projetos homônimos nas duas plataformas como equivalentes automaticamente.

Jogos vindos de modpack devem distinguir arquivos do pack e modificações locais. Antes de atualizar, mostrar o que muda e preservar mundos/dados; não sobrescrever silenciosamente mods adicionados pelo usuário. No protótipo, representar o caso de atualização e a distinção entre jogo criado manualmente e jogo vindo de modpack, mesmo que a implementação completa venha depois.

### Direção visual confirmada — detalhes em refinamento

**Mundo da marca:** imersivo, calmo, luminoso, preciso.

**Assinatura:** capas verticais de jogos e atmosfera derivada da capa em hover, com transições contínuas. Vidro e luz complementam a composição em pontos selecionados.

| Elemento | Direção sugerida |
| --- | --- |
| Base | Grafite profundo com pequenos desvios de cor extraídos do cenário |
| Luz | Uma cor ambiente por instalação, aplicada com moderação |
| Vidro | Uso seletivo em controles agrupados e sobreposições; dispensar recipiente quando alinhamento e contraste bastarem |
| Tipografia | Sans legível, títulos moderados; escolher fonte redistribuível antes de empacotar |
| Espaçamento | Escala de 4/8 px, com composição mais espaçosa na home |
| Formas | Cantos suaves coerentes, bordas finas e sombra discreta |
| Movimento | Resposta imediata; feedback em cerca de 150–220 ms, trocas de cenário em 250–400 ms |

O movimento pode continuar depois que a ação já respondeu, sem bloquear novos comandos. Respeitar redução de movimento, escala de tela e navegação por teclado. Fundo bonito precisa manter texto e seleção legíveis em qualquer capa.

Começar com cenário estático e transições. Vídeo, partículas, paralaxe e sons discretos são possibilidades futuras, a validar pelo ganho visual e custo. Sons devem poder ser desligados.

## Escopo de produto e ordem de implementação

### Recursos definidos para o produto

1. **Biblioteca e criação:** jogos isolados com capas, hover e fundo animados; nome, versão e escolha de Vanilla/Fabric/Forge/NeoForge.
2. **Catálogos:** Modrinth para instalar modpacks como jogos e adicionar mods por jogo.
3. **Perfis e execução:** seletor inferior com múltiplos perfis Microsoft/offline, avatar da skin ou Steve, Java compatível, preparação e início do jogo.
4. **Gerenciamento:** mods instalados, dependências declaradas, ajustes por jogo e padrões globais.
5. **Experiência completa:** estados vazios, progresso, cancelamento seguro, erros úteis, foco de teclado e redução de movimento.

O primeiro protótipo visual deve cobrir a sidebar e os fluxos desses recursos usando dados locais/simulados. A implementação funcional pode começar com uma sequência Vanilla e Modrinth, seguida dos loaders. Essa ordem reduz retrabalho; não remove Fabric, Forge ou NeoForge do escopo definido.

### Extensões posteriores

- Importação de instalações de outros launchers e arquivos locais de modpacks.
- Suporte a loaders adicionais conforme necessidade real.
- Presets de instalação e duplicação, com aviso sobre cópia de mundos e espaço ocupado.
- Coleções de screenshots e personalização mais rica das capas.

Controle/gamepad, sincronização em nuvem, rede social e catálogo amplo de notícias ficam fora da proposta inicial. Só ampliar quando servirem a um uso concreto.

## Stack confirmada

**C++20 + Qt 6 + Qt Quick/QML + CMake** é a stack inicial confirmada.

QML descreve a interface, estados, componentes e animações. C++ cuida de arquivos, rede, processos, instalações e autenticação. Qt Quick usa renderização acelerada por GPU; isso oferece uma base adequada para uma UI própria, mas não garante baixo consumo automaticamente.

Qt Quick é a base desta experiência; não partir de uma tela convencional de widgets e tentar adicionar efeitos ao final. Blur de conteúdo dentro da janela e transparência sobre o desktop são problemas diferentes. A primeira prova pode trabalhar só com o cenário interno, sem depender de efeitos específicos do Windows.

Tauri foi considerado no brainstorming; a decisão atual é seguir com C++ e Qt Quick.

Não há benchmark comparativo neste momento. Linguagem, por si só, não decide memória, fluidez ou tempo de abertura. Não prometer um limite de RAM antes de medir.

O licenciamento dos módulos Qt e dependências precisa entrar na escolha de empacotamento. Decidir a licença do OmniLauncher e conferir os módulos usados antes de distribuir; não presumir que todo módulo Qt tenha as mesmas condições.

### Princípios de leveza

- Priorizar composição, imagens bem dimensionadas e cache antes de adicionar shaders.
- Limitar a área e quantidade de efeitos; evitar blur de tela inteira sendo recalculado continuamente.
- Downloads, verificação de arquivos e preparação fora do trabalho da interface.
- Pausar movimento e mídia ao minimizar ou ocultar a janela.
- Quando o jogo abrir, oferecer política de minimizar ou encerrar o launcher; não manter animações consumindo recursos sem necessidade.

Meta inicial a validar: navegação fluida a 60 fps no computador de referência durante interação, sem renderização contínua desnecessária em repouso. Medir abertura, memória, CPU/GPU e comportamento enquanto Minecraft roda, separando consumo do jogo e do launcher.

## Perfis, testes offline e lançamento do jogo

### Perfil offline

Perfil offline é uma identidade local de teste. Precisa funcionar no primeiro protótipo para validar adicionar/remover, alternar perfis, persistir seleção e iniciar de forma simulada, sem depender de APIs externas. Para o jogo real, o uso offline depende de uma instalação com os arquivos necessários; baixar Java, jogo, mods e modpacks exige rede quando esses arquivos ainda não estiverem presentes.

Persistir a identidade offline de maneira consistente com as convenções do jogo, sem gerar um novo identificador aleatório a cada abertura. Validar o tratamento de nome/UUID com os ambientes de teste antes da implementação funcional; alterações de identidade podem afetar o reconhecimento do jogador e seus dados. Credenciais de uma conta Microsoft não entram na sessão de uma identidade offline.

Prever testes locais e ambientes que aceitem identidade offline. Essa identidade não autentica em servidores que exigem sessão Microsoft/Minecraft válida. Não apresentar o modo offline como equivalente a uma conta oficial autenticada.

### Perfil Microsoft

O login oficial atual é Microsoft. As contas Mojang antigas foram aposentadas no processo de migração; não planejar um formulário separado de usuário/senha Mojang.

Proposta de fluxo: “Conectar Microsoft” abre o navegador do sistema, retorna ao launcher, obtém a sessão necessária nos serviços Xbox/Minecraft e consulta perfil e direito de acesso antes de iniciar o jogo. Validar o fluxo aceito e os requisitos atuais antes da implementação.

Usar registro/client ID próprio do OmniLauncher. Não embutir identificadores de outros launchers. Login Microsoft funcionando, isoladamente, não comprova acesso aos serviços Minecraft. Confirmar cedo o processo de revisão/aprovação aplicável ao aplicativo e validar uma sessão ponta a ponta.

Não coletar senha no launcher. Segredos de sessão ficam no armazenamento protegido do sistema, fora de arquivos de configuração comuns e logs. No Windows, avaliar Credential Manager/DPAPI. Aplicativo desktop é cliente público: não depender de um client secret embutido como proteção.

Essa validação é pequena e deve ocorrer cedo, em paralelo à evolução visual quando começarmos a implementar. Seu resultado não impede o brainstorming nem um protótipo visual com estados simulados.

## Compartilhamento e GitHub

Primeiro público: o autor e seus amigos. Usar repositório privado no GitHub para fonte, histórico e colaboração. A criação do repositório entra na etapa de inicialização do projeto. Definir proprietário e nome ao preparar essa publicação, aproveitando o contexto da conta conectada; não é uma decisão que bloqueia o protótipo visual.

Preparar uma distribuição simples para os amigos na etapa funcional, começando por um pacote Windows com as dependências necessárias. Acesso ao repositório e acesso ao pacote de teste são decisões separadas; não exigir ferramentas de desenvolvimento para executar o launcher.

Manter credenciais, tokens, chaves de API, dados pessoais, instalações, mundos, caches e builds locais fora do Git. Repositório privado também precisa dessa separação. Escolha de licença e avisos dos componentes entram antes de compartilhar os binários; não é necessário criar infraestrutura de atualização automática nesta fase.

## Estrutura futura sugerida

A árvore abaixo é um plano, não diretórios já implementados. Começar com um executável e divisões simples; evitar múltiplos serviços e bibliotecas sem necessidade.

```text
OmniLauncher/
  CMakeLists.txt
  CMakePresets.json
  src/
    app/             # inicialização e integração com QML
    core/            # instalações, downloads e execução
    auth/            # contas Microsoft/offline, seleção e armazenamento de sessão
    providers/       # adaptador Modrinth e metadados de origem
    loaders/         # instalação de Fabric/Forge/NeoForge
    platform/        # detalhes do sistema operacional
  qml/
    theme/           # cores, materiais, medidas e movimento
    components/      # capas, controles, painéis e progresso
    screens/         # home, modpacks, jogo/mods e configurações
  assets/            # fontes, ícones e cenários com origem conhecida
  tests/             # testes proporcionais de auth, manifestos e preparação
  docs/              # decisões e especificações quando houver conteúdo
  README.md
```

Dados do usuário ficam fora da pasta de código: configurações, instalações e cache em locais próprios do sistema. Arquivos baixados e credenciais não entram no repositório. Inicialmente, configurações pequenas em JSON são suficientes; banco de dados só se a necessidade aparecer.

## Sequência de trabalho proposta

1. **Compor a biblioteca:** desenhar a home com capas verticais, fundo derivado das imagens, nomes longos e poucas ações. Definir enquadramento, materiais e contraste.
2. **Provar o feel:** uma janela Qt Quick com sidebar, seletor de Perfis e criação de perfil offline local, criação de jogos com escolha de loader, hover das capas, transição de fundo, teclado e início simulado. Representar estados Microsoft com dados simulados, sem fingir autenticação real. Prototipar também busca de modpacks, instalação como jogo e adição de mods contextual, usando dados simulados. Refinar curvas, interrupções e continuidade; medir custo dos efeitos.
3. **Validar as bordas da experiência:** biblioteca vazia e grande, conta desconectada, preparação, cancelamento, erro, DPI e redimensionamento.
4. **Primeira sequência real:** registro/acesso do app validado, conta oficial, instalação Vanilla e execução do jogo.
5. **Completar o produto:** catálogos reais, dependências e instalação de modpacks, Fabric/Forge/NeoForge e recuperação de downloads. Preservar modificações locais ao evoluir o suporte a atualizações.

Critério do primeiro protótipo: a pessoa entende em poucos segundos qual jogo e perfil estão selecionados, encontra Jogar e navega sem esperar por animação. A avaliação visual e as medições orientarão a decisão final de stack.

## Detalhes a refinar

- A base escura foi confirmada. Refinar contraste, intensidade do fundo e materiais olhando a Home em funcionamento.
- Detalhes de layout, largura da sidebar, fonte e curvas de animação serão escolhidos e refinados no protótipo; não precisam atrasar sua criação.
- Java Edition, Windows, mouse e teclado confirmados; outras plataformas e controle ficam para depois.
- Proprietário e nome do repositório privado serão definidos na etapa de publicação inicial; uso pessoal e com amigos já confirmado.
- Stack inicial confirmada; licença e metas mensuradas de desempenho seguem a validar antes de distribuir.

## Fontes técnicas consultadas

Fontes primárias consultadas em 28/10/2026. Elas sustentam a viabilidade técnica; as escolhas de produto e aparência acima são propostas.

- [Qt Quick Scene Graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html): renderização e aceleração gráfica.
- [Qt Quick MultiEffect](https://doc.qt.io/qt-6/qml-qtquick-effects-multieffect.html): efeitos e composição em QML.
- [Qt: obrigações GPL/LGPL](https://www.qt.io/development/open-source-lgpl-obligations): condições e diferenças entre módulos/licenças.
- [Arquitetura do Tauri](https://v2.tauri.app/concept/architecture/): alternativa com Rust e WebView.
- [Microsoft: aplicações e fluxos de autenticação](https://devblogs.microsoft.com/identity/app-types-and-auth-flows/): clientes públicos e PKCE.
- [Minecraft: suporte Java Edition](https://help.minecraft.net/hc/en-us/sections/44460521913485): lista processo de revisão/aplicação da Game Service API; requisitos específicos ainda precisam ser confirmados.
- [Prism: configuração de APIs](https://prismlauncher.org/wiki/help-pages/apis/): configuração de client ID próprio.
- [Modrinth: busca de projetos](https://docs.modrinth.com/api/operations/searchprojects/) e [versões](https://docs.modrinth.com/api/operations/getprojectversions/): filtros e metadados para os catálogos contextuais.
- [Minecraft: conclusão da migração de contas Mojang](https://www.minecraft.net/en-us/article/account-migration-last-call): login oficial baseado em conta Microsoft.
