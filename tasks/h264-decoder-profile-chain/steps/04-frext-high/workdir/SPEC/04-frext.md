# Milestone 4 — the FRExt amendment: the High profiles

Normative reference: **ITU-T Rec. H.264 / ISO-IEC 14496-10**, the fidelity range
extensions first published as the 2004/2005 amendment and carried in every
edition since. Clause numbers below are that document's. The profiles are
defined in **Annex A.2.4 (High)**, **A.2.5 (High 10)**, **A.2.6 (High 4:2:2)**,
**A.2.7 (High 4:4:4 Predictive)** and **A.2.11 (CAVLC 4:4:4 Intra)**.

This milestone adds `profile_idc` 100, 110, 122, 244 and 44. Everything in
`/app/SPEC/01-baseline.md`, `/app/SPEC/02-main.md` and
`/app/SPEC/03-extended.md` must keep working.

## Sequence parameter set extensions

The High profiles extend the SPS (7.3.2.1) with fields the earlier profiles do
not carry, and a decoder must parse them before anything else works:
`chroma_format_idc`, `separate_colour_plane_flag`, `bit_depth_luma_minus8`,
`bit_depth_chroma_minus8`, `qpprime_y_zero_transform_bypass_flag`, and
`seq_scaling_matrix_present_flag` with its scaling lists.

## The 8x8 transform and Intra_8x8

- `transform_8x8_mode_flag` in the picture parameter set (7.3.2.2) and
  `transform_size_8x8_flag` per macroblock (7.3.5).
- **Intra_8x8 prediction** (8.3.2): all nine prediction modes, and the reference
  sample filtering of 8.3.2.2.1 that precedes them.
- The **8x8 residual transform** and its dequantisation (8.5.13), and the
  interaction of `transform_size_8x8_flag` with `coded_block_pattern` and with
  the CAVLC/CABAC residual block syntax.
- `direct_8x8_inference_flag` interaction with B_8x8 when the 8x8 transform is
  in use.

## Scaling matrices

**Clause 8.5.9** and Tables 7-3 and 7-4:

- `seq_scaling_matrix_present_flag` / `seq_scaling_list_present_flag` in the SPS
  and `pic_scaling_matrix_present_flag` / `pic_scaling_list_present_flag` in the
  PPS, with the `scaling_list` syntax of 7.3.2.1.1.1 including `UseDefaultScalingMatrixFlag`.
- The **fall-back rules A and B** of Table 7-2, which decide what a list that is
  not present inherits, and the default lists `Default_4x4_Intra`,
  `Default_4x4_Inter`, `Default_8x8_Intra` and `Default_8x8_Inter` (Tables 7-3,
  7-4).
- `LevelScale` derivation from the selected lists for both transform sizes.

## Chroma formats and bit depths

- `chroma_format_idc` **0 (monochrome)**, **1 (4:2:0)**, **2 (4:2:2)** and
  **3 (4:4:4)**, including the chroma DC transform for 4:2:2 (the 2x4 transform
  of 8.5.11.1) and the 4:4:4 chroma path, where chroma blocks use the luma
  prediction and transform processes.
- **Bit depths above 8** for luma and chroma independently
  (`bit_depth_luma_minus8`, `bit_depth_chroma_minus8`), with the corresponding
  `QpBdOffset` adjustments and the widened clipping range.
- `separate_colour_plane_flag` equal to 1 (4:4:4 independent mode): the three
  colour planes are coded as separate pictures distinguished by
  `colour_plane_id`.
- The **second chroma QP offset**, `second_chroma_qp_index_offset` in the PPS,
  so Cb and Cr may use different QPs (Table 8-15 applied per component).

## Lossless coding

`qpprime_y_zero_transform_bypass_flag` equal to 1: when `QP'y` is 0, the
transform and quantisation are bypassed (8.5.15), including the special intra
prediction residual handling for vertical and horizontal prediction modes.

## Intra-only profiles

`constraint_set3_flag` selects the Intra variants of High 10, High 4:2:2 and
High 4:4:4, and `profile_idc` 44 is **CAVLC 4:4:4 Intra**, which is intra-only
and CAVLC-only. In an intra-only bitstream every picture is an IDR picture.

## Output

`/app/TEST_INTERFACE.md` already specifies how these are written: plane sizes
follow `chroma_format_idc`, monochrome is written as 4:2:0 with both chroma
planes at `1 << (BitDepthC − 1)`, and a bit depth above 8 makes every sample two
bytes, little-endian.
