// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCPODC_H
#define RPCPODC_H

#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"

#include <univalue.h>

bool VerifyCPIDSignature(std::string sFullSig, bool bRequireEndToEndVerification, std::string& sError);
std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);
bool FilterFile(int iBufferSize, int iNextSuperblock, std::string& sError);
double GetTaskWeight(std::string sCPID);
double GetUTXOWeight(std::string sCPID);
int GetBoincTaskCount();
double GetCryptoPrice(std::string sURL);
std::string RosettaDiagnostics(std::string sEmail, std::string sPass, std::string& sError);
std::string FixRosetta(std::string sEmail, std::string sPass, std::string& sError);
std::string AssociateDCAccount(std::string sProjectId, std::string sBoincEmail, std::string sBoincPassword, std::string sUnbankedPublicKey, bool fForce);
bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature);
int GetLastDCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock);
std::string GetGithubVersion();
uint256 GetDCCHash(std::string sContract);
uint256 GetDCCFileHash();
double GetSumOfXMLColumnFromXMLFile(std::string sFileName, std::string sObjectName, std::string sElementName, double dReqSPM, double dReqSPR, double dTeamRequired, std::string sConcatCPIDs, 
	double dRACThreshhold, std::string sTeamBlacklist, int iNextSuperblock);
std::string GetCPIDByRosettaID(double dRosettaID);
std::string GetCPIDByAddress(std::string sAddress, int iOffset);
double GetBoincTeamByUserId(std::string sProjectId, int nUserId);
double GetTeamRAC();
double GetWCGRACByCPID(std::string sCPID);
double GetBoincRACByUserId(std::string sProjectId, int nUserId);
std::string GetWorkUnitResultElement(std::string sProjectId, int nWorkUnitID, std::string sElement);
std::string SetBoincResearcherHexCode(std::string sProjectId, std::string sAuthCode, std::string sHexKey);
std::string GetBoincUnbankedReport(std::string sProjectID);
std::string GetBoincResearcherHexCodeAndCPID(std::string sProjectId, int nUserId, std::string& sCPID);
int GetBoincResearcherUserId(std::string sProjectId, std::string sAuthenticator);
std::string GetBoincAuthenticator(std::string sProjectID, std::string sProjectEmail, std::string sPasswordHash);
int GetLastDCSuperblockWithPayment(int nChainHeight);
int MyRank(int nHeight);
UniValue GetLeaderboard(int nHeight);
double MyPercentile(int nHeight);
bool SubmitDistributedComputingTrigger(std::string sHex, std::string& gobjecthash, std::string& sError);
bool SignCPID(std::string sCPID, std::string& sError, std::string& out_FullSig);
std::string GetBoincHostsByUser(int iRosettaID, std::string sProjectId);
std::vector<std::string> GetListOfDCCS(std::string sSearch, bool fRequireSig);
double GetTeamPercentage(double dUserTeam, double dProjectTeam, std::string sTeamBlacklist, double dNonBiblepayTeamPercentage);
double VerifyTasks(std::string sCPID, std::string sTasks);
std::string SerializeSanctuaryQuorumTrigger(int iContractAssessmentHeight, int nEventBlockHeight, std::string sContract);
double GetQTPhase(bool fInFuture, double dPrice, int nEventHeight, double& out_PriorPrice, double& out_PriorPhase);
bool VerifySigner(std::string sXML);
double GetPBase(double& out_BTC);
std::string VerifyManyWorkUnits(std::string sProjectId, std::string sTaskIds);
int VerifySanctuarySignatures(std::string sSignatureData);
bool IsStakeSigned(std::string sXML);
std::string GetDCCElement(std::string sData, int iElement, bool fCheckSignature);
bool SubmitDistributedComputingTrigger(std::string sHex, std::string& gobjecthash, std::string& sError);
double GetStakeWeight(CTransaction tx, int64_t nTipTime, std::string sXML, bool bVerifySignature, std::string& sMetrics, std::string& sError);
std::string AttachProject(std::string sAuth);
std::string GetCPID();
double GetRosettaLocalRAC();
std::string GetAccountAuthenticator(std::string sEmail, std::string sPass);
std::string CreateNewRosettaAccount(std::string sEmail, std::string sPass, std::string sNickname);
bool IsBoincInstalled(std::string& sError);
std::string BoincCommand(std::string sCommand, std::string &sError);
int ShellCommand(std::string sCommand, std::string &sOutput, std::string &sError);
std::string GetDCCPublicKey(const std::string& cpid, bool fRequireSig);
int64_t RetrieveCPIDAssociationTime(std::string cpid);
std::string RetrieveDCCWithMaxAge(std::string cpid, int64_t iMaxSeconds);
bool PODCUpdate(std::string& sError, bool bForce, std::string sDebugInfo);
double AscertainResearcherTotalRAC();
double GetMagnitudeByAddress(std::string sAddress);
double GetUserMagnitude(std::string sListOfPublicKeys, double& nBudget, double& nTotalPaid, int& out_iLastSuperblock, std::string& out_Superblocks, int& out_SuperblockCount, int& out_HitCount, double& out_OneDayPaid, double& out_OneWeekPaid, double& out_OneDayBudget, double& out_OneWeekBudget);
UniValue UTXOReport(std::string sCPID);
bool GetTransactionTimeAndAmount(uint256 txhash, int nVout, int64_t& nTime, CAmount& nAmount);
std::string FindResearcherCPIDByAddress(std::string sSearch, std::string& out_address, double& nTotalMagnitude);
double GetPaymentByCPID(std::string CPID, int nHeight);
uint256 GetDCPAMHash(std::string sAddresses, std::string sAmounts);
uint256 GetDCPAMHashByContract(std::string sContract, int nHeight);
double GetMatureMetric(std::string sMetricName, std::string sPrimaryKey, int64_t nMaxAge, int nHeight);
std::string GetMatureString(std::string sMetricName, std::string sPrimaryKey, int64_t nMaxAge, int nHeight);
int64_t GetHistoricalMilestoneAge(int64_t nMaturityAge, int64_t nOffset);
double GetRACFromPODCProject(int iNextSuperblock, std::string sFileName, std::string sResearcherCPID, double dDRMode, double dReqSPM, double dReqSPR, double dTeamRequired,
	double dProjectFactor, double dRACThreshhold, std::string sTeamBlacklist, double& out_Team);
bool FilterPhase2(int iNextSuperblock, std::string sSourcePath, std::string sTargetPath, double dTargetTeam);
bool FilterPhase1(int iNextSuperblock, std::string sConcatCPIDs, std::string sSourcePath, std::string sTargetPath, std::vector<std::string> vCPIDs);
std::string GetBoincTasksByHost(int iHostID, std::string sProjectId);
std::string GetBoincPasswordHash(std::string sProjectPassword, std::string sProjectEmail);
int64_t GetDCCFileTimestamp();
int GetRequiredQuorumLevel(int nHeight);
void GetDistributedComputingGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts);
int GetSanctuaryCount();
double GetMinimumMagnitude();
int GetMinimumResearcherParticipationLevel();
void ClearSanctuaryMemories();
double GetMinimumRequiredUTXOStake(double dRAC, double dFactor);
std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, bool Sign, std::string sExtraPayload, std::string& sError);
bool IsMature(int64_t nTime, int64_t nMaturityAge);
std::string ToYesNo(bool bValue);
bool VoteForGobject(uint256 govobj, std::string sVoteOutcome, std::string& sError);
int64_t GetDCCFileAge();
std::string ExecuteDistributedComputingSanctuaryQuorumProcess();
std::string RetrieveCurrentDCContract(int iCurrentHeight, int iMaximumAgeAllowed);
std::string GetMyPublicKeys();
CAmount GetMoneySupplyEstimate(int nHeight);
int64_t GetStakeTargetModifierPercent(int nHeight, double nWeight);
std::string GetSANDirectory2();

#endif
