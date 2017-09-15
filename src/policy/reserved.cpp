
std::string RetrieveTxOutRecipientAddress(const CBlockIndex* pindex, int iLookback, int iTxOffset, int ivOutOffset)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
	//	chainparams.GetConsensus()
	//if (block.GetHash() == chainparams.GetConsensus().hash) {
    if (pindex==NULL) return "";
	for (int i = 0; i <= iLookback; i++)
	{
		if (pindex->pprev) pindex = pindex->pprev;
		if (pindex->GetBlockHash()==consensusParams.hashgen) break;
	}
		
	CBlock block;
	if (ReadBlockFromDisk(block, pindex, consensusParams, "RETRIEVETXOUTRECIPIENTADDRESS"))
	{
		if (iTxOffset > (int)block.vtx.size()) iTxOffset=block.vtx.size()-1;
		if (ivOutOffset > (int)block.vtx[iTxOffset].vout.size()) ivOutOffset=block.vtx[iTxOffset].vout.size()-1;
		if (iTxOffset >= 0 && ivOutOffset >= 0)
		{
   			std::string sAddress = PubKeyToAddress(block.vtx[iTxOffset].vout[iTxOffset].scriptPubKey);
			//LogPrintf(" \r\n Txoffset %f, voffset %f, address %s ",(double)iTxOffset,(double)ivOutOffset,sAddress.c_str());
			return sAddress;
		}
	}
	
	return "";
}

