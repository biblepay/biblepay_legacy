// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCPOG_H
#define RPCPOG_H

#include "wallet/wallet.h"
#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include "chat.h"
#include "main.h"
#include <univalue.h>

std::string RetrieveMd5(std::string s1);

struct UserVote
{
	int nTotalYesCount;
	int nTotalNoCount;
	int nTotalAbstainCount;
	int nTotalYesWeight;
	int nTotalNoWeight;
	int nTotalAbstainWeight;
};


std::string QueryBibleHashVerses(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexPrev);
CAmount GetDailyMinerEmissions(int nHeight);
std::string CreateBankrollDenominations(double nQuantity, CAmount denominationAmount, std::string& sError);
std::string DefaultRecAddress(std::string sType);
std::string GenerateNewAddress(std::string& sError, std::string sName);
CAmount SelectCoinsForTithing(const CBlockIndex* pindex);
CAmount GetTitheTotal(CTransaction tx);
bool IsTitheLegal(CTransaction ctx, CBlockIndex* pindex, CAmount tithe_amount);
void GetTxTimeAndAmount(uint256 hashInput, int hashInputOrdinal, int64_t& out_nTime, CAmount& out_caAmount);
std::string SendTithe(CAmount caTitheAmount, double dMinCoinAge, CAmount caMinCoinAmount, CAmount caMaxTitheAmount, std::string& sError);
CAmount GetTitheCap(const CBlockIndex* pindexLast);
double R2X(double var);
double Quantize(double nFloor, double nCeiling, double nValue);
CAmount Get24HourTithes(const CBlockIndex* pindexLast);
double GetPOGDifficulty(const CBlockIndex* pindex);
std::string GetActiveProposals();
bool VoteManyForGobject(std::string govobj, std::string strVoteSignal, std::string strVoteOutcome, 
	int iVotingLimit, int& nSuccessful, int& nFailed, std::string& sError);
bool AmIMasternode();
std::string CreateGovernanceCollateral(uint256 GovObjHash, CAmount caFee, std::string& sError);
int GetNextSuperblock();
std::string StoreBusinessObjectWithPK(UniValue& oBusinessObject, std::string& sError);
std::string StoreBusinessObject(UniValue& oBusinessObject, std::string& sError);
bool is_email_valid(const std::string& e);
void SendChat(CChat chat);

UniValue GetBusinessObjectList(std::string sType);
UniValue GetBusinessObjectByFieldValue(std::string sType, std::string sFieldName, std::string sSearchValue);
double GetBusinessObjectTotal(std::string sType, std::string sFieldName, int iAggregationType);
std::string GetBusinessObjectList(std::string sType, std::string sFields);
UniValue GetBusinessObject(std::string sType, std::string sPrimaryKey, std::string& sError);
double GetSporkDouble(std::string sName, double nDefault);
int64_t GetFileSize(std::string sPath);
std::string AddBlockchainMessages(std::string sAddress, std::string sType, std::string sPrimaryKey, 
	std::string sHTML, CAmount nAmount, double minCoinAge, std::string& sError);
std::string ReadCache(std::string sSection, std::string sKey);
void ClearCache(std::string sSection);
std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nMaxAge);
void WriteCache(std::string sSection, std::string sKey, std::string sValue, int64_t locktime, bool IgnoreCase=true);
void PurgeCacheAsOfExpiration(std::string sSection, int64_t nExpiration);
std::string GetSporkValue(std::string sKey);
double GetDifficultyN(const CBlockIndex* blockindex, double N);
std::string TimestampToHRDate(double dtm);
std::string GetArrayElement(std::string s, std::string delim, int iPos);
void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive);
std::string RetrieveTxOutInfo(const CBlockIndex* pindexLast, int iLookback, int iTxOffset, int ivOutOffset, int iDataType);
double GetBlockMagnitude(int nChainHeight);
uint256 PercentToBigIntBase(int iPercent);
std::string GetIPFromAddress(std::string sAddress);
bool SubmitProposalToNetwork(uint256 txidFee, int64_t nStartTime, std::string sHex, std::string& sError, std::string& out_sGovObj);
int GetLastDCSuperblockWithPayment(int nChainHeight);
std::string SubmitToIPFS(std::string sPath, std::string& sError);
std::string SendBusinessObject(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, std::string sSignKey, bool fSign, std::string& sError);
UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string sSearch, std::string& outEntry);
UserVote GetSumOfSignal(std::string sType, std::string sIPFSHash);
int GetSignalInt(std::string sLocalSignal);
void RPCSendMoneyToDestinationWithMinimumBalance(const CTxDestination& address, CAmount nValue, CAmount nMinimumBalanceRequired, double dMinCoinAge, CAmount caMinCoinValue, 
	CWalletTx& wtxNew, std::string& sError);
double GetDifficulty(const CBlockIndex* blockindex);
bool LogLimiter(int iMax1000);
std::string PubKeyToAddress(const CScript& scriptPubKey);
void RecoverOrphanedChain(int iCondition);
void RecoverOrphanedChainNew(int iCondition);
UniValue ContributionReport();
int DeserializePrayersFromFile();
void SerializePrayersToFile(int nHeight);
void UpdatePogPool(CBlockIndex* pindex, const CBlock& block);
void InitializePogPool(const CBlockIndex* pindexLast, int nSize, const CBlock& block);
std::string GetTitherAddress(CTransaction ctx, std::string& sNickName);
CAmount GetTitheAmount(CTransaction ctx);

struct TitheDifficultyParams
{
  double min_coin_age;
  CAmount min_coin_amount;
  CAmount max_tithe_amount;
};

TitheDifficultyParams GetTitheParams(const CBlockIndex* pindex);
TitheDifficultyParams GetTitheParams2(const CBlockIndex* pindex);
std::vector<char> ReadBytesAll(char const* filename);
std::string VectToString(std::vector<unsigned char> v);
CAmount StringToAmount(std::string sValue);
bool IsTitheLegal2(CTitheObject oTithe, TitheDifficultyParams tdp);
bool CompareMask(CAmount nValue, CAmount nMask);
std::string TitheErrorToString(int TitheError);
std::string GetPOGBusinessObjectList(std::string sType, std::string sFields);
bool CopyFile(std::string sSrc, std::string sDest);
CAmount R20(CAmount amount);
bool PODCEnabled(int nHeight);
bool POGEnabled(int nHeight, int64_t nTime);
#endif
