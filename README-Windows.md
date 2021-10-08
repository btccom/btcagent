# BtcAgent for Windows


## Build with Cmake & Visual Studio

Please run all commands below in `x64 Native Tools Command Prompt for VS 20xx` (for 64bit building) or `x86 Native Tools Command Prompt for VS 20xx` (for 32bit building).

Use `-A win32` instead of `-A x64` if you want a 32bit building.

### Cmake

Download binary distributions from https://cmake.org/download/ and install.

Add ```CmakeInstallDirectory\bin``` to ```PATH``` environment variable.


### libevent

Libevent [release-2.1.12-stable](https://github.com/libevent/libevent/releases/tag/release-2.1.12-stable) is recommended because the earlier versions have a deadlock issue: [btcpool#75](https://github.com/btccom/btcpool/issues/75).

```cmd
wget https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz
tar xf libevent-2.1.12-stable.tar.gz
cd libevent-2.1.12-stable

# fix missing files
cd WIN32-Code
wget https://raw.githubusercontent.com/libevent/libevent/master/WIN32-Code/getopt_long.c https://raw.githubusercontent.com/libevent/libevent/master/WIN32-Code/getopt.h https://raw.githubusercontent.com/libevent/libevent/master/WIN32-Code/getopt.c

cd ..
md build && cd build
cmake -DCMAKE_INSTALL_PREFIX="%appdata%\lib\libevent" -DEVENT__LIBRARY_TYPE=STATIC -DEVENT__DISABLE_OPENSSL=ON -A x64 ..
start libevent.sln
```

Then build the ```INSTALL``` project in Visual Studio, it will be installed to ```%appdata%\lib\glog```.

Use ```-DEVENT__DISABLE_OPENSSL=ON``` to avoid finding ```openssl```.


### Glog

Glog [0.3.5](https://github.com/google/glog/releases/tag/v0.3.5) is recommended.

Glog ```0.3.4``` has an issue for VS2015 or later that duplicate definition ```snprintf``` at src/windows/port.cc, comment needed. Even that, the test case crashed with an exception.

```cmd
wget https://github.com/google/glog/archive/v0.3.5.tar.gz
tar xf v0.3.5.tar.gz
cd glog-0.3.5
md build && cd build
cmake -DCMAKE_INSTALL_PREFIX="%appdata%\lib\glog" -A x64 ..
start google-glog.sln
```

Then build the ```INSTALL``` project in Visual Studio, it will be installed to ```%appdata%\lib\glog```.


### btcagent

You can build it with cmake and Visual Studio:

```cmd
git clone https://github.com/btccom/btcagent.git
cd btcagent
md build && cd build
cmake -DLIBEVENT_ROOT_DIR="%appdata%\lib\libevent" -DGLOG_ROOT_DIR="%appdata%\lib\glog" -A x64 ..
start PoolAgent.sln
```

Then build ```ALL_BUILD``` project in Visual Studio. ```build\Debug\btcagent.exe``` is the final product, it static linked with libevent. But by default, it dynamic linked with VC++ runtime library. You must install ```Visual C++ Redistributable for Visual Studio 20xx``` at another computers.

There are ```btcagent``` specific Cmake variables (the values being the default):

```
# Static linking VC++ runtime library (/MT)
POOLAGENT__STATIC_LINKING_VC_LIB:BOOL=OFF

# Use IOCP (I/O Completion Port) replace select() for libevent
POOLAGENT__USE_IOCP:BOOL=OFF

# Use GLog for logging replace stdout
POOLAGENT__USE_GLOG:BOOL=OFF

# Print logs to stdout with files
POOLAGENT__GLOG_TO_STDOUT:BOOL=OFF
```

## Static linking with VC++ runtime library

For static linking with VC++ runtime library, we use ```/MT``` in the project's ```Property Pages``` > ```C/C++``` > ```Code Generation``` > ```Runtime Library``` property instead of ```/MD``` by default. Using ```/MTd``` instead of ```/MDd``` for debug build.

All librarys the project reliant must linked with ```/MT``` or ```/MTd```, else some symbols will lost at the final linking.


### libevent & GLog

You can add there codes to the end of ```CMakeLists.txt``` that modify the default ```/MD``` & ```/MDd``` property to ```/MT``` & ```/MTd```:

```cmake
###
# static linking VC++ runtime library
###
macro(set_linking_vclib CompilerFlag LinkFlag)
  string(REPLACE "/MDd" "" ${CompilerFlag} "${${CompilerFlag}}")
  string(REPLACE "/MD" "" ${CompilerFlag} "${${CompilerFlag}}")
  string(REPLACE "/MTd" "" ${CompilerFlag} "${${CompilerFlag}}")
  string(REPLACE "/MT" "" ${CompilerFlag} "${${CompilerFlag}}")
  set(${CompilerFlag} "${${CompilerFlag}} ${LinkFlag}")
  message("${CompilerFlag}=${${CompilerFlag}}")
endmacro()

message("-- Static linking VC++ runtime library (/MT)")

set_linking_vclib(CMAKE_CXX_FLAGS_DEBUG          "/MTd")
set_linking_vclib(CMAKE_C_FLAGS_DEBUG            "/MTd")
set_linking_vclib(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MTd")
set_linking_vclib(CMAKE_C_FLAGS_RELWITHDEBINFO   "/MTd")
set_linking_vclib(CMAKE_CXX_FLAGS_RELEASE        "/MT")
set_linking_vclib(CMAKE_C_FLAGS_RELEASE          "/MT")
set_linking_vclib(CMAKE_CXX_FLAGS_MINSIZEREL     "/MT")
set_linking_vclib(CMAKE_C_FLAGS_MINSIZEREL       "/MT")
```

Then build as normal.


### btcagent

Use ```-DPOOLAGENT__STATIC_LINKING_VC_LIB=ON``` with cmake command:

```cmd
md build && cd build
cmake -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON -DLIBEVENT_ROOT_DIR="%appdata%\lib\libevent" -DGLOG_ROOT_DIR="%appdata%\lib\glog" -A x64 ..
start PoolAgent.sln
```

## Support Windows XP

Simply add an arg ```-T v140_xp``` to Cmake if build with Visual Studio.

```cmd
cmake -A win32 -T v141_xp ..
```

Libevent and GLog need the arg too.

### libevent

XP has not ```inet_ntop()``` and ```inet_pton()``` so must disable them or a "endpoint not found" will trigger when running.

Edit ```CMakeLists.txt``` and comment the two lines:

```cmake
#CHECK_FUNCTION_EXISTS_EX(inet_ntop EVENT__HAVE_INET_NTOP)
#CHECK_FUNCTION_EXISTS_EX(inet_pton EVENT__HAVE_INET_PTON)
```

And rebuild with clear build dir.

## 32bit or 64bit

```cmd
# 32bit
cmake -A win32 ..

# 64bit
cmake -A x64 ..
```

## Configure & Run

config json file example:
```json
{
    "agent_listen_ip": "0.0.0.0",
    "agent_listen_port": 3333,
    "pools": [
        ["us.ss.btc.com", 1800, "kevin"],
        ["us.ss.btc.com", 1800, "kevin"]
    ]
}
```

run:
```cmd
btcagent.exe -c agent_conf.json
```

run without stdout:
```cmd
btcagent.exe -c agent_conf.json > nul
```

run with GLog enabled:
```cmd
btcagent.exe -c agent_conf.json -l log
```
