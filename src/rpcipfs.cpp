// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "darksend.h"
#include "spork.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "masternode-sync.h"
#include "utilmoneystr.h"
#include "rpcpodc.h"
#include "init.h"

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

extern CWallet* pwalletMain;



void GetGovSuperblockHeights(int& nNextSuperblock, int& nLastSuperblock)
{
    int nBlockHeight = 0;
    {
        LOCK(cs_main);
        nBlockHeight = (int)chainActive.Height();
    }
    int nSuperblockStartBlock = Params().GetConsensus().nSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;
    if(nBlockHeight < nFirstSuperblock)
	{
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    } else {
        nLastSuperblock = nBlockHeight - nBlockHeight % nSuperblockCycle;
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }
}


std::string SignPrice(std::string sValue)
{
	 CMasternode mn;
     if(mnodeman.Get(activeMasternode.vin, mn)) 
	 {
         CBitcoinAddress mnAddress(mn.pubKeyCollateralAddress.GetID());
		 if (mnAddress.IsValid()) 
		 {
			std::string sNonceValue = RoundToString(GetAdjustedTime(), 0);
	        std::string sError = "";
			std::string sSignature = "";
			bool bSigned = SignStake(mnAddress.ToString(), sValue + sNonceValue, sError, sSignature);
			if (bSigned) 
			{
				std::string sDarkSendSigner = "<signer>" + mnAddress.ToString() + "</signer><sig>" + sSignature + "</sig><message>" + sValue + sNonceValue + "</message>";
				return sDarkSendSigner;
			}
		 }
	 }
	 return "";
}

void ClearFile(std::string sPath)
{
	std::ofstream fd(sPath.c_str());
	std::string sEmptyData = "";
	fd.write(sEmptyData.c_str(), sizeof(char)*sEmptyData.size());
	fd.close();
}

std::string ReadAllText(std::string sPath)
{
	boost::filesystem::path pathIn(sPath);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return "";
	std::string sLine = "";
	std::string sJson = "";
    while(std::getline(streamIn, sLine))
    {
		sJson += sLine;
	}
	streamIn.close();
	return sJson;
}


void TestRSA()
{
	/*
	std::string sPath = GetSANDirectory2() + "rsafile";
	std::string sPathEnc = GetSANDirectory2() + "rsafile_enc";
	std::string sPathDec = GetSANDirectory2() + "reafile_dec";
    // int cipher_len = 0;
	std::string sError = "";
	std::string sPubKeyPath = GetSANDirectory2() + "keypath_client_pub";
	std::string sPriKeyPath = GetSANDirectory2() + "keypath_client_pri";
	RSA_GENERATE_KEYPAIR(sPubKeyPath, sPriKeyPath);
	RSA_Encrypt_File(sPubKeyPath, sPath, sPathEnc, sError);
	RSA_Decrypt_File(sPriKeyPath, sPathEnc, sPathDec, sError);
	std::string sData = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	LogPrintf(" Errors phase 3 %s ", sError.c_str());
	std::string sEnc = RSA_Encrypt_String(sPubKeyPath, sData, sError);
	std::string sDec = RSA_Decrypt_String(sPriKeyPath, sEnc, sError);
	LogPrintf(" Status Decoded %s , error %s ", sDec.c_str(), sError.c_str());
	*/
}

std::string SysCommandStdErr(std::string sCommand, std::string sTempFileName, std::string& sStdOut)
{
	std::string sPath = GetSANDirectory2() + sTempFileName;
	ClearFile(sPath);
	sStdOut = SystemCommand2(sCommand.c_str());
	std::string sStdErr = ReadAllText(sPath);
	LogPrintf(" BOINC COMMAND %s , Result %s ", sCommand.c_str(), sStdErr.c_str());

	return sStdErr;
}

UniValue GetIPFSList(int iMaxAgeInDays, std::string& out_Files)
{
	UniValue ret(UniValue::VOBJ);
	std::string sType = "IPFS";
	int64_t nMinStamp = GetAdjustedTime() - (86400 * iMaxAgeInDays);
	std::string sFiles = ""; // TODO:  Make this a map of files for PODS; for now this is OK for a proof-of-concept
	double dCostPerByte = GetSporkDouble("ipfscostperbyte", .0002);
	// Only include the IPFS hashes that actually paid the PODS fees
    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length())==sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
		     	int64_t nTimestamp = mvApplicationCacheTimestamp[(*ii).first];
				if (nTimestamp > nMinStamp)
				{
					std::string sValue = mvApplicationCache[(*ii).first];
					double dPODSFeeCollected = cdbl(ReadCache("IpfsFee" + RoundToString(nTimestamp, 0), sPrimaryKey), 2);
					double dSize = cdbl(ReadCache("IpfsSize" + RoundToString(nTimestamp, 0), sPrimaryKey), 0);
					double dFee = dCostPerByte * dSize;
					if (dPODSFeeCollected >= dFee && dPODSFeeCollected > 0)
					{
						ret.push_back(Pair(sPrimaryKey + " (" + sValue + ")", dPODSFeeCollected));
						sFiles += sPrimaryKey + ";";
					}
				}
			}
		}
	}
	if (fDebugMaster) LogPrintf(" \n IPFS hashes %s ", sFiles.c_str());
	return ret;
}

std::string GetUndownloadedIPFSHash()
{
	double dPODSDuration = GetSporkDouble("podsfileleaseduration", 7);
	std::string sFiles = "";
	GetIPFSList((int)dPODSDuration, sFiles);
	std::vector<std::string> vHashes = Split(sFiles.c_str(),";");
	for (int i = 0; i < (int)vHashes.size(); i++)
	{
		std::string sHash = vHashes[i];
		if (!sHash.empty())
		{
			double dDownloaded = cdbl(ReadCache("ipfs_downloaded", sHash), 0);
			if (dDownloaded == 0) return sHash;
		}
	}
	return "";
}

std::string SubmitBusinessObjectToIPFS(std::string sJSON, std::string& sError)
{
	std::string sBOPath = GetSANDirectory2() + "bo";
	FILE *outBO = fopen(sBOPath.c_str(), "w");
    fputs(sJSON.c_str(), outBO);
	fclose(outBO);
	return SubmitToIPFS(sBOPath, sError);
}


int CheckSanctuaryIPFSHealth(std::string sAddress)
{
	std::string sIP = GetIPFromAddress(sAddress);
	std::string sHash = "QmU3cpfPbqzZQTDtMswsF5VP7hzduhvacqNTuEaEsV3wBX";
	//TODO: Make Spork
	std::string sSporkHash = GetSporkValue("ipfshealthhash");
	std::string sURL = "http://" + sIP + ":8080/ipfs/" + sHash;
	std::string sHealthFN = GetSANDirectory2() + "checkhealth.dat";
	int i = ipfs_download(sURL, sHealthFN, 5, 0, 0);
	LogPrintf(" Checking %s, Result %i ",sAddress.c_str(), i);
	return i;
}

std::map<std::string, std::string> GetSancIPFSQualityReport()
{
	std::map<std::string, std::string> a;
    std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();
    BOOST_FOREACH(CMasternode& mn, vMasternodes) 
	{
		std::string strOutpoint = mn.vin.prevout.ToStringShort();
		std::string sStatus = mn.GetStatus();
		if (sStatus == "ENABLED")
		{
			int iQuality = CheckSanctuaryIPFSHealth(mn.addr.ToString());
			std::string sNarr = iQuality == 1 ? "UP" : "DOWN";
			std::string sRow = mn.addr.ToString() + ": " + sNarr;
			a[mn.addr.ToString()] = sNarr;
			LogPrintf(" %s %s ",sRow.c_str(), sNarr.c_str());
		}
	}
	return a;
}


void ThreadIPFSDiscoverNodes()
{
    // Wait til client is warmed up before starting...
    MilliSleep(1000 * 60 * 60 * 2);
	int64_t nLastRun = 0;

    while (!ShutdownRequested())
	{
        MilliSleep(1000);
		int64_t nAge = GetAdjustedTime() - nLastRun;
		if (nAge > (60 * 60 * 24 * 1))
		{
			bool fMaster = AmIMasternode();
			nLastRun = GetAdjustedTime();
			if ((fMaster && fProd && MyPercentile(chainActive.Tip()->nHeight) <= 10) || (fMaster && MyPercentile(chainActive.Tip()->nHeight) < 99 && !fProd))
			{
				map<std::string, std::string> a = GetSancIPFSQualityReport();
				std::string sData;
				int i = 0;

				BOOST_FOREACH(const PAIRTYPE(std::string, std::string)& item, a)
				{
					// The pair contains the IPV4 address and the node state
					std::string ip = item.first;
					std::string sState = item.second;
					if (sState == "UP")
					{
						sData += ip + ";";
						i++;
					}
		
				}
				// Store the data under key "IPFSNODES"
				if (i > 0)
				{
					std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
					std::string sError;
					AddBlockchainMessages(sAddress, "IPFSNODES", RoundToString(GetAdjustedTime(), 0), sData, .10, 0, sError);
					if (!sError.empty())
					{
						LogPrintf(" Unable to store IPFS Node List: %s ",sData.c_str());
					}
				}
			}
		}
	}
    if (ShutdownRequested())
        return;
}



std::string IPFSAPICommand(std::string sCmd, std::string sArgs, std::string& sError)
{
	std::string sURL3 = "api/v0/" + sCmd + sArgs;
	std::string sResponse = BiblepayHttpPost(true, 0,"POST", "USER", "PS1", "http://127.0.0.1", sURL3, 5001, "", 1);
	LogPrintf("\n IPFS_APICommand %s,  Error %s",sResponse.c_str(), sError.c_str());
	return sResponse;
}
