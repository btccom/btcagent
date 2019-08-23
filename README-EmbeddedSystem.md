Get node.js http://githup.com/ekkarat.w@gmail.com
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
wget https://github.com/libevent/libevent/releases/download/release-2.1.9-beta/libevent-2.1.9-beta.tar.gz
tar zxvf libevent-2.1.9-beta.tar.gz
cd libevent-2.1.9-beta
LIBS=-ldl ./configure --host=mipsel-openwrt-linux
make
copy include and libraries to toolchain

#
# cross build agent
#
git clone https://github.com/btccom/btcagent.git
cd btcagent
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DPOOLAGENT__USE_GLOG=OFF -DTOOLCHAIN=/path/to/toolchain ..
make
```
