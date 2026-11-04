# Milestone 3 — ITU-T H.264 Extended profile

Normative reference: **ITU-T Rec. H.264 / ISO-IEC 14496-10**. Clause numbers
below are that document's. The profile is defined in **Annex A.2.3 (Extended
profile)**.

Extended is the error-resilience profile. It combines the slice-group and
arbitrary-slice-order tools of `/app/SPEC/01-baseline.md` with the B slices, weighted
prediction and interlaced coding of `/app/SPEC/02-main.md`, **excludes CABAC**
(`entropy_coding_mode_flag` is 0 throughout this profile, A.2.3), and adds two
tool families of its own: slice data partitioning, and SP/SI switching slices.

Everything in both of those documents that Extended permits must keep working.

## Slice data partitioning

A coded slice may be split across three NAL units instead of one
(7.4.1, Table 7-1):

- **NAL unit type 2** — `slice_data_partition_a_layer_rbsp` (7.3.2.9.1): the
  slice header, the slice id, and all category 2 syntax elements — macroblock
  types, motion vectors, reference indices, `coded_block_pattern`,
  `mb_qp_delta`.
- **NAL unit type 3** — `slice_data_partition_b_layer_rbsp` (7.3.2.9.2):
  category 3 syntax elements, the residual of **intra** macroblocks, preceded by
  `slice_id` and, when `separate_colour_plane_flag` is 1, `colour_plane_id`, and
  by `redundant_pic_cnt` when `redundant_pic_cnt_present_flag` is 1.
- **NAL unit type 4** — `slice_data_partition_c_layer_rbsp` (7.3.2.9.3):
  category 4 syntax elements, the residual of **inter** macroblocks, with the
  same leading fields.

Requirements:

- Partitions are associated by `slice_id`, and partition A must be decoded
  before its B and C partitions can be interpreted.
- Partitions B and C are **optional**. When a partition is absent, the
  macroblocks that would have taken their residual from it are decoded with
  that residual absent — the prediction still applies.
- Data partitioning is CAVLC-only; `entropy_coding_mode_flag` is 0.

## SP and SI slices

`slice_type` values 3 and 8 (SP) and 4 and 9 (SI), the switching slices of
**clause 8.6**:

- **Slice header fields** (7.3.3): `sp_for_switch_flag` and `slice_qs_delta`,
  and the `QSy` derivation from `pic_init_qs_minus26`.
- **SP decoding for non-switching pictures** (8.6.1): prediction is formed as
  for a P slice, then transformed, and the transform coefficients of the
  prediction and of the residual are added **in the transform domain** before
  requantisation at `QSy` and inverse transformation.
- **SP decoding for switching pictures** (8.6.2), selected by
  `sp_for_switch_flag` equal to 1. The flag is present in the header of every SP
  slice and **must be parsed**: it sits immediately before `slice_qs_delta`, so
  a decoder that skips it desynchronises the bit reader and derives a wrong
  `QSy`. Its **reconstruction** path is another matter -- every SP slice in the
  bitstreams this milestone targets carries `sp_for_switch_flag` equal to 0, as
  do the ITU conformance SP streams, so none of them exercises the flag-1
  branch.
- **SI decoding** (8.6.2): the SI macroblock type, with Intra_4x4 prediction
  followed by the same transform-domain requantisation path.

## What is not in this profile

`entropy_coding_mode_flag` equal to 1 (CABAC) does not occur in an Extended
profile bitstream (A.2.3). `chroma_format_idc` is 1 and both bit depths are 8.

## Which output must match the reference

Data-partitioned and SP/SI bitstreams are treated differently, and the reason is
recorded here:

- **Data-partitioned streams must match on decoded samples**, frame by frame,
  exactly like every other stream.
- **SP and SI streams must match only on the output frames for which the
  reference encoder and the reference decoder agree**, plus the requirement that
  the decoder emit the correct number of output frames at the correct size. The
  SP reconstruction path in the published reference software is not bit-exact
  with the H.264.1 conformance reference output — measured, on `sp1_bt_a`, as a
  divergence beginning at frame 10 and growing to a maximum absolute sample
  difference of 32 by frame 300 — so no bit-exact expectation exists for those
  frames and none is demanded.
