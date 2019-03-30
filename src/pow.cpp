// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"
#include "kjv.h"
#include "validation.h"
#include "arith_uint256.h"
#include "chain.h"
#include "rpcpog.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"

#include <math.h>

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

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params) {
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
	// 3-4-2018 Biblepay:  Change params.nPowTargetSpacing to equal 7 minute blocks during next mandatory, the default calculation calls for 7*60=420 nPowTargetSpacing
	// Approaching this mathematically, with POBH, we have historically emitted 159 blocks per day out of 202, a rate of 20% below normal - a rate that is too slow.
	// The dev team believes this is attributed entirely to the late block threshhold parameter.  After the F11000 cutover block, this late threshhold is being reduced
	// from 30 minutes to 16 minutes, with 16 minutes bringing the blocks from 9 minute spacing to 8.1 minute spacing (meaning we are still going to be slow by 60.1 seconds)
	// Therefore we must reduce the nPowTargetSpacing by 10% more to compensate for the difference.  Shooting for 7 minute blocks, we now adjust nPowTargetSpacing to 390
	// In summary we are making the assumption that the gained 30 seconds per block target will equalize the effect our late blocks per day lag the chain.
	// As of March 9th 2018, we are still emitting blocks 19% too slow, decreasing time interval to 315 
	if (pindexLast->nHeight >= params.F11000_CUTOVER_HEIGHT && pindexLast->nHeight < params.F12000_CUTOVER_HEIGHT)
	{
		_nTargetTimespan = CountBlocks * 390;
	}
	else if (pindexLast->nHeight >= params.F12000_CUTOVER_HEIGHT && pindexLast->nHeight < params.F13000_CUTOVER_HEIGHT)
	{
		_nTargetTimespan = CountBlocks * 310;
	}
	else if (pindexLast->nHeight >= params.F13000_CUTOVER_HEIGHT)
	{
		// 5-31-2018; For June mandatory upgrade: Tighten block solve time down to 7 mins (we are running 4.5% slow)
		_nTargetTimespan = CountBlocks * 296;
	}
	
	if (!fProdChain)
	{
		if (pindexLast->nHeight < 250)
		{
			_nTargetTimespan = CountBlocks; // One second blocks in testnet
		}
		else if (pindexLast->nHeight >= 250 && pindexLast->nHeight < 5000)
		{
			_nTargetTimespan = 30; // 30 second blocks 
		}
		else if (pindexLast->nHeight >= 5000)
		{
			_nTargetTimespan = 120;  // 2 minute blocks in testnet
		}
	}

	// if (fDebugMaster) LogPrintf(" Height: %f, targettimespan %f  nActualTimespan %f ",(double)pindexLast->nHeight, (double)_nTargetTimespan, (double)nActualTimespan);

    if (nActualTimespan < _nTargetTimespan / 2)
        nActualTimespan = _nTargetTimespan / 2;

    if (nActualTimespan > _nTargetTimespan * 2)
        nActualTimespan = _nTargetTimespan * 2;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > UintToArith256(params.powLimit))
	{
        bnNew = UintToArith256(params.powLimit);
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
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

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
	// BiblePay only uses DGW
	return DarkGravityWave(pindexLast, pblock, params);
   
    // this is only active on devnets
    if (pindexLast->nHeight < params.nMinimumDifficultyBlocks) {
        unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
        return nProofOfWorkLimit;
    }

    // Most recent algo first
    if (pindexLast->nHeight + 1 >= params.nPowDGWHeight) {
        return DarkGravityWave(pindexLast, pblock, params);
    }
    else if (pindexLast->nHeight + 1 >= params.nPowKGWHeight) {
        return KimotoGravityWell(pindexLast, params);
    }
    else {
        return GetNextWorkRequiredBTC(pindexLast, pblock, params);
    }
}

// for DIFF_BTC only!
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params, 
	int64_t nBlockTime, int64_t nPrevBlockTime, int nPrevHeight, unsigned int nNonce, const CBlockIndex* pindexPrev, bool bLoadingBlockIndex)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

	if (nPrevHeight < params.EVOLUTION_CUTOVER_HEIGHT)
	{
		bool f_7000;
		bool f_8000;
		bool f_9000; 
		bool fTitheBlocksActive;
		GetMiningParams(nPrevHeight, f_7000, f_8000, f_9000, fTitheBlocksActive);
		uint256 uBibleHashClassic = BibleHashClassic(hash, nBlockTime, nPrevBlockTime, true, nPrevHeight, NULL, false, f_7000, f_8000, f_9000, fTitheBlocksActive, nNonce, params);
		if (UintToArith256(uBibleHashClassic) > bnTarget && nPrevBlockTime > 0) 
		{
			return false;
		}
	}
	else
	{
		uint256 uBibleHash = BibleHashV2(hash, nBlockTime, nPrevBlockTime, true, nPrevHeight);
		if (UintToArith256(uBibleHash) > bnTarget && nPrevBlockTime > 0) 
		{
			return false;
		}
	}
	
    return true;
}
