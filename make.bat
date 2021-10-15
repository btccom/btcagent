@echo off
cd %~dp0

if "%VCPKG_ROOT_DIR%"=="" set "VCPKG_ROOT_DIR=../../vcpkg"

:select_action
if  "%1" == ""         goto usage
if /i %1 == win64      goto make_win64
if /i %1 == win32         goto make_win32
if /i %1 == test       goto make_test
if /i %1 == clean      goto make_clean
goto usage

:usage
echo Usage:
echo     set VCPKG_ROOT_DIR="C:/Users/user/source/repos/vcpkg"
echo     make win64
echo     make win32
echo     make test
echo     make clean
goto :eof

:make_win64
md build.win64
cd build.win64
cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT_DIR%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON -DPOOLAGENT__GLOG_TO_STDOUT=ON ..
start PoolAgent.sln
cd ..
goto :eof

:make_win32
md build.win32
cd build.win32
cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT_DIR%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x86-windows-static -A Win32 -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON -DPOOLAGENT__GLOG_TO_STDOUT=ON ..
start PoolAgent.sln
cd ..
goto :eof

:make_test
echo ------ test.win64
build.win64\Release\unittest
echo ------ test.win32
build.win32\Release\unittest
goto :eof

:make_clean
echo ------ remove build.win64
rd /s /q build.win64
echo ------ remove build.win32
rd /s /q build.win32
goto :eof
