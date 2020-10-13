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
#include <signal.h>

#include <fstream>
#include <streambuf>
#include "Utils.h"

#include "bitcoin/ServerBitcoin.h"
#include "eth/ServerEth.h"

#ifdef _WIN32
 #include "win32/getopt/getopt.h"
#endif

StratumServer *gStratumServer = NULL;

void handler(int sig) {
  if (gStratumServer) {
    gStratumServer->stop();
  }
}

void usage() {
#if defined(SUPPORT_GLOG)
  fprintf(stderr, "Usage:\n\tagent -c \"agent_conf.json\" -l \"log_dir\"\n");
#else
  fprintf(stderr, "Usage:\n\tagent -c \"agent_conf.json\"\n");
#endif
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf   = NULL;
  int c;

#if defined(SUPPORT_GLOG)
  while ((c = getopt(argc, argv, "c:l:h")) != -1) {
#else
  while ((c = getopt(argc, argv, "c:h")) != -1) {
#endif
    switch (c) {
      case 'c':
        optConf = optarg;
        break;
      case 'l':
        optLogDir = optarg;
        break;
      case 'h': default:
        usage();
        exit(0);
    }
  }

  if (optConf == NULL) {
    usage();
    return 1;
  }

#if defined(SUPPORT_GLOG)
  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  if (optLogDir == NULL || strcmp(optLogDir, "stderr") == 0) {
    FLAGS_logtostderr = 1;
  } else {
    FLAGS_log_dir = string(optLogDir);
  }
  FLAGS_max_log_size = 10;  // max log file size 10 MB
  FLAGS_logbuflevel = -1;
  FLAGS_stop_logging_if_full_disk = true;

 #if defined(GLOG_TO_STDOUT)
  // Print logs to stdout with glog
  GLogToStdout glogToStdout;
  google::AddLogSink(&glogToStdout);
 #endif

#endif

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

// Windows will not trigger SIGPIPE when 
// sending data to a disconnected socket.
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  try {
    AgentConf conf;

    // get conf json string
    std::ifstream confStream(optConf);
    string agentJsonStr((std::istreambuf_iterator<char>(confStream)),
                        std::istreambuf_iterator<char>());
    if (!parseConfJson(agentJsonStr, conf)) {
      LOG(ERROR) << "parse json config file failure" << std::endl;
      return 1;
    }

    LOG(INFO) << "[OPTION] Always keep down connections even if pool disconnected: "
              << (conf.alwaysKeepDownconn_ ? "Enabled" : "Disabled");

    if (conf.agentType_ == "eth") {
      gStratumServer = new StratumServerEth(conf);
    } else {
      LOG(INFO) << "[OPTION] Disconnect if a miner lost its AsicBoost mid-way: "
                << (conf.disconnectWhenLostAsicBoost_ ? "Enabled" : "Disabled");

      gStratumServer = new StratumServerBitcoin(conf);
    }

    if (!gStratumServer->run()) {
      LOG(ERROR) << "setup failure" << std::endl;
    }
    delete gStratumServer;
  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what() << std::endl;
    return 1;
  }

#if defined(SUPPORT_GLOG)
  google::ShutdownGoogleLogging();
#endif
  return 0;
}
