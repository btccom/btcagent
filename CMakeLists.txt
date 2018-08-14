cmake_minimum_required (VERSION 2.6)

project (PoolAgent)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Release ..
#
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug build.")
  set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release build.")
  set(CMAKE_CXX_FLAGS "-O2 -Wall")
ELSE()
  message("Some other build type.")
ENDIF()


SET(CMAKE_CXX_COMPILER "g++")
SET(CMAKE_C_COMPILER   "gcc")

# use C++11
SET(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x -DSUPPORT_GLOG")
SET(CMAKE_C_COMPILER_ARG1 "-std=c99 -DSUPPORT_GLOG")
set (CMAKE_CXX_STANDARD 11)

find_package(Glog)
if(NOT GLOG_FOUND)
  message(FATAL_ERROR "Glog not found!")
endif(NOT GLOG_FOUND)

find_package(LibEvent)
if(NOT LibEvent_FOUND)
  message(FATAL_ERROR "libevent2 not found!")
endif(NOT LibEvent_FOUND)

include_directories(src test ${GLOG_INCLUDE_DIRS} ${LIBEVENT_INCLUDE_DIR})
set(THRID_LIBRARIES -lpthread ${GLOG_LIBRARIES} ${LIBEVENT_LIB})

file(GLOB LIB_SOURCES src/*.cc src/*.c)
add_library(btccomagent STATIC ${LIB_SOURCES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest btccomagent ${THRID_LIBRARIES})

file(GLOB_RECURSE AGENT_SOURCES src/agent/*.cc)
add_executable(btcagent-mu ${AGENT_SOURCES})
target_link_libraries(btcagent-mu btccomagent ${THRID_LIBRARIES})


# make instal support
install(TARGETS btcagent-mu
        DESTINATION /usr/bin)
install(FILES src/agent/agent_conf.json README.md README-zh_CN.md
        DESTINATION /etc/btcagent-mu)
install(FILES install/btcagent-multi_user.conf
        DESTINATION /etc/supervisor/conf.d)

# generate deb package by CPack
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "btcagent-multi_user")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "YihaoPeng yihao.peng@bitmain.com")

SET(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/install/debian-control/postinst")

# version
SET(CPACK_PACKAGE_VERSION_MAJOR "0")
SET(CPACK_PACKAGE_VERSION_MINOR "9")
SET(CPACK_PACKAGE_VERSION_PATCH "1-beta")

# dependencies
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "supervisor")

include(CPack)
