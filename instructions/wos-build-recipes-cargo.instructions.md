---
description: "Windows ARM64 recipes for Cargo/Rust projects (target triple aarch64-pc-windows-msvc, .cargo/config.toml, cfg(target_arch) dependencies, build.rs, *-sys crate pkg-config integration)."
applyTo: "**/Cargo.toml,**/Cargo.lock,**/.cargo/config.toml,**/build.rs"
---

# Cargo (Rust) — Windows ARM64 recipes

## 1. `.cargo/config.toml`

```toml
[target.aarch64-pc-windows-msvc]
rustflags = ["-C", "target-feature=+neon"]
```

## 2. `Cargo.toml` — arch-specific deps

```toml
[target.'cfg(target_arch = "aarch64")'.dependencies]
# ARM64-only dependencies

[target.'cfg(all(target_arch = "aarch64", target_os = "windows"))'.dependencies]
# Windows-on-ARM specific
```

## 3. `build.rs`

```rust
fn main() {
    let target_arch = std::env::var("CARGO_CFG_TARGET_ARCH").unwrap_or_default();
    match target_arch.as_str() {
        "x86_64"  => println!("cargo:rustc-cfg=target_x64"),
        "aarch64" => println!("cargo:rustc-cfg=target_arm64"),
        _ => {}
    }
}
```

## 4. `*-sys` crates with C deps

- The `cc` crate auto-selects the ARM64 `cl.exe` when the target is set.
- For pkg-config wrappers set `<PKG>_LIB_DIR` / `<PKG>_INCLUDE_DIR` to ARM64 paths (e.g. vcpkg `installed/arm64-windows`).
- Set `CARGO_TARGET_AARCH64_PC_WINDOWS_MSVC_LINKER` only if a non-default linker is required.
- Common breakages: vendored asm in `ring`, `openssl-sys`, etc. — verify ARM64 support before assuming.

## 5. cfg guards inside source

```rust
#[cfg(all(target_arch = "aarch64", target_os = "windows"))]
mod arm64 { use core::arch::aarch64::*; /* NEON path */ }

#[cfg(target_arch = "x86_64")]
mod x64 { use core::arch::x86_64::*; /* SSE path */ }
```

## Build & test commands

```powershell
cargo build --target aarch64-pc-windows-msvc --release
cargo build --target aarch64-pc-windows-msvc --release --examples
cargo test  --target aarch64-pc-windows-msvc --release --no-run
```
