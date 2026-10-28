# Capas do protótipo OmniLauncher

Geradas em 28/10/2026 com a ferramenta integrada `image_gen` (modo built-in), sem imagens de referência. São ilustrações originais de paisagens voxel para testar a biblioteca; não são screenshots do Minecraft nem materiais oficiais de um modpack.

Arquivos PNG preservados sem edição: `covers/meadow.png`, `covers/copper.png`, `covers/dusk.png`, `covers/frost.png`. Proporção 2:3, 1024 × 1536 px. As artes foram conferidas visualmente quanto a enquadramento, legibilidade em capa, ausência de texto e distinção de cor. Fundos desfocados podem ser derivados pelo renderizador do aplicativo; não foram gerados bitmaps adicionais.

## Prompts finais

### meadow

Use case: stylized-concept. Asset type: original portrait game library cover illustration, exactly 2:3 aspect ratio. Render one polished Minecraft-like voxel landscape as if photographed in a sophisticated block-based game with subtle cinematic shaders. Every landform, tree and building must have convincing cubic Minecraft geometry and pixel-scale surface textures, never smooth realistic landscape forms. Full-bleed cover artwork, not a UI or package mockup. Strong mass composition and one clear focal area, readable at thumbnail size. No title, text, letters, logos, watermarks, UI, borders, PS5 packaging, people or foreground characters. Scene: a winding bright blue river through stepped grassy block hills, blocky oak and birch trees, one small wood-and-stone cottage by the river. Framing: elevated three-quarter view with a gentle diagonal river leading into distant hills, foreground grass framing the bottom, generous sky at top. Warm clear late-afternoon sunlight, rich fresh green and quiet golden blue atmosphere. Serene survival adventure, credible vanilla Minecraft world, vivid yet carefully balanced.

### copper

Use case: stylized-concept. Asset type: original portrait game library cover illustration, exactly 2:3 aspect ratio. Render one polished Minecraft-like voxel landscape as if photographed in a sophisticated block-based game with subtle cinematic shaders. Every landform, tree and building must have convincing cubic Minecraft geometry and pixel-scale surface textures, never smooth realistic landscape forms. Full-bleed cover artwork, not a UI or package mockup. Strong mass composition and one clear focal area, readable at thumbnail size. No title, text, letters, logos, watermarks, UI, borders, PS5 packaging, people or foreground characters. Scene: a beautiful old industrial settlement built into a lush block valley; compact stone and brick factory buildings, large clearly cubic copper-and-timber water wheels beside a narrow canal, exposed copper pipes and timber roofs, creeping greenery. Framing: looking diagonally down the water channel toward the central mill, strong warm silhouettes, layered depth. Cinematic golden-hour sunlight, copper amber, dark moss green and warm stone, a few subtle chimney wisps. Plausible Minecraft modded engineering architecture, restrained detail, not steampunk illustration.

### dusk

Use case: stylized-concept. Asset type: original portrait game library cover illustration, exactly 2:3 aspect ratio. Render one polished Minecraft-like voxel landscape as if photographed in a sophisticated block-based game with subtle cinematic shaders. Every landform, tree and building must have convincing cubic Minecraft geometry and pixel-scale surface textures, never smooth realistic landscape forms. Full-bleed cover artwork, not a UI or package mockup. Strong mass composition and one clear focal area, readable at thumbnail size. No title, text, letters, logos, watermarks, UI, borders, PS5 packaging, people or foreground characters. Scene: immense stepped voxel mountain islands and dramatic square rock arches above a quiet lake, a small distant ancient rectangular portal glowing softly on one island, sparse blocky trees. Framing: one dark foreground ledge guides the eye toward the distant portal and mountain silhouettes, generous lavender-coral evening sky. Cinematic sunset, dusky purple, muted coral reflections and deep blue stone. A sense of remote adventure; all terrain and construction visibly cubical and assembled from Minecraft-scale blocks.

### frost

Use case: stylized-concept. Asset type: original portrait game library cover illustration, exactly 2:3 aspect ratio. Render one polished Minecraft-like voxel landscape as if photographed in a sophisticated block-based game with subtle cinematic shaders. Every landform, tree and building must have convincing cubic Minecraft geometry and pixel-scale surface textures, never smooth realistic landscape forms. Full-bleed cover artwork, not a UI or package mockup. Strong mass composition and one clear focal area, readable at thumbnail size. No title, text, letters, logos, watermarks, UI, borders, PS5 packaging, people or foreground characters. Scene: a dramatic snow-covered voxel mountain valley with stepped blue ice and stone peaks, dark blocky spruce trees, small frozen lake, distant snowy ridgeline. Framing: a few angular pines at the lower edge, a clean leading valley toward one strong central mountain peak, sky in the upper third. Blue-hour cold cinematic light, deep navy shadows, white snow, a restrained green-teal aurora across the clear starry sky. Premium atmospheric winter game key art, faithful block geometry.


# Identidade Omni · Modular v01

Fonte: `branding/modular-v01/` (direção “01 Modular”, escolhida pelo usuário em 28/10/2026; também distribuída como `OmniLauncher-identidade-v01.zip`), produzida com ImageGen built-in; proveniência e prompts originais em `branding/modular-v01/PROVENANCE.md`. São estudos raster, não masters vetoriais.

Derivados em `brand/`, gerados a partir dos PNGs do pacote só por recorte ao contorno (alpha > 128), remoção de resíduos invisíveis (alpha ≤ 8) e redução Lanczos, sem redesenho nem recoloração:

- `logo-primary.png` (378 × 120): assinatura símbolo rosa + wordmark “omni”.
- `symbol-rose.png` (155 × 120): símbolo isolado, para a sidebar compacta.
- `app-icon.png` (256 × 256): símbolo centralizado em quadro transparente, ícone da janela.

Paleta aplicada em `qml/Theme.qml`: rosa `#E68AA6`, rosa claro `#F3B7CB`, grafite `#151216`, superfície `#211D23`, porcelana `#F5F1ED`, secundário `#B0A7AF`.

Fonte da interface: Manrope 400/500/600/700 em `fonts/`, licença SIL OFL 1.1 (`fonts/OFL.txt`).
