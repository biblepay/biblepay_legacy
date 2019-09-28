// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The BiblePay Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "rpcpog.h"
#include "rpcpodc.h"
#include "kjv.h"
#include "coins.h"
#include "core_io.h"
#include "consensus/validation.h"
#include "instantx.h"
#include "validation.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "streams.h"
#include "spork.h"
#include "sync.h"
#include "txdb.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "hash.h"
#include "governance-classes.h"
#include "evo/specialtx.h"
#include "evo/cbtx.h"
#include "smartcontract-client.h"
#include "smartcontract-server.h"
#include "masternode-sync.h"
#include <stdint.h>

#include <univalue.h>

#include <boost/thread/thread.hpp> // boost::thread::interrupt
#include <boost/algorithm/string.hpp> // boost::trim
#include <mutex>
#include <condition_variable>

struct CUpdatedBlock
{
    uint256 hash;
    int height;
};

static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
UniValue protx_register(const JSONRPCRequest& request);
UniValue protx(const JSONRPCRequest& request);
UniValue _bls(const JSONRPCRequest& request);
UniValue gobject_vote_many(const JSONRPCRequest& request);

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

    return dDiff * POBH_FACTOR;
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", blockindex->nVersion)));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)blockindex->nNonce));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToJSON(*tx, uint256(), objTx);
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx->GetHash().GetHex());
    }
    result.push_back(Pair("tx", txs));
    if (!block.vtx[0]->vExtraPayload.empty()) {
        CCbTx cbTx;
        if (GetTxPayload(block.vtx[0]->vExtraPayload, cbTx)) {
            UniValue cbTxObj;
            cbTx.ToJson(cbTxObj);
            result.push_back(Pair("cbTx", cbTxObj));
        }
    }
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
	result.push_back(Pair("hrtime", TimestampToHRDate(block.GetBlockTime())));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
	result.push_back(Pair("subsidy", block.vtx[0]->vout[0].nValue/COIN));
	result.push_back(Pair("anti-botnet-weight", GetABNWeight(block, false)));
	std::string sCPK;
	CheckABNSignature(block, sCPK);
	if (!sCPK.empty())
		result.push_back(Pair("cpk", sCPK));

	result.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0]->vout[0].sTxOutMessage)));
	if (block.vtx.size() > 1)
		result.push_back(Pair("sanctuary_reward", block.vtx[0]->vout[1].nValue/COIN));
	// BiblePay
	bool bShowPrayers = true;

    if (blockindex->pprev)
	{
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
		const Consensus::Params& consensusParams = Params().GetConsensus();
		std::string sVerses = GetBibleHashVerses(block.GetHash(), block.GetBlockTime(), blockindex->pprev->nTime, blockindex->pprev->nHeight, blockindex->pprev);
		if (bShowPrayers) result.push_back(Pair("verses", sVerses));
        // Check work against BibleHash
		bool f7000;
		bool f8000;
		bool f9000;
		bool fTitheBlocksActive;
		GetMiningParams(blockindex->pprev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);
		arith_uint256 hashTarget = arith_uint256().SetCompact(blockindex->nBits);
		uint256 hashWork = blockindex->GetBlockHash();
		uint256 bibleHash = BibleHashClassic(hashWork, block.GetBlockTime(), blockindex->pprev->nTime, false, blockindex->pprev->nHeight, blockindex->pprev, false, f7000, f8000, f9000, fTitheBlocksActive, blockindex->nNonce, consensusParams);
		bool bSatisfiesBibleHash = (UintToArith256(bibleHash) <= hashTarget);
		if (fDebugSpam)
			result.push_back(Pair("satisfiesbiblehash", bSatisfiesBibleHash ? "true" : "false"));
		result.push_back(Pair("biblehash", bibleHash.GetHex()));
		result.push_back(Pair("chaindata", block.vtx[0]->vout[0].sTxOutMessage));
		bool fEnabled = sporkManager.IsSporkActive(SPORK_20_QUANTITATIVE_TIGHTENING_ENABLED);
		if (fEnabled)
		{
			double dPriorPrice = 0;
			double dPriorPhase = 0;
			double dQTPct = GetQTPhase(false, -1, blockindex->nHeight, dPriorPrice, dPriorPhase) / 100;
			result.push_back(Pair("qt_pct", dQTPct));
		}
	}
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
	// Genesis Block only:
	if (blockindex && blockindex->nHeight==0)
	{
		int iStart=0;
		int iEnd=0;
		// Display a verse from Genesis 1:1 for The Genesis Block:
		GetBookStartEnd("gen",iStart,iEnd);
		std::string sVerse = GetVerse("gen", 1, 1, iStart-1, iEnd);
		boost::trim(sVerse);
		result.push_back(Pair("verses", sVerse));
	}

    return result;
}

UniValue getblockcount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest blockchain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest blockchain.\n"
            "\nResult:\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples:\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex * pindex)
{
    if(pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange);
        latestblock.hash = pindex->GetBlockHash();
        latestblock.height = pindex->nHeight;
    }
	cond_blockchange.notify_all();
}

UniValue waitfornewblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "waitfornewblock (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitfornewblock", "1000")
            + HelpExampleRpc("waitfornewblock", "1000")
        );
    int timeout = 0;
    if (request.params.size() > 0)
        timeout = request.params[0].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        block = latestblock;
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        else
            cond_blockchange.wait(lock, [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblock <blockhash> (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. \"blockhash\" (required, string) Block hash to wait for.\n"
            "2. timeout       (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
            + HelpExampleRpc("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
        );
    int timeout = 0;

    uint256 hash = uint256S(request.params[0].get_str());

    if (request.params.size() > 1)
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&hash]{return latestblock.hash == hash || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&hash]{return latestblock.hash == hash || !IsRPCRunning(); });
        block = latestblock;
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblockheight(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblockheight <height> (timeout)\n"
            "\nWaits for (at least) block height and returns the height and hash\n"
            "of the current tip.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. height  (required, int) Block height to wait for (int)\n"
            "2. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblockheight", "\"100\", 1000")
            + HelpExampleRpc("waitforblockheight", "\"100\", 1000")
        );
    int timeout = 0;

    int height = request.params[0].get_int();

    if (request.params.size() > 1)
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&height]{return latestblock.height >= height || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&height]{return latestblock.height >= height || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue getdifficulty(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetDifficulty();
}

std::string EntryDescriptionString()
{
    return "    \"size\" : n,                 (numeric) transaction size in bytes\n"
           "    \"fee\" : n,                  (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
           "    \"modifiedfee\" : n,          (numeric) transaction fee with fee deltas used for mining priority\n"
           "    \"time\" : n,                 (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,               (numeric) block height when transaction entered pool\n"
           "    \"startingpriority\" : n,     (numeric) DEPRECATED. Priority when transaction entered pool\n"
           "    \"currentpriority\" : n,      (numeric) DEPRECATED. Transaction priority now\n"
           "    \"descendantcount\" : n,      (numeric) number of in-mempool descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,       (numeric) size of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,       (numeric) modified fees (see above) of in-mempool descendants (including this one)\n"
           "    \"ancestorcount\" : n,        (numeric) number of in-mempool ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,         (numeric) size of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,         (numeric) modified fees (see above) of in-mempool ancestors (including this one)\n"
           "    \"depends\" : [               (array) unconfirmed transactions used as inputs for this transaction\n"
           "        \"transactionid\",        (string) parent transaction id\n"
           "       ... ],\n"
           "    \"instantsend\" : true|false, (boolean) True if this transaction was sent as an InstantSend one\n"
           "    \"instantlock\" : true|false  (boolean) True if this transaction was locked via InstantSend\n";
}

void entryToJSON(UniValue &info, const CTxMemPoolEntry &e)
{
    AssertLockHeld(mempool.cs);

    info.push_back(Pair("size", (int)e.GetTxSize()));
    info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
    info.push_back(Pair("modifiedfee", ValueFromAmount(e.GetModifiedFee())));
    info.push_back(Pair("time", e.GetTime()));
    info.push_back(Pair("height", (int)e.GetHeight()));
    info.push_back(Pair("startingpriority", e.GetPriority(e.GetHeight())));
    info.push_back(Pair("currentpriority", e.GetPriority(chainActive.Height())));
    info.push_back(Pair("descendantcount", e.GetCountWithDescendants()));
    info.push_back(Pair("descendantsize", e.GetSizeWithDescendants()));
    info.push_back(Pair("descendantfees", e.GetModFeesWithDescendants()));
    info.push_back(Pair("ancestorcount", e.GetCountWithAncestors()));
    info.push_back(Pair("ancestorsize", e.GetSizeWithAncestors()));
    info.push_back(Pair("ancestorfees", e.GetModFeesWithAncestors()));
    const CTransaction& tx = e.GetTx();
    std::set<std::string> setDepends;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        if (mempool.exists(txin.prevout.hash))
            setDepends.insert(txin.prevout.hash.ToString());
    }

    UniValue depends(UniValue::VARR);
    BOOST_FOREACH(const std::string& dep, setDepends)
    {
        depends.push_back(dep);
    }

    info.push_back(Pair("depends", depends));
    info.push_back(Pair("instantsend", instantsend.HasTxLockRequest(tx.GetHash())));
    info.push_back(Pair("instantlock", instantsend.IsLockedInstantSendTransaction(tx.GetHash())));
}

UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        BOOST_FOREACH(const CTxMemPoolEntry& e, mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        BOOST_FOREACH(const uint256& hash, vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nHint: use getmempoolentry to fetch a specific transaction from the mempool.\n"
            "\nArguments:\n"
            "1. verbose (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    bool fVerbose = false;
    if (request.params.size() > 0)
        fVerbose = request.params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getmempoolancestors(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempoolancestors txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool ancestors.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool ancestor transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolancestors", "\"mytxid\"")
            + HelpExampleRpc("getmempoolancestors", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (request.params.size() > 1)
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    mempool.CalculateMemPoolAncestors(*it, setAncestors, noLimit, noLimit, noLimit, noLimit, dummy, false);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        BOOST_FOREACH(CTxMemPool::txiter ancestorIt, setAncestors) {
            o.push_back(ancestorIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        BOOST_FOREACH(CTxMemPool::txiter ancestorIt, setAncestors) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempooldescendants(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempooldescendants txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool descendants.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool descendant transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempooldescendants", "\"mytxid\"")
            + HelpExampleRpc("getmempooldescendants", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (request.params.size() > 1)
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setDescendants;
    mempool.CalculateDescendants(it, setDescendants);
    // CTxMemPool::CalculateDescendants will include the given tx
    setDescendants.erase(it);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        BOOST_FOREACH(CTxMemPool::txiter descendantIt, setDescendants) {
            o.push_back(descendantIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        BOOST_FOREACH(CTxMemPool::txiter descendantIt, setDescendants) {
            const CTxMemPoolEntry &e = *descendantIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempoolentry(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getmempoolentry txid\n"
            "\nReturns mempool data for given transaction\n"
            "\nArguments:\n"
            "1. \"txid\"                   (string, required) The transaction id (must be in mempool)\n"
            "\nResult:\n"
            "{                           (json object)\n"
            + EntryDescriptionString()
            + "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolentry", "\"mytxid\"")
            + HelpExampleRpc("getmempoolentry", "\"mytxid\"")
        );
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    const CTxMemPoolEntry &e = *it;
    UniValue info(UniValue::VOBJ);
    entryToJSON(info, e);
    return info;
}

UniValue getblockhashes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "getblockhashes timestamp\n"
            "\nReturns array of hashes of blocks within the timestamp range provided.\n"
            "\nArguments:\n"
            "1. high         (numeric, required) The newer block timestamp\n"
            "2. low          (numeric, required) The older block timestamp\n"
            "\nResult:\n"
            "[\n"
            "  \"hash\"         (string) The block hash\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhashes", "1231614698 1231024505")
            + HelpExampleRpc("getblockhashes", "1231614698, 1231024505")
        );

    unsigned int high = request.params[0].get_int();
    unsigned int low = request.params[1].get_int();
    std::vector<uint256> blockHashes;

    if (!GetTimestampIndex(high, low, blockHashes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for block hashes");
    }

    UniValue result(UniValue::VARR);
    for (std::vector<uint256>::const_iterator it=blockHashes.begin(); it!=blockHashes.end(); it++) {
        result.push_back(it->GetHex());
    }

    return result;
}

UniValue getblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getblockhash height\n"
            "\nReturns hash of block in best-block-chain at height provided.\n"
            "\nArguments:\n"
            "1. height         (numeric, required) The height index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about blockheader <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (request.params.size() > 1)
        fVerbose = request.params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblockheaders(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "getblockheaders \"hash\" ( count verbose )\n"
            "\nReturns an array of items with information about <count> blockheaders starting from <hash>.\n"
            "\nIf verbose is false, each item is a string that is serialized, hex-encoded data for a single blockheader.\n"
            "If verbose is true, each item is an Object with information about a single blockheader.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. count           (numeric, optional, default/max=" + strprintf("%s", MAX_HEADERS_RESULTS) +")\n"
            "3. verbose         (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "[ {\n"
            "  \"hash\" : \"hash\",               (string)  The block hash\n"
            "  \"confirmations\" : n,           (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,                  (numeric) The block height or index\n"
            "  \"version\" : n,                 (numeric) The block version\n"
            "  \"merkleroot\" : \"xxxx\",         (string)  The merkle root\n"
            "  \"time\" : ttt,                  (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,            (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,                   (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\",           (string)  The bits\n"
            "  \"difficulty\" : x.xxx,          (numeric) The difficulty\n"
            "  \"chainwork\" : \"0000...1f3\"     (string)  Expected number of hashes required to produce the current chain (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string)  The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string)  The hash of the next block\n"
            "}, {\n"
            "       ...\n"
            "   },\n"
            "...\n"
            "]\n"
            "\nResult (for verbose=false):\n"
            "[\n"
            "  \"data\",                        (string)  A string that is serialized, hex-encoded data for block header.\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheaders", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\" 2000")
            + HelpExampleRpc("getblockheaders", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\" 2000")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    int nCount = MAX_HEADERS_RESULTS;
    if (request.params.size() > 1)
        nCount = request.params[1].get_int();

    if (nCount <= 0 || nCount > (int)MAX_HEADERS_RESULTS)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Count is out of range");

    bool fVerbose = true;
    if (request.params.size() > 2)
        fVerbose = request.params[2].get_bool();

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    UniValue arrHeaders(UniValue::VARR);

    if (!fVerbose)
    {
        for (; pblockindex; pblockindex = chainActive.Next(pblockindex))
        {
            CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
            ssBlock << pblockindex->GetBlockHeader();
            std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
            arrHeaders.push_back(strHex);
            if (--nCount <= 0)
                break;
        }
        return arrHeaders;
    }

    for (; pblockindex; pblockindex = chainActive.Next(pblockindex))
    {
        arrHeaders.push_back(blockheaderToJSON(pblockindex));
        if (--nCount <= 0)
            break;
    }

    return arrHeaders;
}

UniValue getblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblock \"blockhash\" ( verbosity ) \n"
            "\nIf verbosity is 0, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbosity is 1, returns an Object with information about block <hash>.\n"
            "If verbosity is 2, returns an Object with information about block <hash> and information about each transaction. \n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. verbosity              (numeric, optional, default=1) 0 for hex-encoded data, 1 for a json object, and 2 for json object with transaction data\n"
            "\nResult (for verbosity = 0):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nResult (for verbose = 1):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                     Same output as verbosity = 1.\n"
            "  \"tx\" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     Same output as verbosity = 1.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
            + HelpExampleRpc("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (request.params.size() > 1) {
        if(request.params[1].isNum())
            verbosity = request.params[1].get_int();
        else
            verbosity = request.params[1].get_bool() ? 1 : 0;
    }
	int NUMBER_LENGTH_NON_HASH = 10;
	if (strHash.length() < NUMBER_LENGTH_NON_HASH && !strHash.empty())
	{
		CBlockIndex* bindex = FindBlockByHeight(cdbl(strHash, 0));
		if (bindex==NULL)
		    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found by height");
		hash = bindex->GetBlockHash();
	}
   
    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (verbosity <= 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, verbosity >= 2);
}

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint256 hashSerialized;
    uint64_t nDiskSize;
    CAmount nTotalAmount;

    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nTotalAmount(0) {}
};

static void ApplyStats(CCoinsStats &stats, CHashWriter& ss, const uint256& hash, const std::map<uint32_t, Coin>& outputs)
{
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(outputs.begin()->second.nHeight * 2 + outputs.begin()->second.fCoinBase);
    stats.nTransactions++;
    for (const auto output : outputs) {
        ss << VARINT(output.first + 1);
        ss << *(const CScriptBase*)(&output.second.out.scriptPubKey);
        ss << VARINT(output.second.out.nValue);
        stats.nTransactionOutputs++;
        stats.nTotalAmount += output.second.out.nValue;
    }
    ss << VARINT(0);
}

//! Calculate statistics about the unspent transaction output set
static bool GetUTXOStats(CCoinsView *view, CCoinsStats &stats)
{
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, Coin> outputs;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            if (!outputs.empty() && key.hash != prevkey) {
                ApplyStats(stats, ss, prevkey, outputs);
                outputs.clear();
            }
            prevkey = key.hash;
            outputs[key.n] = std::move(coin);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    if (!outputs.empty()) {
        ApplyStats(stats, ss, prevkey, outputs);
    }
    stats.hashSerialized = ss.GetHash();
    stats.nDiskSize = view->EstimateSize();
    return true;
}

UniValue pruneblockchain(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "pruneblockchain\n"
            "\nArguments:\n"
            "1. \"height\"       (numeric, required) The block height to prune up to. May be set to a discrete height, or a unix timestamp\n"
            "                  to prune blocks whose block time is at least 2 hours older than the provided timestamp.\n"
            "\nResult:\n"
            "n    (numeric) Height of the last block pruned.\n"
            "\nExamples:\n"
            + HelpExampleCli("pruneblockchain", "1000")
            + HelpExampleRpc("pruneblockchain", "1000"));

    if (!fPruneMode)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Cannot prune blocks because node is not in prune mode.");

    LOCK(cs_main);

    int heightParam = request.params[0].get_int();
    if (heightParam < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");

    // Height value more than a billion is too high to be a block height, and
    // too low to be a block time (corresponds to timestamp from Sep 2001).
    if (heightParam > 1000000000) {
        // Add a 2 hour buffer to include blocks which might have had old timestamps
        CBlockIndex* pindex = chainActive.FindEarliestAtLeast(heightParam - 7200);
        if (!pindex) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not find block with at least the specified timestamp.");
        }
        heightParam = pindex->nHeight;
    }

    unsigned int height = (unsigned int) heightParam;
    unsigned int chainHeight = (unsigned int) chainActive.Height();
    if (chainHeight < Params().PruneAfterHeight())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Blockchain is too short for pruning.");
    else if (height > chainHeight)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Blockchain is shorter than the attempted prune height.");
    else if (height > chainHeight - MIN_BLOCKS_TO_KEEP) {
        LogPrint("rpc", "Attempt to prune blocks close to the tip.  Retaining the minimum number of blocks.");
        height = chainHeight - MIN_BLOCKS_TO_KEEP;
    }

    PruneBlockFilesManual(height);
    return uint64_t(height);
}

UniValue gettxoutsetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
            "  \"transactions\": n,      (numeric) The number of transactions\n"
            "  \"txouts\": n,            (numeric) The number of unspent transaction outputs\n"
            "  \"hash_serialized\": \"hash\",   (string) The serialized hash\n"
            "  \"disk_size\": n,         (numeric) The estimated size of the chainstate on disk\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (GetUTXOStats(pcoinsdbview, stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("hash_serialized_2", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("disk_size", stats.nDiskSize));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

UniValue gettxout(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "gettxout \"txid\" n ( include_mempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout number\n"
            "3. include_mempool  (boolean, optional) Whether to include the mempool\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in " + CURRENCY_UNIT + "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of biblepay addresses\n"
            "        \"address\"     (string) biblepay address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"version\" : n,            (numeric) The version\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nView the details\n"
            + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("gettxout", "\"txid\", 1")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = request.params[1].get_int();
    COutPoint out(hash, n);
    bool fMempool = true;
    if (request.params.size() > 2)
        fMempool = request.params[2].get_bool();

    Coin coin;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoin(out, coin) || mempool.isSpent(out)) { // TODO: filtering spent coins should be done by the CCoinsViewMemPool
            return NullUniValue;
        }
    } else {
        if (!pcoinsTip->GetCoin(out, coin)) {
            return NullUniValue;
        }
    }

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if (coin.nHeight == MEMPOOL_HEIGHT) {
        ret.push_back(Pair("confirmations", 0));
    } else {
        ret.push_back(Pair("confirmations", (int64_t)(pindex->nHeight - coin.nHeight + 1)));
    }
    ret.push_back(Pair("value", ValueFromAmount(coin.out.nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coin.out.scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("coinbase", (bool)coin.fCoinBase));

    return ret;
}

UniValue verifychain(const JSONRPCRequest& request)
{
    int nCheckLevel = GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "verifychain ( checklevel nblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" + strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
            "2. nblocks      (numeric, optional, default=" + strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    if (request.params.size() > 0)
        nCheckLevel = request.params[0].get_int();
    if (request.params.size() > 1)
        nCheckDepth = request.params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip, nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    switch(version)
    {
        case 2:
            activated = pindex->nHeight >= consensusParams.BIP34Height;
            break;
        case 3:
            activated = pindex->nHeight >= consensusParams.BIP66Height;
            break;
        case 4:
            activated = pindex->nHeight >= consensusParams.BIP65Height;
            break;
    }
    rv.push_back(Pair("status", activated));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams)));
    return rv;
}

static UniValue BIP9SoftForkDesc(const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState) {
    case THRESHOLD_DEFINED: rv.push_back(Pair("status", "defined")); break;
    case THRESHOLD_STARTED: rv.push_back(Pair("status", "started")); break;
    case THRESHOLD_LOCKED_IN: rv.push_back(Pair("status", "locked_in")); break;
    case THRESHOLD_ACTIVE: rv.push_back(Pair("status", "active")); break;
    case THRESHOLD_FAILED: rv.push_back(Pair("status", "failed")); break;
    }
    if (THRESHOLD_STARTED == thresholdState)
    {
        rv.push_back(Pair("bit", consensusParams.vDeployments[id].bit));

        int nBlockCount = VersionBitsCountBlocksInWindow(chainActive.Tip(), consensusParams, id);
        int64_t nPeriod = consensusParams.vDeployments[id].nWindowSize ? consensusParams.vDeployments[id].nWindowSize : consensusParams.nMinerConfirmationWindow;
        int64_t nThreshold = consensusParams.vDeployments[id].nThreshold ? consensusParams.vDeployments[id].nThreshold : consensusParams.nRuleChangeActivationThreshold;
        int64_t nWindowStart = chainActive.Height() - (chainActive.Height() % nPeriod);
        rv.push_back(Pair("period", nPeriod));
        rv.push_back(Pair("threshold", nThreshold));
        rv.push_back(Pair("windowStart", nWindowStart));
        rv.push_back(Pair("windowBlocks", nBlockCount));
        rv.push_back(Pair("windowProgress", std::min(1.0, (double)nBlockCount / nThreshold)));
    }
    rv.push_back(Pair("startTime", consensusParams.vDeployments[id].nStartTime));
    rv.push_back(Pair("timeout", consensusParams.vDeployments[id].nTimeout));
    rv.push_back(Pair("since", VersionBitsTipStateSinceHeight(consensusParams, id)));
    return rv;
}

void BIP9SoftForkDescPushBack(UniValue& bip9_softforks, const std::string &name, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    // Deployments with timeout value of 0 are hidden.
    // A timeout value of 0 guarantees a softfork will never be activated.
    // This is used when softfork codes are merged without specifying the deployment schedule.
    if (consensusParams.vDeployments[id].nTimeout > 0)
        bip9_softforks.push_back(Pair(name, BIP9SoftForkDesc(consensusParams, id)));
}

UniValue getblockchaininfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding blockchain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",        (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,         (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,        (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\", (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,     (numeric) the current difficulty\n"
            "  \"mediantime\": xxxxxx,     (numeric) median time for the current best block\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"chainwork\": \"xxxx\"     (string) total amount of work in active chain, in hexadecimal\n"
            "  \"pruned\": xx,             (boolean) if the blocks are subject to pruning\n"
            "  \"pruneheight\": xxxxxx,    (numeric) lowest-height complete block stored\n"
            "  \"softforks\": [            (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of softfork\n"
            "        \"version\": xx,         (numeric) block version\n"
            "        \"reject\": {            (object) progress toward rejecting pre-softfork blocks\n"
            "           \"status\": xx,       (boolean) true if threshold reached\n"
            "        },\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": {          (object) status of BIP9 softforks in progress\n"
            "     \"xxxx\" : {                (string) name of the softfork\n"
            "        \"status\": \"xxxx\",    (string) one of \"defined\", \"started\", \"locked_in\", \"active\", \"failed\"\n"
            "        \"bit\": xx,             (numeric) the bit (0-28) in the block version field used to signal this softfork (only for \"started\" status)\n"
            "        \"period\": xx,          (numeric) the window size/period for this softfork (only for \"started\" status)\n"
            "        \"threshold\": xx,       (numeric) the threshold for this softfork (only for \"started\" status)\n"
            "        \"windowStart\": xx,     (numeric) the starting block height of the current window (only for \"started\" status)\n"
            "        \"windowBlocks\": xx,    (numeric) the number of blocks in the current window that had the version bit set for this softfork (only for \"started\" status)\n"
            "        \"windowProgress\": xx,  (numeric) the progress (between 0 and 1) for activation of this softfork (only for \"started\" status)\n"
            "        \"startTime\": xx,       (numeric) the minimum median time past of a block at which the bit gains its meaning\n"
            "        \"timeout\": xx,         (numeric) the median time past of a block at which the deployment is considered failed if not yet locked in\n"
            "        \"since\": xx            (numeric) height of the first block to which the status applies\n"
            "     }\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain",                 Params().NetworkIDString()));
    obj.push_back(Pair("blocks",                (int)chainActive.Height()));
    obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty",            (double)GetDifficulty()));
    obj.push_back(Pair("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast()));
    obj.push_back(Pair("verificationprogress",  GuessVerificationProgress(Params().TxData(), chainActive.Tip())));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("pruned",                fPruneMode));

    const Consensus::Params& consensusParams = Params().GetConsensus();
    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
	/*
	DEPLOYMENT_CSV is a blockchain vote to store the <median time past> int64_t unixtime in the block.tx.nLockTime field.  Since the bitcoin behavior is to store the height, and we have pre-existing business logic that calculates height delta from this field, we don't want to switch to THRESHOLD_ACTIVE here.  Additionally, BiblePay enforces the allowable mining window (for block timestamps) to be within a 15 minute range.
	If we ever want to enable DEPLOYMENT_CSV, we need to change the chainparam deployment window to be the future, and check the POG business logic for height delta calculations.
	BIP9SoftForkDescPushBack(bip9_softforks, "csv", consensusParams, Consensus::DEPLOYMENT_CSV);
	DIP0001 is a vote to allow up to 2MB blocks.  BiblePay moved to 2MB blocks before DIP1 and therefore our vote never started, but our code uses the 2MB hardcoded literal (matching dash).  So we want to comment out this DIP1 versionbits response as it does not reflect our environment.
    BIP9SoftForkDescPushBack(bip9_softforks, "csv", consensusParams, Consensus::DEPLOYMENT_CSV);
    BIP9SoftForkDescPushBack(bip9_softforks, "dip0001", consensusParams, Consensus::DEPLOYMENT_DIP0001);
	*/
    BIP9SoftForkDescPushBack(bip9_softforks, "dip0003", consensusParams, Consensus::DEPLOYMENT_DIP0003);
    BIP9SoftForkDescPushBack(bip9_softforks, "bip147", consensusParams, Consensus::DEPLOYMENT_BIP147);
    obj.push_back(Pair("softforks",             softforks));
    obj.push_back(Pair("bip9_softforks", bip9_softforks));

    if (fPruneMode)
    {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA))
            block = block->pprev;

        obj.push_back(Pair("pruneheight",        block->nHeight));
    }
    return obj;
}

UniValue getchaintips(const JSONRPCRequest& request)
{
	    if (request.fHelp || request.params.size() > 4)
        throw std::runtime_error(
            "getchaintips ( count branchlen minimum_difficulty )\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nArguments:\n"
            "1. count       (numeric, optional) only show this much of latest tips\n"
            "2. branchlen   (numeric, optional) only show tips that have equal or greater length of branch\n"
			"3. minimum_diff(numeric, optional) only show tips that have equal or greater difficulty\n"
			"4. minimum_branch_length(numeric, optional) only show tips that have equal or greater branch length\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,             (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",             (string) block hash of the tip\n"
            "    \"difficulty\" : x.xxx,       (numeric) The difficulty\n"
            "    \"chainwork\" : \"0000...1f3\"  (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "    \"branchlen\": 0              (numeric) zero for main chain\n"
            "    \"status\": \"active\"          (string) \"active\" for the main chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"difficulty\" : x.xxx,\n"
            "    \"chainwork\" : \"0000...1f3\"\n"
            "    \"branchlen\": 1              (numeric) length of branch connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"            (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    /* Build up a list of chain tips.  We start with the list of all
       known blocks, and successively remove blocks that appear as pprev
       of another block.  */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
		
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
	{
		if (item.second != NULL) setTips.insert(item.second);
	}
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
		if (item.second != NULL)
		{
			if (item.second->pprev != NULL)
			{
				const CBlockIndex* pprev = item.second->pprev;
				if (pprev) setTips.erase(pprev);
			}
		}
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    int nBranchMin = -1;
    int nCountMax = INT_MAX;

    if(request.params.size() >= 1)
        nCountMax = request.params[0].get_int();

    if(request.params.size() == 2)
        nBranchMin = request.params[1].get_int();

	double nMinDiff = 0;
	if (request.params.size() == 3)
		nMinDiff = cdbl(request.params[2].get_str(), 4);

	double nMinBranchLen = 0;
	if (request.params.size() == 4)
		nMinBranchLen = cdbl(request.params[3].get_str(), 0);

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    BOOST_FOREACH(const CBlockIndex* block, setTips)
    {
        const CBlockIndex* pindexFork = chainActive.FindFork(block);
        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        if(branchLen < nBranchMin) continue;

        if(nCountMax-- < 1) break;

        UniValue obj(UniValue::VOBJ);
		bool bInclude = true;
		
		if (nMinDiff > 0 && GetDifficulty(block) < nMinDiff) 
			bInclude = false;
		
		if (block->nHeight < (chainActive.Tip()->nHeight * .65))
			bInclude = false;

		if (nMinBranchLen > 0 && branchLen < nMinBranchLen)
			bInclude = false;

		if (bInclude)
		{
			obj.push_back(Pair("height", block->nHeight));
			bool bSuperblock = (CSuperblock::IsValidBlockHeight(block->nHeight) || CSuperblock::IsSmartContract(block->nHeight));
			obj.push_back(Pair("superblock", bSuperblock));
			obj.push_back(Pair("hash", block->phashBlock->GetHex()));
			obj.push_back(Pair("difficulty", GetDifficulty(block)));
			obj.push_back(Pair("chainwork", block->nChainWork.GetHex()));
			obj.push_back(Pair("branchlen", branchLen));
			obj.push_back(Pair("forkpoint", pindexFork->phashBlock->GetHex()));
			obj.push_back(Pair("forkheight", pindexFork->nHeight));

			std::string status;
			if (chainActive.Contains(block)) 
			{
				// This block is part of the currently active chain.
				status = "active";
			} else if (block->nStatus & BLOCK_FAILED_MASK) {
				// This block or one of its ancestors is invalid.
				status = "invalid";
			} else if (block->nChainTx == 0) {
				// This block cannot be connected because full block data for it or one of its parents is missing.
				status = "headers-only";
			} else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
				// This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
				status = "valid-fork";
			} else if (block->IsValid(BLOCK_VALID_TREE)) {
				// The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
				status = "valid-headers";
			} else {
				// No clue.
				status = "unknown";
			}
			obj.push_back(Pair("status", status));

			res.push_back(obj);
		}
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t) mempool.size()));
    ret.push_back(Pair("bytes", (int64_t) mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t) mempool.DynamicMemoryUsage()));
    size_t maxmempool = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.push_back(Pair("maxmempool", (int64_t) maxmempool));
    ret.push_back(Pair("mempoolminfee", ValueFromAmount(mempool.GetMinFee(maxmempool).GetFeePerK())));

    return ret;
}

UniValue getmempoolinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"bytes\": xxxxx,              (numeric) Sum of all tx sizes\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee for tx to be accepted\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

UniValue preciousblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "preciousblock \"blockhash\"\n"
            "\nTreats a block as if it were received before others with the same work.\n"
            "\nA later preciousblock call can override the effect of an earlier one.\n"
            "\nThe effects of preciousblock are not retained across restarts.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as precious\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("preciousblock", "\"blockhash\"")
            + HelpExampleRpc("preciousblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CBlockIndex* pblockindex;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        pblockindex = mapBlockIndex[hash];
    }

    CValidationState state;
    PreciousBlock(state, Params(), pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue invalidateblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "invalidateblock \"blockhash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, Params(), pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

uint256 Sha256001(int nType, int nVersion, std::string data)
{
    CHash256 ctx;
	unsigned char *val = new unsigned char[data.length()+1];
	strcpy((char *)val, data.c_str());
	ctx.Write(val, data.length()+1);
    uint256 result;
	ctx.Finalize((unsigned char*)&result);
    return result;
}

std::string ScanSanctuaryConfigFile(std::string sName)
{
    int linenumber = 1;
    boost::filesystem::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathMasternodeConfigFile);
    if (!streamConfig.good()) 
		return std::string();
	for(std::string line; std::getline(streamConfig, line); linenumber++)
    {
        if(line.empty()) continue;
        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;
        if (iss >> comment) 
		{
            if(comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

		if (comment == sName)
		{
			streamConfig.close();
			return line;
		}
    }
    streamConfig.close();
    return std::string();
}

boost::filesystem::path GetGenericFilePath(std::string sPath)
{
    boost::filesystem::path pathConfigFile(sPath);
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

void AppendSanctuaryFile(std::string sFile, std::string sData)
{
    boost::filesystem::path pathDeterministicConfigFile = GetGenericFilePath(sFile);
    boost::filesystem::ifstream streamConfig(pathDeterministicConfigFile);
	bool fReadable = streamConfig.good();
	if (fReadable)
		streamConfig.close();
    FILE* configFile = fopen(pathDeterministicConfigFile.string().c_str(), "a");
    if (configFile != nullptr) 
	{
	    if (!fReadable) 
		{
            std::string strHeader = "# Deterministic Sanctuary Configuration File\n"
				"# Format: Sanctuary_Name IP:port(40000=prod,40001=testnet) BLS_Public_Key BLS_Private_Key Collateral_output_txid Collateral_output_index Pro-Registration-TxId Pro-Reg-Collateral-Address Pro-Reg-Sent-TxId\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
        }
    }
	fwrite(sData.c_str(), std::strlen(sData.c_str()), 1, configFile);
    fclose(configFile);
}

UniValue exec(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2  && request.params.size() != 3 && request.params.size() != 4 
		&& request.params.size() != 5 && request.params.size() != 6 && request.params.size() != 7))
        throw std::runtime_error(
		"exec <string::itemname> <string::parameter> \r\n"
        "Executes an RPC command by name. run exec COMMAND for more info \r\n"
		"Available Commands:\r\n"
    );

    std::string sItem = request.params[0].get_str();
	if (sItem.empty()) throw std::runtime_error("Command argument invalid.");

    UniValue results(UniValue::VOBJ);
	results.push_back(Pair("Command",sItem));
	if (sItem == "biblehash")
	{
		if (request.params.size() != 6)
			throw std::runtime_error("You must specify blockhash, blocktime, prevblocktime, prevheight, and nonce IE: run biblehash blockhash 12345 12234 100 256.");
		std::string sBlockHash = request.params[1].get_str();
		std::string sBlockTime = request.params[2].get_str();
		std::string sPrevBlockTime = request.params[3].get_str();
		std::string sPrevHeight = request.params[4].get_str();
		std::string sNonce = request.params[5].get_str();
		int64_t nHeight = cdbl(sPrevHeight,0);
		uint256 blockHash = uint256S("0x" + sBlockHash);
		int64_t nBlockTime = (int64_t)cdbl(sBlockTime,0);
		int64_t nPrevBlockTime = (int64_t)cdbl(sPrevBlockTime,0);
		unsigned int nNonce = cdbl(sNonce,0);
		if (!sBlockHash.empty() && nBlockTime > 0 && nPrevBlockTime > 0 && nHeight >= 0)
		{
			bool f7000;
			bool f8000;
			bool f9000;
			bool fTitheBlocksActive;
			const Consensus::Params& consensusParams = Params().GetConsensus();

			GetMiningParams(nHeight, f7000, f8000, f9000, fTitheBlocksActive);
			uint256 hash = BibleHashClassic(blockHash, nBlockTime, nPrevBlockTime, true, nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, nNonce, consensusParams);
			results.push_back(Pair("BibleHash",hash.GetHex()));
			uint256 hash2 = BibleHashV2(blockHash, nBlockTime, nPrevBlockTime, true, nHeight);
			results.push_back(Pair("BibleHashV2", hash2.GetHex()));
		}
	}
	else if (sItem == "subsidy")
	{
		// Used by the Pools
		if (request.params.size() != 2) 
			throw std::runtime_error("You must specify height.");
		std::string sHeight = request.params[1].get_str();
		int64_t nHeight = (int64_t)cdbl(sHeight,0);
		if (nHeight >= 1 && nHeight <= chainActive.Tip()->nHeight)
		{
			CBlockIndex* pindex = FindBlockByHeight(nHeight);
			const Consensus::Params& consensusParams = Params().GetConsensus();
			if (pindex)
			{
				CBlock block;
				if (ReadBlockFromDisk(block, pindex, consensusParams)) 
				{
        			results.push_back(Pair("subsidy", block.vtx[0]->vout[0].nValue/COIN));
					std::string sRecipient = PubKeyToAddress(block.vtx[0]->vout[0].scriptPubKey);
					results.push_back(Pair("recipient", sRecipient));
					results.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0]->vout[0].sTxOutMessage)));
					results.push_back(Pair("minerguid", ExtractXML(block.vtx[0]->vout[0].sTxOutMessage,"<MINERGUID>","</MINERGUID>")));
				}
			}
		}
		else
		{
			results.push_back(Pair("error","block not found"));
		}
	}
	else if (sItem == "pinfo")
	{
		// Used by the Pools
		results.push_back(Pair("height", chainActive.Tip()->nHeight));
		int64_t nElapsed = GetAdjustedTime() - chainActive.Tip()->nTime;
		int64_t nMN = nElapsed * 256;
		if (nElapsed > (30 * 60)) 
			nMN=999999999;
		if (nMN < 512) nMN = 512;
		results.push_back(Pair("pinfo", nMN));
		results.push_back(Pair("elapsed", nElapsed));
	}
	else if (sItem == "versioncheck")
	{
		std::string sNarr = GetVersionAlert();
		std::string sGithubVersion = GetGithubVersion();
		std::string sCurrentVersion = FormatFullVersion();
		results.push_back(Pair("Github_version", sGithubVersion));
		results.push_back(Pair("Current_version", sCurrentVersion));
		if (!sNarr.empty()) results.push_back(Pair("Alert", sNarr));
	}
	else if (sItem == "reassesschains")
	{
		int iWorkDone = ReassessAllChains();
		results.push_back(Pair("progress", iWorkDone));
	}
	else if (sItem == "autounlockpasswordlength")
	{
		results.push_back(Pair("Length", (double)msEncryptedString.size()));
	}
	else if (sItem == "ipfstest1")
	{
		std::string sResult = BiblePayHTTPSPost2(true, "5001", "192.168.0.153", "BiblePayGui/Welcome", "na", "gear.png");
		results.push_back(Pair("result", sResult));
	}
	else if (sItem == "testgscvote")
	{
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sContract = GetGSCContract(0, true); // As of iLastSuperblock height
		results.push_back(Pair("contract", sContract));
		std::string sAddresses;
		std::string sAmounts;
		int iVotes = 0;
		uint256 uGovObjHash = uint256S("0x0");
		uint256 uPAMHash = GetPAMHashByContract(sContract);
		results.push_back(Pair("pam_hash", uPAMHash.GetHex()));
	
		GetGSCGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
		std::string sError;
		results.push_back(Pair("govobjhash", uGovObjHash.GetHex()));
		results.push_back(Pair("Addresses", sAddresses));
		results.push_back(Pair("Amounts", sAmounts));
		double dTotal = AddVector(sAmounts, "|");
		results.push_back(Pair("Total_Target_Spend", dTotal));
		if (uGovObjHash == uint256S("0x0"))
		{
			// create the contract
			std::string sQuorumTrigger = SerializeSanctuaryQuorumTrigger(iLastSuperblock, iNextSuperblock, sContract);
			std::string sGobjectHash;
			SubmitGSCTrigger(sQuorumTrigger, sGobjectHash, sError);
			results.push_back(Pair("quorum_hex", sQuorumTrigger));
			// Add the contract explanation as JSON
			std::vector<unsigned char> v = ParseHex(sQuorumTrigger);
			std::string sMyQuorumTrigger(v.begin(), v.end());
			UniValue u(UniValue::VOBJ);
			u.read(sMyQuorumTrigger);
			std::string sMyJsonQuorumTrigger = u.write().c_str();
			results.push_back(Pair("quorum_json", sMyJsonQuorumTrigger));
			results.push_back(Pair("quorum_gobject_trigger_hash", sGobjectHash));
			results.push_back(Pair("quorum_error", sError));
		}
		results.push_back(Pair("protocol_version", PROTOCOL_VERSION));
		double nMinGSCProtocolVersion = GetSporkDouble("MIN_GSC_PROTO_VERSION", 0);
		bool bVersionSufficient = (PROTOCOL_VERSION >= nMinGSCProtocolVersion);
		results.push_back(Pair("min_gsc_proto_version", nMinGSCProtocolVersion));
		results.push_back(Pair("version_sufficient", bVersionSufficient));
		results.push_back(Pair("votes_for_my_contract", iVotes));
		int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
		results.push_back(Pair("required_votes", iRequiredVotes));
		results.push_back(Pair("last_superblock", iLastSuperblock));
		results.push_back(Pair("next_superblock", iNextSuperblock));
		CAmount nLastLimit = CSuperblock::GetPaymentsLimit(iLastSuperblock);
		results.push_back(Pair("last_payments_limit", (double)nLastLimit/COIN));
		CAmount nNextLimit = CSuperblock::GetPaymentsLimit(iNextSuperblock);
		results.push_back(Pair("next_payments_limit", (double)nNextLimit/COIN));
		bool fOverbudget = dTotal > (nNextLimit/COIN);
		results.push_back(Pair("overbudget", fOverbudget));
		if (fOverbudget)
			results.push_back(Pair("! CAUTION !", "Superblock exceeds budget, will be rejected."));
	
		bool fTriggered = CSuperblockManager::IsSuperblockTriggered(iNextSuperblock);
		results.push_back(Pair("next_superblock_triggered", fTriggered));

		std::string sReqPay = CSuperblockManager::GetRequiredPaymentsString(iNextSuperblock);
		results.push_back(Pair("next_superblock_req_payments", sReqPay));

		bool bRes = VoteForGSCContract(iNextSuperblock, sContract, sError);
		results.push_back(Pair("vote_result", bRes));
		results.push_back(Pair("vote_error", sError));
	}
	else if (sItem == "disablenetworkmonitor")
	{
		mvNetworkMonitor.clear();
		results.push_back(Pair("disabled", 1));
		fNetworkMonitor = false;
	}
	else if (sItem == "enablenetworkmonitor")
	{
		fNetworkMonitor = true;
		results.push_back(Pair("enabled", fNetworkMonitor));
	}
	else if (sItem == "networkmonitor")
	{
		results.push_back(Pair("NetworkMonitor", fNetworkMonitor));

	    for (auto ii : mvNetworkMonitor)
		{
			double nBytes = mvNetworkMonitor[ii.first];
			std::string sMessageType = ii.first;
	        results.push_back(Pair("Message Type " + sMessageType, nBytes));
	    }
	}
	else if (sItem == "votecleanuputil")
	{

		UniValue a = governance.VoteCleanup1();
		return a;

	}
	else if (sItem == "gobjectcleanuputil")
	{
		std::map<std::string, double> mvOutpointCount;
		mvOutpointCount.clear();
		// Cleans up old gobjects older than 1 day with 0 votes that have not been deleted
		// This is helpful to save bandwidth while we are waiting for the supermajority to upgrade
	    LOCK2(cs_main, governance.cs);
		std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
		int iCounted = 0;
		int iTotal = 0;
		for (const CGovernanceObject* pGovObj : objs) 
		{
			CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
			int64_t nAge = GetAdjustedTime() - myGov->GetCreationTime();
			int iYes = myGov->GetYesCount(VOTE_SIGNAL_FUNDING);
			int iDeleted = myGov->GetYesCount(VOTE_SIGNAL_DELETE);
			if (iYes == 0 && nAge > (60 * 60 * 24 * 1) && iDeleted == 0 && pGovObj->GetObjectType() == GOVERNANCE_OBJECT_TRIGGER)
			{
				JSONRPCRequest myVote;
				myVote.params.setArray();
				myVote.params.push_back("vote-many");
				myVote.params.push_back(myGov->GetHash().GetHex());
				myVote.params.push_back("delete");
				myVote.params.push_back("yes");
				UniValue myResult = gobject_vote_many(myVote);
				if (iCounted == 0)
				{
					results.push_back(Pair("Response", myResult));
				}

				results.push_back(Pair(pGovObj->GetHash().GetHex(), nAge));
				iCounted++;
			}
			else
			{
				iTotal++;
				/*
				results.push_back(Pair("SAV: " + pGovObj->GetHash().GetHex(), nAge));
				results.push_back(Pair("DLV", iDeleted));
				results.push_back(Pair("YV", iYes));
				*/
			}
			// Tally signing masternodes for summary report
			const COutPoint& masternodeOutpoint = myGov->GetMasternodeOutpoint();
			if (masternodeOutpoint != COutPoint()) 
			{
				mvOutpointCount[masternodeOutpoint.ToStringShort()]++;
			}

		}
		results.push_back(Pair("Total Votes", iCounted));
		results.push_back(Pair("Total Counted", iTotal));
	    for (auto ii : mvOutpointCount)
		{
			double nCount = mvOutpointCount[ii.first];
			std::string sOutpoint = ii.first;
	        results.push_back(Pair("SANC " + sOutpoint, nCount));
	    }
    }
	else if (sItem == "hexblocktocoinbase")
	{
		// This call is used by pools (pool.biblepay.org and purepool) to verify a serialized solution
		std::string sBlockHex = request.params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sBlockHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
		if (block.vtx.size() < 1)
		    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Deserialization Error");
    	results.push_back(Pair("txid", block.vtx[0]->GetHash().GetHex()));
		results.push_back(Pair("recipient", PubKeyToAddress(block.vtx[0]->vout[0].scriptPubKey)));
		CBlockIndex* pindexPrev = chainActive.Tip();
		bool f7000;
		bool f8000;
		bool f9000;
		bool fTitheBlocksActive;
		GetMiningParams(pindexPrev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);
		const Consensus::Params& consensusParams = Params().GetConsensus();

		uint256 hash = BibleHashClassic(block.GetHash(), block.GetBlockTime(), pindexPrev->nTime, true, pindexPrev->nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, block.nNonce, consensusParams);
		results.push_back(Pair("biblehash", hash.GetHex()));
		results.push_back(Pair("subsidy", block.vtx[0]->vout[0].nValue/COIN));
		results.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0]->vout[0].sTxOutMessage)));
		std::string sMsg;
		for (unsigned int i = 0; i < block.vtx[0]->vout.size(); i++)
		{
			sMsg += block.vtx[0]->vout[i].sTxOutMessage;
		}
		// Include abn weight in the reply
		double nABNWeight = GetABNWeight(block, false);
		double nMinRequiredABNWeight = GetSporkDouble("requiredabnweight", 0);
		double nABNHeight = GetSporkDouble("abnheight", 0);
		bool fABNPassed = true;
		if (nABNHeight > consensusParams.ABNHeight && pindexPrev->nHeight > nABNHeight && nMinRequiredABNWeight > 0 && !LateBlock(block, pindexPrev, 60) && !LateBlockIndex(pindexPrev, 60))
		{
			if (nABNWeight < nMinRequiredABNWeight) 
				fABNPassed = false;
		} 
		results.push_back(Pair("requiredabnweight", nMinRequiredABNWeight));
		results.push_back(Pair("block_abn_weight", nABNWeight));
		results.push_back(Pair("abn_passed", fABNPassed));
		results.push_back(Pair("blockmessage", sMsg));
	}
	else if (sItem == "search")
	{
		if (request.params.size() != 2 && request.params.size() != 3)
			throw std::runtime_error("You must specify type: IE 'exec search PRAYER'.  Optionally you may enter a search phrase: IE 'exec search PRAYER MOTHER'.");
		std::string sType = request.params[1].get_str();
		std::string sSearch = "";
		if (request.params.size() == 3) 
			sSearch = request.params[2].get_str();
		int iSpecificEntry = 0;
		std::string sEntry = "";
		int iDays = 30;
		UniValue aDataList = GetDataList(sType, iDays, iSpecificEntry, sSearch, sEntry);
		return aDataList;
	}
	else if (sItem == "getsporkdouble")
	{
		std::string sType = request.params[1].get_str();
		double dValue = GetSporkDouble(sType, 0);
		results.push_back(Pair(sType, dValue));
	}
	else if (sItem == "persistsporkmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec persistsporkmessage dcccomputingprojectname rosetta'";
		if (request.params.size() != 4)
			 throw std::runtime_error(sError);
		std::string sType = request.params[1].get_str();
		std::string sPrimaryKey = request.params[2].get_str();
		std::string sValue = request.params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw std::runtime_error(sError);
		sError;
		double dFee = fProd ? 10 : 5001;
    	std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, dFee, true, "", sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		if (!sError.empty()) results.push_back(Pair("Error", sError));
	}
	else if (sItem == "getabnweight")
	{
		CAmount nTotalReq = 0;
		double dABN = pwalletMain->GetAntiBotNetWalletWeight(0, nTotalReq);
		double dMin = 0;
		double dDebug = 0;
		if (request.params.size() > 1)
			dMin = cdbl(request.params[1].get_str(), 2);
		if (request.params.size() > 2)
			dDebug = cdbl(request.params[2].get_str(), 2);
		results.push_back(Pair("version", 2.7));
		results.push_back(Pair("weight", dABN));
		results.push_back(Pair("total_required", nTotalReq / COIN));
		if (dMin > 0) 
		{
			dABN = pwalletMain->GetAntiBotNetWalletWeight(dMin, nTotalReq);
			if (dDebug == 1)
			{
				std::string sData = ReadCache("coin", "age");
				if (sData.length() < 1000000)
					results.push_back(Pair("coin_age_data_pre_select", sData));
			}

			results.push_back(Pair("weight " + RoundToString(dMin, 2), dABN));
			results.push_back(Pair("total_required " + RoundToString(dMin, 2), nTotalReq/COIN));
		}
	}
	else if (sItem == "getpoints")
	{
		if (request.params.size() < 2)
			 throw std::runtime_error("You must specify the txid.");
		std::string sTxId = request.params[1].get_str();
		uint256 hashBlock = uint256();
		CTransactionRef tx;
		uint256 uTx = ParseHashV(sTxId, "txid");
		double nCoinAge = 0;
		CAmount nDonation = 0;
		if (GetTransaction(uTx, tx, Params().GetConsensus(), hashBlock, true))
		{
		    CBlockIndex* pblockindex = mapBlockIndex[hashBlock];
			if (!pblockindex) 
				throw std::runtime_error("bad blockindex for this tx.");
			GetTransactionPoints(pblockindex, tx, nCoinAge, nDonation);
			std::string sDiary = ExtractXML(tx->GetTxMessage(), "<diary>", "</diary>");
			std::string sCampaignName;
			std::string sCPK = GetTxCPK(tx, sCampaignName);
			double nPoints = CalculatePoints(sCampaignName, sDiary, nCoinAge, nDonation, sCPK);
			results.push_back(Pair("pog_points", nPoints));
			results.push_back(Pair("coin_age", nCoinAge));
			results.push_back(Pair("diary_entry", sDiary));
			results.push_back(Pair("orphan_donation", (double)nDonation / COIN));
		}
		else
		{
			results.push_back(Pair("error", "not found"));
		}
	}
	else if (sItem == "auditabntx")
	{
		if (request.params.size() < 2)
			 throw std::runtime_error("You must specify the txid.");
		std::string sTxId = request.params[1].get_str();
		uint256 hashBlock = uint256();
		CTransactionRef tx;
		uint256 uTx = ParseHashV(sTxId, "txid");
		if (GetTransaction(uTx, tx, Params().GetConsensus(), hashBlock, true))
		{
		    CBlockIndex* pblockindex = mapBlockIndex[hashBlock];
			if (!pblockindex) pblockindex = chainActive.Tip();
			double nAuditedWeight = GetAntiBotNetWeight(pblockindex->GetBlockTime(), tx, true, "");
			results.push_back(Pair("audited_weight", nAuditedWeight));
		}
		else
		{
			results.push_back(Pair("error", "not found"));
		}
	}
	else if (sItem == "createabn")
	{
		std::string sError;
		std::string sXML;
		WriteCache("vin", "coinage", "", GetAdjustedTime());
		double dTargetWeight = 0;
		if (request.params.size() > 1)
			dTargetWeight = cdbl(request.params[1].get_str(), 2);
		CReserveKey reserveKey(pwalletMain);
		CWalletTx wtx = CreateAntiBotNetTx(chainActive.Tip(), dTargetWeight, reserveKey, sXML, "ppk", sError);
	
		results.push_back(Pair("xml", sXML));
		results.push_back(Pair("err", sError));
		if (sError.empty())
		{
				results.push_back(Pair("coin_age_data_selected", ReadCache("availablecoins", "age")));
				results.push_back(Pair("success", wtx.GetHash().GetHex()));
				double nAuditedWeight = GetAntiBotNetWeight(chainActive.Tip()->GetBlockTime(), wtx.tx, true, "");
				std::string sData = ReadCache("coin", "age");
				if (sData.length() < 1000000)
					results.push_back(Pair("coin_age_data_pre_select", sData));
				results.push_back(Pair("audited_weight", nAuditedWeight));
				results.push_back(Pair("vin_coin_age_data", ReadCache("vin", "coinage")));
		}
		else
		{
			results.push_back(Pair("age_data", ReadCache("availablecoins", "age")));
			if (true)
			{
				double nAuditedWeight = GetAntiBotNetWeight(chainActive.Tip()->GetBlockTime(), wtx.tx, true, "");
				results.push_back(Pair("vin_coin_age_data", ReadCache("vin", "coinage")));
				std::string sData1 = ReadCache("coin", "age");
				if (sData1.length() < 1000000)
						results.push_back(Pair("coin_age_data_pre_select", sData1));
				results.push_back(Pair("total_audited_weight", nAuditedWeight));
			}
	
			results.push_back(Pair("tx_create_error", sError));
		}
	}
	else if (sItem == "cpk")
	{
		std::string sError;
		if (request.params.size() != 2 && request.params.size() != 3 && request.params.size() != 4 && request.params.size() != 5)
			throw std::runtime_error("You must specify exec cpk nickname [optional e-mail address] [optional vendortype=church/user/vendor] [optional: force=true/false].");
		std::string sNickName = request.params[1].get_str();
		bool fForce = false;
		std::string sEmail;
		std::string sVendorType;

		if (request.params.size() >= 3)
			sEmail = request.params[2].get_str();
		
		if (request.params.size() >= 4)
			sVendorType = request.params[3].get_str();

		if (request.params.size() >= 5)
			fForce = request.params[4].get_str() == "true" ? true : false;

		bool fAdv = AdvertiseChristianPublicKeypair("cpk", sNickName, sEmail, sVendorType, false, fForce, 0, "", sError);
		results.push_back(Pair("Results", fAdv));
		if (!fAdv)
			results.push_back(Pair("Error", sError));
	}
	else if (sItem == "sendmanyxml")
	{
		// BiblePay Pools: Allows pools to send a multi-output tx with ease
		// Format: exec sendmanyxml from_account xml_payload comment
		LOCK2(cs_main, pwalletMain->cs_wallet);
		std::string strAccount = request.params[1].get_str();
		if (strAccount == "*")
			throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
		std::string sXML = request.params[2].get_str();
		int nMinDepth = 1;
		CWalletTx wtx;
		wtx.strFromAccount = strAccount;
		wtx.mapValue["comment"] = request.params[3].get_str();
		std::set<CBitcoinAddress> setAddress;
		std::vector<CRecipient> vecSend;
		CAmount totalAmount = 0;
		std::string sRecipients = ExtractXML(sXML, "<RECIPIENTS>","</RECIPIENTS>");
		std::vector<std::string> vRecips = Split(sRecipients.c_str(), "<ROW>");
		for (int i = 0; i < (int)vRecips.size(); i++)
		{
			std::string sRecip = vRecips[i];
			if (!sRecip.empty())
			{
				std::string sRecipient = ExtractXML(sRecip, "<RECIPIENT>","</RECIPIENT>");
				double dAmount = cdbl(ExtractXML(sRecip,"<AMOUNT>","</AMOUNT>"),4);
				if (!sRecipient.empty() && dAmount > 0)
				{
 					  CBitcoinAddress address(sRecipient);
	   		   	      if (!address.IsValid())
						  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Biblepay address: ") + sRecipient);
					  if (setAddress.count(address))
						  throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + sRecipient);
					  setAddress.insert(address);
					  CScript scriptPubKey = GetScriptForDestination(address.Get());
					  CAmount nAmount = dAmount * COIN;
					  if (nAmount <= 0) 
						  throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
					  totalAmount += nAmount;
					  bool fSubtractFeeFromAmount = false;
				      CRecipient recipient = {scriptPubKey, nAmount, false, fSubtractFeeFromAmount};
					  vecSend.push_back(recipient);
				}
			}
		}
		EnsureWalletIsUnlocked();
		// Check funds
		CAmount nBalance = pwalletMain->GetAccountBalance(strAccount, nMinDepth, ISMINE_SPENDABLE, false);
		if (totalAmount > nBalance)
			throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");
		// Send
		CReserveKey keyChange(pwalletMain);
		CAmount nFeeRequired = 0;
		int nChangePosRet = -1;
		std::string strFailReason;
		bool fUseInstantSend = false;
		bool fUsePrivateSend = false;
		CValidationState state;
		bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason, NULL, true, fUsePrivateSend ? ONLY_DENOMINATED : ALL_COINS, fUseInstantSend);
		if (!fCreated)
			throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
		if (!pwalletMain->CommitTransaction(wtx, keyChange, g_connman.get(), state, NetMsgType::TX))
			throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
		results.push_back(Pair("txid", wtx.GetHash().GetHex()));
	}
	else if (sItem == "unjoin")
	{
		if (request.params.size() != 2)
			throw std::runtime_error("You must specify the project_name to un-join.");
		std::string sProject = request.params[1].get_str();
		std::string sError;
		if (!CheckCampaign(sProject))
			throw std::runtime_error("Campaign does not exist.");
		bool fAdv = AdvertiseChristianPublicKeypair("cpk-" + sProject, "", "", "", true, false, 0, "", sError);
		results.push_back(Pair("Results", fAdv));
		if (!fAdv)
			results.push_back(Pair("Error", sError));
	}
	else if (sItem == "register")
	{
		if (request.params.size() != 2)
			throw std::runtime_error("The purpose of this command is to register your nickname with BMS (the decentralized biblepay web).  This feature will not be available until December 2019.  \nYou must specify your nickname.");
		std::string sProject = "cpk-bmsuser";
		std::string sNN;
		sNN = request.params[1].get_str();
		boost::to_lower(sProject);
		std::string sError;
		bool fAdv = AdvertiseChristianPublicKeypair(sProject, "", sNN, "", false, true, 0, "", sError);
		results.push_back(Pair("Results", fAdv));
		if (!fAdv)
			results.push_back(Pair("Error", sError));
	}
	else if (sItem == "join")
	{
		if (request.params.size() != 2 && request.params.size() != 3)
			throw std::runtime_error("You must specify the project_name.  Optionally specify your nickname or sanctuary IP address.");
		std::string sProject = request.params[1].get_str();
		std::string sOptData;
		if (request.params.size() > 2)
			sOptData = request.params[2].get_str();
		boost::to_lower(sProject);
		std::string sError;
		if (!CheckCampaign(sProject))
			throw std::runtime_error("Campaign does not exist.");
		bool fAdv = AdvertiseChristianPublicKeypair("cpk-" + sProject, "", sOptData, "", false, false, 0, "", sError);
		results.push_back(Pair("Results", fAdv));
		if (!fAdv)
			results.push_back(Pair("Error", sError));
		if (fAdv && sProject == "healing")
		{
			std::string sURL = "https://wiki.biblepay.org/BiblePay_Healing_Campaign";
			std::string sNarr = "Please read this guide: " + sURL + " with critical instructions before attempting Spiritual Warfare or Street Healing.  Thank you for joining the BiblePay Healing campaign. ";
			results.push_back(Pair("Warning!", sNarr));
		}
	}
	else if (sItem == "getcampaigns")
	{
		UniValue c = GetCampaigns();
		return c;
	}
	else if (sItem == "checkcpk")
	{
		if (request.params.size() != 2)
			throw std::runtime_error("You must specify campaign name.");
		std::string sType = request.params[1].get_str();
		std::string sError;
		bool fEnrolled = Enrolled(sType, sError);
		if (!sError.empty())
			results.push_back(Pair("Error", sError));
		results.push_back(Pair("Enrolled_Results", fEnrolled));
	}
	else if (sItem == "bankroll")
	{
		if (request.params.size() != 3)
			throw std::runtime_error("You must specify type: IE 'exec bankroll quantity denomination'.  IE exec bankroll 10 100 (creates ten 100 BBP bills).");
		double nQty = cdbl(request.params[1].get_str(), 0);
		CAmount denomination = cdbl(request.params[2].get_str(), 4) * COIN;
		std::string sError = "";
		std::string sTxId = CreateBankrollDenominations(nQty, denomination, sError);
		if (!sError.empty())
		{
			if (sError == "Signing transaction failed") 
				sError += ".  (Please ensure your wallet is unlocked).";
			results.push_back(Pair("Error", sError));
		}
		else
		{
			results.push_back(Pair("TXID", sTxId));
		}
	}
	else if (sItem == "paycameroon")
	{
		if (request.params.size() != 4)
			throw std::runtime_error("You must specify childid amount_in_USD send_mode.  IE: exec paycameroon childID 40 [test/authorize].");
		std::string sError;
	   	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::string sChildID = request.params[1].get_str();
		double nAmountUSD = cdbl(request.params[2].get_str(), 2);
		std::string sSendMode = request.params[3].get_str();
		double dPriorPrice = 0;
		double dPriorPhase = 0;
		double dCurPhase = GetQTPhase(false, -1, chainActive.Tip()->nHeight, dPriorPrice, dPriorPhase);
		if (dPriorPrice < .00001)
		{
			sError = "BBP Price too low to use feature.  Price must be above .00001USD/BBP ";
			dPriorPrice = .00001;
		}

		if (nAmountUSD < 1)
		{
			sError += "You must enter a USD value greater than $1.00 to use this feature. ";
			nAmountUSD = .01;
		}

		bool fGood = VerifyChild(sChildID);
		if (!fGood || sChildID.empty())
			sError += "Invalid Child ID. (Not sponsored). ";

		if (sSendMode != "authorize")
		{
			sError += "Running in dry run mode. ";
		}

		double nAmount = cdbl(RoundToString(nAmountUSD / dPriorPrice, 2), 2);

		results.push_back(Pair("BBP/USD_Price", dPriorPrice));

		std::string sXML = "<cpk>" + sCPK + "</cpk><childid> " + sChildID + "</childid><amount_usd>" + RoundToString(nAmountUSD, 2) 
			+ "</amount_usd><amount>" + RoundToString(nAmount, 2) + "</amount>";
		std::string sDest = "BHRiFZYUpHj2r3gxw7pHyvByTUk1dGb8vz";
		CBitcoinAddress baDest(sDest);

		bool fSubtractFee = false;
		bool fInstantSend = false;
		CWalletTx wtx;
		bool fSent = false;
		if (sError.empty() && sSendMode == "authorize")
		{
			fSent = RPCSendMoney(sError, baDest.Get(), nAmount * COIN, fSubtractFee, wtx, fInstantSend, sXML);
		}

		if (!fSent)
		{
			results.push_back(Pair("Error", sError));
			results.push_back(Pair("BBPAmount", nAmount));
			results.push_back(Pair("USDAmount", nAmountUSD));
		}
		else
		{
			results.push_back(Pair("txid", wtx.GetHash().GetHex()));
			results.push_back(Pair("childid", sChildID));
			results.push_back(Pair("BBPAmount", nAmount));
			results.push_back(Pair("USDAmount", nAmountUSD));
		}
	}
	else if (sItem == "gschealth")
	{
		std::string sQH = CheckLastQuorumPopularHash();
		std::string sGSCH = CheckGSCHealth();
		results.push_back(Pair("QH", sQH));
		results.push_back(Pair("GSCH", sGSCH));
	}
	else if (sItem == "health")
	{
		// This command pulls the best-superblock (the one with the highest votes for the next height)
		bool bImpossible = (!masternodeSync.IsSynced() || fLiteMode);
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sAddresses;
		std::string sAmounts;
		int iVotes = 0;
		uint256 uGovObjHash = uint256S("0x0");
		uint256 uPAMHash = uint256S("0x0");
		GetGSCGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
		uint256 hPam = GetPAMHash(sAddresses, sAmounts);
		results.push_back(Pair("pam_hash", hPam.GetHex()));
		std::string sContract = GetGSCContract(iLastSuperblock, true);
		uint256 hPAMHash2 = GetPAMHashByContract(sContract);
		results.push_back(Pair("pam_hash_internal", hPAMHash2.GetHex()));
		if (hPAMHash2 != hPam)
		{
			results.push_back(Pair("WARNING", "Our internal PAM hash disagrees with the network. "));
		}
		results.push_back(Pair("govobjhash", uGovObjHash.GetHex()));
		int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
		results.push_back(Pair("Amounts", sAmounts));
		results.push_back(Pair("Addresses", sAddresses));
		results.push_back(Pair("votes", iVotes));
		results.push_back(Pair("required_votes", iRequiredVotes));
		results.push_back(Pair("last_superblock", iLastSuperblock));
		results.push_back(Pair("next_superblock", iNextSuperblock));
		bool fTriggered = CSuperblockManager::IsSuperblockTriggered(iNextSuperblock);
		results.push_back(Pair("next_superblock_triggered", fTriggered));
		if (bImpossible)
		{
			results.push_back(Pair("WARNING", "Running in Lite Mode or Sanctuaries are not synced."));
		}

		bool fHealthy = (!sAmounts.empty() && !sAddresses.empty() && uGovObjHash != uint256S("0x0")) || bImpossible;
		results.push_back(Pair("Healthy", fHealthy));
		bool fPassing = (iVotes >= iRequiredVotes);
	    results.push_back(Pair("GSC_Voted_In", fPassing));
	}
	else if (sItem == "watchman")
	{
		std::string sContract;
		std::string sResponse = WatchmanOnTheWall(true, sContract);
		results.push_back(Pair("Response", sResponse));
		results.push_back(Pair("Contract", sContract));
	}
	else if (sItem == "getgschashes")
	{
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sContract = GetGSCContract(0, true); // As of iLastSuperblock height
		results.push_back(Pair("Contract", sContract));
		uint256 hPAMHash = GetPAMHashByContract(sContract);
		std::string sData;
		GetGovObjDataByPamHash(iNextSuperblock, hPAMHash, sData);
		results.push_back(Pair("Data", sData));
	}
	else if (sItem == "antigpu")
	{
		if (request.params.size() != 2)
			throw std::runtime_error("You must specify height.");
		int nHeight = cdbl(request.params[1].get_str(), 0);
		CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
		if (pblockindex == NULL)   
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
		CBlock block;
		const Consensus::Params& consensusParams = Params().GetConsensus();
		if (ReadBlockFromDisk(block, pblockindex, consensusParams))
		{
			std::string sMsg = GetTransactionMessage(block.vtx[0]);
			int nABNLocator = (int)cdbl(ExtractXML(sMsg, "<abnlocator>", "</abnlocator>"), 0);
			if (block.vtx.size() >= nABNLocator) 
			{
				CTransactionRef tx = block.vtx[nABNLocator];

				std::string sCPK = ExtractXML(tx->GetTxMessage(), "<abncpk>", "</abncpk>");
				results.push_back(Pair("anti_gpu_xml", tx->GetTxMessage()));
				results.push_back(Pair("cpk", sCPK));
				bool fValid = CheckAntiBotNetSignature(tx, "abn", "");
				results.push_back(Pair("sig_valid", fValid));
				bool fAntiGPU = AntiGPU(block, pblockindex->pprev);
				results.push_back(Pair("anti-gpu", fAntiGPU));
			}
		}
		else
		{
			results.push_back(Pair("error", "Unable to read block."));
		}
	}
	else if (sItem == "price")
	{
		double dPriorPrice = 0;
		double dPriorPhase = 0;
		double dCurPhase = GetQTPhase(false, -1, chainActive.Tip()->nHeight, dPriorPrice, dPriorPhase);
		results.push_back(Pair("consensus_price", dPriorPrice));
		results.push_back(Pair("qt_phase", dCurPhase));
		results.push_back(Pair("qt_prior_phase", dPriorPhase));
		double out_BTC = 0;
		double dPrice = GetPBase(out_BTC);
		double dFuturePhase = GetQTPhase(true, dPrice, chainActive.Tip()->nHeight, dPriorPrice, dPriorPhase);
		results.push_back(Pair("qt_future_phase", dFuturePhase));
		bool fEnabled = sporkManager.IsSporkActive(SPORK_20_QUANTITATIVE_TIGHTENING_ENABLED);
		results.push_back(Pair("qt_enabled", fEnabled));
		results.push_back(Pair("cur_price", RoundToString(dPrice, 12)));
		double dBBP = GetCryptoPrice("bbp");
		double dBTC = GetCryptoPrice("btc");
		results.push_back(Pair("BBP/BTC", RoundToString(dBBP, 12)));
		results.push_back(Pair("BTC/USD", dBTC));
	}
	else if (sItem == "sentgsc")
	{
		if (request.params.size() > 3)
			throw std::runtime_error("sentgsc: Reports on the GSC transmissions and ABN transmissions over the last 7 days.  You may optionally specify the CPK and the height: sentgsc cpk height.");
		std::string sMyCPK;
		if (request.params.size() > 1)
			sMyCPK = request.params[1].get_str();
		if (sMyCPK.empty())
			sMyCPK = DefaultRecAddress("Christian-Public-Key");
		double nHeight = 0;
		if (request.params.size() > 2)
			nHeight = cdbl(request.params[2].get_str(), 0);

		UniValue s = SentGSCCReport(nHeight, sMyCPK);
		return s;
	}
	else if (sItem == "upgradesanc")
	{
		if (request.params.size() != 3)
			throw std::runtime_error("You must specify exec upgradesanc sanctuary_name (where the sanctuary_name matches the name in the masternode.conf file) 0/1 (where 0=dry-run, 1=real).");

		std::string sSearch = request.params[1].get_str();
		int iDryRun = cdbl(request.params[2].get_str(), 0);
		std::string sSanc = ScanSanctuaryConfigFile(sSearch);
		if (sSanc.empty())
			throw std::runtime_error("Unable to find sanctuary " + sSearch + " in masternode.conf file.");
		// Legacy Sanc (masternode.conf) data format: sanc_name, ip, mnp, collat, collat ordinal

		std::vector<std::string> vSanc = Split(sSanc.c_str(), " ");
		if (vSanc.size() < 5)
			throw std::runtime_error("Sanctuary entry in masternode.conf corrupted (does not contain 5 parts.)");

		std::string sSancName = vSanc[0];
		std::string sSancIP = vSanc[1];
		std::string sMNP = vSanc[2];
		std::string sCollateralTXID = vSanc[3];
		std::string sCollateralTXIDOrdinal = vSanc[4];
		std::string sSummary = "Creating protx_register command for Sanctuary " + sSancName + " with IP " + sSancIP + " with TXID " + sCollateralTXID;
		// Step 1: Fund the protx fee
		// 1a. Create the new deterministic-sanctuary reward address
		std::string sPayAddress = DefaultRecAddress(sSancName + "-d"); //d means deterministic

		CBitcoinAddress baPayAddress(sPayAddress);
		std::string sVotingAddress = DefaultRecAddress(sSancName + "-v"); //v means voting
		CBitcoinAddress baVotingAddress(sVotingAddress);

		std::string sError;
		std::string sData = "<protx></protx>";  // Reserved for future use

	    CWalletTx wtx;
		bool fSubtractFee = false;
		bool fInstantSend = false;
		// 1b. We must send 1BBP to ourself first here, as the deterministic sanctuaries future fund receiving address must be prefunded with enough funds to cover the non-financial transaction transmission below
		bool fSent = RPCSendMoney(sError, baPayAddress.Get(), 1 * COIN, fSubtractFee, wtx, fInstantSend, sData);

		if (!sError.empty() || !fSent)
			throw std::runtime_error("Unable to fund protx_register fee: " + sError);

		results.push_back(Pair("Summary", sSummary));
		// Generate BLS keypair (This is the keypair for the sanctuary - the BLS public key goes in the chain, the private key goes into the Sanctuaries biblepay.conf file like this: masternodeblsprivkey=nnnnn
		JSONRPCRequest myBLS;
		myBLS.params.setArray();
		myBLS.params.push_back("generate");
		UniValue myBLSPair = _bls(myBLS);
		std::string myBLSPublic = myBLSPair["public"].getValStr();
		std::string myBLSPrivate = myBLSPair["secret"].getValStr();
		
	    JSONRPCRequest newRequest;
		newRequest.params.setArray();
		// Pro-tx-register_prepare preparation format: protx register_prepare 1.55mm_collateralHash 1.55mm_index_collateralIndex ipv4:port_ipAndPort home_voting_address_ownerKeyAddr blsPubKey_operatorPubKey delegate_or_home_votingKeyAddr 0_pctOf_operatorReward payout_address_payoutAddress optional_(feeSourceAddress_of_Pro_tx_fee)

		newRequest.params.push_back("register_prepare");
		newRequest.params.push_back(sCollateralTXID);
		newRequest.params.push_back(sCollateralTXIDOrdinal);
		newRequest.params.push_back(sSancIP);
		
		newRequest.params.push_back(sVotingAddress);  // Home Voting Address
		newRequest.params.push_back(myBLSPublic);     // Remote Sanctuary Public Key (Private and public keypair is stored in deterministicsanctuary.conf on the controller wallet)
		newRequest.params.push_back(sVotingAddress);  // Delegates Voting address (This is a person that can vote for you if you want) - in our case its the same 

		newRequest.params.push_back("0");             // Pct of rewards to share with Operator (This is the amount of reward we want to share with a Sanc Operator - IE a hosting company)
		newRequest.params.push_back(sPayAddress);     // Rewards Pay To Address (This can be changed to be a wallet outside of your wallet, maybe a hardware wallet)
		// 1c.  First send the pro-tx-register_prepare command, and look for the tx, collateralAddress and signMessage response:
		UniValue rProReg = protx(newRequest);
		std::string sProRegTxId = rProReg["tx"].getValStr();
		std::string sProCollAddr = rProReg["collateralAddress"].getValStr();
		std::string sProSignMessage = rProReg["signMessage"].getValStr();
		if (sProSignMessage.empty() || sProRegTxId.empty())
			throw std::runtime_error("Failed to create pro reg tx.");
		// Step 2: Sign the Pro-Reg Tx
		JSONRPCRequest newSig;
		newSig.params.setArray();
		newSig.params.push_back(sProCollAddr);
		newSig.params.push_back(sProSignMessage);
		std::string sProSignature = SignMessageEvo(sProCollAddr, sProSignMessage, sError);
		if (!sError.empty())
			throw std::runtime_error("Unable to sign pro-reg-tx: " + sError);

		std::string sSentTxId;
		if (iDryRun == 1)
		{
			// Note: If this is Not a dry-run, go ahead and submit the non-financial transaction to the network here:
			JSONRPCRequest newSend;
			newSend.params.setArray();
			newSend.params.push_back("register_submit");
			newSend.params.push_back(sProRegTxId);
			newSend.params.push_back(sProSignature);
			UniValue rProReg = protx(newSend);
			results.push_back(rProReg);
			sSentTxId = rProReg.getValStr();
		}
		// Step 3: Report this info back to the user
		results.push_back(Pair("bls_public_key", myBLSPublic));
		results.push_back(Pair("bls_private_key", myBLSPrivate));
		results.push_back(Pair("pro_reg_txid", sProRegTxId));
		results.push_back(Pair("pro_reg_collateral_address", sProCollAddr));
		results.push_back(Pair("pro_reg_signed_message", sProSignMessage));
		results.push_back(Pair("pro_reg_signature", sProSignature));
		results.push_back(Pair("sent_txid", sSentTxId));
	    // Step 4: Store the new deterministic sanctuary in deterministicsanc.conf
		std::string sDSD = sSancName + " " + sSancIP + " " + myBLSPublic + " " + myBLSPrivate + " " + sCollateralTXID + " " + sCollateralTXIDOrdinal + " " + sProRegTxId + " " + sProCollAddr + " " + sSentTxId + "\n";
		if (iDryRun == 1)
			AppendSanctuaryFile("deterministic.conf", sDSD);
	}
	else if (sItem == "navtest_devsonly")
	{
		// ** This command is strictly for testing only by the devs - please disregard **
		// BiblePay - Purchase Plug-In API for web purchases
		// The users Public-Funding-Address keypair contains the user funds they will purchase with (send test funds here)
	    EnsureWalletIsUnlocked();
		// We authenticate with the CPK (this allows sites to not require log-in credentials, and to know the users nickname)
		// We DO NOT pass the CPKs private key outside of the wallet
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::string sPFA = DefaultRecAddress("Public-Funding-Address");
	    CBitcoinAddress pfaAddress;
		if (!pfaAddress.SetString(sPFA))
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Biblepay PFA address");
		CKeyID keyID;
		if (!pfaAddress.GetKeyID(keyID))
			throw JSONRPCError(RPC_TYPE_ERROR, "PFA Address does not refer to a key; have you created a PFA Key?");
		CKey vchSecret;
		if (!pwalletMain->GetKey(keyID, vchSecret))
			throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + sPFA + " is not in wallet.  Please create a PFA key to continue.");
		// This PrivKey will not be revealed to a sanctuary, or on the internet, but instead will be stored in a private place on the local computer, in the local browsers HTML5 local-storage database 
		// and cannot be accessed by web sites or javascript scripting attacks (as the key is stored in a private namespace)
		// PURPOSE: When the user decides to buy something from a BiblePay web-site, the BiblePay purchase api plugin will sign the purchase with this key (from local javascript in our namespace), then 
		// we will only transmit the transaction itself to the network.  (This way the user can conveniently browse and securely buy things on the web).
		// NOTE: The only private key we expose is "Public-Funding-Address", so be very careful and only fund this address with just enough BBP to complete test purchases.
		std::string sPFA_PrivKey = CBitcoinSecret(vchSecret).ToString();
		std::string sNonce = GetRandHash().GetHex();
		std::string sSignature;
		std::string sError;
		bool bSigned = SignStake(sCPK, sNonce, sError, sSignature);
		if (!bSigned || !sError.empty())
			throw JSONRPCError(RPC_TYPE_ERROR, "Unable to sign CPK [" + sError + "]");
		std::string sDestinationURL = "https://Unknown.com/purchase.aspx";
		std::string sData = sCPK + "|" + sNonce + "|" + sSignature + "|" + sPFA + "|" + sPFA_PrivKey + "|" + sDestinationURL;
		std::string sEncData = EncodeBase64(sData);
		msURL = "http://192.168.0.153:5001/bbp_electrum.htm?data=" + sEncData;
		// Launch the browser 
		results.push_back(Pair("data", sData));
	}
	else if (sItem == "analyze")
	{
		if (request.params.size() != 3)
			throw std::runtime_error("You must specify height and nickname.");
		int nHeight = cdbl(request.params[1].get_str(), 0);
		std::string sNickName = request.params[2].get_str();
		WriteCache("analysis", "user", sNickName, GetAdjustedTime());
		UniValue p = GetProminenceLevels(nHeight + BLOCKS_PER_DAY, "");
		std::string sData1 = ReadCache("analysis", "data_1");
		std::string sData2 = ReadCache("analysis", "data_2");
		results.push_back(Pair("Totals", sData2));
		std::vector<std::string> v = Split(sData1.c_str(), "\n");
		for (int i = 0; i < (int)v.size(); i++)
		{
			std::string sRow = v[i];
			results.push_back(Pair(RoundToString(i, 0), sRow));
		}
	}
	else if (sItem == "roi")
	{
		if (request.params.size() != 1 && request.params.size() != 2)
			throw std::runtime_error("roi:  Shows the estimated return on investment for a given donation in the POG campage.  You may optionally specify a specific tithe amount.  IE: roi [bbpamount].");
		double dSpecificAmount = 0;

		if (request.params.size() > 1)
			dSpecificAmount = cdbl(request.params[1].get_str(), 2);
	
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(iLastSuperblock);
		nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;
		std::string sContract = GetGSCContract(iLastSuperblock, false);
		std::string sData = ExtractXML(sContract, "<DATA>", "</DATA>");
		std::vector<std::string> vData = Split(sData.c_str(), "\n");
		double dTotalPaid = 0;
		double nTotalPoints = 0;
		for (int i = 0; i < vData.size(); i++)
		{
			std::vector<std::string> vRow = Split(vData[i].c_str(), "|");
			if (vRow.size() >= 6)
			{
				double nPoints = cdbl(vRow[2], 2);
				double nProminence = cdbl(vRow[3], 4) * 100;
				double nPayment = cdbl(vRow[5], 4);
				CAmount nOwed = nPaymentsLimit * (nProminence / 100);
				dTotalPaid += nPayment;
				nTotalPoints += nPoints;
			}
		}
		results.push_back(Pair("Notes", "Please note this information is for the POG campaign only, and based on yesterday's participation levels."));
		results.push_back(Pair("Contract Height", iLastSuperblock));
		results.push_back(Pair("Total Paid", dTotalPaid));
		results.push_back(Pair("Total Points", nTotalPoints));
		double dPPP = dTotalPaid / nTotalPoints;
		results.push_back(Pair("Payment Per Point", dPPP));
		CAmount nTotalReq;
		// Honor default coin age percentage
		double nDefaultCoinAgePercentage = GetSporkDouble("pogdefaultcoinagepercentage", .10);
		double nCoinAgePercentage = UserSetting("pog_coinagepercentage", nDefaultCoinAgePercentage);
		
		double dCoinAgeIn = pwalletMain->GetAntiBotNetWalletWeight(0, nTotalReq);
		double dTargetAge = dCoinAgeIn * nCoinAgePercentage;
		double dCoinAge = pwalletMain->GetAntiBotNetWalletWeight(dTargetAge, nTotalReq);
		
		results.push_back(Pair("My Coin Age Percentage", nCoinAgePercentage));
		
		results.push_back(Pair("My Current Coin Age", dCoinAge));
		results.push_back(Pair("My free balance", nTotalReq / COIN));
		CBlockIndex* bindex = FindBlockByHeight(iLastSuperblock - 1);
		int nBits = bindex->nBits;
		CAmount nReward = GetBlockSubsidy(nBits, iLastSuperblock - 1, consensusParams, false);
		CAmount nSanc = GetMasternodePayment(iLastSuperblock -1, nReward);
		results.push_back(Pair("Sanctuary Reward @" + RoundToString(iLastSuperblock - 1, 2), nSanc / COIN));
		double nSancROI = ((double)(nSanc/COIN) / SANCTUARY_COLLATERAL) * 100 * 365;
		results.push_back(Pair("Sanctuary ROI Annualized %", nSancROI));
		double nLowAmount = 2;
		double nHighAmount = 50000;
		double nStep = 1000;
		if (dSpecificAmount > 0)
		{
			nLowAmount = dSpecificAmount;
			nHighAmount = dSpecificAmount + 1;
			nStep = 1;
		}
		for (double nTitheAmount = nLowAmount; nTitheAmount < nHighAmount; nTitheAmount += nStep)
		{
			double nPoints = cbrt(nTitheAmount) * dCoinAge;
			if (nTitheAmount > 10000)
				nTitheAmount += 5000;
			results.push_back(Pair("Tithe " + RoundToString(nTitheAmount, 2) + " Points", nPoints));
			// Calculate conceptual ROI
			double nEarned = (dPPP * nPoints) - nTitheAmount;
			results.push_back(Pair("Tithe " + RoundToString(nTitheAmount, 2) + " Reward", (dPPP * nPoints)));
			double nROI = (nEarned / nTitheAmount) * 100;
			results.push_back(Pair("Tithe " + RoundToString(nTitheAmount, 2) + " Daily ROI %", nROI));
			double nROIBalance = (nEarned / (nTotalReq/COIN)) * 100 * 365;
			results.push_back(Pair("Balance " + RoundToString(nTotalReq / COIN, 2) + " Annualized ROI %", nROIBalance));
		}
	}
	else if (sItem == "debugtool1")
	{
		std::string sBlock = request.params[1].get_str();
		int nHeight = (int)cdbl(sBlock,0);
		if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight) 
			throw std::runtime_error("Block number out of range.");
		CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
		const Consensus::Params& consensusParams = Params().GetConsensus();
		double dDiff = GetDifficulty(pblockindex);
		double dDiffThreshhold = fProd ? 1000 : 1;
		results.push_back(Pair("diff", dDiff));
		bool f1 = dDiff > dDiffThreshhold;
		results.push_back(Pair("f1", f1));
		CBlock block;
		ReadBlockFromDisk(block, pblockindex, consensusParams);
		double nMinRequiredABNWeight = GetSporkDouble("requiredabnweight", 0);
		double nABNHeight = GetSporkDouble("abnheight", 0);
		results.push_back(Pair("abnheight", nABNHeight));
		results.push_back(Pair("fprod", fProd));
		results.push_back(Pair("consensusABNHeight", consensusParams.ABNHeight));
		bool f10 = (nABNHeight > 0 && nHeight > consensusParams.ABNHeight && nHeight > nABNHeight && nMinRequiredABNWeight > 0);
		results.push_back(Pair("f10_abnheight", f10));
		results.push_back(Pair("LateBlock", LateBlock(block, pblockindex, 60)));
		bool fLBI = LateBlockIndex(pblockindex, 60);
		results.push_back(Pair("LBI", fLBI));
		results.push_back(Pair("abnreqweight", nMinRequiredABNWeight));
		results.push_back(Pair("abnheight", nABNHeight));
	}
	else if (sItem == "dailysponsorshipcap")	
	{	
		double nCap = GetProminenceCap("CAMEROON-ONE", 1333, .50);
		results.push_back(Pair("cap", nCap));
	}
	else if (sItem == "tuhi")
	{
		UpdateHealthInformation();
	}
	else if (sItem == "cameroon_payments")
	{
		if (request.params.size() !=2)
			throw std::runtime_error("You must specify cameroon_payments type [XML/Auto].");
		std::string sType = request.params[1].get_str();
		std::string sDest = "BHRiFZYUpHj2r3gxw7pHyvByTUk1dGb8vz";
		std::string CP = SearchChain(BLOCKS_PER_DAY * 31, sDest);
		int payment_id = 0;
		if (sType == "XML")
		{
			results.push_back(Pair("payments", CP));
		}
		else
		{
			std::vector<std::string> vRows = Split(CP.c_str(), "<row>");
			for (int i = 0; i < vRows.size(); i++)
			{
				std::string sCPK = ExtractXML(vRows[i], "<cpk>", "</cpk>");
				std::string sChildID = ExtractXML(vRows[i], "<childid>", "</childid>");
				std::string sAmount = ExtractXML(vRows[i], "<amount>", "</amount>");
				std::string sUSD = ExtractXML(vRows[i], "<amount_usd>", "</amount_usd>");
				std::string sBlock = ExtractXML(vRows[i], "<block>", "</block>");
				std::string sTXID = ExtractXML(vRows[i], "<txid>", "</txid>");
				if (!sChildID.empty())
				{
					payment_id++;
					results.push_back(Pair("Payment #", payment_id));
					results.push_back(Pair("CPK", sCPK));
					results.push_back(Pair("childid", sChildID));
					results.push_back(Pair("Amount", sAmount));
					results.push_back(Pair("Amount_USD", sUSD));
					results.push_back(Pair("Block #", sBlock));
					results.push_back(Pair("TXID", sTXID));
				}
			}
		}
	}
	else if (sItem == "blscommand")
	{
		if (request.params.size() != 2)	
			throw std::runtime_error("You must specify blscommand masternodeprivkey masternodeblsprivkey.");	

		std::string sMNP = request.params[1].get_str();
		std::string sMNBLSPrivKey = request.params[2].get_str();
		std::string sCommand = "masternodeblsprivkey=" + sMNBLSPrivKey;
		std::string sEnc = EncryptAES256(sCommand, sMNP);
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::string sXML = "<blscommand>" + sEnc + "</blscommand>";
		std::string sError;
		std::string sResult = SendBlockchainMessage("bls", sCPK, sXML, 1, false, "", sError);
		if (!sError.empty())
			results.push_back(Pair("Errors", sError));
		results.push_back(Pair("blsmessage", sXML));
	}
	else if (sItem == "testhttps")
	{
		std::string sURL = "https://" + GetSporkValue("pool");
		std::string sRestfulURL = "SAN/LastMandatoryVersion.htm";
		std::string sResponse = BiblepayHTTPSPost(false, 0, "", "", "", sURL, sRestfulURL, 443, "", 25, 10000, 1);
		results.push_back(Pair(sRestfulURL, sResponse));
	}
	else if (sItem == "sendmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec sendmessage PRAYER mother Please_pray_for_my_mother._She_has_this_disease.'";
		if (request.params.size() != 4)
			 throw std::runtime_error(sError);

		std::string sType = request.params[1].get_str();
		std::string sPrimaryKey = request.params[2].get_str();
		std::string sValue = request.params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw std::runtime_error(sError);
		std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, 1, false, "", sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "getgovlimit")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nBits = 486585255;
		int nHeight = cdbl(request.params[1].get_str(), 0);
		CAmount nLimit = CSuperblock::GetPaymentsLimit(nHeight);
		CAmount nReward = GetBlockSubsidy(nBits, nHeight, consensusParams, false);
		CAmount nRewardGov = GetBlockSubsidy(nBits, nHeight, consensusParams, true);
		CAmount nSanc = GetMasternodePayment(nHeight, nReward);
        results.push_back(Pair("Limit", (double)nLimit/COIN));
		results.push_back(Pair("Subsidy", (double)nReward/COIN));
		results.push_back(Pair("Sanc", (double)nSanc/COIN));
		// Evo Audit: 14700 gross, @98400=13518421, @129150=13225309/Daily = @129170=1013205
		results.push_back(Pair("GovernanceSubsidy", (double)nRewardGov/COIN));
	}
	else if (sItem == "hexblocktojson")
	{
		std::string sHex = request.params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
        return blockToJSON(block, NULL, true);
	}
	else if (sItem == "hextxtojson")
	{
		std::string sHex = request.params[1].get_str();
		
		CMutableTransaction tx;
        if (!DecodeHexTx(tx, request.params[0].get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tx decode failed");
        UniValue objTx(UniValue::VOBJ);
        TxToJSON(tx, uint256(), objTx);
        results.push_back(objTx);
	}
	else if (sItem == "hextxtojson2")
	{
		std::string sHex = request.params[1].get_str();
		CMutableTransaction tx;
        DecodeHexTx(tx, request.params[0].get_str());
        UniValue objTx(UniValue::VOBJ);
        TxToJSON(tx, uint256(), objTx);
        results.push_back(objTx);
	}
	else if (sItem == "blocktohex")
	{
		std::string sBlock = request.params[1].get_str();
		int nHeight = (int)cdbl(sBlock,0);
		if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight) 
			throw std::runtime_error("Block number out of range.");
		CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
		if (pblockindex==NULL)   
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
		CBlock block;
		const Consensus::Params& consensusParams = Params().GetConsensus();
		ReadBlockFromDisk(block, pblockindex, consensusParams);
		CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
		ssBlock << block;
		std::string sBlockHex = HexStr(ssBlock.begin(), ssBlock.end());
		CTransaction txCoinbase;
		std::string sTxCoinbaseHex = EncodeHexTx(*block.vtx[0]);
		results.push_back(Pair("blockhex", sBlockHex));
		results.push_back(Pair("txhex", sTxCoinbaseHex));
	}
	else if (sItem == "XBBP")
	{
		std::string smd1 = "BiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePay";
		uint256 hMax = uint256S("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
		arith_uint256 aMax = UintToArith256(hMax);
		arith_uint256 aMax420 = aMax * 420;
		uint256 hMaxResult = ArithToUint256(aMax420);
		results.push_back(Pair("hMax", hMax.GetHex()));
		results.push_back(Pair("hMaxResult", hMaxResult.GetHex()));
		arith_uint256 bnDiff = aMax - aMax420;
		uint256 hDiff = ArithToUint256(bnDiff);
		results.push_back(Pair("bnDiff", hDiff.GetHex()));
		arith_uint256 aDiv = aMax420 / 1260;
		uint256 hDivResult = ArithToUint256(aDiv);
		results.push_back(Pair("bnDivision", hDivResult.GetHex()));
		std::vector<unsigned char> vch1 = std::vector<unsigned char>(smd1.begin(), smd1.end());
	    std::string smd2="biblepay";
		std::vector<unsigned char> vch2 = std::vector<unsigned char>(smd2.begin(), smd2.end());
		uint256 hSMD10 = Sha256001(1,170001,smd2);
	    uint256 h1 = SerializeHash(vch1); 
		uint256 hSMD2 = SerializeHash(vch2);
		uint256 h2 = HashBiblePay(vch1.begin(),vch1.end());
		uint256 h3 = HashBiblePay(h2.begin(),h2.end());
		uint256 h4 = HashBiblePay(h3.begin(),h3.end());
		uint256 h5 = HashBiblePay(h4.begin(),h4.end());
		uint256 h00 = HashX11(vch1.begin(), vch1.end());
		uint256 hGroestl = HashGroestl(vch1.begin(), vch1.end());
		uint256 hBiblepayIsolated = HashBiblepayIsolated(vch1.begin(), vch1.end());
		uint256 hBiblePayTest = HashBiblePay(vch1.begin(), vch1.end());
		bool f7000 = false;
		bool f8000 = false;
		bool f9000 = false;
		bool fTitheBlocksActive = false;
		int nHeight = 1;
		GetMiningParams(nHeight, f7000, f8000, f9000, fTitheBlocksActive);
		f7000 = false;
		f8000 = false;
		f9000 = false;
		fTitheBlocksActive = false;
		int64_t nTime = 3;
		int64_t nPrevTime = 2;
		int64_t nPrevHeight = 1;
		int64_t nNonce = 10;
		uint256 inHash = uint256S("0x1234");
		bool bMining = true;
		const Consensus::Params& consensusParams = Params().GetConsensus();

		uint256 uBibleHash = BibleHashClassic(inHash, nTime, nPrevTime, bMining, nPrevHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, nNonce, consensusParams);
		std::vector<unsigned char> vchPlaintext = std::vector<unsigned char>(inHash.begin(), inHash.end());
		std::vector<unsigned char> vchCiphertext;
		BibleEncryptE(vchPlaintext, vchCiphertext);
		/* Try to discover AES256 empty encryption value */
		uint256 hBlank = uint256S("0x0");
		std::vector<unsigned char> vchBlank = std::vector<unsigned char>(hBlank.begin(), hBlank.end());
		std::vector<unsigned char> vchBlankEncrypted;
		BibleEncryptE(vchBlank, vchBlankEncrypted);
		std::string sBlankBase64 = EncodeBase64(VectToString(vchBlankEncrypted));
		results.push_back(Pair("Blank_Base64", sBlankBase64));
		std::string sCipher64 = EncodeBase64(VectToString(vchCiphertext));
		std::string sBibleMD5 = BibleMD5(sCipher64);
		std::string sBibleMD51234 = BibleMD5("1234");
		results.push_back(Pair("h_sha256", hSMD10.GetHex()));
		results.push_back(Pair("AES_Cipher64", sCipher64));
		results.push_back(Pair("AES_BibleMD5", sBibleMD5));
		results.push_back(Pair("Plain_BibleMD5_1234", sBibleMD51234));
		results.push_back(Pair("biblehash", uBibleHash.GetHex()));
		results.push_back(Pair("h00_biblepay", h00.GetHex()));
		results.push_back(Pair("h_Groestl", hGroestl.GetHex()));
		results.push_back(Pair("h_BiblePayIsolated", hBiblepayIsolated.GetHex()));
		results.push_back(Pair("h_BiblePayTest", hBiblePayTest.GetHex()));
		results.push_back(Pair("h1",h1.GetHex()));
		results.push_back(Pair("h2",h2.GetHex()));
		results.push_back(Pair("hSMD2", hSMD2.GetHex()));
		results.push_back(Pair("h3",h3.GetHex()));
		results.push_back(Pair("h4",h4.GetHex()));
		results.push_back(Pair("h5",h5.GetHex()));
	}
	else
	{
		results.push_back(Pair("Error", "Command not found"));
	}

	return results;
}

UniValue reconsiderblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "reconsiderblock \"blockhash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ResetBlockFailureFlags(pblockindex);
    }

    CValidationState state;
    ActivateBestChain(state, Params());

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue getspecialtxes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 5)
        throw std::runtime_error(
            "getspecialtxes \"blockhash\" ( type count skip verbosity ) \n"
            "Returns an array of special transactions found in the specified block\n"
            "\nIf verbosity is 0, returns tx hash for each transaction.\n"
            "If verbosity is 1, returns hex-encoded data for each transaction.\n"
            "If verbosity is 2, returns an Object with information for each transaction.\n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. type                 (numeric, optional, default=-1) Filter special txes by type, -1 means all types\n"
            "3. count                (numeric, optional, default=10) The number of transactions to return\n"
            "4. skip                 (numeric, optional, default=0) The number of transactions to skip\n"
            "5. verbosity            (numeric, optional, default=0) 0 for hashes, 1 for hex-encoded data, and 2 for json object\n"
            "\nResult (for verbosity = 0):\n"
            "[\n"
            "  \"txid\" : \"xxxx\",    (string) The transaction id\n"
            "]\n"
            "\nResult (for verbosity = 1):\n"
            "[\n"
            "  \"data\",               (string) A string that is serialized, hex-encoded data for the transaction\n"
            "]\n"
            "\nResult (for verbosity = 2):\n"
            "[                       (array of Objects) The transactions in the format of the getrawtransaction RPC.\n"
            "  ...,\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getspecialtxes", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
            + HelpExampleRpc("getspecialtxes", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int nTxType = -1;
    if (request.params.size() > 1) {
        nTxType = request.params[1].get_int();
    }

    int nCount = 10;
    if (request.params.size() > 2) {
        nCount = request.params[2].get_int();
        if (nCount < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    }

    int nSkip = 0;
    if (request.params.size() > 3) {
        nSkip = request.params[3].get_int();
        if (nSkip < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative skip");
    }

    int nVerbosity = 0;
    if (request.params.size() > 4) {
        nVerbosity = request.params[4].get_int();
        if (nVerbosity < 0 || nVerbosity > 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity must be in range 0..2");
        }
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    int nTxNum = 0;
    UniValue result(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if (tx->nVersion != 3 || tx->nType == TRANSACTION_NORMAL // ensure it's in fact a special tx
            || (nTxType != -1 && tx->nType != nTxType)) { // ensure special tx type matches filter, if given
                continue;
        }

        nTxNum++;
        if (nTxNum <= nSkip) continue;
        if (nTxNum > nSkip + nCount) break;

        switch (nVerbosity)
        {
            case 0 : result.push_back(tx->GetHash().GetHex()); break;
            case 1 : result.push_back(EncodeHexTx(*tx)); break;
            case 2 :
                {
                    UniValue objTx(UniValue::VOBJ);
                    TxToJSON(*tx, uint256(), objTx);
                    result.push_back(objTx);
                    break;
                }
            default : throw JSONRPCError(RPC_INTERNAL_ERROR, "Unsupported verbosity");
        }
    }

    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafe argNames
  //  --------------------- ------------------------  -----------------------  ------ ----------
    { "blockchain",         "getblockchaininfo",      &getblockchaininfo,      true,  {} },
    { "blockchain",         "getbestblockhash",       &getbestblockhash,       true,  {} },
    { "blockchain",         "getblockcount",          &getblockcount,          true,  {} },
    { "blockchain",         "getblock",               &getblock,               true,  {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockhashes",         &getblockhashes,         true,  {"high","low"} },
    { "blockchain",         "getblockhash",           &getblockhash,           true,  {"height"} },
    { "blockchain",         "getblockheader",         &getblockheader,         true,  {"blockhash","verbose"} },
    { "blockchain",         "getblockheaders",        &getblockheaders,        true,  {"blockhash","count","verbose"} },
    { "blockchain",         "getchaintips",           &getchaintips,           true,  {"count","branchlen"} },
    { "blockchain",         "getdifficulty",          &getdifficulty,          true,  {} },
    { "blockchain",         "getmempoolancestors",    &getmempoolancestors,    true,  {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  &getmempooldescendants,  true,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        &getmempoolentry,        true,  {"txid"} },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         true,  {} },
    { "blockchain",         "getrawmempool",          &getrawmempool,          true,  {"verbose"} },
    { "blockchain",         "getspecialtxes",         &getspecialtxes,         true,  {"blockhash", "type", "count", "skip", "verbosity"} },
    { "blockchain",         "gettxout",               &gettxout,               true,  {"txid","n","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        true,  {} },
    { "blockchain",         "pruneblockchain",        &pruneblockchain,        true,  {"height"} },
    { "blockchain",         "verifychain",            &verifychain,            true,  {"checklevel","nblocks"} },
    { "blockchain",         "preciousblock",          &preciousblock,          true,  {"blockhash"} },

    /* Not shown in help */
    { "hidden",             "exec",				      &exec,                   true,  {"1","2","3","4","5","6","7"} },
    { "hidden",             "invalidateblock",        &invalidateblock,        true,  {"blockhash"} },
    { "hidden",             "reconsiderblock",        &reconsiderblock,        true,  {"blockhash"} },
    { "hidden",             "waitfornewblock",        &waitfornewblock,        true,  {"timeout"} },
    { "hidden",             "waitforblock",           &waitforblock,           true,  {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",     &waitforblockheight,     true,  {"height","timeout"} },
};

void RegisterBlockchainRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

