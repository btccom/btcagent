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

#ifndef SERVER_ETH_H_
#define SERVER_ETH_H_

#include "Server.h"

class ShareEth {
};

class StratumJobEth {
};

class StratumMessageEth : public StratumMessage {
  void decode();

public:
  using StratumMessage::StratumMessage;
};

class StratumServerEth : public StratumServer {
public:
  using StratumServer::StratumServer;

private:
  UpStratumClient *createUpClient(int8_t idx,
                                  struct event_base *base,
                                  const string &userName,
                                  StratumServer *server) override;
  StratumSession *createDownConnection(int8_t upSessionIdx,
                                       uint16_t sessionId,
                                       struct bufferevent *bev,
                                       StratumServer *server,
                                       struct in_addr saddr) override;
};

class UpStratumClientEth : public UpStratumClient {
public:
  using UpStratumClient::UpStratumClient;
  void handleStratumMessage(const string &line) override;
  void handleExMessage_MiningSetDiff(const string *exMessage) override;
};

class StratumSessionEth : public StratumSession {
public:
  using StratumSession::StratumSession;
  void sendMiningNotify(const string &notifyStr) override;
  void sendMiningDifficulty(uint64_t diff) override;

private:
  void handleStratumMessage(const string &line) override;
};

#endif // #ifndef SERVER_ETH_H_