// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include "kjv.h"
#include <math.h>
#include <openssl/crypto.h>

extern bool LogLimiter(int iMax1000);
uint256 BibleHash(uint256 hash, int64_t nBlockTime, int64_t nPrevBlockTime, bool bMining, int nPrevHeight, const CBlockIndex* pindexLast, bool bRequireTxIndex, 
	bool f7000, bool f8000, bool f9000, bool fTitheBlocksActive, unsigned int nNonce);
std::string RoundToString(double d, int place);
void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive);
extern bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime);



unsigned int static KimotoGravityWell(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const CBlockIndex *BlockLastSolved = pindexLast;
    const CBlockIndex *BlockReading = pindexLast;
    uint64_t PastBlocksMass = 0;
    int64_t PastRateActualSeconds = 0;
    int64_t PastRateTargetSeconds = 0;
    double PastRateAdjustmentRatio = double(1);
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
    double EventHorizonDeviation;
    double EventHorizonDeviationFast;
    double EventHorizonDeviationSlow;

    uint64_t pastSecondsMin = params.nPowTargetTimespan * 0.025;
    uint64_t pastSecondsMax = params.nPowTargetTimespan * 7;
    uint64_t PastBlocksMin = pastSecondsMin / params.nPowTargetSpacing;
    uint64_t PastBlocksMax = pastSecondsMax / params.nPowTargetSpacing;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) { return UintToArith256(params.powLimit).GetCompact(); }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        PastBlocksMass++;

        PastDifficultyAverage.SetCompact(BlockReading->nBits);
        if (i > 1) {
            // handle negative arith_uint256
            if(PastDifficultyAverage >= PastDifficultyAveragePrev)
                PastDifficultyAverage = ((PastDifficultyAverage - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev;
            else
                PastDifficultyAverage = PastDifficultyAveragePrev - ((PastDifficultyAveragePrev - PastDifficultyAverage) / i);
        }
        PastDifficultyAveragePrev = PastDifficultyAverage;

        PastRateActualSeconds = BlockLastSolved->GetBlockTime() - BlockReading->GetBlockTime();
        PastRateTargetSeconds = params.nPowTargetSpacing * PastBlocksMass;
        PastRateAdjustmentRatio = double(1);
        if (PastRateActualSeconds < 0) { PastRateActualSeconds = 0; }
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
            PastRateAdjustmentRatio = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
        }
        EventHorizonDeviation = 1 + (0.7084 * pow((double(PastBlocksMass)/double(28.2)), -1.228));
        EventHorizonDeviationFast = EventHorizonDeviation;
        EventHorizonDeviationSlow = 1 / EventHorizonDeviation;

        if (PastBlocksMass >= PastBlocksMin) {
                if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast))
                { assert(BlockReading); break; }
        }
        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        bnNew *= PastRateActualSeconds;
        bnNew /= PastRateTargetSeconds;
    }

    if (bnNew > UintToArith256(params.powLimit)) {
        bnNew = UintToArith256(params.powLimit);
    }

    return bnNew.GetCompact();
}



bool LogLimiter(int iMax1000)
{
	 //The lower the level, the less logged
	 int iVerbosityLevel = rand() % 1000;
	 if (iVerbosityLevel < iMax1000) return true;
	 return false;
}


unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params) 
{
    /* current difficulty formula, Biblepay - DarkGravity v3, written by Evan Duffield */
    const CBlockIndex *BlockLastSolved = pindexLast;
    const CBlockIndex *BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 4; 
    int64_t PastBlocksMax = 4; 
    int64_t CountBlocks = 0;
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
	bool fProdChain = Params().NetworkIDString() == "main" ? true : false;

	// BiblePay - Mandatory Upgrade at block f7000 (As of 08-15-2017 we are @3265 in prod & @1349 in testnet)
	// This change should prevents blocks from being solved in clumps
	if (pindexLast)
	{
		if ((!fProdChain && pindexLast->nHeight >= 1) || (fProdChain && pindexLast->nHeight >= 7000))
		{
			PastBlocksMin = 4;
			PastBlocksMax = 4;
		}
	}

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) 
	{
        return UintToArith256(params.powLimit).GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) 
	{
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        CountBlocks++;

        if(CountBlocks <= PastBlocksMin) 
		{
            if (CountBlocks == 1) { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
              else { PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (arith_uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1); }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if(LastBlockTime > 0)
		{
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);

    int64_t _nTargetTimespan = CountBlocks * params.nPowTargetSpacing; 

    if (nActualTimespan < _nTargetTimespan/2)
        nActualTimespan = _nTargetTimespan/2;

    if (nActualTimespan > _nTargetTimespan*2)
        nActualTimespan = _nTargetTimespan*2;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > UintToArith256(params.powLimit))
	{
        bnNew = UintToArith256(params.powLimit);
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    unsigned int retarget = DIFF_DGW;
	
    // Default Bitcoin style retargeting
    if (retarget == DIFF_BTC)
    {
        unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
        // Genesis block
        if (pindexLast == NULL)
            return nProofOfWorkLimit;
        // Only change once per interval
        if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
        {
            if (params.fPowAllowMinDifficultyBlocks)
            {
                // Special difficulty rule for testnet:
                // If the new block's timestamp is more than 2* 2.5 minutes
                // then allow mining of a min-difficulty block.
                if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                    return nProofOfWorkLimit;
                else
                {
                    // Return the last non-special-min-difficulty-rules-block
                    const CBlockIndex* pindex = pindexLast;
                    while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                        pindex = pindex->pprev;
                    return pindex->nBits;
                }
            }
            return pindexLast->nBits;
        }

        // Go back by what we want to be 1 day worth of blocks
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        assert(nHeightFirst >= 0);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        assert(pindexFirst);
       return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
    }

    // Retarget using Kimoto Gravity Wave
    else if (retarget == DIFF_KGW)
    {
        return KimotoGravityWell(pindexLast, params);
    }

    // Retarget using Dark Gravity Wave 3
    else if (retarget == DIFF_DGW)
    {
        return DarkGravityWave(pindexLast, params);
    }
    return DarkGravityWave(pindexLast, params);
}

// for DIFF_BTC only!
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = (pindexLast->GetBlockTime() - nFirstBlockTime);
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    if (nActualTimespan < params.nPowTargetTimespan/2)
        nActualTimespan = params.nPowTargetTimespan/2;
    if (nActualTimespan > params.nPowTargetTimespan*2)
        nActualTimespan = params.nPowTargetTimespan*2;
    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;
    if (bnNew > bnPowLimit)
	{
		LogPrintf("GetNextWork bNnew %08x > POW LIMIT  %08x \r\n",bnNew.GetCompact(),bnPowLimit.GetCompact());
        bnNew = bnPowLimit;
	}
    /// debug print
    LogPrintf("GetNextWorkRequired RETARGET\n");
    LogPrintf("params.nPowTargetTimespan = %d    nActualTimespan = %d\n", params.nPowTargetTimespan, nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());
    return bnNew.GetCompact();
}

bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime)
{
	if (f9000)
	{
		int64_t nElapsed = nBlockTime - nPrevBlockTime;
		if (nElapsed > (30 * 60)) return true;
		int64_t nMaxNonce = nElapsed * 256;
		if (nMaxNonce < 512) nMaxNonce = 512;
		return (nNonce > nMaxNonce) ? false : true;
	}
	else
	{
		return true;
	}
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params, 
	int64_t nBlockTime, int64_t nPrevBlockTime, int nPrevHeight, unsigned int nNonce, const CBlockIndex* pindexPrev, bool bLoadingBlockIndex)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

	bool fProdChain = Params().NetworkIDString() == "main" ? true : false;
	bool bRequireTxIndexLookup = false;  // This is a project currently in TestNet, slated to go live after Sanctuaries are enabled and fully tested by slack team

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return error("CheckProofOfWork(): nBits below minimum work");
	
	// Check proof of work matches claimed amount
	bool f7000 = ((!fProdChain && nPrevHeight >= 1) || (fProdChain && nPrevHeight >= 7000)) ? true : false;

	bool f_7000;
	bool f_8000;
	bool f_9000; 
	bool fTitheBlocksActive;
	GetMiningParams(nPrevHeight, f_7000, f_8000, f_9000, fTitheBlocksActive);


    if (!f7000 && !f_8000)
	{
		bool bSecurityPass = ((bLoadingBlockIndex && nPrevHeight==0) || nPrevHeight == 0) ? true : false;
		if (UintToArith256(hash) > bnTarget && !bSecurityPass) 
		{
			return error("\r\nCheckProofOfWork(): hash doesn't meet X11 POW Level, Prod %f, Network %s, PrevHeight %f \r\n", 
				(double)fProdChain, Params().NetworkIDString().c_str(), nPrevHeight);
		}
	}
	
	if (f7000 || f_8000)
	{
		uint256 uBibleHash = BibleHash(hash, nBlockTime, nPrevBlockTime, true, nPrevHeight, NULL, false, f_7000, f_8000, f_9000, fTitheBlocksActive, nNonce);
		if (UintToArith256(uBibleHash) > bnTarget)
		{
			uint256 uBibleHash2 = BibleHash(hash, nBlockTime, nPrevBlockTime, true, nPrevHeight, NULL, false, f_7000, f_8000, f_9000, fTitheBlocksActive, nNonce);
			if (UintToArith256(uBibleHash2) > bnTarget)
			{
				uint256 uTarget = ArithToUint256(bnTarget);
				// Biblehash forensic RPC command requires : blockhash, blocktime, prevblocktime and prevheight, IE: ëxec biblehash blockhash 12345 12234 100.
				std::string sForensic = "exec biblehash " + hash.GetHex() + " " + RoundToString(nBlockTime,0) + " " + RoundToString(nPrevBlockTime,0) 
					+ " " + RoundToString(nPrevHeight,0);
				//11-14-2017 Add Forensics: Alex873434
				uint256 h1 = (pindexPrev != NULL) ? pindexPrev->GetBlockHash() : uint256S("0x0");
				std::string sProd = fProdChain ? "TRUE" : "FALSE";
				std::string sSSL(SSLeay_version(SSLEAY_VERSION));
				LogPrintf("CheckProofOfWork(1.0): BlockHash %s, ProdChain %s, SSLVersion %s, BlockTime %f, PrevBlockTime %f, BibleHash1 %s, BibleHash2 %s, TargetHash %s, Forensics %s \n",
					hash.GetHex().c_str(), sProd.c_str(), sSSL.c_str(),  
					(double)nBlockTime,(double)nPrevBlockTime, uBibleHash.GetHex().c_str(), uBibleHash2.GetHex().c_str(), 
					uTarget.GetHex().c_str(), sForensic.c_str());
				return error("CheckProofOfWork(1): BibleHash does not meet POW level, prevheight %f pindexPrev %s ",(double)nPrevHeight,h1.GetHex().c_str());
			}
		}
	}
	
	if (f_9000)
	{
		bool fNonce = CheckNonce(f_9000, nNonce, nPrevHeight, nPrevBlockTime, nBlockTime);
		if (!fNonce)
		{
			return error("CheckProofOfWork: ERROR: High Nonce, PrevTime %f, Time %f, Nonce %f ",(double)nPrevBlockTime, (double)nBlockTime, (double)nNonce);
		}
	}

	if (f7000 && !bLoadingBlockIndex && bRequireTxIndexLookup)
	{
		if	(UintToArith256(BibleHash(hash, nBlockTime, nPrevBlockTime, true, nPrevHeight, pindexPrev, true, f_7000, f_8000, f_9000, fTitheBlocksActive, nNonce)) > bnTarget)
		{
			uint256 h2 = (pindexPrev != NULL) ? pindexPrev->GetBlockHash() : uint256S("0x0");
			return error("CheckProofOfWork(2): BibleHash does not meet POW level with TxIndex Lookup, prevheight %f pindexPrev %s ",(double)nPrevHeight,h2.GetHex().c_str());
		}
	}

    return true;
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}
