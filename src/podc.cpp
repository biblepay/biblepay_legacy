// Copyright (c) 2009-2010 Satoshi Nakamoto
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
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpcserver.h"
#include "podc.h"
#include "streams.h"
#include "sync.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "base58.h"

#include "darksend.h"
#include "wallet/wallet.h"

#include "masternode-payments.h"

#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "masternode-sync.h"
#include "main.h"

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
std::string BiblepayHTTPSPost(bool bPost, int iThreadID, std::string sActionName, std::string sDistinctUser, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, std::string sSolution, int iTimeoutSecs, int iMaxSize, int iBreakOnError);
CBlockIndex* FindBlockByHeight(int nHeight);
bool DownloadDistributedComputingFile(int iNextSuperblock, std::string& sError);
void WriteCache(std::string section, std::string key, std::string value, int64_t locktime);
std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, bool fSign, std::string& sError);
std::string DefaultRecAddress(std::string sType);
std::string AddBlockchainMessages(std::string sAddress, std::string sType, std::string sPrimaryKey, std::string sHTML, CAmount nAmount, std::string& sError);
int GetMinimumResearcherParticipationLevel();
double GetMinimumMagnitude();
std::string GetBoincPasswordHash(std::string sProjectPassword, std::string sProjectEmail);
std::string GetSporkValue(std::string sKey);
std::string GetBoincAuthenticator(std::string sProjectID, std::string sProjectEmail, std::string sPasswordHash);
std::string GetDCCElement(std::string sData, int iElement, bool fCheckSignature);

void PurgeCacheAsOfExpiration(std::string sSection, int64_t nExpiration);
std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nMaxAge);
std::string GetBoincResearcherHexCodeAndCPID(std::string sProjectId, int nUserId, std::string& sCPID);

bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError);

int GetBoincResearcherUserId(std::string sProjectId, std::string sAuthenticator);


std::string RetrieveMd5(std::string s1);
int MyRank(int nHeight);
bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature);
std::string GenerateNewAddress(std::string& sError, std::string sName);
std::string GetMyPublicKeys();
void ClearCache(std::string sSection);
std::string RetrieveDCCWithMaxAge(std::string cpid, int64_t iMaxSeconds);
std::string GetDCCPublicKey(const std::string& cpid, bool fRequireSig);
extern std::string ExecuteDistributedComputingSanctuaryQuorumProcess();

void ClearSanctuaryMemories();

int GetLastDCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock);
int GetSanctuaryCount();





int GetPODCVersion()
{
	return 1; 
}

std::string PackPODC(std::string sBlock, int iTaskLength, int iTimeLength)
{
    // For each Task in the PODC update, convert data to binary
    std::vector<std::string> vRows = Split(sBlock.c_str(), ",");
    std::string sBinary = "";
    for (unsigned int i = 0; i < vRows.size(); i++)
    {
        if (vRows[i].length() > 1)
        {
            double dTask = cdbl(GetElement(vRows[i], "=", 0), 0);
			double dTime = cdbl(GetElement(vRows[i], "=", 1), 0);
            std::string sHexTask = DoubleToHexStr(dTask, 12);
            std::string sHexTime = DoubleToHexStr(dTime, 12);
            std::string sBinTask = ConvertHexToBin(sHexTask, iTaskLength);
            std::string sBinTime = ConvertHexToBin(sHexTime, iTimeLength);
			std::string sEntry = sBinTask + sBinTime;
            sBinary += sEntry;
        }
    }
	return sBinary;
}


std::string UnpackPODC(std::string sBlock, int iTaskLength, int iTimeLength)
{
    if (sBlock.empty()) return sBlock;
    std::string sReconstructedMagnitudes = "";
	int iStepLength = iTaskLength + iTimeLength;
	std::string sReconstituted = "";
    for (unsigned int x = 0; x < sBlock.length(); x += iStepLength)
    {
        if (sBlock.length() >= x + iStepLength)
        {
            std::string sTask = sBlock.substr(x, iTaskLength);
            std::string sTime = sBlock.substr(x + iTaskLength, iTimeLength);
		    std::string sHexTask = ConvertBinToHex(sTask);
            std::string sHexTime = ConvertBinToHex(sTime);
            double dTask = ConvertHexToDouble("0x" + sHexTask);
			double dTime = ConvertHexToDouble("0x" + sHexTime);
            std::string sRow = RoundToString(dTask, 0) + "=" + RoundToString(dTime, 0) + ",";
            sReconstituted += sRow;
        }
    }
	sReconstituted = ChopLast(sReconstituted);
    return sReconstituted;
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
		if (!Contains(sLine, sEndElement))
		{
			sOut += sLine + "\r\n";
		}
	}
	return "";
}

std::string MutateToList(std::string sData)
{
	std::vector<std::string> vInput = Split(sData.c_str(),"<ROW>");
	std::string sList = "";
	for (int i = 0; i < (int)vInput.size(); i++)
	{
		double dRosettaID = cdbl(GetElement(vInput[i], ",", 0),0);
		if (dRosettaID > 0)
		{
			sList += RoundToString(dRosettaID,0) + ",";
		}
	}
	ChopLast(sList);
	return sList;
}

double GetResearcherCredit(double dDRMode, double dAvgCredit, double dUTXOWeight, double dTaskWeight, double dUnbanked, double dTotalRAC, double dReqSPM, double dReqSPR)
{

	// Rob Andrews - BiblePay - 02-21-2018 - Add ability to run in distinct modes (via sporks):
	// 0) Heavenly               = UTXOWeight * TaskWeight * RAC = Magnitude
	// 1) Possessed by UTXO      = UTXOWeight * RAC = Magnitude
	// 2) Possessed by Tasks     = TaskWeight * RAC = Magnitude  
	// 3) The-Law                = RAC = Magnitude
	// 4) DR (Disaster-Recovery) = Proof-Of-BibleHash Only (Heat Mining only)
	double dModifiedCredit = 0;
				
	if (dDRMode == 0)
	{
		dModifiedCredit = dAvgCredit * EnforceLimits(GetUTXOLevel(dUTXOWeight, dTotalRAC, dAvgCredit, dReqSPM, dReqSPR) / 100) * EnforceLimits(dTaskWeight / 100);
		if (dUnbanked == 1) dModifiedCredit = dAvgCredit * 1 * 1;
	}
	else if (dDRMode == 1)
	{
		dModifiedCredit = dAvgCredit * EnforceLimits(GetUTXOLevel(dUTXOWeight, dTotalRAC, dAvgCredit, dReqSPM, dReqSPR) / 100);
		if (dUnbanked==1) dModifiedCredit = dAvgCredit * 1 * 1;
	}
	else if (dDRMode == 2)
	{
		dModifiedCredit = dAvgCredit * EnforceLimits(dTaskWeight/100);
		if (dUnbanked == 1) dModifiedCredit = dAvgCredit * 1 * 1;
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

double GetUTXOLevel(double dUTXOWeight, double dTotalRAC, double dAvgCredit, double dRequiredSPM, double dRequiredSPR)
{
	double dEstimatedMagnitude = (dAvgCredit / (dTotalRAC + .01)) * 1000;
	if (dTotalRAC == 0 && dRequiredSPR == 0) return 100; //This happens in Phase 1 of the Magnitude Calculation
	if (dEstimatedMagnitude > 1000) dEstimatedMagnitude = 1000;
	double dRequirement = 0;
	if (dRequiredSPM > 0) 
	{
		dRequirement = dEstimatedMagnitude * dRequiredSPM;
	}
	else if (dRequiredSPR > 0)
	{
		// If using Stake-Per-RAC, then 
		dRequirement = dAvgCredit * dRequiredSPR;
	}
	double dAchievementPercent = (dUTXOWeight / (dRequirement + .01)) * 100;
	double dSnappedAchievement = SnapToGrid(dAchievementPercent);
	LogPrintf(" EstimatedMag %f, UTXORequirement %f, AchievementPecent %f, SnappedAchivement %f \n", dEstimatedMagnitude,
		dRequirement, dAchievementPercent, dSnappedAchievement);
	return dSnappedAchievement;
}

double SnapToGrid(double dPercent)
{
	double dOut = 0;
	if (dPercent >= 4 && dPercent <= 10) 
	{
		dOut = 10;
	}
	else if (dPercent > 10 && dPercent <= 20)
	{
		dOut = 20;
	}
	else if (dPercent > 20 && dPercent <= 30)
	{
		dOut = 30;
	}
	else if (dPercent > 30 && dPercent <= 40)
	{
		dOut = 40;
	}
	else if (dPercent > 40 && dPercent <= 50)
	{
		dOut = 50;
	}
	else if (dPercent > 50 && dPercent <= 60)
	{
		dOut = 60;
	}
	else if (dPercent > 60 && dPercent <= 70)
	{
		dOut = 70;
	}
	else if (dPercent > 70 && dPercent <= 80)
	{
		dOut = 80;
	}
	else if (dPercent > 80 && dPercent <= 90)
	{
		dOut = 90;
	}
	else if (dPercent > 90)
	{
		dOut = 100;
	}
	else
	{
		dOut = 0;
	}
	return dOut;
}

double EnforceLimits(double dValue)
{
	if (dValue < 0) dValue = 0;
	if (dValue > 1) dValue = 1;
	return dValue;
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

void TouchDailyMagnitudeFile()
{
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";
	FILE *outMagFile = fopen(sDailyMagnitudeFile.c_str(),"w");
	std::string sDCC = GetRandHash().GetHex();
    fputs(sDCC.c_str(), outMagFile);
	fclose(outMagFile);
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


std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end)
{
	std::string extraction = "";
	std::string::size_type loc = XMLdata.find( key, 0 );
	if( loc != std::string::npos )
	{
		std::string::size_type loc_end = XMLdata.find( key_end, loc+3);
		if (loc_end != std::string::npos )
		{
			extraction = XMLdata.substr(loc+(key.length()),loc_end-loc-(key.length()));
		}
	}
	return extraction;
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


double Round(double d, int place)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(place) << d ;
	double r = boost::lexical_cast<double>(ss.str());
	return r;
}



std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr)
{
  size_t pos = 0;
  while((pos = str.find(oldStr, pos)) != std::string::npos){
     str.replace(pos, oldStr.length(), newStr);
     pos += newStr.length();
  }
  return str;
}

double cdbl(std::string s, int place)
{
	if (s=="") s = "0";
	if (s.length() > 255) return 0;
	s = strReplace(s,"\r","");
	s = strReplace(s,"\n","");
	std::string t = "";
	for (int i = 0; i < (int)s.length(); i++)
	{
		std::string u = s.substr(i,1);
		if (u=="0" || u=="1" || u=="2" || u=="3" || u=="4" || u=="5" || u=="6" || u == "7" || u=="8" || u=="9" || u=="." || u=="-") 
		{
			t += u;
		}
	}

    double r = boost::lexical_cast<double>(t);
	double d = Round(r,place);
	return d;
}


std::string RoundToString(double d, int place)
{
	std::ostringstream ss;
    ss << std::fixed << std::setprecision(place) << d ;
    return ss.str() ;
}



bool Contains(std::string data, std::string instring)
{
	std::size_t found = 0;
	found = data.find(instring);
	if (found != std::string::npos) return true;
	return false;
}



double GetCPIDUTXOWeight(double dAmount)
{
	if (dAmount <= 0) return 0;
	if (dAmount > 00000 && dAmount <= 01000) return 25;
	if (dAmount > 01000 && dAmount <= 05000) return 50;
	if (dAmount > 05000 && dAmount <= 10000) return 60;
	if (dAmount > 10000 && dAmount <= 50000) return 75;
	if (dAmount > 50000) return 100;
	return 0;
}



std::string GetWUElement(std::string sWUdata, std::string sRootElementName, double dTaskID, std::string sPrimaryKeyElement, std::string sOutputElement)
{
	// The boinc network forms the XML file hierarchy like this
	/*
	<results>
	<error>ID 976055122</error>
	<result>
	<id>975055123</id>
	<create_time>1519055586</create_time>
	<workunitid>879197165</workunitid>
	<server_state>5</server_state>
	<outcome>1</outcome>
	<client_state>5</client_state>
	<hostid>3247080</hostid>
	<userid>1942555</userid>
	<report_deadline>1519315255</report_deadline>
	<sent_time>1519056055</sent_time>
	<received_time>1519151181</received_time>
	<name>
	</result>
	</results>
	*/
	std::vector<std::string> vPODC = Split(sWUdata.c_str(), sRootElementName);
	for (int i = 0; i < (int)vPODC.size(); i++)
	{
		double dTask = cdbl(ExtractXML(vPODC[i], "<" + sPrimaryKeyElement + ">", "</" + sPrimaryKeyElement + ">"), 0);
		if (dTask == dTaskID)
		{
			std::string sEleValue = ExtractXML(vPODC[i], "<" + sOutputElement + ">", "</" + sOutputElement + ">");
			return sEleValue;
		}	
	}
	return "";
}

/*
bool CheckMessageSignature(std::string sMsg, std::string sSig)
{
     std::string db64 = DecodeBase64(sSig);
     CPubKey key(ParseHex(strTemplePubKey));
	 std::vector<unsigned char> vchMsg = vector<unsigned char>(sMsg.begin(), sMsg.end());
     std::vector<unsigned char> vchSig = vector<unsigned char>(db64.begin(), db64.end());
	 return (!key.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig)) ? false : true;
}


bool CheckMessageSignature(std::string sMsg, std::string sSig, std::string sPubKey)
{
     std::string db64 = DecodeBase64(sSig);
     CPubKey key(ParseHex(sPubKey));
	 std::vector<unsigned char> vchMsg = vector<unsigned char>(sMsg.begin(), sMsg.end());
     std::vector<unsigned char> vchSig = vector<unsigned char>(db64.begin(), db64.end());
	 return (!key.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig)) ? false : true;
}
*/


std::string GetListOfData(std::string sSourceData, std::string sDelimiter, std::string sSubDelimiter, int iSubPosition, int iMaxCount)
{
	std::vector<std::string> vPODC = Split(sSourceData.c_str(), sDelimiter);
	std::string sMyData = "";
	int iInserted = 0;
	for (int i = 0; i < (int)vPODC.size(); i++)
	{
		std::string sElement = GetElement(vPODC[i], sSubDelimiter, iSubPosition);
		if (!sElement.empty())
		{
			sMyData += sElement + sDelimiter;
			iInserted++;
		}
		if (iInserted >= iMaxCount) break;
	}
	sMyData = ChopLast(sMyData);
	return sMyData;
}



std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue)
{
	// JSON: "key":"value",
	std::string sQ = "\"";
	std::string sOut = sQ + sKey + sQ + ":";
	if (bQuoteValue)
	{
		sOut += sQ + sValue + sQ;
	}
	else
	{
		sOut += sValue;
	}
	if (bIncludeDelimiter) sOut += ",";
	return sOut;
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

int64_t StringToUnixTime(std::string sTime)
{
	//Converts a string timestamp DD MMM YYYY HH:MM:SS TZ, such as: "27 Feb 2018, 17:35:55 UTC" to an int64_t UTC UnixTimestamp
	std::vector<std::string> vT = Split(sTime.c_str(), " ");
	if (vT.size() < 5) 
	{
		return 0;
	}
	int iDay = cdbl(vT[0],0);
	int iMonth = MonthToInt(vT[1]);
	int iYear = cdbl(strReplace(vT[2], ",", ""),0);
	std::vector<std::string> vHours = Split(vT[3], ":");
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





