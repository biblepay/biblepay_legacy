// Copyright (c) 2018 The BiblePay Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BIBLEPAY_QUORUMS_INIT_H
#define BIBLEPAY_QUORUMS_INIT_H

class CEvoDB;

namespace llmq
{

void InitLLMQSystem(CEvoDB& evoDb);
void DestroyLLMQSystem();

}

#endif //BIBLEPAY_QUORUMS_INIT_H
