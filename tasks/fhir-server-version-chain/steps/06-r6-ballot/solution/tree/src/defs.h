/* defs.h -- FHIR conformance definitions loaded from a published NPM package. */
#ifndef FHIR_DEFS_H
#define FHIR_DEFS_H

#include "json.h"
#include "rx.h"

typedef struct edef edef;
typedef struct sdef sdef;
typedef struct fhirdefs fhirdefs;

struct edef {
    const char *path;            /* "Patient.name"                  */
    const char *tail;            /* "name" or "value[x]"            */
    const char *id;              /* element id (carries slice names) */
    const char *slice_name;      /* non-NULL for a slice            */
    int         min;
    const char *max;             /* "1", "*", "0"                   */
    vec         types;           /* const char* type codes          */
    vec         target_profiles; /* const char* canonical urls      */
    const char *content_ref;     /* "#Questionnaire.item"           */
    const char *elem_name;       /* DSTU2 ElementDefinition.name    */
    const char *binding_strength;
    const char *binding_vs;
    const char *fixed_type;  jval *fixed_val;
    const char *pattern_type; jval *pattern_val;
    jval       *ed;              /* the ElementDefinition itself    */
    jval       *slicing;         /* .slicing of a slicing root      */
    int         is_modifier;
    int         is_summary;
    edef       *parent, *first_child, *last_child, *next;
    vec         slices;          /* edef* children slices of this path */
};

struct sdef {
    const char *url, *id, *name, *type, *kind, *derivation, *base;
    int         abstract_;
    const char *regex;           /* primitive types only */
    rx         *re;              /* compiled form of `regex`     */
    jval       *sd;
    edef       *root;
    smap        by_path;         /* full path -> edef* (first, non-slice) */
};

typedef struct {
    const char *url, *code, *type, *expression, *base_str;
    vec         bases;           /* const char* resource type names */
    vec         targets;         /* const char* resource type names */
    vec         components;      /* jval* of component entries      */
    jval       *sp;
} sparam;

struct fhirdefs {
    arena       a;
    const char *ver;             /* "4.0.1"    */
    const char *code;            /* "R4"       */
    const char *base_path;       /* "baseR4"   */
    const char *dir;             /* package directory */
    smap        sd_by_url;
    smap        sd_by_name;
    vec         resources;       /* sdef* : kind=resource, not abstract, derivation!=constraint */
    smap        res_by_name;
    vec         sparams;
    smap        sp_by_key;       /* "Patient|name" -> sparam*   */
    smap        compartments;    /* "Patient" -> jval*          */
    smap        opdefs;          /* url -> jval*                */
    smap        valuesets;       /* url -> jval*                */
    smap        codesystems;     /* url -> jval*                */
    smap        conceptmaps;     /* url -> jval*                */
    smap        profiles;        /* canonical -> sdef*, derivation=constraint */
    vec         profile_list;    /* sdef* in load order         */
    int         igs_loaded;
    vec         all_types;       /* const char* names of resource types */
};

fhirdefs *defs_load(const char *pkgdir, const char *ver, const char *code, const char *base_path);
sdef     *defs_sd(fhirdefs *d, const char *name_or_url);
/* Load every implementation guide in the FHIR package cache whose
   `fhirVersions` covers this release: its profiles, value sets, code systems
   and concept maps.  Called once, lazily. */
void      defs_load_igs(fhirdefs *d);
sdef     *defs_profile(fhirdefs *d, const char *canonical);
edef     *sd_path_rel(edef *base, const char *dotted);
sdef     *defs_resource(fhirdefs *d, const char *type);
edef     *sd_path(sdef *s, const char *path);
edef     *ed_child(fhirdefs *d, sdef *s, edef *parent, const char *json_name, const char **out_type);
sparam   *defs_sp(fhirdefs *d, const char *type, const char *code);
int       defs_is_resource(fhirdefs *d, const char *type);
/* Resolve where the children of element `e` (declared as `type_code` inside
   `owner`) are defined.  Backbone elements keep their children in the owner's
   own snapshot; named datatypes resolve to their own StructureDefinition. */
int       ed_children_of(fhirdefs *d, sdef *owner, edef *e, const char *type_code,
                         sdef **out_sd, edef **out_node);
const char *ed_choice_type(edef *e, const char *json_name); /* "valueQuantity" -> "Quantity" */

#endif
