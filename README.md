# ResScale — LeviLauncher Android native mod

ResScale adds a 30%–100% resolution slider to LeviLauncher Mod Menu.

## ABI compatibility fix

The original build used `preloader-android` 0.2.2 headers. That `ModuleInfo` ends after `onConfigChanged`, while newer LeviLauncher/preloader builds add `onKeybind`. Passing the old struct layout into the newer runtime can crash inside `pl::modmenu::registerModule`.

This version uses a local `ModMenuCompat.hpp` with the current `ModuleInfo` layout, while keeping the rest of the SDK dependency unchanged. This specifically addresses the ABI mismatch seen in the tombstone.

## Build

GitHub Actions builds `arm64-v8a` and creates `resscale-v1.0.1-arm64-v8a.levipack`.
