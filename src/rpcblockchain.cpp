// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "coins.h"
#include "chat.h"
#include "consensus/validation.h"
#include "main.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpcserver.h"
#include "podc.h"
#include "rpcpog.h"
#include "streams.h"
#include "sync.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "base58.h"
#include "darksend.h"
#include "wallet/wallet.h"
#include "wallet/rpcwallet.cpp"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "alert.h"
#include "rpcpodc.h"
#include "rpcipfs.h"

#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "masternode-sync.h"
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

using namespace std;

// From rpcrawtransaction.cpp:
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
// From rpcrawtransaction.cpp:
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
// From rpcrawtransaction.cpp
UniValue createrawtransaction(const UniValue& params, bool fHelp);



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
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)blockindex->nNonce));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficultyN(blockindex,10)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
	result.push_back(Pair("blockmessage", blockindex->sBlockMessage));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false, bool bVerbose = false, bool bShowPrayers = true)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
	if (blockindex)
	{
		if (chainActive.Contains(blockindex))
			confirmations = chainActive.Height() - blockindex->nHeight + 1;
	} 

    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
	result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
	if (block.vtx.size() > 0)
	{
		BOOST_FOREACH(const CTransaction&tx, block.vtx)
		{
			if(txDetails)
			{
				UniValue objTx(UniValue::VOBJ);
				TxToJSON(tx, uint256(), objTx);
				txs.push_back(objTx);
			}
			else
				txs.push_back(tx.GetHash().GetHex());
		}
		result.push_back(Pair("tx", txs));
	}
    result.push_back(Pair("time", block.GetBlockTime()));
    if (blockindex) 
	{
		result.push_back(Pair("height", blockindex->nHeight));
		result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
		if ((blockindex->nHeight < F11000_CUTOVER_HEIGHT_PROD && fProd) || (blockindex->nHeight < F11000_CUTOVER_HEIGHT_TESTNET && !fProd))
		{
			result.push_back(Pair("difficulty", GetDifficultyN(blockindex,10)));
		}
		else
		{
			double dPOWDifficulty = GetDifficulty(blockindex)*10;
			result.push_back(Pair("pow_difficulty", dPOWDifficulty));
			double dPODCDifficulty = GetBlockMagnitude(blockindex->nHeight);
			result.push_back(Pair("difficulty", dPODCDifficulty));
		}
		result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
	}

	result.push_back(Pair("hrtime",   TimestampToHRDate(block.GetBlockTime())));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));

	if (block.vtx.size() > 0)
	{
		result.push_back(Pair("subsidy", block.vtx[0].vout[0].nValue/COIN));
		result.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0])));
		if (block.vtx.size() > 1)
		{
			result.push_back(Pair("sanctuary_reward", block.vtx[0].vout[1].nValue/COIN));
		}
	}

	const Consensus::Params& consensusParams = Params().GetConsensus();
	
	if (blockindex && blockindex->pprev)
	{
		//result.push_back(Pair("estimatedsanctuaryreward", dReward));
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
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
		uint256 bibleHash = BibleHash(hashWork, block.GetBlockTime(), blockindex->pprev->nTime, false, blockindex->pprev->nHeight, blockindex->pprev, 
			false, f7000, f8000, f9000, fTitheBlocksActive, blockindex->nNonce);
		bool bSatisfiesBibleHash = (UintToArith256(bibleHash) <= hashTarget);
	

    	result.push_back(Pair("satisfiesbiblehash", bSatisfiesBibleHash ? "true" : "false"));
		result.push_back(Pair("biblehash", bibleHash.GetHex()));
		// Rob A. - 02-11-2018 - Proof-of-Distributed-Computing
		if (PODCEnabled(blockindex->nHeight))
		{
			std::string sCPIDSignature = ExtractXML(block.vtx[0].vout[0].sTxOutMessage, "<cpidsig>","</cpidsig>");
			std::string sCPID = GetElement(sCPIDSignature, ";", 0);
			result.push_back(Pair("CPID", sCPID));
			std::string sError = "";
			bool fCheckCPIDSignature = VerifyCPIDSignature(sCPIDSignature, true, sError);
			result.push_back(Pair("CPID_Signature", fCheckCPIDSignature));
		}
		// POG - 12/3/2018 - Rob A.
		if (fPOGEnabled)
		{
			result.push_back(Pair("block_tithes", (double)blockindex->nBlockTithes / COIN));
			result.push_back(Pair("24_hour_tithes", (double)blockindex->n24HourTithes / COIN));
			result.push_back(Pair("pog_difficulty", blockindex->nPOGDifficulty));
			result.push_back(Pair("min_coin_age", blockindex->nMinCoinAge));
			result.push_back(Pair("min_coin_amt", (double)blockindex->nMinCoinAmount / COIN));
			result.push_back(Pair("max_tithe_amount",(double)blockindex->nMaxTitheAmount / COIN));
		}
		// Proof-Of-Loyalty - 01-18-2018 - Rob A.
		if (fProofOfLoyaltyEnabled)
		{
			if (block.vtx.size() > 1)
			{
				bool bSigned = IsStakeSigned(block.vtx[0].vout[0].sTxOutMessage);
				if (bSigned)
				{
					std::string sError = "";
					std::string sMetrics = "";
					double dStakeWeight = GetStakeWeight(block.vtx[1], block.GetBlockTime(), block.vtx[0].vout[0].sTxOutMessage, true, sMetrics, sError);
					result.push_back(Pair("proof_of_loyalty_weight", dStakeWeight));
					result.push_back(Pair("proof_of_loyalty_errors", sError));
					result.push_back(Pair("proof_of_loyalty_xml", block.vtx[0].vout[0].sTxOutMessage));
					result.push_back(Pair("proof_of_loyalty_metrics", sMetrics));
					int64_t nPercent = GetStakeTargetModifierPercent(blockindex->pprev->nHeight, dStakeWeight);
					uint256 uBase = PercentToBigIntBase(nPercent);
					arith_uint256 bnTarget;
					bool fNegative;
					bool fOverflow;
     				bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
					uint256 hBlockTarget = ArithToUint256(bnTarget);
					result.push_back(Pair("block_target_hash", hBlockTarget.GetHex()));
					bnTarget += UintToArith256(uBase);
					uint256 hPOLBlockTarget = ArithToUint256(bnTarget);
					result.push_back(Pair("proof_of_loyalty_block_target", hPOLBlockTarget.GetHex()));
					result.push_back(Pair("proof_of_loyalty_influence_percentage", nPercent));
				}
			}
		}
	}
	else
	{
		if (blockindex && blockindex->nHeight==0)
		{
			int iStart=0;
			int iEnd=0;
			// Display a verse from Genesis 1:1 for The Genesis Block:
			GetBookStartEnd("gen",iStart,iEnd);
			std::string sVerse = GetVerse("gen",1,1,iStart-1,iEnd);
			boost::trim(sVerse);
			result.push_back(Pair("verses", sVerse));
    	}
	}
	if (block.vtx.size() > 0)
	{
		std::string sPrayers = GetMessagesFromBlock(block, "PRAYER");
		result.push_back(Pair("prayers", sPrayers));
		if (bVerbose)
		{
			std::string sMsg = "";
			for (unsigned int i = 0; i < block.vtx[0].vout.size(); i++)
			{
				sMsg += block.vtx[0].vout[i].sTxOutMessage;
			}
			result.push_back(Pair("blockmessage", sMsg));
		}
	}
	if (blockindex)
	{
		CBlockIndex *pnext = chainActive.Next(blockindex);
		if (pnext)
			result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
	}
    return result;
}


UniValue showblock(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2))
        throw runtime_error(
            "showblock <index>\n"
            "Returns information about the block at <height>.");
	std::string sBlock = params[0].get_str();
	int nHeight = (int)cdbl(sBlock,0);
    if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight)
        throw runtime_error("Block number out of range.");
    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    if (pblockindex==NULL)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    CBlock block;
    const Consensus::Params& consensusParams = Params().GetConsensus();
	ReadBlockFromDisk(block, pblockindex, consensusParams, "SHOWBLOCK");
	bool bVerbose = false;
	if (params.size() > 1) bVerbose = params[1].get_str() == "true" ? true : false;
	return blockToJSON(block, pblockindex, false, bVerbose);
}

UniValue getblockcount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest block chain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest block chain.\n"
            "\nResult\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

UniValue getdifficulty(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetDifficultyN(NULL,10);
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
            const CTransaction& tx = e.GetTx();
            set<string> setDepends;
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                if (mempool.exists(txin.prevout.hash))
                    setDepends.insert(txin.prevout.hash.ToString());
            }

            UniValue depends(UniValue::VARR);
            BOOST_FOREACH(const string& dep, setDepends)
            {
                depends.push_back(dep);
            }

            info.push_back(Pair("depends", depends));
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        BOOST_FOREACH(const uint256& hash, vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nArguments:\n"
            "1. verbose           (boolean, optional, default=false) true for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            "    \"size\" : n,             (numeric) transaction size in bytes\n"
            "    \"fee\" : n,              (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
            "    \"modifiedfee\" : n,      (numeric) transaction fee with fee deltas used for mining priority\n"
            "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
            "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
            "    \"startingpriority\" : n, (numeric) priority when transaction entered pool\n"
            "    \"currentpriority\" : n,  (numeric) transaction priority now\n"
            "    \"descendantcount\" : n,  (numeric) number of in-mempool descendant transactions (including this one)\n"
            "    \"descendantsize\" : n,   (numeric) size of in-mempool descendants (including this one)\n"
            "    \"descendantfees\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including this one)\n"
            "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
            "        \"transactionid\",    (string) parent transaction id\n"
            "       ... ]\n"
            "  }, ...\n"
            "}\n"
            "\nExamples\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    LOCK(cs_main);

    bool fVerbose = false;
    if (params.size() > 0)
        fVerbose = params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getblockhashes(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
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

    unsigned int high = params[0].get_int();
    unsigned int low = params[1].get_int();
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

UniValue getblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash index\n"
            "\nReturns hash of block in best-block-chain at index provided.\n"
            "\nArguments:\n"
            "1. index         (numeric, required) The block index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
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
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the next block\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

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

UniValue getblockheaders(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
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
            "  \"previousblockhash\" : \"hash\",  (string)  The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string)  The hash of the next block\n"
            "  \"chainwork\" : \"0000...1f3\"     (string)  Expected number of hashes required to produce the current chain (in hex)\n"
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

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    int nCount = MAX_HEADERS_RESULTS;
    if (params.size() > 1)
        nCount = params[1].get_int();

    if (nCount <= 0 || nCount > (int)MAX_HEADERS_RESULTS)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Count is out of range");

    bool fVerbose = true;
    if (params.size() > 2)
        fVerbose = params[2].get_bool();

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

UniValue getblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbose is true, returns an Object with information about block <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
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
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
            + HelpExampleRpc("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus(), "GETBLOCK"))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, false, false);
}

UniValue gettxoutsetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
            "  \"transactions\": n,      (numeric) The number of transactions\n"
            "  \"txouts\": n,            (numeric) The number of output transactions\n"
            "  \"bytes_serialized\": n,  (numeric) The serialized size\n"
            "  \"hash_serialized\": \"hash\",   (string) The serialized hash\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (pcoinsTip->GetStats(stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("bytes_serialized", (int64_t)stats.nSerializedSize));
        ret.push_back(Pair("hash_serialized", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    }
    return ret;
}

UniValue gettxout(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout value\n"
            "3. includemempool  (boolean, optional) Whether to included the mem pool\n"
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
            "        \"biblepayaddress\"     (string) biblepay address\n"
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

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = params[1].get_int();
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    CCoins coins;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoins(hash, coins))
            return NullUniValue;
        mempool.pruneSpent(hash, coins); // TO DO: this should be done by the CCoinsViewMemPool
    } else {
        if (!pcoinsTip->GetCoins(hash, coins))
            return NullUniValue;
    }
    if (n<0 || (unsigned int)n>=coins.vout.size() || coins.vout[n].IsNull())
        return NullUniValue;

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT)
        ret.push_back(Pair("confirmations", 0));
    else
        ret.push_back(Pair("confirmations", pindex->nHeight - coins.nHeight + 1));
    ret.push_back(Pair("value", ValueFromAmount(coins.vout[n].nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("version", coins.nVersion));
    ret.push_back(Pair("coinbase", coins.fCoinBase));

    return ret;
}

UniValue verifychain(const UniValue& params, bool fHelp)
{
    int nCheckLevel = GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "verifychain ( checklevel numblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" + strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
            "2. numblocks    (numeric, optional, default=" + strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    if (params.size() > 0)
        nCheckLevel = params[0].get_int();
    if (params.size() > 1)
        nCheckDepth = params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip, nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int minVersion, CBlockIndex* pindex, int nRequired, const Consensus::Params& consensusParams)
{
    int nFound = 0;
    CBlockIndex* pstart = pindex;
    for (int i = 0; i < consensusParams.nMajorityWindow && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }

    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("status", nFound >= nRequired));
    rv.push_back(Pair("found", nFound));
    rv.push_back(Pair("required", nRequired));
    rv.push_back(Pair("window", consensusParams.nMajorityWindow));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("enforce", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams)));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityRejectBlockOutdated, consensusParams)));
    return rv;
}

static UniValue BIP9SoftForkDesc(const std::string& name, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    switch (VersionBitsTipState(consensusParams, id)) {
    case THRESHOLD_DEFINED: rv.push_back(Pair("status", "defined")); break;
    case THRESHOLD_STARTED: rv.push_back(Pair("status", "started")); break;
    case THRESHOLD_LOCKED_IN: rv.push_back(Pair("status", "locked_in")); break;
    case THRESHOLD_ACTIVE: rv.push_back(Pair("status", "active")); break;
    case THRESHOLD_FAILED: rv.push_back(Pair("status", "failed")); break;
    }
    return rv;
}

UniValue getblockchaininfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding block chain processing.\n"
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
            "  \"pruneheight\": xxxxxx,    (numeric) heighest block available\n"
            "  \"softforks\": [            (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of softfork\n"
            "        \"version\": xx,         (numeric) block version\n"
            "        \"enforce\": {           (object) progress toward enforcing the softfork rules for new-version blocks\n"
            "           \"status\": xx,       (boolean) true if threshold reached\n"
            "           \"found\": xx,        (numeric) number of blocks with the new version found\n"
            "           \"required\": xx,     (numeric) number of blocks required to trigger\n"
            "           \"window\": xx,       (numeric) maximum size of examined window of recent blocks\n"
            "        },\n"
            "        \"reject\": { ... }      (object) progress toward rejecting pre-softfork blocks (same fields as \"enforce\")\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": [       (array) status of BIP9 softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of the softfork\n"
            "        \"status\": \"xxxx\",    (string) one of \"defined\", \"started\", \"lockedin\", \"active\", \"failed\"\n"
            "     }\n"
            "  ]\n"
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
    obj.push_back(Pair("difficulty",            (double)GetDifficultyN(chainActive.Tip(),10)));
    obj.push_back(Pair("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast()));
    obj.push_back(Pair("verificationprogress",  Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip())));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("pruned",                fPruneMode));

    const Consensus::Params& consensusParams = Params().GetConsensus();
    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VARR);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    bip9_softforks.push_back(BIP9SoftForkDesc("csv", consensusParams, Consensus::DEPLOYMENT_CSV));
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

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */
		if (a == NULL || b == NULL) return false;

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

UniValue getchaintips(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getchaintips ( count branchlen )\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nArguments:\n"
            "1. count       (numeric, optional) only show this much of latest tips\n"
            "2. branchlen   (numeric, optional) only show tips that have equal or greater length of branch\n"
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

    if(params.size() >= 1)
        nCountMax = params[0].get_int();

    if(params.size() == 2)
        nBranchMin = params[1].get_int();

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    BOOST_FOREACH(const CBlockIndex* block, setTips)
    {
        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        if(branchLen < nBranchMin) continue;

        if(nCountMax-- < 1) break;

        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));
        obj.push_back(Pair("difficulty", GetDifficultyN(block,10)));
        obj.push_back(Pair("chainwork", block->nChainWork.GetHex()));
        obj.push_back(Pair("branchlen", branchLen));

        string status;
        if (chainActive.Contains(block)) {
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


void ScanBlockChainVersion(int nLookback)
{
    mvBlockVersion.clear();
    int nMaxDepth = chainActive.Tip()->nHeight;
    int nMinDepth = (nMaxDepth - nLookback);
    if (nMinDepth < 1) nMinDepth = 1;
    CBlock block;
    CBlockIndex* pblockindex = chainActive.Tip();
 	const Consensus::Params& consensusParams = Params().GetConsensus();
    while (pblockindex->nHeight > nMinDepth)
    {
         if (!pblockindex || !pblockindex->pprev) return;
         pblockindex = pblockindex->pprev;
         if (ReadBlockFromDisk(block, pblockindex, consensusParams, "SCANBLOCKCHAINVERSION")) 
		 {
			std::string sVersion2 = RoundToString(GetBlockVersion(block.vtx[0]), 0);
			mvBlockVersion[sVersion2]++;
		 }
    }
}


std::string ExtXML(std::string sXML, std::string sField)
{
	std::string sValue = ExtractXML(sXML,"<" + sField + ">","</" + sField + ">");
	return sValue;
}

CCommerceObject XMLToObj(std::string XML)
{
	CCommerceObject cco("");
	cco.LockTime = cdbl(ExtXML(XML,"TIME"),0);
	cco.ID = ExtXML(XML,"ID");
	cco.TXID = ExtXML(XML,"TXID");
	cco.Status1 = ExtXML(XML,"STATUS1");
	cco.Status2 = ExtXML(XML,"STATUS2");
	cco.Status3 = ExtXML(XML,"STATUS3");
	cco.Added = ExtXML(XML,"Added");
	cco.URL = ExtXML(XML,"URL");
	cco.Amount = cdbl(ExtXML(XML,"AMOUNT"),4) * COIN;
	cco.Details = ExtXML(XML,"DETAILS");
	cco.Title = ExtXML(XML,"TITLE");
	cco.Error = ExtXML(XML,"ERROR");
	return cco;		
}


std::map<std::string, CCommerceObject> GetProducts()
{
	std::map<std::string, CCommerceObject> mapCommerceObjects;
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("AWS");
	std::string sRes = SQL("get_products", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<PRODUCTS>","</PRODUCTS>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<PRODUCT>");
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			CCommerceObject ccom = XMLToObj(sE);
			if (!ccom.ID.empty())
			{
				mapCommerceObjects.insert(std::make_pair(ccom.ID, ccom));
			}
		}
	}
	return mapCommerceObjects;
}


std::map<std::string, CCommerceObject> GetOrderStatus()
{
	std::map<std::string, CCommerceObject> mapCommerceObjects;
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("AWS");
	std::string sRes = SQL("order_status", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<ORDERSTATUS>","</ORDERSTATUS>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<STATUS>");
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			CCommerceObject ccom = XMLToObj(sE);
			if (!ccom.ID.empty())
			{
				mapCommerceObjects.insert(std::make_pair(ccom.ID, ccom));
			}
		}
	}
	return mapCommerceObjects;
}

CAmount PriceLookupRequest(std::string sProductID)
{
	if (sProductID.empty()) return 0;
	std::map<std::string, CCommerceObject> mapProducts = GetProducts();
	BOOST_FOREACH(const PAIRTYPE(std::string, CCommerceObject)& item, mapProducts)
    {
			CCommerceObject oProduct = item.second;
			if (oProduct.ID==sProductID) return oProduct.Amount;
 	}
	return 0;
}

int GetTransactionVOUT(CWalletTx wtx, CAmount nAmount, int& VOUT)
{
	CTransaction tx;
	uint256 hashBlock;
	if (GetTransaction(wtx.GetHash(), tx, Params().GetConsensus(), hashBlock, true))
	{
		 for (int i = 0; i < (int)tx.vout.size(); i++)
		 {
 			 if (tx.vout[i].nValue == nAmount) 
			 {
				VOUT=i;
				return true;
			 }
		 }
	}
	return false;
}


std::string BuyProduct(std::string ProductID, bool bDryRun, CAmount nMaxPrice)
{
	std::string out_Error = "";
	CAmount nAmount = PriceLookupRequest(ProductID);
	if (nAmount==0)
	{
		 throw runtime_error("Sorry, unable to retrieve product price.  Please try again later.");
	}
	ReadConfigFile(mapArgs, mapMultiArgs);
 	std::string sAddress = DefaultRecAddress("AWS");
	std::string sEE = SQL("get_product_escrow_address", sAddress, "address", out_Error);
	std::string sProductEscrowAddress = ExtractXML(sEE,"<PRODUCT_ESCROW_ADDRESS>","</PRODUCT_ESCROW_ADDRESS>");
	std::string sDeliveryName = GetArg("-delivery_name", "");
	std::string sDeliveryAddress = GetArg("-delivery_address", "");
	std::string sDeliveryAddress2 = GetArg("-delivery_address2", "");
	sDeliveryAddress = strReplace(sDeliveryAddress,"#","");
	sDeliveryAddress2 = strReplace(sDeliveryAddress,"#","");
	std::string sDeliveryCity = GetArg("-delivery_city", "");
	std::string sDeliveryState = GetArg("-delivery_state", "");
	std::string sDeliveryZip = GetArg("-delivery_zip", "");
	std::string sDeliveryPhone = GetArg("-delivery_phone", "");
	if (sDeliveryName.empty())
	{
		 throw runtime_error("Delivery Name Empty (Required by AWS): Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryAddress.empty())
	{
		 throw runtime_error("Delivery Address Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryCity.empty())
	{
		 throw runtime_error("Delivery City Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryState.empty())
	{
		 throw runtime_error("Delivery State Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryZip.empty())
	{
		 throw runtime_error("Delivery Zip Empty: Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}
	else if (sDeliveryPhone.empty())
	{
		 throw runtime_error("Delivery Phone Empty (Required by AWS): Delivery name, address, city, state, zip, and phone must be populated.  Please modify your biblepay.conf file to have: delivery_name, delivery_address, delivery_address2 [OPTIONAL], delivery_city, delivery_state, delivery_zip, and delivery_phone populated.");
	}

	if (sProductEscrowAddress.empty())
	{
		 throw runtime_error("Product Escrow Address was not able to be retrieved from a Sanctuary, please try again later.");
	}

	// Send Payment
	CBitcoinAddress cbAddress(sEE);
	CWalletTx wtx;
	std::string sScript="";
	std::string sTXID = "";
	int VOUT = 0;
	if (nAmount > nMaxPrice && nMaxPrice > 0) nAmount = nMaxPrice;
	if (!bDryRun)
	{
		SendColoredEscrow(cbAddress.Get(), nAmount, false, wtx, sScript);
		sTXID = wtx.GetHash().GetHex();
		bool bFound = GetTransactionVOUT(wtx, nAmount, VOUT);
		if (!bFound) LogPrintf(" Unable to locate VOUT in BuyProduct \n");
		LogPrintf("\n PAID %f for Product %s in TXID %s-VOUT %f  \n", (double)nAmount/COIN, ProductID.c_str(), sTXID.c_str(), (double)VOUT);
	}

	std::string sDryRun = bDryRun ? "DRY" : "FALSE";
	std::string sDelComplete = "<NAME>" + sDeliveryName + "</NAME><DRYRUN>" 
		+ sDryRun + "</DRYRUN><ADDRESS1>" + sDeliveryAddress + "</ADDRESS1><ADDRESS2>" 
		+ sDeliveryAddress2 + "</ADDRESS2><CITY>" 
		+ sDeliveryCity + "</CITY><STATE>" + sDeliveryState + "</STATE><ZIP>" 
		+ sDeliveryZip + "</ZIP><PHONE>" + sDeliveryPhone + "</PHONE>";
	std::string sProduct = "<PRODUCTID>" + ProductID + "</PRODUCTID><AMOUNT>" + RoundToString(nAmount/COIN,4) + "</AMOUNT><TXID>" + sTXID + "</TXID><VOUT>" 
		+ RoundToString(VOUT,0) + "</VOUT>";
	std::string sXML = sDelComplete + sProduct;
	std::string sReply = SQL("buy_product", sAddress, sXML, out_Error);
	std::string sError = ExtractXML(sReply,"<ERR>","</ERR>");
	LogPrintf("  ERROR %s BUY PRODUCT %s \n",sError.c_str(),sReply.c_str());
	
	if (!sError.empty())
	{
		throw runtime_error(sError);
	}
	return sReply;
}


void AddDebugMessage(std::string sMessage)
{
	mapDebug.insert(std::make_pair(GetAdjustedTime(), sMessage));
}

UniValue GetVersionReport()
{
	  UniValue ret(UniValue::VOBJ);
      //Returns a report of the BiblePay version that has been solving blocks over the last N blocks
	  ScanBlockChainVersion(BLOCKS_PER_DAY);
      std::string sBlockVersion = "";
      std::string sReport = "Version, Popularity\r\n";
      std::string sRow = "";
      double dPct = 0;
      ret.push_back(Pair("Version","Popularity,Percent %"));
      double Votes = 0;
	  for(map<std::string,double>::iterator ii=mvBlockVersion.begin(); ii!=mvBlockVersion.end(); ++ii) 
      {
            double Popularity = mvBlockVersion[(*ii).first];
			Votes += Popularity;
      }
      
      for(map<std::string,double>::iterator ii=mvBlockVersion.begin(); ii!=mvBlockVersion.end(); ++ii) 
      {
            double Popularity = mvBlockVersion[(*ii).first];
            sBlockVersion = (*ii).first;
            if (Popularity > 0)
            {
                sRow = sBlockVersion + "," + RoundToString(Popularity,0);
                sReport += sRow + "\r\n";
                dPct = Popularity / (Votes+.01) * 100;
                ret.push_back(Pair(sBlockVersion,RoundToString(Popularity,0) + "; " + RoundToString(dPct,2) + "%"));
            }
      }
      return ret;
}

uint256 Sha256001(int nType, int nVersion, string data)
{
    CHash256 ctx;
	unsigned char *val = new unsigned char[data.length()+1];
	strcpy((char *)val, data.c_str());
	ctx.Write(val, data.length()+1);
    uint256 result;
	ctx.Finalize((unsigned char*)&result);
    return result;
}

UniValue exec(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2  && params.size() != 3 && params.size() != 4 && params.size() != 5 && params.size() != 6 && params.size() != 7))
        throw runtime_error(
		"exec <string::itemname> <string::parameter> \r\n"
        "Executes an RPC command by name. run exec COMMAND for more info \r\n"
		"Available Commands:\r\n"
		"\r\nBlockchain Commands: \r\n"
		"blocktohex - convert block to Hex by block number \r\n"
		"hexblocktocoinbase - Get Coinbase from block Hex\r\n"
		"hexblocktojson - Convert block Hex to JSON for parsing\r\n"
		"versioncheck - Check current version vs Githubs latest.\r\n"
		"amimasternode - Check to see if the client is a masternode.\r\n"
		"datalist - Get a list from data pool, examples (PRAYER,SPORK,MESSAGE,SIN)\r\n"
		"search - Search for Prayers or messages, optionally with a search phrase.\r\n"
		"sendmessage   - Send a message (or Prayer)\r\n"
		"getversion    - Show application dunnoversion info\r\n"
		"versionreport - Show versions that mined the last days blocks\r\n"
		"biblehash     - Get a bible hash generated from parameters\r\n"
		"stakebalance  - Show current balance available to stake\r\n"
		"pinfo         - Unknown,  shows height and some other data.\r\n"
		"subsidy      - show subsidy and some block info for a given block\r\n"

		"\r\n Proof Of Distributed Computing Commands:\r\n"
		"getboincinfo - Get researcher BOINC status \r\n"
		"totalrac      - Show Stake Levels and BBP requirements for current rac \r\n"
		"utxoreport    - Show report of the PODC updates for a cpid \r\n "
		"podcdifficulty - Show difficulty for POW/PODC\r\n"
		"podcupdate - Manually send a PODC update\r\n"
		"dcc - Download Distributed computing data files\r\n"
		"associate - Connect Distributed computing account to this wallet, or connect unbanked CPID \r\n"
		"getcpid - Get current wallet's CPID\r\n"
		"getboinctasks - Get task info from BOINC (must have BOINC installed/running)\r\n"
		"getpodcpayments - Get a list of PODC payments by BBP addresses.\r\n"
		"setpodcunlockpassword - Set password to unlock wallet for PODC updates.\r\n"
		"getblock - Get block formatted in JSON by block number.\r\n"
		"wcgrac - Get current World Compute Grid RAC and payments.\r\n"
		"totalrac - Give a report of the total RAC and stake reward level BBP requirements\r\n"
		"unbanked - Get a list of Unbanked CPID's and payments\r\n"
		"leaderboard - Show PODC Mag Leaderboard\r\n"
		"\r\n IPFS Options\r\n"
		" bolist - List Business Objects\r\n"
		" bosearch - Search Business Objects\r\n"
		" emailbbp - Send BBP Via E-Mail to a contact\r\n"
		" ipfsadd - Add file to IPFS\r\n"
		" ipfsget - Get File from IPFS by hash\r\n"
		" ipfsgetbyurl - Get file from IPFS by URL\r\n"
		" ipfslist\r\n"
		" ipfsquality\r\n"
		" votecount - Get count of an IPFS Object votes.\r\n"
		" vote - Vote for an IPFS Object"
		"\r\n Accountability Commmands\r\n"
		"contributions - Tally payments to the orphan foundation wallet\r\n"
		"theymos - Report showing total BiblePay accountability - summaries by object type\r\n"
    );

    std::string sItem = params[0].get_str();
	if (sItem=="") throw runtime_error("Command argument invalid.");

    UniValue results(UniValue::VOBJ);
	results.push_back(Pair("Command",sItem));
	if (sItem == "contributions")
	{
		UniValue aContributionReport = ContributionReport();
		return aContributionReport;
	}
	else if (sItem == "utxoreport")
	{
		std::string sError = "You must specify a CPID.";
		if (params.size() != 2)
			 throw runtime_error(sError);
		std::string sCPID = params[1].get_str();
		UniValue aUTXOReport = UTXOReport(sCPID);
		return aUTXOReport;
	}
	else if (sItem == "erasechain" || sItem == "recoverorphanednode")
	{
		RecoverOrphanedChainNew(1);
		results.push_back(Pair("Erase_Chain", 1));
    }
	else if (sItem == "persistsporkmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec persistsporkmessage dcccomputingprojectname rosetta'";
		if (params.size() != 4)
			 throw runtime_error(sError);

		std::string sType = params[1].get_str();
		std::string sPrimaryKey = params[2].get_str();
		std::string sValue = params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw runtime_error(sError);
		sError = "";
    	std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, 1, true, sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		if (!sError.empty()) results.push_back(Pair("Error", sError));
	}
	else if (sItem == "sendmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec sendmessage PRAYER mother Please_pray_for_my_mother._She_has_this_disease.'";
		if (params.size() != 4)
			 throw runtime_error(sError);

		std::string sType = params[1].get_str();
		std::string sPrimaryKey = params[2].get_str();
		std::string sValue = params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw runtime_error(sError);
		std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, 1, false, sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "getversion")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int32_t nMyVersion = ComputeBlockVersion(chainActive.Tip(), consensusParams);
		results.push_back(Pair("Block Version",(double)nMyVersion));
		if (pwalletMain) 
		{
			results.push_back(Pair("walletversion", pwalletMain->GetVersion()));
			results.push_back(Pair("wallet_fullversion", FormatFullVersion()));
			results.push_back(Pair("SSL Version", SSLeay_version(SSLEAY_VERSION)));
		}
    
	}
	else if (sItem == "versionreport")
	{
		UniValue uVersionReport = GetVersionReport();
		return uVersionReport;
	}
	else if (sItem == "betatestpoolpost")
	{
		std::string sResponse = BiblepayHttpPost(true, 0, "POST","USER_A","PostSpeed","http://www.biblepay.org","home.html",80,"", 0);
		results.push_back(Pair("beta_post", sResponse));
		results.push_back(Pair("beta_post_length", (double)sResponse.length()));
		std::string sResponse2 = BiblepayHttpPost(true, 0,"POST","USER_A","PostSpeed","http://www.biblepay.org","404.html",80,"", 0);
		results.push_back(Pair("beta_post_404", sResponse2));
		results.push_back(Pair("beta_post_length_404", (double)sResponse2.length()));
	}
	else if (sItem == "biblehash")
	{
		if (params.size() != 6)
			throw runtime_error("You must specify blockhash, blocktime, prevblocktime, prevheight, and nonce IE: run biblehash blockhash 12345 12234 100 256.");
		std::string sBlockHash = params[1].get_str();
		std::string sBlockTime = params[2].get_str();
		std::string sPrevBlockTime = params[3].get_str();
		std::string sPrevHeight = params[4].get_str();
		std::string sNonce = params[5].get_str();
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
			GetMiningParams(nHeight, f7000, f8000, f9000, fTitheBlocksActive);
			uint256 hash = BibleHash(blockHash, nBlockTime, nPrevBlockTime, true, nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, nNonce);
			results.push_back(Pair("BibleHash",hash.GetHex()));
		}
	}
	else if (sItem == "stakebalance")
	{
		CAmount nStakeBalance = pwalletMain->GetUnlockedBalance();
		results.push_back(Pair("StakeBalance", nStakeBalance/COIN));
	}
	else if (sItem == "pinfo")
	{
		results.push_back(Pair("height", chainActive.Tip()->nHeight));
		int64_t nElapsed = GetAdjustedTime() - chainActive.Tip()->nTime;
		int64_t nMN = nElapsed * 256;
		if (nElapsed > (30 * 60)) nMN=999999999;
		if (nMN < 512) nMN = 512;
		results.push_back(Pair("pinfo", nMN));
		results.push_back(Pair("elapsed", nElapsed));
	}
	else if (sItem == "subsidy")
	{
		if (params.size() != 2) 
			throw runtime_error("You must specify height.");
		std::string sHeight = params[1].get_str();
		int64_t nHeight = (int64_t)cdbl(sHeight,0);
		if (nHeight >= 1 && nHeight <= chainActive.Tip()->nHeight)
		{
			CBlockIndex* pindex = FindBlockByHeight(nHeight);
			const Consensus::Params& consensusParams = Params().GetConsensus();
			if (pindex)
			{
				CBlock block;
				if (ReadBlockFromDisk(block, pindex, consensusParams, "GETSUBSIDY")) 
				{
        			results.push_back(Pair("subsidy", block.vtx[0].vout[0].nValue/COIN));
					std::string sRecipient = PubKeyToAddress(block.vtx[0].vout[0].scriptPubKey);
					results.push_back(Pair("recipient", sRecipient));
					results.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0])));
					results.push_back(Pair("minerguid", ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<MINERGUID>","</MINERGUID>")));
				}
			}
		}
		else
		{
			results.push_back(Pair("error","block not found"));
		}
	}
	else if (sItem == "testmultisig")
	{
		// 
	}
	else if (sItem == "sendmanyxml")
	{
		    // exec sendmanyxml from_account xml_payload comment
		    if (!EnsureWalletIsAvailable(fHelp))        return NullUniValue;
		    LOCK2(cs_main, pwalletMain->cs_wallet);
			string strAccount = AccountFromValue(params[1]);
			string sXML = params[2].get_str();
			int nMinDepth = 1;
			CWalletTx wtx;
			wtx.strFromAccount = strAccount;
			wtx.mapValue["comment"] = params[3].get_str();
		    set<CBitcoinAddress> setAddress;
		    vector<CRecipient> vecSend;
	        CAmount totalAmount = 0;
			std::string sRecipients = ExtractXML(sXML,"<RECIPIENTS>","</RECIPIENTS>");
			std::vector<std::string> vRecips = Split(sRecipients.c_str(),"<ROW>");
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
	   			   	      if (!address.IsValid())		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Biblepay address: ")+sRecipient);
						  if (setAddress.count(address)) throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+sRecipient);
						  setAddress.insert(address);
						  CScript scriptPubKey = GetScriptForDestination(address.Get());
						  CAmount nAmount = dAmount * COIN;
						  if (nAmount <= 0)    throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
						  totalAmount += nAmount;
						  bool fSubtractFeeFromAmount = false;
					      CRecipient recipient = {scriptPubKey, nAmount, false, fSubtractFeeFromAmount, false, false, false, "", "", "", ""};
						  vecSend.push_back(recipient);
					}
				}
			}
			EnsureWalletIsUnlocked();
			// Check funds
			CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, ISMINE_SPENDABLE);
			if (totalAmount > nBalance)  throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");
			// Send
			CReserveKey keyChange(pwalletMain);
			CAmount nFeeRequired = 0;
			int nChangePosRet = -1;
			string strFailReason;
			bool fUseInstantSend = false;
			bool fUsePrivateSend = false;
			bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason, NULL, true, fUsePrivateSend ? ONLY_DENOMINATED : ALL_COINS, fUseInstantSend);
			if (!fCreated)    throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
			if (!pwalletMain->CommitTransaction(wtx, keyChange, fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))   throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
		 	results.push_back(Pair("txid",wtx.GetHash().GetHex()));
	}
	else if (sItem == "listdebug")
	{
		BOOST_FOREACH(const PAIRTYPE(int64_t, std::string)& item, mapDebug)
	    {
			std::string sMsg = item.second;
			int64_t nTime = item.first;
			results.push_back(Pair(RoundToString(nTime,0), sMsg));
		}
	}
	else if (sItem == "listproducts")
	{
		std::map<std::string, CCommerceObject> mapProducts = GetProducts();
		BOOST_FOREACH(const PAIRTYPE(std::string, CCommerceObject)& item, mapProducts)
	    {
			CCommerceObject oProduct = item.second;
			UniValue p(UniValue::VOBJ);
			p.push_back(Pair("Product ID",oProduct.ID));
			p.push_back(Pair("URL",oProduct.URL));
			p.push_back(Pair("Price",oProduct.Amount/COIN));
			p.push_back(Pair("Title",oProduct.Title));
			p.push_back(Pair("Details",oProduct.Details));
			results.push_back(Pair(oProduct.ID,p));
		}
	}
	else if (sItem == "buyproduct")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify type: IE 'exec buyproduct productid'.");
		std::string sProductID = params[1].get_str();
		// First, a dry run to ensure no errors exist, like Product does not exist, address validation failed, address incomplete in config file, wallet balance too low, etc.
		std::string sResult = BuyProduct(sProductID, true, 1*COIN);
		std::string sStatus = ExtractXML(sResult,"<STATUS>","</STATUS>");
	
		if (sStatus=="SUCCESS")
		{
			// Dry run succeeded.  Now buy the product for real:
			sResult = BuyProduct(sProductID, false, 0);
			sStatus = ExtractXML(sResult,"<STATUS>","</STATUS>");
		}
		else
		{
			results.push_back(Pair("Status", "Fail"));
		}
		std::string sTXID = ExtractXML(sResult,"<TXID>","</TXID>");
		if (sStatus=="SUCCESS")	results.push_back(Pair("OrderID", sTXID));
		std::string sOrderID = ExtractXML(sResult,"<ORDERID>","</ORDERID>");
		results.push_back(Pair("TXID", sTXID));
		results.push_back(Pair("Status",sStatus));
	}
	else if (sItem == "orderstatus")
	{
		// This command shows the order status of product orders
		std::map<std::string, CCommerceObject> mapProducts = GetOrderStatus();
		BOOST_FOREACH(const PAIRTYPE(std::string, CCommerceObject)& item, mapProducts)
	    {
			CCommerceObject oProduct = item.second;
			UniValue p(UniValue::VOBJ);
			p.push_back(Pair("Product ID",oProduct.ID));
			p.push_back(Pair("Price",oProduct.Amount/COIN));
			p.push_back(Pair("Added", oProduct.Added));
			p.push_back(Pair("Title",oProduct.Title));
			p.push_back(Pair("Status1",oProduct.Status1));
			p.push_back(Pair("Status2",oProduct.Status2));
			p.push_back(Pair("Status3",oProduct.Status3));
			p.push_back(Pair("Details",oProduct.Details));
			results.push_back(Pair(oProduct.ID,p));
		}
	}
	else if (sItem == "XBBP")
	{
		std::string smd1 = "BiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePayBiblePay";
		uint256 hMax = uint256S("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
		// 62 hex chars = 31 bytes
		arith_uint256 aMax = UintToArith256(hMax);
		arith_uint256 aMax420 = aMax * 420;
		uint256 hMaxResult = ArithToUint256(aMax420);
		results.push_back(Pair("hMax", hMax.GetHex()));
		results.push_back(Pair("hMaxResult", hMaxResult.GetHex()));
		// test division
		arith_uint256 bnDiff = aMax - aMax420;
		uint256 hDiff = ArithToUint256(bnDiff);
		results.push_back(Pair("bnDiff", hDiff.GetHex()));
		arith_uint256 aDiv = aMax420 / 1260;
		uint256 hDivResult = ArithToUint256(aDiv);
		results.push_back(Pair("bnDivision", hDivResult.GetHex()));
		std::vector<unsigned char> vch1 = vector<unsigned char>(smd1.begin(), smd1.end());
		//std::vector<unsigned char> vchPlaintext = vector<unsigned char>(hash.begin(), hash.end());
	    std::string smd2="biblepay";
		std::vector<unsigned char> vch2 = vector<unsigned char>(smd2.begin(), smd2.end());
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
		uint256 hBrokenDog = HashBrokenDog(vch1.begin(), vch1.end());
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
		uint256 uBibleHash = BibleHash(inHash, nTime, nPrevTime, bMining, nPrevHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, nNonce);
		std::vector<unsigned char> vchPlaintext = vector<unsigned char>(inHash.begin(), inHash.end());
		std::vector<unsigned char> vchCiphertext;
		BibleEncryptE(vchPlaintext, vchCiphertext);
		/*
		for (int i = 0; i < vch1.size(); i++)
		{
			int ichar = (int)vch1[i];
			results.push_back(Pair(RoundToString(i,0), RoundToString(ichar,0)));
		}
		*/
		/* Try to discover AES256 empty encryption value */
		uint256 hBlank = uint256S("0x0");
		std::vector<unsigned char> vchBlank = vector<unsigned char>(hBlank.begin(), hBlank.end());
		std::vector<unsigned char> vchBlankEncrypted;
		BibleEncryptE(vchBlank, vchBlankEncrypted);
		/*
		for (int i = 0; i < (int)vch1.size(); i++)
		{
			int ichar = (int)vch1[i];
			results.push_back(Pair("BlankInt" + RoundToString(i,0), RoundToString(ichar,0)));
		}
		*/
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
		results.push_back(Pair("h_BrokenDog", hBrokenDog.GetHex()));
		results.push_back(Pair("h1",h1.GetHex()));
		results.push_back(Pair("h2",h2.GetHex()));
		results.push_back(Pair("hSMD2", hSMD2.GetHex()));
		results.push_back(Pair("h3",h3.GetHex()));
		results.push_back(Pair("h4",h4.GetHex()));
		results.push_back(Pair("h5",h5.GetHex()));
		bool f1 = CheckNonce(true, 1, 9000, 10000, 10060);
		results.push_back(Pair("f1",f1));
		bool f2 = CheckNonce(true, 256000, 9000, 10000, 10060);
		results.push_back(Pair("f2",f2));
		bool f3 = CheckNonce(true, 256000, 9000, 10000, 10000+1900);
		results.push_back(Pair("f3",f3));
		bool f4 = CheckNonce(true, 1256000, 9000, 10000, 10000+180);
		results.push_back(Pair("f4",f4));
		bool f5 = CheckNonce(true, 1256000, 9000, 10000, 10000+18000);
		results.push_back(Pair("f5",f5));
	}
	else if (sItem=="ssl")
	{
		std::string sResponse = BiblepayHTTPSPost(true, 0,"POST","USER_A","PostSpeed","https://pool.biblepay.org","Action.aspx", 443, "SOLUTION", 20, 5000);
		results.push_back(Pair("Test",sResponse));
	}
	else if (sItem=="ipfsgetbyurl")
	{
		if (params.size() < 3)
			throw runtime_error("You must specify source IPFSURL and target filename.");
		std::string sURL = params[1].get_str();
		std::string sPath = params[2].get_str();
		int i = ipfs_download(sURL, sPath, 375, 0, 0);
		results.push_back(Pair("Results", i));
	}
	else if (sItem == "ipfsget")
	{
		if (params.size() < 3)
			throw runtime_error("You must specify source IPFS hash and target filename.");
		std::string sHash = params[1].get_str();
		std::string sPath = params[2].get_str();
		std::string sFN = GetFileNameFromPath(sPath);
		std::string sURL = "http://ipfs.biblepay.org:8080/ipfs/" + sHash;
		int i = ipfs_download(sURL, sPath, 375, 0, 0);
		results.push_back(Pair("Results", i));
	}
	else if (sItem == "ipfsgetrange")
	{
		if (params.size() < 3)
			throw runtime_error("You must specify source IPFS hash and target filename.");
		std::string sHash = params[1].get_str();
		std::string sPath = params[2].get_str();
		std::string sFN = GetFileNameFromPath(sPath);
		std::string sURL = "http://ipfs.biblepay.org:8080/ipfs/" + sHash;
		int i = ipfs_download(sURL, sPath, 375, 0, 1024);
		results.push_back(Pair("Results", i));
		sURL = "http://45.63.22.219:8080/ipfs/" + sHash;
		i = ipfs_download(sURL, sPath + "2", 375, 0, 1024);
		results.push_back(Pair("Results 2", i));
	}
	else if (sItem == "ipfsadd")
	{
		if (params.size() < 2)
			throw runtime_error("You must specify source IPFS filename.");
		std::string sPath = params[1].get_str();
		std::string sFN = GetFileNameFromPath(sPath);
		std::vector<char> v = ReadBytesAll(sPath.c_str());
		std::vector<unsigned char> uData(v.begin(), v.end());
		std::string s64 = EncodeBase64(&uData[0], uData.size());
		std::string sData = BiblepayIPFSPost(sFN, s64);
		std::string sHash = ExtractXML(sData,"<HASH>","</HASH>");
		std::string sLink = ExtractXML(sData,"<LINK>","</LINK>");
		std::string sError = ExtractXML(sData,"<ERROR>","</ERROR>");
		results.push_back(Pair("IPFS Hash", sHash));
		results.push_back(Pair("Path", sPath));
		results.push_back(Pair("File", sFN));
		results.push_back(Pair("Link", sLink));
		if (!sError.empty()) 
			results.push_back(Pair("Error", sError));
	}
	else if (sItem == "hp")
	{
		// This is a simple test to show the POL effect on the hashtarget:		
		std::string sType = params[1].get_str();
		double d1 = cdbl(sType, 0);
		uint256 h1 = PercentToBigIntBase((int)d1);
		results.push_back(Pair("h1", h1.GetHex()));
		uint256 h2 = PercentToBigIntBase((int)d1);
		results.push_back(Pair("h2", h2.GetHex()));
		// Move on to test the overflow condition
		arith_uint256 bn1 = UintToArith256(h1);
		arith_uint256 bn2 = UintToArith256(h2);
		arith_uint256 bn3 = bn1 + bn2;
		uint256 h3 = ArithToUint256(bn3);
		results.push_back(Pair("h3", h3.GetHex()));
	}
	else if (sItem == "blocktohex")
	{
		std::string sBlock = params[1].get_str();
		int nHeight = (int)cdbl(sBlock,0);
		if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight) throw runtime_error("Block number out of range.");
		CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
		if (pblockindex==NULL)    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
		CBlock block;
		const Consensus::Params& consensusParams = Params().GetConsensus();
		ReadBlockFromDisk(block, pblockindex, consensusParams, "SHOWBLOCK");
		CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
		//CBlock block1 = const_cast<CBlock&>(*block);
		ssBlock << block;
		std::string sBlockHex = HexStr(ssBlock.begin(), ssBlock.end());
		CTransaction txCoinbase;
		std::string sTxCoinbaseHex = EncodeHexTx(block.vtx[0]);
		results.push_back(Pair("blockhex", sBlockHex));
		results.push_back(Pair("txhex", sTxCoinbaseHex));
	}
	else if (sItem == "hexblocktojson")
	{
		std::string sHex = params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
        return blockToJSON(block, NULL, true, true);
	}
	else if (sItem == "hexblocktocoinbase")
	{
		std::string sBlockHex = params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sBlockHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
		if (block.vtx.size() < 1)
		    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Deserialization Error");
    
		results.push_back(Pair("txid", block.vtx[0].GetHash().GetHex()));
		results.push_back(Pair("recipient", PubKeyToAddress(block.vtx[0].vout[0].scriptPubKey)));
		// 1-23-2018: Biblepay - Rob A., add biblehash to tail end of coinbase tx
		CBlockIndex* pindexPrev = chainActive.Tip();
		bool f7000;
		bool f8000;
		bool f9000;
		bool fTitheBlocksActive;
		GetMiningParams(pindexPrev->nHeight, f7000, f8000, f9000, fTitheBlocksActive);
		uint256 hash = BibleHash(block.GetHash(), block.GetBlockTime(), pindexPrev->nTime, true, 
			pindexPrev->nHeight, NULL, false, f7000, f8000, f9000, fTitheBlocksActive, block.nNonce);
		results.push_back(Pair("biblehash", hash.GetHex()));
		results.push_back(Pair("subsidy", block.vtx[0].vout[0].nValue/COIN));
		results.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0])));
		std::string sMsg = "";
		for (unsigned int i = 0; i < block.vtx[0].vout.size(); i++)
		{
			sMsg += block.vtx[0].vout[i].sTxOutMessage;
		}
		std::string sMySig = ExtractXML(sMsg,"<cpidsig>","</cpidsig>");
		std::string sErr2 = "";
		bool fSigValid = VerifyCPIDSignature(sMySig, true, sErr2);
		results.push_back(Pair("cpid_sig_valid", fSigValid));
		// 4-5-2018 Ensure it is legal for this CPID to solve this block

		bool fLegality = true;
		std::string sLegalityNarr = "";
		int64_t nHeaderAge = GetAdjustedTime() - block.GetBlockTime();
		bool bActiveRACCheck = nHeaderAge < (60 * 15) ? true : false;
		
		if (bActiveRACCheck && PODCEnabled(pindexPrev->nHeight))
		{
			if (fSigValid)
			{
				// Ensure this CPID has not solved any of the last N blocks in prod or last block in testnet if header age is < 1 hour:
				std::string sCPID = GetElement(sMySig, ";", 0);
				bool bSolvedPriorBlocks = HasThisCPIDSolvedPriorBlocks(sCPID, pindexPrev);
				bool f14000 = ((pindexPrev->nHeight > F14000_CUTOVER_HEIGHT_PROD && fProd)  ||  (pindexPrev->nHeight > F14000_CUTOVER_HEIGHT_TESTNET && !fProd));
				if (f14000)
				{
					if (bSolvedPriorBlocks) sLegalityNarr="CPID_SOLVED_RECENT_BLOCK";
				}
				else
				{
					// Ensure this block can only be solved if this CPID was in the last superblock with a payment - but only if the header age is recent (this allows the chain to continue rolling if PODC goes down)
					double nRecentlyPaid = GetPaymentByCPID(sCPID, pindexPrev->nHeight);
					bool bRenumerationLegal = (nRecentlyPaid >= 0 && nRecentlyPaid < .50) ? false : true;
					fLegality = (bRenumerationLegal && !bSolvedPriorBlocks);
					if (!fLegality)
					{
						if (!bRenumerationLegal) sLegalityNarr = "CPID_HAS_NO_MAGNITUDE";
						if (bSolvedPriorBlocks)  sLegalityNarr = "CPID_SOLVED_RECENT_BLOCK";
					}
				}
			}
			else
			{
				fLegality = false;
				sLegalityNarr = "CPID_SIGNATURE_INVALID";
			}
		}
		results.push_back(Pair("cpid_legal", fLegality));
		results.push_back(Pair("cpid_legality_narr", sLegalityNarr));
		results.push_back(Pair("blockmessage", sMsg));
	}
	else if (sItem == "miningdiagnostics")
	{
		std::string sSwitch = params[1].get_str();
		if (sSwitch=="on")
		{
			fMiningDiagnostics = true;
		}
		else
		{
			fMiningDiagnostics = false;
		}
		results.push_back(Pair("mining_diagnostics", fMiningDiagnostics));
	}
	else if (sItem == "podcdifficulty")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nHeight = 0;
		if (chainActive.Tip() != NULL) nHeight = chainActive.Tip()->nHeight;
		if (params.size() == 2) nHeight = cdbl(params[1].get_str(),0);
		CBlockIndex* pindex = NULL;
		if (chainActive.Tip() != NULL && nHeight <= chainActive.Tip()->nHeight) FindBlockByHeight(nHeight);
		double dPOWDifficulty = GetDifficulty(pindex)*10;
		double dPODCDifficulty = GetBlockMagnitude(nHeight);
		results.push_back(Pair("height", nHeight));
		results.push_back(Pair("difficulty_podc", dPODCDifficulty));
		results.push_back(Pair("difficulty_pow", dPOWDifficulty));
	}
	else if (sItem == "amimasternode")
	{
		results.push_back(Pair("AmIMasternode",AmIMasternode()));
	}
	else if (sItem == "writecache")
	{
		if (params.size() != 4)
		{
			results.push_back(Pair("Error","Please specify exec writecache type key value"));
		}
		else
		{
			WriteCache(params[1].get_str(), params[2].get_str(), params[3].get_str(), GetAdjustedTime());
		}
	}
	else if (sItem == "podcupdate")
	{
		std::string sError = "";
		std::string sDebugInfo = "";
		bool bForce = false;
		if (params.size() > 1) bForce = params[1].get_str() == "true" ? true : false;
		if (params.size() > 2) sDebugInfo = params[2].get_str();
		PODCUpdate(sError, bForce, sDebugInfo);
		if (sError.empty()) sError = "true";
		results.push_back(Pair("PODCUpdate", sError));
	}
	else if (sItem == "dcc")
	{
		std::string sError = "";
		int iNextSuperblock = 0;
		GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		bool fSuccess = DownloadDistributedComputingFile(iNextSuperblock, sError);
		results.push_back(Pair("Success", fSuccess));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "testprayers")
	{
		WriteCache("MyWrite","MyKey","MyValue",GetAdjustedTime());
		WriteCache("MyWrite","MyKey2","MyValue2", 15000);
		std::string s1 = ReadCache("MyWrite", "MyKey");
		std::string s2 = ReadCache("MyWrite", "MyKey2");
		results.push_back(Pair("MyWrite1", s1));
		results.push_back(Pair("MyWrite2", s2));
		int64_t nMinimumMemory = GetAdjustedTime() - (60*60*24*4);
		PurgeCacheAsOfExpiration("MyWrite", nMinimumMemory);
		std::string s3 = ReadCache("MyWrite", "MyKey2");
		results.push_back(Pair("MyWrite2-2", s3));

		for (int y = 0; y < 64; y++)
		{
			std::string sPrayer = "";
			GetDataList("PRAYER", 7, iPrayerIndex, "", sPrayer);
			results.push_back(Pair("Prayer", sPrayer));
		}

	}
	else if (sItem == "dcchash")
	{
		std::string sContract = ReadCache("dcc", "contract");
		std::string sHash = ReadCache("dcc", "contract_hash");
		results.push_back(Pair("contract", sContract));
		results.push_back(Pair("hash", sHash));
	}
	else if (sItem == "testmodaldebuginput")
	{
		results.push_back(Pair("testmodal", "Entering Modal Mode Now"));
		string sInput = "";
		cout << "Please enter the value:\n>";
		getline(cin, sInput);
		results.push_back(Pair("Value", sInput));
	}
	else if (sItem == "associate")
	{
		if (params.size() < 3)
			throw runtime_error("You must specify boincemail, boincpassword.  Optionally specify force=true/false, and unbanked CPID.");
		std::string sEmail = params[1].get_str();
		std::string sPW = params[2].get_str();
		std::string sForce = "";
		std::string sUnbanked = "";
		if (params.size() > 3) sForce = params[3].get_str();
		if (params.size() > 4) sUnbanked = params[4].get_str();
		bool fForce = sForce=="true" ? true : false;
		std::string sError = AssociateDCAccount("project1", sEmail, sPW, sUnbanked, fForce);
		std::string sNarr = (sError.empty()) ? "Successfully advertised DC-Key.  Type exec getboincinfo to find more researcher information.  Welcome Aboard!  Thank you for donating your clock-cycles to help cure cancer!" : sError;
		results.push_back(Pair("E-Mail", sEmail));
		results.push_back(Pair("Results", sNarr));
	}
	else if (sItem == "boinctest")
	{
		if (params.size() < 4)
			throw runtime_error("You must specify boincemail, boincpassword, boincprojectid. ");
		
		std::string sEmail = params[1].get_str();
		std::string sPW = params[2].get_str();
		std::string sProjectId = params[3].get_str();

		std::string sPWHash = GetBoincPasswordHash(sPW, sEmail);
		std::string sAuth = GetBoincAuthenticator(sProjectId, sEmail, sPWHash);
		int nUserId = GetBoincResearcherUserId(sProjectId, sAuth);
		results.push_back(Pair("userid", nUserId));
		
		std::string sCPID = "";
		std::string sCode = GetBoincResearcherHexCodeAndCPID(sProjectId, nUserId, sCPID);
		std::string sHexSet = DefaultRecAddress("Rosetta");
			
		SetBoincResearcherHexCode(sProjectId, sAuth, sHexSet);
		std::string sCode2 = GetBoincResearcherHexCodeAndCPID(sProjectId, nUserId, sCPID);
		results.push_back(Pair("hexcode1",sCode));
		results.push_back(Pair("auth",sAuth));
		results.push_back(Pair("cpid",sCPID));
		results.push_back(Pair("userid", nUserId));
		results.push_back(Pair("hexcode2",sCode2));
	}
	else if (sItem == "listdccs")
	{
		std::vector<std::string> sCPIDs = GetListOfDCCS("", false);
		for (int i=0; i < (int)sCPIDs.size(); i++)
		{
			results.push_back(Pair("cpid",sCPIDs[i]));
		}
	}
	else if (sItem == "testdcc")
	{
		std::string sResult = ExecuteDistributedComputingSanctuaryQuorumProcess();
		LogPrintf("\n AcceptBlock::ExecuteDistributedComputingSanctuaryQuorumProcess %s . \n", sResult.c_str());
		results.push_back(Pair("DC_sanctuary_quorum", sResult));
	}
	else if (sItem == "testranks")
	{
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		int iSanctuaryCount = GetSanctuaryCount();

		for (int i = iLastSuperblock-25; i <= iNextSuperblock; i++)
		{
			double nRank = MyPercentile(i);
			int iRank = MyRank(i);
			results.push_back(Pair("percentrank " + RoundToString((double)i,0) + " " + RoundToString(nRank,0), iRank));
		}
		results.push_back(Pair("sanctuary_count" , iSanctuaryCount));
	}
	else if (sItem == "testvote")
	{
		int64_t nAge = GetDCCFileAge();
		uint256 uDCChash = GetDCCFileHash();
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sContract = GetDCCFileContract();
		results.push_back(Pair("file-age", nAge));
		results.push_back(Pair("file-hash", uDCChash.GetHex()));
		results.push_back(Pair("contract", sContract));
		double nRank = MyPercentile(iLastSuperblock);
		results.push_back(Pair("sanctuary_rank", nRank));
		std::string sAddresses="";
		std::string sAmounts = "";
		uint256 uPAMHash = GetDCPAMHashByContract(sContract, iNextSuperblock);
		results.push_back(Pair("pam_hash", uPAMHash.GetHex()));
		int iVotes = 0;
		uint256 uGovObjHash = uint256S("0x0");
		GetDistributedComputingGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
		std::string sError = "";
		results.push_back(Pair("govobjhash", uGovObjHash.GetHex()));
		results.push_back(Pair("Addresses", sAddresses));
		results.push_back(Pair("Amounts", sAmounts));
		if (uGovObjHash == uint256S("0x0"))
		{
			// create the contract
			std::string sQuorumTrigger = SerializeSanctuaryQuorumTrigger(iNextSuperblock, sContract);
			std::string sGobjectHash = "";

			SubmitDistributedComputingTrigger(sQuorumTrigger, sGobjectHash, sError);
			
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
		results.push_back(Pair("votes_for_my_contract", iVotes));
		int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
		results.push_back(Pair("required_votes", iRequiredVotes));
		results.push_back(Pair("last_superblock", iLastSuperblock));
		results.push_back(Pair("next_superblock", iNextSuperblock));
		// QT
		std::string sSig = SignPrice(".01");
		bool fSigValid = VerifyDarkSendSigner(sSig);
		results.push_back(Pair("sigvalid", fSigValid));

		bool fTriggered = CSuperblockManager::IsSuperblockTriggered(iNextSuperblock);
		double nTotalMagnitude = 0;
		LogPrintf(" Phase %f ", 10);
		int iCPIDCount = GetCPIDCount(sContract, nTotalMagnitude);
		results.push_back(Pair("cpid_count", iCPIDCount));
		results.push_back(Pair("total_magnitude", nTotalMagnitude));
		results.push_back(Pair("next_superblock_triggered", fTriggered));
		LogPrintf(" Phase %f ", 11);
		bool bRes = VoteForDistributedComputingContract(iNextSuperblock, sContract, sError);
		results.push_back(Pair("vote_result", bRes));
		results.push_back(Pair("vote_error", sError));
		// Verify the Vote serialization:
		std::string sSerialize = mnpayments.SerializeSanctuaryQuorumSignatures(iNextSuperblock, uDCChash);
		std::string sSigs = ExtractXML(sSerialize,"<SIGS>","</SIGS>");
		results.push_back(Pair("serial", sSerialize));
	}
	else if (sItem == "getcpid")
	{
		// Returns a CPID for a given BBP address
		if (params.size() > 2) throw runtime_error("You must specify one BBP address or none: exec getcpid, or exec getcpid BBPAddress.");
		std::string sAddress = "";
		if (params.size() == 2) sAddress = params[1].getValStr();
		std::string out_address = "";
		double nMagnitude = 0;
		std::string sCPID = FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude);
		results.push_back(Pair("CPID", sCPID));
		results.push_back(Pair("Address", out_address));
		results.push_back(Pair("Magnitude", nMagnitude));
	}
	else if (sItem == "getboinctasks")
	{
		// returns current tasks for this rosetta user id 
		std::string out_address = "";
		double nMagnitude = 0;
		std::string sAddress="";
		std::string sCPID = FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude);
		std::vector<std::string> vCPIDS = Split(msGlobalCPID.c_str(),";");
		int64_t iMaxSeconds = 60 * 24 * 30 * 12 * 60;
		for (int i = 0; i < (int)vCPIDS.size(); i++)
		{
			std::string s1 = vCPIDS[i];
			if (!s1.empty())
			{
				std::string sData = RetrieveDCCWithMaxAge(s1, iMaxSeconds);
				if (!sData.empty())
				{
					std::string sUserId = GetDCCElement(sData, 3, true);
					int nUserId = cdbl(sUserId, 0);
					if (nUserId > 0)
					{
						results.push_back(Pair("CPID", s1));
						std::string sHosts = GetBoincHostsByUser(nUserId, "project1");
						std::vector<std::string> vH = Split(sHosts.c_str(), ",");
						for (int j = 0; j < (int)vH.size(); j++)
						{
							double dHost = cdbl(vH[j], 0);
							results.push_back(Pair("host", dHost));
							std::string sTasks = GetBoincTasksByHost((int)dHost, "project1");
							results.push_back(Pair("Tasks", sTasks));
							// Pre Verify these using Sanctuary Rules
							double dPreverificationPercent = VerifyTasks(sUserId, sTasks);
							results.push_back(Pair("Preverification", dPreverificationPercent));

							std::vector<std::string> vT = Split(sTasks.c_str(), ",");
							for (int k = 0; k < (int)vT.size(); k++)
							{
								std::string sTask = vT[k];
								std::vector<std::string> vEquals = Split(sTask.c_str(), "=");
								if (vEquals.size() > 1)
								{
									std::string sTaskID = vEquals[0];
									std::string sTimestamp = vEquals[1];
									std::string sTimestampHR = TimestampToHRDate(cdbl(sTimestamp,0));
									results.push_back(Pair(sTaskID, sTimestampHR));
									std::string sStatus = GetWorkUnitResultElement("project1", cdbl(sTaskID,0), "outcome");
									results.push_back(Pair("Status", sStatus));
									std::string sSentXMLTime = GetWorkUnitResultElement("project1", cdbl(sTaskID,0), "sent_time");
									std::string sSentXMLTimestamp = TimestampToHRDate(cdbl(sSentXMLTime,0));
									results.push_back(Pair("SentXMLTimeHR", sSentXMLTimestamp));
								}
							}
						}
					}
				}
			}
		}
	}
	else if (sItem == "getpodcpayments")
	{
		if (params.size() == 2)
		{
			std::string sAddressList = params[1].get_str();
			int iNextSuperblock = 0;
		
			int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
			const Consensus::Params& consensusParams = Params().GetConsensus();
			double nBudget = 0;
			double nTotalPaid = 0;
			std::string out_Superblocks = "";
			int out_SuperblockCount = 0;
			int out_HitCount = 0;
			double out_OneDayPaid = 0;
			double out_OneWeekPaid = 0;
			double out_OneDayBudget = 0;
			double out_OneWeekBudget = 0;
			
			double nMagnitude = GetUserMagnitude(sAddressList, nBudget, nTotalPaid, iLastSuperblock, out_Superblocks, out_SuperblockCount, out_HitCount, out_OneDayPaid, out_OneWeekPaid, 
				out_OneDayBudget, out_OneWeekBudget);
			results.push_back(Pair("Total Payments (One Day)", out_OneDayPaid));
			results.push_back(Pair("Total Payments (One Week)", out_OneWeekPaid));
			results.push_back(Pair("Total Budget (One Day)",out_OneDayBudget));
			results.push_back(Pair("Total Budget (One Week)",out_OneWeekBudget));
			results.push_back(Pair("Superblock Count (One Week)", out_SuperblockCount));
			results.push_back(Pair("Superblock Hit Count (One Week)", out_HitCount));
			results.push_back(Pair("Superblock List", out_Superblocks));
			results.push_back(Pair("Last Superblock Height", iLastSuperblock));
			nBudget = CSuperblock::GetPaymentsLimit(iLastSuperblock) / COIN;
			results.push_back(Pair("Last Superblock Budget", nBudget));
			results.push_back(Pair("Magnitude (One-Day)", mnMagnitudeOneDay));
			results.push_back(Pair("Magnitude (One-Week)", nMagnitude));
		}
		else
		{
			throw runtime_error("You must specify a list of BBP Addresses delimited by semicolons.");
		}
	}
	else if (sItem == "getboincinfo")
	{
		std::string out_address = "";
		std::string sAddress = "";
		if (params.size() == 2) sAddress = params[1].get_str();
		double nMagnitude2 = 0;
		LogPrintf("Step 1 %f",GetAdjustedTime());
		std::string sCPID = FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude2);
		int iNextSuperblock = 0;
		LogPrintf("Step 2 %f",GetAdjustedTime());
	
		int iLastSuperblock = GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		LogPrintf("Step 3 %f",GetAdjustedTime());
	
		results.push_back(Pair("CPID", sCPID));
		results.push_back(Pair("Address", out_address));
		results.push_back(Pair("CPIDS", msGlobalCPID));
		
		int64_t nTime = RetrieveCPIDAssociationTime(sCPID);
		int64_t nAge = (GetAdjustedTime() - nTime)/(60*60);
		LogPrintf("Step 4 %f",GetAdjustedTime());
	
		std::string sNarrAge = nAge < 24 ? "** Warning, CPID was associated less than 24 hours ago.  Magnitude may be zero until distributed grid network harvests all credit data.  Please keep crunching. **" : "";
		results.push_back(Pair("CPID-Age (hours)",nAge));
		if (!sNarrAge.empty())
		{
			results.push_back(Pair("Alert", sNarrAge));
		}

		results.push_back(Pair("NextSuperblockHeight", iNextSuperblock));
		double nBudget2 = CSuperblock::GetPaymentsLimit(iNextSuperblock) / COIN;
		results.push_back(Pair("NextSuperblockBudget", nBudget2));
	
	    int64_t iMaxSeconds = 60 * 24 * 30 * 12 * 60;
    	std::vector<std::string> vCPIDS = Split(msGlobalCPID.c_str(),";");
		double nTotalRAC = 0;
		double dBiblepayTeam = cdbl(GetSporkValue("team"),0);
		double dNonBiblepayTeamPercentage = cdbl(GetSporkValue("nonbiblepayteampercentage"), 0);

		for (int i = 0; i < (int)vCPIDS.size(); i++)
		{
			std::string s1 = vCPIDS[i];
			if (!s1.empty())
			{
				std::string sData = RetrieveDCCWithMaxAge(s1, iMaxSeconds);
				int nUserId = cdbl(GetDCCElement(sData, 3, true), 0);
				std::string sAddress = GetDCCElement(sData, 1, true);
				if (nUserId > 0)
				{ 
					double RAC = GetBoincRACByUserId("project1", nUserId);
					results.push_back(Pair(s1 + "_ADDRESS", sAddress));
					results.push_back(Pair(s1 + "_RAC", RAC));
					double dTeam = GetBoincTeamByUserId("project1", nUserId);
					results.push_back(Pair(s1 + "_TEAM", dTeam));
					if (dBiblepayTeam > 0)
					{
						if (dBiblepayTeam != dTeam && dNonBiblepayTeamPercentage != 1)
						{
							results.push_back(Pair("Warning", "CPID " + s1 + " not in team Biblepay.  This CPID is not receiving rewards for cancer research."));
						}
					}
					// Gather the WCG RAC
					double dWCGRAC = GetWCGRACByCPID(s1);
					results.push_back(Pair(s1 + "_WCGRAC", dWCGRAC));
					nTotalRAC += RAC + dWCGRAC;
					// Show the User the estimated Task Weight, and estimated UTXOWeight
					double dTaskWeight = GetTaskWeight(s1);
					double dUTXOWeight = GetUTXOWeight(s1);
					results.push_back(Pair(s1 + "_TaskWeight", dTaskWeight));
					results.push_back(Pair(s1 + "_UTXOWeight", dUTXOWeight));
				}
			}
		}
		if (!msGlobalCPID.empty()) results.push_back(Pair("Total_RAC", nTotalRAC));
		
		const Consensus::Params& consensusParams = Params().GetConsensus();
		double nBudget = 0;
		double nTotalPaid = 0;
		std::string out_Superblocks = "";
		int out_SuperblockCount = 0;
		int out_HitCount = 0;
		double out_OneDayPaid = 0;

		double out_OneWeekPaid = 0;
		double out_OneDayBudget = 0;
		double out_OneWeekBudget = 0;
		std::string sPK = GetMyPublicKeys();

		double nMagnitude = GetUserMagnitude(sPK, nBudget, nTotalPaid, iLastSuperblock, out_Superblocks, out_SuperblockCount, out_HitCount, out_OneDayPaid, out_OneWeekPaid, out_OneDayBudget, out_OneWeekBudget);
		results.push_back(Pair("Total Payments (One Day)", out_OneDayPaid));
		results.push_back(Pair("Total Payments (One Week)", out_OneWeekPaid));
		results.push_back(Pair("Total Budget (One Day)",out_OneDayBudget));
		results.push_back(Pair("Total Budget (One Week)",out_OneWeekBudget));
		results.push_back(Pair("Superblock Count (One Week)", out_SuperblockCount));
		results.push_back(Pair("Superblock Hit Count (One Week)", out_HitCount));
		results.push_back(Pair("Superblock List", out_Superblocks));
		double dLastPayment = GetPaymentByCPID(sCPID, 0);
		results.push_back(Pair("Last Superblock Height", iLastSuperblock));
		nBudget = CSuperblock::GetPaymentsLimit(iLastSuperblock) / COIN;
		results.push_back(Pair("Last Superblock Budget", nBudget));
		results.push_back(Pair("Last Superblock Payment", dLastPayment));
		results.push_back(Pair("Magnitude (One-Day)", mnMagnitudeOneDay));
		results.push_back(Pair("Magnitude (One-Week)", nMagnitude));
	}
	else if (sItem == "racdecay")
	{
		double dMachineCredit = 110;
		if (params.size() == 2) dMachineCredit = cdbl(params[1].get_str(), 0);
		double dOldRAC = 0;
		double dAdditiveRAC = 0;
		double dNewRAC = 0;
		for (int iDay = 1; iDay <= 50; iDay++)
		{
		    dOldRAC = BoincDecayFunction(86400) * dNewRAC;
			dAdditiveRAC = (1 - BoincDecayFunction(86400)) * dMachineCredit;
			// RAC(new) = RAC(old)*d(t) + (1-d(t))*credit(new)
			dNewRAC = dOldRAC + dAdditiveRAC;
			results.push_back(Pair("Day " + RoundToString(iDay,0) + "(" + RoundToString(dAdditiveRAC, 0) + ")", dNewRAC));
		}
	}
	else if (sItem == "search")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify type: IE 'exec search PRAYER'.  Optionally you may enter a search phrase: IE 'exec search PRAYER MOTHER'.");
		std::string sType = params[1].get_str();
		std::string sSearch = "";
		if (params.size() == 3) sSearch = params[2].get_str();
		int iSpecificEntry = 0;
		std::string sEntry = "";
		int iDays = 30;
		UniValue aDataList = GetDataList(sType, iDays, iSpecificEntry, sSearch, sEntry);
		return aDataList;
	}
	else if (sItem == "setpodcunlockpassword")
	{
		if (params.size() != 2) throw runtime_error("You must specify headless podc wallet password.");
		msEncryptedString.reserve(400);
		msEncryptedString = params[1].get_str().c_str();
		results.push_back(Pair("Length", (double)msEncryptedString.size()));
	}
	else if (sItem == "podcpasswordlength")
	{
		results.push_back(Pair("Length", (double)msEncryptedString.size()));
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
	else if (sItem == "testreconstitution")
	{
		std::string sInput = params[1].get_str();
		std::string sPacked = PackPODC(sInput, 7, 7);
		std::string sUnpacked = UnpackPODC(sPacked, 7, 7);
		results.push_back(Pair("original", sInput));
		results.push_back(Pair("packed_len", (double)sPacked.length()));
		results.push_back(Pair("unpacked", sUnpacked));
		results.push_back(Pair("original_len", (double)sInput.length()));
	}
	else if (sItem == "getpodcversion")
	{
		int iVersion = 	GetPODCVersion();
		results.push_back(Pair("version", iVersion));
	}
	else if (sItem == "getblock")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify height.");
		std::string sBlock = params[1].get_str();
		int nHeight = (int)cdbl(sBlock,0);
		if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight)
			throw runtime_error("Block number out of range.");
		CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
		if (pblockindex==NULL)
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
		CBlock block;
		const Consensus::Params& consensusParams = Params().GetConsensus();
		ReadBlockFromDisk(block, pblockindex, consensusParams, "SHOWBLOCK");
		return blockToJSON(block, pblockindex, false, false, false);
	}
	else if (sItem == "wcgrac")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify the cpid.");
		std::string sCPID = params[1].get_str();
		double dWCGRAC = GetWCGRACByCPID(sCPID);
		results.push_back(Pair("WCGRAC", dWCGRAC));
		std::string sDCPK = GetDCCPublicKey(sCPID, true);
		double dPODCPayments = cdbl(ReadCacheWithMaxAge("AddressPayment", sDCPK, GetHistoricalMilestoneAge(14400, 60 * 60 * 24 * 30)), 0);
		results.push_back(Pair("Total PODC Payments (30 days)", dPODCPayments));
	}
	else if (sItem == "totalrac")
	{
		double dRAC = AscertainResearcherTotalRAC();
		double dStake = GetMinimumRequiredUTXOStake(dRAC, 1.1);
		results.push_back(Pair("Total RAC", dRAC));
		results.push_back(Pair("UTXO Target", dStake));
		CAmount nStakeBalance = pwalletMain->GetUnlockedBalance();
		results.push_back(Pair("StakeBalance", nStakeBalance/COIN));
		if ((nStakeBalance/COIN) < dStake)
		{
			results.push_back(Pair("Note", "You have less stake balance available than needed for the PODC UTXO Target.  Coins must be more than 5 confirmations deep to count.  See coin control."));
		}
		// Dave_BBP - Absolute minimum required
		double dMinRequired = cdbl(RoundToString(GetMinimumRequiredUTXOStake(dRAC, .04), 2), 2);
		std::string sNarr1 = "Threshhold to receive 10% rewards (below this UTXO Amount rewards are lost)";
		results.push_back(Pair(sNarr1, dMinRequired));
		
		// Suggested by Capulo - Stake Breaks Table in 10% increments:
		for (double i = .00; i <= .90; i += .10)
		{
			double iTestLevel = i;
			if (iTestLevel == 0) iTestLevel = .041;

			double dRequired = cdbl(RoundToString(GetMinimumRequiredUTXOStake(dRAC, iTestLevel), 2), 2);
			double iLevel = (i*100) + 10;
			
			std::string sNarr = "Stake Level Required For " + RoundToString(iLevel, 0) + "% Level";
			results.push_back(Pair(sNarr, dRequired));
		}
		double dTeamRAC = GetTeamRAC();
		results.push_back(Pair("Total Team RAC", dTeamRAC));
	
	}
	else if (sItem == "reconsiderblocks")
	{
		ReprocessBlocks(BLOCKS_PER_DAY);
		results.push_back(Pair("Reprocessed Blocks", BLOCKS_PER_DAY));
	}
	else if (sItem == "timermain")
	{
		bool bInterval = TimerMain("reconsiderblocks", 3);
		results.push_back(Pair("reconsiderblocks", bInterval));
	}
	else if (sItem == "podcvotingreport")
	{
		std::string strType = "triggers";
		int nStartTime = 0; 
		int nNextHeight = 0;
		GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, nNextHeight);
		LOCK2(cs_main, governance.cs);
		std::vector<CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
		BOOST_FOREACH(CGovernanceObject* pGovObj, objs)
        {
            if(strType == "proposals" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
            if(strType == "triggers" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
            if(strType == "watchdogs" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_WATCHDOG) continue;

			UniValue obj = pGovObj->GetJSONObject();
			int nLocalHeight = obj["event_block_height"].get_int();

            if (nLocalHeight == nNextHeight)
			{
				int iVotes = pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
				std::string sPAM = obj["payment_amounts"].get_str();
				std::vector<std::string> vCPIDs = Split(sPAM.c_str(),"|");
				results.push_back(Pair("Govobj: " + pGovObj->GetHash().GetHex(), iVotes));
				results.push_back(Pair("CPID Count", ((double)vCPIDs.size())));
				if (iVotes > 0) results.push_back(Pair("Amounts", sPAM));
			}
		}
	}
	else if (sItem == "governancevotingreport")
	{
		std::string strType = "triggers";
		int nStartTime = 0; 
		int nNextHeight = 0;
		GetLastDCSuperblockHeight(chainActive.Tip()->nHeight, nNextHeight);
		LOCK2(cs_main, governance.cs);
		std::vector<CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
		BOOST_FOREACH(CGovernanceObject* pGovObj, objs)
        {
            if(strType == "proposals" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
            if(strType == "triggers" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
            if(strType == "watchdogs" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_WATCHDOG) continue;

			UniValue obj = pGovObj->GetJSONObject();
			int nLocalHeight = obj["event_block_height"].get_int();
			int iVotes = pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);

            if (iVotes > 0)
			{
				std::string sPAM = obj["payment_amounts"].get_str();
				std::string sPAD = obj["payment_addresses"].get_str();
				std::string sPRO = obj["proposal_hashes"].get_str();
				results.push_back(Pair("Height: " + RoundToString(nLocalHeight, 0) + ", Govobj: " + pGovObj->GetHash().GetHex(), iVotes));
				results.push_back(Pair("Amounts", sPAM));
				results.push_back(Pair("Addresses", sPAD));
				results.push_back(Pair("Proposals", sPRO));
			}
		}
	}
	else if (sItem == "debug0620")
	{
		bool b1 = IsMature(GetAdjustedTime(), 14400);
		bool b2 = IsMature(GetAdjustedTime() - 15000, 14400);
		results.push_back(Pair("Now", b1));
		results.push_back(Pair("Now-15000",b2));
		WriteCache("debug", "1", "1", GetAdjustedTime());
		WriteCache("debug", "2", "2", GetAdjustedTime() - 15000);
		WriteCache("debug", "3", "3", GetAdjustedTime() - 65000);
		WriteCache("debug", "4", "4", GetAdjustedTime() - 14400 - 86400 - 25000);
		WriteCache("debug", "3", "3b", GetAdjustedTime() - 65000);
		
		std::string s1 = ReadCacheWithMaxAge("debug", "1", GetHistoricalMilestoneAge(14400, 86400));
		std::string s2 = ReadCacheWithMaxAge("debug", "2", GetHistoricalMilestoneAge(14400, 86400));
		std::string s3 = ReadCacheWithMaxAge("debug", "3", GetHistoricalMilestoneAge(14400, 86400));
		std::string s4 = ReadCacheWithMaxAge("debug", "4", GetHistoricalMilestoneAge(14400, 86400));

		results.push_back(Pair("1", s1));
		results.push_back(Pair("2", s2));
		results.push_back(Pair("3", s3));
		results.push_back(Pair("4", s4));
	}
	else if (sItem == "rosettadiagnostics")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify Rosetta e-mail and password. IE 'exec rosettadiagnostics email pass'");
		std::string sEmail= params[1].get_str();
		std::string sPass = params[2].get_str();

		std::string sError = "";
		std::string sHTML = RosettaDiagnostics(sEmail, sPass, sError);
		results.push_back(Pair("Results", sHTML));
	
		if (!sError.empty()) results.push_back(Pair("Errors", sError));
		
	}
	else if (sItem == "attachrosetta")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify Rosetta e-mail and password. IE 'exec rosettadiagnostics email pass'");
		std::string sEmail= params[1].get_str();
		std::string sPass = params[2].get_str();
		std::string sError = "";
		std::string sHTML = FixRosetta(sEmail, sPass, sError);
		results.push_back(Pair("Results", sHTML));
		if (!sError.empty()) results.push_back(Pair("Errors", sError));
	}
	else if (sItem == "testsyscmd")
	{
		/*
		std::string sCommand = params[1].get_str();
		std::string sStandardOut = "";
	    std::string sStandardErr = "";
	    int nNotFound = ShellCommand(sCmd, sStandardOut , sStandardErr);
		results.push_back(Pair(sCommand, sStandardOut + "_" + sStandardErr));
		*/
	}
	else if (sItem == "syscmd")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify the command also. IE 'exec syscmd ls'");
		std::string sCommand = params[1].get_str();
		std::string sResult = SystemCommand2(sCommand.c_str());
		results.push_back(Pair(sCommand,sResult));
	}
	else if (sItem == "ipfslist")
	{
		std::string sFiles = "";
		UniValue uvIPFS = GetIPFSList(7, sFiles);
		return uvIPFS;
	}
	else if (sItem == "ipfsquality")
	{
		map<std::string,std::string> a = GetSancIPFSQualityReport();

		BOOST_FOREACH(const PAIRTYPE(std::string, std::string)& item, a)
		{
			results.push_back(Pair(item.first, item.second));
		}
	}
	else if (sItem == "mnsporktest14")
	{
		bool bSpork14 = sporkManager.IsSporkActive(SPORK_14_REQUIRE_SENTINEL_FLAG);
		results.push_back(Pair("spork14", bSpork14));
	}
	
	else if (sItem == "botestharnessjsontest")
	{
		std::string sQ = "\"";
		std::string sJson = "[[" + sQ + "contact" + sQ + ",{" + sQ + "field1" + sQ + ":" + sQ + "value1" + sQ + ", " + sQ + "field2" + sQ + ":" + sQ + "value2" + sQ + "}]]";
		UniValue objResult(UniValue::VOBJ);
		objResult.read(sJson);
		std::vector<UniValue> arr1 = objResult.getValues();
		std::vector<UniValue> arr2 = arr1.at( 0 ).getValues();
		UniValue obj(UniValue::VOBJ);
		obj = arr2.at( 1 );
		results.push_back(Pair("json", sJson));
		results.push_back(Pair("field1", obj["field1"].get_str()));
		results.push_back(Pair("field2", obj["field2"].get_str()));
	}
	else if (sItem == "addnewobject")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify the object_name, comma-delimited-fields.");

		std::string sObjectName = params[1].get_str();
		std::string sFields     = params[2].get_str();
		std::string sError = "";
			
		if (sObjectName.empty()) sError = "Object Name must be populated.";
		if (sFields.empty()) sError = "Fields must be populated.";
		if (sError.empty())
		{
			UniValue oBO(UniValue::VOBJ);
			oBO.push_back(Pair("object_name", sObjectName));
			// GospelLink:   url,notes
			oBO.push_back(Pair("fields", sFields));
			std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
			oBO.push_back(Pair("receiving_address", sAddress));
		    oBO.push_back(Pair("objecttype", "object"));
			oBO.push_back(Pair("added", RoundToString(GetAdjustedTime(),0)));
			oBO.push_back(Pair("secondarykey", RoundToString(GetAdjustedTime(), 0)));
			std::string sTxId = "";
			sTxId = StoreBusinessObject(oBO, sError);
			results.push_back(Pair("obj_txid", sTxId));
		}
		if (!sError.empty())	results.push_back(Pair("Errors", sError));
	}
	else if (sItem == "addgospellink")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify the URL, notes.");

		std::string sURL       = params[1].get_str();
		std::string sNotes     = params[2].get_str();
		std::string sError = "";
			
		if (sURL.empty()) sError = "URL must be populated.";
		if (sNotes.empty()) sError = "Notes must be populated.";
		if (sError.empty())
		{
			UniValue oBO(UniValue::VOBJ);
			oBO.push_back(Pair("url", sURL));
			oBO.push_back(Pair("notes", sNotes));
			std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
			oBO.push_back(Pair("receiving_address", sAddress));
		    oBO.push_back(Pair("objecttype", "gospellink"));
			oBO.push_back(Pair("secondarykey", RoundToString(GetAdjustedTime(), 0)));
			oBO.push_back(Pair("added", RoundToString(GetAdjustedTime(), 0)));
			std::string sTxId = "";
			sTxId = StoreBusinessObject(oBO, sError);
			results.push_back(Pair("obj_txid", sTxId));
		}
		if (!sError.empty())	results.push_back(Pair("Errors", sError));
	}
	else if (sItem == "bolist")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify business object type: IE 'exec bolist contact'.  ");
		std::string sType = params[1].get_str();
		UniValue aBOList = GetBusinessObjectList(sType);
		return aBOList;
	}
	else if (sItem == "bosearch")
	{
		if (params.size() != 4)
			throw runtime_error("You must specify business object type, search field name, and search field value: IE 'exec bosearch contact email rob@biblepay.org'.  ");

		std::string sType = params[1].get_str();
		std::string sFieldName = params[2].get_str();
		std::string sSearchValue = params[3].get_str();
		UniValue aBO = GetBusinessObjectByFieldValue(sType, sFieldName, sSearchValue);
		results.push_back(Pair(sType + " - " + sFieldName, aBO));
	}
	else if (sItem == "emailbbp")
	{
		if (params.size() != 3)
			throw runtime_error("You must specify e-mail address and amount: IE 'exec emailbbp rob@biblepay.org 5'.  ");

		std::string sEmail = params[1].get_str();
		UniValue aBO = GetBusinessObjectByFieldValue("contact", "email", sEmail);
		if (aBO.size() == 0)
			throw runtime_error("No BBP recipient found with e-mail " + sEmail + ".");
		std::string sAddress = aBO["receiving_address"].get_str();

		CBitcoinAddress address(sAddress);
		if (!address.IsValid())
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Destination Biblepay address");
		// Amount
		CAmount nAmount = AmountFromValue(params[2]);
		if (nAmount <= 0)
			throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

		// Wallet comments
		CWalletTx wtx;
        bool fSubtractFeeFromAmount = false;
		bool fUseInstantSend = false;
		bool fUsePrivateSend = false;
		bool fUseSanctuaryFunds = false;
	    EnsureWalletIsUnlocked();
		SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, fUseInstantSend, fUsePrivateSend, fUseSanctuaryFunds);
		return wtx.GetHash().GetHex();
	}
	else if (sItem == "testrsa")
	{
		TestRSA();
		results.push_back(Pair("rsa", 1));
	}
	else if (sItem == "pbase")
	{
		double dBase = GetPBase();
		results.push_back(Pair("pbase", RoundToString(dBase,12)));
	}
	else if (sItem == "qt")
	{
		// QuantitativeTightening - QT - RANDREWS - BIBLEPAY
		double dPriorPrice = 0;
		double dPriorPhase = 0;
		int iStep = fProd ? BLOCKS_PER_DAY : BLOCKS_PER_DAY/4;
		int iLookback = fProd ? BLOCKS_PER_DAY * 7 : BLOCKS_PER_DAY * 7 * 4;
		int nMinHeight = (chainActive.Tip()->nHeight - 10 - (iLookback));
		if (nMinHeight < 100) nMinHeight = 100;
		results.push_back(Pair("Report", "QT - History"));
		results.push_back(Pair("Height", "Price; QT Percentage"));
		for (int nHeight = nMinHeight; nHeight < chainActive.Tip()->nHeight; nHeight += iStep)
		{
			GetQTPhase(-1, nHeight, dPriorPrice, dPriorPhase);
			std::string sRow = "Price: " + RoundToString(dPriorPrice, 12) + "; QT: " + RoundToString(dPriorPhase, 0) + "%";
			results.push_back(Pair(RoundToString(nHeight, 0), sRow));
		}
	}
	else if (sItem == "legacydel")
	{
		if (params.size() != 4)
			throw runtime_error("You must specify objecttype, primarykey, secondarykey or -1 for NULL.");
		std::string sOT = params[1].get_str();
		std::string sPK = params[2].get_str();
		std::string sSK = params[3].get_str();
		const Consensus::Params& consensusParams = Params().GetConsensus();
	    std::string sFoundationAddress = consensusParams.FoundationAddress;

		if (sSK == "-1") sSK = "";
		UniValue oBO(UniValue::VOBJ);
		oBO.push_back(Pair("objecttype", sOT));
		oBO.push_back(Pair("primarykey", sPK));
		oBO.push_back(Pair("secondarykey", sSK));
		oBO.push_back(Pair("deleted", "1"));
		oBO.push_back(Pair("signingkey", sFoundationAddress));
		std::string sError = "";
		std::string txid = StoreBusinessObjectWithPK(oBO, sError);
		results.push_back(Pair("TXID", txid));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "addexpense")
	{
		if (params.size() != 7)
			throw runtime_error("You must specify amount, newsponsorships, URL, prepaidsponsorships, charity, handledby.");
		std::string sAmt                 = params[1].getValStr();
		std::string sNewSponsorships     = params[2].getValStr();
		std::string sURL                 = params[3].getValStr();
		std::string sPrepaidSponsorships = params[4].getValStr();
		std::string sCharity             = params[5].getValStr();
		std::string sHandledBy           = params[6].getValStr();
		const Consensus::Params& consensusParams = Params().GetConsensus();
	    std::string sFoundationAddress = consensusParams.FoundationAddress;

		UniValue oBO(UniValue::VOBJ);
		oBO.push_back(Pair("objecttype", "expense"));
		oBO.push_back(Pair("primarykey", "expense"));
		oBO.push_back(Pair("secondarykey", RoundToString(GetAdjustedTime(), 0)));
		oBO.push_back(Pair("signingkey", sFoundationAddress));
		oBO.push_back(Pair("added", RoundToString(GetAdjustedTime(), 0)));
		oBO.push_back(Pair("deleted", "0"));
		oBO.push_back(Pair("amount", sAmt));
		oBO.push_back(Pair("new_sponsorships", sNewSponsorships));
		oBO.push_back(Pair("url", sURL));
		oBO.push_back(Pair("prepaid_sponsorships", sPrepaidSponsorships));
		oBO.push_back(Pair("charity", sCharity));
		oBO.push_back(Pair("handled_by", sHandledBy));
		std::string sError = "";
		std::string txid = StoreBusinessObjectWithPK(oBO, sError);
		results.push_back(Pair("TXID", txid));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "addrev")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify \"amount|bbpamount|btcraised|btcprice|notes|handledby|charity.\"");
		std::string sData = params[1].getValStr();
		std::vector<std::string> vData = Split(sData.c_str(),"|");
		if (vData.size() != 7)
			throw runtime_error("You must specify \"amount|bbpamount|btcraised|btcprice|notes|handledby|charity.\"");
		std::string sAmt                 = vData[0];
		std::string sBBPAmount           = vData[1];
		std::string sBTCRaised           = vData[2];
		std::string sBTCPrice            = vData[3];
		std::string sNotes               = vData[4];
		std::string sHandledBy           = vData[5];
		std::string sCharity             = vData[6];
		const Consensus::Params& consensusParams = Params().GetConsensus();
	    std::string sFoundationAddress = consensusParams.FoundationAddress;

		UniValue oBO(UniValue::VOBJ);
		oBO.push_back(Pair("objecttype", "revenue"));
		oBO.push_back(Pair("primarykey", "revenue"));
		oBO.push_back(Pair("secondarykey", RoundToString(GetAdjustedTime(), 0)));
		oBO.push_back(Pair("signingkey", sFoundationAddress));
		oBO.push_back(Pair("added", RoundToString(GetAdjustedTime(), 0)));
		oBO.push_back(Pair("deleted", "0"));
		oBO.push_back(Pair("amount", sAmt));
		oBO.push_back(Pair("bbp_amount", sBBPAmount));
		oBO.push_back(Pair("btc_raised", sBTCRaised));
		oBO.push_back(Pair("btc_price", sBTCPrice));
		oBO.push_back(Pair("notes", sNotes));
		oBO.push_back(Pair("handled_by", sHandledBy));
		oBO.push_back(Pair("charity", sCharity));
		std::string sError = "";
		std::string txid = StoreBusinessObjectWithPK(oBO, sError);
		results.push_back(Pair("TXID", txid));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "addorphan")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify \"orphanid|amount|name|url|charity.\"");
		std::string sData = params[1].getValStr();
		std::vector<std::string> vData = Split(sData.c_str(),"|");
		if (vData.size() != 5)
			throw runtime_error("You must specify \"orphanid|amount|name|url|charity.\"");
		std::string sOrphanID     = vData[0];
		std::string sAmt          = vData[1];
		std::string sName         = vData[2];
		std::string sURL          = vData[3];
		std::string sCharity      = vData[4];
		const Consensus::Params& consensusParams = Params().GetConsensus();
	    std::string sFoundationAddress = consensusParams.FoundationAddress;
		UniValue oBO(UniValue::VOBJ);
		oBO.push_back(Pair("objecttype", "orphan"));
		oBO.push_back(Pair("primarykey", "orphan"));
		oBO.push_back(Pair("secondarykey", RoundToString(GetAdjustedTime(), 0)));
		oBO.push_back(Pair("signingkey", sFoundationAddress));
		oBO.push_back(Pair("orphanid", sOrphanID));
		oBO.push_back(Pair("amount", sAmt));
		oBO.push_back(Pair("name", sName));
		oBO.push_back(Pair("url", sURL));
		oBO.push_back(Pair("charity", sCharity));
		oBO.push_back(Pair("added", RoundToString(GetAdjustedTime(), 0)));
		oBO.push_back(Pair("deleted", "0"));
		std::string sError = "";
		std::string txid = StoreBusinessObjectWithPK(oBO, sError);
		results.push_back(Pair("TXID", txid));
		results.push_back(Pair("Error", sError));
	}

	else if (sItem == "vote")
	{
		if (params.size() != 3)
			throw runtime_error("You must specify type: IE 'exec vote ipfshash vote_signal'.  IE 'exec vote ipfshash [yes/no/abstain]'.");
		std::string sIPFSHash = params[1].get_str();
		std::string sVoteSignal = params[2].get_str();
		if (sVoteSignal != "yes" && sVoteSignal != "no" && sVoteSignal != "abstain")
			throw runtime_error("Vote signal must be yes, no, or abstain.");
		std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
		std::string sOT = "vote";
		std::string sSecondaryKey = sIPFSHash + "-" + sAddress;
		double dStorageFee = 1;
		std::string sError = "";
		std::string sTxId = "";
		std::string sUTXOWeight = "<voteweight>100</voteweight>";
		std::string sVote = "<vote><votehash>" + sIPFSHash + "</votehash><signal>" + sVoteSignal + "</signal>" + sUTXOWeight + "</vote>";
		sTxId = SendBusinessObject(sOT, sSecondaryKey, sVote, dStorageFee, sAddress, true, sError);
		if (sError.empty())
		{
			results.push_back(Pair("TXID", sTxId));
		}
		else
		{
			results.push_back(Pair("Error", sError));
		}

	}
	else if (sItem == "votecount")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify type: IE 'exec votecount ipfshash'.  IE 'exec votecount'.");

		std::string sHash = params[1].get_str();
		UserVote v = GetSumOfSignal("VOTE", sHash);
		results.push_back(Pair("Yes Weight", v.nTotalYesWeight));
		results.push_back(Pair("Yes", v.nTotalYesCount));
		results.push_back(Pair("No Weight", v.nTotalNoWeight));
		results.push_back(Pair("No", v.nTotalNoCount));
		results.push_back(Pair("Abstain Weight", v.nTotalAbstainWeight));
		results.push_back(Pair("Abstain", v.nTotalAbstainCount));
	}
	else if (sItem == "theymos")
	{
		CCoinsStats stats;
		FlushStateToDisk();
		if (!pcoinsTip->GetStats(stats)) 
			throw runtime_error("Wallet must be synced to run the Theymos report.");
		double dTotalEmission = stats.nTotalAmount / COIN;
		double dTotalCharityBudget = dTotalEmission * .10;
		double dTotalBBPSold = GetBusinessObjectTotal("revenue", "bbp_amount", 1);
		double dTotalBTCRaised = GetBusinessObjectTotal("revenue", "btc_raised", 1);
		double dAvgBTCPrice = GetBusinessObjectTotal("revenue", "btc_price", 2);
		double dTotalRevenue = GetBusinessObjectTotal("revenue", "amount", 1);
		double dTotalExpenses = GetBusinessObjectTotal("expense", "amount", 1);
		double dAvgBBPPrice = 0;
		if (dTotalBBPSold > 0) dAvgBBPPrice = dTotalRevenue/dTotalBBPSold;
		results.push_back(Pair("Theymos Report", TimestampToHRDate(GetAdjustedTime())));
		results.push_back(Pair("Total Coinbase Emission (Money Supply)", dTotalEmission));
		results.push_back(Pair("Total Charity Budget", dTotalCharityBudget));
		results.push_back(Pair("Total BBP Sold on Exchanges", dTotalBBPSold));
		results.push_back(Pair("Total BTC Raised", dTotalBTCRaised));
		results.push_back(Pair("Avg BTC Price", dAvgBTCPrice));
		results.push_back(Pair("Avg BBP Price", dAvgBBPPrice));
		results.push_back(Pair("Total Revenue in USD", dTotalRevenue));
		results.push_back(Pair("Total Expenses in USD", dTotalExpenses));
		results.push_back(Pair("Note:", "See Business Objects | Expenses or Revenue for more details and attached PDF receipts.  Click Navigate_To to view the PDF."));
    }
	else if (sItem == "ipfspin")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify type: IE 'exec pin ipfshash.");
		std::string sHash = params[1].get_str();
		//curl "http://localhost:5001/api/v0/pin/add?arg=<ipfs-path>&recursive=true&progress=<value>"

		std::string sError = "";
		std::string sResponse = IPFSAPICommand("pin/add", "?arg=" + sHash + "&recursive=true", sError);
		results.push_back(Pair("Result", sResponse));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "pogaudit")
	{
		int nHeight = chainActive.Tip()->nHeight;
		if (params.size() == 2)	nHeight = cdbl(params[1].get_str(), 0);
		CBlockIndex* pindex = FindBlockByHeight(nHeight);
		TitheDifficultyParams tdp = GetTitheParams(pindex);
		if (pindex)
		{
			BOOST_FOREACH(const PAIRTYPE(std::string, CTitheObject)& item, pindex->mapTithes)
		    {
				CTitheObject oTithe = item.second;
				int iLegal = IsTitheLegal2(oTithe, tdp);
				std::string sErr = TitheErrorToString(iLegal);

				std::string sRow = "Legal: " + RoundToString(iLegal, 0) + " [" + sErr + "], Amount: " + RoundToString((double)(oTithe.Amount/COIN),2) 
					+ ", Height: " + RoundToString(oTithe.Height, 0) 
					+ ", " + oTithe.TXID + "-" + RoundToString(oTithe.Ordinal, 0) 
					+ ", Age: " + RoundToString(oTithe.Age, 2) + ", NickName: " + oTithe.NickName;
				results.push_back(Pair(oTithe.Address, sRow));
			}
		}
	}
	else if (sItem == "getdimensionalbalance")
	{
		if (params.size() != 3)
			throw runtime_error("You must specify min_coin_age (days), min_coin_amount.  IE: exec getdimensionalbalance 1 1000.");

		double dMinAge = cdbl(params[1].get_str(), 4);
		CAmount caMinAmt = cdbl(params[2].get_str(), 4) * COIN;
		if (caMinAmt < (.01 * COIN)) caMinAmt = (.01 * COIN);
		LogPrintf("GetDimensionalBalance::Checking For MinAge %f, Amount %f ", dMinAge, (double)(caMinAmt/COIN));
		std::map<int64_t, CTitheObject> dtb = pwalletMain->GetDimensionalCoins(dMinAge, caMinAmt);
		CAmount nTotal = 0;
		BOOST_FOREACH(const PAIRTYPE(int64_t, CTitheObject)& item, dtb)
    	{
			CTitheObject c = item.second;
			results.push_back(Pair("Amount " + RoundToString(((double)(c.Amount / COIN)), 2), 
				"Age " + RoundToString(c.Age, 2)));
			nTotal += c.Amount;
		}
		results.push_back(Pair("Total", (double)(nTotal / COIN)));
	}
	else if (sItem == "bankroll")
	{
		if (params.size() != 3)
			throw runtime_error("You must specify type: IE 'exec bankroll quantity denomination'.  IE exec bankroll 10 100 (creates ten 100 BBP bills).");
		double nQty = cdbl(params[1].get_str(), 0);
		CAmount denomination = cdbl(params[2].get_str(), 4) * COIN;
		std::string sError = "";
		std::string sTxId = CreateBankrollDenominations(nQty, denomination, sError);
		if (!sError.empty())
		{
			results.push_back(Pair("Error", sError));
		}
		else
		{
			results.push_back(Pair("TXID", sTxId));
		}
	}
	else if (sItem == "istithelegal")
	{
		if (params.size() != 2)
			throw runtime_error("You must specify txid.");
		std::string sTxId = params[1].get_str();
		CTransaction txTithe;
		uint256 hashBlockTithe;
		uint256 uTxTithe = ParseHashV(sTxId, "txid");

		if (GetTransaction(uTxTithe, txTithe, Params().GetConsensus(), hashBlockTithe, true))
		{
			uint256 hashInput = txTithe.vin[0].prevout.hash;
			int hashInputOrdinal = txTithe.vin[0].prevout.n;
			int64_t nTxTime = 0;
			CAmount caAmount = 0;
			GetTxTimeAndAmount(hashInput, hashInputOrdinal, nTxTime, caAmount);
			if (mapBlockIndex.count(hashBlockTithe) == 0) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
		    CBlockIndex* pindex = mapBlockIndex[hashBlockTithe];
			double nTitheAge = (double)((pindex->GetBlockTime() - nTxTime) / 86400);
			CAmount nTotal = GetTitheTotal(txTithe);
			bool bTitheLegal = (nTitheAge >= pindex->pprev->nMinCoinAge && caAmount >= pindex->pprev->nMinCoinAmount && nTotal <= pindex->pprev->nMaxTitheAmount);

			results.push_back(Pair("Tithe_Legal", bTitheLegal));
			results.push_back(Pair("Tithe_Age", nTitheAge));
			results.push_back(Pair("Tithe_Spent_Coin_Amount", (double)(caAmount/COIN)));
			results.push_back(Pair("Tithe_Amount", (double)(nTotal/COIN)));
			results.push_back(Pair("Block_diff_min_coin_age", pindex->nMinCoinAge));
			results.push_back(Pair("Block_diff_min_coin_amt", (double)(pindex->nMinCoinAmount/COIN)));
			results.push_back(Pair("Block_diff_max_tithe_amt", (double)(pindex->nMaxTitheAmount/COIN)));
			results.push_back(Pair("Tithed_Height", pindex->nHeight));
		}
		else
		{
			results.push_back(Pair(sTxId,"Cant locate tithe."));
		}
	}
	else if (sItem == "mempool")
	{
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        BOOST_FOREACH(uint256& hash, vtxid) 
		{
             CTransaction tx;
             bool fInMemPool = mempool.lookup(hash, tx);
			 if (fInMemPool)
			 {
				 results.push_back(Pair("txid", tx.GetHash().GetHex()));
				 std::string sRecipient = PubKeyToAddress(tx.vout[0].scriptPubKey);
				 results.push_back(Pair("vout", sRecipient));
			 }
		}
		/*
	    int nTitheCount = mempool.getTitheCount();
		double dPogDiff = GetPOGDifficulty(chainActive.Tip());
		results.push_back(Pair("tithe_count", nTitheCount));
		results.push_back(Pair("pog_difficulty", dPogDiff));
		*/
	}
	else if (sItem == "chat1")
	{
		if (params.size() != 3)
			throw runtime_error("You must specify To Message.");
	
		std::string sTo = params[1].get_str();
		std::string sMsg = params[2].get_str();
		std::string sNickName = GetArg("-nickname", "");
		CChat chat;
		chat.nTime = GetAdjustedTime();
		chat.nID           = 1;  
		chat.nPriority     = 5;
		chat.sPayload = sMsg;
		chat.sFromNickName = sNickName;
		chat.sToNickName = sTo;
		chat.sDestination = sTo;
		chat.bPrivate = true;
	    results.push_back(Pair("hash", chat.GetHash().GetHex()));
		results.push_back(Pair("chat", chat.ToString()));
		SendChat(chat);
    }
	else if (sItem == "diff1")
	{
		const CBlockIndex *pindexLast = chainActive.Tip();
		int j1 = Get24HourAvgBits(pindexLast, pindexLast->nBits);
		double dDiff = ConvertBitsToDouble(j1);
		double dDiff2 = ConvertBitsToDouble(pindexLast->nBits);
		results.push_back(Pair("bits", (double)j1));
		results.push_back(Pair("bits2",(double)pindexLast->nBits));
		results.push_back(Pair("ddiff", dDiff));
		results.push_back(Pair("ddiff2", dDiff2));
	}
	else if (sItem == "updpogpool")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		CBlock block;
		if (ReadBlockFromDisk(block, chainActive.Tip(), consensusParams, "InitializePogPool"))
		{
			InitializePogPool(chainActive.Tip(), BLOCKS_PER_DAY, block);
		}
		results.push_back(Pair("updpogpool", 1));
	}
	else if (sItem == "comparemask")
	{
		CAmount nMask = (.001) * COIN;
		std::string sType = params[1].get_str();
		CAmount nAmount = cdbl(sType, 10) * COIN;
		std::string s1 = params[1].get_str();
		double n1 = cdbl(s1,4);
		results.push_back(Pair("r1", n1));
		results.push_back(Pair("r2", (double)(R20(n1 * COIN) / COIN)));
		results.push_back(Pair("mask", (double)nMask/COIN));
		results.push_back(Pair("amt", (double)nAmount/COIN));
		results.push_back(Pair("compare", CompareMask(nAmount, nMask)));
		results.push_back(Pair("min_relay_fee", DEFAULT_MIN_RELAY_TX_FEE));
	}
	else if (sItem == "develop")
	{
		results.push_back(Pair("develop_branch", 1));
	}
	else if (sItem == "datalist")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify type: IE 'exec datalist PRAYER'.  Optionally you may enter a lookback period in days: IE 'exec datalist PRAYER 30'.");
		std::string sType = params[1].get_str();
		double dDays = 30;
		if (params.size() == 3)
		{ 
			dDays = cdbl(params[2].get_str(),0);
		}
		int iSpecificEntry = 0;
		std::string sEntry = "";
		UniValue aDataList = GetDataList(sType, (int)dDays, iSpecificEntry, "", sEntry);
		return aDataList;
	}
	else if (sItem == "unbanked")
	{
		std::string sUnbanked = GetBoincUnbankedReport("pool");
		std::vector<std::string> vInput = Split(sUnbanked.c_str(),"<ROW>");
		for (int i = 0; i < (int)vInput.size(); i++)
		{
			double dRosettaID = cdbl(GetElement(vInput[i], ",", 0), 0);
			if (dRosettaID > 0)
			{
				std::string sCPID = GetCPIDByRosettaID(dRosettaID);
				if (sCPID.empty()) sCPID = "CPID_NOT_ON_FILE";
				results.push_back(Pair(sCPID, dRosettaID));
			}
		}
	}
	else if (sItem == "leaderboard")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nLastDCHeight = GetLastDCSuperblockWithPayment(chainActive.Tip()->nHeight);
		UniValue aDataList = GetLeaderboard(nLastDCHeight);
		return aDataList;
	}
	else if (sItem == "clearcache")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify type: IE 'run clearcache PRAYER'.");
		std::string sType = params[1].get_str();
		ClearCache(sType);
		results.push_back(Pair("Cleared",sType));
	}
	else if (sItem == "writecache")
	{
		if (params.size() != 4)
			throw runtime_error("You must specify type, key and value: IE 'run writecache type key value'.");
		std::string sType = params[1].get_str();
		std::string sKey = params[2].get_str();
		std::string sValue = params[3].get_str();
		WriteCache(sType,sKey,sValue,GetAdjustedTime());
		results.push_back(Pair("WriteCache",sValue));
	}
	else if (sItem == "sins")
	{
		std::string sEntry = "";
		int iSpecificEntry = 0;
		UniValue aDataList = GetDataList("SIN", 7, iSpecificEntry, "", sEntry);
		return aDataList;
	}
	else if (sItem == "memorizeprayers")
	{
		MemorizeBlockChainPrayers(false, false, false, false);
		results.push_back(Pair("Memorized",1));
	}
	else if (sItem == "readverse")
	{
		if (params.size() != 3 && params.size() != 4)
			throw runtime_error("You must specify Book and Chapter: IE 'readverse CO2 10'.  Optionally you may enter the VERSE #, IE: 'readverse CO2 10 2'.  To see a list of books: run getbooks.");
		std::string sBook = params[1].get_str();
		int iChapter = cdbl(params[2].get_str(),0);
		int iVerse = 0;
		if (params.size()==4) iVerse = cdbl(params[3].get_str(),0);
		results.push_back(Pair("Book",sBook));
		results.push_back(Pair("Chapter",iChapter));
		if (iVerse > 0) results.push_back(Pair("Verse",iVerse));
		int iStart=0;
		int iEnd=0;
		GetBookStartEnd(sBook,iStart,iEnd);
		for (int i = iVerse; i < 99; i++)
		{
			std::string sVerse = GetVerse(sBook,iChapter,i,iStart-1,iEnd);
			if (iVerse > 0 && i > iVerse) break;
			if (!sVerse.empty())
			{
				std::string sKey = sBook + " " + RoundToString(iChapter,0) + ":" + RoundToString(i,0);
			    results.push_back(Pair(sKey,sVerse));
			}
		}
	
	}
	else if (sItem == "bookname")
	{
		std::string sBookName = params[1].get_str();
		std::string sReversed = GetBookByName(sBookName);
		results.push_back(Pair(sBookName,sReversed));
	}
	else if (sItem == "books")
	{
		for (int i = 0; i < 66; i++)
		{
			std::string sBookName = GetBook(i);
			std::string sReversed = GetBookByName(sBookName);
			results.push_back(Pair(sBookName,sReversed));
		}
	}
	else if (sItem == "version")
	{
		results.push_back(Pair("Version","1.1"));
	}
	else
	{
		results.push_back(Pair("Error","Unknown command: " + sItem));
	}
	return results;
}


UniValue getmempoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
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



UniValue invalidateblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "invalidateblock \"hash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, Params().GetConsensus(), pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "reconsiderblock \"hash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ReconsiderBlock(state, pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

bool IsGovObjPaid(std::string sGobjId)
{
	if (sGobjId.empty()) return false;
	std::string strType = "triggers";
    int nStartTime = 0; 
	nStartTime = GetAdjustedTime() - (86400 * 64);
   
    std::vector<CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sDelim = "|";
	const Consensus::Params& consensusParams = Params().GetConsensus();
				
    BOOST_FOREACH(CGovernanceObject* pGovObj, objs)
    {
            if(strType == "proposals" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
            if(strType == "triggers" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
            if(strType == "watchdogs" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_WATCHDOG) continue;

			UniValue obj = pGovObj->GetJSONObject();
			int iAbsYes = pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			int nHeight = obj["event_block_height"].get_int();
			std::string sLocalHash = pGovObj->GetHash().GetHex();
		
			// 6-19-2018 - R ANDREWS - CHECK IF PROPOSAL WAS PAID
			if (nHeight > 0 && iAbsYes > 0 && nHeight <= chainActive.Tip()->nHeight)
			{
				std::string sProposalHashes = obj["proposal_hashes"].get_str();

				if (false)
				{
						LogPrintf(" Hash %s, Height %f, IsGovObjPaid, govobj %s -- ProposalHashes %s, AbsYesCount %f, nHeight %f \n", 
							sLocalHash.c_str(), (double)nHeight, sGobjId.c_str(), 
							sProposalHashes.c_str(), (double)iAbsYes, (double)nHeight);
				}
			
				if (Contains(sProposalHashes, sGobjId))
				{
					// Trigger contains	the proposal, and it has a net yes vote, lets see if it was actually paid
					std::string sPaymentAddresses = obj["payment_addresses"].get_str();
					std::string sPaymentAmounts = obj["payment_amounts"].get_str();
					if (false) LogPrintf("IsGovObjPaid::Paymentaddresses %s", sPaymentAddresses.c_str());

					CBlockIndex* pindex = FindBlockByHeight(nHeight);
					if (!pindex || sPaymentAddresses.empty() || sPaymentAmounts.empty()) 
					{
						LogPrintf("Trigger @ %f Not Found  or addresses empty or amounts empty",(double)nHeight);
						return false;
					}

					CBlock block;
					std::string sBlockPayments = "";
					std::string sBlockRecips = "";
					if (ReadBlockFromDisk(block, pindex, consensusParams, "IsGovObjPaid")) 
					{
						for (unsigned int i = 0; i < block.vtx[0].vout.size(); i++)
						{
							double dAmount = block.vtx[0].vout[i].nValue / COIN;
							std::string sRecipient = PubKeyToAddress(block.vtx[0].vout[i].scriptPubKey);
							sBlockPayments += RoundToString(dAmount,2) + "|";
							sBlockRecips += sRecipient + "|";
						}

						if (false) LogPrintf("Verifying blockpayments %s, block recips %s ",sBlockPayments.c_str(), sBlockRecips.c_str());

						// Now verify the block paid the proposals in the trigger
						std::vector<std::string> vPAD = Split(sPaymentAddresses.c_str(), "|");
						std::vector<std::string> vPAM = Split(sPaymentAmounts.c_str(), "|");
						if (vPAD.size() != vPAM.size())
						{
							LogPrintf("IsGovObjPaid::vPAD != vPAM size.\n");
							return false;
						}
						if (vPAD.size() < 1 || vPAM.size() < 1) 
						{
							LogPrintf("IsGovObjePaid::vPAD size < 1");
							return false;
						}
						for (int i = 0; i < (int)vPAD.size() && i < (int)vPAM.size(); i++)
						{
							if (!(Contains(sPaymentAmounts, RoundToString(cdbl(vPAM[i],2),2)) && Contains(sPaymentAddresses, vPAD[i])))
							{
								LogPrintf("IsGovObjPaid::Payment for %s %s not found. ",vPAM[i].c_str(), vPAD[i].c_str());
								return false;
							}
						}
						// All payments and addresses were found in this superblock; success.
						return true;
					}
				}
			}
			// ... Iterate to next gobject
	}
	// Gobject Not found - Or trigger not paid
	if (false) LogPrintf("Gobj not found.");
	return false;
}



