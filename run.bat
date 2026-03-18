@echo off
C:\msys64\mingw64\bin\qemu-system-i386.exe -drive format=raw,file=%~dp0claos.img -m 128M
