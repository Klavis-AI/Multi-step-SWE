#include "fork.h"
#include <string.h>

static const char *NAMES[F_COUNT] = {
    "Frontier", "Homestead", "DAO", "EIP150", "EIP158", "Byzantium",
    "Constantinople", "ConstantinopleFix", "Istanbul", "MuirGlacier", "Berlin", "London",
    "ArrowGlacier", "GrayGlacier", "Paris", "Shanghai", "Cancun", "Prague", "Osaka"
};

static const struct { const char *alias; fork_id id; } ALIASES[] = {
    { "Frontier", F_FRONTIER },
    { "Homestead", F_HOMESTEAD },
    { "DAO", F_DAO },
    { "Dao", F_DAO },
    { "TangerineWhistle", F_EIP150 },
    { "EIP150", F_EIP150 },
    { "SpuriousDragon", F_EIP158 },
    { "EIP158", F_EIP158 },
    { "Byzantium", F_BYZANTIUM },
    { "Constantinople", F_CONSTANTINOPLE },
    { "ConstantinopleFix", F_PETERSBURG },
    { "Petersburg", F_PETERSBURG },
    { "Istanbul", F_ISTANBUL },
    { "MuirGlacier", F_MUIRGLACIER },
    { "Berlin", F_BERLIN },
    { "London", F_LONDON },
    { "ArrowGlacier", F_ARROWGLACIER },
    { "GrayGlacier", F_GRAYGLACIER },
    { "Merge", F_PARIS },
    { "Paris", F_PARIS },
    { "Shanghai", F_SHANGHAI },
    { "Cancun", F_CANCUN },
    { "Prague", F_PRAGUE },
    { "Osaka", F_OSAKA },
    { NULL, F_FRONTIER }
};

int fork_by_name(const char *name)
{
    for (int i = 0; ALIASES[i].alias; i++)
        if (!strcmp(ALIASES[i].alias, name)) return (int)ALIASES[i].id;
    return -1;
}

const char *fork_name(fork_id f) { return (f < F_COUNT) ? NAMES[f] : "?"; }
