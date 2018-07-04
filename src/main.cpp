// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The D�sh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "addrman.h"
#include "alert.h"
#include "arith_uint256.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "hash.h"
#include "init.h"
#include "podc.h"
#include "merkleblock.h"
#include "net.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "spork.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "versionbits.h"
#include "darksend.h"
#include "governance.h"
#include "instantx.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "support/allocators/secure.h"
#include <sstream>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> 
#include <boost/math/distributions/poisson.hpp>
#include <boost/thread.hpp>

using namespace std;

#if defined(NDEBUG)
# error "Biblepay Core cannot be compiled without assertions."
#endif

/**
 * Global state
 */

CCriticalSection cs_main;

BlockMap mapBlockIndex;
CChain chainActive;
CBlockIndex *pindexBestHeader = NULL;
int64_t nTimeBestReceived = 0;
int SIN_MODULUS = 0;
int PRAYER_MODULUS = 0;
int64_t nHPSTimerStart = 0;
int64_t nHashCounter = 0;
std::string sGlobalPoolURL = "";
std::string sGlobalPOLError = "";
std::string sGlobalBlockHash = "";
bool fMiningDiagnostics = false;
bool fDistributedComputingCycle = false;
bool fDistributedComputingEnabled = false;
bool fDistributedComputingCycleDownloading = false;
bool fInternalRequestedShutdown = false;


int nDistributedComputingCycles = 0;

extern std::string GetVersionAlert();
std::string GetElement(std::string sIn, std::string sDelimiter, int iPos);

int64_t nGlobalPOLWeight;
int64_t nGlobalInfluencePercentage;

int64_t nLastTradingActivity = 0;
int64_t nBibleMinerPulse;
int64_t SANCTUARY_COLLATERAL = 1550001;
int64_t TEMPLE_COLLATERAL = 10000000;
int64_t MAX_BLOCK_SUBSIDY = 20000;
std::string strTemplePubKey = "";
int64_t MAX_MESSAGE_LENGTH = 3000;
bool fProd = false;
bool fMineSlow = false;
double dHashesPerSec = 0;
std::map<std::string, double> mvBlockVersion;
extern bool WriteKey(std::string sKey, std::string sValue);
extern bool InstantiateOneClickMiningEntries();
extern std::string DefaultRecAddress(std::string sType);
extern CAmount GetRetirementAccountContributionAmount(int nPrevHeight);
extern std::string AmountToString(const CAmount& amount);
extern CAmount StringToAmount(std::string sValue);
bool CheckProofOfLoyalty(double dWeight, uint256 hash, unsigned int nBits, const Consensus::Params& params, 
	int64_t nBlockTime, int64_t nPrevBlockTime, int nPrevHeight, unsigned int nNonce, const CBlockIndex* pindexPrev, bool bLoadingBlockIndex);
double GetStakeWeight(CTransaction tx, int64_t nTipTime, std::string sXML, bool bVerifySignature, std::string& sMetrics, std::string& sError);
extern std::string SignMessage(std::string sMsg, std::string sPrivateKey);
extern void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive);
extern bool NonObnoxiousLog(std::string sLogSection, std::string sLogKey, std::string sValue, int64_t nAllowedSpan);
extern bool TimerMain(std::string timer_name, int max_ms);
std::string FindResearcherCPIDByAddress(std::string sSearch, std::string& out_address, double& nTotalMagnitude);
extern bool IsMature(int64_t nTime, int64_t nMaturityAge);
std::string GetMyPublicKeys();
extern bool HasThisCPIDSolvedPriorBlocks(std::string CPID, CBlockIndex* pindexPrev);
std::string VectorToString(std::vector<unsigned char> v);

CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
bool fLoadingIndex = false;
bool fMasternodesEnabled = false;
bool fTradingEnabled = false;
bool fRetirementAccountsEnabled = false;
bool fProofOfLoyaltyEnabled = false;

int64_t nLastDCContractSubmitted = 0;
std::string ExecuteDistributedComputingSanctuaryQuorumProcess();


int iPrayerIndex = 0;
std::string sOS = "";

std::map<std::string, std::string> mvApplicationCache;
std::map<std::string, int64_t> mvApplicationCacheTimestamp;
std::map<int64_t, std::string> mapDebug;

bool fPoolMiningMode = false;
bool fPoolMiningUseSSL = false;

bool fCommunicatingWithPool = false;
int iMinerThreadCount = 0;

void RecoverOrphanedChain(int iCondition);
extern void SetOverviewStatus();
extern const CBlockIndex* GetBlockIndexByTransactionHash(const uint256 &hash);
extern int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params);
extern std::string RetrieveTxOutInfo(const CBlockIndex* pindex, int iLookback, int iTxOffset, int ivOutOffset, int iDataType);
UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string sSearch, std::string& outEntry);
double GetDifficulty(const CBlockIndex* blockindex);
void MemorizePrayer(std::string sMessage, int64_t nTime, double dAmount, int iPos, std::string sTxID);
std::string PubKeyToAddress(const CScript& scriptPubKey);
std::string GetSin(int iSinNumber, std::string& out_Description);
std::string TimestampToHRDate(double dtm);
extern std::string GetArrayElement(std::string s, std::string delim, int iPos);
double GetDifficultyN(const CBlockIndex* blockindex, double N);
uint256 BibleHash(uint256 hash, int64_t nBlockTime, int64_t nPrevBlockTime, bool bMining, int nPrevHeight, const CBlockIndex* pindexLast, bool bRequireTxIndex, bool f7000, bool f8000, bool f9000, bool fTitheBlocksActive, unsigned int nNonce);
extern void PurgeCacheAsOfExpiration(std::string sSection, int64_t nExpiration);
double GetUserMagnitude(std::string sListOfPublicKeys, double& nBudget, double& nTotalPaid, int& out_iLastSuperblock, std::string& out_Superblocks, int& out_SuperblockCount, int& out_HitCount, double& out_OneDayPaid, double& out_OneWeekPaid, double& out_OneDayBudget, double& out_OneWeekBudget);
double GetPaymentByCPID(std::string CPID, int nHeight);
bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError);
bool VerifyCPIDSignature(std::string sFullSig, bool bRequireEndToEndVerification, std::string& sError);
double GetSporkDouble(std::string sName, double nDefault);

std::string GetGithubVersion();

std::string GetBoincResearcherHexCodeAndCPID(std::string sProjectId, int nUserId, std::string& sCPID);
std::string GetDCCElement(std::string sData, int iElement, bool fCheckSignature);

extern std::string ReadCache(std::string sSection, std::string sKey);
extern void WriteCache(std::string section, std::string key, std::string value, int64_t locktime);
extern void ClearCache(std::string sSection);
extern std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nMaxAge);



bool fImporting = false;
bool fReindex = false;
bool fTxIndex = true;
bool fAddressIndex = false;
bool fTimestampIndex = false;
bool fSpentIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
bool fRequireStandard = true;
unsigned int nBytesPerSigOp = DEFAULT_BYTES_PER_SIGOP;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
size_t nCoinCacheUsage = 5000 * 300;
std::string msGlobalStatus = "";
std::string msGlobalStatus2 = "";
std::string msGlobalStatus3 = "";
std::string msProposalResult = "";
int64_t nProposalStartTime = 0;
int64_t nProposalModulus = 0;
uint256 uTxIdFee = uint256();
int nProposalPrepareHeight = 0;
std::string msProposalHex = "";

bool fPrayersMemorized = false;
double mnMagnitude = 0;
double mnMagnitudeOneDay = 0;
int mnPODCTried = 0;
int mnPODCSent = 0;
double mnPODCAmountSent = 0;

std::string msGlobalCPID = "";
std::string msGUIResponse = "";
SecureString msEncryptedString = "";
std::string msGithubVersion = "";

bool fCheckedPODCUnlock = false;
bool fWalletLoaded = false;

CBlock cblockGenesis;
uint64_t nPruneTarget = 0;
bool fAlerts = DEFAULT_ALERTS;
bool fEnableReplacement = DEFAULT_ENABLE_REPLACEMENT;
extern void MemorizeBlockChainPrayers(bool fDuringConnectBlock, bool fInBackground, bool fColdBoot);

/** Fees smaller than this (in duffs) are considered zero fee (for relaying, mining and transaction creation) */
CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);

CTxMemPool mempool(::minRelayTxFee);

struct COrphanTx {
    CTransaction tx;
    NodeId fromPeer;
};
map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(cs_main);;
map<uint256, set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_main);;
map<uint256, int64_t> mapRejectedBlocks GUARDED_BY(cs_main);
map<std::string, int> mvTimers; // Contains event timers that reset after max ms duration iterator is exceeded
map<uint256, CTransaction> mapComplexTransactions GUARDED_BY(cs_main);;

void EraseOrphansFor(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/**
 * Returns true if there are nRequired or more blocks of minVersion or above
 * in the last Consensus::Params::nMajorityWindow blocks, starting at pstart and going backwards.
 */
static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams);
static void CheckBlockIndex(const Consensus::Params& consensusParams);

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const string strMessageMagic = "DarkCoin Signed Message:\n";

// Internal stuff
namespace {

    struct CBlockIndexWorkComparator
    {
        bool operator()(CBlockIndex *pa, CBlockIndex *pb) const {
            // First sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork) return false;
            if (pa->nChainWork < pb->nChainWork) return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId) return false;
            if (pa->nSequenceId > pb->nSequenceId) return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb) return false;
            if (pa > pb) return true;

            // Identical blocks.
            return false;
        }
    };

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** Number of nodes with fSyncStarted. */
    int nSyncStarted = 0;
    /** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile;
    std::vector<CBlockFileInfo> vinfoBlockFile;
    int nLastBlockFile = 0;
    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    CCriticalSection cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    uint32_t nBlockSequenceId = 1;

    /**
     * Sources of received blocks, saved to be able to send them reject
     * messages or ban them when processing happens afterwards. Protected by
     * cs_main.
     */
    map<uint256, NodeId> mapBlockSource;

    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.7MB
     */
    boost::scoped_ptr<CRollingBloomFilter> recentRejects;
    uint256 hashRecentRejectsChainTip;

    /** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
    struct QueuedBlock {
        uint256 hash;
        CBlockIndex* pindex;     //!< Optional.
        bool fValidatedHeaders;  //!< Whether this block has validated headers at the time of request.
    };
    map<uint256, pair<NodeId, list<QueuedBlock>::iterator> > mapBlocksInFlight;

    /** Number of preferable block download peers. */
    int nPreferredDownload = 0;

    /** Dirty block index entries. */
    set<CBlockIndex*> setDirtyBlockIndex;

    /** Dirty block file entries. */
    set<int> setDirtyFileInfo;

    /** Number of peers from which we're downloading blocks. */
    int nPeersWithValidatedDownloads = 0;
} // anon namespace

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace {

struct CBlockReject {
    unsigned char chRejectCode;
    string strRejectReason;
    uint256 hashBlock;
};

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    std::string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex *pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex *pindexLastCommonBlock;
    //! The best header we have sent our peer.
    CBlockIndex *pindexBestHeaderSent;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    list<QueuedBlock> vBlocksInFlight;
    //! When the first entry in vBlocksInFlight started downloading. Don't care when vBlocksInFlight is empty.
    int64_t nDownloadingSince;
    int nBlocksInFlight;
    int nBlocksInFlightValidHeaders;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;
    //! Whether this peer wants invs or headers (when possible) for block announcements.
    bool fPreferHeaders;

    CNodeState() {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        fShouldBan = false;
        pindexBestKnownBlock = NULL;
        hashLastUnknownBlock.SetNull();
        pindexLastCommonBlock = NULL;
        pindexBestHeaderSent = NULL;
        fSyncStarted = false;
        nStallingSince = 0;
        nDownloadingSince = 0;
        nBlocksInFlight = 0;
        nBlocksInFlightValidHeaders = 0;
        fPreferredDownload = false;
        fPreferHeaders = false;
    }
};

/** Map maintaining per-node state. Requires cs_main. */
map<NodeId, CNodeState> mapNodeState;

// Requires cs_main.
CNodeState *State(NodeId pnode) {
    map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return NULL;
    return &it->second;
}

int GetHeight()
{
    LOCK(cs_main);
    return chainActive.Height();
}

void UpdatePreferredDownload(CNode* node, CNodeState* state)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

    nPreferredDownload += state->fPreferredDownload;
}

void InitializeNode(NodeId nodeid, const CNode *pnode) {
    LOCK(cs_main);
    CNodeState &state = mapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;
    state.name = pnode->addrName;
    state.address = pnode->addr;
}

void FinalizeNode(NodeId nodeid) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);

    if (state->fSyncStarted)
        nSyncStarted--;

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
        AddressCurrentlyConnected(state->address);
    }

    BOOST_FOREACH(const QueuedBlock& entry, state->vBlocksInFlight) {
        mapBlocksInFlight.erase(entry.hash);
    }
    EraseOrphansFor(nodeid);
    nPreferredDownload -= state->fPreferredDownload;
    nPeersWithValidatedDownloads -= (state->nBlocksInFlightValidHeaders != 0);
    assert(nPeersWithValidatedDownloads >= 0);

    mapNodeState.erase(nodeid);

    if (mapNodeState.empty()) {
        // Do a consistency check after the last peer is removed.
        assert(mapBlocksInFlight.empty());
        assert(nPreferredDownload == 0);
        assert(nPeersWithValidatedDownloads == 0);
    }
}

// Requires cs_main.
// Returns a bool indicating whether we requested this block.
bool MarkBlockAsReceived(const uint256& hash) {
    map<uint256, pair<NodeId, list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end()) {
        CNodeState *state = State(itInFlight->second.first);
        state->nBlocksInFlightValidHeaders -= itInFlight->second.second->fValidatedHeaders;
        if (state->nBlocksInFlightValidHeaders == 0 && itInFlight->second.second->fValidatedHeaders) {
            // Last validated block on the queue was received.
            nPeersWithValidatedDownloads--;
        }
        if (state->vBlocksInFlight.begin() == itInFlight->second.second) {
            // First block on the queue was received, update the start download time for the next one
            state->nDownloadingSince = std::max(state->nDownloadingSince, GetTimeMicros());
        }
        state->vBlocksInFlight.erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        mapBlocksInFlight.erase(itInFlight);
        return true;
    }
    return false;
}

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const Consensus::Params& consensusParams, CBlockIndex *pindex = NULL) {
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    QueuedBlock newentry = {hash, pindex, pindex != NULL};
    list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
    state->nBlocksInFlight++;
    state->nBlocksInFlightValidHeaders += newentry.fValidatedHeaders;
    if (state->nBlocksInFlight == 1) {
        // We're starting a block download (batch) from this peer.
        state->nDownloadingSince = GetTimeMicros();
    }
    if (state->nBlocksInFlightValidHeaders == 1 && pindex != NULL) {
        nPeersWithValidatedDownloads++;
    }
    mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
}

/** Check whether the last unknown block a peer advertised is not yet known. */
void ProcessBlockAvailability(NodeId nodeid) {
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    if (!state->hashLastUnknownBlock.IsNull()) {
        BlockMap::iterator itOld = mapBlockIndex.find(state->hashLastUnknownBlock);
        if (itOld != mapBlockIndex.end() && itOld->second->nChainWork > 0) {
            if (state->pindexBestKnownBlock == NULL || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = itOld->second;
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash) {
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    ProcessBlockAvailability(nodeid);

    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end() && it->second->nChainWork > 0) {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == NULL || it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            state->pindexBestKnownBlock = it->second;
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

// Requires cs_main
bool CanDirectFetch(const Consensus::Params &consensusParams)
{
    return chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20;
}

// Requires cs_main
bool PeerHasHeader(CNodeState *state, CBlockIndex *pindex)
{
    if (state->pindexBestKnownBlock && pindex == state->pindexBestKnownBlock->GetAncestor(pindex->nHeight))
        return true;
    if (state->pindexBestHeaderSent && pindex == state->pindexBestHeaderSent->GetAncestor(pindex->nHeight))
        return true;
    return false;
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-NULL. */
CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

/** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
 *  at most count entries. */
void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<CBlockIndex*>& vBlocks, NodeId& nodeStaller) {
    if (count == 0)
        return;

    vBlocks.reserve(vBlocks.size() + count);
    CNodeState *state = State(nodeid);
    assert(state != NULL);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(nodeid);

    if (state->pindexBestKnownBlock == NULL || state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == NULL) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = chainActive[std::min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of its current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    std::vector<CBlockIndex*> vToFetch;
    CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    while (pindexWalk->nHeight < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight, std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the mean time, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
        // already part of our chain (and therefore don't need it even if pruned).
        BOOST_FOREACH(CBlockIndex* pindex, vToFetch) {
            if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA || chainActive.Contains(pindex)) {
                if (pindex->nChainTx)
                    state->pindexLastCommonBlock = pindex;
            } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd) {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != nodeid) {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

} // anon namespace

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    if (state == NULL)
        return false;
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
    BOOST_FOREACH(const QueuedBlock& queue, state->vBlocksInFlight) {
        if (queue.pindex)
            stats.vHeightInFlight.push_back(queue.pindex->nHeight);
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.connect(&GetHeight);
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.disconnect(&GetHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

std::string GetArrayElement(std::string s, std::string delim, int iPos)
{
	std::vector<std::string> vGE = Split(s.c_str(),delim);
	if (iPos > (int)vGE.size()) return "";
	return vGE[iPos];
}


CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    BOOST_FOREACH(const uint256& hash, locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

CCoinsViewCache *pcoinsTip = NULL;
CBlockTreeDB *pblocktree = NULL;

//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:
    unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz > 5000)
    {
        LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash.ToString());
        return false;
    }

    mapOrphanTransactions[hash].tx = tx;
    mapOrphanTransactions[hash].fromPeer = peer;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    LogPrint("mempool", "stored orphan tx %s (mapsz %u prevsz %u)\n", hash.ToString(),
             mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}

void static EraseOrphanTx(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;
    BOOST_FOREACH(const CTxIn& txin, it->second.tx.vin)
    {
        map<uint256, set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
}

void EraseOrphansFor(NodeId peer)
{
    int nErased = 0;
    map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end())
    {
        map<uint256, COrphanTx>::iterator maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer)
        {
            EraseOrphanTx(maybeErase->second.tx.GetHash());
            ++nErased;
        }
    }
    if (nErased > 0) LogPrint("mempool", "Erased %d orphan tx from peer %d\n", nErased, peer);
}


unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                             ? chainActive.Tip()->GetMedianTimePast()
                             : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation.
 */
static std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

static bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

bool TestLockPointValidity(const LockPoints* lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock) {
        // Check whether chainActive is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock)) {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

bool InstantiateOneClickMiningEntries()
{
	WriteKey("addnode","node.biblepay.org");
	WriteKey("addnode","node.biblepay-explorer.org");
	WriteKey("addnode","vultr4.biblepay.org");
	WriteKey("addnode","dnsseed.biblepay-explorer.org");
	int iCores = GetNumCores();
	WriteKey("genproclimit", RoundToString(iCores * 1,0));
	WriteKey("poolport","80");
	WriteKey("workerid","");
	WriteKey("pool","http://pool2.biblepay.org");
	WriteKey("gen","1");
	return true;
}


bool WriteKey(std::string sKey, std::string sValue)
{
    // Allows BiblePay to store the key value in the config file.
    boost::filesystem::path pathConfigFile(GetArg("-conf", "biblepay.conf"));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
    if (!boost::filesystem::exists(pathConfigFile))  
	{
		// Config is empty, create it:
		FILE *outFileNew = fopen(pathConfigFile.string().c_str(),"w");
		fputs("", outFileNew);
		fclose(outFileNew);
		LogPrintf("** Created brand new biblepay.conf file **\n");
	}
    boost::to_lower(sKey);
    std::string sLine = "";
    ifstream streamConfigFile;
    streamConfigFile.open(pathConfigFile.string().c_str());
    std::string sConfig = "";
    bool fWritten = false;
    if(streamConfigFile)
    {	
       while(getline(streamConfigFile, sLine))
       {
            std::vector<std::string> vEntry = Split(sLine,"=");
            if (vEntry.size() == 2)
            {
                std::string sSourceKey = vEntry[0];
                std::string sSourceValue = vEntry[1];
                boost::to_lower(sSourceKey);
                if (sSourceKey==sKey) 
                {
                    sSourceValue = sValue;
                    sLine = sSourceKey + "=" + sSourceValue;
                    fWritten=true;
                }
            }
            sLine = strReplace(sLine,"\r","");
            sLine = strReplace(sLine,"\n","");
            sLine += "\r\n";
            sConfig += sLine;
       }
    }
    if (!fWritten) 
    {
        sLine = sKey + "=" + sValue + "\r\n";
        sConfig += sLine;
    }
    
    streamConfigFile.close();
    FILE *outFile = fopen(pathConfigFile.string().c_str(),"w");
    fputs(sConfig.c_str(), outFile);
    fclose(outFile);
    ReadConfigFile(mapArgs, mapMultiArgs);
    return true;
}




bool CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints* lp, bool useExistingLockPoints)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(mempool.cs);

    CBlockIndex* tip = chainActive.Tip();
    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else {
        // pcoinsTip contains the UTXO set for chainActive.Tip()
        CCoinsViewMemPool viewMemPool(pcoinsTip, mempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn& txin = tx.vin[txinIndex];
            CCoins coins;
            if (!viewMemPool.GetCoins(txin.prevout.hash, coins)) {
                return error("%s: Missing input", __func__);
            }
            if (coins.nHeight == MEMPOOL_HEIGHT) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                prevheights[txinIndex] = coins.nHeight;
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            BOOST_FOREACH(int height, prevheights) {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight+1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}


unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const CTxOut &prevout = inputs.GetOutputFor(tx.vin[i]);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int GetInputAge(const CTxIn &txin)
{
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewMemPool viewMempool(pcoinsTip, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        const CCoins* coins = view.AccessCoins(txin.prevout.hash);

        if (coins) {
            if(coins->nHeight < 0) return 0;
            return chainActive.Height() - coins->nHeight + 1;
        } else {
            return -1;
        }
    }
}

int GetInputAgeIX(const uint256 &nTXHash, const CTxIn &txin)
{
    int nResult = GetInputAge(txin);
    if(nResult < 0) return -1;

    if (nResult < 6 && instantsend.IsLockedInstantSendTransaction(nTXHash))
        return nInstantSendDepth + nResult;

    return nResult;
}

int GetIXConfirmations(const uint256 &nTXHash)
{
    if (instantsend.IsLockedInstantSendTransaction(nTXHash))
        return nInstantSendDepth;

    return 0;
}

int GetUTXOHeight(const COutPoint& outpoint)
{
    LOCK(cs_main);
    CCoins coins;
    if(!pcoinsTip->GetCoins(outpoint.hash, coins) ||
       (unsigned int)outpoint.n>=coins.vout.size() ||
       coins.vout[outpoint.n].IsNull()) {
        return -1;
    }
    return coins.nHeight;
}


bool GetUTXOCoins(const COutPoint& outpoint, CCoins& coins)
{
    LOCK(cs_main);
    return !(!pcoinsTip->GetCoins(outpoint.hash, coins) ||
             (unsigned int)outpoint.n>=coins.vout.size() ||
             coins.vout[outpoint.n].IsNull());
}


bool CheckTransaction(const CTransaction& tx, CValidationState &state)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        vInOutPoints.insert(txin.prevout);
    }

    if (tx.IsCoinBase())
    {
		if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
		{
	        return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
		}
    }
    else
    {
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

bool VerifyDistributedBurnTransaction(std::string sXML)
{
	  // R Andrews - Biblepay - 2-2-2018 - Verify the Ownership of the CPID
	  // DC BurnTX Format: sCPID + ";" + sHexSetting + ";" + sPubAddress + ";" + String(nUserId);
		
	  std::string sMessageType      = ExtractXML(sXML,"<MT>","</MT>");
	  std::string sMessageKey       = ExtractXML(sXML,"<MK>","</MK>");
	  std::string sMessageValue     = ExtractXML(sXML,"<MV>","</MV>");
	  boost::to_upper(sMessageType);
	  boost::to_upper(sMessageKey);
	  if (!Contains(sMessageType,"DCC")) return true;
	  // Check Biblepay signature first:
	  std::string sCPID = GetDCCElement(sMessageValue, 0, false);
	  std::string sHexCode = GetDCCElement(sMessageValue, 1, false);
	  int nUserId = cdbl(GetDCCElement(sMessageValue, 3, false), 0);
	  // Now check the Distributed Computing Grid to ensure the Owner of this CPID actually transmitted the Biblepay uint256 Hex TXID into the DC Account they own:
	  std::string sDCCPID = "";
	  std::string sDCCode = GetBoincResearcherHexCodeAndCPID("project1", nUserId, sDCCPID);
	  
	  boost::to_upper(sDCCPID);
	  boost::to_upper(sCPID);
	  if (sCPID != sDCCPID) 
	  {
		  if (fDebugMaster) LogPrintf("VerifyDistributedBurnTransaction::ResearcherCPID %s does not match DC CPID %s. UserID %f \n", 
			  sCPID.c_str(), sDCCPID.c_str(), (double)nUserId);
		  return false;
	  }

	  if (sHexCode != sDCCode)
	  {
		  LogPrintf("VerifyDistributedBurnTransaction::ResearcherHexCode %s does not match DC HexCode %s.  UserID %f \n", 
			  sHexCode.c_str(), sDCCode.c_str(), (double)nUserId);
		  return false;
	  }
	  if (fDebugMaster) LogPrint("podc", " VerifyDistributedBurnTransaction::PASS for CPID %s \n",sCPID.c_str());
	  return true;
		
}

void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age) {
    int expired = pool.Expire(GetTime() - age);
    if (expired != 0)
        LogPrint("mempool", "Expired %i transactions from the memory pool\n", expired);

    std::vector<uint256> vNoSpendsRemaining;
    pool.TrimToSize(limit, &vNoSpendsRemaining);
    BOOST_FOREACH(const uint256& removed, vNoSpendsRemaining)
        pcoinsTip->Uncache(removed);
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
        state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", "+state.GetDebugMessage(),
        state.GetRejectCode());
}

bool AcceptToMemoryPoolWorker(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                              bool* pfMissingInputs, bool fOverrideMempoolLimit, bool fRejectAbsurdFee,
                              std::vector<uint256>& vHashTxnToUncache, bool fDryRun)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    if (!CheckTransaction(tx, state))
        return false;

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    string reason;
    if (fRequireStandard && !IsStandardTx(tx, reason))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);

    // Don't relay version 2 transactions until CSV is active, and we can be
    // sure that such transactions will be mined (unless we're on
    // -testnet/-regtest).
    const CChainParams& chainparams = Params();
    if (fRequireStandard && tx.nVersion >= 2 && VersionBitsTipState(chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV) != THRESHOLD_ACTIVE) {
        return state.DoS(0, false, REJECT_NONSTANDARD, "premature-version2-tx");
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
        return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool");

    // If this is a Transaction Lock Request check to see if it's valid
    if(instantsend.HasTxLockRequest(hash) && !CTxLockRequest(tx).IsValid())
        return state.DoS(10, error("AcceptToMemoryPool : CTxLockRequest %s is invalid", hash.ToString()),
                            REJECT_INVALID, "bad-txlockrequest");

    // Check for conflicts with a completed Transaction Lock
    BOOST_FOREACH(const CTxIn &txin, tx.vin)
    {
        uint256 hashLocked;
        if(instantsend.GetLockedOutPointTxHash(txin.prevout, hashLocked) && hash != hashLocked)
            return state.DoS(10, error("AcceptToMemoryPool : Transaction %s conflicts with completed Transaction Lock %s",
                                    hash.ToString(), hashLocked.ToString()),
                            REJECT_INVALID, "tx-txlock-conflict");
    }

    // Check for conflicts with in-memory transactions
    set<uint256> setConflicts;
    {
    LOCK(pool.cs); // protect pool.mapNextTx
    BOOST_FOREACH(const CTxIn &txin, tx.vin)
    {
        if (pool.mapNextTx.count(txin.prevout))
        {
            const CTransaction *ptxConflicting = pool.mapNextTx[txin.prevout].ptx;
            if (!setConflicts.count(ptxConflicting->GetHash()))
            {
                // InstantSend txes are not replacable
                if(instantsend.HasTxLockRequest(ptxConflicting->GetHash())) {
                    // this tx conflicts with a Transaction Lock Request candidate
                    return state.DoS(0, error("AcceptToMemoryPool : Transaction %s conflicts with Transaction Lock Request %s",
                                            hash.ToString(), ptxConflicting->GetHash().ToString()),
                                    REJECT_INVALID, "tx-txlockreq-mempool-conflict");
                } else if (instantsend.HasTxLockRequest(hash)) {
                    // this tx is a tx lock request and it conflicts with a normal tx
                    return state.DoS(0, error("AcceptToMemoryPool : Transaction Lock Request %s conflicts with transaction %s",
                                            hash.ToString(), ptxConflicting->GetHash().ToString()),
                                    REJECT_INVALID, "txlockreq-tx-mempool-conflict");
                }
                // Allow opt-out of transaction replacement by setting
                // nSequence >= maxint-1 on all inputs.
                //
                // maxint-1 is picked to still allow use of nLockTime by
                // non-replacable transactions. All inputs rather than just one
                // is for the sake of multi-party protocols, where we don't
                // want a single party to be able to disable replacement.
                //
                // The opt-out ignores descendants as anyone relying on
                // first-seen mempool behavior should be checking all
                // unconfirmed ancestors anyway; doing otherwise is hopelessly
                // insecure.
                bool fReplacementOptOut = true;
                if (fEnableReplacement)
                {
                    BOOST_FOREACH(const CTxIn &txin, ptxConflicting->vin)
                    {
                        if (txin.nSequence < std::numeric_limits<unsigned int>::max()-1)
                        {
                            fReplacementOptOut = false;
                            break;
                        }
                    }
                }
                if (fReplacementOptOut)
                    return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");

                setConflicts.insert(ptxConflicting->GetHash());
            }
        }
    }
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        LockPoints lp;
        {
        LOCK(pool.cs);
        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        view.SetBackend(viewMemPool);

        // do we already have it?
        bool fHadTxInCache = pcoinsTip->HaveCoinsInCache(hash);
        if (view.HaveCoins(hash)) {
            if (!fHadTxInCache)
                vHashTxnToUncache.push_back(hash);
            return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-known");
        }

        // do all inputs exist?
        // Note that this does not check for the presence of actual outputs (see the next check for that),
        // and only helps with filling in pfMissingInputs (to determine missing vs spent).
        BOOST_FOREACH(const CTxIn txin, tx.vin) {
            if (!pcoinsTip->HaveCoinsInCache(txin.prevout.hash))
                vHashTxnToUncache.push_back(txin.prevout.hash);
            if (!view.HaveCoins(txin.prevout.hash)) {
                if (pfMissingInputs)
                    *pfMissingInputs = true;
                return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
            }
        }

        // are the actual inputs available?
        if (!view.HaveInputs(tx))
            return state.Invalid(false, REJECT_DUPLICATE, "bad-txns-inputs-spent");

        // Bring the best block into scope
        view.GetBestBlock();

        nValueIn = view.GetValueIn(tx);

        // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
        view.SetBackend(dummy);

        // Only accept BIP68 sequence locked transactions that can be mined in the next
        // block; we don't want our mempool filled up with transactions that can't
        // be mined yet.
        // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
        // CoinsViewCache instead of create its own
        if (!CheckSequenceLocks(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp))
            return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        unsigned int nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, view);

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn-nValueOut;
        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        double nPriorityDummy = 0;
        pool.ApplyDeltas(hash, nPriorityDummy, nModifiedFees);

        CAmount inChainInputValue;
        double dPriority = view.GetPriority(tx, chainActive.Height(), inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        BOOST_FOREACH(const CTxIn &txin, tx.vin) {
            const CCoins *coins = view.AccessCoins(txin.prevout.hash);
            if (coins->IsCoinBase()) {
                fSpendsCoinbase = true;
                break;
            }
        }

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainActive.Height(), pool.HasNoInputsOf(tx), inChainInputValue, fSpendsCoinbase, nSigOps, lp);
        unsigned int nSize = entry.GetTxSize();

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        if ((nSigOps > MAX_STANDARD_TX_SIGOPS) || (nBytesPerSigOp && nSigOps > nSize / nBytesPerSigOp))
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false,
                strprintf("%d", nSigOps));

        CAmount mempoolRejectFee = pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
        if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee) {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false, strprintf("%d < %d", nFees, mempoolRejectFee));
        } else if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nModifiedFees < ::minRelayTxFee.GetFee(nSize) && !AllowFree(entry.GetPriority(chainActive.Height() + 1))) {
            // Require that free transactions have sufficient priority to be mined in the next block.
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
        }

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree && nModifiedFees < ::minRelayTxFee.GetFee(nSize))
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount >= GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) * 10 * 1000)
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "rate limited free transaction");
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
            dFreeCount += nSize;
        }

        if (fRejectAbsurdFee && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
            return state.Invalid(false,
                REJECT_HIGHFEE, "absurdly-high-fee",
                strprintf("%d > %d", nFees, ::minRelayTxFee.GetFee(nSize) * 10000));

        // Calculate in-mempool ancestors, up to a limit.
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        if (!pool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
        }

        // A transaction that spends outputs that would be replaced by it is invalid. Now
        // that we have the set of all ancestors we can detect this
        // pathological case by making sure setConflicts and setAncestors don't
        // intersect.
        BOOST_FOREACH(CTxMemPool::txiter ancestorIt, setAncestors)
        {
            const uint256 &hashAncestor = ancestorIt->GetTx().GetHash();
            if (setConflicts.count(hashAncestor))
            {
                return state.DoS(10, error("AcceptToMemoryPool: %s spends conflicting transaction %s",
                                           hash.ToString(),
                                           hashAncestor.ToString()),
                                 REJECT_INVALID, "bad-txns-spends-conflicting-tx");
            }
        }

        // Check if it's economically rational to mine this transaction rather
        // than the ones it replaces.
        CAmount nConflictingFees = 0;
        size_t nConflictingSize = 0;
        uint64_t nConflictingCount = 0;
        CTxMemPool::setEntries allConflicting;

        // If we don't hold the lock allConflicting might be incomplete; the
        // subsequent RemoveStaged() and addUnchecked() calls don't guarantee
        // mempool consistency for us.
        LOCK(pool.cs);
        if (setConflicts.size())
        {
            CFeeRate newFeeRate(nModifiedFees, nSize);
            set<uint256> setConflictsParents;
            const int maxDescendantsToVisit = 100;
            CTxMemPool::setEntries setIterConflicting;
            BOOST_FOREACH(const uint256 &hashConflicting, setConflicts)
            {
                CTxMemPool::txiter mi = pool.mapTx.find(hashConflicting);
                if (mi == pool.mapTx.end())
                    continue;

                // Save these to avoid repeated lookups
                setIterConflicting.insert(mi);

                // If this entry is "dirty", then we don't have descendant
                // state for this transaction, which means we probably have
                // lots of in-mempool descendants.
                // Don't allow replacements of dirty transactions, to ensure
                // that we don't spend too much time walking descendants.
                // This should be rare.
                if (mi->IsDirty()) {
                    return state.DoS(0,
                            error("AcceptToMemoryPool: rejecting replacement %s; cannot replace tx %s with untracked descendants",
                                hash.ToString(),
                                mi->GetTx().GetHash().ToString()),
                            REJECT_NONSTANDARD, "too many potential replacements");
                }

                // Don't allow the replacement to reduce the feerate of the
                // mempool.
                //
                // We usually don't want to accept replacements with lower
                // feerates than what they replaced as that would lower the
                // feerate of the next block. Requiring that the feerate always
                // be increased is also an easy-to-reason about way to prevent
                // DoS attacks via replacements.
                //
                // The mining code doesn't (currently) take children into
                // account (CPFP) so we only consider the feerates of
                // transactions being directly replaced, not their indirect
                // descendants. While that does mean high feerate children are
                // ignored when deciding whether or not to replace, we do
                // require the replacement to pay more overall fees too,
                // mitigating most cases.
                CFeeRate oldFeeRate(mi->GetModifiedFee(), mi->GetTxSize());
                if (newFeeRate <= oldFeeRate)
                {
                    return state.DoS(0,
                            error("AcceptToMemoryPool: rejecting replacement %s; new feerate %s <= old feerate %s",
                                  hash.ToString(),
                                  newFeeRate.ToString(),
                                  oldFeeRate.ToString()),
                            REJECT_INSUFFICIENTFEE, "insufficient fee");
                }

                BOOST_FOREACH(const CTxIn &txin, mi->GetTx().vin)
                {
                    setConflictsParents.insert(txin.prevout.hash);
                }

                nConflictingCount += mi->GetCountWithDescendants();
            }
            // This potentially overestimates the number of actual descendants
            // but we just want to be conservative to avoid doing too much
            // work.
            if (nConflictingCount <= maxDescendantsToVisit) {
                // If not too many to replace, then calculate the set of
                // transactions that would have to be evicted
                BOOST_FOREACH(CTxMemPool::txiter it, setIterConflicting) {
                    pool.CalculateDescendants(it, allConflicting);
                }
                BOOST_FOREACH(CTxMemPool::txiter it, allConflicting) {
                    nConflictingFees += it->GetModifiedFee();
                    nConflictingSize += it->GetTxSize();
                }
            } else {
                return state.DoS(0,
                        error("AcceptToMemoryPool: rejecting replacement %s; too many potential replacements (%d > %d)\n",
                            hash.ToString(),
                            nConflictingCount,
                            maxDescendantsToVisit),
                        REJECT_NONSTANDARD, "too many potential replacements");
            }

            for (unsigned int j = 0; j < tx.vin.size(); j++)
            {
                // We don't want to accept replacements that require low
                // feerate junk to be mined first. Ideally we'd keep track of
                // the ancestor feerates and make the decision based on that,
                // but for now requiring all new inputs to be confirmed works.
                if (!setConflictsParents.count(tx.vin[j].prevout.hash))
                {
                    // Rather than check the UTXO set - potentially expensive -
                    // it's cheaper to just check if the new input refers to a
                    // tx that's in the mempool.
                    if (pool.mapTx.find(tx.vin[j].prevout.hash) != pool.mapTx.end())
                        return state.DoS(0, error("AcceptToMemoryPool: replacement %s adds unconfirmed input, idx %d",
                                                  hash.ToString(), j),
                                         REJECT_NONSTANDARD, "replacement-adds-unconfirmed");
                }
            }

            // The replacement must pay greater fees than the transactions it
            // replaces - if we did the bandwidth used by those conflicting
            // transactions would not be paid for.
            if (nModifiedFees < nConflictingFees)
            {
                return state.DoS(0, error("AcceptToMemoryPool: rejecting replacement %s, less fees than conflicting txs; %s < %s",
                                          hash.ToString(), FormatMoney(nModifiedFees), FormatMoney(nConflictingFees)),
                                 REJECT_INSUFFICIENTFEE, "insufficient fee");
            }

            // Finally in addition to paying more fees than the conflicts the
            // new transaction must pay for its own bandwidth.
            CAmount nDeltaFees = nModifiedFees - nConflictingFees;
            if (nDeltaFees < ::minRelayTxFee.GetFee(nSize))
            {
                return state.DoS(0,
                        error("AcceptToMemoryPool: rejecting replacement %s, not enough additional fees to relay; %s < %s",
                              hash.ToString(),
                              FormatMoney(nDeltaFees),
                              FormatMoney(::minRelayTxFee.GetFee(nSize))),
                        REJECT_INSUFFICIENTFEE, "insufficient fee");
            }
        }

		// BiblePay - If this is a colored coin transaction, coins must be sent from coins of same color
		if (fRetirementAccountsEnabled)
		{
    		BOOST_FOREACH(const CTxOut txout, tx.vout) 
			{
				CComplexTransaction cct(tx);
				if (cct.Color=="401")
				{
					BOOST_FOREACH(const CTxIn &txin, tx.vin)
					{
						const CTxOut &prevout = view.GetOutputFor(txin);
                 		uint256 uPriorHash = txin.prevout.hash;
						CAmount nPriorValue = prevout.nValue;
						CComplexTransaction ccHistory(prevout);
						// bool bOK = (ccHistory.Color == cct.Color);
						LogPrintf(" ** Accept To Memory Pool: PriorHash %s, Prior Value %f, OutColor %s, Prior InColor %s ",
							uPriorHash.GetHex().c_str(),(double)nPriorValue,ccHistory.Color.c_str(), cct.Color.c_str());
						// ToDo: Reject tx in memory pool if not colored to colored
					}
				}
			}
		}

		// BiblePay - If this is a Boinc Distributed Computing Burn Transaction, user must prove ownership of the CPID, otherwise reject the transaction
		if (fDistributedComputingEnabled)
		{
	 		BOOST_FOREACH(const CTxOut txout, tx.vout) 
			{
				std::string sXML = txout.sTxOutMessage;
				if (!VerifyDistributedBurnTransaction(sXML))
				{
					LogPrintf("AcceptToMemoryPool::DC Burn Tx Rejected Burn %s \n", 
						sXML.c_str());
					return false;
				}
			}
		}
        // If we aren't going to actually accept it but just were verifying it, we are fine already
        if(fDryRun) return true;

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!CheckInputs(tx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS, true))
            return false;

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true))
        {
            return error("%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                __func__, hash.ToString(), FormatStateMessage(state));
        }

        // Remove conflicting transactions from the mempool
        BOOST_FOREACH(const CTxMemPool::txiter it, allConflicting)
        {
            LogPrint("mempool", "replacing tx %s with %s for %s BTC additional fees, %d delta bytes\n",
                    it->GetTx().GetHash().ToString(),
                    hash.ToString(),
                    FormatMoney(nModifiedFees - nConflictingFees),
                    (int)nSize - (int)nConflictingSize);
        }
        pool.RemoveStaged(allConflicting);

        // Store transaction in memory
        pool.addUnchecked(hash, entry, setAncestors, !IsInitialBlockDownload());

        // Add memory address index
        if (fAddressIndex) {
            pool.addAddressIndex(entry, view);
        }

        // Add memory spent index
        if (fSpentIndex) {
            pool.addSpentIndex(entry, view);
        }

        // trim mempool and check if tx was trimmed
        if (!fOverrideMempoolLimit) {
            LimitMempoolSize(pool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
            if (!pool.exists(hash))
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
        }
    }

    if(!fDryRun) SyncWithWallets(tx, NULL);

    return true;
}


bool ProcessComplexTransaction(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                        bool* pfMissingInputs, bool fOverrideMempoolLimit, bool fRejectAbsurdFee, bool fDryRun, std::vector<uint256>& vHashTxToUncache)

{
	CComplexTransaction cct(tx);
	
	//LogPrintf(" iscomplex %f %s",(double)cct.IsComplex,cct.Color.c_str());
	if (cct.IsComplex)
	{
		// OCO requires an atomic lock- we store the tx in the ComplexOrderType (COT) Memory Pool until it is paired; once paired we move it to the memory pool; however we
		// must return true so that functions like AddToWallet succeed, so we push it back into COT, then check to see if a COT has been Paired here, if so, we pull it out of COT
		// and push it into the memory pool:
		// When an unpaired COT exists, it is not mined by a miner.  When a Paired COT exists, the dual transaction is moved to the memory pool and mined by a miner.
		// 10-19-2017

		if (cct.TransactionType=="OCO")
		{
			// One cancels Other rule
			// Cache this and wait for the corresponding agreement
			// If its already in
			//LogPrintf("82\n");
			if (mapComplexTransactions.count(tx.GetHash()) == 0)
			{
				mapComplexTransactions.insert(make_pair(tx.GetHash(), tx));
				LogPrintf(" 801 - Size of mapcomplexTransactions %f \n",mapComplexTransactions.size());
			}
			// Scan complex order pool for matching complex transaction
			BOOST_FOREACH(PAIRTYPE(const uint256, CTransaction)& item, mapComplexTransactions)
			{
				LogPrintf("74\n");
				const CTransaction ctx = item.second;
				CComplexTransaction hist_complex_tx(ctx);
				LogPrintf("\r\nComplexTransaction hash %s, ctxHash %s, type %s, ExpectedAmount %f, Amount %f, hExpAmount %f, hAmount %f, expectedRecipient %s, Recipient %s, ExpectedColor %s, Color %s, HistComplTxRecAddr %s ",
					  tx.GetHash().GetHex().c_str(), ctx.GetHash().GetHex().c_str(),
					  cct.TransactionType, cct.ExpectedAmount,   cct.Amount, 
					  hist_complex_tx.ExpectedAmount, hist_complex_tx.Amount,
					  cct.RecipientAddress.c_str(), 
					  cct.ExpectedRecipient.c_str(), cct.ExpectedColor.c_str(), cct.Color.c_str(),hist_complex_tx.RecipientAddress.c_str());

				if (tx.GetHash() != ctx.GetHash()) // Dont look at self
				{
					LogPrintf(" 107\n");

					if (hist_complex_tx.TransactionType==cct.TransactionType) // If Complex transaction type matches another nodes transaction type
					{
						LogPrintf(" 108\n");

						if (cct.Amount == hist_complex_tx.ExpectedAmount && cct.ExpectedAmount == hist_complex_tx.Amount) // And they send the amount we expected and we sent the amount they expected
						{
     						LogPrintf(" 109.5 \n");

							if (cct.RecipientAddress == hist_complex_tx.ExpectedRecipient && cct.ExpectedRecipient == hist_complex_tx.RecipientAddress) // And their public key matches expected public key 
							{
							    LogPrintf(" 118 \n");

								LogPrintf(" recip addr %s Exp Recip %s , cctexprec %s color %s exp color %s",
									cct.RecipientAddress.c_str(), hist_complex_tx.ExpectedRecipient.c_str(),
									hist_complex_tx.RecipientAddress.c_str(), cct.Color.c_str(), hist_complex_tx.ExpectedColor.c_str());
											    LogPrintf(" 1199 \n");

   				
								if (cct.Color == hist_complex_tx.ExpectedColor && cct.ExpectedColor == hist_complex_tx.Color) // and they sent the color we expected
								{
									std::vector<uint256> vHashTxToUncache1;

									// Remove the paired transaction and place in the memory pool
									bool res1 = AcceptToMemoryPoolWorker(pool, state, tx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit, fRejectAbsurdFee, vHashTxToUncache1, fDryRun);
									mapComplexTransactions.erase(tx.GetHash());
									std::vector<uint256> vHashTxToUncache2;

									bool res2 = AcceptToMemoryPoolWorker(pool, state, ctx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit, fRejectAbsurdFee, vHashTxToUncache2, fDryRun);
									mapComplexTransactions.erase(ctx.GetHash());
									return res1 && res2;
								}
							}
						}
					}
				}
    
			}
   
			// If this is a cancelled order, remove from COT pool
			return true;
		}
		else if (cct.TransactionType=="CAN")
		{
			mapComplexTransactions.erase(tx.GetHash());
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}

}

bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                        bool* pfMissingInputs, bool fOverrideMempoolLimit, bool fRejectAbsurdFee, bool fDryRun)
{
    std::vector<uint256> vHashTxToUncache;

	//	bool IsComplexTransactionPreProcessed = ProcessComplexTransaction(pool, state, tx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit, fRejectAbsurdFee, fDryRun, vHashTxToUncache);
	//	if (IsComplexTransactionPreProcessed) return IsComplexTransactionPreProcessed;

    bool res = AcceptToMemoryPoolWorker(pool, state, tx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit, fRejectAbsurdFee, vHashTxToUncache, fDryRun);
    if (!res || fDryRun) 
	{
        if(!res) LogPrint("mempool", "%s: %s %s\n", __func__, tx.GetHash().ToString(), state.GetRejectReason());
        BOOST_FOREACH(const uint256& hashTx, vHashTxToUncache)
            pcoinsTip->Uncache(hashTx);
    }
    return res;
}

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector<uint256> &hashes)
{
    if (!fTimestampIndex)
        return error("Timestamp index not enabled");

    if (!pblocktree->ReadTimestampIndex(high, low, hashes))
        return error("Unable to get hashes for timestamps");

    return true;
}

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    if (!fSpentIndex)
        return false;

    if (mempool.getSpentIndex(key, value))
        return true;

    if (!pblocktree->ReadSpentIndex(key, value))
        return false;

    return true;
}

bool GetAddressIndex(uint160 addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex, int start, int end)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressUnspent(uint160 addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}


const CBlockIndex* GetBlockIndexByTransactionHash(const uint256 &hash)
{
 	 CBlockIndex *pindexOut = NULL;
     int nHeight = -1;
     {
           CCoinsViewCache &view = *pcoinsTip;
           const CCoins* coins = view.AccessCoins(hash);
           if (coins)
                nHeight = coins->nHeight;
     }
     if (nHeight > 0) pindexOut = chainActive[nHeight];
     return pindexOut;
}

/** Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash, CTransaction &txOut, const Consensus::Params& consensusParams, uint256 &hashBlock, bool fAllowSlow)
{
    CBlockIndex *pindexSlow = NULL;

    LOCK(cs_main);

    if (mempool.lookup(hash, txOut))
    {
        return true;
    }

    if (fTxIndex) {
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(hash, postx)) {
            CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try {
                file >> header;
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                file >> txOut;
            } catch (const std::exception& e) {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut.GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }

    if (fAllowSlow) { // use coin database to locate block that contains transaction, and scan it
        int nHeight = -1;
        {
            CCoinsViewCache &view = *pcoinsTip;
            const CCoins* coins = view.AccessCoins(hash);
            if (coins)
                nHeight = coins->nHeight;
        }
        if (nHeight > 0)
            pindexSlow = chainActive[nHeight];
    }

    if (pindexSlow) {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow, consensusParams, "GetTransaction")) 
		{
            BOOST_FOREACH(const CTransaction &tx, block.vtx) {
                if (tx.GetHash() == hash) {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}






//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk: OpenBlockFile failed");

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(block);
    fileout << FLATDATA(messageStart) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk: ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams, std::string Context)
{

	// September 14th, 2017 - Robert A. - BiblePay
	// Depending on the context of the call, ensure the block is read from the disk without a CheckProofOfWork Error

	boost::to_upper(Context);
	bool bCheckPOW = true;
	if  (  Context=="RETRIEVETXOUTINFO" 
		|| Context=="GETTRANSACTION" 
		|| Context=="CONNECTTIP"
		|| Context=="VERIFYDB" 
		|| Context=="LOADEXTERNALBLOCKFILE" 
	    ) 
	{
		bCheckPOW = true;
	}
	else if (Context=="PROCESSGETDATA" || Context=="DISCONNECTTIP" || Context=="GETSUBSIDY" || Context=="MEMORIZEBLOCKCHAINPRAYERS") 
	{
		bCheckPOW = false;
	}
	else
	{
		bCheckPOW = true;
	}

    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s with Context %s", pos.ToString(), Context.c_str());

    // Read block
    try {
        filein >> block;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s at %s, Context %s", __func__, e.what(), pos.ToString(), Context.c_str());
    }

    // Check the header
	CBlockIndex* pindexAncestor=mapBlockIndex[block.hashPrevBlock];
    int64_t nAncestorTime = (pindexAncestor==NULL) ? 0 : pindexAncestor->nTime;
	int nPrevHeight = (pindexAncestor==NULL) ? 0 : pindexAncestor->nHeight;
	if (bCheckPOW)
	{
		if (!CheckProofOfWork(block.GetHash(), block.nBits, consensusParams, block.GetBlockTime(), nAncestorTime, nPrevHeight, block.nNonce, pindexAncestor, true))
		{
			LogPrintf("ReadBlockFromDisk (Context %s): Errors in block header at %s", Context.c_str(), pos.ToString());
			return false;
		}
	}
    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams, std::string Context)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos(), consensusParams, Context))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
    return true;
}

double ConvertBitsToDouble(unsigned int nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff = (double)0x0000ffff / (double)(nBits & 0x00ffffff);

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

const CBlockIndex* GetLookbackIndex(int64_t iMinimumBackSpacing, int64_t iFirstModulus, const CBlockIndex* pindexPrev)
{
	
   if (pindexPrev == NULL || pindexPrev->nHeight == 0 || pindexPrev->nHeight < iMinimumBackSpacing || iMinimumBackSpacing < 1) 
   {
        return NULL;
   }
   const CBlockIndex *BlockReading = pindexPrev;
    
   for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) 
   {
		if (i > iMinimumBackSpacing && (BlockReading->nHeight % iFirstModulus==0)) { break; }
        if (BlockReading->pprev) 
		{
			BlockReading=BlockReading->pprev;
		}
		else 
		{
			return NULL;
		}
   }
   if (BlockReading) { return BlockReading; } else { return NULL; }
}

CAmount GetBlockSubsidy(const CBlockIndex* pindexPrev, int nPrevBits, int nPrevHeight, const Consensus::Params& consensusParams, bool fSuperblockPartOnly)
{
	double dDiff = 0;
    CAmount nSubsidyBase;
    dDiff = ConvertBitsToDouble(nPrevBits);
	if ((pindexPrev && !fProd && pindexPrev->nHeight >= 1) || (fProd && pindexPrev && pindexPrev->nHeight >= F7000_CUTOVER_HEIGHT && pindexPrev->nHeight < F7000_CUTOVER_HEIGHT_DIFF_END))
	{
		// This setting included in f7000 regulates the extent in which the block subsidy is lowered by increasing diff; once we remove the x11 component from the biblehash, it was necessary to recalculate the reduction to match the prior regulation level.
		dDiff = dDiff / 700;
		/*		BiblePay Difficulty Level Chart:
		 1            19998.2933653649 
		51            19863.6590388237 
		101           19730.3798134583 
		151           19598.4375645471 
		201           19467.8144693848		
		251           19338.4930012641 
		301           19210.4559235953 
		351           19083.6862841633 
		401           18958.1674095155 
		451           18833.8828994796 
		*/
	}
	else if (fProd && pindexPrev && pindexPrev->nHeight >= F7000_CUTOVER_HEIGHT_DIFF_END)
	{
			dDiff = dDiff / 14000;
	}
		
    nSubsidyBase = (20000 / (pow((dDiff+1.0),2.0))) + 1;
    if(nSubsidyBase > 20000) nSubsidyBase = 20000;
        else if(nSubsidyBase < 5000) nSubsidyBase = 5000;

    // LogPrintf("height %u diff %4.2f reward %d\n", nPrevHeight, dDiff, nSubsidyBase);
    CAmount nSubsidy = nSubsidyBase * COIN;
    // Yearly decline of production by ~19.5% per year, projected ~5.2 Billion coins max by year 2050+.

	// http://wiki.biblepay.org/Emission_Schedule

	bool fSuperblocksEnabled = (pindexPrev->nHeight >= consensusParams.nSuperblockStartBlock) && fMasternodesEnabled;
	int iSubsidyDecreaseInterval = BLOCKS_PER_DAY * 365; // Yearly Initially
	double iDeflationRate = .10; // 10% per year initially
	if (fSuperblocksEnabled)
	{
		iSubsidyDecreaseInterval = BLOCKS_PER_DAY * 30; // After sanctuaries go live, Monthly
		iDeflationRate = .015; // 1.5% per month, compounded monthly (19.5% per year with compounding)
	}
    for (int i = iSubsidyDecreaseInterval; i <= nPrevHeight; i += iSubsidyDecreaseInterval) 
	{
        nSubsidy -= (nSubsidy * iDeflationRate);
    }
		
    // When sanctuaries go live: The Tithe block is disabled, budgeting/superblocks are enabled, and the budget contains a max of : 
	// 10% to Charity budget, 5% for the IT budget, 2.5% PR, 2.5% P2P.  The 80% remaining is split between the miner and the sanctuary.

	// Distributed Computing - Give 10% to charity, 5% to IT, 5% to P2P+PR, 50% to Distributed-Computing, (leaving 27% for Sanctuary and 3% for POW mining)
	double dSuperblockMultiplier = fSuperblocksEnabled ? (!fProd ? .15 : .20) : .10;
	bool fDCLive = (fProd && fDistributedComputingEnabled && pindexPrev->nHeight > F11000_CUTOVER_HEIGHT_PROD) || (!fProd && fDistributedComputingEnabled);
	if (fDCLive) dSuperblockMultiplier = .70;
    CAmount nSuperblockPart = (nPrevHeight > consensusParams.nBudgetPaymentsStartBlock) ? nSubsidy * dSuperblockMultiplier : 0;
    return fSuperblockPartOnly ? nSuperblockPart : nSubsidy - nSuperblockPart;
}

CAmount GetMasternodePayment(int nHeight, CAmount blockValue, CAmount nSanctuaryCollateral)
{
	// Give Sanctuaries 35% of the block reward after Christmas 2017 (leaving 20% for budgets (see budget breakdown below), 35% to DC and 10% to POW miner):
	const Consensus::Params& consensusParams = Params().GetConsensus();
    bool fSuperblocksEnabled = (nHeight >= consensusParams.nSuperblockStartBlock) && fMasternodesEnabled;
	// http://forum.biblepay.org/index.php?topic=33.0
	// Should we carve out additional 1-10% from superblocks for PR and/or P2P Rewards?
	// Poll ended 12-4-2017: Yes, carve an addl 2.5% for PR and 2.5% for P2P rewards (5% more) leaving 80% for miner/sanctuaries.
	// Final Distribution: 10% Charity, 2.5% PR, 2.5% P2P, 5% for IT, 40% for miner, 40% for sanctuary
	CAmount ret = 0;
	int nSpork8Height = fProd ? SPORK8_HEIGHT : SPORK8_HEIGHT_TESTNET;
	if (fProd && nHeight > nSpork8Height)
	{
		ret = blockValue * .50; // Tithe blocks have ended, masternodes are live, budgets live, split masternode payment with miner.
    }
	else
	{
		ret = fSuperblocksEnabled ? blockValue * (!fProd ? .50 : .40) : blockValue * .10;
    }

	bool fDCLive = (fProd && fDistributedComputingEnabled && nHeight > F11000_CUTOVER_HEIGHT_PROD) || (!fProd && fDistributedComputingEnabled);

	if (fDCLive)
	{
		double dRevertedToDCMiners = .50;
		double dMasternodePortion = .40;
		double dTotalPercent = dRevertedToDCMiners + dMasternodePortion;
		ret = blockValue * dTotalPercent;
	}
    return ret;
}

bool IsInitialBlockDownload()
{
    static bool lockIBDState = false;
    if (lockIBDState)
        return false;
    if (fImporting || fReindex)
        return true;
    LOCK(cs_main);
    const CChainParams& chainParams = Params();
    if (fCheckpointsEnabled && chainActive.Height() < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()))
        return true;
    bool state = (chainActive.Height() < pindexBestHeader->nHeight - 24 * 6 ||
            std::max(chainActive.Tip()->GetBlockTime(), pindexBestHeader->GetBlockTime()) < GetTime() - chainParams.MaxTipAge());
    if (!state)
        lockIBDState = true;
    return state;
}

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
CBlockIndex *pindexBestForkTip = NULL, *pindexBestForkBase = NULL;

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before the last checkpoint)
    if (IsInitialBlockDownload())
        return;

    // If our best fork is no longer within 72 blocks (+/- 3 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = NULL;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6)))
    {
        if (!fLargeWorkForkFound && pindexBestForkBase)
        {
            if(pindexBestForkBase->phashBlock){
                std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                    pindexBestForkBase->phashBlock->ToString() + std::string("'");
                CAlert::Notify(warning, true);
            }
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            if(pindexBestForkBase->phashBlock){
                LogPrintf("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n", __func__,
                       pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                       pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
                fLargeWorkForkFound = true;
            }
        }
        else
        {
			// 4-24-2018 - Found chain with more work - attempt recovery
            if(pindexBestInvalid->nHeight > chainActive.Height() + 6)
                LogPrintf("%s: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n", __func__);
            else
                LogPrintf("%s: Warning: Found invalid chain which has higher work (at least ~6 blocks worth of work) than our best chain.\nChain state database corruption likely.\n", __func__);
            fLargeWorkInvalidChainFound = true;
			
			RecoverOrphanedChain(1);

        }
    }
    else
    {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 3 hours if no one mines it) of ours
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
            pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
            chainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch)
{
    if (howmuch == 0)
        return;

    CNodeState *state = State(pnode);
    if (state == NULL)
        return;

    state->nMisbehavior += howmuch;
    int banscore = GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD);
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore)
    {
        LogPrintf("%s: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
        state->fShouldBan = true;
    } else
        LogPrintf("%s: %s (%d -> %d)\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
      log(pindexNew->nChainWork.getdouble())/log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
      pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      tip->GetBlockHash().ToString(), chainActive.Height(), log(tip->nChainWork.getdouble())/log(2.0),
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions();
}

void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state) {
    int nDoS = 0;
    if (state.IsInvalid(nDoS)) {
        std::map<uint256, NodeId>::iterator it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end() && State(it->second)) {
            assert (state.GetRejectCode() < REJECT_INTERNAL); // Blocks are never rejected with internal reject codes
            CBlockReject reject = {(unsigned char)state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), pindex->GetBlockHash()};
            State(it->second)->rejects.push_back(reject);
            if (nDoS > 0)
                Misbehaving(it->second, nDoS);
        }
    }
    if (!state.CorruptionPossible()) 
	{
		if (true)
		{
			pindex->nStatus |= BLOCK_FAILED_VALID;  // PODC - Dirty: Allow PODC to reorganize chain, this happens if Rosetta goes down, dirty blocks can become clean.
			setDirtyBlockIndex.insert(pindex);
			setBlockIndexCandidates.erase(pindex);
		}
        InvalidChainFound(pindex);
    }
}

void UpdateCoins(const CTransaction& tx, CValidationState &state, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase()) {
        txundo.vprevout.reserve(tx.vin.size());
        BOOST_FOREACH(const CTxIn &txin, tx.vin) {
            CCoinsModifier coins = inputs.ModifyCoins(txin.prevout.hash);
            unsigned nPos = txin.prevout.n;

            if (nPos >= coins->vout.size() || coins->vout[nPos].IsNull())
                assert(false);
            // mark an outpoint spent, and construct undo information
            txundo.vprevout.push_back(CTxInUndo(coins->vout[nPos]));
            coins->Spend(nPos);
            if (coins->vout.size() == 0) {
                CTxInUndo& undo = txundo.vprevout.back();
                undo.nHeight = coins->nHeight;
                undo.fCoinBase = coins->fCoinBase;
                undo.nVersion = coins->nVersion;
            }
        }
        // add outputs
        inputs.ModifyNewCoins(tx.GetHash())->FromTx(tx, nHeight);
    }
    else {
        // add outputs for coinbase tx
        // In this case call the full ModifyCoins which will do a database
        // lookup to be sure the coins do not already exist otherwise we do not
        // know whether to mark them fresh or not.  We want the duplicate coinbases
        // before BIP30 to still be properly overwritten.
        inputs.ModifyCoins(tx.GetHash())->FromTx(tx, nHeight);
    }
}

void UpdateCoins(const CTransaction& tx, CValidationState &state, CCoinsViewCache &inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, state, inputs, txundo, nHeight);
}

bool CScriptCheck::operator()() {
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, cacheStore), &error)) {
        return false;
    }
    return true;
}

int GetSpendHeight(const CCoinsViewCache& inputs)
{
    LOCK(cs_main);
    CBlockIndex* pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
    return pindexPrev->nHeight + 1;
}

namespace Consensus {
bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight)
{
        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!inputs.HaveInputs(tx))
            return state.Invalid(false, 0, "", "Inputs unavailable");

        CAmount nValueIn = 0;
        CAmount nFees = 0;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            const COutPoint &prevout = tx.vin[i].prevout;
            const CCoins *coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            // If prev is coinbase, check that it's matured
            if (coins->IsCoinBase()) {
                if (nSpendHeight - coins->nHeight < COINBASE_MATURITY)
                    return state.Invalid(false,
                        REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                        strprintf("tried to spend coinbase at depth %d", nSpendHeight - coins->nHeight));
            }

            // Check for negative or overflow input values
            nValueIn += coins->vout[prevout.n].nValue;
            if (!MoneyRange(coins->vout[prevout.n].nValue) || !MoneyRange(nValueIn))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");

        }

        if (nValueIn < tx.GetValueOut())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())));

        // Tally transaction fees
        CAmount nTxFee = nValueIn - tx.GetValueOut();
        if (nTxFee < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
        nFees += nTxFee;
        if (!MoneyRange(nFees))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    return true;
}
}// namespace Consensus

bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheStore, std::vector<CScriptCheck> *pvChecks)
{
    if (!tx.IsCoinBase())
    {
        if (!Consensus::CheckTxInputs(tx, state, inputs, GetSpendHeight(inputs)))
            return false;

        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks
        // before the last block chain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (fScriptChecks) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                // Verify signature
                CScriptCheck check(*coins, tx, i, flags, cacheStore);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check2(*coins, tx, i,
                                flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100,false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
            }
        }
    }

    return true;
}

namespace {

bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: OpenUndoFile failed", __func__);

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(blockundo);
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("%s: ftell failed", __func__);
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: OpenBlockFile failed", __func__);

    // Read block
    uint256 hashChecksum;
    try {
        filein >> blockundo;
        filein >> hashChecksum;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    if (hashChecksum != hasher.GetHash())
        return error("%s: Checksum mismatch", __func__);

    return true;
}

/** Abort with a message */
bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

} // anon namespace

/**
 * Apply the undo operation of a CTxInUndo to the given chain state.
 * @param undo The undo object.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return True on success.
 */
static bool ApplyTxInUndo(const CTxInUndo& undo, CCoinsViewCache& view, const COutPoint& out)
{
    bool fClean = true;

    CCoinsModifier coins = view.ModifyCoins(out.hash);
    if (undo.nHeight != 0) {
        // undo data contains height: this is the last output of the prevout tx being spent
        if (!coins->IsPruned())
            fClean = fClean && error("%s: undo data overwriting existing transaction", __func__);
        coins->Clear();
        coins->fCoinBase = undo.fCoinBase;
        coins->nHeight = undo.nHeight;
        coins->nVersion = undo.nVersion;
    } else {
        if (coins->IsPruned())
            fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
    }
    if (coins->IsAvailable(out.n))
        fClean = fClean && error("%s: undo data overwriting existing output", __func__);
    if (coins->vout.size() < out.n+1)
        coins->vout.resize(out.n+1);
    coins->vout[out.n] = undo.txout;

    return fClean;
}

bool DisconnectBlock(const CBlock& block, CValidationState& state, const CBlockIndex* pindex, CCoinsViewCache& view, bool* pfClean)
{
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock(): no undo data available");
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock(): failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("DisconnectBlock(): block and undo data inconsistent");

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction &tx = block.vtx[i];
        uint256 hash = tx.GetHash();

        if (fAddressIndex) {

            for (unsigned int k = tx.vout.size(); k-- > 0;) {
                const CTxOut &out = tx.vout[k];

                if (out.scriptPubKey.IsPayToScriptHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                    // undo receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, hash, k, false), out.nValue));

                    // undo unspent index
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(2, uint160(hashBytes), hash, k), CAddressUnspentValue()));

                } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                    // undo receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, hash, k, false), out.nValue));

                    // undo unspent index
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, uint160(hashBytes), hash, k), CAddressUnspentValue()));

                } else {
                    continue;
                }

            }

        }

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        {
        CCoinsModifier outs = view.ModifyCoins(hash);
        outs->ClearUnspendable();

        CCoins outsBlock(tx, pindex->nHeight);
        // The CCoins serialization does not serialize negative numbers.
        // No network rules currently depend on the version here, so an inconsistency is harmless
        // but it must be corrected before txout nversion ever influences a network rule.
        if (outsBlock.nVersion < 0)
            outs->nVersion = outsBlock.nVersion;
        if (*outs != outsBlock)
            fClean = fClean && error("DisconnectBlock(): added transaction mismatch? database corrupted");

        // remove outputs
        outs->Clear();
        }

        // restore inputs
        if (i > 0) { // not coinbases
            const CTxUndo &txundo = blockUndo.vtxundo[i-1];
            if (txundo.vprevout.size() != tx.vin.size())
                return error("DisconnectBlock(): transaction and undo data inconsistent");
            for (unsigned int j = tx.vin.size(); j-- > 0;) {
                const COutPoint &out = tx.vin[j].prevout;
                const CTxInUndo &undo = txundo.vprevout[j];
                if (!ApplyTxInUndo(undo, view, out))
                    fClean = false;

                const CTxIn input = tx.vin[j];

                if (fSpentIndex) {
                    // undo and delete the spent index
                    spentIndex.push_back(make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue()));
                }

                if (fAddressIndex) {
                    const CTxOut &prevout = view.GetOutputFor(tx.vin[j]);
                    if (prevout.scriptPubKey.IsPayToScriptHash()) {
                        vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22);

                        // undo spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, hash, j, true), prevout.nValue * -1));

                        // restore unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(2, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));


                    } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                        vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23);

                        // undo spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, hash, j, true), prevout.nValue * -1));

                        // restore unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));

                    } else {
                        continue;
                    }
                }

            }
        }
    }


    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    if (pfClean) {
        *pfClean = fClean;
        return true;
    }

    if (fAddressIndex) {
        if (!pblocktree->EraseAddressIndex(addressIndex)) {
            return AbortNode(state, "Failed to delete address index");
        }
        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            return AbortNode(state, "Failed to write address unspent index");
        }
    }

    return fClean;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck() {
    RenameThread("biblepay-scriptch");
    scriptcheckqueue.Thread();
}

//
// Called periodically asynchronously; alerts if it smells like
// we're being fed a bad chain (blocks being generated much
// too slowly or too quickly).
//
void PartitionCheck(bool (*initialDownloadCheck)(), CCriticalSection& cs, const CBlockIndex *const &bestHeader,
                    int64_t nPowTargetSpacing)
{
    if (bestHeader == NULL || initialDownloadCheck()) return;

    static int64_t lastAlertTime = 0;
    int64_t now = GetAdjustedTime();
    if (lastAlertTime > now-60*60*24) return; // Alert at most once per day

    const int SPAN_HOURS=1; // was 4h in bitcoin but we have 4x faster blocks
    const int SPAN_SECONDS=SPAN_HOURS*60*60;
    int BLOCKS_EXPECTED = SPAN_SECONDS / nPowTargetSpacing;

    boost::math::poisson_distribution<double> poisson(BLOCKS_EXPECTED);

    std::string strWarning;
    int64_t startTime = GetAdjustedTime()-SPAN_SECONDS;

    LOCK(cs);
    const CBlockIndex* i = bestHeader;
    int nBlocks = 0;
    while (i->GetBlockTime() >= startTime) {
        ++nBlocks;
        i = i->pprev;
        if (i == NULL) return; // Ran out of chain, we must not be fully sync'ed
    }

    // How likely is it to find that many by chance?
    double p = boost::math::pdf(poisson, nBlocks);

    LogPrint("partitioncheck", "%s: Found %d blocks in the last %d hours\n", __func__, nBlocks, SPAN_HOURS);
    LogPrint("partitioncheck", "%s: likelihood: %g\n", __func__, p);

    // Aim for one false-positive about every fifty years of normal running:
    const int FIFTY_YEARS = 50*365*24*60*60;
    double alertThreshold = 1.0 / (FIFTY_YEARS / SPAN_SECONDS);

    if (p <= alertThreshold && nBlocks < BLOCKS_EXPECTED)
    {
        // Many fewer blocks than expected: alert!
        strWarning = strprintf(_("WARNING: check your network connection, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    else if (p <= alertThreshold && nBlocks > BLOCKS_EXPECTED)
    {
        // Many more blocks than expected: alert!
        strWarning = strprintf(_("WARNING: abnormally high number of blocks generated, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    if (!strWarning.empty())
    {
        strMiscWarning = strWarning;
        CAlert::Notify(strWarning, true);
        lastAlertTime = now;
    }
}

// Protected by cs_main
static VersionBitsCache versionbitscache;

int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    LOCK(cs_main);
    int32_t nVersion = VERSIONBITS_TOP_BITS;

    for (int i = 0; i < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++) 
	{
        ThresholdState state = VersionBitsState(pindexPrev, params, (Consensus::DeploymentPos)i, versionbitscache);
        if (state == THRESHOLD_LOCKED_IN || state == THRESHOLD_STARTED) {
            nVersion |= VersionBitsMask(params, (Consensus::DeploymentPos)i);
        }
    }

    return nVersion;
}

bool GetBlockHash(uint256& hashRet, int nBlockHeight)
{
    LOCK(cs_main);
    if(chainActive.Tip() == NULL) return false;
    if(nBlockHeight < -1 || nBlockHeight > chainActive.Height()) return false;
    if(nBlockHeight == -1) nBlockHeight = chainActive.Height();
    hashRet = chainActive[nBlockHeight]->GetBlockHash();
    return true;
}

/**
 * Threshold condition checker that triggers when unknown versionbits are seen on the network.
 */
class WarningBitsConditionChecker : public AbstractThresholdConditionChecker
{
private:
    int bit;

public:
    WarningBitsConditionChecker(int bitIn) : bit(bitIn) {}

    int64_t BeginTime(const Consensus::Params& params) const { return 0; }
    int64_t EndTime(const Consensus::Params& params) const { return std::numeric_limits<int64_t>::max(); }
    int Period(const Consensus::Params& params) const { return params.nMinerConfirmationWindow; }
    int Threshold(const Consensus::Params& params) const { return params.nRuleChangeActivationThreshold; }

    bool Condition(const CBlockIndex* pindex, const Consensus::Params& params) const
    {
        return ((pindex->nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) &&
               ((pindex->nVersion >> bit) & 1) != 0 &&
               ((ComputeBlockVersion(pindex->pprev, params) >> bit) & 1) == 0;
    }
};

// Protected by cs_main
static ThresholdConditionCache warningcache[VERSIONBITS_NUM_BITS];

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool fJustCheck)
{
    const CChainParams& chainparams = Params();
    AssertLockHeld(cs_main);

    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
	int64_t nAncestorTime = pindex->pprev ? pindex->pprev->nTime : 0 ;
	int nAncestorHeight = pindex->pprev ? pindex->pprev->nHeight : 0;
    if (!CheckBlock(block, state, !fJustCheck, !fJustCheck, pindex->nTime, nAncestorTime, nAncestorHeight, pindex->pprev))
        return false;

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == NULL ? uint256() : pindex->pprev->GetBlockHash();
	//Issue here - BiblePay 07-14-2017
	if (!(hashPrevBlock == view.GetBestBlock()))
	{
		LogPrintf(" ** ConnectBlock: PrevBlock != BestBlock; Rebooting Node.  Pray this never happens. ** \r\n");
		fReboot2 = true;
		return false;
	}

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == cblockGenesis.GetHash()) 
	{
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    bool fScriptChecks = true;
    if (fCheckpointsEnabled) {
        CBlockIndex *pindexLastCheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        if (pindexLastCheckpoint && pindexLastCheckpoint->GetAncestor(pindex->nHeight) == pindex) {
            // This block is an ancestor of a checkpoint: disable script checks
            fScriptChecks = false;
        }
    }

    int64_t nTime1 = GetTimeMicros(); nTimeCheck += nTime1 - nTimeStart;
    LogPrint("bench", "    - Sanity checks: %.2fms [%.2fs]\n", 0.001 * (nTime1 - nTimeStart), nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely.
    // This rule was originally applied to all blocks with a timestamp after March 15,2012, 0:00 UTC.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    bool fEnforceBIP30 = !pindex->phashBlock;  // Enforce on CreateNewBlock invocations which don't have a hash.
                
    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for the BIP30 check.
    CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus().BIP34Height);
    //Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond.
    fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height || !(pindexBIP34height->GetBlockHash() == chainparams.GetConsensus().BIP34Hash));

    if (fEnforceBIP30) {
        BOOST_FOREACH(const CTransaction& tx, block.vtx) {
            const CCoins* coins = view.AccessCoins(tx.GetHash());
            if (coins && !coins->IsPruned())
                return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
                                 REJECT_INVALID, "bad-txns-BIP30");
        }
    }

    // BIP16 didn't become active until Apr 1 2012
    int64_t nBIP16SwitchTime = 1333238400;
    bool fStrictPayToScriptHash = (pindex->GetBlockTime() >= nBIP16SwitchTime);

    unsigned int flags = fStrictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;

    // Start enforcing the DERSIG (BIP66) rules, for block.nVersion=3 blocks,
    // when 75% of the network has upgraded:
    if (block.nVersion >= 3 && IsSuperMajority(3, pindex->pprev, chainparams.GetConsensus().nMajorityEnforceBlockUpgrade, chainparams.GetConsensus())) {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY, (BIP65) for block.nVersion=4
    // blocks, when 75% of the network has upgraded:
    if (block.nVersion >= 4 && IsSuperMajority(4, pindex->pprev, chainparams.GetConsensus().nMajorityEnforceBlockUpgrade, chainparams.GetConsensus())) {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindex->pprev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV, versionbitscache) == THRESHOLD_ACTIVE) {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    int64_t nTime2 = GetTimeMicros(); nTimeForks += nTime2 - nTime1;
    LogPrint("bench", "    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : NULL);

    std::vector<int> prevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = block.vtx[i];
        const uint256 txhash = tx.GetHash();

        nInputs += tx.vin.size();
        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return state.DoS(100, error("ConnectBlock(): too many sigops"),
                             REJECT_INVALID, "bad-blk-sigops");

        if (!tx.IsCoinBase())
        {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("ConnectBlock(): inputs missing/spent"),
                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");

            // Check that transaction is BIP68 final
            // BIP68 lock checks (as opposed to nLockTime checks) must
            // be in ConnectBlock because they require the UTXO set
            prevheights.resize(tx.vin.size());
            for (size_t j = 0; j < tx.vin.size(); j++) {
                prevheights[j] = view.AccessCoins(tx.vin[j].prevout.hash)->nHeight;
            }

            if (!SequenceLocks(tx, nLockTimeFlags, &prevheights, *pindex)) {
                return state.DoS(100, error("%s: contains a non-BIP68-final transaction", __func__),
                                 REJECT_INVALID, "bad-txns-nonfinal");
            }

            if (fAddressIndex || fSpentIndex)
            {
                for (size_t j = 0; j < tx.vin.size(); j++) {

                    const CTxIn input = tx.vin[j];
                    const CTxOut &prevout = view.GetOutputFor(tx.vin[j]);
                    uint160 hashBytes;
                    int addressType;

                    if (prevout.scriptPubKey.IsPayToScriptHash()) {
                        hashBytes = uint160(vector <unsigned char>(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22));
                        addressType = 2;
                    } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                        hashBytes = uint160(vector <unsigned char>(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23));
                        addressType = 1;
                    } else {
                        hashBytes.SetNull();
                        addressType = 0;
                    }

                    if (fAddressIndex && addressType > 0) {
                        // record spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(addressType, hashBytes, pindex->nHeight, i, txhash, j, true), prevout.nValue * -1));

                        // remove address from unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(addressType, hashBytes, input.prevout.hash, input.prevout.n), CAddressUnspentValue()));
                    }

                    if (fSpentIndex) {
                        // add the spent index to determine the txid and input that spent an output
                        // and to find the amount and address from an input
                        spentIndex.push_back(make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue(txhash, j, pindex->nHeight, prevout.nValue, addressType, hashBytes)));
                    }
                }

            }

            if (fStrictPayToScriptHash)
            {
                // Add in sigops done by pay-to-script-hash inputs;
                // this is to prevent a "rogue miner" from creating
                // an incredibly-expensive-to-validate block.
                nSigOps += GetP2SHSigOpCount(tx, view);
                if (nSigOps > MAX_BLOCK_SIGOPS)
                    return state.DoS(100, error("ConnectBlock(): too many sigops"),
                                     REJECT_INVALID, "bad-blk-sigops");
            }

            nFees += view.GetValueIn(tx)-tx.GetValueOut();

            std::vector<CScriptCheck> vChecks;
            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, nScriptCheckThreads ? &vChecks : NULL))
                return error("ConnectBlock(): CheckInputs on %s failed with %s",
                    tx.GetHash().ToString(), FormatStateMessage(state));
            control.Add(vChecks);
        }

        if (fAddressIndex) {
            for (unsigned int k = 0; k < tx.vout.size(); k++) {
                const CTxOut &out = tx.vout[k];

                if (out.scriptPubKey.IsPayToScriptHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, txhash, k, false), out.nValue));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(2, uint160(hashBytes), txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));

                } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, txhash, k, false), out.nValue));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, uint160(hashBytes), txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));

                } else {
                    continue;
                }

            }
        }

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        UpdateCoins(tx, state, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        vPos.push_back(std::make_pair(tx.GetHash(), pos));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }
    int64_t nTime3 = GetTimeMicros(); nTimeConnect += nTime3 - nTime2;
    LogPrint("bench", "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(), 0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(), nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs-1), nTimeConnect * 0.000001);

    // biblepay : MODIFIED TO CHECK MASTERNODE PAYMENTS AND SUPERBLOCKS

    // It's possible that we simply don't have enough data and this could fail
    // (i.e. block itself could be a correct one and we need to store it),
    // that's why this is in ConnectBlock. Could be the other way around however -
    // the peer who sent us this block is missing some data and wasn't able
    // to recognize that block is actually invalid.
    CAmount blockReward = nFees + GetBlockSubsidy(pindex->pprev, pindex->pprev->nBits, pindex->pprev->nHeight, chainparams.GetConsensus());
    std::string strError = "";
    if (!IsBlockValueValid(block, pindex->nHeight, blockReward, strError)) 
	{
		LogPrintf("Height %f, Amount %f",(double)pindex->nHeight,(double)block.vtx[0].GetValueOut()/COIN);
        return state.DoS(0, error("ConnectBlock(biblepay): %s", strError), REJECT_INVALID, "bad-cb-amount");
    }
	
    if (!IsBlockPayeeValid(block.vtx[0], pindex->nHeight, blockReward, block.GetBlockTime())) 
	{
        mapRejectedBlocks.insert(make_pair(block.GetHash(), GetTime()));
        return state.DoS(0, error("ConnectBlock(biblepay): couldn't find masternode or superblock payments"),
                                REJECT_INVALID, "bad-cb-payee");
    }

	int nLastTitheBlock = fProd ? LAST_TITHE_BLOCK : LAST_TITHE_BLOCK_TESTNET;
	bool fTitheBlocksActive = (pindex->nHeight < nLastTitheBlock);
	if (fTitheBlocksActive)
	{
		if ((pindex->nHeight % TITHE_MODULUS)==0)
		{
			 std::string sRecipient = PubKeyToAddress(block.vtx[0].vout[0].scriptPubKey);
			 if (sRecipient != chainparams.GetConsensus().FoundationAddress)
			 {
 					 LogPrintf("not destined to foundation (block) foundation \r\n");
		 			 return state.DoS(50,error("ConnectBlock(Biblepay): ScriptPubKey on Tithe Block  not destined to foundation "), REJECT_INVALID, "bad-destination");
			 }
			 CAmount MasterNodeReward = GetBlockSubsidy(pindex->pprev, pindex->pprev->nBits, pindex->pprev->nHeight, chainparams.GetConsensus(), true);
			 CAmount TithePart = blockReward - MasterNodeReward;
     		 if (TithePart != block.vtx[0].vout[0].nValue && false)
			 {
					 LogPrintf("Tithe Block Reward %f, TithePart %f, nValue %f  ",(double)MasterNodeReward/COIN,(double)TithePart/COIN, 
						 (double)block.vtx[0].vout[0].nValue/COIN);
		  			 return state.DoS(10,error("ConnectBlock(Biblepay): Tithe does not match block reward."), REJECT_INVALID, "bad-tithe-amount");
			 }
		}
	}

    // END biblepay

    if (!control.Wait())
        return state.DoS(100, false);

    int64_t nTime4 = GetTimeMicros(); nTimeVerify += nTime4 - nTime2;
    LogPrint("bench", "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime4 - nTime2), nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs-1), nTimeVerify * 0.000001);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos pos;
            if (!FindUndoPos(state, pindex->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock(): FindUndoPos failed");
            if (!UndoWriteToDisk(blockundo, pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
                return AbortNode(state, "Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    if (fAddressIndex) {
        if (!pblocktree->WriteAddressIndex(addressIndex)) {
            return AbortNode(state, "Failed to write address index");
        }

        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            return AbortNode(state, "Failed to write address unspent index");
        }
    }

    if (fSpentIndex)
        if (!pblocktree->UpdateSpentIndex(spentIndex))
            return AbortNode(state, "Failed to write transaction index");

    if (fTimestampIndex)
        if (!pblocktree->WriteTimestampIndex(CTimestampIndexKey(pindex->nTime, pindex->GetBlockHash())))
            return AbortNode(state, "Failed to write timestamp index");

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetTimeMicros(); nTimeIndex += nTime5 - nTime4;
    LogPrint("bench", "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0].GetHash();

    int64_t nTime6 = GetTimeMicros(); nTimeCallbacks += nTime6 - nTime5;
    if (fDebugMaster) LogPrint("bench", "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);
	// Note: The following feature only memorizes the new blocks prayers
	if (!fLoadingIndex) 
	{
		LOCK(cs_main);
		{
			MemorizeBlockChainPrayers(true, false, false);
		}
	}
    return true;
}

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
bool static FlushStateToDisk(CValidationState &state, FlushStateMode mode) {
    const CChainParams& chainparams = Params();
    LOCK2(cs_main, cs_LastBlockFile);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    try {
    if (fPruneMode && fCheckForPruning && !fReindex) {
        FindFilesToPrune(setFilesToPrune, chainparams.PruneAfterHeight());
        fCheckForPruning = false;
        if (!setFilesToPrune.empty()) {
            fFlushForPrune = true;
            if (!fHavePruned) {
                pblocktree->WriteFlag("prunedblockfiles", true);
                fHavePruned = true;
            }
        }
    }
    int64_t nNow = GetTimeMicros();
    // Avoid writing/flushing immediately after startup.
    if (nLastWrite == 0) {
        nLastWrite = nNow;
    }
    if (nLastFlush == 0) {
        nLastFlush = nNow;
    }
    if (nLastSetChain == 0) {
        nLastSetChain = nNow;
    }
    size_t cacheSize = pcoinsTip->DynamicMemoryUsage();
    // The cache is large and close to the limit, but we have time now (not in the middle of a block processing).
    bool fCacheLarge = mode == FLUSH_STATE_PERIODIC && cacheSize * (10.0/9) > nCoinCacheUsage;
    // The cache is over the limit, we have to write now.
    bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && cacheSize > nCoinCacheUsage;
    // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
    bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
    // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
    bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
    // Combine all conditions that result in a full cache flush.
    bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush || fFlushForPrune;
    // Write blocks and block index to disk.
    if (fDoFullFlush || fPeriodicWrite) {
        // Depend on nMinDiskSpace to ensure we can write block index
        if (!CheckDiskSpace(0))
            return state.Error("out of disk space");
        // First make sure all block and undo data is flushed to disk.
        FlushBlockFile();
        // Then update all block file information (which may refer to block and undo files).
        {
            std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
            vFiles.reserve(setDirtyFileInfo.size());
            for (set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                vFiles.push_back(make_pair(*it, &vinfoBlockFile[*it]));
                setDirtyFileInfo.erase(it++);
            }
            std::vector<const CBlockIndex*> vBlocks;
            vBlocks.reserve(setDirtyBlockIndex.size());
            for (set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                vBlocks.push_back(*it);
                setDirtyBlockIndex.erase(it++);
            }
            if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) {
                return AbortNode(state, "Files to write to block index database");
            }
        }
        // Finally remove any pruned files
        if (fFlushForPrune)
            UnlinkPrunedFiles(setFilesToPrune);
        nLastWrite = nNow;
    }
    // Flush best chain related state. This can only be done if the blocks / block index write was also done.
    if (fDoFullFlush) {
        // Typical CCoins structures on disk are around 128 bytes in size.
        // Pushing a new one to the database can cause it to be written
        // twice (once in the log, and once in the tables). This is already
        // an overestimation, as most will delete an existing entry or
        // overwrite one. Still, use a conservative safety factor of 2.
        if (!CheckDiskSpace(128 * 2 * 2 * pcoinsTip->GetCacheSize()))
            return state.Error("out of disk space");
        // Flush the chainstate (which may refer to block index entries).
        if (!pcoinsTip->Flush())
            return AbortNode(state, "Failed to write to coin database");
        nLastFlush = nNow;
    }
    if (fDoFullFlush || ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000)) {
        // Update best block in wallet (so we can detect restored wallets).
        GetMainSignals().SetBestChain(chainActive.GetLocator());
        nLastSetChain = nNow;
    }
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk() {
    CValidationState state;
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush() {
    CValidationState state;
    fCheckForPruning = true;
    FlushStateToDisk(state, FLUSH_STATE_NONE);
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex *pindexNew) {
    const CChainParams& chainParams = Params();
    chainActive.SetTip(pindexNew);

    // New best block
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);

    LogPrintf("%s: new best=%s  height=%d  log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%utx)\n", __func__,
      chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), log(chainActive.Tip()->nChainWork.getdouble())/log(2.0), (unsigned long)chainActive.Tip()->nChainTx,
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
      Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip()), pcoinsTip->DynamicMemoryUsage() * (1.0 / (1<<20)), pcoinsTip->GetCacheSize());

    cvBlockChange.notify_all();

    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!IsInitialBlockDownload())
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chainActive.Tip();
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; bit++) {
            WarningBitsConditionChecker checker(bit);
            ThresholdState state = checker.GetStateFor(pindex, chainParams.GetConsensus(), warningcache[bit]);
            if (state == THRESHOLD_ACTIVE || state == THRESHOLD_LOCKED_IN) {
                if (state == THRESHOLD_ACTIVE) {
                    strMiscWarning = strprintf(_("Warning: unknown new rules activated (versionbit %i)"), bit);
                    if (!fWarned) {
                        CAlert::Notify(strMiscWarning, true);
                        fWarned = true;
                    }
                } else {
                    LogPrintf("%s: unknown new rules are about to activate (versionbit %i)\n", __func__, bit);
                }
            }
        }
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            int32_t nExpectedVersion = ComputeBlockVersion(pindex->pprev, chainParams.GetConsensus());
            if (pindex->nVersion > VERSIONBITS_LAST_OLD_BLOCK_VERSION && (pindex->nVersion & ~nExpectedVersion) != 0)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("%s: %d of last 100 blocks have unexpected version\n", __func__, nUpgraded);
        if (nUpgraded > 100/2)
        {
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: Unknown block versions being mined! It's possible unknown rules are in effect");
            if (!fWarned) {
                CAlert::Notify(strMiscWarning, true);
                fWarned = true;
            }
        }
    }
}

/** Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and manually re-limit mempool size after this, with cs_main held. */
bool static DisconnectTip(CValidationState& state, const Consensus::Params& consensusParams)
{
    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete, consensusParams, "DisconnectTip"))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (!DisconnectBlock(block, state, pindexDelete, view))
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        assert(view.Flush());
    }
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;
    // Resurrect mempool transactions from the disconnected block.
    std::vector<uint256> vHashUpdate;
    BOOST_FOREACH(const CTransaction &tx, block.vtx) {
        // ignore validation errors in resurrected transactions
        list<CTransaction> removed;
        CValidationState stateDummy;
        if (tx.IsCoinBase() || !AcceptToMemoryPool(mempool, stateDummy, tx, false, NULL, true)) {
            mempool.remove(tx, removed, true);
        } else if (mempool.exists(tx.GetHash())) {
            vHashUpdate.push_back(tx.GetHash());
        }
    }
    // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
    // no in-mempool children, which is generally not true when adding
    // previously-confirmed transactions back to the mempool.
    // UpdateTransactionsFromBlock finds descendants of any transactions in this
    // block that were added back and cleans up the mempool state.
    mempool.UpdateTransactionsFromBlock(vHashUpdate);
    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    BOOST_FOREACH(const CTransaction &tx, block.vtx) {
        SyncWithWallets(tx, NULL);
    }
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool static ConnectTip(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexNew, const CBlock* pblock)
{
	if (!(pindexNew->pprev == chainActive.Tip()))
	{
		LogPrintf(" ConnectTip(): Rebooting wallet - pindexNew->pprev != chainActive.Tip() (assert(pindexNew->pprev == chainActive.Tip())); ");
		fReboot2 = true;
		return false;
	}

    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock) {
        if (!ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus(), "ConnectTip"))
            return AbortNode(state, "Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros(); nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(pcoinsTip);
        bool rv = ConnectBlock(*pblock, state, pindexNew, view);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        mapBlockSource.erase(pindexNew->GetBlockHash());
        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);
    // Remove conflicting transactions from the mempool.
    list<CTransaction> txConflicted;
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted, !IsInitialBlockDownload());
    // Update chainActive & related variables.
    UpdateTip(pindexNew);
    // Tell wallet about transactions that went from mempool
    // to conflicted:
    BOOST_FOREACH(const CTransaction &tx, txConflicted) {
        SyncWithWallets(tx, NULL);
    }
    // ... and about transactions that got confirmed:
    BOOST_FOREACH(const CTransaction &tx, pblock->vtx) {
        SyncWithWallets(tx, pblock);
    }

    int64_t nTime6 = GetTimeMicros(); nTimePostConnect += nTime6 - nTime5; nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

bool DisconnectBlocks(int blocks)
{
    LOCK(cs_main);

    CValidationState state;
    const CChainParams& chainparams = Params();

    LogPrintf("DisconnectBlocks -- Got command to replay %d blocks\n", blocks);
    for(int i = 0; i < blocks; i++) {
        if(!DisconnectTip(state, chainparams.GetConsensus()) || !state.IsValid()) {
            return false;
        }
    }

    return true;
}

void ReprocessBlocks(int nBlocks)
{
    LOCK(cs_main);

    std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
    while(it != mapRejectedBlocks.end()){
        //use a window twice as large as is usual for the nBlocks we want to reset
        if((*it).second  > GetTime() - (nBlocks*60*5)) {
            BlockMap::iterator mi = mapBlockIndex.find((*it).first);
            if (mi != mapBlockIndex.end() && (*mi).second) {

                CBlockIndex* pindex = (*mi).second;
                if (fDebugMaster) LogPrint("net","ReprocessBlocks -- %s\n", (*it).first.ToString());

                CValidationState state;
                ReconsiderBlock(state, pindex);
            }
        }
        ++it;
    }

    DisconnectBlocks(nBlocks);

    CValidationState state;
    ActivateBestChain(state, Params());
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain() {
    do {
        CBlockIndex *pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex *pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexMostWork, const CBlock* pblock)
{
    AssertLockHeld(cs_main);
    bool fInvalidFound = false;
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;
    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(state, chainparams.GetConsensus()))
            return false;
        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        BOOST_REVERSE_FOREACH(CBlockIndex *pindexConnect, vpindexToConnect) {
            if (!ConnectTip(state, chainparams, pindexConnect, pindexConnect == pindexMostWork ? pblock : NULL)) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    if (fBlocksDisconnected) {
        mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
        LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
    }
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState &state, const CChainParams& chainparams, const CBlock *pblock) {
    CBlockIndex *pindexMostWork = NULL;
    do {
        boost::this_thread::interruption_point();
        if (ShutdownRequested())
            break;

        CBlockIndex *pindexNewTip = NULL;
        const CBlockIndex *pindexFork;
        bool fInitialDownload;
        {
            LOCK(cs_main);
            CBlockIndex *pindexOldTip = chainActive.Tip();
            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chainActive.Tip())
                return true;

            if (!ActivateBestChainStep(state, chainparams, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL))
                return false;

            pindexNewTip = chainActive.Tip();
            pindexFork = chainActive.FindFork(pindexOldTip);
            fInitialDownload = IsInitialBlockDownload();
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main
        // Always notify the UI if a new block tip was connected
        if (pindexFork != pindexNewTip) {
            uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);

            if (!fInitialDownload) {
                // Find the hashes of all blocks that weren't previously in the best chain.
                std::vector<uint256> vHashes;
                CBlockIndex *pindexToAnnounce = pindexNewTip;
                while (pindexToAnnounce != pindexFork) {
                    vHashes.push_back(pindexToAnnounce->GetBlockHash());
                    pindexToAnnounce = pindexToAnnounce->pprev;
                    if (vHashes.size() == MAX_BLOCKS_TO_ANNOUNCE) {
                        // Limit announcements in case of a huge reorganization.
                        // Rely on the peer's synchronization mechanism in that case.
                        break;
                    }
                }
                // Relay inventory, but don't relay old inventory during initial block download.
                int nBlockEstimate = 0;
                if (fCheckpointsEnabled)
                    nBlockEstimate = Checkpoints::GetTotalBlocksEstimate(chainparams.Checkpoints());
                {
                    LOCK(cs_vNodes);
                    BOOST_FOREACH(CNode* pnode, vNodes) {
                        if (chainActive.Height() > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate)) {
                            BOOST_REVERSE_FOREACH(const uint256& hash, vHashes) {
                                pnode->PushBlockHash(hash);
                            }
                        }
                    }
                }
                // Notify external listeners about the new tip.
                if (!vHashes.empty()) {
                    GetMainSignals().UpdatedBlockTip(pindexNewTip);
                }
            }
        }
    } while(pindexMostWork != chainActive.Tip());
    CheckBlockIndex(chainparams.GetConsensus());

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}

bool InvalidateBlock(CValidationState& state, const Consensus::Params& consensusParams, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
	if (true)
	{
		pindex->nStatus |= BLOCK_FAILED_VALID; // PODC - Dirty Blocks can become clean later
		setDirtyBlockIndex.insert(pindex);
		setBlockIndexCandidates.erase(pindex);
	}
	
    while (chainActive.Contains(pindex)) {
        CBlockIndex *pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;  // PODC - Dirty Blocks can become clean later
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, consensusParams)) {
            mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
            return false;
        }
    }

    LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip())) {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }

    InvalidChainFound(pindex);
    mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    return true;
}

bool ReconsiderBlock(CValidationState& state, CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

CBlockIndex* AddToBlockIndex(const CBlockHeader& block)
{
	
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);

    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object

    CBlockIndex* pindexNew = new CBlockIndex(block);

    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);

    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
		// BiblePay: Rebuilding index results in a pindexNew->pprev == NULL under some circumstances (apparently, only during expirimentation)
		if (pindexNew->pprev)
		{
			pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
		}
        pindexNew->BuildSkip();
    }

    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);

    if (pindexBestHeader == NULL || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;


    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block, CValidationState& state, CBlockIndex *pindexNew, const CDiskBlockPos& pos)
{
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile[nLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = OpenBlockFile(pos);
                if (file) 
				{
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}



bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW, int64_t nBlockTime, int64_t nPrevBlockTime, int nPrevHeight, const CBlockIndex* pindexPrev)
{
	
    // Check proof of work matches claimed amount
	// PhaseShiftUK reports that pool server occasionally returns a high-hash, yet we dont want to ban the pool server, Temporary solution: Change DoS level to prevent pool from being banned

	if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus(), nBlockTime, nPrevBlockTime, nPrevHeight, block.nNonce, pindexPrev, false))
        return state.DoS(5, error("CheckBlockHeader(): proof of work failed"),
		REJECT_INVALID, "high-hash");

    // BiblePay - Check timestamp (reject if > 15 minutes in future).  This is is important since we lower the difficulty after all online nodes cannot solve block in one hour!  
    if (block.GetBlockTime() > GetAdjustedTime() + (15 * 60))
        return state.Invalid(error("CheckBlockHeader(): BiblePay: Block timestamp way too far in the future"),
                             REJECT_INVALID, "time-way-too-new");

    if (nPrevHeight > 24999 && block.GetBlockTime() > GetAdjustedTime() + (5 * 60))
        return state.Invalid(error("CheckBlockHeader(): BiblePay: Block timestamp too far in the future"),
                             REJECT_INVALID, "time-too-new");
    return true;
}

bool CheckBlock(const CBlock& block, CValidationState& state, bool fCheckPOW, bool fCheckMerkleRoot, int64_t nBlockTime, int64_t nPrevBlockTime,
	int nPrevHeight, CBlockIndex* pindexPrev)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, fCheckPOW, nBlockTime, nPrevBlockTime, nPrevHeight, pindexPrev))
        return false;
	
	/* (Checked above)
	if (!CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus(), nBlockTime, nPrevBlockTime, nPrevHeight, pindexPrev, false))
	{
		LogPrintf("CheckBlock::CheckProofOfWork failed PrevHeight %f, pindexPrev %s ",(double)nPrevHeight,pindexPrev->GetBlockHash()->GetHex().c_str());
		return false;
	}
	*/

    // Check the merkle root.
    if (fCheckMerkleRoot) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, error("CheckBlock(): hashMerkleRoot mismatch"),
                             REJECT_INVALID, "bad-txnmrklroot", true);

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, error("CheckBlock(): duplicate transaction"),
                             REJECT_INVALID, "bad-txns-duplicate", true);
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() > MAX_BLOCK_SIZE || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return state.DoS(100, error("CheckBlock(): size limits failed"),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("CheckBlock(): first tx is not coinbase"),
                         REJECT_INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, error("CheckBlock(): more than one coinbase"),
                             REJECT_INVALID, "bad-cb-multiple");


    // biblepay : CHECK TRANSACTIONS FOR INSTANTSEND

    if(sporkManager.IsSporkActive(SPORK_3_INSTANTSEND_BLOCK_FILTERING)) {
        // We should never accept block which conflicts with completed transaction lock,
        // that's why this is in CheckBlock unlike coinbase payee/amount.
        // Require other nodes to comply, send them some data in case they are missing it.
        BOOST_FOREACH(const CTransaction& tx, block.vtx) {
            // skip coinbase, it has no inputs
            if (tx.IsCoinBase()) continue;
            // LOOK FOR TRANSACTION LOCK IN OUR MAP OF OUTPOINTS
            BOOST_FOREACH(const CTxIn& txin, tx.vin) {
                uint256 hashLocked;
                if(instantsend.GetLockedOutPointTxHash(txin.prevout, hashLocked) && hashLocked != tx.GetHash()) {
                    // The node which relayed this will have to swtich later,
                    // relaying instantsend data won't help it.
                    LOCK(cs_main);
                    mapRejectedBlocks.insert(make_pair(block.GetHash(), GetTime()));
                    return state.DoS(0, error("CheckBlock(biblepay): transaction %s conflicts with transaction lock %s",
                                                tx.GetHash().ToString(), hashLocked.ToString()),
                                     REJECT_INVALID, "conflict-tx-lock");
                }
            }
        }
    } else {
        LogPrintf("CheckBlock(biblepay): spork is off, skipping transaction locking checks\n");
    }

    // END biblepay

    // Check transactions
    BOOST_FOREACH(const CTransaction& tx, block.vtx)
        if (!CheckTransaction(tx, state))
            return error("CheckBlock(): CheckTransaction of %s failed with %s",
                tx.GetHash().ToString(),
                FormatStateMessage(state));

    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTransaction& tx, block.vtx)
    {
        nSigOps += GetLegacySigOpCount(tx);
    }
    if (nSigOps > MAX_BLOCK_SIGOPS)
        return state.DoS(100, error("CheckBlock(): out-of-bounds SigOpCount"),
                         REJECT_INVALID, "bad-blk-sigops");

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

static bool CheckIndexAgainstCheckpoint(const CBlockIndex* pindexPrev, CValidationState& state, const CChainParams& chainparams, const uint256& hash)
{
    if (*pindexPrev->phashBlock == cblockGenesis.GetHash())
        return true;

    int nHeight = pindexPrev->nHeight+1;
    // Don't accept any forks from the main chain prior to last checkpoint
    CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
    if (pcheckpoint && nHeight < pcheckpoint->nHeight)
        return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight));

    return true;
}

bool HasThisCPIDSolvedPriorBlocks(std::string CPID, CBlockIndex* pindexPrev)
{
	if (CPID.empty()) return false;
	int iCheckWindow = fProd ? 4 : 1;
	CBlockIndex* pindex = pindexPrev;
	int64_t headerAge = GetAdjustedTime() - pindexPrev->nTime;
	if (headerAge > (60*60*4)) return false;
	const CChainParams& chainparams = Params();
  
	for (int i = 0; i < iCheckWindow; i++)
	{
		if (pindex != NULL)
		{
			CBlock block;
        	if (ReadBlockFromDisk(block, pindex, chainparams.GetConsensus(), "HasThisCPIDSolvedPriorBlocks"))
			{
				std::string sCPIDSig = ExtractXML(block.vtx[0].vout[0].sTxOutMessage, "<cpidsig>","</cpidsig>");
				std::string lastcpid = GetElement(sCPIDSig, ";", 0);
				if (!lastcpid.empty() && !sCPIDSig.empty())
				{
					// LogPrintf(" Current CPID %s, LastCPID %s, Height %f --- ", CPID.c_str(), lastcpid.c_str(), (double)pindex->nHeight);
					if (lastcpid == CPID) return true;
					pindex = pindexPrev->pprev;
				}
			}
		}
	}
	return false;
}

bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex * const pindexPrev)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    int nHeight = pindexPrev->nHeight + 1;
    // Check proof of work
    if (block.nBits != GetNextWorkRequired(pindexPrev, &block, consensusParams))
            return state.DoS(100, error("%s : incorrect proof of work at %d", __func__, nHeight),
                            REJECT_INVALID, "bad-diffbits");
  
    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(error("%s: block's timestamp is too early", __func__),
                             REJECT_INVALID, "time-too-old");

    // Reject block.nVersion=1 blocks when 95% (75% on testnet) of the network has upgraded:
    if (block.nVersion < 2 && IsSuperMajority(2, pindexPrev, consensusParams.nMajorityRejectBlockOutdated, consensusParams))
        return state.Invalid(error("%s: rejected nVersion=1 block", __func__),
                             REJECT_OBSOLETE, "bad-version");

    // Reject block.nVersion=2 blocks when 95% (75% on testnet) of the network has upgraded:
    if (block.nVersion < 3 && IsSuperMajority(3, pindexPrev, consensusParams.nMajorityRejectBlockOutdated, consensusParams))
        return state.Invalid(error("%s: rejected nVersion=2 block", __func__),
                             REJECT_OBSOLETE, "bad-version");

    // Reject block.nVersion=3 blocks when 95% (75% on testnet) of the network has upgraded:
    if (block.nVersion < 4 && IsSuperMajority(4, pindexPrev, consensusParams.nMajorityRejectBlockOutdated, consensusParams))
        return state.Invalid(error("%s : rejected nVersion=3 block", __func__),
                             REJECT_OBSOLETE, "bad-version");

    return true;
}

bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex * const pindexPrev, bool fCheckPOW, bool fMining)
{
    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Start enforcing BIP113 (Median Time Past) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_CSV, versionbitscache) == THRESHOLD_ACTIVE) {
        nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
                              ? pindexPrev->GetMedianTimePast()
                              : block.GetBlockTime();

    // Check that all transactions are finalized
    BOOST_FOREACH(const CTransaction& tx, block.vtx) {
        if (!IsFinalTx(tx, nHeight, nLockTimeCutoff)) {
            return state.DoS(10, error("%s: contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
        }
    }

    // Enforce block.nVersion=2 rule that the coinbase starts with serialized block height
    // if 750 of the last 1,000 blocks are version 2 or greater (51/100 if testnet):
    if (block.nVersion >= 2 && IsSuperMajority(2, pindexPrev, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams))
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0].vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin())) {
            return state.DoS(100, error("%s: block height mismatch in coinbase", __func__), REJECT_INVALID, "bad-cb-height");
        }
    }

	// Rob A., Biblepay, 3/7/2018, Kick Off BotNet Rules
	std::string sBlockVersion = ExtractXML(block.vtx[0].vout[0].sTxOutMessage,"<VER>","</VER>");
	sBlockVersion = strReplace(sBlockVersion, ".", "");
	double dBlockVersion = cdbl(sBlockVersion, 0);
	if (fProd && nHeight > F11000_CUTOVER_HEIGHT_PROD && dBlockVersion < 1101)
	{
		 LogPrintf("ContextualCheckBlock::ERROR Rejecting block version %f at height %f \n",(double)dBlockVersion,(double)nHeight);
		 return false;
	}

	// Rob A., Biblepay, 02-10-2018, Check Proof-Of-Loyalty
	if (fProofOfLoyaltyEnabled && fCheckPOW)
	{
		std::string sError = "";
		std::string sMetrics = "";
		int64_t nAncestorTime = (pindexPrev==NULL) ? 0 : pindexPrev->nTime;
		int nAncestorHeight = (pindexPrev==NULL) ? 0 : pindexPrev->nHeight;

		// Check Stake Signature, and retrieve stake weight
		double dStakeWeight = 0;
		if (block.vtx.size() > 1)
			 dStakeWeight = GetStakeWeight(block.vtx[1], block.GetBlockTime(), block.vtx[0].vout[0].sTxOutMessage, true, sMetrics, sError);
	    bool bPass = CheckProofOfLoyalty(dStakeWeight, 
			block.GetHash(), block.nBits, consensusParams, 
			block.GetBlockTime(), nAncestorTime, nAncestorHeight, 
			block.nNonce, pindexPrev, false);
		if (!bPass)
		{
			LogPrintf("ContextualCheckBlock::ERROR - CheckProofOfLoyalty failed at height %f \n",(double)nHeight);
			return false;
		}
	}


	// Rob A. - Biblepay - 2/8/2018 - Contextual check CPID signature on each block to prevent botnet from forming - level 2
	if (fDistributedComputingEnabled && nHeight > F11000_CUTOVER_HEIGHT_PROD && !fMining)
	{
		int64_t nHeaderAge = GetAdjustedTime() - pindexPrev->nTime;
		bool bActiveRACCheck = nHeaderAge < (60 * 15) ? true : false;
		if (bActiveRACCheck)
		{
			std::string sError = "";
			bool fCPIDFailed = false;
			std::string sCPIDSignature = ExtractXML(block.vtx[0].vout[0].sTxOutMessage, "<cpidsig>","</cpidsig>");
			if (sCPIDSignature.empty())
			{
				if (!fMining && fDebugMaster) LogPrintf(" CPID Signature empty.  Contextual Check Block Failed at height %f. \n", (double)pindexPrev->nHeight+1);
				fCPIDFailed=true;
			}
			bool fCheckCPIDSignature = VerifyCPIDSignature(sCPIDSignature, true, sError);
			if (!fCheckCPIDSignature)
			{
				if (!fMining && fDebugMaster) LogPrintf(" CPID Signature Check Failed.  CPID %s, Error %s \n", block.sBlockMessage.c_str(), sError.c_str());
				fCPIDFailed=true;
			}
			// Ensure this CPID has not solved any of the last N blocks in prod or last block in testnet if header age is < 1 hour:
			std::string sCPID = GetElement(sCPIDSignature, ";", 0);
			bool bSolvedPriorBlocks = HasThisCPIDSolvedPriorBlocks(sCPID, pindexPrev);
			if (bSolvedPriorBlocks)
			{
				if (!fMining && fDebugMaster) LogPrintf(" CPID has solved prior blocks.  Contextual check block failed.  CPID %s ",sCPID.c_str());
				fCPIDFailed=true;
			}
			// Ensure this block can only be solved if this CPID was in the last superblock with a payment - but only if the header age is recent (this allows the chain to continue rolling if PODC goes down)
			double nRecentlyPaid = GetPaymentByCPID(sCPID, nHeight);
			if (nRecentlyPaid >= 0 && nRecentlyPaid < .50)
			{
				if (!fMining && fDebugMaster) LogPrintf(" CPID is not in prior superblock.  Contextual check block failed.  CPID %s, Payments: %f  ", sCPID.c_str(), (double)nRecentlyPaid);
				fCPIDFailed=true;
			}
			if (fCPIDFailed)
			{
				return false;
			}
			
		}
	}


    return true;
}

static bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex=NULL)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    CBlockIndex *pindex = NULL;
	
    if (hash != cblockGenesis.GetHash()) 
	{
        if (miSelf != mapBlockIndex.end()) 
		{
            // Block header is already known.
            pindex = miSelf->second;
            if (ppindex)
                *ppindex = pindex;

			if (!pindex) return false;

            if (pindex->nStatus & BLOCK_FAILED_MASK)
			{
				// 4-24-2018 - This happens second - Found Chain with Blocks marked as invalid
				
				RecoverOrphanedChain(2);

                return state.Invalid(error("%s: block is marked invalid", __func__), 0, "duplicate");
			}
            return true;
        }

        // Get prev block index
		CBlockIndex* pindexAncestor = NULL;
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end()) pindexAncestor = (*mi).second;
	    
        if (mi == mapBlockIndex.end())
		{
			// This happens first 4-24-2018
            return state.DoS(21, error("%s: prev block not found", __func__), 0, "bad-prevblk");
		}
	
		// R Andrews - Biblepay - 02/20/2018 : Process ended with SIG11 Access not within a mapped region (pindexAncestor->nStatus)
		if (!pindexAncestor || pindexAncestor==NULL)
		{
			// This happens 3rd - 4-24-2018 - Found chain with no ancestor
			
			RecoverOrphanedChain(3);

			return state.DoS(15, error("Block has no ancestor. \n"));
        }

        if (pindexAncestor->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");

		// Alex873434:Valgrind (Starting without testnet3 folder causes SIG11 process termination because the following line has no mapBlockIndex iterator)- fixing by reordering the call
		if (!CheckBlockHeader(block, state, true, block.GetBlockTime(), pindexAncestor ? pindexAncestor->nTime : 0, pindexAncestor ? pindexAncestor->nHeight : 0, pindexAncestor))
            return false;
	
		if (fCheckpointsEnabled && !CheckIndexAgainstCheckpoint(pindexAncestor, state, chainparams, hash))
            return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

        if (!ContextualCheckBlockHeader(block, state, pindexAncestor))
            return error("Contextual CheckBlockHeader Failed");
    }
	
    if (pindex == NULL)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    return true;
}

bool IsNodeSynced(int64_t nLastBlockTime)
{
	int64_t nAge = GetAdjustedTime() - nLastBlockTime;
	if (nAge < 60*60*2) return true;
	return false;
}

/** Store block on disk. If dbp is non-NULL, the file is known to already reside on disk */
static bool AcceptBlock(const CBlock& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex, bool fRequested, CDiskBlockPos* dbp)
{
    AssertLockHeld(cs_main);

    CBlockIndex *&pindex = *ppindex;

    if (!AcceptBlockHeader(block, state, chainparams, &pindex))
        return false;
	
    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    if (fAlreadyHave) return true;
    if (!fRequested) {  // If we didn't ask for it:
        if (pindex->nTx != 0) return true;  // This is a previously-processed block that was pruned
        if (!fHasMoreWork) return true;     // Don't process less-work chains
        if (fTooFarAhead) return true;      // Block height is too high
    }

	bool fCheckPOW = true;
    if ((!CheckBlock(block, state, true, true, block.GetBlockTime(), pindex->pprev ? pindex->pprev->nTime : 0, pindex->pprev ? pindex->pprev->nHeight : 0, pindex->pprev)) 
		|| !ContextualCheckBlock(block, state, pindex->pprev, fCheckPOW, false)) 
	{
        if (state.IsInvalid() && !state.CorruptionPossible()) 
		{
			if (true)
			{
				pindex->nStatus |= BLOCK_FAILED_VALID; // PODC - Dirty Blocks can become clean later
				setDirtyBlockIndex.insert(pindex);
			}
        }
        return false;
    }

    int nHeight = pindex->nHeight;

    // Write block to history file
    try 
	{
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != NULL)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize+8, nHeight, block.GetBlockTime(), dbp != NULL))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == NULL)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    if (fCheckForPruning)
        FlushStateToDisk(state, FLUSH_STATE_NONE); // we just allocated more disk space for block files
	// Biblepay - Distributed Computing - 2/3/2018 - Run the DC process if we are in sync
	if (IsNodeSynced(block.GetBlockTime()) && fDistributedComputingEnabled)
	{
		std::string sResult = ExecuteDistributedComputingSanctuaryQuorumProcess();
		if (sResult != "NOT_A_SANCTUARY")
		{
			if (fDebugMaster) LogPrintf(" AcceptBlock::ExecuteDistributedComputingSanctuaryQuorumProcess %s \n", sResult.c_str());
		}
	}

    return true;
}

static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams)
{
    unsigned int nFound = 0;
    for (int i = 0; i < consensusParams.nMajorityWindow && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}


bool ProcessNewBlock(CValidationState& state, const CChainParams& chainparams, const CNode* pfrom, const CBlock* pblock, bool fForceProcessing, CDiskBlockPos* dbp)
{
    // Preliminary checks
	CBlockIndex* pindexAncestor=mapBlockIndex[pblock->hashPrevBlock];
    int64_t nAncestorTime = (pindexAncestor==NULL) ? 0 : pindexAncestor->nTime;
	int nAncestorHeight = (pindexAncestor==NULL) ? 0 : pindexAncestor->nHeight;

    bool checked = CheckBlock(*pblock, state, true, true, pblock->GetBlockTime(), nAncestorTime, nAncestorHeight, pindexAncestor);
    {
        LOCK(cs_main);
        bool fRequested = MarkBlockAsReceived(pblock->GetHash());
        fRequested |= fForceProcessing;
        if (!checked) 
		{
            return error("%s: CheckBlock FAILED", __func__);
        }

        // Store to disk
        CBlockIndex *pindex = NULL;
		bool ret = AcceptBlock(*pblock, state, chainparams, &pindex, fRequested, dbp);
		if (pindex && pfrom) 
		{
            mapBlockSource[pindex->GetBlockHash()] = pfrom->GetId();
        }

        CheckBlockIndex(chainparams.GetConsensus());
        if (!ret)
            return error("%s: AcceptBlock FAILED", __func__);
    }


    if (!ActivateBestChain(state, chainparams, pblock))
        return error("%s: ActivateBestChain failed", __func__);

    masternodeSync.IsBlockchainSynced(true);

    LogPrintf("%s : ACCEPTED\n", __func__);
    return true;
}

bool TestBlockValidity(CValidationState& state, const CChainParams& chainparams, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot, bool fMining)
{
    AssertLockHeld(cs_main);
	if (!(pindexPrev && pindexPrev == chainActive.Tip()))
	{
		LogPrintf(" \r\n ** TestBlockValidity FAILED - pindexNew->pprev != chainActive.Tip() (assert(pindexNew->pprev == chainActive.Tip())); ** \r\n");
		return false;
	}

    if (fCheckpointsEnabled && !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams, block.GetHash()))
	{
        return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());
	}

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, state, fCheckPOW, fCheckMerkleRoot, block.GetBlockTime(), pindexPrev ? pindexPrev->nTime : 0, pindexPrev ? pindexPrev->nHeight : 0, pindexPrev))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev, fCheckPOW, fMining))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, true))
        return false;
    assert(state.IsValid());

    return true;
}

/**
 * BLOCK PRUNING CODE
 */

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    uint64_t retval = 0;
    BOOST_FOREACH(const CBlockFileInfo &file, vinfoBlockFile) {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it) {
        CBlockIndex* pindex = it->second;
        if (pindex->nFile == fileNumber) {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it = range.first;
                range.first++;
                if (it->second == pindex) {
                    mapBlocksUnlinked.erase(it);
                }
            }
        }
    }

    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}


void UnlinkPrunedFiles(std::set<int>& setFilesToPrune)
{
    for (set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it) {
        CDiskBlockPos pos(*it, 0);
        boost::filesystem::remove(GetBlockPosFilename(pos, "blk"));
        boost::filesystem::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Calculate the block/rev files that should be deleted to remain under target*/
void FindFilesToPrune(std::set<int>& setFilesToPrune, uint64_t nPruneAfterHeight)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == NULL || nPruneTarget == 0) {
        return;
    }
    if ((uint64_t)chainActive.Tip()->nHeight <= nPruneAfterHeight) {
        return;
    }

    unsigned int nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint("prune", "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return NULL;
    boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
    boost::filesystem::create_directories(path.parent_path());
    FILE* file = fopen(path.string().c_str(), "rb+");
    if (!file && !fReadOnly)
        file = fopen(path.string().c_str(), "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex * InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex(): new CBlockIndex failed");
    mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool static LoadBlockIndexDB()
{
    const CChainParams& chainparams = Params();
	LogPrintf(" LoadBlockIndexGuts %f ",(double)GetAdjustedTime());

    if (!pblocktree->LoadBlockIndexGuts())
        return false;
	LogPrintf(" Finished %f ",(double)GetAdjustedTime());

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    vector<pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    BOOST_FOREACH(const PAIRTYPE(int, CBlockIndex*)& item, vSortedByHeight)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == NULL || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    set<int> setBlkDataFiles;
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

    // Check whether we have an address index
    pblocktree->ReadFlag("addressindex", fAddressIndex);
    LogPrintf("%s: address index %s\n", __func__, fAddressIndex ? "enabled" : "disabled");

    // Check whether we have a timestamp index
    pblocktree->ReadFlag("timestampindex", fTimestampIndex);
    LogPrintf("%s: timestamp index %s\n", __func__, fTimestampIndex ? "enabled" : "disabled");

    // Check whether we have a spent index
    pblocktree->ReadFlag("spentindex", fSpentIndex);
    LogPrintf("%s: spent index %s\n", __func__, fSpentIndex ? "enabled" : "disabled");

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return true;
    chainActive.SetTip(it->second);

	// If tip is synced set masternode sync as initially synced so users can restart masternodes after a cold boot
	if (GetAdjustedTime() - chainActive.Tip()->GetBlockTime() <= (60*15))
	{
		masternodeSync.IsBlockchainSynced(true);
	}

    PruneBlockIndexCandidates();
	LogPrintf(" Finished Loading Block Index %f ",(double)GetAdjustedTime());

    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n", __func__,
        chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), chainActive.Tip()));

    return true;
}

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100);
}

bool CVerifyDB::VerifyDB(const CChainParams& chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == NULL || chainActive.Tip()->pprev == NULL)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = NULL;
    int nGoodTransactions = 0;
    CValidationState state;
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainActive.Height()-nCheckDepth)
            break;
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus(), "VerifyDB"))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, true, true, block.GetBlockTime(), pindex->pprev ? pindex->pprev->nTime : 0, pindex->pprev ? pindex->pprev->nHeight : 0, pindex->pprev))
            return error("VerifyDB(): *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage) {
            bool fClean = true;
            if (!DisconnectBlock(block, state, pindex, coins, &fClean))
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            pindexState = pindex->pprev;
            if (!fClean) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
                nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip()) {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus(), "VerifyDB"))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(NULL);
    pindexBestInvalid = NULL;
    pindexBestHeader = NULL;
    mempool.clear();
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
    nSyncStarted = 0;
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    mapBlockSource.clear();
    mapBlocksInFlight.clear();
    nPreferredDownload = 0;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    mapNodeState.clear();
    recentRejects.reset(NULL);
    versionbitscache.Clear();
    for (int b = 0; b < VERSIONBITS_NUM_BITS; b++) {
        warningcache[b].clear();
    }

    BOOST_FOREACH(BlockMap::value_type& entry, mapBlockIndex) {
        delete entry.second;
    }
    mapBlockIndex.clear();
    fHavePruned = false;
}

bool LoadBlockIndex()
{
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexDB())
        return false;
    return true;
}

bool InitBlockIndex(const CChainParams& chainparams)
{
    LOCK(cs_main);

    // Initialize global variables that cannot be constructed at startup.
    recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));

    // Check whether we're already initialized
    if (chainActive.Genesis() != NULL)
        return true;

    // Use the provided setting for -txindex in the new database
    fTxIndex = GetBoolArg("-txindex", DEFAULT_TXINDEX);
    pblocktree->WriteFlag("txindex", fTxIndex);

    // Use the provided setting for -addressindex in the new database
    fAddressIndex = GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX);
    pblocktree->WriteFlag("addressindex", fAddressIndex);

    // Use the provided setting for -timestampindex in the new database
    fTimestampIndex = GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX);
    pblocktree->WriteFlag("timestampindex", fTimestampIndex);

    fSpentIndex = GetBoolArg("-spentindex", DEFAULT_SPENTINDEX);
    pblocktree->WriteFlag("spentindex", fSpentIndex);

    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex) {
        try {
            CBlock &block = const_cast<CBlock&>(cblockGenesis);
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize+8, 0, block.GetBlockTime()))
                return error("LoadBlockIndex(): FindBlockPos failed");
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                return error("LoadBlockIndex(): writing genesis block to disk failed");
            CBlockIndex *pindex = AddToBlockIndex(block);
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                return error("LoadBlockIndex(): genesis block not accepted");
            if (!ActivateBestChain(state, chainparams, &block))
                return error("LoadBlockIndex(): genesis block cannot be activated");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
            return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
        } catch (const std::runtime_error& e) {
            return error("LoadBlockIndex(): failed to initialize block database: %s", e.what());
        }
    }

    return true;
}

bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();
	
    int nLoaded = 0;
    try 
	{
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2*MAX_BLOCK_SIZE, MAX_BLOCK_SIZE+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) 
		{
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try 
			{
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SIZE)
                    continue;
            } catch (const std::exception&) 
			{
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
				CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
		
                if (hash != cblockGenesis.GetHash() && mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end()) {
                    LogPrintf("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                            block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

		
                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0) 
				{
                    CValidationState state;
		
                    if (ProcessNewBlock(state, chainparams, NULL, &block, true, dbp))
                        nLoaded++;
	                if (state.IsError())
                        break;
                } 
				else if (hash != cblockGenesis.GetHash() && mapBlockIndex[hash]->nHeight % 1000 == 0) 
				{
                    LogPrintf("Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                // Recursively process earlier encountered successors of this block
                deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) 
				{
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) 
					{
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
	                    if (ReadBlockFromDisk(block, it->second, chainparams.GetConsensus(), "LoadExternalBlockFile"))
                        {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(),
                                    head.ToString());
                            CValidationState dummy;
							LogPrintf(".");

                            if (ProcessNewBlock(dummy, chainparams, NULL, &block, true, &it->second))
                            {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                    }
                }
            } catch (const std::exception& e) 
			{
                if (fDebugMaster) LogPrint("net","%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } 
	catch (const std::runtime_error& e) 
	{
	    AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

void static CheckBlockIndex(const Consensus::Params& consensusParams)
{
	
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex*,CBlockIndex*> forward;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

	
    assert(forward.size() == mapBlockIndex.size());

    std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeGenesis = forward.equal_range(NULL);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = NULL; // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = NULL; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNeverProcessed = NULL; // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex* pindexFirstNotTreeValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotTransactionsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).

	
    while (pindex != NULL) {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == NULL && pindex->nTx == 0) pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTransactionsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == cblockGenesis.GetHash()); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0);  // nSequenceId can't be set for blocks that aren't linked
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA) assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO) assert(pindex->nStatus & BLOCK_HAVE_DATA);
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstNeverProcessed != NULL) == (pindex->nChainTx == 0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNotTransactionsValid != NULL) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == NULL); // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstNeverProcessed == NULL) {
            if (pindexFirstInvalid == NULL) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.
                if (pindexFirstMissing == NULL || pindex == chainActive.Tip()) {
                    assert(setBlockIndexCandidates.count(pindex));
                }
                // If some parent is missing, then it could be that this block was in
                // setBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != NULL && pindexFirstInvalid == NULL) {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA)) assert(!foundInUnlinked); // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (pindexFirstMissing == NULL) assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == NULL && pindexFirstMissing != NULL) {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(fHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0) {
                if (pindexFirstInvalid == NULL) {
                    assert(foundInUnlinked);
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing) pindexFirstMissing = NULL;
            if (pindex == pindexFirstNeverProcessed) pindexFirstNeverProcessed = NULL;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotTransactionsValid) pindexFirstNotTransactionsValid = NULL;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = NULL;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

	
    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

std::string GetWarnings(const std::string& strFor)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;
    string strGUI;

    if (!CLIENT_VERSION_IS_RELEASE) {
        strStatusBar = "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications";
        strGUI = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
    }

    if (GetBoolArg("-testsafemode", DEFAULT_TESTSAFEMODE))
        strStatusBar = strRPC = strGUI = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        nPriority = 1000;
        strStatusBar = strGUI = strMiscWarning;
    }

    if (fLargeWorkForkFound)
    {
        nPriority = 2000;
        strStatusBar = strRPC = "Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.";
        strGUI = _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    }
    else if (fLargeWorkInvalidChainFound)
    {
        nPriority = 2000;
        strStatusBar = strRPC = "Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.";
        strGUI = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
        {
            const CAlert& alert = item.second;
            if (alert.AppliesToMe() && alert.nPriority > nPriority)
            {
                nPriority = alert.nPriority;
                strStatusBar = strGUI = alert.strStatusBar;
            }
        }
    }

    if (strFor == "gui")
        return strGUI;
    else if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(const CInv& inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    switch (inv.type)
    {
    case MSG_TX:
        {
            assert(recentRejects);
            if (chainActive.Tip()->GetBlockHash() != hashRecentRejectsChainTip)
            {
                // If the chain tip has changed previously rejected transactions
                // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
                // or a double-spend. Reset the rejects filter and give those
                // txs a second chance.
                hashRecentRejectsChainTip = chainActive.Tip()->GetBlockHash();
                recentRejects->reset();
            }

            return recentRejects->contains(inv.hash) ||
                   mempool.exists(inv.hash) ||
                   mapOrphanTransactions.count(inv.hash) ||
                   pcoinsTip->HaveCoins(inv.hash);
        }

    case MSG_BLOCK:
        return mapBlockIndex.count(inv.hash);

    /*
        Biblepay Related Inventory Messages

        --

        We shouldn't update the sync times for each of the messages when we already have it.
        We're going to be asking many nodes upfront for the full inventory list, so we'll get duplicates of these.
        We want to only update the time on new hits, so that we can time out appropriately if needed.
    */
    case MSG_TXLOCK_REQUEST:
        return instantsend.AlreadyHave(inv.hash);

    case MSG_TXLOCK_VOTE:
        return instantsend.AlreadyHave(inv.hash);

    case MSG_SPORK:
        return mapSporks.count(inv.hash);

    case MSG_MASTERNODE_PAYMENT_VOTE:
        return mnpayments.mapMasternodePaymentVotes.count(inv.hash);
	
	case MSG_DISTRIBUTED_COMPUTING_VOTE:
		return mnpayments.mapDistributedComputingVotes.count(inv.hash);

    case MSG_MASTERNODE_PAYMENT_BLOCK:
        {
            BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
            return mi != mapBlockIndex.end() && mnpayments.mapMasternodeBlocks.find(mi->second->nHeight) != mnpayments.mapMasternodeBlocks.end();
        }

    case MSG_MASTERNODE_ANNOUNCE:
        return mnodeman.mapSeenMasternodeBroadcast.count(inv.hash) && !mnodeman.IsMnbRecoveryRequested(inv.hash);

    case MSG_MASTERNODE_PING:
        return mnodeman.mapSeenMasternodePing.count(inv.hash);

    case MSG_DSTX:
        return mapDarksendBroadcastTxes.count(inv.hash);

    case MSG_GOVERNANCE_OBJECT:
    case MSG_GOVERNANCE_OBJECT_VOTE:
        return ! governance.ConfirmInventoryRequest(inv);

    case MSG_MASTERNODE_VERIFY:
        return mnodeman.mapSeenMasternodeVerification.count(inv.hash);
    }

    // Don't know what it is, just say we already got one
    return true;
}

void static ProcessGetData(CNode* pfrom, const Consensus::Params& consensusParams)
{
    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

    vector<CInv> vNotFound;

    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        const CInv &inv = *it;
        LogPrint("net", "ProcessGetData -- inv = %s\n", inv.ToString());
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
            {
                bool send = false;
                BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    if (chainActive.Contains(mi->second)) {
                        send = true;
                    } else {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a month older (both in time, and in
                        // best equivalent proof of work) than the best header chain we know about.
                        send = mi->second->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) &&
                            (pindexBestHeader->GetBlockTime() - mi->second->GetBlockTime() < nOneMonth) &&
                            (GetBlockProofEquivalentTime(*pindexBestHeader, *mi->second, *pindexBestHeader, consensusParams) < nOneMonth);
                        if (!send) {
                            LogPrintf("%s: ignoring request from peer=%i for old block that isn't in the main chain\n", __func__, pfrom->GetId());
                        }
                    }
                }
                // disconnect node in case we have reached the outbound limit for serving historical blocks
                // never disconnect whitelisted nodes
                static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
                if (send && CNode::OutboundTargetReached(true) && ( ((pindexBestHeader != NULL) && (pindexBestHeader->GetBlockTime() - mi->second->GetBlockTime() > nOneWeek)) || inv.type == MSG_FILTERED_BLOCK) && !pfrom->fWhitelisted)
                {
                    LogPrint("net", "historical block serving limit reached, disconnect peer=%d\n", pfrom->GetId());

                    //disconnect node
                    pfrom->fDisconnect = true;
                    send = false;
                }
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (send && (mi->second->nStatus & BLOCK_HAVE_DATA)) 
				{
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, (*mi).second, consensusParams, "ProcessGetData"))
					{
                        //assert(!"cannot load block from disk");
						LogPrintf("\r\n** ProcessGetData:Cannot load block from disk.\r\n");
						// BiblePay : If we cant load the block, we probably wrote this block while on a fork and reorganized - now the block does not meet POBh
						pfrom->fDisconnect = true;
						send=false; 
					}
					else
					{
						if (inv.type == MSG_BLOCK)
							pfrom->PushMessage(NetMsgType::BLOCK, block);
						else // MSG_FILTERED_BLOCK)
						{
							LOCK(pfrom->cs_filter);
							if (pfrom->pfilter)
							{
								CMerkleBlock merkleBlock(block, *pfrom->pfilter);
								pfrom->PushMessage(NetMsgType::MERKLEBLOCK, merkleBlock);
								// CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
								// This avoids hurting performance by pointlessly requiring a round-trip
								// Note that there is currently no way for a node to request any single transactions we didn't send here -
								// they must either disconnect and retry or request the full block.
								// Thus, the protocol spec specified allows for us to provide duplicate txn here,
								// however we MUST always provide at least what the remote peer needs
								typedef std::pair<unsigned int, uint256> PairType;
								BOOST_FOREACH(PairType& pair, merkleBlock.vMatchedTxn)
									pfrom->PushMessage(NetMsgType::TX, block.vtx[pair.first]);
							}
							// else
								// no response
						}
					}

                    // Trigger the peer node to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                        pfrom->PushMessage(NetMsgType::INV, vInv);
                        pfrom->hashContinue.SetNull();
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool pushed = false;
                {
                    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                    {
                        LOCK(cs_mapRelay);
                        map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                        if (mi != mapRelay.end()) {
                            ss += (*mi).second;
                            pushed = true;
                        }
                    }
                    if(pushed)
                        pfrom->PushMessage(inv.GetCommand(), ss);
                }

                if (!pushed && inv.type == MSG_TX) {
                    CTransaction tx;
                    if (mempool.lookup(inv.hash, tx)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        pfrom->PushMessage(NetMsgType::TX, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_TXLOCK_REQUEST) {
                    CTxLockRequest txLockRequest;
                    if(instantsend.GetTxLockRequest(inv.hash, txLockRequest)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << txLockRequest;
                        pfrom->PushMessage(NetMsgType::TXLOCKREQUEST, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_TXLOCK_VOTE) {
                    CTxLockVote vote;
                    if(instantsend.GetTxLockVote(inv.hash, vote)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << vote;
                        pfrom->PushMessage(NetMsgType::TXLOCKVOTE, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_SPORK) {
                    if(mapSporks.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mapSporks[inv.hash];
                        pfrom->PushMessage(NetMsgType::SPORK, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_MASTERNODE_PAYMENT_VOTE) 
				{
                    if(mnpayments.HasVerifiedPaymentVote(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mnpayments.mapMasternodePaymentVotes[inv.hash];
                        pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
                        pushed = true;
                    }
                }

				if (!pushed && inv.type == MSG_DISTRIBUTED_COMPUTING_VOTE) 
				{
					LogPrintf(" **Pushing Distributed Votes ** \n");
                    if(mnpayments.HasVerifiedDistributedComputingVote(inv.hash)) 
					{
						LogPrintf(" \nPushing Distributed Votes ** \n");
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(65535);
                        ss << mnpayments.mapDistributedComputingVotes[inv.hash];
                        pfrom->PushMessage(NetMsgType::DISTRIBUTEDCOMPUTINGVOTE, ss);
                        pushed = true;
                    }
                }


                if (!pushed && inv.type == MSG_MASTERNODE_PAYMENT_BLOCK) {
                    BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
                    LOCK(cs_mapMasternodeBlocks);
                    if (mi != mapBlockIndex.end() && mnpayments.mapMasternodeBlocks.count(mi->second->nHeight)) {
                        BOOST_FOREACH(CMasternodePayee& payee, mnpayments.mapMasternodeBlocks[mi->second->nHeight].vecPayees) {
                            std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                            BOOST_FOREACH(uint256& hash, vecVoteHashes) {
                                if(mnpayments.HasVerifiedPaymentVote(hash)) {
                                    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                                    ss.reserve(1000);
                                    ss << mnpayments.mapMasternodePaymentVotes[hash];
                                    pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
                                }
                            }
                        }
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_MASTERNODE_ANNOUNCE) {
                    if(mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)){
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mnodeman.mapSeenMasternodeBroadcast[inv.hash].second;
                        pfrom->PushMessage(NetMsgType::MNANNOUNCE, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_MASTERNODE_PING) {
                    if(mnodeman.mapSeenMasternodePing.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mnodeman.mapSeenMasternodePing[inv.hash];
                        pfrom->PushMessage(NetMsgType::MNPING, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_DSTX) {
                    if(mapDarksendBroadcastTxes.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mapDarksendBroadcastTxes[inv.hash];
                        pfrom->PushMessage(NetMsgType::DSTX, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_GOVERNANCE_OBJECT) {
                    LogPrint("net", "ProcessGetData -- MSG_GOVERNANCE_OBJECT: inv = %s\n", inv.ToString());
                    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                    bool topush = false;
                    {
                        if(governance.HaveObjectForHash(inv.hash)) {
                            ss.reserve(1000);
                            if(governance.SerializeObjectForHash(inv.hash, ss)) {
                                topush = true;
                            }
                        }
                    }
                    LogPrint("net", "ProcessGetData -- MSG_GOVERNANCE_OBJECT: topush = %d, inv = %s\n", topush, inv.ToString());
                    if(topush) {
                        pfrom->PushMessage(NetMsgType::MNGOVERNANCEOBJECT, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_GOVERNANCE_OBJECT_VOTE) {
                    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                    bool topush = false;
                    {
                        if(governance.HaveVoteForHash(inv.hash)) {
                            ss.reserve(1000);
                            if(governance.SerializeVoteForHash(inv.hash, ss)) {
                                topush = true;
                            }
                        }
                    }
                    if(topush) {
                        LogPrint("net", "ProcessGetData -- pushing: inv = %s\n", inv.ToString());
                        pfrom->PushMessage(NetMsgType::MNGOVERNANCEOBJECTVOTE, ss);
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_MASTERNODE_VERIFY) {
                    if(mnodeman.mapSeenMasternodeVerification.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mnodeman.mapSeenMasternodeVerification[inv.hash];
                        pfrom->PushMessage(NetMsgType::MNVERIFY, ss);
                        pushed = true;
                    }
                }

                if (!pushed)
                    vNotFound.push_back(inv);
            }

            // Track requests for our stuff.
            GetMainSignals().Inventory(inv.hash);

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage(NetMsgType::NOTFOUND, vNotFound);
    }
}

bool static ProcessMessage(CNode* pfrom, string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    const CChainParams& chainparams = Params();
    RandAddSeedPerfmon();
    LogPrint("net", "received: %s (%u bytes) peer=%d \n", SanitizeString(strCommand), vRecv.size(), pfrom->id);

    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (!(nLocalServices & NODE_BLOOM) &&
              (strCommand == NetMsgType::FILTERLOAD ||
               strCommand == NetMsgType::FILTERADD ||
               strCommand == NetMsgType::FILTERCLEAR))
    {
        if (pfrom->nVersion >= NO_BLOOM_VERSION) {
            Misbehaving(pfrom->GetId(), 100);
            return false;
        } else if (GetBoolArg("-enforcenodebloom", false)) {
            pfrom->fDisconnect = true;
            return false;
        }
    }


    if (strCommand == NetMsgType::VERSION)
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE, string("Duplicate version message"));
			LogPrintf(" duplicate version message ");
			if (!pfrom->fWhitelisted) Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
        if ((fProd && pfrom->nVersion < MIN_PEER_PROTO_VERSION) || (!fProd && pfrom->nVersion < MIN_PEER_TESTNET_PROTO_VERSION))
        {
            // disconnect from peers older than this proto version
            if (fDebugMaster) LogPrint("net","peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            pfrom->fDisconnect = true;
            return false;
        }

		if (pfrom->nVersion < 70712 && GetAdjustedTime() > 1513649777)
		{
			// After 12-19-2017 @ 06:00:00 AM, hang up on old peers
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            pfrom->fDisconnect = true;
            return false;
		}


        int64_t nTimeDrift = std::abs(GetAdjustedTime() - nTime);
        if (nTimeDrift > (5 * 60))
        {
            LogPrintf("Disconnecting unauthorized peer with Network Time off by %f seconds!\r\n",(double)nTimeDrift);
			Misbehaving(pfrom->GetId(), 12);
			pfrom->fDisconnect = true;
			return false;
        }
      
        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }

		// User Agent Test 2-13-2018 - R Andrews - Biblepay
		std::string sVersion = pfrom->cleanSubVer;
		sVersion = strReplace(sVersion, "Biblepay Core:", "");
		sVersion = strReplace(sVersion, "/", "");
		sVersion = strReplace(sVersion, ".", "");
		double dPeerVersion = cdbl(sVersion, 0);
		if (!fProd && dPeerVersion == 109) dPeerVersion=1090;
		if (dPeerVersion < 1093 && dPeerVersion > 1000 && !fProd)
		{
		    LogPrint("net","Disconnecting unauthorized peer in TestNet using old version %f\r\n",(double)dPeerVersion);
			Misbehaving(pfrom->GetId(), 14);
        	pfrom->fDisconnect = true;
			return false;
		}
		
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            if (fDebugMaster) LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        CNodeState* pNodeState = NULL;
        {
            LOCK(cs_main);
            pNodeState = State(pfrom->GetId());
            assert(pNodeState);
        }

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom, pNodeState);

        // Change version
        pfrom->PushMessage(NetMsgType::VERACK);
        pfrom->ssSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    if (fDebugMaster) LogPrint("net","ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    if (fDebugMaster) LogPrint("net","ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage(NetMsgType::GETADDR);
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        // Relay alerts
        {
            LOCK(cs_mapAlerts);
            BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
                item.second.RelayTo(pfrom);
        }

        pfrom->fSuccessfullyConnected = true;

        string remoteAddr;
        if (fLogIPs)
            remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

		std::string sBanned = GetArg("-banip", "");
   
		if (pfrom->addr.ToString() == sBanned && !sBanned.empty())
		{
	         pfrom->fDisconnect = true;
			 pfrom->fSuccessfullyConnected=false;
		}
	
        if (fDebugMaster) LogPrint("net","receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                  pfrom->cleanSubVer, pfrom->nVersion,
                  pfrom->nStartingHeight, addrMe.ToString(), pfrom->id,
                  remoteAddr);

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        AddTimeData(pfrom->addr, nTimeOffset);
    }


    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }


    else if (strCommand == NetMsgType::VERACK)
    {
        pfrom->SetRecvVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
        }

        if (pfrom->nVersion >= SENDHEADERS_VERSION) {
            // Tell our peer we prefer to receive headers rather than inv's
            // We send this to non-NODE NETWORK peers as well, because even
            // non-NODE NETWORK peers can announce blocks (such as pruning
            // nodes)
            pfrom->PushMessage(NetMsgType::SENDHEADERS);
        }
    }


    else if (strCommand == NetMsgType::ADDR)
    {
        vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            Misbehaving(pfrom->GetId(), 21);
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        BOOST_FOREACH(CAddress& addr, vAddr)
        {
            boost::this_thread::interruption_point();

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(hashSalt) ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    BOOST_FOREACH(CNode* pnode, vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }

    else if (strCommand == NetMsgType::SENDHEADERS)
    {
        LOCK(cs_main);
        State(pfrom->GetId())->fPreferHeaders = true;
    }


    else if (strCommand == NetMsgType::INV)
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            Misbehaving(pfrom->GetId(), 22);
            return error("message inv size() = %u", vInv.size());
        }

        bool fBlocksOnly = GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);

        // Allow whitelisted peers to send data other than blocks in blocks only mode if whitelistrelay is true
        if (pfrom->fWhitelisted && GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY))
            fBlocksOnly = false;

        LOCK(cs_main);

        std::vector<CInv> vToFetch;


        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            if(!inv.IsKnownType()) {
                LogPrint("net", "got inv of unknown type %d: %s peer=%d\n", inv.type, inv.hash.ToString(), pfrom->id);
                continue;
            }

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            LogPrint("net", "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);

            if (inv.type == MSG_BLOCK) {
                UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                if (!fAlreadyHave && !fImporting && !fReindex && !mapBlocksInFlight.count(inv.hash)) {
                    // First request the headers preceding the announced block. In the normal fully-synced
                    // case where a new block is announced that succeeds the current tip (no reorganization),
                    // there are no such headers.
                    // Secondly, and only when we are close to being synced, we request the announced block directly,
                    // to avoid an extra round-trip. Note that we must *first* ask for the headers, so by the
                    // time the block arrives, the header chain leading up to it is already validated. Not
                    // doing this will result in the received block being rejected as an orphan in case it is
                    // not a direct successor.
                    pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexBestHeader), inv.hash);
                    CNodeState *nodestate = State(pfrom->GetId());
                    if (CanDirectFetch(chainparams.GetConsensus()) &&
                        nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        vToFetch.push_back(inv);
                        // Mark block as in flight already, even though the actual "getdata" message only goes out
                        // later (within the same cs_main lock, though).
                        MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                    }
                    LogPrint("net", "getheaders (%d) %s to peer=%d\n", pindexBestHeader->nHeight, inv.hash.ToString(), pfrom->id);
                }
            }
            else
            {
                if (fBlocksOnly)
                    LogPrint("net", "transaction (%s) inv sent in violation of protocol peer=%d\n", inv.hash.ToString(), pfrom->id);
                else if (!fAlreadyHave && !fImporting && !fReindex && !IsInitialBlockDownload())
                    pfrom->AskFor(inv);
            }

            // Track requests for our stuff
            GetMainSignals().Inventory(inv.hash);

            if (pfrom->nSendSize > (SendBufferSize() * 2)) {
                Misbehaving(pfrom->GetId(), 50);
                return error("send buffer size() = %u", pfrom->nSendSize);
            }
        }

        if (!vToFetch.empty())
            pfrom->PushMessage(NetMsgType::GETDATA, vToFetch);
    }


    else if (strCommand == NetMsgType::GETDATA)
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            Misbehaving(pfrom->GetId(), 23);
            return error("message getdata size() = %u", vInv.size());
        }

        if (fDebug || (vInv.size() != 1))
            LogPrint("net", "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);

        if ((fDebug && vInv.size() > 0) || (vInv.size() == 1))
            LogPrint("net", "received getdata for: %s peer=%d\n", vInv[0].ToString(), pfrom->id);

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom, chainparams.GetConsensus());
    }


    else if (strCommand == NetMsgType::GETBLOCKS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain

		if (locator.IsNull()) return false;
        CBlockIndex* pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            // If pruning, don't inv blocks unless we have on disk and are likely to still have
            // for some reasonable time window (1 hour) that block relay might require.
            const int nPrunedBlocksLikelyToHave = MIN_BLOCKS_TO_KEEP - 3600 / chainparams.GetConsensus().nPowTargetSpacing;
            if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) || pindex->nHeight <= chainActive.Tip()->nHeight - nPrunedBlocksLikelyToHave))
            {
                LogPrint("net", " getblocks stopping, pruned or too old block at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }


    else if (strCommand == NetMsgType::GETHEADERS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);
        if (IsInitialBlockDownload() && !pfrom->fWhitelisted) {
            LogPrint("net", "Ignoring getheaders from peer=%d because node is in initial block download\n", pfrom->id);
            return true;
        }

        CNodeState *nodestate = State(pfrom->GetId());
        CBlockIndex* pindex = NULL;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            BlockMap::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = (*mi).second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chainActive, locator);
            if (pindex)
                pindex = chainActive.Next(pindex);
        }

        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        LogPrint("net", "getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString(), pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        // pindex can be NULL either if we sent chainActive.Tip() OR
        // if our peer has chainActive.Tip() (and thus we are sending an empty
        // headers message). In both cases it's safe to update
        // pindexBestHeaderSent to be our tip.
        nodestate->pindexBestHeaderSent = pindex ? pindex : chainActive.Tip();
        pfrom->PushMessage(NetMsgType::HEADERS, vHeaders);
    }


    else if (strCommand == NetMsgType::TX || strCommand == NetMsgType::DSTX || strCommand == NetMsgType::TXLOCKREQUEST)
    {
        // Stop processing the transaction early if
        // We are in blocks only mode and peer is either not whitelisted or whitelistrelay is off
        if (GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY) && (!pfrom->fWhitelisted || !GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY)))
        {
            LogPrint("net", "transaction sent in violation of protocol peer=%d\n", pfrom->id);
            return true;
        }

        vector<uint256> vWorkQueue;
        vector<uint256> vEraseQueue;
        CTransaction tx;
        CTxLockRequest txLockRequest;
        CDarksendBroadcastTx dstx;
        int nInvType = MSG_TX;

        // Read data and assign inv type
        if(strCommand == NetMsgType::TX) {
            vRecv >> tx;
        } else if(strCommand == NetMsgType::TXLOCKREQUEST) {
            vRecv >> txLockRequest;
            tx = txLockRequest;
            nInvType = MSG_TXLOCK_REQUEST;
        } else if (strCommand == NetMsgType::DSTX) {
            vRecv >> dstx;
            tx = dstx.tx;
            nInvType = MSG_DSTX;
        }

        CInv inv(nInvType, tx.GetHash());
        pfrom->AddInventoryKnown(inv);
        pfrom->setAskFor.erase(inv.hash);

        // Process custom logic, no matter if tx will be accepted to mempool later or not
        if (strCommand == NetMsgType::TXLOCKREQUEST) {
            if(!instantsend.ProcessTxLockRequest(txLockRequest)) {
                LogPrint("instantsend", "TXLOCKREQUEST -- failed %s\n", txLockRequest.GetHash().ToString());
                return false;
            }
        } else if (strCommand == NetMsgType::DSTX) {
            uint256 hashTx = tx.GetHash();

            if(mapDarksendBroadcastTxes.count(hashTx)) {
                LogPrint("privatesend", "DSTX -- Already have %s, skipping...\n", hashTx.ToString());
                return true; // not an error
            }

            CMasternode* pmn = mnodeman.Find(dstx.vin);
            if(pmn == NULL) {
                LogPrint("privatesend", "DSTX -- Can't find masternode %s to verify %s\n", dstx.vin.prevout.ToStringShort(), hashTx.ToString());
                return false;
            }

            if(!pmn->fAllowMixingTx) {
                LogPrint("privatesend", "DSTX -- Masternode %s is sending too many transactions %s\n", dstx.vin.prevout.ToStringShort(), hashTx.ToString());
                return true;
            }

            if(!dstx.CheckSignature(pmn->pubKeyMasternode)) {
                LogPrint("privatesend", "DSTX -- CheckSignature() failed for %s\n", hashTx.ToString());
                return false;
            }

            LogPrintf("DSTX -- Got Masternode transaction %s\n", hashTx.ToString());
            mempool.PrioritiseTransaction(hashTx, hashTx.ToString(), 1000, 0.1*COIN);
            pmn->fAllowMixingTx = false;
        }

        LOCK(cs_main);

        bool fMissingInputs = false;
        CValidationState state;

        mapAlreadyAskedFor.erase(inv.hash);

        if (!AlreadyHave(inv) && AcceptToMemoryPool(mempool, state, tx, true, &fMissingInputs))
        {
            // Process custom txes, this changes AlreadyHave to "true"
            if (strCommand == NetMsgType::DSTX) {
                LogPrintf("DSTX -- Masternode transaction accepted, txid=%s, peer=%d\n",
                        tx.GetHash().ToString(), pfrom->id);
                mapDarksendBroadcastTxes.insert(make_pair(tx.GetHash(), dstx));
            } else if (strCommand == NetMsgType::TXLOCKREQUEST) {
                LogPrintf("TXLOCKREQUEST -- Transaction Lock Request accepted, txid=%s, peer=%d\n",
                        tx.GetHash().ToString(), pfrom->id);
                instantsend.AcceptLockRequest(txLockRequest);
            }

            mempool.check(pcoinsTip);
            RelayTransaction(tx);
            vWorkQueue.push_back(inv.hash);

            LogPrint("mempool", "AcceptToMemoryPool: peer=%d: accepted %s (poolsz %u txn, %u kB)\n",
                pfrom->id,
                tx.GetHash().ToString(),
                mempool.size(), mempool.DynamicMemoryUsage() / 1000);

            // Recursively process any orphan transactions that depended on this one
            set<NodeId> setMisbehaving;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                map<uint256, set<uint256> >::iterator itByPrev = mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
                if (itByPrev == mapOrphanTransactionsByPrev.end())
                    continue;
                for (set<uint256>::iterator mi = itByPrev->second.begin();
                     mi != itByPrev->second.end();
                     ++mi)
                {
                    const uint256& orphanHash = *mi;
                    const CTransaction& orphanTx = mapOrphanTransactions[orphanHash].tx;
                    NodeId fromPeer = mapOrphanTransactions[orphanHash].fromPeer;
                    bool fMissingInputs2 = false;
                    // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                    // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                    // anyone relaying LegitTxX banned)
                    CValidationState stateDummy;


                    if (setMisbehaving.count(fromPeer))
                        continue;
                    if (AcceptToMemoryPool(mempool, stateDummy, orphanTx, true, &fMissingInputs2))
                    {
                        LogPrint("mempool", "   accepted orphan tx %s\n", orphanHash.ToString());
                        RelayTransaction(orphanTx);
                        vWorkQueue.push_back(orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    }
                    else if (!fMissingInputs2)
                    {
                        int nDos = 0;
                        if (stateDummy.IsInvalid(nDos) && nDos > 0)
                        {
                            // Punish peer that gave us an invalid orphan tx
                            Misbehaving(fromPeer, nDos);
                            setMisbehaving.insert(fromPeer);
                            LogPrint("mempool", "   invalid orphan tx %s\n", orphanHash.ToString());
                        }
                        // Has inputs but not accepted to mempool
                        // Probably non-standard or insufficient fee/priority
                        LogPrint("mempool", "   removed orphan tx %s\n", orphanHash.ToString());
                        vEraseQueue.push_back(orphanHash);
                        assert(recentRejects);
                        recentRejects->insert(orphanHash);
                    }
                    mempool.check(pcoinsTip);
                }
            }

            BOOST_FOREACH(uint256 hash, vEraseQueue)
                EraseOrphanTx(hash);
        }
        else if (fMissingInputs)
        {
            AddOrphanTx(tx, pfrom->GetId());

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
            unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);
            if (nEvicted > 0)
                LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
        } else {
            assert(recentRejects);
            recentRejects->insert(tx.GetHash());

            if (strCommand == NetMsgType::TXLOCKREQUEST && !AlreadyHave(inv)) {
                // i.e. AcceptToMemoryPool failed, probably because it's conflicting
                // with existing normal tx or tx lock for another tx. For the same tx lock
                // AlreadyHave would have return "true" already.

                // It's the first time we failed for this tx lock request,
                // this should switch AlreadyHave to "true".
                instantsend.RejectLockRequest(txLockRequest);
                // this lets other nodes to create lock request candidate i.e.
                // this allows multiple conflicting lock requests to compete for votes
                RelayTransaction(tx);
            }

            if (pfrom->fWhitelisted && GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)) {
                // Always relay transactions received from whitelisted peers, even
                // if they were already in the mempool or rejected from it due
                // to policy, allowing the node to function as a gateway for
                // nodes hidden behind it.
                //
                // Never relay transactions that we would assign a non-zero DoS
                // score for, as we expect peers to do the same with us in that
                // case.
                int nDoS = 0;
                if (!state.IsInvalid(nDoS) || nDoS == 0) {
                    LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", tx.GetHash().ToString(), pfrom->id);
                    RelayTransaction(tx);
                } else {
                    LogPrintf("Not relaying invalid transaction %s from whitelisted peer=%d (%s)\n", tx.GetHash().ToString(), pfrom->id, FormatStateMessage(state));
                }
            }
        }

        int nDoS = 0;
        if (state.IsInvalid(nDoS))
        {
            LogPrint("mempoolrej", "%s from peer=%d was not accepted: %s\n", tx.GetHash().ToString(),
                pfrom->id,
                FormatStateMessage(state));
            if (state.GetRejectCode() < REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
                pfrom->PushMessage(NetMsgType::REJECT, strCommand, (unsigned char)state.GetRejectCode(),
                                   state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
        FlushStateToDisk(state, FLUSH_STATE_PERIODIC);
    }


    else if (strCommand == NetMsgType::HEADERS && !fImporting && !fReindex) // Ignore headers received while importing
    {
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS) {
		    LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 19);
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++) {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        CBlockIndex *pindexLast = NULL;
		{
			LOCK(cs_main);
			BOOST_FOREACH(const CBlockHeader& header, headers) 
			{
				if (pindexLast != NULL && header.hashPrevBlock != pindexLast->GetBlockHash()) {
					Misbehaving(pfrom->GetId(), 18);
					return error("non-continuous headers sequence");
				}
			}

			BOOST_FOREACH(const CBlockHeader& header, headers) 
			{
				CValidationState state;
				if (!AcceptBlockHeader(header, state, chainparams, &pindexLast)) 
				{
					int nDoS;
					if (state.IsInvalid(nDoS)) 
					{
						if (nDoS > 0)
							Misbehaving(pfrom->GetId(), nDoS);
						std::string strError = "invalid header received " + header.GetHash().ToString();
						return error(strError.c_str());
					}
				}
			}
		}

        if (pindexLast)
            UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        if (nCount == MAX_HEADERS_RESULTS && pindexLast) {
            // Headers message had its maximum size; the peer may have more headers.
            LogPrint("net", "more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight, pfrom->id, pfrom->nStartingHeight);
            pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexLast), uint256());
        }
		else
		{
	        if (nCount == 0) 
			{
                // Nothing interesting. Stop asking this peers for more headers.
                return true;
            }
    
		}

        bool fCanDirectFetch = CanDirectFetch(chainparams.GetConsensus());
        CNodeState *nodestate = State(pfrom->GetId());
        // If this set of headers is valid and ends in a block with at least as
        // much work as our tip, download as much as possible.
        if (fCanDirectFetch && pindexLast->IsValid(BLOCK_VALID_TREE) && chainActive.Tip()->nChainWork <= pindexLast->nChainWork) {
            vector<CBlockIndex *> vToFetch;
            CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast, up to a limit.
            while (pindexWalk && !chainActive.Contains(pindexWalk) && vToFetch.size() <= MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                if (!(pindexWalk->nStatus & BLOCK_HAVE_DATA) &&
                        !mapBlocksInFlight.count(pindexWalk->GetBlockHash())) {
                    // We don't have this block, and it's not yet in flight.
                    vToFetch.push_back(pindexWalk);
                }
                pindexWalk = pindexWalk->pprev;
            }
            // If pindexWalk still isn't on our main chain, we're looking at a
            // very large reorg at a time we think we're close to caught up to
            // the main chain -- this shouldn't really happen.  Bail out on the
            // direct fetch and rely on parallel download instead.
            if (!chainActive.Contains(pindexWalk)) {
                LogPrint("net", "Large reorg, won't direct fetch to %s (%d)\n",
                        pindexLast->GetBlockHash().ToString(),
                        pindexLast->nHeight);
            } else {
                vector<CInv> vGetData;
                // Download as much as possible, from earliest to latest.
                BOOST_REVERSE_FOREACH(CBlockIndex *pindex, vToFetch) {
                    if (nodestate->nBlocksInFlight >= MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        // Can't download any more from this peer
                        break;
                    }
                    vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                    MarkBlockAsInFlight(pfrom->GetId(), pindex->GetBlockHash(), chainparams.GetConsensus(), pindex);
                    LogPrint("net", "Requesting block %s from  peer=%d\n",
                            pindex->GetBlockHash().ToString(), pfrom->id);
                }
                if (vGetData.size() > 1) {
                    LogPrint("net", "Downloading blocks toward %s (%d) via headers direct fetch\n",
                            pindexLast->GetBlockHash().ToString(), pindexLast->nHeight);
                }
                if (vGetData.size() > 0) {
                    pfrom->PushMessage(NetMsgType::GETDATA, vGetData);
                }
            }
        }

        CheckBlockIndex(chainparams.GetConsensus());
    }

    else if (strCommand == NetMsgType::BLOCK && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;

        CInv inv(MSG_BLOCK, block.GetHash());
        LogPrint("net", "received block %s peer=%d\n", inv.hash.ToString(), pfrom->id);

        pfrom->AddInventoryKnown(inv);

        CValidationState state;
        // Process all blocks from whitelisted peers, even if not requested,
        // unless we're still syncing with the network.
        // Such an unrequested block may still be processed, subject to the
        // conditions in AcceptBlock().
        bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
        ProcessNewBlock(state, chainparams, pfrom, &block, forceProcessing, NULL);
        int nDoS;
        if (state.IsInvalid(nDoS)) {
            assert (state.GetRejectCode() < REJECT_INTERNAL); // Blocks are never rejected with internal reject codes
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, (unsigned char)state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), nDoS);
            }
        }

    }


    else if (strCommand == NetMsgType::GETADDR)
    {
        // This asymmetric behavior for inbound and outbound connections was introduced
        // to prevent a fingerprinting attack: an attacker can send specific fake addresses
        // to users' AddrMan and later request them by sending getaddr messages.
        // Making nodes which are behind NAT and can only make outgoing connections ignore
        // the getaddr message mitigates the attack.
        if (!pfrom->fInbound) {
            LogPrint("net", "Ignoring \"getaddr\" from outbound connection. peer=%d\n", pfrom->id);
            return true;
        }

        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr();
        BOOST_FOREACH(const CAddress &addr, vAddr)
            pfrom->PushAddress(addr);
    }


    else if (strCommand == NetMsgType::MEMPOOL)
    {
        if (CNode::OutboundTargetReached(false) && !pfrom->fWhitelisted)
        {
            LogPrint("net", "mempool request with bandwidth limit reached, disconnect peer=%d\n", pfrom->GetId());
            pfrom->fDisconnect = true;
            return true;
        }
        LOCK2(cs_main, pfrom->cs_filter);

        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        vector<CInv> vInv;
        BOOST_FOREACH(uint256& hash, vtxid) {
            CInv inv(MSG_TX, hash);
            if (pfrom->pfilter) {
                CTransaction tx;
                bool fInMemPool = mempool.lookup(hash, tx);
                if (!fInMemPool) continue; // another thread removed since queryHashes, maybe...
                if (!pfrom->pfilter->IsRelevantAndUpdate(tx)) continue;
            }
            vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ) {
                pfrom->PushMessage(NetMsgType::INV, vInv);
                vInv.clear();
            }
        }
        if (vInv.size() > 0)
            pfrom->PushMessage(NetMsgType::INV, vInv);
    }


    else if (strCommand == NetMsgType::PING)
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage(NetMsgType::PONG, nonce);
        }
    }


    else if (strCommand == NetMsgType::PONG)
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime, pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint("net", "pong peer=%d: %s, %x expected, %x received, %u bytes\n",
                pfrom->id,
                sProblem,
                pfrom->nPingNonceSent,
                nonce,
                nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
    }


    else if (fAlerts && strCommand == NetMsgType::ALERT)
    {
        CAlert alert;
        vRecv >> alert;

        uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0)
        {
            if (alert.ProcessAlert(chainparams.AlertKey()))
            {
                // Relay
                pfrom->setKnown.insert(alertHash);
                {
                    LOCK(cs_vNodes);
                    BOOST_FOREACH(CNode* pnode, vNodes)
                        alert.RelayTo(pnode);
                }
            }
            else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
				LogPrintf("Invalid command %s ",strCommand.c_str());
                Misbehaving(pfrom->GetId(), 16);
            }
        }
    }


    else if (strCommand == NetMsgType::FILTERLOAD)
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
            // There is no excuse for sending a too-large filter
            Misbehaving(pfrom->GetId(), 100);
        else
        {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
        }
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == NetMsgType::FILTERADD)
    {
        vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE)
        {
            Misbehaving(pfrom->GetId(), 100);
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                Misbehaving(pfrom->GetId(), 100);
        }
    }


    else if (strCommand == NetMsgType::FILTERCLEAR)
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == NetMsgType::REJECT)
    {
        if (fDebug) {
            try {
                string strMsg; unsigned char ccode; string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                if (strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX)
                {
                    uint256 hash;
                    vRecv >> hash;
                    ss << ": hash " << hash.ToString();
                }
                LogPrint("net", "Reject %s\n", SanitizeString(ss.str()));
            } catch (const std::ios_base::failure&) {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint("net", "Unparseable reject message received\n");
            }
        }
    }
    else
    {
        bool found = false;
        const std::vector<std::string> &allMessages = getAllNetMessageTypes();
        BOOST_FOREACH(const std::string msg, allMessages) {
            if(msg == strCommand) {
                found = true;
                break;
            }
        }

        if (found)
        {
            //probably one the extensions
            darkSendPool.ProcessMessage(pfrom, strCommand, vRecv);
            mnodeman.ProcessMessage(pfrom, strCommand, vRecv);
            mnpayments.ProcessMessage(pfrom, strCommand, vRecv);
            instantsend.ProcessMessage(pfrom, strCommand, vRecv);
            sporkManager.ProcessSpork(pfrom, strCommand, vRecv);
            masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);
            governance.ProcessMessage(pfrom, strCommand, vRecv);
        }
        else
        {
            // Ignore unknown commands for extensibility
            LogPrint("net", "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->id);
        }
    }

    return true;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(CNode* pfrom)
{
    const CChainParams& chainparams = Params();
    //if (fDebug)
    //    LogPrintf("%s(%u messages)\n", __func__, pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom, chainparams.GetConsensus());

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) return fOk;

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage& msg = *it;

        //if (fDebug)
        //    LogPrintf("%s(message %u msgsz, %u bytes, complete:%s)\n", __func__,
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Scan for message start
        if (memcmp(msg.hdr.pchMessageStart, chainparams.MessageStart(), MESSAGE_START_SIZE) != 0) {
            LogPrintf("PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n", SanitizeString(msg.hdr.GetCommand()), pfrom->id);
            fOk = false;
            break;
        }

        // Read header
        CMessageHeader& hdr = msg.hdr;
        if (!hdr.IsValid(chainparams.MessageStart()))
        {
            LogPrintf("PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n", SanitizeString(hdr.GetCommand()), pfrom->id);
            continue;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = ReadLE32((unsigned char*)&hash);
        if (nChecksum != hdr.nChecksum)
        {
            LogPrintf("%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", __func__,
               SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
			//6984
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        }
        catch (const std::ios_base::failure& e)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_MALFORMED, string("error parsing message"));
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n",
					__func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else
            {
				//EXCEPTION: NSt8ios_base7failureE        String length limit exceeded        biblepay in ProcessMessages()     
			    if (strstr(e.what(),"length limit exceeded"))
				{
					LogPrintf(" Length exceeded \n");
				}
				vector<unsigned char> vData;
				vRecv >> vData;
				std::string sData = VectorToString(vData);
				LogPrintf("Function::%s (Command: %s, MessageSize: %u bytes): Exception: %s  - TrappedData %s \n", 
					__func__, SanitizeString(strCommand), nMessageSize, e.what(), sData.c_str());
				
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (const boost::thread_interrupted&) {
            throw;
        }
        catch (const std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
		{
            LogPrint("net","%s (%s, %u bytes) FAILED peer = %d\n", __func__, SanitizeString(strCommand), nMessageSize, pfrom->id);
		}

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}

static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex *pblockindex;
    if (nHeight < chainActive.Tip()->nHeight / 2)
		pblockindex = mapBlockIndex[cblockGenesis.GetHash()];
    else
        pblockindex = chainActive.Tip();
    if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
        pblockindex = pblockindexFBBHLast;
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;
    while (pblockindex->nHeight < nHeight)
		pblockindex = chainActive.Next(pblockindex);
    pblockindexFBBHLast = pblockindex;
    return pblockindex;
}


CAmount StringToAmount(std::string sValue)
{
	if (sValue.empty()) return 0;
    CAmount amount;
    if (!ParseFixedPoint(sValue, 8, &amount)) return 0;
    if (!MoneyRange(amount)) throw runtime_error("AMOUNT OUT OF MONEY RANGE");
    return amount;
}

std::string AmountToString(const CAmount& amount)
{
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
	std::string sAmount = strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder);
	return sAmount;
}


std::string FormatHTML(std::string sInput, int iInsertCount, std::string sStringToInsert)
{
	std::vector<std::string> vInput = Split(sInput.c_str()," ");
	std::string sOut = "";
	int iCt = 0;
	for (int i=0; i < (int)vInput.size(); i++)
	{
		sOut += vInput[i] + " ";
		iCt++;
		if (iCt >= iInsertCount)
		{
			iCt=0;
			sOut += sStringToInsert;
		}
	}
	return sOut;
}

void UpdateMagnitude()
{
		double nBudget = 0;
		double nTotalPaid = 0;
		int iLastSuperblock = 0;
		std::string out_Superblocks = "";
		int out_SuperblockCount = 0;
		int out_HitCount = 0;
		double out_OneDayPaid = 0;
		double out_OneWeekPaid = 0;
		double out_d1 = 0;
		double out_d2 = 0;
		std::string sPK = GetMyPublicKeys();

		mnMagnitude = GetUserMagnitude(sPK, nBudget, nTotalPaid, iLastSuperblock, out_Superblocks, out_SuperblockCount, out_HitCount, out_OneDayPaid, out_OneWeekPaid, out_d1, out_d2);
}

std::string GetVersionAlert()
{
	if (msGithubVersion.empty()) 
	{
		msGithubVersion = GetGithubVersion();
	}
	if (msGithubVersion.empty()) return "";
	std::string sGithubVersion = strReplace(msGithubVersion, ".", "");
	double dGithubVersion = cdbl(sGithubVersion, 0);
	std::string sCurrentVersion = FormatFullVersion();
	sCurrentVersion = strReplace(sCurrentVersion, ".", "");
	double dCurrentVersion = cdbl(sCurrentVersion, 0);
	std::string sNarr = "";
	if (dCurrentVersion < dGithubVersion) sNarr = "<br>** Client Out of Date (v=" + sCurrentVersion + "/v=" + sGithubVersion + ") **";
	return sNarr;
}

void SetOverviewStatus()
{
	double dDiff = GetDifficultyN(chainActive.Tip(), 10);
	double dPOWDifficulty = GetDifficulty(chainActive.Tip()) * 10;

	if (fDistributedComputingEnabled) UpdateMagnitude();
	std::string sPrayer = "NA";
	GetDataList("PRAYER", 7, iPrayerIndex, "", sPrayer);
	msGlobalStatus = "<font color=blue>Blocks: " + RoundToString((double)chainActive.Tip()->nHeight, 0) 
		+ "; PODC Difficulty: " + RoundToString(dDiff,2) 
		+ "; <br>POW Difficulty: " + RoundToString(dPOWDifficulty,2) + ";";
    if (fDistributedComputingEnabled) msGlobalStatus += " Magnitude: " + RoundToString(mnMagnitude,2) + "; ";
	msGlobalStatus += "</font>";
	std::string sVersionAlert = GetVersionAlert();
	if (!sVersionAlert.empty()) msGlobalStatus += " <font color=purple>" + sVersionAlert + "</font> ;";
	// std::string sPrayers = FormatHTML(sPrayer, 12, "<br>");
	msGlobalStatus2 = "<font color=maroon><b>" + sPrayer + "</font></b><br>&nbsp;";
}

void ResetTimerMain(std::string timer_name)
{
	mvTimers[timer_name] = 0;
}


bool TimerMain(std::string timer_name, int max_ms)
{
	mvTimers[timer_name] = mvTimers[timer_name] + 1;
	if (mvTimers[timer_name] > max_ms)
	{
		mvTimers[timer_name]=0;
		return true;
	}
	return false;
}

bool NonObnoxiousLog(std::string sLogSection, std::string sLogKey, std::string sValue, int64_t nAllowedSpan)
{
	boost::to_upper(sLogSection);
	boost::to_upper(sLogKey);
	int64_t nElapsed = GetAdjustedTime() - mvApplicationCacheTimestamp[sLogSection + ";" + sLogKey];
	WriteCache(sLogSection, sLogKey, "1", GetAdjustedTime());
	bool bAllowed = (nElapsed > nAllowedSpan) ? true : false;
	if (bAllowed)
	{
		LogPrintf("[%s], [%s]: %s (elapsed %f)", sLogSection.c_str(), sLogKey.c_str(), sValue.c_str(), (double)nElapsed);
		mvApplicationCacheTimestamp[sLogSection + ";" + sLogKey] = GetAdjustedTime();
	}
	return bAllowed;
}

void WriteCache(std::string sSection, std::string sKey, std::string sValue, int64_t locktime)
{
	if (sSection.empty() || sKey.empty()) return;
	boost::to_upper(sSection);
	boost::to_upper(sKey);
	std::string temp_value = mvApplicationCache[sSection + ";" + sKey];
	if (temp_value.empty())
	{
		mvApplicationCache.insert(map<std::string,std::string>::value_type(sSection + ";" + sKey, sValue));
	    mvApplicationCache[sSection + ";" + sKey] = sValue;
	}
	mvApplicationCache[sSection + ";" + sKey] = sValue;
	// Record Cache Entry timestamp
	int64_t temp_locktime = mvApplicationCacheTimestamp[sSection + ";" + sKey];
	if (temp_locktime == 0)
	{
		mvApplicationCacheTimestamp.insert(map<std::string,int64_t>::value_type(sSection + ";" + sKey,1));
		mvApplicationCacheTimestamp[sSection + ";" + sKey]=locktime;
	}
	mvApplicationCacheTimestamp[sSection + ";" + sKey] = locktime;
}



void PurgeCacheAsOfExpiration(std::string sSection, int64_t nExpiration)
{
	boost::to_upper(sSection);
	for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii)
	{
		std::string sKey = (*ii).first;
		boost::to_upper(sKey);
		if (sKey.length() > sSection.length())
		{
			if (sKey.substr(0,sSection.length())==sSection)
			{
				int64_t nTimestamp = mvApplicationCacheTimestamp[sKey];
				if (nTimestamp < nExpiration)
				{
					mvApplicationCache[sKey]="";
					mvApplicationCacheTimestamp[sKey]=0;
				}
			}
		}
	}
}



bool IsTimeWithinMins(int64_t nTime, int mins)
{
	int64_t nCutoff = GetAdjustedTime() - (60*mins);
	return (nTime < nCutoff) ? false : true;
	return true;
}

void DeleteCache(std::string section, std::string keyname)
{
    std::string pk = section + ";" +keyname;
    mvApplicationCache.erase(pk);
    mvApplicationCacheTimestamp.erase(pk);
}


std::string GetMessagesFromBlock(const CBlock& block, std::string sTargetType)
{
    	std::string sMessages = "";
       	BOOST_FOREACH(const CTransaction &tx, block.vtx)
		{
			if (tx.vout.size() > 0)
			{
			  if (!tx.vout[0].sTxOutMessage.empty())
			  {
				  std::string sMessageType      = ExtractXML(tx.vout[0].sTxOutMessage,"<MT>","</MT>");
				  std::string sMessageKey       = ExtractXML(tx.vout[0].sTxOutMessage,"<MK>","</MK>");
				  std::string sMessageValue     = ExtractXML(tx.vout[0].sTxOutMessage,"<MV>","</MV>");
				  boost::to_upper(sMessageType);
				  boost::to_upper(sMessageKey);
				  if (!sMessageType.empty() && !sMessageKey.empty() && !sMessageValue.empty() && !sTargetType.empty())
				  {
					  if (sMessageType == sTargetType)
					  {
						  sMessages+=sMessageValue + "\r\n";
					  }
				  }
			  }
			}
	  	}
		return sMessages;
}


void MemorizeBlockChainPrayers(bool fDuringConnectBlock, bool fSubThread, bool fColdBoot)
{
		int nMaxDepth = chainActive.Tip()->nHeight;
		int nMinDepth = fDuringConnectBlock ? nMaxDepth - 2 : nMaxDepth - (BLOCKS_PER_DAY * 30 * 12);  // One year
		if (nMinDepth < 0) nMinDepth = 0;
		CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
		const Consensus::Params& consensusParams = Params().GetConsensus();
		if (fSubThread && !fPrayersMemorized) LogPrintf("MemorizeBlockChainPrayers @ %f ",GetAdjustedTime());
		int64_t nMaxPaymentAge = 60 * 60 * 24 * 7;
		while (pindex && pindex->nHeight < nMaxDepth)
		{
			if (pindex) if (pindex->nHeight < chainActive.Tip()->nHeight) pindex = chainActive.Next(pindex);
			if (!pindex) break;
			CBlock block;
			if (ReadBlockFromDisk(block, pindex, consensusParams, "MemorizeBlockChainPrayers")) 
			{
	  			for (unsigned int n = 0; n < block.vtx.size(); n++)
       			{
					double dTotalSent = 0;
					std::string sPrayer = "";
					for (unsigned int i = 0; i < block.vtx[n].vout.size(); i++)
					{
						sPrayer += block.vtx[n].vout[i].sTxOutMessage;
						double dAmount = block.vtx[n].vout[i].nValue/COIN;
						dTotalSent += dAmount;
						// Track Cancer Payment totals by address (so we can implement the additional CheckBlock rule: Researcher has magnitude in last 30 days) - R ANDREWS - 6-27-2018
						// Coinbase Only, vout > 0, and Mature:
						if (n==0 && i > 0 && block.vtx[n].vout.size() > 4)
						{
							std::string sRecipient = PubKeyToAddress(block.vtx[n].vout[i].scriptPubKey);
							double dTally = cdbl(ReadCacheWithMaxAge("AddressPayment", sRecipient, nMaxPaymentAge), 0) + dAmount;
							WriteCache("AddressPayment", sRecipient, RoundToString(dTally, 0), block.GetBlockTime());
						}
					}
					MemorizePrayer(sPrayer, block.GetBlockTime(), dTotalSent, 0, block.vtx[n].GetHash().GetHex());
				}
	  		}
		}
		if (fColdBoot) 
		{
			// ** Initialize distributed-computing CPID
			std::string out_address = "";
			double nMagnitude = 0;
			std::string sAddress = "";
			// Race Condition - Reported by Snat21 & Dave_BBP - Rob Andrews - 6/13/2018
			FindResearcherCPIDByAddress(sAddress, out_address, nMagnitude);
			mnMagnitude=nMagnitude;
			// ** End of Initializing distributed-computing CPID
			fPrayersMemorized = true;
		}
		if (fSubThread && !fPrayersMemorized) LogPrintf("Finished MemorizeBlockChainPrayers @ %f ",GetAdjustedTime());
}


std::string RetrieveTxOutInfo(const CBlockIndex* pindexLast, int iLookback, int iTxOffset, int ivOutOffset, int iDataType)
{
	// When DataType == 1, returns txOut Address
	// When DataType == 2, returns TxId
	// When DataType == 3, returns Blockhash

    if (pindexLast == NULL || pindexLast->nHeight == 0) 
	{
        return "";
    }

    for (int i = 1; i < iLookback; i++) 
	{
        if (pindexLast->pprev == NULL) { break; }
        pindexLast = pindexLast->pprev;
    }
	if (iDataType < 1 || iDataType > 3) return "DATA_TYPE_OUT_OF_RANGE";

	if (iDataType == 3) return pindexLast ? pindexLast->GetBlockHash().GetHex() : "";	
	
	const Consensus::Params& consensusParams = Params().GetConsensus();
	CBlock block;
	if (ReadBlockFromDisk(block, pindexLast, consensusParams, "RetrieveTxOutInfo"))
	{
		if (iTxOffset >= (int)block.vtx.size()) iTxOffset=block.vtx.size()-1;
		if (ivOutOffset >= (int)block.vtx[iTxOffset].vout.size()) ivOutOffset=block.vtx[iTxOffset].vout.size()-1;
		if (iTxOffset >= 0 && ivOutOffset >= 0)
		{
			if (iDataType == 1)
			{
				std::string sPKAddr = PubKeyToAddress(block.vtx[iTxOffset].vout[ivOutOffset].scriptPubKey);
				return sPKAddr;
			}
			else if (iDataType == 2)
			{
				std::string sTxId = block.vtx[iTxOffset].GetHash().ToString();
				return sTxId;
			}
		}
	}
	
	return "";

}


std::string SignMessage(std::string sMsg, std::string sPrivateKey)
{
     CKey key;
     std::vector<unsigned char> vchMsg = vector<unsigned char>(sMsg.begin(), sMsg.end());
     std::vector<unsigned char> vchPrivKey = ParseHex(sPrivateKey);
     std::vector<unsigned char> vchSig;
     key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end()), false); // if key is not correct openssl may crash, test this
     if (!key.Sign(Hash(vchMsg.begin(), vchMsg.end()), vchSig))  
     {
          return "Unable to sign message, check private key.";
     }
     const std::string sig(vchSig.begin(), vchSig.end());     
     std::string SignedMessage = EncodeBase64(sig);
     return SignedMessage;
}

bool IsMature(int64_t nTime, int64_t nMaturityAge)
{
	int64_t nNow = GetAdjustedTime();
	int64_t nRemainder = nNow % nMaturityAge;
	int64_t nCutoff = nNow - nRemainder;
	bool bMature = nTime <= nCutoff;
	return bMature;
}

void MemorizePrayer(std::string sMessage, int64_t nTime, double dAmount, int iPosition, std::string sTxID)
{
	if (sMessage.empty()) return;
	int64_t nAge = GetAdjustedTime() - nTime;
	std::string sPODC = ExtractXML(sMessage, "<PODC_TASKS>", "</PODC_TASKS>");
	if (!sPODC.empty())
	{
		double nMaximumChatterAge = GetSporkDouble("podcmaximumchatterage", (60 * 60 * 24));
		if (nAge < nMaximumChatterAge)
		{
			std::string sErr2 = "";
			std::string sMySig = ExtractXML(sMessage,"<cpidsig>","</cpidsig>");
			bool fSigChecked = VerifyCPIDSignature(sMySig, true, sErr2);
			if (fSigChecked)
			{
				std::string sCPID = GetElement(sMySig, ";", 0);
				WriteCache("UTXOWeight", sCPID, RoundToString(dAmount, 0), nTime);
				WriteCache("CPIDTasks", sCPID, sPODC, nTime);
				if (IsMature(nTime, 14400))
				{
					WriteCache("MatureUTXOWeight", sCPID, RoundToString(dAmount, 0), nTime);
					WriteCache("MatureCPIDTasks", sCPID, sPODC, nTime);
				}
			}
			else
			{
				if (fDebugMaster && false) LogPrintf("\n Time %f SigFailure %s SigLen %f ", (double)nTime, sMySig.c_str(), (double)sMySig.length());
			}
		}
		return;
	}
    if (Contains(sMessage,"<MT>"))
	{
		  std::string sMessageType      = ExtractXML(sMessage,"<MT>","</MT>");
  		  std::string sMessageKey       = ExtractXML(sMessage,"<MK>","</MK>");
		  std::string sMessageValue     = ExtractXML(sMessage,"<MV>","</MV>");
		  std::string sSig              = ExtractXML(sMessage,"<MS>","</MS>");
		  std::string sNonce            = ExtractXML(sMessage,"<NONCE>","</NONCE>");
		  std::string sSporkSig         = ExtractXML(sMessage,"<SPORKSIG>","</SPORKSIG>");
		  boost::to_upper(sMessageType);
		  boost::to_upper(sMessageKey);
		  bool bRequiresSignature = (sMessageType=="SPORK") ? true : false;
		  if (sMessageType == "NEWS") sMessageValue = sTxID;
		  if (!sSporkSig.empty())
		  {
			  double dNonce = cdbl(sNonce, 0);
			  bool bSigInvalid = false;
			  if (dNonce > (nTime+(60 * 60)) || dNonce < (nTime-(60 * 60))) bSigInvalid = true;
			  std::string sError = "";
			  const CChainParams& chainparams = Params();
			  bool fSigValid = CheckStakeSignature(chainparams.GetConsensus().FoundationAddress, sSporkSig, sMessageValue + sNonce, sError);
    		  bool bValid = (fSigValid && !bSigInvalid);
			  if (!bValid)
			  {
					if (fDebugMaster) LogPrintf(" MemorizePrayers::CPIDSignatureFailed - Nonce %f, Time %f , Bad spork Sig %s on message %s on TXID %s \n", (double)dNonce, (double)nTime, sSporkSig.c_str(),
						sMessageValue.c_str(), sTxID.c_str());
					sMessageValue = "";
			  }
			  if (!bValid && bRequiresSignature) sMessageValue = "";
		}
		else
		{
			  if (bRequiresSignature) sMessageValue = "";
		}
		  
		if (!sMessageType.empty() && !sMessageKey.empty() && !sMessageValue.empty())
		{
			std::string sTimestamp = TimestampToHRDate((double)nTime + iPosition);
			// Were using the Block time here because tx time returns seconds past epoch, and adjusting the time by the vout position (so the user can see what time the prayer was accepted in the block).
			std::string sAdjMessageKey = sMessageKey;
			bool bDCC = (sMessageType == "DCC" || sMessageType == "SPORK");
			if (!bDCC)
			{
				if (!(Contains(sMessageKey, "(") && Contains(sMessageKey,")")))
				{
			       sAdjMessageKey = sMessageKey + " (" + sTimestamp + ")";
				}
			}
			WriteCache(sMessageType, sAdjMessageKey, sMessageValue, nTime);
			if (sMessageType == "DCC")
			{
				if (IsMature(nTime, 14400)) WriteCache("MatureDCC", sAdjMessageKey, sMessageValue, nTime);
			}
		}
	}
}





bool SendMessages(CNode* pto)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    {
        // Don't send anything until we get its version message
        if (pto->nVersion == 0)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                pto->PushMessage(NetMsgType::PING, nonce);
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                pto->PushMessage(NetMsgType::PING);
            }
        }

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        // Address refresh broadcast
        int64_t nNow = GetTimeMicros();
        if (!IsInitialBlockDownload() && pto->nNextLocalAddrSend < nNow) {
            AdvertiseLocal(pto);
            pto->nNextLocalAddrSend = PoissonNextSend(nNow, AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL);
        }

        //
        // Message: addr
        //
        if (pto->nNextAddrSend < nNow) {
            pto->nNextAddrSend = PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            BOOST_FOREACH(const CAddress& addr, pto->vAddrToSend)
            {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage(NetMsgType::ADDR, vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage(NetMsgType::ADDR, vAddr);
        }

        CNodeState &state = *State(pto->GetId());
        if (state.fShouldBan) {
            if (pto->fWhitelisted)
                LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
            else {
                pto->fDisconnect = true;
                if (pto->addr.IsLocal())
                    LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString());
                else
                {
                    CNode::Ban(pto->addr, BanReasonNodeMisbehaving);
                }
            }
            state.fShouldBan = false;
        }

        BOOST_FOREACH(const CBlockReject& reject, state.rejects)
            pto->PushMessage(NetMsgType::REJECT, (string)NetMsgType::BLOCK, reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
        state.rejects.clear();

        // Start block sync
        if (pindexBestHeader == NULL)
            pindexBestHeader = chainActive.Tip();
        bool fFetch = state.fPreferredDownload || (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if (!state.fSyncStarted && !pto->fClient && !fImporting && !fReindex) {
            // Only actively request headers from a single peer, unless we're close to end of initial download.
            if ((nSyncStarted == 0 && fFetch) || pindexBestHeader->GetBlockTime() > GetAdjustedTime() - 6 * 60 * 60) { // NOTE: was "close to today" and 24h in Bitcoin
                state.fSyncStarted = true;
                nSyncStarted++;
                const CBlockIndex *pindexStart = pindexBestHeader;
                /* If possible, start at the block preceding the currently
                   best known header.  This ensures that we always get a
                   non-empty list of headers back as long as the peer
                   is up-to-date.  With a non-empty response, we can initialise
                   the peer's known best block.  This wouldn't be possible
                   if we requested starting at pindexBestHeader and
                   got back an empty response.  */
                if (pindexStart->pprev)
                    pindexStart = pindexStart->pprev;
                LogPrint("net", "initial getheaders (%d) to peer=%d (startheight:%d)\n", pindexStart->nHeight, pto->id, pto->nStartingHeight);
                pto->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexStart), uint256());
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fImporting && !IsInitialBlockDownload())
        {
            GetMainSignals().Broadcast(nTimeBestReceived);
        }

        //
        // Try sending block announcements via headers
        //
        {
            // If we have less than MAX_BLOCKS_TO_ANNOUNCE in our
            // list of block hashes we're relaying, and our peer wants
            // headers announcements, then find the first header
            // not yet known to our peer but would connect, and send.
            // If no header would connect, or if we have too many
            // blocks, or if the peer doesn't want headers, just
            // add all to the inv queue.
            LOCK(pto->cs_inventory);
            vector<CBlock> vHeaders;
            bool fRevertToInv = (!state.fPreferHeaders || pto->vBlockHashesToAnnounce.size() > MAX_BLOCKS_TO_ANNOUNCE);
            CBlockIndex *pBestIndex = NULL; // last header queued for delivery
            ProcessBlockAvailability(pto->id); // ensure pindexBestKnownBlock is up-to-date

            if (!fRevertToInv) {
                bool fFoundStartingHeader = false;
                // Try to find first header that our peer doesn't have, and
                // then send all headers past that one.  If we come across any
                // headers that aren't on chainActive, give up.
                BOOST_FOREACH(const uint256 &hash, pto->vBlockHashesToAnnounce) {
                    BlockMap::iterator mi = mapBlockIndex.find(hash);
                    assert(mi != mapBlockIndex.end());
                    CBlockIndex *pindex = mi->second;
                    if (chainActive[pindex->nHeight] != pindex) {
                        // Bail out if we reorged away from this block
                        fRevertToInv = true;
                        break;
                    }
                    if (pBestIndex != NULL && pindex->pprev != pBestIndex) {
                        // This means that the list of blocks to announce don't
                        // connect to each other.
                        // This shouldn't really be possible to hit during
                        // regular operation (because reorgs should take us to
                        // a chain that has some block not on the prior chain,
                        // which should be caught by the prior check), but one
                        // way this could happen is by using invalidateblock /
                        // reconsiderblock repeatedly on the tip, causing it to
                        // be added multiple times to vBlockHashesToAnnounce.
                        // Robustly deal with this rare situation by reverting
                        // to an inv.
                        fRevertToInv = true;
                        break;
                    }
                    pBestIndex = pindex;
                    if (fFoundStartingHeader) {
                        // add this to the headers message
                        vHeaders.push_back(pindex->GetBlockHeader());
                    } else if (PeerHasHeader(&state, pindex)) {
                        continue; // keep looking for the first new block
                    } else if (pindex->pprev == NULL || PeerHasHeader(&state, pindex->pprev)) {
                        // Peer doesn't have this header but they do have the prior one.
                        // Start sending headers.
                        fFoundStartingHeader = true;
                        vHeaders.push_back(pindex->GetBlockHeader());
                    } else {
                        // Peer doesn't have this header or the prior one -- nothing will
                        // connect, so bail out.
                        fRevertToInv = true;
                        break;
                    }
                }
            }
            if (fRevertToInv) {
                // If falling back to using an inv, just try to inv the tip.
                // The last entry in vBlockHashesToAnnounce was our tip at some point
                // in the past.
                if (!pto->vBlockHashesToAnnounce.empty()) {
                    const uint256 &hashToAnnounce = pto->vBlockHashesToAnnounce.back();
                    BlockMap::iterator mi = mapBlockIndex.find(hashToAnnounce);
                    assert(mi != mapBlockIndex.end());
                    CBlockIndex *pindex = mi->second;

                    // Warn if we're announcing a block that is not on the main chain.
                    // This should be very rare and could be optimized out.
                    // Just log for now.
                    if (chainActive[pindex->nHeight] != pindex) {
                        LogPrint("net", "Announcing block %s not on main chain (tip=%s)\n",
                            hashToAnnounce.ToString(), chainActive.Tip()->GetBlockHash().ToString());
                    }

                    // If the peer announced this block to us, don't inv it back.
                    // (Since block announcements may not be via inv's, we can't solely rely on
                    // setInventoryKnown to track this.)
                    if (!PeerHasHeader(&state, pindex)) {
                        pto->PushInventory(CInv(MSG_BLOCK, hashToAnnounce));
                        LogPrint("net", "%s: sending inv peer=%d hash=%s\n", __func__,
                            pto->id, hashToAnnounce.ToString());
                    }
                }
            } else if (!vHeaders.empty()) {
                if (vHeaders.size() > 1) {
                    LogPrint("net", "%s: %u headers, range (%s, %s), to peer=%d\n", __func__,
                            vHeaders.size(),
                            vHeaders.front().GetHash().ToString(),
                            vHeaders.back().GetHash().ToString(), pto->id);
                } else {
                    LogPrint("net", "%s: sending header %s to peer=%d\n", __func__,
                            vHeaders.front().GetHash().ToString(), pto->id);
                }
                pto->PushMessage(NetMsgType::HEADERS, vHeaders);
                state.pindexBestHeaderSent = pBestIndex;
            }
            pto->vBlockHashesToAnnounce.clear();
        }

        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            bool fSendTrickle = pto->fWhitelisted;
            if (pto->nNextInvSend < nNow) {
                fSendTrickle = true;
                pto->nNextInvSend = PoissonNextSend(nNow, AVG_INVENTORY_BROADCAST_INTERVAL);
            }
            LOCK(pto->cs_inventory);
            vInv.reserve(std::min<size_t>(1000, pto->vInventoryToSend.size()));
            vInvWait.reserve(pto->vInventoryToSend.size());
            BOOST_FOREACH(const CInv& inv, pto->vInventoryToSend)
            {
                if (inv.type == MSG_TX && pto->filterInventoryKnown.contains(inv.hash))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(inv.hash) ^ UintToArith256(hashSalt));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((UintToArith256(hashRand) & 3) != 0);

                    if (fTrickleWait)
                    {
                        LogPrint("net", "SendMessages -- queued inv(vInvWait): %s  index=%d peer=%d\n", inv.ToString(), vInvWait.size(), pto->id);
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                pto->filterInventoryKnown.insert(inv.hash);

                LogPrint("net", "SendMessages -- queued inv: %s  index=%d peer=%d\n", inv.ToString(), vInv.size(), pto->id);
                vInv.push_back(inv);
                if (vInv.size() >= 1000)
                {
                    LogPrint("net", "SendMessages -- pushing inv's: count=%d peer=%d\n", vInv.size(), pto->id);
                    pto->PushMessage(NetMsgType::INV, vInv);
                    vInv.clear();
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty()) {
            LogPrint("net", "SendMessages -- pushing tailing inv's: count=%d peer=%d\n", vInv.size(), pto->id);
            pto->PushMessage(NetMsgType::INV, vInv);
        }

        // Detect whether we're stalling
        nNow = GetTimeMicros();
        if (!pto->fDisconnect && state.nStallingSince && state.nStallingSince < nNow - 1000000 * BLOCK_STALLING_TIMEOUT) {
            // Stalling only triggers when the block download window cannot move. During normal steady state,
            // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
            // should only happen during initial block download.
            LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id);
            pto->fDisconnect = true;
        }
        // In case there is a block that has been in flight from this peer for 2 + 0.5 * N times the block interval
        // (with N the number of peers from which we're downloading validated blocks), disconnect due to timeout.
        // We compensate for other peers to prevent killing off peers due to our own downstream link
        // being saturated. We only count validated in-flight blocks so peers can't advertise non-existing block hashes
        // to unreasonably increase our timeout.
        if (!pto->fDisconnect && state.vBlocksInFlight.size() > 0) {
            QueuedBlock &queuedBlock = state.vBlocksInFlight.front();
            int nOtherPeersWithValidatedDownloads = nPeersWithValidatedDownloads - (state.nBlocksInFlightValidHeaders > 0);
            if (nNow > state.nDownloadingSince + consensusParams.nPowTargetSpacing * (BLOCK_DOWNLOAD_TIMEOUT_BASE + BLOCK_DOWNLOAD_TIMEOUT_PER_PEER * nOtherPeersWithValidatedDownloads)) {
                LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", queuedBlock.hash.ToString(), pto->id);
                pto->fDisconnect = true;
            }
        }

        //
        // Message: getdata (blocks)
        //
        vector<CInv> vGetData;
        if (!pto->fDisconnect && !pto->fClient && (fFetch || !IsInitialBlockDownload()) && state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
            vector<CBlockIndex*> vToDownload;
            NodeId staller = -1;
            FindNextBlocksToDownload(pto->GetId(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - state.nBlocksInFlight, vToDownload, staller);
            BOOST_FOREACH(CBlockIndex *pindex, vToDownload) {
                vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), consensusParams, pindex);
                LogPrint("net", "Requesting block %s (%d) peer=%d\n", pindex->GetBlockHash().ToString(),
                    pindex->nHeight, pto->id);
            }
            if (state.nBlocksInFlight == 0 && staller != -1) {
                if (State(staller)->nStallingSince == 0) {
                    State(staller)->nStallingSince = nNow;
                    LogPrint("net", "Stall started peer=%d\n", staller);
                }
            }
        }

        //
        // Message: getdata (non-blocks)
        //
        int64_t nFirst = -1;
        if(!pto->mapAskFor.empty()) {
            nFirst = (*pto->mapAskFor.begin()).first;
        }
        LogPrint("net", "SendMessages (mapAskFor) -- before loop: nNow = %d, nFirst = %d\n", nNow, nFirst);
        while (!pto->fDisconnect && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            LogPrint("net", "SendMessages (mapAskFor) -- inv = %s peer=%d\n", inv.ToString(), pto->id);
            if (!AlreadyHave(inv))
            {
                if (fDebug)
                    LogPrint("net", "Requesting %s peer=%d\n", inv.ToString(), pto->id);
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage(NetMsgType::GETDATA, vGetData);
                    vGetData.clear();
                }
            } else {
                //If we're not going to ask, don't expect a response.
                LogPrint("net", "SendMessages -- already have inv = %s peer=%d\n", inv.ToString(), pto->id);
                pto->setAskFor.erase(inv.hash);
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage(NetMsgType::GETDATA, vGetData);

    }
    return true;
}

 std::string CBlockFileInfo::ToString() const {
     return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
 }

ThresholdState VersionBitsTipState(const Consensus::Params& params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsState(chainActive.Tip(), params, pos, versionbitscache);
}

std::string DefaultRecAddress(std::string sType)
{
	std::string sDefaultRecAddress = "";
	BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        std::string strName = item.second.name;
        bool fMine = IsMine(*pwalletMain, address.Get());
        if (fMine)
		{
		    sDefaultRecAddress=CBitcoinAddress(address).ToString();
			boost::to_upper(strName);
			boost::to_upper(sType);
			if (strName == sType) 
			{
				sDefaultRecAddress=CBitcoinAddress(address).ToString();
				return sDefaultRecAddress;
			}
		}
    }
	return sDefaultRecAddress;
}


void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive)
{
	f7000 = ((!fProd && nPrevHeight > 1) || (fProd && nPrevHeight > F7000_CUTOVER_HEIGHT)) ? true : false;
    f8000 = ((fMasternodesEnabled) && ((!fProd && nPrevHeight >= F8000_CUTOVER_HEIGHT_TESTNET) || (fProd && nPrevHeight >= F8000_CUTOVER_HEIGHT)));
	f9000 = ((!fProd && nPrevHeight >= F9000_CUTOVER_HEIGHT_TESTNET) || (fProd && nPrevHeight >= F9000_CUTOVER_HEIGHT));
	int nLastTitheBlock = fProd ? LAST_TITHE_BLOCK : LAST_TITHE_BLOCK_TESTNET;

    fTitheBlocksActive = ((nPrevHeight+1) < nLastTitheBlock);
}


CAmount GetRetirementAccountContributionAmount(int nPrevHeight)
{
	// Retirement coins are awarded to the miner who found the block as a colored coin in the coinbase in Transaction #1, vout Position #2.
	// The emission rate starts at 9900 colored coins per block and decreases by 1% per day
	if (!fRetirementAccountsEnabled) return 0;
	int iRetirementDecreaseInterval = BLOCKS_PER_DAY * 1; // Daily
	double iDeflationRate = .01; // 1% per Day
	CAmount nRetirementContributionAmount = 20000000 / BLOCKS_PER_DAY; // Colored Satoshi divided by blocks per day, decreasing by 1% per day
    for (int i = iRetirementDecreaseInterval; i <= nPrevHeight; i += iRetirementDecreaseInterval) 
	{
        nRetirementContributionAmount -= (nRetirementContributionAmount * iDeflationRate);
		if (nRetirementContributionAmount < 1) nRetirementContributionAmount = 1;
    }
	return nRetirementContributionAmount * RETIREMENT_COIN;
}


std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nMaxAge)
{
	// This allows us to disregard old cache messages
	std::string sValue = ReadCache(sSection, sKey);
	std::string sFullKey = sSection + ";" + sKey;
	boost::to_upper(sFullKey);
	int64_t nTime = mvApplicationCacheTimestamp[sFullKey];
	if (nTime==0)
	{
		// LogPrintf(" ReadCacheWithMaxAge key %s timestamp 0 \n",sFullKey);
	}
	int64_t nAge = GetAdjustedTime() - nTime;
	if (nAge > nMaxAge) return "";
	return sValue;
}


void ClearCache(std::string sSection)
{
	boost::to_upper(sSection);
	for(map<string,string>::iterator ii=mvApplicationCache.begin(); ii!=mvApplicationCache.end(); ++ii)
	{
		std::string sKey = (*ii).first;
		boost::to_upper(sKey);
		if (sKey.length() > sSection.length())
		{
			if (sKey.substr(0,sSection.length())==sSection)
			{
				mvApplicationCache[sKey]="";
				mvApplicationCacheTimestamp[sKey]=0;
			}
		}
	}
}



std::string ReadCache(std::string sSection, std::string sKey)
{
	boost::to_upper(sSection);
	boost::to_upper(sKey);
	
	if (sSection.empty() || sKey.empty()) return "";
	try
	{
		std::string sValue = mvApplicationCache[sSection + ";" + sKey];
		if (sValue.empty())
		{
			mvApplicationCache.insert(map<std::string,std::string>::value_type(sSection + ";" + sKey,""));
			mvApplicationCache[sSection + ";" + sKey]="";
			return "";
		}
		return sValue;
	}
	catch(...)
	{
		LogPrintf("ReadCache error %s",sSection.c_str());
		return "";
	}
}



class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();

        // orphan transactions
        mapOrphanTransactions.clear();
        mapOrphanTransactionsByPrev.clear();
    }
} instance_of_cmaincleanup;
