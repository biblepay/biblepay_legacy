// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chat.h"
#include "base58.h"
#include "clientversion.h"
#include "net.h"
#include "pubkey.h"
#include "timedata.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include <stdint.h>
#include <algorithm>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>

using namespace std;

map<uint256, CChat> mapChats;
CCriticalSection cs_mapChats;

void CChat::SetNull()
{
    nVersion = 1;
    nTime = 0;
    nID = 0;
	bPrivate = false;
    nPriority = 0;
	sPayload.clear();
    sFromNickName.clear();
    sToNickName.clear();
	sDestination.clear();
}

std::string CChat::ToString() const
{
    return strprintf(
        "CChat(\n"
        "    nVersion     = %d\n"
        "    nTime        = %d\n"
        "    nID          = %d\n"
        "    bPrivate     = %d\n"
        "    nPriority    = %d\n"
		"    sDestination = %s\n"
        "    sPayload     = \"%s\"\n"
	    "    sFromNickName= \"%s\"\n"
        "    sToNickName  = \"%s\"\n"
        ")\n",
        nVersion,
        nTime,
        nID,
        bPrivate,
		nPriority,
		sDestination,
        sPayload,
        sFromNickName,
        sToNickName);
}


bool CChat::IsNull() const
{
    return (nTime == 0);
}

uint256 CChat::GetHash() const
{
	CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << nTime;
	ss << nPriority;	
	ss << sPayload;
	ss << sFromNickName;
	ss << sToNickName;
	ss << sDestination;
    return ss.GetHash();

}

bool CChat::RelayTo(CNode* pnode) const
{
    if (pnode->nVersion == 0) return false;

    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
		pnode->PushMessage(NetMsgType::CHAT, *this);
            return true;
    }
    return false;
}

CChat CChat::getChatByHash(const uint256 &hash)
{
    CChat retval;
    {
        LOCK(cs_mapChats);
        map<uint256, CChat>::iterator mi = mapChats.find(hash);
        if(mi != mapChats.end())
            retval = mi->second;
    }
    return retval;
}

bool CChat::ProcessChat()
{
       

//        mapChats.insert(make_pair(GetHash(), *this));
        // Notify UI and -tnotify if it applies to me
    //        uiInterface.NotifyChatChanged(GetHash(), CT_NEW);
     //       Notify(strStatusBar, fThread);
       

    LogPrintf("\n Accepted chat %f %s dest %s ", nID, sPayload.c_str(), sDestination.c_str());
    return true;
}

