@echo off
cd /d %~dp0

git submodule update --init --recursive

if exist dependencies\detours\lib.X64\detours.lib (
    exit
) else (
    cd dependencies\detours
    nmake
)
