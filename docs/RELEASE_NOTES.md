# Release Notes

## v1.0.8-alpha

### Changes
- **Windows installer**: Switched from a plain `.zip` archive to a proper `.msi` installer built with WiX Toolset v4.
  The installer registers all three tools system-wide and adds them to the system `PATH` automatically.
- **Linux packages**: Added native package formats for major Linux distributions:
  - `.deb` — Debian, Ubuntu, and derivatives (x86_64 / arm64)
  - `.rpm` — Red Hat, Fedora, CentOS, openSUSE, and derivatives (x86_64 / aarch64)
- Generic `.tar.gz` archives are still provided for all platforms as before.

### Components
- **sub**: Transpiler (SUB → Python, JavaScript, C, Go, Rust, and more)
- **subc**: Native compiler (SUB → machine binary via C backend)
- **subi**: Tree-walk interpreter (direct execution)

### Supported Platforms
- Linux x86_64
- Linux arm64 (aarch64)
- macOS arm64 (Apple Silicon)
- Windows x86_64 (MSI installer)

---

## v1.0.8-beta

### Changes
- **Removed macOS x86_64 (Intel) builds** — GitHub Actions no longer provides free Intel macOS runners. Only Apple Silicon (arm64) builds are shipped going forward.

### Components
- **sub**: Transpiler (SUB → Python, JavaScript, C, Go, Rust, and more)
- **subc**: Native compiler (SUB → machine binary via C backend)
- **subi**: Tree-walk interpreter (direct execution)

### Supported Platforms
- Linux x86_64
- Linux arm64 (aarch64)
- macOS arm64 (Apple Silicon)
- Windows x86_64 (MinGW)
