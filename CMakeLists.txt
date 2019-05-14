cmake_minimum_required (VERSION 3.1)

project (PoolAgent)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_C_STANDARD 99)


#
# Cross compilation toolchain
# cmake -DTOOLCHAIN=/path/to/toolchain ..
#
IF(TOOLCHAIN)
  set(CROSS_PREFIX ${TOOLCHAIN}/bin/mipsel-openwrt-linux-)
  set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)
  set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
  message("-- TOOLCHAIN: ${CMAKE_CXX_COMPILER} ${CMAKE_C_COMPILER}")
ENDIF()


#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Release ..
#
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message("-- Build Type: ${CMAKE_BUILD_TYPE} (-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")


IF (CMAKE_SYSTEM_NAME MATCHES "Windows")
  ###
  # Use IOCP (I/O Completion Port) replace select() for libevent
  ###
  option(POOLAGENT__USE_IOCP "Use IOCP (I/O Completion Port) replace select() for libevent" OFF)

  if(POOLAGENT__USE_IOCP)
    message("-- Use IOCP (I/O Completion Port) for libevent (-DPOOLAGENT__USE_IOCP=ON)")
    set(CompilerFlags CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG
                      CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE)
    foreach(CompilerFlag ${CompilerFlags})
      set(${CompilerFlag} "${${CompilerFlag}} -DUSE_IOCP")
      message("${CompilerFlag}=${${CompilerFlag}}")
    endforeach()
  else()
    message("-- Use select() for libevent (-DPOOLAGENT__USE_IOCP=OFF)")
  endif()


  ###
  # static linking VC++ runtime library
  ###
  option(POOLAGENT__STATIC_LINKING_VC_LIB "Static linking VC++ runtime library (/MT)" OFF)

  macro(set_linking_vclib CompilerFlag LinkFlag)
    string(REPLACE "/MDd" "" ${CompilerFlag} "${${CompilerFlag}}")
    string(REPLACE "/MD" "" ${CompilerFlag} "${${CompilerFlag}}")
    string(REPLACE "/MTd" "" ${CompilerFlag} "${${CompilerFlag}}")
    string(REPLACE "/MT" "" ${CompilerFlag} "${${CompilerFlag}}")
    set(${CompilerFlag} "${${CompilerFlag}} ${LinkFlag}")
    message("${CompilerFlag}=${${CompilerFlag}}")
  endmacro()

  if(POOLAGENT__STATIC_LINKING_VC_LIB)
    message("-- Static linking VC++ runtime library (/MT): -DPOOLAGENT__STATIC_LINKING_VC_LIB=ON")

    set_linking_vclib(CMAKE_CXX_FLAGS_DEBUG          "/MTd")
    set_linking_vclib(CMAKE_C_FLAGS_DEBUG            "/MTd")
    set_linking_vclib(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MTd")
    set_linking_vclib(CMAKE_C_FLAGS_RELWITHDEBINFO   "/MTd")
    set_linking_vclib(CMAKE_CXX_FLAGS_RELEASE        "/MT")
    set_linking_vclib(CMAKE_C_FLAGS_RELEASE          "/MT")
    set_linking_vclib(CMAKE_CXX_FLAGS_MINSIZEREL     "/MT")
    set_linking_vclib(CMAKE_C_FLAGS_MINSIZEREL       "/MT")

  else()
    message("-- Dynamic linking VC++ runtime library (/MD): -DPOOLAGENT__STATIC_LINKING_VC_LIB=OFF")

    set_linking_vclib(CMAKE_CXX_FLAGS_DEBUG          "/MDd")
    set_linking_vclib(CMAKE_C_FLAGS_DEBUG            "/MDd")
    set_linking_vclib(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MDd")
    set_linking_vclib(CMAKE_C_FLAGS_RELWITHDEBINFO   "/MDd")
    set_linking_vclib(CMAKE_CXX_FLAGS_RELEASE        "/MD")
    set_linking_vclib(CMAKE_C_FLAGS_RELEASE          "/MD")
    set_linking_vclib(CMAKE_CXX_FLAGS_MINSIZEREL     "/MD")
    set_linking_vclib(CMAKE_C_FLAGS_MINSIZEREL       "/MD")
  endif()

  set(POOLAGENT__GENERATE_DEB_PACKAGE OFF)

ELSE()
  IF(CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
  ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_CXX_FLAGS "-O2 -Wall")
  ENDIF()

  # Debian/Ubuntu software package
  option(POOLAGENT__GENERATE_DEB_PACKAGE "Generate Debian/Ubuntu software package" ON)

  if(POOLAGENT__GENERATE_DEB_PACKAGE)
    message("-- Generate Debian/Ubuntu software package: Enabled (-DPOOLAGENT__GENERATE_DEB_PACKAGE=ON)")
  else()
    message("-- Generate Debian/Ubuntu software package: Disabled (-DPOOLAGENT__GENERATE_DEB_PACKAGE=OFF)")
  endif()

  set(POOLAGENT__USE_IOCP OFF)
  set(POOLAGENT__STATIC_LINKING_VC_LIB OFF)
ENDIF()


###
# Use GLog for logging replace stdout
###
option(POOLAGENT__USE_GLOG "Use GLog for logging replace stdout" ON)

if(POOLAGENT__USE_GLOG)
  message("-- Use GLog for logging (-DPOOLAGENT__USE_GLOG=ON)")
  set(CompilerFlags CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG
                    CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE)
  foreach(CompilerFlag ${CompilerFlags})
    set(${CompilerFlag} "${${CompilerFlag}} -DGOOGLE_GLOG_DLL_DECL= -DSUPPORT_GLOG")
    message("${CompilerFlag}=${${CompilerFlag}}")
  endforeach()
else()
  message("-- Use stdout for logging (-DPOOLAGENT__USE_GLOG=OFF)")
endif()

###
# Print logs to stdout with files
###
option(POOLAGENT__GLOG_TO_STDOUT "Print logs to stdout with files" OFF)

if(POOLAGENT__GLOG_TO_STDOUT)
  message("-- Print logs to stdout with files (-DPOOLAGENT__GLOG_TO_STDOUT=ON)")
  set(CompilerFlags CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG
                    CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE)
  foreach(CompilerFlag ${CompilerFlags})
    set(${CompilerFlag} "${${CompilerFlag}} -DGLOG_TO_STDOUT")
    message("${CompilerFlag}=${${CompilerFlag}}")
  endforeach()
else()
  message("-- Only logs to file (-DPOOLAGENT__GLOG_TO_STDOUT=OFF)")
endif()


###################################### Libs ######################################

if(POOLAGENT__USE_GLOG)
  find_package(Glog)
  if(NOT GLOG_FOUND)
    message(FATAL_ERROR "Glog not found!")
  endif(NOT GLOG_FOUND)
endif()

find_package(LibEvent)
if(NOT LibEvent_FOUND)
  message(FATAL_ERROR "libevent2 not found!")
endif(NOT LibEvent_FOUND)

include_directories(src test ${GLOG_INCLUDE_DIRS} ${LIBEVENT_INCLUDE_DIR})
set(THRID_LIBRARIES ${GLOG_LIBRARIES} ${LIBEVENT_LIB})
IF (CMAKE_SYSTEM_NAME MATCHES "Windows")
  set(THRID_LIBRARIES ${THRID_LIBRARIES} ws2_32)
ELSE()
  set(THRID_LIBRARIES ${THRID_LIBRARIES} pthread)
ENDIF()

file(GLOB LIB_SOURCES src/*.*)
file(GLOB LIB_BITCOIN_SOURCES src/bitcoin/*.*)
file(GLOB LIB_ETH_SOURCES src/eth/*.*)
add_library(btccomagent STATIC ${LIB_SOURCES} ${LIB_BITCOIN_SOURCES} ${LIB_ETH_SOURCES})

file(GLOB_RECURSE TEST_SOURCES test/*.*)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest btccomagent ${THRID_LIBRARIES})

file(GLOB_RECURSE AGENT_SOURCES src/agent/*.*)
IF (CMAKE_SYSTEM_NAME MATCHES "Windows")
  file(GLOB_RECURSE AGENT_SOURCES ${AGENT_SOURCES} src/win32/getopt/*.*)
ENDIF()
add_executable(btcagent ${AGENT_SOURCES})
target_link_libraries(btcagent btccomagent ${THRID_LIBRARIES})


# make instal support
IF(POOLAGENT__GENERATE_DEB_PACKAGE)
  install(TARGETS btcagent
          DESTINATION /usr/bin)
  install(FILES src/agent/agent_conf.json README.md README-zh_CN.md
          DESTINATION /etc/btcagent)
  install(FILES install/btcagent-single_user.conf
          DESTINATION /etc/supervisor/conf.d)

  # generate deb package by CPack
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "btcagent-single_user")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "YihaoPeng yihao.peng@bitmain.com")

  SET(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/install/debian-control/postinst")

  # version
  SET(CPACK_PACKAGE_VERSION_MAJOR "1")
  SET(CPACK_PACKAGE_VERSION_MINOR "0")
  SET(CPACK_PACKAGE_VERSION_PATCH "0")

  # dependencies
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "supervisor")

  include(CPack)
ENDIF()
