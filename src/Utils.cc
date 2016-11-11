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

bool parseConfJson(const string &jsonStr,
                   string &listenIP, string &listenPort,
                   std::vector<PoolConf> &poolConfs) {
  jsmn_parser p;
  jsmn_init(&p);
  jsmntok_t t[64]; // we expect no more than 64 tokens
  int r;
  const char *c = jsonStr.c_str();

  // pase json string
  r = jsmn_parse(&p, jsonStr.c_str(), jsonStr.length(), t, sizeof(t)/sizeof(t[0]));

  // assume the top-level element is an object
  if (r < 1 || t[0].type != JSMN_OBJECT) {
    LOG(ERROR) << "json decode failure";
    return false;
  }

  for (int i = 1; i < r; i++) {
    if (jsoneq(c, &t[i], "agent_listen_ip") == 0) {
      listenIP = getJsonStr(c, &t[i+1]);
      i++;
    }
    else if (jsoneq(c, &t[i], "agent_listen_port") == 0) {
      listenPort = getJsonStr(c, &t[i+1]);
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

        PoolConf conf;
        conf.host_ = getJsonStr(c, &t[idx + 1]);
        conf.port_ = (uint16_t)strtoul(getJsonStr(c, &t[idx + 2]).c_str(), NULL, 10);
        conf.upPoolUserName_= getJsonStr(c, &t[idx + 3]);

        poolConfs.push_back(conf);
      }
      i += poolCount * 4;
    }
  }

  // check parametes
  if (listenIP.length() && listenPort.length() && poolConfs.size()) {
    return true;
  }

  return false;
}

const char *splitNotify(const string &line) {
  const char *pch = strchr(line.c_str(), '"');
  int i = 1;
  while (pch != NULL) {
    pch = strchr(pch + 1, '"');
    i++;
    if (pch != NULL && i == 14) {
      break;
    }
  }
  if (pch == NULL) {
    LOG(ERROR) << "invalid mining.notify: " << line;
    return NULL;
  }
  return pch;
}

string str2lower(const string &str) {
  string data = str;
	std::transform(data.begin(), data.end(), data.begin(), ::tolower);
  return data;
}
