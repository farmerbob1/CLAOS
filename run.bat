@echo off
C:\msys64\mingw64\bin\qemu-system-i386.exe -drive format=raw,file=%~dp0claos.img -device e1000,netdev=net0 -netdev user,id=net0 -audiodev sdl,id=snd0,in.voices=0 -device AC97,audiodev=snd0 -m 128M
