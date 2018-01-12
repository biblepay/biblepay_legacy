// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "kjv.h"
#include "consensus/merkle.h"
#include "main.h"
#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include <assert.h>
#include <boost/assign/list_of.hpp>
#include "chainparamsseeds.h"
#include "init.h"

void CheckGenesisBlock(CBlock block, uint256 targetBlockHash, uint256 targetMerkleRoot);



/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */



class CMainParams : public CChainParams {
public:
	
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = BLOCKS_PER_DAY * 365; // We produce approx 74,825 blocks per year (205 per day)
		consensus.nInstantSendKeepLock = 7;
        consensus.nMasternodePaymentsIncreasePeriod = BLOCKS_PER_DAY * 30; // One month
        
        consensus.nMasternodePaymentsStartBlock = 21600; // Must be less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 21601; 
        
		consensus.nBudgetPaymentsStartBlock = 21551; // This is when we change our block distribution to include PR, P2P, IT expenses
		consensus.nRuleChangeActivationThreshold = 21600; // Same as Masternode Payments Start block
      
        consensus.nBudgetPaymentsCycleBlocks = BLOCKS_PER_DAY * 30; // Monthly
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nBudgetProposalEstablishingTime = 60*60*24;  // One Day

        consensus.nSuperblockStartBlock = 21710; // The first superblock
        consensus.nSuperblockCycle = BLOCKS_PER_DAY * 30; // Monthly
        consensus.nGovernanceMinQuorum = 10;
        
		consensus.nGovernanceFilterElements = 20000;
        consensus.nMasternodeMinimumConfirmations = 7;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x0000039f2a2d9bc85cb7b5e34b5221bf3e840d142a400cb9f3cf1850dcc7a9cc");
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
		consensus.FoundationAddress = "BB2BwSbDCqCqNsfc7FgWFJn4sRgnUt4tsM";
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Biblepay: 1 day
        consensus.nPowTargetSpacing = 7 * 60; // Biblepay: 7 minutes
		
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nMinerConfirmationWindow = 205; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1486252800; // Feb 5th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1517788800; // Feb 5th, 2018

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xbf;
        pchMessageStart[1] = 0x0c;
        pchMessageStart[2] = 0x6b;
        pchMessageStart[3] = 0xbd;
	
        vAlertPubKey = ParseHex("0x0432cb0fcd551eb206e80fa59cc7fd4038923ef3416f3e132637b8e9a4bcd2f8c9e6c4ee9f01c6f3cea7fb5d85b65b7c18ba30e6c133242bf0de488dd839c36001");
        nDefaultPort = 40000;
		// Default BiblePay Port=40000, TestNet = 40001, RPC=Set_by_user

        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nPruneAfterHeight = 100000;
		      
	    vSeeds.push_back(CDNSSeedData("biblepay.org", "dnsseed.biblepay.org"));
        vSeeds.push_back(CDNSSeedData("biblepay.org", "node.biblepay.org"));
		vSeeds.push_back(CDNSSeedData("biblepay.org", "dnsseed.biblepay-explorer.org"));

        // Biblepay addresses starts with 'B'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,25);
        // Biblepay script addresses starts with '7'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,16);
        // Biblepay private keys starts with '7' or 'B'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,182);
        // Biblepay BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        // Biblepay BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();
        // Biblepay BIP44 coin type is '5'
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x80)(0x00)(0x00)(0x05).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "029ce47108ee0ac9212becfad36f6fd519f1264d1c274b2da0c452d959140fe8a6";
        strMasternodePaymentsPubKey = "029ce47108ee0ac9212becfad36f6fd519f1264d1c274b2da0c452d959140fe8a6";
	    checkpointData = (CCheckpointData) 
		{
            boost::assign::map_list_of
            (   7, uint256S("0x00022b1be28b1deb9a51d4d69f3fa393f4ea36621039b6313a6c0796546621de"))
			( 120, uint256S("0x00002fc6c9e4889a8d1a9bd5919a6bd4a4b09091e55049480509da14571e5653"))
			(6999, uint256S("0x000000dfbcdec4e6b0ab899f04d7ce8e4d8bc8a725a47169b626acd207ccea8d"))
			(18900,uint256S("0x94a1ff5e84a31219d5472536215f5a77b00cfd61f3fb99d0e9d3ab392f2ed2a6"))
			(20900,uint256S("0x23d0b5887ca89fc2dddb2f34810675cb1826371172a91b1211be4677fd260490"))
			(21650,uint256S("0x756e18f6a20d02d7af0a32c5705960d58adc4daba24c6a7dd9a8b80776bcca73"))
			(21960,uint256S("0xdd7e0acd7b9569b6fbf84a8262bb5fe3ea28af259f12d060acbcd62d4241fb51"))
			,
            1513776131,     // * UNIX timestamp of last checkpoint block
            37662,          // * total number of transactions between genesis and last checkpoint
                            //   (the tx=... number in the SetBestChain debug.log lines)
            100             // * estimated number of transactions per day after checkpoint
        };
		
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
		// TestNet - fSantuary feature must disable tithe block Before nMasternodePaymentsStartBlock
        consensus.nSubsidyHalvingInterval = 365 * BLOCKS_PER_DAY;
        consensus.nMasternodePaymentsStartBlock = 1201; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 1201;
        consensus.nMasternodePaymentsIncreasePeriod = BLOCKS_PER_DAY*30;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1199;
		consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
    	consensus.nRuleChangeActivationThreshold = 1201; // 75% for testchains
	    consensus.nSuperblockStartBlock = 1201; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPaymentsStartBlock
        consensus.nBudgetProposalEstablishingTime = 60*20; //Possibly 8 in the future
		consensus.FoundationAddress = "yadZnJ3hD3FRC8CiLZEVNqejvQFgNtu5ci";
        consensus.nSuperblockCycle = 25; // Superblocks can be issued hourly on testnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
		consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Biblepay: 1 day
        consensus.nPowTargetSpacing = 1 * 60; // Biblepay: 1 minute
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nMinerConfirmationWindow = 205; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        pchMessageStart[0] = 0xce;
        pchMessageStart[1] = 0xe2;
        pchMessageStart[2] = 0xca;
        pchMessageStart[3] = 0xff;
        vAlertPubKey = ParseHex("0x0432cb0fcd551eb206e80fa59cc7fd4038923ef3416f3e132637b8e9a4bcd2f8c9e6c4ee9f01c6f3cea7fb5d85b65b7c18ba30e6c133242bf0de488dd839c36001");

        nDefaultPort = 40001;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet
        nPruneAfterHeight = 1000;
	
        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("biblepaydot.io",  "testnet-seed.biblepaydot.io"));
        vSeeds.push_back(CDNSSeedData("masternode.io", "test.dnsseed.masternode.io"));

        // Testnet Biblepay addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Testnet Biblepay script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet Biblepay BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Testnet Biblepay BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Testnet Biblepay BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x80)(0x00)(0x00)(0x01).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "029ce47108ee0ac9212becfad36f6fd519f1264d1c274b2da0c452d959140fe8a6";
        strMasternodePaymentsPubKey = "029ce47108ee0ac9212becfad36f6fd519f1264d1c274b2da0c452d959140fe8a6";

		/*
        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (    1, uint256S("0x18b37b60b422ea27d57ceea9dd794b5f74c561565ecc03e85a22ecdf74cbb33a"))
            ( 25000, uint256S("0xa54146e7421afcea13626f59ceebe81aa8885edf25043fc642104333f18a7f50"))
			( 54070, uint256S("0xc10e114f66c6ab29b0b6296e0c86dec482dfded0deb66ff678775fb99e68828e")),
            1511964848, // * UNIX timestamp of last checkpoint block
            54664,     // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            500         // * estimated number of transactions per day after checkpoint
        };
		*/

		
		
		
		checkpointData = (CCheckpointData) 
		{
            boost::assign::map_list_of
            (    1, uint256S("0x18b37b60b422ea27d57ceea9dd794b5f74c561565ecc03e85a22ecdf74cbb33a"))
       		,
            1511964848, // * UNIX timestamp of last checkpoint block
            54664,     // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            500         // * estimated number of transactions per day after checkpoint
        };
	

    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 74825;
        consensus.nMasternodePaymentsStartBlock = 240;
        consensus.nMasternodePaymentsIncreaseBlock = 350;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 1500;
        consensus.nSuperblockCycle = 10;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 1; 
        consensus.BIP34Hash = uint256();
		consensus.FoundationAddress = "yedEtKvuw9CMBKdzGNVM1ezzMMy2ibtXDR";

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Biblepay: 1 day
        consensus.nPowTargetSpacing = 7 * 60; // Biblepay: 7 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 205; 
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
		
        pchMessageStart[0] = 0xfc;
        pchMessageStart[1] = 0xc1;
        pchMessageStart[2] = 0xb7;
        pchMessageStart[3] = 0xdc;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDefaultPort = 40002;
        nPruneAfterHeight = 1000;

        //blkGenesis = const_cast<CBlock&>(cblockGenesis);
        
        

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, uint256S("0x0")),
            0,
            0,
            0
        };
        // Regtest Biblepay addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Regtest Biblepay script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest Biblepay BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Regtest Biblepay BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Regtest Biblepay BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x80)(0x00)(0x00)(0x01).convert_to_container<std::vector<unsigned char> >();
   }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}
