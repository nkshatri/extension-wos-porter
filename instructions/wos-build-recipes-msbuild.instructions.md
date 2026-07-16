---
description: "Windows ARM64 recipes for MSBuild / Visual Studio projects (.sln solution config, .vcxproj platform + PropertyGroup + ItemDefinitionGroup cloning, .props updates, NuGet native runtime.win-arm64 packages). Auto-loaded on VS project files."
applyTo: "**/*.vcxproj,**/*.sln,**/*.props,**/*.targets,**/packages.config"
---

# MSBuild / Visual Studio — Windows ARM64 recipes

## 1. Solution (`.sln`)

Duplicate every `x64` entry for `ARM64`.

`GlobalSection(SolutionConfigurationPlatforms)`:
```
Debug|ARM64 = Debug|ARM64
Release|ARM64 = Release|ARM64
```

`GlobalSection(ProjectConfigurationPlatforms)` — per project GUID:
```
{GUID}.Debug|ARM64.ActiveCfg   = Debug|ARM64
{GUID}.Debug|ARM64.Build.0     = Debug|ARM64
{GUID}.Release|ARM64.ActiveCfg = Release|ARM64
{GUID}.Release|ARM64.Build.0   = Release|ARM64
```

## 2. Project (`.vcxproj`)

### Add ARM64 to the platform list
```xml
<ProjectConfiguration Include="Debug|ARM64">
  <Configuration>Debug</Configuration>
  <Platform>ARM64</Platform>
</ProjectConfiguration>
<ProjectConfiguration Include="Release|ARM64">
  <Configuration>Release</Configuration>
  <Platform>ARM64</Platform>
</ProjectConfiguration>
```

### Clone the x64 PropertyGroups
```xml
<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|ARM64'" Label="Configuration">
  <ConfigurationType>Application</ConfigurationType>
  <PlatformToolset>v143</PlatformToolset>
</PropertyGroup>
<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|ARM64'" Label="Configuration">
  <ConfigurationType>Application</ConfigurationType>
  <PlatformToolset>v143</PlatformToolset>
</PropertyGroup>
```

### Clone the x64 ItemDefinitionGroups
```xml
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|ARM64'">
  <ClCompile>
    <PreprocessorDefinitions>WIN32;NDEBUG;TARGET_ARM64;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    <!-- Remove /arch:AVX*, /arch:SSE2 -->
  </ClCompile>
  <Link>
    <!-- Update AdditionalLibraryDirectories to ARM64 paths -->
  </Link>
</ItemDefinitionGroup>
```

### Remove x64-only knobs from the ARM64 config
- `<AdditionalOptions>` — strip `/arch:SSE2`, `/arch:AVX`, `/arch:AVX2`, `/favor:*`
- Update `<AdditionalLibraryDirectories>` from `x64`/`amd64` to `arm64`
- Force machine target: `<Link>` `<TargetMachine>MachineARM64</TargetMachine>` (or `/MACHINE:ARM64`)
- `<RandomizedBaseAddress>true</RandomizedBaseAddress>` (ARM64 requires ASLR — LNK1246)
- Replace `<MASM>` entries with `<MARMASM>` for ARM64 assembly files
- Drop `<MinimalRebuild>` on ARM64 config (D9035 deprecated)

## 3. Property sheets (`.props`)

```xml
<PropertyGroup Condition="'$(Platform)'=='ARM64'">
  <LibraryPath>$(SolutionDir)lib\arm64;%(LibraryPath)</LibraryPath>
</PropertyGroup>
```

## 4. NuGet native packages

`packages.config` referencing `runtime.win-x64.*` needs `runtime.win-arm64.*` counterparts. If a native package only ships `x64`/`x86` under `build/native/` or `runtimes/`, the package is not ARM64-ready — file an upstream issue, build from source, or replace.

## Build command

```powershell
& $msbuild "<solution>.sln" /t:Build /p:Configuration=Release /p:Platform=ARM64 /m
```
