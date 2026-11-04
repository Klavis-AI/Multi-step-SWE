/*
 * codecbench, ladder step 6 -- bitstream unpacking.
 *
 * Scoped slot, reached through the rtcd pointer installed for it:
 *   unpack_data (reader, buf, w, gclis, group_size = 4, gtli, sign_flag, leftover_signs_num, precinct_bits_left)
 *
 * The reader and the precinct budget are part of the call: the error it returns
 * and the two in/out words are in the digest along with the coefficients and
 * the gclis, so a call that rebuilds the right coefficients from the wrong
 * place is caught here rather than at the end-to-end gate.
 *
 * The payload is written with the encoder's own nibble writer, so the byte
 * layout is the one the decoder reads: for every group whose GCLI exceeds the
 * truncation level, a sign nibble (only when hdr_Fs == 0) followed by
 * GCLI - gtli plane nibbles, most significant plane first.  Every nibble the
 * call reads has been written, and no nibble it does not read has been.
 *
 * Widths are 960, 958, 480, 479, 240 and 962 samples - 960 is 240 groups, the
 * commonest 1080p 4:2:2 band line - with the parameters of the tables below.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "decoder_dsp_rtcd.h"
#include "BitstreamWriter.h"

#define STEP6_GROUP_SIZE 4      /* GROUP_SIZE, the value the codec uses */
#define STEP6_TRUNCATION_MAX 15 /* TRUNCATION_MAX: planes a group may carry */
#define STEP6_MAXW 1024         /* widest band line exercised */
#define STEP6_MAX_GROUPS (STEP6_MAXW / STEP6_GROUP_SIZE + 1)

/* Where the band line sits in the codestream and how much codestream the reader
 * is told it has.  A data sub-packet never starts at byte zero, so the line is
 * placed some way in; the payload array carries STEP6_PAD bytes past the window
 * the reader is given, so no call can reach the end of the allocation. */
#define STEP6_STREAM_BYTES 4096
#define STEP6_OFF 128
#define STEP6_PAD 64

/* A 1080p 4:2:2 frame at the picture-info defaults (decom_h = 5, decom_v = 2,
 * sd = 0 - pi_compute against the built library) has 960-sample luma band lines
 * six times over; 960 samples - 240 groups - is therefore both the widest and
 * the commonest single call of a precinct line, and it is the timing
 * configuration.  The rest of the table moves those lines by a sample or two: a
 * band width is DIV_ROUND_DOWN of a scaled line width, so it is not always a
 * multiple of the group size.  480 is the widest chroma line. */
static const uint32_t k_widths[] = {960, 958, 480, 479, 240, 962};
/* Per-band truncation levels of such a frame, and the sign layout: hdr_Fs == 0
 * carries a sign nibble inside the data sub-packet, hdr_Fs != 0 leaves the signs
 * for the sign sub-packet.  The four correctness rounds cover signs inline and
 * separate, with and without a short last group. */
static const uint8_t k_gtli[] = {4, 2, 7, 1, 5, 3};
static const uint8_t k_sign_flag[] = {0, 0, 1, 1, 0, 1};
/* A band line that starts on the low half of a byte, as every band after the
 * first in a data sub-packet does. */
static const uint8_t k_phase[] = {0, 0, 1, 1, 0, 0};
#define STEP6_NCONF ((int)(sizeof(k_widths) / sizeof(k_widths[0])))

typedef struct {
    uint32_t w;
    uint32_t group_num;
    uint32_t leftover;
    uint32_t gcli_count; /* groups including the leftover one */
    uint8_t gtli;
    uint8_t sign_flag;
    uint8_t phase;
    /* input: the reader and the codestream window it points into */
    bitstream_reader_t br;
    uint8_t payload[STEP6_STREAM_BYTES + STEP6_PAD];
    /* outputs */
    uint16_t buf[STEP6_MAXW + STEP6_PAD];
    uint8_t gclis[STEP6_MAX_GROUPS + STEP6_PAD];
    uint8_t leftover_signs_num;
    int32_t precinct_bits_left;
    SvtJxsErrorType_t ret;
    /* what the call must start from, put back before every invocation */
    int32_t budget0;
} unpack_ctx;

static const uint64_t H0 = 0xCBF29CE484222325ULL;

static void unpack_setup(void *v, uint64_t seed, int round) {
    unpack_ctx *c = v;
    uint64_t s = seed;
    bitstream_writer_t bw;
    nib_writer_t nw;
    const int cfg = (round < 0) ? 0 : round % STEP6_NCONF;

    c->w = k_widths[cfg];
    c->gtli = k_gtli[cfg];
    c->sign_flag = k_sign_flag[cfg];
    c->phase = k_phase[cfg];
    c->group_num = c->w / STEP6_GROUP_SIZE;
    c->leftover = c->w % STEP6_GROUP_SIZE;
    c->gcli_count = c->group_num + (c->leftover ? 1u : 0u);

    /* Everything after the line's own nibbles is codestream the call must not
     * consume, filled with draw-dependent bytes so that consuming past the
     * line's own nibbles changes the digest instead of landing in zeroes. */
    for (uint32_t i = 0; i < (uint32_t)sizeof(c->payload); i++)
        c->payload[i] = (uint8_t)bench_rand_u32(&s, 0xFF);

    /* The writer starts where the reader will start: at the line's first nibble,
     * the low half of the current byte when a previous band left the sub-packet
     * there; nibw_init wants a value in the high half first, any value. */
    bw.mem = c->payload;
    bw.offset = STEP6_OFF;
    bw.bits_used = c->phase ? 4 : 0;
    bw.size = (uint32_t)sizeof(c->payload);
    if (c->phase)
        c->payload[STEP6_OFF] = (uint8_t)((c->payload[STEP6_OFF] & 0x0FU) | (bench_rand_u32(&s, 0xF) << 4));

    nibw_init(&nw, &bw);

    /* Most groups of a real 1080p line at 4 bpp are empty: their GCLI does not
     * exceed the truncation level.  The rest carry one to four planes above it,
     * which is the range the codec's own bit-plane budget lives in.  A GCLI
     * past TRUNCATION_MAX is not generated: no encoder emits one. */
    for (uint32_t g = 0; g < c->gcli_count; g++) {
        uint32_t planes;
        /* The short last group of a line is always given planes, so that its
         * coefficients reach the output. */
        const int leftover_bits = (c->leftover && g + 1 == c->gcli_count);
        if (!leftover_bits && bench_rand_u32(&s, 0xFF) < 222) { /* empty group: costs nothing */
            c->gclis[g] = (uint8_t)bench_rand_range(&s, 0, c->gtli);
            continue;
        }
        planes = 1 + bench_rand_u32(&s, leftover_bits ? 1 : 3);
        if (planes > (uint32_t)(STEP6_TRUNCATION_MAX - c->gtli))
            planes = (uint32_t)(STEP6_TRUNCATION_MAX - c->gtli);
        c->gclis[g] = (uint8_t)(c->gtli + planes);

        if (c->sign_flag == 0)
            nibw_put(&nw, bench_rand_u32(&s, 0xF)); /* the group's signs, first */
        for (uint32_t k = 0; k < planes; k++) {
            uint32_t nibble = bench_rand_u32(&s, 0xF);
            /* With hdr_Fs != 0 the call counts the non-zero coefficients of the
             * short last group in leftover_signs_num, and the decoder skips
             * that many sign bits, so the counter has to be right; the lowest
             * plane below is forced non-zero to keep it off zero. */
            if (leftover_bits && k + 1 == planes)
                for (uint32_t i = c->leftover; i < STEP6_GROUP_SIZE; i++)
                    nibble |= 1u << (STEP6_GROUP_SIZE - 1 - i);
            nibw_put(&nw, nibble);
        }
    }
    nibw_finish(&nw, &bw);

    /* Padding behind the line is never read; keep it deterministic anyway. */
    for (uint32_t g = c->gcli_count; g < STEP6_MAX_GROUPS + STEP6_PAD; g++)
        c->gclis[g] = 0;

    /* What a precinct still has to spend when this band line arrives: the line's
     * own cost (four bits per plane, plus one nibble of signs per non-empty
     * group when inline) plus what earlier bands left.  The first configuration
     * is the tight boundary, so the budget check runs at its edge. */
    {
        int64_t cost = 0;
        for (uint32_t g = 0; g < c->gcli_count; g++) {
            int32_t planes = (int32_t)c->gclis[g] - (int32_t)c->gtli;
            if (planes > 0)
                cost += 4 * (planes + (c->sign_flag == 0 ? 1 : 0));
        }
        c->budget0 = (int32_t)(cost + ((round <= 0) ? 0 : bench_rand_u32(&s, 0x3FFF)));
    }
    c->precinct_bits_left = c->budget0;

    /* Every coefficient the call is responsible for is written by it, so the
     * buffer is primed with a pattern no result can be confused with. */
    for (uint32_t i = 0; i < STEP6_MAXW + STEP6_PAD; i++)
        c->buf[i] = 0xA5A5;

    /* The reader, exactly as the decoder builds it before a data sub-packet. */
    bitstream_reader_init(&c->br, c->payload, STEP6_STREAM_BYTES);
    c->br.offset = STEP6_OFF;
    c->br.bits_used = c->phase ? 4 : 0;

    c->leftover_signs_num = (uint8_t)bench_rand_u32(&s, 0xFF); /* the call may leave it alone */
    c->ret = SvtJxsErrorNone;
}

static void unpack_call(void *v) {
    unpack_ctx *c = v;
    /* unpack_data consumes its input: the reader position and the precinct
     * budget both walk forward, so both are put back where setup left them
     * before each invocation - harness bookkeeping, not part of what is timed. */
    c->br.offset = STEP6_OFF;
    c->br.bits_used = c->phase ? 4 : 0;
    c->precinct_bits_left = c->budget0;
    c->ret = unpack_data(&c->br,
                         c->buf,
                         c->w,
                         c->gclis,
                         STEP6_GROUP_SIZE,
                         c->gtli,
                         c->sign_flag,
                         &c->leftover_signs_num,
                         &c->precinct_bits_left);
}

static void unpack_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    unpack_ctx *c = v;
    uint64_t h = bench_fnv1a(&c->ret, sizeof(c->ret), H0);
    h = bench_fnv1a(c->buf, (size_t)c->w * sizeof(uint16_t), h);
    h = bench_fnv1a(c->gclis, c->gcli_count, h);
    h = bench_fnv1a(&c->leftover_signs_num, sizeof(c->leftover_signs_num), h);
    h = bench_fnv1a(&c->precinct_bits_left, sizeof(c->precinct_bits_left), h);
    /* The reader is an in/out argument: where it stopped and on which half of a
     * byte is part of the call's effect, so it is hashed too. */
    h = bench_fnv1a(&c->br.offset, sizeof(c->br.offset), h);
    h = bench_fnv1a(&c->br.bits_used, sizeof(c->br.bits_used), h);
    bench_hash_finish(h, out);
}

static const bench_case step6_cases[] = {
    {"unpack_data", sizeof(unpack_ctx), unpack_setup, unpack_call, unpack_hash},
};

void bench_register_step06(void) {
    bench_add_all(step6_cases, sizeof(step6_cases) / sizeof(step6_cases[0]));
}
