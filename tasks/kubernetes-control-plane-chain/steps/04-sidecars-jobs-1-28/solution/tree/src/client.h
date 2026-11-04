#ifndef KCP_CLIENT_H
#define KCP_CLIENT_H
#include "json.h"
typedef struct { char *host; int port; int tls; char *token; } kclient;
int   kc_from_kubeconfig(kclient *c, const char *path);
json *kc_get(kclient *c, const char *path, int *status);
json *kc_send(kclient *c, const char *method, const char *path, json *body,
              const char *ctype, int *status);
#endif
