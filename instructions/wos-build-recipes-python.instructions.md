---
description: "Windows ARM64 recipes for Python C-extension projects (setup.py / pyproject.toml, cibuildwheel CIBW_ARCHS_WINDOWS ARM64, wheel availability probe, source-build fallback)."
applyTo: "**/setup.py,**/setup.cfg,**/pyproject.toml,**/*.pyx,**/*.pxd"
---

# Python C extensions — Windows ARM64 recipes

## 1. `setup.py` / `pyproject.toml` — arch guards

```python
import platform, sys
is_arm64 = platform.machine().lower() in ('arm64', 'aarch64')

if is_arm64:
    extra_compile_args = ['/DTARGET_ARM64=1']
else:
    extra_compile_args = ['/DTARGET_X64=1', '/arch:AVX2']
```

## 2. Wheel availability probe

```powershell
pip download --platform win_arm64 --only-binary=:all: <pkg>
```

If nothing downloads, no ARM64 wheel exists — fall back to source build.

## 3. Source build fallback

Under `vcvarsarm64.bat` (or `vcvarsamd64_arm64.bat`):
```powershell
python setup.py build_ext --plat-name=win-arm64
pip install --no-binary=:all: <pkg>
```

## 4. `cibuildwheel` CI matrix

```yaml
env:
  CIBW_ARCHS_WINDOWS: "AMD64 ARM64"
  CIBW_ARCHS: "auto64"
```

## 5. Cython

Cython transpiles fine for ARM64. Ensure the generated `.c` file is compiled with the ARM64 `cl.exe` (loaded via vcvars).

## Blocking notes

- **`numpy`, `scipy`** — ARM64 Windows wheels exist for recent versions.
- **Older packages** may lack ARM64 wheels AND fail to build from source. Record each blocker in the port's Limitations section with: package, version, what was tried, workaround.
- Never silently substitute an x64 wheel — the whole environment must be ARM64.
