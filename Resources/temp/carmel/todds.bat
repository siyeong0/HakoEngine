@echo off
setlocal
pushd "%~dp0"

where texassemble >nul 2>&1 || (echo [ERROR] texassemble not in PATH & pause & exit /b 1)

rem --- Color cubemap (RGBA8 -> R8G8B8A8_UNORM) ---
texassemble cube -if LINEAR -f R8G8B8A8_UNORM -o casper_color.dds ^
  PX_color.png NX_color.png PY_color.png NY_color.png PZ_color.png NZ_color.png
if errorlevel 1 goto :err

rem --- Depth cubemap (Gray16 PNGs -> R16_UNORM) ---
texassemble cube -if LINEAR -f R16_UNORM -o casper_depth.dds ^
  PX_depth.png NX_depth.png PY_depth.png NY_depth.png PZ_depth.png NZ_depth.png
if errorlevel 1 goto :err

echo [OK] Wrote casper_color.dds and casper_depth.dds
pause
exit /b 0

:err
echo [FAIL] texassemble returned %ERRORLEVEL%
pause
exit /b %ERRORLEVEL%
