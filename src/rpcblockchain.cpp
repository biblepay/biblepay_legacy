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
#include "consensus/validation.h"
#include "main.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpcserver.h"
#include "streams.h"
#include "sync.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "base58.h"
#include "darksend.h"
#include "wallet/wallet.h"
#include "wallet/rpcwallet.cpp"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>

using namespace std;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
std::string BiblepayHttpPost(int iThreadID, std::string sActionName, std::string sDistinctUser, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, std::string sSolution);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
std::string DefaultRecAddress(std::string sType);
std::string PubKeyToAddress(const CScript& scriptPubKey);
std::string GetTxNews(uint256 hash, std::string& sHeadline);
std::vector<std::string> Split(std::string s, std::string delim);
bool CheckMessageSignature(std::string sMsg, std::string sSig);
std::string GetTemplePrivKey();
std::string SignMessage(std::string sMsg, std::string sPrivateKey);
extern double CAmountToRetirementDouble(CAmount Amount);
void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& fTitheBlocksActive);
std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);

UniValue ContributionReport();
std::string RoundToString(double d, int place);
extern std::string TimestampToHRDate(double dtm);
void GetBookStartEnd(std::string sBook, int& iStart, int& iEnd);
void ClearCache(std::string section);
void WriteCache(std::string section, std::string key, std::string value, int64_t locktime);
std::string AddNews(std::string sPrimaryKey, std::string sHTML, double dStorageFee);

UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string& outEntry);
uint256 BibleHash(uint256 hash, int64_t nBlockTime, int64_t nPrevBlockTime, bool bMining, int nPrevHeight, const CBlockIndex* pindexLast, bool bRequireTxIndex, bool f7000, bool f8000, bool fTitheBlocksActive);
void MemorizeBlockChainPrayers(bool fDuringConnectBlock);
std::string GetVerse(std::string sBook, int iChapter, int iVerse, int iStart, int iEnd);
std::string GetBookByName(std::string sName);
std::string GetBook(int iBookNumber);
extern std::string rPad(std::string data, int minWidth);


std::string GetMessagesFromBlock(const CBlock& block, std::string sMessages);
std::string GetBibleHashVerses(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexprev);
double cdbl(std::string s, int place);
UniValue createrawtransaction(const UniValue& params, bool fHelp);

CBlockIndex* FindBlockByHeight(int nHeight);
extern double GetDifficulty(const CBlockIndex* blockindex);
extern double GetDifficultyN(const CBlockIndex* blockindex, double N);

extern std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee);
void SendMoneyToDestinationWithMinimumBalance(const CTxDestination& address, CAmount nValue, CAmount nMinimumBalanceRequired, CWalletTx& wtxNew);
std::string SQL(std::string sCommand, std::string sAddress, std::string sArguments, std::string& sError);
extern void StartTradingThread();
extern CTradeTx GetOrderByHash(uint256 uHash);

extern std::string RelayTrade(CTradeTx& t, std::string Type);
extern std::string GetTrades();
static bool bEngineActive = false;


double GetDifficultyN(const CBlockIndex* blockindex, double N)
{
	// Returns Difficulty * N (Most likely this will be used to display the Diff in the wallet, since the BibleHash is much harder to solve than an ASIC hash)
	if ((blockindex && !fProd && blockindex->nHeight >= 1) || (blockindex && fProd && blockindex->nHeight >= 7000))
	{
		return GetDifficulty(blockindex)*(N/10); //f7000 feature
	}
	else
	{
		return GetDifficulty(blockindex)*N;
	}
}

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

    return dDiff;
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

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
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
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
	result.push_back(Pair("hrtime",   TimestampToHRDate(block.GetBlockTime())));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficultyN(blockindex,10)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
	result.push_back(Pair("subsidy", block.vtx[0].vout[0].nValue/COIN));
	result.push_back(Pair("blockversion", ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<VER>","</VER>")));
	
	const Consensus::Params& consensusParams = Params().GetConsensus();
	
	if (blockindex->pprev)
	{
		CAmount MasterNodeReward = GetBlockSubsidy(blockindex->pprev, blockindex->pprev->nBits, blockindex->pprev->nHeight, consensusParams, true);
		result.push_back(Pair("masternodereward", MasterNodeReward));
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
		std::string sVerses = GetBibleHashVerses(block.GetHash(), block.GetBlockTime(), blockindex->pprev->nTime, blockindex->pprev->nHeight, blockindex->pprev);
		result.push_back(Pair("verses", sVerses));
    	// Check work against BibleHash
		bool f7000;
		bool f8000;
		bool fTitheBlocksActive;
		GetMiningParams(blockindex->pprev->nHeight, f7000, f8000, fTitheBlocksActive);

		arith_uint256 hashTarget = arith_uint256().SetCompact(blockindex->nBits);
		uint256 hashWork = blockindex->GetBlockHash();
		uint256 bibleHash = BibleHash(hashWork, block.GetBlockTime(), blockindex->pprev->nTime, false, blockindex->pprev->nHeight, blockindex->pprev, false, f7000, f8000, fTitheBlocksActive);
		bool bSatisfiesBibleHash = (UintToArith256(bibleHash) <= hashTarget);
		result.push_back(Pair("satisfiesbiblehash", bSatisfiesBibleHash ? "true" : "false"));
		result.push_back(Pair("biblehash", bibleHash.GetHex()));
	}
	else
	{
		if (blockindex->nHeight==0)
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
	std::string sPrayers = GetMessagesFromBlock(block, "PRAYER");
	result.push_back(Pair("prayers", sPrayers));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}


UniValue showblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
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
	return blockToJSON(block, pblockindex, false);
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

    return blockToJSON(block, pblockindex);
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
        setTips.insert(item.second);
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        const CBlockIndex* pprev = item.second->pprev;
        if (pprev)
            setTips.erase(pprev);
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
			//std::string sVersion = RoundToString(block.nVersion,0); // In case we ever add a version suffix
			std::string sVersion2 = ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<VER>","</VER>");
			//mvBlockVersion[sVersion]++;
			mvBlockVersion[sVersion2]++;

		 }
    }
}

void CancelOrders(bool bClear)
{
		map<uint256, CTradeTx> uDelete;
		BOOST_FOREACH(PAIRTYPE(const uint256, CTradeTx)& item, mapTradeTxes)
		{
			CTradeTx ttx = item.second;
			uint256 hash = ttx.GetHash();
			if (bClear && ttx.EscrowTXID.empty())
			{
				uDelete.insert(std::make_pair(hash, ttx));
			}
			if (ttx.Action=="CANCEL" && ttx.EscrowTXID.empty())
			{
				uDelete.insert(std::make_pair(hash, ttx));
				RelayTrade(ttx, "cancel"); // Cancel order on the network
			}
			int64_t tradeAge = GetAdjustedTime() - ttx.TradeTime;
			if (tradeAge > (60*60*12) && ttx.EscrowTXID.empty())
			{
				uDelete.insert(std::make_pair(hash, ttx));
			}
		}
		BOOST_FOREACH(PAIRTYPE(const uint256, CTradeTx)& item, uDelete)
		{
			uint256 h = item.first;
			mapTradeTxes.erase(h);
		}
}



std::string RelayTrade(CTradeTx& t, std::string Type)
{
	std::string sArgs="hash=" + t.GetHash().GetHex() + ",address=" + t.Address + ",time=" + RoundToString(t.TradeTime,0) + ",quantity=" + RoundToString(t.Quantity,0)
		+ ",action=" + t.Action + ",symbol=" + t.Symbol + ",price=" + RoundToString(t.Price,4) + ",escrowtxid=" + t.EscrowTXID + ",vout=" + RoundToString(t.VOUT,0) + ",txid=";
	std::string out_Error = "";
	std::string sResponse = SQL(Type, t.Address, sArgs, out_Error);
	// For Buy, Sell or Cancel, refresh the trading engine time so the thread does not die:
	nLastTradingActivity = GetAdjustedTime();
	return out_Error;
}

UniValue GetTradeHistory()
{
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("Trading");
	std::string sRes = SQL("execution_history", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<TRADEHISTORY>","</TRADEHISTORY>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<TRADEH>");
    UniValue ret(UniValue::VOBJ);
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			std::string Symbol = ExtractXML(sE,"<SYMBOL>","</SYMBOL>");
			std::string Action = ExtractXML(sE,"<ACTION>","</ACTION>");
			double Amount = cdbl(ExtractXML(sE,"<AMOUNT>","</AMOUNT>"),4);
			double Quantity = cdbl(ExtractXML(sE,"<QUANTITY>","</QUANTITY>"),4);
			std::string sHash = ExtractXML(sE,"<HASH>","</HASH>");
			double Total = cdbl(ExtractXML(sE,"<TOTAL>","</TOTAL>"),4);
			std::string sExecuted=ExtractXML(sE,"<EXECUTED>","</EXECUTED>");
			//uint256 hash = uint256S("0x" + sHash);
		  	if (!Symbol.empty() && Amount > 0)
			{
				std::string sActNarr = (Action == "BUY") ? "BOT" : "SOLD";
				std::string sNarr = sActNarr + " " + RoundToString(Quantity,0) 
					+ " " + Symbol + " @ " + RoundToString(Amount,4) + " TOTAL " + RoundToString(Total,2) + "BBP ORDER " + sHash + ".";
			 	ret.push_back(Pair(sExecuted, sNarr));
			}
		}
	}
	return ret;
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
	std::string sAddress = DefaultRecAddress("Trading");
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
	std::string sAddress = DefaultRecAddress("Trading");
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
 	std::string sAddress = DefaultRecAddress("Trading");
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

std::string ProcessEscrow()
{
	std::string out_Error = "";
	std::string sAddress = DefaultRecAddress("Trading");
	std::string sRes = SQL("process_escrow", sAddress, "txid=", out_Error);
	std::string sEsc = ExtractXML(sRes,"<ESCROWS>","</ESCROWS>");
	std::vector<std::string> vEscrow = Split(sEsc.c_str(),"<ROW>");
	for (int i = 0; i < (int)vEscrow.size(); i++)
	{
		std::string sE = vEscrow[i];
		if (!sE.empty())
		{
			std::string Symbol = ExtractXML(sE,"<SYMBOL>","</SYMBOL>");
			std::string EscrowAddress = ExtractXML(sE,"<ESCROW_ADDRESS>","</ESCROW_ADDRESS>");
			double Amount = cdbl(ExtractXML(sE,"<AMOUNT>","</AMOUNT>"),4);
			std::string sHash = ExtractXML(sE,"<HASH>","</HASH>");
			uint256 hash = uint256S("0x" + sHash);
		    CTradeTx trade = GetOrderByHash(hash);
			// Ensure this matches our Trading Tx before sending escrow, and Escrow has not already been staked
			LogPrintf(" Orig trade address %s  Orig Trade Amount %f  esc amt %f  ",trade.Address.c_str(),trade.Total, Amount);
			AddDebugMessage("PREPARING ESCROW FOR " + RoundToString(Amount,2) + " " + Symbol + " FOR SANCTUARY " + EscrowAddress + " OLD ESCTXID " + trade.EscrowTXID);
			if (trade.Address==sAddress && trade.EscrowTXID.empty())
			{
				if ((trade.Action=="BUY" && (Amount > (trade.Total-1) && Amount < (trade.Total+1))) || 
					(trade.Action=="SELL" && (Amount > (trade.Quantity-1) && Amount < (trade.Quantity+1))))
				{
					if (!Symbol.empty() && !EscrowAddress.empty() && Amount > 0)
					{
						// Send Escrow in, and remember txid.  Sanctuary will send us our escrow back using this as a 'dependent' TXID if the trade is not executed by piggybacking the escrow refund on the back of the escrow transmission.
						// If the trade is executed, the trade will be processed using a depends-on identifier so the collateral is not lost, by spending its output to the other trader after receiving the recipients escrow.  The recipients collateral will be sent to us by piggybacking the collateral on the back of the same txid the GodNode received.
						CComplexTransaction cct("");
						std::string sColor = (trade.Action == "BUY") ? "" : "401";
						std::string sScript = cct.GetScriptForAssetColor(sColor);
						CBitcoinAddress address(EscrowAddress);
						if (address.IsValid())	
						{
							CAmount nAmount = 0;
							nAmount = (sColor=="401") ? Amount * (RETIREMENT_COIN) * 100 : Amount * COIN;
							CWalletTx wtx;
							// Ensure the Escrow held by market maker holds correct color for each respective leg
							SendColoredEscrow(address.Get(), nAmount, false, wtx, sScript);
							std::string TXID = wtx.GetHash().GetHex();
							std::string sMsg = "Sent " + RoundToString(Amount/COIN,2) + " for escrow on txid " + TXID;
							AddDebugMessage(sMsg);
							LogPrintf("\n Sent %f RetirementCoins for Escrow %s \n", (double)Amount,TXID.c_str());
							trade.EscrowTXID = TXID;
							CTransaction tx;
							uint256 hashBlock;
							if (GetTransaction(wtx.GetHash(), tx, Params().GetConsensus(), hashBlock, true))
							{
								 for (int i=0; i < (int)tx.vout.size(); i++)
								 {
				 					if (tx.vout[i].nValue == nAmount)
									{
										trade.VOUT = i;
										std::string sErr = RelayTrade(trade, "escrow");
									}
								 }
							}
						}
					}
				}
			}
		}
	}
	return out_Error;
}

std::string GetTrades()
{
	std::string out_Error = "";
	std::string sAddress=DefaultRecAddress("Trading");
	std::string sRes = SQL("trades", sAddress, "txid=", out_Error);
	std::string sTrades = ExtractXML(sRes,"<TRADES>","</TRADES>");
	std::vector<std::string> vTrades = Split(sTrades.c_str(),"<ROW>");
	LogPrintf("TradeList %s ",sTrades.c_str());
	AddDebugMessage("Listing " + RoundToString(vTrades.size(),0) + " trades");

	for (int i = 0; i < (int)vTrades.size(); i++)
	{
		std::string sTrade = vTrades[i];
		if (!sTrade.empty())
		{
			int64_t time = cdbl(ExtractXML(sTrade,"<TIME>","</TIME>"),0);
			std::string action = ExtractXML(sTrade,"<ACTION>","</ACTION>");
			std::string symbol = ExtractXML(sTrade,"<SYMBOL>","</SYMBOL>");
			std::string address = ExtractXML(sTrade,"<ADDRESS>","</ADDRESS>");
			int64_t quantity = cdbl(ExtractXML(sTrade,"<QUANTITY>","</QUANTITY>"),0);
			std::string escrow_txid = ExtractXML(sTrade,"<ESCROWTXID>","</ESCROWTXID>");
			double price = cdbl(ExtractXML(sTrade,"<PRICE>","</PRICE>"),4);
			if (!symbol.empty() && !address.empty())
			{
				CTradeTx ttx(time,action,symbol,quantity,price,address);
				ttx.EscrowTXID = escrow_txid;
				uint256 uHash = ttx.GetHash();
				int iTradeCount = mapTradeTxes.count(uHash);
				if (iTradeCount > 0) mapTradeTxes.erase(uHash);
				mapTradeTxes.insert(std::make_pair(uHash, ttx));
			}
		}
	}
	return out_Error;

}

std::string GetOrderText(CTradeTx& trade, double runningTotal)
{
	// This is the order book format 
	// Sell Side: PricePerBBP, Quantity, BBPAmount, BBPTotal ........ SYMBOL (RBBP) ............... Buy Side: PricePerBBP, Quantity, BBPAmount, BBPTotal
	//            1.251        1000      1251.00      1251.00                                                  .25          1000      250.00      250.00
	std::string sOB = rPad(RoundToString(trade.Price,4),6)
		+ "  " + rPad(RoundToString(trade.Quantity,0),8)
		+ "  " + rPad(RoundToString(trade.Quantity*trade.Price,2),8)
		+ "  " + rPad(RoundToString(runningTotal,2),12);
	return sOB;
}

CTradeTx GetOrderByHash(uint256 uHash)
{
	CTradeTx trade;
	int iTradeCount = mapTradeTxes.count(uHash);
	if (iTradeCount==0) return trade;
	trade = mapTradeTxes.find(uHash)->second;
	return trade;
}

std::string rPad(std::string data, int minWidth)
{
	if ((int)data.length() >= minWidth) return data;
	int iPadding = minWidth - data.length();
	std::string sPadding = std::string(iPadding,' ');
	std::string sOut = data + sPadding;
	return sOut;
}

UniValue GetOrderBook(std::string sSymbol)
{
	StartTradingThread();
	std::string sTrades = GetTrades();

	// Generate the Order Book by sorting two maps: one for the sell side, one for the buy side:
	vector<pair<int64_t, uint256> > vSellSide;
	vSellSide.reserve(mapTradeTxes.size());
	vector<pair<int64_t, uint256> > vBuySide;
	vBuySide.reserve(mapTradeTxes.size());
   
    BOOST_FOREACH(const PAIRTYPE(uint256, CTradeTx)& item, mapTradeTxes)
    {
		CTradeTx trade = item.second;
		uint256 hash = trade.GetHash();
		int64_t	rank = trade.Price * 100;
		if (trade.Action=="SELL" && trade.EscrowTXID.empty())
		{
			vSellSide.push_back(make_pair(rank, hash));
		}
		else if (trade.Action=="BUY" && trade.EscrowTXID.empty())
		{
			vBuySide.push_back(make_pair(rank, hash));
		}
    }
    sort(vSellSide.begin(), vSellSide.end());
	sort(vBuySide.begin(), vBuySide.end());
	// Go forward through sell side and backward through buy side simultaneously to make a market:
	int iMaxDisplayRows=30;
	std::string Sell[iMaxDisplayRows];
	std::string Buy[iMaxDisplayRows];
	int iSellRows = 0;
	int iBuyRows = 0;
	int iTotalRows = 0;
	double dTotalSell = 0;
	double dTotalBuy = 0;
    
    BOOST_FOREACH(const PAIRTYPE(int64_t, uint256)& item, vSellSide)
    {
		CTradeTx trade = GetOrderByHash(item.second);
		if (trade.Total > 0 && trade.EscrowTXID.empty())
		{
			dTotalSell += (trade.Quantity * trade.Price);
			Sell[iSellRows] = GetOrderText(trade,dTotalSell);
			iSellRows++;
			if (iSellRows > iMaxDisplayRows) break;
		}
    }
	BOOST_REVERSE_FOREACH(const PAIRTYPE(int64_t, uint256)& item, vBuySide)
    {
		CTradeTx trade = GetOrderByHash(item.second);
		if (trade.Total > 0 && trade.EscrowTXID.empty())
		{
			dTotalBuy += (trade.Quantity * trade.Price);
			Buy[iBuyRows] = GetOrderText(trade,dTotalBuy);
			iBuyRows++;
			if (iBuyRows >= iMaxDisplayRows) break;
		}
    }

    UniValue ret(UniValue::VOBJ);
	iTotalRows = (iSellRows >= iBuyRows) ? iSellRows : iBuyRows;
	std::string sHeader = "[S]Price Quantity  Amount   Total       (" + sSymbol + ")  Price  Quantity Amount     Total   [B]";
	ret.push_back(Pair("0",sHeader));

	for (int i = 0; i <= iTotalRows; i++)
	{
		if (Sell[i].empty()) Sell[i]= rPad(" ", 40);
		if (Buy[i].empty())  Buy[i] = rPad(" ", 40);
		std::string sConsolidatedRow = Sell[i] + " |" + sSymbol + "| " + Buy[i];
		ret.push_back(Pair(RoundToString(i,0), sConsolidatedRow));
	}
	return ret;
	
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


double CAmountToRetirementDouble(CAmount Amount)
{
	double d = Amount / RETIREMENT_COIN / 100;
	return d;
}

UniValue exec(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2  && params.size() != 3 && params.size() != 4 && params.size() != 5 && params.size() != 6 && params.size() != 7))
        throw runtime_error(
		"exec <string::itemname> <string::parameter> \r\n"
        "Executes an RPC command by name.");

    std::string sItem = params[0].get_str();
	if (sItem=="") throw runtime_error("Command argument invalid.");

    UniValue results(UniValue::VOBJ);
	results.push_back(Pair("Command",sItem));
	if (sItem == "contributions")
	{
		UniValue aContributionReport = ContributionReport();
		return aContributionReport;
	}
	else if (sItem == "reboot")
	{
		fReboot2=true;
		results.push_back(Pair("Reboot",1));
    }
	else if (sItem == "getnews")
	{
		// getnews txid
		uint256 hash = ParseHashV(params[1], "parameter 1");
		results.push_back(Pair("TxID",hash.GetHex()));
		std::string sHeadline = "";
		std::string sNews = GetTxNews(hash, sHeadline);
		results.push_back(Pair("Headline", sHeadline));
		results.push_back(Pair("News", sNews));
	}
	else if (sItem == "testnews")
	{
		std::string sTxId = AddNews("primarykey","html",250);
		results.push_back(Pair("Sent",sTxId));
	}
	else if (sItem == "sendmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'run sendmessage PRAYER mother Please_pray_for_my_mother._She_has_this_disease.'";
		if (params.size() != 4)
			 throw runtime_error(sError);

		std::string sType = params[1].get_str();
		std::string sPrimaryKey = params[2].get_str();
		std::string sValue = params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw runtime_error(sError);
		std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, 1);
		results.push_back(Pair("Sent",sValue));
		results.push_back(Pair("TXID",sResult));
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
		std::string sResponse = BiblepayHttpPost(0,"POST","USER_A","PostSpeed","http://www.biblepay.org","home.html",80,"");
		results.push_back(Pair("beta_post", sResponse));
		results.push_back(Pair("beta_post_length", (double)sResponse.length()));
		std::string sResponse2 = BiblepayHttpPost(0,"POST","USER_A","PostSpeed","http://www.biblepay.org","404.html",80,"");
		results.push_back(Pair("beta_post_404", sResponse2));
		results.push_back(Pair("beta_post_length_404", (double)sResponse2.length()));
	}
	else if (sItem == "biblehash")
	{
		if (params.size() != 5)
			throw runtime_error("You must specify blockhash, blocktime, prevblocktime and prevheight, IE: run biblehash blockhash 12345 12234 100.");
		std::string sBlockHash = params[1].get_str();
		std::string sBlockTime = params[2].get_str();
		std::string sPrevBlockTime = params[3].get_str();
		std::string sPrevHeight = params[4].get_str();
		int64_t nHeight = cdbl(sPrevHeight,0);
		uint256 blockHash = uint256S("0x" + sBlockHash);
		int64_t nBlockTime = (int64_t)cdbl(sBlockTime,0);
		int64_t nPrevBlockTime = (int64_t)cdbl(sPrevBlockTime,0);
		if (!sBlockHash.empty() && nBlockTime > 0 && nPrevBlockTime > 0 && nHeight >= 0)
		{
			bool f7000;
			bool f8000;
			bool fTitheBlocksActive;
			GetMiningParams(nHeight, f7000, f8000, fTitheBlocksActive);

			uint256 hash = BibleHash(blockHash, nBlockTime, nPrevBlockTime, true, nHeight, NULL, false, f7000, f8000, fTitheBlocksActive);
			results.push_back(Pair("BibleHash",hash.GetHex()));
		}
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
				}
			}
		}
		else
		{
			results.push_back(Pair("error","block not found"));
		}
	}
	else if ((sItem == "order" || sItem=="trade") && !fProd)
	{
		// Buy/Sell Qty Symbol [PriceInQtyOfBBP]
		// Rob A - BiblePay - Ensure Trading System honors NetworkID
		std::string sAddress=DefaultRecAddress("Trading");
		if (params.size() != 5) 
			throw runtime_error("You must specify Buy/Sell/Cancel Qty Symbol [Price].  Example: exec buy 1000 RBBP 1.25 (Meaning: I offer to buy Qty 1000 RBBP (Retirement BiblePay Coins) for 1.25 BBP EACH (TOTALING=1.25*1000 BBP).");
		std::string sAction = params[1].get_str();
		boost::to_upper(sAction);
		double dQty = cdbl(params[2].get_str(),0);
		std::string sSymbol = params[3].get_str();
		boost::to_upper(sSymbol);
		if (sSymbol != "RBBP")
		{
			throw runtime_error("Sorry, Only symbol RBBP is currently supported.");
		}	

		double dPrice = cdbl(params[4].get_str(),4);
		if (dPrice < .01 || dPrice > 9999)
		{
			throw runtime_error("Sorry, price is out of range.");
		}
		if (dQty < 1 || dQty > 9999999)
		{
			throw runtime_error("Sorry, quantity is out of range.");
		}
		CTradeTx ttx(GetAdjustedTime(),sAction,sSymbol,dQty,dPrice,sAddress);
		results.push_back(Pair("Action",sAction));
		results.push_back(Pair("Symbol",sSymbol));
		results.push_back(Pair("Qty",dQty));
		results.push_back(Pair("Price",dPrice));
		results.push_back(Pair("Rec Address",sAddress));
		// If cancel, remove tx by tx hash
		uint256 uHash = ttx.GetHash();
		int iTradeCount = mapTradeTxes.count(uHash);
		// Note, the Trade class will validate the Action (BUY/SELL/CANCEL), and will validate the Symbol (RBBP/BBP)
		if (sAction=="CANCEL") 
		{
			// Cancel all trades for Address + Symbol + Action
			CancelOrders(false);
			results.push_back(Pair("Canceling Order",uHash.GetHex()));
		}
		else if (sAction == "BUY" || sAction == "SELL")
		{
			if (iTradeCount==0) mapTradeTxes.insert(std::make_pair(uHash, ttx));
			results.push_back(Pair("Placing Order",uHash.GetHex()));
		}
		else
		{
			results.push_back(Pair("Unknown Action",sAction));
		}
		if (!ttx.Error.empty()) results.push_back(Pair("Error",ttx.Error));
		// Relay changes to network - and relay once every 5 mins in case a new node comes online.
		if (!fProd)
		{
			std::string sErr = RelayTrade(ttx, "order");
			if (!sErr.empty()) results.push_back(Pair("Relayed",sErr));
		}
	}
	else if (sItem == "orderbook" && !fProd)
	{
		CancelOrders(true);
		UniValue r = GetOrderBook("RBBP");
		return r;
	}
	else if (sItem == "starttradingengine" && !fProd)
	{
		StartTradingThread();
		results.push_back(Pair("Started",1));
	}
	else if ((sItem == "listorders" || sItem == "orderlist") && !fProd)
	{
		CancelOrders(true);

		std::string sError = GetTrades();
		results.push_back(Pair("#",0));
		if (!sError.empty()) results.push_back(Pair("Error",sError));
		int i = 0;
		BOOST_FOREACH(PAIRTYPE(const uint256, CTradeTx)& item, mapTradeTxes)
		{
			CTradeTx ttx = item.second;
			uint256 hash = ttx.GetHash();
			if (ttx.Total > 0 && ttx.EscrowTXID.empty())
			{
				i++;
				results.push_back(Pair("#",i));
				results.push_back(Pair("Address",ttx.Address));
				results.push_back(Pair("Hash",hash.GetHex()));
				results.push_back(Pair("Symbol",ttx.Symbol));
				results.push_back(Pair("Action",ttx.Action));
				results.push_back(Pair("Qty",ttx.Quantity));
				results.push_back(Pair("BBP_Price",ttx.Price));
			}
		}

	}
	else if (sItem == "tradehistory")
	{
		UniValue aTH = GetTradeHistory();
		return aTH;
	}
	else if (sItem == "testtrade")
	{
		//extern std::map<uint256, CTradeTx> mapTradeTxes;
		uint256 uTradeHash = uint256S("0x1234");
		int iTradeCount = mapTradeTxes.count(uTradeHash);
		// Insert
		std::string sIP = "127.0.0.1";
		CTradeTx ttx(0,"BUY","BBP",10,1000,sIP);
        if (iTradeCount==0) mapTradeTxes.insert(std::make_pair(uTradeHash, ttx));
		// Report on this trade
		results.push_back(Pair("Quantity",ttx.Quantity));
		results.push_back(Pair("Symbol",ttx.Symbol));
		results.push_back(Pair("Action",ttx.Action));
		results.push_back(Pair("Map Length",iTradeCount));
		uint256 h = ttx.GetHash();
		results.push_back(Pair("Hash",h.GetHex()));
		LogPrintTrading("This is a test of trading.");
	}
	else if (sItem == "testmultisig")
	{
		// 
	}
	else if (sItem == "sendto402k")
	{
	    LOCK2(cs_main, pwalletMain->cs_wallet);
		CAmount nAmount = AmountFromValue(params[1].get_str());
	    CBitcoinAddress address(params[2].get_str());
		if (!address.IsValid())	
		{
			results.push_back(Pair("InvalidAddress",1));
		}
		else
		{
			results.push_back(Pair("Destination Address",address.ToString()));
			bool fSubtractFeeFromAmount = false;
			EnsureWalletIsUnlocked();
			std::string sAddress=DefaultRecAddress("Trading");
			results.push_back(Pair("DefRecAddr",sAddress));
			CWalletTx wtx;
			std::string sOutboundColor="";
			CComplexTransaction cct("");
			std::string sScript = cct.GetScriptForAssetColor("401");
			SendColoredEscrow(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, sScript);
			results.push_back(Pair("txid",wtx.GetHash().GetHex()));
		}
	}
	else if (sItem == "sendto401k")
	{
		//sendto401k amount destination
	    LOCK2(cs_main, pwalletMain->cs_wallet);
		CAmount nAmount = AmountFromValue(params[1].get_str())/10;
	    CBitcoinAddress address(params[2].get_str());
		if (!address.IsValid())	
		{
			results.push_back(Pair("InvalidAddress",1));
		}
		else
		{
			results.push_back(Pair("Destination Address",address.ToString()));
			bool fSubtractFeeFromAmount = false;
			EnsureWalletIsUnlocked();
			std::string sAddress=DefaultRecAddress("Trading");
			results.push_back(Pair("DefRecAddr",sAddress));
			// Complex order type
			CWalletTx wtx;
			std::string sExpectedRecipient = sAddress;
			std::string sExpectedColor = "";
			CAmount nExpectedAmount = nAmount;
			std::string sOutboundColor="401";
			results.push_back(Pair("OutboundColor",sOutboundColor));
			CComplexTransaction cct("");
			std::string sScriptComplexOrder = cct.GetScriptForComplexOrder("OCO", sOutboundColor, nExpectedAmount, sExpectedRecipient, sExpectedColor);
			results.push_back(Pair("XML",sScriptComplexOrder));
			cct.Color = sOutboundColor;
			LogPrintf("\nScript for comp order %s ",sScriptComplexOrder.c_str());
			SendColoredEscrow(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, sScriptComplexOrder);
			LogPrintf("\nScript for comp order %s ",sScriptComplexOrder.c_str());
			results.push_back(Pair("txid",wtx.GetHash().GetHex()));
		}
	}
	else if (sItem == "deprecated_createescrowtransaction")
	{

			results.push_back(Pair("deprecated","Please use createrawtransaction."));
			/*
	
		    LOCK(cs_main);
			std::string sXML = params[1].get_str();
			CMutableTransaction rawTx;
			// Process Inputs
			std::string sInputs = ExtractXML(sXML,"<INPUTS>","</INPUTS>");
			std::vector<std::string> vInputs = Split(sInputs.c_str(),"<ROW>");
			for (int i = 0; i < (int)vInputs.size(); i++)
			{
				std::string sInput = vInputs[i];
				if (!sInput.empty())
				{
					std::string sTxId = ExtractXML(sInput,"<TXID>","</TXID>");
					int nOutput = (int)cdbl(ExtractXML(sInput, "<VOUT>","</VOUT>"),0);
					int64_t nLockTime = (int64_t)cdbl(ExtractXML(sInput,"<LOCKTIME>","</LOCKTIME>"),0);
					uint256 txid = uint256S("0x" + sTxId);
					uint32_t nSequence = (nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());
					CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);
					rawTx.vin.push_back(in);
					LogPrintf("ADDING TXID %s ", sTxId.c_str());

				}
			}
		std::string sRecipients = ExtractXML(sXML,"<RECIPIENTS>","</RECIPIENTS>");
		std::vector<std::string> vRecips = Split(sRecipients.c_str(),"<ROW>");
		for (int i = 0; i < (int)vRecips.size(); i++)
		{
			std::string sRecip = vRecips[i];
			if (!sRecip.empty())
			{
				std::string sRecipient = ExtractXML(sRecip, "<RECIPIENT>","</RECIPIENT>");
				double dAmount = cdbl(ExtractXML(sRecip,"<AMOUNT>","</AMOUNT>"),4);
				std::string sData = ExtractXML(sRecip,"<DATA>","</DATA>");
				std::string sColor = ExtractXML(sRecip,"<COLOR>","</COLOR>");
				if (!sData.empty())
				{
				     std::vector<unsigned char> data = ParseHexV(sData,"Data");
					 CTxOut out(0, CScript() << OP_RETURN << data);
					 rawTx.vout.push_back(out);
				}
				else if (!sRecipient.empty())
				{
 					  CBitcoinAddress address(sRecipient);
					  CScript scriptPubKey = GetScriptForDestination(address.Get());
					  CAmount nAmount = sColor=="401" ? dAmount * COIN : dAmount * COIN; 
			          CTxOut out(nAmount, scriptPubKey);
					  rawTx.vout.push_back(out);
	    			  CComplexTransaction cct("");
					  std::string sAssetColorScript = cct.GetScriptForAssetColor(sColor); 
					  rawTx.vout[rawTx.vout.size()-1].sTxOutMessage = sAssetColorScript;
					  LogPrintf("CreateEscrowTx::Adding Recip %s, amount %f, color %s, dAmount %f ", sRecipient.c_str(),(double)nAmount,sAssetColorScript.c_str(), (double)dAmount);
				}
			}
		}
		return EncodeHexTx(rawTx);
		*/

    }
	else if (sItem == "getretirementbalance" || sItem == "retirementbalance")
	{
		results.push_back(Pair("balance",CAmountToRetirementDouble(pwalletMain->GetRetirementBalance())));
	}
	else if (sItem == "generatetemplekey")
	{
	    // Generate the Temple Keypair
	    CKey key;
		key.MakeNewKey(false);
		CPrivKey vchPrivKey = key.GetPrivKey();
		std::string sOutPrivKey = HexStr<CPrivKey::iterator>(vchPrivKey.begin(), vchPrivKey.end());
		std::string sOutPubKey = HexStr(key.GetPubKey());
		results.push_back(Pair("Private", sOutPrivKey));
		results.push_back(Pair("Public", sOutPubKey));
	}
	else if (sItem == "testsign")
	{
		std::string sPrivKey = GetTemplePrivKey();
		std::string sSig = SignMessage("1", sPrivKey);
		std::string sBadSig = SignMessage("1", "000");
		results.push_back(Pair("GoodSig", sSig));
		results.push_back(Pair("BadSig", sBadSig));
		bool bSig1 = CheckMessageSignature("1", sSig);
		bool bSig2 = CheckMessageSignature("1", sBadSig);
		results.push_back(Pair("1_RESULT", bSig1));
		results.push_back(Pair("1_BAD_RESULT", bSig2));
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
	else if (sItem == "datalist")
	{
		if (params.size() != 2 && params.size() != 3)
			throw runtime_error("You must specify type: IE 'exec datalist PRAYER'.  Optionally you may enter a lookback period in days: IE 'run datalist PRAYER 30'.");
		std::string sType = params[1].get_str();
		double dDays = 30;
		if (params.size() == 3)
		{ 
			dDays = cdbl(params[2].get_str(),0);
		}
		int iSpecificEntry = 0;
		std::string sEntry = "";
		UniValue aDataList = GetDataList(sType, (int)dDays, iSpecificEntry, sEntry);
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
		UniValue aDataList = GetDataList("SIN", 7, iSpecificEntry, sEntry);
		return aDataList;
	}
	else if (sItem == "memorizeprayers")
	{
		MemorizeBlockChainPrayers(false);
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

std::string AddNews(std::string sPrimaryKey, std::string sHTML, double dQuotedFee)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CBitcoinAddress address(sAddress);
    if (!address.IsValid()) 
	{
		return "Invalid Foundation Address";
	}
    CWalletTx wtx;
	std::string sType = "NEWS";
 	std::string sMessageType      = "<MT>" + sType + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey + "</MK>";
	std::string sMessageValue     = "<MV></MV><NEWS>";
	std::string s1 = sMessageType + sMessageKey + sMessageValue;
	
	CAmount curBalance = pwalletMain->GetBalance();
    CScript scriptPubKey = GetScriptForDestination(address.Get());
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;
	bool fSubtractFeeFromAmount = false;
	
	// Each 1k block becomes an individual vOut so we can charge a storage fee for each vOut 
	int iStepLength = 990 - s1.length();
	bool bLastStep = false;
	double dStorageFee = 500 * (sHTML.length() / 1000);
	int iParts = cdbl(RoundToString(sHTML.length()/iStepLength,2),0);
	if (iParts < 1) iParts = 1;
	CAmount nAmount = AmountFromValue(dStorageFee);
	for (int i = 0; i <= (int)sHTML.length(); i += iStepLength)
	{
		int iChunkLength = sHTML.length() - i;
		if (iChunkLength <= iStepLength) bLastStep = true;
		if (iChunkLength > iStepLength) 
		{
			iChunkLength = iStepLength;
		} 
		std::string sChunk = sHTML.substr(i, iChunkLength);
		if (bLastStep) sChunk += "</NEWS>";
	    CRecipient recipient = {scriptPubKey, nAmount / iParts, fSubtractFeeFromAmount, false, false, false, "", "", ""};
		s1 += sChunk;
		recipient.Message = s1;
		LogPrintf("\r\n Creating TXID #%f, ChunkLen %f, with Chunk %s \r\n",(double)i,(double)iChunkLength,s1.c_str());
		s1="";
		vecSend.push_back(recipient);
	}
	
    if (curBalance < dStorageFee) return "Insufficient funds (Unlock Wallet).";

	bool fUseInstantSend = false;
	bool fUsePrivateSend = false;

    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet,
                                         strError, NULL, true, fUsePrivateSend ? ONLY_DENOMINATED : ALL_COINS, fUseInstantSend)) 
	{
        if (!fSubtractFeeFromAmount && nAmount + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtx, reservekey, fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))
    {
		throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
	}

	std::string sTxId = wtx.GetHash().GetHex();
	return sTxId;
}



std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CBitcoinAddress address(sAddress);
    if (!address.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Address");
    CAmount nAmount = AmountFromValue(dStorageFee);
	CAmount nMinimumBalance = AmountFromValue(dStorageFee);
    CWalletTx wtx;
	uint256 hashNonce = GetRandHash();
	// Verify these messages are immune to replay attacks (Add message nonce)
 	std::string sMessageType      = "<MT>" + sType  + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey   + "</MK>";
	std::string sMessageValue     = "<MV>" + sValue + "</MV><NONCE>" + hashNonce.GetHex().substr(0,8) + "</NONCE>";
	std::string sMessageSig       = "";
	if (sMessageType=="TEMPLE_COMM")
	{
		std::string sPrivKey = GetTemplePrivKey();
		std::string sSig = SignMessage(sMessageValue, sPrivKey);
		sMessageSig = "<MS>" + sSig + "</MS>";
	}

	std::string s1 = sMessageType + sMessageKey + sMessageValue + sMessageSig;
	wtx.sTxMessageConveyed = s1;
    SendMoneyToDestinationWithMinimumBalance(address.Get(), nAmount, nMinimumBalance, wtx);
    return wtx.GetHash().GetHex().c_str();
}

std::string TimestampToHRDate(double dtm)
{
	if (dtm == 0) return "1-1-1970 00:00:00";
	if (dtm > 9888888888) return "1-1-2199 00:00:00";
	std::string sDt = DateTimeStrFormat("%m-%d-%Y %H:%M:%S",dtm);
	return sDt;
}


UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string& outEntry)
{
	int64_t nEpoch = GetAdjustedTime() - (iMaxAgeInDays*86400);
	if (nEpoch < 0) nEpoch = 0;
    UniValue ret(UniValue::VOBJ);
	boost::to_upper(sType);
	if (sType=="PRAYERS") sType="PRAYER";  // Just in case the user specified PRAYERS
	ret.push_back(Pair("DataList",sType));
	int iPos = 0;

    for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii) 
    {
		std::string sKey = (*ii).first;
	   	if (sKey.length() > sType.length())
		{
			if (sKey.substr(0,sType.length())==sType)
			{
				std::string sPrimaryKey = sKey.substr(sType.length() + 1, sKey.length() - sType.length());
		     	int64_t nTimestamp = mvApplicationCacheTimestamp[(*ii).first];
			
				if (nTimestamp > nEpoch || nTimestamp == 0)
				{
						std::string sValue = mvApplicationCache[(*ii).first];
						std::string sLongValue = sPrimaryKey + " - " + sValue;
						if (iPos==iSpecificEntry) outEntry = sValue;
						std::string sTimestamp = TimestampToHRDate((double)nTimestamp);
					 	ret.push_back(Pair(sPrimaryKey,sValue));
						iPos++;
				}
			}
		}
	}
	iSpecificEntry++;
	if (iSpecificEntry >= iPos) iSpecificEntry=0;  // Reset the iterator.
	return ret;
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






UniValue ContributionReport()
{

	const Consensus::Params& consensusParams = Params().GetConsensus();
	int nMaxDepth = chainActive.Tip()->nHeight;
    CBlock block;
	int nMinDepth = 1;
	double dTotal = 0;
    UniValue ret(UniValue::VOBJ);

	for (int ii = nMinDepth; ii <= nMaxDepth; ii++)
	{
   			CBlockIndex* pblockindex = FindBlockByHeight(ii);
			if (ReadBlockFromDisk(block, pblockindex, consensusParams, "CONTRIBUTIONREPORT"))
			{
				LogPrintf("Reading %f ",(double)ii);

				BOOST_FOREACH(CTransaction& tx, block.vtx)
				{
					 for (int i=0; i < (int)tx.vout.size(); i++)
					 {
				 		std::string sRecipient = PubKeyToAddress(tx.vout[i].scriptPubKey);
						double dAmount = tx.vout[i].nValue/COIN;
						if (sRecipient == consensusParams.FoundationAddress)
						{
								dTotal += dAmount;
						        ret.push_back(Pair("Block ", ii));
								ret.push_back(Pair("Amount", dAmount));
								LogPrintf("Amount %f ",dAmount);
						}
					 }
				 }
			}
	}
	ret.push_back(Pair("Grand Total", dTotal));
	return ret;
}



void static TradingThread(int iThreadID)
{
	// September 17, 2017 - Robert Andrew (BiblePay)
	LogPrintf("Trading Thread started \n");
	//int64_t nLastTrade = GetAdjustedTime();
	RenameThread("biblepay-trading");
	bEngineActive=true;
	int iCall=0;
    try 
	{
        while (true) 
		{
			// Thread dies off after 5 minutes of inactivity
			if ((GetAdjustedTime() - nLastTradingActivity) > (5*60)) break;
			MilliSleep(1000);
			iCall++;
			if (iCall > 30)
			{
				iCall=0;
				std::string sError = ProcessEscrow();
				if (!sError.empty()) LogPrintf("TradingThread::Process Escrow %s \n",sError.c_str());
			    CancelOrders(true);
				sError = GetTrades();
				if (!sError.empty()) LogPrintf("TradingThread::GetTrades %s \n",sError.c_str());
			}

		}
		bEngineActive=false;
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrintf("\nTrading Thread -- terminated\n");
        bEngineActive=false;
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("\nTrading Thread -- runtime error: %s\n", e.what());
        bEngineActive=false;
		throw;
    }
}


static boost::thread_group* tradingThreads = NULL;
void StartTradingThread()
{
	if (bEngineActive) return;
	
	nLastTradingActivity = GetAdjustedTime();
    if (tradingThreads != NULL)
    {
		LogPrintf(" exiting \n");
        tradingThreads->interrupt_all();
        delete tradingThreads;
        tradingThreads = NULL;
    }

    tradingThreads = new boost::thread_group();
	int iThreadID = 0;
	tradingThreads->create_thread(boost::bind(&TradingThread, boost::cref(iThreadID)));
	LogPrintf(" Starting Trading Thread \n" );
}


