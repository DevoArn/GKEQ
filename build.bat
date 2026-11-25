@echo off
rem ---------------------------------------------------------------------------
rem  GKEQ build script (VS 2019 Build Tools + bundled CMake/Ninja)
rem
rem    build.bat            Release build of the VST3 + Standalone
rem    build.bat installer  Release build + Windows installer -> dist\
rem    build.bat tests      Build and run the DSP test runner
rem    build.bat hosttest   Load the built VST3 in a test host
rem    build.bat snapshot   Render the editor (and app icon) to PNG files
rem    build.bat debug      Debug build of the plugin
rem ---------------------------------------------------------------------------
setlocal

set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
set "CMAKEDIR=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CMAKEDIR%\CMake\bin;%CMAKEDIR%\Ninja;%PATH%"

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Could not initialise the MSVC environment.
    exit /b 1
)

set "SRC=%~dp0"
set "OUT=%SRC%build\ModernEQ_artefacts\Release"

rem Fresh clone: fetch JUCE + PFFFT (git submodules)
if not exist "%SRC%external\JUCE\CMakeLists.txt" git -C "%SRC%." submodule update --init --depth 1 || exit /b 1

rem JUCE 7.0.12 asks for CMake 3.22; VS 2019 ships 3.20, which works fine.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "foreach ($f in @('CMakeLists.txt', 'extras\Build\CMake\JUCEModuleSupport.cmake', 'extras\Build\CMake\JUCEUtils.cmake')) {" ^
  "  $p = Join-Path '%SRC%external\JUCE' $f; $t = [IO.File]::ReadAllText($p);" ^
  "  if ($t.Contains('cmake_minimum_required(VERSION 3.22)')) { [IO.File]::WriteAllText($p, $t.Replace('cmake_minimum_required(VERSION 3.22)', 'cmake_minimum_required(VERSION 3.20)')) } }"
if /I "%1"=="tests" goto tests
if /I "%1"=="debug" goto debug
if /I "%1"=="hosttest" goto hosttest
if /I "%1"=="snapshot" goto snapshot
if /I "%1"=="installer" goto installer

:release
call :build_plugin || exit /b 1
echo.
echo VST3 bundle: %OUT%\VST3\GKEQ.vst3
exit /b 0

:installer
call :build_plugin || exit /b 1
python "%SRC%Tools\make_payload.py" "%SRC%build" "%SRC%build\installer\payload.zip" "%SRC%." || exit /b 1
cmake -S "%SRC%." -B "%SRC%build-installer" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMEQ_BUILD_PLUGIN=OFF -DMEQ_BUILD_INSTALLER=ON "-DMEQ_PAYLOAD=%SRC%build\installer\payload.zip" || exit /b 1
cmake --build "%SRC%build-installer" --target ModernEQSetup ModernEQSetupTest || exit /b 1
if not exist "%SRC%dist" mkdir "%SRC%dist"
copy /Y "%SRC%build-installer\ModernEQSetup_artefacts\Release\GKEQ Setup.exe" "%SRC%dist\GKEQ 1.0.0 Setup.exe" >nul || exit /b 1
echo.
echo Installer: %SRC%dist\GKEQ 1.0.0 Setup.exe
exit /b 0

:debug
cmake -S "%SRC%." -B "%SRC%build-debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug || exit /b 1
cmake --build "%SRC%build-debug" --target ModernEQ_VST3 ModernEQ_Standalone || exit /b 1
exit /b 0

:hosttest
call :unlock
cmake -S "%SRC%." -B "%SRC%build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%SRC%build" --target ModernEQ_VST3 ModernEQHostTest || exit /b 1
pushd "%SRC%"
"%SRC%build\ModernEQHostTest_artefacts\Release\ModernEQHostTest.exe" "build\ModernEQ_artefacts\Release\VST3\GKEQ.vst3" "build\editor.png"
set RES=%errorlevel%
popd
exit /b %RES%

:snapshot
cmake -S "%SRC%." -B "%SRC%build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%SRC%build" --target ModernEQUISnapshot || exit /b 1
pushd "%SRC%"
"%SRC%build\ModernEQUISnapshot_artefacts\Release\ModernEQUISnapshot.exe"
set RES=%errorlevel%
popd
exit /b %RES%

:tests
cmake -S "%SRC%." -B "%SRC%build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMEQ_BUILD_PLUGIN=OFF || exit /b 1
cmake --build "%SRC%build-tests" --target ModernEQTests || exit /b 1
"%SRC%build-tests\ModernEQTests_artefacts\Release\ModernEQTests.exe"
exit /b %errorlevel%

rem ---------------------------------------------------------------------------
:build_plugin
call :unlock
cmake -S "%SRC%." -B "%SRC%build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%SRC%build" --target ModernEQ_VST3 ModernEQ_Standalone || exit /b 1
exit /b 0

rem If a host (a DAW) has the built plug-in or the standalone app
rem loaded, Windows refuses to overwrite it. Such a file can still be renamed:
rem move it aside so the linker can write the new version.
:unlock
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "foreach ($f in @('%OUT%\VST3\GKEQ.vst3\Contents\x86_64-win\GKEQ.vst3', '%OUT%\Standalone\GKEQ.exe')) {" ^
  "  if (Test-Path -LiteralPath $f) { try { [IO.File]::Open($f, 'Open', 'ReadWrite', 'None').Close() } catch {" ^
  "    $n = (Split-Path $f -Leaf) + '.old-' + (Get-Date -Format yyyyMMddHHmmss); Rename-Item -LiteralPath $f -NewName $n;" ^
  "    Write-Host ('In use (e.g. by a DAW), moved aside: ' + $n + ' - restart the host to load the new build') } } }"
exit /b 0
