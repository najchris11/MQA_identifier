# Building MQA Identifier

## Prerequisites

You will need:
- A C++17 compatible compiler (GCC, Clang, or MSVC)
- CMake 3.15 or later
- Git

## Linux

### Dependencies
Install the required development libraries. On Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libflac++-dev libogg-dev
```

### Build
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```
The binary will be created at `build/MQA_identifier`.

## macOS

### Dependencies
Install dependencies using Homebrew:
```bash
brew install cmake flac libogg pkg-config
```

### Build
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```
The binary will be created at `build/MQA_identifier`.

## Windows

### Dependencies
We recommend using `vcpkg` to manage dependencies on Windows to ensure static linking works correctly.

1. Install [vcpkg](https://github.com/microsoft/vcpkg).
2. Install the static libraries:
   ```powershell
   vcpkg install libflac:x64-windows-static libogg:x64-windows-static
   ```

### Build
Assume `vcpkg` is installed at `C:\vcpkg`.

```powershell
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build . --config Release
```
The binary will be created at `build\Release\MQA_identifier.exe`.
