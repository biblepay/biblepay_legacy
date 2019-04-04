// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpog.h"
#include "rpcpodc.h"
#include "smartcontract-client.h"
#include "governance-validators.h"
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
	nCoinAge = GetVINCoinAge(pindex, tx);
	bool fSigned = CheckAntiBotNetSignature(tx, "gsc");
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
//////////////////////////////////////////////////////////////////////////////// Watchman-On-The-Wall /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//											BiblePay's version of The Sentinel, written by the BiblePay Devs (March 31st, 2019)                                                                                      //
//                                                                                                                                                                                                                   //

BiblePayProposal GetProposalByHash(uint256 govObj, int nLastSuperblock)
{
	int nSancCount = mnodeman.CountEnabled();
	int nMinPassing = nSancCount * .10;
	if (nMinPassing < 1) nMinPassing = 1;
	CGovernanceObject* myGov = governance.FindGovernanceObject(govObj);
	CProposalValidator validator(myGov->GetDataAsHexString());
	BiblePayProposal bbpProposal;
	validator.GetDataValue("name", bbpProposal.sName);
	validator.GetDataValue("start_epoch", bbpProposal.nStartEpoch);
	validator.GetDataValue("end_epoch", bbpProposal.nEndEpoch);
	validator.GetDataValue("url", bbpProposal.sURL);
	validator.GetDataValue("expensetype", bbpProposal.sExpenseType);
	validator.GetDataValue("payment_amount", bbpProposal.nAmount);
	validator.GetDataValue("payment_address", bbpProposal.sAddress);
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
	for (auto item : vProposalsInBudget)
    {
		BiblePayProposal p = GetProposalByHash(item.second, nLastSuperblock);
		CBitcoinAddress cbaAddress(p.sAddress);
		if (cbaAddress.IsValid() && p.nAmount > .01)
		{
			sAddresses += p.sAddress + "|";
			sPayments += RoundToString(p.nAmount, 2) + "|";
			sHashes += p.uHash.GetHex() + "|";
		}
	}
	if (sPayments.length() > 1) 
		sPayments = sPayments.substr(0, sPayments.length() - 1);
	if (sAddresses.length() > 1)
		sAddresses = sAddresses.substr(0, sAddresses.length() - 1);
	if (sHashes.length() > 1)
		sHashes = sHashes.substr(0, sHashes.length() - 1);
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
		+ uPamHash.GetHex() + "</PAMHASH><SANCTUARYCOUNT>" + RoundToString(nSancCount, 0) + "</SANCTUARYCOUNT></METRICS>";

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


std::string GetGSCContract(int nHeight)
{
	int nNextSuperblock = 0;
	int nLast = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, nNextSuperblock);
	if (nHeight != 0) 
		nLast = nHeight;
	std::string sContract = AssessBlocks(nLast);
	return sContract;
}

double CalculatePoints(std::string sCampaign, double nCoinAge, CAmount nDonation)
{
	boost::to_upper(sCampaign);
	double nPoints = 0;
	if (sCampaign == "POG")
	{
		double nComponent1 = nCoinAge;
		double nTithed = (double)nDonation / COIN;
		double nComponent2 = cbrt(nTithed);
		nPoints = nComponent1 * nComponent2;
		return nPoints;
	}
	return 0;
}

bool VoteForGobject(uint256 govobj, std::string sVoteOutcome, std::string& sError)
{
	vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal("funding");
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

bool NickNameExists(std::string sNickName)
{
	std::map<std::string, CPK> mAllCPKs = GetGSCMap("cpk", "", true);
	boost::to_upper(sNickName);
	for (std::pair<std::string, CPK> a : mAllCPKs)
	{
		if (boost::iequals(a.second.sNickName, sNickName))
			return true;
	}
	return false;
}

std::string AssessBlocks(int nHeight)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight);
	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;
	if (!chainActive.Tip()) 
		return "";
	if (nHeight > chainActive.Tip()->nHeight)
		nHeight = chainActive.Tip()->nHeight - 1;

	int nMaxDepth = nHeight;
	int nMinDepth = nMaxDepth - BLOCKS_PER_DAY;
	if (nMinDepth < 1) 
		return "";
	CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::map<std::string, CPK> mPoints;
	std::map<std::string, CPK> mAllCPKs = GetGSCMap("cpk", "", true);

	while (pindex && pindex->nHeight < nMaxDepth)
	{
		if (pindex->nHeight < chainActive.Tip()->nHeight) 
			pindex = chainActive.Next(pindex);
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int n = 0; n < block.vtx.size(); n++)
			{
				if (block.vtx[n]->IsGSCTransmission() && CheckAntiBotNetSignature(block.vtx[n], "gsc"))
				{
					std::string sCampaignName;
					std::string sCPK = GetTxCPK(block.vtx[n], sCampaignName);
					CPK localCPK = GetCPK(sCPK);

					double nCoinAge = 0;
					CAmount nDonation = 0;
					GetTransactionPoints(pindex, block.vtx[n], nCoinAge, nDonation);
					if (CheckCampaign(sCampaignName) && !sCPK.empty())
					{
						double nPoints = CalculatePoints(sCampaignName, nCoinAge, nDonation);
						if (nPoints > 0)
						{
							CPK c = mPoints[sCPK];
							c.nPoints += nPoints;
							c.sCampaign = sCampaignName;
							c.sAddress = sCPK;
							c.sNickName = localCPK.sNickName;
							mPoints[sCPK] = c;
							if (fDebugSpam)
								LogPrintf("\nUser %s , Points %f, Campaign %s, coinage %f, donation %f, usertotal %f ", c.sAddress, (double)nPoints, c.sCampaign, (double)nCoinAge, 
								(double)nDonation/COIN, (double)c.nPoints);
						}
					}
				}
			}
		}
	}

	// Tally the Team Total 
	double nTotalPoints = 0;
	for (auto pts : mPoints)
	{
		nTotalPoints += pts.second.nPoints;
	}
	LogPrintf("\nTotal Points %f ",(double)nTotalPoints);
	nTotalPoints++;

	// Convert Team totals to Prominence levels
	
	for (auto Members : mPoints)
	{
		mPoints[Members.second.sAddress].nProminence = Members.second.nPoints / nTotalPoints;
		LogPrintf("\nUser %s, Points %f, Prominence %f ", Members.second.sAddress, Members.second.nPoints, Members.second.nProminence);
	}
	// Create the daily contract
	std::string sData;
	std::string sAddresses;
	std::string sPayments;
	std::string sGenData;
	for (auto Members : mPoints)
	{
		CAmount nPayment = Members.second.nProminence * nPaymentsLimit;
		CBitcoinAddress cbaAddress(Members.second.sAddress);
		if (cbaAddress.IsValid() && nPayment > (.25*COIN))
		{
			sAddresses += Members.second.sAddress + "|";
			sPayments += RoundToString(nPayment/COIN, 2) + "|";
			std::string sRow = Members.second.sAddress + "|" + RoundToString(Members.second.nPoints, 0) + "|" + RoundToString(Members.second.nProminence, 4) + "|" + Members.second.sNickName + "|\n";
			sGenData += sRow;
		}
	}
	if (sPayments.length() > 1) 
		sPayments = sPayments.substr(0, sPayments.length() - 1);
	if (sAddresses.length() > 1)
		sAddresses = sAddresses.substr(0, sAddresses.length() - 1);
	
	sData = "<PAYMENTS>" + sPayments + "</PAYMENTS><ADDRESSES>" + sAddresses + "</ADDRESSES><DATA>" + sGenData + "</DATA><LIMIT>" + RoundToString(nPaymentsLimit/COIN, 4) + "</LIMIT><TOTALPOINTS>" + RoundToString(nTotalPoints, 2) + "</TOTALPOINTS>";

	return sData;
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

std::vector<std::pair<std::string, uint256>> GetGSCSortedByGov(int nHeight, uint256 inPamHash)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	std::vector<std::pair<std::string, uint256> > vPropByGov;
	vPropByGov.reserve(objs.size() + 1);
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
			uint256 uPamHash = GetPAMHash(sPAD, sPAM);
			if (inPamHash == uPamHash)
			{
				// Note:  the pair is used in case we want to store an object later (the PamHash is not distinct, but the govHash is).
				vPropByGov.push_back(std::make_pair(myGov->GetHash().GetHex(), myGov->GetHash()));
			}
		}
	}
	return vPropByGov;
}

bool VoteForGSCContract(int nHeight, std::string sMyContract, std::string& sError)
{
	int iPendingVotes = 0;
	uint256 uGovObjHash;
	std::string sPaymentAddresses;
	std::string sAmounts;
	uint256 uPamHash = GetPAMHashByContract(sMyContract);
	GetGSCGovObjByHeight(nHeight, uPamHash, iPendingVotes, uGovObjHash, sPaymentAddresses, sAmounts);
	// Verify Payment data matches our payment data, otherwise dont vote for it
	if (sPaymentAddresses.empty() || sAmounts.empty())
	{
		sError = "Unable to vote for GSC Contract::Foreign addresses or amounts empty.";
		return false;
	}
	// Sort by GSC gobject hash (creation time does not work as multiple nodes may be called during the same second to create a GSC)
	std::vector<std::pair<std::string, uint256>> vPropByGov = GetGSCSortedByGov(nHeight, uPamHash);
	// Now we need to sort the vector by Gov hash
	std::sort(vPropByGov.begin(), vPropByGov.end());

	for (int i = 0; i < vPropByGov.size(); i++)
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(vPropByGov[i].second);
		std::string sAction = (i==0) ? "yes" : "no";
		int iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
		LogPrintf("\nSmartContract-Server::VoteForGSCContract::Voting %s for govHash %s, with pre-existing-votes %f (created %f)",
			sAction, myGov->GetHash().GetHex(), iVotes, myGov->GetCreationTime());
		VoteForGobject(uGovObjHash, "yes", sError);
	}
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
	std::string sHashes = ExtractXML(sContract, "<PROPOSALS>", "</PROPOSALS>");
	bool bStatus = GetContractPaymentData(sContract, iContractAssessmentHeight, sPaymentAddresses, sPaymentAmounts);
	if (!bStatus) return "";

	std::string sProposalHashes = GetPAMHashByContract(sContract).GetHex();
	if (!sHashes.empty()) sProposalHashes = sHashes;

	std::string sType = "2"; // GSC Trigger is always 2
	std::string sQ = "\"";
	std::string sJson = "[[" + sQ + "trigger" + sQ + ",{";
	sJson += GJE("event_block_height", sEventBlockHeight, true, false); // Must be an int
	sJson += GJE("start_epoch", RoundToString(GetAdjustedTime(), 0), true, false);
	sJson += GJE("payment_addresses", sPaymentAddresses,  true, true);
	sJson += GJE("payment_amounts",   sPaymentAmounts,    true, true);
	sJson += GJE("proposal_hashes",   sProposalHashes,    true, true);
	/*
	// QT - Quantitative Tightening - R ANDREWS
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
	*/

	sJson += GJE("type", sType, false, false); 
	sJson += "}]]";
	LogPrintf(" Creating object %s ", sJson);
	std::vector<unsigned char> vchJson = std::vector<unsigned char>(sJson.begin(), sJson.end());
	std::string sHex = HexStr(vchJson.begin(), vchJson.end());
	return sHex;
}

bool ChainSynced(CBlockIndex* pindex)
{
	int64_t nAge = GetAdjustedTime() - pindex->GetBlockTime();
	return (nAge > (60 * 60)) ? false : true;
}

UniValue GetProminenceLevels(int nHeight)
{
	UniValue results(UniValue::VOBJ);
	if (nHeight == 0) 
		return NullUniValue;
      
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight);
	std::string sContract = GetGSCContract(nHeight);
	std::string sData = ExtractXML(sContract, "<DATA>", "</DATA>");
	std::vector<std::string> vData = Split(sData.c_str(), "\n");
	double dTotalPaid = 0;
	for (int i = 0; i < vData.size(); i++)
	{
		std::vector<std::string> vRow = Split(vData[i].c_str(), "|");
		if (vRow.size() >= 4)
		{
			std::string sCPK = vRow[0];
			double nPoints = cdbl(vRow[1], 2);
			double nProminence = cdbl(vRow[2], 4) * 100;
			CPK oPrimary = GetCPKFromProject("cpk", sCPK);
			std::string sNickName = oPrimary.sNickName;
			if (sNickName.empty())
				sNickName = "N/A";
			CAmount nOwed = nPaymentsLimit * (nProminence / 100) * .99;
			std::string sNarr = sCPK + " [" + sNickName + "]" + ", Pts: " + RoundToString(nPoints, 2) + ", Reward: " + RoundToString((double)nOwed / COIN, 2);
			results.push_back(Pair(sNarr, RoundToString(nProminence, 2) + "%"));
		}
	}

	return results;
}

void SendDistressSignal()
{
	static int64_t nLastReset = 0;
	if (GetAdjustedTime() - nLastReset > (60 * 30))
	{
		// Node will try to pull the gobjects again
		LogPrintf("SmartContract-Server::SendDistressSignal: Node is missing a gobject, pulling...%f\n", GetAdjustedTime());
		masternodeSync.Reset();
		masternodeSync.SwitchToNextAsset(*g_connman);
		nLastReset = GetAdjustedTime();
	}
}

void CheckGSCHealth()
{
	// This is for Non-Sancs
	bool bImpossible = (!masternodeSync.IsSynced() || fLiteMode);
	if (bImpossible)
		return;
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	std::string sAddresses;
	std::string sAmounts;
	int iVotes = 0;
	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPAMHash = uint256S("0x0");
	GetGSCGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
	int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
	int nBlocksLeft = iNextSuperblock - chainActive.Tip()->nHeight;
	if (nBlocksLeft < BLOCKS_PER_DAY / 2)
	{
		if (uGovObjHash == uint256S("0x0"))
			SendDistressSignal();
	}
}

bool CheckForValidGSC(int nHeight)
{
	if (!CSuperblock::IsSmartContract(nHeight))
		return false;

	// Reconstruct contract
	std::string sContract = GetGSCContract(nHeight);
	std::string sPayments = ExtractXML(sContract, "<PAYMENTS>", "</PAYMENTS>");
	if (!sPayments.empty())
	{
		LogPrintf("SmartContract-Server::CheckForValidGSC::Contains Payments %s, Node contains empty GSC govobj", sPayments);
		// At this point, we can not assume our local contract is the correct one, but we can assume that our nodes gobjects need synced, and it cannot refuse the superblock (we don't have enough data to make that call which would result in a fork)
		// Trigger a resync
		masternodeSync.Reset();
		masternodeSync.SwitchToNextAsset(*g_connman);
		return true;
	}
	// We don't see a superblock at this height... 
	return false;
}


std::string ExecuteGenericSmartContractQuorumProcess()
{
	if (!chainActive.Tip()) 
		return "INVALID_CHAIN";

	if (!ChainSynced(chainActive.Tip()))
		return "CHAIN_NOT_SYNCED";
	
	if (!fMasternodeMode)   
		return "NOT_A_SANCTUARY";

	bool fWatchmanQuorum = (chainActive.Tip()->nHeight % 10 == 0) && fMasternodeMode;
	if (fWatchmanQuorum)
	{
		std::string sContr;
		std::string sWatchman = WatchmanOnTheWall(false, sContr);
		if (fDebugSpam)
			LogPrintf("WatchmanOnTheWall::Status %s Contract %s", sWatchman, sContr);
	}
	// Goal 1: Be synchronized as a team after the warming period, but be cascading during the warming period
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	int nBlocksSinceLastEpoch = chainActive.Tip()->nHeight - iLastSuperblock;
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int WARMING_DURATION = consensusParams.nSuperblockCycle * .10;
	int nCascadeHeight = GetRandInt(chainActive.Tip()->nHeight);
	bool fWarmingPeriod = nBlocksSinceLastEpoch < WARMING_DURATION;
	int nQuorumAssessmentHeight = fWarmingPeriod ? nCascadeHeight : chainActive.Tip()->nHeight;
	bool fQuorum = (nQuorumAssessmentHeight % 5 == 0);
	if (!fQuorum)
		return "NTFQ_";
	
	//  Check for Pending Contract
	int iVotes = 0;
	std::string sAddresses;
	std::string sAmounts;
	std::string sError;
	std::string sContract = GetGSCContract(0);
	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPamHash = GetPAMHashByContract(sContract);
	
	GetGSCGovObjByHeight(iNextSuperblock, uPamHash, iVotes, uGovObjHash, sAddresses, sAmounts);
	
	int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
	bool bPending = iVotes > iRequiredVotes;
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

	if (uGovObjHash == uint256S("0x0"))
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

