// bench_phy -- per-function measurement harness for the srsRAN 5G NR PHY
// optimization ladder.
//
//   bench_phy <fnid> <seed> bench [reps]
//         -> "fnid <median_ns> <min_ns> <max_ns> <reps>" on stdout
//   bench_phy <fnid> <seed> dump
//         -> the raw output bytes of one call on stdout, and nothing else
//
// The same source is compiled twice per step: once against the step's own base
// tree and once against the candidate tree.  Every function is reached through
// the public factory using the library's DEFAULT selector, so what is timed is
// the implementation the library installs -- an agent who speeds up the
// portable path is measured exactly like one who writes a kernel.
//
// The workload is not an argument: each function's inputs are generated here,
// in this file, from <seed>.  Both trees therefore see byte-identical inputs
// for a given seed, and a candidate cannot choose them.  Each timed repetition
// uses a DIFFERENT draw from the seed, so a result cached against one input
// does not help with the next.
//
// Aggregator: the MEDIAN of the per-repetition times.  Best-of-N is wrong for a
// ratio -- it suppresses slow outliers, which is what it is for, but it also
// preserves spuriously fast ones, and a ratio reports those as speedup.

#include "srsran/adt/bit_buffer.h"
#include "srsran/adt/complex.h"
#include "srsran/adt/span.h"
#include "srsran/ofh/compression/compression_factory.h"
#include "srsran/ofh/compression/compression_params.h"
#include "srsran/ofh/compression/compression_properties.h"
#include "srsran/phy/constants.h"
#include "srsran/phy/generic_functions/precoding/precoding_factories.h"
#include "srsran/phy/support/re_buffer.h"
#include "srsran/phy/upper/channel_coding/channel_coding_factories.h"
#include "srsran/phy/upper/channel_coding/ldpc/ldpc.h"
#include "srsran/phy/upper/channel_coding/ldpc/ldpc_encoder_buffer.h"
#include "srsran/phy/upper/channel_modulation/channel_modulation_factories.h"
#include "srsran/phy/upper/log_likelihood_ratio.h"
#include "srsran/ran/bs_channel_bandwidth.h"
#include "srsran/ran/cyclic_prefix.h"
#include "srsran/ran/precoding/precoding_weight_matrix.h"
#include "srsran/ran/resource_block.h"
#include "srsran/ran/sch/modulation_scheme.h"
#include "srsran/ran/subcarrier_spacing.h"
#include "srsran/srslog/srslog.h"
#include "srsran/support/srsran_assert.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace srsran;

namespace {

// Volatile sink: keeps the calls in the measured region from being elided.
volatile uint64_t g_sink = 0;

using clock_type = std::chrono::steady_clock;

double ns_between(clock_type::time_point a, clock_type::time_point b)
{
  return std::chrono::duration<double, std::nano>(b - a).count();
}

// The number of RE a full-bandwidth (106 RB) slot carries, as the project's own
// precoder benchmark uses it.
constexpr unsigned NOF_RB_BENCH = 106;
constexpr unsigned NOF_RE_BENCH = NOF_RB_BENCH * NRE;

// ---------------------------------------------------------------------------
// Case interface
// ---------------------------------------------------------------------------
class Case
{
public:
  virtual ~Case() = default;

  // Builds every input this case will use, one per timed repetition. Never
  // timed. The seed is supplied by the caller and fixed by the verifier, and it
  // lives in the verifier image alone, so the draw cannot be learned by
  // running the harness.
  virtual void prepare(unsigned reps, uint32_t seed) = 0;

  // The measured call, reading the input built for repetition `rep`.
  virtual void run(unsigned rep) = 0;

  // Writes the output of the most recent run to `f`.
  virtual void dump(FILE* f) = 0;
};

// ---------------------------------------------------------------------------
// LDPC encoder
// ---------------------------------------------------------------------------
class EncCase : public Case
{
public:
  EncCase(ldpc_base_graph_type bg, unsigned ls, uint32_t seed_) :
    seed(seed_), msg_len(((bg == ldpc_base_graph_type::BG1) ? 22U : 10U) * ls)
  {
    std::shared_ptr<ldpc_encoder_factory> factory = create_ldpc_encoder_factory_sw("auto");
    srsran_assert(factory, "no LDPC encoder factory");
    enc = factory->create();
    srsran_assert(enc, "the library installed no LDPC encoder for the default selector");
    cfg = {.base_graph = bg, .lifting_size = static_cast<ldpc::lifting_size_t>(ls), .Nref = 0};
  }

  void prepare(unsigned reps, uint32_t seed_) override
  {
    seed = seed_;
    // Every input, and every output buffer, is materialised here: the measured
    // region is the library call and nothing else. Building the input inside
    // the timed region would add a constant to both sides and compress the
    // ratio it is supposed to report.
    msgs.clear();
    msgs.reserve(reps);
    for (unsigned r = 0; r != reps; ++r) {
      std::mt19937     rgen(seed + 0x9E3779B9U * (r + 1));
      dynamic_bit_buffer d(msg_len);
      for (unsigned i = 0; i != msg_len; ++i) {
        d.insert(rgen() & 1U, i, 1);
      }
      msgs.push_back(std::move(d));
    }
    codes.assign(reps, std::vector<uint8_t>());
  }

  void run(unsigned rep) override
  {
    const ldpc_encoder_buffer& cb = enc->encode(msgs[rep], cfg);
    unsigned                   len = cb.get_codeblock_length();
    std::vector<uint8_t>&      out = codes[rep];
    if (out.size() != len) {
      out.assign(len, 0);
    }
    cb.write_codeblock(out, 0);
    g_sink += out[0];
  }

  void dump(FILE* f) override { fwrite(codes[last_rep].data(), 1, codes[last_rep].size(), f); }

  unsigned last_rep = 0;

private:
  uint32_t                        seed;
  unsigned                        msg_len;
  ldpc_encoder::configuration     cfg{};
  std::unique_ptr<ldpc_encoder>   enc;
  std::vector<dynamic_bit_buffer>   msgs;
  std::vector<std::vector<uint8_t>> codes;
};

// ---------------------------------------------------------------------------
// LDPC rate dematcher
// ---------------------------------------------------------------------------
class RdmCase : public Case
{
public:
  RdmCase(ldpc_base_graph_type bg, unsigned ls, unsigned rv, bool new_data_) :
    new_data(new_data_), full_length(((bg == ldpc_base_graph_type::BG1) ? 66U : 50U) * ls),
    rm_length(((bg == ldpc_base_graph_type::BG1) ? 24U : 12U) * ls)
  {
    std::shared_ptr<ldpc_rate_dematcher_factory> factory = create_ldpc_rate_dematcher_factory_sw("auto");
    srsran_assert(factory, "no LDPC rate dematcher factory");
    dem = factory->create();
    srsran_assert(dem, "the library installed no LDPC rate dematcher for the default selector");
    cfg.tb_common.base_graph   = bg;
    cfg.tb_common.lifting_size = static_cast<ldpc::lifting_size_t>(ls);
    cfg.tb_common.rv           = rv;
    cfg.tb_common.mod          = modulation_scheme::QPSK;
    cfg.tb_common.Nref         = 0;
    cfg.tb_common.cw_length    = rm_length;
    cfg.cb_specific.full_length      = full_length;
    cfg.cb_specific.rm_length        = rm_length;
    cfg.cb_specific.nof_filler_bits  = 0;
    cfg.cb_specific.cw_offset        = 0;
    cfg.cb_specific.nof_crc_bits     = 16;
  }

  void prepare(unsigned reps, uint32_t seed) override
  {
    in.assign(reps, {});
    out.assign(reps, {});
    for (unsigned r = 0; r != reps; ++r) {
      std::mt19937 rgen(seed + 0x9E3779B9U * (r + 1));
      in[r].resize(rm_length);
      for (unsigned i = 0; i != rm_length; ++i) {
        // A noisy received block: most soft bits strong, a few near zero.
        int v = static_cast<int>(rgen() & 0x1FU) - 16;
        if ((rgen() & 7U) == 0) {
          v = static_cast<int>(rgen() % 5U) - 2;
        }
        in[r][i] = log_likelihood_ratio(static_cast<int8_t>(std::max(-120, std::min(120, v * 4))));
      }
      // The combining path accumulates onto the soft block already held for
      // this codeblock, so a dematcher that wrongly overwrites instead of
      // accumulating is caught. Each repetition owns its output buffer, so the
      // reset costs the measured region nothing.
      out[r].resize(full_length);
      for (unsigned i = 0; i != full_length; ++i) {
        out[r][i] = log_likelihood_ratio(static_cast<int8_t>((rgen() % 41U) - 20));
      }
    }
  }

  void run(unsigned rep) override
  {
    dem->rate_dematch(out[rep], in[rep], new_data, cfg);
    g_sink += out[rep][0].to_value_type();
  }

  void dump(FILE* f) override
  {
    fwrite(reinterpret_cast<const int8_t*>(out[last_rep].data()), 1, out[last_rep].size() * sizeof(out[last_rep][0]), f);
  }

  unsigned last_rep = 0;

private:
  bool                                    new_data;
  unsigned                                full_length;
  unsigned                                rm_length;
  codeblock_metadata                      cfg{};
  std::unique_ptr<ldpc_rate_dematcher>    dem;
  std::vector<std::vector<log_likelihood_ratio>> in;
  std::vector<std::vector<log_likelihood_ratio>> out;
};

// ---------------------------------------------------------------------------
// LDPC decoder
// ---------------------------------------------------------------------------
class DecCase : public Case
{
public:
  // The input is a real codeblock: a random message, encoded with the tree's
  // PORTABLE encoder (bit-identical in every tree, so both sides see the same
  // bytes), turned into soft bits. `noise_percent` of the codeblock bits are
  // received inverted and with a weak magnitude. The rate is as high as the two
  // implementations' output agreement allows, and high enough that a draw does
  // not settle into a fixed point in its first iterations: the six-iteration
  // decode is the work the measurement prices, and a draw that settled early
  // would let an implementation do less of that work and still reproduce the
  // reference bytes -- which an output comparison cannot see.
  DecCase(ldpc_base_graph_type bg_, unsigned ls_, unsigned noise_percent_) :
    bg(bg_), ls(ls_), noise_percent(noise_percent_),
    msg_len(((bg == ldpc_base_graph_type::BG1) ? 22U : 10U) * ls), out(msg_len)
  {
    auto dec_factory = create_ldpc_decoder_factory_sw("auto", {.force_decoding = false});
    srsran_assert(dec_factory, "no LDPC decoder factory");
    dec = dec_factory->create();
    srsran_assert(dec, "the library installed no LDPC decoder for the default selector");

    auto enc_factory = create_ldpc_encoder_factory_sw("generic");
    srsran_assert(enc_factory, "no LDPC encoder factory");
    enc = enc_factory->create();
    srsran_assert(enc, "the portable LDPC encoder is missing from this tree");

    cfg.base_graph      = bg;
    cfg.lifting_size    = static_cast<ldpc::lifting_size_t>(ls);
    cfg.nof_filler_bits = 0;
    cfg.nof_crc_bits    = 16;
    cfg.max_iterations  = 6;

    enc_cfg = {.base_graph = bg, .lifting_size = static_cast<ldpc::lifting_size_t>(ls), .Nref = 0};
  }

  void prepare(unsigned reps, uint32_t seed) override
  {
    dynamic_bit_buffer       msg(msg_len);
    std::vector<uint8_t>     cb;
    in.assign(reps, {});
    for (unsigned r = 0; r != reps; ++r) {
      std::mt19937 rgen(seed + 0x9E3779B9U * (r + 1));
      for (unsigned i = 0; i != msg_len; ++i) {
        msg.insert(rgen() & 1U, i, 1);
      }
      const ldpc_encoder_buffer& enc_out = enc->encode(msg, enc_cfg);
      unsigned                   len     = enc_out.get_codeblock_length();
      if (cb.size() != len) {
        cb.assign(len, 0);
      }
      enc_out.write_codeblock(cb, 0);
      in[r].resize(len);
      for (unsigned i = 0; i != len; ++i) {
        // A transmitted bit is a POSITIVE log-likelihood ratio when it is 0 and
        // a negative one when it is 1; inverting this convention does not
        // matter to a differential check between two implementations, but it
        // takes the decoder out of the regime it is specified for and would
        // measure the two paths' rounding on input neither is meant to see.
        int v = cb[i] ? -10 : 10;
        if ((noise_percent != 0) && ((rgen() % 100U) < noise_percent)) {
          v = (v > 0) ? -3 : 3;
        }
        in[r][i] = log_likelihood_ratio(static_cast<int8_t>(v));
      }
    }
  }

  void run(unsigned rep) override
  {
    // A null CRC calculator disables early stopping, so the iteration count --
    // and therefore the timing -- does not depend on how quickly this
    // particular input happens to converge.
    dec->decode(out, in[rep], nullptr, cfg);
    g_sink += out.extract<uint8_t>(0, 1);
  }

  void dump(FILE* f) override
  {
    std::vector<uint8_t> packed((out.size() + 7) / 8, 0);
    for (unsigned i = 0; i != out.size(); ++i) {
      packed[i / 8] |= static_cast<uint8_t>(out.extract<uint8_t>(i, 1) << (7 - (i % 8)));
    }
    fwrite(packed.data(), 1, packed.size(), f);
  }

private:
  ldpc_base_graph_type          bg;
  unsigned                      ls;
  unsigned                      noise_percent;
  unsigned                      msg_len;
  ldpc_decoder::configuration   cfg{};
  ldpc_encoder::configuration   enc_cfg{};
  std::unique_ptr<ldpc_decoder> dec;
  std::unique_ptr<ldpc_encoder> enc;
  std::vector<std::vector<log_likelihood_ratio>> in;
  dynamic_bit_buffer            out;
};

// ---------------------------------------------------------------------------
// Channel precoder
// ---------------------------------------------------------------------------
class PrecodeCase : public Case
{
public:
  PrecodeCase(unsigned nof_ports_, unsigned nof_layers_, bool layer_map_) :
    nof_ports(nof_ports_), nof_layers(nof_layers_), layer_map(layer_map_)
  {
    std::shared_ptr<channel_precoder_factory> factory = create_channel_precoder_factory("auto");
    srsran_assert(factory, "no channel precoder factory");
    prec = factory->create();
    srsran_assert(prec, "the library installed no channel precoder for the default selector");
  }

  void prepare(unsigned reps, uint32_t seed) override
  {
    in_cf.clear();
    out_re.clear();
    cf_pool.assign(reps, {});
    ci_pool.assign(reps, {});
    w_pool.clear();
    w_pool.reserve(reps);
    std::uniform_real_distribution<float> cf_dist(-2.0F, 2.0F);
    std::uniform_real_distribution<float> w_dist(-1.0F, 1.0F);
    std::uniform_int_distribution<int>    ci_dist(-10, 10);
    for (unsigned r = 0; r != reps; ++r) {
      std::mt19937 rgen(seed + 0x9E3779B9U * (r + 1));
      cf_pool[r].resize(nof_layers * NOF_RE_BENCH);
      for (auto& s : cf_pool[r]) {
        s = cf_t(cf_dist(rgen), cf_dist(rgen));
      }
      ci_pool[r].resize(nof_layers * NOF_RE_BENCH);
      for (auto& s : ci_pool[r]) {
        s = ci8_t(static_cast<int8_t>(ci_dist(rgen)), static_cast<int8_t>(ci_dist(rgen)));
      }
      // Built here, not in run(): the weight matrix must be constructed outside
      // the measured span.
      precoding_weight_matrix w(nof_layers, nof_ports);
      for (unsigned p = 0; p != nof_ports; ++p) {
        span<cf_t> port_weights = w.get_port_coefficients(p);
        for (auto& v : port_weights) {
          v = cf_t(w_dist(rgen), w_dist(rgen));
        }
      }
      w_pool.push_back(w);

      // The library call takes a re_buffer_reader, so each repetition needs its
      // own; filling it here keeps the copy out of the measured region.
      in_cf.push_back(std::make_unique<dynamic_re_buffer<cf_t>>(nof_layers, NOF_RE_BENCH));
      out_re.push_back(std::make_unique<dynamic_re_buffer<cbf16_t>>(nof_ports, NOF_RE_BENCH));
      for (unsigned l = 0; l != nof_layers; ++l) {
        std::copy_n(cf_pool[r].begin() + l * NOF_RE_BENCH, NOF_RE_BENCH, in_cf[r]->get_slice(l).begin());
      }
    }
  }

  void run(unsigned rep) override
  {
    if (layer_map) {
      prec->apply_layer_map_and_precoding(*out_re[rep], span<const ci8_t>(ci_pool[rep]), w_pool[rep]);
    } else {
      prec->apply_precoding(*out_re[rep], *in_cf[rep], w_pool[rep]);
    }
    g_sink += reinterpret_cast<const uint16_t*>(out_re[rep]->get_slice(0).data())[0];
  }

  void dump(FILE* f) override
  {
    for (unsigned p = 0; p != nof_ports; ++p) {
      span<const cbf16_t> s = out_re[dumped]->get_slice(p);
      fwrite(s.data(), sizeof(cbf16_t), s.size(), f);
    }
  }

  unsigned dumped = 0;

private:
  unsigned                          nof_ports;
  unsigned                          nof_layers;
  bool                              layer_map;
  std::unique_ptr<channel_precoder> prec;
  std::vector<std::unique_ptr<dynamic_re_buffer<cf_t>>>    in_cf;
  std::vector<std::unique_ptr<dynamic_re_buffer<cbf16_t>>> out_re;
  std::vector<precoding_weight_matrix> w_pool;
  std::vector<std::vector<cf_t>>    cf_pool;
  std::vector<std::vector<ci8_t>>   ci_pool;
};

// ---------------------------------------------------------------------------
// Demodulation mapper
// ---------------------------------------------------------------------------
class DemodCase : public Case
{
public:
  DemodCase(modulation_scheme mod_) : mod(mod_), nof_symbols(NOF_RE_BENCH)
  {
    std::shared_ptr<demodulation_mapper_factory> factory = create_demodulation_mapper_factory();
    srsran_assert(factory, "no demodulation mapper factory");
    dem = factory->create();
    srsran_assert(dem, "the library installed no demodulation mapper");
    llr.resize(static_cast<size_t>(nof_symbols) * get_bits_per_symbol(mod));
  }

  void prepare(unsigned reps, uint32_t seed) override
  {
    sym_pool.assign(reps, {});
    nv_pool.assign(reps, {});
    std::uniform_real_distribution<float> sym_dist(-2.0F, 2.0F);
    std::uniform_real_distribution<float> nv_dist(0.5F, 2.0F);
    for (unsigned r = 0; r != reps; ++r) {
      std::mt19937 rgen(seed + 0x9E3779B9U * (r + 1));
      sym_pool[r].resize(nof_symbols);
      nv_pool[r].resize(nof_symbols);
      for (unsigned i = 0; i != nof_symbols; ++i) {
        sym_pool[r][i] = cf_t(sym_dist(rgen), sym_dist(rgen));
        nv_pool[r][i]  = nv_dist(rgen);
      }
    }
  }

  void run(unsigned rep) override
  {
    dem->demodulate_soft(llr, sym_pool[rep], nv_pool[rep], mod);
    g_sink += llr[0].to_value_type();
  }

  void dump(FILE* f) override
  {
    fwrite(reinterpret_cast<const int8_t*>(llr.data()), 1, llr.size() * sizeof(llr[0]), f);
  }

private:
  modulation_scheme                            mod;
  unsigned                                     nof_symbols;
  std::unique_ptr<demodulation_mapper>         dem;
  std::vector<log_likelihood_ratio>            llr;
  std::vector<std::vector<cf_t>>               sym_pool;
  std::vector<std::vector<float>>              nv_pool;
};

// ---------------------------------------------------------------------------
// O-RAN fronthaul IQ compression / decompression
// ---------------------------------------------------------------------------
class OfhCase : public Case
{
public:
  OfhCase(bool compress_, ofh::compression_type type_, unsigned width_, unsigned nof_prbs_) :
    compress(compress_), params({type_, width_}), nof_prbs(nof_prbs_)
  {
    logger.set_level(srslog::basic_levels::none);
    if (compress) {
      obj_c = ofh::create_iq_compressor(params.type, logger, 0.27F);
      srsran_assert(obj_c != nullptr, "the library installed no IQ compressor for the default selector");
    } else {
      obj_d = ofh::create_iq_decompressor(params.type, logger);
      srsran_assert(obj_d != nullptr, "the library installed no IQ decompressor for the default selector");
    }
    nof_samples = nof_prbs * NOF_SUBCARRIERS_PER_RB;
    comp_size   = ofh::get_compressed_prb_size(params).value() * nof_prbs;
  }

  void prepare(unsigned reps, uint32_t seed) override
  {
    // The compressed input for the decompression direction is produced by the
    // PORTABLE compressor, which both trees share unchanged. Building it with
    // this tree's own default compressor would hand the two trees different
    // bytes and make the differential check meaningless.
    std::unique_ptr<ofh::iq_compressor> ref = ofh::create_iq_compressor(params.type, logger, 0.27F, "generic");
    srsran_assert(ref != nullptr, "the portable IQ compressor is missing from this tree");

    pool_iq.assign(reps, {});
    pool_u8.assign(reps, {});
    for (unsigned r = 0; r != reps; ++r) {
      std::mt19937                          rgen(seed + 0x9E3779B9U * (r + 1));
      std::uniform_real_distribution<float> dist(-1.0F, 1.0F);
      pool_iq[r].resize(nof_samples);
      for (auto& s : pool_iq[r]) {
        s = cbf16_t{dist(rgen), dist(rgen)};
      }
      if (!compress) {
        // The compressed input is produced by the PORTABLE compressor, which
        // both trees share unchanged. Building it with this tree's own default
        // compressor would hand the two trees different bytes and make the
        // differential check meaningless.
        pool_u8[r].assign(comp_size, 0);
        ref->compress(pool_u8[r], pool_iq[r], params);
      }
    }
    out_u8.assign(reps, std::vector<uint8_t>(comp_size));
    out_iq.assign(reps, std::vector<cbf16_t>(nof_samples));
  }

  void run(unsigned rep) override
  {
    if (compress) {
      obj_c->compress(out_u8[rep], pool_iq[rep], params);
      g_sink += out_u8[rep][0];
    } else {
      obj_d->decompress(out_iq[rep], pool_u8[rep], params);
      g_sink += reinterpret_cast<const uint16_t*>(out_iq[rep].data())[0];
    }
  }

  void dump(FILE* f) override
  {
    if (compress) {
      fwrite(out_u8[last_rep].data(), 1, out_u8[last_rep].size(), f);
    } else {
      fwrite(out_iq[last_rep].data(), sizeof(cbf16_t), out_iq[last_rep].size(), f);
    }
  }

  unsigned last_rep = 0;

private:
  bool                                 compress;
  ofh::ru_compression_params           params;
  unsigned                             nof_prbs;
  unsigned                             nof_samples;
  unsigned                             comp_size;
  srslog::basic_logger&                logger = srslog::fetch_basic_logger("OFH-BENCH", false);
  std::unique_ptr<ofh::iq_compressor>  obj_c;
  std::unique_ptr<ofh::iq_decompressor> obj_d;
  std::vector<std::vector<cbf16_t>>   pool_iq;
  std::vector<std::vector<uint8_t>>   pool_u8;
  std::vector<std::vector<uint8_t>>    out_u8;
  std::vector<std::vector<cbf16_t>>    out_iq;
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
std::unique_ptr<Case> make_case(const std::string& fnid, std::string& err)
{
  const unsigned LS256 = 256;
  const unsigned LS320 = 320;
  const unsigned LS384 = 384;

  if (fnid == "ldpc_enc_bg1_ls256") {
    return std::make_unique<EncCase>(ldpc_base_graph_type::BG1, LS256, 0);
  }
  if (fnid == "ldpc_enc_bg1_ls320") {
    return std::make_unique<EncCase>(ldpc_base_graph_type::BG1, LS320, 0);
  }
  if (fnid == "ldpc_enc_bg1_ls384") {
    return std::make_unique<EncCase>(ldpc_base_graph_type::BG1, LS384, 0);
  }
  if (fnid == "ldpc_enc_bg2_ls256") {
    return std::make_unique<EncCase>(ldpc_base_graph_type::BG2, LS256, 0);
  }
  if (fnid == "ldpc_enc_bg2_ls320") {
    return std::make_unique<EncCase>(ldpc_base_graph_type::BG2, LS320, 0);
  }
  if (fnid == "ldpc_enc_bg2_ls384") {
    return std::make_unique<EncCase>(ldpc_base_graph_type::BG2, LS384, 0);
  }

  if (fnid == "ldpc_rdm_bg1_ls256_rv0") {
    return std::make_unique<RdmCase>(ldpc_base_graph_type::BG1, LS256, 0, true);
  }
  if (fnid == "ldpc_rdm_bg1_ls384_rv0") {
    return std::make_unique<RdmCase>(ldpc_base_graph_type::BG1, LS384, 0, true);
  }
  if (fnid == "ldpc_rdm_bg1_ls384_rv2") {
    return std::make_unique<RdmCase>(ldpc_base_graph_type::BG1, LS384, 2, true);
  }
  if (fnid == "ldpc_rdm_bg1_ls384_comb") {
    return std::make_unique<RdmCase>(ldpc_base_graph_type::BG1, LS384, 3, false);
  }
  if (fnid == "ldpc_rdm_bg2_ls256_rv0") {
    return std::make_unique<RdmCase>(ldpc_base_graph_type::BG2, LS256, 0, true);
  }
  if (fnid == "ldpc_rdm_bg2_ls384_rv1") {
    return std::make_unique<RdmCase>(ldpc_base_graph_type::BG2, LS384, 1, true);
  }

  if (fnid == "ldpc_dec_bg1_ls256") {
    return std::make_unique<DecCase>(ldpc_base_graph_type::BG1, LS256, 20);
  }
  if (fnid == "ldpc_dec_bg1_ls384") {
    return std::make_unique<DecCase>(ldpc_base_graph_type::BG1, LS384, 20);
  }
  if (fnid == "ldpc_dec_bg2_ls256") {
    return std::make_unique<DecCase>(ldpc_base_graph_type::BG2, LS256, 20);
  }
  if (fnid == "ldpc_dec_bg2_ls384") {
    return std::make_unique<DecCase>(ldpc_base_graph_type::BG2, LS384, 20);
  }

  if (fnid == "precode_ci8_1x1") {
    return std::make_unique<PrecodeCase>(1, 1, true);
  }
  if (fnid == "precode_ci8_2x2") {
    return std::make_unique<PrecodeCase>(2, 2, true);
  }
  if (fnid == "precode_ci8_4x4") {
    return std::make_unique<PrecodeCase>(4, 4, true);
  }

  if (fnid == "demod_qpsk") {
    return std::make_unique<DemodCase>(modulation_scheme::QPSK);
  }
  if (fnid == "demod_qam16") {
    return std::make_unique<DemodCase>(modulation_scheme::QAM16);
  }
  if (fnid == "demod_qam64") {
    return std::make_unique<DemodCase>(modulation_scheme::QAM64);
  }
  if (fnid == "demod_qam256") {
    return std::make_unique<DemodCase>(modulation_scheme::QAM256);
  }

  if (fnid == "ofh_bfp9_compress") {
    return std::make_unique<OfhCase>(true, ofh::compression_type::BFP, 9, 51);
  }
  if (fnid == "ofh_bfp9_decompress") {
    return std::make_unique<OfhCase>(false, ofh::compression_type::BFP, 9, 51);
  }
  if (fnid == "ofh_bfp16_compress") {
    return std::make_unique<OfhCase>(true, ofh::compression_type::BFP, 16, 51);
  }
  if (fnid == "ofh_bfp16_decompress") {
    return std::make_unique<OfhCase>(false, ofh::compression_type::BFP, 16, 51);
  }
  if (fnid == "ofh_none16_compress") {
    return std::make_unique<OfhCase>(true, ofh::compression_type::none, 16, 51);
  }
  if (fnid == "ofh_none16_decompress") {
    return std::make_unique<OfhCase>(false, ofh::compression_type::none, 16, 51);
  }

  err = "unknown function id: " + fnid;
  return nullptr;
}

void report_known_ids()
{
  static const char* ids[] = {
      "ldpc_enc_bg1_ls256",       "ldpc_enc_bg1_ls320",      "ldpc_enc_bg1_ls384",
      "ldpc_enc_bg2_ls256",       "ldpc_enc_bg2_ls320",      "ldpc_enc_bg2_ls384",
      "ldpc_rdm_bg1_ls256_rv0",   "ldpc_rdm_bg1_ls384_rv0",  "ldpc_rdm_bg1_ls384_rv2",
      "ldpc_rdm_bg1_ls384_comb",  "ldpc_rdm_bg2_ls256_rv0",  "ldpc_rdm_bg2_ls384_rv1",
      "ldpc_dec_bg1_ls256",       "ldpc_dec_bg1_ls384",      "ldpc_dec_bg2_ls256",
      "ldpc_dec_bg2_ls384",       "precode_ci8_1x1",         "precode_ci8_2x2",
      "precode_ci8_4x4",          "demod_qpsk",              "demod_qam16",
      "demod_qam64",              "demod_qam256",            "ofh_bfp9_compress",
      "ofh_bfp9_decompress",      "ofh_bfp16_compress",      "ofh_bfp16_decompress",
      "ofh_none16_compress",      "ofh_none16_decompress",
  };
  for (const char* id : ids) {
    printf("%s\n", id);
  }
}

} // namespace

int main(int argc, char** argv)
{
  if (argc >= 2 && std::string(argv[1]) == "--list") {
    report_known_ids();
    return 0;
  }
  if (argc < 4) {
    fprintf(stderr, "usage: %s <fnid> <seed> <bench|dump> [reps]\n", argv[0]);
    fprintf(stderr, "       %s --list\n", argv[0]);
    return 2;
  }
  const std::string fnid = argv[1];
  const uint32_t    seed = static_cast<uint32_t>(strtoul(argv[2], nullptr, 10));
  const std::string mode = argv[3];
  unsigned          reps = (argc > 4) ? static_cast<unsigned>(strtoul(argv[4], nullptr, 10)) : 301;

  std::string err;
  std::unique_ptr<Case> c = make_case(fnid, err);
  if (c == nullptr) {
    fprintf(stderr, "%s\n", err.c_str());
    return 2;
  }

  if (mode == "dump") {
    c->prepare(1, seed);
    c->run(0);
    c->dump(stdout);
    fflush(stdout);
    return 0;
  }

  if (mode != "bench") {
    fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
  }

  c->prepare(reps, seed);

  // Warm-up: one untimed pass over a few repetitions so that the first timed
  // call is not paying for cold caches and lazy one-time initialisation.
  for (unsigned r = 0; r != std::min(reps, 8U); ++r) {
    c->run(r);
  }

  std::vector<double> t(reps);
  for (unsigned r = 0; r != reps; ++r) {
    clock_type::time_point t0 = clock_type::now();
    c->run(r);
    clock_type::time_point t1 = clock_type::now();
    t[r] = ns_between(t0, t1);
  }

  std::vector<double> sorted = t;
  std::sort(sorted.begin(), sorted.end());
  double median = sorted[sorted.size() / 2];

  printf("%s %.1f %.1f %.1f %u\n", fnid.c_str(), median, sorted.front(), sorted.back(), reps);
  fflush(stdout);
  return 0;
}
