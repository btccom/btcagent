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
#ifndef SERVER_H_
#define SERVER_H_

#include "Utils.h"
#include "jsmn.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/dns.h>

#include <bitset>
#include <map>
#include <memory>
#include <set>
#include <atomic>


// In some cases this macro definition is missing under Windows
#if defined(_WIN32) && !defined(INET_ADDRSTRLEN)
  #define INET_ADDRSTRLEN 16
#endif


#define CMD_MAGIC_NUMBER  0x7Fu
// types
#define CMD_REGISTER_WORKER               0x01u    // Agent -> Pool
#define CMD_SUBMIT_SHARE                  0x02u    // Agent -> Pool,  mining.submit(...)
#define CMD_SUBMIT_SHARE_WITH_TIME        0x03u    // Agent -> Pool,  mining.submit(..., nTime)
#define CMD_UNREGISTER_WORKER             0x04u    // Agent -> Pool
#define CMD_MINING_SET_DIFF               0x05u    // Pool  -> Agent, mining.set_difficulty(diff)
#define CMD_SUBMIT_RESPONSE               0x10u    // Pool  -> Agent, response of the submit (optional)
#define CMD_SUBMIT_SHARE_WITH_VER         0x12u    // Agent -> Pool,  mining.submit(..., nVersionMask)
#define CMD_SUBMIT_SHARE_WITH_TIME_VER    0x13u    // Agent -> Pool,  mining.submit(..., nTime, nVersionMask)
#define CMD_GET_NONCE_PREFIX              0x21u    // Agent -> Pool,  ask the pool to allocate nonce prefix
#define CMD_SET_NONCE_PREFIX              0x22u    // Pool  -> Agent, pool nonce prefix allocation result

// agent, DO NOT CHANGE
#define AGENT_MAX_SESSION_ID   0xFFFEu  // 0xFFFEu = 65534

// default worker name
#define DEFAULT_WORKER_NAME    "__default__"

class StratumSession;
class StratumServer;
class UpStratumClient;


//////////////////////////////// StratumStatus ////////////////////////////////
class StratumStatus {
public:
  enum {
    // make ACCEPT and SOLVED be two singular value,
    // so code bug is unlikely to make false ACCEPT shares

    // share reached the job target (but may not reached the network target)
    ACCEPT = 1798084231, // bin(01101011 00101100 10010110 10000111)

    // share reached the job target but the job is stale
    // if uncle block is allowed in the chain, share can be accept as this
    // status
    ACCEPT_STALE = 950395421, // bin(00111000 10100101 11100010 00011101)

    // share reached the network target
    SOLVED = 1422486894, // bin(‭01010100 11001001 01101101 01101110‬)

    // share reached the network target but the job is stale
    // if uncle block is allowed in the chain, share can be accept as this
    // status
    SOLVED_STALE = 1713984938, // bin(01100110 00101001 01010101 10101010)

    // share reached the network target but the correctness is not verified
    SOLVED_PRELIMINARY =
        1835617709, // // bin(01101101 01101001 01001101 10101101)

    REJECT_NO_REASON = 0,

    JOB_NOT_FOUND_OR_STALE = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY = 23,
    UNAUTHORIZED = 24,
    NOT_SUBSCRIBED = 25,

    ILLEGAL_METHOD = 26,
    ILLEGAL_PARARMS = 27,
    IP_BANNED = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR = 30,
    TIME_TOO_OLD = 31,
    TIME_TOO_NEW = 32,
    ILLEGAL_VERMASK = 33,

    INVALID_SOLUTION = 34,
    WRONG_NONCE_PREFIX = 35,

    JOB_NOT_FOUND = 36,
    STALE_SHARE = 37,

    CLIENT_IS_NOT_SWITCHER = 400,

    UNKNOWN = 2147483647 // bin(01111111 11111111 11111111 11111111)
  };

  static const char *toString(int err);

  inline static bool isAccepted(int status) {
    return (status == ACCEPT) || (status == ACCEPT_STALE) ||
        (status == SOLVED) || (status == SOLVED_STALE);
  }

  inline static bool isAcceptedStale(int status) {
    return (status == ACCEPT_STALE) || (status == SOLVED_STALE);
  }

  inline static bool isRejectedStale(int status) {
    return (status == JOB_NOT_FOUND_OR_STALE) || (status == STALE_SHARE);
  }

  inline static bool isAnyStale(int status) {
    return isAcceptedStale(status) || isRejectedStale(status);
  }

  inline static bool isSolved(int status) {
    return (status == SOLVED) || (status == SOLVED_STALE) ||
        (status == SOLVED_PRELIMINARY);
  }
};


//////////////////////////////// SubmitId //////////////////////////////
struct SubmitId {
  uint16_t sessionId_;
  string idStr_;
};


//////////////////////////////// SessionIDManager //////////////////////////////
class SessionIDManager {
  std::bitset<AGENT_MAX_SESSION_ID + 1> sessionIds_;
  int32_t count_ = 0;
  uint32_t allocIdx_ = 0;

public:
  SessionIDManager();

  bool ifFull();
  bool allocSessionId(uint16_t *sessionId);  // range: [0, AGENT_MAX_SESSION_ID]
  void freeSessionId(const uint16_t sessionId);
};

///////////////////////////////// StratumMessage ///////////////////////////////
class StratumMessage {
protected:
  string content_;

  string method_;      // if it's invalid json string, you can't get the method
  string id_;          // "id"
  bool   isStringId_ = false;  // "id" is string or not

  // json
  jsmntok_t t_[64];    // we expect no more than 64 tokens
  int r_ = 0;

  string findMethod() const;
  string parseId();
  void parse();

  explicit StratumMessage(const string &content);

public:
  bool isValid() const;
  string getMethod() const;
  bool getResultBoolean() const;
  string getId() const;
  bool isStringId() const;

  string getJsonStr(const jsmntok_t *t) const;
  int jsoneq(const jsmntok_t *tok, const char *s) const;
};


/////////////////////////////////// StratumServer //////////////////////////////
class StratumServer {
private:
  //
  // if you use tcp socket for a long time, over than 24 hours at WAN network,
  // you will find it's always get error and disconnect sometime. so we use
  // multi-tcp connections to connect to the pool. if one of them got
  // disconnect, just some miners which belong to this connection(UpStratumClient)
  // will reconnect instead of all miners reconnect to the Agent.
  //
  static const int8_t kUpSessionCount_ = 5;  // MAX is 127
  bool running_ = false;

  struct event *upEvTimer_ = nullptr;

  // libevent2
  struct event_base *base_ = nullptr;
  struct event *signal_event_ = nullptr;
  struct evconnlistener *listener_ = nullptr;

  void checkUpSessions();
  void waitUtilAllUpSessionsAvailable();

  virtual UpStratumClient *createUpClient(int8_t idx,
                                          StratumServer *server) = 0;
  virtual StratumSession *createDownConnection(UpStratumClient &upSession,
                                               uint16_t sessionId,
                                               struct bufferevent *bev,
                                               StratumServer *server,
                                               struct in_addr saddr) = 0;

protected:
  // up stream connnections
  vector<UpStratumClient *> upSessions_;
  vector<int32_t> upSessionCount_;

  // down stream connections
  vector<StratumSession *> downSessions_;

  AgentConf conf_;

public:
  SessionIDManager sessionIDManager_;

public:
  StratumServer(const AgentConf &conf);
  virtual ~StratumServer();

  UpStratumClient *createUpSession(const int8_t idx);

  inline const vector<PoolConf>& getUpPools() { return conf_.pools_; }
  inline struct event_base* getEventBase() { return base_; }
  inline bool disconnectWhenLostAsicBoost() { return conf_.disconnectWhenLostAsicBoost_; }
  inline bool useIpAsWorkerName() { return conf_.useIpAsWorkerName_; }
  inline string ipWorkerNameFormat() { return conf_.ipWorkerNameFormat_; }
  inline bool submitResponseFromServer() { return conf_.submitResponseFromServer_; }
  inline const string &fixedWorkerName() { return conf_.fixedWorkerName_; }
  inline bool poolUseTls() { return conf_.poolUseTls_; }

  void addDownConnection   (StratumSession *conn);
  void removeDownConnection(StratumSession *conn);

  static void listenerCallback(struct evconnlistener *listener,
                               evutil_socket_t fd,
                               struct sockaddr* saddr,
                               int socklen, void *ptr);
  static void downReadCallback (struct bufferevent *, void *ptr);
  static void downEventCallback(struct bufferevent *, short, void *ptr);

  void addUpConnection   (UpStratumClient *conn);
  void removeUpConnection(UpStratumClient *conn);

  static void upReadCallback (struct bufferevent *, void *ptr);
  static void upEventCallback(struct bufferevent *, short, void *ptr);

  void resetUpWatcherTime(time_t seconds);
  
  static void upWatcherCallback(evutil_socket_t fd, short events, void *ptr);
  static void upSesssionCheckCallback(evutil_socket_t fd, short events, void *ptr);

  void sendMiningNotifyToAll(const UpStratumClient *conn);
  void sendFakeMiningNotifyToAll(const UpStratumClient *conn);
  void sendMiningDifficulty(UpStratumClient *upSession, uint64_t diff);
  void sendMiningDifficulty(uint16_t sessionId, uint64_t diff);
  void sendSubmitResponse(const SubmitId &id, int status);

  UpStratumClient *findUpSession();

  void registerWorker  (UpStratumClient *upSession);
  void registerWorker  (StratumSession *downSession);
  void unRegisterWorker(StratumSession *downSession);
  
  bool run();
  void stop();
};


///////////////////////////////// UpStratumClient //////////////////////////////
enum UpStratumClientState {
  UP_INIT          = 0,
  UP_CONNECTED     = 1,
  UP_SUBSCRIBED    = 2,
  UP_AUTHENTICATED = 3
};

class UpStratumClient {
  struct evdns_base *evdnsBase_ = nullptr;
  struct bufferevent *bev_ = nullptr;
  struct evbuffer *inBuf_ = nullptr;

  bool handleMessage();
  virtual void handleStratumMessage(const string &line) = 0;
  virtual void handleExMessage(const string *exMessage);
  void handleExMessage_MiningSetDiff(const string *exMessage);
  void handleExMessage_SubmitResponse(const string *exMessage);

  void initConnection();
  void disconnect();

public:
  UpStratumClientState state_ = UP_INIT;
  int8_t idx_ = 0;
  uint8_t poolIndex_ = 0;
  uint8_t reconnectCount_ = 0;
  StratumServer *server_ = nullptr;

  bool submitResponseFromServer_ = false;
  uint16_t submitIndex_ = 0;
  vector<SubmitId> submitIds_;

  bool poolDiffNeedUpdate_ = true;
  uint64_t poolDefaultDiff_ = 0;
  uint32_t extraNonce1_ = 0;  // session ID

  string userName_;

  // last stratum job received from pool
  uint32_t lastJobReceivedTime_ = 0;

  // The last time it tried to connect to the pool.
  // Used to control the speed of retry.
  uint32_t lastConnectTime_ = 0;

public:
  UpStratumClient(const int8_t idx,
                  StratumServer *server);
  virtual ~UpStratumClient();

  bool connect();
  bool reconnect();

  void recvData(struct evbuffer *buf);
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }
  
  // return false before authorized
  bool sendRequest(const string &str);
  bool submitShare(const string &json, uint16_t sessionId, const string &idStr);

  // means auth success and got at least stratum job
  bool isAvailable();
  inline bool isAuthenticated() { return state_ == UP_AUTHENTICATED; }
};


////////////////////////////////// StratumSession //////////////////////////////
enum StratumSessionState {
  DOWN_CONNECTED     = 0,
  DOWN_SUBSCRIBED    = 1,
  DOWN_AUTHENTICATED = 2
};

class StratumSession {
protected:
  static const int32_t kExtraNonce2Size_ = 4;

  //----------------------
  struct evbuffer *inBuf_ = nullptr;
  StratumSessionState state_ = DOWN_CONNECTED;
  string minerAgent_;
  string workerName_;

  void setReadTimeout(const int32_t timeout);

  virtual void handleStratumMessage(const string &line) = 0;

  void setWorkerName(const string &fullName);

public:
  UpStratumClient &upSession_;
  uint16_t sessionId_ = 0;
  struct bufferevent *bev_ = nullptr;
  StratumServer *server_ = nullptr;
  struct in_addr saddr_;

public:
  StratumSession(UpStratumClient & upSession, uint16_t sessionId,
                 struct bufferevent *bev, StratumServer *server,
                 struct in_addr saddr);
  virtual ~StratumSession();
  virtual void sendMiningNotify() = 0;
  virtual void sendFakeMiningNotify() = 0;
  virtual void sendMiningDifficulty(uint64_t diff) = 0;
  virtual void sendSubmitResponse(const string &idStr, int status) = 0;

  void recvData(struct evbuffer *buf);
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }

  inline const string & minerAgent() { return minerAgent_; }
  inline const string & workerName() { return workerName_; }
};

#endif
