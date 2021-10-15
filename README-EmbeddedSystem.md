# BtcAgent for Embedded System

## Cross Build

* OS: `Ubuntu 20.04 LTS, 64 Bits`

```
apt-get update
apt-get install -y build-essential cmake git wget libssl-dev

#
# prepare toolchain
#
wget http://downloads.openwrt.org.cn/PandoraBox/PandoraBox-Toolchain-ralink-for-mipsel_24kec+dsp-gcc-4.8-linaro_uClibc-0.9.33.2.tar.bz2
tar zxvf PandoraBox-Toolchain-ralink-for-mipsel_24kec+dsp-gcc-4.8-linaro_uClibc-0.9.33.2.tar.bz2

#
# cross build openssl
#
TODO: finish this.
Please complete this step yourself.

#
# cross build libevent
#
wget https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz
tar zxvf libevent-2.1.12-stable.tar.gz
cd libevent-2.1.12-stable
LIBS=-ldl ./configure --host=mipsel-openwrt-linux
make -j$(nproc)
copy include and libraries to toolchain

#
# cross build agent
#
git clone https://github.com/btccom/btcagent.git
cd btcagent
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DPOOLAGENT__USE_GLOG=OFF -DTOOLCHAIN=/path/to/toolchain ..
make -j$(nproc)
```
