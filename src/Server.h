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

#include <iostream>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include <bitset>
#include <map>
#include <set>
#include <unordered_map>
#include <deque>

#define CMD_MAGIC_NUMBER      0x7Fu
// types
#define CMD_REGISTER_WORKER   0x01u             // Agent -> Pool
#define CMD_SUBMIT_SHARE      0x02u             // Agent -> Pool, without block time
#define CMD_SUBMIT_SHARE_WITH_TIME  0x03u       // Agent -> Pool
#define CMD_UNREGISTER_WORKER 0x04u             // Agent -> Pool
#define CMD_MINING_SET_DIFF   0x05u             // Pool  -> Agent

// agent, DO NOT CHANGE
#define AGENT_MAX_SESSION_ID   0xFFFEu  // 0xFFFEu = 65534

// default worker name
#define DEFAULT_WORKER_NAME    "__default__"

class StratumSession;
class StratumServer;
class UpStratumClient;


//////////////////////////////// StratumError ////////////////////////////////
class StratumError {

// Win32 #define NO_ERROR as well. There is the same value, so #undef NO_ERROR first.
#ifdef _WIN32
 #undef NO_ERROR
#endif

public:
  enum {
    NO_ERROR        = 0,

    UNKNOWN         = 20,
    JOB_NOT_FOUND   = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY  = 23,
    UNAUTHORIZED    = 24,
    NOT_SUBSCRIBED  = 25,

    ILLEGAL_METHOD   = 26,
    ILLEGAL_PARARMS  = 27,
    IP_BANNED        = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR   = 30,
    TIME_TOO_OLD     = 31,
    TIME_TOO_NEW     = 32
  };
  static const char * toString(int err);
};


//////////////////////////////// SessionIDManager //////////////////////////////
class SessionIDManager {
  std::bitset<AGENT_MAX_SESSION_ID + 1> sessionIds_;
  int32_t count_;
  uint32_t allocIdx_;

public:
  SessionIDManager();

  bool ifFull();
  bool allocSessionId(uint16_t *sessionId);  // range: [0, AGENT_MAX_SESSION_ID]
  void freeSessionId(const uint16_t sessionId);
};


//////////////////////////////////// Share /////////////////////////////////////
class Share {
public:
  uint32_t jobId_;
  uint32_t time_;
  uint32_t extraNonce2_;
  uint32_t nonce_;

  Share(): jobId_(0), time_(0), extraNonce2_(0), nonce_(0) {}
  Share(const Share &r) {
    jobId_       = r.jobId_;
    time_        = r.time_;
    extraNonce2_ = r.extraNonce2_;
    nonce_       = r.nonce_;
  }
};


///////////////////////////////// StratumJob ///////////////////////////////////
class StratumJob {
public:
  uint32_t jobId_;
  string prevHash_;
  int32_t  version_;
  uint32_t time_;      // block time or stratum job time
  bool  isClean_;

  StratumJob(): jobId_(0), version_(0), time_(0), isClean_(false) {}
  StratumJob(const StratumJob &r) {
    jobId_    = r.jobId_;
    prevHash_ = r.prevHash_;
    version_  = r.version_;
    time_     = r.time_;
    isClean_  = r.isClean_;
  }
};


///////////////////////////////// StratumMessage ///////////////////////////////
class StratumMessage {
  string content_;

  string method_;      // if it's invalid json string, you can't get the method
  string id_;          // "id"
  bool   isStringId_;  // "id" is string or not

  // json
  jsmntok_t t_[64];    // we expect no more than 64 tokens
  int r_;

  Share      share_;    // mining.submit
  StratumJob sjob_;     // mining.notify
  string minerAgent_;   // mining.subscribe
  string workerName_;   // mining.authorize
  string password_;
  uint32_t diff_;       // mining.set_difficulty

  string getJsonStr(const jsmntok_t *t) const;
  int jsoneq(const jsmntok_t *tok, const char *s) const;

  string findMethod() const;
  string parseId();
  void parse();

  void _parseMiningSubmit();
  void _parseMiningNotify();
  void _parseMiningSetDifficulty();
  void _parseMiningSubscribe();
  void _parseMiningAuthorize();

public:
  StratumMessage(const string &content);
  ~StratumMessage();

  bool isValid() const;
  string getMethod() const;
  bool getResultBoolean() const;
  string getId() const;
  bool isStringId() const;

  bool parseMiningSubmit(Share &share) const;
  bool parseMiningSubscribe(string &minerAgent) const;
  bool parseMiningAuthorize(string &workerName) const;
  bool parseMiningNotify(StratumJob &sjob) const;
  bool parseMiningSetDifficulty(uint32_t *diff) const;

  bool getExtraNonce1AndExtraNonce2Size(uint32_t *nonce1, int32_t *n2size) const;
};


/////////////////////////////////// StratumServer //////////////////////////////
class StratumServer {
  //
  // if you use tcp socket for a long time, over than 24 hours at WAN network,
  // you will find it's always get error and disconnect sometime. so we use
  // multi-tcp connections to connect to the pool. if one of them got
  // disconnect, just some miners which belong to this connection(UpStratumClient)
  // will reconnect instead of all miners reconnect to the Agent.
  //
  bool running_;

  string   listenIP_;
  uint16_t listenPort_;

  vector<string>   upPoolHost_;
  vector<uint16_t> upPoolPort_;

  // up stream connnections
  vector<UpStratumClient *> upSessions_;
  vector<int32_t> upSessionCount_;
  struct event *upEvTimer_;

    // down stream connections
  vector<StratumSession *> downSessions_;

  // libevent2
  struct event_base *base_;
  struct event *signal_event_;
  struct evconnlistener *listener_;

  void checkUpSessions();
  void waitUtilAllUpSessionsAvailable();

public:
  SessionIDManager sessionIDManager_;
  SessionIDManager upSessionIDManager_;
  std::unordered_map<int8_t , string> upUsers_;
  int8_t  maxUpSessionCount_ = 0;
  std::unordered_map<string , int8_t> usersUp_;
  static const int8_t kUpSessionCount_ = 5;  // MAX is 127

public:
  StratumServer(const string &listenIP, const uint16_t listenPort);
  ~StratumServer();

  UpStratumClient *createUpSession(const int8_t idx);
  void addUpPool(const string &host, const uint16_t port);

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

  static void upWatcherCallback(evutil_socket_t fd, short events, void *ptr);
  static void upSesssionCheckCallback(evutil_socket_t fd, short events, void *ptr);

  void sendMiningNotifyToAll(const int8_t idx, const string &notify);
  void sendMiningNotify(StratumSession *downSession);
  void sendDefaultMiningDifficulty(StratumSession *downSession);
  void sendMiningDifficulty(UpStratumClient *upconn,
                            uint16_t sessionId, uint64_t diff);

  int8_t findUpSessionIdx(const string &workName);

  void submitShare(const Share &share, StratumSession *downSession);
  void registerWorker  (StratumSession *downSession, const char *minerAgent,
                        const string &workerName);
  void unRegisterWorker(StratumSession *downSession);

  bool setup();
  bool setupUpStratumSessions(const string &workerName);
  void run();
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
  struct bufferevent *bev_;
  struct evbuffer *inBuf_;
  uint64_t extraNonce2_;


  bool handleMessage();
  void handleStratumMessage(const string &line);
  void handleExMessage_MiningSetDiff(const string *exMessage);

  void convertMiningNotifyStr(const string &line);

public:
  UpStratumClientState state_;
  int8_t idx_;
  StratumServer *server_;

  uint32_t poolDefaultDiff_;
  uint32_t extraNonce1_;  // session ID

  string   latestMiningNotifyStr_;
  // latest there job Id & time, use to check if send nTime
  uint8_t  latestJobId_[3];
  uint32_t latestJobGbtTime_[3];

  // last stratum job received from pool
  uint32_t lastJobReceivedTime_;

  vector<StratumSession *> upDownSessions_;
  string userName_;
public:
  UpStratumClient(const int8_t idx,
                  struct event_base *base, const string &userName,
                  StratumServer *server);
  ~UpStratumClient();

  bool connect(struct sockaddr_in &sin);

  void recvData(struct evbuffer *buf);
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }

  void sendMiningNotify(const string &line);

  // means auth success and got at least stratum job
  bool isAvailable();

  void submitShare();
  void submitWorkerInfo();
};


////////////////////////////////// StratumSession //////////////////////////////
enum StratumSessionState {
  DOWN_CONNECTED     = 0,
  DOWN_SUBSCRIBED    = 1,
  DOWN_AUTHENTICATED = 2
};

class StratumSession {
  //----------------------
  struct evbuffer *inBuf_;
  static const int32_t kExtraNonce2Size_ = 4;

  void setReadTimeout(const int32_t timeout);

  void handleStratumMessage(const string &line);

  void handleRequest(const string &idStr, const StratumMessage &smsg);
  void handleRequest_Subscribe(const string &idStr, const StratumMessage &smsg);
  void handleRequest_Authorize(const string &idStr, const StratumMessage &smsg);
  void handleRequest_Submit   (const string &idStr, const StratumMessage &smsg);

  void responseError(const string &idStr, int code);
  void responseTrue(const string &idStr);

public:
  int8_t  upSessionIdx_;
  uint16_t sessionId_;
  struct bufferevent *bev_;
  StratumServer *server_;
  string workerName_;
  string userName_;
  StratumSessionState state_;
  char *minerAgent_;


public:
  StratumSession(const int8_t upSessionIdx, const uint16_t sessionId,
                 struct bufferevent *bev, StratumServer *server);
  ~StratumSession();

  void recvData(struct evbuffer *buf);
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }
};

#endif
