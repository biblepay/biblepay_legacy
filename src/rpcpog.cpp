// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcpog.h"
#include "spork.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpodc.h"
#include "init.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "governance-classes.h"
#include "governance.h"
#include "masternode-sync.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "messagesigner.h"
#include "smartcontract-server.h"
#include "smartcontract-client.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <math.h>       /* round, floor, ceil, trunc */
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <openssl/md5.h>
// For HTTPS (for the pool communication)
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <boost/asio.hpp>
#include "net.h" // for CService
#include "netaddress.h"
#include "netbase.h" // for LookupHost
#include "wallet/wallet.h"
#include <sstream>

#ifdef ENABLE_WALLET
extern CWallet* pwalletMain;
#endif // ENABLE_WALLET


std::string GenerateNewAddress(std::string& sError, std::string sName)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
	{
		if (!pwalletMain->IsLocked(true))
			pwalletMain->TopUpKeyPool();
		// Generate a new key that is added to wallet
		CPubKey newKey;
		if (!pwalletMain->GetKeyFromPool(newKey, false))
		{
			sError = "Keypool ran out, please call keypoolrefill first";
			return std::string();
		}
		CKeyID keyID = newKey.GetID();
		pwalletMain->SetAddressBook(keyID, sName, "receive"); //receive == visible in address book, hidden = non-visible
		LogPrintf(" created new address %s ", CBitcoinAddress(keyID).ToString().c_str());
		return CBitcoinAddress(keyID).ToString();
	}
}

std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue)
{
	// This is a helper for the Governance gobject create method
	std::string sQ = "\"";
	std::string sOut = sQ + sKey + sQ + ":";
	if (bQuoteValue)
	{
		sOut += sQ + sValue + sQ;
	}
	else
	{
		sOut += sValue;
	}
	if (bIncludeDelimiter) sOut += ",";
	return sOut;
}

std::string RoundToString(double d, int place)
{
	std::ostringstream ss;
    ss << std::fixed << std::setprecision(place) << d ;
    return ss.str() ;
}

double Round(double d, int place)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(place) << d ;
	double r = 0;
	try
	{
		r = boost::lexical_cast<double>(ss.str());
		return r;
	}
	catch(boost::bad_lexical_cast const& e)
	{
		LogPrintf("caught bad lexical cast %f", 1);
		return 0;
	}
	catch(...)
	{
		LogPrintf("caught bad lexical cast %f", 2);
	}
	return r;
}

std::vector<std::string> Split(std::string s, std::string delim)
{
	size_t pos = 0;
	std::string token;
	std::vector<std::string> elems;
	while ((pos = s.find(delim)) != std::string::npos)
	{
		token = s.substr(0, pos);
		elems.push_back(token);
		s.erase(0, pos + delim.length());
	}
	elems.push_back(s);
	return elems;
}

double cdbl(std::string s, int place)
{
	if (s=="") s = "0";
	if (s.length() > 255) return 0;
	s = strReplace(s, "\r","");
	s = strReplace(s, "\n","");
	std::string t = "";
	for (int i = 0; i < (int)s.length(); i++)
	{
		std::string u = s.substr(i,1);
		if (u=="0" || u=="1" || u=="2" || u=="3" || u=="4" || u=="5" || u=="6" || u == "7" || u=="8" || u=="9" || u=="." || u=="-") 
		{
			t += u;
		}
	}
	double r= 0;
	try
	{
	    r = boost::lexical_cast<double>(t);
	}
	catch(boost::bad_lexical_cast const& e)
	{
		LogPrintf("caught cdbl bad lexical cast %f from %s with %f", 1, s, (double)place);
		return 0;
	}
	catch(...)
	{
		LogPrintf("caught cdbl bad lexical cast %f", 2);
	}
	double d = Round(r, place);
	return d;
}

bool Contains(std::string data, std::string instring)
{
	std::size_t found = 0;
	found = data.find(instring);
	if (found != std::string::npos) return true;
	return false;
}

std::string GetElement(std::string sIn, std::string sDelimiter, int iPos)
{
	if (sIn.empty())
		return std::string();
	std::vector<std::string> vInput = Split(sIn.c_str(), sDelimiter);
	if (iPos < (int)vInput.size())
	{
		return vInput[iPos];
	}
	return std::string();
}

std::string GetSporkValue(std::string sKey)
{
	boost::to_upper(sKey);
    std::pair<std::string, int64_t> v = mvApplicationCache[std::make_pair("SPORK", sKey)];
	return v.first;
}

double GetSporkDouble(std::string sName, double nDefault)
{
	double dSetting = cdbl(GetSporkValue(sName), 2);
	if (dSetting == 0) return nDefault;
	return dSetting;
}

std::map<std::string, std::string> GetSporkMap(std::string sPrimaryKey, std::string sSecondaryKey)
{
	boost::to_upper(sPrimaryKey);
	boost::to_upper(sSecondaryKey);
	std::string sDelimiter = "|";
    std::pair<std::string, int64_t> v = mvApplicationCache[std::make_pair(sPrimaryKey, sSecondaryKey)];
	std::vector<std::string> vSporks = Split(v.first, sDelimiter);
	std::map<std::string, std::string> mSporkMap;
	for (int i = 0; i < vSporks.size(); i++)
	{
		std::string sMySpork = vSporks[i];
		if (!sMySpork.empty())
			mSporkMap.insert(std::make_pair(sMySpork, RoundToString(i, 0)));
	}
	return mSporkMap;
}

std::string Left(std::string sSource, int bytes)
{
	if (sSource.length() >= bytes)
	{
		return sSource.substr(0, bytes);
	}
	return std::string();
}	

bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError)
{
	CBitcoinAddress addr2(sBitcoinAddress);
	if (!addr2.IsValid()) 
	{
		strError = "Invalid address";
		return false;
	}
	CKeyID keyID2;
	if (!addr2.GetKeyID(keyID2)) 
	{
		strError = "Address does not refer to key";
		return false;
	}
	bool fInvalid = false;
	std::vector<unsigned char> vchSig2 = DecodeBase64(sSignature.c_str(), &fInvalid);
	if (fInvalid)
	{
		strError = "Malformed base64 encoding";
		return false;
	}
	CHashWriter ss2(SER_GETHASH, 0);
	ss2 << strMessageMagic;
	ss2 << strMessage;
	CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss2.GetHash(), vchSig2)) 
	{
		strError = "Unable to recover public key.";
		return false;
	}
	bool fSuccess = (pubkey2.GetID() == keyID2);
	return fSuccess;
}


CPK GetCPK(std::string sData)
{
	// CPK DATA FORMAT: sCPK + "|" + Sanitized NickName + "|" + LockTime + "|" + SecurityHash + "|" + CPK Signature + "|" + Email + "|" + VendorType + "|" + OptData
	CPK k;
	std::vector<std::string> vDec = Split(sData.c_str(), "|");
	if (vDec.size() < 5) return k;
	std::string sSecurityHash = vDec[3];
	std::string sSig = vDec[4];
	std::string sCPK = vDec[0];
	if (sCPK.empty()) return k;
	if (vDec.size() >= 6)
		k.sEmail = vDec[5];
	if (vDec.size() >= 7)
		k.sVendorType = vDec[6];
	if (vDec.size() >= 8)
		k.sOptData = vDec[7];

	k.fValid = CheckStakeSignature(sCPK, sSig, sSecurityHash, k.sError);
	if (!k.fValid) 
	{
		LogPrintf("GetCPK::Error Sig %s, SH %s, Err %s, CPK %s, NickName %s ", sSig, sSecurityHash, k.sError, sCPK, vDec[1]);
		return k;
	}

	k.sAddress = sCPK;
	k.sNickName = vDec[1];
	k.nLockTime = (int64_t)cdbl(vDec[2], 0);

	return k;

} 

std::map<std::string, CPK> GetChildMap(std::string sGSCObjType)
{
	std::map<std::string, CPK> mCPKMap;
	boost::to_upper(sGSCObjType);
	int i = 0;
	for (auto ii : mvApplicationCache)
	{
		if (Contains(ii.first.first, sGSCObjType))
		{
			CPK k = GetCPK(ii.second.first);
			i++;
			mCPKMap.insert(std::make_pair(k.sAddress + "-" + RoundToString(i, 0), k));
		}
	}
	return mCPKMap;
}


std::map<std::string, CPK> GetGSCMap(std::string sGSCObjType, std::string sSearch, bool fRequireSig)
{
	std::map<std::string, CPK> mCPKMap;
	boost::to_upper(sGSCObjType);
	for (auto ii : mvApplicationCache)
	{
    	if (ii.first.first == sGSCObjType)
		{
			CPK k = GetCPK(ii.second.first);
			if (!k.sAddress.empty() && k.fValid)
			{
				if ((!sSearch.empty() && (sSearch == k.sAddress || sSearch == k.sNickName)) || sSearch.empty())
				{
					mCPKMap.insert(std::make_pair(k.sAddress, k));
				}
			}
		}
	}
	return mCPKMap;
}

CAmount CAmountFromValue(const UniValue& value)
{
    if (!value.isNum() && !value.isStr()) return 0;
    CAmount amount;
    if (!ParseFixedPoint(value.getValStr(), 8, &amount)) return 0;
    if (!MoneyRange(amount)) return 0;
    return amount;
}

static CCriticalSection csReadWait;
int64_t GetCacheEntryAge(std::string sSection, std::string sKey)
{
	LOCK(csReadWait);
	std::pair<std::string, int64_t> v = mvApplicationCache[std::make_pair(sSection, sKey)];
	int64_t nTimestamp = v.second;
	int64_t nAge = GetAdjustedTime() - nTimestamp;
	return nAge;
}

std::string ReadCache(std::string sSection, std::string sKey)
{
	LOCK(csReadWait); 
	boost::to_upper(sSection);
	boost::to_upper(sKey);
	// NON-CRITICAL TODO : Find a way to eliminate this to_upper while we transition to non-financial transactions
	if (sSection.empty() || sKey.empty())
		return std::string();
	std::pair<std::string, int64_t> t = mvApplicationCache[std::make_pair(sSection, sKey)];
	return t.first;
}

std::string TimestampToHRDate(double dtm)
{
	if (dtm == 0) return "1-1-1970 00:00:00";
	if (dtm > 9888888888) return "1-1-2199 00:00:00";
	std::string sDt = DateTimeStrFormat("%m-%d-%Y %H:%M:%S",dtm);
	return sDt;
}

std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end)
{
	std::string extraction = "";
	std::string::size_type loc = XMLdata.find( key, 0 );
	if( loc != std::string::npos )
	{
		std::string::size_type loc_end = XMLdata.find( key_end, loc+3);
		if (loc_end != std::string::npos )
		{
			extraction = XMLdata.substr(loc+(key.length()),loc_end-loc-(key.length()));
		}
	}
	return extraction;
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

static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex *pblockindex;
	if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight) return NULL;

    if (nHeight < chainActive.Tip()->nHeight / 2)
		pblockindex = mapBlockIndex[chainActive.Genesis()->GetBlockHash()];
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

std::string DefaultRecAddress(std::string sType)
{
	std::string sDefaultRecAddress;
	for (auto item : pwalletMain->mapAddressBook)
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
				sDefaultRecAddress = CBitcoinAddress(address).ToString();
				return sDefaultRecAddress;
			}
		}
    }

	if (!sType.empty())
	{
		std::string sError;
		sDefaultRecAddress = GenerateNewAddress(sError, sType);
		if (sError.empty()) return sDefaultRecAddress;
	}
	
	return sDefaultRecAddress;
}

std::string CreateBankrollDenominations(double nQuantity, CAmount denominationAmount, std::string& sError)
{
	// First mark the denominations with the 1milliBBP TitheMarker
	denominationAmount += ((.001) * COIN);
	CAmount nBankrollMask = .001 * COIN;

	CAmount nTotal = denominationAmount * nQuantity;

	CAmount curBalance = pwalletMain->GetBalance();
	if (curBalance < nTotal)
	{
		sError = "Insufficient funds (Unlock Wallet).";
		return std::string();
	}
	std::string sTitheAddress = DefaultRecAddress("CHRISTIAN-PUBLIC-KEYPAIR");
	CBitcoinAddress cbAddress(sTitheAddress);
	CWalletTx wtx;
	
    CScript scriptPubKey = GetScriptForDestination(cbAddress.Get());
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
	for (int i = 0; i < nQuantity; i++)
	{
		bool fSubtractFeeFromAmount = false;
	    CRecipient recipient = {scriptPubKey, denominationAmount, false, fSubtractFeeFromAmount};
		vecSend.push_back(recipient);
	}
	
	bool fUseInstantSend = false;
	double minCoinAge = 0;
	std::string sOptData;
    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, sError, NULL, true, ONLY_NONDENOMINATED, fUseInstantSend, 0, sOptData)) 
	{
		if (!sError.empty())
		{
			return std::string();
		}

        if (nTotal + nFeeRequired > pwalletMain->GetBalance())
		{
            sError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
			return std::string();
		}
    }
	CValidationState state;
    if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), state, fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))
    {
		sError = "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.";
		return std::string();
	}

	std::string sTxId = wtx.GetHash().GetHex();
	return sTxId;
}

std::string RetrieveMd5(std::string s1)
{
	try
	{
		const char* chIn = s1.c_str();
		unsigned char digest2[16];
		MD5((unsigned char*)chIn, strlen(chIn), (unsigned char*)&digest2);
		char mdString2[33];
		for(int i = 0; i < 16; i++) sprintf(&mdString2[i*2], "%02x", (unsigned int)digest2[i]);
 		std::string xmd5(mdString2);
		return xmd5;
	}
    catch (std::exception &e)
	{
		return std::string();
	}
}


std::string PubKeyToAddress(const CScript& scriptPubKey)
{
	CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);
    return address2.ToString();
}    

/*
void GetTxTimeAndAmountAndHeight(uint256 hashInput, int hashInputOrdinal, int64_t& out_nTime, CAmount& out_caAmount, int& out_height)
{
	CTransaction tx1;
	uint256 hashBlock1;
	if (GetTransaction(hashInput, tx1, Params().GetConsensus(), hashBlock1, true))
	{
		out_caAmount = tx1.vout[hashInputOrdinal].nValue;
		BlockMap::iterator mi = mapBlockIndex.find(hashBlock1);
		if (mi != mapBlockIndex.end())
		{
			CBlockIndex* pindexHistorical = mapBlockIndex[hashBlock1];              
			out_nTime = pindexHistorical->GetBlockTime();
			out_height = pindexHistorical->nHeight;
			return;
		}
		else
		{
			LogPrintf("\nUnable to find hashBlock %s", hashBlock1.GetHex().c_str());
		}
	}
	else
	{
		LogPrintf("\nUnable to find hashblock1 in GetTransaction %s ",hashInput.GetHex().c_str());
	}
}
*/

/*
bool IsTitheLegal(CTransaction ctx, CBlockIndex* pindex, CAmount tithe_amount)
{
	// R ANDREWS - BIBLEPAY - 12/19/2018 
	// We quote the difficulty params to the user as of the best block hash, so when we check a tithe to be legal, we must check it as of the prior block's difficulty params
	if (pindex==NULL || pindex->pprev==NULL || pindex->nHeight < 2) return false;

	uint256 hashInput = ctx.vin[0].prevout.hash;
	int hashInputOrdinal = ctx.vin[0].prevout.n;
	int64_t nTxTime = 0;
	CAmount caAmount = 0;
	int iHeight = 0;
	GetTxTimeAndAmountAndHeight(hashInput, hashInputOrdinal, nTxTime, caAmount, iHeight);
	
	double nTitheAge = (double)((pindex->GetBlockTime() - nTxTime) / 86400);
	if (nTitheAge >= pindex->pprev->nMinCoinAge && caAmount >= pindex->pprev->nMinCoinAmount && tithe_amount <= pindex->pprev->nMaxTitheAmount)
	{
		return true;
	}
	
	return false;
}
*/

/*
double GetTitheAgeAndSpentAmount(CTransaction ctx, CBlockIndex* pindex, CAmount& spentAmount)
{
	// R ANDREWS - BIBLEPAY - 1/4/2018 
	if (pindex==NULL || pindex->pprev==NULL || pindex->nHeight < 2) return false;
	uint256 hashInput = ctx.vin[0].prevout.hash;
	int hashInputOrdinal = ctx.vin[0].prevout.n;
	int64_t nTxTime = 0;
	int iHeight = 0;
	GetTxTimeAndAmountAndHeight(hashInput, hashInputOrdinal, nTxTime, spentAmount, iHeight);
	double nTitheAge = R2X((double)(pindex->GetBlockTime() - nTxTime) / 86400);
	return nTitheAge;
}
*/

CAmount GetRPCBalance()
{
	return pwalletMain->GetBalance();
}

bool RPCSendMoney(std::string& sError, const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, bool fUseInstantSend, std::string sOptionalData)
{
    CAmount curBalance = pwalletMain->GetBalance();

    // Check amount
    if (nValue <= 0)
	{
        sError = "Invalid amount";
		return false;
	}
	
	if (pwalletMain->IsLocked())
	{
		sError = "Wallet unlock required";
		return false;
	}

	if (nValue > curBalance)
	{
		sError = "Insufficient funds";
		return false;
	}
    // Parse Biblepay address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
	bool fForce = false;
    CRecipient recipient = {scriptPubKey, nValue, fForce, fSubtractFeeFromAmount};
	vecSend.push_back(recipient);
	
    int nMinConfirms = 0;
    if (!pwalletMain->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ONLY_NONDENOMINATED, fUseInstantSend, 0, sOptionalData)) 
	{
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > pwalletMain->GetBalance())
		{
            sError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
			return false;
		}
		sError = "Unable to Create Transaction: " + strError;
		return false;
    }
    CValidationState state;
        
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey, g_connman.get(), state,  fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))
	{
        sError = "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.";
		return false;
	}
	return true;
}

CAmount R20(CAmount amount)
{
	double nAmount = amount / COIN; 
	nAmount = nAmount + 0.5 - (nAmount < 0); 
	int iAmount = (int)nAmount;
	return (iAmount * COIN);
}

double R2X(double var) 
{ 
    double value = (int)(var * 100 + .5); 
    return (double)value / 100; 
} 

double Quantize(double nFloor, double nCeiling, double nValue)
{
	double nSpan = nCeiling - nFloor;
	double nLevel = nSpan * nValue;
	double nOut = nFloor + nLevel;
	if (nOut > std::max(nFloor, nCeiling)) nOut = std::max(nFloor, nCeiling);
	if (nOut < std::min(nCeiling, nFloor)) nOut = std::min(nCeiling, nFloor);
	return nOut;
}

int GetHeightByEpochTime(int64_t nEpoch)
{
	if (!chainActive.Tip()) return 0;
	int nLast = chainActive.Tip()->nHeight;
	if (nLast < 1) return 0;
	for (int nHeight = nLast; nHeight > 0; nHeight--)
	{
		CBlockIndex* pindex = FindBlockByHeight(nHeight);
		if (pindex)
		{
			int64_t nTime = pindex->GetBlockTime();
			if (nEpoch > nTime) return nHeight;
		}
	}
	return -1;
}

void GetGovSuperblockHeights(int& nNextSuperblock, int& nLastSuperblock)
{
	
    int nBlockHeight = 0;
    {
        LOCK(cs_main);
        nBlockHeight = (int)chainActive.Height();
    }
    int nSuperblockStartBlock = Params().GetConsensus().nSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;
    if(nBlockHeight < nFirstSuperblock)
	{
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    } else {
        nLastSuperblock = nBlockHeight - nBlockHeight % nSuperblockCycle;
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }
}

std::string GetActiveProposals()
{
    int nStartTime = GetAdjustedTime() - (86400 * 32);
    LOCK2(cs_main, governance.cs);
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sXML;
	int id = 0;
	std::string sDelim = "|";
	std::string sZero = "\0";
	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);
	for (const CGovernanceObject* pGovObj : objs) 
    {
		if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
		int64_t nEpoch = 0;
		int64_t nStartEpoch = 0;
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		UniValue obj = myGov->GetJSONObject();
		std::string sURL;
		std::string sCharityType;
		nStartEpoch = cdbl(obj["start_epoch"].getValStr(), 0);
		nEpoch = cdbl(obj["end_epoch"].getValStr(), 0);
		sURL = obj["url"].getValStr();
		sCharityType = obj["expensetype"].getValStr();
		if (sCharityType.empty()) sCharityType = "N/A";
		BiblePayProposal bbpProposal = GetProposalByHash(pGovObj->GetHash(), nLastSuperblock);
		std::string sHash = pGovObj->GetHash().GetHex();
		int nEpochHeight = GetHeightByEpochTime(nStartEpoch);
		// First ensure the proposals gov height has not passed yet
		bool bIsPaid = nEpochHeight < nLastSuperblock;
		std::string sReport = DescribeProposal(bbpProposal);
		if (fDebugSpam && fDebug)
			LogPrintf("\nGetActiveProposals::Proposal %s , epochHeight %f, nLastSuperblock %f, IsPaid %f ", 
					sReport, nEpochHeight, nLastSuperblock, (double)bIsPaid);
		if (!bIsPaid)
		{
			int iYes = pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING);
			int iNo = pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING);
			int iAbstain = pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING);
			id++;
			if (sCharityType.empty()) sCharityType = "N/A";
			std::string sProposalTime = TimestampToHRDate(nStartEpoch);
			if (id == 1) sURL += "&t=" + RoundToString(GetAdjustedTime(), 0);
			std::string sName;
			sName = obj["name"].getValStr();
			double dCharityAmount = 0;
			dCharityAmount = cdbl(obj["payment_amount"].getValStr(), 2);
			std::string sRow = "<proposal>" + sHash + sDelim 
				+ sName + sDelim 
				+ RoundToString(dCharityAmount, 2) + sDelim
				+ sCharityType + sDelim
				+ sProposalTime + sDelim
					+ RoundToString(iYes, 0) + sDelim
					+ RoundToString(iNo, 0) + sDelim + RoundToString(iAbstain,0) 
					+ sDelim + sURL;
				sXML += sRow;
		}
	}
	return sXML;
}

bool VoteManyForGobject(std::string govobj, std::string strVoteSignal, std::string strVoteOutcome, 
	int iVotingLimit, int& nSuccessful, int& nFailed, std::string& sError)
{
        
	uint256 hash(uint256S(govobj));
	vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
	if(eVoteSignal == VOTE_SIGNAL_NONE) 
	{
		sError = "Invalid vote signal (funding).";
		return false;
	}
    vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
    if(eVoteOutcome == VOTE_OUTCOME_NONE) 
	{
        sError = "Invalid vote outcome (yes/no/abstain)";
		return false;
	}
  
    std::vector<CMasternodeConfig::CMasternodeEntry> entries = masternodeConfig.getEntries();

#ifdef ENABLE_WALLET
    if (deterministicMNManager->IsDeterministicMNsSporkActive()) 
	{
        if (!pwalletMain) 
		{
			sError = "Voting is not supported when wallet is disabled.";
			return false;
        }
        entries.clear();
        auto mnList = deterministicMNManager->GetListAtChainTip();
        mnList.ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) 
		{
            bool found = false;
            for (const auto &mne : entries) 
			{
                uint256 nTxHash;
                nTxHash.SetHex(mne.getTxHash());

                int nOutputIndex = 0;
                if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
                    continue;
                }

                if (nTxHash == dmn->collateralOutpoint.hash && (uint32_t)nOutputIndex == dmn->collateralOutpoint.n) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                CKey ownerKey;
                if (pwalletMain->GetKey(dmn->pdmnState->keyIDVoting, ownerKey)) {
                    CBitcoinSecret secret(ownerKey);
                    CMasternodeConfig::CMasternodeEntry mne(dmn->proTxHash.ToString(), dmn->pdmnState->addr.ToStringIPPort(false), secret.ToString(), dmn->collateralOutpoint.hash.ToString(), itostr(dmn->collateralOutpoint.n));
                    entries.push_back(mne);
                }
            }
        });
    }
#else
    if (deterministicMNManager->IsDeterministicMNsSporkActive()) 
	{
        sError = "Voting is not supported when wallet is disabled.";
		return false;
    }
#endif
	UniValue vOutcome;
    
	try
	{
		vOutcome = VoteWithMasternodeList(entries, hash, eVoteSignal, eVoteOutcome, nSuccessful, nFailed);
	}
	catch(std::runtime_error& e)
	{
		sError = e.what();
		return false;
	}
	catch (std::exception& e) 
	{
       sError = e.what();
	   return false;
    }
	catch (...) 
	{
		sError = "Voting failed.";
		return false;
	}
    
	bool fResult = nSuccessful > 0 ? true : false;
	return fResult;
}

std::string CreateGovernanceCollateral(uint256 GovObjHash, CAmount caFee, std::string& sError)
{
	CWalletTx wtx;
	if(!pwalletMain->GetBudgetSystemCollateralTX(wtx, GovObjHash, caFee, false)) 
	{
		sError = "Error creating collateral transaction for governance object.  Please check your wallet balance and make sure your wallet is unlocked.";
		return std::string();
	}
	if (sError.empty())
	{
		// -- make our change address
		CReserveKey reservekey(pwalletMain);
		CValidationState state;
        pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), state, NetMsgType::TX);
		DBG( cout << "gobject: prepare "
					<< " strData = " << govobj.GetDataAsString()
					<< ", hash = " << govobj.GetHash().GetHex()
					<< ", txidFee = " << wtx.GetHash().GetHex()
					<< endl; );
		return wtx.GetHash().ToString();
	}
	return std::string();
}

int GetNextSuperblock()
{
	int nLastSuperblock, nNextSuperblock;
    // Get current block height
    int nBlockHeight = 0;
    {
        LOCK(cs_main);
        nBlockHeight = (int)chainActive.Height();
    }

    // Get chain parameters
    int nSuperblockStartBlock = Params().GetConsensus().nSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;

    // Get first superblock
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;

    if(nBlockHeight < nFirstSuperblock)
	{
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    }
	else 
	{
        nLastSuperblock = nBlockHeight - nBlockHeight % nSuperblockCycle;
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }
	return nNextSuperblock;
}

/*
std::string StoreBusinessObjectWithPK(UniValue& oBusinessObject, std::string& sError)
{
	std::string sJson = oBusinessObject.write(0,0);
	std::string sPK = oBusinessObject["primarykey"].getValStr();
	std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
	std::string sSignKey = oBusinessObject["signingkey"].getValStr();
	if (sSignKey.empty()) sSignKey = sAddress;
	std::string sOT = oBusinessObject["objecttype"].getValStr();
	std::string sSecondaryKey = oBusinessObject["secondarykey"].getValStr();
	std::string sIPFSHash = SubmitBusinessObjectToIPFS(sJson, sError);
	if (sError.empty())
	{
		double dStorageFee = 1;
		std::string sTxId = "";
		sTxId = SendBusinessObject(sOT, sPK + sSecondaryKey, sIPFSHash, dStorageFee, sSignKey, true, sError);
		WriteCache(sPK, sSecondaryKey, sIPFSHash, GetAdjustedTime());
		return sTxId;
	}
	return;
}
*/


bool LogLimiter(int iMax1000)
{
	 //The lower the level, the less logged
	 int iVerbosityLevel = rand() % 1000;
	 if (iVerbosityLevel < iMax1000) return true;
	 return false;
}

bool is_email_valid(const std::string& e)
{
	return (Contains(e, "@") && Contains(e,".") && e.length() > MINIMUM_EMAIL_LENGTH) ? true : false;
}

/*

std::string StoreBusinessObject(UniValue& oBusinessObject, std::string& sError)
{
	std::string sJson = oBusinessObject.write(0,0);
	std::string sAddress = DefaultRecAddress(BUSINESS_OBJECTS);
	std::string sPrimaryKey = oBusinessObject["objecttype"].getValStr();
	std::string sSecondaryKey = oBusinessObject["secondarykey"].getValStr();
	std::string sIPFSHash = SubmitBusinessObjectToIPFS(sJson, sError);
	if (sError.empty())
	{
		double dStorageFee = 1;
		std::string sTxId = "";
		sTxId = SendBusinessObject(sPrimaryKey, sAddress + sSecondaryKey, sIPFSHash, dStorageFee, sAddress, true, sError);
		return sTxId;
	}
	return;
}
*/

int64_t GetFileSizeB(std::string sPath)
{
	if (!boost::filesystem::exists(sPath)) return 0;
	return (int64_t)boost::filesystem::file_size(sPath);
}

bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime, const Consensus::Params& params)
{
	if (!f9000) return true;
	int64_t MAX_AGE = 30 * 60;
	int NONCE_FACTOR = 256;
	int MAX_NONCE = 512;
	int64_t nElapsed = nBlockTime - nPrevBlockTime;
	if (nElapsed > MAX_AGE) return true;
	int64_t nMaxNonce = nElapsed * NONCE_FACTOR;
	if (nMaxNonce < MAX_NONCE) nMaxNonce = MAX_NONCE;
	return (nNonce > nMaxNonce) ? false : true;
}

static CCriticalSection csClearWait;
void ClearCache(std::string sSection)
{
	LOCK(csClearWait);
	boost::to_upper(sSection);
	for (auto ii : mvApplicationCache) 
	{
		if (ii.first.first == sSection)
		{
			mvApplicationCache[std::make_pair(ii.first.first, ii.first.second)] = std::make_pair(std::string(), 0);
		}
	}
}

static CCriticalSection csWriteWait;
void WriteCache(std::string sSection, std::string sKey, std::string sValue, int64_t locktime, bool IgnoreCase)
{
	LOCK(csWriteWait); 
	if (sSection.empty() || sKey.empty()) return;
	if (IgnoreCase)
	{
		boost::to_upper(sSection);
		boost::to_upper(sKey);
	}
	std::pair<std::string, std::string> s1 = std::make_pair(sSection, sKey);
	// Record Cache Entry timestamp
	std::pair<std::string, int64_t> v1 = std::make_pair(sValue, locktime);
	mvApplicationCache[s1] = v1;
}

void WriteCacheDouble(std::string sKey, double dValue)
{
	std::string sValue = RoundToString(dValue, 2);
	WriteCache(sKey, "double", sValue, GetAdjustedTime(), true);
}

double ReadCacheDouble(std::string sKey)
{
	double dVal = cdbl(ReadCache(sKey, "double"), 2);
	return dVal;
}

std::string GetArrayElement(std::string s, std::string delim, int iPos)
{
	std::vector<std::string> vGE = Split(s.c_str(),delim);
	if (iPos > (int)vGE.size())
		return std::string();
	return vGE[iPos];
}

void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	f7000 = nPrevHeight > consensusParams.F7000_CUTOVER_HEIGHT;
    f8000 = nPrevHeight >= consensusParams.F8000_CUTOVER_HEIGHT;
	f9000 = nPrevHeight >= consensusParams.F9000_CUTOVER_HEIGHT;
	int nLastTitheBlock = consensusParams.LAST_TITHE_BLOCK;
    fTitheBlocksActive = (nPrevHeight + 1) < nLastTitheBlock;
}

/*
std::string RetrieveTxOutInfo(const CBlockIndex* pindexLast, int iLookback, int iTxOffset, int ivOutOffset, int iDataType)
{
	// When DataType == 1, returns txOut Address
	// When DataType == 2, returns TxId
	// When DataType == 3, returns Blockhash

    if (pindexLast == NULL || pindexLast->nHeight == 0) 
	{
        return ;
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
	if (ReadBlockFromDisk(block, pindexLast, consensusParams))
	{
		if (iTxOffset >= (int)block.vtx.size()) iTxOffset=block.vtx.size()-1;
		if (ivOutOffset >= (int)block.vtx[iTxOffset]->vout.size()) ivOutOffset=block.vtx[iTxOffset]->vout.size()-1;
		if (iTxOffset >= 0 && ivOutOffset >= 0)
		{
			if (iDataType == 1)
			{
				std::string sPKAddr = PubKeyToAddress(block.vtx[iTxOffset]->vout[ivOutOffset].scriptPubKey);
				return sPKAddr;
			}
			else if (iDataType == 2)
			{
				std::string sTxId = block.vtx[iTxOffset]->GetHash().ToString();
				return sTxId;
			}
		}
	}
	
	return;

}
*/

std::string GetIPFromAddress(std::string sAddress)
{
	std::vector<std::string> vAddr = Split(sAddress.c_str(),":");
	if (vAddr.size() < 2)
		return std::string();
	return vAddr[0];
}


bool SubmitProposalToNetwork(uint256 txidFee, int64_t nStartTime, std::string sHex, std::string& sError, std::string& out_sGovObj)
{
	if(!masternodeSync.IsBlockchainSynced()) 
	{
		sError = "Must wait for client to sync with masternode network. ";
		return false;
    }
    // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS
    uint256 hashParent = uint256();
    int nRevision = 1;
	CGovernanceObject govobj(hashParent, nRevision, nStartTime, txidFee, sHex);
    DBG( cout << "gobject: submit "
             << " strData = " << govobj.GetDataAsString()
             << ", hash = " << govobj.GetHash().GetHex()
             << ", txidFee = " << txidFee.GetHex()
             << endl; );

    std::string strHash = govobj.GetHash().ToString();
    if(!govobj.IsValidLocally(sError, true)) 
	{
		sError += "Object submission rejected because object is not valid.";
		LogPrintf("\n OBJECT REJECTED:\n gobject submit 0 1 %f %s %s \n", (double)nStartTime, sHex.c_str(), txidFee.GetHex().c_str());
		return false;
    }
    // RELAY THIS OBJECT - Reject if rate check fails but don't update buffer

	bool fRateCheckBypassed = false;
    if(!governance.MasternodeRateCheck(govobj, true, false, fRateCheckBypassed)) 
	{
        sError = "Object creation rate limit exceeded";
		return false;
	}
    //governance.AddSeenGovernanceObject(govobj.GetHash(), SEEN_OBJECT_IS_VALID);
    govobj.Relay(*g_connman);
    governance.AddGovernanceObject(govobj, *g_connman);
	out_sGovObj = govobj.GetHash().ToString();
	return true;
}


std::vector<char> ReadBytesAll(char const* filename)
{
    std::ifstream ifs(filename, std::ios::binary|std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();
    std::vector<char>  result(pos);
    ifs.seekg(0, std::ios::beg);
    ifs.read(&result[0], pos);
    return result;
}


std::string GetFileNameFromPath(std::string sPath)
{
	sPath = strReplace(sPath, "/", "\\");
	std::vector<std::string> vRows = Split(sPath.c_str(), "\\");
	std::string sFN = "";
	for (int i = 0; i < (int)vRows.size(); i++)
	{
		sFN = vRows[i];
	}
	return sFN;
}

std::string SubmitToIPFS(std::string sPath, std::string& sError)
{
	if (!boost::filesystem::exists(sPath)) 
	{
		sError = "IPFS File not found.";
		return std::string();
	}
	std::string sFN = GetFileNameFromPath(sPath);
	std::vector<char> v = ReadBytesAll(sPath.c_str());
	std::vector<unsigned char> uData(v.begin(), v.end());
	std::string s64 = EncodeBase64(&uData[0], uData.size());
	std::string sData; // BiblepayIPFSPost(sFN, s64);
	std::string sHash = ExtractXML(sData,"<HASH>","</HASH>");
	std::string sLink = ExtractXML(sData,"<LINK>","</LINK>");
	sError = ExtractXML(sData,"<ERROR>","</ERROR>");
	return sHash;
}	
/*
std::string SendBusinessObject(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, std::string sSignKey, bool fSign, std::string& sError)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CBitcoinAddress address(sAddress);
    if (!address.IsValid())
	{
		sError = "Invalid Destination Address";
		return sError;
	}
    CAmount nAmount = AmountFromValue(dStorageFee);
	CAmount nMinimumBalance = AmountFromValue(dStorageFee);
    CWalletTx wtx;
	boost::to_upper(sPrimaryKey); // Following same pattern
	boost::to_upper(sType);
	std::string sNonceValue = RoundToString(GetAdjustedTime(), 0);
	// Add immunity to replay attacks (Add message nonce)
 	std::string sMessageType      = "<MT>" + sType  + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey   + "</MK>";
	std::string sMessageValue     = "<MV>" + sValue + "</MV>";
	std::string sNonce            = "<NONCE>" + sNonceValue + "</NONCE>";
	std::string sBOSignKey        = "<BOSIGNER>" + sSignKey + "</BOSIGNER>";
	std::string sMessageSig = "";

	if (fSign)
	{
		std::string sSignature = "";
		bool bSigned = SignStake(sSignKey, sValue + sNonceValue, sError, sSignature);
		if (bSigned) 
		{
			sMessageSig = "<BOSIG>" + sSignature + "</BOSIG>";
		}
		else
		{
			LogPrintf(" signing business object failed %s key %s ",sPrimaryKey.c_str(), sSignKey.c_str());
		}
	}
	std::string s1 = sMessageType + sMessageKey + sMessageValue + sNonce + sBOSignKey + sMessageSig;
	RPCSendMoney(address.Get(), nAmount, nMinimumBalance, 0, 0, "", 0, wtx, sError);
	if (!sError.empty()) ;
    return wtx.GetHash().GetHex().c_str();
}
*/

int GetSignalInt(std::string sLocalSignal)
{
	boost::to_upper(sLocalSignal);
	if (sLocalSignal=="NO") return 1;
	if (sLocalSignal=="YES") return 2;
	if (sLocalSignal=="ABSTAIN") return 3;
	return 0;
}

UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string sSearch, std::string& outEntry)
{
	int64_t nEpoch = GetAdjustedTime() - (iMaxAgeInDays * 86400);
	if (nEpoch < 0) nEpoch = 0;
    UniValue ret(UniValue::VOBJ);
	boost::to_upper(sType);
	if (sType=="PRAYERS")
		sType="PRAYER";  // Just in case the user specified PRAYERS
	ret.push_back(Pair("DataList",sType));
	int iPos = 0;
	int iTotalRecords = 0;
	for (auto ii : mvApplicationCache) 
	{
		if (ii.first.first == sType)
		{
			std::pair<std::string, int64_t> v = mvApplicationCache[std::make_pair(ii.first.first, ii.first.second)];
			int64_t nTimestamp = v.second;
			if (nTimestamp > nEpoch || nTimestamp == 0)
			{
				iTotalRecords++;
				if (iPos == iSpecificEntry) 
					outEntry = v.first;
				std::string sTimestamp = TimestampToHRDate((double)nTimestamp);
				if (!sSearch.empty())
				{
					if (boost::iequals(ii.first.first, sSearch) || Contains(ii.first.second, sSearch))
					{
						ret.push_back(Pair(ii.first.second + " (" + sTimestamp + ")", v.first));
					}
				}
				else
				{
					ret.push_back(Pair(ii.first.second + " (" + sTimestamp + ")", v.first));
				}
				iPos++;
			}
		}
	}
	iSpecificEntry++;
	if (iSpecificEntry >= iTotalRecords)
		iSpecificEntry=0;  // Reset the iterator.
	return ret;
}

UniValue ContributionReport()
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int nMaxDepth = chainActive.Tip()->nHeight;
    CBlock block;
	int nMinDepth = 1;
	double dTotal = 0;
	double dChunk = 0;
    UniValue ret(UniValue::VOBJ);
	int iProcessedBlocks = 0;
	int nStart = 1;
	int nEnd = 1;
	for (int ii = nMinDepth; ii <= nMaxDepth; ii++)
	{
   			CBlockIndex* pblockindex = FindBlockByHeight(ii);
			if (ReadBlockFromDisk(block, pblockindex, consensusParams))
			{
				iProcessedBlocks++;
				nEnd = ii;
				for (auto tx : block.vtx) 
				{
					 for (int i=0; i < (int)tx->vout.size(); i++)
					 {
				 		std::string sRecipient = PubKeyToAddress(tx->vout[i].scriptPubKey);
						double dAmount = tx->vout[i].nValue/COIN;
						bool bProcess = false;
						if (sRecipient == consensusParams.FoundationAddress)
						{ 
							bProcess = true;
						}
						else if (pblockindex->nHeight == 24600 && dAmount == 2894609)
						{
							bProcess=true; // This compassion payment was sent to Robs address first by mistake; add to the audit 
						}
						if (bProcess)
						{
								dTotal += dAmount;
								dChunk += dAmount;
						}
					 }
				 }
		  		 double nBudget = CSuperblock::GetPaymentsLimit(ii) / COIN;
				 if (iProcessedBlocks >= (BLOCKS_PER_DAY*7) || (ii == nMaxDepth-1) || (nBudget > 5000000))
				 {
					 iProcessedBlocks = 0;
					 std::string sNarr = "Block " + RoundToString(nStart, 0) + " - " + RoundToString(nEnd, 0);
					 ret.push_back(Pair(sNarr, dChunk));
					 dChunk = 0;
					 nStart = nEnd;
				 }

			}
	}
	
	ret.push_back(Pair("Grand Total", dTotal));
	return ret;
}

void SerializePrayersToFile(int nHeight)
{
	if (nHeight < 100) return;
	std::string sSuffix = fProd ? "_prod" : "_testnet";
	std::string sTarget = GetSANDirectory2() + "prayers2" + sSuffix;
	FILE *outFile = fopen(sTarget.c_str(), "w");
	LogPrintf("Serializing Prayers... %f ", GetAdjustedTime());
	for (auto ii : mvApplicationCache) 
	{
		std::pair<std::string, int64_t> v = mvApplicationCache[std::make_pair(ii.first.first, ii.first.second)];
	   	int64_t nTimestamp = v.second;
		std::string sValue = v.first;
		bool bSkip = false;
		if (ii.first.first == "MESSAGE" && (sValue == std::string() || sValue == std::string()))
			bSkip = true;
		if (!bSkip)
		{
			std::string sRow = RoundToString(nTimestamp, 0) + "<colprayer>" + RoundToString(nHeight, 0) + "<colprayer>" + ii.first.first + ";" 
				+ ii.first.second + "<colprayer>" + sValue + "<rowprayer>\r\n";
			fputs(sRow.c_str(), outFile);
		}
	}
	LogPrintf("...Done Serializing Prayers... %f ", GetAdjustedTime());
    fclose(outFile);
}

int DeserializePrayersFromFile()
{
	LogPrintf("\nDeserializing prayers from file %f", GetAdjustedTime());
	std::string sSuffix = fProd ? "_prod" : "_testnet";
	std::string sSource = GetSANDirectory2() + "prayers2" + sSuffix;

	boost::filesystem::path pathIn(sSource);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
	if (!streamIn) return -1;
	int nHeight = 0;
	std::string line;
	int iRows = 0;
    while(std::getline(streamIn, line))
    {
		std::vector<std::string> vRows = Split(line.c_str(),"<rowprayer>");
		for (int i = 0; i < (int)vRows.size(); i++)
		{
			std::vector<std::string> vCols = Split(vRows[i].c_str(),"<colprayer>");
			if (vCols.size() > 3)
			{
				int64_t nTimestamp = cdbl(vCols[0], 0);
				int cHeight = cdbl(vCols[1], 0);
				if (cHeight > nHeight) nHeight = cHeight;
				std::string sKey = vCols[2];
				std::string sValue = vCols[3];
				std::vector<std::string> vKeys = Split(sKey.c_str(), ";");
				if (vKeys.size() > 1)
				{
					WriteCache(vKeys[0], vKeys[1], sValue, nTimestamp, true); //ignore case
					iRows++;
				}
			}
		}
	}
	streamIn.close();
    LogPrintf(" Processed %f prayer rows - %f\n", iRows, GetAdjustedTime());
	return nHeight;
}

CAmount GetTitheAmount(CTransactionRef ctx)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	for (unsigned int z = 0; z < ctx->vout.size(); z++)
	{
		std::string sRecip = PubKeyToAddress(ctx->vout[z].scriptPubKey);
		if (sRecip == consensusParams.FoundationAddress) 
		{
			return ctx->vout[z].nValue;  // First Tithe amount found in transaction counts
		}
	}
	return 0;
}

std::string VectToString(std::vector<unsigned char> v)
{
     std::string s(v.begin(), v.end());
     return s;
}

CAmount StringToAmount(std::string sValue)
{
	if (sValue.empty()) return 0;
    CAmount amount;
    if (!ParseFixedPoint(sValue, 8, &amount)) return 0;
    if (!MoneyRange(amount)) throw std::runtime_error("AMOUNT OUT OF MONEY RANGE");
    return amount;
}

bool CompareMask(CAmount nValue, CAmount nMask)
{
	if (nMask == 0) return false;
	std::string sAmt = "0000000000000000000000000" + AmountToString(nValue);
	std::string sMask= AmountToString(nMask);
	std::string s1 = sAmt.substr(sAmt.length() - sMask.length() + 1, sMask.length() - 1);
	std::string s2 = sMask.substr(1, sMask.length() - 1);
	return (s1 == s2);
}

bool CopyFile(std::string sSrc, std::string sDest)
{
	boost::filesystem::path fSrc(sSrc);
	boost::filesystem::path fDest(sDest);
	try
	{
		#if BOOST_VERSION >= 104000
			boost::filesystem::copy_file(fSrc, fDest, boost::filesystem::copy_option::overwrite_if_exists);
		#else
			boost::filesystem::copy_file(fSrc, fDest);
		#endif
	}
	catch (const boost::filesystem::filesystem_error& e) 
	{
		LogPrintf("CopyFile failed - %s ",e.what());
		return false;
    }
	return true;
}

std::string Caption(std::string sDefault, int iMaxLen)
{
	if (sDefault.length() > iMaxLen)
		sDefault = sDefault.substr(0, iMaxLen);
	std::string sValue = ReadCache("message", sDefault);
	return sValue.empty() ? sDefault : sValue;		
}

double GetBlockVersion(std::string sXML)
{
	std::string sBlockVersion = ExtractXML(sXML,"<VER>","</VER>");
	sBlockVersion = strReplace(sBlockVersion, ".", "");
	if (sBlockVersion.length() == 3) sBlockVersion += "0"; 
	double dBlockVersion = cdbl(sBlockVersion, 0);
	return dBlockVersion;
}

struct TxMessage
{
  std::string sMessageType;
  std::string sMessageKey;
  std::string sMessageValue;
  std::string sSig;
  std::string sNonce;
  std::string sSporkSig;
  std::string sIPFSHash;
  std::string sBOSig;
  std::string sBOSigner;
  std::string sTimestamp;
  std::string sIPFSSize;
  std::string sCPIDSig;
  std::string sCPID;
  std::string sPODCTasks;
  std::string sTxId;
  std::string sVoteSignal;
  std::string sVoteHash;
  double      nNonce;
  double      dAmount;
  bool        fNonceValid;
  bool        fPrayersMustBeSigned;
  bool        fSporkSigValid;
  bool        fBOSigValid;
  bool        fPassedSecurityCheck;
  int64_t     nAge;
  int64_t     nTime;
};


bool CheckSporkSig(TxMessage t)
{
	std::string sError = "";
	const CChainParams& chainparams = Params();
	bool fSigValid = CheckStakeSignature(chainparams.GetConsensus().FoundationAddress, t.sSporkSig, t.sMessageValue + t.sNonce, sError);
    bool bValid = (fSigValid && t.fNonceValid);
	if (!bValid)
	{
		LogPrint("net", "CheckSporkSig:SigFailed - Type %s, Nonce %f, Time %f, Bad spork Sig %s on message %s on TXID %s \n", t.sMessageType.c_str(), t.nNonce, t.nTime, 
			               t.sSporkSig.c_str(), t.sMessageValue.c_str(), t.sTxId.c_str());
	}
	return bValid;
}

bool CheckBusinessObjectSig(TxMessage t)
{
	if (!t.sBOSig.empty() && !t.sBOSigner.empty())
	{	
		std::string sError = "";
		bool fBOSigValid = CheckStakeSignature(t.sBOSigner, t.sBOSig, t.sMessageValue + t.sNonce, sError);
   		if (!fBOSigValid)
		{
			LogPrint("net", "MemorizePrayers::BO_SignatureFailed - Type %s, Nonce %f, Time %f, Bad BO Sig %s on message %s on TXID %s \n", 
				t.sMessageType.c_str(),	t.nNonce, t.nTime, t.sBOSig.c_str(), t.sMessageValue.c_str(), t.sTxId.c_str());
	   	}
		return fBOSigValid;
	}
	return false;
}



TxMessage GetTxMessage(std::string sMessage, int64_t nTime, int iPosition, std::string sTxId, double dAmount, double dFoundationDonation, int nHeight)
{
	TxMessage t;
	t.sMessageType = ExtractXML(sMessage,"<MT>","</MT>");
	t.sMessageKey  = ExtractXML(sMessage,"<MK>","</MK>");
	t.sMessageValue= ExtractXML(sMessage,"<MV>","</MV>");
	t.sSig         = ExtractXML(sMessage,"<MS>","</MS>");
	t.sNonce       = ExtractXML(sMessage,"<NONCE>","</NONCE>");
	t.nNonce       = cdbl(t.sNonce, 0);
	t.sSporkSig    = ExtractXML(sMessage,"<SPORKSIG>","</SPORKSIG>");
	t.sIPFSHash    = ExtractXML(sMessage,"<IPFSHASH>", "</IPFSHASH>");
	t.sBOSig       = ExtractXML(sMessage,"<BOSIG>", "</BOSIG>");
	t.sBOSigner    = ExtractXML(sMessage,"<BOSIGNER>", "</BOSIGNER>");
	t.sIPFSHash    = ExtractXML(sMessage,"<ipfshash>", "</ipfshash>");
	t.sIPFSSize    = ExtractXML(sMessage,"<ipfssize>", "</ipfssize>");
	t.sCPIDSig     = ExtractXML(sMessage,"<cpidsig>","</cpidsig>");
	t.sCPID        = GetElement(t.sCPIDSig, ";", 0);
	t.sPODCTasks   = ExtractXML(sMessage, "<PODC_TASKS>", "</PODC_TASKS>");
	t.sTxId        = sTxId;
	t.nTime        = nTime;
	t.dAmount      = dAmount;
    boost::to_upper(t.sMessageType);
	boost::to_upper(t.sMessageKey);
	t.sTimestamp = TimestampToHRDate((double)nTime + iPosition);
	t.fNonceValid = (!(t.nNonce > (nTime+(60 * 60)) || t.nNonce < (nTime-(60 * 60))));
	t.nAge = GetAdjustedTime() - nTime;
	t.fPrayersMustBeSigned = (GetSporkDouble("prayersmustbesigned", 0) == 1);

	if (t.sMessageType == "PRAYER" && (!(Contains(t.sMessageKey, "(") ))) t.sMessageKey += " (" + t.sTimestamp + ")";
	if (t.sMessageType == "SPORK")
	{
		t.fSporkSigValid = CheckSporkSig(t);                                                                                                                                                                                                                 
		if (!t.fSporkSigValid) t.sMessageValue  = "";
		t.fPassedSecurityCheck = t.fSporkSigValid;
	}
	else if (t.sMessageType == "PRAYER" && t.fPrayersMustBeSigned)
	{
		double dMinimumUnsignedPrayerDonation = GetSporkDouble("minimumunsignedprayerdonationamount", 3000);
		// If donation is to Foundation and meets minimum amount and is not signed
		if (dFoundationDonation >= dMinimumUnsignedPrayerDonation)
		{
			t.fPassedSecurityCheck = true;
		}
		else
		{
			t.fSporkSigValid = CheckSporkSig(t);
			if (!t.fSporkSigValid) t.sMessageValue  = "";
			t.fPassedSecurityCheck = t.fSporkSigValid;
		}
	}
	else if (t.sMessageType == "PRAYER" && !t.fPrayersMustBeSigned)
	{
		// We allow unsigned prayers, as long as abusers don't deface the system (if they do, we set the spork requiring signed prayers and we manually remove the offensive prayers using a signed update)
		t.fPassedSecurityCheck = true; 
	}
	else if (t.sMessageType == "ATTACHMENT" || t.sMessageType=="CPIDTASKS")
	{
		t.fPassedSecurityCheck = true;
	}
	else if (t.sMessageType == "REPENT")
	{
		t.fPassedSecurityCheck = true;
	}
	else if (t.sMessageType == "MESSAGE")
	{
		// these are sent by our users to each other
		t.fPassedSecurityCheck = true;
	}
	else if (t.sMessageType == "DCC" || Contains(t.sMessageType, "CPK"))
	{
		// These now have a security hash on each record and are checked individually using CheckStakeSignature
		t.fPassedSecurityCheck = true;
	}
	else if (t.sMessageType == "EXPENSE" || t.sMessageType == "REVENUE" || t.sMessageType == "ORPHAN")
	{
		t.sSporkSig = t.sBOSig;
		t.fSporkSigValid = CheckSporkSig(t);
		if (!t.fSporkSigValid) 
		{
			t.sMessageValue  = "";
		}
		t.fPassedSecurityCheck = t.fSporkSigValid;
	}
	else if (t.sMessageType == "VOTE")
	{
		t.fBOSigValid = CheckBusinessObjectSig(t);
		t.fPassedSecurityCheck = t.fBOSigValid;
	}
	else
	{
		// We assume this is a business object
		t.fBOSigValid = CheckBusinessObjectSig(t);
		if (!t.fBOSigValid) 
			t.sMessageValue = "";
		t.fPassedSecurityCheck = t.fBOSigValid;
	}
	return t;
}

bool IsCPKWL(std::string sCPK, std::string sNN)
{
	std::string sWL = GetSporkValue("cpkdiarywl");
	return (Contains(sWL, sNN));
}

void MemorizePrayer(std::string sMessage, int64_t nTime, double dAmount, int iPosition, std::string sTxID, int nHeight, double dFoundationDonation, double dAge, double dMinCoinAge)
{
	if (sMessage.empty()) return;
	TxMessage t = GetTxMessage(sMessage, nTime, iPosition, sTxID, dAmount, dFoundationDonation, nHeight);
	std::string sDiary = ExtractXML(sMessage, "<diary>", "</diary>");
	
	if (!sDiary.empty())
	{
		std::string sCPK = ExtractXML(sMessage, "<abncpk>", "</abncpk>");
		CPK oPrimary = GetCPKFromProject("cpk", sCPK);
		std::string sNickName = Caption(oPrimary.sNickName, 10);
		bool fWL = IsCPKWL(sCPK, sNickName);
		if (fWL)
		{
			if (sNickName.empty()) sNickName = "NA";
			std::string sEntry = sDiary + " [" + sNickName + "]";
			WriteCache("diary", RoundToString(nTime, 0), sEntry, nTime);
		}
	}
	if (!t.sIPFSHash.empty())
	{
		WriteCache("IPFS", t.sIPFSHash, RoundToString(nHeight, 0), nTime, false);
		WriteCache("IPFSFEE" + RoundToString(nTime, 0), t.sIPFSHash, RoundToString(dFoundationDonation, 0), nTime, true);
		WriteCache("IPFSSIZE" + RoundToString(nTime, 0), t.sIPFSHash, t.sIPFSSize, nTime, true);
	}
	if (t.fPassedSecurityCheck && !t.sMessageType.empty() && !t.sMessageKey.empty() && !t.sMessageValue.empty())
	{
		WriteCache(t.sMessageType, t.sMessageKey, t.sMessageValue, nTime, true);
	}
}

void MemorizeBlockChainPrayers(bool fDuringConnectBlock, bool fSubThread, bool fColdBoot, bool fDuringSanctuaryQuorum)
{
	int nDeserializedHeight = 0;
	if (fColdBoot)
	{
		nDeserializedHeight = DeserializePrayersFromFile();
		if (chainActive.Tip()->nHeight < nDeserializedHeight && nDeserializedHeight > 0)
		{
			LogPrintf(" Chain Height %f, Loading entire prayer index\n", chainActive.Tip()->nHeight);
			nDeserializedHeight = 0;
		}
	}
	if (fDebugSpam && fDebug)
		LogPrintf("Memorizing prayers tip height %f @ time %f deserialized height %f ", chainActive.Tip()->nHeight, GetAdjustedTime(), nDeserializedHeight);

	int nMaxDepth = chainActive.Tip()->nHeight;
	int nMinDepth = fDuringConnectBlock ? nMaxDepth - 2 : nMaxDepth - (BLOCKS_PER_DAY * 30 * 12 * 7);  // Seven years
	if (fDuringSanctuaryQuorum) nMinDepth = nMaxDepth - (BLOCKS_PER_DAY * 14); // Two Weeks
	if (nDeserializedHeight > 0 && nDeserializedHeight < nMaxDepth) nMinDepth = nDeserializedHeight;
	if (nMinDepth < 0) nMinDepth = 0;
	CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	while (pindex && pindex->nHeight < nMaxDepth)
	{
		if (pindex) if (pindex->nHeight < chainActive.Tip()->nHeight) pindex = chainActive.Next(pindex);
		if (!pindex) break;
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			if (pindex->nHeight % 25000 == 0)
				LogPrintf(" MBCP %f @ %f, ", pindex->nHeight, GetAdjustedTime());
			for (unsigned int n = 0; n < block.vtx.size(); n++)
    		{
				double dTotalSent = 0;
				std::string sPrayer = "";
				double dFoundationDonation = 0;
				for (unsigned int i = 0; i < block.vtx[n]->vout.size(); i++)
				{
					sPrayer += block.vtx[n]->vout[i].sTxOutMessage;
					double dAmount = block.vtx[n]->vout[i].nValue / COIN;
					dTotalSent += dAmount;
					// The following 3 lines are used for PODS (Proof of document storage); allowing persistence of paid documents in IPFS
					std::string sPK = PubKeyToAddress(block.vtx[n]->vout[i].scriptPubKey);
					if (sPK == consensusParams.FoundationAddress || sPK == consensusParams.FoundationPODSAddress)
					{
						dFoundationDonation += dAmount;
					}
				}
				double dAge = GetAdjustedTime() - block.GetBlockTime();
				MemorizePrayer(sPrayer, block.GetBlockTime(), dTotalSent, 0, block.vtx[n]->GetHash().GetHex(), pindex->nHeight, dFoundationDonation, dAge, 0);
			}
	 	}
	}
	if (fColdBoot) 
	{
		if (nMaxDepth > (nDeserializedHeight - 1000))
		{
			SerializePrayersToFile(nMaxDepth - 1);
		}
	}
	if (fDebugSpam && fDebug)
		LogPrintf("...Finished MemorizeBlockChainPrayers @ %f ", GetAdjustedTime());
}

std::string SignMessageEvo(std::string strAddress, std::string strMessage, std::string& sError)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
	if (pwalletMain->IsLocked())
	{
		sError = "Sorry, wallet must be unlocked.";
		return std::string();
	}

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
	{
		sError = "Invalid address";
		return std::string();
	}

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
	{
		sError = "Address does not refer to key";
		return std::string();
	}

    CKey key;
    if (!pwalletMain->GetKey(keyID, key))
	{
        sError = "Private key not available";
	    return std::string();
	}

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
	{
        sError = "Sign failed";
		return std::string();
	}

    return EncodeBase64(&vchSig[0], vchSig.size());
}

std::string SignMessage(std::string sMsg, std::string sPrivateKey)
{
     CKey key;
     std::vector<unsigned char> vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());
     std::vector<unsigned char> vchPrivKey = ParseHex(sPrivateKey);
     std::vector<unsigned char> vchSig;
     key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end()), false);
     if (!key.Sign(Hash(vchMsg.begin(), vchMsg.end()), vchSig))  
     {
          return "Unable to sign message, check private key.";
     }
     const std::string sig(vchSig.begin(), vchSig.end());     
     std::string SignedMessage = EncodeBase64(sig);
     return SignedMessage;
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

std::string GetDomainFromURL(std::string sURL)
{
	std::string sDomain;
	int HTTPS_LEN = 8;
	int HTTP_LEN = 7;
	if (sURL.find("https://") != std::string::npos)
	{
		sDomain = sURL.substr(HTTPS_LEN, sURL.length() - HTTPS_LEN);
	}
	else if(sURL.find("http://") != std::string::npos)
	{
		sDomain = sURL.substr(HTTP_LEN, sURL.length() - HTTP_LEN);
	}
	else
	{
		sDomain = sURL;
	}
	return sDomain;
}

bool TermPeekFound(std::string sData, int iBOEType)
{
	std::string sVerbs = "</html>|</HTML>|<EOF>|<END>|</account_out>|</am_set_info_reply>|</am_get_info_reply>";
	std::vector<std::string> verbs = Split(sVerbs, "|");
	bool bFound = false;
	for (int i = 0; i < verbs.size(); i++)
	{
		if (sData.find(verbs[i]) != std::string::npos)
			bFound = true;
	}
	if (iBOEType==1)
	{
		if (sData.find("</user>") != std::string::npos) bFound = true;
		if (sData.find("</error>") != std::string::npos) bFound = true;
		if (sData.find("</error_msg>") != std::string::npos) bFound = true;
	}
	else if (iBOEType == 2)
	{
		if (sData.find("</results>") != std::string::npos) bFound = true;
		if (sData.find("}}") != std::string::npos) bFound = true;
	}
	else if (iBOEType == 3)
	{
		if (sData.find("}") != std::string::npos) bFound = true;
	}
	return bFound;
}

std::string PrepareHTTPPost(bool bPost, std::string sPage, std::string sHostHeader, const std::string& sMsg, const std::map<std::string,std::string>& mapRequestHeaders)
{
	std::ostringstream s;
	std::string sUserAgent = "Mozilla/5.0";
	std::string sMethod = bPost ? "POST" : "GET";

	s << sMethod + " /" + sPage + " HTTP/1.1\r\n"
		<< "User-Agent: " + sUserAgent + "/" << FormatFullVersion() << "\r\n"
		<< "Host: " + sHostHeader + "" << "\r\n"
		<< "Content-Length: " << sMsg.size() << "\r\n";

	for (auto item : mapRequestHeaders) 
	{
        s << item.first << ": " << item.second << "\r\n";
	}
    s << "\r\n" << sMsg;
    return s.str();
}

static double HTTP_PROTO_VERSION = 2.0;

std::string BiblepayHTTPSPost(bool bPost, int iThreadID, std::string sActionName, std::string sDistinctUser, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, 
	std::string sSolution, int iTimeoutSecs, int iMaxSize, int iBOE)
{
	// The OpenSSL version of BiblepayHTTPSPost *only* works with SSL websites, hence the need for BiblePayHTTPPost(2) (using BOOST).  The dev team is working on cleaning this up before the end of 2019 to have one standard version with cleaner code and less internal parts. //
	try
	{
		double dDebugLevel = cdbl(GetArg("-devdebuglevel", "0"), 0);
	
		std::map<std::string, std::string> mapRequestHeaders;
		mapRequestHeaders["Miner"] = sDistinctUser;
		mapRequestHeaders["Action"] = sPayload;
		mapRequestHeaders["Solution"] = sSolution;
		mapRequestHeaders["Agent"] = FormatFullVersion();
		// BiblePay supported pool Network Chain modes: main, test, regtest
		const CChainParams& chainparams = Params();
		mapRequestHeaders["NetworkID"] = chainparams.NetworkIDString();
		mapRequestHeaders["ThreadID"] = RoundToString(iThreadID, 0);
		mapRequestHeaders["OS"] = sOS;

		mapRequestHeaders["SessionID"] = msSessionID;
		mapRequestHeaders["WorkerID1"] = GetArg("-workerid", "");
		mapRequestHeaders["WorkerID2"] = GetArg("-workeridfunded", "");
		mapRequestHeaders["HTTP_PROTO_VERSION"] = RoundToString(HTTP_PROTO_VERSION, 0);
		int iActivePort = cdbl(GetArg("-port", RoundToString(chainparams.GetDefaultPort(), 0)), 0);
		mapRequestHeaders["port"] = RoundToString(iActivePort, 0);

		BIO* bio;
		SSL_CTX* ctx;
		//   Registers the SSL/TLS ciphers and digests and starts the security layer.
		SSL_library_init();
		ctx = SSL_CTX_new(SSLv23_client_method());
		if (ctx == NULL)
		{
			return "<ERROR>CTX_IS_NULL</ERROR>";
		}
		bio = BIO_new_ssl_connect(ctx);
		std::string sDomain = GetDomainFromURL(sBaseURL);
		std::string sDomainWithPort = sDomain + ":" + "443";
		BIO_set_conn_hostname(bio, sDomainWithPort.c_str());
		if(BIO_do_connect(bio) <= 0)
		{
			return "<ERROR>Failed connection to " + sDomainWithPort + "</ERROR>";
		}

		if (sDomain.empty()) return "<ERROR>DOMAIN_MISSING</ERROR>";
		// Evo requires 2 args instead of 3, the last used to be true for DNS resolution=true

		CNetAddr cnaMyHost;
		LookupHost(sDomain.c_str(), cnaMyHost, true);
 	    CService addrConnect = CService(cnaMyHost, 443);

		if (!addrConnect.IsValid())
		{
  			return "<ERROR>DNS_ERROR</ERROR>"; 
		}
		std::string sPost = PrepareHTTPPost(bPost, sPage, sDomain, sPayload, mapRequestHeaders);
		if (dDebugLevel == 1)
			LogPrintf("Trying connection to %s ", sPost);
		const char* write_buf = sPost.c_str();
		if(BIO_write(bio, write_buf, strlen(write_buf)) <= 0)
		{
			return "<ERROR>FAILED_HTTPS_POST</ERROR>";
		}
		//  Variables used to read the response from the server
		int size;
		char buf[1024];
		clock_t begin = clock();
		std::string sData = "";
		for(;;)
		{
			//  Get chunks of the response 1023 at the time.
			size = BIO_read(bio, buf, 1023);
			if(size <= 0)
			{
				break;
			}
			buf[size] = 0;
			std::string MyData(buf);
			sData += MyData;
			clock_t end = clock();
			double elapsed_secs = double(end - begin) / (CLOCKS_PER_SEC + .01);
			if (elapsed_secs > iTimeoutSecs) break;
			if (TermPeekFound(sData, iBOE)) break;

			if (sData.find("Content-Length:") != std::string::npos)
			{
				double dMaxSize = cdbl(ExtractXML(sData,"Content-Length: ","\n"),0);
				std::size_t foundPos = sData.find("Content-Length:");
				if (dMaxSize > 0)
				{
					iMaxSize = dMaxSize + (int)foundPos + 16;
				}
			}
			if ((int)sData.size() >= (iMaxSize-1)) break;
		}
		// R ANDREW - JAN 4 2018: Free bio resources
		BIO_free_all(bio);
		if (dDebugLevel == 1)
			LogPrintf("Received %s ", sData);
		return sData;
	}
	catch (std::exception &e)
	{
        return "<ERROR>WEB_EXCEPTION</ERROR>";
    }
	catch (...)
	{
		return "<ERROR>GENERAL_WEB_EXCEPTION</ERROR>";
	}
}

std::string BiblePayHTTPSPost2(bool bPost, std::string sProtocol, std::string sDomain, std::string sPage, std::string sPayload, std::string sFileName)
{
	std::ostringstream ssOut;
	try
	{
		// This version of BiblePayHTTPSPost has the advantage of working with both HTTP & HTTPS, and works from both QT and the daemon (++).  Our dev team is in the process of testing this across all use cases to ensure it is safe to replace V1.
		std::map<std::string, std::string> mapRequestHeaders;
		mapRequestHeaders["Agent"] = FormatFullVersion();
		std::vector<char> v;
		std::string s64;
		if (!sFileName.empty())
		{
			mapRequestHeaders["Filename"] = sFileName;
			v = ReadBytesAll(sFileName.c_str());
			std::vector<unsigned char> uData(v.begin(), v.end());
			s64 = EncodeBase64(&uData[0], uData.size());
		}
		/* mapRequestHeaders["Content-Type"] = "application/json"; */
		
		std::string sPost = PrepareHTTPPost(bPost, sPage, sDomain, s64, mapRequestHeaders);
		LogPrintf("Preparing post for %s with %s ",sDomain, sPost);

		boost::asio::io_service io_service;
		// Get a list of endpoints corresponding to the server name.
		boost::asio::ip::tcp::resolver resolver(io_service);
		boost::asio::ip::tcp::resolver::query query(sDomain, sProtocol);

		boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		// Try each endpoint until we successfully establish a connection.
		boost::asio::ip::tcp::socket socket(io_service);
		boost::asio::connect(socket, endpoint_iterator);

		boost::asio::ip::tcp::endpoint remote_ep = socket.remote_endpoint();
		boost::asio::ip::address remote_ad = remote_ep.address();
		std::string sRAD = remote_ad.to_string();

		LogPrintf(" BPHP Connecting to address %s", sRAD);
	
		boost::asio::streambuf request;
		std::ostream request_stream(&request);
		request_stream << sPost;
		// Send the request.
		boost::asio::write(socket, request);
		boost::asio::streambuf response;
		boost::asio::read_until(socket, response, "\r\n");
		// Check that response is OK.
		std::istream response_stream(&response);
		std::string http_version;
		response_stream >> http_version;
		unsigned int status_code;
		response_stream >> status_code;
		std::string status_message;
		std::getline(response_stream, status_message);
		if (!response_stream || http_version.substr(0, 5) != "HTTP/")
			return "invalid http response";
		if (status_code != 200)
			return "response returned with status code " + RoundToString(status_code, 0);
		// Read the response headers, which are terminated by a blank line.
		boost::asio::read_until(socket, response, "\r\n");
		std::string header;
		while (std::getline(response_stream, header) && header != "\r")
		{
			ssOut << header << "\n";
		}
		ssOut << "\n";
		// Write whatever content we already have to output.
		if (response.size() > 0)
			ssOut << &response;
		// Read until EOF, writing data to output as we go.
		boost::system::error_code error;
		while (boost::asio::read(socket, response, boost::asio::transfer_at_least(1), error))
		{
			ssOut << &response;
			std::string s1 = ssOut.str();
			if (Contains(s1, "</html>") || Contains(s1,"<eof>") || Contains(s1,"<END>"))
				break;
		}
  }
  catch (std::exception& e)
  {
	  std::cout << e.what();
	  return "HTTPS Post Exception";
  }
  std::string  sRead = ssOut.str();
  return sRead;
}

std::string GetVersionAlert()
{
	if (msGithubVersion.empty()) 
	{
		msGithubVersion = GetGithubVersion();
	}
	if (msGithubVersion.empty())
		return std::string();
	std::string sGithubVersion = strReplace(msGithubVersion, ".", "");
	std::string sError = ExtractXML(sGithubVersion, "<ERROR>", "</ERROR>");
	if (!sError.empty())
	{
		LogPrintf("GetVersionAlert::Error Encountered error %s while checking for latest mandatory version %s", sError, sGithubVersion);
		return std::string();
	}
	double dGithubVersion = cdbl(sGithubVersion, 0);
	std::string sCurrentVersion = FormatFullVersion();
	sCurrentVersion = strReplace(sCurrentVersion, ".", "");
	double dCurrentVersion = cdbl(sCurrentVersion, 0);
	std::string sNarr = "";
	bool bDevBranch = (Contains(strSubVersion, "Develop") || Contains(strSubVersion, "Test"));
	if (bDevBranch)
		return std::string();
	if (dCurrentVersion < dGithubVersion && fProd) sNarr = "<br>** Client Out of Date (v=" + sCurrentVersion + "/v=" + sGithubVersion + ") **";
	return sNarr;
}

bool WriteKey(std::string sKey, std::string sValue)
{
	std::string sDelimiter = sOS == "WIN" ? "\r\n" : "\n";

    // Allows BiblePay to store the key value in the config file.
    boost::filesystem::path pathConfigFile(GetArg("-conf", "biblepay.conf"));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
    if (!boost::filesystem::exists(pathConfigFile))  
	{
		// Config is empty, create it:
		FILE *outFileNew = fopen(pathConfigFile.string().c_str(), "w");
		fputs("", outFileNew);
		fclose(outFileNew);
		LogPrintf("** Created brand new biblepay.conf file **\n");
	}
    boost::to_lower(sKey);
    std::string sLine = "";
    std::ifstream streamConfigFile;
    streamConfigFile.open(pathConfigFile.string().c_str());
    std::string sConfig = "";
    bool fWritten = false;
    if(streamConfigFile)
    {	
       while(getline(streamConfigFile, sLine))
       {
            std::vector<std::string> vEntry = Split(sLine, "=");
            if (vEntry.size() == 2)
            {
                std::string sSourceKey = vEntry[0];
                std::string sSourceValue = vEntry[1];
                boost::to_lower(sSourceKey);
                if (sSourceKey == sKey) 
                {
                    sSourceValue = sValue;
                    sLine = sSourceKey + "=" + sSourceValue;
                    fWritten = true;
                }
            }
            sLine = strReplace(sLine,"\r", "");
            sLine = strReplace(sLine,"\n", "");
			if (!sLine.empty())
			{
				sLine += sDelimiter;
				sConfig += sLine;
			}
       }
    }
    if (!fWritten) 
    {
        sLine = sKey + "=" + sValue + sDelimiter;
        sConfig += sLine;
    }
    
    streamConfigFile.close();
    FILE *outFile = fopen(pathConfigFile.string().c_str(), "w");
    fputs(sConfig.c_str(), outFile);
    fclose(outFile);
    ReadConfigFile(pathConfigFile.string().c_str());
    return true;
}

bool InstantiateOneClickMiningEntries()
{
	WriteKey("addnode","node.biblepay.org");
	WriteKey("addnode","explorer.biblepay.org");
	WriteKey("genproclimit", "1");
	// WriteKey("poolport","80");
	// WriteKey("workerid","");
	// WriteKey("pool","http://pool.biblepay.org");
	WriteKey("gen","1");
	return true;
}

std::string GetCPKData(std::string sProjectId, std::string sPK)
{
	return ReadCache(sProjectId, sPK);
}

bool AdvertiseChristianPublicKeypair(std::string sProjectId, std::string sNickName, std::string sEmail, std::string sVendorType, bool fUnJoin, bool fForce, CAmount nFee, std::string sOptData, std::string &sError)
{	
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	boost::to_lower(sProjectId);

	if (sProjectId == "cpk-bmsuser")
	{
		sCPK = DefaultRecAddress("PUBLIC-FUNDING-ADDRESS");
		bool fExists = NickNameExists(sProjectId, sNickName);
		if (fExists && false) 
		{
			sError = "Sorry, BMS Nick Name is already taken.";
			return false;
		}
	}
	else
	{
		if (!sNickName.empty())
		{
			bool fExists = NickNameExists("cpk", sNickName);
			if (fExists) 
			{
				sError = "Sorry, NickName is already taken.";
				return false;
			}
		}
	}

	std::string sRec = GetCPKData(sProjectId, sCPK);
	if (fUnJoin)
	{
		if (sRec.empty()) {
			sError = "Sorry, you are not enrolled in this project.";
			return false;
		}
	}
	else if (!sRec.empty() && !fForce) 
    {
		sError = "ALREADY_IN_CHAIN";
		return false;
    }

	

	if (sNickName.length() > 20 && sVendorType.empty())
	{
		sError = "Sorry, nickname length must be 10 characters or less.";
		return false;
	}

	double nLastCPK = ReadCacheDouble(sProjectId);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	if ((chainActive.Tip()->nHeight - nLastCPK) < 4 && nLastCPK > 0)
    {
		sError = _("A CPK was advertised less then 4 blocks ago. Please wait for your CPK to enter the chain.");
        return false;
    }

    CAmount nBalance = pwalletMain->GetBalance();
	double nCPKAdvertisementFee = GetSporkDouble("CPKAdvertisementFee", 1);    
	if (nFee == 0) 
		nFee = nCPKAdvertisementFee * COIN;
    
    if (nBalance < nFee)
    {
        sError = "Balance too low to advertise CPK, 1 BBP minimum is required.";
        return false;
    }
	
	sNickName = SanitizeString(sNickName);
	LIMITED_STRING(sNickName, 20);
	std::string sMsg = GetRandHash().GetHex();
	std::string sPK = sCPK;
	if (fUnJoin) sPK = "";	
	std::string sData = sPK + "|" + sNickName + "|" + RoundToString(GetAdjustedTime(),0) + "|" + sMsg;
	std::string sSignature;
	bool bSigned = false;
	bSigned = SignStake(sCPK, sMsg, sError, sSignature);
	// Only append the signature after we prove they can sign...
	if (bSigned)
	{
		sData += "|" + sSignature + "|" + sEmail + "|" + sVendorType + "|" + sOptData;
	}
	else
	{
		sError = "Unable to sign CPK " + sCPK + " (" + sError + ")";
		return false;
	}
	
	std::string sSigGSC;
	bSigned = SignStake(sCPK, sMsg, sError, sSigGSC);
	std::string sExtraGscPayload = "<gscsig>" + sSigGSC + "</gscsig><abncpk>" + sCPK + "</abncpk><abnmsg>" + sMsg + "</abnmsg>";
	std::string sResult = SendBlockchainMessage(sProjectId, sCPK, sData, nFee/COIN, false, sExtraGscPayload, sError);
	if (!sError.empty())
	{
		return false;
	}
	WriteCacheDouble(sProjectId, chainActive.Tip()->nHeight);
	return true;
}

std::string GetTransactionMessage(CTransactionRef tx)
{
	std::string sMsg;
	for (unsigned int i = 0; i < tx->vout.size(); i++) 
	{
		sMsg += tx->vout[i].sTxOutMessage;
	}
	return sMsg;
}

bool CheckAntiBotNetSignature(CTransactionRef tx, std::string sType, std::string sSolver)
{
	std::string sXML = GetTransactionMessage(tx);
	std::string sSig = ExtractXML(sXML, "<" + sType + "sig>", "</" + sType + "sig>");
	std::string sMessage = ExtractXML(sXML, "<abnmsg>", "</abnmsg>");
	std::string sPPK = ExtractXML(sMessage, "<ppk>", "</ppk>");
	double dCheckPoolSigs = GetSporkDouble("checkpoolsigs", 0);

	if (!sSolver.empty() && !sPPK.empty() && dCheckPoolSigs == 1)
	{
		if (sSolver != sPPK)
		{
			LogPrintf("CheckAntiBotNetSignature::Pool public key != solver public key, signature %s \n", "rejected");
			return false;
		}
	}
	for (unsigned int i = 0; i < tx->vout.size(); i++)
	{
		const CTxOut& txout = tx->vout[i];
		std::string sAddr = PubKeyToAddress(txout.scriptPubKey);
		std::string sError;
		bool fSigned = CheckStakeSignature(sAddr, sSig, sMessage, sError);
		if (fSigned)
			return true;
	}
	return false;
}

double GetVINCoinAge(int64_t nBlockTime, CTransactionRef tx, bool fDebug)
{
	double dTotal = 0;
	std::string sDebugData = "\nGetVINCoinAge: ";
	for (int i = 0; i < (int)tx->vin.size(); i++) 
	{
    	int n = tx->vin[i].prevout.n;
		CAmount nAmount = 0;
		int64_t nTime = 0;
		bool fOK = GetTransactionTimeAndAmount(tx->vin[i].prevout.hash, n, nTime, nAmount);
		double nSancScalpingDisabled = GetSporkDouble("preventsanctuaryscalping", 0);
		if (nSancScalpingDisabled == 1 && nAmount == (SANCTUARY_COLLATERAL * COIN)) 
		{
			LogPrintf("\nGetVinCoinAge, Detected unlocked sanctuary in txid %s, Amount %f ", tx->GetHash().GetHex(), nAmount/COIN);
			nAmount = 0;
		}
		if (fOK && nTime > 0 && nAmount > 0)
		{
			double nAge = (nBlockTime - nTime) / (86400 + .01);
			if (nAge > 365) nAge = 365;           
			if (nAge < 0)   nAge = 0;
			double dWeight = nAge * (nAmount / COIN);
			dTotal += dWeight;
			if (fDebug)
				sDebugData += "Output #" + RoundToString(i, 0) + ", Weight: " + RoundToString(dWeight, 2) + ", Age: " + RoundToString(nAge, 2) + ", Amount: " + RoundToString(nAmount / COIN, 2) 
				+ ", TxTime: " + RoundToString(nTime, 0) + "...";
		}
	}
	if (fDebug)
		WriteCache("vin", "coinage", sDebugData, GetAdjustedTime());
	return dTotal;
}

double GetAntiBotNetWeight(int64_t nBlockTime, CTransactionRef tx, bool fDebug, std::string sSolver)
{
	double nCoinAge = GetVINCoinAge(nBlockTime, tx, fDebug);
	bool fSigned = CheckAntiBotNetSignature(tx, "abn", sSolver);
	if (!fSigned) 
	{
		if (fDebugSpam && fDebug && nCoinAge > 0)
			LogPrintf("antibotnetsignature failed on tx %s with purported coin-age of %f \n",tx->GetHash().GetHex(), nCoinAge);
		return 0;
	}
	return nCoinAge;
}

static int64_t miABNTime = 0;
static CWalletTx mtxABN;
static std::string msABNXML;
static std::string msABNError;
static bool mfABNSpent = false;
static std::mutex cs_abn;

void SpendABN()
{
	mfABNSpent = true;
	miABNTime = 0;
}

CWalletTx GetAntiBotNetTx(CBlockIndex* pindexLast, double nMinCoinAge, CReserveKey& reservekey, std::string& sXML, std::string sPoolMiningPublicKey, std::string& sError)
{
	// Share the ABN among all threads, until it's spent or expires
	int64_t nAge = GetAdjustedTime() - miABNTime;
	if (nAge > (60 * 10) || mfABNSpent)
	{
        std::unique_lock<std::mutex> lock(cs_abn);
		{
			mtxABN = CreateAntiBotNetTx(pindexLast, nMinCoinAge, reservekey, sXML, sPoolMiningPublicKey, sError);
			mfABNSpent = false;
			miABNTime = GetAdjustedTime();
			msABNXML = sXML;
			msABNError = sError;
			return mtxABN;
		}
	}
	else
	{
		sXML = msABNXML;
		sError = msABNError;
		return mtxABN;
	}
}

CWalletTx CreateAntiBotNetTx(CBlockIndex* pindexLast, double nMinCoinAge, CReserveKey& reservekey, std::string& sXML, std::string sPoolMiningPublicKey, std::string& sError)
{
		CWalletTx wtx;
		CAmount nReqCoins = 0;
		double nABNWeight = pwalletMain->GetAntiBotNetWalletWeight(0, nReqCoins);
	
		if (nABNWeight < nMinCoinAge) 
		{
			sError = "Sorry, your coin-age is too low to create an anti-botnet transaction.";
			return wtx;
		}

		if (pwalletMain->IsLocked() && msEncryptedString.empty())
		{
			WriteCache("poolthread0", "poolinfo4", "Unable to create abn tx (wallet locked)", GetAdjustedTime());
			sError = "Sorry, the wallet must be unlocked to create an anti-botnet transaction.";
			return wtx;
		}
		// In Phase 2, we do a dry run to assess the required Coin Amount in the Coin Stake
		nABNWeight = pwalletMain->GetAntiBotNetWalletWeight(nMinCoinAge, nReqCoins);
		CAmount nBalance = pwalletMain->GetBalance();
		if (fDebug && fDebugSpam)
			LogPrintf("\nABN Tx Total Bal %f, Needed %f, ABNWeight %f ", (double)nBalance/COIN, (double)nReqCoins/COIN, nABNWeight);

		if (nReqCoins > nBalance)
		{
			LogPrintf("\nCreateAntiBotNetTx::Wallet Total Bal %f (>6 confirms), Needed %f (>5 confirms), ABNWeight %f ", 
				(double)nBalance/COIN, (double)nReqCoins/COIN, nABNWeight);
			sError = "Sorry, your balance of " + RoundToString(nBalance/COIN, 2) + " is lower than the required ABN transaction amount of " + RoundToString(nReqCoins/COIN, 2) + " when seeking coins aged > 5 confirms deep.";
			return wtx;
		}
		if (nReqCoins < (1 * COIN))
		{
			sError = "Sorry, no coins available for an ABN transaction.";
			return wtx;
		}
		// BiblePay - Use Encrypted string if we have it
		bool bTriedToUnlock = false;
		if (pwalletMain->IsLocked() && !msEncryptedString.empty())
		{
			bTriedToUnlock = true;
			if (!pwalletMain->Unlock(msEncryptedString, false))
			{
				static int nNotifiedOfUnlockIssue = 0;
				if (nNotifiedOfUnlockIssue == 0)
					LogPrintf("\nUnable to unlock wallet with SecureString.\n");
				nNotifiedOfUnlockIssue++;
				sError = "Unable to unlock wallet with autounlock password provided";
				return wtx;
			}
		}

		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		CBitcoinAddress baCPKAddress(sCPK);
		CScript spkCPKScript = GetScriptForDestination(baCPKAddress.Get());
		std::string sMessage = GetRandHash().GetHex();
		if (!sPoolMiningPublicKey.empty())
		{
			sMessage = "<nonce>" + GetRandHash().GetHex() + "</nonce><ppk>" + sPoolMiningPublicKey + "</ppk>";
		}
		sXML += "<MT>ABN</MT><abnmsg>" + sMessage + "</abnmsg>";
		std::string sSignature;
		std::string strError;
		bool bSigned = SignStake(sCPK, sMessage, sError, sSignature);
		if (!bSigned) 
		{
			if (bTriedToUnlock)
				pwalletMain->Lock();
			sError = "CreateABN::Failed to sign.";
			return wtx;
		}
		sXML += "<abnsig>" + sSignature + "</abnsig><abncpk>" + sCPK + "</abncpk><abnwgt>" + RoundToString(nMinCoinAge, 0) + "</abnwgt>";
		bool fCreated = false;		
		std::string sDebugInfo;
		std::string sMiningInfo;
		CAmount nUsed = 0;
		double nTargetABNWeight = pwalletMain->GetAntiBotNetWalletWeight(nMinCoinAge, nUsed);
		int nChangePosRet = -1;
		bool fSubtractFeeFromAmount = true;
		CAmount nAllocated = nUsed;
		CAmount nSpending = nAllocated - (1 * COIN);
		CAmount nConsumed = 0;
		std::vector<CRecipient> vecSend;
				
		double dChangeQty = cdbl(GetArg("-changequantity", "10"), 2);
		if (dChangeQty < 01) dChangeQty = 1;
		if (dChangeQty > 50) dChangeQty = 50;
		double iQty = (nAllocated / COIN) > nMinCoinAge ? dChangeQty : 1;
		double nEach = (double)1 / iQty;
		for (int i = 0; i < iQty; i++)
		{
			CAmount nIndividualAmount = nSpending * nEach;
			CRecipient recipient = {spkCPKScript, nIndividualAmount, false, fSubtractFeeFromAmount};
			vecSend.push_back(recipient);
			nConsumed += nIndividualAmount;
		}

		CAmount nFeeRequired = 0;
		fCreated = pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false, 0, sXML, nMinCoinAge, nAllocated);
		double nTest = GetAntiBotNetWeight(chainActive.Tip()->GetBlockTime(), wtx.tx, true, "");
		sDebugInfo = "TargetWeight=" + RoundToString(nTargetABNWeight, 0) + ", UsingBBP=" + RoundToString((double)nConsumed/COIN, 2) 
				+ ", SpendingBBP=" + RoundToString((double)nAllocated/COIN, 2) + ", NeededWeight=" + RoundToString(nMinCoinAge, 0) + ", GotWeight=" + RoundToString(nTest, 2);
		sMiningInfo = "[" + RoundToString(nMinCoinAge, 0) + " ABN OK] Amount=" + RoundToString(nUsed/COIN, 2) + ", Weight=" + RoundToString(nTest, 2);
		if (fDebug)
		{
			LogPrintf(" CreateABN::%s", sDebugInfo);
		}
		if (fCreated && nTest >= nMinCoinAge)
		{
			// Bubble ABN info to user
			WriteCache("poolthread0", "poolinfo4", sMiningInfo, GetAdjustedTime());
		}
	
		if (bTriedToUnlock)
			pwalletMain->Lock();
		if (!fCreated)    
		{
			sError = "CreateABN::Fail::" + strError + "::" + sDebugInfo;
			return wtx;
		}
		return wtx;
}

double GetABNWeight(const CBlock& block, bool fMining)
{
	if (block.vtx.size() < 1) return 0;
	std::string sMsg = GetTransactionMessage(block.vtx[0]);
	std::string sSolver = PubKeyToAddress(block.vtx[0]->vout[0].scriptPubKey);
	int nABNLocator = (int)cdbl(ExtractXML(sMsg, "<abnlocator>", "</abnlocator>"), 0);
	if (block.vtx.size() < nABNLocator) return 0;
	CTransactionRef tx = block.vtx[nABNLocator];
	double dWeight = GetAntiBotNetWeight(block.GetBlockTime(), tx, true, sSolver);
	return dWeight;
}

bool CheckABNSignature(const CBlock& block, std::string& out_CPK)
{
	if (block.vtx.size() < 1) return 0;
	std::string sSolver = PubKeyToAddress(block.vtx[0]->vout[0].scriptPubKey);
	std::string sMsg = GetTransactionMessage(block.vtx[0]);
	int nABNLocator = (int)cdbl(ExtractXML(sMsg, "<abnlocator>", "</abnlocator>"), 0);
	if (block.vtx.size() < nABNLocator) return 0;
	CTransactionRef tx = block.vtx[nABNLocator];
	out_CPK = ExtractXML(tx->GetTxMessage(), "<abncpk>", "</abncpk>");
	return CheckAntiBotNetSignature(tx, "abn", sSolver);
}

std::string GetPOGBusinessObjectList(std::string sType, std::string sFields)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
	if (chainActive.Tip()->nHeight < consensusParams.EVOLUTION_CUTOVER_HEIGHT) 
		return "";
	
	CPK myCPK = GetMyCPK("cpk");
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
    std::string sData;  
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(iNextSuperblock);
	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;
		
	std::string sContract = GetGSCContract(iNextSuperblock, false);
	std::string s1 = ExtractXML(sContract, "<DATA>", "</DATA>");
	std::string sDetails = ExtractXML(sContract, "<DETAILS>", "</DETAILS>");
	std::vector<std::string> vData = Split(sType =="pog" ? s1.c_str() : sDetails.c_str(), "\n");
	//	Detail Row Format: sCampaignName + "|" + CPKAddress + "|" + nPoints + "|" + nProminence + "|" + Members.second.sNickName 
	//	Leaderboard Fields: "campaign,nickname,cpk,points,owed,prominence";

	double dTotalPaid = 0;
	double nTotalPoints = 0;
	double nMyPoints = 0;
	double dLimit = (double)nPaymentsLimit / COIN;
	for (int i = 0; i < vData.size(); i++)
	{
		std::vector<std::string> vRow = Split(vData[i].c_str(), "|");
		if (vRow.size() >= 6)
		{
			std::string sCampaign = vRow[0];
			std::string sCPK = vRow[1];
			double nPoints = cdbl(vRow[2], 2);
			nTotalPoints += nPoints;
			double nProminence = cdbl(vRow[3], 8) * 100;
			std::string sNickName = Caption(vRow[4], 10);
			if (sNickName.empty())
				sNickName = "N/A";
			double nOwed = (sType=="_pog") ?  cdbl(vRow[5], 4) : dLimit * nProminence / 100;
			if (sCPK == myCPK.sAddress)
				nMyPoints += nPoints;
			std::string sRow = sCampaign + "<col>" + sNickName + "<col>" + sCPK + "<col>" + RoundToString(nPoints, 2) 
				+ "<col>" + RoundToString(nOwed, 2) 
				+ "<col>" + RoundToString(nProminence, 2) + "<object>";
			sData += sRow;
		}
	}
	double dPD = 1;
	sData += "<difficulty>" + RoundToString(GetDifficulty(chainActive.Tip()), 2) + "</difficulty>";
	sData += "<my_points>" + RoundToString(nMyPoints, 0) + "</my_points>";
	sData += "<my_nickname>" + myCPK.sNickName + "</my_nickname>";
	sData += "<total_points>" + RoundToString(nTotalPoints, 0) + "</total_points>";
	sData += "<participants>"+ RoundToString(vData.size() - 1, 0) + "</participants>";
	sData += "<lowblock>" + RoundToString(iNextSuperblock, 0) + "</lowblock>";
	sData += "<highblock>" + RoundToString(iNextSuperblock + BLOCKS_PER_DAY - 1, 0)  + "</highblock>";
	return sData;
}

const CBlockIndex* GetBlockIndexByTransactionHash(const uint256 &hash)
{
	CBlockIndex* pindexHistorical;
	CTransactionRef tx1;
	uint256 hashBlock1;
	if (GetTransaction(hash, tx1, Params().GetConsensus(), hashBlock1, true))
	{
		BlockMap::iterator mi = mapBlockIndex.find(hashBlock1);
		if (mi != mapBlockIndex.end())
			return mapBlockIndex[hashBlock1];     
	}
    return pindexHistorical;
}

CAmount GetTitheTotal(CTransaction tx)
{
	CAmount nTotal = 0;
	const Consensus::Params& consensusParams = Params().GetConsensus();
	double nCheckPODS = GetSporkDouble("tithingcheckpodsaddress", 0);
	double nCheckQT = GetSporkDouble("tithingcheckqtaddress", 0);
	for (int i=0; i < (int)tx.vout.size(); i++)
	{
 		std::string sRecipient = PubKeyToAddress(tx.vout[i].scriptPubKey);
		if (sRecipient == consensusParams.FoundationAddress || (sRecipient == consensusParams.FoundationPODSAddress && nCheckPODS == 1) || (sRecipient == consensusParams.FoundationQTAddress && nCheckQT == 1))
		{ 
			nTotal += tx.vout[i].nValue;
		}
	 }
	 return nTotal;
}

CAmount GetNonTitheTotal(CTransaction tx)
{
	CAmount nTotal = 0;
	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::string sBlk = GetSporkValue("RcvBlk");
	if (!sBlk.empty())
	{
		double nLow = GetSporkDouble("RcvBlkLow", 0);
		double nHigh = GetSporkDouble("RcvBlkHigh", 0);
		for (int i = 0; i < (int)tx.vout.size(); i++)
		{
 			std::string sRecip = PubKeyToAddress(tx.vout[i].scriptPubKey);
			if (!sRecip.empty() && Contains(sBlk, sRecip))
			{
				double nAmt = (double)tx.vout[i].nValue / COIN;
				if (nAmt > nLow && nAmt < nHigh)
					nTotal += tx.vout[i].nValue;
			}
		}
	}
	return nTotal;
}

double AddVector(std::string sData, std::string sDelim)
{
	double dTotal = 0;
	std::vector<std::string> vAdd = Split(sData.c_str(), sDelim);
	for (int i = 0; i < (int)vAdd.size(); i++)
	{
		std::string sElement = vAdd[i];
		double dAmt = cdbl(sElement, 2);
		dTotal += dAmt;
	}
	return dTotal;
}

int ReassessAllChains()
{
    int iProgress = 0;
    LOCK(cs_main);
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
	{
		if (item.second != NULL) 
			setTips.insert(item.second);
	}

    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
		if (item.second != NULL)
		{
			if (item.second->pprev != NULL)
			{
				const CBlockIndex* pprev = item.second->pprev;
				if (pprev)
					setTips.erase(pprev);
			}
		}
    }

    int nBranchMin = -1;
    int nCountMax = INT_MAX;

	BOOST_FOREACH(const CBlockIndex* block, setTips)
    {
        const CBlockIndex* pindexFork = chainActive.FindFork(block);
        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        if(branchLen < nBranchMin)
			continue;

        if(nCountMax-- < 1) 
			break;

		if (block->nHeight > (chainActive.Tip()->nHeight - (BLOCKS_PER_DAY * 5)))
		{
			bool fForked = !chainActive.Contains(block);
			if (fForked)
			{
				uint256 hashFork(uint256S(block->phashBlock->GetHex()));
				CBlockIndex* pblockindex = mapBlockIndex[hashFork];
				if (pblockindex != NULL)
				{
					LogPrintf("\nReassessAllChains::Working on Fork %s at height %f ", hashFork.GetHex(), block->nHeight);
					ResetBlockFailureFlags(pblockindex);
					iProgress++;
				}
			}
		}
	}
	
	CValidationState state;
	ActivateBestChain(state, Params());
	return iProgress;
}

double GetFees(CTransactionRef tx)
{
	CAmount nFees = 0;
	CAmount nValueIn = 0;
	CAmount nValueOut = 0;
	for (int i = 0; i < (int)tx->vin.size(); i++) 
	{
    	int n = tx->vin[i].prevout.n;
		CAmount nAmount = 0;
		int64_t nTime = 0;
		bool fOK = GetTransactionTimeAndAmount(tx->vin[i].prevout.hash, n, nTime, nAmount);
		if (fOK && nTime > 0 && nAmount > 0)
		{
			nValueIn += nAmount;
		}
	}
	for (int i = 0; i < (int)tx->vout.size(); i++)
	{
		nValueOut += tx->vout[i].nValue;
	}
	nFees = nValueIn - nValueOut;
	if (fDebug)
		LogPrintf("GetFees::ValueIn %f, ValueOut %f, nFees %f ", (double)nValueIn/COIN, (double)nValueOut/COIN, (double)nFees/COIN);
	return nFees;
}

void LogPrintWithTimeLimit(std::string sSection, std::string sValue, int64_t nMaxAgeInSeconds)
{
	int64_t nAge = GetCacheEntryAge(sSection, sValue);
	if (nAge < nMaxAgeInSeconds) 
		return;
	// Otherwise, print the log
	LogPrintf("%s::%s", sSection, sValue);
	WriteCache(sSection, sValue, sValue, GetAdjustedTime());
}

double GetROI(double nTitheAmount)
{
	// For a given Tithe Amount, return the conceptual ROI
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

	double dPPP = dTotalPaid / nTotalPoints;
	CAmount nTotalReq;
	double dCoinAge = pwalletMain->GetAntiBotNetWalletWeight(0, nTotalReq);
	double nPoints = cbrt(nTitheAmount) * dCoinAge;
	double nEarned = (dPPP * nPoints) - nTitheAmount;
	double nROI = (nEarned / nTitheAmount) * 100;
	return nROI;
}

void ProcessBLSCommand(CTransactionRef tx)
{
	std::string sXML = GetTransactionMessage(tx);
	std::string sEnc = ExtractXML(sXML, "<blscommand>", "</blscommand>");
	LogPrintf("\nBLS Command %s %s ", sXML, sEnc);

	if (msMasterNodeLegacyPrivKey.empty())
		return;

	if (sEnc.empty()) 
		return;
	// Decrypt using the masternodeprivkey
	std::string sCommand = DecryptAES256(sEnc, msMasterNodeLegacyPrivKey);
	if (!sCommand.empty())
	{
		std::vector<std::string> vCmd = Split(sCommand, "=");
		if (vCmd.size() == 2)
		{
			if (vCmd[0] == "masternodeblsprivkey" && !vCmd[1].empty())
			{
				LogPrintf("\nProcessing bls command %s with %s", vCmd[0], vCmd[1]);
				WriteKey(vCmd[0], vCmd[1]);
				// At this point we should re-read the masternodeblsprivkey.  Then ensure the activeMasternode info is updated and the deterministic masternode starts.
			}
		}

	}
}

void UpdateHealthInformation()
{
	// This is an optional BiblePay feature where we will display your sanctuaries health information in the pool.
	// This allows you to see your sancs health even if you host with one of our turnkey providers (such as Apollon).
	// Health information currently includes:  Best BlockHash, Best Chain Height, Sanctuary Vote - Popular Contract - Vote Level, Sanctuary Public IP
	// The primary benefit to doing this is this allows a user to know a sanc needs resynced, and to ensure our blockhashes as a community.
	// For example, if we receive a post on the forum claiming a home users node is forked, we can run this report and assess the network situation globally.
	// Additionally this will allow us as a community to see that the entire GSC health vote levels match (this ensure gobject propagation integrity).
	// To enable the feature, set the key:  healthupdate=1 (this is the default for a sanctuary), or healthupdate=0 (to disable this on your sanctuary).
	// Note: This code only works on sanctuaries (it does not work on home distributions).
	// Assess GSC Health
	double iEnabled = cdbl(GetArg("-healthupdate", "1"), 0);
	if (iEnabled == 0)
		return;

	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	std::string sAddresses;
	std::string sAmounts;
	int iVotes = 0;
	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPAMHash = uint256S("0x0");
	GetGSCGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
	std::string sXML;
	uint256 hPam = GetPAMHash(sAddresses, sAmounts);
	sXML = "<GSCVOTES>" + RoundToString(iVotes, 0) + "</GSCVOTES><PAMHASH>" + hPam.GetHex() + "</PAMHASH>";
	sXML += "<BLOCKS>" + RoundToString(chainActive.Tip()->nHeight, 0) + "</BLOCKS>";
	sXML += "<HASH>" + chainActive.Tip()->GetBlockHash().GetHex() + "</HASH>";
	// Post the Health information
	int SSL_PORT = 443;
	int TRANSMISSION_TIMEOUT = 30000;
	int CONNECTION_TIMEOUT = 15;
	std::string sResponse;
	std::string sHealthPage = "Action.aspx?action=health-post";
	std::string sHost = "https://" + GetSporkValue("pool");
	sResponse = BiblepayHTTPSPost(true, 0, "POST", "", "", sHost, sHealthPage, SSL_PORT, sXML, CONNECTION_TIMEOUT, TRANSMISSION_TIMEOUT, 0);
	std::string sError = ExtractXML(sResponse,"<ERROR>","</ERROR>");
	std::string sResponseInner = ExtractXML(sResponse,"<RESPONSE>","</RESPONSE>");
	if (fDebugSpam)
		LogPrintf("UpdateHealthInformation %s %s", sError, sResponseInner);
}

std::string SearchChain(int nBlocks, std::string sDest)
{
	if (!chainActive.Tip()) 
		return std::string();
	int nMaxDepth = chainActive.Tip()->nHeight;
	int nMinDepth = nMaxDepth - nBlocks;
	if (nMinDepth < 1) 
		nMinDepth = 1;
	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::string sData;
	CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
	while (pindex && pindex->nHeight < nMaxDepth)
	{
		if (pindex->nHeight < chainActive.Tip()->nHeight) 
			pindex = chainActive.Next(pindex);
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int n = 0; n < block.vtx.size(); n++)
			{
				std::string sMsg = GetTransactionMessage(block.vtx[n]);
				std::string sCPK = ExtractXML(sMsg, "<cpk>", "</cpk>");
				std::string sUSD = ExtractXML(sMsg, "<amount_usd>", "</amount_usd>");
				std::string sChildID = ExtractXML(sMsg, "<childid>", "</childid>");
				boost::trim(sChildID);

				for (int i = 0; i < block.vtx[n]->vout.size(); i++)
				{
					double dAmount = block.vtx[n]->vout[i].nValue / COIN;
					std::string sPK = PubKeyToAddress(block.vtx[n]->vout[i].scriptPubKey);
					if (sPK == sDest && dAmount > 0 && !sChildID.empty())
					{
						std::string sRow = "<row><block>" + RoundToString(pindex->nHeight, 0) + "</block><destination>" + sPK + "</destination><cpk>" + sCPK + "</cpk><childid>" 
							+ sChildID + "</childid><amount>" + RoundToString(dAmount, 2) + "</amount><amount_usd>" 
							+ sUSD + "</amount_usd><txid>" + block.vtx[n]->GetHash().GetHex() + "</txid></row>";
						sData += sRow;
					}
				}
			}
		}
	}
	return sData;
}