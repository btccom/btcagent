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

#include "ServerBitcoin.h"

using namespace std;

StratumMessageBitcoin::StratumMessageBitcoin(const string &content) : StratumMessage{content} {
  decode();
}

void StratumMessageBitcoin::decode() {
  // Processing JSONRPC response
  if (id_ == JSONRPC_GET_CAPS_REQ_ID) {
    _parseAgentGetCapabilities();
    return;
  }

  // Processing JSONRPC request / notify
  // find method name
  const string method = findMethod();
  if (method == "") {
    return;
  }

  if (method == "mining.submit") {
    _parseMiningSubmit();
    return;
  }

  if (method == "mining.notify") {
    _parseMiningNotify();
    return;
  }

  if (method == "mining.set_difficulty") {
    _parseMiningSetDifficulty();
    return;
  }

  if (method == "mining.set_version_mask") {
    _parseMiningSetVersionMask();
    return;
  }

  if (method == "mining.subscribe") {
    _parseMiningSubscribe();
    return;
  }

  if (method == "mining.authorize") {
    _parseMiningAuthorize();
    return;
  }

  if (method == "mining.configure") {
    _parseMiningConfigure();
    return;
  }
}

void StratumMessageBitcoin::_parseMiningSubmit() {
  for (int i = 1; i < r_; i++) {
    //
    // {"params": ["slush.miner1", "bf", "00000001", "504e86ed", "b2957c02"],
    //  "id": 4, "method": "mining.submit"}
    //
    // [Worker Name, Job ID, ExtraNonce2(hex), nTime(hex), nonce(hex)]
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 5) {
	  share_.hasVersionMask_ = t_[i+1].size >= 6;

      i++;  // ptr move to params
      i++;  // ptr move to params[0]

      // Job ID
      string idStr = getJsonStr(&t_[i+1]);
      // start with 'f' means a fake job
      if (idStr.size() < 1 || idStr[0] == 'f') {
        share_.isFakeJob_ = true;
      } else {
        share_.jobId_       = (uint8_t) strtoul(idStr.c_str(), NULL, 10);
      }

      // ExtraNonce2(hex)
      share_.extraNonce2_ = (uint32_t)strtoul(getJsonStr(&t_[i+2]).c_str(), NULL, 16);
      // nTime(hex)
      share_.time_        = (uint32_t)strtoul(getJsonStr(&t_[i+3]).c_str(), NULL, 16);
      // nonce(hex)
      share_.nonce_       = (uint32_t)strtoul(getJsonStr(&t_[i+4]).c_str(), NULL, 16);

      if (share_.hasVersionMask_) {
        // versionMask(hex)
        share_.versionMask_ = (uint32_t)strtoul(getJsonStr(&t_[i+5]).c_str(), NULL, 16);
      }

      // set the method_
      method_ = "mining.submit";
      break;
    }
  }
}

void StratumMessageBitcoin::_parseMiningNotify() {
  for (int i = 1; i < r_; i++) {
    //
    // 9 elements in parmas
    // "params": [
    // "",           // Job ID
    // "",           // Hash of previous block
    // "",           // coinbase1
    // "",           // coinbase2
    // [],           // list of merkle branches
    // "00000002",   // block version, hex
    // "1c2ac4af",   // bits, hex
    // "504e86b9",   // time, hex
    // false]        // is_clean
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 9) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      sjob_.jobId_    = (uint8_t)strtoul(getJsonStr(&t_[i]).c_str(), NULL, 10);
      sjob_.prevHash_ = getJsonStr(&t_[i+1]);

      // list of merkle branches
      i += 4;  // ptr move to params[4]: list of merkle branches
      if (t_[i].type != JSMN_ARRAY)
        return;  // should be array

      i += t_[i].size + 1;  // move to params[5]

      sjob_.version_  = (int32_t) strtoul(getJsonStr(&t_[i]).c_str(),   NULL, 16);
      sjob_.time_     = (uint32_t)strtoul(getJsonStr(&t_[i+2]).c_str(), NULL, 16);

      const string isClean = str2lower(getJsonStr(&t_[i+3]));
      sjob_.isClean_  = (isClean == "true") ? true : false;

      // set the method_
      method_ = "mining.notify";
      break;
    }
  }
}

void StratumMessageBitcoin::_parseMiningSetDifficulty() {
  for (int i = 1; i < r_; i++) {
    //
    // { "id": null, "method": "mining.set_difficulty", "params": [2]}
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 1) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      const uint32_t diff = (uint32_t)strtoul(getJsonStr(&t_[i]).c_str(), NULL, 10);

      if (diff > 0) {
        diff_ = diff;
        // set the method_
        method_ = "mining.set_difficulty";
      }
      break;
    }
  }
}

void StratumMessageBitcoin::_parseMiningSetVersionMask() {
  for (int i = 1; i < r_; i++) {
    //
    // {"id":null,"method":"mining.set_version_mask","params":["1fffe000"]}
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 1) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      const uint32_t versionMask = (uint32_t)strtoul(getJsonStr(&t_[i]).c_str(), NULL, 16);

      if (versionMask > 0) {
        versionMask_ = versionMask;
        // set the method_
        method_ = "mining.set_version_mask";
      }
      break;
    }
  }
}

void StratumMessageBitcoin::_parseMiningSubscribe() {
  for (int i = 1; i < r_; i++) {
    //
    // {"id": 1, "method": "mining.subscribe", "params": ["bfgminer/4.4.0-32-gac4e9b3", "01ad557d"]}
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY) {
      // some miners will use empty "params"
      if (t_[i+1].size >= 1) {
        i++;  // ptr move to params
        i++;  // ptr move to params[0]
        minerAgent_ = getJsonStr(&t_[i]);
      }

      // set the method_
      method_ = "mining.subscribe";
      break;
    }
  }
}

void StratumMessageBitcoin::_parseMiningAuthorize() {
  for (int i = 1; i < r_; i++) {
    //
    // {"params": ["slush.miner1", "password"], "id": 2, "method": "mining.authorize"}
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 1) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      workerName_ = getJsonStr(&t_[i]);

      // set the method_
      method_ = "mining.authorize";
      break;
    }
  }
}

void StratumMessageBitcoin::_parseMiningConfigure() {
  for (int i = 1; i < r_; i++) {
    //
    // {"id":3,"method":"mining.configure","params":[
    //    ["version-rolling"],
    //    {"version-rolling.mask":"1fffe000","version-rolling.min-bit-count":2}
    // ]}
    //
    if (jsoneq(&t_[i], "version-rolling.mask") == 0 && t_[i+1].type == JSMN_STRING) {
      i++;  // ptr move to value
      const uint32_t versionMask = (uint32_t)strtoul(getJsonStr(&t_[i]).c_str(), NULL, 16);

      if (versionMask > 0) {
        versionMask_ = versionMask;

        // set the method_
        method_ = "mining.configure";
        break;
      }
    }
  }
}

void StratumMessageBitcoin::_parseAgentGetCapabilities() {
  serverCapabilities_.clear();

  for (int i = 1; i < r_; i++) {
    //
    // {"id":"agent.caps","result":{"capabilities":["verrol","xxx",...]}}
    //
    if (jsoneq(&t_[i], "capabilities") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size >= 1) {
      size_t size = t_[i+1].size;

      i++;  // ptr move to capabilities
      i++;  // ptr move to capabilities[0]

      for (size_t j=0; j<size; j++) {
        if (t_[i].type == JSMN_STRING) {
          serverCapabilities_.insert(getJsonStr(&t_[i]).c_str());
        }
        i++;
      }

      break;
    }
  }

  // set the method_
  method_ = "agent.get_capabilities";
}

bool StratumMessageBitcoin::parseMiningSubmit(ShareBitcoin &share) const {
  if (method_ != "mining.submit")
    return false;
  share = share_;
  return true;
}

bool StratumMessageBitcoin::parseMiningSubscribe(string &minerAgent) const {
  if (method_ != "mining.subscribe")
    return false;
  minerAgent = minerAgent_;
  return true;
}

bool StratumMessageBitcoin::parseMiningAuthorize(string &workerName) const {
  if (method_ != "mining.authorize")
    return false;
  workerName = workerName_;
  return true;
}

bool StratumMessageBitcoin::parseMiningNotify(StratumJobBitcoin &sjob) const {
  if (method_ != "mining.notify")
    return false;
  sjob = sjob_;
  return true;
}

bool StratumMessageBitcoin::parseMiningSetDifficulty(uint32_t *diff) const {
  if (method_ != "mining.set_difficulty")
    return false;
  *diff = diff_;
  return true;
}

bool StratumMessageBitcoin::parseMiningSetVersionMask(uint32_t *versionMask) const {
  if (method_ != "mining.set_version_mask")
    return false;
  *versionMask = versionMask_;
  return true;
}

bool StratumMessageBitcoin::parseMiningConfigure(uint32_t *versionMask) const {
  if (method_ != "mining.configure")
    return false;
  *versionMask = versionMask_;
  return true;
}

bool StratumMessageBitcoin::parseAgentGetCapabilities(std::set<string> &serverCapabilities) const {
  if (method_ != "agent.get_capabilities")
    return false;
  serverCapabilities = serverCapabilities_;
  return true;
}

bool StratumMessageBitcoin::getExtraNonce1AndExtraNonce2Size(uint32_t *nonce1,
                                                      int32_t *n2size) const {
  bool r = false;
  for (int i = 1; i < r_; i++) {
    //
    // 1. Subscriptions details
    // 2. Extranonce1 - Hex-encoded
    // 3. Extranonce2_size
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    if (jsoneq(&t_[i], "result") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 3) {
      i++;  // ptr move to "result"

      //
      // subscriptions details: 2-tuple
      //
      i++;  // ptr move to result[0]
      i++;  // ptr move to result[0][0]
      // ["mining.set_difficulty","01000002"]
      if (!(t_[i].type == JSMN_ARRAY && t_[i].size == 2)) {
        return false;
      }
      i += t_[i].size;

      i++;  // ptr move to result[0][1]
      // ["mining.notify","01000002"]
      if (!(t_[i].type == JSMN_ARRAY && t_[i].size == 2)) {
        return false;
      }
      i += t_[i].size;

      // extranonce1, hex
      i++;  // ptr move to result[1]
      *nonce1 = (uint32_t)strtoul(getJsonStr(&t_[i]).c_str(), NULL, 16);

      // Extranonce2_size
      i++;  // ptr move to result[2]
      *n2size = (int32_t)strtol(getJsonStr(&t_[i]).c_str(), NULL, 10);

      r = true;
      break;
    }
  }
  return r;
}

void StratumServerBitcoin::submitShare(const ShareBitcoin &share,
                                       StratumSessionBitcoin *downSession,
                                       const string &idStr) {
  auto &up = static_cast<UpStratumClientBitcoin &>(downSession->upSession_);

  // ignore fake job shares
  if (share.isFakeJob_) {
    DLOG(INFO) << "ignore a fake job";
    return;
  }

  bool hasVersionMask = share.versionMask_ != 0;
  bool isTimeChanged = true;
  if ((share.jobId_ == up.latestJobId_[2] && share.time_ == up.latestJobGbtTime_[2]) ||
      (share.jobId_ == up.latestJobId_[1] && share.time_ == up.latestJobGbtTime_[1]) ||
      (share.jobId_ == up.latestJobId_[0] && share.time_ == up.latestJobGbtTime_[0])) {
    isTimeChanged = false;
  }

  //
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t)] | [nVersionMask (uint32_t)] |
  //
  uint16_t len = 15;
  if (isTimeChanged) len += 4;
  if (hasVersionMask) len += 4;

  uint8_t cmd = (isTimeChanged && hasVersionMask) ? CMD_SUBMIT_SHARE_WITH_TIME_VER :
                (isTimeChanged)  ? CMD_SUBMIT_SHARE_WITH_TIME :
                (hasVersionMask) ? CMD_SUBMIT_SHARE_WITH_VER :
                /* default */      CMD_SUBMIT_SHARE;

  string buf;
  buf.resize(len, 0);
  uint8_t *p = (uint8_t *)buf.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = cmd;

  // len
  *(uint16_t *)p = len;
  p += 2;

  // jobId
  *p++ = (uint8_t)share.jobId_;

  // session Id
  *(uint16_t *)p = downSession->sessionId_;
  p += 2;

  // extra_nonce2
  *(uint32_t *)p = share.extraNonce2_;
  p += 4;

  // nonce
  *(uint32_t *)p = share.nonce_;
  p += 4;

  // ntime
  if (isTimeChanged) {
    *(uint32_t *)p = share.time_;
    p += 4;
  }

  // nVersionMask
  if (hasVersionMask) {
    *(uint32_t *)p = share.versionMask_;
    p += 4;
  }

  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  // send buf
  up.submitShare(buf, downSession->sessionId_, idStr);
}

UpStratumClient *StratumServerBitcoin::createUpClient(int8_t idx,
                                                      StratumServer *server) {
  return new UpStratumClientBitcoin(idx, server);
}

StratumSession *StratumServerBitcoin::createDownConnection(UpStratumClient &upSession,
                                                           uint16_t sessionId,
                                                           struct bufferevent *bev,
                                                           StratumServer *server,
                                                           struct in_addr saddr) {
  return new StratumSessionBitcoin(upSession, sessionId, bev, server, saddr);
}

void StratumServerBitcoin::sendVersionMaskToAll(const UpStratumClient *conn) {
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSessionBitcoin *s = static_cast<StratumSessionBitcoin *>(downSessions_[i]);
    if (s == NULL || &s->upSession_ != conn) {
      continue;
    }
    s->sendVersionMask();
  }
}

UpStratumClientBitcoin::UpStratumClientBitcoin(int8_t idx,
                                               StratumServer *server)
    : UpStratumClient{idx, server}
    , versionMask_(0)
    , latestJobId_{0, 0, 0}
    , latestJobGbtTime_{0, 0, 0} {
}

void UpStratumClientBitcoin::handleStratumMessage(const string &line) {
  DLOG(INFO) << "UpStratumClient recv(" << line.size() << "): " << line << std::endl;

  StratumMessageBitcoin smsg(line);
  if (!smsg.isValid()) {
    if (!strEmpty(line)) {
      LOG(ERROR) << "decode line fail, not a json string: " << line << std::endl;
    }
    return;
  }

  StratumJobBitcoin sjob;
  uint32_t difficulty = 0u;
  uint32_t versionMask = 0u;
  std::set<string> serverCapabilities;

  if (state_ == UP_AUTHENTICATED) {
    if (smsg.parseMiningNotify(sjob)) {
      //
      // mining.notify
      //
      convertMiningNotifyStr(line);  // convert mining.notify string
      server_->sendMiningNotifyToAll(this); // send to all down sessions

      latestJobId_[0]      = latestJobId_[1];
      latestJobGbtTime_[0] = latestJobGbtTime_[1];
      latestJobId_[1]      = latestJobId_[2];
      latestJobGbtTime_[1] = latestJobGbtTime_[2];

      // the jobId always between [0, 9]
      latestJobId_[2]      = sjob.jobId_;
      latestJobGbtTime_[2] = sjob.time_;

      // set last job received time
      lastJobReceivedTime_ = (uint32_t)time(NULL);

      DLOG(INFO) << "up[" << (int32_t)idx_ << "] stratum job"
                 << ", jobId: "    << sjob.jobId_
                 << ", prevhash: " << sjob.prevHash_
                 << ", version: "  << sjob.version_
                 << ", clean: "    << (sjob.isClean_ ? "true" : "false") << std::endl;
    }
    else if (smsg.parseMiningSetDifficulty(&difficulty)) {
      //
      // mining.set_difficulty
      //
      // just set the default pool diff, than ignore
      if (poolDiffNeedUpdate_) {
        poolDefaultDiff_ = difficulty;
        poolDiffNeedUpdate_ = false;
        server_->sendMiningDifficulty(this, poolDefaultDiff_);
      }
    }
  }

  if (state_ == UP_CONNECTED) {
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    uint32_t nonce1 = 0u;
    int32_t n2size = 0;
    if (!smsg.getExtraNonce1AndExtraNonce2Size(&nonce1, &n2size)) {
      LOG(FATAL) << "get extra nonce1 and extra nonce2 failure" << std::endl;
      return;
    }
    extraNonce1_ = nonce1;
    DLOG(INFO) << "extraNonce1 / SessionID: " << extraNonce1_ << std::endl;

    // check extra nonce2's size, MUST be 8 bytes
    if (n2size != 8) {
      LOG(FATAL) << "extra nonce2's size is NOT 8 bytes" << std::endl;
      return;
    }

    // subscribe successful
    state_ = UP_SUBSCRIBED;
    sendMiningAuthorize();
    return;
  }

  if (state_ == UP_SUBSCRIBED) {
    if (smsg.getResultBoolean() == true) {
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
    else if (smsg.parseMiningSetVersionMask(&versionMask)) {
      //
      // {"id":null,"method":"mining.set_version_mask","params":["1fffe000"]}
      //

      bool sendVersionMask = (versionMask_ != versionMask);
      versionMask_ = versionMask;

      if (sendVersionMask) {
        static_cast<StratumServerBitcoin *>(server_)->sendVersionMaskToAll(this);
      }
    }
    else if (smsg.parseAgentGetCapabilities(serverCapabilities)) {
      // Check if the server supports BTCAgent's version rolling
      if (serverCapabilities.count(BTCAGENT_PROTOCOL_CAP_VERROL) > 0) {
         LOG(INFO) << "AsicBoost via BTCAgent enabled, allowed version mask: "
                << Strings::Format("%08x", versionMask_) << std::endl;
      }

      if (submitResponseFromServer_) {
        // Check if the server can response of the submit
        if (serverCapabilities.count(BTCAGENT_PROTOCOL_CAP_SUBRES) == 0) {
          submitResponseFromServer_ = false;
          LOG(WARNING) << "Pool server [" << (int32_t)idx_ << "] cannot send response of the submit!";
        } else {
          LOG(INFO) << "Pool server [" << (int32_t)idx_ << "] will send response of the submit.";
        }
      }
    }
  }
}

void UpStratumClientBitcoin::convertMiningNotifyStr(const string &line) {
  const char *pch = splitNotify(line);
  latestMiningNotifyStr_.clear();

  latestMiningNotifyStr_.append(line.c_str(), pch - line.c_str());
  latestMiningNotifyStr_.append(Strings::Format("%08x", extraNonce1_));
  latestMiningNotifyStr_.append(pch);
}

void UpStratumClientBitcoin::sendMiningAuthorize() {
  // do mining.configure (for version rolling)
  string s = Strings::Format("{\"id\":1,\"method\":\"mining.configure\",\"params\":["
                          "[\"version-rolling\"],"
                          "{\"version-rolling.mask\":\"ffffffff\",\"version-rolling.min-bit-count\":0}"
                      "]}\n");
  sendData(s);

  // do agent.get_capabilities
  string caps = "\"" BTCAGENT_PROTOCOL_CAP_VERROL "\"";
  if (submitResponseFromServer_) {
    caps += ",\"" BTCAGENT_PROTOCOL_CAP_SUBRES "\"";
  }
  string getCaps = "{\"id\":\"" JSONRPC_GET_CAPS_REQ_ID "\",\"method\":\"agent.get_capabilities\",\"params\":[[" + caps + "]]}\n";
  sendData(getCaps);

  // do mining.authorize
  s = Strings::Format("{\"id\":1,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"\"]}\n", userName_.c_str());
  sendData(s);

  // fix subres (submit_response_from_server)
  // Subres negotiation must be sent after authentication, or sserver will not send the response.
  sendData(getCaps);
}

void StratumSessionBitcoin::sendMiningNotify() {
  sendData(static_cast<UpStratumClientBitcoin &>(upSession_).latestMiningNotifyStr_);
}

void StratumSessionBitcoin::sendFakeMiningNotify() {
  const string &notify = static_cast<UpStratumClientBitcoin &>(upSession_).latestMiningNotifyStr_;
  string fakeNotify;
  time_t now = time(nullptr);

  const char *idPosition = splitNotify(notify, 9);
  const char *pch = splitNotify(notify);

  if (idPosition != nullptr && pch != nullptr && ++idPosition < pch) {
    fakeNotify.append(notify.c_str(), idPosition - notify.c_str());
    fakeNotify.append(Strings::Format("f%04" PRIx16, (uint16_t)now));
    fakeNotify.append(idPosition, pch - idPosition);
    fakeNotify.append(Strings::Format("%016" PRIx64, (uint64_t)now));
    fakeNotify.append(pch);
  } else {
    fakeNotify = notify;
  }

  sendData(fakeNotify);
}

void StratumSessionBitcoin::sendMiningDifficulty(uint64_t diff) {
  sendData(Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[%" PRIu64"]}\n", diff));
}

void StratumSessionBitcoin::sendSubmitResponse(const string &idStr, int status) {
  if (StratumStatus::isAccepted(status)) {
    responseTrue(idStr);
  } else {
    responseError(idStr, status);
  }
}

void StratumSessionBitcoin::sendVersionMask() {
  if (wantedVersionMask_ == 0) {
    return;
  }

  const uint32_t allowedVersionMask = static_cast<UpStratumClientBitcoin &>(upSession_).allowedVersionMask();
  const uint32_t versionMask = wantedVersionMask_ & allowedVersionMask;

  if (allowedVersionMask == 0) {
    LOG(WARNING) << Strings::Format("The server doesn't support AsicBoost! Version mask of server: %08x, miner: %08x", allowedVersionMask, wantedVersionMask_) << std::endl;
  }
  else if (versionMask == 0) {
    LOG(WARNING) << Strings::Format("Incompatible AsicBoost version mask detected, server: %08x, miner: %08x", allowedVersionMask, wantedVersionMask_) << std::endl;
  }

  string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[\"%08x\"]}\n", versionMask);
  sendData(s);
}

void StratumSessionBitcoin::handleStratumMessage(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line << std::endl;

  StratumMessageBitcoin smsg(line);
  if (!smsg.isValid()) {
    if (!strEmpty(line)) {
      LOG(ERROR) << "decode line fail, not a json string: " << line << std::endl;
    }
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
  responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
}

void StratumSessionBitcoin::handleRequest(const string &idStr,
                                          const StratumMessageBitcoin &smsg) {
  const string method = smsg.getMethod();
  if (method == "mining.submit") {  // most of requests are 'mining.submit'
    handleRequest_Submit(idStr, smsg);
  }
  else if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, smsg);
  }
  else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, smsg);
  }
  else if (method == "mining.configure") {
    handleRequest_MiningConfigure(idStr, smsg);
  } else {
    // unrecognised method, just ignore it
    LOG(WARNING) << "unrecognised method: \"" << method << "\"" << std::endl;
  }
}

void StratumSessionBitcoin::handleRequest_Subscribe(const string &idStr,
                                                    const StratumMessageBitcoin &smsg) {
  if (state_ != DOWN_CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }
  state_ = DOWN_SUBSCRIBED;

  //
  //  params[0] = client version     [optional]
  //  params[1] = session id of pool [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["bfgminer/4.4.0-32-gac4e9b3", "01ad557d"]}
  //

  string minerAgent;
  if (!smsg.parseMiningSubscribe(minerAgent)) {
    minerAgent = "unknown";
  }

  // 30 is max length for miner agent
  minerAgent_ = minerAgent.substr(0, 30);

  //
  // Response:
  //
  //  result[0] = 2-tuple with name of subscribed notification and subscription ID.
  //              Theoretically it may be used for unsubscribing, but obviously miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase.
  //  result[2] = Extranonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  //
  assert(kExtraNonce2Size_ == 4);
  const uint32_t extraNonce1 = (uint32_t)sessionId_;
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"%08x\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%08x\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1, extraNonce1,
                                   extraNonce1, kExtraNonce2Size_);
  sendData(s);
}

void StratumSessionBitcoin::handleRequest_Authorize(const string &idStr,
                                                    const StratumMessageBitcoin &smsg) {
  if (state_ != DOWN_SUBSCRIBED) {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }

  //
  //  params[0] = user[.worker]
  //  params[1] = password
  //  eg. {"params": ["slush.miner1", "password"], "id": 2, "method": "mining.authorize"}
  //

  string fullWorkerName;
  if (!smsg.parseMiningAuthorize(fullWorkerName)) {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  setWorkerName(fullWorkerName);

  // auth success
  responseTrue(idStr);
  state_ = DOWN_AUTHENTICATED;

  // sent sessionId, minerAgent_, workerName to server_
  server_->registerWorker(this);

  // send mining.set_difficulty
  sendMiningDifficulty(upSession_.poolDefaultDiff_);

  // send latest stratum job
  sendMiningNotify();
}

void StratumSessionBitcoin::handleRequest_MiningConfigure(const string &idStr,
                                                          const StratumMessageBitcoin &smsg) {
  // mining.configure can be called before mining.authorize and
  // is usually called before it.
  /*if (state_ != DOWN_AUTHENTICATED) {
    responseError(idStr, StratumStatus::UNAUTHORIZED);	    responseError(idStr, StratumStatus::UNAUTHORIZED);
    return;	    return;
  }	  }*/

  if (!smsg.parseMiningConfigure(&wantedVersionMask_ )) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  const uint32_t allowedVersionMask = static_cast<UpStratumClientBitcoin &>(upSession_).allowedVersionMask();
  const uint32_t versionMask = wantedVersionMask_ & allowedVersionMask;

  if (allowedVersionMask == 0) {
    LOG(WARNING) << Strings::Format("The server doesn't support AsicBoost! Version mask of server: %08x, miner: %08x", allowedVersionMask, wantedVersionMask_) << std::endl;
  }
  else if (versionMask == 0) {
    LOG(WARNING) << Strings::Format("Incompatible AsicBoost version mask detected, server: %08x, miner: %08x", allowedVersionMask, wantedVersionMask_) << std::endl;
  }

  string s = Strings::Format("{\"id\":%s,\"result\":{\"version-rolling\":true,\"version-rolling.mask\":\"%08x\"},\"error\":null}\n"
                             "{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[\"%08x\"]}\n",
                             idStr.c_str(), versionMask, versionMask);
  sendData(s);
}

void StratumSessionBitcoin::handleRequest_Submit(const string &idStr,
                                                 const StratumMessageBitcoin &smsg) {
  if (state_ != DOWN_AUTHENTICATED) {
    responseError(idStr, StratumStatus::UNAUTHORIZED);
    // there must be something wrong, send reconnect command
    const string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    sendData(s);
    return;
  }

  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  ShareBitcoin share;
  if (!smsg.parseMiningSubmit(share)) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  if (share.hasVersionMask_) {
    versionRollingShareCounter_++;
  }
  else if (versionRollingShareCounter_ > 100 && server_->disconnectWhenLostAsicBoost()) {
    // Version rolling disabled mid-way, it may be a firmware issue.
    // Reconnect to avoid loss of hashrate.
    const string s =
        "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    sendData(s);

    // get source IP address
    char saddrBuffer[INET_ADDRSTRLEN];
    evutil_inet_ntop(AF_INET, &saddr_, saddrBuffer, INET_ADDRSTRLEN);

    LOG(INFO) << "AsicBoost disabled mid-way, send client.reconnect. "
              << "worker: " << workerName_ << ", ip: " << saddrBuffer
              << ", version rolling shares: " << versionRollingShareCounter_
              << std::endl;
  }

  // submit share
  static_cast<StratumServerBitcoin *>(server_)->submitShare(share, this, idStr);

  if (!upSession_.submitResponseFromServer_ || share.isFakeJob_) {
    responseTrue(idStr);  // we assume shares are valid
  }
}

void StratumSessionBitcoin::responseError(const string &idStr, int errCode) {
  //
  // {"id": 10, "result": null, "error":[21, "Job not found", null]}
  //
  char buf[128];
  int len = snprintf(buf, sizeof(buf),
                     "{\"id\":%s,\"result\":null,\"error\":[%d,\"%s\",null]}\n",
                     idStr.empty() ? "null" : idStr.c_str(),
                     errCode, StratumStatus::toString(errCode));
  sendData(buf, len);
}

void StratumSessionBitcoin::responseTrue(const string &idStr) {
  const string s = "{\"id\":" + idStr + ",\"result\":true,\"error\":null}\n";
  sendData(s);
}
