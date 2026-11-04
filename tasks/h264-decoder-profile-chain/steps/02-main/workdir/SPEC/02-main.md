# Milestone 2 — ITU-T H.264 Main profile

Normative reference: **ITU-T Rec. H.264 / ISO-IEC 14496-10**. Clause numbers
below are that document's. The profile is defined in **Annex A.2.2 (Main
profile)**.

Main adds three things to the tool set in `/app/SPEC/01-baseline.md`: a second
entropy coder, bi-predictive slices, and interlaced coding. It also *removes* the slice-group tools of that
document, and forbids redundant coded pictures, which Baseline permits but
`/app/SPEC/01-baseline.md` does not require: a Main profile bitstream has
`num_slice_groups_minus1` equal to 0 and carries no redundant coded pictures
(A.2.2) — while everything else in that document must keep working, because a
Main profile stream still uses I and P slices, the same transforms, the same
deblocking filter and the same reference-picture management.

## CABAC — context-adaptive binary arithmetic coding

**Clause 9.3** in full, selected by `entropy_coding_mode_flag` equal to 1 in the
picture parameter set:

- Initialisation of all context variables from the tables of 9.3.1.1, for every
  `cabac_init_idc` and every slice QP.
- The arithmetic decoding engine (9.3.3.2): `DecodeDecision`, renormalisation,
  `DecodeBypass` and `DecodeTerminate`.
- Binarization and context index derivation (9.3.2, 9.3.3.1) for every syntax
  element CABAC codes, including `mb_skip_flag`, `mb_type`, `sub_mb_type`,
  `mvd_l0`/`mvd_l1`, `ref_idx_l0`/`ref_idx_l1`, `coded_block_pattern`,
  `mb_qp_delta`, `intra_chroma_pred_mode`, `coded_block_flag`,
  `significant_coeff_flag`, `last_significant_coeff_flag` and
  `coeff_abs_level_minus1`, with the UEGk suffixes.
- `cabac_alignment_one_bit` and the `end_of_slice_flag` termination.

CAVLC (`entropy_coding_mode_flag` equal to 0) remains a Main profile
possibility and must keep working.

## B slices

- `slice_type` values 1 and 6, and the macroblock and sub-macroblock types of
  Tables 7-14 and 7-18: `B_Direct_16x16`, `B_L0_*`, `B_L1_*`, `B_Bi_*`,
  `B_8x8` and `B_Skip`.
- **Reference picture list 1** initialisation (8.2.4.2.3 for frames, 8.2.4.2.5
  for fields) and **modification** (8.2.4.3, driven by
  `ref_pic_list_modification_flag_l1`), and `num_ref_idx_l1_active_minus1`.
- **Direct prediction** (8.4.1.2): both `direct_spatial_mv_pred_flag` values —
  spatial direct (8.4.1.2.2) and temporal direct (8.4.1.2.3) — the co-located
  picture and block derivation (8.4.1.2.1), and `direct_8x8_inference_flag`.
- Bi-prediction sample averaging (8.4.2.3.1).
- Storage and use of B pictures as references (`nal_ref_idc` non-zero on a B
  slice).

## Weighted prediction

**Clause 8.4.2.3.2**, both modes:

- **Explicit**, selected by `weighted_pred_flag` for P and by
  `weighted_bipred_idc` equal to 1 for B: the `pred_weight_table` syntax
  (7.3.3.2) with `luma_log2_weight_denom`, `chroma_log2_weight_denom`, and the
  per-entry weights and offsets.
- **Implicit**, selected by `weighted_bipred_idc` equal to 2: weights derived
  from the picture order counts of the two references.

## Interlaced coding

- **Field pictures (PAFF)**: `frame_mbs_only_flag` equal to 0 with
  `field_pic_flag` equal to 1, `bottom_field_flag`, and the decoding of a
  coded field as a picture in its own right.
- **Complementary field pairs**: two fields forming one frame for output, and
  the reference-picture handling that goes with it — field and frame reference
  lists (8.2.4.2.2, 8.2.4.2.4, 8.2.4.2.5), `PicNum` and `FrameNumWrap`
  derivation for fields (8.2.4.1), and field-pair marking in 8.2.5.
- **Macroblock-adaptive frame/field coding (MBAFF)**:
  `mb_adaptive_frame_field_flag` equal to 1 with `mb_field_decoding_flag` per
  macroblock pair, the MBAFF neighbour derivation (6.4.10), and MBAFF
  deblocking (8.7.1, including filtering a frame macroblock against a field
  macroblock).
- **Picture order count for fields** (8.2.1): `TopFieldOrderCnt` and
  `BottomFieldOrderCnt`, and `delta_pic_order_cnt_bottom`.

## Stream-level NAL units

Bitstreams at this milestone carry NAL unit types the earlier profile's streams
do not: **9** (access unit delimiter, 7.3.2.4), **10** (end of sequence) and
**11** (end of stream). They carry no decoded samples, but a decoder that
mis-parses one loses synchronisation with the byte stream, so they must be
recognised and skipped per clause 7.4.1.

## Output

Output order still follows the bumping process of **Annex C**, and a
complementary field pair is written as the single interleaved frame it forms,
as `/app/TEST_INTERFACE.md` specifies. `chroma_format_idc` is 1 and both bit
depths are 8 throughout this profile (A.2.2).
