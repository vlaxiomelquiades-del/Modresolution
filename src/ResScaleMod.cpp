#include "ResScaleConfig.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <android/native_window.h>
#include <jni.h>

#include <pl/Config.hpp>
#include <pl/Mod.hpp>
#include "ModMenuCompat.hpp"
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

// ============================================================================
// ResScale
//
// Adiciona, dentro do próprio Mod Menu do Levi, um slider de 30% a 100% que
// controla a resolução interna de renderização do jogo.
//
// Mecanismo: em vez de procurar uma variável de resolução dentro do
// libminecraftpe.so (o que exigiria assinatura por versão via Zaphkiel),
// este mod faz hook em `ANativeWindow_fromSurface` (símbolo estável e
// exportado por libandroid.so). O Minecraft Bedrock usa a GameActivity do
// Android (androidx.games) como base da MainActivity, e é exatamente essa
// função que a GameActivity chama internamente para converter a Surface
// (SurfaceView) em um ANativeWindow nativo. Interceptando essa chamada,
// conseguimos forçar `ANativeWindow_setBuffersGeometry()` com um tamanho de
// buffer menor que a tela antes do jogo criar sua superfície EGL/Vulkan em
// cima dele — o compositor do Android faz o upscale do buffer pequeno até o
// tamanho real da tela, exatamente como um "resolution changer" de sistema
// faria, mas de dentro do próprio processo do jogo.
//
// IMPORTANTE: isso ainda não foi validado rodando o jogo de verdade (não
// tenho como compilar/rodar isso aqui). Veja o README.md para como testar e
// qual é o plano B (via Zaphkiel) se esse hook específico não disparar.
// ============================================================================

namespace {

using resscale::ResScaleConfig;

constexpr const char *kModuleId = "resscale.module";
constexpr const char *kPercentKey = "percent";

using FromSurfaceFn = ANativeWindow *(*)(JNIEnv *, jobject);

int parseInt(std::string_view value, int fallback) {
  std::string text(value);
  char *end = nullptr;
  errno = 0;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
    return fallback;
  }
  return static_cast<int>(parsed);
}

} // namespace

class ResScaleMod {
public:
  static ResScaleMod &instance() {
    static ResScaleMod mod;
    return mod;
  }

  ResScaleMod() : mSelf(*ll::mod::NativeMod::current()) {}

  [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

  bool load() {
    auto &self = getSelf();
    mConfigFile.emplace();
    if (!mConfigFile->load()) {
      self.getLogger().error("Falha ao carregar config do ResScale");
      mConfigFile.reset();
      return false;
    }
    normalizeConfig();
    mConfigFile->save();
    return true;
  }

  bool enable() {
    auto &self = getSelf();
    const int percent = mConfigFile ? mConfigFile->value().percent : 100;

    const bool moduleRegistered =
        pl::modmenu::ModuleBuilder(kModuleId, "Resolução do Jogo")
            .modId(self.getId())
            .description(
                "Reduz a resolução interna de renderização (30% a 100% da "
                "resolução nativa).")
            .defaultEnabled(true)
            .config(kPercentKey, "Resolução (%)",
                    pl::modmenu::ConfigType::SliderInt, std::to_string(percent),
                    std::to_string(resscale::kMinPercent),
                    std::to_string(resscale::kMaxPercent))
            .onConfigChanged(onConfigChanged)
            .registerModule();

    if (!moduleRegistered) {
      self.getLogger().error("Falha ao registrar módulo no Mod Menu");
      return false;
    }

    const uintptr_t target =
        pl::memory::resolveSignature("ANativeWindow_fromSurface", "libandroid.so");
    if (target == 0) {
      self.getLogger().error(
          "ANativeWindow_fromSurface não foi encontrada em libandroid.so");
      pl::modmenu::unregisterModule(kModuleId);
      return false;
    }

    mHook = pl::memory::HookHandle(reinterpret_cast<void *>(target),
                                   reinterpret_cast<void *>(&detourFromSurface),
                                   reinterpret_cast<void **>(&mOriginal));

    if (!mHook.installed()) {
      self.getLogger().error(
          "Falha ao instalar hook em ANativeWindow_fromSurface");
      pl::modmenu::unregisterModule(kModuleId);
      return false;
    }

    self.getLogger().info("ResScale ativo. Resolução inicial: {}%", percent);
    return true;
  }

  bool disable() {
    resetToNativeLocked();
    mHook.reset();
    pl::modmenu::unregisterModule(kModuleId);
    getSelf().getLogger().info("ResScale desativado, resolução restaurada");
    return true;
  }

  bool unload() {
    mConfigFile.reset();
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
  std::optional<pl::config::ConfigFile<ResScaleConfig>> mConfigFile;
  pl::memory::HookHandle mHook;
  FromSurfaceFn mOriginal{};

  std::mutex mWindowMutex;
  ANativeWindow *mWindow{nullptr};
  int32_t mNativeWidth{0};
  int32_t mNativeHeight{0};

  void normalizeConfig() {
    if (!mConfigFile) {
      return;
    }
    auto &cfg = mConfigFile->value();
    cfg.percent =
        std::clamp(cfg.percent, resscale::kMinPercent, resscale::kMaxPercent);
  }

  // --- Hook: ANativeWindow_fromSurface -------------------------------------

  static ANativeWindow *detourFromSurface(JNIEnv *env, jobject surface) {
    return instance().handleFromSurface(env, surface);
  }

  ANativeWindow *handleFromSurface(JNIEnv *env, jobject surface) {
    ANativeWindow *window = mOriginal(env, surface);
    if (window == nullptr) {
      return window;
    }

    std::lock_guard<std::mutex> lock(mWindowMutex);
    mWindow = window;
    // Lê o tamanho ANTES de aplicar qualquer geometria customizada, senão
    // uma leitura futura devolveria o tamanho já escalado, não o nativo.
    mNativeWidth = ANativeWindow_getWidth(window);
    mNativeHeight = ANativeWindow_getHeight(window);
    applyScaleLocked();
    return window;
  }

  // --- Mod Menu: slider de resolução ---------------------------------------

  static void onConfigChanged(std::string_view moduleId, std::string_view key,
                              std::string_view value) {
    instance().handleConfigChanged(moduleId, key, value);
  }

  void handleConfigChanged(std::string_view moduleId, std::string_view key,
                           std::string_view value) {
    if (moduleId != kModuleId || key != kPercentKey || !mConfigFile) {
      return;
    }

    auto &cfg = mConfigFile->value();
    cfg.percent = std::clamp(parseInt(value, cfg.percent),
                             resscale::kMinPercent, resscale::kMaxPercent);
    mConfigFile->save();

    std::lock_guard<std::mutex> lock(mWindowMutex);
    applyScaleLocked();
  }

  // --- Aplicação real do downscale ------------------------------------------

  void applyScaleLocked() {
    if (mWindow == nullptr || mNativeWidth <= 0 || mNativeHeight <= 0) {
      return;
    }

    const int percent = mConfigFile ? mConfigFile->value().percent : 100;
    if (percent >= resscale::kMaxPercent) {
      // width=height=0 faz o Android voltar a usar o tamanho nativo da
      // superfície (documentado no NDK: ANativeWindow_setBuffersGeometry).
      ANativeWindow_setBuffersGeometry(mWindow, 0, 0, 0);
      return;
    }

    // Arredonda pra baixo em número par (alinhamento amigável pra GPU).
    int32_t scaledWidth = (mNativeWidth * percent / 100) & ~1;
    int32_t scaledHeight = (mNativeHeight * percent / 100) & ~1;
    scaledWidth = std::max(scaledWidth, 2);
    scaledHeight = std::max(scaledHeight, 2);

    ANativeWindow_setBuffersGeometry(mWindow, scaledWidth, scaledHeight, 0);
  }

  void resetToNativeLocked() {
    std::lock_guard<std::mutex> lock(mWindowMutex);
    if (mWindow != nullptr) {
      ANativeWindow_setBuffersGeometry(mWindow, 0, 0, 0);
    }
  }
};

PL_REGISTER_MOD(ResScaleMod, ResScaleMod::instance())
