# BtcAgent for Windows


## Build with Cmake & Visual Studio


### Cmake

Download binary distributions from https://cmake.org/download/ and install.

Add ```CmakeInstallDirectory\bin``` to ```PATH``` environment variable.


### libevent

There is no cmake support for ```libevent-2.0.x-stable```. You have to build it by yourself if you want to stable version. It has a ```makefile.nmake``` but unfinished and not recommended by developers.

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

Glog is disabled in Windows version. May it be available in future.


### btcagent

You can build it with cmake and Visual Studio:

```cmd
copy CMakeLists4Windows.txt CMakeLists.txt
md build && cd build
cmake -G "Visual Studio 14 2015" ..
start PoolAgent.sln
```

Then build ```ALL_BUILD``` project in Visual Studio. ```build\Debug\agent.exe``` is the final product, it static linked with libevent. But by default, it dynamic linked with VC++ runtime library. You must install ```Visual C++ Redistributable for Visual Studio 20xx``` at another computers.


## Static linking with VC++ runtime library

For static linking with VC++ runtime library, we use ```/MT``` in the project's ```Property Pages``` > ```C/C++``` > ```Code Generation``` > ```Runtime Library``` property instead of ```/MD``` by default. Using ```/MTd``` instead of ```/MDd``` for debug.

All librarys the project reliant must linked with ```/MT``` or ```/MTd```, else some symbols will lost at the final linking.


### libevent

You can add there codes to ```CMakeLists.txt``` of ```libevent``` that modify the default ```/MD``` & ```/MDd``` property to ```/MT``` & ```/MTd```:

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


### btcagent

Use ```-DPOOLAGENT__STATIC_LINKING_VC_LIB=ON``` with cmake command:

```cmd
copy CMakeLists4Windows.txt CMakeLists.txt
md build && cd build
cmake -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON -G "Visual Studio 14 2015" ..
start PoolAgent.sln
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
