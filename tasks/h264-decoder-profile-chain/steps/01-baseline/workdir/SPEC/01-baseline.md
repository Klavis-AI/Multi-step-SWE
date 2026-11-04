# Milestone 1 — ITU-T H.264 Baseline profile

Normative reference: **ITU-T Rec. H.264 / ISO-IEC 14496-10**, *Advanced video
coding for generic audiovisual services*. Clause numbers below are that
document's. The profile is defined in **Annex A.2.1 (Baseline profile)**.

The decoder must decode any bitstream conforming to the Baseline profile and
write its decoded pictures as `/app/TEST_INTERFACE.md` specifies.

## Bitstream and syntax

- **Annex B** byte stream: start code prefixes, trailing zero bytes, and
  `emulation_prevention_three_byte` removal (7.4.1.1).
- **NAL units** (7.3.1): `nal_ref_idc`, `nal_unit_type`; types 1 (non-IDR
  slice), 5 (IDR slice), 6 (SEI), 7 (SPS) and 8 (PPS). A NAL unit of a type the
  decoder does not use is discarded, as clause 7.4.1 requires.
- **Exp-Golomb** (9.1): `ue(v)`, `se(v)`, `me(v)` (Tables 9-4) and `te(v)`.
- **Sequence parameter set** (7.3.2.1) and **picture parameter set** (7.3.2.2),
  including multiple parameter sets active at different times in one stream.
- **Slice header** (7.3.3), **reference picture list modification** (7.3.3.1)
  and **decoded reference picture marking** (7.3.3.3).
- **Slice data** (7.3.4) with `mb_skip_run`.

Baseline permits slice types I and P only (`slice_type` 0, 2, 5, 7).

## Picture order and reference management

- **Picture order count** (8.2.1), all three of `pic_order_cnt_type` 0, 1 and 2.
- **Reference picture lists** (8.2.4): initialisation for P slices, and
  modification by `ref_pic_list_modification` including `long_term_pic_num`.
- **Decoded reference picture marking** (8.2.5): sliding window, adaptive
  marking with `memory_management_control_operation` 1-6, long-term references,
  and IDR handling (`no_output_of_prior_pics_flag`, `long_term_reference_flag`).
- **`gaps_in_frame_num_value_allowed_flag`** (8.2.5.2): inference of
  non-existing frames.
- **Output order**: the bumping process of **Annex C**.

## Macroblock decoding

- **Macroblock types** (7.4.5, Tables 7-11 and 7-13): `I_4x4`, `I_16x16` in all
  its `CodedBlockPatternChroma`/prediction-mode combinations, `I_PCM`, `P_L0_16x16`,
  `P_L0_L0_16x8`, `P_L0_L0_8x16`, `P_8x8`, `P_8x8ref0` and `P_Skip`, with
  sub-macroblock types (Table 7-17).
- **Intra prediction**: Intra_4x4 with all nine prediction modes and the
  `Intra4x4PredMode` inference rule (8.3.1); Intra_16x16 with its four modes
  (8.3.3); chroma with its four modes (8.3.4); `constrained_intra_pred_flag`.
- **Inter prediction** (8.4): motion vector prediction including the median
  predictor and the 16x8 / 8x16 directional rules, `P_Skip` motion vector
  derivation, quarter-sample luma interpolation with the six-tap filter
  (8.4.2.2.1) and eighth-sample chroma interpolation (8.4.2.2.2), and
  prediction from multiple reference frames.
- **Transform and dequantisation** (8.5): the 4x4 residual transform, the
  Intra_16x16 luma DC Hadamard transform, the 2x2 chroma DC transform,
  `QPy`/`QPc` derivation with `chroma_qp_index_offset` (Table 8-15), and
  `mb_qp_delta`.
- **CAVLC** (9.2): `coeff_token` with the `nC` derivation from neighbouring
  blocks, trailing ones and their signs, level decoding with `suffixLength`
  escalation and the level_prefix 15/16 escapes, `total_zeros` and `run_before`.

## In-loop deblocking

**Clause 8.7** in full: boundary strength derivation, the `alpha`/`beta` tables
and `tC0` (Tables 8-16 and 8-17), luma and chroma filtering, the strong
bS = 4 filter, `disable_deblocking_filter_idc` values 0, 1 and 2 (including
the "filter across slice boundaries" distinction), and
`slice_alpha_c0_offset_div2` / `slice_beta_offset_div2`.

## Slice groups and arbitrary slice order

- **Macroblock to slice group map** (8.2.2): all seven `slice_group_map_type`
  values 0-6, `slice_group_change_direction_flag` and
  `slice_group_change_rate_minus1`, and the `slice_group_id` list for type 6.
- **Arbitrary slice order**: slices of a picture may arrive in any order of
  `first_mb_in_slice`.

## Output

- **Frame cropping** from the active SPS (7.4.2.1.1), applied on output.
- `chroma_format_idc` is 1 (4:2:0) and both bit depths are 8 throughout this
  profile (Annex A.2.1), so output samples are one byte each.
