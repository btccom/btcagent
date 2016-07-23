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
#ifndef SERVER_H_
#define SERVER_H_

#include "Common.h"
#include "Utils.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <bitset>

#include "utilities_js.hpp"



//////////////////////////////// StratumError ////////////////////////////////
class StratumError {
public:
  enum {
    NO_ERROR        = 0,

    UNKNOWN         = 20,
    JOB_NOT_FOUND   = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY  = 23,
    UNAUTHORIZED    = 24,
    NOT_SUBSCRIBED  = 25,

    ILLEGAL_METHOD   = 26,
    ILLEGAL_PARARMS  = 27,
    IP_BANNED        = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR   = 30,
    TIME_TOO_OLD     = 31,
    TIME_TOO_NEW     = 32
  };
  static const char * toString(int err);
};




//////////////////////////////// SessionIDManager //////////////////////////////
#define MAX_SESSION_ID   0xFFFFu   // 65535 = 2^16 - 1

class SessionIDManager {
  std::bitset<MAX_SESSION_ID + 1> sessionIds_;
  uint16_t allocIdx_;
  int32_t  count_;

public:
  SessionIDManager();

  bool ifFull();
  uint16_t allocSessionId();
  void freeSessionId(const uint16_t sessionId);
};




////////////////////////////////// StratumSession //////////////////////////////
class StratumServer {
public:
  SessionIDManager sessionIDManager_;

public:
  StratumServer();
  ~StratumServer();
};




////////////////////////////////// StratumSession //////////////////////////////
class StratumSession {
  // mining state
  enum State {
    CONNECTED     = 0,
    SUBSCRIBED    = 1,
    AUTHENTICATED = 2
  };

  //----------------------
  static const int32_t kExtraNonce2Size_ = 4;
  State state_;
  uint16_t sessionId_;
  char *minerAgent_;

  void setReadTimeout(const int32_t timeout);

  bool tryReadLine(string &line);
  void handleLine(const string &line);

  void handleRequest(const string &idStr, const string &method,
                     const JsonNode &jparams);
  void handleRequest_Subscribe  (const string &idStr, const JsonNode &jparams);
  void handleRequest_Authorize  (const string &idStr, const JsonNode &jparams);
  void handleRequest_Submit     (const string &idStr, const JsonNode &jparams);

  void responseError(const string &idStr, int code);
  void responseTrue(const string &idStr);

public:
  struct bufferevent* bev_;
  evutil_socket_t fd_;
  StratumServer *server_;


public:
  StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                 StratumServer *server);
  ~StratumSession();

  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }

  void recvData();

};

#endif
