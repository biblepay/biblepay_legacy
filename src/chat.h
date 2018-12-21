// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAT_H
#define BITCOIN_CHAT_H

#include "serialize.h"
#include "sync.h"

#include <map>
#include <set>
#include <stdint.h>
#include <string>

class CChat;
class CNode;
class uint256;
class WalletModel;

extern std::map<uint256, CChat> mapChats;
extern CCriticalSection cs_mapChats;

/** A chat object allows a Public or Private communication payload to be sent from One to Many nodes and from the node to the UI chat channel.
    BiblePay uses the biblepay.conf nickname key to set the local users nickname.
	A new chat is started in either Private or Public mode.  Public mode means many users may enter the chat.  Private means the chat is 1:1.
	Although the Private chat allows PM from user->user, it is not yet encrypted.  RSA key exchange and truly private encrypted chat is coming soon.
	Payload format:  sent_time, public/private indicator, sending nickname, destination room name, payload
	Note:  A payload containing a ::PING will ring the end user to wake the user up and accept the chat.  If the user responds with a ::PONG the PM chat will begin.
 */
class CChat
{
public:
	int nType;
    int nVersion;
    int64_t nTime;
    int nID;
	bool bPrivate;
    int nPriority;  // 1-5, 5 meaning critical, 1 meaning unimportant
	std::string sPayload;
	std::string sFromNickName;
	std::string sToNickName;
	std::string sDestination;
	void setWalletModel(WalletModel *walletModel);
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) 
	{
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(nTime);
        READWRITE(nID);
        READWRITE(bPrivate);
        READWRITE(nPriority);
        READWRITE(LIMITED_STRING(sPayload, 65536));
        READWRITE(LIMITED_STRING(sFromNickName, 256));
        READWRITE(LIMITED_STRING(sToNickName, 256));
		READWRITE(LIMITED_STRING(sDestination, 256));
    }

    void SetNull();
    bool IsNull() const;
	void Deserialize(std::string sData);

    std::string ToString() const;
	std::string Serialized() const;

    CChat()
    {
        SetNull();
    }

	CChat(std::string sSerialized)
	{
		SetNull();
        Deserialize(sSerialized);
	}

    uint256 GetHash() const;
    bool RelayTo(CNode* pnode) const;
    bool ProcessChat(); 
    static void Notify(const std::string& strMessage, bool fThread);

    /*
     * Get copy of (active) chat by hash. Returns a null chat obj if it is not found.
     */
    static CChat getChatByHash(const uint256 &hash);
};

#endif // BITCOIN_CHAT_H
