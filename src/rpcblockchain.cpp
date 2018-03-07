// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "coins.h"
#include "consensus/validation.h"
#include "main.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpcserver.h"
#include "streams.h"
#include "sync.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "base58.h"
#include "darksend.h"
#include "wallet/wallet.h"
#include "wallet/rpcwallet.cpp"
#include "masternode-payments.h"

#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "masternode-sync.h"

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()

#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>

#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>


using namespace std;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
std::string BiblepayHttpPost(int iThreadID, std::string sActionName, std::string sDistinctUser, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, std::string sSolution);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
std::string DefaultRecAddress(std::string sType);
std::string PubKeyToAddress(const CScript& scriptPubKey);
std::string GetTxNews(uint256 hash, std::string& sHeadline);
std::vector<std::string> Split(std::string s, std::string delim);
bool CheckMessageSignature(std::string sMsg, std::string sSig);
bool CheckMessageSignature(std::string sMsg, std::string sSig, std::string sPubKey);
extern uint256 PercentToBigIntBase(int iPercent);
std::string GetTemplePrivKey();
extern UniValue GetLeaderboard(int nHeight);
extern double MyPercentile(int nHeight);

std::string SignMessage(std::string sMsg, std::string sPrivateKey);
extern double CAmountToRetirementDouble(CAmount Amount);
void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive);
std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);
std::string PrepareHTTPPost(std::string sPage, std::string sHostHeader, const string& sMsg, const map<string,string>& mapRequestHeaders);
std::string GetDomainFromURL(std::string sURL);
std::string BiblepayHTTPSPost(int iThreadID, std::string sActionName, std::string sDistinctUser, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort,
	std::string sSolution, int iTimeoutSecs, int iMaxSize, int iBreakOnError = 0);
extern CTransaction CreateCoinStake(CBlockIndex* pindexLast, CScript scriptCoinstakeKey, CAmount nTargetValue, int iMinConfirms, std::string& sXML, std::string& sError);
extern bool IsStakeSigned(std::string sXML);
extern std::string GetSANDirectory2();

extern int64_t GetStakeTargetModifierPercent(int nHeight, double nWeight);
extern double GetStakeWeight(CTransaction tx, int64_t nTipTime, std::string sXML, bool bVerifySignature, std::string& sMetrics, std::string& sError);
UniValue ContributionReport();
std::string RoundToString(double d, int place);
extern std::string TimestampToHRDate(double dtm);
void GetBookStartEnd(std::string sBook, int& iStart, int& iEnd);
extern std::string RetrieveDCCWithMaxAge(std::string cpid, int64_t iMaxSeconds);
void ClearCache(std::string section);
void WriteCache(std::string section, std::string key, std::string value, int64_t locktime);
extern std::string AddBlockchainMessages(std::string sAddress, std::string sType, std::string sPrimaryKey, std::string sHTML, CAmount nAmount, std::string& sError);


extern bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError);
std::string ReadCache(std::string sSection, std::string sKey);
extern uint256 GetDCCFileHash();
extern std::string GetDCCFileContract();
extern uint256 GetDCCHash(std::string sContract);
extern bool AdvertiseDistributedComputingKey(std::string sProjectId, std::string sAuth, std::string sCPID, int nUserId, bool fForce, std::string sUnbankedPublicKey, std::string &sError);
extern std::string AssociateDCAccount(std::string sProjectId, std::string sBoincEmail, std::string sBoincPassword, std::string sUnbankedPublicKey, bool fForce);
extern std::vector<std::string> GetListOfDCCS(std::string sSearch);
extern std::string GetSporkValue(std::string sKey);
extern int GetLastDCSuperblockWithPayment(int nChainHeight);
extern std::string GetDCCElement(std::string sData, int iElement);
std::string FindResearcherCPIDByAddress(std::string sSearch, std::string& out_address, double& nTotalMagnitude);
extern double GetUserMagnitude(double& nBudget, double& nTotalPaid, int& iLastSuperblock, std::string& out_Superblocks, int& out_SuperblockCount, int& out_HitCount, double& out_OneDayPaid, double& out_OneWeekPaid, double& out_OneDayBudget, double& out_OneWeekBudget);
std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nMaxAge);

extern bool SignCPID(std::string sCPID, std::string& sError, std::string& out_FullSig);
extern bool VerifyCPIDSignature(std::string sFullSig, bool bRequireEndToEndVerification, std::string& sError);
extern int GetCPIDCount(std::string sContract, double& nTotalMagnitude);
extern int VerifySanctuarySignatures(std::string sSignatureData);
extern double GetMagnitudeInContract(std::string sContract, std::string sCPID);
extern std::string GetBoincResearcherHexCodeAndCPID(std::string sProjectId, int nUserId, std::string& sCPID);
extern std::string ExecuteDistributedComputingSanctuaryQuorumProcess();
extern void TouchDailyMagnitudeFile();

extern int GetRequiredQuorumLevel(int iHeight);
UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string& outEntry);
uint256 BibleHash(uint256 hash, int64_t nBlockTime, int64_t nPrevBlockTime, bool bMining, int nPrevHeight, const CBlockIndex* pindexLast, bool bRequireTxIndex, bool f7000, bool f8000, bool f9000, bool fTitheBlocksActive, unsigned int nNonce);
void MemorizeBlockChainPrayers(bool fDuringConnectBlock);
std::string GetVerse(std::string sBook, int iChapter, int iVerse, int iStart, int iEnd);
std::string GetBookByName(std::string sName);
std::string GetBook(int iBookNumber);
extern std::string rPad(std::string data, int minWidth);
bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime);
extern int GetLastDCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock);
extern std::string GetElement(std::string sIn, std::string sDelimiter, int iPos);
extern bool VoteForDistributedComputingContract(int nHeight, std::string sContract, std::string sError);
std::string GetMessagesFromBlock(const CBlock& block, std::string sMessages);
std::string GetBibleHashVerses(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexprev);
double cdbl(std::string s, int place);
UniValue createrawtransaction(const UniValue& params, bool fHelp);
extern std::string NameFromURL2(std::string sURL);

extern std::string SystemCommand2(const char* cmd);
extern bool FilterFile(int iBufferSize, std::string& sError);
bool DownloadDistributedComputingFile(std::string& sError);

CBlockIndex* FindBlockByHeight(int nHeight);
extern double GetDifficulty(const CBlockIndex* blockindex);
extern double GetDifficultyN(const CBlockIndex* blockindex, double N);
extern std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, bool fSign, std::string& sError);
// void SendMoneyToDestinationWithMinimumBalance(const CTxDestination& address, CAmount nValue, CAmount nMinimumBalanceRequired, CWalletTx& wtxNew);
std::string SQL(std::string sCommand, std::string sAddress, std::string sArguments, std::string& sError);
extern void StartTradingThread();
extern CTradeTx GetOrderByHash(uint256 uHash);
extern std::string GetDCCPublicKey(const std::string& cpid);
extern std::string RelayTrade(CTradeTx& t, std::string Type);
extern std::string GetTrades();
static bool bEngineActive = false;
extern void GetDistributedComputingGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_sAddresses, std::string& out_sAmounts);
extern bool VoteForGobject(uint256 govobj, std::string& sError);
std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue);
extern uint256 GetDCPAMHash(std::string sAddresses, std::string sAmounts);
std::string GetMyPublicKeys();

extern uint256 GetDCPAMHashByContract(std::string sContract, int nHeight);
bool Contains(std::string data, std::string instring);
extern bool GetContractPaymentData(std::string sContract, int nBlockHeight, std::string& sPaymentAddresses, std::string& sAmounts);
extern double GetPaymentByCPID(std::string CPID);
extern std::string FilterBoincData(std::string sData, std::string sRootElement, std::string sEndElement, std::string sExtra);
extern bool FileExists2(std::string sPath);
std::string GetCPIDByAddress(std::string sAddress, int iOffset);
extern double GetBoincTeamByUserId(std::string sProjectId, int nUserId);
extern double GetBlockMagnitude(int nChainHeight);
extern std::string GetBoincHostsByUser(int iRosettaID, std::string sProjectId);
extern std::string GetBoincTasksByHost(int iHostID, std::string sProjectId);
extern std::string GetWorkUnitResultElement(std::string sProjectId, int nWorkUnitID, std::string sElement);
extern bool PODCUpdate(std::string& sError, bool bForce);
extern std::string SendCPIDMessage(std::string sAddress, CAmount nAmount, std::string sXML, std::string& sError);
extern bool AmIMasternode();
double VerifyTasks(std::string sTasks);
extern std::string VerifyManyWorkUnits(std::string sProjectId, std::string sTaskIds);
extern std::string ChopLast(std::string sMyChop);
extern double GetResearcherCredit(double dDRMode, double dAvgCredit, double dUTXOWeight, double dTaskWeight, double dUnbanked);
extern std::string GetBoincUnbankedReport(std::string sProjectID);
void PurgeCacheAsOfExpiration(std::string sSection, int64_t nExpiration);
extern void ClearSanctuaryMemories();
extern double GetTaskWeight(std::string sCPID);
extern double GetUTXOWeight(std::string sCPID);
extern double BoincDecayFunction(double AgeInDays, double TotalRAC);
std::string GetCPIDByRosettaID(double dRosettaID);
extern double GetSporkDouble(std::string sName, double nDefault);


double GetDifficultyN(const CBlockIndex* blockindex, double N)
{
	if (fDistributedComputingEnabled)
	{
		if (chainActive.Tip() == NULL) return 1;
		int nHeight = (blockindex == NULL) ? chainActive.Tip()->nHeight : blockindex->nHeight;
		double nBlockMagnitude = GetBlockMagnitude(nHeight);
		return nBlockMagnitude;
	}

	// Returns Difficulty * N (Most likely this will be used to display the Diff in the wallet, since the BibleHash is much harder to solve than an ASIC hash)
	if ((blockindex && !fProd && blockindex->nHeight >= 1) || (blockindex && fProd && blockindex->nHeight >= 7000))
	{
		return GetDifficulty(blockindex)*(N/10); //f7000 feature
	}
	else
	{
		return GetDifficulty(blockindex)*N;
	}
}

double BoincDecayFunction(double dAgeInSecs)
{
	// Rob Andrews - 2/26/2018 - The Boinc Decay function decays a researchers total credit by the Linear Regression constant (.69314) (divided by 7 (half of its half life) * One Exponent (^2.71) 
	// Meaning that each fortnight of non activity, the dormant RAC will half.
	// Decay Function Algorithm:  Decay = 2.71 ^ (-1 * AgeInDays * (LinearRegressionConstant(.69314) / 7))  * TotalRAC
	double LN2 = 0.69314; // Used in BOINC
	double Decay = pow(2.71, (-1 * LN2 * (dAgeInSecs) / 604800));
	return Decay;
}


uint256 PercentToBigIntBase(int iPercent)
{
	// Given a Proof-Of-Loyalty User Weight (boost level) of 0 - 90% (as a whole number), create a base hash target level for the user
	if (iPercent == 0) return uint256S("0x0");
	if (iPercent > 100) iPercent = 100;
	uint256 uRefHash1 = uint256S("0xffffffffffff");
	arith_uint256 bn1 = UintToArith256(uRefHash1);
	arith_uint256 bn2 = (bn1 * ((int64_t)iPercent)) / 100;
	uint256 h1 = ArithToUint256(bn2);
	int iBigIntLeadCount = (int)((100-iPercent) * .08);
	std::string sH1 = h1.GetHex().substr(63 - 12, 12);
	std::string sFF = std::string(63, 'F');
	std::string sZero = std::string(iBigIntLeadCount, '0');
	std::string sLeadingDiff = "00";
	std::string sConcat = (sLeadingDiff + sZero + sH1 + sFF).substr(0,64);
	uint256 uHash = uint256S("0x" + sConcat);
	return uHash;
}

/*
    std::vector<std::pair<int, CMasternode> > vecMasternodeRanks = GetMasternodeRanks(pCurrentBlockIndex->nHeight - 1, MIN_POSE_PROTO_VERSION);
    LOCK2(cs_main, cs);
    int nCount = 0;
    int nMyRank = -1;
    int nRanksTotal = (int)vecMasternodeRanks.size();
    std::vector<std::pair<int, CMasternode> >::iterator it = vecMasternodeRanks.begin();
    while(it != vecMasternodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("masternode", "CMasternodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin == activeMasternode.vin) {
            nMyRank = it->first;
            LogPrint("masternode", "CMasternodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d masternodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }
	*/

int MyRank(int nHeight)
{
    if(!fMasterNode) return 0;
	int nMinRequiredProtocol = mnpayments.GetMinMasternodePaymentsProto();
    int nRank = mnodeman.GetMasternodeRank(activeMasternode.vin, nHeight, nMinRequiredProtocol, false);
	return nRank;
}

std::string GetSporkValue(std::string sKey)
{
	boost::to_upper(sKey);
    const std::string key = "SPORK;" + sKey;
    const std::string& value = mvApplicationCache[key];
	return value;
}

std::string RetrieveDCCWithMaxAge(std::string cpid, int64_t iMaxSeconds)
{
	boost::to_upper(cpid); // CPID must be uppercase to retrieve
    const std::string key = "DCC;" + cpid;
    const std::string& value = mvApplicationCache[key];
	const Consensus::Params& consensusParams = Params().GetConsensus();
    int64_t iAge = chainActive.Tip() != NULL ? chainActive.Tip()->nTime - mvApplicationCacheTimestamp[key] : 0;
	return (iAge > iMaxSeconds) ? "" : value;
}

int64_t RetrieveCPIDAssociationTime(std::string cpid)
{
	std::string key = "DCC;" + cpid;
	return mvApplicationCacheTimestamp[key];
}

std::string RetrieveCurrentDCContract(int iCurrentHeight, int iMaximumAgeAllowed)
{
	// There may only be ONE contract per day, and it is stored by BlockNumber (determined in Superblock)
	int nNextHeight = 0;
	int nLastHeight = GetLastDCSuperblockHeight(iCurrentHeight, nNextHeight);
	std::string sContractID = RoundToString(nLastHeight,0);
	std::string sContract = RetrieveDCCWithMaxAge(sContractID, iMaximumAgeAllowed);
	return sContract;
}


int GetLastDCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock)
{
    // Compute last/next superblock
    int nLastSuperblock = 0;
    int nSuperblockStartBlock = Params().GetConsensus().nDCCSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nDCCSuperblockCycle;
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;
    if(nCurrentHeight < nFirstSuperblock)
	{
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    } 
	else 
	{
        nLastSuperblock = nCurrentHeight - (nCurrentHeight % nSuperblockCycle);
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }
	return nLastSuperblock;

}


int GetSanctuaryQuorumTimeSliceWindow()
{
	// Starting 82800 seconds after the last superblock contract, decide the timeslice window that will dictate when the chosen sanctuary should spring into action
	int nNextHeight = 0;
	int nLastSuperblockHeight = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, nNextHeight);
	CBlockIndex* pblockindex = FindBlockByHeight(nLastSuperblockHeight);
	int iLastSuperblockTime = pblockindex->nTime;
	int iRank = MyRank(nLastSuperblockHeight);
	int iWindowDurationInSeconds = 20;
	int iDurationTilEndOfSuperblock = 82800;
	int iWindowStart = iLastSuperblockTime + iDurationTilEndOfSuperblock + (iRank * iWindowDurationInSeconds);
	return iWindowStart;
}

int GetSanctuaryCount()
{
	std::vector<std::pair<int, CMasternode> > vMasternodeRanks = mnodeman.GetMasternodeRanks();
	int iSanctuaryCount = vMasternodeRanks.size();
	return iSanctuaryCount;
}

double MyPercentile(int nHeight)
{
	int iRank = MyRank(nHeight);
	int iSanctuaryCount = GetSanctuaryCount();
	//Note: We can also :  return mnodeman.CountEnabled() if we want only Enabled masternodes
	if (iSanctuaryCount < 1) return 0;
	double dPercentile = iRank / iSanctuaryCount;
	LogPrintf(" MyPercentile Rank %f, SancCount %f, Percent %f",(double)iRank,(double)iSanctuaryCount,(double)dPercentile*100);
	return dPercentile * 100;
}

bool AmIMasternode()
{
	 if (!fMasterNode) return false;
	 CMasternode mn;
     if(mnodeman.Get(activeMasternode.vin, mn)) 
	 {
         CBitcoinAddress mnAddress(mn.pubKeyCollateralAddress.GetID());
		 if (mnAddress.IsValid()) return true;
     }
	 return false;
}

int GetRequiredQuorumLevel(int nHeight)
{
	// This is an anti-ddos feature that prevents ddossing the distributed computing grid
	// REQUIREDQUORUM = X - Testnet Phase 3 change
	int iSanctuaryQuorumLevel = fProd ? .10 : .02;
	int iRequiredVotes = GetSanctuaryCount() * iSanctuaryQuorumLevel;
	if (fProd && iRequiredVotes < 3) iRequiredVotes = 3;
	if (!fProd && iRequiredVotes < 2) iRequiredVotes = 2;
	if (!fProd && nHeight < 7000) iRequiredVotes = 1; // Testnet Level 1
	return iRequiredVotes;
}

std::string GetBoincPasswordHash(std::string sProjectPassword, std::string sProjectEmail)
{
	return RetrieveMd5(sProjectPassword + sProjectEmail);
}

std::string GetBoincAuthenticator(std::string sProjectID, std::string sProjectEmail, std::string sPasswordHash)
{
	std::string sProjectURL = "https://" + GetSporkValue(sProjectID);
	std::string sRestfulURL = "lookup_account.php";
	std::string sArgs = "?email_addr=" + sProjectEmail + "&passwd_hash=" + sPasswordHash + "&get_opaque_auth=1";
	std::string sURL = sProjectURL + sRestfulURL + sArgs;
	std::string sUser = sProjectEmail;
	std::string sResponse = BiblepayHTTPSPost(0, "", sUser, "", sProjectURL, sRestfulURL + sArgs, 443, "", 20, 25000, 1);
	if (false) LogPrintf("BoincResponse %s %s \n", sProjectURL + sRestfulURL, sResponse.c_str());
	std::string sAuthenticator = ExtractXML(sResponse, "<authenticator>","</authenticator>");
	return sAuthenticator;
}

std::string GetBoincUnbankedReport(std::string sProjectID)
{
	std::string sProjectURL = "https://" + GetSporkValue(sProjectID);
	std::string sRestfulURL = "Action.aspx";
	std::string sArgs = "?action=unbanked";
	std::string sURL = sProjectURL + sRestfulURL + sArgs;
	std::string sResponse = BiblepayHTTPSPost(0, "", sProjectID, "", sProjectURL, sRestfulURL + sArgs, 443, "", 20, 25000, 1);
	std::string sUnbanked = ExtractXML(sResponse, "<UNBANKED>","</UNBANKED>");
	return sUnbanked;
}

int GetBoincResearcherUserId(std::string sProjectId, std::string sAuthenticator)
{
		std::string sProjectURL= "https://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "am_get_info.php?account_key=" + sAuthenticator;
		std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 25000, 1);
		if (false) LogPrintf("BoincResponse %s %s \n",sProjectURL + sRestfulURL, sResponse.c_str());
		int nId = cdbl(ExtractXML(sResponse, "<id>","</id>"),0);
		return nId;
}

std::string GetBoincResearcherHexCodeAndCPID(std::string sProjectId, int nUserId, std::string& sCPID)
{
	 	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "show_user.php?userid=" + RoundToString(nUserId,0) + "&format=xml";
		std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
		if (false) LogPrintf("GetBoincResearcherHexCodeAndCPID::url     %s%s       , User %f , BoincResponse %s \n",sProjectURL.c_str(), sRestfulURL.c_str(), (double)nUserId, sResponse.c_str());
		std::string sHexCode = ExtractXML(sResponse, "<url>","</url>");
		sHexCode = strReplace(sHexCode,"http://","");
		sHexCode = strReplace(sHexCode,"https://","");
		sCPID = ExtractXML(sResponse, "<cpid>","</cpid>");
		return sHexCode;
}

std::string VerifyManyWorkUnits(std::string sProjectId, std::string sTaskIds)
{
 	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "rosetta/result_status.php?ids=" + sTaskIds;
	std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 15, 275000, 2);
	return sResponse;
}

std::string GetWorkUnitResultElement(std::string sProjectId, int nWorkUnitID, std::string sElement)
{
 	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "rosetta/result_status.php?ids=" + RoundToString(nWorkUnitID, 0);
	std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 35000, 1);
	std::string sResult = ExtractXML(sResponse, "<" + sElement + ">", "</" + sElement + ">");
	return sResult;
}

double GetBoincRACByUserId(std::string sProjectId, int nUserId)
{
		std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "show_user.php?userid=" + RoundToString(nUserId,0) + "&format=xml";
		std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
		double dRac = cdbl(ExtractXML(sResponse,"<expavg_credit>", "</expavg_credit>"), 2);
		return dRac;
}


double GetBoincTeamByUserId(std::string sProjectId, int nUserId)
{
		std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "show_user.php?userid=" + RoundToString(nUserId,0) + "&format=xml";
		std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
		double dTeam = cdbl(ExtractXML(sResponse,"<teamid>", "</teamid>"), 0);
		return dTeam;
}

std::string SetBoincResearcherHexCode(std::string sProjectId, std::string sAuthCode, std::string sHexKey)
{
	 	std::string sProjectURL = "https://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "am_set_info.php?account_key=" + sAuthCode + "&url=" + sHexKey;
		std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
		if (fDebugMaster) LogPrint("boinc","SetBoincResearcherHexCode::BoincResponse %s \n",sResponse.c_str());
		std::string sHexCode = ExtractXML(sResponse, "<url>","</url>");
		return sHexCode;
}


std::string GetDCCFileContract()
{
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";
	boost::filesystem::path pathFiltered(sDailyMagnitudeFile);
    std::ifstream streamFiltered;
    streamFiltered.open(pathFiltered.string().c_str());
	if (!streamFiltered) return "";
	std::string sContract = "";
	std::string sLine = "";
    while(std::getline(streamFiltered, sLine))
    {
		sContract += sLine +"\n";
	}
	streamFiltered.close();
	return sContract;
}


int VerifySanctuarySignatures(std::string sSignatureData)
{
		std::vector<std::string> vR = Split(sSignatureData.c_str(),"<SIG>");
		int iSigners = 0;
		int iSignaturesValid = 0;
		std::string strError = "";
		for (int i = 0; i < (int)vR.size(); i++)
		{
			std::vector<std::string> vSig = Split(vR[i].c_str(),",");
			// Signature #, vin, height, signature, signedmessage, verified
			if (vSig.size() > 5)
			{
				std::string sVin = vSig[1];
				std::string sSig1 = vSig[3];
				std::string sMsg = vSig[4];
				std::string sPubKey = vSig[6];
				bool fInvalid = false;
				std::string sError = "";
				vector<unsigned char> vchSig2 = DecodeBase64(sSig1.c_str(), &fInvalid);
				CPubKey mySancPubkey(ParseHex(sPubKey));
				bool bVerified = darkSendSigner.VerifyMessage(mySancPubkey, vchSig2, sMsg, strError);
				iSigners++;
				std::string sReconstituted = sPubKey + ","+ sSig1 + ",";
				LogPrintf("recon %s ",sReconstituted.c_str());
				if (bVerified) iSignaturesValid++;
			}
		}
		LogPrintf("VerifySanctuarySignatures: Signatures %f, Sigs Valid %f ", (double)iSigners, (double)iSignaturesValid);
		return iSignaturesValid;
}

double GetMagnitudeInContract(std::string sContract, std::string sCPID)
{
	std::vector<std::string> vC = Split(sContract.c_str(),"<ROW>");
	for (int i = 0; i < (int)vC.size(); i++)
	{
		std::vector<std::string> vCPID = Split(vC[i].c_str(),",");
		if (vCPID.size() >= 3)
		{
			std::string cpid = vCPID[1];
			std::string address = vCPID[0];
			if (sCPID == cpid)
			{
				double dMag = cdbl(vCPID[2],2);
				return dMag;
			}
		}
	}
	return 0;
}

int GetCPIDCount(std::string sContract, double& nTotalMagnitude)
{
	// Address, CPID, Magnitude
	std::vector<std::string> vC = Split(sContract.c_str(),"<ROW>");
	int iCPIDCount = 0;
	for (int i = 0; i < (int)vC.size(); i++)
	{
		std::vector<std::string> vCPID = Split(vC[i].c_str(),",");
		if (vCPID.size() >= 3)
		{
			std::string cpid = vCPID[1];
			std::string address = vCPID[0];
			double dMag = cdbl(vCPID[2],2);
			nTotalMagnitude += dMag;
			if (!cpid.empty())
			{
				iCPIDCount++;
			}
		}
	}
	return iCPIDCount;
}

int GetMinimumResearcherParticipationLevel()
{
	return fProd ? 5 : 1;
}

double GetMinimumMagnitude()
{
	return fProd ? 5 : 1;
}

uint256 GetDCPAMHashByContract(std::string sContract, int nHeight)
{
	std::string sPAD = "";
	std::string sPAM = "";
	GetContractPaymentData(sContract, nHeight, sPAD, sPAM);
	if (sPAD.empty() || sPAM.empty()) return uint256S("0x0");
	uint256 uHash = GetDCPAMHash(sPAD, sPAM);
	return uHash;
}

uint256 GetDCPAMHash(std::string sAddresses, std::string sAmounts)
{
	std::string sConcat = sAddresses + sAmounts;
	if (sConcat.empty()) return uint256S("0x0");
	std::string sHash = RetrieveMd5(sConcat);
	return uint256S("0x" + sHash);
}

uint256 GetDCCHash(std::string sContract)
{
	std::string sHash = RetrieveMd5(sContract);
	// If TotalMagnitude < Min, return 0x0
	// If Researcher (CPID) Count < Min, return 0x0
	double nTotalMagnitude = 0;
	int iCPIDCount = GetCPIDCount(sContract, nTotalMagnitude);
	if (iCPIDCount < GetMinimumResearcherParticipationLevel()) return uint256S("0x0");
	if (nTotalMagnitude < GetMinimumMagnitude()) return uint256S("0x0");
	return uint256S("0x" + sHash);
}

bool VoteForDistributedComputingContract(int nHeight, std::string sMyContract, std::string sError)
{
	int iPendingVotes = 0;
	uint256 uGovObjHash;
	std::string sPaymentAddresses = "";
	std::string sAmounts = "";
	uint256 uPamHash = GetDCPAMHashByContract(sMyContract, nHeight);
	GetDistributedComputingGovObjByHeight(nHeight, uPamHash, iPendingVotes, uGovObjHash, sPaymentAddresses, sAmounts);
	// Verify Payment data matches our payment data, otherwise dont vote for it
	std::string sMyLocalPaymentAddresses = "";
	std::string sMyLocalAmounts = "";

	GetContractPaymentData(sMyContract, nHeight, sMyLocalPaymentAddresses, sMyLocalAmounts);
	if (sMyLocalPaymentAddresses.empty() || sMyLocalAmounts.empty())
	{
		sError = "Unable to vote for DC contract::Local Addresses or Amounts empty.";
		return false;
	}

	if (sPaymentAddresses.empty() || sAmounts.empty())
	{
		sError = "Ünable to vote for DC Contract::Foreign addresses or amounts empty.";
		return false;
	}

	if (sPaymentAddresses != sMyLocalPaymentAddresses)
	{
		sError = "Unable to vote for DC Contract::My local contract != foreign contract payment addresses.";
		return false;
	}

	if (sAmounts != sMyLocalAmounts)
	{
		sError = "Unable to vote for DC Contract::My local contract Amounts != foreign contract amounts.";
		return false;
	}
	bool bResult = VoteForGobject(uGovObjHash, sError);
	return bResult;
}


bool VoteForGobject(uint256 govobj, std::string& sError)
{
	    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal("funding");
        vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome("yes");
        int nSuccessful = 0;
        int nFailed = 0;
        std::vector<unsigned char> vchMasterNodeSignature;
        std::string strMasterNodeSignMessage;
        CMasternode mn;
        bool fMnFound = mnodeman.Get(activeMasternode.vin, mn);

        if(!fMnFound) {
            nFailed++;
			sError = "Can't find masternode by collateral output";
			return false;
        }

        CGovernanceVote vote(mn.vin, govobj, eVoteSignal, eVoteOutcome);
        if(!vote.Sign(activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode)) 
		{
            nFailed++;
			sError = "Failure to sign distributed computing contract.";
			return false;
        }

        CGovernanceException exception;
        if(governance.ProcessVoteAndRelay(vote, exception)) 
		{
            nSuccessful++;
			return true;
        }
        else 
		{
            nFailed++;
            sError = "Failed to Relay object " + exception.GetMessage();
			return false;
        }
		return true;
}



void GetDistributedComputingGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, 
	std::string& out_PaymentAddresses, std::string& out_PaymentAmounts)
{
	    std::string strType = "triggers";
        int nStartTime = 0; 
        LOCK2(cs_main, governance.cs);
        std::vector<CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
		int iHighVotes = -1;
        BOOST_FOREACH(CGovernanceObject* pGovObj, objs)
        {
            if(strType == "proposals" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
            if(strType == "triggers" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
            if(strType == "watchdogs" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_WATCHDOG) continue;

			UniValue obj = pGovObj->GetJSONObject();
			int nLocalHeight = obj["event_block_height"].get_int();

            if (nLocalHeight == nHeight)
			{
				
				int iVotes = pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
				std::string sPAD = obj["payment_addresses"].get_str();
				std::string sPAM = obj["payment_amounts"].get_str();
				uint256 uHash = GetDCPAMHash(sPAD, sPAM);
				
				// LogPrintf(" PAD %s, PAM %s, localheight %f , LocalPamHash %s ", sPAD.c_str(), sPAM.c_str(), (double)nLocalHeight, uHash.GetHex().c_str());

				if (uOptFilter != uint256S("0x0") && uHash != uOptFilter) continue;
				// This governance-object matches the trigger height and the optional filter
				if (iVotes > iHighVotes) 
				{
					iHighVotes = iVotes;
					
					out_PaymentAddresses = sPAD;
					out_PaymentAmounts = sPAM;
					out_nVotes = iHighVotes;
					out_uGovObjHash = pGovObj->GetHash();
				}
			}
           
        }
 }


bool SubmitDistributedComputingTrigger(std::string sHex, std::string& gobjecthash, std::string& sError)
{
	  if(!masternodeSync.IsBlockchainSynced()) 
	  {
			sError = "Must wait for client to sync with masternode network. Try again in a minute or so.";
			return false;
      }
      CMasternode mn;
      bool fMnFound = mnodeman.Get(activeMasternode.vin, mn);

      DBG( cout << "gobject: submit activeMasternode.pubKeyMasternode = " << activeMasternode.pubKeyMasternode.GetHash().ToString()
             << ", vin = " << activeMasternode.vin.prevout.ToStringShort()
             << ", params.size() = " << params.size()
             << ", fMnFound = " << fMnFound << endl; );

      uint256 txidFee;
      uint256 hashParent = uint256();
      int nRevision = 1;
      int nTime = GetAdjustedTime();
	  std::string strData = sHex;
	  CGovernanceObject govobj(hashParent, nRevision, nTime, txidFee, strData);

      DBG( cout << "gobject: submit "
             << " strData = " << govobj.GetDataAsString()
             << ", hash = " << govobj.GetHash().GetHex()
             << ", txidFee = " << txidFee.GetHex()
             << endl; );

      // Attempt to sign triggers if we are a MN
      if((govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER)) 
	  {
            if(fMnFound) 
			{
                govobj.SetMasternodeInfo(mn.vin);
                govobj.Sign(activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode);
            }
            else 
			{
                sError = "Only valid masternodes can submit this type of object";
				return false;
            }
      }

      std::string strHash = govobj.GetHash().ToString();
      if(!govobj.IsValidLocally(sError, true)) 
	  {
            LogPrintf("SubmitDistributedComputingContract::Object submission rejected because object is not valid - hash = %s, strError = %s\n", strHash, sError);
			sError += "Governance object is not valid - " + strHash;
			return false;
      }

      // RELAY THIS OBJECT 2/8/2018
	  int64_t nAge = GetAdjustedTime() - nLastDCContractSubmitted;
	  if (nAge < (60*15))
	  {
            sError = "Local Creation rate limit exceeded (0208)";
			return false;
	  }

      governance.AddSeenGovernanceObject(govobj.GetHash(), SEEN_OBJECT_IS_VALID);
      govobj.Relay();
      LogPrintf("gobject(submit) -- Adding locally created governance object - %s\n", strHash);
      bool fAddToSeen = true;
      governance.AddGovernanceObject(govobj, fAddToSeen);
	  gobjecthash = govobj.GetHash().ToString();
	  nLastDCContractSubmitted = GetAdjustedTime();

	  return true;
}

bool GetContractPaymentData(std::string sContract, int nBlockHeight, std::string& sPaymentAddresses, std::string& sAmounts)
{
	// 2-19-2018 - Proof-of-distributed-computing
	CAmount nDCPaymentsTotal = CSuperblock::GetPaymentsLimit(nBlockHeight);
	uint256 uHash = GetDCCHash(sContract);
	double nTotalMagnitude = 0;
	int iCPIDCount = GetCPIDCount(sContract, nTotalMagnitude);
	if (nTotalMagnitude < .01) 
	{
		LogPrintf(" \n ** GetContractPaymentData::SUPERBLOCK CONTAINS NO MAGNITUDE height %f (cpid count %f ), hash %s %s** \n", (double)nBlockHeight, 
			(double)iCPIDCount, uHash.GetHex().c_str(), sContract.c_str());
		return false;
	}
	if (nTotalMagnitude > 1000)
	{
		LogPrintf("\n ** GetContractPaymentData::SUPERBLOCK MAGNITUDE EXCEEDS LIMIT OF 1000 (cpid count %f )", (double)iCPIDCount);
		return false;
	}
	double dDCPaymentsTotal = nDCPaymentsTotal / COIN;
	if (dDCPaymentsTotal < 1)
	{
		LogPrintf(" \n ** GetContractPaymentData::Superblock Payment Budget is lower than 1 BBP ** \n");
		return false;
	}
	double PaymentPerMagnitude = (dDCPaymentsTotal-1) / nTotalMagnitude;
	std::vector<std::string> vRows = Split(sContract.c_str(),"<ROW>");
	double dTotalPaid = 0;
	
	for (int i = 0; i < (int)vRows.size(); i++)
	{
		std::vector<std::string> vCPID = Split(vRows[i].c_str(),",");
		if (vCPID.size() >= 3)
		{
			std::string sCpid = vCPID[1];
			std::string sAddress = vCPID[0];
			double dMagnitude = cdbl(vCPID[2],2);
			if (!sCpid.empty() && dMagnitude > 0)
			{
				double dOwed = PaymentPerMagnitude * dMagnitude;
				dTotalPaid += dOwed;
				sPaymentAddresses += sAddress + "|";
				sAmounts += RoundToString(dOwed,2) + "|";
			}
		}
	}
	if (dTotalPaid > dDCPaymentsTotal)
	{
		LogPrintf(" \n ** GetContractPaymentData::Superblock Payment Budget %f exceeds payment limit %f ** \n",dTotalPaid,dDCPaymentsTotal);
	}
	if (sPaymentAddresses.length() > 1) sPaymentAddresses=sPaymentAddresses.substr(0,sPaymentAddresses.length()-1);
	if (sAmounts.length() > 1) sAmounts=sAmounts.substr(0,sAmounts.length()-1);
	return true;

}

std::string SerializeSanctuaryQuorumTrigger(int nEventBlockHeight, std::string sContract)
{
		std::string sEventBlockHeight = RoundToString(nEventBlockHeight,0);
		std::string sPaymentAddresses = "";
		std::string sPaymentAmounts = "";
		bool bStatus = GetContractPaymentData(sContract, nEventBlockHeight, sPaymentAddresses, sPaymentAmounts);
		if (!bStatus) return "";
		std::string sProposalHashes = GetDCCHash(sContract).GetHex();
		std::string sType = "2"; //DC Trigger is always 2
		std::string sQ = "\"";
		std::string sJson = "[[" + sQ + "trigger" + sQ + ",{";
		sJson += GJE("event_block_height", sEventBlockHeight, true, false); // Must be an int
		sJson += GJE("payment_addresses", sPaymentAddresses, true, true);
		sJson += GJE("payment_amounts", sPaymentAmounts, true, true);
		sJson += GJE("proposal_hashes", sProposalHashes, true, true);
		//sJson += GJE("contract", sContract, true, true);
		sJson += GJE("type", sType, false, false); 
		sJson += "}]]";
	    std::vector<unsigned char> vchJson = vector<unsigned char>(sJson.begin(), sJson.end());
		std::string sHex = HexStr(vchJson.begin(), vchJson.end());
	    return sHex;
}

uint256 GetDCCFileHash()
{
	std::string sContract = GetDCCFileContract();
	uint256 uhash = GetDCCHash(sContract);
	return uhash;    
}

std::string GetSANDirectory2()
{
	 boost::filesystem::path pathConfigFile(GetArg("-conf", "biblepay.conf"));
     if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
	 boost::filesystem::path dir = pathConfigFile.parent_path();
	 std::string sDir = dir.string() + "/SAN/";
	 boost::filesystem::path pathSAN(sDir);
	 if (!boost::filesystem::exists(pathSAN))
	 {
		 boost::filesystem::create_directory(pathSAN);
	 }
	 return sDir;
}

int64_t GetDCCFileAge()
{
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";
	boost::filesystem::path pathFiltered(sDailyMagnitudeFile);
	// ST13 last_write_time error:  Rob A. - Biblepay - 2/8/2018
	if (!boost::filesystem::exists(pathFiltered)) return GetAdjustedTime() - 0;
	int64_t nTime = last_write_time(pathFiltered);
	int64_t nAge = GetAdjustedTime() - nTime;
	return nAge;
}


// Support for Distributed Computing - Robert A. - Biblepay - 1/31/2018 
// This is the Distributed Computing Consensus Process
// Each morning, a sanctuary goes through this process:  Do I have an active superblock?  If not, do I have an active Daily DCC Proposal?

std::string ExecuteDistributedComputingSanctuaryQuorumProcess()
{
	// If not a sanctuary, exit
	if (!fDistributedComputingEnabled) return "";
	if (fProd && chainActive.Tip()->nHeight < F11000_CUTOVER_HEIGHT_PROD) return "";
	// This happens on sanctuaries only.  The node will check to see if the contract duration expired.
	// When it expires, we must assemble a new contract as a sanctuary team.
	// Since the contract is valid for 86400 seconds, we start this process one hour early (IE 82800 seconds after the last valid contract started)
	if (!AmIMasternode()) return "NOT_A_SANCTUARY";
	if (!chainActive.Tip()) return "INVALID_CHAIN";
	std::string sContract = RetrieveCurrentDCContract(chainActive.Tip()->nHeight, 82800);
	if (!sContract.empty()) return "ACTIVE_CONTRACT";

	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	
	//  Check for Pending Contract

	int iPendingVotes = 0;
	uint256 uGovObjHash;
	std::string sAddresses = "";
	std::string sAmounts = "";
	GetDistributedComputingGovObjByHeight(iNextSuperblock, uint256S("0x0"), iPendingVotes, uGovObjHash, sAddresses, sAmounts);
	std::string sError = "";
		
	bool bPending = iPendingVotes >= GetRequiredQuorumLevel(chainActive.Tip()->nHeight);

	if (bPending) 
	{
		if (fDebugMaster) LogPrintf("We have a pending superblock at height %f \n",(double)iNextSuperblock);
		return "PENDING_SUPERBLOCK";
	}

	if ((fProd && MyPercentile(iLastSuperblock) <= 33) || (MyPercentile(iLastSuperblock) < 99 && !fProd))
	{
	    LOCK(cs_main);
		{
			// I am a chosen Sanctuary...
			int64_t nAge = GetDCCFileAge();
			uint256 uDCChash = GetDCCFileHash();
			sContract = GetDCCFileContract();
			LogPrintf(" DCC hash %s  Age %f ",uDCChash.GetHex(), (float)nAge);
			if (uDCChash == uint256S("0x0") || nAge > (60 * 60))
			{
				// Pull down the distributed computing file
				LogPrintf(" Chosen Sanctuary - pulling down the DCC file... \n");
				bool fSuccess =  DownloadDistributedComputingFile(sError);
				if (fSuccess)
				{
					LogPrintf("ExecuteDistributedSanctuaryQuorum::Error - Unable to download DC file %f ",sError.c_str());
					return "DC_DOWNLOAD_ERROR";
				}
				return "DOWNLOADING_DCC_FILE";
			}
			if (fDebugMaster) LogPrintf(" DCC hash %s ",uDCChash.GetHex());

			int iVotes = 0;
			uint256 uGovObjHash = uint256S("0x0");
			std::string sAddresses = "";
			std::string sAmounts = "";
			uint256 uPAMhash = GetDCPAMHashByContract(sContract, iNextSuperblock);
			GetDistributedComputingGovObjByHeight(iNextSuperblock, uPAMhash, iVotes, uGovObjHash, sAddresses, sAmounts);
			bool bContractExists = (uGovObjHash != uint256S("0x0"));

			if (!bContractExists)
			{
				// If this chosen sanctuary is online during this sanctuary timeslice window (IE 30 second window)
				// We are the chosen sanctuary - no contract exists - create it
				std::string sQuorumTrigger = SerializeSanctuaryQuorumTrigger(iNextSuperblock, sContract);
				std::string sGobjectHash = "";
				SubmitDistributedComputingTrigger(sQuorumTrigger, sGobjectHash, sError);
				LogPrintf(" ** DISTRIBUTEDCOMPUTING::CreatingDCContract Hex %s , Gobject %s, results %s **\n", sQuorumTrigger.c_str(), sGobjectHash.c_str(), sError.c_str());
				return "CREATING_CONTRACT";
			}

			int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);

			if (iVotes < iRequiredVotes)
			{
				bool bResult = VoteForDistributedComputingContract(iNextSuperblock, sContract, sError);
				if (bResult) return "VOTED_FOR_DC_CONTRACT";
				if (!bResult)
				{
					LogPrintf(" **unable to vote for DC contract %s ", sError.c_str());
				}
				return "UNABLE_TO_VOTE_FOR_DC_CONTRACT";
			}
			else if (iVotes >= iRequiredVotes)
			{
				LogPrintf(" DCC Contract %s has won.  Waiting for superblock. ", uDCChash.GetHex());
				return "PENDING_SUPERBLOCK";
			}
		}
		
	}

	return "NOT_A_CHOSEN_SANCTUARY";
}




double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (chainActive.Tip() == NULL)
            return 1.0;
        else
            blockindex = chainActive.Tip();
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)blockindex->nNonce));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficultyN(blockindex,10)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
	result.push_back(Pair("blockmessage", blockindex->sBlockMessage));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
	if (blockindex)
	{
		if (chainActive.Contains(blockindex))
			confirmations = chainActive.Height() - blockindex->nHeight + 1;
	}

    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
	result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
	if (block.vtx.size() > 0)
	{
		BOOST_FOREACH(const CTransaction&tx, block.vtx)
		{
			if(txDetails)
			{
				UniValue objTx(UniValue::VOBJ);
				TxToJSON(tx, uint256(), objTx);
				txs.push_back(objTx);
			}
			else
				txs.push_back(tx.GetHash().GetHex());
		}
		result.push_back(Pair("tx", txs));
	}
    result.push_back(Pair("time", block.GetBlockTime()));
    if (blockindex) 
	{
		result.push_back(Pair("height", blockindex->nHeight));
		result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
		if ((blockindex->nHeight < F11000_CUTOVER_HEIGHT_PROD && fProd) || (blockindex->nHeight < F11000_CUTOVER_HEIGHT_TESTNET && !fProd))
		{
			result.push_back(Pair("difficulty", GetDifficultyN(blockindex,10)));
		}
		else
		{
			double dPOWDifficulty = GetDifficulty(blockindex)*10;
			result.push_back(Pair("pow_difficulty", dPOWDifficulty));
			double dPODCDifficulty = GetBlockMagnitude(blockindex->nHeight);
			result.push_back(Pair("difficulty", dPODCDifficulty));
		}
		result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
	}

	result.push_back(Pair("hrtime",   TimestampToHRDate(block.GetBlockTime())));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));

	if (block.vtx.size() > 0)
	{
		result.push_back(Pair("subsidy", block.vtx[0].vout[0].nValue/COIN));
		result.push_back(Pair("blockversion", ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<VER>","</VER>")));
	}

	const Consensus::Params& consensusParams = Params().GetConsensus();
	
	if (blockindex && blockindex->pprev)
	{
		CAmount MasterNodeReward = GetBlockSubsidy(blockindex->pprev, blockindex->pprev->nBits, blockindex->pprev->nHeight, consensusParams, true);
		result.push_back(Pair("masternodereward", MasterNodeReward));
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
		std::string sVerses = GetBibleHashVerses(block.GetHash(), block.GetBlockTime(), blockindex->pprev->nTime, blockindex->pprev->nHeight, blockindex->pprev);
		result.push_back(Pair("verses", sVerses));
    	// Check work against BibleHash
		bool f7000;
		bool f8000;
		bool f9000;
		bool fTitheBlocksActive;
		GetMiningParams(blockindex->pprev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);

		arith_uint256 hashTarget = arith_uint256().SetCompact(blockindex->nBits);
		uint256 hashWork = blockindex->GetBlockHash();
		uint256 bibleHash = BibleHash(hashWork, block.GetBlockTime(), blockindex->pprev->nTime, false, blockindex->pprev->nHeight, blockindex->pprev, 
			false, f7000, f8000, f9000, fTitheBlocksActive, blockindex->nNonce);
		bool bSatisfiesBibleHash = (UintToArith256(bibleHash) <= hashTarget);
		result.push_back(Pair("blockmessage", blockindex->sBlockMessage));
    	result.push_back(Pair("satisfiesbiblehash", bSatisfiesBibleHash ? "true" : "false"));
		result.push_back(Pair("biblehash", bibleHash.GetHex()));
		// Rob A. - 02-11-2018 - Proof-of-Distributed-Computing
		if (fDistributedComputingEnabled)
		{
			std::string sCPIDSignature = ExtractXML(block.vtx[0].vout[0].sTxOutMessage, "<cpidsig>","</cpidsig>");
			std::string sCPID = GetElement(sCPIDSignature, ";", 0);
			result.push_back(Pair("CPID", sCPID));
			std::string sError = "";
			bool fCheckCPIDSignature = VerifyCPIDSignature(sCPIDSignature, true, sError);
			result.push_back(Pair("CPID_Signature", fCheckCPIDSignature));
		}
		// Proof-Of-Loyalty - 01-18-2018 - Rob A.
		if (fProofOfLoyaltyEnabled)
		{
			if (block.vtx.size() > 1)
			{
				bool bSigned = IsStakeSigned(block.vtx[0].vout[0].sTxOutMessage);
				if (bSigned)
				{
					std::string sError = "";
					std::string sMetrics = "";
					double dStakeWeight = GetStakeWeight(block.vtx[1], block.GetBlockTime(), block.vtx[0].vout[0].sTxOutMessage, true, sMetrics, sError);
					result.push_back(Pair("proof_of_loyalty_weight", dStakeWeight));
					result.push_back(Pair("proof_of_loyalty_errors", sError));
					result.push_back(Pair("proof_of_loyalty_xml", block.vtx[0].vout[0].sTxOutMessage));
					result.push_back(Pair("proof_of_loyalty_metrics", sMetrics));
					int64_t nPercent = GetStakeTargetModifierPercent(blockindex->pprev->nHeight, dStakeWeight);
					uint256 uBase = PercentToBigIntBase(nPercent);
					arith_uint256 bnTarget;
					bool fNegative;
					bool fOverflow;
     				bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
					uint256 hBlockTarget = ArithToUint256(bnTarget);
					result.push_back(Pair("block_target_hash", hBlockTarget.GetHex()));
					bnTarget += UintToArith256(uBase);
					uint256 hPOLBlockTarget = ArithToUint256(bnTarget);
					result.push_back(Pair("proof_of_loyalty_block_target", hPOLBlockTarget.GetHex()));
					result.push_back(Pair("proof_of_loyalty_influence_percentage", nPercent));
				}
			}
		}
	}
	else
	{
		if (blockindex && blockindex->nHeight==0)
		{
			int iStart=0;
			int iEnd=0;
			// Display a verse from Genesis 1:1 for The Genesis Block:
			GetBookStartEnd("gen",iStart,iEnd);
			std::string sVerse = GetVerse("gen",1,1,iStart-1,iEnd);
			boost::trim(sVerse);
			result.push_back(Pair("verses", sVerse));
    	}
	}
	if (block.vtx.size() > 0)
	{
		std::string sPrayers = GetMessagesFromBlock(block, "PRAYER");
		result.push_back(Pair("prayers", sPrayers));
	}
	if (blockindex)
	{
		CBlockIndex *pnext = chainActive.Next(blockindex);
		if (pnext)
			result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
	}
    return result;
}


UniValue showblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "showblock <index>\n"
            "Returns information about the block at <height>.");
	std::string sBlock = params[0].get_str();
	int nHeight = (int)cdbl(sBlock,0);
    if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight)
        throw runtime_error("Block number out of range.");
    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    if (pblockindex==NULL)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    CBlock block;
    const Consensus::Params& consensusParams = Params().GetConsensus();
	ReadBlockFromDisk(block, pblockindex, consensusParams, "SHOWBLOCK");
	return blockToJSON(block, pblockindex, false);
}

UniValue getblockcount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest block chain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest block chain.\n"
            "\nResult\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

UniValue getdifficulty(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetDifficultyN(NULL,10);
}

UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        BOOST_FOREACH(const CTxMemPoolEntry& e, mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            info.push_back(Pair("size", (int)e.GetTxSize()));
            info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
            info.push_back(Pair("modifiedfee", ValueFromAmount(e.GetModifiedFee())));
            info.push_back(Pair("time", e.GetTime()));
            info.push_back(Pair("height", (int)e.GetHeight()));
            info.push_back(Pair("startingpriority", e.GetPriority(e.GetHeight())));
            info.push_back(Pair("currentpriority", e.GetPriority(chainActive.Height())));
            info.push_back(Pair("descendantcount", e.GetCountWithDescendants()));
            info.push_back(Pair("descendantsize", e.GetSizeWithDescendants()));
            info.push_back(Pair("descendantfees", e.GetModFeesWithDescendants()));
            const CTransaction& tx = e.GetTx();
            set<string> setDepends;
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                if (mempool.exists(txin.prevout.hash))
                    setDepends.insert(txin.prevout.hash.ToString());
            }

            UniValue depends(UniValue::VARR);
            BOOST_FOREACH(const string& dep, setDepends)
            {
                depends.push_back(dep);
            }

            info.push_back(Pair("depends", depends));
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        BOOST_FOREACH(const uint256& hash, vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nArguments:\n"
            "1. verbose           (boolean, optional, default=false) true for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            "    \"size\" : n,             (numeric) transaction size in bytes\n"
            "    \"fee\" : n,              (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
            "    \"modifiedfee\" : n,      (numeric) transaction fee with fee deltas used for mining priority\n"
            "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
            "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
            "    \"startingpriority\" : n, (numeric) priority when transaction entered pool\n"
            "    \"currentpriority\" : n,  (numeric) transaction priority now\n"
            "    \"descendantcount\" : n,  (numeric) number of in-mempool descendant transactions (including this one)\n"
            "    \"descendantsize\" : n,   (numeric) size of in-mempool descendants (including this one)\n"
            "    \"descendantfees\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including this one)\n"
            "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
            "        \"transactionid\",    (string) parent transaction id\n"
            "       ... ]\n"
            "  }, ...\n"
            "}\n"
            "\nExamples\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    LOCK(cs_main);

    bool fVerbose = false;
    if (params.size() > 0)
        fVerbose = params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getblockhashes(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getblockhashes timestamp\n"
            "\nReturns array of hashes of blocks within the timestamp range provided.\n"
            "\nArguments:\n"
            "1. high         (numeric, required) The newer block timestamp\n"
            "2. low          (numeric, required) The older block timestamp\n"
            "\nResult:\n"
            "[\n"
            "  \"hash\"         (string) The block hash\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhashes", "1231614698 1231024505")
            + HelpExampleRpc("getblockhashes", "1231614698, 1231024505")
        );

    unsigned int high = params[0].get_int();
    unsigned int low = params[1].get_int();
    std::vector<uint256> blockHashes;

    if (!GetTimestampIndex(high, low, blockHashes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for block hashes");
    }

    UniValue result(UniValue::VARR);
    for (std::vector<uint256>::const_iterator it=blockHashes.begin(); it!=blockHashes.end(); it++) {
        result.push_back(it->GetHex());
    }

    return result;
}

UniValue getblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash index\n"
            "\nReturns hash of block in best-block-chain at index provided.\n"
            "\nArguments:\n"
            "1. index         (numeric, required) The block index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about blockheader <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the next block\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblockheaders(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "getblockheaders \"hash\" ( count verbose )\n"
            "\nReturns an array of items with information about <count> blockheaders starting from <hash>.\n"
            "\nIf verbose is false, each item is a string that is serialized, hex-encoded data for a single blockheader.\n"
            "If verbose is true, each item is an Object with information about a single blockheader.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. count           (numeric, optional, default/max=" + strprintf("%s", MAX_HEADERS_RESULTS) +")\n"
            "3. verbose         (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "[ {\n"
            "  \"hash\" : \"hash\",               (string)  The block hash\n"
            "  \"confirmations\" : n,           (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,                  (numeric) The block height or index\n"
            "  \"version\" : n,                 (numeric) The block version\n"
            "  \"merkleroot\" : \"xxxx\",         (string)  The merkle root\n"
            "  \"time\" : ttt,                  (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,            (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,                   (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\",           (string)  The bits\n"
            "  \"difficulty\" : x.xxx,          (numeric) The difficulty\n"
            "  \"previousblockhash\" : \"hash\",  (string)  The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string)  The hash of the next block\n"
            "  \"chainwork\" : \"0000...1f3\"     (string)  Expected number of hashes required to produce the current chain (in hex)\n"
            "}, {\n"
            "       ...\n"
            "   },\n"
            "...\n"
            "]\n"
            "\nResult (for verbose=false):\n"
            "[\n"
            "  \"data\",                        (string)  A string that is serialized, hex-encoded data for block header.\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheaders", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\" 2000")
            + HelpExampleRpc("getblockheaders", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\" 2000")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    int nCount = MAX_HEADERS_RESULTS;
    if (params.size() > 1)
        nCount = params[1].get_int();

    if (nCount <= 0 || nCount > (int)MAX_HEADERS_RESULTS)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Count is out of range");

    bool fVerbose = true;
    if (params.size() > 2)
        fVerbose = params[2].get_bool();

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    UniValue arrHeaders(UniValue::VARR);

    if (!fVerbose)
    {
        for (; pblockindex; pblockindex = chainActive.Next(pblockindex))
        {
            CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
            ssBlock << pblockindex->GetBlockHeader();
            std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
            arrHeaders.push_back(strHex);
            if (--nCount <= 0)
                break;
        }
        return arrHeaders;
    }

    for (; pblockindex; pblockindex = chainActive.Next(pblockindex))
    {
        arrHeaders.push_back(blockheaderToJSON(pblockindex));
        if (--nCount <= 0)
            break;
    }

    return arrHeaders;
}

UniValue getblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbose is true, returns an Object with information about block <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
            + HelpExampleRpc("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus(), "GETBLOCK"))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex);
}

UniValue gettxoutsetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
            "  \"transactions\": n,      (numeric) The number of transactions\n"
            "  \"txouts\": n,            (numeric) The number of output transactions\n"
            "  \"bytes_serialized\": n,  (numeric) The serialized size\n"
            "  \"hash_serialized\": \"hash\",   (string) The serialized hash\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (pcoinsTip->GetStats(stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("bytes_serialized", (int64_t)stats.nSerializedSize));
        ret.push_back(Pair("hash_serialized", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    }
    return ret;
}

UniValue gettxout(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout value\n"
            "3. includemempool  (boolean, optional) Whether to included the mem pool\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in " + CURRENCY_UNIT + "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of biblepay addresses\n"
            "        \"biblepayaddress\"     (string) biblepay address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"version\" : n,            (numeric) The version\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nView the details\n"
            + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("gettxout", "\"txid\", 1")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = params[1].get_int();
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    CCoins coins;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoins(hash, coins))
            return NullUniValue;
        mempool.pruneSpent(hash, coins); // TO DO: this should be done by the CCoinsViewMemPool
    } else {
        if (!pcoinsTip->GetCoins(hash, coins))
            return NullUniValue;
    }
    if (n<0 || (unsigned int)n>=coins.vout.size() || coins.vout[n].IsNull())
        return NullUniValue;

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT)
        ret.push_back(Pair("confirmations", 0));
    else
        ret.push_back(Pair("confirmations", pindex->nHeight - coins.nHeight + 1));
    ret.push_back(Pair("value", ValueFromAmount(coins.vout[n].nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("version", coins.nVersion));
    ret.push_back(Pair("coinbase", coins.fCoinBase));

    return ret;
}

UniValue verifychain(const UniValue& params, bool fHelp)
{
    int nCheckLevel = GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "verifychain ( checklevel numblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" + strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
            "2. numblocks    (numeric, optional, default=" + strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    if (params.size() > 0)
        nCheckLevel = params[0].get_int();
    if (params.size() > 1)
        nCheckDepth = params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip, nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int minVersion, CBlockIndex* pindex, int nRequired, const Consensus::Params& consensusParams)
{
    int nFound = 0;
    CBlockIndex* pstart = pindex;
    for (int i = 0; i < consensusParams.nMajorityWindow && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }

    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("status", nFound >= nRequired));
    rv.push_back(Pair("found", nFound));
    rv.push_back(Pair("required", nRequired));
    rv.push_back(Pair("window", consensusParams.nMajorityWindow));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("enforce", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams)));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityRejectBlockOutdated, consensusParams)));
    return rv;
}

static UniValue BIP9SoftForkDesc(const std::string& name, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    switch (VersionBitsTipState(consensusParams, id)) {
    case THRESHOLD_DEFINED: rv.push_back(Pair("status", "defined")); break;
    case THRESHOLD_STARTED: rv.push_back(Pair("status", "started")); break;
    case THRESHOLD_LOCKED_IN: rv.push_back(Pair("status", "locked_in")); break;
    case THRESHOLD_ACTIVE: rv.push_back(Pair("status", "active")); break;
    case THRESHOLD_FAILED: rv.push_back(Pair("status", "failed")); break;
    }
    return rv;
}

UniValue getblockchaininfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding block chain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",        (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,         (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,        (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\", (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,     (numeric) the current difficulty\n"
            "  \"mediantime\": xxxxxx,     (numeric) median time for the current best block\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"chainwork\": \"xxxx\"     (string) total amount of work in active chain, in hexadecimal\n"
            "  \"pruned\": xx,             (boolean) if the blocks are subject to pruning\n"
            "  \"pruneheight\": xxxxxx,    (numeric) heighest block available\n"
            "  \"softforks\": [            (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of softfork\n"
            "        \"version\": xx,         (numeric) block version\n"
            "        \"enforce\": {           (object) progress toward enforcing the softfork rules for new-version blocks\n"
            "           \"status\": xx,       (boolean) true if threshold reached\n"
            "           \"found\": xx,        (numeric) number of blocks with the new version found\n"
            "           \"required\": xx,     (numeric) number of blocks required to trigger\n"
            "           \"window\": xx,       (numeric) maximum size of examined window of recent blocks\n"
            "        },\n"
            "        \"reject\": { ... }      (object) progress toward rejecting pre-softfork blocks (same fields as \"enforce\")\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": [       (array) status of BIP9 softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of the softfork\n"
            "        \"status\": \"xxxx\",    (string) one of \"defined\", \"started\", \"lockedin\", \"active\", \"failed\"\n"
            "     }\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain",                 Params().NetworkIDString()));
    obj.push_back(Pair("blocks",                (int)chainActive.Height()));
    obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty",            (double)GetDifficultyN(chainActive.Tip(),10)));
    obj.push_back(Pair("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast()));
    obj.push_back(Pair("verificationprogress",  Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip())));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("pruned",                fPruneMode));

    const Consensus::Params& consensusParams = Params().GetConsensus();
    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VARR);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    bip9_softforks.push_back(BIP9SoftForkDesc("csv", consensusParams, Consensus::DEPLOYMENT_CSV));
    obj.push_back(Pair("softforks",             softforks));
    obj.push_back(Pair("bip9_softforks", bip9_softforks));

    if (fPruneMode)
    {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA))
            block = block->pprev;

        obj.push_back(Pair("pruneheight",        block->nHeight));
    }
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

UniValue getchaintips(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getchaintips ( count branchlen )\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nArguments:\n"
            "1. count       (numeric, optional) only show this much of latest tips\n"
            "2. branchlen   (numeric, optional) only show tips that have equal or greater length of branch\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,             (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",             (string) block hash of the tip\n"
            "    \"difficulty\" : x.xxx,       (numeric) The difficulty\n"
            "    \"chainwork\" : \"0000...1f3\"  (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "    \"branchlen\": 0              (numeric) zero for main chain\n"
            "    \"status\": \"active\"          (string) \"active\" for the main chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"difficulty\" : x.xxx,\n"
            "    \"chainwork\" : \"0000...1f3\"\n"
            "    \"branchlen\": 1              (numeric) length of branch connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"            (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    /* Build up a list of chain tips.  We start with the list of all
       known blocks, and successively remove blocks that appear as pprev
       of another block.  */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
        setTips.insert(item.second);
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        const CBlockIndex* pprev = item.second->pprev;
        if (pprev)
            setTips.erase(pprev);
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    int nBranchMin = -1;
    int nCountMax = INT_MAX;

    if(params.size() >= 1)
        nCountMax = params[0].get_int();

    if(params.size() == 2)
        nBranchMin = params[1].get_int();

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    BOOST_FOREACH(const CBlockIndex* block, setTips)
    {
        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        if(branchLen < nBranchMin) continue;

        if(nCountMax-- < 1) break;

        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));
        obj.push_back(Pair("difficulty", GetDifficultyN(block,10)));
        obj.push_back(Pair("chainwork", block->nChainWork.GetHex()));
        obj.push_back(Pair("branchlen", branchLen));

        string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nChainTx == 0) {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BLOCK_VALID_TREE)) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.push_back(Pair("status", status));

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t) mempool.size()));
    ret.push_back(Pair("bytes", (int64_t) mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t) mempool.DynamicMemoryUsage()));
    size_t maxmempool = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.push_back(Pair("maxmempool", (int64_t) maxmempool));
    ret.push_back(Pair("mempoolminfee", ValueFromAmount(mempool.GetMinFee(maxmempool).GetFeePerK())));

    return ret;
}


void ScanBlockChainVersion(int nLookback)
{
    mvBlockVersion.clear();
    int nMaxDepth = chainActive.Tip()->nHeight;
    int nMinDepth = (nMaxDepth - nLookback);
    if (nMinDepth < 1) nMinDepth = 1;
    CBlock block;
    CBlockIndex* pblockindex = chainActive.Tip();
 	const Consensus::Params& consensusParams = Params().GetConsensus();
    while (pblockindex->nHeight > nMinDepth)
    {
         if (!pblockindex || !pblockindex->pprev) return;
         pblockindex = pblockindex->pprev;
         if (ReadBlockFromDisk(block, pblockindex, consensusParams, "SCANBLOCKCHAINVERSION")) 
		 {
			std::string sVersion2 = ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<VER>","</VER>");
			mvBlockVersion[sVersion2]++;
		 }
    }
}

void CancelOrders(bool bClear)
{
		map<uint256, CTradeTx> uDelete;
		BOOST_FOREACH(PAIRTYPE(const uint256, CTradeTx)& item, mapTradeTxes)
		{
			CTradeTx ttx = item.second;
			uint256 hash = ttx.GetHash();
			if (bClear && ttx.EscrowTXID.empty())
			{
				uDelete.insert(std::make_pair(hash, ttx));
			}
			if (ttx.Action=="CANCEL" && ttx.EscrowTXID.empty())
			{
				uDelete.insert(std::make_pair(hash, ttx));
				RelayTrade(ttx, "cancel"); // Cancel order on the network
			}
			int64_t tradeAge = GetAdjustedTime() - ttx.TradeTime;
			if (tradeAge > (60*60*12) && ttx.EscrowTXID.empty())
			{
				uDelete.insert(std::make_pair(hash, ttx));
			}
		}
		BOOST_FOREACH(PAIRTYPE(const uint256, CTradeTx)& item, uDelete)
		{
			uint256 h = item.first;
			mapTradeTxes.erase(h);
		}
}



std::string RelayTrade(CTradeTx& t, std::string Type)
{
	std::string sArgs="hash=" + t.GetHash().GetHex() + ",address=" + t.Address + ",time=" + RoundToString(t.TradeTime,0) + ",quantity=" + RoundToString(t.Quantity,0)
		+ ",action=" + t.Action + ",symbol=" + t.Symbol + ",price=" + RoundToString(t.Price,4) + ",escrowtxid=" + t.EscrowTXID + ",vout=" + RoundToString(t.VOUT,0) + ",txid=";
	std::string out_Error = "";
	std::string sResponse = SQL(Type, t.Address, sArgs, out_Error);
	// For Buy, Sell or Cancel, refresh the trading engine time so the thread does not die:
	nLastTradingActivity = GetAdjustedTime();
	return out_Error;
}

UniValue GetTradeHistory()
{
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("Trading");
	std::string sRes = SQL("execution_history", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<TRADEHISTORY>","</TRADEHISTORY>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<TRADEH>");
    UniValue ret(UniValue::VOBJ);
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			std::string Symbol = ExtractXML(sE,"<SYMBOL>","</SYMBOL>");
			std::string Action = ExtractXML(sE,"<ACTION>","</ACTION>");
			double Amount = cdbl(ExtractXML(sE,"<AMOUNT>","</AMOUNT>"),4);
			double Quantity = cdbl(ExtractXML(sE,"<QUANTITY>","</QUANTITY>"),4);
			std::string sHash = ExtractXML(sE,"<HASH>","</HASH>");
			double Total = cdbl(ExtractXML(sE,"<TOTAL>","</TOTAL>"),4);
			std::string sExecuted=ExtractXML(sE,"<EXECUTED>","</EXECUTED>");
			//uint256 hash = uint256S("0x" + sHash);
		  	if (!Symbol.empty() && Amount > 0)
			{
				std::string sActNarr = (Action == "BUY") ? "BOT" : "SOLD";
				std::string sNarr = sActNarr + " " + RoundToString(Quantity,0) 
					+ " " + Symbol + " @ " + RoundToString(Amount,4) + " TOTAL " + RoundToString(Total,2) + "BBP ORDER " + sHash + ".";
			 	ret.push_back(Pair(sExecuted, sNarr));
			}
		}
	}
	return ret;
}

std::string ExtXML(std::string sXML, std::string sField)
{
	std::string sValue = ExtractXML(sXML,"<" + sField + ">","</" + sField + ">");
	return sValue;
}

CCommerceObject XMLToObj(std::string XML)
{
	CCommerceObject cco("");
	cco.LockTime = cdbl(ExtXML(XML,"TIME"),0);
	cco.ID = ExtXML(XML,"ID");
	cco.TXID = ExtXML(XML,"TXID");
	cco.Status1 = ExtXML(XML,"STATUS1");
	cco.Status2 = ExtXML(XML,"STATUS2");
	cco.Status3 = ExtXML(XML,"STATUS3");
	cco.Added = ExtXML(XML,"Added");
	cco.URL = ExtXML(XML,"URL");
	cco.Amount = cdbl(ExtXML(XML,"AMOUNT"),4) * COIN;
	cco.Details = ExtXML(XML,"DETAILS");
	cco.Title = ExtXML(XML,"TITLE");
	cco.Error = ExtXML(XML,"ERROR");
	return cco;		
}

std::map<std::string, CCommerceObject> GetProducts()
{
	std::map<std::string, CCommerceObject> mapCommerceObjects;
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("Trading");
	std::string sRes = SQL("get_products", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<PRODUCTS>","</PRODUCTS>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<PRODUCT>");
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			CCommerceObject ccom = XMLToObj(sE);
			if (!ccom.ID.empty())
			{
				mapCommerceObjects.insert(std::make_pair(ccom.ID, ccom));
			}
		}
	}
	return mapCommerceObjects;
}


std::map<std::string, CCommerceObject> GetOrderStatus()
{
	std::map<std::string, CCommerceObject> mapCommerceObjects;
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("Trading");
	std::string sRes = SQL("order_status", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<ORDERSTATUS>","</ORDERSTATUS>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<STATUS>");
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			CCommerceObject ccom = XMLToObj(sE);
			if (!ccom.ID.empty())
			{
				mapCommerceObjects.insert(std::make_pair(ccom.ID, ccom));
			}
		}
	}
	return mapCommerceObjects;
}

CAmount PriceLookupRequest(std::string sProductID)
{
	if (sProductID.empty()) return 0;
	std::map<std::string, CCommerceObject> mapProducts = GetProducts();
	BOOST_FOREACH(const PAIRTYPE(std::string, CCommerceObject)& item, mapProducts)
    {
			CCommerceObject oProduct = item.second;
			if (oProduct.ID==sProductID) return oProduct.Amount;
 	}
	return 0;
}

int GetTransactionVOUT(CWalletTx wtx, CAmount nAmount, int& VOUT)
{
	CTransaction tx;
	uint256 hashBlock;
	if (GetTransaction(wtx.GetHash(), tx, Params().GetConsensus(), hashBlock, true))
	{
		 for (int i = 0; i < (int)tx.vout.size(); i++)
		 {
 			 if (tx.vout[i].nValue == nAmount) 
			 {
				VOUT=i;
				return true;
			 }
		 }
	}
	return false;
}


std::string BuyProduct(std::string ProductID, bool bDryRun, CAmount nMaxPrice)
{
	std::string out_Error = "";
	CAmount nAmount = PriceLookupRequest(ProductID);
	if (nAmount==0)
	{
		 throw runtime_error("Sorry, unable to retrieve product price.  Please try again later.");
	}
	ReadConfigFile(mapArgs, mapMultiArgs);
 	std::string sAddress = DefaultRecAddress("Trading");
	std::string sEE = SQL("get_product_escrow_address", sAddress, "address", out_Error);
	std::string sProductEscrowAddress = ExtractXML(sEE,"<PRODUCT_ESCROW_ADDRESS>","</PRODUCT_ESCROW_ADDRESS>");
	std::string sDeliveryName = GetArg("-delivery_name", "");
	std::string sDeliveryAddress = GetArg("-delivery_address", "");
	std::string sDeliveryAddress2 = GetArg("-delivery_address2", "");
	sDeliveryAddress = strReplace(sDeliveryAddress,"#","");
	sDeliveryAddress2 = strReplace(sDeliveryAddress,"#","");
	std::string sDeliveryCity = GetArg("-delivery_city", "");
	std::string sDeliveryState = GetArg("-delivery_state", "");
	std::string sDeliveryZip = GetArg("-delivery_zip", "");
	std::string sDeliveryPhone = GetArg("-delivery_phone", "");
	if (sDeliveryName.empty())
	{
		 throw runtime_error("Delivery Name Empty (Required by AWS): Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryAddress.empty())
	{
		 throw runtime_error("Delivery Address Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryCity.empty())
	{
		 throw runtime_error("Delivery City Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryState.empty())
	{
		 throw runtime_error("Delivery State Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryZip.empty())
	{
		 throw runtime_error("Delivery Zip Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryPhone.empty())
	{
		 throw runtime_error("Delivery Phone Empty (Required by AWS): Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}

	if (sProductEscrowAddress.empty())
	{
		 throw runtime_error("Product Escrow Address was not able to be retrieved from a Sanctuary, please try again later.");
	}

	// Send Payment
	CBitcoinAddress cbAddress(sEE);
	CWalletTx wtx;
	std::string sScript="";
	std::string sTXID = "";
	int VOUT = 0;
	if (nAmount > nMaxPrice && nMaxPrice > 0) nAmount = nMaxPrice;
	if (!bDryRun)
	{
		SendColoredEscrow(cbAddress.Get(), nAmount, false, wtx, sScript);
		sTXID = wtx.GetHash().GetHex();
		bool bFound = GetTransactionVOUT(wtx, nAmount, VOUT);
		if (!bFound) LogPrintf(" Unable to locate VOUT in BuyProduct \n");
		LogPrintf("\n PAID %f for Product %s in TXID %s-VOUT %f  \n", (double)nAmount/COIN, ProductID.c_str(), sTXID.c_str(), (double)VOUT);
	}

	std::string sDryRun = bDryRun ? "DRY" : "FALSE";
	std::string sDelComplete = "<NAME>" + sDeliveryName + "</NAME><DRYRUN>" 
		+ sDryRun + "</DRYRUN><ADDRESS1>" + sDeliveryAddress + "</ADDRESS1><ADDRESS2>" 
		+ sDeliveryAddress2 + "</ADDRESS2><CITY>" 
		+ sDeliveryCity + "</CITY><STATE>" + sDeliveryState + "</STATE><ZIP>" 
		+ sDeliveryZip + "</ZIP><PHONE>" + sDeliveryPhone + "</PHONE>";
	std::string sProduct = "<PRODUCTID>" + ProductID + "</PRODUCTID><AMOUNT>" + RoundToString(nAmount/COIN,4) + "</AMOUNT><TXID>" + sTXID + "</TXID><VOUT>" 
		+ RoundToString(VOUT,0) + "</VOUT>";
	std::string sXML = sDelComplete + sProduct;
	std::string sReply = SQL("buy_product", sAddress, sXML, out_Error);
	std::string sError = ExtractXML(sReply,"<ERR>","</ERR>");
	LogPrintf("  ERROR %s BUY PRODUCT %s \n",sError.c_str(),sReply.c_str());
	
	if (!sError.empty())
	{
		throw runtime_error(sError);
	}
	return sReply;
}


void AddDebugMessage(std::string sMessage)
{
	mapDebug.insert(std::make_pair(GetAdjustedTime(), sMessage));
}

std::string ProcessEscrow()
{
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("Trading");
	std::string sRes = SQL("process_escrow", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<ESCROWS>","</ESCROWS>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<ROW>");
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			std::string Symbol = ExtractXML(sE,"<SYMBOL>","</SYMBOL>");
			std::string EscrowAddress = ExtractXML(sE,"<ESCROW_ADDRESS>","</ESCROW_ADDRESS>");
			double Amount = cdbl(ExtractXML(sE,"<AMOUNT>","</AMOUNT>"),4);
			std::string sHash = ExtractXML(sE,"<HASH>","</HASH>");
			uint256 hash = uint256S("0x" + sHash);
		    CTradeTx trade = GetOrderByHash(hash);
			// Ensure this matches our Trading Tx before sending escrow, and Escrow has not already been staked
			LogPrintf(" Orig trade address %s  Orig Trade Amount %f  esc amt %f  ",trade.Address.c_str(),trade.Total, Amount);
			AddDebugMessage("PREPARING ESCROW FOR " + RoundToString(Amount,2) + " " + Symbol + " FOR SANCTUARY " + EscrowAddress + " OLD ESCTXID " + trade.EscrowTXID);
			if (trade.Address==sAddress && trade.EscrowTXID.empty())
			{
				if ((trade.Action=="BUY" && (Amount > (trade.Total-1) && Amount < (trade.Total+1))) || 
					(trade.Action=="SELL" && (Amount > (trade.Quantity-1) && Amount < (trade.Quantity+1))))
				{
					if (!Symbol.empty() && !EscrowAddress.empty() && Amount > 0)
					{
						// Send Escrow in, and remember txid.  Sanctuary will send us our escrow back using this as a 'dependent' TXID if the trade is not executed by piggybacking the escrow refund on the back of the escrow transmission.
						// If the trade is executed, the trade will be processed using a depends-on identifier so the collateral is not lost, by spending its output to the other trader after receiving the recipients escrow.  The recipients collateral will be sent to us by piggybacking the collateral on the back of the same txid the GodNode received.
						CComplexTransaction cct("");
						std::string sColor = (trade.Action == "BUY") ? "" : "401";
						std::string sScript = cct.GetScriptForAssetColor(sColor);
						CBitcoinAddress address(EscrowAddress);
						if (address.IsValid())	
						{
							CAmount nAmount = 0;
							nAmount = (sColor=="401") ? Amount * (RETIREMENT_COIN) * 100 : Amount * COIN;
							CWalletTx wtx;
							// Ensure the Escrow held by market maker holds correct color for each respective leg
							SendColoredEscrow(address.Get(), nAmount, false, wtx, sScript);
							std::string TXID = wtx.GetHash().GetHex();
							std::string sMsg = "Sent " + RoundToString(Amount/COIN,2) + " for escrow on txid " + TXID;
							AddDebugMessage(sMsg);
							LogPrintf("\n Sent %f RetirementCoins for Escrow %s \n", (double)Amount,TXID.c_str());
							trade.EscrowTXID = TXID;
							CTransaction tx;
							uint256 hashBlock;
							if (GetTransaction(wtx.GetHash(), tx, Params().GetConsensus(), hashBlock, true))
							{
								 for (int i=0; i < (int)tx.vout.size(); i++)
								 {
				 					if (tx.vout[i].nValue == nAmount)
									{
										trade.VOUT = i;
										std::string sErr = RelayTrade(trade, "escrow");
									}
								 }
							}
						}
					}
				}
			}
		}
	}
	return out_Error;
}

std::string GetTrades()
{
	std::string out_Error = "";
	std::string sAddress=DefaultRecAddress("Trading");
	std::string sRes = SQL("trades", sAddress, "txid=", out_Error);
	std::string sTrades = ExtractXML(sRes,"<TRADES>","</TRADES>");
	std::vector<std::string> vTrades = Split(sTrades.c_str(),"<ROW>");
	LogPrintf("TradeList %s ",sTrades.c_str());
	AddDebugMessage("Listing " + RoundToString(vTrades.size(),0) + " trades");

	for (int i = 0; i < (int)vTrades.size(); i++)
	{
		std::string sTrade = vTrades[i];
		if (!sTrade.empty())
		{
			int64_t time = cdbl(ExtractXML(sTrade,"<TIME>","</TIME>"),0);
			std::string action = ExtractXML(sTrade,"<ACTION>","</ACTION>");
			std::string symbol = ExtractXML(sTrade,"<SYMBOL>","</SYMBOL>");
			std::string address = ExtractXML(sTrade,"<ADDRESS>","</ADDRESS>");
			int64_t quantity = cdbl(ExtractXML(sTrade,"<QUANTITY>","</QUANTITY>"),0);
			std::string escrow_txid = ExtractXML(sTrade,"<ESCROWTXID>","</ESCROWTXID>");
			double price = cdbl(ExtractXML(sTrade,"<PRICE>","</PRICE>"),4);
			if (!symbol.empty() && !address.empty())
			{
				CTradeTx ttx(time,action,symbol,quantity,price,address);
				ttx.EscrowTXID = escrow_txid;
				uint256 uHash = ttx.GetHash();
				int iTradeCount = mapTradeTxes.count(uHash);
				if (iTradeCount > 0) mapTradeTxes.erase(uHash);
				mapTradeTxes.insert(std::make_pair(uHash, ttx));
			}
		}
	}
	return out_Error;

}

std::string GetOrderText(CTradeTx& trade, double runningTotal)
{
	// This is the order book format 
	// Sell Side: PricePerBBP, Quantity, BBPAmount, BBPTotal ........ SYMBOL (RBBP) ............... Buy Side: PricePerBBP, Quantity, BBPAmount, BBPTotal
	//            1.251        1000      1251.00      1251.00                                                  .25          1000      250.00      250.00
	std::string sOB = rPad(RoundToString(trade.Price,4),6)
		+ "  " + rPad(RoundToString(trade.Quantity,0),8)
		+ "  " + rPad(RoundToString(trade.Quantity*trade.Price,2),8)
		+ "  " + rPad(RoundToString(runningTotal,2),12);
	return sOB;
}

CTradeTx GetOrderByHash(uint256 uHash)
{
	CTradeTx trade;
	int iTradeCount = mapTradeTxes.count(uHash);
	if (iTradeCount==0) return trade;
	trade = mapTradeTxes.find(uHash)->second;
	return trade;
}

std::string rPad(std::string data, int minWidth)
{
	if ((int)data.length() >= minWidth) return data;
	int iPadding = minWidth - data.length();
	std::string sPadding = std::string(iPadding,' ');
	std::string sOut = data + sPadding;
	return sOut;
}

UniValue GetOrderBook(std::string sSymbol)
{
	StartTradingThread();
	std::string sTrades = GetTrades();

	// Generate the Order Book by sorting two maps: one for the sell side, one for the buy side:
	vector<pair<int64_t, uint256> > vSellSide;
	vSellSide.reserve(mapTradeTxes.size());
	vector<pair<int64_t, uint256> > vBuySide;
	vBuySide.reserve(mapTradeTxes.size());
   
    BOOST_FOREACH(const PAIRTYPE(uint256, CTradeTx)& item, mapTradeTxes)
    {
		CTradeTx trade = item.second;
		uint256 hash = trade.GetHash();
		int64_t	rank = trade.Price * 100;
		if (trade.Action=="SELL" && trade.EscrowTXID.empty())
		{
			vSellSide.push_back(make_pair(rank, hash));
		}
		else if (trade.Action=="BUY" && trade.EscrowTXID.empty())
		{
			vBuySide.push_back(make_pair(rank, hash));
		}
    }
    sort(vSellSide.begin(), vSellSide.end());
	sort(vBuySide.begin(), vBuySide.end());
	// Go forward through sell side and backward through buy side simultaneously to make a market:
	int iMaxDisplayRows=30;
	std::string Sell[iMaxDisplayRows];
	std::string Buy[iMaxDisplayRows];
	int iSellRows = 0;
	int iBuyRows = 0;
	int iTotalRows = 0;
	double dTotalSell = 0;
	double dTotalBuy = 0;
    
    BOOST_FOREACH(const PAIRTYPE(int64_t, uint256)& item, vSellSide)
    {
		CTradeTx trade = GetOrderByHash(item.second);
		if (trade.Total > 0 && trade.EscrowTXID.empty())
		{
			dTotalSell += (trade.Quantity * trade.Price);
			Sell[iSellRows] = GetOrderText(trade,dTotalSell);
			iSellRows++;
			if (iSellRows > iMaxDisplayRows) break;
		}
    }
	BOOST_REVERSE_FOREACH(const PAIRTYPE(int64_t, uint256)& item, vBuySide)
    {
		CTradeTx trade = GetOrderByHash(item.second);
		if (trade.Total > 0 && trade.EscrowTXID.empty())
		{
			dTotalBuy += (trade.Quantity * trade.Price);
			Buy[iBuyRows] = GetOrderText(trade,dTotalBuy);
			iBuyRows++;
			if (iBuyRows >= iMaxDisplayRows) break;
		}
    }

    UniValue ret(UniValue::VOBJ);
	iTotalRows = (iSellRows >= iBuyRows) ? iSellRows : iBuyRows;
	std::string sHeader = "[S]Price Quantity  Amount   Total       (" + sSymbol + ")  Price  Quantity Amount     Total   [B]";
	ret.push_back(Pair("0",sHeader));

	for (int i = 0; i <= iTotalRows; i++)
	{
		if (Sell[i].empty()) Sell[i]= rPad(" ", 40);
		if (Buy[i].empty())  Buy[i] = rPad(" ", 40);
		std::string sConsolidatedRow = Sell[i] + " |" + sSymbol + "| " + Buy[i];
		ret.push_back(Pair(RoundToString(i,0), sConsolidatedRow));
	}
	return ret;
	
}


UniValue GetVersionReport()
{
	  UniValue ret(UniValue::VOBJ);
      //Returns a report of the BiblePay version that has been solving blocks over the last N blocks
	  ScanBlockChainVersion(BLOCKS_PER_DAY);
      std::string sBlockVersion = "";
      std::string sReport = "Version, Popularity\r\n";
      std::string sRow = "";
      double dPct = 0;
      ret.push_back(Pair("Version","Popularity,Percent %"));
      double Votes = 0;
	  for(map<std::string,double>::iterator ii=mvBlockVersion.begin(); ii!=mvBlockVersion.end(); ++ii) 
      {
            double Popularity = mvBlockVersion[(*ii).first];
			Votes += Popularity;
      }
      
      for(map<std::string,double>::iterator ii=mvBlockVersion.begin(); ii!=mvBlockVersion.end(); ++ii) 
      {
            double Popularity = mvBlockVersion[(*ii).first];
            sBlockVersion = (*ii).first;
            if (Popularity > 0)
            {
                sRow = sBlockVersion + "," + RoundToString(Popularity,0);
                sReport += sRow + "\r\n";
                dPct = Popularity / (Votes+.01) * 100;
                ret.push_back(Pair(sBlockVersion,RoundToString(Popularity,0) + "; " + RoundToString(dPct,2) + "%"));
            }
      }
      return ret;
}

bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature)
{
	// 3-1-2018 - Unlock wallet if SecureKey is available
	bool bTriedToUnlock = false;
	if (!msEncryptedString.empty() && pwalletMain->IsLocked())
	{
		bTriedToUnlock = true;
		if (!pwalletMain->Unlock(msEncryptedString, false))
		{
			static int nNotifiedOfUnlockIssue = 0;
			if (nNotifiedOfUnlockIssue == 0)
				LogPrintf("\nUnable to unlock wallet with SecureString.\n");
			nNotifiedOfUnlockIssue++;
		}
	}

	CBitcoinAddress addr(sBitcoinAddress);
    CKeyID keyID;
	if (!addr.GetKeyID(keyID))
	{
		sError = "Address does not refer to key";
		return false;
	}
	CKey key;
	if (!pwalletMain->GetKey(keyID, key))
	{
		sError = "Private key not available";
		return false;
	}
	CHashWriter ss(SER_GETHASH, 0);
	ss << strMessageMagic;
	ss << strMessage;
	vector<unsigned char> vchSig;
	if (!key.SignCompact(ss.GetHash(), vchSig))
	{
		sError = "Sign failed";
		return false;
	}
	sSignature = EncodeBase64(&vchSig[0], vchSig.size());
	if (bTriedToUnlock)
	{
		pwalletMain->Lock();
	}
	return true;
}


bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError)
{
		    CBitcoinAddress addr2(sBitcoinAddress);
			if (!addr2.IsValid()) 
			{
				strError = "Invalid address";
				return false;
			}
		    CKeyID keyID2;
			if (!addr2.GetKeyID(keyID2)) 
			{
				strError = "Address does not refer to key";
				return false;
			}
			bool fInvalid = false;
			vector<unsigned char> vchSig2 = DecodeBase64(sSignature.c_str(), &fInvalid);
			if (fInvalid)
			{
				strError = "Malformed base64 encoding";
				return false;
			}

			CHashWriter ss2(SER_GETHASH, 0);
			ss2 << strMessageMagic;
			ss2 << strMessage;
			CPubKey pubkey2;
    		if (!pubkey2.RecoverCompact(ss2.GetHash(), vchSig2)) 
			{
				strError = "Unable to recover public key.";
				return false;
			}
			bool fSuccess = (pubkey2.GetID() == keyID2);
			return fSuccess;
}

bool IsStakeSigned(std::string sXML)
{
	std::string sSignature = ExtractXML(sXML, "<SIG_0>","</SIG_0>");
	return (sSignature.empty()) ? false : true;
}

bool GetTransactionTimeAndAmount(uint256 txhash, int nVout, int64_t& nTime, CAmount& nAmount)
{
	uint256 hashBlock = uint256();
	CTransaction tx2;
	if (GetTransaction(txhash, tx2, Params().GetConsensus(), hashBlock, true))
	{
		   BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
           if (mi != mapBlockIndex.end() && (*mi).second) 
		   {
              CBlockIndex* pMNIndex = (*mi).second; 
			  nTime = pMNIndex->GetBlockTime();
		      nAmount = tx2.vout[nVout].nValue;
			  return true;
		   }
	}
	return false;
}

double GetTaskWeight(std::string sCPID)
{
	double nMaximumChatterAge = GetSporkDouble("podmaximumchatterage", (60 * 60 * 24));
	std::string sTaskList = ReadCacheWithMaxAge("CPIDTasks", sCPID, nMaximumChatterAge);
	return (sTaskList.empty()) ? 0 : 100;
}


double GetUTXOWeight(std::string sCPID)
{
	double nMaximumChatterAge = GetSporkDouble("podmaximumchatterage", (60 * 60 * 24));
	double dUTXOWeight = cdbl(ReadCacheWithMaxAge("UTXOWeight", sCPID, nMaximumChatterAge), 0);
	return dUTXOWeight;
}


double GetStakeWeight(CTransaction tx, int64_t nTipTime, std::string sXML, bool bVerifySignature, std::string& sMetrics, std::string& sError)
{
	// Calculate coin age - Proof Of Loyalty - 01-17-2018
	double dTotal = 0;
	std::string sXMLAmt = "";
	std::string sXMLAge = "";
	CAmount nTotalAmount = 0;
	double nTotalAge = 0;
	int iInstances = 0;
	if (tx.vin.size() < 1) return 0;
    for (size_t iIndex = 0; iIndex < tx.vin.size(); iIndex++) 
	{
        //const CTxIn& txin = tx.vin[iIndex];
    	int n = tx.vin[iIndex].prevout.n;
		CAmount nAmount = 0;
		int64_t nTime = 0;
		bool bSuccess = GetTransactionTimeAndAmount(tx.vin[iIndex].prevout.hash, n, nTime, nAmount);
		if (bSuccess && nTime > 0 && nAmount > 0)
		{
			double nAge = (nTipTime - nTime)/(86400+.01);
			if (nAge > 365) nAge = 365;           // If the age > 1 YEAR, cap at 1 YEAR
			if (nAge < 1 && fProd) nAge = 0;      // If the age < 1 DAY, set to zero (coins must be more than 1 day old to stake them)
			nTotalAmount += nAmount;
			nTotalAge += nAge;
			iInstances++;
			double dWeight = nAge * (nAmount/COIN);
			dTotal += dWeight;
		}
		//LogPrintf(" time %f Amount %f  Age %s  , ",(double)nTime,(double)nAmount/COIN,RoundToString(nAge,2));
	}
	double dAverageAge = 0;
	if (iInstances > 0) dAverageAge = nTotalAge / iInstances;
	sXMLAmt = "<polamount>" + RoundToString(nTotalAmount/COIN, 2) + "</polamount>";
	sXMLAge = "<polavgage>" + RoundToString(dAverageAge, 3) + "</polavgage>";

	sMetrics = sXMLAmt + sXMLAge;
	if (bVerifySignature)
	{
		bool fSigned = false;
		// Ensure the signature works for every output:
		std::string sMessage = ExtractXML(sXML, "<polmessage>","</polmessage>");
		for (int iIndex = 0; iIndex < (int)tx.vout.size(); iIndex++) 
		{
			std::string sSignature = ExtractXML(sXML, "<SIG_" + RoundToString(iIndex,0) + ">","</SIG_" + RoundToString(iIndex,0) + ">");
			const CTxOut& txout = tx.vout[iIndex];
			std::string sAddr = PubKeyToAddress(txout.scriptPubKey);
			fSigned = CheckStakeSignature(sAddr, sSignature, sMessage, sError);
			if (!fSigned) break;
		}

		if (!fSigned) dTotal=0;
	}
	return dTotal;
}


CAmount GetMoneySupplyEstimate(int nHeight)
{
	int64_t nAvgReward = 13657;
	double nUnlockedSupplyPercentage = .40;
	CAmount nSupply = nAvgReward * nHeight * nUnlockedSupplyPercentage * COIN;
	return nSupply;
}

int64_t GetStakeTargetModifierPercent(int nHeight, double nWeight)
{
	// A user who controls 1% of the money supply may stake once per day
	// Users Weight = CoinAmountStaked * CoinAgeInDays
	// Target Modifier Percent = UserWeight / Supply
	double dSupply = (GetMoneySupplyEstimate(nHeight)/COIN) + .01;
	if (nWeight > dSupply) nWeight = dSupply;
	double dPercent = (nWeight / dSupply) * 100;
	if (dPercent > 90) dPercent = 90;
	int64_t iPercent = (int64_t)dPercent;
	return iPercent;
}

CTransaction CreateCoinStake(CBlockIndex* pindexLast, CScript scriptCoinstakeKey, double dProofOfLoyaltyPercentage, int iMinConfirms, std::string& sXML, std::string& sError)
{
    CAmount curBalance = pwalletMain->GetUnlockedBalance();
	CAmount nTargetValue = curBalance * dProofOfLoyaltyPercentage;
	CTransaction ctx;
	
    if (nTargetValue <= 0 || nTargetValue > curBalance) 
	{
		sError = "BALANCE TOO LOW TO STAKE";
		return ctx;
	}

	if (pwalletMain->IsLocked())
	{
		sError = "WALLET MUST BE UNLOCKED TO STAKE";
		return ctx;
	}

    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptCoinstakeKey, nTargetValue, false, false, false, false, "", "", ""};
	recipient.Message = "<polweight>" + RoundToString(nTargetValue/COIN,2) + "</polweight>";
				
    vecSend.push_back(recipient);

	CWalletTx wtx;

	bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, 
		strError, NULL, true, ALL_COINS, false, iMinConfirms);
	if (!fCreated)    
	{
		sError = "INSUFFICIENT FUNDS";
		return ctx;
	}

			
	//ctx = (CTransaction)wtx;
	ctx = (CTransaction)wtx;

	std::string sMetrics = "";
	double dWeight = GetStakeWeight(ctx, pindexLast->GetBlockTime(), "", false, sMetrics, sError);

    // EnsureWalletIsUnlocked();
	// Ensure they can sign every output
	std::string sMessage = GetRandHash().GetHex();
	sXML += "<polmessage>" + sMessage + "</polmessage><polweight>"+RoundToString(dWeight,2) + "</polweight>" + sMetrics;

	for (int iIndex = 0; iIndex < (int)ctx.vout.size(); iIndex++) 
	{
		std::string sKey = "SIG_" + RoundToString(iIndex,0);
		const CTxOut& txout = ctx.vout[iIndex];
	    std::string sAddr = PubKeyToAddress(txout.scriptPubKey);
		std::string sSignature = "";
		bool bSigned = SignStake(sAddr, sMessage, sError, sSignature);
		if (bSigned)
		{
			sXML += "<" + sKey + ">" + sSignature + "</" + sKey + ">";
		}
		else
		{
			sXML += "<ERR>SIGN_ERROR</ERR>";
			LogPrintf(" Unable to sign stake %s \n", sAddr.c_str());
		}
	}
	return ctx;
}


double CAmountToRetirementDouble(CAmount Amount)
{
	double d = Amount / RETIREMENT_COIN / 100;
	return d;
}

UniValue exec(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2  && params.size() != 3 && params.size() != 4 && params.size() != 5 && params.size() != 6 && params.size() != 7))
        throw runtime_error(
		"exec <string::itemname> <string::parameter> \r\n"
        "Executes an RPC command by name.");

    std::string sItem = params[0].get_str();
	if (sItem=="") throw runtime_error("Command argument invalid.");

    UniValue results(UniValue::VOBJ);
	results.push_back(Pair("Command",sItem));
	if (sItem == "contributions")
	{
		UniValue aContributionReport = ContributionReport();
		return aContributionReport;
	}
	else if (sItem == "reboot")
	{
		fReboot2=true;
		results.push_back(Pair("Reboot",1));
    }
	else if (sItem == "getnews")
	{
		// getnews txid
		uint256 hash = ParseHashV(params[1], "parameter 1");
		results.push_back(Pair("TxID", hash.GetHex()));
		std::string sHeadline = "";
		std::string sNews = GetTxNews(hash, sHeadline);
		results.push_back(Pair("Headline", sHeadline));
		results.push_back(Pair("News", sNews));
	}
	else if (sItem == "testnews")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		std::string sAddress = consensusParams.FoundationAddress;
		std::string sError = "";
		std::string sTxId = AddBlockchainMessages(sAddress, "NEWS", "primarykey", "html", 250*COIN, sError);
		results.push_back(Pair("TXID", sTxId));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "persistsporkmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec persistsporkmessage dcccomputingprojectname rosetta'";
		if (params.size() != 4)
			 throw runtime_error(sError);

		std::string sType = params[1].get_str();
		std::string sPrimaryKey = params[2].get_str();
		std::string sValue = params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw runtime_error(sError);
    	std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, 1, true, sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "sendmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec sendmessage PRAYER mother Please_pray_for_my_mother._She_has_this_disease.'";
		if (params.size() != 4)
			 throw runtime_error(sError);

		std::string sType = params[1].get_str();
		std::string sPrimaryKey = params[2].get_str();
		std::string sValue = params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw runtime_error(sError);
		std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, 1, false, sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "getversion")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int32_t nMyVersion = ComputeBlockVersion(chainActive.Tip(), consensusParams);
		results.push_back(Pair("Block Version",(double)nMyVersion));
		if (pwalletMain) 
		{
			results.push_back(Pair("walletversion", pwalletMain->GetVersion()));
			results.push_back(Pair("wallet_fullversion", FormatFullVersion()));
			results.push_back(Pair("SSL Version", SSLeay_version(SSLEAY_VERSION)));
		}
    
	}
	else if (sItem == "versionreport")
	{
		UniValue uVersionReport = GetVersionReport();
		return uVersionReport;
	}
	else if (sItem == "betatestpoolpost")
	{
		std::string sResponse = BiblepayHttpPost(0,"POST","USER_A","PostSpeed","http://www.biblepay.org","home.html",80,"");
		results.push_back(Pair("beta_post", sResponse));
		results.push_back(Pair("beta_post_length", (double)sResponse.length()));
		std::string sResponse2 = BiblepayHttpPost(0,"POST","USER_A","PostSpeed","http://www.biblepay.org","404.html",80,"");
		results.push_back(Pair("beta_post_404", sResponse2));
		results.push_back(Pair("beta_post_length_404", (double)sResponse2.length()));
	}
	else if (sItem == "biblehash")
	{
		if (params.size() != 6)
			throw runtime_error("You must specify blockhash, blocktime, prevblocktime, prevheight, and nonce IE: run biblehash blockhash 12345 12234 100 256.");
		std::string sBlockHash = params[1].get_str();
		std::string sBlockTime = params[2].get_str();
		std::string sPrevBlockTime = params[3].get_str();
		std::string sPrevHeight = params[4].get_str();
		std::string sNonce = params[5].get_str();
		int64_t nHeight = cdbl(sPrevHeight,0);
		uint256 blockHash = uint256S("0x" + sBlockHash);
		int64_t nBlockTime = (int64_t)cdbl(sBlockTime,0);
		int64_t nPrevBlockTime = (int64_t)cdbl(sPrevBlockTime,0);
		unsigned int nNonce = cdbl(sNonce,0);
		if (!sBlockHash.empty() && nBlockTime > 0 && nPrevBlockTime > 0 && nHeight >= 0)
		{
			bool f7000;
			bool f8000;
			bool f9000;
			bool fTitheBlocksActive;
			GetMiningParams(nHeight, f7000, f8000, f9000, fTitheBlocksActive);
			uint256 hash = BibleHash(blockHash, nBlockTime, nPrevBlockTime, true, nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, nNonce);
			results.push_back(Pair("BibleHash",hash.GetHex()));
		}
	}
	else if (sItem == "stakebalance")
	{
		CAmount nStakeBalance = pwalletMain->GetUnlockedBalance();
		results.push_back(Pair("StakeBalance", nStakeBalance/COIN));
	}
	else if (sItem == "pinfo")
	{
		results.push_back(Pair("height", chainActive.Tip()->nHeight));
		int64_t nElapsed = GetAdjustedTime() - chainActive.Tip()->nTime;
		int64_t nMN = nElapsed * 256;
		if (nElapsed > (30 * 60)) nMN=999999999;
		if (nMN < 512) nMN = 512;
		results.push_back(Pair("pinfo", nMN));
		results.push_back(Pair("elapsed", nElapsed));
	}
	else if (sItem == "subsidy")
	{
		if (params.size() != 2) 
			throw runtime_error("You must specify height.");
		std::string sHeight = params[1].get_str();
		int64_t nHeight = (int64_t)cdbl(sHeight,0);
		if (nHeight >= 1 && nHeight <= chainActive.Tip()->nHeight)
		{
			CBlockIndex* pindex = FindBlockByHeight(nHeight);
			const Consensus::Params& consensusParams = Params().GetConsensus();
			if (pindex)
			{
				CBlock block;
				if (ReadBlockFromDisk(block, pindex, consensusParams, "GETSUBSIDY")) 
				{
        				results.push_back(Pair("subsidy", block.vtx[0].vout[0].nValue/COIN));
						std::string sRecipient = PubKeyToAddress(block.vtx[0].vout[0].scriptPubKey);
						results.push_back(Pair("recipient", sRecipient));
						std::string sBlockVersion = ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<VER>","</VER>");
						std::string sBlockVersion2 = strReplace(sBlockVersion, ".", "");
						double dBlockVersion = cdbl(sBlockVersion2, 0);
						results.push_back(Pair("blockversion", sBlockVersion));
						results.push_back(Pair("blockversion2", dBlockVersion));
						results.push_back(Pair("minerguid", ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<MINERGUID>","</MINERGUID>")));
				}
			}
		}
		else
		{
			results.push_back(Pair("error","block not found"));
		}
	}
	else if ((sItem == "order" || sItem=="trade") && !fProd)
	{
		// Buy/Sell Qty Symbol [PriceInQtyOfBBP]
		// Rob A - BiblePay - Ensure Trading System honors NetworkID
		std::string sAddress=DefaultRecAddress("Trading");
		if (params.size() != 5) 
			throw runtime_error("You must specify Buy/Sell/Cancel Qty Symbol [Price].  Example: exec buy 1000 RBBP 1.25 (Meaning: I offer to buy Qty 1000 RBBP (Retirement BiblePay Coins) for 1.25 BBP EACH (TOTALING=1.25*1000 BBP).");
		std::string sAction = params[1].get_str();
		boost::to_upper(sAction);
		double dQty = cdbl(params[2].get_str(),0);
		std::string sSymbol = params[3].get_str();
		boost::to_upper(sSymbol);
		if (sSymbol != "RBBP")
		{
			throw runtime_error("Sorry, Only symbol RBBP is currently supported.");
		}	

		double dPrice = cdbl(params[4].get_str(),4);
		if (dPrice < .01 || dPrice > 9999)
		{
			throw runtime_error("Sorry, price is out of range.");
		}
		if (dQty < 1 || dQty > 9999999)
		{
			throw runtime_error("Sorry, quantity is out of range.");
		}
		CTradeTx ttx(GetAdjustedTime(),sAction,sSymbol,dQty,dPrice,sAddress);
		results.push_back(Pair("Action",sAction));
		results.push_back(Pair("Symbol",sSymbol));
		results.push_back(Pair("Qty",dQty));
		results.push_back(Pair("Price",dPrice));
		results.push_back(Pair("Rec Address",sAddress));
		// If cancel, remove tx by tx hash
		uint256 uHash = ttx.GetHash();
		int iTradeCount = mapTradeTxes.count(uHash);
		// Note, the Trade class will validate the Action (BUY/SELL/CANCEL), and will validate the Symbol (RBBP/BBP)
		if (sAction=="CANCEL") 
		{
			// Cancel all trades for Address + Symbol + Action
			CancelOrders(false);
			results.push_back(Pair("Canceling Order",uHash.GetHex()));
		}
		else if (sAction == "BUY" || sAction == "SELL")
		{
			if (iTradeCount==0) mapTradeTxes.insert(std::make_pair(uHash, ttx));
			results.push_back(Pair("Placing Order",uHash.GetHex()));
		}
		else
		{
			results.push_back(Pair("Unknown Action",sAction));
		}
		if (!ttx.Error.empty()) results.push_back(Pair("Error",ttx.Error));
		// Relay changes to network - and relay once every 5 mins in case a new node comes online.
		if (!fProd)
		{
			std::string sErr = RelayTrade(ttx, "order");
			if (!sErr.empty()) results.push_back(Pair("Relayed",sErr));
		}
	}
	else if (sItem == "orderbook" && !fProd)
	{
		CancelOrders(true);
		UniValue r = GetOrderBook("RBBP");
		return r;
	}
	else if (sItem == "starttradingengine" && !fProd)
	{
		StartTradingThread();
		results.push_back(Pair("Started",1));
	}
	else if ((sItem == "listorders" || sItem == "orderlist") && !fProd)
	{
		CancelOrders(true);

		std::string sError = GetTrades();
		results.push_back(Pair("#",0));
		if (!sError.empty()) results.push_back(Pair("Error",sError));
		int i = 0;
		BOOST_FOREACH(PAIRTYPE(const uint256, CTradeTx)& item, mapTradeTxes)
		{
			CTradeTx ttx = item.second;
			uint256 hash = ttx.GetHash();
			if (ttx.Total > 0 && ttx.EscrowTXID.empty())
			{
				i++;
				results.push_back(Pair("#",i));
				results.push_back(Pair("Address",ttx.Address));
				results.push_back(Pair("Hash",hash.GetHex()));
				results.push_back(Pair("Symbol",ttx.Symbol));
				results.push_back(Pair("Action",ttx.Action));
				results.push_back(Pair("Qty",ttx.Quantity));
				results.push_back(Pair("BBP_Price",ttx.Price));
			}
		}

	}
	else if (sItem == "tradehistory")
	{
		UniValue aTH = GetTradeHistory();
		return aTH;
	}
	else if (sItem == "testtrade")
	{
		//extern std::map<uint256, CTradeTx> mapTradeTxes;
		uint256 uTradeHash = uint256S("0x1234");
		int iTradeCount = mapTradeTxes.count(uTradeHash);
		// Insert
		std::string sIP = "127.0.0.1";
		CTradeTx ttx(0,"BUY","BBP",10,1000,sIP);
        if (iTradeCount==0) mapTradeTxes.insert(std::make_pair(uTradeHash, ttx));
		// Report on this trade
		results.push_back(Pair("Quantity",ttx.Quantity));
		results.push_back(Pair("Symbol",ttx.Symbol));
		results.push_back(Pair("Action",ttx.Action));
		results.push_back(Pair("Map Length",iTradeCount));
		uint256 h = ttx.GetHash();
		results.push_back(Pair("Hash",h.GetHex()));
		LogPrintTrading("This is a test of trading.");
	}
	else if (sItem == "testmultisig")
	{
		// 
	}
	else if (sItem == "sendto402k")
	{
	    LOCK2(cs_main, pwalletMain->cs_wallet);
		CAmount nAmount = AmountFromValue(params[1].get_str());
	    CBitcoinAddress address(params[2].get_str());
		if (!address.IsValid())	
		{
			results.push_back(Pair("InvalidAddress",1));
		}
		else
		{
			results.push_back(Pair("Destination Address",address.ToString()));
			bool fSubtractFeeFromAmount = false;
			EnsureWalletIsUnlocked();
			std::string sAddress=DefaultRecAddress("Trading");
			results.push_back(Pair("DefRecAddr",sAddress));
			CWalletTx wtx;
			std::string sOutboundColor="";
			CComplexTransaction cct("");
			std::string sScript = cct.GetScriptForAssetColor("401");
			SendColoredEscrow(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, sScript);
			results.push_back(Pair("txid",wtx.GetHash().GetHex()));
		}
	}
	else if (sItem == "sendto401k")
	{
		//sendto401k amount destination
	    LOCK2(cs_main, pwalletMain->cs_wallet);
		CAmount nAmount = AmountFromValue(params[1].get_str())/10;
	    CBitcoinAddress address(params[2].get_str());
		if (!address.IsValid())	
		{
			results.push_back(Pair("InvalidAddress",1));
		}
		else
		{
			results.push_back(Pair("Destination Address",address.ToString()));
			bool fSubtractFeeFromAmount = false;
			EnsureWalletIsUnlocked();
			std::string sAddress=DefaultRecAddress("Trading");
			results.push_back(Pair("DefRecAddr",sAddress));
			// Complex order type
			CWalletTx wtx;
			std::string sExpectedRecipient = sAddress;
			std::string sExpectedColor = "";
			CAmount nExpectedAmount = nAmount;
			std::string sOutboundColor="401";
			results.push_back(Pair("OutboundColor",sOutboundColor));
			CComplexTransaction cct("");
			std::string sScriptComplexOrder = cct.GetScriptForComplexOrder("OCO", sOutboundColor, nExpectedAmount, sExpectedRecipient, sExpectedColor);
			results.push_back(Pair("XML",sScriptComplexOrder));
			cct.Color = sOutboundColor;
			LogPrintf("\nScript for comp order %s ",sScriptComplexOrder.c_str());
			SendColoredEscrow(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, sScriptComplexOrder);
			LogPrintf("\nScript for comp order %s ",sScriptComplexOrder.c_str());
			results.push_back(Pair("txid",wtx.GetHash().GetHex()));
		}
	}
	else if (sItem == "sendmanyxml")
	{
		    // exec sendmanyxml from_account xml_payload comment
		    if (!EnsureWalletIsAvailable(fHelp))        return NullUniValue;
		    LOCK2(cs_main, pwalletMain->cs_wallet);
			string strAccount = AccountFromValue(params[1]);
			string sXML = params[2].get_str();
			int nMinDepth = 1;
			CWalletTx wtx;
			wtx.strFromAccount = strAccount;
			wtx.mapValue["comment"] = params[3].get_str();
		    set<CBitcoinAddress> setAddress;
		    vector<CRecipient> vecSend;
	        CAmount totalAmount = 0;
			std::string sRecipients = ExtractXML(sXML,"<RECIPIENTS>","</RECIPIENTS>");
			std::vector<std::string> vRecips = Split(sRecipients.c_str(),"<ROW>");
			for (int i = 0; i < (int)vRecips.size(); i++)
			{
				std::string sRecip = vRecips[i];
				if (!sRecip.empty())
				{
					std::string sRecipient = ExtractXML(sRecip, "<RECIPIENT>","</RECIPIENT>");
					double dAmount = cdbl(ExtractXML(sRecip,"<AMOUNT>","</AMOUNT>"),4);
					if (!sRecipient.empty() && dAmount > 0)
					{
 						  CBitcoinAddress address(sRecipient);
	   			   	      if (!address.IsValid())		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Biblepay address: ")+sRecipient);
						  if (setAddress.count(address)) throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+sRecipient);
						  setAddress.insert(address);
						  CScript scriptPubKey = GetScriptForDestination(address.Get());
						  CAmount nAmount = dAmount * COIN;
						  if (nAmount <= 0)    throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
						  totalAmount += nAmount;
						  bool fSubtractFeeFromAmount = false;
					      CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount, false, false, false, "", "", ""};
						  vecSend.push_back(recipient);
					}
				}
			}
			EnsureWalletIsUnlocked();
			// Check funds
			CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, ISMINE_SPENDABLE);
			if (totalAmount > nBalance)  throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");
			// Send
			CReserveKey keyChange(pwalletMain);
			CAmount nFeeRequired = 0;
			int nChangePosRet = -1;
			string strFailReason;
			bool fUseInstantSend = false;
			bool fUsePrivateSend = false;
			bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason, NULL, true, fUsePrivateSend ? ONLY_DENOMINATED : ALL_COINS, fUseInstantSend);
			if (!fCreated)    throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
			if (!pwalletMain->CommitTransaction(wtx, keyChange, fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))   throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
		 	results.push_back(Pair("txid",wtx.GetHash().GetHex()));
	}
	else if (sItem == "deprecated_createescrowtransaction")
	{

			results.push_back(Pair("deprecated","Please use createrawtransaction."));
			/*
	
		    LOCK(cs_main);
			std::string sXML = params[1].get_str();
			CMutableTransaction rawTx;
			// Process Inputs
			std::string sInputs = ExtractXML(sXML,"<INPUTS>","</INPUTS>");
			std::vector<std::string> vInputs = Split(sInputs.c_str(),"<ROW>");
			for (int i = 0; i < (int)vInputs.size(); i++)
			{
				std::string sInput = vInputs[i];
				if (!sInput.empty())
				{
					std::string sTxId = ExtractXML(sInput,"<TXID>","</TXID>");
					int nOutput = (int)cdbl(ExtractXML(sInput, "<VOUT>","</VOUT>"),0);
					int64_t nLockTime = (int64_t)cdbl(ExtractXML(sInput,"<LOCKTIME>","</LOCKTIME>"),0);
					uint256 txid = uint256S("0x" + sTxId);
					uint32_t nSequence = (nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());
					CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);
					rawTx.vin.push_back(in);
					LogPrintf("ADDING TXID %s ", sTxId.c_str());

				}
			}
		std::string sRecipients = ExtractXML(sXML,"<RECIPIENTS>","</RECIPIENTS>");
		std::vector<std::string> vRecips = Split(sRecipients.c_str(),"<ROW>");
		for (int i = 0; i < (int)vRecips.size(); i++)
		{
			std::string sRecip = vRecips[i];
			if (!sRecip.empty())
			{
				std::string sRecipient = ExtractXML(sRecip, "<RECIPIENT>","</RECIPIENT>");
				double dAmount = cdbl(ExtractXML(sRecip,"<AMOUNT>","</AMOUNT>"),4);
				std::string sData = ExtractXML(sRecip,"<DATA>","</DATA>");
				std::string sColor = ExtractXML(sRecip,"<COLOR>","</COLOR>");
				if (!sData.empty())
				{
				     std::vector<unsigned char> data = ParseHexV(sData,"Data");
					 CTxOut out(0, CScript() << OP_RETURN << data);
					 rawTx.vout.push_back(out);
				}
				else if (!sRecipient.empty())
				{
 					  CBitcoinAddress address(sRecipient);
					  CScript scriptPubKey = GetScriptForDestination(address.Get());
					  CAmount nAmount = sColor=="401" ? dAmount * COIN : dAmount * COIN; 
			          CTxOut out(nAmount, scriptPubKey);
					  rawTx.vout.push_back(out);
	    			  CComplexTransaction cct("");
					  std::string sAssetColorScript = cct.GetScriptForAssetColor(sColor); 
					  rawTx.vout[rawTx.vout.size()-1]age = sAssetColorScript;
					  LogPrintf("CreateEscrowTx::Adding Recip %s, amount %f, color %s, dAmount %f ", sRecipient.c_str(),(double)nAmount,sAssetColorScript.c_str(), (double)dAmount);
				}
			}
		}
		return EncodeHexTx(rawTx);
		*/

    }
	else if (sItem == "getretirementbalance" || sItem == "retirementbalance")
	{
		results.push_back(Pair("balance",CAmountToRetirementDouble(pwalletMain->GetRetirementBalance())));
	}
	else if (sItem == "generatetemplekey")
	{
	    // Generate the Temple Keypair
	    CKey key;
		key.MakeNewKey(false);
		CPrivKey vchPrivKey = key.GetPrivKey();
		std::string sOutPrivKey = HexStr<CPrivKey::iterator>(vchPrivKey.begin(), vchPrivKey.end());
		std::string sOutPubKey = HexStr(key.GetPubKey());
		results.push_back(Pair("Private", sOutPrivKey));
		results.push_back(Pair("Public", sOutPubKey));
	}
	else if (sItem == "testsign")
	{
		std::string sPrivKey = GetTemplePrivKey();
		std::string sSig = SignMessage("1", sPrivKey);
		std::string sBadSig = SignMessage("1", "000");
		results.push_back(Pair("GoodSig", sSig));
		results.push_back(Pair("BadSig", sBadSig));
		bool bSig1 = CheckMessageSignature("1", sSig);
		bool bSig2 = CheckMessageSignature("1", sBadSig);
		results.push_back(Pair("1_RESULT", bSig1));
		results.push_back(Pair("1_BAD_RESULT", bSig2));
	}
	else if (sItem == "listdebug")
	{
		BOOST_FOREACH(const PAIRTYPE(int64_t, std::string)& item, mapDebug)
	    {
			std::string sMsg = item.second;
			int64_t nTime = item.first;
			results.push_back(Pair(RoundToString(nTime,0), sMsg));
		}

	}
	else if (sItem == "listproducts")
	{
		std::map<std::string, CCommerceObject> mapProducts = GetProducts();
		BOOST_FOREACH(const PAIRTYPE(std::string, CCommerceObject)& item, mapProducts)
	    {
			CCommerceObject oProduct = item.second;
			UniValue p(UniValue::VOBJ);
			p.push_back(Pair("Product ID",oProduct.ID));
			p.push_back(Pair("URL",oProduct.URL));
			p.push_back(Pair("Price",oProduct.Amount/COIN));
			p.push_back(Pair("Title",oProduct.Title));
			p.push_back(Pair("Details",oProduct.Details));
			results.push_back(Pair(oProduct.ID,p));
		}
	}
	else if (sItem == "buyproduct")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify type: IE 'exec buyproduct productid'.");
		std::string sProductID = params[1].get_str();
		// First, a dry run to ensure no errors exist, like Product does not exist, address validation failed, address incomplete in config file, wallet balance too low, etc.
		std::string sResult = BuyProduct(sProductID, true, 1*COIN);
		std::string sStatus = ExtractXML(sResult,"<STATUS>","</STATUS>");
	
		if (sStatus=="SUCCESS")
		{
			// Dry run succeeded.  Now buy the product for real:
			sResult = BuyProduct(sProductID, false, 0);
			sStatus = ExtractXML(sResult,"<STATUS>","</STATUS>");
		}
		else
		{
			results.push_back(Pair("Status", "Fail"));
		}
		std::string sTXID = ExtractXML(sResult,"<TXID>","</TXID>");
		if (sStatus=="SUCCESS")	results.push_back(Pair("OrderID", sTXID));
		std::string sOrderID = ExtractXML(sResult,"<ORDERID>","</ORDERID>");
		results.push_back(Pair("TXID", sTXID));
		results.push_back(Pair("Status",sStatus));
	}
	else if (sItem == "orderstatus")
	{
		// This command shows the order status of product orders
		std::map<std::string, CCommerceObject> mapProducts = GetOrderStatus();
		BOOST_FOREACH(const PAIRTYPE(std::string, CCommerceObject)& item, mapProducts)
	    {
			CCommerceObject oProduct = item.second;
			UniValue p(UniValue::VOBJ);
			p.push_back(Pair("Product ID",oProduct.ID));
			p.push_back(Pair("Price",oProduct.Amount/COIN));
			p.push_back(Pair("Added", oProduct.Added));
			p.push_back(Pair("Title",oProduct.Title));
			p.push_back(Pair("Status1",oProduct.Status1));
			p.push_back(Pair("Status2",oProduct.Status2));
			p.push_back(Pair("Status3",oProduct.Status3));
			p.push_back(Pair("Details",oProduct.Details));
			results.push_back(Pair(oProduct.ID,p));
		}
	}
	else if (sItem == "XBBP")
	{
		std::string smd1="BiblePay";
		std::vector<unsigned char> vch1 = vector<unsigned char>(smd1.begin(), smd1.end());
		//std::vector<unsigned char> vchPlaintext = vector<unsigned char>(hash.begin(), hash.end());
	 
	    uint256 h1 = SerializeHash(vch1); 
		uint256 h2 = HashBiblePay(vch1.begin(),vch1.end());
		uint256 h3 = HashBiblePay(h2.begin(),h2.end());
		uint256 h4 = HashBiblePay(h3.begin(),h3.end());
		uint256 h5 = HashBiblePay(h4.begin(),h4.end());
		results.push_back(Pair("h1",h1.GetHex()));
		results.push_back(Pair("h2",h2.GetHex()));
		results.push_back(Pair("h3",h3.GetHex()));
		results.push_back(Pair("h4",h4.GetHex()));
		results.push_back(Pair("h5",h5.GetHex()));
		bool f1 = CheckNonce(true, 1, 9000, 10000, 10060);
		results.push_back(Pair("f1",f1));
		bool f2 = CheckNonce(true, 256000, 9000, 10000, 10060);
		results.push_back(Pair("f2",f2));
		bool f3 = CheckNonce(true, 256000, 9000, 10000, 10000+1900);
		results.push_back(Pair("f3",f3));
		bool f4 = CheckNonce(true, 1256000, 9000, 10000, 10000+180);
		results.push_back(Pair("f4",f4));
		bool f5 = CheckNonce(true, 1256000, 9000, 10000, 10000+18000);
		results.push_back(Pair("f5",f5));
	}
	else if (sItem=="ssl")
	{
		std::string sResponse = BiblepayHTTPSPost(0,"POST","USER_A","PostSpeed","https://pool.biblepay.org","Action.aspx", 443, "SOLUTION", 20, 5000);
		results.push_back(Pair("Test",sResponse));
	}
	else if (sItem == "hp")
	{
		// This is a simple test to show the POL effect on the hashtarget:		
		std::string sType = params[1].get_str();
		double d1 = cdbl(sType, 0);
		uint256 h1 = PercentToBigIntBase((int)d1);
		results.push_back(Pair("h1", h1.GetHex()));
		uint256 h2 = PercentToBigIntBase((int)d1);
		results.push_back(Pair("h2", h2.GetHex()));
		// Move on to test the overflow condition
		arith_uint256 bn1 = UintToArith256(h1);
		arith_uint256 bn2 = UintToArith256(h2);
		arith_uint256 bn3 = bn1 + bn2;
		uint256 h3 = ArithToUint256(bn3);
		results.push_back(Pair("h3", h3.GetHex()));
	}
	else if (sItem == "hexblocktojson")
	{
		std::string sHex = params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
        return blockToJSON(block, NULL);
	}
	else if (sItem == "hexblocktocoinbase")
	{
		std::string sBlockHex = params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sBlockHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
		std::string sTxHex = params[2].get_str();
		CTransaction tx;
	    if (!DecodeHexTx(tx, sTxHex))
		    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
		if (block.vtx.size() < 1)
		    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block Mutilation Error");
    	if (block.vtx[0].GetHash().GetHex() != tx.GetHash().GetHex())
		{
		    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Coinbase TX not in block");
		}
		results.push_back(Pair("txid", block.vtx[0].GetHash().GetHex()));
		results.push_back(Pair("recipient", PubKeyToAddress(block.vtx[0].vout[0].scriptPubKey)));
		// 1-23-2018: Biblepay - Rob A., add biblehash to tail end of coinbase tx
		CBlockIndex* pindexPrev = chainActive.Tip();
		bool f7000;
		bool f8000;
		bool f9000;
		bool fTitheBlocksActive;
		GetMiningParams(pindexPrev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);
		uint256 hash = BibleHash(block.GetHash(), block.GetBlockTime(), pindexPrev->nTime, true, 
			pindexPrev->nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, block.nNonce);
		results.push_back(Pair("biblehash", hash.GetHex()));
		results.push_back(Pair("subsidy", block.vtx[0].vout[0].nValue/COIN));
		results.push_back(Pair("blockversion", ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<VER>","</VER>")));
	}
	else if (sItem == "miningdiagnostics")
	{
		std::string sSwitch = params[1].get_str();
		if (sSwitch=="on")
		{
			fMiningDiagnostics = true;
		}
		else
		{
			fMiningDiagnostics = false;
		}
		results.push_back(Pair("mining_diagnostics", fMiningDiagnostics));
	}
	else if (sItem == "podcdifficulty")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nHeight = 0;
		if (chainActive.Tip() != NULL) nHeight = chainActive.Tip()->nHeight;
		if (params.size() == 2) nHeight = cdbl(params[1].get_str(),0);
		CBlockIndex* pindex = NULL;
		if (chainActive.Tip() != NULL && nHeight <= chainActive.Tip()->nHeight) FindBlockByHeight(nHeight);
		double dPOWDifficulty = GetDifficulty(pindex)*10;
		double dPODCDifficulty = GetBlockMagnitude(nHeight);
		results.push_back(Pair("height", nHeight));
		results.push_back(Pair("difficulty_podc", dPODCDifficulty));
		results.push_back(Pair("difficulty_pow", dPOWDifficulty));
	}
	else if (sItem == "amimasternode")
	{
		results.push_back(Pair("AmIMasternode",AmIMasternode()));
	}
	else if (sItem == "writecache")
	{
		if (params.size() != 4)
		{
			results.push_back(Pair("Error","Please specify exec writecache type key value"));
		}
		else
		{
			WriteCache(params[1].get_str(), params[2].get_str(), params[3].get_str(), GetAdjustedTime());
		}
	}
	else if (sItem == "podcupdate")
	{
		std::string sError = "";
		bool bForce = false;
		if (params.size() > 1) bForce = params[1].get_str() == "true" ? true : false;
		PODCUpdate(sError, bForce);
		results.push_back(Pair("PODCUpdate", sError));
	}
	else if (sItem == "dcc")
	{
		std::string sError = "";
		bool fSuccess =  DownloadDistributedComputingFile(sError);
		results.push_back(Pair("Success", fSuccess));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "testprayers")
	{
		WriteCache("MyWrite","MyKey","MyValue",GetAdjustedTime());
		WriteCache("MyWrite","MyKey2","MyValue2", 15000);
		std::string s1 = ReadCache("MyWrite", "MyKey");
		std::string s2 = ReadCache("MyWrite", "MyKey2");
		results.push_back(Pair("MyWrite1", s1));
		results.push_back(Pair("MyWrite2", s2));
		int64_t nMinimumMemory = GetAdjustedTime() - (60*60*24*4);
		PurgeCacheAsOfExpiration("MyWrite", nMinimumMemory);
		std::string s3 = ReadCache("MyWrite", "MyKey2");
		results.push_back(Pair("MyWrite2-2", s3));

		for (int y = 0; y < 64; y++)
		{
			std::string sPrayer = "";
			GetDataList("PRAYER", 7, iPrayerIndex, sPrayer);
			results.push_back(Pair("Prayer", sPrayer));
		}

	}
	else if (sItem == "dcchash")
	{
		std::string sContract = ReadCache("dcc", "contract");
		std::string sHash = ReadCache("dcc", "contract_hash");
		results.push_back(Pair("contract", sContract));
		results.push_back(Pair("hash", sHash));
	}
	else if (sItem == "associate")
	{
		if (params.size() < 3)
			throw runtime_error("You must specify boincemail, boincpassword.  Optionally specify force=true/false.");
		std::string sEmail = params[1].get_str();
		std::string sPW = params[2].get_str();
		std::string sForce = "";
		std::string sUnbanked = "";
		if (params.size() > 3) sForce = params[3].get_str();
		if (params.size() > 4) sUnbanked = params[4].get_str();
		bool fForce = sForce=="true" ? true : false;
		std::string sError = AssociateDCAccount("project1", sEmail, sPW, sUnbanked, fForce);
		std::string sNarr = (sError.empty()) ? "Successfully advertised DC-Key.  Type exec getboincinfo to find more researcher information.  Welcome Aboard!  Thank you for donating your clock-cycles to help cure cancer!" : sError;
		results.push_back(Pair("E-Mail", sEmail));
		results.push_back(Pair("Results", sNarr));
	}
	else if (sItem == "boinctest")
	{
		if (params.size() < 4)
			throw runtime_error("You must specify boincemail, boincpassword, boincprojectid. ");
		
		std::string sEmail = params[1].get_str();
		std::string sPW = params[2].get_str();
		std::string sProjectId = params[3].get_str();

		std::string sPWHash = GetBoincPasswordHash(sPW, sEmail);
		std::string sAuth = GetBoincAuthenticator(sProjectId, sEmail, sPWHash);
		int nUserId = GetBoincResearcherUserId(sProjectId, sAuth);
		results.push_back(Pair("userid", nUserId));
		
		std::string sCPID = "";
		std::string sCode = GetBoincResearcherHexCodeAndCPID(sProjectId, nUserId, sCPID);
		std::string sHexSet = DefaultRecAddress("Rosetta");
			
		SetBoincResearcherHexCode(sProjectId, sAuth, sHexSet);
		std::string sCode2 = GetBoincResearcherHexCodeAndCPID(sProjectId, nUserId, sCPID);
		results.push_back(Pair("hexcode1",sCode));
		results.push_back(Pair("auth",sAuth));
		results.push_back(Pair("cpid",sCPID));
		results.push_back(Pair("userid", nUserId));
		results.push_back(Pair("hexcode2",sCode2));
	}
	else if (sItem == "listdccs")
	{
		std::vector<std::string> sCPIDs = GetListOfDCCS("");
		for (int i=0; i < (int)sCPIDs.size(); i++)
		{
			results.push_back(Pair("cpid",sCPIDs[i]));
		}
	}
	else if (sItem == "testdcc")
	{
		std::string sResult = ExecuteDistributedComputingSanctuaryQuorumProcess();
		LogPrintf("\n AcceptBlock::ExecuteDistributedComputingSanctuaryQuorumProcess %s . \n", sResult.c_str());
		results.push_back(Pair("DC_sanctuary_quorum", sResult));
	}
	else if (sItem == "testranks")
	{
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		int iSanctuaryCount = GetSanctuaryCount();

		for (int i = iLastSuperblock-25; i <= iNextSuperblock; i++)
		{
			double nRank = MyPercentile(i);
			int iRank = MyRank(i);
			results.push_back(Pair("percentrank " + RoundToString((double)i,0) + " " + RoundToString(nRank,0), iRank));
		}
		results.push_back(Pair("sanctuary_count" , iSanctuaryCount));
	}
	else if (sItem == "testvote")
	{
		int64_t nAge = GetDCCFileAge();
		uint256 uDCChash = GetDCCFileHash();
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sContract = GetDCCFileContract();
		results.push_back(Pair("fileage", nAge));
		results.push_back(Pair("filehash", uDCChash.GetHex()));
		results.push_back(Pair("contract", sContract));
		double nRank = MyPercentile(iLastSuperblock);

		results.push_back(Pair("sanctuary_rank", nRank));
		std::string sAddresses="";
		std::string sAmounts = "";
		uint256 uPAMHash = GetDCPAMHashByContract(sContract, iNextSuperblock);
		results.push_back(Pair("pam_hash", uPAMHash.GetHex()));
		LogPrintf(" ********* fileage %f  contract %s \n ",(double)nAge, sContract.c_str());
		int iVotes = 0;
		uint256 uGovObjHash = uint256S("0x0");
		GetDistributedComputingGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
		std::string sError = "";
		LogPrintf(" count %f \n", (double)iVotes);
		results.push_back(Pair("govobjhash", uGovObjHash.GetHex()));
		results.push_back(Pair("Addresses", sAddresses));
		results.push_back(Pair("Amounts", sAmounts));
		uint256 uPamHash2 = GetDCPAMHash(sAddresses, sAmounts);
		results.push_back(Pair("pam_hash2", uPamHash2.GetHex()));

		if (uGovObjHash == uint256S("0x0"))
		{
			// create the contract
			std::string sQuorumTrigger = SerializeSanctuaryQuorumTrigger(iNextSuperblock, sContract);
			std::string sGobjectHash = "";
			SubmitDistributedComputingTrigger(sQuorumTrigger, sGobjectHash, sError);
			results.push_back(Pair("quorum_hex", sQuorumTrigger));
			results.push_back(Pair("quorum_gobject_trigger_hash", sGobjectHash));
			results.push_back(Pair("quorum_error", sError));
		}
		results.push_back(Pair("votes_for_my_contract", iVotes));
		results.push_back(Pair("contract1", sContract));
		int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
		results.push_back(Pair("RequiredVotes", iRequiredVotes));
		results.push_back(Pair("last_superblock", iLastSuperblock));
		results.push_back(Pair("next_superblock", iNextSuperblock));
		bool fTriggered = CSuperblockManager::IsSuperblockTriggered(iNextSuperblock);
		double nTotalMagnitude = 0;
		int iCPIDCount = GetCPIDCount(sContract, nTotalMagnitude);
		results.push_back(Pair("cpid_count", iCPIDCount));
		results.push_back(Pair("total_magnitude", nTotalMagnitude));


		results.push_back(Pair("next_superblock_triggered", fTriggered));
		bool bRes = VoteForDistributedComputingContract(iNextSuperblock, sContract, sError);
		results.push_back(Pair("vote1",bRes));
		results.push_back(Pair("vote1error",sError));

		// Verify the Vote serialization:
		std::string sSerialize = mnpayments.SerializeSanctuaryQuorumSignatures(iNextSuperblock, uDCChash);
		std::string sSigs = ExtractXML(sSerialize,"<SIGS>","</SIGS>");
		int iVSigners = VerifySanctuarySignatures(sSigs);
		results.push_back(Pair("serial", sSerialize));
		results.push_back(Pair("verified_sigs", iVSigners));
	}
	else if (sItem == "getcpid")
	{
		// Returns a CPID for a given BBP address
		if (params.size() > 2) throw runtime_error("You must specify one BBP address or none: exec getcpid, or exec getcpid BBPAddress.");
		std::string sAddress = "";
		if (params.size() == 2) sAddress = params[1].get_str();
		std::string out_address = "";
		double nMagnitude = 0;
		std::string sCPID = FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude);
		results.push_back(Pair("CPID", sCPID));
		results.push_back(Pair("Address", out_address));
		results.push_back(Pair("Magnitude", nMagnitude));
	}
	else if (sItem == "getboinctasks")
	{
		// returns current tasks for this rosetta user id 
		std::string out_address = "";
		double nMagnitude = 0;
		std::string sAddress="";
		std::string sCPID = FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude);
		std::vector<std::string> vCPIDS = Split(msGlobalCPID.c_str(),";");
		int64_t iMaxSeconds = 60 * 24 * 30 * 12 * 60;
		for (int i = 0; i < (int)vCPIDS.size(); i++)
		{
			std::string s1 = vCPIDS[i];
			if (!s1.empty())
			{
				std::string sData = RetrieveDCCWithMaxAge(s1, iMaxSeconds);
				if (!sData.empty())
				{
					std::string sUserId = GetDCCElement(sData, 3);
					int nUserId = cdbl(sUserId, 0);
					if (nUserId > 0)
					{
						results.push_back(Pair("CPID", s1));
						std::string sHosts = GetBoincHostsByUser(nUserId, "project1");
						std::vector<std::string> vH = Split(sHosts.c_str(), ",");
						for (int j = 0; j < (int)vH.size(); j++)
						{
							double dHost = cdbl(vH[j], 0);
							results.push_back(Pair("host", dHost));
							std::string sTasks = GetBoincTasksByHost((int)dHost, "project1");
							results.push_back(Pair("Tasks", sTasks));
							// Pre Verify these using Sanctuary Rules
							double dPreverificationPercent = VerifyTasks(sTasks);
							results.push_back(Pair("Preverification", dPreverificationPercent));

							std::vector<std::string> vT = Split(sTasks.c_str(), ",");
							for (int k = 0; k < (int)vT.size(); k++)
							{
								std::string sTask = vT[k];
								std::vector<std::string> vEquals = Split(sTask.c_str(), "=");
								if (vEquals.size() > 1)
								{
									std::string sTaskID = vEquals[0];
									std::string sTimestamp = vEquals[1];
									std::string sTimestampHR = TimestampToHRDate(cdbl(sTimestamp,0));
									//results.push_back(Pair("SentTime", sTimestamp));
									//results.push_back(Pair("SentTimestamp", sTimestampHR));
									results.push_back(Pair(sTaskID, sTimestampHR));
									std::string sStatus = GetWorkUnitResultElement("project1", cdbl(sTaskID,0), "outcome");
									results.push_back(Pair("Status", sStatus));
									std::string sSentXMLTime = GetWorkUnitResultElement("project1", cdbl(sTaskID,0), "sent_time");
									std::string sSentXMLTimestamp = TimestampToHRDate(cdbl(sSentXMLTime,0));
									//results.push_back(Pair("SentXMLTime", sSentXMLTime));
									results.push_back(Pair("SentXMLTimeHR", sSentXMLTimestamp));
									//std::string sRAC = GetWorkUnitResultElement("project1", cdbl(sTaskID,0), "granted_credit");
									//results.push_back(Pair("RACRewarded", sRAC));

								}
							}
						}
					}
				}
			}
		}
	}
	else if (sItem == "getboincinfo")
	{
		std::string out_address = "";
		std::string sAddress = "";
		if (params.size() == 2) sAddress = params[1].get_str();
		double nMagnitude2 = 0;
		std::string sCPID = FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude2);
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	
		results.push_back(Pair("CPID", sCPID));
		results.push_back(Pair("Address", out_address));
		results.push_back(Pair("CPIDS", msGlobalCPID));
		
		int64_t nTime = RetrieveCPIDAssociationTime(sCPID);
		int64_t nAge = (GetAdjustedTime() - nTime)/(60*60);

		std::string sNarrAge = nAge < 24 ? "** Warning, CPID was associated less than 24 hours ago.  Magnitude may be zero until distributed grid network harvests all credit data.  Please keep crunching. **" : "";
		results.push_back(Pair("CPID-Age (hours)",nAge));
		if (!sNarrAge.empty())
		{
			results.push_back(Pair("Alert", sNarrAge));
		}

		results.push_back(Pair("NextSuperblockHeight", iNextSuperblock));
		double nBudget2 = CSuperblock::GetPaymentsLimit(iNextSuperblock) / COIN;
		results.push_back(Pair("NextSuperblockBudget", nBudget2));
	
	    int64_t iMaxSeconds = 60 * 24 * 30 * 12 * 60;
    	std::vector<std::string> vCPIDS = Split(msGlobalCPID.c_str(),";");
		double nTotalRAC = 0;
		double dBiblepayTeam = cdbl(GetSporkValue("team"),0);
		for (int i = 0; i < (int)vCPIDS.size(); i++)
		{
			std::string s1 = vCPIDS[i];
			if (!s1.empty())
			{
				std::string sData = RetrieveDCCWithMaxAge(s1, iMaxSeconds);
				int nUserId = cdbl(GetDCCElement(sData, 3),0);
				std::string sAddress = GetDCCElement(sData, 1);
				if (nUserId > 0)
				{
					double RAC = GetBoincRACByUserId("project1", nUserId);
					nTotalRAC += RAC;
					results.push_back(Pair(s1 + "_ADDRESS", sAddress));
					results.push_back(Pair(s1 + "_RAC", RAC));
					double dTeam = GetBoincTeamByUserId("project1", nUserId);
					results.push_back(Pair(s1 + "_TEAM", dTeam));
					if (dBiblepayTeam > 0)
					{
						if (dBiblepayTeam != dTeam)
						{
							results.push_back(Pair("Warning", "CPID " + s1 + " not in team Biblepay.  This CPID is not receiving rewards for cancer research."));
						}
					}
					// Show the User the estimated Task Weight, and estimated UTXOWeight
					double dTaskWeight = GetTaskWeight(s1);
					double dUTXOWeight = GetUTXOWeight(s1);
					results.push_back(Pair(s1 + "_TaskWeight", dTaskWeight));
					results.push_back(Pair(s1 + "_UTXOWeight", dUTXOWeight));
				}
			}
		}
		if (!msGlobalCPID.empty()) results.push_back(Pair("Total_RAC", nTotalRAC));
		
		const Consensus::Params& consensusParams = Params().GetConsensus();
		double nBudget = 0;
		double nTotalPaid = 0;
		std::string out_Superblocks = "";
		int out_SuperblockCount = 0;
		int out_HitCount = 0;
		double out_OneDayPaid = 0;

		double out_OneWeekPaid = 0;
		double out_OneDayBudget = 0;
		double out_OneWeekBudget = 0;
		double nMagnitude = GetUserMagnitude(nBudget, nTotalPaid, iLastSuperblock, out_Superblocks, out_SuperblockCount, out_HitCount, out_OneDayPaid, out_OneWeekPaid, out_OneDayBudget, out_OneWeekBudget);

		results.push_back(Pair("Total Payments (One Day)", out_OneDayPaid));
		results.push_back(Pair("Total Payments (One Week)", out_OneWeekPaid));
		results.push_back(Pair("Total Budget (One Day)",out_OneDayBudget));
		results.push_back(Pair("Total Budget (One Week)",out_OneWeekBudget));
		results.push_back(Pair("Superblock Count (One Week)", out_SuperblockCount));
		results.push_back(Pair("Superblock Hit Count (One Week)", out_HitCount));
		results.push_back(Pair("Superblock List", out_Superblocks));
		double dLastPayment = GetPaymentByCPID(sCPID);
		results.push_back(Pair("Last Superblock Height", iLastSuperblock));
		nBudget = CSuperblock::GetPaymentsLimit(iLastSuperblock) / COIN;
		results.push_back(Pair("Last Superblock Budget", nBudget));
		results.push_back(Pair("Last Superblock Payment", dLastPayment));
		results.push_back(Pair("Magnitude (One-Day)", mnMagnitudeOneDay));
		results.push_back(Pair("Magnitude (One-Week)", nMagnitude));
	}
	else if (sItem == "racdecay")
	{
		double dMachineCredit = 110;
		if (params.size() == 2) dMachineCredit = cdbl(params[1].get_str(), 0);
		double dOldRAC = 0;
		double dAdditiveRAC = 0;
		double dNewRAC = 0;
		for (int iDay = 1; iDay <= 50; iDay++)
		{
		    dOldRAC = BoincDecayFunction(86400) * dNewRAC;
			dAdditiveRAC = (1 - BoincDecayFunction(86400)) * dMachineCredit;
			// RAC(new) = RAC(old)*d(t) + (1-d(t))*credit(new)
			dNewRAC = dOldRAC + dAdditiveRAC;
			results.push_back(Pair("Day " + RoundToString(iDay,0) + "(" + RoundToString(dAdditiveRAC, 0) + ")", dNewRAC));
		}
	}
	else if (sItem == "datalist")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify type: IE 'exec datalist PRAYER'.  Optionally you may enter a lookback period in days: IE 'run datalist PRAYER 30'.");
		std::string sType = params[1].get_str();
		double dDays = 30;
		if (params.size() == 3)
		{ 
			dDays = cdbl(params[2].get_str(),0);
		}
		int iSpecificEntry = 0;
		std::string sEntry = "";
		UniValue aDataList = GetDataList(sType, (int)dDays, iSpecificEntry, sEntry);
		return aDataList;
	}
	else if (sItem == "unbanked")
	{
		std::string sUnbanked = GetBoincUnbankedReport("pool");
		std::vector<std::string> vInput = Split(sUnbanked.c_str(),"<ROW>");
		for (int i = 0; i < (int)vInput.size(); i++)
		{
			double dRosettaID = cdbl(GetElement(vInput[i], ",", 0),0);
			if (dRosettaID > 0)
			{
				std::string sCPID = GetCPIDByRosettaID(dRosettaID);
				if (sCPID.empty()) sCPID = "CPID_NOT_ON_FILE";
				results.push_back(Pair(sCPID, dRosettaID));
			}
		}
	}
	else if (sItem == "leaderboard")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nLastDCHeight = GetLastDCSuperblockWithPayment(chainActive.Tip()->nHeight);
		UniValue aDataList = GetLeaderboard(nLastDCHeight);
		return aDataList;
	}
	else if (sItem == "clearcache")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify type: IE 'run clearcache PRAYER'.");
		std::string sType = params[1].get_str();
		ClearCache(sType);
		results.push_back(Pair("Cleared",sType));
	}
	else if (sItem == "writecache")
	{
		if (params.size() != 4)
			throw runtime_error("You must specify type, key and value: IE 'run writecache type key value'.");
		std::string sType = params[1].get_str();
		std::string sKey = params[2].get_str();
		std::string sValue = params[3].get_str();
		WriteCache(sType,sKey,sValue,GetAdjustedTime());
		results.push_back(Pair("WriteCache",sValue));
	}
	else if (sItem == "sins")
	{
		std::string sEntry = "";
		int iSpecificEntry = 0;
		UniValue aDataList = GetDataList("SIN", 7, iSpecificEntry, sEntry);
		return aDataList;
	}
	else if (sItem == "memorizeprayers")
	{
		MemorizeBlockChainPrayers(false);
		results.push_back(Pair("Memorized",1));
	}
	else if (sItem == "readverse")
	{
		if (params.size() != 3 && params.size() != 4)
			throw runtime_error("You must specify Book and Chapter: IE 'readverse CO2 10'.  Optionally you may enter the VERSE #, IE: 'readverse CO2 10 2'.  To see a list of books: run getbooks.");
		std::string sBook = params[1].get_str();
		int iChapter = cdbl(params[2].get_str(),0);
		int iVerse = 0;
		if (params.size()==4) iVerse = cdbl(params[3].get_str(),0);
		results.push_back(Pair("Book",sBook));
		results.push_back(Pair("Chapter",iChapter));
		if (iVerse > 0) results.push_back(Pair("Verse",iVerse));
		int iStart=0;
		int iEnd=0;
		GetBookStartEnd(sBook,iStart,iEnd);
		for (int i = iVerse; i < 99; i++)
		{
			std::string sVerse = GetVerse(sBook,iChapter,i,iStart-1,iEnd);
			if (iVerse > 0 && i > iVerse) break;
			if (!sVerse.empty())
			{
				std::string sKey = sBook + " " + RoundToString(iChapter,0) + ":" + RoundToString(i,0);
			    results.push_back(Pair(sKey,sVerse));
			}
		}
	
	}
	else if (sItem == "bookname")
	{
		std::string sBookName = params[1].get_str();
		std::string sReversed = GetBookByName(sBookName);
		results.push_back(Pair(sBookName,sReversed));
	}
	else if (sItem == "books")
	{
		for (int i = 0; i < 66; i++)
		{
			std::string sBookName = GetBook(i);
			std::string sReversed = GetBookByName(sBookName);
			results.push_back(Pair(sBookName,sReversed));
		}
	}
	else if (sItem == "version")
	{
		results.push_back(Pair("Version","1.1"));
	}
	else
	{
		results.push_back(Pair("Error","Unknown command: " + sItem));
	}
	return results;
}


UniValue getmempoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"bytes\": xxxxx,              (numeric) Sum of all tx sizes\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee for tx to be accepted\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

std::string AddBlockchainMessages(std::string sAddress, std::string sType, std::string sPrimaryKey, 
	std::string sHTML, CAmount nAmount, std::string& sError)
{
	CBitcoinAddress cbAddress(sAddress);
    if (!cbAddress.IsValid()) 
	{
		sError = "Invalid Destination Address";
		return "";
	}
    CWalletTx wtx;
 	std::string sMessageType      = "<MT>" + sType + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey + "</MK>";
	std::string s1 = "<BCM>" + sMessageType + sMessageKey;
	CAmount curBalance = pwalletMain->GetBalance();
	if (curBalance < nAmount)
	{
		sError = "Insufficient funds (Unlock Wallet).";
		return "";
	}

    CScript scriptPubKey = GetScriptForDestination(cbAddress.Get());
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;
	bool fSubtractFeeFromAmount = false;
	int iStepLength = (MAX_MESSAGE_LENGTH - 100) - s1.length();
	if (iStepLength < 1) return "";
	bool bLastStep = false;
	int iParts = cdbl(RoundToString(sHTML.length() / iStepLength, 2), 0);
	if (iParts < 1) iParts = 1;
	for (int i = 0; i <= (int)sHTML.length(); i += iStepLength)
	{
		int iChunkLength = sHTML.length() - i;
		if (iChunkLength <= iStepLength) bLastStep = true;
		if (iChunkLength > iStepLength) 
		{
			iChunkLength = iStepLength;
		} 
		std::string sChunk = sHTML.substr(i, iChunkLength);
		if (bLastStep) sChunk += "</BCM>";
	    CRecipient recipient = {scriptPubKey, nAmount / iParts, fSubtractFeeFromAmount, false, false, false, "", "", ""};
		s1 += sChunk;
		recipient.Message = s1;
		LogPrintf("\r\n Creating TXID #%f, ChunkLen %f, with Chunk %s \r\n",(double)i,(double)iChunkLength,s1.c_str());
		s1 = "";
		vecSend.push_back(recipient);
	}
	
    
	bool fUseInstantSend = false;
	bool fUsePrivateSend = false;

    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet,
                                         sError, NULL, true, fUsePrivateSend ? ONLY_DENOMINATED : ALL_COINS, fUseInstantSend)) 
	{
		if (!sError.empty())
		{
			return "";
		}

        if (!fSubtractFeeFromAmount && nAmount + nFeeRequired > pwalletMain->GetBalance())
		{
            sError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
			return "";
		}
    }
    if (!pwalletMain->CommitTransaction(wtx, reservekey, fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))
    {
		sError = "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.";
		return "";
	}

	std::string sTxId = wtx.GetHash().GetHex();
	return sTxId;
}



std::string SendCPIDMessage(std::string sAddress, CAmount nAmount, std::string sXML, std::string& sError)
{
	// 2-20-2018 - R. ANDREWS - BIBLEPAY
	const Consensus::Params& consensusParams = Params().GetConsensus();
    CBitcoinAddress address(sAddress);
    if (!address.IsValid())
	{
		sError = "Invalid Destination Address";
		return sError;
	}
    CWalletTx wtx;
	wtx.sTxMessageConveyed = sXML;
    SendMoneyToDestinationWithMinimumBalance(address.Get(), nAmount, nAmount, wtx, sError);
	if (!sError.empty())
	{
		return "";
	}
    return wtx.GetHash().GetHex().c_str();
}


std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, bool Sign, std::string& sError)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CBitcoinAddress address(sAddress);
    if (!address.IsValid())
	{
		sError = "Invalid Destination Address";
		return sError;
	}
    CAmount nAmount = AmountFromValue(dStorageFee);
	CAmount nMinimumBalance = AmountFromValue(dStorageFee);
    CWalletTx wtx;
	boost::to_upper(sPrimaryKey); // DC Message can't be found if not uppercase
	boost::to_upper(sType);
	std::string sNonceValue = RoundToString(GetAdjustedTime(), 0);
	// Add immunity to replay attacks (Add message nonce)
 	std::string sMessageType      = "<MT>" + sType  + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey   + "</MK>";
	std::string sMessageValue     = "<MV>" + sValue + "</MV>";
	std::string sNonce            = "<NONCE>" + sNonceValue + "</NONCE>";
	std::string sMessageSig = "";
	// 1-29-2018 -- Allow Dev to Sign Blockchain Message with Spork Key
	if (Sign)
	{
		std::string sSignature = "";
		bool bSigned = SignStake(consensusParams.FoundationAddress, sValue + sNonceValue, sError, sSignature);
		if (bSigned) sMessageSig = "<SPORKSIG>" + sSignature + "</SPORKSIG>";
		LogPrintf(" Signing Nonce%f , With spork Sig %s on message %s  \n", (double)GetAdjustedTime(), 
			 sMessageSig.c_str(), sValue.c_str());
	}

	std::string s1 = sMessageType + sMessageKey + sMessageValue + sNonce + sMessageSig;
	wtx.sTxMessageConveyed = s1;
    SendMoneyToDestinationWithMinimumBalance(address.Get(), nAmount, nMinimumBalance, wtx, sError);
	if (!sError.empty()) return "";
    return wtx.GetHash().GetHex().c_str();
}

std::string TimestampToHRDate(double dtm)
{
	if (dtm == 0) return "1-1-1970 00:00:00";
	if (dtm > 9888888888) return "1-1-2199 00:00:00";
	std::string sDt = DateTimeStrFormat("%m-%d-%Y %H:%M:%S",dtm);
	return sDt;
}

int MonthToInt(std::string sMonth)
{
	boost::to_upper(sMonth);
	if (sMonth == "JAN") return 1;
	if (sMonth == "FEB") return 2;
	if (sMonth == "MAR") return 3;
	if (sMonth == "APR") return 4;
	if (sMonth == "MAY") return 5;
	if (sMonth == "JUN") return 6;
	if (sMonth == "JUL") return 7;
	if (sMonth == "AUG") return 8;
	if (sMonth == "SEP") return 9;
	if (sMonth == "OCT") return 10;
	if (sMonth == "NOV") return 11;
	if (sMonth == "DEC") return 12;
	return 0;
}

time_t SyntheticUTCTime(const struct tm *tm) 
{
    // Month-to-day offset for non-leap-years.
    static const int month_day[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    // Most of the calculation is easy; leap years are the main difficulty.
    int month = tm->tm_mon % 12;
    int year = tm->tm_year + tm->tm_mon / 12;
    if (month < 0) 
	{   // Negative values % 12 are still negative.
        month += 12;
        --year;
    }

    // This is the number of Februaries since 1900.
    const int year_for_leap = (month > 1) ? year + 1 : year;

    time_t rt = tm->tm_sec                             // Seconds
        + 60 * (tm->tm_min                          // Minute = 60 seconds
        + 60 * (tm->tm_hour                         // Hour = 60 minutes
        + 24 * (month_day[month] + tm->tm_mday - 1  // Day = 24 hours
        + 365 * (year - 70)                         // Year = 365 days
        + (year_for_leap - 69) / 4                  // Every 4 years is     leap...
        - (year_for_leap - 1) / 100                 // Except centuries...
        + (year_for_leap + 299) / 400)));           // Except 400s.
    return rt < 0 ? -1 : rt;
}
	
int64_t StringToUnixTime(std::string sTime)
{
	//Converts a string timestamp DD MMM YYYY HH:MM:SS TZ, such as: "27 Feb 2018, 17:35:55 UTC" to an int64_t UTC UnixTimestamp
	std::vector<std::string> vT = Split(sTime.c_str()," ");
	if (vT.size() < 5) 
	{
		return 0;
	}
	int iDay = cdbl(vT[0],0);
	int iMonth = MonthToInt(vT[1]);
	int iYear = cdbl(strReplace(vT[2],",",""),0);
	std::vector<std::string> vHours = Split(vT[3],":");
	if (vHours.size() < 3) 
	{
		return 0;
	}
	int iHour = cdbl(vHours[0], 0);
	int iMinute = cdbl(vHours[1], 0);
	int iSecond = cdbl(vHours[2], 0);
	struct tm tm;
	tm.tm_year = iYear-1900;
	tm.tm_mon = iMonth-1;
	tm.tm_mday = iDay;
	tm.tm_hour = iHour;
	tm.tm_min = iMinute;
	tm.tm_sec = iSecond;
	return SyntheticUTCTime(&tm);
}


UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string& outEntry)
{
	int64_t nEpoch = GetAdjustedTime() - (iMaxAgeInDays * 86400);
	if (nEpoch < 0) nEpoch = 0;
    UniValue ret(UniValue::VOBJ);
	boost::to_upper(sType);
	if (sType=="PRAYERS") sType="PRAYER";  // Just in case the user specified PRAYERS
	ret.push_back(Pair("DataList",sType));
	int iPos = 0;
	int iTotalRecords = 0;
    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length())==sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
		     	int64_t nTimestamp = mvApplicationCacheTimestamp[(*ii).first];
			
				if (nTimestamp > nEpoch || nTimestamp == 0)
				{
						iTotalRecords++;
						std::string sValue = mvApplicationCache[(*ii).first];
						std::string sLongValue = sPrimaryKey + " - " + sValue;
						if (iPos==iSpecificEntry) outEntry = sValue;
						std::string sTimestamp = TimestampToHRDate((double)nTimestamp);
					 	ret.push_back(Pair(sPrimaryKey + " (" + sTimestamp + ")", sValue));
						iPos++;
				}
			}
		}
	}
	iSpecificEntry++;
	if (iSpecificEntry >= iTotalRecords) iSpecificEntry=0;  // Reset the iterator.
	return ret;
}



UniValue invalidateblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "invalidateblock \"hash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, Params().GetConsensus(), pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "reconsiderblock \"hash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ReconsiderBlock(state, pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

std::string GetDCCElement(std::string sData, int iElement)
{
    std::vector<std::string> vDecoded = Split(sData.c_str(),";");
	if (vDecoded.size() < 5) return "";
	std::string sCPID = vDecoded[0];
	std::string sPubKey = vDecoded[2];
	std::string sUserId = vDecoded[3];
	std::string sMessage = sCPID + ";" + vDecoded[1] + ";" + sPubKey + ";" + sUserId;
	std::string sSig = vDecoded[4];
	std::string sError = "";
	bool fSigned = CheckStakeSignature(sPubKey, sSig, sMessage, sError);
	if (fSigned) return vDecoded[iElement];
	return "";
}


std::vector<std::string> GetListOfDCCS(std::string sSearch)
{
	// Return a list of Distributed Computing Participants - R Andrijas - Biblepay - 1-29-2018
	std::string sType = "DCC";
	std::string sOut = "";
	boost::to_upper(sSearch);
    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length())==sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
				std::string sValue = mvApplicationCache[(*ii).first];
				std::string sCPID = GetDCCElement(sValue, 0);
				std::string sAddress = GetDCCElement(sValue, 2);
				boost::to_upper(sAddress);
				boost::to_upper(sCPID);
				if (!sSearch.empty()) if (sSearch==sCPID || sSearch == sAddress)
				{
					sOut += sValue + "<ROW>";
				}
				if (sSearch.empty() && !sCPID.empty()) sOut += sValue + "<ROW>";
			}
		}
	}
	std::vector<std::string> vCPID = Split(sOut.c_str(),"<ROW>");
	return vCPID;
}


std::string GetDCCPublicKey(const std::string& cpid)
{
	if (cpid.empty()) return "";
	int iMonths = 11;
    int64_t iMaxSeconds = 60 * 24 * 30 * iMonths * 60;
    std::string sData = RetrieveDCCWithMaxAge(cpid, iMaxSeconds);
    if (sData.empty())
	{
		return "";
	}
    // DCC data structure: CPID,hashRand,PubKey,ResearcherID,Sig(base64Enc)
	std::string sElement = GetDCCElement(sData, 2); // Public Key
	return sElement;
}

void TouchDailyMagnitudeFile()
{
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";
	FILE *outMagFile = fopen(sDailyMagnitudeFile.c_str(),"w");
	std::string sDCC = GetRandHash().GetHex();
    fputs(sDCC.c_str(), outMagFile);
	fclose(outMagFile);
}

std::string AssociateDCAccount(std::string sProjectId, std::string sBoincEmail, std::string sBoincPassword, std::string sUnbankedPublicKey, bool fForce)
{
	if (sBoincEmail.empty()) return "E-MAIL_EMPTY";
	if (sBoincPassword.empty()) return "BOINC_PASSWORD_EMPTY";
	std::string sPWHash = GetBoincPasswordHash(sBoincPassword, sBoincEmail);
	std::string sAuth = GetBoincAuthenticator(sProjectId, sBoincEmail, sPWHash);
	int nUserId = GetBoincResearcherUserId(sProjectId, sAuth);
	std::string sCPID = "";
	std::string sCode = GetBoincResearcherHexCodeAndCPID(sProjectId, nUserId, sCPID);
	if (sCPID.empty()) return "INVALID_CREDENTIALS";
	std::string sError = "";
	AdvertiseDistributedComputingKey(sProjectId, sAuth, sCPID, nUserId, fForce, sUnbankedPublicKey, sError);
	return sError;
}

std::string ChopLast(std::string sMyChop)
{
	if (sMyChop.length() > 1) sMyChop = sMyChop.substr(0,sMyChop.length()-1);
	return sMyChop;
}


double GetPaymentByCPID(std::string CPID)
{
	// 2-10-2018 - R ANDREW - BIBLEPAY - Provide ability to return last payment amount (in most recent superblock) for a given CPID
	std::string sDCPK = GetDCCPublicKey(CPID);
	if (sDCPK.empty()) return -2;
	if (CPID.empty()) return -3;
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iNextSuperblock = 0;  
	int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	CBlockIndex* pindex = FindBlockByHeight(iLastSuperblock);
	CBlock block;
	double nTotalBlock = 0;
	double nBudget = 0;
	if (pindex==NULL) return -1;
	double nTotalPaid=0;
	nTotalBlock=0;
	if (ReadBlockFromDisk(block, pindex, consensusParams, "GetPaymentByCPID")) 
	{
		nBudget = CSuperblock::GetPaymentsLimit(iLastSuperblock) / COIN;
		for (unsigned int i = 1; i < block.vtx[0].vout.size(); i++)
		{
			std::string sRecipient = PubKeyToAddress(block.vtx[0].vout[i].scriptPubKey);
			double dAmount = block.vtx[0].vout[i].nValue/COIN;
			nTotalBlock += dAmount;
			if (Contains(sDCPK, sRecipient))
			{
				nTotalPaid += dAmount;
			}
		}
	}
	else
	{
		return -1;
	}
	if (nBudget == 0 || nTotalBlock == 0) return -1;
	if (nBudget < 21000 || nTotalBlock < 21000) return -1; 
	bool bSuperblockHit = (nTotalBlock > nBudget-100);
	if (!bSuperblockHit) return -1;
	return nTotalPaid;
}

int GetResearcherCount(std::string recipient, std::string RecipList)
{
	int nCount = 0;
	std::vector<std::string> vInput = Split(RecipList.c_str(), ",");
	for (int i = 0; i < (int)vInput.size(); i++)
	{
		std::string R = vInput[i];
		if (R == recipient) nCount++;
	}
	return nCount;
}


double GetBlockMagnitude(int nChainHeight)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	if (chainActive.Tip() == NULL) return 0;
	if (nChainHeight < 1) return 0;
	if (nChainHeight > chainActive.Tip()->nHeight) return 0;
	int nHeight = GetLastDCSuperblockWithPayment(nChainHeight);
	CBlockIndex* pindex = FindBlockByHeight(nHeight);
	CBlock block;
	double nParticipants = 0;
	double nTotalBlock = 0;
	if (ReadBlockFromDisk(block, pindex, consensusParams, "GetBlockMagnitude")) 
	{
		  for (unsigned int i = 1; i < block.vtx[0].vout.size(); i++)
		  {
				double dAmount = block.vtx[0].vout[i].nValue/COIN;
				nTotalBlock += dAmount;
				nParticipants++;
		  }
    }
	if (nTotalBlock <= 20000) return 0;
	double dPODCDiff = pow(nParticipants, 1.4);
    return dPODCDiff;
}



UniValue GetLeaderboard(int nHeight)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	CBlockIndex* pindex = FindBlockByHeight(nHeight);
	CBlock block;
	double nTotalBlock = 0;
	vector<pair<double, std::string> > vLeaderboard;
    UniValue ret(UniValue::VOBJ);
   
	if (ReadBlockFromDisk(block, pindex, consensusParams, "GetLeaderboard")) 
	{
		  vLeaderboard.reserve(block.vtx[0].vout.size());
		  for (unsigned int i = 1; i < block.vtx[0].vout.size(); i++)
		  {
				double dAmount = block.vtx[0].vout[i].nValue/COIN;
				nTotalBlock += dAmount;
		  }
		  std::string Recips = "";
		  for (unsigned int i = 1; i < block.vtx[0].vout.size(); i++)
		  {
			    std::string sRecipient = PubKeyToAddress(block.vtx[0].vout[i].scriptPubKey);
				double dAmount = block.vtx[0].vout[i].nValue/COIN;
				double nMagnitude = (dAmount / (nTotalBlock+.01)) * 1000;
				int nResearchCount = GetResearcherCount(sRecipient, Recips);
				std::string sCPID = GetCPIDByAddress(sRecipient, nResearchCount);
				Recips += sRecipient + ",";
				if (sCPID.empty()) sCPID = sRecipient;
				vLeaderboard.push_back(make_pair(nMagnitude, sCPID));
		  }
   	
		  sort(vLeaderboard.begin(), vLeaderboard.end());
		  ret.push_back(Pair("Leaderboard Report",GetAdjustedTime()));
		  ret.push_back(Pair("Height", nHeight));
		  ret.push_back(Pair("Total Block", nTotalBlock));

	   	  BOOST_REVERSE_FOREACH(const PAIRTYPE(double, std::string)& item, vLeaderboard)
          {
			  std::string sCPID = item.second;
			  double nMagnitude = item.first;
		 	  ret.push_back(Pair(sCPID, nMagnitude));
		  }
	}
    return ret;
    
}


int GetLastDCSuperblockWithPayment(int nChainHeight)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iNextSuperblock = 0;  
	for (int b = nChainHeight; b > 1; b--)
	{
		int iLastSuperblock = GetLastDCSuperblockHeight(b+1, iNextSuperblock);
		if (iLastSuperblock == b)
		{
			CBlockIndex* pindex = FindBlockByHeight(b);
			CBlock block;
			double nTotalBlock = 0;
			if (ReadBlockFromDisk(block, pindex, consensusParams, "GetLastDCSuperblockWithPayments")) 
			{
				  double nBudget = CSuperblock::GetPaymentsLimit(iLastSuperblock) / COIN;
				  nTotalBlock=0;
				  for (unsigned int i = 1; i < block.vtx[0].vout.size(); i++)
				  {
						double dAmount = block.vtx[0].vout[i].nValue/COIN;
						nTotalBlock += dAmount;
				  }
   				  if (nTotalBlock > nBudget-100) return b;
			}
		}
	}
	return 0;
}



double GetUserMagnitude(double& nBudget, double& nTotalPaid, int& out_iLastSuperblock, std::string& out_Superblocks, int& out_SuperblockCount, int& out_HitCount, double& out_OneDayPaid, double& out_OneWeekPaid, double& out_OneDayBudget, double& out_OneWeekBudget)
{
	std::string sPK = GetMyPublicKeys();
	// Query actual magnitude from last superblock
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iNextSuperblock = 0;  
	for (int b = chainActive.Tip()->nHeight; b > 1; b--)
	{
		int iLastSuperblock = GetLastDCSuperblockHeight(b+1, iNextSuperblock);
		if (iLastSuperblock == b)
		{
			out_SuperblockCount++;
			CBlockIndex* pindex = FindBlockByHeight(b);
			CBlock block;
			double nTotalBlock = 0;
			if (ReadBlockFromDisk(block, pindex, consensusParams, "MemorizeBlockChainPrayers")) 
			{
					  nBudget = CSuperblock::GetPaymentsLimit(iLastSuperblock) / COIN;
					  nTotalPaid=0;
					  nTotalBlock=0;
					  int Age = GetAdjustedTime() - block.GetBlockTime();
							
					  for (unsigned int i = 1; i < block.vtx[0].vout.size(); i++)
					  {
				            std::string sRecipient = PubKeyToAddress(block.vtx[0].vout[i].scriptPubKey);
							double dAmount = block.vtx[0].vout[i].nValue/COIN;
							nTotalBlock += dAmount;
							if (Contains(sPK, sRecipient))
							{
								nTotalPaid += dAmount;
								if (Age > 0 && Age < 86400)
								{
									out_OneDayPaid += dAmount;
								}
								if (Age > 0 && Age < (7*86400)) out_OneWeekPaid += dAmount;
							}
					  }
					  if (nTotalBlock > nBudget-100) 
					  {
						    if (out_iLastSuperblock == 0) out_iLastSuperblock = iLastSuperblock;
						    out_Superblocks += RoundToString(b,0) + ",";
							out_HitCount++;
							if (Age > 0 && Age < 86400)
							{
								out_OneDayBudget += nBudget;
							}
							if (Age > 0 && Age < (7*86400)) 
							{
								out_OneWeekBudget += nBudget;
							}
							if (Age > (7*86400)) 
							{
								break;
							}
					  }
				}
			}
		}
		if (out_OneWeekBudget > 0)
		{
			mnMagnitude = out_OneWeekPaid / out_OneWeekBudget * 1000;
		}
		if (out_OneDayBudget > 0)
		{
			mnMagnitudeOneDay = out_OneDayPaid / out_OneDayBudget * 1000;
		}
		out_Superblocks = ChopLast(out_Superblocks);
			
		return mnMagnitude;
}

std::string GetElement(std::string sIn, std::string sDelimiter, int iPos)
{
	if (sIn.empty()) return "";
	std::vector<std::string> vInput = Split(sIn.c_str(), sDelimiter);
	if (iPos < (int)vInput.size())
	{
		return vInput[iPos];
	}
	return "";
}

bool VerifyCPIDSignature(std::string sFullSig, bool bRequireEndToEndVerification, std::string& sError)
{
	if (sFullSig.empty())
	{
		sError = "CPID Empty.";
		return false;
	}
	std::string sCPID = GetElement(sFullSig, ";", 0);
	std::string sHash = GetElement(sFullSig, ";", 1);
	std::string sPK = GetElement(sFullSig, ";", 2);
    std::string sMessage = sCPID + ";" + sHash + ";" + sPK;
	std::string sSig = GetElement(sFullSig, ";", 3);
	bool bVerified = CheckStakeSignature(sPK, sSig, sMessage, sError);
	if (!bVerified)
	{
		if (fDebugMaster) LogPrint("podc", "VerifyCPIDSignature::CPID Signature Failed : %s, Addr %s  Sig %s \n", sMessage, sPK, sSig);
		return false;
	}
	if (bRequireEndToEndVerification)
	{
		std::string sDCPK = GetDCCPublicKey(sCPID);
		if (sDCPK != sPK) 
		{
			LogPrintf(" End To End CPID Verification Failed, Address does not match advertised public key for CPID %s, Advertised Addr %s, Addr %s ", sCPID.c_str(), sDCPK.c_str(), sPK.c_str());
			return false;
		}
	}
	return true;

}


bool SignCPID(std::string sCPID, std::string& sError, std::string& out_FullSig)
{
	// Sign Researcher CPID - 2/8/2018 - Rob A. - Biblepay
	if (sCPID.empty()) sCPID = GetElement(msGlobalCPID, ";", 0);
	std::string sHash = GetRandHash().GetHex();
	std::string sDCPK = GetDCCPublicKey(sCPID);
    std::string sMessage = sCPID + ";" + sHash + ";" + sDCPK;
	std::string sSignature = "";
	bool bSigned = SignStake(sDCPK, sMessage, sError, sSignature);
	if (!bSigned)
	{
		if (fDebugMaster) LogPrint("podc"," Failed to Sign CPID Signature for CPID %s, with PubKey %s, Message %s, Error %s ", 
			sCPID.c_str(), sDCPK.c_str(), sMessage.c_str(), sError.c_str());
		return false;
	}
	else
	{
		sMessage += ";" + sSignature;
	}
	out_FullSig = "<cpidsig>" + sMessage + "</cpidsig>";
	return true;
}


double GetSumOfXMLColumnFromXMLFile(std::string sFileName, std::string sObjectName, std::string sElementName)
{
	boost::filesystem::path pathIn(sFileName);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return 0;
	double dTotal = 0;
	std::string sLine = "";
	std::string sBuffer = "";
	double dTeamRequired = cdbl(GetSporkValue("team"), 0);
	double dDRMode = cdbl(GetSporkValue("dr"), 0);
    while(std::getline(streamIn, sLine))
    {
		sBuffer += sLine;
	}
	streamIn.close();
   	std::vector<std::string> vRows = Split(sBuffer.c_str(), sObjectName);
	for (int i = 0; i < (int)vRows.size(); i++)
	{
		std::string sData = vRows[i];
		double dTeam = cdbl(ExtractXML(sData,"<teamid>","</teamid>"), 0);
		double dUTXOWeight = cdbl(ExtractXML(sData,"<utxoweight>","</utxoweight>"), 0);
		double dTaskWeight = cdbl(ExtractXML(sData,"<taskweight>","</taskweight>"), 0);
		double dUnbanked = cdbl(ExtractXML(sData,"<unbanked>","</unbanked>"), 0);
		bool bTeamMatch = (dTeamRequired > 0) ? (dTeam == dTeamRequired) : true;
		if (bTeamMatch)
		{
			std::string sValue = ExtractXML(sData, "<" + sElementName + ">","</" + sElementName + ">");
			double dAvgCredit = cdbl(sValue,2);
			double dModifiedCredit = GetResearcherCredit(dDRMode, dAvgCredit, dUTXOWeight, dTaskWeight, dUnbanked);
			dTotal += dModifiedCredit;
			//LogPrintf(" Adding %f from RAC of %f Grand Total %f \n", dModifiedCredit, dAvgCredit, dTotal);
		}
    }
	return dTotal;
}

double GetResearcherCredit(double dDRMode, double dAvgCredit, double dUTXOWeight, double dTaskWeight, double dUnbanked)
{

	// Rob Andrews - BiblePay - 02-21-2018 - Add ability to run in distinct modes (via sporks):
	// 0) Heavenly               = UTXOWeight * TaskWeight * RAC = Magnitude
	// 1) Possessed by Tasks     = TaskWeight * RAC = Magnitude  
	// 2) Possessed by UTXO      = UTXOWeight * RAC = Magnitude
	// 3) The-Law                = RAC = Magnitude
	// 4) DR (Disaster-Recovery) = Proof-Of-BibleHash Only (Heat Mining only)
	double dModifiedCredit = 0;
	
	if (dDRMode == 0)
	{
		dModifiedCredit = dAvgCredit * (dUTXOWeight/100) * (dTaskWeight/100);
		if (dUnbanked==1) dModifiedCredit = dAvgCredit * 1 * 1;
	}
	else if (dDRMode == 1)
	{
		dModifiedCredit = dAvgCredit * (dUTXOWeight/100);
		if (dUnbanked==1) dModifiedCredit = dAvgCredit * 1 * 1;
	}
	else if (dDRMode == 2)
	{
		dModifiedCredit = dAvgCredit * (dTaskWeight/100);
		if (dUnbanked==1) dModifiedCredit = dAvgCredit * 1 * 1;
	}
	else if (dDRMode == 3)
	{
		dModifiedCredit = dAvgCredit;
	}
	else if (dDRMode == 4)
	{
		dModifiedCredit = 0;
	}
	return dModifiedCredit;
}
	
bool IsInList(std::string sData, std::string sDelimiter, std::string sValue)
{
	std::vector<std::string> vRows = Split(sData.c_str(), sDelimiter);
	for (int i = 0; i < (int)vRows.size(); i++)
	{
		std::string sLocalValue = vRows[i];
		if (!sLocalValue.empty() && sValue==sLocalValue)
		{
			return true;
		}
	}
	return false;
}

void ClearSanctuaryMemories()
{
	double nMaximumChatterAge = GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	int64_t nMinimumMemory = GetAdjustedTime() - (nMaximumChatterAge * 7);
	PurgeCacheAsOfExpiration("UTXOWeight", nMinimumMemory);
	PurgeCacheAsOfExpiration("CPIDTasks", nMinimumMemory);
	PurgeCacheAsOfExpiration("Unbanked", nMinimumMemory);
}

bool FilterFile(int iBufferSize, std::string& sError)
{
	std::vector<std::string> vCPIDs = GetListOfDCCS("");
    std::string buffer;
    std::string line;
    std::string sTarget = GetSANDirectory2() + "user";
	std::string sFiltered = GetSANDirectory2() + "filtered";
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";

    if (!boost::filesystem::exists(sTarget.c_str())) 
	{
        sError = "DCC input file does not exist.";
        return false;
    }

	FILE *outFile = fopen(sFiltered.c_str(),"w");
    std::string sBuffer = "";
	int iBuffer = 0;
	int64_t nMaxAge = (int64_t)GetSporkDouble("podmaximumchatterage", (60 * 60 * 24));
		
	boost::filesystem::path pathIn(sTarget);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return false;
	std::string sConcatCPIDs = "";
	std::string sUnbankedList = GetBoincUnbankedReport("pool");
	ClearSanctuaryMemories();
	ClearCache("Unbanked");
	
	for (int i = 0; i < (int)vCPIDs.size(); i++)
	{
		std::string sCPID1 = GetDCCElement(vCPIDs[i], 0);
		double dRosettaID = cdbl(GetDCCElement(vCPIDs[i], 3), 0);
		if (!sCPID1.empty())
		{
			sConcatCPIDs += sCPID1 + ",";
			std::string sTaskList = ReadCacheWithMaxAge("CPIDTasks", sCPID1, nMaxAge);
			double dVerifyTasks = VerifyTasks(sTaskList);
			// LogPrintf(" Verifying tasks %s for %s = %f ", sTaskList.c_str(), sCPID1.c_str(), dVerifyTasks);
			WriteCache("TaskWeight", sCPID1, RoundToString(dVerifyTasks, 0), GetAdjustedTime());
			if (dRosettaID > 0 && IsInList(sUnbankedList, ",", RoundToString(dRosettaID,0)))
			{
				WriteCache("Unbanked", sCPID1, "1", GetAdjustedTime());
			}
		}
	}

	boost::to_upper(sConcatCPIDs);
	LogPrintf("CPID List concatenated %s ",sConcatCPIDs.c_str());

	// Phase 1: Scan the Combined Researcher file for all Biblepay Researchers (who have associated BiblePay Keys with Research Projects)
	// Filter the file down to BiblePay researchers:
	int iLines = 0;
	std::string sOutData = "";
    while(std::getline(streamIn, line))
    {
		  std::string sCpid = ExtractXML(line,"<cpid>","</cpid>");
		  sBuffer += line + "<ROW>";
		  iBuffer++;
		  iLines++;
		  if (iLines % 1000000 == 0) LogPrintf(" Processing DCC Line %f ",(double)iLines);
		  if (!sCpid.empty())
		  {
			 boost::to_upper(sCpid);
			 if (Contains(sConcatCPIDs, sCpid))
			 {
				for (int i = 0; i < (int)vCPIDs.size(); i++)
				{
					std::string sBiblepayResearcher = GetDCCElement(vCPIDs[i], 0);
					boost::to_upper(sBiblepayResearcher);
					if (!sBiblepayResearcher.empty() && sBiblepayResearcher == sCpid)
					{
						for (int z = 0; z < 2; z++)
						{
							// Add in the User URL and Team
							bool bAddlData = static_cast<bool>(std::getline(streamIn, line));
							if (bAddlData) sBuffer += line + "<ROW>";
						}

						double dUTXOWeight = cdbl(ReadCacheWithMaxAge("UTXOWeight", sCpid, nMaxAge), 0);
						double dTaskWeight = cdbl(ReadCacheWithMaxAge("TaskWeight", sCpid, nMaxAge), 0);
						double dUnbanked = cdbl(ReadCacheWithMaxAge("Unbanked", sCpid, nMaxAge), 0);
						std::string sExtra = "<utxoweight>" + RoundToString(dUTXOWeight, 0) 
							+ "</utxoweight>\r\n<taskweight>" 
							+ RoundToString(dTaskWeight, 0) + "</taskweight><unbanked>" + RoundToString(dUnbanked, 0) + "</unbanked>\r\n";
						std::string sData = FilterBoincData(sBuffer, "<user>","</user>", sExtra);
						sOutData += sData;
						sBuffer = "";
					}
			 	}
			 }
			 else
			 {
				 sBuffer="";
			 }
		  }
    }
	fputs(sOutData.c_str(), outFile);
	streamIn.close();
    fclose(outFile);
	//  Phase II : Normalize the file for Biblepay (this process asseses the magnitude of each BiblePay Researcher relative to one another, with 100 being the leader, 0 being a researcher with no activity)
	//  We measure users by RAC - the BOINC Decay function: expavg_credit.  This is the half-life of the users cobblestone emission over a one month period.

	double dTotalRAC = GetSumOfXMLColumnFromXMLFile(sFiltered, "<user>", "expavg_credit");
	if (dTotalRAC < 10)
	{
		sError = "Total DCC credit less than the project minimum.  Unable to calculate magnitudes.";
		return false;
	}

	dTotalRAC += 100; // Ensure magnitude never exceeds 1000 due to rounding errors.

	// Emit the BiblePay DCC Leaderboard file, and then stamp it with the Biblepay hash
	// Leaderboard file format:  Biblepay-Public-Key-Compressed, DCC-CPID, DCC-Magnitude <rowdelimiter>
	boost::filesystem::path pathFiltered(sFiltered);
    std::ifstream streamFiltered;
    streamFiltered.open(pathFiltered.string().c_str());
	if (!streamFiltered) 
	{
		sError = "Unable to open filtered file";
		return false;
	}

	std::string sUser = "";
	std::string sDCC = "";
	double dTeamRequired = cdbl(GetSporkValue("team"), 0);
	double dDRMode = cdbl(GetSporkValue("dr"), 0);

    while(std::getline(streamFiltered, line))
    {
		sUser += line;
		if (Contains(line, "</user>"))
		{
			std::string sCPID = ExtractXML(sUser,"<cpid>","</cpid>");
			double dAvgCredit = cdbl(ExtractXML(sUser,"<expavg_credit>","</expavg_credit>"), 4);
			double dTeam = cdbl(ExtractXML(sUser,"<teamid>","</teamid>"), 0);
			std::string sRosettaID = ExtractXML(sUser,"<id>","</id>");
			double dUTXOWeight = cdbl(ReadCacheWithMaxAge("UTXOWeight", sCPID, nMaxAge),0);
			double dTaskWeight = cdbl(ReadCacheWithMaxAge("TaskWeight", sCPID, nMaxAge),0);
			double dUnbanked = cdbl(ReadCacheWithMaxAge("Unbanked", sCPID, nMaxAge), 0);
			double dModifiedCredit = GetResearcherCredit(dDRMode, dAvgCredit, dUTXOWeight, dTaskWeight, dUnbanked);
			bool bTeamMatch = (dTeamRequired > 0) ? (dTeam == dTeamRequired) : true;
			if (bTeamMatch)
			{
				std::string BPK = GetDCCPublicKey(sCPID);
				double dMagnitude = (dModifiedCredit / dTotalRAC) * 1000;
				std::string sRow = BPK + "," + sCPID + "," + RoundToString(dMagnitude, 3) + "," + sRosettaID + "," + RoundToString(dTeam,0) 
					+ "," + RoundToString(dUTXOWeight,0) + "," + RoundToString(dTaskWeight,0) + "," + RoundToString(dTotalRAC,0) + "\n<ROW>";
				sDCC += sRow;
			}
			sUser = "";
		}
    }
	streamFiltered.close();
    // Phase 3: Create the Daily Magnitude Contract and hash it
	FILE *outMagFile = fopen(sDailyMagnitudeFile.c_str(),"w");
    fputs(sDCC.c_str(), outMagFile);
	fclose(outMagFile);

	uint256 uhash = GetDCCHash(sDCC);
	// Persist the contract in memory for verification
    WriteCache("dcc", "contract", sDCC, GetAdjustedTime());
	WriteCache("dcc", "contract_hash", uhash.GetHex(), GetAdjustedTime());

    return true;
}



bool FileExists2(std::string sPath)
{
	if (!sPath.empty())
	{
		try
		{
			boost::filesystem::path pathImg(sPath);
			return (boost::filesystem::exists(pathImg));
		}
		catch(const boost::filesystem::filesystem_error& e)
		{
			return false;
		}
		catch (const std::exception& e) 
		{
			return false;
		}
		catch(...)
		{
			return false;
		}

	}
	return false;
}


std::string FilterBoincData(std::string sData, std::string sRootElement, std::string sEndElement, std::string sExtra)
{
	std::vector<std::string> vRows = Split(sData.c_str(),"<ROW>");
	std::string sOut = sRootElement + "\r\n";
	for (int i = vRows.size()-1; i >= 0; i--)
	{
		// Filter backwards through the file until we find the root XML element:
		std::string sLine = vRows[i];
		if (sLine == sRootElement)
		{
			sOut += sExtra + "\r\n" + sEndElement + "\r\n";
			return sOut;
		}
		sOut += sLine + "\r\n";
	}
	return "";
}


std::string NameFromURL2(std::string sURL)
{
	std::vector<std::string> vURL = Split(sURL.c_str(),"/");
	std::string sOut = "";
	if (vURL.size() > 0)
	{
		std::string sName = vURL[vURL.size()-1];
		sName=strReplace(sName,"'","");
		std::string sDir = GetSANDirectory2();
		std::string sPath = sDir + sName;
		return sPath;
	}
	return "";
}


std::string SystemCommand2(const char* cmd)
{
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while(!feof(pipe))
    {
        if(fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }
    pclose(pipe);
    return result;
}

std::string GenerateNewAddress(std::string& sError, std::string sName)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
	{
		if (!pwalletMain->IsLocked(true))
			pwalletMain->TopUpKeyPool();
		// Generate a new key that is added to wallet
		CPubKey newKey;
		if (!pwalletMain->GetKeyFromPool(newKey))
		{
			sError = "Keypool ran out, please call keypoolrefill first";
			return "";
		}
		CKeyID keyID = newKey.GetID();
		std::string strAccount = "";
		pwalletMain->SetAddressBook(keyID, strAccount, sName);
		return CBitcoinAddress(keyID).ToString();
	}
}


std::string GetBoincHostsByUser(int iRosettaID, std::string sProjectId)
{
    	std::string sProjectURL= "https://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "rosetta/hosts_user.php?userid=" + RoundToString(iRosettaID, 0);
		std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 25, 95000, 1);
		std::vector<std::string> vRows = Split(sResponse.c_str(), "<tr");
		std::string sOut = "";
        for (int j = 1; j < (int)vRows.size(); j++)
        {
			vRows[j] = strReplace(vRows[j], "</td>", "");
			std::vector<std::string> vCols = Split(vRows[j].c_str(), "<td>");
            if (vCols.size() > 6)
            {
                    double hostid = cdbl(ExtractXML(vCols[1], "hostid=", ">"),0);
					if (hostid > 0)
					{
						sOut += RoundToString(hostid,0) + ",";
					}
            }
        }
		sOut = ChopLast(sOut);
		return sOut;
}

std::string GetBoincTasksByHost(int iHostID, std::string sProjectId)
{
	std::string sOut = "";
    for (int i = 0; i < 120; i = i + 20)
    {
		std::string sProjectURL= "https://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "rosetta/results.php?hostid=" + RoundToString(iHostID, 0) + "&offset=" + RoundToString(i,0) + "&show_names=0&state=1&appid=";
		std::string sResponse = BiblepayHTTPSPost(0, "", "", "", sProjectURL, sRestfulURL, 443, "", 25, 95000, 1);
       	std::vector<std::string> vRows = Split(sResponse.c_str(), "<tr");
	    for (int j = 1; j < (int)vRows.size(); j++)
        {
					vRows[j] = strReplace(vRows[j], "</td>", "");
					vRows[j] = strReplace(vRows[j], "\n", "");
                    vRows[j] = strReplace(vRows[j], "<td align=right>", "<td>");
					std::vector<std::string> vCols = Split(vRows[j].c_str(), "<td>");
					if (vCols.size() > 6)
					{
                        std::string sTaskStartTime = vCols[3];
						double dWorkUnitID = cdbl(ExtractXML(vCols[1], "resultid=", "\""), 0);
                        std::string sStatus = vCols[5];
						boost::trim(sTaskStartTime);
						boost::trim(sStatus);
						int64_t nStartTime = StringToUnixTime(sTaskStartTime);
						if (dWorkUnitID > 0 && nStartTime > 0)
						{
							sOut += RoundToString(dWorkUnitID, 0) + "=" + RoundToString(nStartTime, 0) + ",";
						}
                    }
        }
   }
   sOut = ChopLast(sOut);
   return sOut;
}

double GetSporkDouble(std::string sName, double nDefault)
{
	double dSetting = cdbl(GetSporkValue(sName), 2);
	if (dSetting == 0) return nDefault;
	return dSetting;
}
			
bool PODCUpdate(std::string& sError, bool bForce)
{
	if (!fDistributedComputingEnabled) return false;

	std::vector<std::string> vCPIDS = Split(msGlobalCPID.c_str(),";");
	int64_t iMaxSeconds = 60 * 24 * 30 * 12 * 60;
	for (int i = 0; i < (int)vCPIDS.size(); i++)
	{
		std::string s1 = vCPIDS[i];

		if (!s1.empty())
		{
			std::string sData = RetrieveDCCWithMaxAge(s1, iMaxSeconds);
			std::string sAddress = GetDCCElement(sData, 1);
			std::string sOutstanding = "";

			if (!sData.empty() && !sAddress.empty())
			{
				std::string sUserId = GetDCCElement(sData, 3);
				int nUserId = cdbl(sUserId, 0);
				if (nUserId > 0)
				{
					std::string sHosts = GetBoincHostsByUser(nUserId, "project1");
					std::vector<std::string> vH = Split(sHosts.c_str(), ",");
					for (int j = 0; j < (int)vH.size(); j++)
					{
						double dHost = cdbl(vH[j], 0);
						std::string sTasks = GetBoincTasksByHost((int)dHost, "project1");
						std::vector<std::string> vT = Split(sTasks.c_str(), ",");
						for (int k = 0; k < (int)vT.size(); k++)
						{
							std::string sTask = vT[k];
							std::vector<std::string> vEquals = Split(sTask.c_str(), "=");
							if (vEquals.size() > 1)
							{
								std::string sTaskID = vEquals[0];
								std::string sTimestamp = vEquals[1];
								if (cdbl(sTimestamp,0) > 0 && cdbl(sTaskID,0) > 0)
								{
									// Biblepay network does not know this device started this task, add this to the UTXO transaction
									sOutstanding += sTaskID + "=" + sTimestamp + ",";
									// WriteCache("podc_start_time", sTaskID, sTimestamp, cdbl(sTimestamp,0));
									if (sOutstanding.length() > 35000) break; // Don't let them blow up the blocksize
								}
							}
						}
					}
				}
			}

			if (!sOutstanding.empty())
			{
				sOutstanding = ChopLast(sOutstanding);
				// Create PODC UTXO Transaction - R Andrews - 02-20-2018 - These tasks will be checked on the Sanctuary side, to ensure they were started at this time.  
				// If so, the UTXO COINAMOUNT * AGE * RAC will be rewarded for this task.
				double nMinimumChatterAge = GetSporkDouble("podcminimumchatterage", (60 * 60 * 4));
				std::string sCurrentState = ReadCacheWithMaxAge("CPIDTasks", s1, nMinimumChatterAge);
				double nMaximumChatterAge = GetSporkDouble("podmaximumchatterage", (60 * 60 * 24));
				std::string sOldState = ReadCacheWithMaxAge("CPIDTasks", s1, nMaximumChatterAge);
				bool bFresh = (sOldState == sOutstanding || sOutstanding == sCurrentState || (!sCurrentState.empty()));
				if (bForce) bFresh = false;
				if (!bFresh)
				{
					double dProofOfLoyaltyPercentage = cdbl(GetArg("-polpercentage", "10"), 2) / 100;
					CAmount curBalance = pwalletMain->GetUnlockedBalance(); // 3-5-2018, R Andrews
					
					CAmount nTargetValue = curBalance * dProofOfLoyaltyPercentage;
					if (nTargetValue < 1)
					{
						sError = "Unable to create PODC UTXO::Balance too low.";
						return false;
					}
					std::string sFullSig = "";
					bool bTriedToUnlock = false;
					if (!msEncryptedString.empty() && pwalletMain->IsLocked())
					{
						bTriedToUnlock = true;
						if (!pwalletMain->Unlock(msEncryptedString, false))
						{
							static int nNotifiedOfUnlockIssue = 0;
							if (nNotifiedOfUnlockIssue == 0)
							{
								sError = "Unable to unlock wallet with SecureString.";
								WriteCache("poolthread0", "poolinfo2", "Unable to unlock wallet with password provided.", GetAdjustedTime());
							}
							nNotifiedOfUnlockIssue++;
						}
					}

					// Sign
					std::string sErrorInternal = "";
		
					bool fSigned = SignCPID(s1, sErrorInternal, sFullSig);
					if (!fSigned)
					{
						sError = "Failed to Sign CPID Signature. [" + sErrorInternal + "]";
						return false;
					}
					std::string sXML = "<PODC_TASKS>" + sOutstanding + "</PODC_TASKS>" + sFullSig;

					AddBlockchainMessages(sAddress, "PODC_UPDATE", s1, sXML, nTargetValue, sErrorInternal);
					if (bTriedToUnlock) pwalletMain->Lock();

					if (!sErrorInternal.empty())
					{
						sError = sErrorInternal;
						return false;
					}
					if (fDebugMaster) LogPrint("podc","\n PODCUpdate::Signed UTXO: %s ", sXML.c_str());
					WriteCache("CPIDTasks", s1, sOutstanding, GetAdjustedTime());
				}
			}
		}
	}
	return true;
}


bool AdvertiseDistributedComputingKey(std::string sProjectId, std::string sAuth, std::string sCPID, int nUserId, bool fForce, std::string sUnbankedPublicKey, std::string &sError)
{	
     LOCK(cs_main);
     {
		 try
		 {
			
			/*
			if (!sUnbankedPublicKey.empty() && fForce)
			{
				sError = "Force is not allowed with an unbanked public key.";
				return false;
			}
			*/
			if (!sUnbankedPublicKey.empty())
			{
				CBitcoinAddress address(sUnbankedPublicKey);
				if (!address.IsValid())
				{
					sError = "Unbanked public key invalid.";
					return false;
				}
			}
            std::string sDCC = GetDCCPublicKey(sCPID);
            if (!sDCC.empty() && !fForce) 
            {
				// The fForce flag allows the researcher to re-associate the CPID to another BBP address - for example when they lose their wallet.
                sError = "ALREADY_IN_CHAIN";
                return false;
            }
			
    		std::string sPubAddress = DefaultRecAddress("Rosetta");
			if (!sUnbankedPublicKey.empty()) sPubAddress = sUnbankedPublicKey;

			std::string sDummyCPID = GetCPIDByAddress(sPubAddress, 0);
			
			if (!sDummyCPID.empty())
			{
				if (!sUnbankedPublicKey.empty())
				{
					sError = "Unbanked public key already in use.";
					return false;
				}
				sPubAddress = GenerateNewAddress(sError, "Rosetta");
				if (!sError.empty()) return false;
				if (sPubAddress.empty())
				{
					sError = "Unable to create new Biblepay Receiving Address.";
					return false;
				}
			}

            static int nLastDCCAdvertised = 0;
			const Consensus::Params& consensusParams = Params().GetConsensus();
	        if ((chainActive.Tip()->nHeight - nLastDCCAdvertised) < 5 && sUnbankedPublicKey.empty())
            {
                sError = _("A DCBTX was advertised less then 5 blocks ago. Please wait a full 5 blocks for your DCBTX to enter the chain.");
                return false;
            }

            CAmount nStakeBalance = pwalletMain->GetBalance();

            if (nStakeBalance < (1*COIN))
            {
                sError = "Balance too low to advertise DCC, 1 BBP minimum is required.";
                return false;
            }
			
            std::string sHexSet = sPubAddress;

			SetBoincResearcherHexCode(sProjectId, sAuth, sHexSet);
			std::string sCode = GetBoincResearcherHexCodeAndCPID(sProjectId, nUserId, sCPID);
			if (sHexSet != sCode) return "Unable to set boinc cookie in boinc project.";

			// Construct the DC-Burn Tx:
			// Note that we only expose the public CPID, nonce, public project userid and the researcher's payment address, but we dispose of the boinc password and e-mail address to protect the identity of the researcher.
			// The payment address is required so the Sanctuaries can airdrop payments for a given research magnitude level
			int iUnbanked = sUnbankedPublicKey.empty() ? 0 : 1;
			std::string sData = sCPID + ";" + sHexSet + ";" + sPubAddress + ";" + RoundToString(nUserId,0);
			std::string sSignature = "";
			bool bSigned = SignStake(sPubAddress, sData, sError, sSignature);
			// Only append the signature after we prove they can sign...
			if (bSigned)
			{
				sData += ";" + sSignature;
			}
			else
			{
				if (sUnbankedPublicKey.empty())
				{
					sError = "Unable to sign CPID " + sCPID;
					return false;
				}
				else
				{
					sData += ";" + sSignature;
				}
			}
			// Append the Unbanked flag
			sData += ";" + RoundToString(iUnbanked, 0);

		    // This prevents repeated DCC advertisements
            nLastDCCAdvertised = chainActive.Tip()->nHeight;
			std::string sResult = SendBlockchainMessage("DCC", sCPID, sData, 1, false, sError);
			if (!sError.empty())
			{
				return false;
			}
            return true;
	 }
	 catch (std::exception &e) 
     {
                sError = "Error: Unable to advertise DC-BurnTx";
                return false;
         }
	catch(...)
		 {
			 LogPrintf("RPC error \n");
			 sError = "RPC Error";
			 return false;
		 }
	}
}



UniValue ContributionReport()
{

	const Consensus::Params& consensusParams = Params().GetConsensus();
	int nMaxDepth = chainActive.Tip()->nHeight;
    CBlock block;
	int nMinDepth = 1;
	double dTotal = 0;
    UniValue ret(UniValue::VOBJ);

	for (int ii = nMinDepth; ii <= nMaxDepth; ii++)
	{
   			CBlockIndex* pblockindex = FindBlockByHeight(ii);
			if (ReadBlockFromDisk(block, pblockindex, consensusParams, "CONTRIBUTIONREPORT"))
			{
				LogPrintf("Reading %f ",(double)ii);

				BOOST_FOREACH(CTransaction& tx, block.vtx)
				{
					 for (int i=0; i < (int)tx.vout.size(); i++)
					 {
				 		std::string sRecipient = PubKeyToAddress(tx.vout[i].scriptPubKey);
						double dAmount = tx.vout[i].nValue/COIN;
						if (sRecipient == consensusParams.FoundationAddress)
						{
								dTotal += dAmount;
						        ret.push_back(Pair("Block ", ii));
								ret.push_back(Pair("Amount", dAmount));
								LogPrintf("Amount %f ",dAmount);
						}
					 }
				 }
			}
	}
	ret.push_back(Pair("Grand Total", dTotal));
	return ret;
}



void static TradingThread(int iThreadID)
{
	// September 17, 2017 - Robert Andrew (BiblePay)
	LogPrintf("Trading Thread started \n");
	//int64_t nLastTrade = GetAdjustedTime();
	RenameThread("biblepay-trading");
	bEngineActive=true;
	int iCall=0;
    try 
	{
        while (true) 
		{
			// Thread dies off after 5 minutes of inactivity
			if ((GetAdjustedTime() - nLastTradingActivity) > (5*60)) break;
			MilliSleep(1000);
			iCall++;
			if (iCall > 30)
			{
				iCall=0;
				std::string sError = ProcessEscrow();
				if (!sError.empty()) LogPrintf("TradingThread::Process Escrow %s \n",sError.c_str());
			    CancelOrders(true);
				sError = GetTrades();
				if (!sError.empty()) LogPrintf("TradingThread::GetTrades %s \n",sError.c_str());
			}

		}
		bEngineActive=false;
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrintf("\nTrading Thread -- terminated\n");
        bEngineActive=false;
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("\nTrading Thread -- runtime error: %s\n", e.what());
        bEngineActive=false;
		throw;
    }
}


static boost::thread_group* tradingThreads = NULL;
void StartTradingThread()
{
	if (bEngineActive) return;
	
	nLastTradingActivity = GetAdjustedTime();
    if (tradingThreads != NULL)
    {
		LogPrintf(" exiting \n");
        tradingThreads->interrupt_all();
        delete tradingThreads;
        tradingThreads = NULL;
    }

    tradingThreads = new boost::thread_group();
	int iThreadID = 0;
	tradingThreads->create_thread(boost::bind(&TradingThread, boost::cref(iThreadID)));
	LogPrintf(" Starting Trading Thread \n" );
}


