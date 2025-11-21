# BUILDING.md

Ginkgo depends on **GLFW**, **OpenGL**, **libcurl**, **GNU make**, and the **clang** compiler.  
These are needed both to **build** Ginkgo and also at **runtime**, because Ginkgo compiles small C/C++ snippets into a shared library using `clang++`.

Below are brief setup notes for macOS, Linux, and Windows.  
Adjust paths as needed for your environment.

> WARNING - i havent tested these much yet :)

---

## macOS

### Requirements
- Xcode command line tools (provides `clang` and system headers)  
  ```sh
  xcode-select --install
  ```
- Homebrew (optional, recommended): https://brew.sh

### Install dependencies
```sh
brew install glfw curl
```

### Build
```sh
./m
```

### Run
```sh
./g
```

---

## Linux (example: Ubuntu / Debian)

### Install dependencies
```sh
sudo apt update
sudo apt install     clang make pkg-config libglfw3-dev libcurl4-openssl-dev     mesa-common-dev libgl1-mesa-dev     libasound2-dev
```

(`libasound2-dev` is for raw MIDI/ALSA.)

### Build
```sh
./m
```

### Run
```sh
./g
```

---

## Windows (MSYS2 / MinGW)

### 1. Requirements

First, install MSYS2 from: https://www.msys2.org

After installation, start the MingW shell by searching in the start menu for `MSYS2 MinGW x64` or `MSYS2 CLANGARM64`

> Do **not** use the generic “MSYS” shell.  
> Always use the **MinGW** shells - search in your start menu for:  
> - **x64:** `MSYS2 MinGW x64` (AKA `mingw64.exe`)  
> - **ARM64:** `MSYS2 CLANGARM64` (AKA `clangarm64.exe`)

### 2. Update MSYS2 packages

Open the correct shell (as above), then:

```sh
pacman -Syu
```

If it asks you to close the terminal, do so, reopen the same shell, and rerun the same command until no further updates are needed.

### 3. Install dependencies
x64:
```sh
pacman -S     mingw-w64-x86_64-clang     mingw-w64-x86_64-make     mingw-w64-x86_64-pkgconf     mingw-w64-x86_64-glfw     mingw-w64-x86_64-curl
```
arm64:
```sh
pacman -S     mingw-w64-clang-aarch64-clang     mingw-w64-clang-aarch64-make     mingw-w64-clang-aarch64-pkgconf     mingw-w64-clang-aarch64-glfw     mingw-w64-clang-aarch64-curl
```

### Build
```sh
mingw32-make.exe
```

### Run
```sh
./ginkgo_windows.exe
```

