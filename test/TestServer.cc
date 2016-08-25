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
#include <glog/logging.h>

#include "gtest/gtest.h"
#include "Common.h"
#include "Utils.h"
#include "Server.h"


TEST(Server, SessionIDManager) {
  SessionIDManager m;
  uint16_t id;

  // fill all session ids
  for (uint32_t i = 0; i <= AGENT_MAX_SESSION_ID; i++) {
    ASSERT_EQ(m.allocSessionId(&id), true);
    ASSERT_EQ(id, i);
  }

  // it's full now
  ASSERT_EQ(m.ifFull(), true);
  ASSERT_EQ(m.allocSessionId(&id), false);

  // free the fisrt one
  id = 0u;
  m.freeSessionId(id);
  ASSERT_EQ(m.ifFull(), false);

  ASSERT_EQ(m.allocSessionId(&id), true);
  ASSERT_EQ(id, 0);
  ASSERT_EQ(m.ifFull(), true);

  // free the last one
  id = AGENT_MAX_SESSION_ID;
  m.freeSessionId(id);
  ASSERT_EQ(m.ifFull(), false);

  ASSERT_EQ(m.allocSessionId(&id), true);
  ASSERT_EQ(id, AGENT_MAX_SESSION_ID);
  ASSERT_EQ(m.ifFull(), true);
}
