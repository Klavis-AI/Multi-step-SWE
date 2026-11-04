/*
 * sortdir.c -- LD_PRELOAD shim: every directory a process lists comes back in
 * byte order of its entry names, whatever filesystem it lives on.
 *
 * Why: Hadoop's local listing (RawLocalFileSystem.listStatus -> File.list ->
 * readdir) does not sort, so the order of a corpus directory's part files --
 * and with it the order of a job's partitions, every seeded RDD.sample draw and
 * every partition-ordered print -- is the order the host filesystem returns.
 * On ext4 that is htree hash order under a per-filesystem seed, i.e. a
 * different order on every host. Sorting at readdir makes the replay and the
 * recordings agree on any host.
 *
 * Each DIR* is read to the end at open time with the real readdir64, the
 * entries are sorted with strcmp, and readdir/readdir64/readdir_r/readdir64_r,
 * rewinddir, telldir and seekdir are served from that snapshot. glibc on
 * x86_64/aarch64 gives struct dirent and struct dirent64 the same layout.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct snap {
    DIR *dir;
    struct dirent64 **ent;
    size_t n, pos;
    struct snap *next;
};

static struct snap *snaps;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static DIR *(*real_opendir)(const char *);
static DIR *(*real_fdopendir)(int);
static int (*real_closedir)(DIR *);
static struct dirent64 *(*real_readdir64)(DIR *);

static void init(void) {
    if (!real_readdir64) {
        real_opendir = dlsym(RTLD_NEXT, "opendir");
        real_fdopendir = dlsym(RTLD_NEXT, "fdopendir");
        real_closedir = dlsym(RTLD_NEXT, "closedir");
        real_readdir64 = dlsym(RTLD_NEXT, "readdir64");
    }
}

static int cmp(const void *a, const void *b) {
    return strcmp((*(struct dirent64 *const *)a)->d_name,
                  (*(struct dirent64 *const *)b)->d_name);
}

static DIR *capture(DIR *d) {
    if (!d) return d;
    struct snap *s = calloc(1, sizeof *s);
    if (!s) return d;
    size_t cap = 0;
    struct dirent64 *e;
    int saved = errno;
    while ((e = real_readdir64(d)) != NULL) {
        if (s->n == cap) {
            cap = cap ? cap * 2 : 64;
            struct dirent64 **g = realloc(s->ent, cap * sizeof *g);
            if (!g) break;
            s->ent = g;
        }
        struct dirent64 *c = malloc(e->d_reclen > sizeof *c ? e->d_reclen : sizeof *c);
        if (!c) break;
        memcpy(c, e, e->d_reclen);
        s->ent[s->n++] = c;
    }
    errno = saved;
    if (s->n > 1) qsort(s->ent, s->n, sizeof *s->ent, cmp);
    s->dir = d;
    pthread_mutex_lock(&mu);
    s->next = snaps;
    snaps = s;
    pthread_mutex_unlock(&mu);
    return d;
}

static struct snap *find(DIR *d) {
    pthread_mutex_lock(&mu);
    struct snap *s = snaps;
    while (s && s->dir != d) s = s->next;
    pthread_mutex_unlock(&mu);
    return s;
}

DIR *opendir(const char *name) { init(); return capture(real_opendir(name)); }
DIR *fdopendir(int fd) { init(); return capture(real_fdopendir(fd)); }

int closedir(DIR *d) {
    init();
    pthread_mutex_lock(&mu);
    struct snap **p = &snaps;
    while (*p && (*p)->dir != d) p = &(*p)->next;
    struct snap *s = *p;
    if (s) *p = s->next;
    pthread_mutex_unlock(&mu);
    if (s) {
        for (size_t i = 0; i < s->n; i++) free(s->ent[i]);
        free(s->ent);
        free(s);
    }
    return real_closedir(d);
}

struct dirent64 *readdir64(DIR *d) {
    init();
    struct snap *s = find(d);
    if (!s) return real_readdir64(d);
    return s->pos < s->n ? s->ent[s->pos++] : NULL;
}

struct dirent *readdir(DIR *d) { return (struct dirent *)readdir64(d); }

int readdir64_r(DIR *d, struct dirent64 *buf, struct dirent64 **res) {
    struct dirent64 *e = readdir64(d);
    if (!e) { *res = NULL; return 0; }
    memcpy(buf, e, e->d_reclen);
    *res = buf;
    return 0;
}

int readdir_r(DIR *d, struct dirent *buf, struct dirent **res) {
    return readdir64_r(d, (struct dirent64 *)buf, (struct dirent64 **)res);
}

void rewinddir(DIR *d) {
    struct snap *s = find(d);
    if (s) { s->pos = 0; return; }
    void (*real)(DIR *) = dlsym(RTLD_NEXT, "rewinddir");
    real(d);
}

long telldir(DIR *d) {
    struct snap *s = find(d);
    if (s) return (long)s->pos;
    long (*real)(DIR *) = dlsym(RTLD_NEXT, "telldir");
    return real(d);
}

void seekdir(DIR *d, long off) {
    struct snap *s = find(d);
    if (s) { s->pos = off < 0 ? 0 : (size_t)off > s->n ? s->n : (size_t)off; return; }
    void (*real)(DIR *, long) = dlsym(RTLD_NEXT, "seekdir");
    real(d, off);
}
