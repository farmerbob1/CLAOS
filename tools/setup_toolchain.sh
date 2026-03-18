#!/bin/bash
#
# CLAOS — Toolchain Setup Script
#
# Installs the cross-compiler and tools needed to build CLAOS.
# Run this ONCE before your first build.
#
# Supports: MSYS2 (Windows), Ubuntu/Debian (WSL or native Linux), macOS
#

set -e

echo "=== CLAOS Toolchain Setup ==="
echo ""

# Detect platform
if [ -f /etc/os-release ]; then
    . /etc/os-release
    PLATFORM="$ID"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "mingw"* ]]; then
    PLATFORM="msys2"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macos"
else
    PLATFORM="unknown"
fi

echo "Detected platform: $PLATFORM"
echo ""

case "$PLATFORM" in
    msys2)
        echo "Installing via MSYS2 pacman..."
        echo "NOTE: You need MSYS2 installed (https://www.msys2.org/)"
        echo "      Run this script from the MSYS2 MINGW64 terminal."
        echo ""
        pacman -S --needed --noconfirm \
            mingw-w64-x86_64-cross-i686-elf-gcc \
            mingw-w64-x86_64-cross-i686-elf-binutils \
            nasm \
            mingw-w64-x86_64-qemu \
            make
        ;;

    ubuntu|debian|pop)
        echo "Installing via apt..."
        sudo apt update
        sudo apt install -y \
            gcc-i686-linux-gnu \
            binutils-i686-linux-gnu \
            nasm \
            qemu-system-x86 \
            make

        # On Ubuntu/Debian, the cross-compiler is named differently.
        # We create symlinks so the Makefile works.
        echo ""
        echo "NOTE: Ubuntu uses 'i686-linux-gnu-gcc' instead of 'i686-elf-gcc'."
        echo "You may need to update CC/LD in the Makefile, or build a proper"
        echo "i686-elf cross-compiler from source. For quick testing, try:"
        echo "  make CC=i686-linux-gnu-gcc LD=i686-linux-gnu-ld"
        echo ""
        echo "For a proper freestanding cross-compiler, see:"
        echo "  https://wiki.osdev.org/GCC_Cross-Compiler"
        ;;

    macos)
        echo "Installing via Homebrew..."
        brew install nasm qemu
        brew install i686-elf-gcc i686-elf-binutils || {
            echo ""
            echo "If the above fails, try:"
            echo "  brew tap nativeos/i686-elf-toolchain"
            echo "  brew install i686-elf-gcc i686-elf-binutils"
        }
        ;;

    *)
        echo "Unknown platform. Please install manually:"
        echo "  - i686-elf-gcc (freestanding cross-compiler)"
        echo "  - i686-elf-ld (from binutils)"
        echo "  - nasm (assembler)"
        echo "  - qemu-system-i386 (emulator)"
        echo "  - make"
        echo ""
        echo "See: https://wiki.osdev.org/GCC_Cross-Compiler"
        exit 1
        ;;
esac

echo ""
echo "=== Verifying installation ==="

check_tool() {
    if command -v "$1" &>/dev/null; then
        echo "  [OK] $1 found: $(command -v "$1")"
    else
        echo "  [!!] $1 NOT FOUND"
    fi
}

check_tool i686-elf-gcc
check_tool i686-elf-ld
check_tool nasm
check_tool qemu-system-i386
check_tool make

echo ""
echo "=== Setup complete! Run 'make' to build CLAOS. ==="
