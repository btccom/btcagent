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

#include <arpa/inet.h>

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
SessionIDManager::SessionIDManager(): count_(0) {
  sessionIds_.reset();
}

bool SessionIDManager::ifFull() {
  if (count_ >= (int32_t)(AGENT_MAX_SESSION_ID + 1)) {
    return true;
  }
  return false;
}

uint16_t SessionIDManager::allocSessionId() {
  // find an empty bit, always find the smallest
  uint16_t idx = 0;
  while (sessionIds_.test(idx) == true) {
    idx++;
    if (idx > AGENT_MAX_SESSION_ID) {
      idx = 0;
    }
  }

  // set to true
  sessionIds_.set(idx, true);
  count_++;

  return idx;
}

void SessionIDManager::freeSessionId(const uint16_t sessionId) {
  sessionIds_.set(sessionId, false);
  count_--;
}



///////////////////////////////// UpStratumClient //////////////////////////////
UpStratumClient::UpStratumClient(const int8_t idx, struct event_base *base,
                                 const string &userName, StratumServer *server)
: state_(INIT), idx_(idx), server_(server), poolDefaultDiff_(0)
{
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
  assert(bev_ != NULL);

  bufferevent_setcb(bev_,
                    StratumServer::upReadCallback, NULL,
                    StratumServer::upEventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);

  extraNonce1_ = 0u;
  extraNonce2_ = 0u;

  inBuf_ = evbuffer_new();

  latestJobId_[0] = latestJobId_[1] = latestJobId_[2] = 0;
  latestJobGbtTime_[0] = latestJobGbtTime_[1] = latestJobGbtTime_[2] = 0;
}

UpStratumClient::~UpStratumClient() {
  bufferevent_free(bev_);
}

bool UpStratumClient::connect(struct sockaddr_in &sin) {
  // bufferevent_socket_connect(): This function returns 0 if the connect
  // was successfully launched, and -1 if an error occurred.
  int res = bufferevent_socket_connect(bev_, (struct sockaddr *)&sin, sizeof(sin));
  if (res == 0) {
    state_ = CONNECTED;
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
  // | magic_number(1) | cmd(1) | len (2) | diff (4) | count(2) | session_id (2) ... |
  //
  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint32_t diff  = *(uint32_t *)(p + 4);
  const uint16_t count = *(uint16_t *)(p + 8);

  uint16_t *sessionIdPtr = (uint16_t *)(p + 10);
  for (uint8_t i = 0; i < count; i++) {
    uint16_t sessionId = *sessionIdPtr++;
    server_->sendMiningDifficulty(this, sessionId, diff);
  }
}

void UpStratumClient::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "UpStratumClient send(" << len << "): " << data;
}

void UpStratumClient::sendMiningNotify(const string &line) {
  const char *pch = splitNotify(line);

  // send to all down sessions
  server_->sendMiningNotifyToAll(idx_, line.c_str(),
                                 pch - line.c_str(), pch);
}

void UpStratumClient::handleStratumMessage(const string &line) {
  DLOG(INFO) << "UpStratumClient recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jresult = jnode["result"];
  JsonNode jerror  = jnode["error"];
  JsonNode jmethod = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    JsonNode jparams  = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      latestMiningNotifyStr_ = line;
      sendMiningNotify(line);

      latestJobId_[1]      = latestJobId_[2];
      latestJobGbtTime_[1] = latestJobGbtTime_[2];
      latestJobId_[0]      = latestJobId_[1];
      latestJobGbtTime_[0] = latestJobGbtTime_[1];

      // the jobId always between [0, 9]
      latestJobId_[2]      = (uint8_t)jparamsArr[0].uint32();  /* job id     */
      latestJobGbtTime_[2] = jparamsArr[7].uint32_hex();       /* block time */
    }
    else if (jmethod.str() == "mining.set_difficulty") {
      // just set the default pool diff, than ignore
      if (poolDefaultDiff_ == 0) {
        poolDefaultDiff_ = jparamsArr[0].uint32();
      }
    }
    else {
      LOG(ERROR) << "unknown method: " << line;
    }
    return;
  }

  if (state_ == AUTHENTICATED) {
    //
    // {"error": null, "id": 2, "result": true}
    //
    if (jerror.type()  != Utilities::JS::type::Null ||
        jresult.type() != Utilities::JS::type::Bool ||
        jresult.boolean() != true) {
      // TODO
    }
    return;
  }

  if (state_ == CONNECTED) {
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    if (jerror.type() != Utilities::JS::type::Null) {
      LOG(ERROR) << "json result is null, err: " << jerror.str();
      return;
    }
    std::vector<JsonNode> resArr = jresult.array();
    if (resArr.size() < 3) {
      LOG(ERROR) << "result element's number is less than 3: " << line;
      return;
    }
    extraNonce1_ = resArr[1].uint32_hex();
    DLOG(INFO) << "extraNonce1 / SessionID: " << extraNonce1_;

    // check extra nonce2's size, MUST be 8 bytes
    if (resArr[2].uint32() != 8) {
      LOG(FATAL) << "extra nonce2's size is NOT 8 bytes";
      return;
    }
    // subscribe successful
    state_ = SUBSCRIBED;

    // do mining.authorize
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"%s\", \"\"]}\n",
                               userName_.c_str());
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED && jresult.boolean() == true) {
    // authorize successful
    state_ = AUTHENTICATED;
    LOG(INFO) << "auth success, name: \"" << userName_
    << "\", extraNonce1: " << extraNonce1_;
    return;
  }
}

bool UpStratumClient::isAvailable() {
  if (latestMiningNotifyStr_.empty() == false && poolDefaultDiff_ != 0) {
    return true;
  }
  return false;
}


////////////////////////////////// StratumSession //////////////////////////////
StratumSession::StratumSession(const int8_t upSessionIdx,
                               const uint16_t sessionId,
                               struct bufferevent *bev, StratumServer *server)
: state_(CONNECTED), minerAgent_(NULL), upSessionIdx_(upSessionIdx),
sessionId_(sessionId), bev_(bev), server_(server)
{
  inBuf_ = evbuffer_new();
}

StratumSession::~StratumSession() {
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

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jid = jnode["id"];
  JsonNode jmethod = jnode["method"];
  JsonNode jparams = jnode["params"];

  string idStr = "null";
  if (jid.type() == Utilities::JS::type::Int) {
    idStr = jid.str();
  } else if (jid.type() == Utilities::JS::type::Str) {
    idStr = "\"" + jnode["id"].str() + "\"";
  }

  if (jmethod.type() == Utilities::JS::type::Str &&
      jmethod.size() != 0 &&
      jparams.type() == Utilities::JS::type::Array) {
    handleRequest(idStr, jmethod.str(), jparams);
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

void StratumSession::handleRequest(const string &idStr, const string &method,
                                   JsonNode &jparams) {
  if (method == "mining.submit") {  // most of requests are 'mining.submit'
    handleRequest_Submit(idStr, jparams);
  }
  else if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams);
  }
  else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, jparams);
  } else {
    // unrecognised method, just ignore it
    LOG(WARNING) << "unrecognised method: \"" << method << "\"";
  }
}

void StratumSession::handleRequest_Subscribe(const string &idStr,
                                             const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumError::UNKNOWN);
    return;
  }
  state_ = SUBSCRIBED;

  //
  //  params[0] = client version     [optional]
  //  params[1] = session id of pool [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["bfgminer/4.4.0-32-gac4e9b3", "01ad557d"]}
  //

  if (jparams.children()->size() >= 1) {
    // 30 is max length for miner agent
    minerAgent_ = strdup(jparams.children()->at(0).str().substr(0, 30).c_str());
  }

  //  result[0] = 2-tuple with name of subscribed notification and subscription ID.
  //              Theoretically it may be used for unsubscribing, but obviously miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase.
  //  result[2] = Extranonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  assert(kExtraNonce2Size_ == 4);
  const uint32_t extraNonce1 = (uint32_t)sessionId_;
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"%08x\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%08x\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1, extraNonce1,
                                   extraNonce1, kExtraNonce2Size_);
  sendData(s);
}

void StratumSession::handleRequest_Authorize(const string &idStr,
                                             const JsonNode &jparams) {
  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumError::NOT_SUBSCRIBED);
    return;
  }

  //
  //  params[0] = user[.worker]
  //  params[1] = password
  //  eg. {"params": ["slush.miner1", "password"], "id": 2, "method": "mining.authorize"}
  //
  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumError::INVALID_USERNAME);
    return;
  }

  // auth success
  responseTrue(idStr);
  state_ = AUTHENTICATED;

  string workerName = getWorkerName(jparams.children()->at(0).str());
  if (workerName.empty()) {
    workerName = "default";
  }

  // sent sessionId, minerAgent_, workerName to server_
  server_->registerWorker(this, sessionId_, minerAgent_, workerName);
  free(minerAgent_);
  minerAgent_ = NULL;

  // send mining.set_difficulty
  server_->sendDefaultMiningDifficulty(this);

  // send latest stratum job
  server_->sendMiningNotify(this);
}

void StratumSession::handleRequest_Submit(const string &idStr,
                                          JsonNode &jparams) {
  if (state_ != AUTHENTICATED) {
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
  if (jparams.children()->size() < 5) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }

  // submit share
  server_->submitShare(jparams, this);

  responseTrue(idStr);  // we assume shares are valid
}



/////////////////////////////////// StratumServer //////////////////////////////
StratumServer::StratumServer(const uint16_t listenPort, const string upPoolUserName)
:running_ (true), upPoolUserName_(upPoolUserName), base_(NULL)
{
  memset(&listenAddr_, 0, sizeof(listenAddr_));
  listenAddr_.sin_family = AF_INET;
  listenAddr_.sin_port   = htons(listenPort);
  listenAddr_.sin_addr.s_addr = htonl(INADDR_ANY);

  upSessions_.resize(kUpSessionCount_);
  upSessionCount_.resize(kUpSessionCount_);
  upEvTimer_ = NULL;

  downSessions_.resize(16);  // must pre alloc some elements here

  assert(upSessions_.size() == upSessionCount_.size());
}

StratumServer::~StratumServer() {
  // TODO
  if (upEvTimer_)
    event_del(upEvTimer_);

  event_base_free(base_);
}

void StratumServer::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop tcp server event loop";
  running_ = false;
  event_base_loopexit(base_, NULL);
}

void StratumServer::addUpPool(const char *host, const uint16_t port) {
  upPoolHost_.push_back(string(host));
  upPoolPort_.push_back(port);
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

    UpStratumClient *up = new UpStratumClient(idx, base_, upPoolUserName_, this);
    if (!up->connect(sin)) {
      delete up;
      continue;
    }
    return up;  // connect success
  }

  return NULL;
}

bool StratumServer::setup() {
  if (upPoolHost_.size() == 0 || upPoolHost_.size() != upPoolPort_.size())
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
    upSessions_[i] = up;
    addUpConnection(upSessions_[i]);
  }

  // wait util all up session available
  struct event *checkTimer;
  checkTimer = event_new(base_, -1, EV_PERSIST,
                         StratumServer::upSesssionCheckCallback, this);
  struct timeval oneSec = {1, 0};
  event_add(checkTimer, &oneSec);

  // run event dispatch, it will break util all up sessions are available
  event_base_dispatch(base_);
  event_del(checkTimer);

  // setup up sessions watcher
  upEvTimer_ = event_new(base_, -1, EV_PERSIST,
                         StratumServer::upWatcherCallback, this);
  struct timeval tenSec = {10, 0};
  event_add(upEvTimer_, &tenSec);

  // set up ev listener
  listener_ = evconnlistener_new_bind(base_,
                                      StratumServer::listenerCallback,
                                      (void*)this,
                                      LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                      // backlog, Set to -1 for a reasonable default
                                      -1,
                                      (struct sockaddr*)&listenAddr_,
                                      sizeof(listenAddr_));
  if(!listener_) {
    LOG(ERROR) << "cannot create listener";
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
    assert(upSessions_[i] != NULL);
    if (upSessions_[i]->isAvailable() == false) {
      return;
    }
  }
  // all up session is available, break event loop
  event_base_loopbreak(base_);
}

void StratumServer::upWatcherCallback(evutil_socket_t fd,
                                      short events, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  server->checkUpSessions();
}

void StratumServer::checkUpSessions() {
  // check up sessions
  for (int8_t i = 0; i < kUpSessionCount_; i++) {
    if (upSessions_[i] != NULL)
      continue;

    UpStratumClient *up = createUpSession(i);
    if (up == NULL)
      continue;
    upSessions_[i] = up;
    addUpConnection(upSessions_[i]);
  }
}

void StratumServer::listenerCallback(struct evconnlistener *listener,
                                     evutil_socket_t fd,
                                     struct sockaddr *saddr,
                                     int socklen, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  struct event_base  *base = (struct event_base*)server->base_;
  struct bufferevent *bev;

  if (server->sessionIDManager_.ifFull()) {
    close(fd);
    return;
  }

  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
  if(bev == NULL) {
    LOG(ERROR) << "bufferevent_socket_new fail";
    server->stop();
    return;
  }

  const int8_t upSessionIdx = server->findUpSessionIdx();
  if (upSessionIdx == -1) {
    LOG(ERROR) << "no available up session";
    close(fd);
    return;
  }

  const uint16_t sessionId = server->sessionIDManager_.allocSessionId();
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
    LOG(INFO) << "downsocket closed";
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(ERROR) << "got an error on the downsocket: "
    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "downsocket read/write timeout, events: " << events;
  }
  else {
    LOG(ERROR) << "unhandled downsocket events: " << events;
  }
  server->removeDownConnection(conn);
}

void StratumServer::addDownConnection(StratumSession *conn) {
  if (downSessions_.size() < (size_t)(conn->sessionId_ + 1)) {
    downSessions_.resize((size_t)(conn->sessionId_ + 1));
  }
  assert(downSessions_[conn->sessionId_] == NULL);
  downSessions_[conn->sessionId_] = conn;
  upSessionCount_[conn->upSessionIdx_]++;
}

void StratumServer::removeDownConnection(StratumSession *conn) {
  // unregister worker
  unRegisterWorker(conn, conn->sessionId_);

  // clear resources
  bufferevent_free(conn->bev_);
  sessionIDManager_.freeSessionId(conn->sessionId_);
  downSessions_[conn->sessionId_] = NULL;
  upSessionCount_[conn->upSessionIdx_]--;
  delete conn;
}

void StratumServer::run() {
  assert(base_ != NULL);
  event_base_dispatch(base_);
}

void StratumServer::upReadCallback(struct bufferevent *bev, void *ptr) {
  static_cast<UpStratumClient *>(ptr)->recvData(bufferevent_get_input(bev));
}

void StratumServer::addUpConnection(UpStratumClient *conn) {
  LOG(INFO) << "add up connection, idx: " << conn->idx_;
  assert(upSessions_[conn->idx_] == NULL);

  upSessions_[conn->idx_] = conn;
}

void StratumServer::removeUpConnection(UpStratumClient *conn) {
  LOG(INFO) << "remove up connection, idx: " << conn->idx_;
  assert(upSessions_[conn->idx_] != NULL);

  // remove down session which belong to this up connection
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSession *s = downSessions_[i];
    if (s->upSessionIdx_ == conn->idx_)
      removeDownConnection(s);
  }

  upSessions_[conn->idx_] = NULL;
  upSessionCount_[conn->idx_] = 0;
  delete conn;
}

void StratumServer::upEventCallback(struct bufferevent *bev,
                                    short events, void *ptr) {
  UpStratumClient *up = static_cast<UpStratumClient *>(ptr);
  StratumServer *server = up->server_;

  if (events & BEV_EVENT_CONNECTED) {
    up->state_ = UpStratumClient::State::CONNECTED;

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
    LOG(ERROR) << "got an error on the upsession: "
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

void StratumServer::sendMiningNotifyToAll(const int8_t idx,
                                          const char *p1, size_t p1Len,
                                          const char *p2) {
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSession *s = downSessions_[i];
    if (s == NULL || s->upSessionIdx_ != idx)
      continue;

    s->sendData(p1, p1Len);
    const string e1 = Strings::Format("%08x", (uint32_t)s->sessionId_);
    s->sendData(e1.c_str(), 8);
    s->sendData(p2, strlen(p2));
  }
}

void StratumServer::sendMiningNotify(StratumSession *downSession) {
  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];
  assert(up != NULL);

  const string &notify = up->latestMiningNotifyStr_;
  const char *pch = splitNotify(notify);

  downSession->sendData(notify.c_str(), pch - notify.c_str());
  const string e1 = Strings::Format("%08x", (uint32_t)downSession->sessionId_);
  downSession->sendData(e1.c_str(), 8);
  downSession->sendData(pch, strlen(pch));
}

void StratumServer::sendDefaultMiningDifficulty(StratumSession *downSession) {
  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];
  assert(up != NULL);
  const string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                             ",\"params\":[%" PRIu32"]}\n",
                             up->poolDefaultDiff_);
  downSession->sendData(s);
}

void StratumServer::sendMiningDifficulty(UpStratumClient *upconn,
                                         uint16_t sessionId, uint32_t diff) {
  StratumSession *downSession = downSessions_[sessionId];
  if (downSession == NULL)
    return;

  const string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                                   ",\"params\":[%" PRIu32"]}\n", diff);
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

void StratumServer::submitShare(JsonNode &jparams,
                                StratumSession *downSession) {
  auto jparamsArr = jparams.array();
  if (jparamsArr.size() < 5) {
    LOG(ERROR) << "invalid share, params num is less than 5";
    return;
  }

  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];

  bool isTimeChanged = true;
  const uint8_t  jobId = (uint8_t)jparamsArr[1].uint32();
  const uint32_t nTime = jparamsArr[3].uint32_hex();

  if ((jobId == up->latestJobId_[2] && nTime == up->latestJobGbtTime_[2]) ||
      (jobId == up->latestJobId_[1] && nTime == up->latestJobGbtTime_[1]) ||
      (jobId == up->latestJobId_[0] && nTime == up->latestJobGbtTime_[0])) {
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
  *p++ = *jparamsArr[1].str().c_str();

  // session Id
  *(uint16_t *)p = downSession->sessionId_;
  p += 2;

  // extra_nonce2
  *(uint32_t *)p = jparamsArr[2].uint32_hex();
  p += 4;

  // nonce
  *(uint32_t *)p = jparamsArr[4].uint32_hex();
  p += 4;

  // ntime
  if (isTimeChanged) {
    *(uint32_t *)p = nTime;
    p += 4;
  }
  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  // send buf
  up->sendData(buf);
}

void StratumServer::registerWorker(StratumSession *downSession,
                                   uint16_t sessionId,
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

void StratumServer::unRegisterWorker(StratumSession *downSession,
                                     uint16_t sessionId) {
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
  *(uint16_t *)p = sessionId;
  p += 2;
  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  UpStratumClient *up = upSessions_[downSession->upSessionIdx_];
  up->sendData(buf);
}
