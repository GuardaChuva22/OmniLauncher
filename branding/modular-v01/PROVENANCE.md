# OmniLauncher · Modular v01

## Proveniência e escopo

Data: 28/10/2026. Ferramenta: `image_gen` integrada, modo built-in, edição com referências locais e `transparent_background: true`. Nenhuma API/CLI alternativa foi usada. Nenhum recorte, recoloração ou edição por Python foi executado; Python/PIL foi usado exclusivamente para análise dos arquivos. Os PNGs foram copiados diretamente do resultado da ferramenta.

Referência exata selecionada: `C:\Users\Admin\Documents\! VIBECODING\OmniLauncher\branding\explorations-02\01-modular.png`.

São **estudos raster de identidade**, não masters vetoriais. A direção, a silhueta modular, os dois vãos largos e o wordmark foram preservados visualmente; regeneração por imagem introduziu pequenas diferenças de contorno e variações sutis de cor. Os tokens de marca são #E68AA6 e #F5F1ED; os pixels dos PNGs são aproximações desses tokens, não uma prova de cor exata.

## Arquivos finais

| Arquivo | Dimensões | Bounds alpha >32 | Bounds alpha >128 |
| --- | --- | --- | --- |
| symbol-rose.png | 1254×1254 | (95,229,1159,1054) | (95,230,1159,1054) |
| symbol-porcelain.png | 1254×1254 | (97,229,1158,1053) | (97,229,1158,1053) |
| logo-primary.png | 2172×724 | (141,70,2031,670) | (141,70,2031,670) |

Pasta final: `C:\Users\Admin\Documents\! VIBECODING\OmniLauncher\branding\modular-v01`.

Bounds usam a convenção PIL (left,top,right,bottom), extremos direito/inferior exclusivos. O tool retornou 1254×1254 para símbolos apesar do pedido 1024×1024. A assinatura retornou proporção 3:1 correta.

### QA

- Exterior e vãos internos possuem transparência real; amostras dos dois canais dos símbolos retornaram alpha0. Alguns resíduos da extração têm alpha≤1 no rosa e ≤2 na porcelana nas regiões analisadas. Não são buracos opacos nem fundo embutido; podem aparecer exagerados em alguns visualizadores de alpha bruto.
- Silhuetas rosa e porcelana comparadas em alpha>128 no mesmo canvas: interseção/união 0,98761. São visualmente próximas, mas não idênticas pixel a pixel.
- Cor predominante aproximada: rosa RGB(229,128,154); porcelana RGB(243,238,233). A assinatura tem rosa predominante RGB(231,134,155) e porcelana RGB(244,238,233).
- Sem linha de apresentação, número01 ou marcas adicionais. A assinatura contém somente símbolo e wordmark “omni”.
- O símbolo rosa teve uma correção de renderização após a primeira extração. As variantes usaram o rosa corrigido junto à referência original, evitando nova direção de design.
- Contornos e vãos largos continuam reconhecíveis visualmente. A prova final16/24/32px será feita no brandbook; não afirmar que estes PNGs substituem uma reconstrução vetorial futura.

## Resultados originais do tool

- Primeira extração rosa (descartada): `C:\Users\Admin\.codex\generated_images\01a18453-5bab-7f11-b63d-6b26b7a9a6e5\exec-cdc70cd5-b88e-4b04-89a8-9018662c31e7.png`.
- Rosa corrigido (selecionado): `C:\Users\Admin\.codex\generated_images\01a18453-5bab-7f11-b63d-6b26b7a9a6e5\exec-60234d3e-56e0-4f96-b0c3-287540687bde.png`.
- Porcelana: `C:\Users\Admin\.codex\generated_images\01a18453-5bab-7f11-b63d-6b26b7a9a6e5\exec-a8d4115f-0069-4e21-bc2a-cde682470094.png`.
- Assinatura: `C:\Users\Admin\.codex\generated_images\01a18453-5bab-7f11-b63d-6b26b7a9a6e5\exec-2a59c70d-dbe6-4efb-93b8-917f5e22a290.png`.

## Prompts exatos

### Extração rosa inicial

Use case: precise-object-edit. Image 1 is the exact selected identity reference, NOT inspiration for a redesign. Extract ONLY the LARGE rose modular symbol at the upper left of Image 1 as a standalone production logo asset. Preserve its exact silhouette: the three staggered inclined/rising modular masses, the two broad diagonal-to-vertical negative-space passages, their individual widths, angles, staggered heights, proportions and rounded corners. Do not reinterpret, simplify, add joins, add strokes, or change the mark. Keep its original aspect ratio (wider than tall). Remove the word 'omni', all other small marks, line, number 01 and background. Render on a genuinely transparent RGBA canvas, including transparency inside both internal channels. Flat single solid fill exactly sRGB hex #E68AA6 (230,138,166), without any texture, shading, gradient, illumination or noise. Crisp anti-aliased edge only. Square 1024x1024 PNG canvas, symbol centered at about 80% of canvas width with comfortable transparent margins, no framing. Output exactly this extracted symbol and nothing else.

### Correção rosa selecionada

Use case: precise-object-edit. Image 1 is the approved Omni Modular identity geometry. Image 2 is an extraction of its large symbol with defects to correct. Preserve precisely the existing symbol geometry of Image 2 and the approved silhouette of Image 1: three staggered inclined modular masses with two wide diagonal-to-vertical open channels, same widths, angles, heights and rounded corners. CHANGE ONLY rendering defects: remove ALL gradient, noise, texture, shading, white speckles and stray pixels. Render the entire positive symbol in ONE mathematically flat uniform solid rose color #E68AA6 (RGB 230,138,166), fully opaque alpha255 inside. Background AND BOTH NEGATIVE CHANNELS must be completely transparent alpha0 and absolutely clean; transparency must reach through all gaps to the exterior. Anti-aliasing may exist only on the contour pixels. No tint variation within solid areas, no nearly-transparent noise anywhere. Center the exact symbol with 10% horizontal transparent margins on a square 1024x1024 transparent PNG. This is a production flat vector-style logo extraction, not an illustration, not a presentation, no text, no additional elements. Do not add new geometry or change the two channels.

### Símbolo porcelana

Use case: precise-object-edit. Image 1 is the approved Omni Modular brand reference. Image 2 is the selected extracted rose symbol, the exact visual master for this operation. CHANGE ONLY THE COLOR of the positive symbol of Image 2 to flat porcelain #F5F1ED (RGB 245,241,237). Preserve Image 2 pixel placement, canvas, aspect ratio, exact silhouette, three staggered inclined modular masses, both broad diagonal-to-vertical negative-space channels, all angles, widths, proportions and corner radii. Do not redraw, simplify, round differently, change spacing or change the mark. Keep genuinely transparent RGBA background and both channels completely transparent; no stray dots. Entire symbol solid opaque porcelain, no gradient, texture, shading, glow or nearly-transparent noise. Single isolated symbol centered with transparent margins on square 1024x1024 canvas. No text, no number, no border, no presentation.

### Assinatura principal

Use case: precise-object-edit. Image 1 is the exact selected Omni Modular brand identity, not inspiration for a new design. Image 2 is its selected standalone symbol and must define the exact symbol geometry in the output. Produce ONLY the horizontal primary logo on a genuinely transparent RGBA background: the exact symbol from Image 2 at left in solid rose #E68AA6, and the exact lowercase wordmark 'omni' from the LARGE signature of Image 1 at right in solid porcelain #F5F1ED. Preserve the reference wordmark letterforms, weight, rounded geometry, proportions and spacing; preserve symbol geometry including its three staggered inclined modular masses and two broad channels. Maintain the symbol/wordmark relative scale and gap of the LARGE signature in Image 1. Do not redesign any shapes or typography, do not substitute a font, do not change spelling. Full horizontal signature centered on a 3:1 transparent canvas, small transparent margins of about 6%, fill most width. All holes and exterior transparent. Pure solid fills, crisp contours, no shading/gradient/texture/noise, no background, no small supplementary marks, no number 01, no line, no mockup or presentation. Exact text: omni.

