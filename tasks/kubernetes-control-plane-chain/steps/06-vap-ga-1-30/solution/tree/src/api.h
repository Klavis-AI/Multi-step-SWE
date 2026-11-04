#ifndef KCP_API_H
#define KCP_API_H
#include "json.h"
#include "http.h"
#include "store.h"

typedef struct {
    char *group, *version, *resource, *kind, *singular, *prefix, *canon;
    int namespaced;
    json *verbs, *shortnames, *categories, *subresources;
    int optional;            /* served only with --runtime-config */
    int enabled;
} resdef;

typedef struct {
    char *name;
    json *groups;            /* array of strings */
    char *uid;               /* as the authenticator reported it */
} ident;

typedef struct {
    resdef *rd;
    char *ns, *name, *sub, *verb;
    char *group, *version, *resource;
} reqinfo;

/* configuration, filled from the command line */
typedef struct {
    char *bind_address, *cert, *key, *client_ca, *token_file, *etcd_servers;
    int  secure_port;
    char *sa_key, *sa_sign_key, *sa_issuer, *api_audiences;
    char *authorization_mode, *storage_media_type;
    char *runtime_config, *feature_gates, *authn_config;
    int  allow_privileged;
} apiconfig;

extern apiconfig CFG;
extern json *SURFACE;          /* required API surface, from the embedded blob */
extern int RELEASE_MINOR;

/* schema.c */
void      schema_init(void);
resdef   *res_lookup(const char *group, const char *version, const char *resource);
resdef   *res_by_kind(const char *group, const char *version, const char *kind);
json     *res_all(void);
void      apply_defaults(resdef *rd, json *obj, const char *version);
/* returns NULL when valid, else an array of {reason, field} causes */
json     *validate_obj(resdef *rd, json *obj, json *old, const char *version);
/* the subresource the current request addresses, "" for the resource itself */
extern const char *VALIDATE_SUB;
/* an admission refusal that is about a different object than the request's:
   NamespaceLifecycle reports the namespace, not the thing being created */
/* set when a field could not be decoded at all: a 400, not a 422 */
int   decode_level_error(resdef *rd, json *obj);
extern const char *ADMIT_DETAIL_KIND;
extern char *ADMIT_DETAIL_NAME;
json     *convert_to(resdef *rd, const json *obj, const char *version);
json     *convert_from(resdef *rd, const json *obj, const char *version);
void      prepare_status_defaults(resdef *rd, json *obj);
int       has_sub(resdef *rd, const char *sub);

/* helpers shared across the server */
json *status_obj(int code, const char *reason, const char *msg, const char *group,
                 const char *kind, const char *name, json *causes);
void  reply_obj(hconn *c, int code, json *obj);
void  reply_status(hconn *c, int code, const char *reason, const char *msg,
                   const char *group, const char *kind, const char *name, json *causes);
char *obj_key(resdef *rd, const char *ns, const char *name);
const char *meta_str(const json *o, const char *field);
json *meta(json *o);
void  set_av(json *o, const char *group, const char *version);
int   res_is_ephemeral(const char *r);
/* is this group-version actually served by this server? */
int   gv_enabled(const char *gv);
/* does this release's published OpenAPI name this group at all? */
int   upstream_serves_group(const char *g);
const char *embedded_served_groups(void);
/* decode an application/vnd.kubernetes.protobuf request body, NULL if it is
   not one this server understands */
json *k8s_protobuf_decode(const char *body, size_t len);

/* auth */
int  authenticate(hreq *r, ident *who);
int  authorize(const ident *who, const reqinfo *ri, const char *nonresource_path);
void rbac_reset_cache(void);

/* admission */
json *admit(const reqinfo *ri, json *obj, json *old, const ident *who, int *code,
            const char **reason, char **msg);

/* patch.c */
json *apply_json_patch(const json *orig, const json *patch, char **err);
json *apply_merge_patch(const json *orig, const json *patch);
json *apply_strategic_patch(resdef *rd, const json *orig, const json *patch);

/* openapi/discovery blobs */
const char *embedded_surface(void);
const char *embedded_openapi_v2(void);
const char *embedded_openapi_v3_root(void);
const char *embedded_openapi_v3(const char *path);
const char *embedded_schema(void);
#endif
