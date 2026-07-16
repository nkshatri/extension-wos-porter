---
description: "Windows ARM64 recipes for node-gyp native addons (binding.gyp target_arch conditions, package.json prebuild for win32-arm64, npm_config_arch cross-install)."
---

# node-gyp — Windows ARM64 recipes

## 1. `binding.gyp`

```json
{
  "targets": [{
    "target_name": "myaddon",
    "sources": ["src/addon.cc"],
    "conditions": [
      ["target_arch=='arm64'", {
        "defines": ["TARGET_ARM64=1"],
        "sources": ["src/arch/arm64/impl.cc"]
      }],
      ["target_arch=='x64'", {
        "defines": ["TARGET_X64=1"],
        "sources": ["src/arch/x64/impl.cc"],
        "msvs_settings": {
          "VCCLCompilerTool": {
            "AdditionalOptions": ["/arch:AVX2"]
          }
        }
      }]
    ]
  }]
}
```

## 2. `package.json` — prebuild + install scripts

If using `prebuildify` or `node-pre-gyp`, ensure `win32-arm64` is in the target matrix. Otherwise `npm install --build-from-source` on the target device.

## Build commands

Native (ARM64 Node):
```powershell
npx node-gyp rebuild --arch=arm64
```

Cross-install from x64 host:
```powershell
$env:npm_config_arch = 'arm64'; $env:npm_config_target_arch = 'arm64'
npm rebuild
```

Common offenders that need `--build-from-source` or ARM64 prebuilts: `bcrypt`, `sharp`, `node-sass`, `canvas`. Confirm each publishes `win32-arm64` binaries.
