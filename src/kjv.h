// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KJV_H
#define BITCOIN_KJV_H

#include "chain.h"
#include "cnv.h"

void initkjv();
uint256 BibleHashClassic(uint256 hash, int64_t nBlockTime, int64_t nPrevBlockTime, bool bMining, int nPrevHeight, const CBlockIndex* pindexLast, bool bRequireTxIndex, 
	bool f7000, bool f8000, bool f9000, bool fTitheBlocksActive, unsigned int nNonce, const Consensus::Params& params);
uint256 BibleHashV2(uint256 hash, int64_t nBlockTime, int64_t nPrevBlockTime, bool bMining, int nPrevHeight);
std::string GetSin(int iSinNumber, std::string& out_Description);
std::string GetVerse(std::string sBook, int iChapter, int iVerse, int iBookStart, int iBookEnd);
std::string GetBook(int iBookNumber);
std::string GetBookByName(std::string sName);
std::string GetPrayer(int iPrayerNumber, std::string& out_Title);
std::string BibleMD5(std::string sData);
void GetBookStartEnd(std::string sBook, int& iStart, int& iEnd);
bool BibleEncryptE(std::vector<unsigned char> vchPlaintext, std::vector<unsigned char> &vchCiphertext);
extern std::string GetBibleHashVerses(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexPrev);
std::string GetBibleHashVerseNumber(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexPrev, int iVerseNumber);
std::string GetVerseML(std::string sLanguage, std::string sBook, int iChapter, int iVerse, int iBookStart, int iBookEnd);

#endif
