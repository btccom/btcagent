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

#include <algorithm>

using namespace std;

static string stripEthAddrFromFullName(const string& fullNameStr) {
  const size_t pos = fullNameStr.find('.');
  // The Ethereum address is 42 bytes and starting with "0x" as normal
  // Example: 0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482
  if (pos == 42 && fullNameStr[0] == '0' && (fullNameStr[1] == 'x' || fullNameStr[1] == 'X')) {
    return fullNameStr.substr(pos + 1);
  }
  return fullNameStr;
}

static char Diff2TargetTable[] = { 'f', '7', '3', '1'};

static string ethDiff2Target(uint64_t difficulty) {
  uint8_t diffLog2 = difficulty ? log2(difficulty) : 0;
  string target;
  target.reserve(64);
  target.append(diffLog2 / 4, '0');
  target.append(1, Diff2TargetTable[diffLog2 % 4]);
  target.append(63 - diffLog2 / 4, 'f');
  return target; // NRVO
}

static uint8_t hex2binChar(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return (c - 'a') + 10;
  if (c >= 'A' && c <= 'F')
    return (c - 'A') + 10;
  return 0xff;
}

static bool Hex2Bin(const string &in, uint8_t *out, size_t length) {
  uint8_t h, l;
  // skip space, 0x
  const char *psz = in.c_str();
  size_t size = in.size();
  while (isspace(*psz))
    psz++;
  if (psz[0] == '0' && tolower(psz[1]) == 'x')
    psz += 2;

  // convert
  size_t i = 0;
  while (psz + 2 <= in.c_str() + size) {
    if (i >= length) return false;
    h = hex2binChar(*psz++);
    l = hex2binChar(*psz++);
    if (h == 0xff || l == 0xff) return false;
    out[i++] = (h << 4) | l;
  }
  return true;
}

static const uint32_t MAX_NONCE_PREFIX = 0xffffff;

// NICEHASH_STRATUM uses a different difficulty value than the Ethereum network and BTCPool ETH.
// Conversion between difficulty and target is done the same way as with Bitcoin;
// difficulty of 1 is transformed to target being in HEX:
// 00000000ffff0000000000000000000000000000000000000000000000000000
// @see https://www.nicehash.com/sw/Ethereum_specification_R1.txt
inline double Eth_DiffToNicehashDiff(uint64_t diff) {
  // Ethereum difficulty is numerically equivalent to 2^32 times the difficulty of Bitcoin/NICEHASH_STRATUM.
  return ((double)diff) / ((double)4294967296.0);
}

bool StratumMessageEth::parseMiningSubscribe(string &agent, string &protocol) const {
  for (int i = 1; i < r_; i++) {
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY) {
      int params = t_[i+1].size;
      if (params >= 1) {
        i++;  // ptr move to params
        i++;  // ptr move to params[0]
        agent = getJsonStr(&t_[i]).substr(0, 30);
        protocol = params > 1 ? getJsonStr(&t_[i+1]) : "";
        return true;
      }
    }
  }

  // No parameter means standard stratum
  return true;
}

bool StratumMessageEth::parseMiningAuthorize(string &workerName) const {
  for (int i = 1; i < r_; i++) {
    // STRATUM / NICEHASH_STRATUM: {"id":3, "method":"mining.authorize", "params":["test.aaa", "x"]}
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 1) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      workerName = getJsonStr(&t_[i]);
      return true;
    }
  }

  return false;
}

bool StratumMessageEth::parseMiningSetDifficulty(double &difficulty) const {
  for (int i = 1; i < r_; i++) {
    //
    // { "id": null, "method": "mining.set_difficulty", "params": [2]}
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 1) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      difficulty = strtod(getJsonStr(&t_[i]).c_str(), NULL);
      return true;
    }
  }

  return false;
}

bool StratumMessageEth::parseMiningNotify(StratumJobEth &sjob) const {
  for (int i = 1; i < r_; i++) {
    // Agent mining.notify: there is no need to parse these values
    // {"id":6,"method":"mining.notify","params":
    // ["dd159c7ec5b056ad9e95e7c997829f667bc8e34c6d43fcb9e0c440ed94a85d80",
    // "a8784097a4d03c2d2ac6a3a2beebd0606aa30a8536a700446b40800841c0162c",
    // "0000000112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba",12345,false]}
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 4) {
      i++;  // ptr move to params
      sjob.header_ = getJsonStr(&t_[++i]); // param[0]
      sjob.seed_ = getJsonStr(&t_[++i]); // param[1]
      sjob.target_ = getJsonStr(&t_[++i]); // param[2]
      sjob.isClean_ = getJsonStr(&t_[++i]); // param[4]
      return true;
    }
  }
  return false;
}

bool StratumMessageEth::parseMiningSubmit(ShareEth &share) const {
  for (int i = 1; i < r_; i++) {
    // etherminer (STRATUM)
    // {"id": 4, "method": "mining.submit",
    // "params": ["0x7b9d694c26a210b9f0d35bb9bfdd70a413351111.fatrat1117",
    // "ae778d304393d441bf8e1c47237261675caa3827997f671d8e5ec3bd5d862503",
    // "0x4cc7c01bfbe51c67",
    // "0xae778d304393d441bf8e1c47237261675caa3827997f671d8e5ec3bd5d862503",
    // "0x52fdd9e9a796903c6b88af4192717e77d9a9c6fa6a1366540b65e6bcfa9069aa"]}
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 5) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      if (!Hex2Bin(getJsonStr(&t_[++i]), share.header_, 32)) return false; // param[1]
      if (!Hex2Bin(getJsonStr(&t_[++i]), share.nonce_, 8)) return false; // param[2]
      return true;
    }
  }
  return false;
}

bool StratumMessageEth::parseMiningSubmitNiceHash(ShareEth &share) const {
  for (int i = 1; i < r_; i++) {
    //NICEHASH_STRATUM
    // {"id": 244,
    //  "method": "mining.submit",
    //  "params": [ "username", "bf0488aa", "909d9bbc0f" ]
    // }
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 3) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      if (!Hex2Bin(getJsonStr(&t_[++i]), share.header_, 32)) return false; // param[1]
      auto nonceStr = getJsonStr(&t_[++i]);  // param[2]
      // We shall have 5 bytes nonce as we sent 3 bytes prefix
      if (nonceStr.size() != 10 || !Hex2Bin(nonceStr, share.nonce_ + 3, 5)) return false;
      return true;
    }
  }
  return false;
}

bool StratumMessageEth::parseSubmitLogin(string &workerName) const {
  string part1;
  string part2;
  for (int i = 1; i < r_; i++) {
    // ETH_PROXY (Claymore):              {"worker": "eth1.0", "jsonrpc": "2.0", "params": ["0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482.test.aaa", "x"], "id": 2, "method": "eth_submitLogin"}
    // ETH_PROXY (EthMiner, situation 1): {"id":1, "method":"eth_submitLogin", "params":["0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482"], "worker":"test.aaa"}
    // ETH_PROXY (EthMiner, situation 2): {"id":1, "method":"eth_submitLogin", "params":["test"], "worker":"aaa"}
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 1) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      part1 = getJsonStr(&t_[i]);
    } else if (jsoneq(&t_[i], "worker") == 0 && t_[i+1].type == JSMN_STRING) {
      part2 = getJsonStr(&t_[i+1]);
    }
  }

  workerName = stripEthAddrFromFullName(part1 + '.' + part2);
  return !workerName.empty();
}

bool StratumMessageEth::parseSubmitWork(ShareEth &share) const {
  for (int i = 1; i < r_; i++) {
    // Claymore (ETHPROXY)
    // {"id":4,"method":"eth_submitWork",
    // "params":["0x17a0eae8082fb64c","0x94a789fba387d454312db3287f8440f841de762522da8ba620b7fcf34a80330c",
    // "0x2cc7dad9f2f92519891a2d5f67378e646571b89e5994fe9290d6d669e480fdff"]}
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 3) {
      i++;  // ptr move to params
      if (!Hex2Bin(getJsonStr(&t_[++i]), share.nonce_, 8)) return false; // param[0]
      if (!Hex2Bin(getJsonStr(&t_[++i]), share.header_, 32)) return false; // param[1]
      return true;
    }
  }
  return false;
}

bool StratumMessageEth::parseNoncePrefix(uint16_t &noncePrefix) const {
  for (int i = 1; i < r_; i++) {
    // {
    //   "id": 1,
    //   "result": [
    //     [
    //       "mining.notify",
    //       "ae6812eb4cd7735a302a8a9dd95cf71f",
    //       "EthereumStratum/1.0.0"
    //     ],
    //     "080c"
    //   ],
    //   "error": null
    // }
    if (jsoneq(&t_[i], "result") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 2) {
      i++;  // ptr move to "result"
      if (!(t_[i].type == JSMN_ARRAY && t_[i].size == 2)) {
        return false;
      }
      i += t_[i].size;

      // noncePrefix
      i++;  // ptr move to result[1]
      noncePrefix = strtoul(getJsonStr(&t_[i]).c_str(), NULL, 16);

      return true;
    }
  }
  return false;
}

EthProtocolProxy::EthProtocolProxy(StratumSessionEth &session)
    : EthProtocol{session}
    , noncePrefix_{0} {
}

void EthProtocolProxy::handleRequest(const string &idStr, const StratumMessageEth &smsg) {
  auto method = smsg.getMethod();
  if (method == "eth_getWork") {
    if (session_.state_ == DOWN_AUTHENTICATED) {
      handleRequest_GetWork(idStr, smsg);
    } else {
      responseError(idStr, StratumError::UNAUTHORIZED);
    }
  } else if (method == "eth_submitWork") {
    if (session_.state_ == DOWN_AUTHENTICATED) {
      handleRequest_Submit(idStr, smsg);
    } else {
      responseError(idStr, StratumError::UNAUTHORIZED);
    }
  } else if (method == "eth_submitLogin") {
    if (session_.state_ == DOWN_CONNECTED) {
      handleRequest_Authorize(idStr, smsg);
    } else {
      responseError(idStr, StratumError::UNKNOWN);
    }
  } else if (method == "eth_submitHashrate") {
    responseTrue(idStr);
  } else {
    // unrecognised method, just ignore it
    LOG(WARNING) << "unrecognised method: \"" << method << "\"" << std::endl;
  }
}

void EthProtocolProxy::handleRequest_GetWork(const string &idStr, const StratumMessageEth &smsg) {
  // Clymore eth_getWork
  // {"id":3,"jsonrpc":"2.0","result":
  // ["0x599fffbc07777d4b6455c0e7ca479c9edbceef6c3fec956fecaaf4f2c727a492",
  // "0x1261dfe17d0bf58cb2861ae84734488b1463d282b7ee88ccfa18b7a92a7b77f7",
  // "0x0112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba","0x4ec6f5"]}
  sendMiningNotifyWithId(idStr, static_cast<UpStratumClientEth &>(session_.upSession_).latestJob_);
  session_.sendMiningNotify();
}

void EthProtocolProxy::handleRequest_Authorize(const string &idStr, const StratumMessageEth &smsg) {
  if (smsg.parseSubmitLogin(workerName_)) {
    idLogin_ = idStr;
    session_.getNoncePrefix();
  } else {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
  }
}

void EthProtocolProxy::handleRequest_Submit(const string &idStr, const StratumMessageEth &smsg) {
  ShareEth share;
  if (smsg.parseSubmitWork(share)) {
    session_.submitShare(share);
    responseTrue(idStr);  // we assume shares are valid
  } else {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
  }
}

void EthProtocolProxy::responseError(const string &idStr, int code) {
  auto s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
                           idStr.empty() ? "null" : idStr.c_str(),
                           code,
                           StratumError::toString(code));
  session_.sendData(s);
}

void EthProtocolProxy::responseTrue(const string &idStr) {
  session_.sendData(Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true}\n", idStr.c_str()));
}

void EthProtocolProxy::setNoncePrefix(uint32_t noncePrefix) {
  if (noncePrefix > MAX_NONCE_PREFIX) {
    responseError(idLogin_, StratumError::INTERNAL_ERROR);
    return;
  }

  if (session_.state_ != DOWN_CONNECTED) {
    responseError(idLogin_, StratumError::UNKNOWN);
    return;
  }

  noncePrefix = noncePrefix_;
  session_.handleRequest_Authorize(idLogin_, workerName_);
}

void EthProtocolProxy::setDifficulty(uint64_t difficulty) {
  // Claymore use 58 bytes target
  target_ = ethDiff2Target(difficulty).substr(6);
}

void EthProtocolProxy::sendMiningNotify(const StratumJobEth &sjob) {
  sendMiningNotifyWithId("0", sjob);
}

void EthProtocolProxy::sendMiningNotifyWithId(const string &idStr, const StratumJobEth &sjob) {
  // Claymore eth_getWork
  // {"id":3,"jsonrpc":"2.0","result":
  // ["0x599fffbc07777d4b6455c0e7ca479c9edbceef6c3fec956fecaaf4f2c727a492",
  // "0x1261dfe17d0bf58cb2861ae84734488b1463d282b7ee88ccfa18b7a92a7b77f7",
  // "0x0112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba","0x4ec6f5"]}
  auto s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\","
                           "\"result\":[\"0x%s\",\"0x%s\",\"0x%s\","
                           // nonce cannot start with 0x because of
                           // a compatibility issue with AntMiner E3.
                           "\"%06x\"]}\n",
                           idStr.c_str(),
                           sjob.header_.c_str(),
                           sjob.seed_.c_str(),
                           target_.c_str(),
                           noncePrefix_);
  session_.sendData(s);
}

void EthProtocolStratum::handleRequest(const string &idStr, const StratumMessageEth &smsg) {
  auto method = smsg.getMethod();
  if (method == "mining.submit") {
    if (session_.state_ == DOWN_AUTHENTICATED) {
      handleRequest_Submit(idStr, smsg);
    } else {
      responseError(idStr, StratumError::UNAUTHORIZED);
      // there must be something wrong, send reconnect command
      session_.sendData("{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n");
    }
  } else if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, smsg);
  } else if (method == "mining.authorize") {
    if (session_.state_ == DOWN_SUBSCRIBED) {
      handleRequest_Authorize(idStr, smsg);
    } else {
      responseError(idStr, StratumError::NOT_SUBSCRIBED);
    }
  } else {
    // unrecognised method, just ignore it
    LOG(WARNING) << "unrecognised method: \"" << method << "\"" << std::endl;
  }
}

void EthProtocolStratum::responseError(const string &idStr, int code) {
  auto s = Strings::Format("{\"id\":%s,\"result\":null,\"error\":[%d,\"%s\",null]}\n",
                           idStr.empty() ? "null" : idStr.c_str(),
                           code,
                           StratumError::toString(code));
  session_.sendData(s);
}

void EthProtocolStratum::responseTrue(const string &idStr) {
  session_.sendData("{\"id\":" + idStr + ",\"result\":true,\"error\":null}\n");
}

void EthProtocolStandard::handleRequest_Subscribe(const string &idStr, const StratumMessageEth &smsg) {
  // There is no need to allocate nonce prefix as the client does not support it...
  session_.state_ = DOWN_SUBSCRIBED;
  responseTrue(idStr);
}

void EthProtocolStandard::handleRequest_Submit(const string &idStr, const StratumMessageEth &smsg) {
  ShareEth share;
  if (smsg.parseMiningSubmit(share)) {
    session_.submitShare(share);
    responseTrue(idStr);  // we assume shares are valid
  } else {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
  }
}

void EthProtocolStandard::handleRequest_Authorize(const string &idStr, const StratumMessageEth &smsg) {
  string workerName;
  if (smsg.parseMiningAuthorize(workerName)) {
    session_.handleRequest_Authorize(idStr, workerName);
  } else {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
  }
}

void EthProtocolStandard::setDifficulty(uint64_t difficulty) {
  target_ = ethDiff2Target(difficulty);
}

void EthProtocolStandard::sendMiningNotify(const StratumJobEth &sjob) {
  //Etherminer mining.notify
  //{"id":6,"method":"mining.notify","params":
  //["dd159c7ec5b056ad9e95e7c997829f667bc8e34c6d43fcb9e0c440ed94a85d80",
  //"dd159c7ec5b056ad9e95e7c997829f667bc8e34c6d43fcb9e0c440ed94a85d80",
  //"a8784097a4d03c2d2ac6a3a2beebd0606aa30a8536a700446b40800841c0162c",
  //"0000000112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba",false]}
  auto s = Strings::Format("{\"id\":null,\"method\":\"mining.notify\","
                           "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",%s]}\n",
                           sjob.header_.c_str(),
                           sjob.header_.c_str(),
                           sjob.seed_.c_str(),
                           target_.c_str(),
                           sjob.isClean_.c_str());
  session_.sendData(s);
}

void EthProtocolNiceHash::setNoncePrefix(uint32_t noncePrefix) {
  if (noncePrefix > MAX_NONCE_PREFIX) {
    responseError(idSubscribe_, StratumError::INTERNAL_ERROR);
    return;
  }

  if (session_.state_ != DOWN_CONNECTED) {
    responseError(idSubscribe_, StratumError::UNKNOWN);
    return;
  }

  session_.state_ = DOWN_SUBSCRIBED;
  noncePrefix_ = noncePrefix;

  // mining.notify of NICEHASH_STRATUM's subscribe
  // {
  //   "id": 1,
  //   "result": [
  //     [
  //       "mining.notify",
  //       "ae6812eb4cd7735a302a8a9dd95cf71f",
  //       "EthereumStratum/1.0.0"
  //     ],
  //     "080c"
  //   ],
  //   "error": null
  // }
  auto s = Strings::Format("{\"id\":%s,\"result\":[["
                           "\"mining.notify\","
                           "\"%06x\","
                           "\"EthereumStratum/1.0.0\""
                           "],\"%06x\"],\"error\":null}\n",
                           idSubscribe_.c_str(), noncePrefix, noncePrefix);
  session_.sendData(s);
}

void EthProtocolNiceHash::setDifficulty(uint64_t difficulty) {
  if (difficulty != lastDiff_) {
    // NICEHASH_STRATUM mining.set_difficulty
    // {"id": null,
    //  "method": "mining.set_difficulty",
    //  "params": [ 0.5 ]
    // }
    auto s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[%lf]}\n",
                             Eth_DiffToNicehashDiff(difficulty));
    session_.sendData(s);
    lastDiff_ = difficulty;
  }
}

void EthProtocolNiceHash::handleRequest_Subscribe(const string &idStr, const StratumMessageEth &smsg) {
  session_.getNoncePrefix();
  idSubscribe_ = idStr;
}

void EthProtocolNiceHash::handleRequest_Submit(const string &idStr, const StratumMessageEth &smsg) {
  ShareEth share;
  if (smsg.parseMiningSubmitNiceHash(share)) {
    // Prepend the nonce prefix
    for (size_t i  = 0; i < 3; ++i) {
      share.nonce_[2 - i] = noncePrefix_ >> (i * 8);
    }
    session_.submitShare(share);
    responseTrue(idStr);  // we assume shares are valid
  } else {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
  }
}

void EthProtocolNiceHash::handleRequest_Authorize(const string &idStr, const StratumMessageEth &smsg) {
  string workerName;
  if (smsg.parseMiningAuthorize(workerName)) {
    session_.handleRequest_Authorize(idStr, workerName);
  } else {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
  }
}

void EthProtocolNiceHash::sendMiningNotify(const StratumJobEth &sjob) {
  // NICEHASH_STRATUM mining.notify
  // { "id": null,
  //   "method": "mining.notify",
  //   "params": [
  //     "bf0488aa",
  //     "abad8f99f3918bf903c6a909d9bbc0fdfa5a2f4b9cb1196175ec825c6610126c",
  //     "645cf20198c2f3861e947d4f67e3ab63b7b2e24dcc9095bd9123e7b33371f6cc",
  //     true
  //   ]}
  auto s = Strings::Format("{\"id\":null,\"method\":\"mining.notify\","
                           "\"params\":[\"%s\",\"%s\",\"%s\",%s]}\n",
                           sjob.header_.c_str(),
                           sjob.seed_.c_str(),
                           sjob.header_.c_str(),
                           sjob.isClean_.c_str());
  session_.sendData(s);
}

StratumMessageEth::StratumMessageEth(const string &line) : StratumMessage{line} {
  method_ = findMethod();
}

void StratumServerEth::setNoncePrefix(uint16_t sessionId, uint32_t noncePrefix) {
  auto session = downSessions_[sessionId];
  if (!session) return;
  static_cast<StratumSessionEth *>(session)->setNoncePrefix(noncePrefix);
}

UpStratumClient *StratumServerEth::createUpClient(int8_t idx,
                                                  StratumServer *server) {
  return new UpStratumClientEth(idx, server);
}

StratumSession *StratumServerEth::createDownConnection(UpStratumClient &upSession,
                                                       uint16_t sessionId,
                                                       struct bufferevent *bev,
                                                       StratumServer *server,
                                                       struct in_addr saddr) {
  return new StratumSessionEth(upSession, sessionId, bev, server, saddr);
}

void UpStratumClientEth::handleStratumMessage(const string &line) {
  DLOG(INFO) << "UpStratumClient recv(" << line.size() << "): " << line << std::endl;

  StratumMessageEth smsg(line);
  if (!smsg.isValid()) {
    LOG(ERROR) << "decode line fail, not a json string" << std::endl;
    return;
  }

  if (state_ == UP_AUTHENTICATED) {
    double difficulty = 0;
    if (smsg.parseMiningNotify(latestJob_)) {
      //
      // mining.notify
      //
      server_->sendMiningNotifyToAll(this); // send to all down sessions

      // set last job received time
      lastJobReceivedTime_ = (uint32_t)time(NULL);

      DLOG(INFO) << "up[" << (int32_t)idx_ << "] stratum job"
                 << ", header: "  << latestJob_.header_
                 << ", seed: "    << latestJob_.seed_
                 << ", target: "  << latestJob_.target_
                 << ", clean: "    << latestJob_.isClean_ << std::endl;
    } else if (smsg.parseMiningSetDifficulty(difficulty)) {
      //
      // mining.set_difficulty
      //
      // just set the default pool diff, than ignore
      if (poolDefaultDiff_ == 0) {
        poolDefaultDiff_ = difficulty * 4294967296.0;
      }
    }
  }

  if (state_ == UP_CONNECTED) {
    uint16_t extraNonce1;
    if (!smsg.parseNoncePrefix(extraNonce1)) {
      LOG(FATAL) << "get extra nonce1 and extra nonce2 failure" << std::endl;
      return;
    }
    extraNonce1_ = extraNonce1;
    DLOG(INFO) << "extraNonce1 / SessionID: " << extraNonce1_ << std::endl;

    // subscribe successful
    state_ = UP_SUBSCRIBED;
    sendMiningAuthorize();
    return;
  }

  if (state_ == UP_SUBSCRIBED && smsg.getResultBoolean() == true) {
    //
    // check authenticated result
    // {"error": null, "id": 2, "result": true}
    //
    state_ = UP_AUTHENTICATED;  // authorize successful
    LOG(INFO) << "auth success, name: \"" << userName_
              << "\", extraNonce1: " << extraNonce1_ << std::endl;

    // Register existing miners
    server_->registerWorker(this);

    return;
  }
}

void UpStratumClientEth::handleExMessage(const string *exMessage) {
  uint8_t cmd = exMessage->data()[1];
  switch (cmd) {
  case CMD_SET_NONCE_PREFIX:
    handleExMessage_SetNoncePrefix(exMessage);
    break;
  default:
    LOG(ERROR) << "received unknown ex-message, type: " << static_cast<uint16_t>(cmd)
               << ", len: " << exMessage->size() << std::endl;
    break;
  }
}

void UpStratumClientEth::handleExMessage_SetNoncePrefix(const string *exMessage) {
  //
  // | magic_number(1) | cmd(1) | len (2) | session_id (uint16_t) | nonce prefix (uint32_t) |
  //
  auto p = reinterpret_cast<const uint8_t *>(exMessage->data());
  auto len = *reinterpret_cast<const uint16_t *>(p + 2);
  if (len != 10) {
    LOG(ERROR) << "received CMD_SET_NONCE_PREFIX with invalid length " << len << std::endl;
    return;
  }

  auto sessionId = *reinterpret_cast<const uint16_t *>(p + 4);
  auto noncePrefix = *reinterpret_cast<const uint32_t *>(p + 6);
  static_cast<StratumServerEth *>(server_)->setNoncePrefix(sessionId, noncePrefix);
}

void UpStratumClientEth::sendMiningAuthorize() {
  // do mining.authorize (ETH has no version rolling)
  string s = Strings::Format("{\"id\":1,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"\"]}\n", userName_.c_str());
  sendData(s);
}

StratumSessionEth::StratumSessionEth(UpStratumClient &upSession,
                                     uint16_t sessionId,
                                     struct bufferevent *bev,
                                     StratumServer *server,
                                     struct in_addr saddr)
    : StratumSession{upSession, sessionId, bev, server, saddr}
    , protocol_(new EthProtocolProxy{*this}) {
}

void StratumSessionEth::sendMiningNotify() {
  protocol_->sendMiningNotify(static_cast<UpStratumClientEth &>(upSession_).latestJob_);
}

void StratumSessionEth::sendMiningDifficulty(uint64_t diff) {
  protocol_->setDifficulty(diff);
}

void StratumSessionEth::getNoncePrefix() {
  //
  // | magic_number(1) | cmd(1) | len (2) | session_id (uint16_t) |
  //
  uint16_t len = 1 + 1 + 2 + 2;

  uint8_t cmd = CMD_GET_NONCE_PREFIX;

  string buf;
  buf.resize(len, 0);
  uint8_t *p = (uint8_t *)buf.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = cmd;

  // len
  *(uint16_t *)p = len;
  p += 2;

  // session Id
  *(uint16_t *)p = sessionId_;
  p += 2;

  upSession_.sendRequest(buf);
}

void StratumSessionEth::setNoncePrefix(uint32_t noncePrefix) {
  protocol_->setNoncePrefix(noncePrefix);
}

void StratumSessionEth::submitShare(const ShareEth &share) {
  //
  // | magic_number(1) | cmd(1) | len (2) | session_id (uint16_t) | nonce (8) | header_hash (uint256) |
  //
  uint16_t len = 1 + 1 + 2 + 2 + 8 + 32;

  uint8_t cmd = CMD_SUBMIT_SHARE;

  string buf;
  buf.resize(len, 0);
  uint8_t *p = (uint8_t *)buf.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = cmd;

  // len
  *(uint16_t *)p = len;
  p += 2;

  // session Id
  *(uint16_t *)p = sessionId_;
  p += 2;

  // nonce
  for (int i = 7; i >= 0; --i) {
    *p++ = share.nonce_[i];
  }

  // header hash
  for (int j = 31; j >=0; --j) {
    *p++ = share.header_[j];
  }

  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  // send buf
  upSession_.sendRequest(buf);
}

void StratumSessionEth::handleStratumMessage(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line << std::endl;

  StratumMessageEth smsg(line);
  if (!smsg.isValid()) {
    LOG(ERROR) << "decode line fail, not a json string" << std::endl;
    return;
  }

  string idStr;
  if (smsg.isStringId()) {
    idStr = "\"" + smsg.getId() + "\"";
  } else {
    idStr = smsg.getId();
  }
  if (idStr.empty()) {
    idStr = "null";
  }

  if (!smsg.getMethod().empty()) {
    handleRequest(idStr, smsg);
    return;
  }

  // invalid params
  protocol_->responseError(idStr, StratumError::ILLEGAL_PARARMS);
}

void StratumSessionEth::handleRequest(const string &idStr, const StratumMessageEth &smsg) {
  auto method = smsg.getMethod();
  if (method == "mining.subscribe") {
    if (state_ == DOWN_CONNECTED) {
      handleRequest_Subscribe(idStr, smsg);
    } else {
      protocol_->responseError(idStr, StratumError::UNKNOWN);
      return;
    }
  }

  protocol_->handleRequest(idStr, smsg);
}

void StratumSessionEth::handleRequest_Subscribe(const string &idStr, const StratumMessageEth &smsg) {
  string protocol;
  if (smsg.parseMiningSubscribe(minerAgent_, protocol)) {
    protocol = str2lower(protocol);
    if (protocol.substr(0, 16) == "ethereumstratum/") {
      protocol_ = make_unique<EthProtocolNiceHash>(*this);
    } else {
      protocol_ = make_unique<EthProtocolStandard>(*this);
      state_ = DOWN_SUBSCRIBED;
    }
  }
}

void StratumSessionEth::handleRequest_Authorize(const string &idStr, const string &fullName) {
  workerName_ = getWorkerName(fullName);
  if (workerName_.empty())
    workerName_ = DEFAULT_WORKER_NAME;

  // auth success
  protocol_->responseTrue(idStr);
  state_ = DOWN_AUTHENTICATED;

  // sent sessionId, minerAgent_, workerName to server_
  server_->registerWorker(this);

  // send mining.set_difficulty
   sendMiningDifficulty(upSession_.poolDefaultDiff_);

  // send latest stratum job
   sendMiningNotify();
}
