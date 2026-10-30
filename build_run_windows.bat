@echo off

call "%~dp0setup_windows.bat"
if errorlevel 1 exit /b %ERRORLEVEL%

pushd "%~dp0"
if errorlevel 1 exit /b 1

set "BUILD_DIR=%CD%\.build"
set "APP_DIR=%BUILD_DIR%\Release"

if not exist "%BUILD_DIR%\CMakeCache.txt" goto :configuration_required
if not exist "%BUILD_DIR%\conanrun.bat" goto :configuration_required

echo Building Gittyup in Release...
cmake --build --preset conan-release --parallel
if errorlevel 1 (
    echo ERROR: Gittyup build failed.
    popd
    exit /b 1
)

if not exist "%APP_DIR%\gittyup.exe" (
    echo ERROR: Executable not found at "%APP_DIR%\gittyup.exe".
    popd
    exit /b 1
)

call "%BUILD_DIR%\conanrun.bat"
if errorlevel 1 (
    echo ERROR: Could not activate the Conan runtime environment.
    popd
    exit /b 1
)

echo Starting Gittyup...
pushd "%APP_DIR%"
gittyup.exe
set "EXIT_CODE=%ERRORLEVEL%"
popd

if exist "%BUILD_DIR%\deactivate_conanrun.bat" call "%BUILD_DIR%\deactivate_conanrun.bat"
popd
exit /b %EXIT_CODE%

:configuration_required
echo ERROR: Conan dependencies and CMake must be configured first.
echo Run conan_cmake_windows.bat before this build/run phase.
popd
exit /b 1
