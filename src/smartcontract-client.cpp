// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-client.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpodc.h"
#include "rpcpog.h"
#include "init.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "governance.h"
#include "governance-validators.h"
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


UniValue GetCampaigns()
{
	UniValue results(UniValue::VOBJ);
	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	int i = 0;
	// List of Campaigns
	for (auto s : mCampaigns)
	{
		results.push_back(Pair("campaign " + s.first, s.first));
	}

	// List of Christian-Keypairs (Global members)
	std::map<std::string, CPK> cp = GetGSCMap("cpk", "", true);
	for (std::pair<std::string, CPK> a : cp)
	{
		CPK c1 = a.second;
		results.push_back(Pair("member " + a.first, c1.sAddress));
	}

	// List of CPKs per Campaign
	for (auto s : mCampaigns)
	{
		std::string sCampaign = s.first;
	
		std::map<std::string, CPK> cp1 = GetGSCMap("cpk-" + sCampaign, "", true);
		for (std::pair<std::string, CPK> a : cp1)
		{
			CPK c1 = a.second;
			results.push_back(Pair("campaign-" + sCampaign + "-member", c1.sAddress));
		}
	}
	
	return results;
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
	return false;
}

CWalletTx CreateGSCClientTransmission(std::string sCampaign, CBlockIndex* pindexLast, double nCoinAgePercentage, CAmount nFoundationDonation, CReserveKey& reservekey, std::string& sXML, std::string& sError)
{
	CWalletTx wtx;
	if (pwalletMain->IsLocked())
	{
		sError = "Sorry, wallet must be unlocked.";
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

	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	CBitcoinAddress baCPKAddress(sCPK);
	CScript spkCPKScript = GetScriptForDestination(baCPKAddress.Get());
	CAmount nFeeRequired;
	std::vector<CRecipient> vecSend;
	int nChangePosRet = -1;
	bool fSubtractFeeFromAmount = false;
	CRecipient recipient = {spkCPKScript, nPayment1, false, fSubtractFeeFromAmount};
	vecSend.push_back(recipient); // This transmission is to Me
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
	sXML += "<gscsig>" + sSignature + "</gscsig><abncpk>" + sCPK + "</abncpk><gsccampaign>" + sCampaign + "</gsccampaign><abnwgt>" + RoundToString(nTargetCoinAge, 0) + "</abnwgt>";
	std::string strError;
	CAmount nBuffer = 10 * COIN;
	bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false, 0, sXML, nTargetCoinAge, nTargetSpend + nBuffer);

	if (!fCreated)    
	{
		sError = "CreateGSCTransmission::Fail::" + strError;
		return wtx;
	}
	bool fChecked = CheckAntiBotNetSignature(wtx.tx, "gsc");
	if (!fChecked)
	{
		sError = "CreateGSCTransmission::Fail::Signing Failed.";
		return wtx;
	}

	return wtx;
}

double UserSetting(std::string sName, double dDefault)
{
	double dConfigSetting = cdbl(GetArg("-" + sName, "0"), 4);
	if (dConfigSetting == 0) dConfigSetting = dDefault;
	return dConfigSetting;
}
	

//////////////////////////////////////////////////////////////////////////// CAMPAIGN #1 - Created March 23rd, 2019 - PROOF-OF-GIVING /////////////////////////////////////////////////////////////////////////////
// Loop through each campaign, create the applicable client-side transaction 

bool CreateClientSideTransaction()
{
	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	// CRITICAL - Change this to 12 hours before we go to prod
	double nTransmissionFrequency = GetSporkDouble("gscclienttransmissionfrequency", (60 * 60 * 1));
	// List of Campaigns
	for (auto s : mCampaigns)
	{
		int64_t nLastGSC = (int64_t)ReadCacheDouble(s.first + "_lastclientgsc");
		int64_t nAge = GetAdjustedTime() - nLastGSC;
		if (nAge > nTransmissionFrequency)
		{
			WriteCacheDouble(s.first + "_lastclientgsc", GetAdjustedTime());
			// This particular campaign needs a transaction sent
			LogPrintf("\nSmartContract-Client::Creating Client side transaction for campaign %s ", s.first);
			std::string sError;
			std::string sXML;
		    CReserveKey reservekey(pwalletMain);
			double nCoinAgePercentage = UserSetting(s.first + "_coinagepercentage", .10);
			CAmount nFoundationDonation = UserSetting(s.first + "_foundationdonation", 7) * COIN;
			CWalletTx wtx = CreateGSCClientTransmission(s.first, chainActive.Tip(), nCoinAgePercentage, nFoundationDonation, reservekey, sXML, sError);
			LogPrintf("\nCreated client side transmission - %s (Error) %s with txid %s ", sXML, sError, wtx.tx->GetHash().GetHex());
			CValidationState state;

			if (sError.empty())
			{
			    if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), state,  NetMsgType::TX))
				{
					LogPrint("GSC", "\nUnable to Commit transaction %s", wtx.tx->GetHash().GetHex());
					return false;
				}
			}
			
		}
	}
	return true;
}
