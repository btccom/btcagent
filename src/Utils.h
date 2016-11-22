/*
 Mining Pool Agent

 Copyright (C) 2016  BTC.COM

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef UTILS_H_
#define UTILS_H_

#define __STDC_FORMAT_MACROS
#define __STDC_LIMIT_MACROS

#include <assert.h>
#include <math.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "jsmn.h"

using  std::string;
using  std::vector;

//
// WARNING: DO NOT CHANGE THE NAME.
// the version could be changed like: "btccom-agent/xx.xx"
//
#define BTCCOM_MINER_AGENT   "btccom-agent/0.1"

#define LOG(x) std::cout
#define DLOG(x) std::cout

class Strings {
public:
  static string Format(const char * fmt, ...);
  static void Append(string & dest, const char * fmt, ...);
};

class PoolConf {
public:
  string host_;
  uint16_t port_;
  string upPoolUserName_;

  PoolConf(): port_(0u) {}

  PoolConf(const PoolConf &r) {
    host_ = r.host_;
    port_ = r.port_;
    upPoolUserName_ = r.upPoolUserName_;
  }
};

string getJsonStr(const char *c,const jsmntok_t *t);
bool parseConfJson(const string &jsonStr,
                   string &listenIP, string &listenPort,
                   std::vector<PoolConf> &poolConfs);

// slite stratum 'mining.notify'
const char *splitNotify(const string &line);

string str2lower(const string &str);

#endif
