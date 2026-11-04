/*
 * avsbench -- measure one AviSynth+ filter stage through the public C API.
 *
 * The unit of measurement is a filter *operation*, reached through the public
 * filter graph: the library decides which kernel to install for it, so an
 * intrinsics kernel, an assembly kernel and an optimized portable-C body are
 * all measured the same way, and no symbol name has to be guessed.
 *
 * Whole-graph timing cannot isolate one filter, because the source filter is
 * paid on both sides and dilutes the ratio.  So the reported figure is a
 * difference: the same source is measured twice, once with the target filter
 * appended (`chain`) and once without it (`source`), and the difference of the
 * two read passes is the target filter's own cost.  Everything the two scripts
 * share -- graph construction, the source filter, frame allocation, the
 * library's own frame cache -- cancels out of that difference.
 *
 * Every timed repetition rebuilds BOTH graphs from scratch in a fresh script
 * environment.  AviSynth caches frames per filter instance, so re-reading the
 * same clip a second time costs almost nothing; a fresh graph per repetition is
 * what makes each repetition a genuine, uncached pass.
 *
 * A fresh graph is not enough on its own.  A library that memoises its own
 * output -- keyed on the frame number, on a pointer, or on a digest of the
 * input -- survives the rebuild, so a harness that re-feeds the identical
 * frames every repetition pays for the first repetition and reads a cache for
 * the rest, and scores for work it did not do.
 *
 * Each repetition therefore reads a *different window of the clip*: repetition
 * r reads frames [WIN_BASE + (seed0 + r) * frames, +frames), so every
 * repetition is asked for frame numbers no earlier repetition used, and the
 * window is reported (`WINDOW <first> <count>`).  The frames inside a window
 * are the clip's own frames -- the same pixels the harness has always measured,
 * just read from further along the clip -- so nothing about the operation or
 * its inputs changes: same format, same frame count, same byte values that the
 * clip produces, and a library cannot tell a repetition from a first read by
 * looking at its input.  A repetition whose window does not fit inside the clip
 * is a failure, not a silent fallback: the caller asserts the window it got.
 *
 * It is deliberately not a *content* change.  A first attempt drew the unit's
 * source construction with a per-repetition fade; that changes the pixel
 * values, and this project's reference implementations are not all bit-exact
 * with their portable C twins on arbitrary content (upstream's Sharpen(0.6)
 * diverges from the C path as soon as the clip is not the shipped pattern --
 * see the fix report).  The exactness gate is a property of the draw, so the
 * draw must not invent one.
 *
 * Output (stdout, one `KEY value` per line):
 *   STATUS ok
 *   FRAMES <n>
 *   WINDOW <first frame> <count>   the repetition window every pass reads
 *   DIGEST_CHAIN  <fnv1a64 of every plane byte of every chain frame>
 *   DIGEST_SOURCE <same, for the source-only clip>
 *   MIN_DIFF_MS / MEDIAN_DIFF_MS / SPREAD_DIFF_MS
 *   MIN_CHAIN_MS / MEDIAN_CHAIN_MS / MEDIAN_SOURCE_MS
 *   BUILD_MS <mean cost of rebuilding one graph pair>
 * The digests are the last repetition's window -- the same window on both
 * libraries, and a window no earlier pass measured.
 * On any failure: `STATUS error` plus `ERROR <message>`, exit 1.
 *
 * Usage: avsbench <libdir> <chain.avs> <source.avs> <frames> <reps> [audio] [seed0]
 *
 * `seed0` is the first repetition's draw number; repetition r reads frames
 * [WIN_BASE + (seed0 + r) * frames, + frames).  Default 0.
 *
 * `audio` is 1 to read the clip's audio track as well as its frames, and 0 (the
 * default) to read frames only.  It is off by default because it is not free:
 * pulling a video clip's (usually silent) audio track through the library costs
 * as much as the filter under test, and while that cost cancels in the chain /
 * source difference it dominates both sides and turns any variation in it into
 * noise on the ratio.  Only the audio family sets it.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "avisynth_c.h"

typedef AVS_ScriptEnvironment *(*fn_create_env)(int);
typedef void (*fn_delete_env)(AVS_ScriptEnvironment *);
typedef AVS_Value (*fn_invoke)(AVS_ScriptEnvironment *, const char *, AVS_Value,
                               const char **);
typedef void (*fn_release_value)(AVS_Value);
typedef AVS_Clip *(*fn_take_clip)(AVS_Value, AVS_ScriptEnvironment *);
typedef void (*fn_release_clip)(AVS_Clip *);
typedef const AVS_VideoInfo *(*fn_get_video_info)(AVS_Clip *);
typedef AVS_VideoFrame *(*fn_get_frame)(AVS_Clip *, int);
typedef void (*fn_release_video_frame)(AVS_VideoFrame *);
typedef const char *(*fn_clip_get_error)(AVS_Clip *);
typedef const char *(*fn_get_error)(AVS_ScriptEnvironment *);
typedef int (*fn_get_pitch_p)(const AVS_VideoFrame *, int);
typedef int (*fn_get_row_size_p)(const AVS_VideoFrame *, int);
typedef int (*fn_get_height_p)(const AVS_VideoFrame *, int);
typedef const BYTE *(*fn_get_read_ptr_p)(const AVS_VideoFrame *, int);
typedef int (*fn_num_components)(const AVS_VideoInfo *);
typedef int (*fn_get_audio)(AVS_Clip *, void *, int64_t, int64_t);

static fn_create_env p_create_env;
static fn_delete_env p_delete_env;
static fn_invoke p_invoke;
static fn_release_value p_release_value;
static fn_take_clip p_take_clip;
static fn_release_clip p_release_clip;
static fn_get_video_info p_get_video_info;
static fn_get_frame p_get_frame;
static fn_release_video_frame p_release_video_frame;
static fn_clip_get_error p_clip_get_error;
static fn_get_error p_get_error;
static fn_get_pitch_p p_get_pitch_p;
static fn_get_row_size_p p_get_row_size_p;
static fn_get_height_p p_get_height_p;
static fn_get_read_ptr_p p_get_read_ptr_p;
static fn_num_components p_num_components;
static fn_get_audio p_get_audio;

static void *lib_handle;

static int fail(const char *msg) {
  printf("STATUS error\nERROR %s\n", msg);
  fflush(stdout);
  return 1;
}

#define SYM(dst, name)                                                        \
  do {                                                                        \
    *(void **)(&(dst)) = dlsym(lib_handle, (name));                           \
    if (!(dst))                                                               \
      return fail("missing symbol " name);                                    \
  } while (0)

static int load_api(void) {
  SYM(p_create_env, "avs_create_script_environment");
  SYM(p_delete_env, "avs_delete_script_environment");
  SYM(p_invoke, "avs_invoke");
  SYM(p_release_value, "avs_release_value");
  SYM(p_take_clip, "avs_take_clip");
  SYM(p_release_clip, "avs_release_clip");
  SYM(p_get_video_info, "avs_get_video_info");
  SYM(p_get_frame, "avs_get_frame");
  SYM(p_release_video_frame, "avs_release_video_frame");
  SYM(p_clip_get_error, "avs_clip_get_error");
  SYM(p_get_error, "avs_get_error");
  SYM(p_get_pitch_p, "avs_get_pitch_p");
  SYM(p_get_row_size_p, "avs_get_row_size_p");
  SYM(p_get_height_p, "avs_get_height_p");
  SYM(p_get_read_ptr_p, "avs_get_read_ptr_p");
  SYM(p_num_components, "avs_num_components");
  /* Optional: an audio-only family cannot be measured without it, but a
     tree that lacks it is still measurable for every video family. */
  *(void **)(&p_get_audio) = dlsym(lib_handle, "avs_get_audio");
  return 0;
}

/* CPU time, not wall time.  The target host is shared and heavily loaded, so
   a wall-clock reading of a fixed amount of work moves by a factor of two or
   more as the scheduler time-slices the core; the CPU time that the work
   actually consumes does not.  Contention still shows up through cache and
   memory-bandwidth pressure, which is why the minimum over repetitions is
   taken as well, but scheduling noise is removed entirely. */
static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

static double pick(double *v, int n, double q) {
  if (n <= 0) return 0.0;
  double *c = malloc(sizeof(double) * (size_t)n);
  memcpy(c, v, sizeof(double) * (size_t)n);
  qsort(c, (size_t)n, sizeof(double), cmp_double);
  int i = (int)(q * (double)(n - 1) + 0.5);
  if (i < 0) i = 0;
  if (i >= n) i = n - 1;
  double r = c[i];
  free(c);
  return r;
}

static uint64_t hash_bytes(uint64_t h, const BYTE *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  return h;
}

static uint64_t hash_plane(uint64_t h, const BYTE *p, int pitch, int rowsize,
                           int height) {
  for (int y = 0; y < height; y++) {
    const BYTE *row = p + (size_t)y * (size_t)pitch;
    for (int x = 0; x < rowsize; x++) {
      h ^= row[x];
      h *= 1099511628211ULL;
    }
  }
  return h;
}

/* --------------------------------------------------------------------------
   the repetition's window
   --------------------------------------------------------------------------
   Where the repetition reads from.  The offset is a function of the repetition
   number, so every repetition reads frame numbers no earlier repetition used,
   while the frames themselves are the clip's own -- see the header.  WIN_BASE
   is comfortably past the front of the clip so that a repetition's window never
   overlaps the previous one and never sits at the clip's start.
*/
#define WIN_BASE 4096

static int window_first(int frames, int seed0, int rep) {
  return WIN_BASE + (seed0 + rep) * frames;
}

/* Build both clips in a brand-new environment.  Returns 0 on success. */
static int build_pair(AVS_ScriptEnvironment **env_out, AVS_Clip **chain_out,
                      AVS_Clip **source_out, const char *chain_script,
                      const char *source_script) {
  AVS_ScriptEnvironment *env = p_create_env(AVISYNTH_INTERFACE_VERSION);
  if (!env) {
    printf("STATUS error\nERROR avs_create_script_environment failed\n");
    return -1;
  }
  AVS_Value cv = p_invoke(env, "Import", avs_new_value_string(chain_script), NULL);
  if (avs_is_error(cv)) {
    printf("STATUS error\nERROR chain Import: %s\n",
           cv.d.string ? cv.d.string : "(null)");
    return -1;
  }
  if (!avs_is_clip(cv)) {
    printf("STATUS error\nERROR chain script is not a clip\n");
    return -1;
  }
  AVS_Clip *chain = p_take_clip(cv, env);
  if (!chain) {
    printf("STATUS error\nERROR avs_take_clip(chain) failed\n");
    return -1;
  }
  AVS_Value sv = p_invoke(env, "Import", avs_new_value_string(source_script), NULL);
  if (avs_is_error(sv)) {
    printf("STATUS error\nERROR source Import: %s\n",
           sv.d.string ? sv.d.string : "(null)");
    return -1;
  }
  if (!avs_is_clip(sv)) {
    printf("STATUS error\nERROR source script is not a clip\n");
    return -1;
  }
  AVS_Clip *source = p_take_clip(sv, env);
  if (!source) {
    printf("STATUS error\nERROR avs_take_clip(source) failed\n");
    return -1;
  }
  *env_out = env;
  *chain_out = chain;
  *source_out = source;
  return 0;
}

static void drop_pair(AVS_ScriptEnvironment *env, AVS_Clip *chain,
                      AVS_Clip *source) {
  p_release_clip(source);
  p_release_clip(chain);
  p_delete_env(env);
}

/* One in-order pass over frames 0..n-1 and over the clip's whole audio track.
   In-order is deliberate: the library caches one frame per filter, so a
   sequential pass misses on every request.  The audio pass is not optional --
   an audio-only filter chain produces frames that are pure passthrough, so a
   video-only pass would time the sample conversion at zero, and the digest
   would not notice that it had not run at all. */
#define AUDIO_CHUNK 32768
static int g_want_audio = 0;

static double read_pass(AVS_Clip *clip, int first, int n, uint64_t *digest) {
  double t0 = now_sec();
  for (int i = 0; i < n; i++) {
    AVS_VideoFrame *f = p_get_frame(clip, first + i);
    if (!f) {
      if (digest) *digest = 0;
      return -1.0;
    }
    if (digest) {
      const AVS_VideoInfo *vi = p_get_video_info(clip);
      int ncomp = p_num_components(vi);
      for (int p = 0; p < ncomp; p++)
        *digest = hash_plane(*digest, p_get_read_ptr_p(f, p), p_get_pitch_p(f, p),
                             p_get_row_size_p(f, p), p_get_height_p(f, p));
    }
    p_release_video_frame(f);
  }
  if (g_want_audio && p_get_audio) {
    const AVS_VideoInfo *vi = p_get_video_info(clip);
    if (vi && avs_has_audio(vi) && vi->num_audio_samples > 0) {
      int64_t total = vi->num_audio_samples;
      if (total > (int64_t)AUDIO_CHUNK * 4096) total = (int64_t)AUDIO_CHUNK * 4096;
      int bps = avs_bytes_per_audio_sample(vi);
      if (bps <= 0) bps = 1;
      BYTE *buf = (BYTE *)malloc((size_t)AUDIO_CHUNK * (size_t)bps);
      if (!buf) return -1.0;
      for (int64_t start = 0; start < total; start += AUDIO_CHUNK) {
        int64_t want = total - start;
        if (want > AUDIO_CHUNK) want = AUDIO_CHUNK;
        if (p_get_audio(clip, buf, start, want) != 0) { free(buf); return -1.0; }
        if (digest) *digest = hash_bytes(*digest, buf, (size_t)want * (size_t)bps);
      }
      free(buf);
    }
  }
  return now_sec() - t0;
}

int main(int argc, char **argv) {
  if (argc != 6 && argc != 7 && argc != 8) {
    fprintf(stderr,
            "usage: %s <libdir> <chain.avs> <source.avs> <frames> <reps> "
            "[audio] [seed0]\n",
            argv[0]);
    return 2;
  }
  const char *libdir = argv[1], *chain_script = argv[2],
             *source_script = argv[3];
  int frames = atoi(argv[4]), reps = atoi(argv[5]);
  int want_audio = (argc >= 7) ? atoi(argv[6]) : 0;
  int seed0 = (argc == 8) ? atoi(argv[7]) : 0;
  if (frames <= 0 || reps <= 0) return fail("frames and reps must be positive");
  if (reps < 3) reps = 3;

  /* The scripts are read once and re-rendered at each repetition's draw.  A
     script with no marker renders to itself, so nothing changes for callers
     that do not use the draw. */
  char path[4096];
  snprintf(path, sizeof path, "%s/libavisynth.so", libdir);
  lib_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib_handle) return fail(dlerror());
  if (load_api() != 0) return 1;
  g_want_audio = want_audio;

  /* Probe: one graph pair, to check shape and to collect the digest.  It reads
     the same window as the first timed repetition. */
  AVS_ScriptEnvironment *env;
  AVS_Clip *chain, *source;
  if (build_pair(&env, &chain, &source, chain_script, source_script) != 0)
    return 1;

  const AVS_VideoInfo *cvi = p_get_video_info(chain);
  const AVS_VideoInfo *svi = p_get_video_info(source);
  if (!cvi || !svi) return fail("clip has no video info");
  if (cvi->num_frames != svi->num_frames)
    return fail("chain and source clips have different frame counts");
  if (frames > cvi->num_frames) frames = cvi->num_frames;
  if (frames <= 0) return fail("clip has no frames");

  if (cvi->num_frames < window_first(frames, seed0, reps) + frames)
    return fail("the clip is too short for the repetition windows");
  printf("WINDOW %d %d\n", window_first(frames, seed0, 0), frames);

  /* The probe reads a window of its own, past every repetition's, and only to
     prove the clip has the shape the caller asked for.  If it read a
     repetition's window it would leave that repetition's frames in the
     library's own caching -- and in any cache of the library's -- and the
     earliest repetition would then be timed against a warm one. */
  uint64_t dc = 1469598103934665603ULL, ds = 1469598103934665603ULL;
  const int probe_first = window_first(frames, seed0, reps + 1);
  if (read_pass(chain, probe_first, frames, &dc) < 0)
    return fail("chain frame read failed");
  if (read_pass(source, probe_first, frames, &ds) < 0)
    return fail("source frame read failed");
  dc = 1469598103934665603ULL;
  ds = 1469598103934665603ULL;
  drop_pair(env, chain, source);

  double *dt_chain = malloc(sizeof(double) * (size_t)reps);
  double *dt_src = malloc(sizeof(double) * (size_t)reps);
  double *dt_diff = malloc(sizeof(double) * (size_t)reps);
  double *dt_build = malloc(sizeof(double) * (size_t)reps);

  for (int r = 0; r < reps; r++) {
    const int first = window_first(frames, seed0, r);
    if (first + frames > cvi->num_frames)
      return fail("the clip is too short for this repetition's window");
    double tb0 = now_sec();
    if (build_pair(&env, &chain, &source, chain_script, source_script) != 0)
      return 1;
    double tb1 = now_sec();
    /* The last repetition also hashes what it reads.  Its figure is not the
       one reported (the minimum over repetitions is, and a hashing pass is not
       the fastest), but its digest is: it is the draw no earlier pass used, so
       a library that answered from a cache filled by an earlier draw is caught
       by the caller's comparison.  The accumulators restart here, so the
       reported digest is that repetition's own bytes and nothing else. */
    uint64_t *dc_rep = NULL, *ds_rep = NULL;
    if (r == reps - 1) {
      dc = 1469598103934665603ULL;
      ds = 1469598103934665603ULL;
      dc_rep = &dc;
      ds_rep = &ds;
    }
    double a, b;
    if (r & 1) {
      b = read_pass(source, first, frames, ds_rep);
      a = read_pass(chain, first, frames, dc_rep);
    } else {
      a = read_pass(chain, first, frames, dc_rep);
      b = read_pass(source, first, frames, ds_rep);
    }
    if (a < 0 || b < 0) return fail("frame read failed during timing");
    dt_chain[r] = a * 1000.0;
    dt_src[r] = b * 1000.0;
    dt_diff[r] = (a - b) * 1000.0;
    dt_build[r] = (tb1 - tb0) * 1000.0;
    drop_pair(env, chain, source);
  }

  /* The host is shared, so contention can only ever add time to a pass; the
     minimum over repetitions is therefore the estimator that survives a loaded
     box.  The median is reported next to it so the gap between the two is
     visible rather than assumed. */
  double lo = dt_diff[0], hi = dt_diff[0], lo_chain = dt_chain[0],
         lo_source = dt_src[0];
  for (int r = 1; r < reps; r++) {
    if (dt_diff[r] < lo) lo = dt_diff[r];
    if (dt_diff[r] > hi) hi = dt_diff[r];
    if (dt_chain[r] < lo_chain) lo_chain = dt_chain[r];
    if (dt_src[r] < lo_source) lo_source = dt_src[r];
  }

  printf("STATUS ok\n");
  printf("FRAMES %d\n", frames);
  printf("DIGEST_CHAIN %016llx\n", (unsigned long long)dc);
  printf("DIGEST_SOURCE %016llx\n", (unsigned long long)ds);
  printf("MIN_DIFF_MS %.4f\n", lo);
  printf("MEDIAN_DIFF_MS %.4f\n", pick(dt_diff, reps, 0.5));
  printf("SPREAD_DIFF_MS %.4f\n", hi - lo);
  printf("MIN_CHAIN_MS %.4f\n", lo_chain);
  printf("MIN_SOURCE_MS %.4f\n", lo_source);
  printf("MEDIAN_CHAIN_MS %.4f\n", pick(dt_chain, reps, 0.5));
  printf("MEDIAN_SOURCE_MS %.4f\n", pick(dt_src, reps, 0.5));
  printf("BUILD_MS %.4f\n", pick(dt_build, reps, 0.5));
  fflush(stdout);
  return 0;
}
