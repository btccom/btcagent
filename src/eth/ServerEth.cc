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

#include "ServerEth.h"

using namespace std;

void StratumMessageEth::decode() {
}

UpStratumClient *StratumServerEth::createUpClient(int8_t idx,
                                                  struct event_base *base,
                                                  const string &userName,
                                                  StratumServer *server) {
  return new UpStratumClientEth(idx, base, userName, server);
}

StratumSession *StratumServerEth::createDownConnection(int8_t upSessionIdx,
                                                       uint16_t sessionId,
                                                       struct bufferevent *bev,
                                                       StratumServer *server,
                                                       struct in_addr saddr) {
  return new StratumSessionEth(upSessionIdx, sessionId, bev, server, saddr);
}

void UpStratumClientEth::handleStratumMessage(const string &line) {
}

void UpStratumClientEth::handleExMessage_MiningSetDiff(const string *exMessage) {
}

void StratumSessionEth::sendMiningNotify(const string &notifyStr) {
}

void StratumSessionEth::sendMiningDifficulty(uint64_t diff) {
}

void StratumSessionEth::handleStratumMessage(const string &line) {
}
