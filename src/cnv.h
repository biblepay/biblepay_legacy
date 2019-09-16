// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_CNV_H
#define BITCOIN_CNV_H

#include "chain.h"

void initcnv();
std::string GetVerseCNV(std::string sBook, int iChapter, int iVerse, int iBookStart, int iBookEnd);

#endif
