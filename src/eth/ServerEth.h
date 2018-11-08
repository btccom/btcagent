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
public:
  uint8_t nonce_[8];
  uint8_t header_[32];
};

class StratumJobEth {
public:
  string header_;
  string seed_;
  string target_;
  string isClean_;

  StratumJobEth() {}
  StratumJobEth(const StratumJobEth &r) {
    header_   = r.header_;
    seed_     = r.seed_;
    target_   = r.target_;
    isClean_  = r.isClean_;
  }
};

class StratumMessageEth : public StratumMessage {
public:
  explicit StratumMessageEth(const string &line);
  bool parseMiningSubscribe(string &agent, string &protocol) const;
  bool parseMiningAuthorize(string &workerName) const;
  bool parseMiningSetDifficulty(double &difficulty) const;
  bool parseMiningNotify(StratumJobEth &sjob) const;
  bool parseMiningSubmit(ShareEth &share) const;
  bool parseMiningSubmitNiceHash(ShareEth &share) const;
  bool parseSubmitLogin(string &workerName) const;
  bool parseSubmitWork(ShareEth &share) const;
  bool parseNoncePrefix(uint16_t &noncePrefix) const;
};

class StratumSessionEth;

class EthProtocol {
protected:
  StratumSessionEth &session_;
  explicit EthProtocol(StratumSessionEth &session) : session_{session} {}
public:
  virtual ~EthProtocol() = default;
  virtual void handleRequest(const string &idStr, const StratumMessageEth &smsg) = 0;
  virtual void responseError(const string &idStr, int code) = 0;
  virtual void responseTrue(const string &idStr) = 0;
  virtual void setNoncePrefix(uint32_t noncePrefix) = 0;
  virtual void setDifficulty(uint64_t difficulty) = 0;
  virtual void sendMiningNotify(const StratumJobEth &sjob) = 0;
};

class EthProtocolProxy : public EthProtocol {
public:
  EthProtocolProxy(StratumSessionEth &session);
  void handleRequest(const string &idStr, const StratumMessageEth &smsg) override;
  void responseError(const string &idStr, int code) override;
  void responseTrue(const string &idStr) override;
  void setNoncePrefix(uint32_t noncePrefix) override;
  void setDifficulty(uint64_t difficulty) override;
  void sendMiningNotify(const StratumJobEth &sjob) override;
  void sendMiningNotifyWithId(const string &idStr, const StratumJobEth &sjob);

private:
  void handleRequest_GetWork(const string &idStr, const StratumMessageEth &smsg);
  void handleRequest_Submit(const string &idStr, const StratumMessageEth &smsg);
  void handleRequest_Authorize(const string &idStr, const StratumMessageEth &smsg);

  string idLogin_;
  string workerName_;
  string target_;
  uint32_t noncePrefix_;
};

class EthProtocolStratum : public EthProtocol {
protected:
  explicit EthProtocolStratum(StratumSessionEth &session) : EthProtocol{session} {}
public:
  void handleRequest(const string &idStr, const StratumMessageEth &smsg) override;
  void responseError(const string &idStr, int code) override;
  void responseTrue(const string &idStr) override;

private:
  virtual void handleRequest_Subscribe(const string &idStr, const StratumMessageEth &smsg) = 0;
  virtual void handleRequest_Submit(const string &idStr, const StratumMessageEth &smsg) = 0;
  virtual void handleRequest_Authorize(const string &idStr, const StratumMessageEth &smsg) = 0;
};

class EthProtocolStandard : public EthProtocolStratum {
public:
  explicit EthProtocolStandard(StratumSessionEth &session) : EthProtocolStratum{session} {}
  void setNoncePrefix(uint32_t noncePrefix) override {};
  void setDifficulty(uint64_t difficulty) override;
  void sendMiningNotify(const StratumJobEth &sjob) override;

private:
  void handleRequest_Subscribe(const string &idStr, const StratumMessageEth &smsg) override;
  void handleRequest_Submit(const string &idStr, const StratumMessageEth &smsg) override;
  void handleRequest_Authorize(const string &idStr, const StratumMessageEth &smsg) override;

  string target_;
};

class EthProtocolNiceHash : public EthProtocolStratum {
public:
  explicit EthProtocolNiceHash(StratumSessionEth &session) : EthProtocolStratum{session}, lastDiff_{0}, noncePrefix_{0} {}
  void setNoncePrefix(uint32_t noncePrefix) override;
  void setDifficulty(uint64_t difficulty) override;
  void sendMiningNotify(const StratumJobEth &sjob) override;

private:
  void handleRequest_Subscribe(const string &idStr, const StratumMessageEth &smsg) override;
  void handleRequest_Submit(const string &idStr, const StratumMessageEth &smsg) override;
  void handleRequest_Authorize(const string &idStr, const StratumMessageEth &smsg) override;

  uint64_t lastDiff_;
  string idSubscribe_;
  uint32_t noncePrefix_;
};

class StratumServerEth : public StratumServer {
public:
  using StratumServer::StratumServer;
  void setNoncePrefix(uint16_t sessionId, uint32_t noncePrefix);

private:
  UpStratumClient *createUpClient(int8_t idx,
                                  struct event_base *base,
                                  const string &userName,
                                  StratumServer *server) override;
  StratumSession *createDownConnection(UpStratumClient &upSession,
                                       uint16_t sessionId,
                                       struct bufferevent *bev,
                                       StratumServer *server,
                                       struct in_addr saddr) override;
};

class UpStratumClientEth : public UpStratumClient {
  friend class StratumSessionEth;
  friend class EthProtocolProxy;
public:
  using UpStratumClient::UpStratumClient;
  void handleStratumMessage(const string &line) override;
  void handleExMessage(const string *exMessage) override;

private:
  void handleExMessage_SetNoncePrefix(const string *exMessage);
  void sendMiningAuthorize();

  StratumJobEth latestJob_;
};

class StratumSessionEth : public StratumSession {
  friend class EthProtocolProxy;
  friend class EthProtocolStratum;
  friend class EthProtocolStandard;
  friend class EthProtocolNiceHash;
public:
  StratumSessionEth(UpStratumClient &upSession,
                    uint16_t sessionId,
                    struct bufferevent *bev,
                    StratumServer *server,
                    struct in_addr saddr);

  void sendMiningNotify() override;
  void sendMiningDifficulty(uint64_t diff) override;
  void getNoncePrefix();
  void setNoncePrefix(uint32_t noncePrefix);
  void submitShare(const ShareEth &share);

private:
  void handleStratumMessage(const string &line) override;

  void handleRequest(const string &idStr, const StratumMessageEth &smsg);
  void handleRequest_Subscribe(const string &idStr, const StratumMessageEth &smsg);
  void handleRequest_Authorize(const string &idStr, const string &fullName);

  std::unique_ptr<EthProtocol> protocol_;
};

#endif // #ifndef SERVER_ETH_H_