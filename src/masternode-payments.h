// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include "util.h"
#include "core_io.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "utilstrencodings.h"

class CMasternodePayments;
class CMasternodePaymentVote;
class CMasternodeBlockPayees;
class CDistributedComputingVote;

static const int MNPAYMENTS_SIGNATURES_REQUIRED         = 6;
static const int MNPAYMENTS_SIGNATURES_TOTAL            = 10;

//! minimum peer version that can receive and send masternode payment messages,
//  vote for masternode and be elected as a payment winner
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION_1 = 70206;
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION_2 = 70206;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapMasternodeBlocks;
extern CCriticalSection cs_mapMasternodePayeeVotes;

extern CMasternodePayments mnpayments;

/// TO DO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward, int64_t nBlockTime);
void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount competetiveMiningTithe, CTxOut& txoutMasternodeRet, std::vector<CTxOut>& voutSuperblockRet);
std::string GetRequiredPaymentsString(int nBlockHeight);

class CMasternodePayee
{
private:
    CScript scriptPubKey;
    std::vector<uint256> vecVoteHashes;
    CTxIn vinMasternode;

public:
    CMasternodePayee() :
        scriptPubKey(),
        vecVoteHashes(),
	    vinMasternode()
        {}

    CMasternodePayee(CScript payee, uint256 hashIn, CTxIn vinMN) :
        scriptPubKey(payee),
		vecVoteHashes(),
		vinMasternode(vinMN)
		{
			vecVoteHashes.push_back(hashIn);
		}
        

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) 
	{
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
		READWRITE(vinMasternode);
    }

    CScript GetPayee() { return scriptPubKey; }
	CTxIn   GetVin()   { return vinMasternode;}
    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
    int GetVoteCount() { return vecVoteHashes.size(); }
};

// Keep track of votes for payees from masternodes
class CMasternodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CMasternodePayee> vecPayees;

    CMasternodeBlockPayees() :
        nBlockHeight(0),
        vecPayees()
        {}
    CMasternodeBlockPayees(int nBlockHeightIn) :
        nBlockHeight(nBlockHeightIn),
        vecPayees()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CMasternodePaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet, CAmount& nCollateral, std::string& sScript);

    bool HasPayeeWithVotes(CScript payeeIn, int nVotesReq);
	CAmount GetTxSanctuaryCollateral(const CTransaction& txNew);

    bool IsTransactionValid(const CTransaction& txNew);

    std::string GetRequiredPaymentsString();
};







// vote for the winning distributed computing credit contract
class CDistributedComputingVote
{
public:
    CTxIn vinMasternode;
	int nHeight;
    uint256 contractHash;
    CScript payee;
    std::vector<unsigned char> vchSig;
	std::string contract;
	CPubKey pubKeyMasternode;

    CDistributedComputingVote() :
        vinMasternode(),
		nHeight(0),
        contractHash(uint256S("0x0")),
        payee(),
        vchSig(),
		contract(""),
		pubKeyMasternode()
        {}

    CDistributedComputingVote(CTxIn vinMasternode, int iHeight, uint256 uContractHash, CScript payee, std::string sContract, CPubKey pkMaster) :
        vinMasternode(vinMasternode),
		nHeight(iHeight),
        contractHash(uContractHash),
        payee(payee),
        vchSig(),
		contract(sContract),
		pubKeyMasternode(pkMaster)
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) 
	{
        READWRITE(vinMasternode);
		READWRITE(nHeight);
        READWRITE(contractHash);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
		READWRITE(contract);
		READWRITE(pubKeyMasternode);
    }

    uint256 GetHash() const 
	{
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    	ss << nHeight;
        ss << contractHash;
        ss << vinMasternode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyMasternode, uint256 contractHash, int &nDos);

    bool IsValid(CNode* pnode, uint256 contractHash, std::string& strError);
    void Relay();

    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};




// vote for the winning payment
class CMasternodePaymentVote
{
public:
    CTxIn vinMasternode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CMasternodePaymentVote() :
        vinMasternode(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CMasternodePaymentVote(CTxIn vinMasternode, int nBlockHeight, CScript payee) :
        vinMasternode(vinMasternode),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinMasternode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinMasternode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyMasternode, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError);
    void Relay();

    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
    // masternode count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

public:
    std::map<uint256, CMasternodePaymentVote> mapMasternodePaymentVotes;
	std::map<uint256, CDistributedComputingVote> mapDistributedComputingVotes;
    std::map<int, CMasternodeBlockPayees> mapMasternodeBlocks;
    std::map<COutPoint, int> mapMasternodesLastVote;

    CMasternodePayments() : nStorageCoeff(1.25), nMinBlocksToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) 
	{
        READWRITE(mapMasternodePaymentVotes);
        READWRITE(mapMasternodeBlocks);
		READWRITE(mapDistributedComputingVotes);
    }

    void Clear();

    bool AddPaymentVote(const CMasternodePaymentVote& vote);
    bool HasVerifiedPaymentVote(uint256 hashIn);
	bool HasVerifiedDistributedComputingVote(uint256 hashIn);
	std::string SerializeSanctuaryQuorumSignatures(int nHeight, uint256 hashIn);
	bool ProcessBlock(int nBlockHeight);
	//bool AddDistributedComputingVote(const CDistributedComputingVote& vote);
	void Sync(CNode* node);
    void RequestLowDataPaymentBlocks(CNode* pnode);
    void CheckAndRemove();
	bool GetBlockPayeeAndCollateral(int nBlockHeight, CScript& payee, CAmount& nCollateral, std::string& sCollateralScript);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CMasternode& mn, int nNotBlockHeight);
    bool CanVote(COutPoint outMasternode, int nBlockHeight);
    int GetMinMasternodePaymentsProto();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount caCompetitiveMiningTithe, CTxOut& txoutMasternodeRet);
    std::string ToString() const;

    int GetBlockCount() { return mapMasternodeBlocks.size(); }
    int GetVoteCount() { return mapMasternodePaymentVotes.size(); }
    bool IsEnoughData();
    int GetStorageLimit();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
