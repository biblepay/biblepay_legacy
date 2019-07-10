// Copyright (c) 2014-2019 The Dash-Core Developers, The BiblePay Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spork.h"
#include "utilmoneystr.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "masternode-sync.h"
#include "smartcontract-server.h"
#include "rpcpog.h"
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

std::string ToYesNo(bool bValue)
{
	std::string sYesNo = bValue ? "Yes" : "No";
	return sYesNo;
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

bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature)
{
	LOCK(cs_main);
	{	
		// Unlock wallet if SecureKey is available
		bool bTriedToUnlock = false;
		if (pwalletMain->IsLocked() && !msEncryptedString.empty())
		{
			bTriedToUnlock = true;
			if (!pwalletMain->Unlock(msEncryptedString, false))
			{
				static int nNotifiedOfUnlockIssue = 0;
				if (nNotifiedOfUnlockIssue == 0)
					LogPrintf("\nUnable to unlock wallet with SecureString.\n");
				nNotifiedOfUnlockIssue++;
				sError = "Unable to unlock wallet with autounlock password provided";
				return false;
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
		std::vector<unsigned char> vchSig;
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


std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, bool Sign, std::string sExtraPayload, std::string& sError)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CBitcoinAddress address(sAddress);
    if (!address.IsValid())
	{
		sError = "Invalid Destination Address";
		return sError;
	}
    CAmount nAmount = CAmountFromValue(dStorageFee);
	CAmount nMinimumBalance = CAmountFromValue(dStorageFee);
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
		if (bSigned) 
		{
			sMessageSig = "<SPORKSIG>" + sSignature + "</SPORKSIG>";
			sMessageSig += "<BOSIG>" + sSignature + "</BOSIG>";
			sMessageSig += "<BOSIGNER>" + consensusParams.FoundationAddress + "</BOSIGNER>";
		}
		if (!bSigned) LogPrintf("Unable to sign spork %s ", sError);
		LogPrintf(" Signing Nonce%f , With spork Sig %s on message %s  \n", (double)GetAdjustedTime(), 
			 sMessageSig.c_str(), sValue.c_str());
	}
	std::string s1 = sMessageType + sMessageKey + sMessageValue + sNonce + sMessageSig + sExtraPayload;
	LogPrintf("SendBlockchainMessage %s", s1);
	bool fSubtractFee = false;
	bool fInstantSend = false;
	bool fSent = RPCSendMoney(sError, address.Get(), nAmount, fSubtractFee, wtx, fInstantSend, s1);

	if (!sError.empty())
		return std::string();
    return wtx.GetHash().GetHex().c_str();
}

std::string GetGithubVersion()
{
	std::string sURL = "https://" + GetSporkValue("pool");
	std::string sRestfulURL = "SAN/LastMandatoryVersion.htm";
	std::string sResponse = BiblepayHTTPSPost(false, 0, "", "", "", sURL, sRestfulURL, 443, "", 25, 10000, 1);
	if (sResponse.length() > 11)
	{
		sResponse = sResponse.substr(sResponse.length() - 11, 11);
		sResponse = strReplace(sResponse, "\n", "");
		sResponse = strReplace(sResponse, "\r", "");
    }
	return sResponse;
}

double GetCryptoPrice(std::string sSymbol)
{
	int SSL_PORT = 443;
	int CONNECTION_TIMEOUT = 15;
	int TRANSMISSION_TIMEOUT = 15000;
	int TERM_TYPE = 1;
	std::string sC1 = BiblepayHTTPSPost(false, 0, "", "", "api", GetSporkValue("pool"), GetSporkValue("getcryptoprice" + sSymbol), SSL_PORT, "", CONNECTION_TIMEOUT, TRANSMISSION_TIMEOUT, TERM_TYPE);
	double dDebugLevel = cdbl(GetArg("-debuglevel", "0"), 0);
	if (dDebugLevel == 1)
		LogPrintf("CryptoPrice %s", sC1);
	std::string sPrice = ExtractXML(sC1, "<MIDPOINT>", "</MIDPOINT>");
	double dMid = cdbl(sPrice, 12);
	return dMid;
}

double GetPBase(double& out_BTC)
{
	// Get the BiblePay market price based on midpoint of bid-ask in Satoshi * BTC price in USD
	double dBBP = GetCryptoPrice("bbp");
	double dBTC = GetCryptoPrice("btc");
	out_BTC = dBTC;
	double dPriceUSD = dBTC * dBBP;
	return dPriceUSD;
}

bool VerifySigner(std::string sXML)
{
	std::string sSignature = ExtractXML(sXML, "<sig>", "</sig>");
	std::string sSigner = ExtractXML(sXML, "<signer>", "</signer>");
	std::string sMessage = ExtractXML(sXML, "<message>", "</message>");
	std::string sError;
	bool fValid = CheckStakeSignature(sSigner, sSignature, sMessage, sError);
	return fValid;
}

double GetQTPhase(bool fInFuture, double dPrice, int nEventHeight, double& out_PriorPrice, double& out_PriorPhase)
{
	// fInFuture is required to calculate tomorrow's qt phase.  (Otherwise, return the historical value).
	// If a -1 dPrice is passed in, the caller wants the prior days QT level and price.
    double nMaximumTighteningPercentage = GetSporkDouble("qtmaxpercentage", 0);
	double nQTStartHeight = GetSporkDouble("qtheight", 0);
	bool fEnabled = sporkManager.IsSporkActive(SPORK_20_QUANTITATIVE_TIGHTENING_ENABLED) && nQTStartHeight > 0 && nEventHeight > nQTStartHeight;
	if (nMaximumTighteningPercentage == 0 || !fEnabled)
		return 0;
	double nPriceThreshhold = GetSporkDouble("qtpricethreshhold", 0);
	if (dPrice == 0)
		return 0;
	// Step 3: Get yesterdays phase
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int iNextSuperblock = 0;
	int nTotalSamples = 5;
	// Check N Samples for the first valid price and phase
	for (int iDay = 0; iDay < nTotalSamples; iDay++)
	{
		int iLastSuperblock = GetLastGSCSuperblockHeight(nEventHeight, iNextSuperblock);
		if (nEventHeight < consensusParams.QTHeight || iLastSuperblock < consensusParams.QTHeight || iLastSuperblock > chainActive.Tip()->nHeight) 
			return 0;
		std::string sXML;
		CBlockIndex* pindex = FindBlockByHeight(iLastSuperblock);
		if (pindex == NULL) return 0;
		CBlock block;
		out_PriorPhase = 0;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int i = 0; i < block.vtx[0]->vout.size(); i++)
			{
				std::string sRecipient = PubKeyToAddress(block.vtx[0]->vout[i].scriptPubKey);
				if (sRecipient == consensusParams.FoundationQTAddress)
				{
					out_PriorPhase = ((double)block.vtx[0]->vout[i].nValue / COIN) * 100;
					if (out_PriorPhase > 100) out_PriorPhase = 0;
					if (out_PriorPhase < 000) out_PriorPhase = 0;
				}
				sXML += block.vtx[0]->vout[i].sTxOutMessage;
			}		
		}
		else
		{
			LogPrintf("GetQtPhase::ERROR Unable to read block from disk %f ", iLastSuperblock);
			return 0;
		}
		out_PriorPrice = cdbl(ExtractXML(sXML, "<price>", "</price>"), 12);
		if (out_PriorPhase > 0) 
			break;
		nEventHeight -= consensusParams.nDCCSuperblockCycle;
		if (nEventHeight < consensusParams.nSuperblockStartBlock) 
			break;
	}

	if (!fInFuture)
	{
		return out_PriorPhase;
	}

	// Otherwise, Calculate the new phase
	double dModifier = (dPrice >= nPriceThreshhold) ? -1 : 1;
	double dPhase = out_PriorPhase + dModifier;
	if (dPhase > nMaximumTighteningPercentage) dPhase = nMaximumTighteningPercentage; 
	if (dPhase <  0) dPhase = 0;
	if (dPhase > 90) dPhase = 90;
	return dPhase;
}


bool GetTransactionTimeAndAmount(uint256 txhash, int nVout, int64_t& nTime, CAmount& nAmount)
{
	uint256 hashBlock = uint256();
	CTransactionRef tx2;
	if (GetTransaction(txhash, tx2, Params().GetConsensus(), hashBlock, true))
	{
		   BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
           if (mi != mapBlockIndex.end() && (*mi).second) 
		   {
              CBlockIndex* pMNIndex = (*mi).second; 
			  nTime = pMNIndex->GetBlockTime();
		      nAmount = tx2->vout[nVout].nValue;
			  return true;
		   }
	}
	return false;
}

/*
int64_t GetDCCFileTimestamp()
{
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";
	boost::filesystem::path pathFiltered(sDailyMagnitudeFile);
	if (!boost::filesystem::exists(pathFiltered)) return GetAdjustedTime() - 0;
	int64_t nTime = last_write_time(pathFiltered);
	return nTime;
}
*/

bool IsMature(int64_t nTime, int64_t nMaturityAge)
{
	int64_t nNow = GetAdjustedTime();
	int64_t nRemainder = nNow % nMaturityAge;
	int64_t nCutoff = nNow - nRemainder;
	bool bMature = nTime <= nCutoff;
	return bMature;
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


CAmount GetMoneySupplyEstimate(int nHeight)
{
	int64_t nAvgReward = 13657;
	double nUnlockedSupplyPercentage = .40;
	CAmount nSupply = nAvgReward * nHeight * nUnlockedSupplyPercentage * COIN;
	return nSupply;
}


std::string rPad(std::string data, int minWidth)
{
	if ((int)data.length() >= minWidth) return data;
	int iPadding = minWidth - data.length();
	std::string sPadding = std::string(iPadding,' ');
	std::string sOut = data + sPadding;
	return sOut;
}

