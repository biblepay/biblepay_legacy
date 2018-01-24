// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"
#include "base58.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "clientversion.h"
#include "hash.h"
#include "main.h"
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

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <queue>

using namespace std;

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
uint256 BibleHash(uint256 hash, int64_t nBlockTime, int64_t nPrevBlockTime, bool bMining, int nPrevHeight, const CBlockIndex* pindexLast, bool bRequireTxIndex, bool f7000, bool f8000, bool f9000, bool fTitheBlocksActive, unsigned int nNonce);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
void WriteCache(std::string section, std::string key, std::string value, int64_t locktime);
std::string ReadCache(std::string section, std::string key);
std::string BiblepayHttpPost(int iThreadID, std::string sActionName, std::string sDistinctUser, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, std::string sSolution);
std::string RoundToString(double d, int place);
std::string PoolRequest(int iThreadID, std::string sAction, std::string sPoolURL, std::string sMinerID, std::string sSolution);
double cdbl(std::string s, int place);
void ClearCache(std::string section);
std::string TimestampToHRDate(double dtm);
void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive);
bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime);
std::string BiblepayHTTPSPost(int iThreadID, std::string sActionName, std::string sDistinctUser, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort,
	std::string sSolution, int iTimeoutSecs, int iMaxSize);
CTransaction CreateCoinStake(CBlockIndex* pindexLast, CScript scriptCoinstakeKey, double dPercent, int iMinConfirms, std::string& sXML, std::string& sError);
double GetStakeWeight(CTransaction tx, int64_t nTipTime, std::string sXML, bool bVerifySignature, std::string& sMetrics, std::string& sError);
uint256 PercentToBigIntBase(int iPercent);
int64_t GetStakeTargetModifierPercent(int nHeight, double nWeight);
bool IsStakeSigned(std::string sXML);

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


CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn, std::string sPoolMiningPublicKey, std::string sMinerGuid, 
	int iThreadId, CAmount competetiveMiningTithe, double dProofOfLoyaltyPercentage)
{
    // Create new block
    auto_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if(!pblocktemplate.get())
        return NULL;
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience

    // Create coinbase tx
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;
	CBlockIndex* pindexPrev = chainActive.Tip();
	
	// BiblePay - Add support for Pools - this section applies during Non-Tithe blocks
	if (!sPoolMiningPublicKey.empty())
	{
		CBitcoinAddress cbaPoolAddress(sPoolMiningPublicKey);
        CScript spkPoolScript = GetScriptForDestination(cbaPoolAddress.Get());
    	txNew.vout[0].scriptPubKey = spkPoolScript;
	}

	int nLastTitheBlock = fProd ? LAST_TITHE_BLOCK : LAST_TITHE_BLOCK_TESTNET;
	bool fTitheBlocksActive = ((pindexPrev->nHeight+1) < nLastTitheBlock);
	
	if (fTitheBlocksActive)
	{
		if (((pindexPrev->nHeight+1) % TITHE_MODULUS)==0)
		{
			CBitcoinAddress cbaFoundationAddress(chainparams.GetConsensus().FoundationAddress);
			CScript spkFoundationAddress = GetScriptForDestination(cbaFoundationAddress.Get());
    		txNew.vout[0].scriptPubKey = spkFoundationAddress;
		}
	}

    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    // Limit to between 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE-1000), nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

    // Collect memory pool transactions into the block
    CTxMemPool::setEntries inBlock;
    CTxMemPool::setEntries waitSet;

    // This vector will be sorted into a priority queue:
    vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    std::priority_queue<CTxMemPool::txiter, std::vector<CTxMemPool::txiter>, ScoreCompare> clearedTxs;
    bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    uint64_t nBlockSize = 1000;
    uint64_t nBlockTx = 0;
    unsigned int nBlockSigOps = 100;
    int lastFewTxs = 0;
    CAmount nFees = 0;
	if (iThreadId > 30) iThreadId = 0;

    {
        LOCK2(cs_main, mempool.cs);
        const int nHeight = pindexPrev->nHeight + 1;
        pblock->nTime = GetAdjustedTime() + iThreadId;
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

        // Add our coinbase tx as first transaction
        pblock->vtx.push_back(txNew);
        pblocktemplate->vTxFees.push_back(-1); // updated at end
        pblocktemplate->vTxSigOps.push_back(-1); // updated at end
        pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (chainparams.MineBlocksOnDemand())
            pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

		// *****************************************  BIBLEPAY - Proof-Of-Loyalty - 01/19/2018 ***************************************************
		std::string sXML = "";
		std::string sError = "";
		
		if (fProofOfLoyaltyEnabled && dProofOfLoyaltyPercentage > 0)
		{
				CTransaction txPOL = CreateCoinStake(pindexPrev, scriptPubKeyIn, dProofOfLoyaltyPercentage, BLOCKS_PER_DAY, sXML, sError);
				if (!sError.empty()) 
				{
					sGlobalPOLError = sError;
				}
				if (!sXML.empty())
				{
					pblock->vtx.push_back(txPOL);
					LogPrintf("CreateNewBlock::Created Proof-Of-Loyalty Block for %f BBP with sig %s \n",dProofOfLoyaltyPercentage, sXML.c_str());
					sGlobalPOLError = "";
				}
		}
		else
		{
				CTransaction txPOL = CreateCoinStake(pindexPrev, scriptPubKeyIn, .01, BLOCKS_PER_DAY, sXML, sError);
				if (!sError.empty()) 
				{
					sGlobalPOLError = sError;
				}
				if (!sXML.empty())
				{
					pblock->vtx.push_back(txPOL);
					sGlobalPOLError = "";
				}
		}

		// **************************************** End of Biblepay - Proof-Of-Loyalty ***********************************************************

		// BIBLEPAY - Add Memory Pool Transactions to Block
        int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                ? nMedianTimePast
                                : pblock->GetBlockTime();


        bool fPriorityBlock = nBlockPrioritySize > 0;
        if (fPriorityBlock) {
            vecPriority.reserve(mempool.mapTx.size());
            for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
                 mi != mempool.mapTx.end(); ++mi)
            {
                double dPriority = mi->GetPriority(nHeight);
                CAmount dummy;
                mempool.ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
                vecPriority.push_back(TxCoinAgePriority(dPriority, mi));
            }
            std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
        }

        CTxMemPool::indexed_transaction_set::nth_index<3>::type::iterator mi = mempool.mapTx.get<3>().begin();
        CTxMemPool::txiter iter;

        while (mi != mempool.mapTx.get<3>().end() || !clearedTxs.empty())
        {
            bool priorityTx = false;
            if (fPriorityBlock && !vecPriority.empty()) { // add a tx from priority queue to fill the blockprioritysize
                priorityTx = true;
                iter = vecPriority.front().second;
                actualPriority = vecPriority.front().first;
                std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                vecPriority.pop_back();
            }
            else if (clearedTxs.empty()) { // add tx with next highest score
                iter = mempool.mapTx.project<0>(mi);
                mi++;
            }
            else {  // try to add a previously postponed child tx
                iter = clearedTxs.top();
                clearedTxs.pop();
            }

            if (inBlock.count(iter))
                continue; // could have been added to the priorityBlock

            const CTransaction& tx = iter->GetTx();

            bool fOrphan = false;
            BOOST_FOREACH(CTxMemPool::txiter parent, mempool.GetMemPoolParents(iter))
            {
                if (!inBlock.count(parent)) {
                    fOrphan = true;
                    break;
                }
            }
            if (fOrphan) {
                if (priorityTx)
                    waitPriMap.insert(std::make_pair(iter,actualPriority));
                else
                    waitSet.insert(iter);
                continue;
            }

            unsigned int nTxSize = iter->GetTxSize();
            if (fPriorityBlock &&
                (nBlockSize + nTxSize >= nBlockPrioritySize || !AllowFree(actualPriority))) {
                fPriorityBlock = false;
                waitPriMap.clear();
            }
            if (!priorityTx &&
                (iter->GetModifiedFee() < ::minRelayTxFee.GetFee(nTxSize) && nBlockSize >= nBlockMinSize)) {
                break;
            }
            if (nBlockSize + nTxSize >= nBlockMaxSize) {
                if (nBlockSize >  nBlockMaxSize - 100 || lastFewTxs > 50) {
                    break;
                }
                // Once we're within 1000 bytes of a full block, only look at 50 more txs
                // to try to fill the remaining space.
                if (nBlockSize > nBlockMaxSize - 1000) {
                    lastFewTxs++;
                }
                continue;
            }

            if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
                continue;

            unsigned int nTxSigOps = iter->GetSigOpCount();
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS) {
                if (nBlockSigOps > MAX_BLOCK_SIGOPS - 2) {
                    break;
                }
                continue;
            }

            CAmount nTxFees = iter->GetFee();
            // Added
            pblock->vtx.push_back(tx);
            pblocktemplate->vTxFees.push_back(nTxFees);
            pblocktemplate->vTxSigOps.push_back(nTxSigOps);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fPrintPriority)
            {
                double dPriority = iter->GetPriority(nHeight);
                CAmount dummy;
                mempool.ApplyDeltas(tx.GetHash(), dPriority, dummy);
                if (fDebugMaster) LogPrintf("priority %.1f fee %s txid %s\n", dPriority , CFeeRate(iter->GetModifiedFee(), nTxSize).ToString(), tx.GetHash().ToString());
            }

            inBlock.insert(iter);
	        // Add transactions that depend on this one to the priority queue
            BOOST_FOREACH(CTxMemPool::txiter child, mempool.GetMemPoolChildren(iter))
            {
                if (fPriorityBlock) {
                    waitPriIter wpiter = waitPriMap.find(child);
                    if (wpiter != waitPriMap.end()) {
                        vecPriority.push_back(TxCoinAgePriority(wpiter->second,child));
                        std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                        waitPriMap.erase(wpiter);
                    }
                }
                else {
                    if (waitSet.count(child)) {
                        clearedTxs.push(child);
                        waitSet.erase(child);
                    }
                }
            }
        }

        // NOTE: unlike in bitcoin, we need to pass PREVIOUS block height here
        CAmount blockReward = nFees + GetBlockSubsidy(pindexPrev, pindexPrev->nBits, pindexPrev->nHeight, Params().GetConsensus());

        // Compute regular coinbase transaction.
        txNew.vout[0].nValue = blockReward;
        txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;
		
		// Add BiblePay version to the subsidy tx message
		std::string sVersion = FormatFullVersion();
		txNew.vout[0].sTxOutMessage += "<VER>" + sVersion + "</VER>";
		if (!sMinerGuid.empty())
		{
			txNew.vout[0].sTxOutMessage += "<MINERGUID>" + sMinerGuid + "</MINERGUID>";
		}
		if (fProofOfLoyaltyEnabled && !sXML.empty())
		{
			txNew.vout[0].sTxOutMessage += sXML;
		}
		
        // Update coinbase transaction with additional info about masternode and governance payments,
        // get some info back to pass to getblocktemplate
        FillBlockPayments(txNew, nHeight, blockReward, competetiveMiningTithe, pblock->txoutMasternode, pblock->voutSuperblock);
        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
		if (fDebug10) LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

        // Update block coinbase
        pblock->vtx[0] = txNew;
        pblocktemplate->vTxFees[0] = -nFees;

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
        pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
        pblock->nNonce         = 0;
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

        CValidationState state;
	    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) 
		{
	        //throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
			LogPrintf("TestBlockValidity failed while creating new block\r\n");
			return NULL; //This allows the miner to recover by itself
        }
    }
    return pblocktemplate.release();
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
    CMutableTransaction txCoinbase(pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = txCoinbase;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//

// ***TO DO*** ScanHash is not yet used in Biblepay
//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
//bool static ScanHash(const CBlockHeader *pblock, uint32_t& nNonce, uint256 *phash)
//{
//    // Write the first 76 bytes of the block header to a double-SHA256 state.
//    CHash256 hasher;
//    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//    ss << *pblock;
//    assert(ss.size() == 80);
//    hasher.Write((unsigned char*)&ss[0], 76);

//    while (true) {
//        nNonce++;

//        // Write the last 4 bytes of the block header (the nonce) to a copy of
//        // the double-SHA256 state, and compute the result.
//        CHash256(hasher).Write((unsigned char*)&nNonce, 4).Finalize((unsigned char*)phash);

//        // Return the nonce if the hash has at least some zero bits,
//        // caller will check if it has enough to reach the target
//        if (((uint16_t*)phash)[15] == 0)
//            return true;

//        // If nothing found after trying for a while, return -1
//        if ((nNonce & 0xfff) == 0)
//            return false;
//    }
//}


void UpdatePoolProgress(const CBlock* pblock, std::string sPoolAddress, arith_uint256 hashPoolTarget, CBlockIndex* pindexPrev, std::string sMinerGuid, std::string sWorkID, 
	int iThreadID, unsigned int iThreadWork, int64_t nThreadStart, unsigned int nNonce)
{
	bool f7000;
	bool f8000;
	bool f9000;
	bool fTitheBlocksActive;
	GetMiningParams(pindexPrev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);

	uint256 hashSolution = BibleHash(pblock->GetHash(), pblock->GetBlockTime(), pindexPrev->nTime, true, pindexPrev->nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, nNonce);
	// Send the solution to the pool - 1-22-2018 - R Andrija
	CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
	CBlock block1 = const_cast<CBlock&>(*pblock);
    ssBlock << block1;
    std::string sBlockHex = HexStr(ssBlock.begin(), ssBlock.end());
    CTransaction txCoinbase;
	std::string sTxCoinbaseHex = EncodeHexTx(pblock->vtx[0]);

	if (!sPoolAddress.empty())
	{
		std::string sWorkerID = GetArg("-workerid","");
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
		WriteCache("pool" + RoundToString(iThreadID, 0),"communication","1",GetAdjustedTime());
		SetThreadPriority(THREAD_PRIORITY_NORMAL);
		// Clear the pool cache
		ClearCache("poolcache");
		std::string sResult = PoolRequest(iThreadID,"solution",sPoolURL,sWorkerID,sSolution);
		WriteCache("pool" + RoundToString(iThreadID, 0),"communication","0",GetAdjustedTime());
		WriteCache("poolthread"+RoundToString(iThreadID,0),"poolinfo2","Submitting Solution " + TimestampToHRDate(GetAdjustedTime()),GetAdjustedTime());
		if (fDebugMaster) LogPrint("pool","PoolStatus: %s, URL %s, workerid %s, solu %s ",sResult.c_str(), sPoolURL.c_str(), sWorkerID.c_str(), sSolution.c_str());
	}
}

static bool ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams)
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("\r\nProcessBlockFound::Generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));
				
    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("ProcessBlockFound -- generated block is stale");
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlock(state, chainparams, NULL, pblock, true, NULL))
        return error("ProcessBlockFound -- ProcessNewBlock() failed, block not accepted");

    return true;
}

static CCriticalSection csBusyWait;
void BusyWait()
{
	LOCK(csBusyWait); 
	for (int busy = 0; busy < 100; busy++)
	{
		bool bCommunicating = false;
		for (int i = 0; i < iMinerThreadCount; i++)
		{
			if (ReadCache("pool" + RoundToString(i, 0), "communication") == "1") 
			{
				bCommunicating = true; 
				break;
			}
		}
		if (!bCommunicating) break;
		MilliSleep(100);
	}
}

std::string PoolRequest(int iThreadID, std::string sAction, std::string sPoolURL, std::string sMinerID, std::string sSolution)
{
	int iPoolPort = (int)cdbl(GetArg("-poolport", "80"),0);
	std::string sPoolPage = "Action.aspx";
	std::string sMultiResponse = "";
	if (sPoolURL.find("https:") == string::npos)
	{
		sMultiResponse = BiblepayHttpPost(iThreadID, "POST", sMinerID, sAction, sPoolURL, sPoolPage, iPoolPort, sSolution);
		fPoolMiningUseSSL = false;
	}
	else
	{
		sMultiResponse = BiblepayHTTPSPost(iThreadID, "POST", sMinerID, sAction, sPoolURL, sPoolPage, 443, sSolution, 15, 50000);
		fPoolMiningUseSSL = true;
	}
	// Glean the pool responses
	std::string sError = ExtractXML(sMultiResponse,"<ERROR>","</ERROR>");
	std::string sResponse = ExtractXML(sMultiResponse,"<RESPONSE>","</RESPONSE>");
	if (!sError.empty())
	{
		// Notify User of Mining Problem with Pool (for example, if they entered an invalid workerid in conf file)
		WriteCache("poolthread" + RoundToString(iThreadID,0),"poolinfo3",sError,GetAdjustedTime());
		return "";
	}
	return sResponse;
}

bool GetPoolMiningMode(int iThreadID, int& iFailCount, std::string& out_PoolAddress, arith_uint256& out_HashTargetPool, std::string& out_MinerGuid, std::string& out_WorkID)
{
	// If user is not pool mining, return false.
	// If user is pool mining, and pool is down, return false so that the client reverts back to solo mining mode automatically.
	// Honor TestNet and RegTestNet when communicating with pools, so we support all three NetworkID types for robust support/testing.

	std::string sPoolURL = GetArg("-pool", "");
	out_MinerGuid = "";
	out_WorkID = "";
	out_PoolAddress = "";

	// If pool URL is empty, user is not pool mining
	if (sPoolURL.empty()) return false;
	std::string sWorkerID = GetArg("-workerid",""); //This is the setting reqd to communicate with the pool that points to the pools web account workerID, so the owner of the worker can receive credit for work
	if (sWorkerID.empty()) 
	{
 		WriteCache("poolthread" + RoundToString(iThreadID,0),"poolinfo3","WORKER ID EMPTY IN POOL CONFIG",GetAdjustedTime());
		return false;
	}
	// Before hitting the pool with a request, see if the pool recently gave us fully qualified work, if so prefer it
	SetThreadPriority(THREAD_PRIORITY_NORMAL);
	if (iThreadID > 0) MilliSleep(iThreadID * 100);
	// 10 second Busy Wait while communicating with pool
	BusyWait();
	std::string sCachedAddress = ReadCache("poolcache", "pooladdress");
	std::string sCachedHashTarget = ReadCache("poolcache", "poolhashtarget");
	std::string sCachedMinerGuid = ReadCache("poolcache", "minerguid");
	std::string sCachedWorkID = ReadCache("poolcache", "workid");
	sGlobalPoolURL = sPoolURL;

	if (!sCachedAddress.empty() && !sCachedHashTarget.empty() && !sCachedMinerGuid.empty() && !sCachedWorkID.empty())
	{
		iFailCount = 0;
		out_PoolAddress = sCachedAddress;
	    out_HashTargetPool = UintToArith256(uint256S("0x" + sCachedHashTarget.substr(0, 64)));
		out_MinerGuid = sCachedMinerGuid;
		out_WorkID = sCachedWorkID;
		WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo1", out_PoolAddress, GetAdjustedTime());
		WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo2", "RMC_" + TimestampToHRDate(GetAdjustedTime()), GetAdjustedTime());
		return true;
	}

	// Test Pool to ensure it can send us work before committing to being a pool miner
	WriteCache("pool" + RoundToString(iThreadID, 0),"communication","1",GetAdjustedTime());
	std::string sResult = PoolRequest(iThreadID, "readytomine2", sPoolURL, sWorkerID, "");
	WriteCache("pool" + RoundToString(iThreadID, 0),"communication","0",GetAdjustedTime());
	if (fDebugMaster) LogPrint("pool","POOL RESULT %s ",sResult.c_str());
	std::string sPoolAddress = ExtractXML(sResult,"<ADDRESS>","</ADDRESS>");
	if (sPoolAddress.empty()) 
	{
		iFailCount++;
		if (iFailCount >= 5)
		{
			WriteCache("poolthread" + RoundToString(iThreadID,0),"poolinfo3","POOL DOWN-REVERTING TO SOLO MINING",GetAdjustedTime());
		}
		return false;
	}
	else
	{
		 CBitcoinAddress cbaPoolAddress(sPoolAddress);
		 if (!cbaPoolAddress.IsValid())
		 {
		     WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "INVALID POOL ADDRESS",GetAdjustedTime());
			 return false;  // Ensure pool returns a valid address for this network
		 }
		 // Verify pool has a hash target for this miner
		 std::string sHashTarget = ExtractXML(sResult,"<HASHTARGET>","</HASHTARGET>");
		 if (sHashTarget.empty())
		 {
	 		 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "POOL HAS NO AVAILABLE WORK",GetAdjustedTime());
			 return false; //Revert to solo mining
		 }
		 out_HashTargetPool = UintToArith256(uint256S("0x" + sHashTarget.substr(0, 64)));
		 out_MinerGuid = ExtractXML(sResult,"<MINERGUID>","</MINERGUID>");
		 out_WorkID = ExtractXML(sResult,"<WORKID>","</WORKID>");
		 if (out_MinerGuid.empty()) 
		 {
		 	 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "MINER GUID IS EMPTY",GetAdjustedTime());
			 return false;
		 }
		 if (out_WorkID.empty()) 
		 {
		 	 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo3", "POOL HAS NO AVAILABLE WORK",GetAdjustedTime());
			 return false;
		 }
		 out_PoolAddress=sPoolAddress;
		 // Start Pool Mining
		 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo1", sPoolAddress,GetAdjustedTime());
		 WriteCache("poolthread" + RoundToString(iThreadID,0), "poolinfo2", "RM_" + TimestampToHRDate(GetAdjustedTime()), GetAdjustedTime());
		 iFailCount=0;
		 // Cache pool mining settings to share across threads to decrease pool load
		 WriteCache("poolcache", "pooladdress", out_PoolAddress, GetAdjustedTime());
		 WriteCache("poolcache", "poolhashtarget", sHashTarget, GetAdjustedTime());
		 WriteCache("poolcache", "minerguid", out_MinerGuid, GetAdjustedTime());
		 WriteCache("poolcache", "workid", out_WorkID, GetAdjustedTime());
		 MilliSleep(1000);
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


void static BibleMiner(const CChainParams& chainparams, int iThreadID, int iFeatureSet)
{
	// September 17, 2017 - Robert Andrew (BiblePay)

	LogPrintf("BibleMiner -- started thread %f \n",(double)iThreadID);
    unsigned int iBibleMinerCount = 0;
	int64_t nThreadStart = GetTimeMillis();
	int64_t nThreadWork = 0;
	int64_t nLastReadyToMine = GetAdjustedTime() - 480;
	int64_t nLastClearCache = GetAdjustedTime() - 480;
	int64_t nLastShareSubmitted = GetAdjustedTime() - 480;
	int iFailCount = 0;
	// bool fCompetetiveMining = GetArg("-competetivemining", "true")=="true";

    CAmount competetiveMiningTithe = 0;
recover:
	int iStart = rand() % 1000;
	MilliSleep(iStart);
    
	SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("biblepay-miner");

    unsigned int nExtraNonce = 0;
	
    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);
	std::string sPoolMiningAddress = "";
	std::string sMinerGuid = "";
	std::string sWorkID = "";
	std::string sPoolConfURL = GetArg("-pool", "");
		
    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

		arith_uint256 hashTargetPool = UintToArith256(uint256S("0x0"));

        while (true) 
		{
            if (chainparams.MiningRequiresPeers()) 
			{
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                do 
				{
                    bool fvNodesEmpty;
                    {
                        LOCK(cs_vNodes);
                        fvNodesEmpty = vNodes.empty();
                    }
		            if ((!fvNodesEmpty && !IsInitialBlockDownload())) 
					{
						break;
					}
					// if (chainActive.Tip()->nHeight < 60000 || masternodeSync.IsSynced()))
                        
                    MilliSleep(1000);
                } while (true);
            }

			// Pool Support
			if (!fPoolMiningMode || hashTargetPool == 0)
			{
				if ((GetAdjustedTime() - nLastReadyToMine) > (3*60))
				{
					nLastReadyToMine = GetAdjustedTime();
					fPoolMiningMode = GetPoolMiningMode(iThreadID, iFailCount, sPoolMiningAddress, hashTargetPool, sMinerGuid, sWorkID);
					if (fDebugMaster) LogPrint("pool","Checking with Pool: Pool Address %s \r\n",sPoolMiningAddress.c_str());
				}
			}
		    
            //
            // Create new block
            //

            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();
            if(!pindexPrev) break;
			double dProofOfLoyaltyPercentage = cdbl(GetArg("-polpercentage", "10"),2) / 100;
			competetiveMiningTithe = 0;

		    auto_ptr<CBlockTemplate> pblocktemplate(CreateNewBlock(chainparams, coinbaseScript->reserveScript, sPoolMiningAddress, sMinerGuid,
				iThreadID, competetiveMiningTithe, dProofOfLoyaltyPercentage));
            if (!pblocktemplate.get())
            {
                LogPrintf("BiblepayMiner -- Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
				MilliSleep(5000);
				goto recover;
            }
            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);
			
			std::string sPoolNarr = GetPoolMiningNarr(sPoolMiningAddress);
			
			if (fDebugMaster && fDebug10) LogPrintf("BiblepayMiner -- Running miner %s with %u transactions in block (%u bytes)\n", sPoolNarr.c_str(),
				pblock->vtx.size(), ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

            //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
			// Proof of Loyalty - 01-19-2018
			nGlobalPOLWeight = 0;
			nGlobalInfluencePercentage = 0;
			if (fProofOfLoyaltyEnabled)
			{
				    std::string sMetrics = "";
					std::string sError = "";
					bool bSigned = IsStakeSigned(pblock->vtx[0].vout[0].sTxOutMessage);
					if (bSigned)
					{
						double dStakeWeight = GetStakeWeight(pblock->vtx[1], pblock->GetBlockTime(), 
							pblock->vtx[0].vout[0].sTxOutMessage, true, sMetrics, sError);
						int64_t nPercent = GetStakeTargetModifierPercent(pindexPrev->nHeight, dStakeWeight);
						uint256 uBase = PercentToBigIntBase(nPercent);
						hashTarget += UintToArith256(uBase);
						nGlobalPOLWeight = dStakeWeight;
						nGlobalInfluencePercentage = nPercent;
						LogPrintf(" POLWeight %f, InfluencePercent %f  ubase %s  \n",(double)nGlobalPOLWeight,(double)nGlobalInfluencePercentage, uBase.GetHex().c_str());
					}

			}

			unsigned int nHashesDone = 0;
			unsigned int nBibleHashesDone = 0;
			bool f7000;
			bool f8000;
			bool f9000;
			bool fTitheBlocksActive;
			GetMiningParams(pindexPrev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);

			SetThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL);
     		
		    while (true)
            {
			    while (true)
                {
					// BiblePay: Proof of BibleHash requires the blockHash to not only be less than the Hash Target, but also,
					// the BibleHash of the blockhash must be less than the target.
					// The BibleHash is generated from chained bible verses, a historical tx lookup, one AES encryption operation, and MD5 hash
					uint256 x11_hash = pblock->GetHash();
					uint256 hash;
					hash = BibleHash(x11_hash, pblock->GetBlockTime(), pindexPrev->nTime, true, pindexPrev->nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, pblock->nNonce);
					nBibleHashesDone += 1;
					nHashesDone += 1;
					nThreadWork += 1;
					
					if (fPoolMiningMode)
					{
						if (UintToArith256(hash) <= hashTargetPool)
						{
							bool fNonce = CheckNonce(f9000, pblock->nNonce, pindexPrev->nHeight, pindexPrev->nTime, pblock->GetBlockTime());

							if (UintToArith256(hash) <= hashTargetPool && fNonce)
							{
								if ((GetAdjustedTime() - nLastShareSubmitted) > (2*60))
								{
									nLastShareSubmitted = GetAdjustedTime();
									UpdatePoolProgress(pblock, sPoolMiningAddress, hashTargetPool, pindexPrev, sMinerGuid, sWorkID, iThreadID, nThreadWork, nThreadStart, pblock->nNonce);
									hashTargetPool = UintToArith256(uint256S("0x0"));
									nThreadStart = GetTimeMillis();
									nThreadWork = 0;
									SetThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL);
									break;
     							}
							}
						}
					}

					if (UintToArith256(hash) <= hashTarget)
					{
						bool fNonce = CheckNonce(f9000, pblock->nNonce, pindexPrev->nHeight, pindexPrev->nTime, pblock->GetBlockTime());
						if (fNonce)
						{
							// Found a solution
							SetThreadPriority(THREAD_PRIORITY_NORMAL);
							ProcessBlockFound(pblock, chainparams);
							SetThreadPriority(THREAD_PRIORITY_LOWEST);
							coinbaseScript->KeepScript();
							// In regression test mode, stop mining after a block is found. This
							// allows developers to controllably generate a block on demand.
							if (chainparams.MineBlocksOnDemand())
									throw boost::thread_interrupted();
							break;
						}
					}
						
					pblock->nNonce += 1;
			
					// 0x4FFF is approximately 20 seconds, then we update hashmeter
					if ((pblock->nNonce & 0x4FFF) == 0)
						break;
					
					if ((pblock->nNonce & 0xFF) == 0)
					{
						bool fNonce = CheckNonce(f9000, pblock->nNonce, pindexPrev->nHeight, pindexPrev->nTime, pblock->GetBlockTime());
						if (fNonce) nHashCounterGood += 0xFF;
						if (!fNonce)
						{
							pblock->nNonce = 0x9FFF;
							competetiveMiningTithe += 1; //Add One satoshi
							caGlobalCompetetiveMiningTithe += 1;
							// LogPrintf("Resetting mining tithe %f",(double)caGlobalCompetetiveMiningTithe);
							break;
						}

					}
                }
				//
				if (competetiveMiningTithe > (5 * COIN)) competetiveMiningTithe = 0;

				// Update HashesPerSec
				nHashCounter += nHashesDone;
				nHashesDone = 0;
				nBibleHashCounter += nBibleHashesDone;
				nBibleMinerPulse++;
				dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
				iBibleMinerCount++;

                // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                
				if (fPoolMiningMode && (!sPoolMiningAddress.empty()) && ((GetAdjustedTime() - nLastReadyToMine) > 7*60))
				{
					if (fDebugMaster) LogPrintf(" Pool mining hard block; checking for new work; \n");
					hashTargetPool = UintToArith256(uint256S("0x0"));
					WriteCache("poolthread"+RoundToString(iThreadID,0),"poolinfo3","CFW " + TimestampToHRDate(GetAdjustedTime()),GetAdjustedTime());
					break;
				}

				if ((GetAdjustedTime() - nLastClearCache) > (10 * 60))
				{
					nLastClearCache=GetAdjustedTime();
					WriteCache("poolcache", "pooladdress", "", GetAdjustedTime());
					ClearCache("poolcache");
					ClearCache("poolthread" + RoundToString(iThreadID, 0));
					WriteCache("pool" + RoundToString(iThreadID, 0),"communication","0",GetAdjustedTime());
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

				if (vNodes.empty() && chainparams.MiningRequiresPeers())
                    break;

                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
				{
					competetiveMiningTithe = 0;
                    break;
				}

                if (pindexPrev != chainActive.Tip() || pindexPrev==NULL || chainActive.Tip()==NULL)
				{
					competetiveMiningTithe = 0;
					break;
				}

				if (pblock->nNonce >= 0x9FFF)
                    break;
	                        
                // Update nTime every few seconds
			    if (pindexPrev)
				{
					if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
					{
						competetiveMiningTithe = 0;
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
        LogPrintf("\r\nBiblepayMiner -- terminated\n");
		dHashesPerSec = 0;
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("\r\nBiblepayMiner -- runtime error: %s\n", e.what());
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
	int iBibleNumber = 0;			
    for (int i = 0; i < nThreads; i++)
	{
		ClearCache("poolthread" + RoundToString(i,0));
	    minerThreads->create_thread(boost::bind(&BibleMiner, boost::cref(chainparams), boost::cref(i), boost::cref(iBibleNumber)));
		LogPrintf(" Starting Thread #%f with Bible #%f      ",(double)i,(double)iBibleNumber);
	    MilliSleep(100); // Avoid races
	}
	iMinerThreadCount = nThreads;

	// Maintain the HashPS
	nHPSTimerStart = GetTimeMillis();
	nHashCounter = 0;
	nBibleHashCounter = 0;
	LogPrintf(" ** Started %f BibleMiner threads. ** \r\n",(double)nThreads);
}
