@echo off
setlocal
pushd "%~dp0"

where texassemble >nul 2>&1 || (echo [ERROR] texassemble not in PATH & pause & exit /b 1)
where texconv     >nul 2>&1 || (echo [ERROR] texconv not in PATH & pause & exit /b 1)

rem --- 1) 큐브맵 조립(아직 밉맵 없음) ---
texassemble cube -if LINEAR -f R8G8B8A8_UNORM -o casper_color.dds ^
  PX_color.png NX_color.png PY_color.png NY_color.png PZ_color.png NZ_color.png || goto :err

texassemble cube -if LINEAR -f R16_UNORM -o casper_depth.dds ^
  PX_depth.png NX_depth.png PY_depth.png NY_depth.png PZ_depth.png NZ_depth.png || goto :err

rem --- 2) texconv로 풀 밉체인 생성(-m 0 = 최대 밉) ---
texconv -y -o . -f R8G8B8A8_UNORM -if LINEAR -m 0 casper_color.dds || goto :err
rem 깊이 큐브맵은 평균 다운샘플이 의미와 다를 수 있음. 필요시 전용 컴퓨트로 MIN/MAX 생성 권장.
texconv -y -o . -f R16_UNORM      -if POINT  -m 0 casper_depth.dds || goto :err

echo [OK] Wrote casper_color.dds and casper_depth.dds with full mip chains
pause
exit /b 0

:err
echo [FAIL] tex* returned %ERRORLEVEL%
pause
exit /b %ERRORLEVEL%
