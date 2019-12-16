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

double GetNecessaryCoinAgePercentageForPODC()
{
	std::string sCPID = GetResearcherCPID(std::string());
	if (sCPID.empty())
	{
		LogPrintf("GetNecessaryCoinAgePercentage::Researcher Not Linked.%f\n", 801);
		return 0;
	}

	Researcher r = mvResearchers[sCPID];
	if (!r.found)
	{
		LogPrintf("GetNecessaryCoinAgePercentage::Researcher not participating with RAC in WCG.%f\n", 802);
		return 0;
	}
	if (r.rac < 1)
	{
		LogPrintf("Researchers RAC < 1.%f\n", 803);
		return 0;
	}
	CAmount nReqCoins = 0;
	double nTotalCoinAge = pwalletMain->GetAntiBotNetWalletWeight(0, nReqCoins);
	if (nTotalCoinAge < 1)
	{
		LogPrintf("Sorry, wallet coin age is below 1.%f\n", 804);
		return 0;
	}
	double nReqForRAC = GetRequiredCoinAgeForPODC(r.rac, r.teamid);
	if (nReqForRAC < 1)
	{
		LogPrintf("Sorry, accumulated coin-age is below 1.%f\n", 805);
		return 0;
	}
	if (nReqForRAC > nTotalCoinAge)
		nReqForRAC = nTotalCoinAge;

	double nPerc = (nReqForRAC / nTotalCoinAge) + .01;
	if (nPerc > .99) 
		nPerc = .99;  // Leave a little room for the tx fee
	return nPerc;
}
	

CWalletTx CreateGSCClientTransmission(std::string sCampaign, std::string sDiary, CBlockIndex* pindexLast, double nCoinAgePercentage, CAmount nFoundationDonation, 
	CReserveKey& reservekey, std::string& sXML, std::string& sError, std::string& sWarning)
{
	CWalletTx wtx;

	if (sCampaign == "HEALING" && sDiary.empty())
	{
		sError = "Sorry, Diary entry must be populated to create a Healing transmission.";
		return wtx;
	}

	CAmount nReqCoins = 0;
	double nCoinAge = pwalletMain->GetAntiBotNetWalletWeight(0, nReqCoins);
	if (sCampaign == "WCG")
	{
		nCoinAgePercentage = GetNecessaryCoinAgePercentageForPODC();
		LogPrintf("\nCreateGSCClientTransmission::Attempting to use %f in coinagepercentage.", nCoinAgePercentage);
		if (nCoinAgePercentage > .95)
		{
			sWarning = "WARNING!  PODC is using " + RoundToString(nCoinAgePercentage, 2) + "% of your coin age.  This means your RAC will be reduced, resulting in a lower PODC reward. ";
			// Side by side comparison - by Sun K. 
			std::string sCPID = GetResearcherCPID(std::string());
			if (!sCPID.empty())
			{
				Researcher r = mvResearchers[sCPID];
				if (r.found && r.rac > 1)
				{
					double nReqForNonBBP = GetRequiredCoinAgeForPODC(r.rac, r.teamid);
					double nReqForBBP = GetRequiredCoinAgeForPODC(r.rac, 35006);
					sWarning += " (Team BiblePay requires " + RoundToString(nReqForBBP, 0) + " in coin-age, while Non-BiblePay teams require " + RoundToString(nReqForNonBBP, 0) + ".) ";
				}
			}
		}
	}

	CAmount nPayment1 = (nReqCoins * nCoinAgePercentage) + (1*COIN);
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

	if (nTargetSpend < (.01 * COIN))
	{
		sError = "Sorry, GSC transmission amount is less than .01 BBP.";
		return wtx;
	}

	std::string sPubPurseKey = GetEPArg(true);
	if (sPubPurseKey.empty())
	{
		sError = "Sorry, you must set up an external purse to send GSC transmissions.  Please type 'exec createpurse help'.";
		return wtx;
	}

	const Consensus::Params& consensusParams = Params().GetConsensus();

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
		sError = "SmartContractClient::CreateGSCTransmission::Failed to sign.";
		return wtx;
	}

	sXML += "<gscsig>" + sSignature + "</gscsig><abncpk>" + sCPK + "</abncpk><gsccampaign>" + sCampaign + "</gsccampaign><abnwgt>" 
		+ RoundToString(nTargetCoinAge, 0) + "</abnwgt><diary>" + sDiary + "</diary>";
	std::string strError;
	
	bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, 
		ALL_COINS, false, 0, sXML, nTargetCoinAge, nTargetSpend, .01, sPubPurseKey);

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
		LogPrintf("CPK Missing %f\n", 789);
		sError = "User has no CPK.";
		return false;
	}
	// If we got this far, it was signed.
	if (sCampaignName == "cpk")
		return true;
	// Now check the project signature.
	std::string scn = "cpk-" + sCampaignName;
	boost::to_upper(scn);

	CPK myProject = GetMyCPK(scn);
	if (myProject.sAddress.empty())
	{
		sError = "User is not enrolled in " + scn + ".";
		return false;
	}
	return true;
}

bool CreateAllGSCTransmissions(std::string& sError)
{
	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	std::string sErrorEnrolled;
	// For each enrolled campaign:
	for (auto s : mCampaigns)
	{
		if (Enrolled(s.first, sErrorEnrolled))
		{
			std::string sError1 = std::string();
			std::string sWarning = std::string();
			CreateGSCTransmission(false, "", sError1, s.first, sWarning);
			if (!sError1.empty())
			{
				LogPrintf("\nCreateAllGSCTransmissions %f, Campaign %s, Error [%s].\n", GetAdjustedTime(), s.first, sError1);
			}
			if (!sWarning.empty())
				LogPrintf("\nWARNING [%s]", sWarning);
		}
	}
}

bool CreateGSCTransmission(bool fForce, std::string sDiary, std::string& sError, std::string sSpecificCampaignName, std::string& sWarning)
{
	boost::to_upper(sSpecificCampaignName);
	if (sSpecificCampaignName == "HEALING")
	{
		if (sDiary.length() < 10 || sDiary.empty())
		{
			sError = "Sorry, you have selected diary projects only, but biblepay did not receive a diary entry.";
			return false;
		}
	}

	double nDisableClientSideTransmission = UserSetting("disablegsctransmission", 0);
	if (nDisableClientSideTransmission == 1)
	{
		sError = "Client Side Transactions are disabled via disablegsctransmission.";
		return false;
	}
				
	double nDefaultCoinAgePercentage = GetSporkDouble(sSpecificCampaignName + "defaultcoinagepercentage", .10);
	std::string sError1;
	if (!Enrolled(sSpecificCampaignName, sError1))
	{
		sError = "Sorry, CPK is not enrolled in project. [" + sError1 + "].  Error 795. ";
		return false;
	}

	LogPrintf("\nSmartContract-Client::Creating Client side transaction for campaign %s ", sSpecificCampaignName);
	std::string sXML;
	CReserveKey reservekey(pwalletMain);
	double nCoinAgePercentage = UserSetting(sSpecificCampaignName + "_coinagepercentage", nDefaultCoinAgePercentage);
	std::string sError2;
	if (sSpecificCampaignName == "CAMEROON-ONE" || sSpecificCampaignName == "KAIROS")
	{
		nCoinAgePercentage = 0.0001;
	}
	CAmount nFoundationDonation = 0;
	CWalletTx wtx = CreateGSCClientTransmission(sSpecificCampaignName, sDiary, chainActive.Tip(), nCoinAgePercentage, nFoundationDonation, reservekey, sXML, sError, sWarning);
	LogPrintf("\nCreated client side transmission - %s [%s] with txid %s ", sXML, sError, wtx.tx->GetHash().GetHex());
	// Bubble any error to getmininginfo - or clear the error
	if (!sError.empty())
	{
		return false;
	}
	CValidationState state;
	if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), state,  NetMsgType::TX))
	{
			LogPrintf("\nCreateGSCTransmission::Unable to Commit transaction for campaign %s - %s", sSpecificCampaignName, wtx.tx->GetHash().GetHex());
			sError = "GSC Commit failed " + sSpecificCampaignName;
			return false;
	}
	return true;
}
