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
		if (bSigned) sMessageSig = "<SPORKSIG>" + sSignature + "</SPORKSIG>";
		if (!bSigned) LogPrintf("Unable to sign spork %s ", sError);
		LogPrintf(" Signing Nonce%f , With spork Sig %s on message %s  \n", (double)GetAdjustedTime(), 
			 sMessageSig.c_str(), sValue.c_str());
	}
	std::string s1 = sMessageType + sMessageKey + sMessageValue + sNonce + sMessageSig;
	//	estinationWithMinimumBalance(address.Get(), nAmount, nMinimumBalance, 0, 0, "", 0, wtx, sError);
	bool fSubtractFee = false;
	bool fInstantSend = false;
	bool fSent = RPCSendMoney(sError, address.Get(), nAmount, fSubtractFee, wtx, fInstantSend, s1);

	if (!sError.empty()) return "";
    return wtx.GetHash().GetHex().c_str();
}

std::string GetGithubVersion()
{
	std::string sURL = "https://" + GetSporkValue("pool");
	std::string sRestfulURL = "SAN/LastMandatoryVersion.htm";
	std::string sResponse = BiblepayHTTPSPost(false, 0, "", "", "", sURL, sRestfulURL, 443, "", 25, 10000, 1);
	if (sResponse.length() > 6)
	{
		sResponse = sResponse.substr(sResponse.length()-11, 11);
		sResponse = strReplace(sResponse, "\n", "");
		sResponse = strReplace(sResponse, "\r", "");
    }
	return sResponse;
}


/*
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
*/

/*
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
*/

/*
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
			if (ReadBlockFromDisk(block, pindex, consensusParams)) 
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
*/

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

