// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCONTRACTCLIENT_H
#define SMARTCONTRACTCLIENT_H

#include "wallet/wallet.h"
#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include <univalue.h>

class CWallet;

UniValue GetCampaigns();
bool CheckCampaign(std::string sName);
bool CreateClientSideTransaction();

#endif
