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
#pragma once

#include <string>
#include <openssl/ssl.h>

void init_ssl_locking();
std::string get_ssl_err_string();

SSL_CTX *get_client_SSL_CTX();
SSL_CTX *get_client_SSL_CTX_With_Cache();

SSL_CTX *
get_server_SSL_CTX(const std::string &certFile, const std::string &keyFile);
