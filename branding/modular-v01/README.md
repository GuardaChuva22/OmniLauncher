# OmniLauncher - identidade Modular v01

A direção **01 Modular** foi escolhida pelo usuário em 28/10/2026. A marca usa três massas encaixadas, com dois vãos amplos, preservando a relação entre módulos e uma biblioteca de Jogos.

Esta edição desenvolve a direção escolhida. Paleta, tipografia e aplicações são a proposta de sistema para revisão. A identidade ainda não foi aplicada ao código da Home.

## Arquivos

- `symbol-rose.png`: símbolo rosa com transparência.
- `symbol-porcelain.png`: símbolo claro com transparência.
- `logo-primary.png`: assinatura horizontal com símbolo rosa e nome claro.
- `OmniLauncher-brandbook-v01.pdf`: guia visual de seis páginas.
- `brand-tokens.json`: cores e parâmetros de aplicação em formato editável.
- `fonts/`: Manrope, pesos 400/500/600/700 e licença OFL.
- `PROVENANCE.md`: referência e prompts usados na ferramenta ImageGen built-in.

Os arquivos de logo são PNGs raster produzidos com ImageGen. Os tokens de cor são exatos para futuras aplicações por código; pequenas variações dos pixels das imagens geradas não redefinem a paleta. Eles não devem ser apresentados como master SVG ou desenho vetorial finalizado.

## Cor

| Cor | Hex | Papel |
| --- | --- | --- |
| Rosa queimado luminoso | `#E68AA6` | Marca, seleção e ação principal |
| Rosa claro | `#F3B7CB` | Hover da ação principal |
| Grafite quente | `#151216` | Fundo principal |
| Superfície | `#211D23` | Menus e sobreposições |
| Porcelana | `#F5F1ED` | Texto e versão clara da marca |
| Secundário | `#B0A7AF` | Metadados e informação de apoio |

Usar texto grafite no botão rosa. Reservar o rosa a poucos pontos recorrentes; as capas fornecem a variedade de cor e atmosfera. Cores semânticas de erro, sucesso e aviso não herdam automaticamente a cor da marca.

Os contrastes calculados para cores sólidas estão no JSON e no guia. Avaliar novamente a composição real quando houver imagens, transparência e desfoque.

## Tipografia

Família proposta: **Manrope**, 400 para leitura, 500 para controles, 600 para títulos/ações e 700 apenas em destaques necessários. Evitar pesos finos e títulos grandes sem função.

O wordmark “omni” é uma peça gráfica; não é o nome digitado em Manrope. Em textos corridos, usar **OmniLauncher**.

## Forma e tamanho

Preservar proporções, inclinação e vãos. Não girar, esticar, separar os módulos ou reconstruir o wordmark com outra fonte.

Respiro inicial proposto: um terço da altura do símbolo em cada lado. O guia mostra uma prova em 16, 24, 32 e 48 px a 72 ppi. Usar 24 px como ponto de partida; a versão de 16 px requer ajuste óptico específico antes de aprovação para produção.

## Aplicação

Poucas superfícies, espaço, alinhamento e contraste organizam a interface. Vidro, sombra e luz pertencem à aplicação, sem alterar a geometria da logo. A página de interface do PDF é um estudo de aplicação, não uma captura da Home já modificada.

**Jogos** são instalações com versão, loader e mods. **Perfis** são identidades Microsoft ou offline. Exemplos: “Criar jogo”, “Adicionar mods ao jogo” e “Trocar Perfil”. Não acrescentar taglines ou explicações decorativas aos fluxos.

## Fontes

- [Manrope - projeto e licença](https://github.com/googlefonts/manrope)
- [Arquivos da família Manrope no Google Fonts](https://github.com/google/fonts/tree/main/ofl/manrope)
- [W3C - contraste mínimo](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)

O arquivo variável da Manrope foi obtido do repositório oficial Google Fonts e instanciado localmente nos quatro pesos. A licença original permanece na pasta `fonts/`.
