// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"

uint256 CBlockHeader::GetHash() const
{
    return HashX11(BEGIN(nVersion), END(nNonce));
}

uint256 CBlockHeader::GetHashBible() const
{
	return HashBiblePay(BEGIN(nVersion),END(nNonce));
}

uint256 CTitheObject::GetHash() const
{
	return uint256S("0x0");
	/*
	std::string sKey = Address + RoundToString(Amount, 4) + RoundToString(Height, 0) + RoundToString(HeightLast, 0) + LastBlockHash.GetHex();
		std::string sHash = RetrieveMd5(sKey);
		uint256 hash = uint256S("0x" + sHash);
		return hash;
		*/
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    return s.str();
}
