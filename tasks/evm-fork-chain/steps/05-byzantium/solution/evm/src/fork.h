#ifndef FORK_H
#define FORK_H
#include <stdbool.h>

typedef enum {
    F_FRONTIER = 0, F_HOMESTEAD, F_DAO, F_EIP150, F_EIP158, F_BYZANTIUM,
    F_CONSTANTINOPLE, F_PETERSBURG, F_ISTANBUL, F_MUIRGLACIER, F_BERLIN, F_LONDON,
    F_ARROWGLACIER, F_GRAYGLACIER, F_PARIS, F_SHANGHAI, F_CANCUN, F_PRAGUE, F_OSAKA,
    F_COUNT
} fork_id;

int fork_by_name(const char *name);      /* -1 if unknown */
const char *fork_name(fork_id f);

#define IS_HOMESTEAD(f)      ((f) >= F_HOMESTEAD)
#define IS_EIP150(f)         ((f) >= F_EIP150)
#define IS_EIP158(f)         ((f) >= F_EIP158)
#define IS_BYZANTIUM(f)      ((f) >= F_BYZANTIUM)
#define IS_CONSTANTINOPLE(f) ((f) >= F_CONSTANTINOPLE)
#define IS_PETERSBURG(f)     ((f) >= F_PETERSBURG)
#define IS_ISTANBUL(f)       ((f) >= F_ISTANBUL)
#define IS_MUIR(f)           ((f) >= F_MUIRGLACIER)
#define IS_BERLIN(f)         ((f) >= F_BERLIN)
#define IS_LONDON(f)         ((f) >= F_LONDON)
#define IS_ARROW(f)          ((f) >= F_ARROWGLACIER)
#define IS_GRAY(f)           ((f) >= F_GRAYGLACIER)
#define IS_PARIS(f)          ((f) >= F_PARIS)
#define IS_SHANGHAI(f)       ((f) >= F_SHANGHAI)
#define IS_CANCUN(f)         ((f) >= F_CANCUN)
#define IS_PRAGUE(f)         ((f) >= F_PRAGUE)
#define IS_OSAKA(f)          ((f) >= F_OSAKA)
/* EIP-1283 net gas metering was live only in Constantinople (removed by Petersburg) */
#define IS_EIP1283(f)        ((f) == F_CONSTANTINOPLE)
#endif
