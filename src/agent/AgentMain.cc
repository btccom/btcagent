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

#include "Server.h"

StratumServer *gStratumServer = NULL;

void handler(int sig) {
  if (gStratumServer) {
    gStratumServer->stop();
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tagent -c \"agent_conf.json\"\n");
}

int main(int argc, char **argv) {
  char *optConf   = NULL;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:h")) != -1) {
    switch (c) {
      case 'c':
        optConf = optarg;
        break;
      case 'h': default:
        usage();
        exit(0);
    }
  }

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  try {
    string listenIP, listenPort;
    std::vector<PoolConf> poolConfs;

    // get conf json string
    std::ifstream agentConf(optConf);
    string agentJsonStr((std::istreambuf_iterator<char>(agentConf)),
                        std::istreambuf_iterator<char>());
    if (!parseConfJson(agentJsonStr, listenIP, listenPort, poolConfs)) {
      LOG(ERROR) << "parse json config file failure";
      return false;
    }

    gStratumServer = new StratumServer(listenIP, atoi(listenPort.c_str()));

    // add pools
    for (size_t i = 0; i < poolConfs.size(); i++) {
      gStratumServer->addUpPool(poolConfs[i].host_,
                                poolConfs[i].port_,
                                poolConfs[i].upPoolUserName_);
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

  return 0;
}
