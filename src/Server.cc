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

static
bool resolve(const string &host, struct	in_addr *sin_addr) {
  struct evutil_addrinfo *ai = NULL;
  struct evutil_addrinfo hints_in;
  memset(&hints_in, 0, sizeof(evutil_addrinfo));
  // AF_INET, v4; AF_INT6, v6; AF_UNSPEC, both v4 & v6
  hints_in.ai_family   = AF_UNSPEC;
  hints_in.ai_socktype = SOCK_STREAM;
  hints_in.ai_protocol = IPPROTO_TCP;
  hints_in.ai_flags    = EVUTIL_AI_ADDRCONFIG;

  // TODO: use non-blocking to resolve hostname
  int err = evutil_getaddrinfo(host.c_str(), NULL, &hints_in, &ai);
  if (err != 0) {
    LOG(ERROR) << "evutil_getaddrinfo err: " << err << ", " << evutil_gai_strerror(err);
    return false;
  }
  if (ai == NULL) {
    LOG(ERROR) << "evutil_getaddrinfo res is null";
    return false;
  }

  // only get the first record, ignore ai = ai->ai_next
  if (ai->ai_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in*)ai->ai_addr;
    *sin_addr = sin->sin_addr;

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sin->sin_addr), ipStr, INET_ADDRSTRLEN);
    LOG(INFO) << "resolve host: " << host << ", ip: " << ipStr;
  } else if (ai->ai_family == AF_INET6) {
    // not support yet
    LOG(ERROR) << "not support ipv6 yet";
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
  loc = evbuffer_search_eol(inBuf, NULL, NULL, EVBUFFER_EOL_LF);
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
string getWorkerName(const string &fullName) {
  size_t pos = fullName.find(".");
  if (pos == fullName.npos) {
    return "";
  }
  return fullName.substr(pos + 1);  // not include '.'
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
      share_.jobId_       = (uint8_t) strtoul(getJsonStr(&t_[i+1]).c_str(), NULL, 10);
      // ExtraNonce2(hex)
      share_.extraNonce2_ = (uint32_t)strtoul(getJsonStr(&t_[i+2]).c_str(), NULL, 16);
      // nTime(hex)
      share_.time_        = (uint32_t)strtoul(getJsonStr(&t_[i+3]).c_str(), NULL, 16);
      // nonce(hex)
      share_.nonce_       = (uint32_t)strtoul(getJsonStr(&t_[i+4]).c_str(), NULL, 16);

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

void StratumMessage::_parseMiningSetDifficulty() {
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
    LOG(ERROR) << "failed to parse JSON: " << r_;
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


///////////////////////////////// UpStratumClient //////////////////////////////
UpStratumClient::UpStratumClient(const int8_t idx, struct event_base *base,
                                 const string &userName, StratumServer *server)
: state_(UP_INIT), idx_(idx), server_(server), poolDefaultDiff_(0)
{
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  assert(bev_ != NULL);

  inBuf_ = evbuffer_new();
  assert(inBuf_ != NULL);

  bufferevent_setcb(bev_,
                    StratumServer::upReadCallback, NULL,
                    StratumServer::upEventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);

  extraNonce1_ = 0u;
  extraNonce2_ = 0u;
  userName_ = userName;

  latestJobId_[0] = latestJobId_[1] = latestJobId_[2] = 0;
  latestJobGbtTime_[0] = latestJobGbtTime_[1] = latestJobGbtTime_[2] = 0;

  lastJobReceivedTime_ = 0u;

  DLOG(INFO) << "idx_: " << (int32_t)idx_;
}

UpStratumClient::~UpStratumClient() {
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
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

  while (handleMessage()) {
  }
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
        << ", len: " << exMessageLen;
        break;
    }
    return true;  // read message success, return true
  }

  // stratum message
  string line;
  if (tryReadLine(line, inBuf_)) {
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
    server_->sendMiningDifficulty(this, sessionId, diff);
  }

  LOG(INFO) << "up[" << (int32_t)idx_ << "] CMD_MINING_SET_DIFF, diff: "
  << diff << ", sessions count: " << count;
}

void UpStratumClient::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
//  DLOG(INFO) << "UpStratumClient send(" << len << "): " << data;
}

void UpStratumClient::sendMiningNotify(const string &line) {
  // send to all down sessions
  server_->sendMiningNotifyToAll(idx_, latestMiningNotifyStr_);
}

void UpStratumClient::convertMiningNotifyStr(const string &line) {
  const char *pch = splitNotify(line);
  latestMiningNotifyStr_.clear();

  latestMiningNotifyStr_.append(line.c_str(), pch - line.c_str());
  latestMiningNotifyStr_.append(Strings::Format("%08x", extraNonce1_));
  latestMiningNotifyStr_.append(pch);
}

void UpStratumClient::handleStratumMessage(const string &line) {
  DLOG(INFO) << "UpStratumClient recv(" << line.size() << "): " << line;

  StratumMessage smsg(line);
  if (!smsg.isValid()) {
    LOG(ERROR) << "decode line fail, not a json string";
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
      sendMiningNotify(line);        // send stratum job to all miners

      latestJobId_[1]      = latestJobId_[2];
      latestJobGbtTime_[1] = latestJobGbtTime_[2];
      latestJobId_[0]      = latestJobId_[1];
      latestJobGbtTime_[0] = latestJobGbtTime_[1];

      // the jobId always between [0, 9]
      latestJobId_[2]      = (uint8_t)sjob.jobId_;
      latestJobGbtTime_[2] = sjob.time_;

      // set last job received time
      lastJobReceivedTime_ = (uint32_t)time(NULL);

      DLOG(INFO) << "up[" << (int32_t)idx_ << "] stratum job"
      << ", jobId: "    << sjob.jobId_
      << ", prevhash: " << sjob.prevHash_
      << ", version: "  << sjob.version_
      << ", clean: "    << (sjob.isClean_ ? "true" : "false");
    }
    else if (smsg.parseMiningSetDifficulty(&difficulty)) {
      //
      // mining.set_difficulty
      //
      // just set the default pool diff, than ignore
      if (poolDefaultDiff_ == 0) {
        poolDefaultDiff_ = difficulty;
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
      LOG(FATAL) << "get extra nonce1 and extra nonce2 failure";
      return;
    }
    extraNonce1_ = nonce1;
    DLOG(INFO) << "extraNonce1 / SessionID: " << extraNonce1_;

    // check extra nonce2's size, MUST be 8 bytes
    if (n2size != 8) {
      LOG(FATAL) << "extra nonce2's size is NOT 8 bytes";
      return;
    }

    // subscribe successful
    state_ = UP_SUBSCRIBED;

    // do mining.authorize
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"%s\", \"\"]}\n",
                               userName_.c_str());
    sendData(s);
    return;
  }

  if (state_ == UP_SUBSCRIBED && smsg.getResultBoolean() == true) {
    //
    // check authenticated result
    // {"error": null, "id": 2, "result": true}
    //
    state_ = UP_AUTHENTICATED;  // authorize successful
    LOG(INFO) << "auth success, name: \"" << userName_
    << "\", extraNonce1: " << extraNonce1_;
    return;
  }
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


////////////////////////////////// StratumSession //////////////////////////////
StratumSession::StratumSession(const int8_t upSessionIdx,
                               const uint16_t sessionId,
                               struct bufferevent *bev, StratumServer *server)
: state_(DOWN_CONNECTED), minerAgent_(NULL), upSessionIdx_(upSessionIdx),
sessionId_(sessionId), bev_(bev), server_(server)
{
  inBuf_ = evbuffer_new();
  assert(inBuf_ != NULL);
}

StratumSession::~StratumSession() {
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
}

void StratumSession::setReadTimeout(const int32_t timeout) {
  // clear it
  bufferevent_set_timeouts(bev_, NULL, NULL);

  // set a new one
  struct timeval readtv  = {timeout, 0};
  struct timeval writetv = {120, 0};
  bufferevent_set_timeouts(bev_, &readtv, &writetv);
}

void StratumSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "send(" << len << "): " << data;
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
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  StratumMessage smsg(line);
  if (!smsg.isValid()) {
    LOG(ERROR) << "decode line fail, not a json string";
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

void StratumSession::handleRequest(const string &idStr,
                                   const StratumMessage &smsg) {
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
    LOG(WARNING) << "unrecognised method: \"" << method << "\"";
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

  string minerAgent;
  if (!smsg.parseMiningSubscribe(minerAgent)) {
    minerAgent = "unknown";
  }

  // 30 is max length for miner agent
  minerAgent_ = strdup(minerAgent.substr(0, 30).c_str());

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

  workerName = getWorkerName(fullWorkerName);  // split by '.'
  if (workerName.empty())
    workerName = DEFAULT_WORKER_NAME;

  // auth success
  responseTrue(idStr);
  state_ = DOWN_AUTHENTICATED;

  // sent sessionId, minerAgent_, workerName to server_
  server_->registerWorker(this, minerAgent_, workerName);

  // minerAgent_ will not use anymore
  if (minerAgent_) {
    free(minerAgent_);
    minerAgent_ = NULL;
  }

  // send mining.set_difficulty
  server_->sendDefaultMiningDifficulty(this);

  // send latest stratum job
  server_->sendMiningNotify(this);
}

void StratumSession::handleRequest_Submit(const string &idStr,
                                          const StratumMessage &smsg) {
  if (state_ != DOWN_AUTHENTICATED) {
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
  server_->submitShare(share, this);

  responseTrue(idStr);  // we assume shares are valid
}



/////////////////////////////////// StratumServer //////////////////////////////
StratumServer::StratumServer(const string &listenIP, const uint16_t listenPort)
:running_ (true), listenIP_(listenIP), listenPort_(listenPort), base_(NULL)
{
  upSessions_    .resize(kUpSessionCount_, NULL);
  upSessionCount_.resize(kUpSessionCount_, 0);

  upEvTimer_ = NULL;
  downSessions_.resize(AGENT_MAX_SESSION_ID + 1, NULL);
}

StratumServer::~StratumServer() {
  // remove upsessions
  for (size_t i = 0; i < upSessions_.size(); i++) {
    UpStratumClient *upsession = upSessions_[i];  // alias
    if (upsession == NULL)
      continue;

    // remove this upsession and down sessions which belong to this
    // up connection will be remove too.
    removeUpConnection(upsession);
  }

  if (signal_event_)
    event_free(signal_event_);

  if (upEvTimer_)
    event_del(upEvTimer_);

  if (listener_)
    evconnlistener_free(listener_);

  if (base_)
  	event_base_free(base_);
}

void StratumServer::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop tcp server event loop";
  running_ = false;
  event_base_loopexit(base_, NULL);
}

void StratumServer::addUpPool(const string &host, const uint16_t port,
                              const string &upPoolUserName) {
  upPoolHost_    .push_back(host);
  upPoolPort_    .push_back(port);
  upPoolUserName_.push_back(upPoolUserName);

  LOG(INFO) << "add pool: " << host << ":" << port << ", username: " << upPoolUserName;
}

UpStratumClient *StratumServer::createUpSession(const int8_t idx) {
  for (size_t i = 0; i < upPoolHost_.size(); i++) {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(upPoolPort_[i]);
    if (!resolve(upPoolHost_[i], &sin.sin_addr)) {
      continue;
    }

    UpStratumClient *up = new UpStratumClient(idx, base_, upPoolUserName_[i], this);
    if (!up->connect(sin)) {
      delete up;
      continue;
    }
    LOG(INFO) << "success connect[" << (int32_t)up->idx_ << "]: " << upPoolHost_[i] << ":"
    << upPoolPort_[i] << ", username: " << upPoolUserName_[i];

    return up;  // connect success
  }

  return NULL;
}

bool StratumServer::setup() {
  if (upPoolHost_.size() == 0)
    return false;

  base_ = event_base_new();
  if(!base_) {
    LOG(ERROR) << "server: cannot create event base";
    return false;
  }

  // create up sessions
  for (int8_t i = 0; i < kUpSessionCount_; i++) {
    UpStratumClient *up = createUpSession(i);
    if (up == NULL)
      return false;

    assert(up->idx_ == i);
    addUpConnection(up);
  }

  // wait util all up session available
  {
    struct event *checkTimer;
    checkTimer = event_new(base_, -1, EV_PERSIST,
                           StratumServer::upSesssionCheckCallback, this);
    struct timeval oneSec = {1, 0};
    event_add(checkTimer, &oneSec);

    // run event dispatch, it will break util all up sessions are available
    event_base_dispatch(base_);

    // get here means: all up sessions are available
    event_del(checkTimer);
    event_free(checkTimer);
  }

  // if one of upsessions init failure, it'll stop the server.
  if (!running_) {
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
  if (inet_pton(AF_INET, listenIP_.c_str(), &sin.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << listenIP_;
    return false;
  }

  listener_ = evconnlistener_new_bind(base_,
                                      StratumServer::listenerCallback,
                                      (void*)this,
                                      LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                      // backlog, Set to -1 for a reasonable default
                                      -1,
                                      (struct sockaddr*)&sin, sizeof(sin));
  if(!listener_) {
    LOG(ERROR) << "cannot create listener: " << listenIP_ << ":" << listenPort_;
    return false;
  }
  return true;
}

void StratumServer::upSesssionCheckCallback(evutil_socket_t fd,
                                            short events, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  server->waitUtilAllUpSessionsAvailable();
}

void StratumServer::waitUtilAllUpSessionsAvailable() {
  for (int8_t i = 0; i < kUpSessionCount_; i++) {

    // lost upsession when init, we should stop server
    if (upSessions_[i] == NULL) {
      stop();
      return;
    }

    if (upSessions_[i]->isAvailable() == false) {
      return;  // someone is not ready yet
    }
  }

  // if we get here, means all up session is available, break event loop
  event_base_loopbreak(base_);
}

void StratumServer::upWatcherCallback(evutil_socket_t fd,
                                      short events, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  server->checkUpSessions();
}

void StratumServer::checkUpSessions() {
  // check up sessions
  for (int8_t i = 0; i < kUpSessionCount_; i++)
  {
    // if upsession's socket error, it'll be removed and set to NULL
    if (upSessions_[i] != NULL) {
      if (upSessions_[i]->isAvailable() == true)
      	continue;
      else
        removeUpConnection(upSessions_[i]);
    }

    UpStratumClient *up = createUpSession(i);
    if (up != NULL)
      addUpConnection(up);
  }
}

void StratumServer::listenerCallback(struct evconnlistener *listener,
                                     evutil_socket_t fd,
                                     struct sockaddr *saddr,
                                     int socklen, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  struct event_base  *base = (struct event_base*)server->base_;
  struct bufferevent *bev;

  // can't alloc session Id
  if (server->sessionIDManager_.ifFull()) {

#ifdef _WIN32
    LOG(ERROR) << "fixme: close(fd)!";
#else
    close(fd);
#endif

    return;
  }

  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if(bev == NULL) {
    LOG(ERROR) << "bufferevent_socket_new fail";
    server->stop();
    return;
  }

  const int8_t upSessionIdx = server->findUpSessionIdx();
  if (upSessionIdx == -1) {
    LOG(ERROR) << "no available up session";

#ifdef _WIN32
    LOG(ERROR) << "fixme: close(fd)!";
#else
    close(fd);
#endif

    return;
  }

  uint16_t sessionId = 0u;
  server->sessionIDManager_.allocSessionId(&sessionId);

  StratumSession *conn = new StratumSession(upSessionIdx, sessionId, bev, server);
  bufferevent_setcb(bev,
                    StratumServer::downReadCallback, NULL,
                    StratumServer::downEventCallback, (void*)conn);

  // By default, a newly created bufferevent has writing enabled.
  bufferevent_enable(bev, EV_READ|EV_WRITE);

  server->addDownConnection(conn);
}

void StratumServer::downReadCallback(struct bufferevent *bev, void *ptr) {
  static_cast<StratumSession *>(ptr)->recvData(bufferevent_get_input(bev));
}

void StratumServer::downEventCallback(struct bufferevent *bev,
                                      short events, void *ptr) {
  StratumSession *conn  = static_cast<StratumSession *>(ptr);
  StratumServer *server = conn->server_;

  // should not be 'BEV_EVENT_CONNECTED'
  assert((events & BEV_EVENT_CONNECTED) != BEV_EVENT_CONNECTED);

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "downsocket closed, sessionId: " << conn->sessionId_;
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(INFO) << "got an error on the downsocket, sessionId: " << conn->sessionId_
    << ", err: " << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "downsocket read/write timeout, events: " << events
    << ", sessionId:" << conn->sessionId_;
  }
  else {
    LOG(ERROR) << "unhandled downsocket events: " << events
    << ", sessionId:" << conn->sessionId_;
  }
  server->removeDownConnection(conn);
}

void StratumServer::addDownConnection(StratumSession *conn) {
  assert(downSessions_.size() >= (size_t)(conn->sessionId_ + 1));

  assert(downSessions_[conn->sessionId_] == NULL);
  downSessions_  [conn->sessionId_] = conn;
  upSessionCount_[conn->upSessionIdx_]++;
}

void StratumServer::removeDownConnection(StratumSession *downconn) {
  // unregister worker
  unRegisterWorker(downconn);

  // clear resources
  sessionIDManager_.freeSessionId(downconn->sessionId_);
  downSessions_  [downconn->sessionId_] = NULL;
  upSessionCount_[downconn->upSessionIdx_]--;
  delete downconn;
}

void StratumServer::run() {
  assert(base_ != NULL);
  event_base_dispatch(base_);
}

void StratumServer::upReadCallback(struct bufferevent *bev, void *ptr) {
  static_cast<UpStratumClient *>(ptr)->recvData(bufferevent_get_input(bev));
}

void StratumServer::addUpConnection(UpStratumClient *conn) {
  DLOG(INFO) << "add up connection, idx: " << (int32_t)(conn->idx_);
  assert(upSessions_[conn->idx_] == NULL);

  upSessions_[conn->idx_] = conn;
}

void StratumServer::removeUpConnection(UpStratumClient *upconn) {
  DLOG(INFO) << "remove up connection, idx: " << (int32_t)(upconn->idx_);
  assert(upSessions_[upconn->idx_] != NULL);

  // remove down session which belong to this up connection
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSession *s = downSessions_[i];
    if (s == NULL)
      continue;

    if (s->upSessionIdx_ == upconn->idx_)
      removeDownConnection(s);
  }

  upSessions_    [upconn->idx_] = NULL;
  upSessionCount_[upconn->idx_] = 0;
  delete upconn;
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
    return;
  }

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "upsession closed";
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(INFO) << "got an error on the upsession: "
    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "upsession read/write timeout, events: " << events;
  }
  else {
    LOG(ERROR) << "unhandled upsession events: " << events;
  }

  server->removeUpConnection(up);
}

void StratumServer::sendMiningNotifyToAll(const int8_t idx, const string &notify) {
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSession *s = downSessions_[i];
    if (s == NULL || s->upSessionIdx_ != idx)
      continue;

    s->sendData(notify);
  }
}

void StratumServer::sendMiningNotify(StratumSession *downSession) {
  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];
  if (up == NULL || up->latestMiningNotifyStr_.length() == 0)
    return;

  downSession->sendData(up->latestMiningNotifyStr_);
}

void StratumServer::sendDefaultMiningDifficulty(StratumSession *downSession) {
  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];
  if (up == NULL)
    return;

  const string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                                   ",\"params\":[%" PRIu32"]}\n",
                                   up->poolDefaultDiff_);
  downSession->sendData(s);
}

void StratumServer::sendMiningDifficulty(UpStratumClient *upconn,
                                         uint16_t sessionId, uint64_t diff) {
  StratumSession *downSession = downSessions_[sessionId];
  if (downSession == NULL)
    return;

  const string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                                   ",\"params\":[%" PRIu64"]}\n", diff);
  downSession->sendData(s);
}

int8_t StratumServer::findUpSessionIdx() {
  int32_t count = -1;
  int8_t idx = -1;

  for (size_t i = 0; i < upSessions_.size(); i++) {
    if (upSessions_[i] == NULL || !upSessions_[i]->isAvailable())
      continue;

    if (count == -1) {
      idx = i;
      count = upSessionCount_[i];
    }
    else if (upSessionCount_[i] < count) {
      idx = i;
      count = upSessionCount_[i];
    }
  }
  return idx;
}

void StratumServer::submitShare(const Share &share,
                                StratumSession *downSession) {
  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];

  bool isTimeChanged = true;
  if ((share.jobId_ == up->latestJobId_[2] && share.time_ == up->latestJobGbtTime_[2]) ||
      (share.jobId_ == up->latestJobId_[1] && share.time_ == up->latestJobGbtTime_[1]) ||
      (share.jobId_ == up->latestJobId_[0] && share.time_ == up->latestJobGbtTime_[0])) {
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
  up->sendData(buf);
}

void StratumServer::registerWorker(StratumSession *downSession,
                                   const char *minerAgent,
                                   const string &workerName) {
  //
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  //
  uint16_t len = 0;
  len += (1+1+2+2); // magic_num, cmd, len, session_id
  // client agent
  len += (minerAgent != NULL) ? strlen(minerAgent) : 0;
  len += 1; // '\0'
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
  if (minerAgent != NULL) {
    // strcpy: including the terminating null byte
    strcpy((char *)p, minerAgent);
    p += strlen(minerAgent) + 1;
  } else {
    *p++ = '\0';
  }

  // worker name
  strcpy((char *)p, workerName.c_str());
  p += workerName.length() + 1;
  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];
  up->sendData(buf);
}

void StratumServer::unRegisterWorker(StratumSession *downSession) {
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

  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];
  up->sendData(buf);
}
