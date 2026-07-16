---
description: "Windows ARM64 CI/CD recipes aligned with Arm AppReady (GitHub Actions windows-11-arm runner, AppVeyor, Azure Pipelines, GitLab CI). Auto-loaded on workflow files. Provides drop-in matrix entries that mirror an existing x64 job."
applyTo: "**/.github/workflows/*.yml,**/.github/workflows/*.yaml,**/appveyor.yml,**/.appveyor.yml,**/azure-pipelines.yml,**/azure-pipelines.yaml,**/.gitlab-ci.yml,**/.circleci/config.yml,**/Jenkinsfile"
---

# Windows ARM64 CI/CD recipes

Align with the Arm AppReady **Build** stage: every existing x64/AMD64 job should be mirrored by an ARM64 job on a native ARM64 runner. Do NOT drop the x64 job — CI must exercise both targets.

Reference: [Arm AppReady for Windows on Arm](https://developer.arm.com/laptops-and-desktops/windows-app-ready) and [Set up Visual Studio for Windows on Arm](https://learn.microsoft.com/en-us/visualstudio/install/visual-studio-on-arm-devices).

## GitHub Actions — `windows-11-arm` runner

Native ARM64 hosted runner (GA on GitHub-hosted runners). Mirror any existing `windows-latest` / `windows-2022` job with a `windows-11-arm` sibling.

```yaml
jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: windows-latest
            platform: x64
            vcpkg_triplet: x64-windows
            cargo_target: x86_64-pc-windows-msvc
            cmake_arch: x64
          - os: windows-11-arm          # ARM64 native runner
            platform: ARM64
            vcpkg_triplet: arm64-windows
            cargo_target: aarch64-pc-windows-msvc
            cmake_arch: ARM64
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Set up MSVC
        uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: ${{ matrix.platform == 'ARM64' && 'arm64' || 'x64' }}
      - name: Build (MSBuild)
        if: contains(fromJson('["msbuild"]'), matrix.build_system)
        run: msbuild MyProject.sln /t:Build /p:Configuration=Release /p:Platform=${{ matrix.platform }} /m
      - name: Build (CMake)
        if: contains(fromJson('["cmake"]'), matrix.build_system)
        run: |
          cmake -S . -B build -A ${{ matrix.cmake_arch }}
          cmake --build build --config Release --parallel
      - name: Build (Cargo)
        if: contains(fromJson('["cargo"]'), matrix.build_system)
        run: cargo build --target ${{ matrix.cargo_target }} --release
      - name: Verify architecture with dumpbin
        run: |
          Get-ChildItem -Recurse -Include *.exe,*.dll |
            ForEach-Object { dumpbin /HEADERS $_.FullName | Select-String 'machine \(' }
      - name: Test
        if: matrix.platform == 'ARM64'  # tests run natively on ARM64 runner
        run: ctest --test-dir build -C Release --output-on-failure
```

Notes:
- Use `windows-11-arm` (not `windows-arm`). The runner ships MSVC ARM64 toolset, CMake, and the Windows 11 SDK.
- Tests can execute natively on the ARM64 runner; x64 host must skip execution (only build + `dumpbin` verify).
- For **cibuildwheel** (Python C ext): `CIBW_ARCHS_WINDOWS: "AMD64 ARM64"` and it will pick the right runner automatically.

## AppVeyor

```yaml
image: Visual Studio 2022
platform:
  - x64
  - ARM64
configuration: Release
build:
  parallel: true
  verbosity: minimal
build_script:
  - msbuild MyProject.sln /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /m
test_script:
  - ps: |
      if ($env:PLATFORM -eq 'ARM64') {
        # AppVeyor's Windows images are x64; ARM64 build runs, tests are skipped
        Write-Host "Cross-compiled on x64 host — tests deferred to native ARM64 device"
      } else {
        ctest --test-dir build -C Release --output-on-failure
      }
```

## Azure Pipelines

```yaml
strategy:
  matrix:
    x64:
      buildPlatform: 'x64'
    ARM64:
      buildPlatform: 'ARM64'
pool:
  vmImage: 'windows-2022'   # host is x64; ARM64 build is cross-compile
steps:
  - task: VSBuild@1
    inputs:
      solution: '**/*.sln'
      platform: '$(buildPlatform)'
      configuration: 'Release'
  - powershell: |
      Get-ChildItem -Recurse -Include *.exe,*.dll |
        ForEach-Object { dumpbin /HEADERS $_.FullName | Select-String 'machine \(' }
```

For native ARM64 tests, add a self-hosted ARM64 agent pool and route the ARM64 job with `pool: { name: 'ARM64Pool' }`.

## GitLab CI

```yaml
build-arm64:
  tags: [windows, arm64]
  script:
    - cmake -S . -B build -A ARM64
    - cmake --build build --config Release --parallel
    - Get-ChildItem -Recurse -Include *.exe,*.dll | ForEach-Object { dumpbin /HEADERS $_.FullName | Select-String 'machine \(' }
```

Requires a self-hosted ARM64 runner registered with the `arm64` tag.

## CircleCI

CircleCI has no first-party Windows-on-ARM executor — build on a Windows x64 executor as cross-compile and defer tests to a self-hosted ARM64 machine, or drop CircleCI in favor of GitHub Actions for ARM64.

## Jenkins

Add a `windows-arm64` label to a Jenkins agent (self-hosted; see the MS-Learn setup guide) and pin the ARM64 job with `agent { label 'windows-arm64' }`.

## Cross-cutting rules

- **Never** substitute an x64 build artifact for an ARM64 job. If cross-compile succeeds but no native runner is available, mark tests as "deferred to native ARM64 device" — do not fake a pass.
- Add a `dumpbin /HEADERS` step so CI itself proves every output is `machine (AA64)`. This catches regressions where a stray dependency stays x64.
- For Python / Node projects, prefer the platform-native toolchain (`cibuildwheel` for wheels, `npm rebuild` with `npm_config_target_arch=arm64`). The extension's [wos-build-recipes-python](wos-build-recipes-python.instructions.md) and [wos-build-recipes-nodegyp](wos-build-recipes-nodegyp.instructions.md) recipes cover the cross-install commands used inside the CI script.
- Cite the Arm AppReady program in the ARM64 job comment so downstream reviewers know it is a native ARM64 target, not emulation.
