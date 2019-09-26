// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCONTRACTSERVER_H
#define SMARTCONTRACTSERVER_H

#include "wallet/wallet.h"
#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include "rpcpog.h"
#include <univalue.h>

class CWallet;

std::string AssessBlocks(int nHeight, bool fCreating);
int GetLastGSCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock);
std::string GetGSCContract(int nHeight, bool fCreating);
bool SubmitGSCTrigger(std::string sHex, std::string& gobjecthash, std::string& sError);
void GetGSCGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts);
uint256 GetPAMHashByContract(std::string sContract);
uint256 GetPAMHash(std::string sAddresses, std::string sAmounts);
bool VoteForGSCContract(int nHeight, std::string sMyContract, std::string& sError);
std::string ExecuteGenericSmartContractQuorumProcess();
UniValue GetProminenceLevels(int nHeight, std::string sFilterNickName);
bool NickNameExists(std::string sProjectName, std::string sNickName);
int GetRequiredQuorumLevel(int nHeight);
void GetTransactionPoints(CBlockIndex* pindex, CTransactionRef tx, double& nCoinAge, CAmount& nDonation);
bool ChainSynced(CBlockIndex* pindex);
std::string WatchmanOnTheWall(bool fForce, std::string& sContract);
void GetGovObjDataByPamHash(int nHeight, uint256 hPamHash, std::string& out_Data);
BiblePayProposal GetProposalByHash(uint256 govObj, int nLastSuperblock);
std::string DescribeProposal(BiblePayProposal bbpProposal);
std::string GetTxCPK(CTransactionRef tx, std::string& sCampaignName);
double CalculatePoints(std::string sCampaign, std::string sDiary, double nCoinAge, CAmount nDonation, std::string sCPK);
double GetCameroonChildBalance(std::string sChildID);
double GetProminenceCap(std::string sCampaignName, double nPoints, double nProminence);
std::string CheckGSCHealth();
std::string CheckLastQuorumPopularHash();
bool VerifyChild(std::string childID);

#endif
