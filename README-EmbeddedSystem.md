# BtcAgent for Embedded System

## Cross Build

* OS: `Ubuntu 14.04 LTS, 64 Bits`

```
apt-get update
apt-get install -y build-essential cmake git

#
# prepare toolchain
#
wget http://downloads.openwrt.org.cn/PandoraBox/PandoraBox-Toolchain-ralink-for-mipsel_24kec+dsp-gcc-4.8-linaro_uClibc-0.9.33.2.tar.bz2
tar zxvf PandoraBox-Toolchain-ralink-for-mipsel_24kec+dsp-gcc-4.8-linaro_uClibc-0.9.33.2.tar.bz2

#
# cross build libevent
#
wget https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz
tar zxvf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
LIBS=-ldl ./configure --host=mipsel-openwrt-linux
make
copy include and libraries to toolchain

#
# cross build agent
#
git clone https://github.com/btccom/btcagent.git -b develop
cd btcagent
cp CMakeLists4EmbeddedSystem.txt CMakeLists.txt
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DTOOLCHAIN=/path/to/toolchain ..
make
