# BtcAgent for Windows


## Build with Cmake & Visual Studio


### Cmake

Download binary distributions from https://cmake.org/download/ and install.

Add ```CmakeInstallDirectory\bin``` to ```PATH``` environment variable.


### libevent

```libevent-2.0.x-stable``` is no cmake support. You have to build it by yourself if you want to stable version. It has a ```makefile.nmake``` but unfinished and not recommended by developers.

And ```libevent-2.1.x-rc``` has good support for cmake. You can open a ```cmd``` and ```cd``` to the source code directory, then run these command:

```cmd
md build && cd build
cmake -DEVENT__DISABLE_OPENSSL=ON -G "Visual Studio 14 2015" ..
start libevent.sln
```

Use ```-DEVENT__DISABLE_OPENSSL=ON``` avoiding build ```openssl``` at first. You can choose another generator listed in ```cmake --help``` instead of ```Visual Studio 14 2015```.

Once you opened ```libevent.sln``` with Visual Studio, you can build all projects (with ```ALL_BUILD``` project) and install it (with ```INSTALL``` project, administrator permission needed).

The ```INSTALL``` project will install libevent to ```C:\Program Files (x86)\libevent``` by default, copy its ```lib``` and ```include``` directory into ```X:\Program Files (x86)\Microsoft Visual Studio xx.0\VC``` directory and finished the install.


### Glog

Last release ```glog: 0.3.4``` has an issue for VS2015 that duplicate definition ```snprintf``` at src/windows/port.cc, comment needed if using VS2015. Even that, the test case crashed with an exception.

Recommended version is the master branch from github, it fix the issue. The build command with cmake is:

```cmd
git clone https://github.com/google/glog.git
mkdir glog/build
cd glog/build
cmake ..
start -G "Visual Studio 14 2015" google-glog.sln
```

Then build ```ALL_BUILD``` & ```INSTALL``` project with VS2015, then copy ```C:\Program Files (x86)\google-glog\include``` & ```C:\Program Files (x86)\google-glog\lib``` to ```VS_install_dir\VC\```.

If you build with ```google-glog.sln``` in the project root directory, you have to copy ```libglog_static.lib``` to ```VS_install_dir\VC\lib``` and rename it to ```glog.lib``` so cmake can find it when build ```btcagent```. (It isn't recommended because of ```snprintf``` issue.)


### btcagent

You can build it with cmake and Visual Studio:

```cmd
copy CMakeLists4Windows.txt CMakeLists.txt
md build && cd build
cmake -G "Visual Studio 14 2015" ..
start PoolAgent.sln
```

Then build ```ALL_BUILD``` project in Visual Studio. ```build\Debug\agent.exe``` is the final product, it static linked with libevent. But by default, it dynamic linked with VC++ runtime library. You must install ```Visual C++ Redistributable for Visual Studio 20xx``` at another computers.

There are ```btcagent``` specific Cmake variables (the values being the default):

```
# Static linking VC++ runtime library (/MT)
POOLAGENT__STATIC_LINKING_VC_LIB:BOOL=OFF

# Use IOCP (I/O Completion Port) replace select() for libevent
POOLAGENT__USE_IOCP:BOOL=OFF

# Use GLog for logging replace stdout
POOLAGENT__USE_GLOG:BOOL=OFF
```

## Static linking with VC++ runtime library

For static linking with VC++ runtime library, we use ```/MT``` in the project's ```Property Pages``` > ```C/C++``` > ```Code Generation``` > ```Runtime Library``` property instead of ```/MD``` by default. Using ```/MTd``` instead of ```/MDd``` for debug.

All librarys the project reliant must linked with ```/MT``` or ```/MTd```, else some symbols will lost at the final linking.


### libevent & GLog

You can add there codes to the end of ```CMakeLists.txt``` that modify the default ```/MD``` & ```/MDd``` property to ```/MT``` & ```/MTd```:

```cmake
###
# static linking VC++ runtime library
###
message("Static linking VC++ runtime library (/MT).")
# debug mode
set(CompilerFlags CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
foreach(CompilerFlag ${CompilerFlags})
  string(REPLACE "/MDd" "" ${CompilerFlag} "${${CompilerFlag}}")
  string(REPLACE "/MD" "" ${CompilerFlag} "${${CompilerFlag}}")
  set(${CompilerFlag} "${${CompilerFlag}} /MTd")
  message("${CompilerFlag}=${${CompilerFlag}}")
endforeach()
# release mode
set(CompilerFlags CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE)
foreach(CompilerFlag ${CompilerFlags})
  string(REPLACE "/MDd" "" ${CompilerFlag} "${${CompilerFlag}}")
  string(REPLACE "/MD" "" ${CompilerFlag} "${${CompilerFlag}}")
  set(${CompilerFlag} "${${CompilerFlag}} /MT")
  message("${CompilerFlag}=${${CompilerFlag}}")
endforeach()
```

Then build as normal.


### btcagent

Use ```-DPOOLAGENT__STATIC_LINKING_VC_LIB=ON``` with cmake command:

```cmd
copy CMakeLists4Windows.txt CMakeLists.txt
md build && cd build
cmake -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON -G "Visual Studio 14 2015" ..
start PoolAgent.sln
```

## Support Windows XP

Simply add an arg ```-T v140_xp``` to Cmake if build with VS2015.

```cmd
cmake -G "Visual Studio 14 2015" -T v140_xp ..
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

Default is 32bit:

```cmd
cmake -G "Visual Studio 14 2015" ..
```

Append ```Win64``` at generator for 64bit:

```cmd
cmake -G "Visual Studio 14 2015 Win64" ..
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
agent.exe -c agent_conf.json
```

run without stdout:
```cmd
agent.exe -c agent_conf.json > nul
```

run with GLog enabled:
```cmd
agent.exe -c agent_conf.json -l log
```
