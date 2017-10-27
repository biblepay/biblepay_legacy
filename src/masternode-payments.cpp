// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "darksend.h"
#include "governance-classes.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CMasternodePayments mnpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapMasternodeBlocks;
CCriticalSection cs_mapMasternodePaymentVotes;

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In Biblepay some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

std::string RoundToString(double d, int place);

CAmount GetRetirementAccountContributionAmount(int nPrevHeight);
extern CAmount GetTxSanctuaryCollateral(const CTransaction& txNew);

extern CAmount GetScriptSanctuaryCollateral(const CTransaction& txNew);
extern CAmount GetSanctuaryCollateral(CTxIn vin);



CAmount GetScriptSanctuaryCollateral(const CTransaction& txCollateral)
{

		std::string sXML = "";
		BOOST_REVERSE_FOREACH(CTxOut txout, txCollateral.vout) 
		{
			sXML += txout.sTxOutMessage;
		}
		uint256 hash = uint256S("0x" + ExtractXML(sXML,"<HASH>","</HASH>"));
		int N = cdbl(ExtractXML(sXML,"<N>","</N>"),0);
		CCoins coins;
		COutPoint templeOutpoint = COutPoint(hash, N);
          
		if(!pcoinsTip->GetCoins(hash, coins) || (unsigned int)N >= coins.vout.size() || coins.vout[N].IsNull()) 
		{
			if (fDebugMaster && false) LogPrintf("GetScriptSanctuaryCollateral::Unable to retrieve Masternode UTXO, hash=%s %s\n", 
				hash.GetHex().c_str(), templeOutpoint.ToStringShort().c_str());
			return false;
		}

		CAmount Collateral = coins.vout[N].nValue;
		// Ensure the address matches also
		BOOST_REVERSE_FOREACH(CTxOut txout, txCollateral.vout) 
		{
			if (txout.nValue == (TEMPLE_COLLATERAL * COIN))
			{
				// Ensure payee scriptSig matches collateral scriptSig
				bool bScriptedAddress = (coins.vout[N].scriptPubKey == txout.scriptPubKey);
				if (bScriptedAddress && (Collateral == TEMPLE_COLLATERAL * COIN))
				{
					return Collateral;
				}
				else
				{
					CTxDestination address1;
				    ExtractDestination(txout.scriptPubKey, address1);
		            CBitcoinAddress cbAddress1(address1);
					LogPrintf("Unable to find sanctuary address %s ",cbAddress1.ToString().c_str());
					return 0;
				}
			}
		}
		return 0;		
}

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet)
{
    strErrorRet = "";
	CAmount nMaxFees = 100 * COIN;

    bool isBlockRewardValueMet = (block.vtx[0].GetValueOut() <= blockReward);
    if(!isBlockRewardValueMet && fDebugMaster) LogPrintf("block.vtx[0].GetValueOut() %lld <= blockReward %lld\n", block.vtx[0].GetValueOut(), blockReward);

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.nSuperblockStartBlock) 
	{
		/* This is for Budgeting, not used by BiblePay
        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
        if(nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
            // NOTE: make sure SPORK_13_OLD_SUPERBLOCK_FLAG is disabled when 12.1 starts to go live
            if(masternodeSync.IsSynced() && !sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) 
			{
                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
                LogPrint("gobject", "IsBlockValueValid -- Client synced but budget spork is disabled, checking block value against block reward\n");
                if(!isBlockRewardValueMet) 
				{
					std::string sNarr = "coinbase pays too much at height " + RoundToString(nBlockHeight,0) 
						+ " (actual=" + RoundToString(block.vtx[0].GetValueOut(),0) + " vs limit=" 
						+ RoundToString(blockReward,0) + ", exceeded block reward, budgets are disabled";
					LogPrintf(sNarr.c_str());

                    strErrorRet = sNarr;
                }
                return isBlockRewardValueMet;
            }
            LogPrint("gobject", "IsBlockValueValid -- WARNING: Skipping budget block value checks, accepting block\n");
            return true;
        }
        // LogPrint("gobject", "IsBlockValueValid -- Block is not in budget cycle window, checking block value against block reward\n");
		*/
		// This section is before superblocks went live between Sep 2017 and Christmas 2017 - ensure each block is between 5000-20000:
        if(!isBlockRewardValueMet) 
		{
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in budget cycle window",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // superblocks started

    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
    bool isSuperblockMaxValueMet = (block.vtx[0].GetValueOut() <= nSuperblockMaxValue);

    LogPrint("gobject", "block.vtx[0].GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0].GetValueOut(), nSuperblockMaxValue);
	
    if(!masternodeSync.IsSynced()) 
	{
        // not enough data but at least it must NOT exceed superblock max value
        if(CSuperblock::IsValidBlockHeight(nBlockHeight)) 
		{
            if(fDebugMaster) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, checking superblock max bounds only\n");
            if(!isSuperblockMaxValueMet) {
                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
                                        nBlockHeight, block.vtx[0].GetValueOut(), nSuperblockMaxValue);
				LogPrintf(strErrorRet.c_str());
            }
            return isSuperblockMaxValueMet;
        }
		// This is Not a superblock: If we are Not synced, and this is a temple payment, find the temples large sum of collateral and verify the payment amount

		CAmount nTotalSubsidy = block.vtx[0].GetValueOut();
		CAmount nCollateral = GetScriptSanctuaryCollateral(block.vtx[0]);
		CAmount nSanctuaryPayment = GetMasternodePayment(nBlockHeight, block.vtx[0].GetValueOut(), nCollateral);
		if (nCollateral == (TEMPLE_COLLATERAL * COIN))
		{
			isBlockRewardValueMet = (nTotalSubsidy <= (blockReward + nSanctuaryPayment));
			LogPrintf(" Temple Collateral Found %f,  But block pays too much (TotalSubsidy > blockReward+nSanctuaryPayment) %f", (double)nCollateral, 
				(double)block.vtx[0].GetValueOut());
		}
		if (nTotalSubsidy > (MAX_BLOCK_SUBSIDY * COIN) && (nCollateral < (TEMPLE_COLLATERAL * COIN)))
		{
			// Should never happen.  This means block pays > Block Max and no template collateral was found
			LogPrintf(" Template collateral not found, and block pays %f while block only pays %f",(double)nTotalSubsidy/COIN, (double)blockReward/COIN);
			return false;
		}
		if (nTotalSubsidy > (MAX_BLOCK_SUBSIDY * COIN) && (nCollateral == (TEMPLE_COLLATERAL * COIN)))
		{
			// In this case, the temple collateral was found, ensure the appropriate distribution percentages are met
			CAmount MinerReward = block.vtx[0].vout[0].nValue;
			if (block.vtx[0].vout.size() > 2) MinerReward += block.vtx[0].vout[1].nValue;
			CAmount TempleReward = block.vtx[0].GetValueOut() - MinerReward;
			if (TempleReward > ( ((blockReward + nMaxFees) * 10) ))
			{
				LogPrintf(" Temple reward %f pays more than 10* Block reward %f ", (double)TempleReward/COIN, (double)blockReward/COIN);
				return false;
			}
			if (MinerReward > ( ((blockReward + nMaxFees))))
			{
				LogPrintf(" Miner reward portion of Temple distribution schedule pays more than actual block %f ", (double)MinerReward/COIN);
				return false;
			}
		}

		// End of Temple Subsidy handling
        if(!isBlockRewardValueMet) 
		{
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }

    // we are synced, let's try to check as much data as we can
	bool fSuperblocksEnabled = (nBlockHeight >= consensusParams.nSuperblockStartBlock) && fMasternodesEnabled;

    if(fSuperblocksEnabled) 
	{
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) 
		{
            if(CSuperblockManager::IsValid(block.vtx[0], nBlockHeight, blockReward)) 
			{
                LogPrint("gobject", "IsBlockValueValid -- Valid superblock at height %d: %s", nBlockHeight, block.vtx[0].ToString());
                // all checks are done in CSuperblock::IsValid, nothing to do here
                return true;
            }

            // triggered but invalid? that's weird
            LogPrintf("IsBlockValueValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, block.vtx[0].ToString());
            // should NOT allow invalid superblocks, when superblocks are enabled
            strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
            return false;
        }
		// Superblocks are enabled, this is not a superblock
        LogPrint("gobject", "IsBlockValueValid -- No triggered superblock detected at height %d\n", nBlockHeight);
		// If this is a Temple, verify the reward
		CAmount nTotalSubsidy = block.vtx[0].GetValueOut();
		CAmount nCollateral = GetScriptSanctuaryCollateral(block.vtx[0]);
		CAmount nSanctuaryPayment = GetMasternodePayment(nBlockHeight, block.vtx[0].GetValueOut(), nCollateral);
		CAmount MinerReward = block.vtx[0].vout[0].nValue;
		if (block.vtx[0].vout.size() > 2) MinerReward += block.vtx[0].vout[1].nValue;
		CAmount TempleReward = block.vtx[0].GetValueOut() - MinerReward;
		
		if (nCollateral == (TEMPLE_COLLATERAL * COIN))
		{
			isBlockRewardValueMet = (nTotalSubsidy <= (blockReward + nSanctuaryPayment));
			if (!isBlockRewardValueMet) LogPrintf(" Temple reward %f pays more than 10* Block reward %f ", (double)TempleReward/COIN, (double)blockReward/COIN);
			return false;
		
			if (TempleReward > ( ((blockReward + nMaxFees) * 10) ))
			{
				LogPrintf(" Temple reward %f pays more than 10* Block reward %f ", (double)TempleReward/COIN, (double)blockReward/COIN);
				return false;
			}
		}
		
		if (MinerReward > ( ((blockReward + nMaxFees))))
		{
			LogPrintf(" Miner reward portion of Temple distribution schedule pays more than actual block %f ", (double)MinerReward/COIN);
			return false;
		}

        if(!isBlockRewardValueMet) 
		{
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
                                    nBlockHeight, blockReward + nSanctuaryPayment, nTotalSubsidy);
			return false;
        }
    } 
	else 
	{
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
        if(!isBlockRewardValueMet) 
		{
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
    }

    // it MUST be a regular block
    return isBlockRewardValueMet;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{

	const Consensus::Params& consensusParams = Params().GetConsensus();

	if (nBlockHeight < consensusParams.nMasternodePaymentsStartBlock) return true;


    if(!masternodeSync.IsSynced()) 
	{
        //there is no budget data to use to check anything, let's just accept the longest chain
        if(fDebugMaster) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    // we are still using budgets, but we have no data about them anymore,
    // we can only check masternode payments

    
    if(nBlockHeight < consensusParams.nSuperblockStartBlock) 
	{
        if(mnpayments.IsTransactionValid(txNew, nBlockHeight)) {
            if (fDebugMaster) LogPrint("mnpayments", "IsBlockPayeeValid -- Valid masternode payment at height %d: %s", nBlockHeight, txNew.ToString());
            return true;
        }

		/* This is for Budgeting - Not used by BiblePay
        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
        if(nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) 
		{
            if(!sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
                if (fDebugMaster) LogPrint("gobject", "IsBlockPayeeValid -- ERROR: Client synced but budget spork is disabled and masternode payment is invalid\n");
                return false;
            }
            // NOTE: this should never happen in real, SPORK_13_OLD_SUPERBLOCK_FLAG MUST be disabled when 12.1 starts to go live
            LogPrint("gobject", "IsBlockPayeeValid -- WARNING: Probably valid budget block, have no data, accepting\n");
            return true;
        }

		*/
		bool bSpork8 = true;
        if(fMasternodesEnabled && bSpork8) 
		{
            LogPrintf("IsBlockPayeeValid -- ERROR: Invalid masternode payment detected at height %d: %s", nBlockHeight, txNew.ToString());
            return false;
        }
	
        if (fDebugMaster) LogPrintf("IsBlockPayeeValid -- WARNING: Masternode payment enforcement is disabled, accepting any payee\n");
        return true;
    }

    // superblocks started
    // SEE IF THIS IS A VALID SUPERBLOCK
	bool fSuperblocksEnabled = (nBlockHeight >= consensusParams.nSuperblockStartBlock) && fMasternodesEnabled;

    if(fSuperblocksEnabled) 
	{
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            if(CSuperblockManager::IsValid(txNew, nBlockHeight, blockReward)) {
                LogPrint("gobject", "IsBlockPayeeValid -- Valid superblock at height %d: %s", nBlockHeight, txNew.ToString());
                return true;
            }

            LogPrintf("IsBlockPayeeValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, txNew.ToString());
            // should NOT allow such superblocks, when superblocks are enabled
            return false;
        }
        // continue validation, should pay MN
        LogPrint("gobject", "IsBlockPayeeValid -- No triggered superblock detected at height %d\n", nBlockHeight);
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockPayeeValid -- Superblocks are disabled, no superblocks allowed\n");
    }

    // IF THIS ISN'T A SUPERBLOCK OR SUPERBLOCK IS INVALID, IT SHOULD PAY A MASTERNODE DIRECTLY
    if(mnpayments.IsTransactionValid(txNew, nBlockHeight)) {
        LogPrint("mnpayments", "IsBlockPayeeValid -- Valid masternode payment at height %d: %s", nBlockHeight, txNew.ToString());
        return true;
    }
	bool bSpork8 = true;
	if(bSpork8 && fMasternodesEnabled) 
	{
        LogPrintf("IsBlockPayeeValid -- ERROR: Invalid masternode payment detected at height %d: %s", nBlockHeight, txNew.ToString());
        return false;
    }

    LogPrintf("IsBlockPayeeValid -- WARNING: Masternode payment enforcement is disabled, accepting any payee\n");
    return true;
}

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet, std::vector<CTxOut>& voutSuperblockRet)
{
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
	
    const Consensus::Params& consensusParams = Params().GetConsensus();
	bool fSuperblocksEnabled = nBlockHeight >= consensusParams.nSuperblockStartBlock && fMasternodesEnabled;

    if(fSuperblocksEnabled &&
        CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) 
	{
            if (fDebugMaster) LogPrint("gobject", "FillBlockPayments -- triggered superblock creation at height %d\n", nBlockHeight);
            CSuperblockManager::CreateSuperblock(txNew, nBlockHeight, voutSuperblockRet);
            return;
    }

	if (fSuperblocksEnabled)
	{
		// FILL BLOCK PAYEE WITH MASTERNODE PAYMENT OTHERWISE
		mnpayments.FillBlockPayee(txNew, nBlockHeight, blockReward, txoutMasternodeRet);
		if (fDebug10) LogPrintf("FillBlockPayments -- nBlockHeight %d blockReward %f txoutMasternodeRet %s txNew %s",     
			nBlockHeight, (double)blockReward, txoutMasternodeRet.ToString(), txNew.ToString());
	}
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    // IF WE HAVE A ACTIVATED TRIGGER FOR THIS HEIGHT - IT IS A SUPERBLOCK, GET THE REQUIRED PAYEES
    if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        return CSuperblockManager::GetRequiredPaymentsString(nBlockHeight);
    }

    // OTHERWISE, PAY MASTERNODE
    return mnpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CMasternodePayments::Clear()
{
    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePaymentVotes);
    mapMasternodeBlocks.clear();
    mapMasternodePaymentVotes.clear();
}

bool CMasternodePayments::CanVote(COutPoint outMasternode, int nBlockHeight)
{
    LOCK(cs_mapMasternodePaymentVotes);

    if (mapMasternodesLastVote.count(outMasternode) && mapMasternodesLastVote[outMasternode] == nBlockHeight) {
        return false;
    }

    //record this masternode voted
    mapMasternodesLastVote[outMasternode] = nBlockHeight;
    return true;
}

/**
*   FillBlockPayee
*
*   Fill Masternode ONLY payment block
*/

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet)
{
	if (fRetirementAccountsEnabled)
	{
		// Award colored coins for retirement
        CAmount retirementAmount = GetRetirementAccountContributionAmount(nBlockHeight);
		// Ensure retirement emission per block is capped at a max of 10% of miner block subsidy
		if (retirementAmount > (txNew.vout[0].nValue * .10)) retirementAmount = txNew.vout[0].nValue * .10;
		txNew.vout.resize(txNew.vout.size()+1);
	    txNew.vout[0].nValue -= retirementAmount;
	    txNew.vout[txNew.vout.size()-1].scriptPubKey = txNew.vout[0].scriptPubKey;
		txNew.vout[txNew.vout.size()-1].nValue = retirementAmount;
		CComplexTransaction cct(txNew);
		std::string sAssetColorScript = cct.GetScriptForAssetColor("401"); // Get the script for 401k coins
		txNew.vout[txNew.vout.size()-1].sTxOutMessage = sAssetColorScript;
	}

    txoutMasternodeRet = CTxOut();
    CScript payee;
	CAmount nCollateral = 0;
	std::string sCollateralScript = "";
    if(!mnpayments.GetBlockPayeeAndCollateral(nBlockHeight, payee, nCollateral, sCollateralScript)) 
	{
        // no masternode detected...
        int nCount = 0;
        CMasternode *winningNode = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount);
        if(!winningNode) {
            // ...and we can't calculate it on our own
            if (fDebugMaster) LogPrintf("CMasternodePayments::FillBlockPayee -- Failed to detect masternode to pay\n");
            return;
        }
        // fill payee with locally calculated winner and hope for the best
        payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
    }

    // GET MASTERNODE PAYMENT VARIABLES SETUP
    CAmount masternodePayment = GetMasternodePayment(nBlockHeight, blockReward, nCollateral);
    // split reward between miner ...
	if (masternodePayment > txNew.vout[0].nValue)
	{
		// For Temples, we must first increase the block subsidy, then remove it, to satisfy all security conditions in ConnectBlock
		txNew.vout[0].nValue += masternodePayment;
	
	}
    txNew.vout[0].nValue -= masternodePayment;
    // ... and masternode
    txoutMasternodeRet = CTxOut(masternodePayment, payee);
	
    txNew.vout.push_back(txoutMasternodeRet);
	if (nCollateral == (TEMPLE_COLLATERAL * COIN))
	{
		// For Temples, add the Collateral Script
		txoutMasternodeRet.sTxOutMessage += sCollateralScript;
	}

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);
    LogPrintf("CMasternodePayments::FillBlockPayee -- Masternode payment %f to %s\n", (double)masternodePayment, address2.ToString().c_str());
}






int CMasternodePayments::GetMinMasternodePaymentsProto() {
    return sporkManager.IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES)
            ? MIN_MASTERNODE_PAYMENT_PROTO_VERSION_2
            : MIN_MASTERNODE_PAYMENT_PROTO_VERSION_1;
}

void CMasternodePayments::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // Ignore any payments messages until masternode list is synced
    if(!masternodeSync.IsMasternodeListSynced()) return;

    if(fLiteMode) return; // disable all Biblepay specific functionality

    if (strCommand == NetMsgType::MASTERNODEPAYMENTSYNC) { //Masternode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masternodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

		if (fMasternodesEnabled)
		{
			if(netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC)) 
			{
				// Asking for the payments list multiple times in a short period of time is no good
				LogPrintf("MASTERNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
				Misbehaving(pfrom->GetId(), 15);
				return;
			}
		}

        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC);

        Sync(pfrom);
        LogPrintf("MASTERNODEPAYMENTSYNC -- Sent Masternode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MASTERNODEPAYMENTVOTE) { // Masternode Payments Vote for the Winner

        CMasternodePaymentVote vote;
        vRecv >> vote;

        if(pfrom->nVersion < GetMinMasternodePaymentsProto()) return;

        if(!pCurrentBlockIndex) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        {
            LOCK(cs_mapMasternodePaymentVotes);
            if(mapMasternodePaymentVotes.count(nHash)) {
                LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), pCurrentBlockIndex->nHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapMasternodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapMasternodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = pCurrentBlockIndex->nHeight - GetStorageLimit();
        if(vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > pCurrentBlockIndex->nHeight+20) {
            LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, pCurrentBlockIndex->nHeight);
            return;
        }

        std::string strError = "";
        if(!vote.IsValid(pfrom, pCurrentBlockIndex->nHeight, strError)) {
            LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if(!CanVote(vote.vinMasternode.prevout, vote.nBlockHeight)) {
            LogPrintf("MASTERNODEPAYMENTVOTE -- masternode already voted, masternode=%s\n", vote.vinMasternode.prevout.ToStringShort());
            return;
        }

        masternode_info_t mnInfo = mnodeman.GetMasternodeInfo(vote.vinMasternode);
        if(!mnInfo.fInfoValid) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("MASTERNODEPAYMENTVOTE -- masternode is missing %s\n", vote.vinMasternode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinMasternode);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeyMasternode, pCurrentBlockIndex->nHeight, nDos)) {
            if(nDos) {
                LogPrintf("MASTERNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinMasternode);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s\n", address2.ToString(), vote.nBlockHeight, pCurrentBlockIndex->nHeight, 
			vote.vinMasternode.prevout.ToStringShort());

        if(AddPaymentVote(vote)){
            vote.Relay();
            masternodeSync.AddedPaymentVote();
        }
    }
}

bool CMasternodePaymentVote::Sign()
{
    std::string strError;
    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    if(!darkSendSigner.SignMessage(strMessage, vchSig, activeMasternode.keyMasternode)) {
        LogPrintf("CMasternodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(activeMasternode.pubKeyMasternode, vchSig, strMessage, strError)) {
        LogPrintf("CMasternodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}



bool CMasternodePayments::GetBlockPayeeAndCollateral(int nBlockHeight, CScript& payee, CAmount& nCollateral, std::string& sScript)
{
    if(mapMasternodeBlocks.count(nBlockHeight))
	{
        mapMasternodeBlocks[nBlockHeight].GetBestPayee(payee, nCollateral, sScript);
		return true;
    }

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CMasternodePayments::IsScheduled(CMasternode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if(!pCurrentBlockIndex) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for(int64_t h = pCurrentBlockIndex->nHeight; h <= pCurrentBlockIndex->nHeight + 8; h++)
	{
        if(h == nNotBlockHeight) continue;
		CAmount Collateral = 0;
		std::string sScript = "";
        if(mapMasternodeBlocks.count(h) && mapMasternodeBlocks[h].GetBestPayee(payee,Collateral,sScript) && mnpayee == payee) 
		{
            return true;
        }
    }

    return false;
}

bool CMasternodePayments::AddPaymentVote(const CMasternodePaymentVote& vote)
{
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    if(HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePaymentVotes);

    mapMasternodePaymentVotes[vote.GetHash()] = vote;

    if(!mapMasternodeBlocks.count(vote.nBlockHeight)) {
       CMasternodeBlockPayees blockPayees(vote.nBlockHeight);
       mapMasternodeBlocks[vote.nBlockHeight] = blockPayees;
    }

    mapMasternodeBlocks[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CMasternodePayments::HasVerifiedPaymentVote(uint256 hashIn)
{
    LOCK(cs_mapMasternodePaymentVotes);
    std::map<uint256, CMasternodePaymentVote>::iterator it = mapMasternodePaymentVotes.find(hashIn);
    return it != mapMasternodePaymentVotes.end() && it->second.IsVerified();
}


CAmount GetSanctuaryCollateral(CTxIn vin)
{
	CCoins coins;
	if(!pcoinsTip->GetCoins(vin.prevout.hash, coins) 
	|| (unsigned int)vin.prevout.n>=coins.vout.size() 
	|| coins.vout[vin.prevout.n].IsNull()) 
	{
         LogPrintf("GetSanctuaryCollateral::Unable to retrieve Masternode UTXO, masternode=%s\n", vin.prevout.ToStringShort());
         return 0;
    }
	return coins.vout[vin.prevout.n].nValue;
}



void CMasternodeBlockPayees::AddPayee(const CMasternodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) 
	{
        if (payee.GetPayee() == vote.payee) 
		{
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CMasternodePayee payeeNew(vote.payee, vote.GetHash(), vote.vinMasternode);
	// BiblePay 10-26-2017 - Track Sanctuary VIN on voted payees
	// Verify collateral

	CAmount nCollateral = GetSanctuaryCollateral(vote.vinMasternode);
	LogPrintf(" Sanctuary AddPayee VIN %s collateral %f txid %s %f\n", vote.vinMasternode.prevout.ToStringShort(), 
		nCollateral, vote.vinMasternode.prevout.hash.GetHex().c_str(), vote.vinMasternode.prevout.n);
      
    vecPayees.push_back(payeeNew);
}



std::string GetTempleCollateralScript(CMasternodePayee& payee)
{

	CAmount caCollateral = GetSanctuaryCollateral(payee.GetVin());
	if (caCollateral == (TEMPLE_COLLATERAL * COIN))
	{
				std::string sCollateral = "<COLLATERAL><HASH>" + payee.GetVin().prevout.hash.GetHex() + "</HASH><N>" 
						+ RoundToString((int)payee.GetVin().prevout.n,0) + "</N><AMOUNT>" 
						+ RoundToString(caCollateral/COIN,4) + "</AMOUNT></COLLATERAL>";
					return sCollateral;
	}
    return "";

}



bool CMasternodeBlockPayees::GetBestPayee(CScript& payeeRet, CAmount& nCollateral, std::string& sScript)
{
    LOCK(cs_vecPayees);

    if(!vecPayees.size()) 
	{
        LogPrint("mnpayments", "CMasternodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) 
	{
        if (payee.GetVoteCount() > nVotes) 
		{
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
			nCollateral = GetSanctuaryCollateral(payee.GetVin());
			sScript = GetTempleCollateralScript(payee);
        }
    }

    return (nVotes > -1);
}

bool CMasternodeBlockPayees::HasPayeeWithVotes(CScript payeeIn, int nVotesReq)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("mnpayments", "CMasternodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}


bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

	CAmount nCollateral = GetTxSanctuaryCollateral(txNew);
    CAmount nMasternodePayment = GetMasternodePayment(nBlockHeight, txNew.GetValueOut(), nCollateral);

    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) 
	{
        if (payee.GetVoteCount() >= nMaxSignatures) 
		{
            nMaxSignatures = payee.GetVoteCount();
        }
    }

    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) 
	{
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) 
		{
            BOOST_FOREACH(CTxOut txout, txNew.vout) 
			{
                if (payee.GetPayee() == txout.scriptPubKey && nMasternodePayment == txout.nValue) 
				{
                    LogPrint("mnpayments", "CMasternodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == "") 
			{
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f biblepay\n", strPayeesPossible, (float)nMasternodePayment/COIN);
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}


CAmount CMasternodeBlockPayees::GetTxSanctuaryCollateral(const CTransaction& txNew)
{
	BOOST_REVERSE_FOREACH(CMasternodePayee& payee, vecPayees) 
	{
		BOOST_REVERSE_FOREACH(CTxOut txout, txNew.vout) 
		{
		    if (payee.GetPayee() == txout.scriptPubKey) 
			{
				CAmount caCollateral = GetSanctuaryCollateral(payee.GetVin());
				LogPrintf(" collateral 10262017 %f ",caCollateral);
				return caCollateral;
            }
        }
	}

	return 0;
}


std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if(mapMasternodeBlocks.count(nBlockHeight)){
        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if(mapMasternodeBlocks.count(nBlockHeight)){
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CMasternodePayments::CheckAndRemove()
{
    if(!pCurrentBlockIndex) return;

    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CMasternodePaymentVote>::iterator it = mapMasternodePaymentVotes.begin();
    while(it != mapMasternodePaymentVotes.end()) {
        CMasternodePaymentVote vote = (*it).second;

        if(pCurrentBlockIndex->nHeight - vote.nBlockHeight > nLimit) 
		{
            if (fDebugMaster) LogPrint("mnpayments", "CMasternodePayments::CheckAndRemove -- Removing old Masternode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapMasternodePaymentVotes.erase(it++);
            mapMasternodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
	if (fDebugMaster) LogPrintf("CMasternodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CMasternodePaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError)
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if(!pmn) {
        strError = strprintf("Unknown Masternode: prevout=%s", vinMasternode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Masternode
        if(masternodeSync.IsMasternodeListSynced()) {
            mnodeman.AskForMN(pnode, vinMasternode);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if(nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_MASTERNODE_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = mnpayments.GetMinMasternodePaymentsProto();
    } else {
        // allow non-updated masternodes for old blocks
        nMinRequiredProtocol = MIN_MASTERNODE_PAYMENT_PROTO_VERSION_1;
    }

    if(pmn->nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Masternode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", pmn->nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only masternodes should try to check masternode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify masternode rank for future block votes only.
    if(!fMasterNode && nBlockHeight < nValidationHeight) return true;

    int nRank = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 101, nMinRequiredProtocol, false);

    if(nRank == -1) {
        LogPrint("mnpayments", "CMasternodePaymentVote::IsValid -- Can't calculate rank for masternode %s\n",
                    vinMasternode.prevout.ToStringShort());
        return false;
    }

    if(nRank > MNPAYMENTS_SIGNATURES_TOTAL) 
	{
        // It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Masternode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if(nRank > MNPAYMENTS_SIGNATURES_TOTAL*2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("Masternode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL*2, nRank);
            LogPrintf("CMasternodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 25);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight)
{
    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE
    if(fLiteMode || !fMasterNode) return false;
	const Consensus::Params& consensusParams = Params().GetConsensus();

	if (nBlockHeight < consensusParams.nMasternodePaymentsStartBlock) return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about masternodes.
    if(!masternodeSync.IsMasternodeListSynced()) return false;

    int nRank = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight - 101, GetMinMasternodePaymentsProto(), false);

    if (nRank == -1) {
        if (fDebugMaster) LogPrint("mnpayments", "CMasternodePayments::ProcessBlock -- Unknown Masternode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) 
	{
        if (fDebugMaster) LogPrint("mnpayments", "CMasternodePayments::ProcessBlock -- Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }


    // LOCATE THE NEXT MASTERNODE WHICH SHOULD BE PAID

    if (fDebugMaster) LogPrintf("CMasternodePayments::ProcessBlock -- Start: nBlockHeight=%d, masternode=%s\n", nBlockHeight, activeMasternode.vin.prevout.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CMasternode *pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == NULL) 
	{
        if (fDebugMaster) LogPrintf("CMasternodePayments::ProcessBlock -- ERROR: Failed to find masternode to pay\n");
        return false;
    }

    if (fDebugMaster) LogPrintf("CMasternodePayments::ProcessBlock -- Masternode found by GetNextMasternodeInQueueForPayment(): %s\n", pmn->vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CMasternodePaymentVote voteNew(activeMasternode.vin, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    if (fDebugMaster) LogPrintf("CMasternodePayments::ProcessBlock -- vote: payee=%s, nBlockHeight=%d\n", address2.ToString(), nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR MASTERNODE KEYS

	if (fDebugMaster) LogPrintf("CMasternodePayments::ProcessBlock -- Signing vote\n");
    if (voteNew.Sign()) 
	{
        if (fDebugMaster) LogPrintf("CMasternodePayments::ProcessBlock -- AddPaymentVote()\n");

        if (AddPaymentVote(voteNew)) 
		{
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CMasternodePaymentVote::Relay()
{
    // do not relay until synced
    if (!masternodeSync.IsWinnersListSynced()) return;
    CInv inv(MSG_MASTERNODE_PAYMENT_VOTE, GetHash());
    RelayInv(inv);
}

bool CMasternodePaymentVote::CheckSignature(const CPubKey& pubKeyMasternode, int nValidationHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    std::string strError = "";
    if (!darkSendSigner.VerifyMessage(pubKeyMasternode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if(masternodeSync.IsMasternodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CMasternodePaymentVote::CheckSignature -- Got bad Masternode payment signature, masternode=%s, error: %s", vinMasternode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CMasternodePaymentVote::ToString() const
{
    std::ostringstream info;

    info << vinMasternode.prevout.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << ScriptToAsmStr(payee) <<
            ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CMasternodePayments::Sync(CNode* pnode)
{
    LOCK(cs_mapMasternodeBlocks);

    if(!pCurrentBlockIndex) return;

    int nInvCount = 0;

    for(int h = pCurrentBlockIndex->nHeight; h < pCurrentBlockIndex->nHeight + 20; h++) {
        if(mapMasternodeBlocks.count(h)) {
            BOOST_FOREACH(CMasternodePayee& payee, mapMasternodeBlocks[h].vecPayees) {
                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256& hash, vecVoteHashes) {
                    if(!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_MASTERNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CMasternodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, MASTERNODE_SYNC_MNW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CMasternodePayments::RequestLowDataPaymentBlocks(CNode* pnode)
{
    if(!pCurrentBlockIndex || !fMasternodesEnabled) return;

    LOCK2(cs_main, cs_mapMasternodeBlocks);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = pCurrentBlockIndex;

    while(pCurrentBlockIndex->nHeight - pindex->nHeight < nLimit) {
        if(!mapMasternodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_MASTERNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ) {
                if (fDebugMaster) LogPrintf("CMasternodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if(!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();

    while(it != mapMasternodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        BOOST_FOREACH(CMasternodePayee& payee, it->second.vecPayees) {
            if(payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if(fFound || nTotalVotes >= (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED)/2) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
        DBG (
            // Let's see why this failed
            BOOST_FOREACH(CMasternodePayee& payee, it->second.vecPayees) {
                CTxDestination address1;
                ExtractDestination(payee.GetPayee(), address1);
                CBitcoinAddress address2(address1);
                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
            }
            printf("block %d votes total %d\n", it->first, nTotalVotes);
        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if(GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_MASTERNODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if(vToFetch.size() == MAX_INV_SZ) {
            if (fDebugMaster) LogPrintf("CMasternodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if(!vToFetch.empty()) 
	{
        if (fDebugMaster) LogPrintf("CMasternodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
    }
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePaymentVotes.size() <<
            ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}

bool CMasternodePayments::IsEnoughData()
{
    float nAverageVotes = (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CMasternodePayments::GetStorageLimit()
{
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CMasternodePayments::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    if (fDebugMaster) LogPrint("mnpayments", "CMasternodePayments::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    ProcessBlock(pindex->nHeight + 10);
}
