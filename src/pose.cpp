#include "pose.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "masternode-sync.h"


std::string GetSancStatus(const CDeterministicMNList& dmnList, const CDeterministicMNCPtr& dmn)
{
	if (dmnList.IsMNValid(dmn)) 
		return "ENABLED";
    if (dmnList.IsMNPoSeBanned(dmn)) 
		return "POSE_BANNED";
    return "UNKNOWN";
};

double CalculateScore(POSEScore p)
{
	if (p.nTries <= 0) 
		return 1;
	double nResult = p.nSuccess / p.nTries;
	return nResult;
}

POSEScore GetPOSEScore(std::string sNode)
{
	POSEScore p = mvPOSEScore[sNode];
	p.nScore = CalculateScore(p);
	mvPOSEScore[sNode] = p;
	return p;
}

void AdjustScore(std::string sNode, int iScore)
{
	POSEScore p = mvPOSEScore[sNode];
	p.nSuccess += iScore;
	p.nTries++;
	p.nScore = CalculateScore(p);
	p.nLastTried = GetAdjustedTime();
	mvPOSEScore[sNode] = p;
}

bool VerifyNode(std::string sNode)
{
	std::vector<CNodeStats> vstats;
	g_connman->GetNodeStats(vstats);
	BOOST_FOREACH(const CNodeStats& stats, vstats) 
	{
		if (stats.addr.ToString() == sNode)
		{
			if (stats.nVersion >= MIN_PEER_PROTO_VERSION)
			{
				AdjustScore(sNode, 1);
				return true;
			}
 		}
	}
	AdjustScore(sNode, 0);
	return false;
}

void ThreadPOSE(CConnman& connman)
{
	static int POSE_BATCH_SIZE = 10;
	MilliSleep(60 * 1000);
	int64_t nLastReset = 0;

	while (1 == 1)
	{
	    if (ShutdownRequested())
			return;

		bool bImpossible = (!masternodeSync.IsSynced() || fLiteMode || (!fMasternodeMode));
		if (bImpossible)
		{
			MilliSleep(60000);
		}
		else
		{
			try
			{
				int64_t nElapsed = GetAdjustedTime() - nLastReset;
				if (nElapsed > (60 * 60 * 24))
				{
					// Reset once per day to give everyone a fair chance to make up for failures.
					mvPOSEScore.clear();
					nLastReset = GetAdjustedTime();
				}
				auto mnList = deterministicMNManager->GetListAtChainTip();
				int iLocator = 0;
				int iSancCount = mnList.GetAllMNsCount();
				int iStartingPoint = GetRandInt(iSancCount);
				int iProcessed = 0;
				// Connect, check version, stay connected and test version
				mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) 
				{
					if (iLocator >= iStartingPoint && iProcessed < POSE_BATCH_SIZE)
					{
						CAddress addr(CService(), NODE_NONE);
						g_connman->OpenNetworkConnection(addr, false, NULL, dmn->pdmnState->addr.ToString().c_str());
						iProcessed++;
						MilliSleep(1000);
					}
					iLocator++;
				});
				iLocator = 0;
				iProcessed = 0;
				MilliSleep(10000);
				// Now verify the protocol version
				mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) 
				{
					if (iLocator >= iStartingPoint && iProcessed < POSE_BATCH_SIZE)
					{
						std::string strOutpoint = dmn->collateralOutpoint.ToStringShort();
						std::string sStatus = GetSancStatus(mnList, dmn);
						bool bVerifyNode = VerifyNode(dmn->pdmnState->addr.ToString().c_str());
						LogPrintf("\nPOSE::VerifyNode::Masternode outpoint %s, status %s, Address %s, Verified %f ", 
							strOutpoint, sStatus, dmn->pdmnState->addr.ToString(), (double)bVerifyNode);
						iProcessed++;
					}
					iLocator++;
				});
			}
			catch(...)
			{
				LogPrintf("Error encountered in POSE main loop. %f \n", 0);
			}
			int iSleeper = fProd ? 10 : 3;
			MilliSleep(iSleeper * 60000);
		}
	}
}


