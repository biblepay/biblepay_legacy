// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The BiblePay Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "kjv.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "rpcpog.h"
#include "validation.h"
#include "hash.h"
#include "validation.h"
#include "net.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "validationinterface.h"
#include "smartcontract-client.h"
#include "governance-classes.h" // For superblock Height
#include "rpcpodc.h"  // For strReplace

#include "evo/specialtx.h"
#include "evo/cbtx.h"
#include "evo/simplifiedmns.h"
#include "evo/deterministicmns.h"

#include "llmq/quorums_blockprocessor.h"
#include "llmq/quorums_chainlocks.h"

#include <algorithm>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <queue>
#include <utility>

//////////////////////////////////////////////////////////////////////////////
//
// BiblepayMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;

class ScoreCompare
{
public:
    ScoreCompare() {}

    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b)
    {
        return CompareTxMemPoolEntryByScore()(*b,*a); // Convert to less than
    }
};

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}


BlockAssembler::Options::Options() {	
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);	    
    nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;	
}



BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options) : chainparams(params)	
{
    blockMinFeeRate = options.blockMinFeeRate;	   
    // Limit size to between 1K and MaxBlockSize()-1K for sanity:	 
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MaxBlockSize(fDIP0001ActiveAtTip) - 1000), (unsigned int)options.nBlockMaxSize));	
}


static BlockAssembler::Options DefaultOptions(const CChainParams& params)	
{	
    // Block resource limits	
    BlockAssembler::Options options;	
    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;	
    if (IsArgSet("-blockmaxsize")) 
	{	
        options.nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);	
    }

    if (IsArgSet("-blockmintxfee")) 
	{
        CAmount n = 0;
        ParseMoney(GetArg("-blockmintxfee", ""), n);
		options.blockMinFeeRate = CFeeRate(n);
    }
	else 
	{
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }
	return options;
}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockSize = 1000;
    nBlockSigOps = 100;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

BlockAssembler::BlockAssembler(const CChainParams& params) : BlockAssembler(params, DefaultOptions(params)) {}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, std::string sPoolMiningPublicKey, std::string sMinerGuid, int iThreadId, bool fFunded)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
	    
    CBlockIndex* pindexPrev = chainActive.Tip();
    nHeight = pindexPrev->nHeight + 1;
	bool fDIP0003Active_context = nHeight >= chainparams.GetConsensus().DIP0003Height;	
    bool fDIP0008Active_context = VersionBitsState(chainActive.Tip(), chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0008, versionbitscache) == THRESHOLD_ACTIVE;
    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus(), chainparams.BIP9CheckMasternodesUpgraded());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    if (fDIP0003Active_context) {
        for (auto& p : chainparams.GetConsensus().llmqs) {
            CTransactionRef qcTx;
            if (llmq::quorumBlockProcessor->GetMinableCommitmentTx(p.first, nHeight, qcTx)) {
                pblock->vtx.emplace_back(qcTx);
                pblocktemplate->vTxFees.emplace_back(0);
                pblocktemplate->vTxSigOps.emplace_back(0);
                nBlockSize += qcTx->GetTotalSize();
                ++nBlockTx;
            }
        }
    }

	// BiblePay Anti-Bot-Net System
	std::string sABNLocator;
	
	/*
	double nMinCoinAge = GetSporkDouble("requiredabnweight", 0);
	if (nMinCoinAge > 0 && !fFunded)
	{
		CReserveKey reserve1(pwalletMain);
		std::string sXML1;
		std::string sError1;
		CWalletTx wtxABN = GetAntiBotNetTx(chainActive.Tip(), nMinCoinAge, reserve1, sXML1, sPoolMiningPublicKey, sError1);
		if (sError1.empty() && wtxABN.tx != NULL)
		{
			pblock->vtx.emplace_back(wtxABN.tx);
			CAmount nABNFees = GetFees(wtxABN.tx);
			nFees += nABNFees;
			pblocktemplate->vTxFees.emplace_back(nABNFees);
			pblocktemplate->vTxSigOps.emplace_back(0);
			nBlockSize += wtxABN.tx->GetTotalSize();
			++nBlockTx;
			sABNLocator = "<abnlocator>" + RoundToString(nBlockTx, 0) + "</abnlocator>";
			WriteCache("poolthread" + RoundToString(iThreadId, 0), "poolinfo4", "ABN: OK", GetAdjustedTime());
		}
		else
		{
			WriteCache("poolthread" + RoundToString(iThreadId, 0), "poolinfo4", "Unable to Create ABN: " + sError1, GetAdjustedTime());
			if (fDebugSpam)
				LogPrintf("\n***** CreateNewBlock::Unable to add ABN because %s *****\n", sError1.c_str());
		}
	}
	*/


    
    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockSize = nBlockSize;
	if (fDebugSpam)
		LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;

	// BiblePay - Add Pool Support
	if (!sPoolMiningPublicKey.empty())
	{
		CBitcoinAddress cbaPoolAddress(sPoolMiningPublicKey);
		CScript spkPoolScript = GetScriptForDestination(cbaPoolAddress.Get());
		coinbaseTx.vout[0].scriptPubKey = spkPoolScript;
	}

    // NOTE: unlike in bitcoin, we need to pass PREVIOUS block height here
    CAmount blockReward = nFees + GetBlockSubsidy(pindexPrev->nBits, pindexPrev->nHeight, Params().GetConsensus(), false);

    // Compute regular coinbase transaction.
    coinbaseTx.vout[0].nValue = blockReward;
	
    if (!fDIP0003Active_context) {
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    } else {
        coinbaseTx.vin[0].scriptSig = CScript() << OP_RETURN;

        coinbaseTx.nVersion = 3;
        coinbaseTx.nType = TRANSACTION_COINBASE;

        CCbTx cbTx;
		if (fDIP0008Active_context) {	
            cbTx.nVersion = 2;	
        } else {	
            cbTx.nVersion = 1;	
        }

        cbTx.nHeight = nHeight;

        CValidationState state;
        if (!CalcCbTxMerkleRootMNList(*pblock, pindexPrev, cbTx.merkleRootMNList, state)) {
			 throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootMNList failed: %s", __func__, FormatStateMessage(state)));
	    }	
        if (fDIP0008Active_context) {	
            if (!CalcCbTxMerkleRootQuorums(*pblock, pindexPrev, cbTx.merkleRootQuorums, state)) {	
                throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootQuorums failed: %s", __func__, FormatStateMessage(state)));        
			}
		}
        SetTxPayload(coinbaseTx, cbTx);
    }

	// Add BiblePay version to the subsidy tx message
	std::string sVersion = FormatFullVersion();
	coinbaseTx.vout[0].sTxOutMessage += "<VER>" + sVersion + "</VER>" + sABNLocator;
	if (!sMinerGuid.empty())
		coinbaseTx.vout[0].sTxOutMessage += "<MINERGUID>" + sMinerGuid + "</MINERGUID>";

    // Update coinbase transaction with additional info about masternode and governance payments,
    // get some info back to pass to getblocktemplate
    FillBlockPayments(coinbaseTx, nHeight, blockReward, pblocktemplate->voutMasternodePayments, pblocktemplate->voutSuperblockPayments);
    // LogPrintf("CreateNewBlock -- nBlockHeight %d blockReward %lld txoutMasternode %s coinbaseTx %s", nHeight, blockReward, pblocktemplate->txoutsMasternode.ToString(), coinbaseTx.ToString());

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vTxFees[0] = -nFees;

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    pblocktemplate->nPrevBits = pindexPrev->nBits;
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(*pblock->vtx[0]);
	
    CValidationState state;
    if (!fFunded && !TestBlockValidityLite(state, chainparams, *pblock, pindexPrev, false, false, true, true)) 
	{
		if (fDebugSpam)
			LogPrint("miner", "BibleMiner failed to create new block\n");
        return NULL;
    }
    int64_t nTime2 = GetTimeMicros();

	if (fDebugSpam)
		LogPrint("bench", "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, unsigned int packageSigOps)
{
    if (nBlockSize + packageSize >= nBlockMaxSize)
        return false;
    if (nBlockSigOps + packageSigOps >= MaxBlockSigOps(fDIP0001ActiveAtTip))
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - safe TXs in regard to ChainLocks
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    BOOST_FOREACH (const CTxMemPool::txiter it, package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
		 if (!llmq::chainLocksHandler->IsTxSafeForMining(it->GetTx().GetHash())) 
			 return false;
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOps.push_back(iter->GetSigOpCount());
    nBlockSize += iter->GetTxSize();
    ++nBlockTx;
    nBlockSigOps += iter->GetSigOpCount();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    BOOST_FOREACH(const CTxMemPool::txiter it, alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        BOOST_FOREACH(CTxMemPool::txiter desc, descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCountWithAncestors -= it->GetSigOpCount();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    if (mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it))
        return true;
    return false;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareModifiedEntry()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        unsigned int packageSigOps = iter->GetSigOpCountWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOps = modit->nSigOpCountWithAncestors;
        }

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOps)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockSize > nBlockMaxSize - 1000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final and safe
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}


void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}


//////////////////////////////////////////////////////////////////////////////
//                                                                          //
//  BiblePay Proof-of-Bible-Hash (POBH) Internal miner - March 12th, 2019   //
//                                                                          //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////
std::string PoolRequest(int iThreadID, std::string sAction, std::string sPoolURL, std::string sMinerID, std::string sSolution)
{
	int SSL_PORT = 443;
	int POOL_TRANSMISSION_TIMEOUT = 45000;
	int POOL_CONNECTION_TIMEOUT = 15;
	bool bSSL = true;
	int iPoolPort = (int)cdbl(GetArg("-poolport", "80"), 0);
	std::string sPoolPage = "Action.aspx";
	std::string sMultiResponse;
	/*
	Support for HTTP only pools:
	if (sPoolURL.find("https:") == string::npos)
	{
		sMultiResponse = BiblepayHttpPost(true, iThreadID, "POST", sMinerID, sAction, sPoolURL, sPoolPage, iPoolPort, sSolution, 0);
		fPoolMiningUseSSL = false;
	}
	*/
	if (bSSL)
	{
		sMultiResponse = BiblepayHTTPSPost(true, iThreadID, "POST", sMinerID, sAction, sPoolURL, sPoolPage, SSL_PORT, sSolution, POOL_CONNECTION_TIMEOUT, POOL_TRANSMISSION_TIMEOUT, 0);
		fPoolMiningUseSSL = true;
	}
	// Glean the pool responses
	std::string sError = ExtractXML(sMultiResponse,"<ERROR>","</ERROR>");
	std::string sResponse = ExtractXML(sMultiResponse,"<RESPONSE>","</RESPONSE>");
	if (!sError.empty())
	{
		// Notify User of Mining Problem with Pool (for example, if they entered an invalid workerid in conf file)
		WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", sError, GetAdjustedTime());
		return std::string();
	}
	return sResponse;
}

void UpdatePoolProgress(const CBlock* pblock, std::string sPoolAddress, arith_uint256 hashPoolTarget, CBlockIndex* pindexPrev, std::string sMinerGuid, std::string sWorkID, 
	int iThreadID, int64_t iThreadWork, int64_t nThreadStart, unsigned int nNonce, std::string sWorkerID)
{
	bool f7000;
	bool f8000;
	bool f9000;
	bool fTitheBlocksActive;
	GetMiningParams(pindexPrev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);
	const Consensus::Params& consensusParams = Params().GetConsensus();

	uint256 hashSolution = BibleHashClassic(pblock->GetHash(), pblock->GetBlockTime(), pindexPrev->nTime, true, pindexPrev->nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, nNonce, consensusParams);
	// Send the solution to the pool (BiblePay)
	CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
	CBlock block1 = const_cast<CBlock&>(*pblock);
    ssBlock << block1;
    std::string sBlockHex = HexStr(ssBlock.begin(), ssBlock.end());
    CTransaction txCoinbase;
	std::string sTxCoinbaseHex = EncodeHexTx(*pblock->vtx[0]);

	if (!sPoolAddress.empty())
	{
		std::string sPoolURL = GetArg("-pool", "");
		if (sWorkerID.empty() || sPoolURL.empty()) return;
		std::string sSolution = pblock->GetHash().ToString() 
			+ "," + RoundToString(pblock->GetBlockTime(),0) 
			+ "," + RoundToString(pindexPrev->nTime,0) 
			+ "," + RoundToString(pindexPrev->nHeight,0) 
			+ "," + hashSolution.GetHex() 
			+ "," + sMinerGuid 
			+ "," + sWorkID 
			+ "," + RoundToString(iThreadID,0) 
			+ "," + RoundToString(iThreadWork,0) 
			+ "," + RoundToString(nThreadStart,0) 
			+ "," + RoundToString(nHashCounter,0) 
			+ "," + RoundToString(nHPSTimerStart,0)
			+ "," + RoundToString(GetTimeMillis(),0)
			+ "," + RoundToString(nNonce,0)
			+ "," + sBlockHex
			+ "," + sTxCoinbaseHex;
		// Clear the pool cache
		ClearCache("poolcache");
		std::string sResult = PoolRequest(iThreadID, "solution", sPoolURL, sWorkerID, sSolution);
		WriteCache("poolthread"+RoundToString(iThreadID,0), "poolinfo2", "Submitting Solution " + TimestampToHRDate(GetAdjustedTime()),GetAdjustedTime());
		LogPrint("pool", "PoolStatus: %s, URL %s, workerid %s, solu %s ",sResult.c_str(), sPoolURL.c_str(), sWorkerID.c_str(), sSolution.c_str());
	}
}

std::string UsingDualABNs(bool& fUsingDualABN, bool fInternalABNOK)
{
	std::string sWorkerIDDefault = GetArg("-workerid", "");        // Self Supplied ABN
	std::string sWorkerIDTurnkey = GetArg("-workeridfunded", ""); // Funded ABN workerid
	bool fDual = (!sWorkerIDDefault.empty() && !sWorkerIDTurnkey.empty()) ? true : false;
	std::string sChosen = fInternalABNOK ? sWorkerIDDefault : sWorkerIDTurnkey;
	
	if (fDual)
	{
		return sChosen;
	}
	else
	{
		// Revert to old behavior; just use default workerid
		return sWorkerIDDefault;
	}
}

static CCriticalSection csBusyWait;
bool GetPoolMiningMode(int iThreadID, int& iFailCount, std::string& out_PoolAddress, arith_uint256& out_HashTargetPool, std::string& out_MinerGuid, 
	std::string& out_WorkID, std::string& out_BlockData, bool fInternalABNOK, std::string& out_sWorkerID, int64_t& out_nDeadline, std::string& out_sPoolCommand)
{
	// If user is not pool mining, return false.
	// If user is pool mining, and pool is down, return false so that the client reverts back to solo mining mode automatically.
	// Honor TestNet and RegTestNet when communicating with pools, so we support all three NetworkID types for robust support/testing.
	LOCK(csBusyWait); 
	std::string sPoolURL = GetArg("-pool", "");
	// If pool URL is empty, user is not pool mining
	if (sPoolURL.empty())
		return false;
	
	// Choose between default workerid (self-supplied ABN), or turnkey workerid (pool mining with funded ABNs)
	bool fUsingDualABNs = false;
	out_sWorkerID = UsingDualABNs(fUsingDualABNs, fInternalABNOK);

	if (out_sWorkerID.empty()) 
	{
		LogPrintf("\nWorkerID empty in config.");
 		WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "WORKER ID EMPTY IN CONFIG FILE", GetAdjustedTime());
		return false;
	}
	// Before hitting the pool with a request, see if the pool recently gave us fully qualified work, if so prefer it
	out_PoolAddress = ReadCache("poolcache", "pooladdress");
	std::string sCachedHashTargetPool = ReadCache("poolcache", "poolhashtarget");
	out_MinerGuid = ReadCache("poolcache", "minerguid");
	out_WorkID = ReadCache("poolcache", "workid");
	out_BlockData = ReadCache("poolcache", "blockdata");
	out_sPoolCommand = ReadCache("poolcache", "poolcommand");
	out_nDeadline = (int64_t)cdbl(ReadCache("poolcache", "deadline"), 0);

	sGlobalPoolURL = sPoolURL;

	if (!out_PoolAddress.empty() && !sCachedHashTargetPool.empty() && !out_MinerGuid.empty() && !out_WorkID.empty())
	{
		iFailCount = 0;
		out_HashTargetPool = UintToArith256(uint256S("0x" + sCachedHashTargetPool));
		if (!fUsingDualABNs)
			WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo1", out_PoolAddress, GetAdjustedTime());
		WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo2", "RMC_" + TimestampToHRDate(GetAdjustedTime()), GetAdjustedTime());
		if (iThreadID == 0)
			WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo1", "Pool mining with " + out_sWorkerID, GetAdjustedTime());

		return true;
	}

	// Test Pool to ensure it can send us work before committing to being a pool miner
	std::string sResult = PoolRequest(iThreadID, "readytomine2", sPoolURL, out_sWorkerID, "");
	LogPrint("pool", "POOL RESULT %s ", sResult.c_str());
	std::string sPoolAddress = ExtractXML(sResult,"<ADDRESS>","</ADDRESS>");
	if (sPoolAddress.empty()) 
	{
		iFailCount++;
		if (iFailCount >= 5)
		{
			WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "POOL DOWN-REVERTING TO SOLO MINING", GetAdjustedTime());
		}
		return false;
	}
	else
	{
		 CBitcoinAddress cbaPoolAddress(sPoolAddress);
		 if (!cbaPoolAddress.IsValid())
		 {
		     WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "INVALID POOL ADDRESS", GetAdjustedTime());
			 return false;  // Ensure pool returns a valid address for this network
		 }
		 // Verify pool has a hash target for this miner
		 std::string sHashTarget = ExtractXML(sResult,"<HASHTARGET>", "</HASHTARGET>");
		 if (sHashTarget.empty() || sHashTarget.length() != 64)
		 {
	 		 WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo3", "POOL HAS NO AVAILABLE WORK", GetAdjustedTime());
			 return false; //Revert to solo mining
		 }
		 out_HashTargetPool = UintToArith256(uint256S("0x" + sHashTarget));
		 out_MinerGuid = ExtractXML(sResult, "<MINERGUID>", "</MINERGUID>");
		 out_WorkID = ExtractXML(sResult, "<WORKID>", "</WORKID>");
		 out_BlockData = ExtractXML(sResult, "<BLOCKDATA>", "</BLOCKDATA>");
		 out_nDeadline = (int64_t)cdbl(ExtractXML(sResult, "<DEADLINE>", "</DEADLINE>"), 0);
		 out_sPoolCommand = ExtractXML(sResult, "<POOLCOMMAND>", "</POOLCOMMAND>");

		 if (out_MinerGuid.empty()) 
		 {
		 	 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "MINER GUID IS EMPTY", GetAdjustedTime());
			 return false;
		 }
		 if (out_WorkID.empty()) 
		 {
		 	 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "POOL HAS NO AVAILABLE WORK", GetAdjustedTime());
			 return false;
		 }
		 out_PoolAddress = sPoolAddress;
		 // Start Pool Mining
		 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo1", sPoolAddress, GetAdjustedTime());
		 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo2", "RM_" + TimestampToHRDate(GetAdjustedTime()), GetAdjustedTime());
		 iFailCount = 0;
		 // Cache pool mining settings to share across threads to decrease pool load
		 WriteCache("poolcache", "pooladdress", out_PoolAddress, GetAdjustedTime());
		 WriteCache("poolcache", "poolhashtarget", sHashTarget, GetAdjustedTime());
		 WriteCache("poolcache", "minerguid", out_MinerGuid, GetAdjustedTime());
		 WriteCache("poolcache", "workid", out_WorkID, GetAdjustedTime());
		 WriteCache("poolcache", "blockdata", out_BlockData, GetAdjustedTime());
		 WriteCache("poolcache", "deadline", RoundToString(out_nDeadline, 0), GetAdjustedTime());
		 WriteCache("poolcache", "poolcommand", out_sPoolCommand, GetAdjustedTime());

		 if (iThreadID == 0)
			 WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo1", "Pool mining with " + out_sWorkerID, GetAdjustedTime());
		 return true;
	}
	return false;
}


std::string GetPoolMiningNarr(std::string sPoolAddress)
{
	const CChainParams& chainparams = Params();
	std::string sNetwork = chainparams.NetworkIDString();

	if (sPoolAddress.empty())
	{
		return "SOLO MINING AGAINST " + sNetwork + " NET";
	}
	else
	{
		std::string sPoolURL = GetArg("-pool", "");
		std::string sNarr = "POOL MINING WITH POOL " + sPoolURL + " AGAINST " + sNetwork + " NET, POOL RECV ADDRESS " + sPoolAddress;
		return sNarr;
	}
}

void RelayMiningMessage(std::string sMessage)
{
	WriteCache("poolthread0", "poolinfo3", sMessage, GetAdjustedTime());
}

void UpdateHashesPerSec(double& nHashesDone)
{
	// Update HashesPerSec
	nHashCounter += nHashesDone;
	nHashesDone = 0;
	dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
	nBibleMinerPulse++;
}

bool PeersExist()
{
	 int iConCount = (int)g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
	 return (iConCount > 0);
}

bool LateBlock(CBlock block, CBlockIndex* pindexPrev, int iMinutes)
{
	// After 60 minutes, we no longer require the anti-bot-net weight (prevent the chain from freezing)
	// Note we use two hard times to make this rule fork-proof
	int64_t nAgeTip = block.GetBlockTime() - pindexPrev->nTime;
	return (nAgeTip > (60 * iMinutes)) ? true : false;
}

bool InternalABNSufficient()
{
	double nMinRequiredABNWeight = GetSporkDouble("requiredabnweight", 0);
	CAmount nTotalReq = 0;
	if (pwalletMain->IsLocked())
		return false;
	double dABN = pwalletMain->GetAntiBotNetWalletWeight(nMinRequiredABNWeight, nTotalReq);
	if (dABN < nMinRequiredABNWeight) 
			return false;
	return true;
}

bool IsUnableToMine()
{
	double nMinRequiredABNWeight = GetSporkDouble("requiredabnweight", 0);
	if (nMinRequiredABNWeight > 0 && pwalletMain->IsLocked())
		return true;
	return false;
}

bool IsMyABNSufficient(CBlock block, CBlockIndex* pindexPrev, int nHeight)
{
	// Rule #2 for Smart Contracts - Resist mining a bad smart contract unless we absolutely have to (this prevents bad blocks from being generated on testnet and regtestnet)
	bool bIsSuperblock = CSuperblock::IsSmartContract(nHeight);
	CAmount nPayments = block.vtx[0]->GetValueOut();
	if (bIsSuperblock && nPayments < ((MAX_BLOCK_SUBSIDY + 1) * COIN) && !LateBlock(block, pindexPrev, (60 * 60 * 8)))
	{
		LogPrintf("\nMiner::IsMyAbnSufficient, Block Height %f, This superblock has no recipients!  Recreating block...", (double)nHeight);
		return false;
	}
	return true;
}

bool CreateBlockForStratum(std::string sAddress, std::string& sError, CBlock& blockX)
{
	// Create Evo block
	bool fFunded = false;
	std::string sMinerGuid;
	int iThreadID = 0;
	boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);
	std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript, 
		sAddress, sMinerGuid, iThreadID, fFunded));
	if (!pblocktemplate.get())
    {
		LogPrint("miner", "CreateBlockForStratum::No block to mine %f", iThreadID);
		sError = "Wallet Locked/ABN Required";
		SpendABN();
		return false;
    }
	CBlock *pblock = &pblocktemplate->block;
	int iStart = rand() % 65536;
	unsigned int nExtraNonce = GetAdjustedTime() + iStart; // This is the Extra Nonce (not the nonce); this helps put every miner on their own private hash in the pool (since they don't have a distinct receiving address)
	CBlockIndex* pindexPrev = chainActive.Tip();
	IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);
	blockX = const_cast<CBlock&>(*pblock);
	return true;
}	

void static BibleMiner(const CChainParams& chainparams, int iThreadID, int iFeatureSet)
{

	/*
					if (fPoolMiningMode)
					{
						if (UintToArith256(hash) <= hashTargetPool)
						{
							bool fNonce = CheckNonce(f9000, pblock->nNonce, pindexPrev->nHeight, pindexPrev->nTime, pblock->GetBlockTime(), consensusParams);
							if (UintToArith256(hash) <= hashTargetPool && fNonce && hashTargetPool > 0)
							{
								nLastShareSubmitted = GetAdjustedTime();
								UpdatePoolProgress(pblock, sPoolMiningAddress, hashTargetPool, pindexPrev, sMinerGuid, sWorkID, iThreadID, nThreadWork, nThreadStart, pblock->nNonce, sWorkerID);
								hashTargetPool = UintToArith256(uint256S("0x0"));
								nThreadStart = GetTimeMillis();
								nThreadWork = 0;
								nLastPoolShareSolved = GetAdjustedTime();
							}
						}
					}
	*/

	LogPrintf("BibleMiner -- started thread %f \n", (double)iThreadID);
    int64_t nThreadStart = GetTimeMillis();
	int64_t nLastGSC = GetAdjustedTime();
	int64_t nThreadWork = 0;
	int64_t nLastReadyToMine = GetAdjustedTime() - 480;
	int64_t nLastClearCache = GetAdjustedTime();
	int64_t nLastShareSubmitted = GetAdjustedTime() - 480;
	int64_t nLastGUI = GetAdjustedTime() - 30;
	int64_t nLastPoolShareSolved = GetAdjustedTime();
	int64_t POOL_MIN_MINUTES = 3 * 60;
	int64_t POOL_MAX_MINUTES = 15 * 60;
	int64_t nLastMiningBreak = 0;
	int64_t STAGNANT_WORK_THRESHHOLD = 60 * 15;
	int64_t nDeadline = 0;
	std::string sPoolCommand;

	bool fInternalABNOK = false;
	std::string sWorkerID;
	bool fUsingDualABNs = false;
	UsingDualABNs(fUsingDualABNs, false);

	int64_t nGSCFrequency = cdbl(GetSporkValue("gscclientminerfrequency"), 0);
	if (nGSCFrequency == 0) 
		nGSCFrequency = (60 * 60 * 4);
	int iFailCount = 0;
	// This allows the miner to dictate how much sleep will occur when distributed computing is enabled.  This will let Rosetta use the maximum CPU time.  NOTE: The default is 200ms per 256 hashes.
	double dMinerSleep = cdbl(GetArg("-minersleep", "325"), 0);
	double dJackrabbitStart = cdbl(GetArg("-jackrabbitstart", "0"), 0);
	unsigned int nExtraNonce = 0;
	double nHashesDone = 0;
	int iOuterLoop = 0;
    RenameThread("biblepay-miner");
	if (iThreadID == 0)
		SpendABN();
					
    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);
	std::string sPoolMiningAddress;
	std::string sMinerGuid;
	std::string sWorkID;
	std::string sBlockData;
	std::string sPoolConfURL = GetArg("-pool", "");
	int iStart = rand() % 1000;
	MilliSleep(iStart);
	nLastReadyToMine = 0;

recover:
	arith_uint256 hashTargetPool = UintToArith256(uint256S("0x0"));

    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

		arith_uint256 hashTargetPool = UintToArith256(uint256S("0x0"));

        while (true) 
		{
            if (chainparams.MiningRequiresPeers() || fReindex)
			{
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                while(true)
				{
		            if (PeersExist() && !IsInitialBlockDownload() && masternodeSync.IsSynced() && !fReindex)
						break;
					if (dJackrabbitStart == 1) 
						break;
                    
                    MilliSleep(1000);
                    
                } 
            }

			// Pool Support
			if (!fPoolMiningMode || hashTargetPool == 0)
			{
				if ((GetAdjustedTime() - nLastReadyToMine) > POOL_MIN_MINUTES)
				{ 
					nLastReadyToMine = GetAdjustedTime();

					fInternalABNOK = InternalABNSufficient();
					std::string sNarr = fInternalABNOK ? "Internal ABN: OK" : "Internal ABN: Invalid";
					if (iThreadID==0)
						WriteCache("poolthread0", "poolinfo5", sNarr + " " + RoundToString(GetAdjustedTime(), 0), GetAdjustedTime());

					fPoolMiningMode = GetPoolMiningMode(iThreadID, iFailCount, sPoolMiningAddress, hashTargetPool, sMinerGuid, sWorkID, sBlockData, fInternalABNOK, sWorkerID, nDeadline, sPoolCommand);
					if (!sPoolCommand.empty())
					{
						LogPrintf("Miner::GetPoolMiningMode::Received Command %s %f", sPoolCommand, GetAdjustedTime());
					}
					if (fDebugSpam && !sPoolMiningAddress.empty())
						LogPrint("pool", "Checking with Pool: Pool Address %s \r\n", sPoolMiningAddress.c_str());
				}
			}
		    
            //
            // Create new block
            //

            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();
            if(!pindexPrev) break;

			// R Andrews - 10-31-2018 - Move this to the main thread, so we can support external mining programs:
			/*
			if (false)
			{
				// POG - R ANDREWS - 12/6/2018 - Once every gscclientminerfrequency, call the gsc client side and create applicable transactions
				int64_t nGSCAge = GetAdjustedTime() - nLastGSC;
				if (iThreadID == 0 && (nGSCAge > nGSCFrequency))
				{
					nLastGSC = GetAdjustedTime();
					std::string sError;
					bool fCreated = ientSideTransaction(false, false, "", sError);
					if (!fCreated)
					{
						LogPrintf("\nBibleMiner::Unable to create client side GSC transaction. (See Log [%s]). %f", sError, iThreadID);
					}
				}
			}
			*/


			if (!fProd && mempool.size() == 0 && GetSporkDouble("SLEEP_DURING_EMPTY_BLOCKS", 0) == 1)
                MilliSleep(1000 * 60 * 7);
           
			// Create Evo block
			bool fFunded = !sBlockData.empty();

			std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript, sPoolMiningAddress, sMinerGuid, iThreadID, fFunded));
			if (!pblocktemplate.get())
            {
				std::string sSuffix = IsUnableToMine() ? "Wallet Locked/ABN Required" : "";
				WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo4", "No block to mine... " + sSuffix + " Please wait... " + RoundToString(GetAdjustedTime(), 0), GetAdjustedTime());
				MilliSleep(15000);
				SpendABN();
				LogPrint("miner", "No block to mine %f", iThreadID);
				goto recover;
            }

			CBlock *pblock = &pblocktemplate->block;
		
			// Pool support for funded ABNs - BiblePay - R Andrews
			if (fPoolMiningMode && !sBlockData.empty())
			{
				if (!DecodeHexBlk(pblocktemplate->block, sBlockData))
				{
					WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo4", "Failed to retrieve funded ABN from pool.", GetAdjustedTime());
					SpendABN();
					MilliSleep(30000);
					goto recover;
				}
			    CValidationState state;
				bool fValid = TestBlockValidityLite(state, chainparams, *pblock, pindexPrev, false, false, true, false);
				// Handle the edge case where the pool gave us a bad block to mine
				if (!fValid)
				{
					WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo4", "Received a stale block from the pool... Please wait... ", GetAdjustedTime());
					MilliSleep(15000);
					SpendABN();
					ClearCache("poolcache");
					nLastReadyToMine = 0;
					goto recover;
				}
				std::string sValid = fValid ? "Valid" : "Invalid";
				WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo4", "Mining with funded " + sValid + " ABN " + pblocktemplate->block.vtx[0]->GetHash().GetHex(), GetAdjustedTime());
				WriteCache("poolthread0", "poolinfo5", "", GetAdjustedTime());
			}
			
			bool bABNOK = IsMyABNSufficient(*pblock, pindexPrev, pindexPrev->nHeight + 1);
			fInternalABNOK = bABNOK;
			if (!bABNOK)
			{
				WriteCache("poolthread" + RoundToString(iThreadID, 0), "poolinfo4", "ABN weight is too low to mine", GetAdjustedTime());
				MilliSleep(15000);
			}
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);
			nHashesDone++;
			UpdateHashesPerSec(nHashesDone);
			std::string sPoolNarr = GetPoolMiningNarr(sPoolMiningAddress);
			if (fDebugSpam)
				LogPrint("miner", "BiblepayMiner -- Running miner %s with %u transactions in block (%u bytes)\n", sPoolNarr.c_str(), pblock->vtx.size(), ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));
		    //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
			bool f7000;
			bool f8000;
			bool f9000;
			bool fTitheBlocksActive;
			GetMiningParams(pindexPrev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);
			const Consensus::Params& consensusParams = Params().GetConsensus();

			while (true)
			{
				// This is the outer loop (intentional)
			    while (true)
                {
					// BiblePay: Proof of BibleHash requires the blockHash to not only be less than the Hash Target, but also,
					// the BibleHash of the blockhash must be less than the target.
					// The BibleHash is generated from chained bible verses, AES encryption, MD5, X11, and the custom biblepay.c hash
					uint256 x11_hash = pblock->GetHash();
					uint256 hash = BibleHashV2(x11_hash, pblock->GetBlockTime(), pindexPrev->nTime, true, pindexPrev->nHeight);

					nHashesDone += 1;
					nThreadWork += 1;

					if (UintToArith256(hash) <= hashTarget)
					{
						bool fNonce = CheckNonce(f9000, pblock->nNonce, pindexPrev->nHeight, pindexPrev->nTime, pblock->GetBlockTime(), consensusParams);
						if (fNonce)
						{
							// Found a solution
					        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
							SpendABN();
							if (bABNOK)
							{
								bool bAccepted = !ProcessNewBlock(Params(), shared_pblock, true, NULL);
								if (!bAccepted)
								{
									LogPrint("miner", "\nblock rejected.");
									MilliSleep(15000);
								}
								coinbaseScript->KeepScript();
								// In regression test mode, stop mining after a block is found. This
								// allows developers to controllably generate a block on demand.
								if (chainparams.MineBlocksOnDemand())
										throw boost::thread_interrupted();
								break;
							}
						}
					}
						
					pblock->nNonce += 1;
			
					if ((pblock->nNonce & 0xFFF) == 0)
					{
						// If the user is pool mining, and has not found a share in 15 minutes, get new work
						int64_t nStagnantWorkElapsedTime = GetAdjustedTime() - nLastPoolShareSolved;
						if (fPoolMiningMode && nStagnantWorkElapsedTime > STAGNANT_WORK_THRESHHOLD)
						{
							hashTargetPool = UintToArith256(uint256S("0x0"));
							nThreadStart = GetTimeMillis();
							nThreadWork = 0;
							nLastPoolShareSolved = GetAdjustedTime();
							nLastReadyToMine = 0;
							break;
						}

						boost::this_thread::interruption_point();
              			if (dMinerSleep > 0) 
							MilliSleep(dMinerSleep);

						int64_t nElapsed = GetAdjustedTime() - nLastGUI;
				
						if (nElapsed > 5)
						{
							nLastGUI = GetAdjustedTime();
							UpdateHashesPerSec(nHashesDone);
							bool fNonce = CheckNonce(f9000, pblock->nNonce, pindexPrev->nHeight, pindexPrev->nTime, pblock->GetBlockTime(), consensusParams);
							if (!fNonce)
								pblock->nNonce = 0x9FFF;
						}
						int64_t nElapsedLastMiningBreak = GetAdjustedTime() - nLastMiningBreak;
						if (nElapsedLastMiningBreak > 60)
						{
							nLastMiningBreak = GetAdjustedTime();
							break;
						}
					}
			    }

				UpdateHashesPerSec(nHashesDone);
			
		        // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                iOuterLoop++;
				if (!bABNOK)
				{
					MilliSleep(5000);
					break;  // Allow them to try to create a new block (the 5 second sleep is to ensure that we don't hit the user with too many new ABN transactions per minute.
					// NOTE: If the block is Late (> 60 mins old), bABNOK will be true (meaning they can mine at full speed during a late block)
				}

				if (fPoolMiningMode && (!sPoolMiningAddress.empty()))
				{
					if ((GetAdjustedTime() - nLastReadyToMine) > POOL_MAX_MINUTES || (GetAdjustedTime() > nDeadline && nDeadline > 0))
					{
						std::string sNarr = nDeadline > 0 ? "Deadline missed. " : "Pool mining hard block. ";
						LogPrint("miner", sNarr);
						if (nDeadline > 0)
							LogPrintf("Miner %f %s", iThreadID, sNarr);
						hashTargetPool = UintToArith256(uint256S("0x0"));
						WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "CFW " + TimestampToHRDate(GetAdjustedTime()), GetAdjustedTime());
						if (iThreadID == 0) 
							WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", sNarr, GetAdjustedTime());
						ClearCache("poolcache");
						break;
					}
				}

				if ((GetAdjustedTime() - nLastClearCache) > (POOL_MIN_MINUTES))
				{
					nLastClearCache = GetAdjustedTime();
					WriteCache("poolcache", "pooladdress", "", GetAdjustedTime());
					ClearCache("poolcache");
					ClearCache("poolthread" + RoundToString(iThreadID, 0));
					WriteCache("gsc", "errors", "", GetAdjustedTime());
				}

				if ((sPoolMiningAddress.empty() && ((GetAdjustedTime() - nLastReadyToMine) > (10*60))))
				{
					if (!sPoolConfURL.empty())
					{
						// This happens when the user wants to pool mine, the pool was down, so we are solo mining, now we need to check to see if the pool is back up during the next iteration
						ClearCache("poolcache");
						WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "Checking on Pool Health to see if back up..." + TimestampToHRDate(GetAdjustedTime()),GetAdjustedTime());
						hashTargetPool = UintToArith256(uint256S("0x0"));
						break;
					}
				}

				if (!PeersExist() && chainparams.MiningRequiresPeers())
                    break;

                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
				{
			        break;
				}

                if (pindexPrev != chainActive.Tip() || pindexPrev==NULL || chainActive.Tip()==NULL)
				{
					break;
				}

				if (pblock->nNonce >= 0x9FFF)
                    break;
	                        
                // Update nTime every few seconds
			    if (pindexPrev)
				{
					if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
					{
						break; // Recreate the block if the clock has run backwards,
							   // so that we can use the correct time.
					}
				}

				if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks)
				{
					// Changing pblock->nTime can change work required on testnet:
					hashTarget.SetCompact(pblock->nBits);
				}
            }
        }
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrint("miner", "\r\nBiblepayMiner -- terminated\n %f", iThreadID);
		dHashesPerSec = 0;
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrint("miner", "\r\nBiblepayMiner -- runtime error: %s\n", e.what());
		dHashesPerSec = 0;
		// This happens occasionally when TestBlockValidity fails; I suppose the best thing to do for now is start the thread over.
		nThreadStart = GetTimeMillis();
		nThreadWork = 0;
		MilliSleep(1000);
		goto recover;
		// throw;
    }
}

void GenerateBiblecoins(bool fGenerate, int nThreads, const CChainParams& chainparams)
{
    static boost::thread_group* minerThreads = NULL;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
	ClearCache("poolcache");
	
 	if (msSessionID.empty())
		msSessionID = GetRandHash().GetHex();

	int iBibleNumber = 0;			
    for (int i = 0; i < nThreads; i++)
	{
		ClearCache("poolthread" + RoundToString(i, 0));
	    minerThreads->create_thread(boost::bind(&BibleMiner, boost::cref(chainparams), boost::cref(i), boost::cref(iBibleNumber)));
	    MilliSleep(100); 
	}
	iMinerThreadCount = nThreads;

	// Maintain the HashPS
	nHPSTimerStart = GetTimeMillis();
	nHashCounter = 0;
	LogPrintf(" ** Started %f BibleMiner threads. ** \r\n",(double)nThreads);
}


