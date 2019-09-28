// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpog.h"
#include "rpcpodc.h"
#include "smartcontract-client.h"
#include "smartcontract-server.h"
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
#include <boost/algorithm/string.hpp> // for trim(), and case insensitive compare
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
#include <cmath>
#ifdef ENABLE_WALLET
extern CWallet* pwalletMain;
#endif // ENABLE_WALLET

void GetTransactionPoints(CBlockIndex* pindex, CTransactionRef tx, double& nCoinAge, CAmount& nDonation)
{
	nCoinAge = GetVINCoinAge(pindex->GetBlockTime(), tx, false);
	bool fSigned = CheckAntiBotNetSignature(tx, "gsc", "");
	nDonation = GetTitheAmount(tx);
	if (!fSigned) 
	{
		nCoinAge = 0;
		nDonation = 0;
		LogPrintf("antibotnetsignature failed on tx %s with purported coin-age of %f \n", tx->GetHash().GetHex(), nCoinAge);
		return;
	}
	return;
}

std::string GetTxCPK(CTransactionRef tx, std::string& sCampaignName)
{
	std::string sMsg = GetTransactionMessage(tx);
	std::string sCPK = ExtractXML(sMsg, "<abncpk>", "</abncpk>");
	sCampaignName = ExtractXML(sMsg, "<gsccampaign>", "</gsccampaign>");
	return sCPK;
}

//////////////////////////////////////////////////////////////////////////////// Cameroon-One /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double GetBBPPrice()
{
	static int64_t nLastPriceCheck = 0;
	int64_t nElapsed = GetAdjustedTime() - nLastPriceCheck;
	static double nLastPrice = 0;
	if (nElapsed < (60 * 60))
	{
		return nLastPrice;
	}
	nLastPriceCheck = GetAdjustedTime();
	double dPriorPrice = 0;
	double dPriorPhase = 0;
	double out_BTC = 0;
	nLastPrice = GetPBase(out_BTC);
	return nLastPrice;
}

double GetProminenceCap(std::string sCampaignName, double nPoints, double nProminence)
{
	if (sCampaignName != "CAMEROON-ONE")
		return nProminence;
	double nCameroonOneMonthlyRate = GetSporkDouble("cameroononemonthlyrate", 40);
	double nDailyCharges = nCameroonOneMonthlyRate / 30;
	if (nDailyCharges <= .01)
		return 0;
	double nUSDSpent = nPoints / 1000;  // Amount user spent in one day on children
	double nChildrenSponsored = nUSDSpent / nDailyCharges;
	// Cap @ BBP Rate * Child Count
	double nPrice = GetBBPPrice();
	if (nPrice <= 0)
		nPrice = .0004; // Guess
	int nNextSuperblock = 0;
	int nLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, nNextSuperblock);
	CAmount nBudget = CSuperblock::GetPaymentsLimit(nNextSuperblock);
	if (nBudget < 1)
		return 0;
	double nPaymentsLimit = (nBudget / COIN);
	double nUserReward = nPaymentsLimit * nProminence;
	double nRewardUSD = nUserReward * nPrice;
	if (nRewardUSD > nUSDSpent)
	{
		// Cap is in effect, so reverse engineer the payment to the actual market value
		double nProjectedBBP = nUSDSpent / nPrice;
		double nProjectedProminence = nProjectedBBP / nPaymentsLimit;
		if (fDebugSpam)
			LogPrintf(" GetProminenceCap Exceeded - new prominence %f ", nProjectedProminence);
		nProminence = nProjectedProminence;
	}
	if (fDebugSpam)
		LogPrintf("\n GetProminenceCap Points %f, Prominence %f, USD Price %f, UserReward %f  ", nPoints, nProminence, nPrice, nRewardUSD);
	return nProminence;
}

std::string GetCameroonOneChildData()
{
	static int64_t nLastQuery = 0;
	static std::string sCache;
	int64_t nElapsed = GetAdjustedTime() - nLastQuery;
	if (nElapsed < (60 * 1))
	{
		return sCache;
	}
	std::string sURL = GetSporkValue("cameroonone");
	std::string sRestfulURL = GetSporkValue("childapi");
	sCache = BiblepayHTTPSPost(false, 0, "", "", "", sURL, sRestfulURL, 443, "", 25, 10000, 1);
	nLastQuery = GetAdjustedTime();
	return sCache;
}

double GetCameroonChildBalance(std::string sChildID)
{
	std::string sData = GetCameroonOneChildData();
	std::vector<std::string> vRows = Split(sData.c_str(), "\n");
	// Child ID, Added, DR/CR, Notes
	double dTotal = -999;  // -999 means child not found.
	for (int i = 1; i < vRows.size(); i++)
	{
		vRows[i] = strReplace(vRows[i], "\r", "");
		vRows[i] = strReplace(vRows[i], "\n", "");
		vRows[i] = strReplace(vRows[i], "\"", "");
		std::vector<std::string> vCols = Split(vRows[i].c_str(), ",");
		if (vCols.size() >= 4)
		{
			std::string sID = vCols[0];
			if (sID == sChildID)
			{
				std::string sAdded = vCols[1];
				double dr = cdbl(vCols[2], 2);
				std::string sNotes = vCols[3];
				if (dTotal == -999) 
					dTotal = 0;
				dTotal += dr;
				if (fDebugSpam)
					LogPrintf("childid %s, added %s, notes %s DrCr %f total %f \n", sID, sAdded, sNotes, dr, dTotal);
			}
		}
	}
	return dTotal;
}

//////////////////////////////////////////////////////////////////////////////// Watchman-On-The-Wall /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//											BiblePay's version of The Sentinel, written by the BiblePay Devs (March 31st, 2019)                                                                                      //
//                                                                                                                                                                                                                   //

BiblePayProposal GetProposalByHash(uint256 govObj, int nLastSuperblock)
{
	int nSancCount = mnodeman.CountEnabled();
	int nMinPassing = nSancCount * .10;
	if (nMinPassing < 1) nMinPassing = 1;
	CGovernanceObject* myGov = governance.FindGovernanceObject(govObj);
	UniValue obj = myGov->GetJSONObject();
	BiblePayProposal bbpProposal;
	bbpProposal.sName = obj["name"].getValStr();
	bbpProposal.nStartEpoch = cdbl(obj["start_epoch"].getValStr(), 0);
	bbpProposal.nEndEpoch = cdbl(obj["end_epoch"].getValStr(), 0);
	bbpProposal.sURL = obj["url"].getValStr();
	bbpProposal.sExpenseType = obj["expensetype"].getValStr();
	bbpProposal.nAmount = cdbl(obj["payment_amount"].getValStr(), 2);
	bbpProposal.sAddress = obj["payment_address"].getValStr();
	bbpProposal.uHash = myGov->GetHash();
	bbpProposal.nHeight = GetHeightByEpochTime(bbpProposal.nStartEpoch);
	bbpProposal.nMinPassing = nMinPassing;
	bbpProposal.nYesVotes = myGov->GetYesCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nNoVotes = myGov->GetNoCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nAbstainVotes = myGov->GetAbstainCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nNetYesVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nLastSuperblock = nLastSuperblock;
	bbpProposal.sProposalHRTime = TimestampToHRDate(bbpProposal.nStartEpoch);
	bbpProposal.fPassing = bbpProposal.nNetYesVotes >= nMinPassing;
	bbpProposal.fIsPaid = bbpProposal.nHeight < nLastSuperblock;
	return bbpProposal;
}

std::string DescribeProposal(BiblePayProposal bbpProposal)
{
	std::string sReport = "Proposal StartDate: " + bbpProposal.sProposalHRTime + ", Hash: " + bbpProposal.uHash.GetHex() + " for Amount: " + RoundToString(bbpProposal.nAmount, 2) + "BBP, Name: " 
				+ bbpProposal.sName + ", ExpType: " + bbpProposal.sExpenseType + ", PAD: " + bbpProposal.sAddress 
				+ ", Height: " + RoundToString(bbpProposal.nHeight, 0) 
				+ ", Votes: " + RoundToString(bbpProposal.nNetYesVotes, 0) + ", LastSB: " 
				+ RoundToString(bbpProposal.nLastSuperblock, 0);
	return sReport;
}

std::string WatchmanOnTheWall(bool fForce, std::string& sContract)
{
	if (!fMasternodeMode && !fForce)   
		return "NOT_A_WATCHMAN_SANCTUARY";
	if (!chainActive.Tip()) 
		return "WATCHMAN_INVALID_CHAIN";
	if (!ChainSynced(chainActive.Tip()))
		return "WATCHMAN_CHAIN_NOT_SYNCED";

	const Consensus::Params& consensusParams = Params().GetConsensus();
	int MIN_EPOCH_BLOCKS = consensusParams.nSuperblockCycle * .10; // TestNet Weekly superblocks (1435=144), Prod Monthly superblocks (6150=615), this means a 144 block warning in TestNet, and a 615 block warning in Prod

	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);

	int nSancCount = mnodeman.CountEnabled();
	std::string sReport;

	int nBlocksUntilEpoch = nNextSuperblock - chainActive.Tip()->nHeight;
	if (nBlocksUntilEpoch < 0)
		return "WATCHMAN_LOW_HEIGHT";

	if (nBlocksUntilEpoch < MIN_EPOCH_BLOCKS && !fForce)
		return "WATCHMAN_TOO_EARLY_FOR_COMING";

	int nStartTime = GetAdjustedTime() - (86400 * 32);
    LOCK2(cs_main, governance.cs);
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::vector<std::pair<int, uint256> > vProposalsSortedByVote;
	vProposalsSortedByVote.reserve(objs.size() + 1);
    
	for (const CGovernanceObject* pGovObj : objs) 
    {
		if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
		BiblePayProposal bbpProposal = GetProposalByHash(pGovObj->GetHash(), nLastSuperblock);
		// We need unpaid, passing that fit within the budget
		sReport = DescribeProposal(bbpProposal);
		if (!bbpProposal.fIsPaid)
		{
			if (bbpProposal.fPassing)
			{
				LogPrintf("\n Watchman::Inserting %s for NextSB: %f", sReport, (double)nNextSuperblock);
				vProposalsSortedByVote.push_back(std::make_pair(bbpProposal.nNetYesVotes, bbpProposal.uHash));
			}
			else
			{
				LogPrintf("\n Watchman (not inserting) %s because we have Votes %f (req votes %f)", sReport, bbpProposal.nNetYesVotes, bbpProposal.nMinPassing);
			}
		}
		else
		{
			LogPrintf("\n Watchman (Found Paid) %s ", sReport);
		}
	}
	// Now we need to sort the vector of proposals by Vote descending
	std::sort(vProposalsSortedByVote.begin(), vProposalsSortedByVote.end());
	std::reverse(vProposalsSortedByVote.begin(), vProposalsSortedByVote.end());
	// Now lets only move proposals that fit in the budget
	std::vector<std::pair<double, uint256> > vProposalsInBudget;
	vProposalsInBudget.reserve(objs.size() + 1);
    
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nNextSuperblock);
	CAmount nSpent = 0;
	for (auto item : vProposalsSortedByVote)
    {
		BiblePayProposal p = GetProposalByHash(item.second, nLastSuperblock);
		if (((p.nAmount * COIN) + nSpent) < nPaymentsLimit)
		{
			nSpent += (p.nAmount * COIN);
			vProposalsInBudget.push_back(std::make_pair(p.nAmount, p.uHash));
			sReport = DescribeProposal(p);
			LogPrintf("\n Watchman::Adding Budget Proposal %s -- Running Total %f ", sReport, (double)nSpent/COIN);
		}
    }
	// Create the contract
	std::string sAddresses;
	std::string sPayments;
	std::string sHashes;
	std::string sVotes;
	for (auto item : vProposalsInBudget)
    {
		BiblePayProposal p = GetProposalByHash(item.second, nLastSuperblock);
		CBitcoinAddress cbaAddress(p.sAddress);
		if (cbaAddress.IsValid() && p.nAmount > .01)
		{
			sAddresses += p.sAddress + "|";
			sPayments += RoundToString(p.nAmount, 2) + "|";
			sHashes += p.uHash.GetHex() + "|";
			sVotes += RoundToString(p.nNetYesVotes, 0) + "|";
		}
	}
	if (sPayments.length() > 1) 
		sPayments = sPayments.substr(0, sPayments.length() - 1);
	if (sAddresses.length() > 1)
		sAddresses = sAddresses.substr(0, sAddresses.length() - 1);
	if (sHashes.length() > 1)
		sHashes = sHashes.substr(0, sHashes.length() - 1);
	if (sVotes.length() > 1)
		sVotes = sVotes.substr(0, sVotes.length() -1);

	sContract = "<ADDRESSES>" + sAddresses + "</ADDRESSES><PAYMENTS>" + sPayments + "</PAYMENTS><PROPOSALS>" + sHashes + "</PROPOSALS>";

	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPamHash = GetPAMHashByContract(sContract);
	int iTriggerVotes = 0;
	GetGSCGovObjByHeight(nNextSuperblock, uPamHash, iTriggerVotes, uGovObjHash, sAddresses, sPayments);
	std::string sError;

	if (sPayments.empty())
	{
		return "EMPTY_CONTRACT";
	}
	sContract += "<VOTES>" + RoundToString(iTriggerVotes, 0) + "</VOTES><METRICS><HASH>" + uGovObjHash.GetHex() + "</HASH><PAMHASH>" 
		+ uPamHash.GetHex() + "</PAMHASH><SANCTUARYCOUNT>" + RoundToString(nSancCount, 0) + "</SANCTUARYCOUNT></METRICS><VOTEDATA>" + sVotes + "</VOTEDATA>";

	if (uGovObjHash == uint256S("0x0"))
	{
		std::string sWatchmanTrigger = SerializeSanctuaryQuorumTrigger(nNextSuperblock, nNextSuperblock, sContract);
		std::string sGobjectHash;
		SubmitGSCTrigger(sWatchmanTrigger, sGobjectHash, sError);
		LogPrintf("**WatchmanOnTheWall::SubmitWatchmanTrigger::CreatingWatchmanContract hash %s , gobject %s, results %s **\n", sWatchmanTrigger, sGobjectHash, sError);
		sContract += "<ACTION>CREATING_CONTRACT</ACTION>";
		return "WATCHMAN_CREATING_CONTRACT";
	}
	else if (iTriggerVotes < (nSancCount / 2))
	{
		bool bResult = VoteForGSCContract(nNextSuperblock, sContract, sError);
		LogPrintf("**WatchmanOnTheWall::VotingForWatchmanTrigger PAM Hash %s, Trigger Votes %f  (%s)", uPamHash.GetHex(), (double)iTriggerVotes, sError);
		sContract += "<ACTION>VOTING</ACTION>";
		return "WATCHMAN_VOTING";
	}

	return "WATCHMAN_SUCCESS";
}


//////////////////////////////////////////////////////////////////////////////// GSC Server side Abstraction Interface ////////////////////////////////////////////////////////////////////////////////////////////////


std::string GetGSCContract(int nHeight, bool fCreating)
{
	int nNextSuperblock = 0;
	int nLast = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, nNextSuperblock);
	if (nHeight != 0) 
		nLast = nHeight;
	std::string sContract = AssessBlocks(nLast, fCreating);
	return sContract;
}

double CalculatePoints(std::string sCampaign, std::string sDiary, double nCoinAge, CAmount nDonation, std::string sCPK)
{
	boost::to_upper(sCampaign);
	double nPoints = 0;

	if (sCampaign == "POG")
	{
		double nComponent1 = nCoinAge;
		double nTithed = (double)nDonation / COIN;
		bool f666 = (nTithed == .666 || nTithed == 666.000 || nTithed == 6666.666 || nTithed == 6666 || nTithed == 666.666 || nTithed == 6660.00);
		if (f666)          nTithed = 0;
		if (nTithed < .25) nTithed = 0;
		double nTitheFactor = GetSporkDouble("pogtithefactor", 1);
		double nComponent2 = cbrt(nTithed) * nTitheFactor;
		nPoints = nComponent1 * nComponent2;
		return nPoints;
	}
	else if (sCampaign == "HEALING")
	{
		double nMultiplier = sDiary.empty() ? 0 : 1;
		nPoints = nCoinAge * nMultiplier;
		return nPoints;
	}
	else if (sCampaign == "CAMEROON-ONE")
	{
		double nTotalPoints = 0;
		// We need to find how many children this CPK sponsors first.
		std::map<std::string, CPK> cp1 = GetChildMap("cpk|cameroon-one");
		for (std::pair<std::string, CPK> a : cp1)
		{
			std::string sSponsorCPK = a.second.sAddress;
			std::string sChildID = a.second.sOptData;
			if (!sChildID.empty() && sSponsorCPK == sCPK)
			{
				double nBalance = GetCameroonChildBalance(sChildID);
				if (nBalance != -999 && nBalance <= 0)
				{
					// This child is in good standing (with a credit balance).
					double nCameroonOneMonthlyRate = GetSporkDouble("cameroononemonthlyrate", 40);
					double nDailyCharges = nCameroonOneMonthlyRate / 30;
					nTotalPoints += (nDailyCharges * 1000);
					if (fDebugSpam)
						LogPrintf("\nFound Cameroon-One Child %s for CPK %s, crediting Daily Charges %f, TotalPoints %f ", sChildID, sSponsorCPK, nDailyCharges, nTotalPoints);
				}
			}
		}
		return nTotalPoints;
	}
	return 0;
}

bool VoteForGobject(uint256 govobj, std::string sVoteSignal, std::string sVoteOutcome, std::string& sError)
{
	if (sVoteSignal != "funding" && sVoteSignal != "delete")
	{
		LogPrintf("Sanctuary tried to vote in a way that is prohibited.  Vote failed. %s", sVoteSignal);
		return false;
	}

	vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(sVoteSignal);
	vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(sVoteOutcome);
	int nSuccessful = 0;
	int nFailed = 0;
	int govObjType;
	{
        LOCK(governance.cs);
        CGovernanceObject *pGovObj = governance.FindGovernanceObject(govobj);
        if (!pGovObj) 
		{
			sError = "Governance object not found";
			return false;
        }
        govObjType = pGovObj->GetObjectType();
    }

    CMasternode mn;
    bool fMnFound = mnodeman.Get(activeMasternodeInfo.outpoint, mn);

    if (!fMnFound) 
	{
        sError = "Can't find masternode by collateral output";
		return false;
    }

    CGovernanceVote vote(mn.outpoint, govobj, eVoteSignal, eVoteOutcome);

    bool signSuccess = false;
    if (deterministicMNManager->IsDeterministicMNsSporkActive()) {
        if (govObjType == GOVERNANCE_OBJECT_PROPOSAL && eVoteSignal == VOTE_SIGNAL_FUNDING) 
		{
            sError = "Can't use vote-conf for proposals when deterministic masternodes are active";
			return false;
        }
        if (activeMasternodeInfo.blsKeyOperator) 
		{
            signSuccess = vote.Sign(*activeMasternodeInfo.blsKeyOperator);
        }
    } else {
        signSuccess = vote.Sign(activeMasternodeInfo.legacyKeyOperator, activeMasternodeInfo.legacyKeyIDOperator);
    }
    if (!signSuccess) 
	{
        sError = "Failure to sign.";
		return false;
	}

    CGovernanceException exception;
    if (governance.ProcessVoteAndRelay(vote, exception, *g_connman)) 
	{
        nSuccessful++;
    } else {
        nFailed++;
    }

    return (nSuccessful > 0) ? true : false;
   
}

bool NickNameExists(std::string sProjectName, std::string sNickName)
{
	std::map<std::string, CPK> mAllCPKs = GetGSCMap(sProjectName, "", true);
	boost::to_upper(sNickName);
	for (std::pair<std::string, CPK> a : mAllCPKs)
	{
		if (boost::iequals(a.second.sNickName, sNickName))
			return true;
	}
	return false;
}

std::string AssessBlocks(int nHeight, bool fCreatingContract)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight);
	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;
	CAmount nQTBuffer = nPaymentsLimit * .01;
	nPaymentsLimit -= nQTBuffer;

	int64_t nPaymentBuffer = sporkManager.GetSporkValue(SPORK_31_GSC_BUFFER);
	if (nPaymentBuffer > 0 && nPaymentBuffer < (nPaymentsLimit / COIN))
	{
		nPaymentsLimit -= nPaymentBuffer * COIN;
	}

	if (!chainActive.Tip()) 
		return std::string();
	if (nHeight > chainActive.Tip()->nHeight)
		nHeight = chainActive.Tip()->nHeight - 1;

	int nMaxDepth = nHeight;
	int nMinDepth = nMaxDepth - BLOCKS_PER_DAY;
	if (nMinDepth < 1) 
		return std::string();
	CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::map<std::string, CPK> mPoints;
	std::map<std::string, double> mCampaignPoints;
	std::map<std::string, CPK> mCPKCampaignPoints;
	std::map<std::string, double> mCampaigns;
	double dDebugLevel = cdbl(GetArg("-debuglevel", "0"), 0);
	std::string sDiaries;
	std::string sAnalyzeUser = ReadCache("analysis", "user");
	std::string sAnalysisData1;

	while (pindex && pindex->nHeight < nMaxDepth)
	{
		if (pindex->nHeight < chainActive.Tip()->nHeight) 
			pindex = chainActive.Next(pindex);
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int n = 0; n < block.vtx.size(); n++)
			{
				if (block.vtx[n]->IsGSCTransmission() && CheckAntiBotNetSignature(block.vtx[n], "gsc", ""))
				{
					std::string sCampaignName;
					std::string sCPK = GetTxCPK(block.vtx[n], sCampaignName);
					CPK localCPK = GetCPKFromProject("cpk", sCPK);
					double nCoinAge = 0;
					CAmount nDonation = 0;
					GetTransactionPoints(pindex, block.vtx[n], nCoinAge, nDonation);
					if (CheckCampaign(sCampaignName) && !sCPK.empty())
					{
						std::string sDiary = ExtractXML(block.vtx[n]->GetTxMessage(), "<diary>","</diary>");
						double nPoints = CalculatePoints(sCampaignName, sDiary, nCoinAge, nDonation, sCPK);
						if (sCampaignName == "CAMEROON-ONE" && mCPKCampaignPoints[sCPK + sCampaignName].nPoints > 0)
							nPoints = 0;
						if (nPoints > 0)
						{
							// CPK 
							CPK c = mPoints[sCPK];
							c.sCampaign = sCampaignName;
							c.sAddress = sCPK;
							c.sNickName = localCPK.sNickName;
							c.nPoints += nPoints;
							mCampaignPoints[sCampaignName] += nPoints;
							mPoints[sCPK] = c;

							// CPK-Campaign
							CPK cCPKCampaignPoints = mCPKCampaignPoints[sCPK + sCampaignName];
							cCPKCampaignPoints.sAddress = sCPK;
							cCPKCampaignPoints.sNickName = c.sNickName;
							cCPKCampaignPoints.nPoints += nPoints;
							mCPKCampaignPoints[sCPK + sCampaignName] = cCPKCampaignPoints;
							if (dDebugLevel == 1)
								LogPrintf("\nUser %s , NN %s, Diary %s, height %f, TXID %s, nn %s, Points %f, Campaign %s, coinage %f, donation %f, usertotal %f ",
								c.sAddress, localCPK.sNickName, sDiary, pindex->nHeight, block.vtx[n]->GetHash().GetHex(), localCPK.sNickName, 
								(double)nPoints, c.sCampaign, (double)nCoinAge, 
								(double)nDonation/COIN, (double)c.nPoints);
							if (!sAnalyzeUser.empty() && sAnalyzeUser == c.sNickName)
							{
								std::string sInfo = "User: " + c.sAddress + ", Diary: " + sDiary + ", Height: " + RoundToString(pindex->nHeight, 2)
									+ ", TXID: " + block.vtx[n]->GetHash().GetHex() + ", NickName: " 
									+ localCPK.sNickName + ", Points: " + RoundToString(nPoints, 2) 
									+ ", Campaign: " + c.sCampaign + ", CoinAge: " + RoundToString(nCoinAge, 4) 
									+ ", Donation: " + RoundToString(nDonation/COIN, 4) + ", UserTotal: " + RoundToString(c.nPoints, 2) + "\n";
									sAnalysisData1 += sInfo;
							}
							if (c.sCampaign == "HEALING" && !sDiary.empty())
							{
								sDiaries += "\n" + sCPK + "|" + localCPK.sNickName + "|" + sDiary;
							}
						}
					}
				}
			}
		}
	}

	std::string sData;
	std::string sGenData;
	std::string sDetails;
	double nTotalPoints = 0;
	// Convert To Campaign-CPK-Prominence levels
	std::string sAnalysisData2;
	for (auto myCampaign : mCampaignPoints)
	{
		std::string sCampaignName = myCampaign.first;
		double nCampaignPercentage = GetSporkDouble(sCampaignName + "campaignpercentage", 0);
		if (nCampaignPercentage < 0) nCampaignPercentage = 0;
		double nCampaignPoints = mCampaignPoints[sCampaignName];
		if (fDebugSpam)
			LogPrintf("\n SCS-AssessBlocks::Processing Campaign %s (%f), Payment Pctg [%f], TotalPoints %f ", 
			myCampaign.first, myCampaign.second, nCampaignPercentage, nCampaignPoints);
		nCampaignPoints += 1;
		nTotalPoints += nCampaignPoints;
		for (auto Members : mPoints)
		{
			std::string sKey = Members.second.sAddress + sCampaignName;

			double nPreProminence = (mCPKCampaignPoints[sKey].nPoints / nCampaignPoints) * nCampaignPercentage;
			// Verify we did not exceed the cap
			double nCap = GetProminenceCap(sCampaignName, mCPKCampaignPoints[sKey].nPoints, nPreProminence);
			mCPKCampaignPoints[sKey].nProminence = nCap;

			if (fDebugSpam)
				LogPrintf("\nUser %s, Campaign %s, Points %f, Prominence %f ", mCPKCampaignPoints[sKey].sAddress, sCampaignName, 
				mCPKCampaignPoints[sKey].nPoints, mCPKCampaignPoints[sKey].nProminence);
			std::string sRow = sCampaignName + "|" + Members.second.sAddress + "|" + RoundToString(mCPKCampaignPoints[sKey].nPoints, 0) + "|" 
				+ RoundToString(mCPKCampaignPoints[sKey].nProminence, 8) + "|" + Members.second.sNickName + "|" + 
				RoundToString(nCampaignPoints, 0) + "\n";
			if (!sAnalyzeUser.empty() && sAnalyzeUser == Members.second.sNickName)
			{
				sAnalysisData2 += sRow;
			}
			sDetails += sRow;
		}
	}
	WriteCache("analysis", "data_1", sAnalysisData1, GetAdjustedTime());
	WriteCache("analysis", "data_2", sAnalysisData2, GetAdjustedTime());

	// Grand Total for Smart Contract
	for (auto Members : mCPKCampaignPoints)
	{
		mPoints[Members.second.sAddress].nProminence += Members.second.nProminence;
	}
	
	// Create the Daily Contract
	// Allow room for a QT change between contract creation time and superblock generation time
	double nMaxContractPercentage = .98;
	std::string sAddresses;
	std::string sPayments;
	std::string sProminenceExport = "<PROMINENCE>";
	double nGSCContractType = GetSporkDouble("GSC_CONTRACT_TYPE", 0);
	double GSC_MIN_PAYMENT = 1;
	double nTotalProminence = 0;
	if (nGSCContractType == 0)
		GSC_MIN_PAYMENT = .25;
	for (auto Members : mPoints)
	{
		CAmount nPayment = Members.second.nProminence * nPaymentsLimit * nMaxContractPercentage;
		CBitcoinAddress cbaAddress(Members.second.sAddress);
		if (cbaAddress.IsValid() && nPayment > (GSC_MIN_PAYMENT * COIN))
		{
			sAddresses += Members.second.sAddress + "|";
			if (nGSCContractType == 0)
			{
				sPayments += RoundToString(nPayment / COIN, 2) + "|";
			}
			else if (nGSCContractType == 1)
			{
				sPayments += RoundToString((double)nPayment / COIN, 2) + "|";
			}
			CPK localCPK = GetCPKFromProject("cpk", Members.second.sAddress);
			std::string sRow =  "ALL|" + Members.second.sAddress + "|" + RoundToString(Members.second.nPoints, 0) + "|" 
				+ RoundToString(Members.second.nProminence, 4) + "|" 
				+ localCPK.sNickName + "|" 
				+ RoundToString((double)nPayment / COIN, 2) + "\n";
			sGenData += sRow;
			nTotalProminence += Members.second.nProminence;
			sProminenceExport += "<CPK>" + Members.second.sAddress + "|" + RoundToString(Members.second.nPoints, 0) + "|" + RoundToString(Members.second.nProminence, 4) + "|" + localCPK.sNickName + "</CPK>";
		}
	}
	sProminenceExport += "</PROMINENCE>";
	
	std::string QTData;
	if (fCreatingContract)
	{
		// Add the QT Phase
		double out_PriorPrice = 0;
		double out_PriorPhase = 0;
		double out_BTC = 0;
		double dPrice = GetPBase(out_BTC);
		double dPhase = GetQTPhase(true, dPrice, chainActive.Tip()->nHeight, out_PriorPrice, out_PriorPhase);
		if (dPhase > 0 && !consensusParams.FoundationQTAddress.empty())
		{
			sAddresses += consensusParams.FoundationQTAddress + "|";
			sPayments += RoundToString(dPhase / 100, 4) + "|";
		}
		QTData = "<QTDATA><PRICE>" + RoundToString(dPrice, 12) + "</PRICE><BTCPRICE>" + RoundToString(out_BTC, 2) + "</BTCPRICE><QTPHASE>" + RoundToString(dPhase, 0) + "</QTPHASE></QTDATA>";
	}
	
	if (sPayments.length() > 1) 
		sPayments = sPayments.substr(0, sPayments.length() - 1);
	if (sAddresses.length() > 1)
		sAddresses = sAddresses.substr(0, sAddresses.length() - 1);

	std::string sCPKList = "<CPKLIST>";
	std::map<std::string, CPK> mAllC = GetGSCMap("cpk", "", true);
	for (std::pair<std::string, CPK> a : mAllC)
	{ 
		sCPKList += "<C>" + a.second.sAddress + "|" + a.second.sNickName + "</C>";
	}
	sCPKList += "</CPKLIST>";
	// The BiblePay Daily Export should also send a list of registered stratis nodes in this XML report.
	std::string sStratisNodes = "<STRATISNODES>";
	std::map<std::string, CPK> mAllStratis = GetGSCMap("stratis", "", true);
	for (std::pair<std::string, CPK> a : mAllStratis)
	{
		// ToDo:  The stratis public IP field must be added to our campaign object and to the daily export
		sStratisNodes += "<NODE>" + a.second.sAddress + "|" + a.second.sNickName + "</NODE>";
	}
	sStratisNodes += "</STRATISNODES>";
	double nTotalPayments = nTotalProminence * (double)nPaymentsLimit / COIN;

	sData = "<PAYMENTS>" + sPayments + "</PAYMENTS><ADDRESSES>" + sAddresses + "</ADDRESSES><DATA>" + sGenData + "</DATA><LIMIT>" 
		+ RoundToString(nPaymentsLimit/COIN, 4) + "</LIMIT><TOTALPROMINENCE>" + RoundToString(nTotalProminence, 2) + "</TOTALPROMINENCE><TOTALPAYOUT>" + RoundToString(nTotalPayments, 2) 
		+ "</TOTALPAYOUT><TOTALPOINTS>" + RoundToString(nTotalPoints, 2) + "</TOTALPOINTS><MINDEPTH>" 
		+ RoundToString(nMinDepth, 0) + "</MINDEPTH><MAXDEPTH>" + RoundToString(nMaxDepth, 0) + "</MAXDEPTH><DIARIES>" 
		+ sDiaries + "</DIARIES><DETAILS>" + sDetails + "</DETAILS>" + QTData + sProminenceExport + sCPKList + sStratisNodes;
	if (dDebugLevel == 1)
		LogPrintf("XML %s", sData);
	return sData;
}

void DailyExport()
{
	// This procedure exports data to Stratis clients
	double dDisableStratisExport = cdbl(GetArg("-disablestratisexport", "0"), 0);
	if (dDisableStratisExport == 1) 
		return;
	std::string sSuffix = fProd ? "_prod" : "_testnet";
	std::string sTarget = GetSANDirectory2() + "dataexport" + sSuffix;
	FILE *outFile = fopen(sTarget.c_str(), "w");
	if (!chainActive.Tip()) 
		return;
	std::string sContract = GetGSCContract(chainActive.Tip()->nHeight, false);
	fputs(sContract.c_str(), outFile);
	fclose(outFile);
}

int GetRequiredQuorumLevel(int nHeight)
{
	static int MINIMUM_QUORUM_PROD = 10;
	static int MINIMUM_QUORUM_TESTNET = 3;
	int nCount = mnodeman.CountEnabled();
	int nReq = nCount * .20;
	int nMinimumQuorum = fProd ? MINIMUM_QUORUM_PROD : MINIMUM_QUORUM_TESTNET;
	if (nReq < nMinimumQuorum) nReq = nMinimumQuorum;
	return nReq;
}

uint256 GetPAMHash(std::string sAddresses, std::string sAmounts)
{
	std::string sConcat = sAddresses + sAmounts;
	if (sConcat.empty()) return uint256S("0x0");
	std::string sHash = RetrieveMd5(sConcat);
	return uint256S("0x" + sHash);
}

std::vector<std::pair<int64_t, uint256>> GetGSCSortedByGov(int nHeight, uint256 inPamHash, bool fIncludeNonMatching)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	std::vector<std::pair<int64_t, uint256> > vPropByGov;
	vPropByGov.reserve(objs.size() + 1);
	int iOffset = 0;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
		UniValue obj = myGov->GetJSONObject();
		int nLocalHeight = obj["event_block_height"].get_int();
		if (nLocalHeight == nHeight)
		{
			iOffset++;
			std::string sPAD = obj["payment_addresses"].get_str();
			std::string sPAM = obj["payment_amounts"].get_str();
			int iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			uint256 uPamHash = GetPAMHash(sPAD, sPAM);
			if (fIncludeNonMatching && inPamHash != uPamHash)
			{
				// This is a Gov Obj that matches the height, but does not match the contract, we need to vote it down
				vPropByGov.push_back(std::make_pair(myGov->GetCreationTime() + iOffset, myGov->GetHash()));
			}
			if (!fIncludeNonMatching && inPamHash == uPamHash)
			{
				// Note:  the pair is used in case we want to store an object later (the PamHash is not distinct, but the govHash is).
				vPropByGov.push_back(std::make_pair(myGov->GetCreationTime() + iOffset, myGov->GetHash()));
			}
		}
	}
	return vPropByGov;
}

bool IsOverBudget(int nHeight, std::string sAmounts)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight);
	if (sAmounts.empty()) return false;
	std::vector<std::string> vPayments = Split(sAmounts.c_str(), "|");
	double dTotalPaid = 0;
	for (int i = 0; i < vPayments.size(); i++)
	{
		dTotalPaid += cdbl(vPayments[i], 2);
	}
	if ((dTotalPaid * COIN) > nPaymentsLimit)
		return true;
	return false;
}

bool VoteForGSCContract(int nHeight, std::string sMyContract, std::string& sError)
{
	int iPendingVotes = 0;
	uint256 uGovObjHash;
	std::string sPaymentAddresses;
	std::string sAmounts;
	uint256 uPamHash = GetPAMHashByContract(sMyContract);
	GetGSCGovObjByHeight(nHeight, uPamHash, iPendingVotes, uGovObjHash, sPaymentAddresses, sAmounts);
	
	bool fOverBudget = IsOverBudget(nHeight, sAmounts);

	// Verify Payment data matches our payment data, otherwise dont vote for it
	if (sPaymentAddresses.empty() || sAmounts.empty())
	{
		sError = "Unable to vote for GSC Contract::Foreign addresses or amounts empty.";
		return false;
	}
	// Sort by GSC gobject hash (creation time does not work as multiple nodes may be called during the same second to create a GSC)
	// Phase 1: Eliminate all Ties, and vote for the lowest Valid matching contract:
	std::vector<std::pair<int64_t, uint256>> vPropByGov = GetGSCSortedByGov(nHeight, uPamHash, false);
	// Now we need to sort the vector by Gov hash
	std::sort(vPropByGov.begin(), vPropByGov.end());
	std::string sAction;
	int iVotes = 0; 
	
	for (int i = 0; i < vPropByGov.size(); i++)
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(vPropByGov[i].second);
		iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
		LogPrintf("\nSmartContract-Server::VoteForGSCContractOrderedByHash::Voting %s for govHash %s, with pre-existing-votes %f (created %f) Overbudget %f ",
			sAction, myGov->GetHash().GetHex(), iVotes, myGov->GetCreationTime(), (double)fOverBudget);
		if (i == 0)
		{
			sAction = fOverBudget ? "no" : "yes";
			VoteForGobject(myGov->GetHash(), "funding", sAction, sError);
			break;
		}
	}
	// Phase 2: Vote against contracts at this height that do not match our hash
	bool bFeatureOn = true;
	if (bFeatureOn && uPamHash != uint256S("0x0"))
	{
		vPropByGov = GetGSCSortedByGov(nHeight, uPamHash, true);
		for (int i = 0; i < vPropByGov.size(); i++)
		{
			CGovernanceObject* myGovForRemoval = governance.FindGovernanceObject(vPropByGov[i].second);
			int iVotes = myGovForRemoval->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			LogPrintf("\nSmartContract-Server::VoteDownBadGCCContracts::Voting %s for govHash %s, with pre-existing-votes %f (created %f)",
				sAction, myGovForRemoval->GetHash().GetHex(), iVotes, myGovForRemoval->GetCreationTime());
			if (i == 0)
			{
				VoteForGobject(myGovForRemoval->GetHash(), "funding", "no", sError);
				break;
			}
		}
	}
	/*
	//Phase 3:  Vote to delete very old contracts - This causes too much spam - this is now moved to the cache delete sub
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	vPropByGov = GetGSCSortedByGov(iLastSuperblock, uPamHash, true);
	for (int i = 0; i < vPropByGov.size(); i++)
	{
		CGovernanceObject* myGovYesterday = governance.FindGovernanceObject(vPropByGov[i].second);
		int iVotes = myGovYesterday->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
		LogPrintf("\nSmartContract-Server::DeleteYesterdaysEmptyGSCContracts::Voting %s for govHash %s, with pre-existing-votes %f (created %f)",
				sAction, myGovYesterday->GetHash().GetHex(), iVotes, myGovYesterday->GetCreationTime());
		int64_t nAge = GetAdjustedTime() - myGovYesterday->GetCreationTime();
		if (iVotes == 0 && nAge > (60 * 60 * 24))
		{
			VoteForGobject(myGovYesterday->GetHash(), "delete", "yes", sError);
		}
	}
	*/

	return sError.empty() ? true : false;
}

bool SubmitGSCTrigger(std::string sHex, std::string& gobjecthash, std::string& sError)
{
	if(!masternodeSync.IsBlockchainSynced()) 
	{
		sError = "Must wait for client to sync with Sanctuary network. Try again in a minute or so.";
		return false;
	}

	if (!fMasternodeMode)
	{
		sError = "You must be a sanctuary to submit a GSC trigger.";
		return false;
	}

	bool fMnFound = mnodeman.Has(activeMasternodeInfo.outpoint);
	DBG( std::cout << "gobject: submit activeSancInfo.keyIDOperator = " << activeMasternodeInfo.legacyKeyIDOperator.ToString()
         << ", outpoint = " << activeMasternodeInfo.outpoint.ToStringShort()
         << ", params.size() = " << request.params.size()
         << ", fMnFound = " << fMnFound << std::endl; );
	uint256 txidFee;
	uint256 hashParent = uint256();
	int nRevision = 1;
	int nTime = GetAdjustedTime();
	std::string strData = sHex;
	int64_t nLastGSCSubmitted = 0;
	CGovernanceObject govobj(hashParent, nRevision, nTime, txidFee, strData);

	if (fDebug) LogPrintf("\nSubmitting GSC Trigger %s ", govobj.GetDataAsPlainString());

    DBG( std::cout << "gobject: submit "
         << " GetDataAsPlainString = " << govobj.GetDataAsPlainString()
         << ", hash = " << govobj.GetHash().GetHex()
         << ", txidFee = " << txidFee.GetHex()
         << std::endl; );

	if (govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) 
	{
        if (fMnFound) {
            govobj.SetMasternodeOutpoint(activeMasternodeInfo.outpoint);
            if (deterministicMNManager->IsDeterministicMNsSporkActive()) 
			{
                govobj.Sign(*activeMasternodeInfo.blsKeyOperator);
            } else {
                govobj.Sign(activeMasternodeInfo.legacyKeyOperator, activeMasternodeInfo.legacyKeyIDOperator);
            }
        } else 
		{
            sError = "Object submission rejected because node is not a Sanctuary\n";
			return false;
        }
    }

	std::string strHash = govobj.GetHash().ToString();
	std::string strError;
	bool fMissingMasternode;
	bool fMissingConfirmations;
    {
        LOCK(cs_main);
        if (!govobj.IsValidLocally(strError, fMissingMasternode, fMissingConfirmations, true) && !fMissingConfirmations) 
		{
            sError = "gobject(submit) -- Object submission rejected because object is not valid - hash = " + strHash + ", strError = " + strError;
		    return false;
	    }
    }

	int64_t nAge = GetAdjustedTime() - nLastGSCSubmitted;
	if (nAge < (60 * 15))
	{
		sError = "Local Creation rate limit exceeded (0208)";
		return false;
	}

	if (fMissingConfirmations) 
	{
        governance.AddPostponedObject(govobj);
        govobj.Relay(*g_connman);
    } 
	else 
	{
        governance.AddGovernanceObject(govobj, *g_connman);
    }

	gobjecthash = govobj.GetHash().ToString();
	nLastGSCSubmitted = GetAdjustedTime();

	return true;
}

int GetLastGSCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock)
{
    int nLastSuperblock = 0;
    int nSuperblockStartBlock = Params().GetConsensus().nDCCSuperblockStartBlock;
	int nHeight = nCurrentHeight;
	for (; nHeight > nSuperblockStartBlock; nHeight--)
	{
		if (CSuperblock::IsSmartContract(nHeight))
		{
			nLastSuperblock = nHeight;
			break;
		}
	}
	nHeight = nLastSuperblock + 1;

	for (; nHeight > nLastSuperblock; nHeight++)
	{
		if (CSuperblock::IsSmartContract(nHeight))
		{
			nNextSuperblock = nHeight;
			break;
		}
	}

	return nLastSuperblock;
}

uint256 GetPAMHashByContract(std::string sContract)
{
	std::string sAddresses = ExtractXML(sContract, "<ADDRESSES>","</ADDRESSES>");
	std::string sAmounts = ExtractXML(sContract, "<PAYMENTS>","</PAYMENTS>");
	uint256 u = GetPAMHash(sAddresses, sAmounts);
	/* LogPrintf("GetPAMByContract addr %s, amounts %s, uint %s",sAddresses, sAmounts, u.GetHex()); */
	return u;
}

void GetGSCGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	int iHighVotes = -1;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
	    UniValue obj = myGov->GetJSONObject();
		int nLocalHeight = obj["event_block_height"].get_int();
		if (nLocalHeight == nHeight)
		{
			std::string sPAD = obj["payment_addresses"].get_str();
			std::string sPAM = obj["payment_amounts"].get_str();
			int iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			uint256 uHash = GetPAMHash(sPAD, sPAM);
			/* LogPrintf("\n Found gscgovobj2 %s with votes %f with pad %s and pam %s , pam hash %s ", myGov->GetHash().GetHex(), (double)iVotes, sPAD, sPAM, uHash.GetHex()); */
			if (uOptFilter != uint256S("0x0") && uHash != uOptFilter) continue;
			// This governance-object matches the trigger height and the optional filter
			if (iVotes > iHighVotes) 
			{
				iHighVotes = iVotes;
				out_PaymentAddresses = sPAD;
				out_PaymentAmounts = sPAM;
				out_nVotes = iHighVotes;
				out_uGovObjHash = myGov->GetHash();
			}
		}
	}
}

void GetGovObjDataByPamHash(int nHeight, uint256 hPamHash, std::string& out_Data)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	int iHighVotes = -1;
	std::string sData;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
	    UniValue obj = myGov->GetJSONObject();
		int nLocalHeight = obj["event_block_height"].get_int();
		if (nLocalHeight == nHeight)
		{
			std::string sPAD = obj["payment_addresses"].get_str();
			std::string sPAM = obj["payment_amounts"].get_str();
			int iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			uint256 uHash = GetPAMHash(sPAD, sPAM);
			if (hPamHash == uHash) 
			{	
				std::string sRow = "gov=" + myGov->GetHash().GetHex() + ",pam=" + hPamHash.GetHex() + ",votes=" + RoundToString(iVotes, 0) + ";     ";
				sData += sRow;
			}
		}
	}
	out_Data = sData;
}


bool GetContractPaymentData(std::string sContract, int nBlockHeight, std::string& sPaymentAddresses, std::string& sAmounts)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nBlockHeight);
	sPaymentAddresses = ExtractXML(sContract, "<ADDRESSES>", "</ADDRESSES>");
	sAmounts = ExtractXML(sContract, "<PAYMENTS>", "</PAYMENTS>");
	std::vector<std::string> vPayments = Split(sAmounts.c_str(), "|");
	double dTotalPaid = 0;
	for (int i = 0; i < vPayments.size(); i++)
	{
		dTotalPaid += cdbl(vPayments[i], 2);
	}
	if (dTotalPaid < 1 || (dTotalPaid * COIN) > nPaymentsLimit)
	{
		LogPrintf(" \n ** GetContractPaymentData::Superblock Payment Budget is out of bounds:  Limit %f,  Actual %f  ** \n", (double)nPaymentsLimit/COIN, (double)dTotalPaid);
		return false;
	}
	return true;
}

uint256 GetGSCHash(std::string sContract)
{
	std::string sHash = RetrieveMd5(sContract);
	return uint256S("0x" + sHash);
}

std::string SerializeSanctuaryQuorumTrigger(int iContractAssessmentHeight, int nEventBlockHeight, std::string sContract)
{
	std::string sEventBlockHeight = RoundToString(nEventBlockHeight, 0);
	std::string sPaymentAddresses;
	std::string sPaymentAmounts;
	// For Evo compatibility and security purposes, we move the QT Phase into the GSC contract so all sancs must agree on the phase
	std::string sQTData = ExtractXML(sContract, "<QTDATA>", "</QTDATA>");
	std::string sHashes = ExtractXML(sContract, "<PROPOSALS>", "</PROPOSALS>");
	bool bStatus = GetContractPaymentData(sContract, iContractAssessmentHeight, sPaymentAddresses, sPaymentAmounts);
	if (!bStatus) 
		return std::string();
	std::string sVoteData = ExtractXML(sContract, "<VOTEDATA>", "</VOTEDATA>");
	std::string sProposalHashes = GetPAMHashByContract(sContract).GetHex();
	if (!sHashes.empty())
		sProposalHashes = sHashes;
	std::string sType = "2"; // GSC Trigger is always 2
	std::string sQ = "\"";
	std::string sJson = "[[" + sQ + "trigger" + sQ + ",{";
	sJson += GJE("event_block_height", sEventBlockHeight, true, false); // Must be an int
	sJson += GJE("start_epoch", RoundToString(GetAdjustedTime(), 0), true, false);
	sJson += GJE("payment_addresses", sPaymentAddresses,  true, true);
	sJson += GJE("payment_amounts",   sPaymentAmounts,    true, true);
	sJson += GJE("proposal_hashes",   sProposalHashes,    true, true);
	if (!sVoteData.empty())
		sJson += GJE("vote_data", sVoteData, true, true);
	if (!sQTData.empty())
	{
		sJson += GJE("price", ExtractXML(sQTData, "<PRICE>", "</PRICE>"), true, true);
		sJson += GJE("qtphase", ExtractXML(sQTData, "<QTPHASE>", "</QTPHASE>"), true, true);
		sJson += GJE("btcprice", ExtractXML(sQTData,"<BTCPRICE>", "</BTCPRICE>"), true, true);
	}
	sJson += GJE("type", sType, false, false); 
	sJson += "}]]";
	LogPrintf("\nSerializeSanctuaryQuorumTrigger:Creating New Object %s ", sJson);
	std::vector<unsigned char> vchJson = std::vector<unsigned char>(sJson.begin(), sJson.end());
	std::string sHex = HexStr(vchJson.begin(), vchJson.end());
	return sHex;
}

bool ChainSynced(CBlockIndex* pindex)
{
	int64_t nAge = GetAdjustedTime() - pindex->GetBlockTime();
	return (nAge > (60 * 60)) ? false : true;
}

bool Included(std::string sFilterNickName, std::string sCPK)
{
	CPK oPrimary = GetCPKFromProject("cpk", sCPK);
	std::string sNickName = Caption(oPrimary.sNickName, 10);
	bool fIncluded = false;
	if (((sNickName == sFilterNickName || oPrimary.sNickName == sFilterNickName) && !sFilterNickName.empty()) || (sFilterNickName.empty()))
		fIncluded = true;
	return fIncluded;
}

UniValue GetProminenceLevels(int nHeight, std::string sFilterNickName)
{
	UniValue results(UniValue::VOBJ);
	if (nHeight == 0) 
		return NullUniValue;
      
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight);
	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;

	std::string sContract = GetGSCContract(nHeight, false);
	std::string sData = ExtractXML(sContract, "<DATA>", "</DATA>");
	std::string sDetails = ExtractXML(sContract, "<DETAILS>", "</DETAILS>");
	std::string sDiaries = ExtractXML(sContract, "<DIARIES>", "</DIARIES>");
	std::vector<std::string> vData = Split(sData.c_str(), "\n");
	std::vector<std::string> vDetails = Split(sDetails.c_str(), "\n");
	std::vector<std::string> vDiaries = Split(sDiaries.c_str(), "\n");
	results.push_back(Pair("Prominence v1.1", "Details"));
	std::string nMinDepth = ExtractXML(sContract, "<MINDEPTH>", "</MINDEPTH>");
	std::string nMaxDepth = ExtractXML(sContract, "<MAXDEPTH>", "</MAXDEPTH>");
	results.push_back(Pair("Block Range", nMinDepth + "-" + nMaxDepth));

	// DETAIL ROW FORMAT: sCampaignName + "|" + Members.Address + "|" + nPoints + "|" + nProminence + "|" + NickName + "|\n";
	std::string sMyCPK = DefaultRecAddress("Christian-Public-Key");

	for (int i = 0; i < vDetails.size(); i++)
	{
		std::vector<std::string> vRow = Split(vDetails[i].c_str(), "|");
		if (vRow.size() >= 4)
		{
			std::string sCampaignName = vRow[0];
			std::string sCPK = vRow[1];
			double nPoints = cdbl(vRow[2], 2);
			double nProminence = cdbl(vRow[3], 8) * 100;
			CPK oPrimary = GetCPKFromProject("cpk", sCPK);
			std::string sNickName = Caption(oPrimary.sNickName, 10);
			if (sNickName.empty())
				sNickName = "N/A";
			std::string sNarr = sCampaignName + ": " + sCPK + " [" + sNickName + "], Pts: " + RoundToString(nPoints, 2);
			if (Included(sFilterNickName, sCPK))
				results.push_back(Pair(sNarr, RoundToString(nProminence, 2) + "%"));
		}
	}
	if (vDiaries.size() > 0)
		results.push_back(Pair("Healing", "Diary Entries"));
	for (int i = 0; i < vDiaries.size(); i++)
	{
		std::vector<std::string> vRow = Split(vDiaries[i].c_str(), "|");
		if (vRow.size() >= 2)
		{
			std::string sCPK = vRow[0];
			if (Included(sFilterNickName, sCPK))
				results.push_back(Pair(Caption(vRow[1], 10), vRow[2]));
		}
	}

	double dTotalPaid = 0;
	// Allow room for a change in QT between first contract creation time and next superblock
	double nMaxContractPercentage = .98;
	results.push_back(Pair("Prominence", "Totals"));
	for (int i = 0; i < vData.size(); i++)
	{
		std::vector<std::string> vRow = Split(vData[i].c_str(), "|");
		if (vRow.size() >= 6)
		{
			std::string sCampaign = vRow[0];
			std::string sCPK = vRow[1];
			double nPoints = cdbl(vRow[2], 2);
			double nProminence = cdbl(vRow[3], 4) * 100;
			double nPayment = cdbl(vRow[5], 4);
			std::string sNickName = vRow[4];
			if (sNickName.empty())
				sNickName = "N/A";
			CAmount nOwed = nPaymentsLimit * (nProminence / 100) * nMaxContractPercentage;
			std::string sNarr = sCampaign + ": " + sCPK + " [" + Caption(sNickName, 10) + "]" + ", Pts: " + RoundToString(nPoints, 2) 
				+ ", Reward: " + RoundToString(nPayment, 3);
			if (Included(sFilterNickName, sCPK))
				results.push_back(Pair(sNarr, RoundToString(nProminence, 3) + "%"));
		}
	}

	return results;
}

void SendDistressSignal()
{
	static int64_t nLastReset = 0;
	if (GetAdjustedTime() - nLastReset > (60 * 60 * 1))
	{
		// Node will try to pull the gobjects again
		LogPrintf("SmartContract-Server::SendDistressSignal: Node is missing a gobject, retrying...%f\n", GetAdjustedTime());
		masternodeSync.Reset();
		masternodeSync.SwitchToNextAsset(*g_connman);
		nLastReset = GetAdjustedTime();
	}
}

std::string CheckLastQuorumPopularHash()
{
	if (!fProd)
		return "SUCCESS";
	std::string sURL = "https://" + GetSporkValue("pool");
	std::string sRestfulURL = "Action.aspx?action=gethash";
	std::string sResponse = BiblepayHTTPSPost(false, 0, "", "", "", sURL, sRestfulURL, 443, "", 25, 10000, 1);
	std::string sQuorumHash = ExtractXML(sResponse, "<hash>", "</hash>");
	int iBlock = (int)cdbl(ExtractXML(sResponse, "<blocks>", "</blocks>"), 0);
	// This is the hash of the top 100 sanctuaries for the block that occurred % 10:
	if (iBlock == 0 || sQuorumHash.empty() || sQuorumHash == "0")
	{
		return "SUCCESS";
	}
	// Test against local hash to see if we are in sync with the sanctuaries
	uint256 myHash = uint256();
	GetBlockHash(myHash, iBlock);
	std::string sMyLocalHash = myHash.GetHex();
	if (sMyLocalHash != sQuorumHash)
	{
		return "FAIL";
	}
	if (fDebug)
		LogPrintf("Localhash %s, qh %s ", sMyLocalHash, sQuorumHash);
	return "SUCCESS";
}

static int64_t nLastQuorumHashCheckup = 0;
std::string CheckGSCHealth()
{
	if (nLastQuorumHashCheckup == 0)
		nLastQuorumHashCheckup = GetAdjustedTime();

	double nCheckGSCOptionDisabled = GetSporkDouble("disablegschealthcheck", 0);
	if (nCheckGSCOptionDisabled == 1)
		return "DISABLED";

	bool bImpossible = (!masternodeSync.IsSynced() || fLiteMode);
	if (bImpossible)
		return "IMPOSSIBLE";

	int64_t nQHAge = GetAdjustedTime() - nLastQuorumHashCheckup;
	if (nQHAge > (60 * 60 * 4))
	{
		std::string QH = CheckLastQuorumPopularHash();
		nLastQuorumHashCheckup = GetAdjustedTime();
		if (QH == "FAIL")
		{
			double nReassessOption = cdbl(GetArg("-disablereassesschains", "0"), 0);
			if (nReassessOption == 0)
			{
				SendDistressSignal();
				ReassessAllChains();
			}
		}
	}

	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	std::string sAddresses;
	std::string sAmounts;
	int iVotes = 0;
	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPAMHash = uint256S("0x0");
	GetGSCGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
	uint256 hPam = GetPAMHash(sAddresses, sAmounts);
	std::string sContract = GetGSCContract(iLastSuperblock, true);
	uint256 hPAMHash2 = GetPAMHashByContract(sContract);
	int nBlocksLeft = iNextSuperblock - chainActive.Tip()->nHeight;
	if (nBlocksLeft < BLOCKS_PER_DAY / 2)
	{
		if (uGovObjHash == uint256S("0x0") || (hPAMHash2 != hPam))
		{
			SendDistressSignal();
			return "DISTRESS";
		}
	}
	return "HEALTHY";
}

bool VerifyChild(std::string childID)
{
	std::map<std::string, CPK> cp1 = GetChildMap("cpk|cameroon-one");
	std::string sMyCPK = DefaultRecAddress("Christian-Public-Key");
	for (std::pair<std::string, CPK> a : cp1)
	{
		std::string sChildID = a.second.sOptData;
		if (childID == sChildID)
			return true;
	}
	return false;
}

std::string ExecuteGenericSmartContractQuorumProcess()
{
	bool fHealthCheckup = (chainActive.Tip()->nHeight % 10 == 0);
	if (fHealthCheckup)
	{
		CheckGSCHealth();
	}

	if (!chainActive.Tip()) 
		return "INVALID_CHAIN";

	if (!ChainSynced(chainActive.Tip()))
		return "CHAIN_NOT_SYNCED";
	
	if (!fMasternodeMode)   
		return "NOT_A_SANCTUARY";

	double nMinGSCProtocolVersion = GetSporkDouble("MIN_GSC_PROTO_VERSION", 0);
	if (PROTOCOL_VERSION < nMinGSCProtocolVersion)
		return "GSC_PROTOCOL_REQUIRES_UPGRADE";

	bool fWatchmanQuorum = (chainActive.Tip()->nHeight % 10 == 0) && fMasternodeMode;
	if (fWatchmanQuorum)
	{
		std::string sContr;
		std::string sWatchman = WatchmanOnTheWall(false, sContr);
		if (fDebugSpam)
			LogPrintf("WatchmanOnTheWall::Status %s Contract %s", sWatchman, sContr);
		UpdateHealthInformation();
	}
	bool fStratisExport = (chainActive.Tip()->nHeight % BLOCKS_PER_DAY == 0) && fMasternodeMode;
	if (fStratisExport)
		DailyExport();

	// Goal 1: Be synchronized as a team after the warming period, but be cascading during the warming period
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	int nBlocksSinceLastEpoch = chainActive.Tip()->nHeight - iLastSuperblock;
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int WARMING_DURATION = consensusParams.nSuperblockCycle * .10;
	int nCascadeHeight = GetRandInt(chainActive.Tip()->nHeight);
	bool fWarmingPeriod = nBlocksSinceLastEpoch < WARMING_DURATION;
	int nQuorumAssessmentHeight = fWarmingPeriod ? nCascadeHeight : chainActive.Tip()->nHeight;

	int nCreateWindow = chainActive.Tip()->nHeight * .25;
	bool fPrivilegeToCreate = nCascadeHeight < nCreateWindow;

	if (!fProd)
		fPrivilegeToCreate = true;

	bool fQuorum = (nQuorumAssessmentHeight % 5 == 0);
	if (!fQuorum)
		return "NTFQ_";
	
	//  Check for Pending Contract
	int iVotes = 0;
	std::string sAddresses;
	std::string sAmounts;
	std::string sError;
	std::string sContract = GetGSCContract(0, true);
	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPamHash = GetPAMHashByContract(sContract);
	
	GetGSCGovObjByHeight(iNextSuperblock, uPamHash, iVotes, uGovObjHash, sAddresses, sAmounts);
	
	int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
	bool bPending = iVotes > iRequiredVotes;

	bool fFeatureOff = true;
	if (!fFeatureOff)
	{
		// R ANDREWS - We can remove this in the mandatory if our gobject syncing problem is resolved.
		// Regardless of having a pending superblock or not, let's forward our crown jewel to the network once every quorum - to ensure they have it
		if (uGovObjHash != uint256S("0x0"))
		{
			CGovernanceObject *myGov = governance.FindGovernanceObject(uGovObjHash);
			if (myGov)
			{
				myGov->Relay(*g_connman);
				LogPrintf(" Relaying crown jewel %s ", myGov->GetHash().GetHex());
			}
		}
	}

	if (bPending) 
	{
		if (fDebug)
			LogPrintf("\n ExecuteGenericSmartContractQuorum::We have a pending superblock at height %f \n", (double)iNextSuperblock);
		return "PENDING_SUPERBLOCK";
	}
	// R ANDREWS - BIBLEPAY - 4/2/2019
	// If we are > halfway into daily GSC deadline, and have not received the gobject, emit a distress signal
	int nBlocksLeft = iNextSuperblock - chainActive.Tip()->nHeight;
	if (nBlocksLeft < BLOCKS_PER_DAY / 2)
	{
		if (iVotes < iRequiredVotes || uGovObjHash == uint256S("0x0") || sAddresses.empty())
		{
			LogPrintf("\n ExecuteGenericSmartContractQuorum::DistressAlert!  Not enough votes %f for GSC %s!", 
				(double)iVotes, uGovObjHash.GetHex());
		}
	}

	if (uGovObjHash == uint256S("0x0") && fPrivilegeToCreate)
	{
		std::string sQuorumTrigger = SerializeSanctuaryQuorumTrigger(iLastSuperblock, iNextSuperblock, sContract);
		std::string sGobjectHash;
		SubmitGSCTrigger(sQuorumTrigger, sGobjectHash, sError);
		LogPrintf("**ExecuteGenericSmartContractQuorumProcess::CreatingGSCContract Hex %s , Gobject %s, results %s **\n", sQuorumTrigger.c_str(), sGobjectHash.c_str(), sError.c_str());
		return "CREATING_CONTRACT";
	}
	if (iVotes <= iRequiredVotes)
	{
		bool bResult = VoteForGSCContract(iNextSuperblock, sContract, sError);
		if (!bResult)
		{
			LogPrintf("\n**ExecuteGenericSmartContractQuorum::Unable to vote for GSC contract: Reason [%s] ", sError.c_str());
			return "UNABLE_TO_VOTE_FOR_GSC_CONTRACT";
		}
		else
		{
			LogPrintf("\n**ExecuteGenericSmartContractQuorum::Voted Successfully %f.", 1);
			return "VOTED_FOR_GSC_CONTRACT";
		}
	}
	else if (iVotes > iRequiredVotes)
	{
		LogPrintf(" ExecuteGenericSmartContractQuorum::GSC Contract %s has won.  Waiting for superblock. ", uGovObjHash.GetHex());
		return "PENDING_SUPERBLOCK";
	}

	return "NOT_A_CHOSEN_SANCTUARY";
}

