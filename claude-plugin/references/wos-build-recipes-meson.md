---
description: "Windows ARM64 recipes for Meson projects (aarch64 cross file, meson.build host_cpu detection, guarded compiler args, arch-specific source lists)."
---

# Meson — Windows ARM64 recipes

## 1. Cross file — `cross/arm64-windows.txt`

```ini
[binaries]
c       = 'cl'
cpp     = 'cl'
ar      = 'lib'
windres = 'rc'

[host_machine]
system     = 'windows'
cpu_family = 'aarch64'
cpu        = 'aarch64'
endian     = 'little'

[built-in options]
c_args   = ['/DTARGET_ARM64=1']
cpp_args = ['/DTARGET_ARM64=1']
```

## 2. `meson.build` — architecture detection

```meson
host_cpu = host_machine.cpu_family()

if host_cpu == 'x86_64'
  add_project_arguments('-DTARGET_X64=1', language: ['c', 'cpp'])
elif host_cpu == 'aarch64'
  add_project_arguments('-DTARGET_ARM64=1', language: ['c', 'cpp'])
endif
```

## 3. Guard x64-only compiler args

```meson
if host_cpu == 'x86_64'
  add_project_arguments('/arch:AVX2', language: ['c', 'cpp'])
endif
```

## 4. Arch-specific sources

```meson
if host_cpu == 'aarch64'
  project_sources += files('src/arch/arm64/impl.c')
elif host_cpu == 'x86_64'
  project_sources += files('src/arch/x64/impl.c')
endif
```

## Configure & build

```powershell
# Load ARM64 vcvars first (vcvarsarm64.bat or vcvarsamd64_arm64.bat)
meson setup build-arm64 --cross-file cross/arm64-windows.txt
meson compile -C build-arm64
```
