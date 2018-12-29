// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCIPFS_H
#define RPCIPFS_H

#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"

void GetGovSuperblockHeights(int& nNextSuperblock, int& nLastSuperblock);
std::string SignPrice(std::string sValue);
void ClearFile(std::string sPath);
std::string ReadAllText(std::string sPath);
void TestRSA();
std::string SysCommandStdErr(std::string sCommand, std::string sTempFileName, std::string& sStdOut);
UniValue GetIPFSList(int iMaxAgeInDays, std::string& out_Files);
std::string GetUndownloadedIPFSHash();
int CheckSanctuaryIPFSHealth(std::string sAddress);
std::string SubmitBusinessObjectToIPFS(std::string sJSON, std::string& sError);
UniValue GetSancIPFSQualityReport();
std::string IPFSAPICommand(std::string sCmd, std::string sArgs, std::string& sError);


#endif
