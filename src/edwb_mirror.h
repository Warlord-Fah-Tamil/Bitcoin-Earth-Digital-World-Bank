#ifndef BITCOIN_EDWB_MIRROR_H
#define BITCOIN_EDWB_MIRROR_H

#include <cstdint>
#include <primitives/block.h>

struct MainnetBlockMirror
{
    CBlock block;
    int64_t nReceivedTime{0};
};

bool IsMainnetMirrorQueueFull();

bool PushMainnetMirrorBlock(MainnetBlockMirror mirror_block);

bool PopNextMainnetBlock(MainnetBlockMirror& mirror_block);

#endif // BITCOIN_EDWB_MIRROR_H
