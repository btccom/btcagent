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


#ifndef _WIN32
  #include <unistd.h>
#endif

#include <errno.h>

#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include <memory>

#include "jsmn.h"

#if defined(SUPPORT_GLOG)
  #include <glog/logging.h>
#else
  #define LOG(x) std::cout

  #ifdef NDEBUG
    // Disable debug output with Release build.
    // It's safe because compiler will ignore whole the
    // output streaming expression no matter a line break.
    #define DLOG(x) if(0)std::cout
  #else
    #define DLOG(x) std::cout
  #endif
#endif

using  std::string;
using  std::vector;

#if __cplusplus == 201103L

namespace std {

// note: this implementation does not disable this overload for array types
template<typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args)
{
    return unique_ptr<T>(new T(forward<Args>(args)...));
}

}

#endif // #if __cplusplus == 201103L

//
// WARNING: DO NOT CHANGE THE NAME.
// the version could be changed like: "btccom-agent/xx.xx.xx-edition"
//
#define BTCCOM_MINER_AGENT   "btccom-agent/1.0.0-su"

#if defined(SUPPORT_GLOG) && defined(GLOG_TO_STDOUT)
// Print logs to stdout with glog
class GLogToStdout : public google::LogSink {
public:
    virtual void send(google::LogSeverity severity, const char* full_filename,
        const char* base_filename, int line,
        const struct ::tm* tm_time,
        const char* message, size_t message_len);

    virtual std::string ToString(google::LogSeverity severity, const char* file, int line,
        const struct ::tm* tm_time,
        const char* message, size_t message_len);
};
#endif

class Strings {
public:
  static string Format(const char * fmt, ...);
  static void Append(string & dest, const char * fmt, ...);
  static string ReplaceAll(std::string str, const std::string& from, const std::string& to);
  static string FormatIP(uint32_t ipv4Int, string format);
};

struct PoolConf {
  string host_;
  uint16_t port_ = 0;
  string upPoolUserName_;
};

struct AgentConf {
  string agentType_ = "btc";
	string listenIP_ = "0.0.0.0";
  uint16_t listenPort_ = 3333;
  std::vector<PoolConf> pools_;
  bool alwaysKeepDownconn_ = false;
	bool disconnectWhenLostAsicBoost_ = true;
  bool submitResponseFromServer_ = false;
  bool useIpAsWorkerName_ = false;
  bool poolUseTls_ = false;
  string ipWorkerNameFormat_ = "{1}x{2}x{3}x{4}";
  string fixedWorkerName_;
};

string getJsonStr(const char *c,const jsmntok_t *t);
bool parseConfJson(const string &jsonStr, AgentConf &conf);

// slite stratum 'mining.notify'
// 14: the end of coinbase1
const char *splitNotify(const string &line, int number = 14);

string str2lower(const string &str);
bool strEmpty(const string &str);

#endif
