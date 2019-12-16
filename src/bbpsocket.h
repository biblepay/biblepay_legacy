// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BBPSOCKET_H
#define BBPSOCKET_H

#include <univalue.h>
#include "util.h"
#include "clientversion.h"
#include "rpcpog.h"
#include "netmessagemaker.h"
#include "activemasternode.h"

std::string BBPPost(std::string sHost, std::string sService, std::string sPage, std::string sPayload, int iTimeout);
std::string PrepareHTTPPost(bool bPost, std::string sPage, std::string sHostHeader, const std::string& sMsg, const std::map<std::string,std::string>& mapRequestHeaders);

#endif
