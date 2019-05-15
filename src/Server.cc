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
#include <numeric>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/util.h>

#if (defined _WIN32 && defined USE_IOCP)
 #include <event2/thread.h>
#endif

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
      return "Invliad subaccount name";
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
SessionIDManager::SessionIDManager() {
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
StratumMessage::StratumMessage(const string &content)
: content_(content) {
  parse();
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
      return id_;
    }
  }
  return "";
}

void StratumMessage::parse() {
  jsmn_parser p;
  jsmn_init(&p);

  r_ = jsmn_parse(&p, content_.c_str(), content_.length(), t_, sizeof(t_) / sizeof(t_[0]));
  if (r_ < 0) {
    LOG(ERROR) << "failed to parse JSON: " << r_ << std::endl;
    return;
  }

  // assume the top-level element is an object
  if (r_ < 1 || t_[0].type != JSMN_OBJECT)
    return;

  parseId();
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

///////////////////////////////// UpStratumClient //////////////////////////////
UpStratumClient::UpStratumClient(const int8_t idx,
                                 StratumServer *server)
: idx_(idx), server_(server)
{
  initConnection();
  DLOG(INFO) << "idx_: " << (int32_t)idx_ << std::endl;
}

UpStratumClient::~UpStratumClient() {
  disconnect();
}

void UpStratumClient::initConnection() {
  auto base = server_->getEventBase();

  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  assert(bev_ != NULL);

  evdnsBase_ = evdns_base_new(base, 1);
  assert(evdnsBase_ != NULL);

  inBuf_ = evbuffer_new();
  assert(inBuf_ != NULL);

  bufferevent_setcb(bev_,
                    StratumServer::upReadCallback, NULL,
                    StratumServer::upEventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);
}

void UpStratumClient::disconnect() {
  state_ = UP_INIT;

  evbuffer_free(inBuf_);
  inBuf_ = nullptr;

  bufferevent_free(bev_);
  bev_ = nullptr;

  evdns_base_free(evdnsBase_, 0);
  evdnsBase_ = nullptr;
}

bool UpStratumClient::connect() {
  auto &pools = server_->getUpPools();
  for (size_t i=0; i < pools.size(); i++) {
    userName_ = pools[i].upPoolUserName_;

    int res = bufferevent_socket_connect_hostname(bev_, evdnsBase_, AF_INET, pools[i].host_.c_str(), (int)pools[i].port_);
    if (res == 0) {
      state_ = UP_CONNECTED;
      lastConnectTime_ = time(nullptr);

      LOG(INFO) << "success connect[" << (int32_t)idx_ << "]: " << pools[i].host_ << ":"
                << pools[i].port_ << ", subaccount name: " << userName_ << std::endl;
      return true;
    }
  }

  return false;
}

bool UpStratumClient::reconnect() {
  time_t now = time(nullptr);
  if (now - lastConnectTime_ < 5) {
    // Too fast reconnecting.
    // StratumServer::checkUpSessions() will do the reconnect again().
    server_->resetUpWatcherTime(5);
    return false;
  }

  disconnect();

  if (now - lastJobReceivedTime_ > 30) {
    lastJobReceivedTime_ = now;
    server_->sendFakeMiningNotifyToAll(this);
  }

  initConnection();
  return connect();
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
        handleExMessage(&exMessage);
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

void UpStratumClient::handleExMessage(const string *exMessage) {
  LOG(ERROR) << "received unknown ex-message, type: " << static_cast<uint16_t>(exMessage->data()[1])
             << ", len: " << exMessage->size() << std::endl;
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
    server_->sendMiningDifficulty(sessionId, diff);
  }

  LOG(INFO) << "up[" << (int32_t)idx_ << "] CMD_MINING_SET_DIFF, diff: "
            << diff << ", sessions count: " << count << std::endl;
}

void UpStratumClient::sendData(const char *data, size_t len) {
  if (state_ == UP_INIT) {
    DLOG(INFO) << "UpStratumClient unavailable, skip(" << len << ")" << std::endl;
    return;
  }

  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
  // DLOG(INFO) << "UpStratumClient send(" << len << "): " << data << std::endl;
}

bool UpStratumClient::sendRequest(const string &str) {
 if (!isAuthenticated()) {
  DLOG(INFO) << "UpStratumClient not authorized, skip(" << str.size() << ")" << std::endl;
  return false;
 } 
 sendData(str);
 return true;
}

bool UpStratumClient::isAvailable() {
  const uint32_t kJobExpiredTime = 60 * 5;  // seconds

  if (state_ == UP_AUTHENTICATED &&
      poolDefaultDiff_ != 0 &&
      lastJobReceivedTime_ + kJobExpiredTime > (uint32_t)time(NULL)) {
    return true;
  }
  return false;
}


////////////////////////////////// StratumSession //////////////////////////////
StratumSession::StratumSession(UpStratumClient & upSession, uint16_t sessionId,
                               struct bufferevent *bev, StratumServer *server,
                               struct in_addr saddr)
: upSession_(upSession)
, sessionId_(sessionId), bev_(bev)
, server_(server), saddr_(saddr)
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

/////////////////////////////////// StratumServer //////////////////////////////
StratumServer::StratumServer(const string &listenIP, const uint16_t listenPort)
: listenIP_(listenIP), listenPort_(listenPort)
{
  upSessions_    .resize(kUpSessionCount_, NULL);
  upSessionCount_.resize(kUpSessionCount_, 0);
  downSessions_.resize(AGENT_MAX_SESSION_ID + 1, NULL);
}

StratumServer::~StratumServer() {
  // remove upsessions
  for (size_t i = 0; i < upSessions_.size(); i++) {
    UpStratumClient *upsession = upSessions_[i];  // alias
    if (upsession == NULL) {
      continue;
    }

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
  if (!running_) {
    return;
  }

  LOG(INFO) << "stop tcp server event loop" << std::endl;
  running_ = false;
  event_base_loopexit(base_, NULL);
}

void StratumServer::addUpPool(const std::vector<PoolConf> &poolConfs) {
  upPools_ =  poolConfs;

  for (const auto &pool : upPools_) {
    LOG(INFO) << "add pool: " << pool.host_ << ":" << pool.port_ << ", subaccount name: " << pool.upPoolUserName_ << std::endl;
  }
}

const vector<PoolConf> & StratumServer::getUpPools() {
  return upPools_;
}

struct event_base * StratumServer::getEventBase() {
  return base_;
}

UpStratumClient * StratumServer::createUpSession(int8_t idx) {
  UpStratumClient *up = createUpClient(idx, this);
  if (!up->connect()) {
    return nullptr;
  }
  return up;
}

bool StratumServer::run(bool alwaysKeepDownconn) {
  alwaysKeepDownconn_ = alwaysKeepDownconn;

  if (running_) {
    return false;
  }
  running_ = true;

  if (upPools_.size() == 0) {
    return false;
  }

#ifdef _WIN32
  WSADATA wsa_data;

  if (WSAStartup(0x202, &wsa_data) == SOCKET_ERROR) {
      LOG(ERROR) << "WSAStartup failed: " << WSAGetLastError() << std::endl;
      return false;
  }

  #ifdef USE_IOCP
    // use IOCP for Windows
    evthread_use_windows_threads();
    struct event_config *cfg = event_config_new();
    event_config_set_flag(cfg, EVENT_BASE_FLAG_STARTUP_IOCP);
    base_ = event_base_new_with_config(cfg);
  #else
    // use select() by default
    base_ = event_base_new();
  #endif
#else
  base_ = event_base_new();
#endif

  if(!base_) {
    LOG(ERROR) << "server: cannot create event base" << std::endl;
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

  // every 15 seconds to check if up session's available
  resetUpWatcherTime(15);

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

  LOG(INFO) << "startup is successful, listening: " << listenIP_ << ":" << listenPort_ << std::endl;
    
  assert(base_ != NULL);
  event_base_dispatch(base_);
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

void StratumServer::resetUpWatcherTime(time_t seconds) {
  if (upEvTimer_ == nullptr) {
    // setup up sessions watcher
    upEvTimer_ = event_new(base_, -1, EV_PERSIST,
      StratumServer::upWatcherCallback, this);
  } else {
    event_del(upEvTimer_);
  }

  struct timeval tenSec = {seconds, 0};
  event_add(upEvTimer_, &tenSec);
}

void StratumServer::upWatcherCallback(evutil_socket_t fd,
                                      short events, void *ptr) {
  StratumServer *server = static_cast<StratumServer *>(ptr);
  server->checkUpSessions();
}

void StratumServer::checkUpSessions() {
  size_t aliveUpSessions = 0;

  // check up sessions
  for (int8_t i = 0; i < kUpSessionCount_; i++)
  {
    // if upsession's socket error, it'll be removed and set to NULL
    if (upSessions_[i] != nullptr) {
      if (upSessions_[i]->isAvailable() == true) {
        aliveUpSessions++;
        continue;
      }

      if (alwaysKeepDownconn_) {
        upSessions_[i]->reconnect();
        continue;
      }
      
      removeUpConnection(upSessions_[i]);
    }

    UpStratumClient *up = createUpSession(i);
    if (up != nullptr) {
      addUpConnection(up);
    }
  }

  // Print when the number of connections changes
  static size_t lastDownSessions = 0;
  static size_t lastUpSessions = 0;

  size_t aliveDownSessions = std::accumulate(upSessionCount_.begin(), upSessionCount_.end(), 0);
  
  if (lastDownSessions != aliveDownSessions || lastUpSessions != aliveUpSessions) {
    if (aliveUpSessions == kUpSessionCount_) {
      resetUpWatcherTime(15);
    }

    lastDownSessions = aliveDownSessions;
    lastUpSessions = aliveUpSessions;

    LOG(INFO) << "connection number changed, servers: " << aliveUpSessions
              << ", miners: " << aliveDownSessions << std::endl;
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
    closesocket(fd);
#else
    close(fd);
#endif

    return;
  }

  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if(bev == NULL) {
    LOG(ERROR) << "bufferevent_socket_new fail" << std::endl;
    server->stop();
    return;
  }

  auto upSession = server->findUpSession();
  if (upSession == nullptr) {
    LOG(ERROR) << "no available up session" << std::endl;

#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif

    return;
  }

  uint16_t sessionId = 0u;
  server->sessionIDManager_.allocSessionId(&sessionId);

  StratumSession *conn = server->createDownConnection(*upSession,
                                                      sessionId,
                                                      bev,
                                                      server,
                                                      reinterpret_cast<struct sockaddr_in *>(saddr)->sin_addr);
  bufferevent_setcb(bev,
                    StratumServer::downReadCallback, NULL,
                    StratumServer::downEventCallback, (void*)conn);

  // By default, a newly created bufferevent has writing enabled.
  bufferevent_enable(bev, EV_READ|EV_WRITE);

  server->addDownConnection(conn);
  
  // get source IP address
  char saddrBuffer[INET_ADDRSTRLEN];
  evutil_inet_ntop(AF_INET, &conn->saddr_, saddrBuffer, INET_ADDRSTRLEN);
  
  LOG(INFO) << "miner connected, sessionId: " << conn->sessionId_ << ", IP: " << saddrBuffer << std::endl;
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
  server->removeDownConnection(conn);
}

void StratumServer::addDownConnection(StratumSession *conn) {
  assert(downSessions_.size() >= (size_t)(conn->sessionId_ + 1));

  assert(downSessions_[conn->sessionId_] == NULL);
  downSessions_  [conn->sessionId_] = conn;
  upSessionCount_[conn->upSession_.idx_]++;
}

void StratumServer::removeDownConnection(StratumSession *downconn) {
  // unregister worker
  unRegisterWorker(downconn);

  // clear resources
  sessionIDManager_.freeSessionId(downconn->sessionId_);
  downSessions_  [downconn->sessionId_] = NULL;
  upSessionCount_[downconn->upSession_.idx_]--;
  delete downconn;
}

void StratumServer::upReadCallback(struct bufferevent *bev, void *ptr) {
  static_cast<UpStratumClient *>(ptr)->recvData(bufferevent_get_input(bev));
}

void StratumServer::addUpConnection(UpStratumClient *conn) {
  DLOG(INFO) << "add up connection, idx: " << (int32_t)(conn->idx_) << std::endl;
  assert(upSessions_[conn->idx_] == NULL);

  upSessions_[conn->idx_] = conn;
}

void StratumServer::removeUpConnection(UpStratumClient *upconn) {
  DLOG(INFO) << "remove up connection, idx: " << (int32_t)(upconn->idx_) << std::endl;
  assert(upSessions_[upconn->idx_] != NULL);

  if (upSessions_[upconn->idx_] == NULL) {
    LOG(ERROR) << "network unavailable" << std::endl;
    exit(1);
  }

  // remove down session which belong to this up connection
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSession *s = downSessions_[i];
    if (s == NULL)
      continue;

    if (&s->upSession_ == upconn)
      removeDownConnection(s);
  }

  upSessions_    [upconn->idx_] = NULL;
  upSessionCount_[upconn->idx_] = 0;
  delete upconn;
}

void StratumServer::upEventCallback(struct bufferevent *bev,
                                    short events, void *ptr) {
  //
  // `ptr == nullptr` means that this is a global event, regardless of
  // the particular connection.
  //
  // It will be NULL if the OS (not only Windows but also Linux) has
  // no network device or just no available network access.
  //
  // The situation often occurs when Wifi users lost their connection.
  // Or each time while Windows XP startup - it will autostart every
  // program before init it's network.
  //
  if (ptr == nullptr) {
    LOG(ERROR) << "unhandled events from local network: " << events << std::endl;
    return;
  }

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

  if (server->alwaysKeepDownconn_) {
    up->reconnect();
    return;
  }
  
  server->removeUpConnection(up);
}

void StratumServer::sendMiningNotifyToAll(const UpStratumClient *conn) {
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSession *s = downSessions_[i];
    if (s == NULL || &s->upSession_ != conn)
      continue;

    s->sendMiningNotify();
  }
}

void StratumServer::sendFakeMiningNotifyToAll(const UpStratumClient *conn) {
  size_t counter = 0;
  for (size_t i = 0; i < downSessions_.size(); i++) {
    StratumSession *s = downSessions_[i];
    if (s == NULL || &s->upSession_ != conn)
      continue;

    s->sendFakeMiningNotify();
    counter++;
  }

  if (counter > 0) {
    LOG(INFO) << "Send fake job to " << counter << " miners";
  }
}

void StratumServer::sendMiningDifficulty(UpStratumClient *upSession, uint64_t diff) {
  for (auto downSession : downSessions_) {
    if (downSession != nullptr && upSession == &downSession->upSession_) {
      downSession->sendMiningDifficulty(diff);
    }
  }
}

void StratumServer::sendMiningDifficulty(uint16_t sessionId, uint64_t diff) {
  StratumSession *downSession = downSessions_[sessionId];
  if (downSession == NULL)
    return;

  downSession->sendMiningDifficulty(diff);
}

UpStratumClient *StratumServer::findUpSession() {
  int32_t count = INT32_MAX;
  UpStratumClient *upSession = nullptr;

  for (size_t i = 0; i < upSessions_.size(); i++) {
    if (upSessions_[i] == nullptr || !upSessions_[i]->isAvailable()) {
      continue;
    }
    if (upSessionCount_[i] < count) {
      upSession = upSessions_[i];
      count = upSessionCount_[i];
    }
  }

  // Provide an unavailable connection if no available connection can be found
  if (upSession == nullptr && alwaysKeepDownconn_) {
    for (size_t i = 0; i < upSessions_.size(); i++) {
      if (upSessions_[i] == nullptr) {
        continue;
      }
      if (upSessionCount_[i] < count) {
        upSession = upSessions_[i];
        count = upSessionCount_[i];
      }
    }
  }

  return upSession;
}

void StratumServer::registerWorker(UpStratumClient *upSession) {
  for (auto downSession : downSessions_) {
    if (downSession != nullptr && upSession == &downSession->upSession_) {
      registerWorker(downSession);
    }
  }
}

void StratumServer::registerWorker(StratumSession *downSession) {
  //
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  //
  uint16_t len = 0;
  len += (1+1+2+2); // magic_num, cmd, len, session_id
  // client agent
  len += downSession->minerAgent().size() + 1; // miner agent and '\0'
  len += downSession->workerName().size() + 1;  // worker name and '\0'

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
  strcpy((char *)p, downSession->minerAgent().c_str());
  p += downSession->minerAgent().size() + 1;

  // worker name
  strcpy((char *)p, downSession->workerName().c_str());
  p += downSession->workerName().size() + 1;
  assert(p - (uint8_t *)buf.data() == (int64_t)buf.size());

  downSession->upSession_.sendRequest(buf);
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

  downSession->upSession_.sendRequest(buf);
}
