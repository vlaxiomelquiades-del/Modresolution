# ResScale

Mod nativo `.so` (preload-native) pro LeviLauncher: adiciona um slider
**"Resolução (%)"** dentro do próprio Mod Menu do Levi, de **30% a 100%** da
resolução nativa da tela. Em 100% o jogo roda na resolução real; abaixo disso
o buffer de renderização é reduzido e o Android faz o upscale na
apresentação — o mesmo princípio de um "resolution changer" de sistema, só
que rodando dentro do processo do próprio jogo.

## Como funciona

Em vez de caçar uma variável de resolução dentro do `libminecraftpe.so`
(o que exigiria assinatura própria por versão, via Zaphkiel), o mod faz hook
em `ANativeWindow_fromSurface`, símbolo estável exportado por
`libandroid.so`. A `MainActivity` do Minecraft Bedrock herda de
`GameActivity` (androidx.games), e é essa função que a GameActivity chama
internamente pra transformar a `Surface` em `ANativeWindow`. No hook, deixamos
a chamada original acontecer, lemos a largura/altura nativas com
`ANativeWindow_getWidth/getHeight`, e então chamamos
`ANativeWindow_setBuffersGeometry()` com o tamanho escalado antes do jogo
criar a superfície EGL/Vulkan em cima da window.

O slider chama `onConfigChanged`, que salva o valor em `config/config.json` e
reaplica a geometria na hora, sem precisar reabrir o jogo (assumindo que o
Minecraft não fixe o tamanho do swapchain uma única vez — se isso acontecer,
o valor novo vale a partir da próxima criação de superfície).

## ⚠️ Ainda não testado rodando o jogo

Não tenho como compilar nem rodar isso — o hook em
`ANativeWindow_fromSurface` é a abordagem tecnicamente correta e é o que
ferramentas de resolution-changer usam no Android em geral, mas **precisa ser
validado no aparelho**. Se o slider não mudar nada visualmente:

1. Adicione um log em `handleFromSurface()` (`self.getLogger().info(...)`)
   pra confirmar se o hook está disparando.
2. Se não disparar: o RenderDragon pode estar pegando a `ANativeWindow` por
   outro caminho nessa versão. Use o Zaphkiel pra achar, dentro do
   `libminecraftpe.so`, a função que cria a swapchain/surface (Vulkan:
   próximo de `vkCreateSwapchainKHR`/`vkCreateAndroidSurfaceKHR`; GL:
   próximo de `eglCreateWindowSurface`) e troque o alvo do hook por ela.
3. Se disparar mas a imagem não mudar: pode haver upscale desabilitado no
   `SurfaceView` do host — nesse caso o efeito visual seria "janela pequena
   no canto" em vez de upscale, e o fix é garantir que o `SurfaceView`
   continue com `SCALE_TO_WINDOW`/tamanho de view fixo (o padrão do Android).

## Build

Precisa do NDK com suporte a C++20. FetchContent baixa o SDK
(`preloader-android`) e as dependências de `<pl/Config.hpp>`
(`nlohmann_json`, `boost::pfr`, `magic_enum`, `fmt`) — então o configure
exige rede.

```bash
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

Ajuste `$ANDROID_NDK_HOME` pro caminho do seu NDK no Termux. A saída é
`build/libresscale.so`.

## Empacotar e instalar

A pasta do mod (nome = id do mod, mantenha estável entre versões) deve
ficar assim:

```text
resscale/
├── manifest.json
└── libresscale.so
```

Não é preciso criar `config/config.json` na mão — o `load()` cria o arquivo
com os valores padrão (`percent: 100`) automaticamente na primeira execução.
Copie essa pasta pro diretório de mods nativos do Levi (ou zipe como
`resscale.levipack`) e ative o mod normalmente pela tela de mods.

## Por que 30% como mínimo

Foi o valor pedido — está fixado em `resscale::kMinPercent` em
`src/ResScaleConfig.hpp` caso queira ajustar depois.
