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

#ifdef MAC_OSX
#include <boost/process/system.hpp>        // for ShellCommand
#include <boost/process/io.hpp>
namespace bp = boost::process;
#endif

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


/*
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
*/

/*
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
*/

/*
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


int64_t GetDCCFileTimestamp()
{
	std::string sDailyMagnitudeFile = GetSANDirectory2() + "magnitude";
	boost::filesystem::path pathFiltered(sDailyMagnitudeFile);
	if (!boost::filesystem::exists(pathFiltered)) return GetAdjustedTime() - 0;
	int64_t nTime = last_write_time(pathFiltered);
	return nTime;
}
*/
		

/*
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

// Support for Distributed Computing - Robert A. - Biblepay - 1/31/2018 
// This is the Distributed Computing Consensus Process
// Each morning, a sanctuary goes through this process:  Do I have an active superblock?  If not, do I have an active Daily DCC Proposal?

/*

std::string ExecuteDistributedComputingSanctuaryQuorumProcess()
{
	// If not a sanctuary, exit
	if (!PODCEnabled(chainActive.Tip()->nHeight)) return "";

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
		if (fDebug) LogPrintf("We have a pending superblock at height %f \n",(double)iNextSuperblock);
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
			if (fDebug) LogPrintf(" DCC hash %s ",uDCChash.GetHex());

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

*/


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

