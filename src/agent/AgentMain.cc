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
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

#include <fstream>
#include <streambuf>

#include <glog/logging.h>

#include "Server.h"

StratumServer *gStratumServer = nullptr;

void handler(int sig) {
  if (gStratumServer) {
    gStratumServer->stop();
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tagent -c \"agent_conf.json\" -l \"log_dir\"\n");
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf   = NULL;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:l:h")) != -1) {
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

  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  FLAGS_log_dir = string(optLogDir);
  FLAGS_max_log_size = 100;  // max log file size 1 MB
  FLAGS_logbuflevel = -1;
  FLAGS_stop_logging_if_full_disk = true;

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  try {
    JsonNode j;  // conf json
    // parse xxxx.json
    std::ifstream agentConf(optConf);
    string agentJsonStr((std::istreambuf_iterator<char>(agentConf)),
                        std::istreambuf_iterator<char>());
    if (!JsonNode::parse(agentJsonStr.c_str(),
                         agentJsonStr.c_str() + agentJsonStr.length(), j)) {
      LOG(ERROR) << "json decode failure";
      exit(EXIT_FAILURE);
    }

    gStratumServer = new StratumServer(j["agent_listen_ip"].str(),
                                       j["agent_listen_port"].uint16());

    // add pools
    {
      auto pools = j["pools"].array();
      for (size_t i = 0; i < pools.size(); i++) {
        auto poolParams = pools[i].array();
        gStratumServer->addUpPool(poolParams[0].str(),
                                  poolParams[1].uint16(),
                                  poolParams[2].str());
    	}
    }

    if (!gStratumServer->setup()) {
      LOG(ERROR) << "setup failure";
    } else {
      gStratumServer->run();
    }
    delete gStratumServer;
  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}
