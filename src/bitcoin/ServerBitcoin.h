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

#ifndef SERVER_BITCOIN_H_
#define SERVER_BITCOIN_H_

#include "Server.h"

// Supported BTCAgent features / capabilities, a JSON array.
// Sent within the request / response of agent.get_capabilities for protocol negotiation.
// Known capabilities:
//     verrol: version rolling (shares with a version mask can be submitted through a BTCAgent session).
#define BTCAGENT_PROTOCOL_CAPABILITIES "[\"verrol\"]"
#define BTCAGENT_PROTOCOL_CAP_VERROL "verrol"
#define JSONRPC_GET_CAPS_REQ_ID "agent.caps"

class ShareBitcoin {
public:
  uint32_t jobId_ = 0;
  uint32_t time_ = 0;
  uint32_t extraNonce2_ = 0;
  uint32_t nonce_ = 0;
  uint32_t versionMask_ = 0;
  bool isFakeJob_ = false;

  ShareBitcoin() = default;
  ShareBitcoin(const ShareBitcoin &r) = default;
};

class StratumJobBitcoin {
public:
  uint32_t jobId_;
  string prevHash_;
  int32_t  version_;
  uint32_t time_;      // block time or stratum job time
  bool  isClean_;

  StratumJobBitcoin(): jobId_(0), version_(0), time_(0), isClean_(false) {}
  StratumJobBitcoin(const StratumJobBitcoin &r) {
    jobId_    = r.jobId_;
    prevHash_ = r.prevHash_;
    version_  = r.version_;
    time_     = r.time_;
    isClean_  = r.isClean_;
  }
};

class StratumMessageBitcoin : public StratumMessage {
  ShareBitcoin share_; // mining.submit
  StratumJobBitcoin sjob_; // mining.notify
  string minerAgent_; // mining.subscribe
  string workerName_; // mining.authorize
  uint32_t diff_; // mining.set_difficulty
  uint32_t versionMask_; // mining.set_version_mask
  std::set<string> serverCapabilities_; // agent.get_capabilities

  void decode();

  void _parseMiningSubmit();
  void _parseMiningNotify();
  void _parseMiningSetDifficulty();
  void _parseMiningSetVersionMask();
  void _parseMiningSubscribe();
  void _parseMiningAuthorize();
  void _parseMiningConfigure();
  void _parseAgentGetCapabilities();

public:
  explicit StratumMessageBitcoin(const string &content);

  bool parseMiningSubmit(ShareBitcoin &share) const;
  bool parseMiningSubscribe(string &minerAgent) const;
  bool parseMiningAuthorize(string &workerName) const;
  bool parseMiningNotify(StratumJobBitcoin& sjob) const;
  bool parseMiningSetDifficulty(uint32_t *diff) const;
  bool parseMiningSetVersionMask(uint32_t *versionMask) const;
  bool parseMiningConfigure(uint32_t *versionMask) const;
  bool parseAgentGetCapabilities(std::set<string> &serverCapabilities) const;

  bool getExtraNonce1AndExtraNonce2Size(uint32_t *nonce1, int32_t *n2size) const;
};

class StratumSessionBitcoin;

class StratumServerBitcoin : public StratumServer {
public:
  using StratumServer::StratumServer;
  void submitShare(const ShareBitcoin &share, StratumSessionBitcoin *downSession);

private:
  UpStratumClient *createUpClient(int8_t idx,
                                  StratumServer *server) override;
  StratumSession *createDownConnection(UpStratumClient &upSession,
                                       uint16_t sessionId,
                                       struct bufferevent *bev,
                                       StratumServer *server,
                                       struct in_addr saddr) override;
};

class UpStratumClientBitcoin : public UpStratumClient {
  friend class StratumServerBitcoin;
  friend class StratumSessionBitcoin;
public:
  UpStratumClientBitcoin(int8_t idx, StratumServer *server);

private:
  void handleStratumMessage(const string &line) override;

  void convertMiningNotifyStr(const string &line);
  void sendMiningAuthorize();

  uint32_t versionMask_; // for version rolling

  // latest there job Id & time, use to check if send nTime
  uint8_t  latestJobId_[3];
  uint32_t latestJobGbtTime_[3];
  string   latestMiningNotifyStr_;
};

class StratumSessionBitcoin : public StratumSession {
public:
  using StratumSession::StratumSession;
  void sendMiningNotify() override;
  void sendFakeMiningNotify() override;
  void sendMiningDifficulty(uint64_t diff) override;

private:
  void handleStratumMessage(const string &line) override;

  void handleRequest(const string &idStr, const StratumMessageBitcoin &smsg);
  void handleRequest_Subscribe(const string &idStr, const StratumMessageBitcoin &smsg);
  void handleRequest_Authorize(const string &idStr, const StratumMessageBitcoin &smsg);
  void handleRequest_Submit(const string &idStr, const StratumMessageBitcoin &smsg);
  void handleRequest_MiningConfigure(const string &idStr, const StratumMessageBitcoin &smsg);

  void responseError(const string &idStr, int code);
  void responseTrue(const string &idStr);
};

#endif // #ifndef SERVER_BITCOIN_H_