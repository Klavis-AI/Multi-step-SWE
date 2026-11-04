/* bulk.h -- the asynchronous request pattern, Bulk Data export, $everything. */
#ifndef FHIR_BULK_H
#define FHIR_BULK_H

#include "util.h"
#include "json.h"
#include "defs.h"
#include "store.h"
#include "server.h"

/* Start an export.  `level` is "system", "patient" or "group"; `gid` is the
   Group id for the group level.  -> the status endpoint's job id. */
const char *bulk_start(arena *a, server *srv, fhirbase *b, const char *level,
                       const char *gid, const char *qs, const char *request_url);

/* The status endpoint.  -> 202 while running, 200 with the manifest when done,
   404 when unknown, 500 with an OperationOutcome when the job failed. */
int bulk_status(arena *a, server *srv, const char *jobid, jval **body,
                const char **progress);
int bulk_cancel(const char *jobid);

/* One NDJSON file of a finished job. -> the bytes, or NULL. */
const char *bulk_file(const char *jobid, const char *name, size_t *len);

/* Every resource in a patient's or an encounter's compartment. */
jval *everything(arena *a, server *srv, fhirbase *b, const char *type,
                 const char *id, const char *qs);

#endif
