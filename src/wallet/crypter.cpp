// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypter.h"

#include "script/script.h"
#include "script/standard.h"
#include "util.h"
#include <openssl/md5.h>

#include <string>
#include <vector>
#include <boost/foreach.hpp>
#include <openssl/aes.h>
#include <openssl/evp.h>

unsigned char chKeyBiblePay[256];
unsigned char chIVBiblePay[256];
bool fKeySetBiblePay;


bool CCrypter::SetKeyFromPassphrase(const SecureString& strKeyData, const std::vector<unsigned char>& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod)
{
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
        return false;

    int i = 0;
    if (nDerivationMethod == 0)
        i = EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha512(), &chSalt[0],
                          (unsigned char *)&strKeyData[0], strKeyData.size(), nRounds, chKey, chIV);

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        memory_cleanse(chKey, sizeof(chKey));
        memory_cleanse(chIV, sizeof(chIV));
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV)
{
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE || chNewIV.size() != WALLET_CRYPTO_KEY_SIZE)
        return false;

    memcpy(&chKey[0], &chNewKey[0], sizeof chKey);
    memcpy(&chIV[0], &chNewIV[0], sizeof chIV);

    fKeySet = true;
    return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext)
{
    if (!fKeySet)
        return false;

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCK_SIZE - 1 bytes
    int nLen = vchPlaintext.size();
    int nCLen = nLen + AES_BLOCK_SIZE, nFLen = 0;
    vchCiphertext = std::vector<unsigned char> (nCLen);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (!ctx) return false;

    bool fOk = true;

    EVP_CIPHER_CTX_init(ctx);
    if (fOk) fOk = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKey, chIV) != 0;
    if (fOk) fOk = EVP_EncryptUpdate(ctx, &vchCiphertext[0], &nCLen, &vchPlaintext[0], nLen) != 0;
    if (fOk) fOk = EVP_EncryptFinal_ex(ctx, (&vchCiphertext[0]) + nCLen, &nFLen) != 0;
    EVP_CIPHER_CTX_cleanup(ctx);

    EVP_CIPHER_CTX_free(ctx);

    if (!fOk) return false;

    vchCiphertext.resize(nCLen + nFLen);
    return true;
}

bool CCrypter::Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext)
{
    if (!fKeySet)
        return false;

    // plaintext will always be equal to or lesser than length of ciphertext
    int nLen = vchCiphertext.size();
    int nPLen = nLen, nFLen = 0;

    vchPlaintext = CKeyingMaterial(nPLen);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (!ctx) return false;

    bool fOk = true;

    EVP_CIPHER_CTX_init(ctx);
    if (fOk) fOk = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKey, chIV) != 0;
    if (fOk) fOk = EVP_DecryptUpdate(ctx, &vchPlaintext[0], &nPLen, &vchCiphertext[0], nLen) != 0;
    if (fOk) fOk = EVP_DecryptFinal_ex(ctx, (&vchPlaintext[0]) + nPLen, &nFLen) != 0;
    EVP_CIPHER_CTX_cleanup(ctx);

    EVP_CIPHER_CTX_free(ctx);

    if (!fOk) return false;

    vchPlaintext.resize(nPLen + nFLen);
    return true;
}

bool LoadBibleKey(std::string biblekey, std::string salt)
{
	const char* chBibleKey = biblekey.c_str();
	const char* chSalt = salt.c_str();
	OPENSSL_cleanse(chKeyBiblePay, sizeof(chKeyBiblePay));
    OPENSSL_cleanse(chIVBiblePay, sizeof(chIVBiblePay));
    EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha512(),(unsigned char *)chSalt,
		(unsigned char *)chBibleKey, 
		strlen(chBibleKey), 1,
		chKeyBiblePay, chIVBiblePay);
    fKeySetBiblePay = true;
    return true;
}


std::vector<unsigned char> StringToVector(std::string sData)
{
        std::vector<unsigned char> v(sData.begin(), sData.end());
		return v;
}

std::string VectorToString(std::vector<unsigned char> v)
{
        std::string s(v.begin(), v.end());
        return s;
}

std::string PubKeyToAddress(const CScript& scriptPubKey)
{
	CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);
    return address2.ToString();
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
		return "";
	}
}

void PrintStratisKeyDebugInfo()
{
	std::string sKey1(reinterpret_cast<char*>(chKeyBiblePay));
	std::string sIV1(reinterpret_cast<char*>(chIVBiblePay));
	std::string sRow = "chKeyBiblePay: ";
	for (int i = 0; i < (int)sKey1.length(); i++)
	{
		int ichar = (int)sKey1[i];
		sRow += RoundToString(ichar,0) + ",";
	}
	sRow += "  chIVBiblePay: ";
	for (int i = 0; i < (int)sIV1.length(); i++)
	{
		int ichar = (int)sIV1[i];
		sRow += RoundToString(ichar,0) + ",";
	}
	LogPrintf(" \n StratisKeyDebugInfo: %s \n",sRow.c_str());
}

bool BibleEncrypt(std::vector<unsigned char> vchPlaintext, std::vector<unsigned char> &vchCiphertext)
{
	if (!fKeySetBiblePay) LoadBibleKey("biblepay","eb5a781ea9da2ef36");
    int nLen = vchPlaintext.size();
    int nCLen = nLen + AES_BLOCK_SIZE, nFLen = 0;
    vchCiphertext = std::vector<unsigned char> (nCLen);
	//EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool fOk = true;
    EVP_CIPHER_CTX_init(ctx);
	if (fOk) fOk = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKeyBiblePay, chIVBiblePay);
    if (fOk) fOk = EVP_EncryptUpdate(ctx, &vchCiphertext[0], &nCLen, &vchPlaintext[0], nLen);
    if (fOk) fOk = EVP_EncryptFinal_ex(ctx, (&vchCiphertext[0])+nCLen, &nFLen);
    EVP_CIPHER_CTX_free(ctx);
    if (!fOk) return false;
    vchCiphertext.resize(nCLen + nFLen);
    return true;
}

bool BibleDecrypt(const std::vector<unsigned char>& vchCiphertext,std::vector<unsigned char>& vchPlaintext)
{
	LoadBibleKey("biblepay","eb5a781ea9da2ef36");
	int nLen = vchCiphertext.size();
    int nPLen = nLen, nFLen = 0;
    //EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool fOk = true;
    EVP_CIPHER_CTX_init(ctx);
    if (fOk) fOk = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKeyBiblePay, chIVBiblePay);
    if (fOk) fOk = EVP_DecryptUpdate(ctx, &vchPlaintext[0], &nPLen, &vchCiphertext[0], nLen);
    if (fOk) fOk = EVP_DecryptFinal_ex(ctx, (&vchPlaintext[0])+nPLen, &nFLen);
    EVP_CIPHER_CTX_free(ctx);
    if (!fOk) return false;
    vchPlaintext.resize(nPLen + nFLen);
    return true;
}



static bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV, std::vector<unsigned char> &vchCiphertext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);
    if(!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Encrypt(*((const CKeyingMaterial*)&vchPlaintext), vchCiphertext);
}


// General secure AES 256 CBC encryption routine
bool EncryptAES256(const SecureString& sKey, const SecureString& sPlaintext, const std::string& sIV, std::string& sCiphertext)
{
    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCK_SIZE - 1 bytes
    int nLen = sPlaintext.size();
    int nCLen = nLen + AES_BLOCK_SIZE;
    int nFLen = 0;

    // Verify key sizes
    if(sKey.size() != 32 || sIV.size() != AES_BLOCK_SIZE) {
        LogPrintf("crypter EncryptAES256 - Invalid key or block size: Key: %d sIV:%d\n", sKey.size(), sIV.size());
        return false;
    }

    // Prepare output buffer
    sCiphertext.resize(nCLen);

    // Perform the encryption
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (!ctx) return false;

    bool fOk = true;

    EVP_CIPHER_CTX_init(ctx);
    if (fOk) fOk = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (const unsigned char*) &sKey[0], (const unsigned char*) &sIV[0]);
    if (fOk) fOk = EVP_EncryptUpdate(ctx, (unsigned char*) &sCiphertext[0], &nCLen, (const unsigned char*) &sPlaintext[0], nLen);
    if (fOk) fOk = EVP_EncryptFinal_ex(ctx, (unsigned char*) (&sCiphertext[0])+nCLen, &nFLen);
    EVP_CIPHER_CTX_cleanup(ctx);

    EVP_CIPHER_CTX_free(ctx);

    if (!fOk) return false;

    sCiphertext.resize(nCLen + nFLen);
    return true;
}


static bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext, const uint256& nIV, CKeyingMaterial& vchPlaintext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);
    if(!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial*)&vchPlaintext));
}

bool DecryptAES256(const SecureString& sKey, const std::string& sCiphertext, const std::string& sIV, SecureString& sPlaintext)
{
    // plaintext will always be equal to or lesser than length of ciphertext
    int nLen = sCiphertext.size();
    int nPLen = nLen, nFLen = 0;

    // Verify key sizes
    if(sKey.size() != 32 || sIV.size() != AES_BLOCK_SIZE) {
        LogPrintf("crypter DecryptAES256 - Invalid key or block size\n");
        return false;
    }

    sPlaintext.resize(nPLen);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (!ctx) return false;

    bool fOk = true;

    EVP_CIPHER_CTX_init(ctx);
    if (fOk) fOk = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (const unsigned char*) &sKey[0], (const unsigned char*) &sIV[0]);
    if (fOk) fOk = EVP_DecryptUpdate(ctx, (unsigned char *) &sPlaintext[0], &nPLen, (const unsigned char *) &sCiphertext[0], nLen);
    if (fOk) fOk = EVP_DecryptFinal_ex(ctx, (unsigned char *) (&sPlaintext[0])+nPLen, &nFLen);
    EVP_CIPHER_CTX_cleanup(ctx);

    EVP_CIPHER_CTX_free(ctx);

    if (!fOk) return false;

    sPlaintext.resize(nPLen + nFLen);
    return true;
}


static bool DecryptKey(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCryptedSecret, const CPubKey& vchPubKey, CKey& key)
{
    CKeyingMaterial vchSecret;
    if(!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
        return false;

    if (vchSecret.size() != 32)
        return false;

    key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
    return key.VerifyPubKey(vchPubKey);
}

bool CCryptoKeyStore::SetCrypted()
{
    LOCK(cs_KeyStore);
    if (fUseCrypto)
        return true;
    if (!mapKeys.empty())
        return false;
    fUseCrypto = true;
    return true;
}

bool CCryptoKeyStore::Lock(bool fAllowMixing)
{
    if (!SetCrypted())
        return false;

    if(!fAllowMixing) {
        LOCK(cs_KeyStore);
        vMasterKey.clear();
    }

    fOnlyMixingAllowed = fAllowMixing;
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn, bool fForMixingOnly)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        bool keyPass = false;
        bool keyFail = false;
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
        for (; mi != mapCryptedKeys.end(); ++mi)
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKey key;
            if (!DecryptKey(vMasterKeyIn, vchCryptedSecret, vchPubKey, key))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        if (keyPass && keyFail)
        {
            LogPrintf("The wallet is probably corrupted: Some keys decrypt but not all.\n");
            assert(false);
        }
        if (keyFail || !keyPass)
            return false;
        vMasterKey = vMasterKeyIn;
        fDecryptionThoroughlyChecked = true;
    }
    fOnlyMixingAllowed = fForMixingOnly;
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::AddKeyPubKey(key, pubkey);

        if (IsLocked(true))
            return false;

        std::vector<unsigned char> vchCryptedSecret;
        CKeyingMaterial vchSecret(key.begin(), key.end());
        if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(), vchCryptedSecret))
            return false;

        if (!AddCryptedKey(pubkey, vchCryptedSecret))
            return false;
    }
    return true;
}


bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        mapCryptedKeys[vchPubKey.GetID()] = make_pair(vchPubKey, vchCryptedSecret);
    }
    return true;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetKey(address, keyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut);
        }
    }
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            vchPubKeyOut = (*mi).second.first;
            return true;
        }
        // Check for watch-only pubkeys
        return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
    }
    return false;
}

bool CCryptoKeyStore::EncryptKeys(CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!mapCryptedKeys.empty() || IsCrypted())
            return false;

        fUseCrypto = true;
        BOOST_FOREACH(KeyMap::value_type& mKey, mapKeys)
        {
            const CKey &key = mKey.second;
            CPubKey vchPubKey = key.GetPubKey();
            CKeyingMaterial vchSecret(key.begin(), key.end());
            std::vector<unsigned char> vchCryptedSecret;
            if (!EncryptSecret(vMasterKeyIn, vchSecret, vchPubKey.GetHash(), vchCryptedSecret))
                return false;
            if (!AddCryptedKey(vchPubKey, vchCryptedSecret))
                return false;
        }
        mapKeys.clear();
    }
    return true;
}





/*
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

*/

/*
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
			SendColoredEscrow(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, sScriptComplexOrder);
			results.push_back(Pair("txid",wtx.GetHash().GetHex()));
		}
	}

	else if (sItem == "deprecated_createescrowtransaction")
	{

			results.push_back(Pair("deprecated","Please use createrawtransaction."));
		    LOCK(cs_main);
			std::string sXML = params[1].get_str();
			CMutableTransaction rawTx;
			// Process Inputs
			std::string sInputs = ExtractXML(sXML,"<INPUTS>","</INPUTS>");
			std::vector<std::string> vInputs = sInputs.c_str(),"<ROW>");
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
		std::vector<std::string> vRecips = sRecipients.c_str(),"<ROW>");
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
					  rawTx.vout[rawTx.vout.size()-1]age = sAssetColorScript;
					  LogPrintf("CreateEscrowTx::Adding Recip %s, amount %f, color %s, dAmount %f ", sRecipient.c_str(),(double)nAmount,sAssetColorScript.c_str(), (double)dAmount);
				}
			}
		}
		return EncodeHexTx(rawTx);

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

	    }


*/


