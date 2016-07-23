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

#include "Utils.h"

#include <bitset>

//////////////////////////////// SessionIDManager //////////////////////////////
#define MAX_SESSION_ID   0xFFFFu   // 65535 = 2^16 - 1

class SessionIDManager {
  std::bitset<MAX_SESSION_ID + 1> sessionIds_;
  uint16_t allocIdx_;
  int32_t  count_;

public:
  SessionIDManager(): allocIdx_(0), count_(0) {
    sessionIds_.reset();
  }

  bool ifFull() {
    if (count_ == MAX_SESSION_ID + 1) {
      return false;
    }
    return true;
  }

  uint16_t allocSessionId() {
    // find an empty bit
    while (sessionIds_.test(allocIdx_) == true) {
      allocIdx_++;
      if (allocIdx_ > MAX_SESSION_ID) {
        allocIdx_ = 0;
      }
    }

    // set to true
    sessionIds_.set(allocIdx_, true);
    count_++;

    return allocIdx_;
  }

  void freeSessionId(uint16_t sessionId) {
    sessionIds_.set(sessionId, false);
    count_--;
  }
};

#endif
