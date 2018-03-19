@echo off
cd %~dp0

:select_action
if  "%1" == "" goto usage
if /i %1 == win64      goto make_win64
if /i %1 == xp         goto make_xp
if /i %1 == test       goto make_test
if /i %1 == clean      goto make_clean
goto usage

:usage
echo Usage:
echo     make win64
echo     make xp
echo     make test
echo     make clean
goto :eof

:make_win64
md build.win64
cd build.win64
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64
cmake -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON -DPOOLAGENT__USE_GLOG=ON -DPOOLAGENT__GLOG_TO_STDOUT=ON -G "Visual Studio 14 2015 Win64" ..
start PoolAgent.sln
cd ..
goto :eof

:make_xp
md build.xp
cd build.xp
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x86
cmake -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON -DPOOLAGENT__USE_GLOG=ON -DPOOLAGENT__GLOG_TO_STDOUT=ON -G "Visual Studio 14 2015" -T v140_xp ..
start PoolAgent.sln
cd ..
goto :eof

:make_test
echo ------ test.win64
build.win64\Release\unittest
echo ------ test.xp
build.xp\Release\unittest
goto :eof

:make_clean
echo ------ remove build.win64
rd /s /q build.win64
echo ------ remove build.xp
rd /s /q build.xp
goto :eof
