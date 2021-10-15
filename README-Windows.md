# BtcAgent for Windows


## Build with Cmake & Visual Studio

Please run all commands below in `x64 Native Tools Command Prompt for VS 20xx` (for 64bit building) or `x86 Native Tools Command Prompt for VS 20xx` (for 32bit building).

Use `-A win32` instead of `-A x64` if you want a 32bit building.

### Cmake

Download binary distributions from https://cmake.org/download/ and install.

Add ```CmakeInstallDirectory\bin``` to ```PATH``` environment variable.


### vcpkg

See https://github.com/microsoft/vcpkg for more details.

```
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat

.\vcpkg\vcpkg.exe install openssl:x86-windows-static
.\vcpkg\vcpkg.exe install glog:x86-windows-static
.\vcpkg\vcpkg.exe install libevent[core,openssl,thread]:x86-windows-static

.\vcpkg\vcpkg.exe install openssl:x64-windows-static
.\vcpkg\vcpkg.exe install glog:x64-windows-static
.\vcpkg\vcpkg.exe install libevent[core,openssl,thread]:x64-windows-static
```

### btcagent

You can build it with cmake and Visual Studio:

```cmd
git clone https://github.com/btccom/btcagent.git
cd btcagent

# change to your vcpkg dir
set VCPKG_ROOT_DIR=C:/Users/user/source/repos/vcpkg

.\make.bat win32

.\make.bat win64
```

Then build ```ALL_BUILD``` project in Visual Studio.

#### btcagent cmake options

There are ```btcagent``` specific Cmake variables (the values being the default):

```
# Static linking VC++ runtime library (/MT)
POOLAGENT__STATIC_LINKING_VC_LIB:BOOL=OFF

# Use GLog for logging replace stdout
POOLAGENT__USE_GLOG:BOOL=OFF

# Print logs to stdout with files
POOLAGENT__GLOG_TO_STDOUT:BOOL=OFF
```

See [make.bat](make.bat) for usage.

## Configure & Run

config json file example:
```json
{
    "agent_listen_ip": "0.0.0.0",
    "agent_listen_port": 3333,
    "pool_use_tls": false,
    "pools": [
        ["us.ss.btc.com", 1800, "YourSubAccountName"],
        ["us.ss.btc.com", 443, "YourSubAccountName"],
        ["us.ss.btc.com", 3333, "YourSubAccountName"]
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
