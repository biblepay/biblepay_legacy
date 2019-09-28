// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-client.h"
#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpodc.h"
#include "rpcpog.h"
#include "init.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "governance.h"
#include "masternode-sync.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "messagesigner.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <math.h>       /* round, floor, ceil, trunc */
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#ifdef ENABLE_WALLET
extern CWallet* pwalletMain;
#endif // ENABLE_WALLET

//////////////////////////////////////////////////////////////////// BIBLEPAY - SMART CONTRACTS - CLIENT SIDE ///////////////////////////////////////////////////////////////////////////////////////////////

CPK GetCPKFromProject(std::string sProjName, std::string sCPKPtr)
{
	std::string sRec = GetCPKData(sProjName, sCPKPtr);
	CPK oCPK = GetCPK(sRec);
	return oCPK;
}

UniValue GetCampaigns()
{
	UniValue results(UniValue::VOBJ);
	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	int i = 0;
	// List of Campaigns
	results.push_back(Pair("List Of", "BiblePay Campaigns"));
	for (auto s : mCampaigns)
	{
		results.push_back(Pair("campaign " + s.first, s.first));
	} 

	results.push_back(Pair("List Of", "BiblePay CPKs"));
	// List of Christian-Keypairs (Global members)
	std::map<std::string, CPK> cp = GetGSCMap("cpk", "", true);
	for (std::pair<std::string, CPK> a : cp)
	{
		CPK oPrimary = GetCPKFromProject("cpk", a.second.sAddress);
		results.push_back(Pair("member [" + Caption(oPrimary.sNickName, 10) + "]", a.second.sAddress));
	}

	results.push_back(Pair("List Of", "Campaign Participants"));
	// List of participating CPKs per Campaign
	for (auto s : mCampaigns)
	{
		std::string sCampaign = s.first;
		std::map<std::string, CPK> cp1 = GetGSCMap("cpk-" + sCampaign, "", true);
		for (std::pair<std::string, CPK> a : cp1)
		{
			CPK oPrimary = GetCPKFromProject("cpk", a.second.sAddress);
			results.push_back(Pair("campaign-" + sCampaign + "-member [" + Caption(oPrimary.sNickName, 10) + "]", oPrimary.sAddress));
		}
	}
	
	return results;
}

CPK GetMyCPK(std::string sProjectName)
{
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	std::string sRec = GetCPKData(sProjectName, sCPK);
	CPK myCPK = GetCPK(sRec);
	return myCPK;
}

bool CheckCampaign(std::string sName)
{
	boost::to_upper(sName);
		
	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	for (auto s : mCampaigns)
	{
		std::string sCampaignName = s.first;
		boost::to_upper(sCampaignName);
		if (sCampaignName == sName)
			return true;
	}
	if (sName == "BMS" || sName == "BMSUSER")
		return true;
	return false;
}


UniValue SentGSCCReport(int nHeight, std::string sMyCPK)
{
	UniValue results(UniValue::VOBJ);
	
	if (!chainActive.Tip()) 
		return NullUniValue;

	if (nHeight == 0) 
		nHeight = chainActive.Tip()->nHeight - 1;

	if (nHeight > chainActive.Tip()->nHeight - 1)
		nHeight = chainActive.Tip()->nHeight - 1;

	int nMaxDepth = nHeight;
	int nMinDepth = nMaxDepth - (BLOCKS_PER_DAY * 7);
	if (nMinDepth < 1) 
		return NullUniValue;

	CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
	const Consensus::Params& consensusParams = Params().GetConsensus();

	double nTotalPoints = 0;
	if (sMyCPK.empty())
		return NullUniValue;

	while (pindex && pindex->nHeight < nMaxDepth)
	{
		if (pindex->nHeight < chainActive.Tip()->nHeight) 
			pindex = chainActive.Next(pindex);
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int n = 0; n < block.vtx.size(); n++)
			{
				std::string sCampaignName;
				std::string sDate = TimestampToHRDate(pindex->GetBlockTime());

				if (block.vtx[n]->IsGSCTransmission() && CheckAntiBotNetSignature(block.vtx[n], "gsc", ""))
				{
					std::string sCPK = GetTxCPK(block.vtx[n], sCampaignName);
					double nCoinAge = 0;
					CAmount nDonation = 0;
					GetTransactionPoints(pindex, block.vtx[n], nCoinAge, nDonation);
					std::string sDiary = ExtractXML(block.vtx[n]->GetTxMessage(), "<diary>", "</diary>");
					if (CheckCampaign(sCampaignName) && !sCPK.empty() && sMyCPK == sCPK)
					{
						double nPoints = CalculatePoints(sCampaignName, sDiary, nCoinAge, nDonation, sCPK);
						std::string sReport = "Points: " + RoundToString(nPoints, 0) + ", Campaign: "+ sCampaignName 
							+ ", CoinAge: "+ RoundToString(nCoinAge, 0) + ", Donation: "+ RoundToString((double)nDonation/COIN, 2) 
							+ ", Height: "+ RoundToString(pindex->nHeight, 0) + ", Date: " + sDate;
						nTotalPoints += nPoints;
						results.push_back(Pair(block.vtx[n]->GetHash().GetHex(), sReport));
					}
				}
				else if (block.vtx[n]->IsABN() && CheckAntiBotNetSignature(block.vtx[n], "abn", ""))
				{
					std::string sCPK = GetTxCPK(block.vtx[n], sCampaignName);
					double nWeight = GetAntiBotNetWeight(pindex->GetBlockTime(), block.vtx[n], true, "");
					if (!sCPK.empty() && sMyCPK == sCPK)
					{
						std::string sReport = "ABN Weight: " + RoundToString(nWeight, 2) + ", Height: "+ RoundToString(pindex->nHeight, 0) + ", Date: " + sDate;
						results.push_back(Pair(block.vtx[n]->GetHash().GetHex(), sReport));
					}
				}
			}
		}
	}
	results.push_back(Pair("Total", nTotalPoints));
	return results;
}

CWalletTx CreateGSCClientTransmission(std::string sCampaign, std::string sDiary, CBlockIndex* pindexLast, double nCoinAgePercentage, CAmount nFoundationDonation, CReserveKey& reservekey, std::string& sXML, std::string& sError)
{
	CWalletTx wtx;
	if (pwalletMain->IsLocked() && msEncryptedString.empty())
	{
		sError = "Sorry, wallet must be unlocked.";
		return wtx;
	}

	if (sCampaign == "HEALING" && sDiary.empty())
	{
		sError = "Sorry, Diary entry must be populated to create a Healing transmission.";
		return wtx;
	}

	CAmount nReqCoins = 0;
	double nCoinAge = pwalletMain->GetAntiBotNetWalletWeight(0, nReqCoins);
	CAmount nPayment1 = nReqCoins * nCoinAgePercentage;
	CAmount nTargetSpend = nPayment1 + nFoundationDonation;
	double nTargetCoinAge = nCoinAge * nCoinAgePercentage;
	
	CAmount nBalance = pwalletMain->GetBalance();
	LogPrintf("\nCreateGSCClientTransmission - Total Bal %f, Needed %f, Coin-Age %f, Target-Spend-Pct %f, FoundationDonationAmount %f ",
		(double)nBalance/COIN, (double)nTargetSpend/COIN, nCoinAge, nCoinAgePercentage, (double)nFoundationDonation/COIN);

	if (nTargetSpend > nBalance)
	{
		sError = "Sorry, your balance is lower than the required GSC Transmission amount.";
		return wtx;
	}
	if (nTargetSpend < (1*COIN))
	{
		sError = "Sorry, no coins available for a GSC Transmission.";
		return wtx;
	}
	const Consensus::Params& consensusParams = Params().GetConsensus();
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
			return wtx;
		}
	}

	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	CBitcoinAddress baCPKAddress(sCPK);
	CScript spkCPKScript = GetScriptForDestination(baCPKAddress.Get());
	CAmount nFeeRequired;
	std::vector<CRecipient> vecSend;
	int nChangePosRet = -1;
	// R ANDREWS - Split change into 10 Bankroll Denominations - this makes smaller amounts available for ABNs
	double nMinRequiredABNWeight = GetSporkDouble("requiredabnweight", 0);
	bool fSubtractFeeFromAmount = true;
	double dChangeQty = cdbl(GetArg("-changequantity", "10"), 2);
	if (dChangeQty < 01) dChangeQty = 1;
	if (dChangeQty > 50) dChangeQty = 50;
		
	double iQty = (nPayment1/COIN) > nMinRequiredABNWeight ? dChangeQty : 1;
	double nEach = (double)1 / iQty;
	for (int i = 0; i < iQty; i++)
	{
		CAmount nIndividualAmount = nPayment1 * nEach;
		// LogPrintf("SCC::CreateGSCC nDist %f loc %f amt %f", (double)nPayment1/COIN, i, (double)nIndividualAmount/COIN);
		CRecipient recipient = {spkCPKScript, nIndividualAmount, false, fSubtractFeeFromAmount};
		vecSend.push_back(recipient); // This transmission is to Me
	}

	CBitcoinAddress baFoundation(consensusParams.FoundationAddress);
    CScript spkFoundation = GetScriptForDestination(baFoundation.Get());
	
	if (nFoundationDonation > 0)
	{
		CRecipient recipientFoundation = {spkFoundation, nFoundationDonation, false, fSubtractFeeFromAmount};
		vecSend.push_back(recipientFoundation);
		LogPrintf(" Donating %f to the foundation. ", (double)nFoundationDonation/COIN);
	}

	std::string sMessage = GetRandHash().GetHex();
	sXML += "<MT>GSCTransmission</MT><abnmsg>" + sMessage + "</abnmsg>";
	std::string sSignature;
	bool bSigned = SignStake(sCPK, sMessage, sError, sSignature);
	if (!bSigned) 
	{
		if (bTriedToUnlock)		{ pwalletMain->Lock();	}
		sError = "SmartContractClient::CreateGSCTransmission::Failed to sign.";
		return wtx;
	}
	sXML += "<gscsig>" + sSignature + "</gscsig><abncpk>" + sCPK + "</abncpk><gsccampaign>" + sCampaign + "</gsccampaign><abnwgt>" + RoundToString(nTargetCoinAge, 0) + "</abnwgt><diary>" + sDiary + "</diary>";
	std::string strError;
	bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false, 0, sXML, nTargetCoinAge, nTargetSpend);
	if (bTriedToUnlock)		{ pwalletMain->Lock();	}

	if (!fCreated)    
	{
		sError = "CreateGSCTransmission::Fail::" + strError;
		return wtx;
	}
	bool fChecked = CheckAntiBotNetSignature(wtx.tx, "gsc", "");
	if (!fChecked)
	{
		sError = "CreateGSCTransmission::Fail::Signing Failed.";
		return wtx;
	}

	return wtx;
}

double UserSetting(std::string sName, double dDefault)
{
	boost::to_lower(sName);
	// Step 1:  Check the biblepay.conf setting to see if overridden
	double dConfigSetting = cdbl(GetArg("-" + sName, "0"), 4);
	if (dConfigSetting == 0) 
		dConfigSetting = dDefault;
	return dConfigSetting;
}
	

//////////////////////////////////////////////////////////////////////////// CAMPAIGN #1 - Created March 23rd, 2019 - PROOF-OF-GIVING /////////////////////////////////////////////////////////////////////////////
// Loop through each campaign, create the applicable client-side transaction 

bool Enrolled(std::string sCampaignName, std::string& sError)
{
	// First, verify the user is in good standing
	CPK myCPK = GetMyCPK("cpk");
	if (myCPK.sAddress.empty()) 
	{
		sError = "User has no CPK.";
		return false;
	}
	// If we got this far, it was signed.
	if (sCampaignName == "cpk") return true;
	// Now check the project signature.
	CPK myProject = GetMyCPK("cpk-" + sCampaignName);
	if (myProject.sAddress.empty())
	{
		sError = "User is not enrolled.";
		return false;
	}
	return true;
}

bool CreateClientSideTransaction(bool fForce, bool fDiaryProjectsOnly, std::string sDiary, std::string& sError, CAmount nFoundationDonationOverride, std::string sSpecificCampaignName)
{
	// Lets only do this if its conceptually profitable
	// Note the force flag above:  If the call is from the Miner, force=false.  If the call is from the RPC command "sendgsc", the force=true.

	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	double nTransmissionFrequency = GetSporkDouble("gscclienttransmissionfrequency", (60 * 60 * 12));
	if (sDiary.length() < 10) 
		sDiary = "";
	if (fDiaryProjectsOnly && sDiary.empty())
	{
		sError = "Sorry, you have selected diary projects only, but biblepay did not receive a diary entry.";
		return false;
	}
	boost::to_upper(sSpecificCampaignName);

	double nDisableClientSideTransmission = UserSetting("disablegsctransmission", 0);
	if (nDisableClientSideTransmission == 1)
		return false;
				
	// List of Campaigns
	for (auto s : mCampaigns)
	{
		int64_t nLastGSC = (int64_t)ReadCacheDouble(s.first + "_lastclientgsc");
		int64_t nAge = GetAdjustedTime() - nLastGSC;
		double nDefaultCoinAgePercentage = GetSporkDouble(s.first + "defaultcoinagepercentage", .10);
		double nDefaultTithe = GetSporkDouble(s.first + "defaulttitheamount", 0);
		CAmount nFoundationDonation = UserSetting(s.first + "_foundationdonation", nDefaultTithe) * COIN;
		if (nFoundationDonationOverride > 0)
				nFoundationDonation = nFoundationDonationOverride;
		bool fProfitable = true;  
		if (s.first == "POG")
		{
			double nROI = GetROI((double)nFoundationDonation / COIN);
			if (nROI <= 0.01)
			{
				fProfitable = false;
				LogPrintf("Not participating in the %s campaign because BiblePay believes it may be unprofitable (%f percent) today. ", s.first, (double)nROI);
			}
			else
			{
				LogPrintf("\nTithing %f BBP into the Orphan Foundation for %s, because expected ROI is %f percent.", (double)nFoundationDonation/COIN,
					s.first, (double)nROI);
			}
		}

		if (fForce || (fProfitable && nAge > nTransmissionFrequency))
		{
			WriteCacheDouble(s.first + "_lastclientgsc", GetAdjustedTime());
			// This particular campaign needs a transaction sent (if the user is in good standing and enrolled in this project)
			std::string sError1;
			bool fPreCheckPassed = true;
			if (s.first == "HEALING" && sDiary.empty())
				fPreCheckPassed = false;
			if (fDiaryProjectsOnly && s.first != "HEALING") 
				fPreCheckPassed = false;
			if (!sSpecificCampaignName.empty() && s.first != sSpecificCampaignName)
				fPreCheckPassed = false;
			if (Enrolled(s.first, sError1) && fPreCheckPassed)
			{
				LogPrintf("\nSmartContract-Client::Creating Client side transaction for campaign %s ", s.first);
				std::string sXML;
				CReserveKey reservekey(pwalletMain);
				double nCoinAgePercentage = UserSetting(s.first + "_coinagepercentage", nDefaultCoinAgePercentage);
				std::string sError2;
				CWalletTx wtx = CreateGSCClientTransmission(s.first, sDiary, chainActive.Tip(), nCoinAgePercentage, nFoundationDonation, reservekey, sXML, sError2);
				LogPrintf("\nCreated client side transmission - %s [%s] with txid %s ", sXML, sError, wtx.tx->GetHash().GetHex());
				// Bubble any error to getmininginfo - or clear the error
				if (!sError2.empty())
					WriteCache("gsc", "errors", s.first + ": " + sError2, GetAdjustedTime());
				CValidationState state;

				if (sError2.empty())
				{
					if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), state,  NetMsgType::TX))
					{
						LogPrint("GSC", "\nUnable to Commit transaction %s", wtx.tx->GetHash().GetHex());
						WriteCache("gsc", "errors", "GSC Commit Client Transmission failed " + s.first, GetAdjustedTime());
						return false;
					}
				}
				else
				{
					sError += sError2;
					if (fDiaryProjectsOnly)
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}
