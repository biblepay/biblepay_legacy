// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "darksend.h"
#include "spork.h"
#include "utilmoneystr.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "masternode-sync.h"
#include "rpcipfs.h"
#include "rpcpog.h"
#include "rpcserver.h"

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


#ifdef MAC_OSX
#include <boost/process/system.hpp>        // for ShellCommand
#include <boost/process/io.hpp>
namespace bp = boost::process;
#endif

extern CWallet* pwalletMain;


std::string RetrieveDCCWithMaxAge(std::string cpid, int64_t iMaxSeconds)
{
	boost::to_upper(cpid); // CPID must be uppercase to retrieve
    const std::string key = "DCC;" + cpid;
    const std::string& value = mvApplicationCache[key];
	const Consensus::Params& consensusParams = Params().GetConsensus();
    int64_t iAge = chainActive.Tip() != NULL ? chainActive.Tip()->nTime - mvApplicationCacheTimestamp[key] : 0;
	return (iAge > iMaxSeconds) ? "" : value;
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


std::string GetDCCElement(std::string sData, int iElement, bool fCheckSignature)
{
    std::vector<std::string> vDecoded = Split(sData.c_str(),";");
	if (vDecoded.size() < 5 || (vDecoded.size() < (unsigned int)(iElement + 1))) return "";
	std::string sCPID = vDecoded[0];
	std::string sPubKey = vDecoded[2];
	std::string sUserId = vDecoded[3];
	std::string sMessage = sCPID + ";" + vDecoded[1] + ";" + sPubKey + ";" + sUserId;
	std::string sSig = vDecoded[4];
	std::string sError = "";
	bool fSigned = false;
	if (fCheckSignature) fSigned = CheckStakeSignature(sPubKey, sSig, sMessage, sError);
	if (fSigned || !fCheckSignature) return vDecoded[iElement];
	return "";
}

std::string GetDCCPublicKey(const std::string& cpid, bool fRequireSig)
{
	if (cpid.empty()) return "";
	int iMonths = 120;
    int64_t iMaxSeconds = 60 * 24 * 30 * iMonths * 60;
    std::string sData = RetrieveDCCWithMaxAge(cpid, iMaxSeconds);
    if (sData.empty())
	{
		return "";
	}
    // DCC data structure: CPID,hashRand,PubKey,ResearcherID,Sig(base64Enc)
	std::string sElement = GetDCCElement(sData, 2, fRequireSig); // Public Key
	return sElement;
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
		std::string sDCPK = GetDCCPublicKey(sCPID, true);
		if (sDCPK != sPK) 
		{
			std::string sCPIDError = "End To End CPID Verification Failed, Address does not match advertised public key for CPID " + sCPID + ", Advertised Addr " + sDCPK + ", Addr " + sPK;
			NonObnoxiousLog("cpid", "VerifyCPIDSignature", sCPIDError, 300);
			return false;
		}
	}
	return true;
}


bool HasThisCPIDSolvedPriorBlocks(std::string CPID, CBlockIndex* pindexPrev)
{
	if (CPID.empty()) return false;
	int iCheckWindow = fProd ? 4 : 1;
	CBlockIndex* pindex = pindexPrev;
	int64_t headerAge = GetAdjustedTime() - pindexPrev->nTime;
	if (headerAge > (60 * 60 * 4)) return false;
	const CChainParams& chainparams = Params();
  
	for (int i = 0; i < iCheckWindow; i++)
	{
		if (pindex != NULL)
		{
			CBlock block;
        	if (ReadBlockFromDisk(block, pindex, chainparams.GetConsensus(), "HasThisCPIDSolvedPriorBlocks"))
			{
				std::string sCPIDSig = ExtractXML(block.vtx[0].vout[0].sTxOutMessage, "<cpidsig>","</cpidsig>");
				std::string lastcpid = GetElement(sCPIDSig, ";", 0);
				if (!lastcpid.empty() && !sCPIDSig.empty())
				{
					// LogPrintf(" Current CPID %s, LastCPID %s, Height %f --- ", CPID.c_str(), lastcpid.c_str(), (double)pindex->nHeight);
					if (lastcpid == CPID) 
					{
						// if (fDebugMaster) LogPrintf(" CPID %s has solvd prior blx @ height %f in prod %f iteration i %f ", CPID.c_str(), (double)pindex->nHeight, fProd, i);
						return true;
					}
					pindex = pindexPrev->pprev;
				}
			}
		}
	}
	return false;
}

std::vector<std::string> GetListOfDCCS(std::string sSearch, bool fRequireSig)
{
	// Return a list of Distributed Computing Participants - Rob A. - Biblepay - 1-29-2018
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
				std::string sCPID = GetDCCElement(sValue, 0, fRequireSig);
				std::string sAddress = GetDCCElement(sValue, 2, fRequireSig);
				boost::to_upper(sAddress);
				boost::to_upper(sCPID);
				if (!sSearch.empty()) if (sSearch == sCPID || sSearch == sAddress)
				{
					sOut += sValue + "<ROW>";
				}
				if (sSearch.empty() && !sCPID.empty()) sOut += sValue + "<ROW>";
			}
		}
	}
	std::vector<std::string> vCPID = Split(sOut.c_str(), "<ROW>");
	return vCPID;
}

int64_t GetHistoricalMilestoneAge(int64_t nMaturityAge, int64_t nOffset)
{
	// This function returns the timestamp in history of the last PODC Quorum Cutoff (by default the quorum cutoff occurs once every 4 hours)
	int64_t nNow = GetAdjustedTime();
	int64_t nRemainder = nNow % nMaturityAge;
	int64_t nLookback = nRemainder + nOffset;
	return nLookback;
}

double GetMatureMetric(std::string sMetricName, std::string sPrimaryKey, int64_t nMaxAge, int nHeight)
{
	double dMetric = 0;
	if ((fProd && nHeight > F13000_CUTOVER_HEIGHT_PROD) || (!fProd && nHeight > F13000_CUTOVER_HEIGHT_TESTNET))
	{
		// Do it the new way (Gather everything that is mature, with no maximum age)
		// Mature means we stake a point in history (one timestamp per day) as Yesterdays point, and mature means it is older than that historical point.
		// This should theoretically allow the sancs to come to a more perfect consensus each day for PODC elements: DCCs (Distinct CPIDs), UTXOWeights (UTXO Stake Amounts), TaskWeights (Task confirmations), and Unbanked Indicators
		std::string sNewMetricName = "Mature" + sMetricName;
		dMetric = cdbl(ReadCacheWithMaxAge(sNewMetricName, sPrimaryKey, GetHistoricalMilestoneAge(14400, nMaxAge)), 0);
	}
	else
	{
		// Do it the old fashioned way (Gather everything up to chain tip, honoring maximum age)
		dMetric = cdbl(ReadCacheWithMaxAge(sMetricName, sPrimaryKey, nMaxAge), 0);
	}
	return dMetric;
}

bool FilterPhase1(int iNextSuperblock, std::string sConcatCPIDs, std::string sSourcePath, std::string sTargetPath, std::vector<std::string> vCPIDs)
{
	boost::filesystem::path pathIn(sSourcePath);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return false;

	FILE *outFile = fopen(sTargetPath.c_str(), "w");
	std::string sBuffer = "";
	int iBuffer = 0;
	
	// Phase 1: Scan the Combined Researcher file for all Biblepay Researchers (who have associated BiblePay Keys with Research Projects)
	// Filter the file down to BiblePay researchers:
	int iLines = 0;
	std::string sOutData = "";
	std::string line;
    int64_t nMaxAge = (int64_t)GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	 
    while(std::getline(streamIn, line))
    {
		  std::string sCpid = ExtractXML(line,"<cpid>","</cpid>");
		  sBuffer += line + "<ROW>";
		  iBuffer++;
		  iLines++;
		  if (iLines % 2000000 == 0) LogPrintf(" Processing DCC Line %f ",(double)iLines);
		  if (!sCpid.empty())
		  {
			 boost::to_upper(sCpid);
			 if (Contains(sConcatCPIDs, sCpid))
			 {
				for (int i = 0; i < (int)vCPIDs.size(); i++)
				{
					std::string sBiblepayResearcher = GetDCCElement(vCPIDs[i], 0, false);
					boost::to_upper(sBiblepayResearcher);
					if (!sBiblepayResearcher.empty() && sBiblepayResearcher == sCpid)
					{
						for (int z = 0; z < 2; z++)
						{
							// Add in the User URL and Team
							bool bAddlData = static_cast<bool>(std::getline(streamIn, line));
							if (bAddlData) sBuffer += line + "<ROW>";
						}

						double dUTXOWeight = GetMatureMetric("UTXOWeight", sCpid, nMaxAge, iNextSuperblock);
						double dTaskWeight = GetMatureMetric("TaskWeight", sCpid, nMaxAge, iNextSuperblock);
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
	return true;
}


bool FilterPhase2(int iNextSuperblock, std::string sSourcePath, std::string sTargetPath, double dTargetTeam)
{
	boost::filesystem::path pathIn(sSourcePath);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return false;

	FILE *outFile = fopen(sTargetPath.c_str(), "w");
	std::string sBuffer = "";
	// This file is used by the faucets; Find all team members who are not necessarily yet associated in the chain:
	std::string line;
     
    while(std::getline(streamIn, line))
    {
		  sBuffer += line;
		  if (Contains(sBuffer,"</user>"))
		  {
			  	 std::string sCpid = ExtractXML(sBuffer,"<cpid>","</cpid>");
				 double dTeamID = cdbl(ExtractXML(sBuffer,"<teamid>","</teamid>"), 0);
				 double dRac = cdbl(ExtractXML(sBuffer,"<expavg_credit>","</expavg_credit>"), 0);
				 double dTotalRAC = cdbl(ExtractXML(sBuffer,"<total_credit>","</total_credit>"), 0);
				 double dCreated = cdbl(ExtractXML(sBuffer,"<create_time>","</create_time>"), 0);
				 std::string sName = ExtractXML(sBuffer,"<name>","</name>");
				 if (dTargetTeam == dTeamID)
				 {
					 std::string sRow = sCpid + "," + RoundToString(dTeamID,0) + "," + RoundToString(dRac,0) + "," + RoundToString(dTotalRAC,0) + "," + RoundToString(dCreated,0) + "," + sName + "\r\n";
					 fputs(sRow.c_str(), outFile);
				 }
				 sBuffer="";
		  }
				 
    }
	streamIn.close();
    fclose(outFile);
	return true;
}

double GetTeamPercentage(double dUserTeam, double dProjectTeam, std::string sTeamBlacklist, double dNonBiblepayTeamPercentage)
{
	// Return a reward percentage for a given team 
	// First, if blacklists are enabled, if user team is in blacklist, reject the users RAC
	if (!sTeamBlacklist.empty())
	{
		std::vector<std::string> vTeams = Split(sTeamBlacklist.c_str(), ";");
		for (int i = 0; i < (int)vTeams.size(); i++)
		{
			double dBlacklistedTeam = cdbl(vTeams[i], 0);
			if (dBlacklistedTeam != 0 && dUserTeam != 0 && dBlacklistedTeam == dUserTeam)
			{
				return 0;
			}
		}
	}
	if (dProjectTeam == 0) return 1;
	// Next, if BiblePay team is required, verify the team matches
	return (dUserTeam == dProjectTeam) ? 1 : dNonBiblepayTeamPercentage;
}

double GetSumOfXMLColumnFromXMLFile(std::string sFileName, std::string sObjectName, std::string sElementName, double dReqSPM, double dReqSPR, double dTeamRequired, std::string sConcatCPIDs, 
	double dRACThreshhold, std::string sTeamBlacklist, int iNextSuperblock)
{
	boost::filesystem::path pathIn(sFileName);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return 0;
	double dTotal = 0;
	std::string sLine = "";
	std::string sBuffer = "";
	double dDRMode = cdbl(GetSporkValue("dr"), 0);
	double dNonBiblepayTeamPercentage = cdbl(GetSporkValue("nonbiblepayteampercentage"), 2);
	//int64_t nMaxAge = (int64_t)GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	
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
		std::string sCPID = ExtractXML(sData,"<cpid>","</cpid>");
		boost::to_upper(sCPID);
		if (Contains(sConcatCPIDs, sCPID))
		{
			double dTeamPercentage = GetTeamPercentage(dTeam, dTeamRequired, sTeamBlacklist, dNonBiblepayTeamPercentage);
			if (dTeamPercentage > 0)
			{
				std::string sValue = ExtractXML(sData, "<" + sElementName + ">","</" + sElementName + ">");
				double dAvgCredit = cdbl(sValue,2);
				double dModifiedCredit = GetResearcherCredit(dDRMode, dAvgCredit, dUTXOWeight, dTaskWeight, dUnbanked, 0, dReqSPM, dReqSPR, dRACThreshhold, dTeamPercentage);
				dTotal += dModifiedCredit;
				if (fDebugMaster && false) LogPrintf(" Adding CPID %s, Team %f, modifiedrac %f from RAC %f  with nonbbptp %f   DRMode %f,  Grand Total %f \n", 
					sCPID.c_str(), dTeam, dModifiedCredit, dAvgCredit, dNonBiblepayTeamPercentage, dDRMode, dTotal);
			}
		}
    }
	return dTotal;
}

int GetMinimumResearcherParticipationLevel()
{
	return fProd ? 5 : 1;
}

double GetMinimumMagnitude()
{
	return fProd ? 5 : 1;
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


double GetRACFromPODCProject(int iNextSuperblock, std::string sFileName, std::string sResearcherCPID, double dDRMode, double dReqSPM, double dReqSPR, double dTeamRequired,
	double dProjectFactor, double dRACThreshhold, std::string sTeamBlacklist, double& out_Team)
{
	boost::filesystem::path pathFiltered(sFileName);
	std::ifstream streamFiltered;
    streamFiltered.open(pathFiltered.string().c_str());
	if (!streamFiltered) return 0;
	std::string sUser = "";
	int64_t nMaxAge = (int64_t)GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	double dUTXOWeight = GetMatureMetric("UTXOWeight", sResearcherCPID, nMaxAge, iNextSuperblock);
	double dTaskWeight = GetMatureMetric("TaskWeight", sResearcherCPID, nMaxAge, iNextSuperblock);
	double dNonBiblepayTeamPercentage = cdbl(GetSporkValue("nonbiblepayteampercentage"), 2);

	double dUnbanked = cdbl(ReadCacheWithMaxAge("Unbanked", sResearcherCPID, nMaxAge), 0);
	double dTotalRAC = 0;
	double dTotalFound = 0;
	boost::to_upper(sResearcherCPID);
	std::string line = "";
    while(std::getline(streamFiltered, line))
    {
		sUser += line;
		if (Contains(line, "</user>"))
		{
			std::string sCPID = ExtractXML(sUser, "<cpid>", "</cpid>");
			boost::to_upper(sCPID);
			if (sCPID == sResearcherCPID)
			{
				double dAvgCredit = cdbl(ExtractXML(sUser, "<expavg_credit>", "</expavg_credit>"), 4);
				double dTeam = cdbl(ExtractXML(sUser, "<teamid>", "</teamid>"), 0);
				out_Team = dTeam;
				// if (fDebugMaster) LogPrintf(" CPID %s, ExtraRAC %f, Team %f, TeamReq %f  ", sCPID.c_str(), dModifiedCredit, dTeam, dTeamRequired);
				double dPercent = GetTeamPercentage(dTeam, dTeamRequired, sTeamBlacklist, dNonBiblepayTeamPercentage);
				double dModifiedCredit = GetResearcherCredit(dDRMode, dAvgCredit, dUTXOWeight, dTaskWeight, dUnbanked, dTotalRAC, dReqSPM, dReqSPR, dRACThreshhold, dPercent) * dProjectFactor;
				dTotalFound += dModifiedCredit;
			}
			sUser = "";
		}
    }
	streamFiltered.close();
	return dTotalFound;
}

void ClearSanctuaryMemories()
{
	double nMaximumChatterAge = GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	int64_t nMinimumMemory = GetAdjustedTime() - (nMaximumChatterAge * 7);
	PurgeCacheAsOfExpiration("UTXOWeight", nMinimumMemory);
	PurgeCacheAsOfExpiration("CPIDTasks", nMinimumMemory);
	PurgeCacheAsOfExpiration("Unbanked", nMinimumMemory);
}

std::string GetBoincUnbankedReport(std::string sProjectID)
{
	// Legacy URL = https://host/Action.aspx?action=unbanked
	std::string sHost = "https://" + GetSporkValue("unbankedhost");
	std::string sPage = GetSporkValue("unbankedpage"); // "page.php?action=unbanked";
	std::string sURL = sHost + sPage;
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", sProjectID, "", sHost, sPage, 443, "", 20, 25000, 1);
	std::string sUnbanked = ExtractXML(sResponse, "<UNBANKED>","</UNBANKED>");
	return sUnbanked;
} 

std::string VerifyManyWorkUnits(std::string sProjectId, std::string sTaskIds)
{
 	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "rosetta/result_status.php?ids=" + sTaskIds;
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 15, 275000, 2);
	return sResponse;
}

double VerifyTasks(std::string sCPID, std::string sTasks)
{
	if (sTasks.empty()) return 0;
	double dCounted = 0;
	double dVerified = 0;
	std::string sTaskIds = GetListOfData(sTasks, ",", "=", 0, 255);
	std::string sTimestamps = GetListOfData(sTasks, ",", "=", 1, 255);
	std::string sResults = VerifyManyWorkUnits("project1", sTaskIds);
	std::vector<std::string> vPODC = Split(sTaskIds.c_str(), ",");
	std::vector<std::string> vTimes = Split(sTimestamps.c_str(), ",");
	std::string sDebug = sResults;
	if (sDebug.length() > 2500) sDebug = sDebug.substr(0, 2499);
	if (fDebugMaster) LogPrint("podc", "\n\n VerifyTasks CPID %s, taskids %s, stamps %s, output %s \n\n\n", sCPID.c_str(), sTaskIds.c_str(), sTimestamps.c_str(), sDebug.c_str());
	for (int i = 0; i < (int)vPODC.size() && i < (int)vTimes.size(); i++)
	{
		double dTask = cdbl(vPODC[i], 0);
		double dTime = cdbl(vTimes[i], 0);
		dCounted++;
		if (dTask > 0 && dTime > 0)
		{
			double dXMLTime = cdbl(GetWUElement(sResults, "<result>", dTask, "id", "sent_time"), 0);
			if (dXMLTime > 0 && dXMLTime == dTime) dVerified++;
		}
		if (dCounted > 255) break;
	}
	if (dCounted < 1) return 0;
	double dSource = (dVerified / dCounted) * 60000;
	double dSnapped = GetCPIDUTXOWeight(dSource);
	if (fDebugMaster) LogPrint("podc", "\n VerifyTasks::Tasks %f  Verified %f   Source %f  Snapped %f  ", dCounted, dVerified, dSource, dSnapped);
	return dSnapped;
}

std::string GetMatureString(std::string sMetricName, std::string sPrimaryKey, int64_t nMaxAge, int nHeight)
{
	std::string sMetric = "";
	if ((fProd && nHeight > F13000_CUTOVER_HEIGHT_PROD) || (!fProd && nHeight > F13000_CUTOVER_HEIGHT_TESTNET))
	{
		// Mature means we stake a point in history (one timestamp per day) as Yesterdays point, and mature means it is older than that historical point.
		// This should theoretically allow the sancs to come to a more perfect consensus each day for PODC elements: DCCs (Distinct CPIDs), UTXOWeights (UTXO Stake Amounts), TaskWeights (Task confirmations), and Unbanked Indicators
		std::string sNewMetricName = "Mature" + sMetricName;
		sMetric = ReadCacheWithMaxAge(sNewMetricName, sPrimaryKey, GetHistoricalMilestoneAge(14400, nMaxAge));
	}
	else
	{
		// The old fashioned way (Gather everything up to chain tip, honoring maximum age)
		sMetric = ReadCacheWithMaxAge(sMetricName, sPrimaryKey, nMaxAge);
	}
	return sMetric;
}

bool FilterFile(int iBufferSize, int iNextSuperblock, std::string& sError)
{
	std::vector<std::string> vCPIDs = GetListOfDCCS("", false);
    std::string buffer;
    std::string line;
    std::string sTarget = GetSANDirectory2() + "user1";
	std::string sFiltered = GetSANDirectory2() + "filtered1";
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";

    if (!boost::filesystem::exists(sTarget.c_str())) 
	{
        sError = "DCC input file does not exist.";
        return false;
    }

    int64_t nMaxAge = (int64_t)GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	double dReqSPM = GetSporkDouble("requiredspm", 500);
	double dReqSPR = GetSporkDouble("requiredspr", 0);
	double dRACThreshhold = GetSporkDouble("racthreshhold", 0);

	std::string sTeamBlacklist = GetSporkValue("teamblacklist");
	std::string sConcatCPIDs = "";
	std::string sUnbankedList = MutateToList(GetBoincUnbankedReport("pool"));
	if (true)
	{
		ClearSanctuaryMemories();
		LOCK(cs_main);
		{
			// This ensures that all mature CPIDs (assessed as of yesterdays single timestamp point) are memorized (IE we didnt skip over any because they were not mature 1-4 hours ago)
			MemorizeBlockChainPrayers(false, false, false, true);
		}
	}

	ClearCache("Unbanked");
	double dDRMode = cdbl(GetSporkValue("dr"), 0);
	for (int i = 0; i < (int)vCPIDs.size(); i++)
	{
		std::string sCPID1 = GetDCCElement(vCPIDs[i], 0, true);
		double dRosettaID = cdbl(GetDCCElement(vCPIDs[i], 3, false), 0);
		double dUnbankedIndicator = cdbl(GetDCCElement(vCPIDs[i], 5, false), 0);
		if (dUnbankedIndicator==1) sCPID1 = GetDCCElement(vCPIDs[i], 0, false);
		if (!sCPID1.empty())
		{
			if (!sCPID1.empty()) sConcatCPIDs += sCPID1 + ",";
			std::string sTaskList = GetMatureString("CPIDTasks", sCPID1, nMaxAge, iNextSuperblock);
			double dVerifyTasks = 0;
			// R ANDREWS; 5-9-2018
			if (dDRMode == 0 || dDRMode == 2) dVerifyTasks = VerifyTasks(sCPID1, sTaskList);
			WriteCache("TaskWeight", sCPID1, RoundToString(dVerifyTasks, 0), GetAdjustedTime());
			if (dRosettaID > 0 && IsInList(sUnbankedList, ",", RoundToString(dRosettaID,0)))
			{
				WriteCache("Unbanked", sCPID1, "1", GetAdjustedTime());
			}
			else
			{
				if (dUnbankedIndicator == 1)
				{
					WriteCache("Unbanked", sCPID1, "0", GetAdjustedTime());
				}
			}
		}
	}
	boost::to_upper(sConcatCPIDs);
	if (fDebugMaster) LogPrintf("Filter Phase 1: CPID List concatenated %s, unbanked %s  ",sConcatCPIDs.c_str(), sUnbankedList.c_str());

	// Filter each BOINC Project file down to the individual BiblePay records

	bool bResult = FilterPhase1(iNextSuperblock, sConcatCPIDs, sTarget, sFiltered, vCPIDs);
	if (!bResult)
	{
		LogPrintf(" \n FilterFile::FilterPhase 1 failed. \n");
		return false;
	}
	std::string sTeamFile1 = GetSANDirectory2() + "team1";
	std::string sTeamFile2 = GetSANDirectory2() + "team2";

	double dTeamRequired = cdbl(GetSporkValue("team"), 0);
	double dTeamBackupProject = cdbl(GetSporkValue("team2"), 0);
	
	bResult = FilterPhase2(iNextSuperblock, sTarget, sTeamFile1, dTeamRequired);

	std::string sTarget2 = GetSANDirectory2() + "user2";
	std::string sFiltered2 = GetSANDirectory2() + "filtered2";
	
    if (boost::filesystem::exists(sTarget2.c_str())) 
	{
		FilterPhase1(iNextSuperblock, sConcatCPIDs, sTarget2, sFiltered2, vCPIDs);
    }
	bResult = FilterPhase2(iNextSuperblock, sTarget2, sTeamFile2, dTeamBackupProject);

	//  Phase II : Normalize the file for Biblepay (this process asseses the magnitude of each BiblePay Researcher relative to one another, with 100 being the leader, 0 being a researcher with no activity)
	//  We measure users by RAC - the BOINC Decay function: expavg_credit.  This is the half-life of the users cobblestone emission over a one month period.
	
	double dRAC1 = GetSumOfXMLColumnFromXMLFile(sFiltered, "<user>", "expavg_credit", dReqSPM, dReqSPR, dTeamRequired, sConcatCPIDs, dRACThreshhold, sTeamBlacklist, iNextSuperblock);
	double dRAC2 = GetSumOfXMLColumnFromXMLFile(sFiltered2,"<user>", "expavg_credit", dReqSPM, dReqSPR, dTeamBackupProject, sConcatCPIDs, dRACThreshhold, sTeamBlacklist, iNextSuperblock);
	double dTotalRAC = dRAC1 + dRAC2;
	LogPrintf(" \n FilterPhase2: Team %f, backupteam %f, Proj1 RAC %f, Proj2 RAC %f, Total RAC %f \n", dTeamRequired, dTeamBackupProject, dRAC1, dRAC2, dTotalRAC);
	if (dTotalRAC < 10)
	{
		sError = "Total DC credit less than the project minimum.  Unable to calculate magnitudes.";
		return false;
	}

	dTotalRAC += 100; // Ensure magnitude never exceeds 1000 due to rounding errors.

	// Emit the BiblePay DCC Leaderboard file, and then stamp it with the Biblepay hash
	// Leaderboard file format:  Biblepay-Public-Key-Compressed, DCC-CPID, DCC-Magnitude <rowdelimiter>
	int iTries = 0;
	// Adding a more robust contract creator loop, just in case magnitude exceeds 1000 due to rounding errors, this gives the sanctuary a second chance to make it correct.
	double dGlobalMagnitudeFactor = 1;  
	double dTotalRosetta = 0;
	double dTotalWCG = 0;
	int iRows = 0;
	double dTotalMagnitude = 0;
	std::string sDCC = "";
	/* Assess Magnitude Levels */
	while(true)
	{
		iTries++;
		if (iTries > 70) break;

		std::string sUser = "";
		sDCC = "";
		double dBackupProjectFactor = cdbl(GetSporkValue("project2factor"), 2);
		double dDRMode = cdbl(GetSporkValue("dr"), 0);
		std::string sTeamBlacklist = GetSporkValue("teamblacklist");
		dTotalMagnitude = 0;
		dTotalRosetta = 0;
		dTotalWCG = 0;
		iRows = 0;
		
		// ********************************************************************   Phase 3: Assemble Magnitudes   **********************************************************************************
		double doutWCGTeam = 0;
		double doutRAHTeam = 0;
		for (int i = 0; i < (int)vCPIDs.size(); i++)
		{
			std::string sPreCPID = GetDCCElement(vCPIDs[i], 0, false);
			double dUnbanked = cdbl(ReadCacheWithMaxAge("Unbanked", sPreCPID, nMaxAge), 0);
			bool fRequireSig = dUnbanked == 1 ? false : true;
			std::string sCPID = GetDCCElement(vCPIDs[i], 0, fRequireSig);
			double dRosettaID = cdbl(GetDCCElement(vCPIDs[i], 3, false), 0);
			if (!sCPID.empty())
			{
				double dUTXOWeight = GetMatureMetric("UTXOWeight", sCPID, nMaxAge, iNextSuperblock);
				double dTaskWeight = GetMatureMetric("TaskWeight", sCPID, nMaxAge, iNextSuperblock);
				// If backup project enabled, add additional credit
				double dWCGRAC = 0;
				if (dTeamBackupProject > 0) 
				{
					// Note that dDR Mode is set to 3 so that we can penalize the user based on the TOTAL UTXO LEVEL BELOW, not once per project (since they fit in ONE UTXO LEVEL SLOT):
					dWCGRAC = GetRACFromPODCProject(iNextSuperblock, sFiltered2, sCPID, 3, dReqSPM, dReqSPR, dTeamBackupProject, dBackupProjectFactor, dRACThreshhold, sTeamBlacklist, doutWCGTeam);
				}
				double dRosettaRAC = GetRACFromPODCProject(iNextSuperblock, sFiltered, sCPID, 3, dReqSPM, dReqSPR, dTeamRequired, 1.0, dRACThreshhold, sTeamBlacklist, doutRAHTeam);

				dTotalRosetta += dRosettaRAC;
				dTotalWCG += dWCGRAC;

				double dModifiedCredit = GetResearcherCredit(dDRMode, dRosettaRAC + dWCGRAC, dUTXOWeight, dTaskWeight, dUnbanked, dTotalRAC, dReqSPM, dReqSPR, dRACThreshhold, 1);
				// R ANDREW : BIBLEPAY : We include any CPID with adj. credit > 0 or RAC > 100 so that the user can see this record in the superblock view report (IE they have 0 UTXO WEIGHT and want to diagnose the problem).  We filter out adjusted RAC of zero (zero mag reward) with < 100 RAC (decaying long gone boincers) for a succinct superblock contract.
				if (dModifiedCredit > 0 || (dRosettaRAC + dWCGRAC > 9))
				{
					std::string BPK = GetDCCPublicKey(sCPID, fRequireSig);
					double dMagnitude = (dModifiedCredit / dTotalRAC) * 999 * dGlobalMagnitudeFactor;
					double dUTXO = GetUTXOLevel(dUTXOWeight, dTotalRAC, dRosettaRAC + dWCGRAC, dReqSPM, dReqSPR, dRACThreshhold);
					if (!BPK.empty())
					{
						std::string sRow = BPK + "," + sCPID + "," + RoundToString(dMagnitude, 3) + "," 
							+ RoundToString(dRosettaID, 0) + "," + RoundToString(doutRAHTeam, 0) 
							+ "," + RoundToString(dUTXOWeight, 0) + "," + RoundToString(dTaskWeight, 0) 
							+ "," + RoundToString(dTotalRAC, 0) + "," 
							+ RoundToString(dUnbanked, 0) + "," + RoundToString(dUTXO, 2) + "," + RoundToString(dRosettaRAC, 0) + "," 
							+ RoundToString(dModifiedCredit, 0) + "," + RoundToString(iNextSuperblock, 0) + ","
							+ RoundToString(dWCGRAC, 0) + "," + RoundToString(doutWCGTeam, 0) + "\n<ROW>";
						sDCC += sRow;
						dTotalMagnitude += dMagnitude;
						iRows++;
					}
					else
					{
						if (fDebugMaster) LogPrintf(" Non-Included DCC CPID %s , RosettaRAC %f, WCGRAC %f, UTXO Weight %f, Task Weight %f, DRMode %f, RacThreshhold %f, TotalRAC %f \n",
							sCPID.c_str(), dRosettaRAC, dWCGRAC, dUTXOWeight, dTaskWeight, dDRMode, dRACThreshhold, dTotalRAC);
					}
				}
				else
				{
					if (fDebugMaster) LogPrintf(" Non-Included CPID %s , RosettaRAC %f, WCGRAC %f, UTXO Weight %f, Task Weight %f, DRMode %f, RacThreshhold %f, TotalRAC %f \n",
						sCPID.c_str(), dRosettaRAC, dWCGRAC, dUTXOWeight, dTaskWeight, dDRMode, dRACThreshhold, dTotalRAC);
				}
				sUser = "";
			}
		}
		if (dTotalMagnitude < 1000) break;
		// Magnitude exceeded 1000, adjust the global factor:
		dGlobalMagnitudeFactor -= .02;
		if (fDebugMaster) LogPrintf("\n FilterFile::AssessMagnitudeLevels, Attempt #%f, GlobalMagnitudeFactor %f, Current Magnitude %f  TotalRosetta %f, Total WCG %f \n", iTries, dGlobalMagnitudeFactor, dTotalMagnitude, dTotalRosetta, dTotalWCG);
	}
	/* End of Assess Magnitude Levels */

    // Phase 3: Create the Daily Magnitude Contract and hash it
	FILE *outMagFile = fopen(sDailyMagnitudeFile.c_str(),"w");
    fputs(sDCC.c_str(), outMagFile);
	fclose(outMagFile);

	uint256 uhash = GetDCCHash(sDCC);
	// Persist the contract in memory for verification
    WriteCache("dcc", "contract", sDCC, GetAdjustedTime());
	WriteCache("dcc", "contract_hash", uhash.GetHex(), GetAdjustedTime());
	LogPrintf("\n Created Contract with %f rows, Total Magnitude %f \n", (double)iRows, dTotalMagnitude);
    return true;
}

double GetTaskWeight(std::string sCPID)
{
	double nMaximumChatterAge = GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	std::string sTaskList = ReadCacheWithMaxAge("CPIDTasks", sCPID, nMaximumChatterAge);
	return (sTaskList.empty()) ? 0 : 100;
}

double GetUTXOWeight(std::string sCPID)
{
	double nMaximumChatterAge = GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
	double dUTXOWeight = cdbl(ReadCacheWithMaxAge("UTXOWeight", sCPID, nMaximumChatterAge), 0);
	return dUTXOWeight;
}


double GetMinimumRequiredUTXOStake(double dRAC, double dFactor)
{
	double dReqSPM = GetSporkDouble("requiredspm", 500);
	double dReqSPR = GetSporkDouble("requiredspr", 0);
	double dRequirement = 0;
	double dEstimatedMagnitude = dRAC / 5000; // This is a rough estimate only lasting until April 1, 2018 pending outcome of SPR vote
	LogPrintf(" getminimumrequiredutxostake RAC %f, reqspm %f, reqspr %f ",dRAC, dReqSPM, dReqSPR);
	if (dReqSPM > 0) 
	{
		dRequirement = dEstimatedMagnitude * dReqSPM * dFactor;
	}
	else if (dReqSPR > 0)
	{
		// If using Stake-Per-RAC, then 
		dRequirement = dRAC * dReqSPR * dFactor;
	}
	if (dRequirement < 10) dRequirement = 10;
	return dRequirement;
}




#ifdef MAC_OSX
int ShellCommand(std::string sCommand, std::string &sOutput, std::string &sError)
{
    try
    {
        bp::ipstream output;
        bp::ipstream error;
        
        bp::system(sCommand, bp::std_out > output, bp::std_err > error);
        //int nResult = bp::system(sCommand);
        
        std::string line;
        while (std::getline(output, line)) {
            sOutput += line + "\n";
        }
        while (std::getline(error, line)) {
            sError += line + "\n";
        }
        return 0;
    }
    catch (std::exception& e)
    {
        sError += e.what();
        return 1;
    }

}
#elif defined(WIN32)
int ShellCommand(std::string sCommand, std::string &sOutput, std::string &sError)
{
	double dBoincMetrics = cdbl(GetArg("-boincmetrics", "0"), 0);
	if (dBoincMetrics == -1) return 0;

	if (dBoincMetrics == 1)
	{
		sError = SysCommandStdErr(sCommand, "boinctemp", sOutput);
	}
	else
	{	
		sOutput = sError = SystemCommand2(sCommand.c_str());
	}
    return (Contains(sOutput, "not found"))?1:0;
}
#else
int ShellCommand(std::string sCommand, std::string &sOutput, std::string &sError)
{
    sError = SysCommandStdErr(sCommand, "boinctemp", sOutput);
    return (Contains(sError, "not found") || Contains(sOutput, "not found"))?1:0;
}
#endif

std::string BoincCommand(std::string sCommand, std::string &sError)
{
	// Boinc sends some output to stderr, some to stdout
    std::string sPath = GetSANDirectory2() + "boinctemp";
    std::string sEXEPath = "";
    std::string sErrorNotFound = "";
    std::string sCmd;

    if (sOS=="WIN")
    {
        sEXEPath = "\"c:\\program files\\BOINC\\boinccmd\"";
        sErrorNotFound += "Boinc is not installed.  Please run BOINC installer and make sure boinccmd.exe is found in "+sEXEPath;
        sCmd = sEXEPath + " >" + sPath + " " + sCommand + " 2>&1";
    }
    else if (sOS=="LIN")
    {
        sEXEPath = "boinccmd";
        sErrorNotFound += "Boinc is not installed.  Please run 'sudo apt-get install boincmgr boinc'.";
        sCmd = sEXEPath + " >" + sPath + " " + sCommand + " 2>&1";
    }
    else // (sOS=="MAC")
    {
        sEXEPath = "\"/Library/Application Support/BOINC Data/boinccmd\"";
        sErrorNotFound += "boinccmd is not found. Download 'Unix command-line version' for MacOS separately. Then copy 'move_to_boinc_dir' contents into /Library/Application Support/BOINC Data/ folder and try again.";
        sCmd = sEXEPath + " " + sCommand + " 2";
    }

	std::string sStandardOut = "";
    std::string sStandardErr = "";
	//std::string sResult = SysCommandStdErr(sCmd, "boinctemp", sStandardOut);
    // nResult == 0 ok, 1 failure
    int nNotFound = ShellCommand(sCmd, sStandardOut , sStandardErr);
	// We handle Not installed, account exists, boincinstalled and account does not exist
    if (nNotFound==1)
    {
        sError += sErrorNotFound;
    }
	sStandardErr += "<EOF>";
	return sStandardErr;
}



int GetBoincTaskCount()
{
	std::string sError = "";
	std::string sData = BoincCommand("--get_tasks", sError);
	std::vector<std::string> vTasks = Split(sData.c_str(), "project");
	int nTasks = 0;
	for (int i = 0; i < (int)vTasks.size(); i++)
	{
		std::string sState = ExtractXML(vTasks[i], "active_task_state:", "app");
		boost::trim(sState);
	
		if (!Contains(sState, "SUSPENDED") && !sState.empty()) nTasks++;
	}
	return nTasks;
}

std::string GetAccountAuthenticator(std::string sEmail, std::string sPass)
{
	// Note:  the password is Not sent over the internet; it is sent to boinccmd which hashes the user+pass first, then asks Rosetta for the authenticator (by hash)
	std::string sError = "";
	std::string sURL = "http://boinc.bakerlab.org/rosetta";
	std::string sData = BoincCommand("--lookup_account " + sURL + " " + sEmail + " " + sPass, sError);
	std::string sAuth = ExtractXML(sData, "key:", "<EOF>");
	return sAuth;
}

std::string ToYesNo(bool bValue)
{
	std::string sYesNo = bValue ? "Yes" : "No";
	return sYesNo;
}

double GetRosettaLocalRAC()
{
	std::string sError = "";
	std::string sData = BoincCommand("--get_project_status", sError);
	std::string sRosetta = ExtractXML(sData, "Rosetta", "user_name");
	if (!Contains(sRosetta, "bakerlab.org")) return -1;
	std::string sSnippet = ExtractXML(sData, "bakerlab.org", "scheduler");
	double dAvgCredit = cdbl(ExtractXML(sSnippet, "user_expavg_credit:", "host"), 2);
	return dAvgCredit;
}

std::string GetCPID()
{
	std::string sError = "";
	std::string sData = BoincCommand("--get_project_status", sError);
	std::string sCPID = ExtractXML(sData, "cross-project ID:", "<EOF>");
	boost::trim(sCPID);
	return sCPID;
}

bool IsBoincInstalled(std::string& sError)
{
	std::string sData = BoincCommand("--help", sError);
	bool bInstalled = (Contains(sData, "project_attach"));
	return bInstalled;						
}

double GetWCGRACByCPID(std::string sCPID)
{
	std::string sProjectURL = "http://" + GetSporkValue("pool");
	std::string sRestfulURL = "Action.aspx?action=wcgrac&cpid=" + sCPID + "&format=xml";
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
	double dRac = cdbl(ExtractXML(sResponse,"<WCGRAC>", "</WCGRAC>"), 2);
	return dRac;
}

double GetBoincRACByUserId(std::string sProjectId, int nUserId)
{
	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "show_user.php?userid=" + RoundToString(nUserId,0) + "&format=xml";
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
	double dRac = cdbl(ExtractXML(sResponse,"<expavg_credit>", "</expavg_credit>"), 2);
	return dRac;
}

int GetLastDCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock)
{
    // Compute last/next superblock
    int nLastSuperblock = 0;
    int nSuperblockStartBlock = Params().GetConsensus().nDCCSuperblockStartBlock;
    // int nSuperblockCycle = Params().GetConsensus().nDCCSuperblockCycle;
    // int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
	// 6-5-2018 - R ANDREWS - CASCADING SUPERBLOCKS 
	// So this looks complicated, but this logic allows us to transition over the F12000-F13000 superblock interval cycle change with a broken schedule in-between
	int nHeight = nCurrentHeight;
	for (; nHeight > nSuperblockStartBlock; nHeight--)
	{
		if (CSuperblock::IsDCCSuperblock(nHeight))
		{
			nLastSuperblock = nHeight;
			break;
		}
	}
	nHeight++;
	for (; nHeight > nSuperblockStartBlock; nHeight++)
	{
		if (CSuperblock::IsDCCSuperblock(nHeight))
		{
			nNextSuperblock = nHeight;
			break;
		}
	}
	return nLastSuperblock;
}

uint256 GetDCPAMHash(std::string sAddresses, std::string sAmounts)
{
	std::string sConcat = sAddresses + sAmounts;
	if (sConcat.empty()) return uint256S("0x0");
	std::string sHash = RetrieveMd5(sConcat);
	return uint256S("0x" + sHash);
}


void GetDistributedComputingGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts)
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



double GetMagnitudeByAddress(std::string sAddress)
{
	CDistributedComputingVote upcomingVote;
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	
	int iVotes = 0;
	std::string sContract = "";
	uint256 uGovObjHash;
	double nTotal = 0;
	std::string sPAD = "";
	std::string sPAM = "";
	GetDistributedComputingGovObjByHeight(iLastSuperblock, uint256S("0x0"), iVotes, uGovObjHash, sPAD, sPAM);
		
	double dBudget = CSuperblock::GetPaymentsLimit(iLastSuperblock) / COIN;
	std::vector<std::string> vSPAD = Split(sPAD.c_str(), "|");
	std::vector<std::string> vSPAM = Split(sPAM.c_str(), "|");

	if (dBudget < 1) return 0;
	// Since an address my have more than one cpid... (not recommended but nevertheless...) get the grand total magnitude per address 
	for (int i=0; i < (int)vSPAD.size() && i < (int)vSPAM.size(); i++)
	{
		std::string sLocalAddress = vSPAD[i];
		double dAmt = cdbl(vSPAM[i],2);
		std::string sAmount = vSPAM[i];
		if (sAddress==sLocalAddress) 
		{
			double dMag = (dAmt / dBudget) * 1000;
			nTotal += dMag;
		}
	  
	}
	nTotal = cdbl(RoundToString(nTotal,2),2);
	return nTotal;
}

std::string FindResearcherCPIDByAddress(std::string sSearch, std::string& out_address, double& nTotalMagnitude)
{
	std::string sDefaultRecAddress = "";
	msGlobalCPID = "";
	std::string sLastCPID = "";
	nTotalMagnitude = 0;
	BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        std::string strName = item.second.name;
        bool fMine = IsMine(*pwalletMain, address.Get());
        if (fMine)
		{
		    std::string sAddress = CBitcoinAddress(address).ToString();
			boost::to_upper(strName);
			// If we have a valid burn in the chain, prefer it
			std::vector<std::string> vCPIDs = GetListOfDCCS(sAddress, true);
			if (vCPIDs.size() > 0)
			{
				nTotalMagnitude += GetMagnitudeByAddress(sAddress);
				for (int i=0; i < (int)vCPIDs.size(); i++)
				{
					std::string sCPID = GetDCCElement(vCPIDs[i], 0, false);
					if (sSearch.empty() && !sCPID.empty()) 
					{
						out_address = sAddress;
						msGlobalCPID += sCPID + ";";
						sLastCPID = sCPID;
					}
					else if (!sSearch.empty())
					{
						if (sSearch == sAddress && !sCPID.empty()) 
						{
							nTotalMagnitude = GetMagnitudeByAddress(sAddress);
							out_address = sAddress;
							return sCPID;
						}
					}
				}
			}
		}
    }
	mnMagnitude = nTotalMagnitude;
	return sLastCPID;
}

double AscertainResearcherTotalRAC()
{
	std::string out_address = "";
	std::string sAddress = "";
	double nMagnitude2 = 0;
	std::string sCPID = FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude2);
	std::vector<std::string> vCPIDS = Split(msGlobalCPID.c_str(),";");
	double nTotalRAC = 0;
	int64_t iMaxSeconds = 60 * 24 * 30 * 12 * 60;
	for (int i = 0; i < (int)vCPIDS.size(); i++)
	{
		std::string s1 = vCPIDS[i];
		if (!s1.empty())
		{
			std::string sData = RetrieveDCCWithMaxAge(s1, iMaxSeconds);
			int nUserId = cdbl(GetDCCElement(sData, 3, true), 0);
			std::string sAddress = GetDCCElement(sData, 1, true);
			if (nUserId > 0)
			{
				double RAC = GetBoincRACByUserId("project1", nUserId);
				double dWCGRAC = GetWCGRACByCPID(s1);
				nTotalRAC += RAC + dWCGRAC;
			}
		}
	}
	return nTotalRAC;
}



std::string RosettaDiagnostics(std::string sEmail, std::string sPass, std::string& sError)
{
		std::string sHTML = "";
		if (sEmail.empty()) sError = "E-mail must be populated.";
		if (sPass.empty()) sError += "Password must be populated.";
		bool fBoincInstalled = IsBoincInstalled(sError);
		std::string sAuth = "";
		if (sError.empty())
		{
			sAuth = GetAccountAuthenticator(sEmail, sPass);
			if (sAuth.empty())
			{
				sError = "No Rosetta account exists. ";
			}
		}

		sHTML += "Boinc Installed: " + ToYesNo(fBoincInstalled) + "\n";

		if (sError.empty())
		{
			sHTML += "Rosetta_Account: " + sAuth + "\n";

			double dRosettaRAC = GetRosettaLocalRAC();
			sHTML += "Rosetta_RAC: " + RoundToString(dRosettaRAC, 2) + "\n";
		
			std::string sCPID = GetCPID();
			if (dRosettaRAC != -1)
			{
				sHTML += "CPID: " + sCPID + "\n";
			}
		}

		if (sError.empty())
		{
			double dRAC = AscertainResearcherTotalRAC();
			double dStake = GetMinimumRequiredUTXOStake(dRAC, 1.1);
			sHTML += "UTXO_Target: " + RoundToString(dStake, 2) + "\n";
			double dStakeBalance = pwalletMain->GetUnlockedBalance() / COIN;
			sHTML += "Stake_Balance: " + RoundToString(dStakeBalance, 2) + "\n";
			if (dStakeBalance < dStake)
			{
				sError += "You have less stake balance available than needed for the PODC UTXO Target.  Coins must be more than 5 confirmations deep to count.  See coin control.  Please run exec totalrac.";
			}
		}

		return sHTML;
}

std::string CreateNewRosettaAccount(std::string sEmail, std::string sPass, std::string sNickname)
{
	std::string sError = "";
	std::string sURL = "http://boinc.bakerlab.org/rosetta";
	std::string sData = BoincCommand("--create_account " + sURL + " " + sEmail + " " + sPass + " " + sNickname, sError);
	std::string sAuth = ExtractXML(sData, "key:", "<EOF>");
	return sAuth;
}

std::string AttachProject(std::string sAuth)
{
	// Note:  the password is Not sent over the internet; it is sent to boinccmd which hashes the user+pass first, then asks Rosetta for the authenticator (by hash)
	std::string sError = "";
	std::string sURL = "http://boinc.bakerlab.org/rosetta";
	std::string sData = BoincCommand("--project_attach " + sURL + " " + sAuth, sError);
	if (Contains(sData, "already attached")) return "ALREADY_ATTACHED";
	// LOL, if the attaching was successful, the reply is empty
	return "";
}

std::string FixRosetta(std::string sEmail, std::string sPass, std::string& sError)
{
	std::string sHTML = "";
	if (sEmail.empty()) sError = "E-mail must be populated.";
	if (sPass.empty()) sError += "Password must be populated.";

	bool fBoincInstalled = IsBoincInstalled(sError);
	sHTML += "Boinc_Installed: " + ToYesNo(fBoincInstalled) + "\n";
	std::string sAuth = "";
	if (sError.empty())
	{
		sAuth = GetAccountAuthenticator(sEmail, sPass);
		// During the attachrosetta process, if no account exists, create one for them
		if (sAuth.empty())
		{
			sHTML += "Rosetta Account Does Not Exist:  Creating New Account\n";
			sAuth = CreateNewRosettaAccount(sEmail, sPass, sEmail);
			if (sAuth.empty())
			{
				sError += "Unable to create Rosetta Account\n";
			}
			else
			{
				sHTML += "Created new Rosetta Account: " + sAuth+ "\n";
				sAuth = GetAccountAuthenticator(sEmail, sPass);
			}
		}
	}

	if (sError.empty())
	{
		sHTML += "Rosetta_Account: " + sAuth + "\n";
		// Attach the project
		std::string sAttached = AttachProject(sAuth);
		double dRosettaRAC = 0;
		if (!sAttached.empty()) 
		{
			// This is not really an error, BOINC is telling us the project is already attached:
			sHTML += "Attaching Rosetta Project: " + sAttached + "\n";
		}
		else
		{
			sHTML += "Attaching Rosetta Project: Attached Successfully\n";
			for (int i = 0; i < 15; i++)
			{
				MilliSleep(1000);
				dRosettaRAC = GetRosettaLocalRAC();
				if (dRosettaRAC != -1) break;
			}
		}
		dRosettaRAC = GetRosettaLocalRAC();
		sHTML += "Rosetta RAC: " + RoundToString(dRosettaRAC, 0) + "\n";
		std::string sCPID = GetCPID();
		sHTML += "CPID: " + sCPID + "\n";
	}
	return sHTML;
}

std::string GetBoincResearcherHexCodeAndCPID(std::string sProjectId, int nUserId, std::string& sCPID)
{
 	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "show_user.php?userid=" + RoundToString(nUserId,0) + "&format=xml";
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 12, 5000, 1);
	if (false) LogPrintf("GetBoincResearcherHexCodeAndCPID::url     %s%s  ,User %f , BoincResponse %s \n",sProjectURL.c_str(), sRestfulURL.c_str(), (double)nUserId, sResponse.c_str());
	std::string sHexCode = ExtractXML(sResponse, "<url>","</url>");
	sHexCode = strReplace(sHexCode,"http://","");
	sHexCode = strReplace(sHexCode,"https://","");
	sCPID = ExtractXML(sResponse, "<cpid>","</cpid>");
	return sHexCode;
}


std::string GetCPIDByAddress(std::string sAddress, int iOffset)
{
	std::vector<std::string> vCPIDs = GetListOfDCCS(sAddress, true);
	int nFound = 0;
	if (vCPIDs.size() > 0)
	{
		for (int i=0; i < (int)vCPIDs.size(); i++)
		{
			std::string sCPID = GetDCCElement(vCPIDs[i], 0, false);
			std::string sInternalAddress = GetDCCElement(vCPIDs[i], 1, false);
			if (sAddress == sInternalAddress) 
			{
				nFound++;
				if (nFound > iOffset) return sCPID;
			}
		}
	}
	return "";
}

bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature)
{
	LOCK(cs_main);
	{	
		// Unlock wallet if SecureKey is available
		bool bTriedToUnlock = false;
		if (pwalletMain->IsLocked())
		{
			if (!msEncryptedString.empty())
			{
				bTriedToUnlock = true;
				if (!pwalletMain->Unlock(msEncryptedString, false))
				{
					static int nNotifiedOfUnlockIssue = 0;
					if (nNotifiedOfUnlockIssue == 0)
						LogPrintf("\nUnable to unlock wallet with SecureString.\n");
					nNotifiedOfUnlockIssue++;
					sError = "Unable to unlock wallet with PODC password provided";
					return false;
				}
			}
		}

		CBitcoinAddress addr(sBitcoinAddress);
		CKeyID keyID;
		if (!addr.GetKeyID(keyID))
		{
			sError = "Address does not refer to key";
			if (bTriedToUnlock)		{ pwalletMain->Lock();	}
			return false;
		}
		CKey key;
		if (!pwalletMain->GetKey(keyID, key))
		{
			sError = "Private key not available";
			if (bTriedToUnlock)		{ pwalletMain->Lock();	}
			return false;
		}
		CHashWriter ss(SER_GETHASH, 0);
		ss << strMessageMagic;
		ss << strMessage;
		vector<unsigned char> vchSig;
		if (!key.SignCompact(ss.GetHash(), vchSig))
		{
			sError = "Sign failed";
			if (bTriedToUnlock)		{ pwalletMain->Lock();	}
			return false;
		}
		sSignature = EncodeBase64(&vchSig[0], vchSig.size());
		if (bTriedToUnlock)
		{
			pwalletMain->Lock();
		}
		return true;
	}
}

std::string SetBoincResearcherHexCode(std::string sProjectId, std::string sAuthCode, std::string sHexKey)
{
	std::string sProjectURL = "https://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "am_set_info.php?account_key=" + sAuthCode + "&url=" + sHexKey;
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
	if (fDebugMaster) LogPrint("boinc","SetBoincResearcherHexCode::BoincResponse %s \n",sResponse.c_str());
	std::string sHexCode = ExtractXML(sResponse, "<url>","</url>");
	return sHexCode;
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
 	std::string sMessageType      = "<MT>" + sType  + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey   + "</MK>";
	std::string sMessageValue     = "<MV>" + sValue + "</MV>";
	std::string sNonce            = "<NONCE>" + sNonceValue + "</NONCE>";
	std::string sMessageSig = "";
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
    RPCSendMoneyToDestinationWithMinimumBalance(address.Get(), nAmount, nMinimumBalance, 0, 0, wtx, sError);
	if (!sError.empty()) return "";
    return wtx.GetHash().GetHex().c_str();
}

bool AdvertiseDistributedComputingKey(std::string sProjectId, std::string sAuth, std::string sCPID, int nUserId, bool fForce, std::string sUnbankedPublicKey, std::string &sError)
{	
     LOCK(cs_main);
     {
		 try
		 {
			if (!sUnbankedPublicKey.empty())
			{
				CBitcoinAddress address(sUnbankedPublicKey);
				if (!address.IsValid())
				{
					sError = "Unbanked public key invalid.";
					return false;
				}
			}
            std::string sDCC = GetDCCPublicKey(sCPID, false);
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
				sPubAddress = GenerateNewAddress(sError, "Rosetta_" + sDummyCPID);
				if (!sError.empty()) return false;
				if (sPubAddress.empty())
				{
					sError = "Unable to create new Biblepay Receiving Address.";
					return false;
				}
			}

            static int nLastDCCAdvertised = 0;
			const Consensus::Params& consensusParams = Params().GetConsensus();
	        if ((chainActive.Tip()->nHeight - nLastDCCAdvertised) < 3 && sUnbankedPublicKey.empty())
            {
                sError = _("A DCBTX was advertised less then 3 blocks ago. Please wait a full 3 blocks for your DCBTX to enter the chain.");
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
			// This is where we must create two sets of business logic, one for unbanked and one for banked - R Andrews - 3/8/2018 - Biblepay
			bool bSigned = false;
			bSigned = SignStake(sPubAddress, sData, sError, sSignature);
			if (!sUnbankedPublicKey.empty())
			{
				sError = "";
				bSigned = true;
				sSignature = "";
			}
			
			// Only append the signature after we prove they can sign...
			if (bSigned)
			{
				sData += ";" + sSignature;
			}
			else
			{
				if (sUnbankedPublicKey.empty())
				{
					sError = "Unable to sign CPID " + sCPID + " (" + sError + ")";
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
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", sUser, "", sProjectURL, sRestfulURL + sArgs, 443, "", 20, 25000, 1);
	if (false) LogPrintf("BoincResponse %s %s \n", sProjectURL + sRestfulURL, sResponse.c_str());
	std::string sAuthenticator = ExtractXML(sResponse, "<authenticator>","</authenticator>");
	return sAuthenticator;
}

int GetBoincResearcherUserId(std::string sProjectId, std::string sAuthenticator)
{
	std::string sProjectURL= "https://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "am_get_info.php?account_key=" + sAuthenticator;
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 12, 25000, 1);
	if (false) LogPrintf("BoincResponse %s %s \n",sProjectURL + sRestfulURL, sResponse.c_str());
	int nId = cdbl(ExtractXML(sResponse, "<id>","</id>"),0);
	return nId;
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

std::string GetGithubVersion()
{
	std::string sURL = "https://" + GetSporkValue("pool");
	std::string sRestfulURL = "SAN/version.htm";
	std::string sResponse = BiblepayHTTPSPost(false, 0, "", "", "", sURL, sRestfulURL, 443, "", 25, 10000, 1);
	if (sResponse.length() > 6)
	{
		sResponse = sResponse.substr(sResponse.length()-11, 11);
		sResponse = strReplace(sResponse, "\n", "");
		sResponse = strReplace(sResponse, "\r", "");
    }
	return sResponse;
}


uint256 GetDCCFileHash()
{
	std::string sContract = GetDCCFileContract();
	uint256 uhash = GetDCCHash(sContract);
	return uhash;    
}

std::string GetCPIDByRosettaID(double dRosettaID)
{
	std::vector<std::string> vCPIDs = GetListOfDCCS("", false);
	if (vCPIDs.size() > 0)
	{
		for (int i=0; i < (int)vCPIDs.size(); i++)
		{
			std::string sCPID = GetDCCElement(vCPIDs[i], 0, false);
			double dInternalRosettaId = cdbl(GetDCCElement(vCPIDs[i], 3, false),0);
			if (dInternalRosettaId == dRosettaID) return sCPID;
		}
	}
	return "";
}

std::string GetWorkUnitResultElement(std::string sProjectId, int nWorkUnitID, std::string sElement)
{
 	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "rosetta/result_status.php?ids=" + RoundToString(nWorkUnitID, 0);
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 35000, 1);
	std::string sResult = ExtractXML(sResponse, "<" + sElement + ">", "</" + sElement + ">");
	return sResult;
}


double GetTeamRAC()
{
	std::string sProjectURL = "http://" + GetSporkValue("pool");
	std::string sRestfulURL = "Action.aspx?action=teamrac&format=xml";
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
	double dRac = cdbl(ExtractXML(sResponse,"<TEAMRAC>", "</TEAMRAC>"), 2);
	return dRac;
}

double GetBoincTeamByUserId(std::string sProjectId, int nUserId)
{
	std::string sProjectURL = "http://" + GetSporkValue(sProjectId);
	std::string sRestfulURL = "show_user.php?userid=" + RoundToString(nUserId,0) + "&format=xml";
	std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 20, 5000, 1);
	double dTeam = cdbl(ExtractXML(sResponse,"<teamid>", "</teamid>"), 0);
	return dTeam;
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
   				  if (nTotalBlock > (nBudget * .50) && nBudget > 0) return b;
			}
		}
	}
	return 0;
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

int GetSanctuaryCount()
{
	std::vector<std::pair<int, CMasternode> > vMasternodeRanks = mnodeman.GetMasternodeRanks();
	int iSanctuaryCount = vMasternodeRanks.size();
	return iSanctuaryCount;
}

int MyRank(int nHeight)
{
    if(!fMasterNode) return 0;
	int nMinRequiredProtocol = mnpayments.GetMinMasternodePaymentsProto();
    int nRank = mnodeman.GetMasternodeRank(activeMasternode.vin, nHeight, nMinRequiredProtocol, false);
	return nRank;
}

double MyPercentile(int nHeight)
{
	double dRank = cdbl(GetArg("-sanctuaryrank", "0"), 0);
	if (dRank > 0) return dRank; // Allow aggregators to constantly re-assess the contract for stats sites
	int iRank = MyRank(nHeight);
	int iSanctuaryCount = GetSanctuaryCount();
	//Note: We can also :  return mnodeman.CountEnabled() if we want only Enabled masternodes
	if (iSanctuaryCount < 1) return 0;
	double dPercentile = iRank / iSanctuaryCount;
	if (fDebugMaster) LogPrint("podc", " MyPercentile Rank %f, SancCount %f, Percent %f",(double)iRank,(double)iSanctuaryCount,(double)dPercentile*100);
	return dPercentile * 100;
}

std::string GetBoincHostsByUser(int iRosettaID, std::string sProjectId)
{
    	std::string sProjectURL= "https://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "rosetta/hosts_user.php?userid=" + RoundToString(iRosettaID, 0);
		std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 25, 95000, 1);
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

bool SignCPID(std::string sCPID, std::string& sError, std::string& out_FullSig)
{
	// Sign Researcher CPID - 2/8/2018 - Rob A. - Biblepay
	if (sCPID.empty()) sCPID = GetElement(msGlobalCPID, ";", 0);
	std::string sHash = GetRandHash().GetHex();
	std::string sDCPK = GetDCCPublicKey(sCPID, true);
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

bool IsStakeSigned(std::string sXML)
{
	std::string sSignature = ExtractXML(sXML, "<SIG_0>","</SIG_0>");
	return (sSignature.empty()) ? false : true;
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

double GetPBase()
{
	std::string sBase1 = DecodeBase64(GetSporkValue("pbase1"));
	std::string sBase2 = DecodeBase64(GetSporkValue("pbase2"));
	std::string sBase3 = DecodeBase64(GetSporkValue("pbase3"));
	std::string sError = "";
	std::string sRes = BiblepayHTTPSPost(false, 0, "", "", "", sBase1, sBase2, 443, "", 15, 35000, 3);
	double dBase1 = cdbl(ExtractXML(sRes, sBase3 + "\":", "}"), 12);
    return dBase1;
}


bool VerifyDarkSendSigner(std::string sXML)
{
	std::string sSignature = ExtractXML(sXML, "<sig>", "</sig>");
	std::string sSigner = ExtractXML(sXML, "<signer>", "</signer>");
	std::string sMessage = ExtractXML(sXML, "<message>", "</message>");
	std::string sError = "";
	bool fValid = CheckStakeSignature(sSigner, sSignature, sMessage, sError);
	return fValid;
}

double GetQTPhase(double dPrice, int nEventHeight, double& out_PriorPrice, double& out_PriorPhase)
{
	// If -1 is passed in, the caller wants the prior days QT level and price.
    double nMaximumTighteningPercentage = GetSporkDouble("qtmaxpercentage", 0);
	// If feature is off return 0:
	if (nMaximumTighteningPercentage == 0) return 0;
	double nPriceThreshhold = GetSporkDouble("qtpricethreshhold", 0);
	
	// Step 1:  If our price is > QTPriceThreshhold (initially .01), we are in phase 0
	if (dPrice >= nPriceThreshhold) return 0;
	// Step 2: If price is 0, something is wrong.
	if (dPrice == 0) return 0;
	
	// Step 3: Get yesterdays phase
	// Find the last superblock height before this height
	
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iNextSuperblock = 0;
	nEventHeight -= 1;
	int nTotalSamples = 3;
	// Check N Samples for the first valid price and phase
	for (int iDay = 0; iDay < nTotalSamples; iDay++)
	{
		int iLastSuperblock = GetLastDCSuperblockHeight(nEventHeight-1, iNextSuperblock);
		//LogPrintf(" Looking for superblock before %f at %f ", nEventHeight, iLastSuperblock);
		if (iLastSuperblock > chainActive.Tip()->nHeight)
		{
			LogPrintf(" Last Superblock %f, tip %f ", iLastSuperblock, chainActive.Tip()->nHeight);
		}
		if (nEventHeight < 1000 || iLastSuperblock < 1000 || iLastSuperblock > chainActive.Tip()->nHeight) return 0;
		std::string sXML = "";
		CBlockIndex* pindex = FindBlockByHeight(iLastSuperblock);
		if (pindex != NULL)
		{
			CBlock block;
			if (ReadBlockFromDisk(block, pindex, consensusParams, "IsGovObjPaid")) 
			{
				for (unsigned int i = 0; i < block.vtx[0].vout.size(); i++)
				{
					sXML += block.vtx[0].vout[i].sTxOutMessage;
				}		
			}
			bool bValid = VerifyDarkSendSigner(sXML);
			if (bValid)
			{
				out_PriorPhase = cdbl(ExtractXML(sXML, "<qtphase>", "</qtphase>"), 0);
				out_PriorPrice = cdbl(ExtractXML(sXML, "<price>", "</price>"), 12);
				break;
			}
			else
			{
				// LogPrintf(" unable to verify price %s for height %f ",sXML.c_str(), iLastSuperblock);
			}
		}
		else
		{
			return 0;
		}
		nEventHeight -= consensusParams.nDCCSuperblockCycle;
		if (nEventHeight < 1000) break;
	}
	double dPhase = out_PriorPhase + 1;
	if (dPhase > nMaximumTighteningPercentage) dPhase = nMaximumTighteningPercentage; 
	return dPhase;
}


bool GetContractPaymentData(std::string sContract, int nBlockHeight, std::string& sPaymentAddresses, std::string& sAmounts)
{
	// Proof-of-distributed-computing (Feb 19th 2018)
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
	std::vector<std::string> vRows = Split(sContract.c_str(), "<ROW>");
	double dTotalPaid = 0;
	
	for (int i = 0; i < (int)vRows.size(); i++)
	{
		std::vector<std::string> vCPID = Split(vRows[i].c_str(), ",");
		if (vCPID.size() >= 3)
		{
			std::string sCpid = vCPID[1];
			std::string sAddress = vCPID[0];
			double dMagnitude = cdbl(vCPID[2],2);
			if (!sCpid.empty() && dMagnitude > 0 && !sAddress.empty())
			{
				CBitcoinAddress cbaResearcherAddress(sAddress);
				if (cbaResearcherAddress.IsValid()) 
				{
					double dOwed = PaymentPerMagnitude * dMagnitude;
					dTotalPaid += dOwed;
					sPaymentAddresses += sAddress + "|";
					sAmounts += RoundToString(dOwed, 2) + "|";
				}
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
	sJson += GJE("payment_addresses", sPaymentAddresses,  true, true);
	sJson += GJE("payment_amounts",   sPaymentAmounts,    true, true);
	sJson += GJE("proposal_hashes",   sProposalHashes,    true, true);
	// QT - Quantitative Tightening - R ANDREWS - 9-14-2018
	double dPrice = GetPBase();
	std::string sPrice = RoundToString(dPrice, 12);
	sJson += GJE("price", sPrice, true, true);
	double out_PriorPrice = 0;
	double out_PriorPhase = 0;
	double dPhase = GetQTPhase(dPrice, nEventBlockHeight, out_PriorPrice, out_PriorPhase);
	std::string sPhase = RoundToString(dPhase, 0);
	sJson += GJE("qtphase", sPhase, true, true);
	std::string sSig = SignPrice(sPrice);
	sJson += GJE("sig", sSig, true, true);
	bool fSigValid = VerifyDarkSendSigner(sSig);
	LogPrintf("Creating Contract Sig %s  sigvalid %f ",sSig.c_str(),(double)fSigValid);

	sJson += GJE("type", sType, false, false); 
	sJson += "}]]";
	std::vector<unsigned char> vchJson = vector<unsigned char>(sJson.begin(), sJson.end());
	std::string sHex = HexStr(vchJson.begin(), vchJson.end());
	return sHex;
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


CTransaction CreateCoinStake(CBlockIndex* pindexLast, CScript scriptCoinstakeKey, double dProofOfLoyaltyPercentage, int iMinConfirms, std::string& sXML, std::string& sError)
{
	// This is part of POG
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
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptCoinstakeKey, nTargetValue, false, false, false, false, false, "", "", "", ""};
	recipient.Message = "<polweight>" + RoundToString(nTargetValue/COIN,2) + "</polweight>";
				
    vecSend.push_back(recipient);

	CWalletTx wtx;

	bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ONLY_NOT1000IFMN, false, iMinConfirms);
	if (!fCreated)    
	{
		sError = "INSUFFICIENT FUNDS";
		return ctx;
	}

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

std::string GetBoincTasksByHost(int iHostID, std::string sProjectId)
{
	std::string sOut = "";
    for (int i = 0; i < 120; i = i + 20)
    {
		std::string sProjectURL= "https://" + GetSporkValue(sProjectId);
		std::string sRestfulURL = "rosetta/results.php?hostid=" + RoundToString(iHostID, 0) + "&offset=" + RoundToString(i,0) + "&show_names=0&state=1&appid=";
		std::string sResponse = BiblepayHTTPSPost(true, 0, "", "", "", sProjectURL, sRestfulURL, 443, "", 25, 95000, 1);
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

bool PODCUpdate(std::string& sError, bool bForce, std::string sDebugInfo)
{
	if (!fDistributedComputingEnabled) return false;
	std::vector<std::string> vCPIDS = Split(msGlobalCPID.c_str(), ";");
	std::string sPrimaryCPID = GetElement(msGlobalCPID, ";", 0);
	if (sPrimaryCPID.empty())
	{
		sError = "Unable to find any CPIDS.  Please try exec getboincinfo.";
		return false;
	}
	double dDRMode = cdbl(GetSporkValue("dr"), 0);
	// 0) Heavenly               = UTXOWeight * TaskWeight * RAC = Magnitude
	// 1) Possessed by UTXO      = UTXOWeight * RAC = Magnitude
	// 2) Possessed by Tasks     = TaskWeight * RAC = Magnitude  
	// 3) The-Law                = RAC = Magnitude
	// 4) DR (Disaster-Recovery) = Proof-Of-BibleHash Only (Heat Mining only) (Mode 1, 3, 4 are not checking tasks)
	bool bCheckingTasks = true;
	if (dDRMode == 1 || dDRMode == 3 || dDRMode == 4) bCheckingTasks = false;

	bool bForceUTXO = false;
	if (!bCheckingTasks)
	{
		// In this mode the sanctuaries are not checking tasks, so forcefully send a UTXO Update (to allow backup project to function)
		sDebugInfo = "00000=00000";
		bForceUTXO = true;
	}
	int64_t iMaxSeconds = 60 * 24 * 30 * 12 * 60;
	int iInserted = 0;
	int iCPIDSProcessed = 0;	
	for (int i = 0; i < (int)vCPIDS.size(); i++)
	{
		std::string s1 = vCPIDS[i];
		if (!s1.empty())
		{
			std::string sData = RetrieveDCCWithMaxAge(s1, iMaxSeconds);
			std::string sAddress = GetDCCElement(sData, 1, true);
			std::string sOutstanding = "";
			if (!sData.empty() && !sAddress.empty())
			{
				std::string sUserId = GetDCCElement(sData, 3, true);
				int nUserId = cdbl(sUserId, 0);
				if (nUserId > 0)
				{
					iCPIDSProcessed++;
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
									iInserted++;
									if (iInserted > 255 || sOutstanding.length() > 35000) break; // Don't let them blow up the blocksize
									if (iInserted > 1 && !bCheckingTasks) break; // Don't spam the transactions if we aren't using the data
								}
							}
						}
					}
				}
			}
			if (!sDebugInfo.empty())
			{
				sOutstanding += sDebugInfo + "=" + RoundToString(GetAdjustedTime(), 0) + ",";
				iInserted++;
			}
			if (!sOutstanding.empty())
			{
				sOutstanding = ChopLast(sOutstanding);
				mnPODCTried++;
					
				// Create PODC UTXO Transaction - R Andrews - 02-20-2018 - These tasks will be checked on the Sanctuary side, to ensure they were started at this time.  
				// If so, the UTXO COINAMOUNT * AGE * RAC will be rewarded for this task.
				double nMinimumChatterAge = GetSporkDouble("podcminimumchatterage", (60 * 60 * 4));
				if (bForceUTXO) LogPrintf("Forcefully creating UTXO transmission %s %f", sDebugInfo.c_str(), (double)nMinimumChatterAge);
				std::string sCurrentState = ReadCacheWithMaxAge("CPIDTasks", s1, nMinimumChatterAge);
				double nMaximumChatterAge = GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
				std::string sOldState = ReadCacheWithMaxAge("CPIDTasks", s1, nMaximumChatterAge);
				bool bFresh = (sOldState == sOutstanding || sOutstanding == sCurrentState || (!sCurrentState.empty()));
				if (bForce) bFresh = false;
				if (!bFresh)
				{
					double dRAC = AscertainResearcherTotalRAC();
					double dUTXOOverride = cdbl(GetArg("-utxooverride", "0"), 2);
					double dUTXOAmount = GetMinimumRequiredUTXOStake(dRAC, 1.1);
					if (dUTXOOverride > 0 || dUTXOOverride < 0) dUTXOAmount = dUTXOOverride;
					LogPrintf(" PODCUpdate::Creating UTXO, Users RAC %f for %f ",dRAC, (double)dUTXOAmount);
					CAmount curBalance = pwalletMain->GetUnlockedBalance(); // 3-5-2018, R Andrews
					CAmount nTargetValue = dUTXOAmount * COIN;
					if (nTargetValue > curBalance && curBalance > (1*COIN))
					{
						LogPrintf(" \n PODCUpdate::Creating UTXO, Stake balance %f less than required amount %f, changing Target to stake balance. \n",(double)curBalance/COIN, (double)nTargetValue/COIN);
						nTargetValue = curBalance;
					}
					if ((nTargetValue < 1 || dUTXOAmount < 0) && !bForceUTXO)
					{
						sError = "Unable to create PODC UTXO::Target UTXO too low.";
						return false;
					}
					if (curBalance < nTargetValue)
					{
						sError = "Unable to create PODC UTXO::Balance (" + RoundToString(curBalance/COIN, 2) + ") less than target UTXO (" + RoundToString(nTargetValue/COIN,2) + ").";
						return false;
					}
					std::string sFullSig = "";
					bool bTriedToUnlock = false;
					LOCK(cs_main);
					{
						if (!msEncryptedString.empty() && pwalletMain->IsLocked())
						{
							bTriedToUnlock = true;
							if (!pwalletMain->Unlock(msEncryptedString, false))
							{
								static int nNotifiedOfUnlockIssue = 0;
								if (nNotifiedOfUnlockIssue == 0)
								{
									WriteCache("poolthread0", "poolinfo2", "Unable to unlock wallet with password provided.", GetAdjustedTime());
								}
								sError = "Unable to unlock wallet with SecureString.";
								nNotifiedOfUnlockIssue++;
							}
						}

						// Sign
						std::string sErrorInternal = "";
						bool fSigned = SignCPID(s1, sErrorInternal, sFullSig);
						if (!fSigned)
						{
							sError = "Failed to Sign CPID Signature. [" + sErrorInternal + "]";
							if (bTriedToUnlock) pwalletMain->Lock();
							return false;
						}
						std::string sXML = "<PODC_TASKS>" + sOutstanding + "</PODC_TASKS>" + sFullSig;
						double dMinCoinAge = cdbl(GetSporkValue("podcmincoinage"), 0);
	
						AddBlockchainMessages(sAddress, "PODC_UPDATE", s1, sXML, nTargetValue, dMinCoinAge, sErrorInternal);
						if (bTriedToUnlock) pwalletMain->Lock();

						if (!sErrorInternal.empty())
						{
							sError = sErrorInternal;
							return false;
						}
						else
						{
							mnPODCSent++;
							mnPODCAmountSent += dUTXOAmount;
						}
						if (fDebugMaster) LogPrint("podc", "\n PODCUpdate::Signed UTXO: %s ", sXML.c_str());
						WriteCache("CPIDTasks", s1, sOutstanding, GetAdjustedTime());
					}
				}
			}
		}
	}
	if (iInserted == 0)
	{
		sError = "Processed 0 tasks for CPID " + GetElement(msGlobalCPID, ";", 0);
		return false;
	}
	sError = "Processed (" + RoundToString((double)iInserted, 0) + ") over " + RoundToString((double)iCPIDSProcessed, 0) + " CPID(s) successfully.";
	LogPrintf(" UTXOUpdate %s ", sError.c_str());
	return true;
}

int64_t RetrieveCPIDAssociationTime(std::string cpid)
{
	std::string key = "DCC;" + cpid;
	return mvApplicationCacheTimestamp[key];
}

UniValue UTXOReport(std::string sCPID)
{
    UniValue Report(UniValue::VOBJ);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iBlocks = 0;
	int64_t nLastTimestamp = GetAdjustedTime();
	double dAvgAmount = 0;
	double dTotalAmount = 0;
	double dInstances = 0;
	double dAvgSpan = 0;
	double dTotalSpan = 0;
	int b = chainActive.Tip()->nHeight;
	for (; b > 1; b--)
	{
		CBlockIndex* pindex = FindBlockByHeight(b);
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams, "MemorizeBlockChainPrayers")) 
		{
			iBlocks++;
			if (iBlocks > (BLOCKS_PER_DAY*10)) break;
			BOOST_FOREACH(const CTransaction &tx, block.vtx)
			{
				double dUTXOAmount = 0;
				std::string sMsg = "";
			
				for (unsigned int i = 0; i < tx.vout.size(); i++)
				{
					sMsg += tx.vout[i].sTxOutMessage;
					dUTXOAmount += tx.vout[i].nValue / COIN;
				}
				std::string sPODC = ExtractXML(sMsg, "<PODC_TASKS>", "</PODC_TASKS>");
				if (!sPODC.empty())
				{
					std::string sErr2 = "";
					std::string sMySig = ExtractXML(sMsg,"<cpidsig>","</cpidsig>");
					bool fSigChecked = VerifyCPIDSignature(sMySig, true, sErr2);
					std::string sDiskCPID = GetElement(sMySig, ";", 0);
					if (fSigChecked && sDiskCPID == sCPID)
					{
						dTotalAmount += dUTXOAmount;
						dInstances++;
						std::string sTimestamp = TimestampToHRDate(block.GetBlockTime());
						int64_t nTimestamp = block.GetBlockTime();
						int64_t nSpan = nLastTimestamp - nTimestamp;
						std::string sHash = tx.GetHash().GetHex();
						std::string sEntry = RoundToString(b, 0) + " [" + sTimestamp + "]" + " (" + RoundToString(dUTXOAmount,0) + " BBP) [TXID=" + sHash + "] ";
						double nSpanDays = nSpan / 86400.01;
						dTotalSpan += nSpanDays;
						Report.push_back(Pair(sEntry, RoundToString(nSpanDays, 2)));
    					nLastTimestamp = nTimestamp;
					}
				}
			}
		}
	}
	dAvgSpan = dTotalSpan / (dInstances + .01);
	dAvgAmount = dTotalAmount / (dInstances + .01);
	Report.push_back(Pair("Average Span (Days)", dAvgSpan));
	Report.push_back(Pair("Average UTXO Amount", dAvgAmount));
	return Report;
}

double GetUserMagnitude(std::string sListOfPublicKeys, double& nBudget, double& nTotalPaid, int& out_iLastSuperblock, std::string& out_Superblocks, int& out_SuperblockCount, int& out_HitCount, double& out_OneDayPaid, double& out_OneWeekPaid, double& out_OneDayBudget, double& out_OneWeekBudget)
{
	// Query actual magnitude from last superblock
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iNextSuperblock = 0;  
	for (int b = chainActive.Tip()->nHeight; b > 1; b--)
	{
		int iLastSuperblock = GetLastDCSuperblockHeight(b + 1, iNextSuperblock);
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
							if (Contains(sListOfPublicKeys, sRecipient))
							{
								nTotalPaid += dAmount;
								if (Age > 0 && Age < 86400)
								{
									out_OneDayPaid += dAmount;
								}
								if (Age > 0 && Age < (7 * 86400)) out_OneWeekPaid += dAmount;
							}
					  }
					  if (nTotalBlock > (nBudget * .50) && nBudget > 0) 
					  {
						    if (out_iLastSuperblock == 0) out_iLastSuperblock = iLastSuperblock;
						    out_Superblocks += RoundToString(b,0) + ",";
							out_HitCount++;
							if (Age > 0 && Age < 86400)
							{
								out_OneDayBudget += nBudget;
							}
							if (Age > 0 && Age < (7 * 86400)) 
							{
								out_OneWeekBudget += nBudget;
							}
							if (Age > (7 * 86400)) 
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

int64_t GetDCCFileTimestamp()
{
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";
	boost::filesystem::path pathFiltered(sDailyMagnitudeFile);
	if (!boost::filesystem::exists(pathFiltered)) return GetAdjustedTime() - 0;
	int64_t nTime = last_write_time(pathFiltered);
	return nTime;
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


double GetPaymentByCPID(std::string CPID, int nHeight)
{
	// 616WestWarnsworth - 6-27-2018 - Change the rule (in order to heat mine) from 'CPID with Magnitude' to 'CPID that has staked a UTXO within the last 7 days'
	// In order for this function to pass the checkblock test, the return value must be > .50
	std::string sDCPK = GetDCCPublicKey(CPID, true);
	if (sDCPK.empty()) return -2;
	if (CPID.empty()) return -3;

	if ((fProd && nHeight > F13000_CUTOVER_HEIGHT_PROD) || (!fProd && nHeight > F13000_CUTOVER_HEIGHT_TESTNET))
	{
		// The new way - CPIDs Research address must have had a payment within the last 7 days:
		/* Reserved:  Tracking UTXO Amount Staked in last 7 Days
		
		double dUTXOStakedInLast7Days = cdbl(ReadCacheWithMaxAge("MatureUTXOWeight", CPID, GetHistoricalMilestoneAge(14400, 60 * 60 * 24 * 7)), 0);
		return dUTXOStakedInLast7Days;
		*/
		double dPODCPayments = cdbl(ReadCacheWithMaxAge("AddressPayment", sDCPK, GetHistoricalMilestoneAge(14400, 60 * 60 * 24 * 7)), 0);
		return dPODCPayments;
	}
	// Otherwise fall through to the old way...


	// 2-10-2018 - R ANDREWS - BIBLEPAY - Provide ability to return last payment amount (in most recent superblock) for a given CPID
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iNextSuperblock = 0;  
	int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	CBlockIndex* pindex = FindBlockByHeight(iLastSuperblock);
	CBlock block;
	double nTotalBlock = 0;
	double nBudget = 0;
	if (pindex == NULL) return -1;
	double nTotalPaid=0;
	nTotalBlock = 0;
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
	bool bSuperblockHit = (nTotalBlock > (nBudget * .50) && nBudget > 0);
	if (!bSuperblockHit) return -1;
	return nTotalPaid;
}

int GetRequiredQuorumLevel(int nHeight)
{
	// This is an anti-ddos feature that prevents ddossing the distributed computing grid
	// REQUIREDQUORUM = 10% or 2%
	int iSanctuaryQuorumLevel = fProd ? .10 : .02;
	int iRequiredVotes = GetSanctuaryCount() * iSanctuaryQuorumLevel;
	if (fProd && iRequiredVotes < 3) iRequiredVotes = 3;
	if (!fProd && iRequiredVotes < 2) iRequiredVotes = 2;
	if (!fProd && nHeight < 7000) iRequiredVotes = 1; // Testnet Level 1
	return iRequiredVotes;
}

bool VoteForGobject(uint256 govobj, std::string sVoteOutcome, std::string& sError)
{
	    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal("funding");
		//yes, no or abstain

        vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(sVoteOutcome);
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
		sError = "Unable to vote for DC Contract::Foreign addresses or amounts empty.";
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
	bool bResult = VoteForGobject(uGovObjHash, "yes", sError);
	return bResult;
}


bool IsMature(int64_t nTime, int64_t nMaturityAge)
{
	int64_t nNow = GetAdjustedTime();
	int64_t nRemainder = nNow % nMaturityAge;
	int64_t nCutoff = nNow - nRemainder;
	bool bMature = nTime <= nCutoff;
	return bMature;
}

std::string GetMyPublicKeys()
{
	std::string sPK = "";
	BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        std::string strName = item.second.name;
        bool fMine = IsMine(*pwalletMain, address.Get());
        if (fMine)
		{
		    std::string sAddress = CBitcoinAddress(address).ToString();
			sPK += sAddress + "|";
	    }
	}
	if (sPK.length() > 1) sPK = sPK.substr(0,sPK.length()-1);
	return sPK;
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
	
	// 4-3-2018 - R ANDREWS - Honor Sanctuary Aggregator Nodes
	double dAggregationRank = cdbl(GetArg("-sanctuaryrank", "0"), 0);
	bool bAggregator = (dAggregationRank > 0 && dAggregationRank < 10);
	if (!AmIMasternode()) return "NOT_A_SANCTUARY";
	
	// This happens on sanctuaries only.  The node will check to see if the contract duration expired.
	// When it expires, we must assemble a new contract as a sanctuary team.
	// Since the contract is valid for 86400 seconds, we start this process one hour early (IE 82800 seconds after the last valid contract started)
	if (!chainActive.Tip()) return "INVALID_CHAIN";
	std::string sContract = RetrieveCurrentDCContract(chainActive.Tip()->nHeight, 82800);
	if (!sContract.empty() && !bAggregator) return "ACTIVE_CONTRACT";

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
	
	if (bPending && !bAggregator) 
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
			// Synchronized Sanctuary Quorum Download:  R ANDREWS - 6-26-2018
			int64_t iFileTimestamp = GetDCCFileTimestamp();
			// Is file older than the current quorum timestamp:
			bool bIsOld = IsMature(iFileTimestamp, 60 * 60 * 4);
			LogPrintf(" DCC hash %s  Age %f  FileTimeStamp %f   IsOld  %s ",uDCChash.GetHex(), (float)nAge, (float)iFileTimestamp, ToYesNo(bIsOld).c_str());
			
			if (uDCChash == uint256S("0x0") || bIsOld)
			{
				// Pull down the distributed computing file
				LogPrintf("\n Chosen Sanctuary - pulling down the DCC file... Aggregator %f, Percentile %f \n", dAggregationRank, MyPercentile(iLastSuperblock));
				bool fSuccess =  DownloadDistributedComputingFile(iNextSuperblock, sError);
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

std::string rPad(std::string data, int minWidth)
{
	if ((int)data.length() >= minWidth) return data;
	int iPadding = minWidth - data.length();
	std::string sPadding = std::string(iPadding,' ');
	std::string sOut = data + sPadding;
	return sOut;
}
