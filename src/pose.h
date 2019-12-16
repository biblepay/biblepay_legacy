// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef POSE_H
#define POSE_H

#include <univalue.h>
#include "util.h"
#include "clientversion.h"
#include "rpcpog.h"
#include "netmessagemaker.h"
#include "activemasternode.h"
#include "evo/deterministicmns.h"

struct POSEScore
{
	int nTries = 0;
	int nSuccess = 0;
	int nFail = 0;
	int64_t nLastTried = 0;
	double nScore = 0;
};

double GetSancScore(std::string sNode);
POSEScore GetPOSEScore(std::string sNode);

#endif
