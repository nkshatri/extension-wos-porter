---
description: "Windows ARM64 recipes for CMake projects (root CMakeLists.txt platform detection, arch-conditional sources, compiler-flag guards, cross-compilation toolchain file, vcpkg triplet integration). Auto-loaded on CMake files."
applyTo: "**/CMakeLists.txt,**/*.cmake,**/CMakePresets.json,**/cmake/**"
---

# CMake — Windows ARM64 recipes

## 1. Root `CMakeLists.txt` — platform detection

Near existing platform checks add:

```cmake
# ARM64 support
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
    set(TARGET_ARM64 TRUE)
    add_definitions(-DTARGET_ARM64=1)
    message(STATUS "Building for ARM64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(TARGET_X64 TRUE)
    add_definitions(-DTARGET_X64=1)
    message(STATUS "Building for x64")
endif()
```

If the project already has processor checks (e.g. `if(CMAKE_SIZEOF_VOID_P EQUAL 8)`), extend them rather than duplicating.

## 2. Architecture-conditional sources

```cmake
if(TARGET_ARM64)
    target_sources(${PROJECT_NAME} PRIVATE src/arch/arm64/impl.c)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:-march=armv8-a+simd>)
elseif(TARGET_X64)
    target_sources(${PROJECT_NAME} PRIVATE src/arch/x64/impl.c)
endif()
```

## 3. Compiler flags

Guard x64-only flags:

```cmake
if(TARGET_X64)
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
elseif(TARGET_ARM64)
    # NEON is implicit on MSVC ARM64
endif()
```

Remove or guard: `/arch:SSE2`, `/arch:AVX`, `/arch:AVX2`, `/arch:AVX512`, `-msse*`, `-mavx*`.

## 4. Cross-compile toolchain file

`cmake/arm64-windows-toolchain.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)
set(CMAKE_C_COMPILER cl)
set(CMAKE_CXX_COMPILER cl)
set(CMAKE_C_COMPILER_TARGET arm64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET arm64-pc-windows-msvc)
set(CMAKE_GENERATOR_PLATFORM ARM64)
```

## 5. vcpkg triplet selection

```cmake
if(TARGET_ARM64)
    set(VCPKG_TARGET_TRIPLET "arm64-windows" CACHE STRING "")
else()
    set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "")
endif()
```

## Configure/build commands

```powershell
cmake -S . -B build-arm64 -A ARM64 -DBUILD_TESTING=ON -DBUILD_EXAMPLES=ON
cmake --build build-arm64 --config Release --parallel
```
