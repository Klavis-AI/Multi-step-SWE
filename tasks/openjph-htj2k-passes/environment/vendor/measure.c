/* End-to-end timing harness for the OpenJPH milestone workspace.
 *
 *   /app/tools/measure [<build-dir>]        default build-dir: /app/build
 *
 * Runs one encode and one decode of a fixed sample image through the tree the
 * candidate built and prints milliseconds.  Everything that selects the work
 * -- the input, the transform, the quantization, the number of repetitions --
 * is compiled in, so nothing about the measurement is recoverable from the
 * command line or from a readable script.
 *
 * This is a *feedback* instrument: the graded measurement is per function and
 * runs inside the verifier, on inputs the candidate never sees.  Treat this
 * number as indicative only.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>

static const char *in_image = "/app/workload/work.ppm";
static const char *tmp_j2c  = "/tmp/.measure_roundtrip.j2c";
static const int   reps     = 5;      /* best of N: whole-program wall clock */
static const char *enc_args[] = {
  "-reversible", "true", "-num_decomps", "5", NULL };
static const char *dec_args[] = { NULL };

static double now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int exists(const char *p)
{
  struct stat st;
  return stat(p, &st) == 0;
}

/* run `exe args...` with the file at `out_indices` replaced by `out`.
 * Returns 0 on success, non-zero if the child failed. */
static int run(char *const argv[], const char *out, double *ms)
{
  double t0 = now_ms();
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    execv(argv[0], argv);
    _exit(127);
  }
  int st = 0;
  if (waitpid(pid, &st, 0) < 0) return -1;
  *ms = now_ms() - t0;
  (void)out;
  if (!WIFEXITED(st)) return -1;
  return WEXITSTATUS(st) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
  const char *bld = argc > 1 ? argv[1] : "/app/build";
  char compress[4096], expand[4096];
  snprintf(compress, sizeof compress,
           "%s/src/apps/ojph_compress/ojph_compress", bld);
  snprintf(expand, sizeof expand,
           "%s/src/apps/ojph_expand/ojph_expand", bld);

  if (!exists(compress) || !exists(expand)) {
    fprintf(stderr, "measure: no ojph_compress / ojph_expand under '%s'.\n"
                    "         build the tree first, e.g.\n"
                    "         cmake -S /app/ojph -B /app/build "
                    "-DCMAKE_BUILD_TYPE=Release\n"
                    "         cmake --build /app/build -j\"$(nproc)\"\n", bld);
    return 2;
  }
  if (!exists(in_image)) {
    fprintf(stderr, "measure: %s is missing\n", in_image);
    return 2;
  }

  char *enc[16]; int n = 0;
  enc[n++] = compress; enc[n++] = (char *)"-i"; enc[n++] = (char *)in_image;
  enc[n++] = (char *)"-o"; enc[n++] = (char *)tmp_j2c;
  for (int i = 0; enc_args[i]; ++i) enc[n++] = (char *)enc_args[i];
  enc[n] = NULL;

  double best_enc = 1e18;
  for (int i = 0; i < reps; ++i) {
    double ms;
    if (run(enc, tmp_j2c, &ms) != 0) {
      fprintf(stderr, "measure: encode failed (repetition %d)\n", i + 1);
      return 1;
    }
    if (ms < best_enc) best_enc = ms;
  }

  char *dec[16]; n = 0;
  dec[n++] = expand; dec[n++] = (char *)"-i"; dec[n++] = (char *)tmp_j2c;
  dec[n++] = (char *)"-o"; dec[n++] = (char *)"/tmp/.measure_roundtrip.ppm";
  for (int i = 0; dec_args[i]; ++i) dec[n++] = (char *)dec_args[i];
  dec[n] = NULL;

  double best_dec = 1e18;
  for (int i = 0; i < reps; ++i) {
    double ms;
    if (run(dec, NULL, &ms) != 0) {
      fprintf(stderr, "measure: decode failed (repetition %d)\n", i + 1);
      return 1;
    }
    if (ms < best_dec) best_dec = ms;
  }

  printf("encode %.1f ms   decode %.1f ms   total %.1f ms\n",
         best_enc, best_dec, best_enc + best_dec);
  return 0;
}
