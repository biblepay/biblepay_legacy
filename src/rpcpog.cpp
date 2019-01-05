// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "darksend.h"
#include "spork.h"
#include "rpcserver.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcipfs.h"
#include "rpcpodc.h"
#include "init.h"

#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "masternode-sync.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "kjv.h"

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
#include <openssl/md5.h>

extern CWallet* pwalletMain;
extern CPoolObject GetPoolVector(const CBlockIndex* pindex, int iPaymentTier);


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
		pwalletMain->SetAddressBook(keyID, sName, "receive"); //receive == visible in address book, hidden = non-visible
		LogPrintf(" created new address %s ", CBitcoinAddress(keyID).ToString().c_str());
		return CBitcoinAddress(keyID).ToString();
	}
}


std::string DefaultRecAddress(std::string sType)
{
	std::string sDefaultRecAddress = "";
	BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        std::string strName = item.second.name;
        bool fMine = IsMine(*pwalletMain, address.Get());
        if (fMine)
		{
		    sDefaultRecAddress=CBitcoinAddress(address).ToString();
			boost::to_upper(strName);
			boost::to_upper(sType);
			if (strName == sType) 
			{
				sDefaultRecAddress=CBitcoinAddress(address).ToString();
				return sDefaultRecAddress;
			}
		}
    }

	// IPFS-PODS - R ANDREWS - One biblepay public key is associated with each type of signed business object
	if (!sType.empty())
	{
		std::string sError = "";
		sDefaultRecAddress = GenerateNewAddress(sError, sType);
		if (sError.empty()) return sDefaultRecAddress;
	}
	
	return sDefaultRecAddress;
}


std::string CreateBankrollDenominations(double nQuantity, CAmount denominationAmount, std::string& sError)
{
	// First mark the denominations with the 1milliBBP TitheMarker (this saves them from being spent in PODC Updates):
	denominationAmount += ((.001) * COIN);

	CAmount nTotal = denominationAmount * nQuantity;

	CAmount curBalance = pwalletMain->GetUnlockedBalance();
	if (curBalance < nTotal)
	{
		sError = "Insufficient funds (Unlock Wallet).";
		return "";
	}
	std::string sTitheAddress = DefaultRecAddress("TITHES");
	CBitcoinAddress cbAddress(sTitheAddress);
	CWalletTx wtx;
	
    CScript scriptPubKey = GetScriptForDestination(cbAddress.Get());
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
	for (int i = 0; i < nQuantity; i++)
	{
		bool fSubtractFeeFromAmount = false;
	    CRecipient recipient = {scriptPubKey, denominationAmount, false, fSubtractFeeFromAmount, false, false, false, "", "", "", ""};
		recipient.Message = "";
		vecSend.push_back(recipient);
	}
	
	bool fUseInstantSend = false;
    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, sError, NULL, true, ONLY_NOT1000IFMN, fUseInstantSend)) 
	{
		if (!sError.empty())
		{
			return "";
		}

        if (nTotal + nFeeRequired > pwalletMain->GetBalance())
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

CAmount GetDailyMinerEmissions(int nHeight)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
	int nBits = 486585255;
	if (nHeight < 1 || pindexBestHeader==NULL || pindexBestHeader->nHeight < 2) return 0;
	bool fIsPogSuperblock = CSuperblock::IsPOGSuperblock(nHeight);
	if (fIsPogSuperblock) nHeight = nHeight - 1;
    CAmount nReaperReward = GetBlockSubsidy(pindexBestHeader->pprev, nBits, nHeight, consensusParams, false);
	CAmount caMasternodePortion = GetMasternodePayment(nHeight, nReaperReward);
    CAmount nDailyRewards = (nReaperReward-caMasternodePortion) * BLOCKS_PER_DAY * 4; // This includes deflation
	return nDailyRewards;
}


TitheDifficultyParams GetTitheParams(const CBlockIndex* pindex)
{
	// Tithe Parameter Ranges:
	// min_coin_age  : 0 - 60 (days)
	// min_coin_amount : 1 - 25000
	// max_tithe_amount: 300 - 1 (descending)
	TitheDifficultyParams td;
	td.min_coin_age = 99999;
	td.min_coin_amount = 0;
	td.max_tithe_amount = 0;
	if (pindex == NULL || pindex->nHeight == 0) return td;
	CAmount nTitheCap = GetTitheCap(pindex);
	if (nTitheCap < 1) return td;
	double nQLevel = (((double)pindex->n24HourTithes/COIN) / ((double)nTitheCap/COIN));
	td.min_coin_age = R2X(Quantize(0, 60, nQLevel));
	td.min_coin_amount = R2X(Quantize(1, 25000, nQLevel)) * COIN;
	td.max_tithe_amount = R2X(Quantize(300, 1, nQLevel)) * COIN; // Descending tithe amount
	return td;
}

CAmount SelectCoinsForTithing(const CBlockIndex* pindex)
{
	TitheDifficultyParams tdp = GetTitheParams(pindex);
	std::map<double, CAmount> dtb = pwalletMain->GetDimensionalCoins(tdp.min_coin_age, tdp.min_coin_amount);
	CAmount nBigCoin = 0;
	BOOST_FOREACH(const PAIRTYPE(double, CAmount)& item, dtb)
   	{
		CAmount nCoinAmount = item.second;
		if (nCoinAmount > nBigCoin) nBigCoin = nCoinAmount;
	}
	return nBigCoin;
}


CAmount GetTitheTotal(CTransaction tx)
{
	CAmount nTotal = 0;
	const Consensus::Params& consensusParams = Params().GetConsensus();
    
	for (int i=0; i < (int)tx.vout.size(); i++)
	{
 		std::string sRecipient = PubKeyToAddress(tx.vout[i].scriptPubKey);
		if (sRecipient == consensusParams.FoundationAddress)
		{ 
			nTotal += tx.vout[i].nValue;
		}
	 }
	 return nTotal;
}

std::string QueryBibleHashVerses(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexPrev)
{
	return GetBibleHashVerses(hash, nBlockTime, nPrevBlockTime, nPrevHeight, pindexPrev);
}

std::string RetrieveMd5(std::string s1)
{
	try
	{
		const char* chIn = s1.c_str();
		unsigned char digest2[16];
		MD5((unsigned char*)chIn, strlen(chIn), (unsigned char*)&digest2);
		char mdString2[33];
		for(int i = 0; i < 16; i++) sprintf(&mdString2[i*2], "%02x", (unsigned int)digest2[i]);
 		std::string xmd5(mdString2);
		return xmd5;
	}
    catch (std::exception &e)
	{
		return "";
	}
}


std::string PubKeyToAddress(const CScript& scriptPubKey)
{
	CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);
    return address2.ToString();
}    


void GetTxTimeAndAmount(uint256 hashInput, int hashInputOrdinal, int64_t& out_nTime, CAmount& out_caAmount)
{
	CTransaction tx1;
	uint256 hashBlock1;
	if (GetTransaction(hashInput, tx1, Params().GetConsensus(), hashBlock1, true))
	{
		out_caAmount = tx1.vout[hashInputOrdinal].nValue;
		BlockMap::iterator mi = mapBlockIndex.find(hashBlock1);
		if (mi != mapBlockIndex.end())
		{
			CBlockIndex* pindexHistorical = mapBlockIndex[hashBlock1];              
			out_nTime = pindexHistorical->GetBlockTime();
			return;
		}
		else
		{
			LogPrintf("\nUnable to find hashBlock %s", hashBlock1.GetHex().c_str());
		}
	}
	else
	{
		LogPrintf("\nUnable to find hashblock1 in GetTransaction %s ",hashInput.GetHex().c_str());
	}

}


bool IsTitheLegal(CTransaction ctx, CBlockIndex* pindex, CAmount tithe_amount)
{
	// R ANDREWS - BIBLEPAY - 12/19/2018 
	// We quote the difficulty params to the user as of the best block hash, so when we check a tithe to be legal, we must check it as of the prior block's difficulty params
	if (pindex==NULL || pindex->pprev==NULL || pindex->nHeight < 2) return false;

	uint256 hashInput = ctx.vin[0].prevout.hash;
	int hashInputOrdinal = ctx.vin[0].prevout.n;
	int64_t nTxTime = 0;
	CAmount caAmount = 0;
	GetTxTimeAndAmount(hashInput, hashInputOrdinal, nTxTime, caAmount);
	
	double nTitheAge = (double)(pindex->GetBlockTime() - nTxTime) / 86400;
	if (false && !fProd && fPOGEnabled && fDebugMaster) 
		LogPrintf(" Prior Coin Amount %f, Tithe Amt %f, Tithe_height # %f, Spend_time %f, Age %f        ", 
		(double)caAmount/COIN, (double)tithe_amount/COIN, pindex->nHeight, nTxTime, (double)nTitheAge);
	if (nTitheAge >= pindex->pprev->nMinCoinAge && caAmount >= pindex->pprev->nMinCoinAmount && tithe_amount <= pindex->pprev->nMaxTitheAmount)
	{
		return true;
	}
	
	return false;
}

int64_t GetTitheAge(CTransaction ctx, CBlockIndex* pindex)
{
	// R ANDREWS - BIBLEPAY - 1/4/2018 
	if (pindex==NULL || pindex->pprev==NULL || pindex->nHeight < 2) return false;
	uint256 hashInput = ctx.vin[0].prevout.hash;
	int hashInputOrdinal = ctx.vin[0].prevout.n;
	int64_t nTxTime = 0;
	CAmount caAmount = 0;
	GetTxTimeAndAmount(hashInput, hashInputOrdinal, nTxTime, caAmount);
	double nTitheAge = (double)(pindex->GetBlockTime() - nTxTime) / 86400;
	return nTitheAge;
}


bool RPCSendMoney(std::string& sError, const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, 
	bool fUseInstantSend=false, bool fUsePrivateSend=false, bool fUseSanctuaryFunds=false, double nMinCoinAge = 0, CAmount nMinCoinAmount = 0)
{
    CAmount curBalance = pwalletMain->GetBalance();

    // Check amount
    if (nValue <= 0)
	{
        sError = "Invalid amount";
		return false;
	}

    if (nValue > curBalance)
	{
		sError = "Insufficient funds";
		return false;
	}
    // Parse Biblepay address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError = "";
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
	bool fForce = false;
    CRecipient recipient = {scriptPubKey, nValue, fForce, fSubtractFeeFromAmount, false, false, false, "", "", "", ""};
    vecSend.push_back(recipient);
	int nMinConfirms = 0;
    if (!pwalletMain->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, 
		fUsePrivateSend ? ONLY_DENOMINATED : (fUseSanctuaryFunds ? ALL_COINS : ONLY_NOT1000IFMN), fUseInstantSend, nMinConfirms, nMinCoinAge, nMinCoinAmount)) 
	{
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > pwalletMain->GetBalance())
		{
            sError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
			// The Snat - Reported this can crash POG on 12-30-2018 (resolved by handling this)
			return false;
		}
		sError = "Unable to Create Transaction: " + strError;
		return false;
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey, fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))
	{
        sError = "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.";
		return false;
	}
	return true;
}


void RPCSendMoneyToDestinationWithMinimumBalance(const CTxDestination& address, CAmount nValue, CAmount nMinimumBalanceRequired, double dMinCoinAge, CAmount caMinCoinValue, 
	CWalletTx& wtxNew, std::string& sError)
{
	if (pwalletMain->IsLocked())
	{
		sError = "Wallet Unlock Required";
		return;
	}
   
    if (pwalletMain->GetBalance() < nMinimumBalanceRequired || nValue > pwalletMain->GetBalance()) 
	{
		sError = "Insufficient funds";
		return;
	}
    RPCSendMoney(sError, address, nValue, false, wtxNew, false, false, false, dMinCoinAge, caMinCoinValue);
}



std::string SendTithe(CAmount caTitheAmount, double dMinCoinAge, CAmount caMinCoinAmount, CAmount caMaxTitheAmount, std::string& sError)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CBitcoinAddress address(sAddress);
    if (!address.IsValid())
	{
		sError = "Invalid Destination Address";
		return sError;
	}

	if (fPOGEnabled && caTitheAmount > caMaxTitheAmount)
	{
		sError = "Sorry, your tithe exceeds the maximum allowed tithe for this difficulty level.  (See getmininginfo).";
		return sError;
	}
			
    CWalletTx wtx;
	std::string sTitheAddress = DefaultRecAddress("TITHES");
	wtx.sTxMessageConveyed = "<TITHER>" + sTitheAddress + "</TITHER><NICKNAME>" + msNickName + "</NICKNAME><TITHESIGNER>" + sTitheAddress + "</TITHESIGNER>";
    
	// R ANDREWS - BIBLEPAY - HONOR UNLOCK PASSWORD IF WE HAVE ONE

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
				sError = "Unable to unlock wallet with POG password provided";
				return sError;
			}
		}
		else LogPrintf(" Encrypted string empty.");
	}

	
    RPCSendMoneyToDestinationWithMinimumBalance(address.Get(), caTitheAmount, caTitheAmount, dMinCoinAge, caMinCoinAmount, wtx, sError);
	
	if (bTriedToUnlock) pwalletMain->Lock();
	if (!sError.empty()) return "";
    return wtx.GetHash().GetHex().c_str();
}


CAmount GetTitheCap(const CBlockIndex* pindexLast)
{
	// NOTE: We must call GetTitheCap with nHeight because some calls from RPC look into the future - and there is no block index yet for the future - our GetBlockSubsidy figures the deflation harmlessly however
    const Consensus::Params& consensusParams = Params().GetConsensus();
	int nBits = 486585255;  // Set diff at about 1.42 for Superblocks (This preserves compatibility with our getgovernanceinfo cap)
	// The first call to GetBlockSubsidy calculates the future reward (and this has our standard deflation of 19% per year in it)
	if (pindexLast == NULL || pindexLast->nHeight < 2) return 0;
    CAmount nSuperblockPartOfSubsidy = GetBlockSubsidy(pindexLast, nBits, pindexLast->nHeight, consensusParams, true);
	// R ANDREWS - POG - 12/6/2018 - CRITICAL - If we go with POG + PODC, the Tithe Cap must be lower to ensure miner profitability

	// TestNet : POG+POBH with PODC enabled  = (100.5K miner payments, 50K daily pogpool tithe cap, deflating) = 0.003125 (4* the blocks per day in testnet)
	// Prod    : POG+POBH with PODC enabled  = (100.5K miner payments, 50K daily pogpool tithe cap, deflating) = 0.00075

    CAmount nPaymentsLimit = 0;
	double nTitheCapFactor = GetSporkDouble("tithecapfactor", 1);

	if (fProd)
	{
		nPaymentsLimit = nSuperblockPartOfSubsidy * consensusParams.nSuperblockCycle * .00075 * nTitheCapFactor; // Half of monthly charity budget - with deflation - per day
	}
	else
	{
		nPaymentsLimit = nSuperblockPartOfSubsidy * consensusParams.nSuperblockCycle * .003125 * nTitheCapFactor; // Half of monthly charity budget - with deflation - per day
	}
	return nPaymentsLimit;
}


double R2X(double var) 
{ 
    double value = (int)(var * 100 + .5); 
    return (double)value / 100; 
} 

double Quantize(double nFloor, double nCeiling, double nValue)
{
	double nSpan = nCeiling - nFloor;
	double nLevel = nSpan * nValue;
	double nOut = nFloor + nLevel;
	if (nOut > std::max(nFloor, nCeiling)) nOut = std::max(nFloor, nCeiling);
	if (nOut < std::min(nCeiling, nFloor)) nOut = std::min(nCeiling, nFloor);
	return nOut;
}

CAmount Get24HourTithes(const CBlockIndex* pindexLast)
{
	CAmount nTotal = 0;
    if (pindexLast == NULL || pindexLast->nHeight == 0)  return 0;
	int nLookback = 50;
	int nStepBack = 4;
	for (int i = 1; i <= nStepBack; i++)
	{
	    if (pindexLast->pprev == NULL) { break; }
	    pindexLast = pindexLast->pprev;
	}

    for (int i = 1; i <= nLookback; i++) 
	{
        if (pindexLast->pprev == NULL) { break; }
		nTotal += pindexLast->nBlockTithes;
        pindexLast = pindexLast->pprev;
    }
	return nTotal * 4;
}

double GetPOGDifficulty(const CBlockIndex* pindex)
{
	if (pindex == NULL || pindex->nHeight == 0)  return 0;
	CAmount nTitheCap = GetTitheCap(pindex);
	if (nTitheCap < 1) return 0;
	double nDiff = (((double)pindex->n24HourTithes/COIN) / ((double)nTitheCap/COIN)) * 65535;
	return nDiff;
}

int GetHeightByEpochTime(int64_t nEpoch)
{
	if (!chainActive.Tip()) return 0;
	int nLast = chainActive.Tip()->nHeight;
	if (nLast < 1) return 0;
	for (int nHeight = nLast; nHeight > 0; nHeight--)
	{
		CBlockIndex* pindex = FindBlockByHeight(nHeight);
		if (pindex)
		{
			int64_t nTime = pindex->GetBlockTime();
			if (nEpoch > nTime) return nHeight;
		}
	}
	return -1;
}

std::string GetActiveProposals()
{
	std::string strType = "proposals";
    int nStartTime = GetAdjustedTime() - (86400 * 32);
    LOCK2(cs_main, governance.cs);
    std::vector<CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	/*
	Bhavanis input format = <proposal>1,proposal name1,amount1,expensetype1,createtime1,yescount1,nocount1,abstaincount1,url1
	Proposal format:   "DataString": "[[\"proposal\",{\"end_epoch\":\"1525409746\",\"name\":\"MacOS compiles - iOS version support machine\",\"payment_address\":\
	"BE2XrQurnfzbqAGgMX9F8dXEyReML2Zr6H\",\"payment_amount\":\"270270\",\"start_epoch\":\"1525409746\",\"type\":1,\"url\":\"https://forum.biblepay.org/index.php?topic=171.0\"}]]",
	"Hash": "3a89aed047f627edee22853b8a7b3b3fbb5b6298d42ff78a495c4e38c0107fff",
    "CollateralHash": "6b5ca3adc9c8a4536721a79a2a40dd7d1ce682e06132bee52700bb9940d7553c",
    "ObjectType": 1,
	*/

	std::string sXML = "";
	int id = 0;
	std::string sDelim = "|";
	std::string sZero = "\0";
	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);


    BOOST_FOREACH(CGovernanceObject* pGovObj, objs)
    {
		if(strType == "proposals" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
        if(strType == "triggers" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
        if(strType == "watchdogs" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_WATCHDOG) continue;
		UniValue obj = pGovObj->GetJSONObject();
		std::string sHash = pGovObj->GetHash().GetHex();
			
		int64_t nEpoch = (int64_t)cdbl(obj["end_epoch"].get_str(), 0);
		int nEpochHeight = GetHeightByEpochTime(nEpoch);

		// First ensure the proposals gov height has not passed yet
		bool bIsPaid = nEpochHeight < nLastSuperblock;
		if (!bIsPaid)
		{
			int iYes = pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING);
			int iNo = pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING);
			int iAbstain = pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING);
			id++;
			// Attempt to retrieve the expense type from the JSON object
			std::string sCharityType = "";
			try
			{
				sCharityType = obj["expensetype"].get_str();
			}
			catch (const std::runtime_error& e) 
			{
				sCharityType = "NA";
			}
   			if (sCharityType.empty()) sCharityType = "N/A";
			std::string sEndEpoch = obj["end_epoch"].get_str();
			if (!sEndEpoch.empty())
			{
				std::string sProposalTime = TimestampToHRDate(cdbl(sEndEpoch,0));
				std::string sURL = obj["url"].get_str();
				if (id == 1) sURL += "&t=" + RoundToString(GetAdjustedTime(), 0);
				// proposal_hashes
				std::string sRow = "<proposal>" + sHash + sDelim 
					+ obj["name"].get_str() + sDelim 
					+ obj["payment_amount"].get_str() + sDelim
					+ sCharityType + sDelim
					+ sProposalTime + sDelim
					+ RoundToString(iYes,0) + sDelim
					+ RoundToString(iNo,0) + sDelim + RoundToString(iAbstain,0) 
					+ sDelim + sURL;
				sXML += sRow;
			}
		}
	}
	if (fDebugMaster && false) LogPrintf("Proposals %s \n", sXML.c_str());
	return sXML;
}


bool VoteManyForGobject(std::string govobj, std::string strVoteSignal, std::string strVoteOutcome, 
	int iVotingLimit, int& nSuccessful, int& nFailed, std::string& sError)
{
        uint256 hash(uint256S(govobj));
		vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
		if(eVoteSignal == VOTE_SIGNAL_NONE) 
		{
			sError = "Invalid vote signal (funding).";
			return false;
		}

        vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
        if(eVoteOutcome == VOTE_OUTCOME_NONE) 
		{
            sError = "Invalid vote outcome (yes/no/abstain)";
			return false;
	    }

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();
        UniValue resultsObj(UniValue::VOBJ);

        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) 
		{
            std::string strError;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            UniValue statusObj(UniValue::VOBJ);

            if(!darkSendSigner.GetKeysFromSecret(mne.getPrivKey(), keyMasternode, pubKeyMasternode))
			{
                nFailed++;
                sError += "Masternode signing error, could not set key correctly. (" + mne.getAlias() + ")";
                continue;
            }

            uint256 nTxHash;
            nTxHash.SetHex(mne.getTxHash());

            int nOutputIndex = 0;
            if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) 
			{
                continue;
            }

            CTxIn vin(COutPoint(nTxHash, nOutputIndex));

            CMasternode mn;
            bool fMnFound = mnodeman.Get(vin, mn);

            if(!fMnFound) 
			{
                nFailed++;
				sError += "Can't find masternode by collateral output (" + mne.getAlias() + ")";
                continue;
            }

            CGovernanceVote vote(mn.vin, hash, eVoteSignal, eVoteOutcome);
            if(!vote.Sign(keyMasternode, pubKeyMasternode))
			{
                nFailed++;
				sError += "Failure to sign. (" + mne.getAlias() + ")";
                continue;
            }

            CGovernanceException exception;
            if(governance.ProcessVoteAndRelay(vote, exception)) 
			{
                nSuccessful++;
				if (iVotingLimit > 0 && nSuccessful >= iVotingLimit) break;
            }
            else 
			{
                nFailed++;
				sError += "Error (" + exception.GetMessage() + ")";
            }

        }

     	return (nSuccessful > 0) ? true : false;
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

std::string CreateGovernanceCollateral(uint256 GovObjHash, CAmount caFee, std::string& sError)
{
	CWalletTx wtx;
	if(!pwalletMain->GetBudgetSystemCollateralTX(wtx, GovObjHash, caFee, false)) 
	{
		sError = "Error creating collateral transaction for governance object.  Please check your wallet balance and make sure your wallet is unlocked.";
		return "";
	}
	if (sError.empty())
	{
		// -- make our change address
		CReserveKey reservekey(pwalletMain);
		pwalletMain->CommitTransaction(wtx, reservekey, NetMsgType::TX);
		DBG( cout << "gobject: prepare "
					<< " strData = " << govobj.GetDataAsString()
					<< ", hash = " << govobj.GetHash().GetHex()
					<< ", txidFee = " << wtx.GetHash().GetHex()
					<< endl; );
		return wtx.GetHash().ToString();
	}
	return "";
}


int GetNextSuperblock()
{
	int nLastSuperblock, nNextSuperblock;
    // Get current block height
    int nBlockHeight = 0;
    {
        LOCK(cs_main);
        nBlockHeight = (int)chainActive.Height();
    }

    // Get chain parameters
    int nSuperblockStartBlock = Params().GetConsensus().nSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;

    // Get first superblock
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;

    if(nBlockHeight < nFirstSuperblock)
	{
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    }
	else 
	{
        nLastSuperblock = nBlockHeight - nBlockHeight % nSuperblockCycle;
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }
	return nNextSuperblock;
}

std::string StoreBusinessObjectWithPK(UniValue& oBusinessObject, std::string& sError)
{
	std::string sJson = oBusinessObject.write(0,0);
	std::string sPK = oBusinessObject["primarykey"].getValStr();
	std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
	std::string sSignKey = oBusinessObject["signingkey"].getValStr();
	if (sSignKey.empty()) sSignKey = sAddress;
	std::string sOT = oBusinessObject["objecttype"].getValStr();
	std::string sSecondaryKey = oBusinessObject["secondarykey"].getValStr();
	std::string sIPFSHash = SubmitBusinessObjectToIPFS(sJson, sError);
	if (sError.empty())
	{
		double dStorageFee = 1;
		std::string sTxId = "";
		sTxId = SendBusinessObject(sOT, sPK + sSecondaryKey, sIPFSHash, dStorageFee, sSignKey, true, sError);
		return sTxId;
	}
	return "";
}

bool LogLimiter(int iMax1000)
{
	 //The lower the level, the less logged
	 int iVerbosityLevel = rand() % 1000;
	 if (iVerbosityLevel < iMax1000) return true;
	 return false;
}

double GetSporkDouble(std::string sName, double nDefault)
{
	double dSetting = cdbl(GetSporkValue(sName), 2);
	if (dSetting == 0) return nDefault;
	return dSetting;
}


bool is_email_valid(const std::string& e)
{
	return (Contains(e, "@") && Contains(e,".") && e.length() > MINIMUM_EMAIL_LENGTH) ? true : false;
}

std::string StoreBusinessObject(UniValue& oBusinessObject, std::string& sError)
{
	std::string sJson = oBusinessObject.write(0,0);
	std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
	std::string sPrimaryKey = oBusinessObject["objecttype"].getValStr();
	std::string sSecondaryKey = oBusinessObject["secondarykey"].getValStr();
	std::string sIPFSHash = SubmitBusinessObjectToIPFS(sJson, sError);
	if (sError.empty())
	{
		double dStorageFee = 1;
		std::string sTxId = "";
		sTxId = SendBusinessObject(sPrimaryKey, sAddress + sSecondaryKey, sIPFSHash, dStorageFee, sAddress, true, sError);
		return sTxId;
	}
	return "";
}


UniValue GetBusinessObjectList(std::string sType)
{
	UniValue ret(UniValue::VOBJ);
	boost::to_upper(sType);
    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length()) == sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
				std::string sIPFSHash = mvApplicationCache[(*ii).first];
				std::string sError = "";
				UniValue o = GetBusinessObject(sType, sPrimaryKey, sError);
				if (o.size() > 0)
				{
					bool fDeleted = o["deleted"].getValStr() == "1";
					if (!fDeleted)
							ret.push_back(Pair(sPrimaryKey + " (" + sIPFSHash + ")", o));
				}
			}
		}
	}
	return ret;
}



double GetBusinessObjectTotal(std::string sType, std::string sFieldName, int iAggregationType)
{
	UniValue ret(UniValue::VOBJ);
	boost::to_upper(sType);
	double dTotal = 0;
	double dTotalRows = 0;
    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length())==sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
				std::string sIPFSHash = mvApplicationCache[(*ii).first];
				std::string sError = "";
				UniValue o = GetBusinessObject(sType, sPrimaryKey, sError);
				if (o.size() > 0)
				{
					bool fDeleted = o["deleted"].getValStr() == "1";
					if (!fDeleted)
					{
						std::string sBOValue = o[sFieldName].getValStr();
						double dBOValue = cdbl(sBOValue, 2);
						dTotal += dBOValue;
						dTotalRows++;
					}
				}
			}
		}
	}
	if (iAggregationType == 1) return dTotal;
	double dAvg = 0;
	if (dTotalRows > 0) dAvg = dTotal / dTotalRows;
	if (iAggregationType == 2) return dAvg;
	return 0;
}




UniValue GetBusinessObjectByFieldValue(std::string sType, std::string sFieldName, std::string sSearchValue)
{
	UniValue ret(UniValue::VOBJ);
	boost::to_upper(sType);
	boost::to_upper(sSearchValue);
    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length())==sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
				std::string sIPFSHash = mvApplicationCache[(*ii).first];
				std::string sError = "";
				UniValue o = GetBusinessObject(sType, sPrimaryKey, sError);
				if (o.size() > 0)
				{
					bool fDeleted = o["deleted"].getValStr() == "1";
					if (!fDeleted)
					{
						std::string sBOValue = o[sFieldName].getValStr();
						boost::to_upper(sBOValue);
						if (sBOValue == sSearchValue)
							return o;
					}
				}
			}
		}
	}
	return ret;
}




std::string GetBusinessObjectList(std::string sType, std::string sFields)
{
	boost::to_upper(sType);
	std::vector<std::string> vFields = Split(sFields.c_str(), ",");
	std::string sData = "";
    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length()) == sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
				std::string sIPFSHash = mvApplicationCache[(*ii).first];
				std::string sError = "";
				UniValue o = GetBusinessObject(sType, sPrimaryKey, sError);
				if (o.size() > 0)
				{
					bool fDeleted = o["deleted"].getValStr() == "1";
					if (!fDeleted)
					{
						// 1st column is ID - objecttype - recaddress - secondarykey
						std::string sPK = sType + "-" + sPrimaryKey + "-" + sIPFSHash;
						std::string sRow = sPK + "<col>";
						for (int i = 0; i < (int)vFields.size(); i++)
						{
							sRow += o[vFields[i]].getValStr() + "<col>";
						}
						sData += sRow + "<object>";
					}
				}
			}
		}
	}
	LogPrintf("BOList data %s \n",sData.c_str());
	return sData;
}

std::string GetDataFromIPFS(std::string sURL, std::string& sError)
{
	std::string sPath = GetSANDirectory2() + "guid";
	std::ofstream fd(sPath.c_str());
	std::string sEmptyData = "";
	fd.write(sEmptyData.c_str(), sizeof(char)*sEmptyData.size());
	fd.close();

	int i = ipfs_download(sURL, sPath, 15, 0, 0);
	if (i != 1) 
	{
		sError = "IPFS Download error.";
		return "";
	}
	boost::filesystem::path pathIn(sPath);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) 
	{
		sError = "FileSystem Error.";
		return "";
	}
	std::string sLine = "";
	std::string sJson = "";
    while(std::getline(streamIn, sLine))
    {
		sJson += sLine;
	}
	streamIn.close();
	return sJson;
}


std::string GetJSONFromIPFS(std::string sHash, std::string& sError)
{
	std::string sURL = "http://ipfs.biblepay.org:8080/ipfs/" + sHash;
	return GetDataFromIPFS(sURL, sError);
}

UniValue GetBusinessObject(std::string sType, std::string sPrimaryKey, std::string& sError)
{
	UniValue o(UniValue::VOBJ);
	std::string sIPFSHash = ReadCache(sType, sPrimaryKey);

	if (sIPFSHash.empty()) 
	{
		o.push_back(Pair("objecttype", "0"));
		sError = "Object not found";
		return o;
	}
	std::string sJson = GetJSONFromIPFS(sIPFSHash, sError);
	//LogPrintf(" json %s ",sJson.c_str());

	if (!sError.empty()) return o;
	try  
	{
	    UniValue o(UniValue::VOBJ);
	    o.read(sJson);
		LogPrintf("objecttype %s ",o["objecttype"].get_str().c_str());
		return o;
    }
    catch(std::exception& e) 
	{
        std::ostringstream ostr;
        ostr << "BusinessObject::LoadData Error parsing JSON" << ", e.what() = " << e.what();
        DBG( cout << ostr.str() << endl; );
        LogPrintf( ostr.str().c_str() );
        sError = e.what();
		return o;
    }
    catch(...) 
	{
        std::ostringstream ostr;
        ostr << "BusinessObject::LoadData Unknown Error parsing JSON";
        DBG( cout << ostr.str() << endl; );
        LogPrintf( ostr.str().c_str() );
		sError = "Unknown error while parsing JSON business object";
		return o;
    }
	return o;
}

int64_t GetFileSize(std::string sPath)
{
	if (!boost::filesystem::exists(sPath)) return 0;
	return (int64_t)boost::filesystem::file_size(sPath);
}


std::string AddBlockchainMessages(std::string sAddress, std::string sType, std::string sPrimaryKey, 
	std::string sHTML, CAmount nAmount, double minCoinAge, std::string& sError)
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
	CAmount curBalance = pwalletMain->GetUnlockedBalance();
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
	if (iStepLength < 1) 
	{
		sError = "Message length error.";
		return "";
	}
	bool bLastStep = false;
	// 3-8-2018 R ANDREWS - Ensure each UTXO charge is rounded up
	double dBasicParts = ((double)sHTML.length() / (double)iStepLength);
	int iParts = std::ceil(dBasicParts);
	if (iParts < 1) iParts = 1;
	int iPosition = 0;
	double dUTXOAmount = nAmount / COIN;
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
		double dLegAmount = dUTXOAmount / ((double)iParts);
		CAmount caLegAmount = dLegAmount * COIN;
	    CRecipient recipient = {scriptPubKey, caLegAmount, false, fSubtractFeeFromAmount, false, false, false, "", "", "", ""};
		s1 += sChunk;
		recipient.Message = s1;
		LogPrintf("\r\n AddBlockChainMessage::Creating TXID Amount %f, MsgLength %f, StepLen %f, BasicParts %f, Parts %f, vout %f, ResumePos %f, ChunkLen %f, with Chunk %s \r\n", 
			dLegAmount, (double)sHTML.length(), (double)iStepLength, (double)dBasicParts, (double)iParts, (double)iPosition, (double)i, (double)iChunkLength, s1.c_str());
		s1 = "";
		iPosition++;
		vecSend.push_back(recipient);
	}
	
    
	bool fUseInstantSend = false;
	// 3-12-2018; Never spend sanctuary funds - R ANDREWS - BIBLEPAY
	// 12-5-2018; ToDo: Ensure PODC Age > .75 days old (TheSnat)
	// PODC_Update: Addl params required to enforce coin_age: bool fUseInstantSend=false, int iMinConfirms = 0, double dMinCoinAge = 0, CAmount caMinCoinAmount = 0
	// 1-4-2019; Ensure we don't spend POG bankroll denominations
	CAmount nBankrollMask = (.001) * COIN;

    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet,
                                         sError, NULL, true, ONLY_NOT1000IFMN, fUseInstantSend, 0, minCoinAge, 0, nBankrollMask)) 
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




std::string ReadCache(std::string sSection, std::string sKey)
{
	boost::to_upper(sSection);
	boost::to_upper(sKey);
	
	if (sSection.empty() || sKey.empty()) return "";
	try
	{
		std::string sValue = mvApplicationCache[sSection + ";" + sKey];
		if (sValue.empty())
		{
			mvApplicationCache.insert(map<std::string,std::string>::value_type(sSection + ";" + sKey,""));
			mvApplicationCache[sSection + ";" + sKey]="";
			return "";
		}
		return sValue;
	}
	catch(...)
	{
		LogPrintf("ReadCache error %s",sSection.c_str());
		return "";
	}
}



std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nMaxAge)
{
	// This allows us to disregard old cache messages
	std::string sValue = ReadCache(sSection, sKey);
	std::string sFullKey = sSection + ";" + sKey;
	boost::to_upper(sFullKey);
	int64_t nTime = mvApplicationCacheTimestamp[sFullKey];
	if (nTime==0)
	{
		// LogPrintf(" ReadCacheWithMaxAge key %s timestamp 0 \n",sFullKey);
	}
	int64_t nAge = GetAdjustedTime() - nTime;
	if (nAge > nMaxAge) return "";
	return sValue;
}


void ClearCache(std::string sSection)
{
	boost::to_upper(sSection);
	for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii)
	{
		std::string sKey = (*ii).first;
		boost::to_upper(sKey);
		if (sKey.length() > sSection.length())
		{
			if (sKey.substr(0,sSection.length())==sSection)
			{
				mvApplicationCache[sKey]="";
				mvApplicationCacheTimestamp[sKey]=0;
			}
		}
	}
}


void WriteCache(std::string sSection, std::string sKey, std::string sValue, int64_t locktime, bool IgnoreCase)
{
	if (sSection.empty() || sKey.empty()) return;
	if (IgnoreCase)
	{
		boost::to_upper(sSection);
		boost::to_upper(sKey);
	}
	std::string temp_value = mvApplicationCache[sSection + ";" + sKey];
	if (temp_value.empty())
	{
		mvApplicationCache.insert(map<std::string,std::string>::value_type(sSection + ";" + sKey, sValue));
	    mvApplicationCache[sSection + ";" + sKey] = sValue;
	}
	mvApplicationCache[sSection + ";" + sKey] = sValue;
	// Record Cache Entry timestamp
	int64_t temp_locktime = mvApplicationCacheTimestamp[sSection + ";" + sKey];
	if (temp_locktime == 0)
	{
		mvApplicationCacheTimestamp.insert(map<std::string,int64_t>::value_type(sSection + ";" + sKey,1));
		mvApplicationCacheTimestamp[sSection + ";" + sKey]=locktime;
	}
	mvApplicationCacheTimestamp[sSection + ";" + sKey] = locktime;
}

void PurgeCacheAsOfExpiration(std::string sSection, int64_t nExpiration)
{
	boost::to_upper(sSection);
	for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii)
	{
		std::string sKey = (*ii).first;
		boost::to_upper(sKey);
		if (sKey.length() > sSection.length())
		{
			if (sKey.substr(0,sSection.length())==sSection)
			{
				int64_t nTimestamp = mvApplicationCacheTimestamp[sKey];
				if (nTimestamp < nExpiration)
				{
					mvApplicationCache[sKey]="";
					mvApplicationCacheTimestamp[sKey]=0;
				}
			}
		}
	}
}

std::string GetSporkValue(std::string sKey)
{
	boost::to_upper(sKey);
    const std::string key = "SPORK;" + sKey;
    const std::string& value = mvApplicationCache[key];
	return value;
}


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

std::string TimestampToHRDate(double dtm)
{
	if (dtm == 0) return "1-1-1970 00:00:00";
	if (dtm > 9888888888) return "1-1-2199 00:00:00";
	std::string sDt = DateTimeStrFormat("%m-%d-%Y %H:%M:%S",dtm);
	return sDt;
}

std::string GetArrayElement(std::string s, std::string delim, int iPos)
{
	std::vector<std::string> vGE = Split(s.c_str(),delim);
	if (iPos > (int)vGE.size()) return "";
	return vGE[iPos];
}

void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive)
{
	f7000 = ((!fProd && nPrevHeight > 1) || (fProd && nPrevHeight > F7000_CUTOVER_HEIGHT)) ? true : false;
    f8000 = ((fMasternodesEnabled) && ((!fProd && nPrevHeight >= F8000_CUTOVER_HEIGHT_TESTNET) || (fProd && nPrevHeight >= F8000_CUTOVER_HEIGHT)));
	f9000 = ((!fProd && nPrevHeight >= F9000_CUTOVER_HEIGHT_TESTNET) || (fProd && nPrevHeight >= F9000_CUTOVER_HEIGHT));
	int nLastTitheBlock = fProd ? LAST_TITHE_BLOCK : LAST_TITHE_BLOCK_TESTNET;
    fTitheBlocksActive = ((nPrevHeight+1) < nLastTitheBlock);
}

std::string RetrieveTxOutInfo(const CBlockIndex* pindexLast, int iLookback, int iTxOffset, int ivOutOffset, int iDataType)
{
	// When DataType == 1, returns txOut Address
	// When DataType == 2, returns TxId
	// When DataType == 3, returns Blockhash

    if (pindexLast == NULL || pindexLast->nHeight == 0) 
	{
        return "";
    }

    for (int i = 1; i < iLookback; i++) 
	{
        if (pindexLast->pprev == NULL) { break; }
        pindexLast = pindexLast->pprev;
    }
	if (iDataType < 1 || iDataType > 3) return "DATA_TYPE_OUT_OF_RANGE";

	if (iDataType == 3) return pindexLast ? pindexLast->GetBlockHash().GetHex() : "";	
	
	const Consensus::Params& consensusParams = Params().GetConsensus();
	CBlock block;
	if (ReadBlockFromDisk(block, pindexLast, consensusParams, "RetrieveTxOutInfo"))
	{
		if (iTxOffset >= (int)block.vtx.size()) iTxOffset=block.vtx.size()-1;
		if (ivOutOffset >= (int)block.vtx[iTxOffset].vout.size()) ivOutOffset=block.vtx[iTxOffset].vout.size()-1;
		if (iTxOffset >= 0 && ivOutOffset >= 0)
		{
			if (iDataType == 1)
			{
				std::string sPKAddr = PubKeyToAddress(block.vtx[iTxOffset].vout[ivOutOffset].scriptPubKey);
				return sPKAddr;
			}
			else if (iDataType == 2)
			{
				std::string sTxId = block.vtx[iTxOffset].GetHash().ToString();
				return sTxId;
			}
		}
	}
	
	return "";

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

bool IsTitheLegal2(CTitheObject oTithe, TitheDifficultyParams tdp)
{
	CBitcoinAddress cbaAddress(oTithe.Address);
	if (!cbaAddress.IsValid()) return -1;
	if (oTithe.Amount < (.50*COIN)) return -2;
	if (oTithe.Amount > tdp.max_tithe_amount) return -3;
	if (oTithe.Age < tdp.min_coin_age) return -4;
	if (oTithe.Amount < tdp.min_coin_amount) return -5;
	return 1;
}

std::string TitheErrorToString(int TitheError)
{
	if (TitheError == -1) return "ADDRESS_NOT_VALID";
	if (TitheError == -2) return "TITHE_TOO_LOW";
	if (TitheError == -3) return "TITHE_TOO_HIGH";
	if (TitheError == -4) return "TITHE_DOES_NOT_MEET_COIN_AGE_REQUIREMENTS";
	if (TitheError == -5) return "TITHE_DOES_NOT_MEET_COIN_VALUE_REQUIREMENTS";
	if (TitheError == 01) return "";
	return "N/A";
}

CPoolObject GetPoolVector(const CBlockIndex* pindexSource, int iPaymentTier)
{
	CPoolObject cPool;
	cPool.nPaymentTier = iPaymentTier;
	int iLookback = BLOCKS_PER_DAY;
	const CBlockIndex *pindex = pindexSource;
	if (pindex==NULL || pindex->nHeight == 0 || pindex->nHeight < iLookback) return cPool;

	cPool.nHeightFirst = pindex->nHeight - iLookback;
	cPool.nHeightLast = pindex->nHeight;
	
	cPool.mapTithes.clear();
	cPool.mapPoolPayments.clear();
	std::string sMyTitheAddress = DefaultRecAddress("TITHES");

	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::map<std::string, CTitheObject>::iterator itTithes;
	
	for (int ix = 0; ix < iLookback; ix++)
	{
		if (pindex == NULL) break;
		TitheDifficultyParams tdp = GetTitheParams(pindex);

		BOOST_FOREACH(const PAIRTYPE(std::string, CTitheObject)& item, pindex->mapTithes)
	    {
			CTitheObject oTithe = item.second;
			int iLegal = IsTitheLegal2(oTithe, tdp);
			if (iLegal == 1)
			{		
				CTitheObject cTithe = cPool.mapTithes[oTithe.Address];
				cTithe.Amount += oTithe.Amount;
				cTithe.Height = oTithe.Height;
				// { cTithe.PaymentTier = oTithe.Height % 16; }
				cTithe.PaymentTier = 0;  // Pog V1.1 - Everyone is in Tier 0
				cTithe.Address = oTithe.Address;
				cTithe.NickName = oTithe.NickName;
				cTithe.Trace += RoundToString(pindex->nHeight, 0) + ";";
				cPool.mapTithes[oTithe.Address] = cTithe;
			}
			else
			{
				if ((double)(oTithe.Amount/COIN) > 0.50) 
					LogPrintf(" \n Invalid-tithe found from address %s for amount of %f ", oTithe.Address.c_str(), (double)oTithe.Amount/COIN);
			}
		}
		pindex = pindex->pprev;
	}
	
	// Classify the tithes into Tiers
	BOOST_FOREACH(const PAIRTYPE(std::string, CTitheObject)& item, cPool.mapTithes)
    {
		CTitheObject oTithe = item.second;
		if (oTithe.Amount > cPool.nHighTithe) cPool.nHighTithe = oTithe.Amount;
		cPool.oTierRecipients[oTithe.PaymentTier]++;
		cPool.oTierTotals[oTithe.PaymentTier] += oTithe.Amount;
		cPool.oPaymentTotals[0] += oTithe.Amount;
		cPool.TotalTithes += oTithe.Amount;
		// Track User Tithes
		if (oTithe.Address == sMyTitheAddress) 
		{
			cPool.UserTithes += oTithe.Amount;
			cPool.nUserPaymentTier = oTithe.PaymentTier;
		}
 	}
	// Calculate tithe_weight
	BOOST_FOREACH(const PAIRTYPE(std::string, CTitheObject)& item, cPool.mapTithes)
    {
		CTitheObject oTithe = item.second;
		double dTierTotal = (double)(cPool.oTierTotals[oTithe.PaymentTier] / COIN);

		if (dTierTotal > 0.01)
		{
			oTithe.Weight = (double)((double)(oTithe.Amount / COIN) / dTierTotal);
			cPool.mapTithes[oTithe.Address] = oTithe;
		}
	}
	// Insert the tithes into the payment vector
	BOOST_FOREACH(const PAIRTYPE(std::string, CTitheObject)& item, cPool.mapTithes)
    {
		CTitheObject oTithe = item.second;
		if (oTithe.Weight > 0 && oTithe.PaymentTier == iPaymentTier)
		{
			cPool.mapPoolPayments.insert(std::make_pair(oTithe.Address, oTithe));
		}
	}
	
	return cPool;
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

std::string GetIPFromAddress(std::string sAddress)
{
	std::vector<std::string> vAddr = Split(sAddress.c_str(),":");
	if (vAddr.size() < 2) return "";
	return vAddr[0];
}

void SendChat(CChat chat)
{
	LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
		chat.RelayTo(pnode);
    }
	chat.ProcessChat();
}


bool SubmitProposalToNetwork(uint256 txidFee, int64_t nStartTime, std::string sHex, std::string& sError, std::string& out_sGovObj)
{
        if(!masternodeSync.IsBlockchainSynced()) 
		{
			sError = "Must wait for client to sync with masternode network. ";
			return false;
        }
        // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS
        uint256 hashParent = uint256();
        int nRevision = 1;
	    CGovernanceObject govobj(hashParent, nRevision, nStartTime, txidFee, sHex);
        DBG( cout << "gobject: submit "
             << " strData = " << govobj.GetDataAsString()
             << ", hash = " << govobj.GetHash().GetHex()
             << ", txidFee = " << txidFee.GetHex()
             << endl; );

        std::string strHash = govobj.GetHash().ToString();
        if(!govobj.IsValidLocally(sError, true)) 
		{
            sError += "Object submission rejected because object is not valid.";
			LogPrintf("\n OBJECT REJECTED:\n gobject submit 0 1 %f %s %s \n", (double)nStartTime, sHex.c_str(), txidFee.GetHex().c_str());

			return false;
        }

        // RELAY THIS OBJECT - Reject if rate check fails but don't update buffer
        if(!governance.MasternodeRateCheck(govobj)) 
		{
            sError = "Object creation rate limit exceeded";
			return false;
        }
        // This check should always pass, update buffer
        if(!governance.MasternodeRateCheck(govobj, UPDATE_TRUE)) 
		{
            sError = "Object submission rejected because of rate check failure (buffer updated)";
			return false;
        }
        governance.AddSeenGovernanceObject(govobj.GetHash(), SEEN_OBJECT_IS_VALID);
        govobj.Relay();
        bool fAddToSeen = true;
        governance.AddGovernanceObject(govobj, fAddToSeen);
		out_sGovObj = govobj.GetHash().ToString();
		return true;
}

std::vector<char> ReadBytesAll(char const* filename)
{
    std::ifstream ifs(filename, std::ios::binary|std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();
    std::vector<char>  result(pos);
    ifs.seekg(0, std::ios::beg);
    ifs.read(&result[0], pos);
    return result;
}

std::string SubmitToIPFS(std::string sPath, std::string& sError)
{
	if (!boost::filesystem::exists(sPath)) 
	{
		sError = "IPFS File not found.";
		return "";
	}
	std::string sFN = GetFileNameFromPath(sPath);
	std::vector<char> v = ReadBytesAll(sPath.c_str());
	std::vector<unsigned char> uData(v.begin(), v.end());
	std::string s64 = EncodeBase64(&uData[0], uData.size());
	std::string sData = BiblepayIPFSPost(sFN, s64);
	std::string sHash = ExtractXML(sData,"<HASH>","</HASH>");
	std::string sLink = ExtractXML(sData,"<LINK>","</LINK>");
	sError = ExtractXML(sData,"<ERROR>","</ERROR>");
	return sHash;
}	

std::string SendBusinessObject(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, std::string sSignKey, bool fSign, std::string& sError)
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
	boost::to_upper(sPrimaryKey); // Following same pattern
	boost::to_upper(sType);
	std::string sNonceValue = RoundToString(GetAdjustedTime(), 0);
	// Add immunity to replay attacks (Add message nonce)
 	std::string sMessageType      = "<MT>" + sType  + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey   + "</MK>";
	std::string sMessageValue     = "<MV>" + sValue + "</MV>";
	std::string sNonce            = "<NONCE>" + sNonceValue + "</NONCE>";
	std::string sBOSignKey        = "<BOSIGNER>" + sSignKey + "</BOSIGNER>";
	std::string sMessageSig = "";
	LogPrintf(" signing business object %s key %s ",sPrimaryKey.c_str(), sSignKey.c_str());

	if (fSign)
	{
		std::string sSignature = "";
		bool bSigned = SignStake(sSignKey, sValue + sNonceValue, sError, sSignature);
		if (bSigned) 
		{
			sMessageSig = "<BOSIG>" + sSignature + "</BOSIG>";
			WriteCache(sType, sSignKey, sValue, GetAdjustedTime());
		}
	}
	std::string s1 = sMessageType + sMessageKey + sMessageValue + sNonce + sBOSignKey + sMessageSig;
	wtx.sTxMessageConveyed = s1;
	LOCK2(cs_main, pwalletMain->cs_wallet);
	{
		RPCSendMoneyToDestinationWithMinimumBalance(address.Get(), nAmount, nMinimumBalance, 0, 0, wtx, sError);
	}
	if (!sError.empty()) return "";
    return wtx.GetHash().GetHex().c_str();
}



int GetSignalInt(std::string sLocalSignal)
{
	boost::to_upper(sLocalSignal);
	if (sLocalSignal=="NO") return 1;
	if (sLocalSignal=="YES") return 2;
	if (sLocalSignal=="ABSTAIN") return 3;
	return 0;
}

UserVote GetSumOfSignal(std::string sType, std::string sIPFSHash)
{
	// Get sum of business object type - ipfshash - signal by ipfshash/objecttype/voter
	boost::to_upper(sType);
	boost::to_upper(sIPFSHash);
	std::string sData = "";
	UserVote v = UserVote();

    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
		std::string sValue = mvApplicationCache[(*ii).first];
		if (Contains(sKey, sType + ";" + sIPFSHash))
		{
			std::string sLocalSignal = ExtractXML(sValue, "<signal>", "</signal>");
			double dWeight = cdbl(ExtractXML(sValue, "<voteweight>", "</voteweight>"), 2);
			int iSignal = GetSignalInt(sLocalSignal);
			if (iSignal == 1)
			{
				v.nTotalNoCount++;
				v.nTotalNoWeight += dWeight;
			}
			else if (iSignal == 2)
			{
				v.nTotalYesCount++;
				v.nTotalYesWeight += dWeight;
			}
			else if (iSignal == 3)
			{
				v.nTotalAbstainCount++;
				v.nTotalAbstainWeight += dWeight;
			}
		}
	}
	return v;
}

UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string sSearch, std::string& outEntry)
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
					if (!sSearch.empty())
					{
						std::string sPK1 = sPrimaryKey;
						std::string sPK2 = sValue;
						boost::to_upper(sPK1);
						boost::to_upper(sPK2);
						boost::to_upper(sSearch);
						if (Contains(sPK1, sSearch) || Contains(sPK2, sSearch))
						{
							ret.push_back(Pair(sPrimaryKey + " (" + sTimestamp + ")", sValue));
						}
					}
					else
					{
						ret.push_back(Pair(sPrimaryKey + " (" + sTimestamp + ")", sValue));
					}
					iPos++;
				}
			}
		}
	}
	iSpecificEntry++;
	if (iSpecificEntry >= iTotalRecords) iSpecificEntry=0;  // Reset the iterator.
	return ret;
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

void RecoverOrphanedChainNew(int iCondition)
{
	// R Andrews - Aug 12th, 2018 - The Heavy Handed Method
	// Trying this method since RecoverOrphanedChain segafaults, due to having a NULL mapblockindex pointer on blocks where the ~BLOCK_INVALID mask is set (when getchaintips shows a potential invalid chain with more work).
	StartShutdown(1); //this notifies the daemon
}

void RecoverOrphanedChain(int iCondition)
{
	// This process is designed to recover from normal forks.
	// Since we mark a block as Dirty when it occurs, the wallet cannot recover around a bad block unless the block is reconsidered.
	// (This can happen if a heat-mined block is considered bad but as since has gone over the 15 min threshhold, and may want to be reconsidered).
	// Condition 1 = Found Chain with More Work
	// Condition 2 = Blocks marked as invalid
	// Condition 3 = No ancestor
	// Attempt to mark these blocks as clean and reprocess 3 days worth of blocks as long as we have not attempted this within 750 seconds:
	if (iCondition == 2)
	{
		if (!TimerMain("InvalidBlock", 10)) return;
	}
	bool bAllowed = NonObnoxiousLog("RecoverOrphanedChain", "BlockChainTools", "1", 750);
	if (bAllowed)
	{
		LogPrintf("\n Attempting to Recover Block Chain under %f condition... %f \n", (double)GetAdjustedTime(), (double)iCondition);
		ReprocessBlocks(BLOCKS_PER_DAY * 3);
		LogPrintf("\n Finished. %f \n", (double)GetAdjustedTime());
	}
}


UniValue ContributionReport()
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int nMaxDepth = chainActive.Tip()->nHeight;
    CBlock block;
	int nMinDepth = 1;
	double dTotal = 0;
	double dChunk = 0;
    UniValue ret(UniValue::VOBJ);
	int iProcessedBlocks = 0;
	int nStart = 1;
	int nEnd = 1;
	for (int ii = nMinDepth; ii <= nMaxDepth; ii++)
	{
   			CBlockIndex* pblockindex = FindBlockByHeight(ii);
			if (ReadBlockFromDisk(block, pblockindex, consensusParams, "CONTRIBUTIONREPORT"))
			{
				iProcessedBlocks++;
				nEnd = ii;
				BOOST_FOREACH(CTransaction& tx, block.vtx)
				{
					 for (int i=0; i < (int)tx.vout.size(); i++)
					 {
				 		std::string sRecipient = PubKeyToAddress(tx.vout[i].scriptPubKey);
						double dAmount = tx.vout[i].nValue/COIN;
						bool bProcess = false;
						if (sRecipient == consensusParams.FoundationAddress)
						{ 
							bProcess = true;
						}
						else if (pblockindex->nHeight == 24600 && dAmount == 2894609)
						{
							bProcess=true; // This compassion payment was sent to Robs address first by mistake; add to the audit 
						}
						if (bProcess)
						{
								dTotal += dAmount;
								dChunk += dAmount;
						}
					 }
				 }
		  		 double nBudget = CSuperblock::GetPaymentsLimit(ii) / COIN;
				 if (iProcessedBlocks >= (BLOCKS_PER_DAY*7) || (ii == nMaxDepth-1) || (nBudget > 5000000))
				 {
					 iProcessedBlocks = 0;
					 std::string sNarr = "Block " + RoundToString(nStart, 0) + " - " + RoundToString(nEnd, 0);
					 ret.push_back(Pair(sNarr, dChunk));
					 dChunk = 0;
					 nStart = nEnd;
				 }

			}
	}
	
	ret.push_back(Pair("Grand Total", dTotal));
	return ret;
}

void SerializePrayersToFile(int nHeight)
{
	if (nHeight < 100) return;
	std::string sSuffix = fProd ? "_prod" : "_testnet";
	std::string sTarget = GetSANDirectory2() + "prayers2" + sSuffix;
	FILE *outFile = fopen(sTarget.c_str(), "w");
	LogPrintf("Serializing Prayers... %f ",GetAdjustedTime());
	for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	int64_t nTimestamp = mvApplicationCacheTimestamp[(*ii).first];
		std::string sValue = mvApplicationCache[(*ii).first];
		bool bSkip = false;
		if (sKey.length() > 7 && sKey.substr(0,7)=="MESSAGE" && (sValue == "" || sValue==" ")) bSkip = true;
		if (!bSkip)
		{
			std::string sRow = RoundToString(nTimestamp, 0) + "<colprayer>" + RoundToString(nHeight, 0) + "<colprayer>" + sKey + "<colprayer>" + sValue + "<rowprayer>\r\n";
			fputs(sRow.c_str(), outFile);
		}
	}
	LogPrintf("...Done Serializing Prayers... %f ",GetAdjustedTime());
    fclose(outFile);
}

int DeserializePrayersFromFile()
{
	std::string sSuffix = fProd ? "_prod" : "_testnet";
	std::string sSource = GetSANDirectory2() + "prayers2" + sSuffix;

	boost::filesystem::path pathIn(sSource);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return -1;
	int nHeight = 0;
	std::string line;
	int iRows = 0;
    while(std::getline(streamIn, line))
    {
		std::vector<std::string> vRows = Split(line.c_str(),"<rowprayer>");
		for (int i = 0; i < (int)vRows.size(); i++)
		{
			std::vector<std::string> vCols = Split(vRows[i].c_str(),"<colprayer>");
			if (vCols.size() > 3)
			{
				int64_t nTimestamp = cdbl(vCols[0], 0);
				int cHeight = cdbl(vCols[1], 0);
				if (cHeight > nHeight) nHeight = cHeight;
				std::string sKey = vCols[2];
				std::string sValue = vCols[3];
				std::vector<std::string> vKeys = Split(sKey.c_str(), ";");
				if (vKeys.size() > 1)
				{
					WriteCache(vKeys[0], vKeys[1], sValue, nTimestamp);
					iRows++;
				}
			}
		}
	}
    LogPrintf(" Processed %f prayer rows \n", iRows);
	streamIn.close();
	return nHeight;
}

CAmount GetTitheAmount(CTransaction ctx)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	for (unsigned int z = 0; z < ctx.vout.size(); z++)
	{
		std::string sRecip = PubKeyToAddress(ctx.vout[z].scriptPubKey);
		if (sRecip == consensusParams.FoundationAddress) 
		{
			return ctx.vout[z].nValue;  // First Tithe amount found in transaction counts
		}
	}
	return 0;
}


std::string GetTitherAddress(CTransaction ctx, std::string& sNickName)
{
	std::string sMsg = "";
	std::string sTither = "";
	const Consensus::Params& consensusParams = Params().GetConsensus();
	
	for (unsigned int z = 0; z < ctx.vout.size(); z++)
	{
		sMsg += ctx.vout[z].sTxOutMessage;
	}
	for (unsigned int z = 0; z < ctx.vout.size(); z++)
	{
		sTither = PubKeyToAddress(ctx.vout[z].scriptPubKey);
		if (sTither != consensusParams.FoundationAddress) break;
	}

	std::string sSignedTither = ExtractXML(sMsg, "<TITHER>", "</TITHER>");
	sNickName = ExtractXML(sMsg, "<NICKNAME>", "</NICKNAME>");
	if (!sSignedTither.empty())
	{
		CBitcoinAddress cbaAddress(sSignedTither);
		if (cbaAddress.IsValid()) return sSignedTither;
	}

	CBitcoinAddress cbaAddress(sTither);
	if (cbaAddress.IsValid()) return sTither;

	return "";
}

void UpdatePogPool(CBlockIndex* pindex, const CBlock& block)
{
	if (!fPOGEnabled) return;
	if (pindex == NULL || pindex->nHeight == 0) return;
	std::map<std::string, CTitheObject>::iterator itTithes;
	CAmount nTithes = 0;
	pindex->n24HourTithes = Get24HourTithes(pindex);
	pindex->nPOGDifficulty = GetPOGDifficulty(pindex); 
	// Set the block parameters based on current difficulty level
	TitheDifficultyParams tdp = GetTitheParams(pindex);
	pindex->nMinCoinAge = tdp.min_coin_age;
	pindex->nMinCoinAmount = tdp.min_coin_amount;
	pindex->nMaxTitheAmount = tdp.max_tithe_amount;
	pindex->mapTithes.clear();
	// Induct the Legal tithes - skipping the out-of-bounds tithes
	for (unsigned int nTx = 0; nTx < block.vtx.size(); nTx++)
	{
		if (!block.vtx[nTx].IsCoinBase())
		{
			std::string sNickName = "";
			std::string sTither = GetTitherAddress(block.vtx[nTx], sNickName);
			CAmount nAmount = GetTitheAmount(block.vtx[nTx]);
			if (!sTither.empty() && nAmount > (.49*COIN) && nAmount <= (300.00*COIN))
			{
				std::string sTXID = block.vtx[nTx].GetHash().GetHex();
				int64_t nTitheAge = GetTitheAge(block.vtx[nTx], pindex);
				nTithes += nAmount;
				CTitheObject cTithe = pindex->mapTithes[sTXID];
				cTithe.Amount = nAmount;
				cTithe.Height = pindex->nHeight;
				cTithe.Address = sTither;
				cTithe.NickName = sNickName;
				cTithe.Age = nTitheAge;
				cTithe.TXID = sTXID;
				cTithe.Ordinal = nTx;
				pindex->mapTithes[sTXID] = cTithe;
				if (false) LogPrintf("\n Induct height %f, NN %s, amt %f -> ", cTithe.Height, sTither.c_str(), (double)cTithe.Amount/COIN);
			}
		}
		pindex->nBlockTithes = nTithes;
	}
}

void InitializePogPool(const CBlockIndex* pindexSource, int nSize, const CBlock& block)
{
	const CBlockIndex *pindexLast = pindexSource;

    if (pindexLast == NULL || pindexLast->nHeight == 0)  return;
	int64_t nAge = GetAdjustedTime() - pindexLast->GetBlockTime();
	if (nAge > (60 * 60 * 36) && nSize < (BLOCKS_PER_DAY+1)) return;
	//LogPrintf(" InitializePogPool Size %f Height %f ",nSize, pindexSource->nHeight);

	if (nSize==1)
	{
		if (pindexSource) UpdatePogPool(mapBlockIndex[pindexSource->GetBlockHash()], block);
		return;
	}
	const Consensus::Params& consensusParams = Params().GetConsensus();

    for (int i = 1; i < nSize; i++) 
	{
        if (pindexLast->pprev == NULL) return;
        pindexLast = pindexLast->pprev;
    }
	// Must be updated in ascending order here
	for (int i = 1; i <= nSize; i++)
	{
		if (pindexLast)
		{
			CBlock cblock;
			if (ReadBlockFromDisk(cblock, pindexLast, consensusParams, "InitializePogPool"))
			{
				UpdatePogPool(mapBlockIndex[pindexLast->GetBlockHash()], cblock);
			}
			pindexLast = chainActive.Next(pindexLast);
		}
 	}
	// Special case for last block
	if (pindexSource) UpdatePogPool(mapBlockIndex[pindexSource->GetBlockHash()], block);
}

std::string VectToString(std::vector<unsigned char> v)
{
     std::string s(v.begin(), v.end());
     return s;
}

CAmount StringToAmount(std::string sValue)
{
	if (sValue.empty()) return 0;
    CAmount amount;
    if (!ParseFixedPoint(sValue, 8, &amount)) return 0;
    if (!MoneyRange(amount)) throw runtime_error("AMOUNT OUT OF MONEY RANGE");
    return amount;
}

bool CompareMask(CAmount nValue, CAmount nMask)
{
	if (nMask == 0) return false;
	std::string sAmt = "0000000000000000000000000" + AmountToString(nValue);
	std::string sMask= AmountToString(nMask);
	std::string s1 = sAmt.substr(sAmt.length() - sMask.length() + 1, sMask.length() - 1);
	std::string s2 = sMask.substr(1, sMask.length() - 1);
	return (s1 == s2);
}
