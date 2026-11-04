# Milestone 5 — the MVC amendment, and decoding layered streams

Normative reference: **ITU-T Rec. H.264 / ISO-IEC 14496-10**. Multiview Video
Coding was added by the 2009 amendment and is carried in **Annex H**; Scalable

> **Edition.** Clause and annex numbers in this document follow **Rec. ITU-T H.264 (04/2017)**, in which MVC is Annex H. Later editions renumber: in 08/2024 (V15) the IPR annex was removed and every subsequent annex shifted one letter, so MVC is Annex G there and Annex H is MVC-plus-depth. Read the clause titles, not only the numbers.
Video Coding was added by the 2007 amendment and is carried in **Annex G**. The
profiles are defined in **H.10.1.1 (Multiview High, `profile_idc` 118)** and
**H.10.1.2 (Stereo High, `profile_idc` 128)**.

Everything in `/app/SPEC/01-baseline.md`, `/app/SPEC/02-main.md`,
`/app/SPEC/03-extended.md` and `/app/SPEC/04-frext.md` must keep working.

## The layered NAL unit types

Three NAL unit types that earlier profiles never carry (Table 7-1):

- **type 14, prefix NAL unit** (7.3.2.12): carries
  `nal_unit_header_mvc_extension` for the base-view slice that follows it.
- **type 15, subset sequence parameter set** (7.3.2.1.3): an SPS carrying
  `seq_parameter_set_mvc_extension` (H.7.3.2.1.4).
- **type 20, coded slice extension** (7.3.2.13): a non-base-view slice, preceded
  by `nal_unit_header_mvc_extension` with `non_idr_flag`, `priority_id`,
  `view_id`, `temporal_id`, `anchor_pic_flag` and `inter_view_flag`.

A decoder must read `nal_unit_header_mvc_extension` (H.7.3.1.1) to know which
view a slice belongs to.

## Multiview decoding

- **The MVC SPS extension** (H.7.3.2.1.4): `num_views_minus1`, the `view_id`
  list, and the anchor and non-anchor inter-view reference lists
  (`num_anchor_refs_l0/l1`, `anchor_ref_l0/l1`, `num_non_anchor_refs_l0/l1`,
  `non_anchor_ref_l0/l1`), plus the level/operation-point fields.
- **Inter-view prediction** (H.8.4): an inter-view reference is a decoded view
  component of the same access unit, entered into the reference picture lists
  by the initialisation process of H.8.2.1, after the temporal references and in
  the order the SPS extension gives. `inter_view_flag` says whether a view
  component may be used that way.
- **View order index** and the `VOIdx` derivation, and the distinction between
  anchor and non-anchor access units.
- **Reference list modification for MVC** (H.8.2.2), including
  `abs_diff_view_idx_minus1`.
- All views are decoded and all are output.

## Decoding a layered stream as its base layer

A multiview bitstream remains decodable by a decoder that implements only the
base view, because the layered data lives in NAL unit types the base
specification tells a decoder to ignore: **clause 7.4.1 requires a decoder to
discard NAL units with `nal_unit_type` values reserved or unspecified for it**,
and the prefix NAL unit (**14**), the subset sequence parameter set (**15**) and
the coded slice extension (**20**) are exactly that for a decoder that
implements neither Annex H nor Annex G.

So a layered bitstream must still decode to its **base layer** output: types 14,
15 and 20 are skipped, the type 1 and 5 slices are decoded normally, and the
result is the base-view video. For some of the bitstreams this milestone
targets, only that base-view output must match.

## Output

`/app/TEST_INTERFACE.md` defines the output layout. Which FILES that layout is
written to depends on the bitstream:

- A bitstream that **declares** more than one view — one carrying a subset
  sequence parameter set (NAL unit type 15) — is decoded in multiview mode,
  and each view it outputs goes to its own file. The trigger is what the
  bitstream declares, not how many views your decoder chooses to output: a
  decoder that emits only the base view of such a stream still writes the
  per-view filename, not the plain `-o` name.
- A bitstream with no subset SPS is single-view and writes the plain `-o`
  file, exactly as at every earlier milestone.

The per-view name is the `-o` argument with `_ViewId%04d` inserted before the
extension, where the number is that view's **actual `view_id`** from the
`nal_unit_header_mvc_extension` — it is an identifier, not a sequential
index, and it is common for the second view to be `view_id = 2`:

```
-o out.yuv   ->   out_ViewId0000.yuv   (view_id 0, the base view)
                  out_ViewId0002.yuv   (view_id 2, NOT 0001)
```

Each file holds that view's decoded pictures in output order, in exactly the
layout `/app/TEST_INTERFACE.md` specifies.
