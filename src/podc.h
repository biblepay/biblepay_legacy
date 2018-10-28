// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * PROOF-OF-DISTRIBUTED-COMPUTING
 * ROBERT ANDREWS - BIBLEPAY
 * 3-21-2018
 */
#ifndef BITCOIN_PODC_H
#define BITCOIN_PODC_H

#include "uint256.h"


int GetPODCVersion();
std::string PackPODC(std::string sBlock, int iTaskLength, int iTimeLength);
std::string UnpackPODC(std::string sBlock, int iTaskLength, int iTimeLength);
std::string FilterBoincData(std::string sData, std::string sRootElement, std::string sEndElement, std::string sExtra);
std::string MutateToList(std::string sData);
double GetResearcherCredit(double dDRMode, double dAvgCredit, double dUTXOWeight, double dTaskWeight, double dUnbanked, double dTotalRAC, double dReqSPM, double dReqSPR, double dRACThreshhold, double dTeamPercent);
double GetUTXOLevel(double dUTXOWeight, double dTotalRAC, double dAvgCredit, double dRequiredSPM, double dRequiredSPR, double dRACThreshhold);
double SnapToGrid(double dPercent);
double EnforceLimits(double dValue);
int GetResearcherCount(std::string recipient, std::string RecipList);
void TouchDailyMagnitudeFile();
std::string GetSANDirectory2();

double GetMagnitudeInContract(std::string sContract, std::string sCPID);
int GetCPIDCount(std::string sContract, double& nTotalMagnitude);
std::string GetDCCFileContract();

double BoincDecayFunction(double dAgeInSecs);

std::string RoundToString(double d, int place);

std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);

double GetCPIDUTXOWeight(double dAmount);

std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);

double cdbl(std::string s, int place);

bool Contains(std::string data, std::string instring);
std::string GetWUElement(std::string sWUdata, std::string sRootElementName, double dTaskID, std::string sPrimaryKeyElement, std::string sOutputElement);
std::string GetListOfData(std::string sSourceData, std::string sDelimiter, std::string sSubDelimiter, int iSubPosition, int iMaxCount);
std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue);
int64_t StringToUnixTime(std::string sTime);
time_t SyntheticUTCTime(const struct tm *tm);


#endif // BITCOIN_PODC_H
