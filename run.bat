@echo off
C:\msys64\mingw64\bin\qemu-system-i386.exe -drive format=raw,file=%~dp0claos.img -device e1000,netdev=net0 -netdev user,id=net0 -m 128M
