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
#include "Utils.h"

#include <stdarg.h>

#include <algorithm>
#include <iostream>
#include <iomanip>

#include <event2/util.h>

#if defined(SUPPORT_GLOG) && defined(GLOG_TO_STDOUT)
void GLogToStdout::send(google::LogSeverity severity, const char* full_filename,
    const char* base_filename, int line, const struct ::tm* tm_time,
    const char* message, size_t message_len) {

    std::cout << ToString(severity, base_filename, line, tm_time, message, message_len) << std::endl;
}

std::string GLogToStdout::ToString(google::LogSeverity severity, const char* file, int line,
    const struct ::tm* tm_time, const char* message, size_t message_len) {

    std::ostringstream stream;

    stream << '[' << 1900 + tm_time->tm_year << '-'
        << std::setfill('0')
        << std::setw(2) << 1 + tm_time->tm_mon << '-'
        << std::setw(2) << tm_time->tm_mday << ' '
        << std::setw(2) << tm_time->tm_hour << ':'
        << std::setw(2) << tm_time->tm_min << ':'
        << std::setw(2) << tm_time->tm_sec << "] ";

    stream << string(message, message_len);

    return stream.str();
}
#endif

string Strings::Format(const char * fmt, ...) {
  char tmp[512];
  string dest;
  va_list al;
  va_start(al, fmt);
  int len = vsnprintf(tmp, 512, fmt, al);
  va_end(al);
  if (len>511) {
    char * destbuff = new char[len+1];
    va_start(al, fmt);
    len = vsnprintf(destbuff, len+1, fmt, al);
    va_end(al);
    dest.append(destbuff, len);
    delete[] destbuff;
  } else {
    dest.append(tmp, len);
  }
  return dest;
}

void Strings::Append(string & dest, const char * fmt, ...) {
  char tmp[512];
  va_list al;
  va_start(al, fmt);
  int len = vsnprintf(tmp, 512, fmt, al);
  va_end(al);
  if (len>511) {
    char * destbuff = new char[len+1];
    va_start(al, fmt);
    len = vsnprintf(destbuff, len+1, fmt, al);
    va_end(al);
    dest.append(destbuff, len);
    delete[] destbuff;
  } else {
    dest.append(tmp, len);
  }
}

string Strings::ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

string Strings::FormatIP(uint32_t ipv4Int, string format) {
  union IPv4Arr {
    uint32_t ipv4Int;
    uint8_t parts[4];
  } ipv4Arr;

  ipv4Arr.ipv4Int = std::move(ipv4Int);

  format = Strings::ReplaceAll(format, string("{1}"), std::to_string(ipv4Arr.parts[0]));
  format = Strings::ReplaceAll(format, string("{2}"), std::to_string(ipv4Arr.parts[1]));
  format = Strings::ReplaceAll(format, string("{3}"), std::to_string(ipv4Arr.parts[2]));
  format = Strings::ReplaceAll(format, string("{4}"), std::to_string(ipv4Arr.parts[3]));

  return format;
}

string getJsonStr(const char *c,const jsmntok_t *t) {
  if (t == NULL || t->end <= t->start)
    return "";

  return string(c + t->start, t->end - t->start);
}

static
int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
  if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

bool parseConfJson(const string &jsonStr, AgentConf &conf) {
  jsmn_parser p;
  jsmn_init(&p);
  jsmntok_t t[64]; // we expect no more than 64 tokens
  int r;
  const char *c = jsonStr.c_str();

  // pase json string
  r = jsmn_parse(&p, jsonStr.c_str(), jsonStr.length(), t, sizeof(t)/sizeof(t[0]));

  // assume the top-level element is an object
  if (r < 1 || t[0].type != JSMN_OBJECT) {
    LOG(ERROR) << "json decode failure" << std::endl;
    return false;
  }

  for (int i = 1; i < r; i++) {
    if (jsoneq(c, &t[i], "agent_type") == 0) {
      conf.agentType_ = getJsonStr(c, &t[i+1]);
      i++;
    }
    if (jsoneq(c, &t[i], "agent_listen_ip") == 0) {
      conf.listenIP_ = getJsonStr(c, &t[i+1]);
      i++;
    }
    else if (jsoneq(c, &t[i], "agent_listen_port") == 0) {
      conf.listenPort_ = (uint16_t)strtoul(getJsonStr(c, &t[i+1]).c_str(), NULL, 10);
      i++;
    }
    else if (jsoneq(c, &t[i], "pools") == 0) {
      //
      // "pools": [
      //    ["cn.ss.btc.com", 1800, "kevin1"],
      //    ["cn.ss.btc.com", 1800, "kevin2"],
      // ]
      //
      i++;
      if (t[i].type != JSMN_ARRAY) {
        return false;  // we expect "pools" to be an array of array
      }
      const int poolCount = t[i].size;

      for (int j = 0; j < poolCount; j++) {
        // we expect pools to be an array: 3 elements
        int idx = i + 1 + j*4;
        if (t[idx].type != JSMN_ARRAY || t[idx].size != 3) {
          return false;
        }

        PoolConf pool;
        pool.host_ = getJsonStr(c, &t[idx + 1]);
        pool.port_ = (uint16_t)strtoul(getJsonStr(c, &t[idx + 2]).c_str(), NULL, 10);
        pool.upPoolUserName_ = getJsonStr(c, &t[idx + 3]);

        conf.pools_.push_back(pool);
      }
      i += poolCount * 4;
    }
    else if (jsoneq(c, &t[i], "always_keep_downconn") == 0) {
      string opt = getJsonStr(c, &t[i+1]);
      std::transform(opt.begin(), opt.end(), opt.begin(), ::tolower);
      conf.alwaysKeepDownconn_ = (opt == "true");
      i++;
    }
    else if (jsoneq(c, &t[i], "disconnect_when_lost_asicboost") == 0) {
      string opt = getJsonStr(c, &t[i + 1]);
      std::transform(opt.begin(), opt.end(), opt.begin(), ::tolower);
      conf.disconnectWhenLostAsicBoost_ = (opt == "true");
      i++;
    }
    else if (jsoneq(c, &t[i], "use_ip_as_worker_name") == 0) {
      string opt = getJsonStr(c, &t[i + 1]);
      std::transform(opt.begin(), opt.end(), opt.begin(), ::tolower);
      conf.useIpAsWorkerName_ = (opt == "true");
      i++;
    }
    else if (jsoneq(c, &t[i], "ip_worker_name_format") == 0) {
      conf.ipWorkerNameFormat_ = getJsonStr(c, &t[i+1]);
      i++;
    }
    else if (jsoneq(c, &t[i], "submit_response_from_server") == 0) {
      string opt = getJsonStr(c, &t[i + 1]);
      std::transform(opt.begin(), opt.end(), opt.begin(), ::tolower);
      conf.submitResponseFromServer_ = (opt == "true");
      i++;
    }
    else if (jsoneq(c, &t[i], "fixed_worker_name") == 0) {
      conf.fixedWorkerName_ = getJsonStr(c, &t[i+1]);
      i++;
    }
    else if (jsoneq(c, &t[i], "pool_use_tls") == 0) {
      string opt = getJsonStr(c, &t[i + 1]);
      std::transform(opt.begin(), opt.end(), opt.begin(), ::tolower);
      conf.poolUseTls_ = (opt == "true");
      i++;
    }
  }

  // check parametes
  if (conf.listenIP_.empty()) {
    LOG(ERROR) << "[conf] agent_listen_ip cannot be empty.";
    return false;
  }

  if (conf.listenPort_ == 0) {
    LOG(ERROR) << "[conf] agent_listen_port must be between 1 and 65535.";
    return false;
  }

  if (conf.pools_.empty()) {
    LOG(ERROR) << "[conf] pools cannot be empty.";
    return false;
  }

  for (size_t i=0; i<conf.pools_.size(); i++) {
    const auto &pool = conf.pools_[i];
    if (pool.host_.empty()) {
      LOG(ERROR) << "[conf] the host of pool " << i << " cannot be empty.";
      return false;
    }
    if (pool.port_ == 0) {
      LOG(ERROR) << "[conf] the port of pool " << i << " (" << pool.host_
                 << ") must be between 1 and 65535.";
      return false;
    }
  }

  return true;
}

const char *splitNotify(const string &line, int number) {
  const char *pch = strchr(line.c_str(), '"');
  int i = 1;
  while (pch != NULL) {
    pch = strchr(pch + 1, '"');
    i++;
    if (pch != NULL && i == number) {
      break;
    }
  }
  if (pch == NULL) {
    LOG(ERROR) << "invalid mining.notify: " << line << std::endl;
    return NULL;
  }
  return pch;
}

string str2lower(const string &str) {
  string data = str;
  std::transform(data.begin(), data.end(), data.begin(), ::tolower);
  return data;
}

bool strEmpty(const string &str) {
  return str.find_last_not_of(" \t\f\v\n\r") == str.npos;
}
