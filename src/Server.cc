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
#include "Server.h"

#ifndef _WIN32
 #include <arpa/inet.h>
#endif

#include <time.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/util.h>

#if (defined _WIN32 && defined USE_IOCP)
 #include <event2/thread.h>
#endif

static
bool resolve(const string &host, struct in_addr *sin_addr) {
  struct evutil_addrinfo *ai = nullptr;
  struct evutil_addrinfo hints_in;
  memset(&hints_in, 0, sizeof(evutil_addrinfo));
  // AF_INET, v4; AF_INT6, v6; AF_UNSPEC, both v4 & v6
  hints_in.ai_family   = AF_UNSPEC;
  hints_in.ai_socktype = SOCK_STREAM;
  hints_in.ai_protocol = IPPROTO_TCP;
  hints_in.ai_flags    = EVUTIL_AI_ADDRCONFIG;

  // TODO: use non-blocking to resolve hostname
  int err = evutil_getaddrinfo(host.c_str(), nullptr, &hints_in, &ai);
  if (err != 0) {
    LOG(ERROR) << "evutil_getaddrinfo err: " << err << ", " << evutil_gai_strerror(err) << std::endl;
    return false;
  }
  if (ai == nullptr) {
    LOG(ERROR) << "evutil_getaddrinfo res is nullptr" << std::endl;
    return false;
  }

  // only get the first record, ignore ai = ai->ai_next
  if (ai->ai_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in*)ai->ai_addr;
    *sin_addr = sin->sin_addr;

    char ipStr[INET_ADDRSTRLEN];
    evutil_inet_ntop(AF_INET, &(sin->sin_addr), ipStr, INET_ADDRSTRLEN);
    LOG(INFO) << "resolve host: " << host << ", ip: " << ipStr << std::endl;
  } else if (ai->ai_family == AF_INET6) {
    // not support yet
    LOG(ERROR) << "not support ipv6 yet" << std::endl;
    return false;
  }
  evutil_freeaddrinfo(ai);
  return true;
}

static
bool tryReadLine(string &line, struct evbuffer *inBuf) {
  line.clear();

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(inBuf, nullptr, nullptr, EVBUFFER_EOL_LF);
  if (loc.pos == -1) {
    return false;  // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1);  // containing "\n"
  evbuffer_remove(inBuf, (void *)line.data(), line.size());

  return true;
}

static
void splitWorkerName(const string &fullName, string &userName, string &workerName) {
  size_t pos = fullName.find(".");
  if (pos == fullName.npos) {
    userName = fullName;
    workerName = "";
  } else {
    userName = fullName.substr(0, pos);
    workerName = fullName.substr(pos + 1);
  }
}

inline static
void closeSocket(evutil_socket_t fd) {
  #ifdef _WIN32
    closesocket(fd);
  #else
    close(fd);
  #endif
}

//////////////////////////////// StratumError ////////////////////////////////
const char * StratumError::toString(int err) {
  switch (err) {
    case NO_ERROR:
      return "no error";

    case JOB_NOT_FOUND:
      return "Job not found (=stale)";
    case DUPLICATE_SHARE:
      return "Duplicate share";
    case LOW_DIFFICULTY:
      return "Low difficulty";
    case UNAUTHORIZED:
      return "Unauthorized worker";
    case NOT_SUBSCRIBED:
      return "Not subscribed";

    case ILLEGAL_METHOD:
      return "Illegal method";
    case ILLEGAL_PARARMS:
      return "Illegal params";
    case IP_BANNED:
      return "Ip banned";
    case INVALID_USERNAME:
      return "Invliad username";
    case INTERNAL_ERROR:
      return "Internal error";
    case TIME_TOO_OLD:
      return "Time too old";
    case TIME_TOO_NEW:
      return "Time too new";

    case UNKNOWN: default:
      return "Unknown";
  }
}


//////////////////////////////// SessionIDManager //////////////////////////////
SessionIDManager::SessionIDManager(): count_(0), allocIdx_(0) {
  sessionIds_.reset();
}

bool SessionIDManager::ifFull() {
  if (count_ >= (int32_t)(AGENT_MAX_SESSION_ID + 1)) {
    return true;
  }
  return false;
}

bool SessionIDManager::allocSessionId(uint16_t *id) {
  assert(AGENT_MAX_SESSION_ID < UINT16_MAX);

  if (ifFull())
    return false;

  const uint32_t beginIdx = allocIdx_;
  while (sessionIds_.test(allocIdx_) == true) {
    allocIdx_++;
    if (allocIdx_ > AGENT_MAX_SESSION_ID) {
      allocIdx_ = 0;
    }

    // should not be here, just in case dead loop
    if (allocIdx_ == beginIdx) {
      return false;
    }
  }

  // set to true
  sessionIds_.set(allocIdx_, true);
  count_++;

  assert(allocIdx_ <= UINT16_MAX);
  *id = (uint16_t)allocIdx_;
  return true;
}

void SessionIDManager::freeSessionId(const uint16_t sessionId) {
  sessionIds_.set(sessionId, false);
  count_--;
}


///////////////////////////////// StratumMessage //////////////////////////////
StratumMessage::StratumMessage(const string &content):
content_(content), isStringId_(false), r_(0), diff_(0) {
  parse();
}
StratumMessage::~StratumMessage() {
}

string StratumMessage::getJsonStr(const jsmntok_t *t) const {
  return ::getJsonStr(content_.c_str(), t);
}

int StratumMessage::jsoneq(const jsmntok_t *tok, const char *s) const {
  const char *json = content_.c_str();
  if (tok->type == JSMN_STRING &&
      (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

string StratumMessage::findMethod() const {
  for (int i = 1; i < r_; i++) {
    if (jsoneq(&t_[i], "method") == 0) {
      return getJsonStr(&t_[i+1]);
    }
  }
  return "";
}

string StratumMessage::parseId() {
  for (int i = 1; i < r_; i++) {
    if (jsoneq(&t_[i], "id") == 0) {
      i++;

      isStringId_ = (t_[i].type == JSMN_STRING) ? true : false;
      id_ = getJsonStr(&t_[i]);
    }
  }
  return "";
}

void StratumMessage::_parseMiningSubmit() {
  for (int i = 1; i < r_; i++) {
    //
    // {"params": ["slush.miner1", "bf", "00000001", "504e86ed", "b2957c02"],
    //  "id": 4, "method": "mining.submit"}
    //
    // [Worker Name, Job ID, ExtraNonce2(hex), nTime(hex), nonce(hex)]
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 5) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]

      // Job ID
      share_.jobId_       = (uint8_t) strtoul(getJsonStr(&t_[i+1]).c_str(), nullptr, 10);
      // ExtraNonce2(hex)
      share_.extraNonce2_ = (uint32_t)strtoul(getJsonStr(&t_[i+2]).c_str(), nullptr, 16);
      // nTime(hex)
      share_.time_        = (uint32_t)strtoul(getJsonStr(&t_[i+3]).c_str(), nullptr, 16);
      // nonce(hex)
      share_.nonce_       = (uint32_t)strtoul(getJsonStr(&t_[i+4]).c_str(), nullptr, 16);

      // set the method_
      method_ = "mining.submit";
      break;
    }
  }
}

void StratumMessage::_parseMiningNotify() {
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
      sjob_.jobId_    = (uint8_t)strtoul(getJsonStr(&t_[i]).c_str(), nullptr, 10);
      sjob_.prevHash_ = getJsonStr(&t_[i+1]);

      // list of merkle branches
      i += 4;  // ptr move to params[4]: list of merkle branches
      if (t_[i].type != JSMN_ARRAY)
        return;  // should be array

      i += t_[i].size + 1;  // move to params[5]

      sjob_.version_  = (uint32_t)strtoul(getJsonStr(&t_[i]).c_str(),   nullptr, 16);
      sjob_.time_     = (uint32_t)strtoul(getJsonStr(&t_[i+2]).c_str(), nullptr, 16);

      const string isClean = str2lower(getJsonStr(&t_[i+3]));
      sjob_.isClean_  = (isClean == "true") ? true : false;

      // set the method_
      method_ = "mining.notify";
      break;
    }
  }
}

void StratumMessage::_parseMiningSetDifficulty() {
  for (int i = 1; i < r_; i++) {
    //
    // { "id": nullptr, "method": "mining.set_difficulty", "params": [2]}
    //
    if (jsoneq(&t_[i], "params") == 0 && t_[i+1].type == JSMN_ARRAY && t_[i+1].size == 1) {
      i++;  // ptr move to params
      i++;  // ptr move to params[0]
      const uint32_t diff = (uint32_t)strtoul(getJsonStr(&t_[i]).c_str(), nullptr, 10);

      if (diff > 0) {
        diff_ = diff;
        // set the method_
        method_ = "mining.set_difficulty";
      }
      break;
    }
  }
}

void StratumMessage::_parseMiningSubscribe() {
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

void StratumMessage::_parseMiningAuthorize() {
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

void StratumMessage::parse() {
  jsmn_parser p;
  jsmn_init(&p);

  r_ = jsmn_parse(&p, content_.c_str(), content_.length(), t_, sizeof(t_)/sizeof(t_[0]));
  if (r_ < 0) {
    LOG(ERROR) << "failed to parse JSON: " << r_ << std::endl;
    return;
  }

  // assume the top-level element is an object
  if (r_ < 1 || t_[0].type != JSMN_OBJECT)
    return;

  parseId();

  // find method name
  const string method = findMethod();
  if (method == "")
    return;

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

  if (method == "mining.subscribe") {
    _parseMiningSubscribe();
    return;
  }

  if (method == "mining.authorize") {
    _parseMiningAuthorize();
    return;
  }
}

string StratumMessage::getMethod() const {
  return method_;
}

string StratumMessage::getId() const {
  return id_;
}
bool StratumMessage::isStringId() const {
  return isStringId_;
}

bool StratumMessage::getResultBoolean() const {
  for (int i = 1; i < r_; i++) {
    if (jsoneq(&t_[i], "result") == 0 && t_[i+1].type == JSMN_PRIMITIVE) {
      const string s = getJsonStr(&t_[i+1]);
      return str2lower(s) == "true" ? true : false;
    }
  }
  return false;
}

bool StratumMessage::isValid() const {
  // assume the top-level element is an object
  return (r_ < 1 || t_[0].type != JSMN_OBJECT) ? false : true;
}

bool StratumMessage::parseMiningSubmit(Share &share) const {
  if (method_ != "mining.submit")
    return false;
  share = share_;
  return true;
}

bool StratumMessage::parseMiningSubscribe(string &minerAgent) const {
  if (method_ != "mining.subscribe")
    return false;
  minerAgent = minerAgent_;
  return true;
}

bool StratumMessage::parseMiningAuthorize(string &workerName) const {
  if (method_ != "mining.authorize")
    return false;
  workerName = workerName_;
  return true;

}
bool StratumMessage::parseMiningNotify(StratumJob &sjob) const {
  if (method_ != "mining.notify")
    return false;
  sjob = sjob_;
  return true;
}
bool StratumMessage::parseMiningSetDifficulty(uint32_t *diff) const {
  if (method_ != "mining.set_difficulty")
    return false;
  *diff = diff_;
  return true;
}

bool StratumMessage::getExtraNonce1AndExtraNonce2Size(uint32_t *nonce1,
                                                      int32_t *n2size) const {
  bool r = false;
  for (int i = 1; i < r_; i++) {
    //
    // 1. Subscriptions details
    // 2. Extranonce1 - Hex-encoded
    // 3. Extranonce2_size
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":nullptr}
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
      *nonce1 = (uint32_t)strtoul(getJsonStr(&t_[i]).c_str(), nullptr, 16);

      // Extranonce2_size
      i++;  // ptr move to result[2]
      *n2size = (int32_t)strtol(getJsonStr(&t_[i]).c_str(), nullptr, 10);

      r = true;
      break;
    }
  }
  return r;
}


///////////////////////////////// UpStratumClient //////////////////////////////
UpStratumClient::UpStratumClient(StratumSession *downSession,
                                 struct event_base *base,
                                 StratumServer *server)
: state_(UP_INIT), server_(server), poolDefaultDiff_(0)
{
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  assert(bev_ != NULL);

  inBuf_ = evbuffer_new();
  assert(inBuf_ != NULL);

  bufferevent_setcb(bev_,
                    StratumServer::upReadCallback, NULL,
                    StratumServer::upEventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);

  userName_ = downSession->userName_;
  extraNonce1_ = 0u;
  extraNonce2_ = 0u;

  latestJobId_[0] = latestJobId_[1] = latestJobId_[2] = 0;
  latestJobGbtTime_[0] = latestJobGbtTime_[1] = latestJobGbtTime_[2] = 0;

  lastJobReceivedTime_ = 0u;

  // session with the same sessionID should not exist
  assert(downSessions_.find(downSession->sessionId_) == downSessions_.end());

  downSession->upSession_ = this;
  downSessions_[downSession->sessionId_] = downSession;
}

UpStratumClient::~UpStratumClient() {
  // delete all downSessions
  for (auto downSessionIter : downSessions_) {
    delete downSessionIter.second;
  }

  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
}

size_t UpStratumClient::downSessionCount() {
  return downSessions_.size();
}

bool UpStratumClient::connect(struct sockaddr_in &sin) {
  // bufferevent_socket_connect(): This function returns 0 if the connect
  // was successfully launched, and -1 if an error occurred.
  int res = bufferevent_socket_connect(bev_, (struct sockaddr *)&sin, sizeof(sin));
  if (res == 0) {
    state_ = UP_CONNECTED;
    return true;
  }
  return false;
}

void UpStratumClient::recvData(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(inBuf_, buf);
  // read until no more messages
  while (handleMessage());
}

bool UpStratumClient::handleMessage() {
  const size_t evBufLen = evbuffer_get_length(inBuf_);

  // no matter what kind of messages, length should at least 4 bytes
  if (evBufLen < 4)
    return false;

  uint8_t buf[4];
  evbuffer_copyout(inBuf_, buf, 4);

  // handle ex-message
  if (buf[0] == CMD_MAGIC_NUMBER) {
    const uint16_t exMessageLen = *(uint16_t *)(buf + 2);

    //
    // It is not a valid message if exMessageLen < 4, because the length of
    // message header (1 byte magic_number + 1 byte type/cmd + 2 bytes length)
    // is 4. The header length is included in exMessageLen.
    //
    // Without the checking at below, send "\x0f\xff\x00\x00" to the agent,
    // and it will fall into infinite loop with handleMessage() calling.
    //
    if (exMessageLen < 4) {
      LOG(ERROR) << "received invalid ex-message, type: " << std::hex << (int)buf[1]
        << ", len: " << exMessageLen;
      return false;
    }

    if (evBufLen < exMessageLen)  // didn't received the whole message yet
      return false;

    // copies and removes the first datlen bytes from the front of buf
    // into the memory at data
    string exMessage;
    exMessage.resize(exMessageLen);
    evbuffer_remove(inBuf_, (uint8_t *)exMessage.data(), exMessage.size());

    switch (buf[1]) {
      case CMD_MINING_SET_DIFF:
        handleExMessage_MiningSetDiff(&exMessage);
        break;

      default:
        LOG(ERROR) << "received unknown ex-message, type: " << buf[1]
        << ", len: " << exMessageLen << std::endl;
        break;
    }
    return true;  // read message success, return true
  }

  // stratum message
  string line;
  if (tryReadLine(line, inBuf_)) {
    DLOG(INFO) << "Try to read line : " << line << std::endl;
    handleStratumMessage(line);
    return true;
  }

  return false;  // read mesasge failure
}

void UpStratumClient::handleExMessage_MiningSetDiff(const string *exMessage) {
  //
  // CMD_MINING_SET_DIFF
  // | magic_number(1) | cmd(1) | len (2) | diff_2_exp(1) | count(2) | session_id (2) ... |
  //
  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint8_t diff_2exp = *(p + 4);
  const uint64_t diff = (uint64_t)exp2(diff_2exp);

  const uint16_t count   = *(uint16_t *)(p + 5);
  uint16_t *sessionIdPtr =  (uint16_t *)(p + 7);

  for (size_t i = 0; i < count; i++) {
    uint16_t sessionId = *sessionIdPtr++;
    auto sessionIter = downSessions_.find(sessionId);
    if (sessionIter == downSessions_.end()) {
      LOG(WARNING) << "MiningSetDiff: session not found. userName: " << userName_ << ", sessionId: " << sessionId << std::endl;
      continue;
    }
    sendMiningDifficulty(sessionIter->second, diff);
  }

  LOG(INFO) << "up[" << userName_ << "] CMD_MINING_SET_DIFF, diff: "
  << diff << ", sessions count: " << count << std::endl;
}

void UpStratumClient::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
//  DLOG(INFO) << "UpStratumClient send(" << len << "): " << data << std::endl;
}

void UpStratumClient::sendMiningDifficulty(StratumSession *downSession, uint64_t diff) {
  const string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                                   ",\"params\":[%" PRIu64"]}\n", diff);
  downSession->sendData(s);
}

void UpStratumClient::sendMiningNotify(StratumSession *downSession) {
  downSession->sendData(latestMiningNotifyStr_);
}

void UpStratumClient::sendLineToAllDownSessions(const string &line) {
  for (auto downSessionIter : downSessions_) {
    downSessionIter.second->sendData(line);
  }
}

void UpStratumClient::sendMiningNotifyToAll() {
  for (auto downSessionIter : downSessions_) {
    downSessionIter.second->sendData(latestMiningNotifyStr_);
  }
}

void UpStratumClient::sendDefaultMiningDifficulty(StratumSession *downSession) {
  DLOG(INFO) << "Start to send sendDefaultMiningDifficulty " << std::endl;
  const string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                                   ",\"params\":[%" PRIu32"]}\n",
                                   poolDefaultDiff_);

  downSession->sendData(s);
}

void UpStratumClient::convertMiningNotifyStr(const string &line) {
  const char *pch = splitNotify(line);
  latestMiningNotifyStr_.clear();

  latestMiningNotifyStr_.append(line.c_str(), pch - line.c_str());
  latestMiningNotifyStr_.append(Strings::Format("%08x", extraNonce1_));
  latestMiningNotifyStr_.append(pch);
}

void UpStratumClient::registerWorker(StratumSession *downSession) {
  const string &minerAgent = downSession->minerAgent_;
  const string &workerName = downSession->workerName_;

  //
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  //
  uint16_t len = 0;
  len += (1 + 1 + 2 + 2); // magic_num, cmd, len, session_id
  // client agent
  len += minerAgent.length() + 1; // miner agent and '\0'
  len += workerName.length() + 1;  // worker name and '\0'

  string buf;
  buf.resize(len, 0);
  uint8_t *p = (uint8_t *)buf.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_REGISTER_WORKER;

  // len
  *(uint16_t *)p = len;
  p += 2;

  // session Id
  *(uint16_t *)p = downSession->sessionId_;
  p += 2;

  // miner agent
  // strcpy: including the terminating null byte
  strcpy((char *)p, minerAgent.c_str());
  p += minerAgent.size() + 1;

  // worker name
  strcpy((char *)p, workerName.c_str());
  p += workerName.length() + 1;
  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());
  DLOG(INFO) << "registerWorker: " << userName_  << "." << workerName << ", " << minerAgent << ", " << downSession->sessionId_;

  sendData(buf);
}

void UpStratumClient::unRegisterWorker(StratumSession *downSession) {
  //
  // CMD_UNREGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) |
  //
  const uint16_t len = 6;
  string buf;
  buf.resize(len, 0);
  uint8_t *p = (uint8_t *)buf.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_UNREGISTER_WORKER;

  // len
  *(uint16_t *)p = len;
  p += 2;

  // session Id
  *(uint16_t *)p = downSession->sessionId_;
  p += 2;
  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  sendData(buf);
}

void UpStratumClient::handleStratumMessage(const string &line) {
  DLOG(INFO) << "UpStratumClient recv(" << line.size() << "): " << line << std::endl;

  StratumMessage smsg(line);
  if (!smsg.isValid()) {
    LOG(ERROR) << "decode line fail, not a json string" << std::endl;
    return;
  }

  const string method = smsg.getMethod();
  StratumJob sjob;
  uint32_t difficulty = 0u;

  if (state_ == UP_AUTHENTICATED) {
    if (smsg.parseMiningNotify(sjob)) {
      //
      // mining.notify
      //
      convertMiningNotifyStr(line);  // convert mining.notify string
      sendMiningNotifyToAll();       // send stratum job to all miners
     
      latestJobId_[0]      = latestJobId_[1];
      latestJobGbtTime_[0] = latestJobGbtTime_[1];
      latestJobId_[1]      = latestJobId_[2];
      latestJobGbtTime_[1] = latestJobGbtTime_[2];

      // the jobId always between [0, 9]
      latestJobId_[2]      = (uint8_t)sjob.jobId_;
      latestJobGbtTime_[2] = sjob.time_;

      // set last job received time
      lastJobReceivedTime_ = (uint32_t)time(NULL);

      DLOG(INFO) << "up[" << userName_ << "] stratum job"
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
      if (poolDefaultDiff_ == 0) {
        poolDefaultDiff_ = difficulty;
        
        // send to miner
        for (auto downSessionItr : downSessions_) {
          StratumSession *downSession = downSessionItr.second;
          sendDefaultMiningDifficulty(downSession);
        }
      }
    }

    return;
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

    // do mining.authorize
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"%s\", \"\"]}\n",
                               userName_.c_str());
    DLOG(INFO) << "From pool authorize information " << s << std::endl;
    sendData(s);
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

      for (auto downSessionItr : downSessions_) {
        StratumSession *downSession = downSessionItr.second;
        downSession->responseAuthorizedSuccess();
        registerWorker(downSession);
      }
    }
    else {
      LOG(INFO) << "auth failed, name: \"" << userName_
      << "\", response: " << line << std::endl;

      // Reply all downSessions established before upSession is authenticated
      sendLineToAllDownSessions(line);
    }
    
    return;
  }
}

void UpStratumClient::addDownSession(StratumSession *downSession) {
  // session with the same sessionID should not exist
  assert(downSessions_.find(downSession->sessionId_) == downSessions_.end());

  downSession->upSession_ = this;
  downSessions_[downSession->sessionId_] = downSession;

  downSession->responseAuthorizedSuccess();
  registerWorker(downSession);

  // send mining.set_difficulty
  sendDefaultMiningDifficulty(downSession);
  // send latest stratum job
  sendMiningNotify(downSession);
}

void UpStratumClient::removeDownSession(StratumSession *downSession) {
  auto downSessionIter = downSessions_.find(downSession->sessionId_);
  if (downSessionIter == downSessions_.end()) {
    return;
  }
  downSessions_.erase(downSessionIter);
  unRegisterWorker(downSession);
  delete downSession;
}

bool UpStratumClient::isAvailable() {
  const uint32_t kJobExpiredTime = 60 * 5;  // seconds

  if (state_ == UP_AUTHENTICATED &&
      latestMiningNotifyStr_.empty() == false &&
      poolDefaultDiff_ != 0 &&
      lastJobReceivedTime_ + kJobExpiredTime > (uint32_t)time(NULL)) {
    return true;
  }
  return false;
}

void UpStratumClient::submitShare(const Share &share,
                                  StratumSession *downSession) {

  bool isTimeChanged = true;
  if ((share.jobId_ == latestJobId_[2] && share.time_ == latestJobGbtTime_[2]) ||
      (share.jobId_ == latestJobId_[1] && share.time_ == latestJobGbtTime_[1]) ||
      (share.jobId_ == latestJobId_[0] && share.time_ == latestJobGbtTime_[0])) {
    isTimeChanged = false;
  }

  //
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) |]
  //
  string buf;
  const uint16_t len = isTimeChanged ? 19 : 15;  // fixed 19 or 15 bytes
  buf.resize(len, 0);
  uint8_t *p = (uint8_t *)buf.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = isTimeChanged ? CMD_SUBMIT_SHARE_WITH_TIME : CMD_SUBMIT_SHARE;

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
  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  // send buf
  sendData(buf);
}

////////////////////////////////// StratumSession //////////////////////////////
StratumSession::StratumSession(const uint16_t sessionId,
                               struct bufferevent *bev, StratumServer *server,
                               struct in_addr saddr)
: state_(DOWN_CONNECTED), bev_(bev), server_(server),
  sessionId_(sessionId), upSession_(nullptr), saddr_(saddr)
{
  inBuf_ = evbuffer_new();
  assert(inBuf_ != nullptr);
}

StratumSession::~StratumSession() {
  server_->sessionIDManager_.freeSessionId(sessionId_);
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
}

void StratumSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "send(" << len << "): " << data << std::endl;
}

void StratumSession::recvData(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(inBuf_, buf);

  string line;
  while (tryReadLine(line, inBuf_)) {
    handleStratumMessage(line);
  }
}

void StratumSession::handleStratumMessage(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line << std::endl;

  StratumMessage smsg(line);
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
  responseError(idStr, StratumError::ILLEGAL_PARARMS);
}

void StratumSession::responseError(const string &idStr, int errCode) {
  //
  // {"id": 10, "result": null, "error":[21, "Job not found", null]}
  //
  char buf[128];
  int len = snprintf(buf, sizeof(buf),
                     "{\"id\":%s,\"result\":null,\"error\":[%d,\"%s\",null]}\n",
                     idStr.empty() ? "null" : idStr.c_str(),
                     errCode, StratumError::toString(errCode));
  sendData(buf, len);
}

void StratumSession::responseTrue(const string &idStr) {
  const string s = "{\"id\":" + idStr + ",\"result\":true,\"error\":null}\n";
  sendData(s);
}

void StratumSession::responseAuthorizedSuccess() {
  responseTrue(authorizeMsgId_);
  authorizeMsgId_.clear();
}

void StratumSession::handleRequest(const string &idStr,
                                   const StratumMessage &smsg) {
  DLOG(INFO) << "Start handle the DownSession";
  const string method = smsg.getMethod();
  if (method == "mining.submit") {  // most of requests are 'mining.submit'
    handleRequest_Submit(idStr, smsg);
  }
  else if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, smsg);
  }
  else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, smsg);
  } else {
    // unrecognised method, just ignore it
    LOG(WARNING) << "unrecognised method: \"" << method << "\"" << std::endl;
  }
}

void StratumSession::handleRequest_Subscribe(const string &idStr,
                                             const StratumMessage &smsg) {
  if (state_ != DOWN_CONNECTED) {
    responseError(idStr, StratumError::UNKNOWN);
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

  if (!smsg.parseMiningSubscribe(minerAgent_)) {
    minerAgent_ = "unknown";
  }
  DLOG(INFO) << "Subcribe Process, minerAgent is " << minerAgent_;

  // 30 is max length for miner agent
  if (minerAgent_.size() > 30) {
    minerAgent_.resize(30);
  }

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

void StratumSession::handleRequest_Authorize(const string &idStr,
                                             const StratumMessage &smsg) {
  if (state_ != DOWN_SUBSCRIBED) {
    responseError(idStr, StratumError::NOT_SUBSCRIBED);
    return;
  }

  //
  //  params[0] = user[.worker]
  //  params[1] = password
  //  eg. {"params": ["slush.miner1", "password"], "id": 2, "method": "mining.authorize"}
  //

  string workerName, fullWorkerName;
  if (!smsg.parseMiningAuthorize(fullWorkerName)) {
    responseError(idStr, StratumError::INVALID_USERNAME);
    return;
  }

  splitWorkerName(fullWorkerName, userName_, workerName_);  // split by '.'

  // user cannot be empty otherwise it cannot minning
  if (userName_.empty()) {
    responseError(idStr, StratumError::INVALID_USERNAME);
    return;
  }

  if (workerName_.empty()) {
    workerName_ = DEFAULT_WORKER_NAME;
  }

  DLOG(INFO) << "Authorize success, user: " << userName_ << ", worker: " << workerName_ << std::endl;

  authorizeMsgId_ = idStr;
  server_->addDownSession(this);

  // auth finished
  // the result of authenticated will response by upSession
  state_ = DOWN_AUTHENTICATED;
}

void StratumSession::handleRequest_Submit(const string &idStr,
                                          const StratumMessage &smsg) {
  if (state_ != DOWN_AUTHENTICATED || upSession_ == nullptr) {
    responseError(idStr, StratumError::UNAUTHORIZED);
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
  Share share;
  if (!smsg.parseMiningSubmit(share)) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }

  // submit share
  upSession_->submitShare(share, this);

  responseTrue(idStr);  // we assume shares are valid
}

/////////////////////////////////// StratumServer //////////////////////////////
StratumServer::StratumServer(const string &listenIP, const uint16_t listenPort)
:running_ (true), listenIP_(listenIP), listenPort_(listenPort), base_(nullptr)
{
  upEvTimer_ = nullptr;
}

StratumServer::~StratumServer() {
  if (signal_event_)
    event_free(signal_event_);

  if (upEvTimer_)
    event_del(upEvTimer_);

  if (listener_)
    evconnlistener_free(listener_);

  if (base_)
    event_base_free(base_);
}

bool StratumServer::setup() {

#ifdef _WIN32
  // create event base for win32
  WSADATA wsa_data;
  if (WSAStartup(0x202, &wsa_data) == SOCKET_ERROR) {
      LOG(ERROR) << "WSAStartup failed: " << WSAGetLastError() << std::endl;
      return false;
  }

  #ifdef USE_IOCP
    // use IOCP on Windows
    evthread_use_windows_threads();
    struct event_config *cfg = event_config_new();
    event_config_set_flag(cfg, EVENT_BASE_FLAG_STARTUP_IOCP);
    base_ = event_base_new_with_config(cfg);
  #else
    // use select() by default
    base_ = event_base_new();
  #endif
  // end of creating event base for win32
#else
  // create event base for unix-like system
  base_ = event_base_new();
#endif

  if(!base_) {
    LOG(ERROR) << "server: cannot create event base" << std::endl;
    return false;
  }

  // setup up sessions watcher
  upEvTimer_ = event_new(base_, -1, EV_PERSIST,
                         StratumServer::upWatcherCallback, this);
  // every 15 seconds to check if up session's available
  struct timeval tenSec = {15, 0};
  event_add(upEvTimer_, &tenSec);

  // set up ev listener
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port   = htons(listenPort_);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  if (evutil_inet_pton(AF_INET, listenIP_.c_str(), &sin.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << listenIP_ << std::endl;
    return false;
  }
  DLOG(INFO) << "load the listen address success";

  listener_ = evconnlistener_new_bind(base_,
                                      StratumServer::listenerCallback,
                                      (void*)this,
                                      LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                      // backlog, Set to -1 for a reasonable default
                                      -1,
                                      (struct sockaddr*)&sin, sizeof(sin));
  if(!listener_) {
    LOG(ERROR) << "cannot create listener: " << listenIP_ << ":" << listenPort_ << std::endl;
    return false;
  }

  LOG(INFO) << "listen on " << listenIP_ << ":" << listenPort_ << std::endl;
  return true;
}

void StratumServer::run() {
  assert(base_ != nullptr);
  event_base_dispatch(base_);
}

void StratumServer::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop tcp server event loop" << std::endl;
  running_ = false;
  event_base_loopexit(base_, nullptr);
}

void StratumServer::addUpPool(const string &host, const uint16_t port) {
  upPoolHost_.push_back(host);
  upPoolPort_.push_back(port);

  LOG(INFO) << "add pool: " << host << ":" << port << std::endl;
}

void StratumServer::listenerCallback(struct evconnlistener *listener,
                                     evutil_socket_t fd,
                                     struct sockaddr *saddr,
                                     int socklen, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  struct event_base  *base = (struct event_base*)server->base_;
  struct bufferevent *bev;

  uint16_t sessionId = 0u;
  if (!server->sessionIDManager_.allocSessionId(&sessionId)) {
    LOG(WARNING) << "session id is full" << std::endl;
    closeSocket(fd);
    return;
  }

  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if(bev == nullptr) {
    LOG(ERROR) << "bufferevent_socket_new fail" << std::endl;
    server->stop();
    return;
  }

  StratumSession *conn = new StratumSession(sessionId, bev, server,
                                ((struct sockaddr_in *)saddr)->sin_addr);
  bufferevent_setcb(bev,
                    StratumServer::downReadCallback, nullptr,
                    StratumServer::downEventCallback, (void*)conn);

  // By default, a newly created bufferevent has writing enabled.
  bufferevent_enable(bev, EV_READ|EV_WRITE);
  
  // get source IP address
  char saddrBuffer[INET_ADDRSTRLEN];
  evutil_inet_ntop(AF_INET, &conn->saddr_, saddrBuffer, INET_ADDRSTRLEN);
  
  LOG(INFO) << "miner connected, sessionId: " << conn->sessionId_ << ", IP: " << saddrBuffer << std::endl;
}

void StratumServer::downReadCallback(struct bufferevent *bev, void *ptr) {
  static_cast<StratumSession *>(ptr)->recvData(bufferevent_get_input(bev));
}

void StratumServer::downEventCallback(struct bufferevent *bev, short events, void *ptr) {
  StratumSession *conn  = static_cast<StratumSession *>(ptr);
  StratumServer *server = conn->server_;

  // should not be 'BEV_EVENT_CONNECTED'
  assert((events & BEV_EVENT_CONNECTED) != BEV_EVENT_CONNECTED);
  
  // get source IP address
  char saddrBuffer[INET_ADDRSTRLEN];
  evutil_inet_ntop(AF_INET, &conn->saddr_, saddrBuffer, INET_ADDRSTRLEN);

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "miner disconnected, sessionId: " << conn->sessionId_ << ", IP: " << saddrBuffer << std::endl;
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(INFO) << "got an error from the miner, sessionId: " << conn->sessionId_ << ", IP: " << saddrBuffer 
    << ", err: " << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()) << std::endl;
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "read/write from the miner timeout, events: " << events
    << ", sessionId:" << conn->sessionId_ << ", IP: " << saddrBuffer << std::endl;
  }
  else {
    LOG(ERROR) << "unhandled event from the miner: " << events
    << ", sessionId:" << conn->sessionId_ << ", IP: " << saddrBuffer << std::endl;
  }

  server->removeDownSession(conn);
}

void StratumServer::addDownSession(StratumSession *downSession) {
  auto upSetIter = upSessionSets_.find(downSession->userName_);
  
  // cannot find a upSession
  if (upSetIter == upSessionSets_.end() || upSetIter->second.empty()) {
    createUpSession(downSession);
    return;
  }

  // the min upSession number is not reached
  if (upSetIter->second.size() < minUpConnPerUser_) {
    createUpSession(downSession);
    return;
  }

  // find a upSession that has fewest downSessions
  UpStratumClient *minConnUpSession = nullptr;
  for (auto upSessionIter : upSetIter->second) {
    if (minConnUpSession == nullptr) {
      minConnUpSession = upSessionIter;
    }
    else if (upSessionIter->downSessionCount() < minConnUpSession->downSessionCount()) {
      minConnUpSession = upSessionIter;
    }
  }

  // not found or too high connections
  if (minConnUpSession == nullptr || minConnUpSession->downSessionCount() >= maxDownConnPerUpConn_) {
    createUpSession(downSession);
    return;
  }

  // add to the upSession
  minConnUpSession->addDownSession(downSession);
}

void StratumServer::removeDownSession(StratumSession *downSession) {
  UpStratumClient *upSession = downSession->upSession_;

  // there is no upSession bound to the downSession
  // delete it directly
  if (upSession == nullptr) {
    delete downSession;
    return;
  }

  // otherwise, delete from upSession
  upSession->removeDownSession(downSession);

  // clear idle upSession
  if (upSession->downSessionCount() == 0) {
    removeUpSession(upSession);
  }
}

void StratumServer::removeUpSession(UpStratumClient *upSession) {
  auto upSetIter = upSessionSets_.find(upSession->userName_);
  if (upSetIter == upSessionSets_.end()) {
    return;
  }

  auto upSessionIter = upSetIter->second.find(upSession);
  if (upSessionIter == upSetIter->second.end()) {
    return;
  }

  // remove upSession
  upSetIter->second.erase(upSessionIter);

  // remove empty upSessionSet
  if (upSetIter->second.empty()) {
    upSessionSets_.erase(upSetIter);
  }

  delete upSession;
}

void StratumServer::createUpSession(StratumSession *downSession) {
  UpStratumClient *up = new UpStratumClient(downSession, base_, this);

  for (size_t i = 0; i < upPoolHost_.size(); i++) {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(upPoolPort_[i]);
    if (!resolve(upPoolHost_[i], &sin.sin_addr)) {
      continue;
    }
    if (!up->connect(sin)) {
      continue;
    }
  }

  if (up->state_ != UP_CONNECTED) {
    delete up; // delete upSession and the downSession it contained
    return;
  }

  upSessionSets_[downSession->userName_].insert(up);
}

void StratumServer::upEventCallback(struct bufferevent *bev,
                                    short events, void *ptr) {
  UpStratumClient *up = static_cast<UpStratumClient *>(ptr);
  StratumServer *server = up->server_;

  if (events & BEV_EVENT_CONNECTED) {
    up->state_ = UP_CONNECTED;

    // do subscribe
    string s = Strings::Format("{\"id\":1,\"method\":\"mining.subscribe\""
                               ",\"params\":[\"%s\"]}\n", BTCCOM_MINER_AGENT);
    up->sendData(s);
    DLOG(INFO) << "subscribe to the pooler ";
    return;
  }

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "pool server closed the connection" << std::endl;
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(INFO) << "got an error from the pool server: "
    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()) << std::endl;
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "read/write from pool server timeout, events: " << events << std::endl;
  }
  else {
    LOG(ERROR) << "unhandled events from pool server: " << events << std::endl;
  }

  server->removeUpSession(up);
}

void StratumServer::upReadCallback(struct bufferevent *bev, void *ptr) {
  static_cast<UpStratumClient *>(ptr)->recvData(bufferevent_get_input(bev));
}

void StratumServer::upWatcherCallback(evutil_socket_t fd,
                                      short events, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  server->checkUpSessions();
}

void StratumServer::checkUpSessions() {
  DLOG(INFO) << "start checkUpSessions..." << std::endl;

  auto upSetIter = upSessionSets_.begin();
  while (upSetIter != upSessionSets_.end()) {
    auto upSet = upSetIter->second;

    auto upSessionIter = upSet.begin();
    while (upSessionIter != upSet.end()) {
      UpStratumClient *upSession = *upSessionIter;

      if (!upSession->isAvailable()) {
        LOG(WARNING) << "remove dead up session, userName: " << upSession->userName_
                     << ", downSessionCount: " << upSession->downSessionCount() << std::endl;
        upSessionIter = upSet.erase(upSessionIter);
        delete upSession;
        continue;
      }

      upSessionIter++;
    }

    if (upSet.size() == 0) {
      upSetIter = upSessionSets_.erase(upSetIter);
      continue;
    }

    upSetIter++;
  }

}
