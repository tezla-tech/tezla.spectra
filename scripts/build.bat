@echo off
REM Thin wrapper so the build works from a plain cmd.exe prompt too.
REM Everything after this is handled by build.ps1 -- see docs/BUILD.md.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
