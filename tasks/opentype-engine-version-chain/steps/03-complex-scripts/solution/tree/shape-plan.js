/* shape-plan.js -- the default shaping model: script/language detection and
 * their OpenType tag mapping, direction resolution, the default feature set
 * and its order, fallback positioning when the font has no GPOS, and the
 * handling of default-ignorable characters.  Also the "font funcs" (cmap,
 * hmtx advances, post glyph names) because sfnt.js does not read cmap/post.
 *
 * Owner: S3.  See NOTES-S3.md.
 */
import {
  DIRECTION_INVALID, DIRECTION_LTR, DIRECTION_RTL, DIRECTION_TTB, DIRECTION_BTT,
  isHorizontal, isVertical, isForward, isBackward, reverseDirection,
  satAdd, satSub, satNeg, clampToI32, makePos,
  CLUSTER_LEVEL_MONOTONE_CHARACTERS,
  GLYPH_FLAG_UNSAFE_TO_BREAK, GLYPH_FLAG_UNSAFE_TO_CONCAT,
  GLYPH_FLAG_SAFE_TO_INSERT_TATWEEL, GLYPH_FLAG_DEFINED,
  BUFFER_FLAG_PRESERVE_DEFAULT_IGNORABLES, BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES,
  BUFFER_FLAG_PRODUCE_UNSAFE_TO_CONCAT, BUFFER_FLAG_PRODUCE_SAFE_TO_INSERT_TATWEEL,
  SCRATCH_HAS_DEFAULT_IGNORABLES, SCRATCH_HAS_SPACE_FALLBACK, SCRATCH_HAS_CGJ,
  SCRATCH_HAS_CONTINUATIONS, SCRATCH_HAS_FRACTION_SLASH,
  SCRATCH_HAS_VARIATION_SELECTOR_FALLBACK, SCRATCH_HAS_GPOS_ATTACHMENT,
} from './buffer.js';

/* Generated from ucd/UnicodeData.txt, Scripts.txt, PropertyValueAliases.txt (Unicode 17.0.0).
 * Run-length tables: each entry is '<delta-start base36>.<value base36>'. */
const UD_GC = '0.0 w.t 1.l 3.n 1.l 3.m 1.i 1.l 1.p 1.l 1.h 1.l 2.d a.l 2.p 3.l 2.9 q.m 1.l 1.i 1.o 1.g 1.o 1.5 q.m 1.p 1.i 1.p 1.0 x.t 1.l 1.n 4.q 1.l 1.o 1.q 1.7 1.k 1.p 1.1 1.q 1.o 1.q 1.p 1.f 2.o 1.5 1.l 2.o 1.f 1.7 1.j 1.f 3.l 1.9 n.p 1.9 7.5 o.p 1.5 8.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 2.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 2.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 2.5 1.9 1.5 1.9 1.5 3.9 2.5 1.9 1.5 1.9 2.5 1.9 3.5 2.9 4.5 1.9 2.5 1.9 3.5 3.9 2.5 1.9 2.5 1.9 1.5 1.9 1.5 1.9 2.5 1.9 1.5 2.9 1.5 1.9 2.5 1.9 3.5 1.9 1.5 1.9 2.5 2.7 1.9 1.5 3.7 4.9 1.8 1.5 1.9 1.8 1.5 1.9 1.8 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 2.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 2.9 1.8 1.5 1.9 1.5 1.9 3.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 7.9 2.5 1.9 2.5 2.9 1.5 1.9 4.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1x.7 2.5 q.6 i.o 4.6 c.o e.6 5.o 7.6 1.o 1.6 1.o h.c 34.9 1.5 1.9 1.5 1.6 1.o 1.9 1.5 1.2 2.6 1.5 3.l 1.9 1.2 4.o 2.9 1.l 1.9 3.2 1.9 1.2 1.9 2.5 1.9 h.2 1.9 9.5 z.9 1.5 2.9 3.5 3.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 5.9 1.5 1.p 1.9 1.5 1.9 2.5 2.9 1f.5 1c.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.q 1.c 5.b 2.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 2.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 2.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.2 1.9 12.2 2.6 1.l 6.5 15.l 1.h 1.2 2.q 2.n 1.2 1.c 19.h 1.c 1.l 1.c 2.l 1.c 2.l 1.c 1.2 8.7 r.2 4.7 4.l 2.2 b.1 6.p 3.l 2.n 1.l 2.q 2.c b.l 1.1 1.l 3.7 w.6 1.7 a.c l.d a.l 4.7 2.c 1.7 2r.l 1.7 1.c 7.1 1.q 1.c 6.6 2.c 2.q 1.c 4.7 2.d a.7 3.q 2.7 1.l e.2 1.1 1.7 1.c 1.7 u.c r.2 2.7 2h.c b.7 1.2 e.d a.7 x.c 9.6 2.q 1.l 3.6 1.2 2.c 1.n 2.7 m.c 4.6 1.c 9.6 1.c 3.6 1.c 5.2 2.l f.2 1.7 p.c 3.2 2.l 1.2 1.7 b.2 5.7 o.o 1.7 7.1 2.2 5.c 9.7 15.6 1.c o.1 1.c w.a 1.7 1i.c 1.a 1.c 1.7 1.a 3.c 8.a 4.c 1.a 2.7 1.c 7.7 a.c 2.l 2.d a.l 1.6 1.7 f.c 1.a 2.2 1.7 8.2 2.7 2.2 2.7 m.2 1.7 7.2 1.7 1.2 3.7 4.2 2.c 1.7 1.a 3.c 4.2 2.a 2.2 2.a 2.c 1.7 1.2 8.a 1.2 4.7 2.2 1.7 3.c 2.2 2.d a.7 2.n 2.f 6.q 1.n 1.7 1.l 1.c 1.2 2.c 2.a 1.2 1.7 6.2 4.7 2.2 2.7 m.2 1.7 7.2 1.7 2.2 1.7 2.2 1.7 2.2 2.c 1.2 1.a 3.c 2.2 4.c 2.2 2.c 3.2 3.c 1.2 7.7 4.2 1.7 1.2 7.d a.c 2.7 3.c 1.l 1.2 a.c 2.a 1.2 1.7 9.2 1.7 3.2 1.7 m.2 1.7 7.2 1.7 2.2 1.7 5.2 2.c 1.7 1.a 3.c 5.2 1.c 2.a 1.2 1.a 2.c 1.2 2.7 1.2 f.7 2.c 2.2 2.d a.l 1.n 1.2 7.7 1.c 6.2 1.c 1.a 2.2 1.7 8.2 2.7 2.2 2.7 m.2 1.7 7.2 1.7 2.2 1.7 5.2 2.c 1.7 1.a 1.c 1.a 1.c 4.2 2.a 2.2 2.a 2.c 1.2 7.c 2.a 1.2 4.7 2.2 1.7 3.c 2.2 2.d a.q 1.7 1.f 6.2 a.c 1.7 1.2 1.7 6.2 3.7 3.2 1.7 4.2 3.7 2.2 1.7 1.2 1.7 2.2 3.7 2.2 3.7 3.2 3.7 c.2 4.a 2.c 1.a 2.2 3.a 3.2 1.a 3.c 1.2 2.7 1.2 6.a 1.2 e.d a.f 3.q 6.n 1.q 1.2 5.c 1.a 3.c 1.7 8.2 1.7 3.2 1.7 n.2 1.7 g.2 2.c 1.7 1.c 3.a 4.2 1.c 3.2 1.c 4.2 7.c 2.2 1.7 3.2 1.7 2.2 2.7 2.c 2.2 2.d a.2 7.l 1.f 7.q 1.7 1.c 1.a 2.l 1.7 8.2 1.7 3.2 1.7 n.2 1.7 a.2 1.7 5.2 2.c 1.7 1.a 1.c 1.a 5.2 1.c 1.a 2.2 1.a 2.c 2.2 7.a 2.2 5.7 3.2 1.7 2.c 2.2 2.d a.2 1.7 2.a 1.2 c.c 2.a 2.7 9.2 1.7 3.2 1.7 15.c 2.7 1.a 3.c 4.2 1.a 3.2 1.a 3.c 1.7 1.q 1.2 4.7 3.a 1.f 7.7 3.c 2.2 2.d a.f 9.q 1.7 6.2 1.c 1.a 2.2 1.7 i.2 3.7 o.2 1.7 9.2 1.7 1.2 2.7 7.2 3.c 1.2 4.a 3.c 3.2 1.c 1.2 1.a 8.2 6.d a.2 2.a 2.l 1.2 c.7 1c.c 1.7 2.c 7.2 4.n 1.7 6.6 1.c 8.l 1.d a.l 2.2 11.7 2.2 1.7 1.2 1.7 5.2 1.7 o.2 1.7 1.2 1.7 a.c 1.7 2.c 9.7 1.2 2.7 5.2 1.6 1.2 1.c 7.2 1.d a.2 2.7 4.2 w.7 1.q 3.l f.q 1.l 1.q 3.c 2.q 6.d a.f a.q 1.c 1.q 1.c 1.q 1.c 1.m 1.i 1.m 1.i 1.a 2.7 8.2 1.7 10.2 4.c e.a 1.c 5.l 1.c 2.7 5.c b.2 1.c 10.2 1.q 8.c 1.q 6.2 1.q 2.l 5.q 4.l 2.2 11.7 17.a 2.c 4.a 1.c 6.a 1.c 2.a 2.c 2.7 1.d a.l 6.7 6.a 2.c 2.7 4.c 3.7 1.a 3.7 2.a 7.7 3.c 4.7 d.c 1.a 2.c 2.a 6.c 1.7 1.a 1.d a.a 3.c 1.q 2.9 12.2 1.9 1.2 5.9 1.2 2.5 17.l 1.6 1.5 3.7 95.2 1.7 4.2 2.7 7.2 1.7 1.2 1.7 4.2 2.7 15.2 1.7 4.2 2.7 x.2 1.7 4.2 2.7 7.2 1.7 1.2 1.7 4.2 2.7 f.2 1.7 1l.2 1.7 4.2 2.7 1v.2 2.c 3.l 9.f k.2 3.7 g.q a.2 6.9 2e.2 2.5 6.2 2.h 1.7 h8.q 1.l 1.7 h.t 1.7 q.m 1.i 1.2 3.7 23.l 3.e 3.7 8.2 7.7 i.c 3.a 1.2 9.7 j.c 2.a 1.l 2.2 9.7 i.c 2.2 c.7 d.2 1.7 3.2 1.c 2.2 c.7 1g.c 2.a 1.c 7.a 8.c 1.a 2.c b.l 3.6 1.l 3.n 1.7 1.c 1.2 2.d a.2 6.f a.2 6.l 6.h 1.l 4.c 3.1 1.c 1.d a.2 6.7 z.6 1.7 1h.2 7.7 5.c 2.7 y.c 1.7 1.2 5.7 1y.2 a.7 v.2 1.c 3.a 4.c 2.a 3.2 4.a 2.c 1.a 6.c 3.2 4.q 1.2 3.l 2.d a.7 u.2 2.7 5.2 b.7 18.2 4.7 q.2 6.d a.f 1.2 3.q y.7 n.c 2.a 2.c 1.2 2.l 2.7 1h.a 1.c 1.a 1.c 7.2 1.c 1.a 1.c 1.a 2.c 8.a 6.c a.2 2.c 1.d a.2 6.d a.2 6.l 7.6 1.l 6.2 2.c e.b 1.c v.2 2.c c.2 k.c 4.a 1.7 1b.c 1.a 1.c 5.a 1.c 1.a 5.c 1.a 2.7 8.2 1.l 2.d a.l 7.q a.c 9.q 9.l 3.c 2.a 1.7 u.a 1.c 4.a 2.c 2.a 1.c 3.7 2.d a.7 18.c 1.a 1.c 2.a 3.c 1.a 1.c 3.a 2.2 8.l 4.7 10.a 8.c 8.a 2.c 2.2 3.l 5.d a.2 3.7 3.d a.7 u.6 6.l 2.5 9.9 1.5 1.2 5.9 17.2 2.9 3.l 8.2 8.c 3.l 1.c d.a 1.c 7.7 4.c 1.7 6.c 1.7 2.a 1.c 2.7 1.2 5.5 18.6 1r.5 d.6 1.5 y.6 11.c 1s.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 9.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 9.9 8.5 6.2 2.9 6.2 2.5 8.9 8.5 8.9 8.5 6.2 2.9 6.2 2.5 8.2 1.9 1.2 1.9 1.2 1.9 1.2 1.9 1.5 8.9 8.5 e.2 2.5 8.8 8.5 8.8 8.5 8.8 8.5 5.2 1.5 2.9 4.8 1.o 1.5 1.o 3.5 3.2 1.5 2.9 4.8 1.o 3.5 4.2 2.5 2.9 4.2 1.o 3.5 8.9 5.o 3.2 2.5 3.2 1.5 2.9 4.8 1.o 2.2 1.t b.1 5.h 6.l 2.k 1.j 1.m 1.k 2.j 1.m 1.k 1.l 8.r 1.s 1.1 5.t 1.l 9.k 1.j 1.l 4.g 2.l 3.p 1.m 1.i 1.l b.p 1.l 1.g 1.l a.t 1.1 5.2 1.1 a.f 1.6 1.2 2.f 6.p 3.m 1.i 1.6 1.f a.p 3.m 1.i 1.2 1.6 d.2 3.n y.2 e.c d.b 4.c 1.b 3.c c.2 f.q 2.9 1.q 4.9 1.q 2.5 1.9 3.5 2.9 3.5 1.q 1.9 1.q 2.p 1.9 5.q 6.9 1.q 1.9 1.q 1.9 1.q 1.9 4.q 1.5 1.9 4.5 1.7 4.5 1.q 2.5 2.9 2.p 5.9 1.5 4.q 1.p 1.q 2.5 1.q 1.f g.e z.9 1.5 1.e 4.f 1.q 2.2 4.p 5.q 5.p 2.q 4.p 1.q 2.p 1.q 2.p 1.q 7.p 1.q v.p 2.q 2.p 1.q 1.p 1.q v.p 7g.q 8.m 1.i 1.m 1.i 1.q k.p 2.q 7.m 1.i 1.q 29.p 1.q u.p p.q 14.p 6.q 20.2 m.q b.2 l.f 1o.q 26.f m.q 53.p 1.q 9.p 1.q 1i.p 8.q 33.p 1.q 6w.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.f u.q 18.p 5.m 1.i 1.p v.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.p g.q 74.p 3n.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.p 1r.m 1.i 1.m 1.i 1.p w.m 1.i 1.p 76.q 1c.p l.q 2.p 6.q 13.2 2.q 3u.9 1c.5 1c.9 1.5 1.9 3.5 2.9 1.5 1.9 1.5 1.9 1.5 1.9 4.5 1.9 1.5 2.9 1.5 6.6 2.9 3.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 2.q 6.9 1.5 1.9 1.5 1.c 3.9 1.5 1.2 5.l 4.f 1.l 2.5 12.2 1.5 1.2 5.5 1.2 2.7 1k.2 7.6 1.l 1.2 e.c 1.7 n.2 9.7 7.2 1.7 7.2 1.7 7.2 1.7 7.2 1.7 7.2 1.7 7.2 1.7 7.2 1.7 7.2 1.c w.l 2.k 1.j 1.k 1.j 1.l 3.k 1.j 1.l 1.k 1.j 1.l 9.h 1.l 2.h 1.l 1.k 1.j 1.l 2.k 1.j 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.l 5.6 1.l a.h 2.l 4.h 1.l 1.m 1.l d.q 2.l 3.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.h 1.2 y.q q.2 1.q 2h.2 c.q 5y.2 q.q g.t 1.l 3.q 1.6 1.7 1.e 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.q 2.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.h 1.m 1.i 2.q 1.e 9.c 4.a 2.h 1.6 5.q 2.e 3.6 1.7 1.l 1.q 2.2 1.7 2e.2 2.c 2.o 2.6 2.7 1.h 1.7 2i.l 1.6 3.7 1.2 5.7 17.2 1.7 2m.2 1.q 2.f 4.q a.7 w.q 12.2 9.q 1.7 g.q v.2 1.f a.q u.f 8.q 1.f f.q w.f a.q 13.f f.q 8w.7 534.q 1s.7 g7p.6 1.7 vr.2 3.q 1j.2 9.7 14.6 6.l 2.7 7g.6 1.l 3.7 g.d a.7 2.2 k.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.7 1.c 1.b 3.l 1.c a.l 1.6 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.6 2.c 2.7 1y.e a.c 2.l 6.2 8.o n.6 9.o 2.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 3.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.6 1.5 8.9 1.5 1.9 1.5 1.9 2.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.6 1.o 2.9 1.5 1.9 1.5 1.7 1.9 1.5 1.9 1.5 3.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 5.5 1.9 5.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 4.5 1.9 1.5 1.9 2.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.5 1.9 1.2 k.6 4.9 1.5 1.7 1.6 2.5 1.7 7.c 1.7 3.c 1.7 4.c 1.7 n.a 2.c 2.a 1.q 4.c 1.2 3.f 6.q 2.n 1.q 1.2 6.7 1g.l 4.2 8.a 2.7 1e.a g.c 2.2 8.l 2.d a.2 6.c i.7 6.l 3.7 1.l 1.7 2.c 1.d a.7 s.c 8.l 2.7 n.c b.a 2.2 b.l 1.7 t.2 3.c 3.a 1.7 1b.c 1.a 2.c 4.a 2.c 2.a 3.l d.2 1.6 1.d a.2 4.l 2.7 5.c 1.6 1.7 9.d a.7 5.2 1.7 15.c 6.a 2.c 2.a 2.c 2.2 9.7 3.c 1.7 8.c 1.a 1.2 2.d a.2 2.l 4.7 g.6 1.7 6.q 3.7 1.a 1.c 1.a 1.7 1e.c 1.7 1.c 3.7 2.c 2.7 5.c 2.7 1.c 1.7 1.2 o.7 2.6 1.l 2.7 b.a 1.c 2.a 2.l 2.7 1.6 2.a 1.c 1.2 a.7 6.2 2.7 6.2 2.7 6.2 9.7 7.2 1.7 7.2 1.5 17.o 1.6 4.5 9.6 1.o 2.2 4.5 28.7 z.a 2.c 1.a 2.c 1.a 2.l 1.a 1.c 1.2 2.d a.2 6.7 8mc.2 c.7 n.2 4.7 1d.2 4.4 1kw.3 4xs.7 a6.2 2.7 2y.2 12.5 7.2 c.5 5.2 5.7 1.c 1.7 a.p 1.7 d.2 1.7 5.2 1.7 1.2 1.7 2.2 1.7 2.2 1.7 30.o h.q g.7 a3.i 1.m 1.q g.7 1s.q 2.7 1i.q 8.2 w.7 c.n 1.q 3.c g.l 7.m 1.i 1.l 1.2 6.c g.l 1.h 2.g 2.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.m 1.i 1.l 2.m 1.i 1.l 4.g 3.l 3.2 1.l 4.h 1.m 1.i 1.m 1.i 1.m 1.i 1.l 3.p 1.h 1.p 3.2 1.l 1.n 1.l 2.2 4.7 5.2 1.7 3r.2 2.1 1.2 1.l 3.n 1.l 3.m 1.i 1.l 1.p 1.l 1.h 1.l 2.d a.l 2.p 3.l 2.9 q.m 1.l 1.i 1.o 1.g 1.o 1.5 q.m 1.p 1.i 1.p 1.m 1.i 1.l 1.m 1.i 1.l 2.7 a.6 1.7 19.6 2.7 v.2 3.7 6.2 2.7 6.2 2.7 6.2 2.7 3.2 3.n 2.p 1.o 1.q 1.n 2.2 1.q 1.p 4.q 2.2 a.1 3.q 2.2 2.7 c.2 1.7 q.2 1.7 j.2 1.7 2.2 1.7 f.2 2.7 e.2 y.7 3f.2 5.l 3.2 4.f 19.2 3.q 9.e 1h.f 4.q h.f 2.q 3.2 1.q d.2 3.q 1.2 1b.q 19.c 1.2 3m.7 t.2 3.7 1d.2 f.c 1.f r.2 4.7 w.f 4.2 9.7 k.e 1.7 8.e 1.2 5.7 12.c 5.2 5.7 u.2 1.l 1.7 10.2 4.7 8.l 1.e 5.2 16.9 14.5 14.7 26.2 2.d a.2 6.9 10.2 4.5 10.2 4.7 14.2 8.7 1g.2 b.l 1.9 b.2 1.9 f.2 1.9 7.2 1.9 2.2 1.5 b.2 1.5 f.2 1.5 7.2 1.5 2.2 3.7 1g.2 c.7 8n.2 9.7 m.2 a.7 8.2 o.6 6.2 1.6 16.2 1.6 9.2 1x.7 6.2 2.7 1.2 1.7 18.2 1.7 2.2 3.7 1.2 2.7 n.2 1.l 1.f 8.7 n.q 2.f 7.7 v.2 8.f 9.2 1c.7 j.2 1.7 2.2 5.f 5.7 m.f 6.2 3.l 1.7 q.2 5.l 1.7 q.2 12.7 1k.2 4.f 2.7 2.f g.2 2.f 1a.7 1.c 3.2 1.c 2.2 5.c 4.7 4.2 1.7 3.2 1.7 t.2 2.c 3.2 4.c 1.f 9.2 7.l 9.2 7.7 t.f 2.l 1.7 t.f 3.2 w.7 8.q 1.7 s.c 2.2 4.f 5.l 7.2 9.7 1i.2 3.l 7.7 m.2 2.f 8.7 j.2 5.f 8.7 i.2 7.l 4.2 c.f 7.2 28.7 21.2 1j.9 1f.2 d.5 1f.2 7.f 6.7 10.c 4.2 8.d a.2 6.d a.7 4.6 1.7 1.9 m.2 3.c 5.h 1.6 1.5 m.2 8.p 2.2 5s.f v.2 1.7 16.2 1.c 2.h 1.2 2.7 2.2 g.7 3.6 1.7 2.2 8.l 1.q 8.2 x.c 6.7 t.f a.7 1.2 8.7 m.c b.f 4.l 5.2 m.7 i.c 4.l 4.2 12.7 l.f 7.2 k.7 n.2 9.a 1.c 1.a 1.7 1h.c f.l 7.2 4.f k.d a.c 1.7 2.c 2.7 1.2 9.c 3.a 1.7 19.a 3.c 4.a 2.c 2.l 2.1 1.l 4.c 1.2 a.1 1.2 2.7 p.2 7.d a.2 6.c 3.7 10.c 5.a 1.c 8.2 1.d a.l 4.7 1.a 2.7 1.2 8.7 z.c 1.l 2.7 1.2 9.c 2.a 1.7 1c.a 3.c 9.a 2.7 4.l 4.c 4.l 1.a 1.c 1.d a.7 1.l 1.7 1.l 3.2 1.f k.2 b.7 i.2 1.7 p.a 3.c 3.a 2.c 1.a 1.c 2.l 6.c 1.7 2.c 1.2 1q.7 7.2 1.7 1.2 1.7 4.2 1.7 f.2 1.7 a.l 1.2 6.7 1b.c 1.a 3.c 8.2 5.d a.2 6.c 2.a 2.2 1.7 8.2 2.7 2.2 2.7 m.2 1.7 7.2 1.7 2.2 1.7 5.2 1.c 2.7 1.a 2.c 1.a 4.2 2.a 2.2 2.a 3.2 2.7 1.2 6.a 1.2 5.7 5.a 2.2 2.c 7.2 3.c 5.2 b.7 a.2 1.7 1.2 2.7 1.2 1.7 12.2 1.7 1.a 3.c 6.2 1.a 1.2 2.a 1.2 1.a 4.2 1.a 2.c 1.a 1.c 1.7 1.c 1.7 1.l 2.2 1.l 2.2 8.c 2.2 t.7 1h.a 3.c 8.a 2.c 3.a 1.c 1.7 4.l 5.d a.l 2.2 1.l 1.c 1.7 3.2 u.7 1c.a 3.c 6.a 1.c 1.a 4.c 2.a 1.c 2.7 2.l 1.7 1.2 8.d a.2 4m.7 1b.a 3.c 4.2 2.a 4.c 2.a 1.c 2.l n.7 4.c 2.2 y.7 1c.a 3.c 8.a 2.c 1.a 1.c 2.l 3.7 1.2 b.d a.2 6.l d.2 j.7 17.c 1.a 1.c 1.a 2.c 6.a 1.c 1.7 1.l 1.2 6.d a.2 6.d k.2 s.7 r.2 2.c 1.a 1.c 1.a 2.c 4.a 1.c 5.2 4.d a.f 2.l 3.q 1.7 7.2 55.7 18.a 3.c 9.a 1.c 2.l 1.2 2s.9 w.5 w.d a.f 9.2 c.7 8.2 2.7 1.2 2.7 8.2 1.7 2.2 1.7 o.a 6.2 1.a 2.2 2.c 2.a 1.c 1.7 1.a 1.7 1.a 1.c 1.l 3.2 9.d a.2 1y.7 8.2 2.7 13.a 3.c 4.2 2.c 2.a 4.c 1.7 1.l 1.7 1.a 1.2 r.7 1.c a.7 14.c 6.a 1.7 1.c 4.l 8.c 1.2 8.7 1.c 6.a 2.c 3.7 1a.c d.a 1.c 2.l 3.7 1.l 5.2 d.7 21.2 7.l a.2 2e.c 1.a 1.c 3.a 1.c 1.a 1.2 2g.7 x.l 1.2 e.d a.2 6.7 9.2 1.7 11.a 1.c 7.2 1.c 6.a 1.c 1.7 1.l 5.2 a.d a.f j.2 3.l 2.7 u.2 2.c m.2 1.a 1.c 7.a 1.c 2.a 1.c 2.2 21.7 7.2 1.7 2.2 1.7 12.c 6.2 3.c 1.2 1.c 2.2 1.c 7.7 1.c 1.2 8.d a.2 6.7 6.2 1.7 2.2 1.7 w.a 5.2 1.c 2.2 1.a 2.c 1.a 1.c 1.7 1.2 7.d a.2 6.7 15.6 1.7 2.2 4.d a.2 6u.7 j.c 2.a 2.l 2.2 7.c 2.7 1.a 1.7 d.2 1.7 y.a 2.c 5.2 3.a 2.c 1.a 1.c 1.l d.d a.c 1.2 2d.7 1.2 f.f l.q 8.n 4.q h.2 d.l 1.7 pm.2 2u.e 33.2 1.l 5.2 b.7 5g.2 218.7 2p.l 2.2 d.7 ts.1 g.c 1.7 6.c f.2 a.7 32z.2 5.7 g7.2 5a1.7 u.c c.a 3.c 3.d a.2 1c6.7 ft.2 7.7 v.2 1.d a.2 4.l 2.7 27.2 1.d a.2 6.7 u.2 2.c 5.l 1.2 a.7 1c.c 7.l 5.q 4.6 4.l 1.q 1.2 a.d a.2 1.f 7.2 1.7 l.2 5.7 j.2 c0.6 3.7 14.6 2.l 3.d a.2 5i.9 w.5 w.f n.l 4.2 5.9 p.2 2.5 p.2 18.7 23.2 4.c 1.7 1.a 1j.2 7.c 4.6 d.2 1s.6 2.l 1.6 1.c 1.2 b.a 2.6 2.e 3.2 9.7 5p2.2 15.7 w.2 2p.7 37.2 6pp.6 4.2 1.6 7.2 1.6 2.2 1.7 83.2 f.7 1.2 t.7 3.2 2.7 1.2 e.7 4.2 8.7 b0.2 1s4.7 2z.2 5.7 d.2 3.7 9.2 7.7 a.2 2.q 1.c 2.l 1.1 4.2 318.q 6o.d a.q 3.2 3.q c4.2 6.q n.2 f.q g.p 1.2 f.c 1a.2 2.c n.2 9.q 38.2 1o.q 6u.2 a.q 13.2 2.q 1o.a 2.c 3.q 3.a 6.1 8.c 8.q 2.c 7.q u.c 4.q 1p.2 l.q 1u.c 3.q 1.2 3e.f k.2 c.f k.2 c.q 2f.2 9.f p.2 3r.9 q.5 q.9 q.5 7.2 1.5 i.9 q.5 q.9 1.2 1.9 2.2 2.9 1.2 2.9 2.2 2.9 4.2 1.9 8.5 4.2 1.5 1.2 1.5 7.2 1.5 b.9 q.5 q.9 2.2 1.9 4.2 2.9 8.2 1.9 7.2 1.5 q.9 2.2 1.9 4.2 1.9 5.2 1.9 1.2 3.9 7.2 1.5 q.9 q.5 q.9 q.5 q.9 q.5 q.9 q.5 q.9 q.5 q.9 q.5 s.2 2.9 p.p 1.5 p.p 1.5 6.9 p.p 1.5 p.p 1.5 6.9 p.p 1.5 p.p 1.5 6.9 p.p 1.5 p.p 1.5 6.9 p.p 1.5 p.p 1.5 6.9 1.5 1.2 2.d 1e.q e8.c 1j.q 4.c 1e.q 8.c 1.q e.c 1.q 2.l 5.2 f.c 5.2 1.c f.2 uo.5 a.7 1.5 k.2 6.5 6.2 5x.c 7.2 1.c h.2 2.c 7.2 1.c 2.2 1.c 5.2 5.6 1q.2 x.c 1.2 34.7 19.2 3.c 7.6 7.2 2.d a.2 4.7 1.q 1.2 8w.7 u.c 1.2 h.7 18.c 4.d a.2 5.n 1.2 cw.7 r.6 1.c 4.d a.2 5y.7 u.c 2.7 1.d a.2 4.l 1.2 5c.7 v.2 1.7 3.c 1.7 2.c 1.7 7.c 2.7 5.c 1.2 8.7 1.6 1.2 68.7 7.2 1.7 4.2 1.7 2.2 1.7 f.2 1.7 5h.2 2.f 9.c 7.2 15.9 y.5 y.c 7.6 1.2 4.d a.2 4.l 2.2 lt.f 1n.q 1.f 3.n 1.f 4.2 24.f 19.q 1.f f.2 5e.7 4.2 1.7 r.2 1.7 2.2 1.7 1.2 2.7 1.2 1.7 a.2 1.7 4.2 1.7 1.2 1.7 1.2 6.7 1.2 4.7 1.2 1.7 1.2 1.7 1.2 1.7 3.2 1.7 2.2 1.7 1.2 2.7 1.2 1.7 1.2 1.7 1.2 1.7 1.2 1.7 1.2 1.7 2.2 1.7 1.2 2.7 4.2 1.7 7.2 1.7 4.2 1.7 4.2 1.7 1.2 1.7 a.2 1.7 h.2 5.7 3.2 1.7 5.2 1.7 h.2 1g.p 2.2 7i.q 18.2 4.q 2s.2 c.q f.2 2.q f.2 1.q f.2 1.q 11.2 a.f d.q 4h.2 1k.q t.2 d.q 18.2 4.q 9.2 7.q 2.2 e.q 6.2 4a.q 6z.o 5.q k9.2 3.q h.2 3.q d.2 3.q 62.2 6.q c.2 4.q 1.2 f.q c.2 4.q 1k.2 8.q a.2 6.q 14.2 8.q u.2 2.q c.2 4.q 2.2 e.p 9.2 13.q 9k.2 8.q e.2 2.q d.2 3.q b.2 3.q 1l.2 1.q 1.2 4.q g.2 2.q c.2 4.q a.2 7.q 43.2 1.q 2k.d a.q 1.2 sl.7 wyo.2 w.7 3dq.2 2.7 4ge.2 2.7 5rl.2 f.7 ha.2 1wi.7 f2.2 15u.7 3t7.2 5.7 6ju.2 f5vr.1 1.2 u.1 2o.2 3k.c 6o.2 1e6o.3 1eke.2 2.3 1eke.2';
const UD_CCC = '0.0 lc.6e l.6g 1.64 4.6g 1.60 1.64 5.5m 2.64 4.5m 2.64 b.1 5.64 4.6e 8.6o 1.6e 1.64 3.6e 3.64 2.0 1.6e 3.64 4.6e 1.6g 1.64 2.6e 1.6h 1.6i 2.6h 1.6i 2.6h 1.6e d.0 7n.6e 5.0 7d.64 1.6e 4.64 1.6e 3.66 1.64 1.6e 6.64 6.6e 2.64 1.6e 2.66 1.6c 1.6e 1.a 1.b 1.c 1.d 1.e 1.f 1.g 1.h 1.i 1.j 2.k 1.l 1.m 1.0 1.n 1.0 1.o 1.p 1.0 1.6e 1.64 1.0 1.i 1.0 20.6e 8.u 1.v 1.w 1.0 1c.r 1.s 1.t 1.u 1.v 1.w 1.x 1.y 1.6e 2.64 2.6e 5.64 1.6e 2.64 1.0 g.z 1.0 2t.6e 7.0 2.6e 4.64 1.6e 1.0 2.6e 2.0 1.64 1.6e 2.64 1.0 z.10 1.0 u.6e 1.64 1.6e 2.64 1.6e 2.64 3.6e 1.64 2.6e 1.64 1.6e 3.64 1.6e 1.64 1.6e 1.64 1.6e 1.64 1.6e 2.0 4g.6e 7.64 1.6e 1.0 9.64 1.0 o.6e 4.0 1.6e 9.0 1.6e 3.0 1.6e 5.0 17.64 3.0 1n.6e 2.64 3.6e 4.0 16.6e 5.64 5.6e e.0 1.64 1.6e 2.64 1.6e 2.64 1.6e 3.64 3.r 1.s 1.t 1.6e 3.64 1.6e 2.64 2.6e 5.0 1o.7 1.0 g.9 1.0 3.6e 1.64 1.6e 2.0 2v.7 1.0 g.9 1.0 1c.6e 1.0 1p.7 1.0 g.9 1.0 32.7 1.0 g.9 1.0 32.7 1.0 g.9 1.0 3j.9 1.0 32.7 1.0 g.9 1.0 7.2c 1.2j 1.0 2t.7 1.0 g.9 1.0 31.9 2.0 g.9 1.0 3g.9 1.0 31.2v 2.9 1.0 d.2z 4.0 30.3a 2.9 1.0 d.3e 4.0 24.64 2.0 r.64 1.0 1.64 1.0 1.60 1.0 1j.3l 1.3m 1.0 1.3o 1.0 5.3m 4.0 2.3m 1.0 1.6e 2.9 1.0 1.6e 2.0 1q.64 1.0 34.7 1.0 1.9 2.0 2a.64 1.0 jz.6e 3.0 qc.9 2.0 u.9 1.0 4d.9 1.0 a.6e 1.0 5n.6c 1.0 3z.66 1.6e 1.64 1.0 63.6e 1.64 1.0 1z.9 1.0 k.6e 8.0 2.64 1.0 1c.6e 5.64 6.6e 2.64 1.0 1.64 2.6e 2.64 2.6e 5.64 1.6e i.64 1.0 2.6e 6.64 1.6e 4.6i 1.0 20.7 1.0 f.9 1.0 12.6e 1.64 1.6e 7.0 1i.9 2.0 1m.7 1.0 b.9 2.0 1v.7 1.0 48.6e 3.0 1.1 1.64 5.6e 2.64 4.6e 1.0 1.1 7.0 4.64 1.0 6.6e 1.0 3.6e 2.0 5i.6e 2.64 1.6e 7.64 1.6e 2.6i 1.5y 1.64 1.5m 1.6e 11.6g 1.6c 2.64 1.62 1.6e 1.6h 1.64 1.6e 1.64 1.0 k0.6e 2.1 2.6e 4.1 3.6e 2.0 4.6e 1.0 3.1 2.6e 1.64 1.6e 1.1 2.64 4.6e 1.0 2da.6e 3.0 3x.9 1.0 2o.6e w.0 fe.62 1.6c 1.6g 1.66 1.68 2.0 2x.8 2.0 n9w.6e 1.0 4.6e a.0 w.6e 2.0 28.6e 2.0 7o.9 1.0 11.9 1.0 47.9 1.0 r.6e i.0 1l.64 3.0 11.9 1.0 2n.7 1.0 c.9 1.0 6n.6e 1.0 1.6e 2.64 1.0 2.6e 2.0 5.6e 2.0 1.6e 1.0 1g.9 1.0 6u.9 1.0 fn4.q 1.0 ld.6e 7.64 7.6e 2.0 r1.64 1.0 6a.64 1.0 45.6e 5.0 1aq.64 1.0 1.6e 1.0 14.6e 1.1 1.64 1.0 4.9 1.0 4l.6e 1.64 1.0 fx.6e 4.0 1t.6e 5.0 8t.6e 2.0 25.64 2.0 1.64 3.0 1y.64 2.6e 3.64 1.6e 1.64 4.0 1d.6e 1.64 1.6e 1.64 1.0 5c.9 1.0 15.9 1.0 e.9 1.0 1l.9 1.7 1.0 1x.6e 3.0 1c.9 2.0 1q.7 1.0 24.9 1.0 9.7 1.0 2y.9 1.7 1.0 4y.7 1.9 1.0 28.7 2.0 g.9 1.0 o.6e 7.0 3.6e 5.0 2h.9 3.0 35.9 1.0 3.7 1.0 n.6e 1.0 2r.9 1.7 1.0 6z.9 1.7 1.0 3i.9 1.0 3a.9 1.7 1.0 37.9 1.0 7h.9 1.7 1.0 76.9 2.0 4.7 1.0 4c.9 1.0 2b.9 1.0 i.9 1.0 29.9 1.0 bp.9 1.0 76.7 1.0 1.9 2.0 29.9 1.0 bt.9 2.0 d0s.9 1.0 1xc.1 5.0 1n.6e 7.0 xl.6 2.0 f58.1 1.0 43q.60 2.1 3.0 3.6a 1.60 5.0 8.64 8.0 2.6e 5.64 2.0 u.6e 4.0 44.6e 3.0 2pn.6e 7.0 1.6e h.0 2.6e 7.0 1.6e 2.0 1.6e 5.0 2s.6e 1.0 4g.6e 7.0 af.6e 1.0 1p.6e 4.0 e4.6g 2.64 1.6e 1.0 72.6e 1.64 1.0 6r.6e 1.0 2.6e 1.0 7.6e 2.0 5.6e 1.0 d6.64 7.0 31.6e 6.7 1.0';
const UD_SCRIPTS = ["Adlm", "Aghb", "Ahom", "Arab", "Armi", "Armn", "Avst", "Bali", "Bamu", "Bass", "Batk", "Beng", "Berf", "Bhks", "Bopo", "Brah", "Brai", "Bugi", "Buhd", "Cakm", "Cans", "Cari", "Cham", "Cher", "Chrs", "Copt", "Cpmn", "Cprt", "Cyrl", "Deva", "Diak", "Dogr", "Dsrt", "Dupl", "Egyp", "Elba", "Elym", "Ethi", "Gara", "Geor", "Glag", "Gong", "Gonm", "Goth", "Gran", "Grek", "Gujr", "Gukh", "Guru", "Hang", "Hani", "Hano", "Hatr", "Hebr", "Hira", "Hluw", "Hmng", "Hmnp", "Hung", "Ital", "Java", "Kali", "Kana", "Kawi", "Khar", "Khmr", "Khoj", "Kits", "Knda", "Krai", "Kthi", "Lana", "Laoo", "Latn", "Lepc", "Limb", "Lina", "Linb", "Lisu", "Lyci", "Lydi", "Mahj", "Maka", "Mand", "Mani", "Marc", "Medf", "Mend", "Merc", "Mero", "Mlym", "Modi", "Mong", "Mroo", "Mtei", "Mult", "Mymr", "Nagm", "Nand", "Narb", "Nbat", "Newa", "Nkoo", "Nshu", "Ogam", "Olck", "Onao", "Orkh", "Orya", "Osge", "Osma", "Ougr", "Palm", "Pauc", "Perm", "Phag", "Phli", "Phlp", "Phnx", "Plrd", "Prti", "Rjng", "Rohg", "Runr", "Samr", "Sarb", "Saur", "Sgnw", "Shaw", "Shrd", "Sidd", "Sidt", "Sind", "Sinh", "Sogd", "Sogo", "Sora", "Soyo", "Sund", "Sunu", "Sylo", "Syrc", "Tagb", "Takr", "Tale", "Talu", "Taml", "Tang", "Tavt", "Tayo", "Telu", "Tfng", "Tglg", "Thaa", "Thai", "Tibt", "Tirh", "Tnsa", "Todr", "Tols", "Toto", "Tutg", "Ugar", "Vaii", "Vith", "Wara", "Wcho", "Xpeo", "Xsux", "Yezi", "Yiii", "Zanb", "Zinh", "Zyyy", "Zzzz"];
const UD_SC = '0.4t 1t.21 q.4t 6.21 q.4t 1b.21 1.4t f.21 1.4t 5.21 n.4t 1.21 v.4t 1.21 ch.4t 13.21 5.4t 5.e 2.4t k.4s 34.19 4.4t 1.19 3.4u 2.19 4.4t 1.19 1.4u 4.19 1.4t 1.19 1.4t 1.19 3.4u 1.19 1.4u 1.19 k.4u 1.19 1r.p e.19 g.s 3p.4s 2.s 4p.4u 1.5 12.4u 2.5 1e.4u 2.5 3.4u 1.1h 1j.4u 8.1h r.4u 4.1h 6.4u b.3 5.4t 1.3 6.4t 1.3 e.4t 1.3 3.4t 1.3 w.4t 1.3 a.4s b.3 q.4s 1.3 30.4t 1.3 y.3x e.4u 1.3x 1o.4u 2.3x 3.3 1c.49 1e.4u e.2u 1n.4u 2.2u 3.3g 1a.4u 2.3g f.4u 1.2b s.4u 2.2b 1.4u 1.3x b.4u 5.3 y.4u 5.3 23.4t 1.3 t.t 29.4s 4.t f.4t 2.t q.b 4.4u 1.b 8.4u 2.b 2.4u 2.b m.4u 1.b 7.4u 1.b 1.4u 3.b 4.4u 2.b 9.4u 2.b 2.4u 2.b 4.4u 8.b 1.4u 4.b 2.4u 1.b 5.4u 2.b p.4u 2.1c 3.4u 1.1c 6.4u 4.1c 2.4u 2.1c m.4u 1.1c 7.4u 1.1c 2.4u 1.1c 2.4u 1.1c 2.4u 2.1c 1.4u 1.1c 5.4u 4.1c 2.4u 2.1c 3.4u 3.1c 1.4u 7.1c 4.4u 1.1c 1.4u 7.1c h.4u a.1a 3.4u 1.1a 9.4u 1.1a 3.4u 1.1a m.4u 1.1a 7.4u 1.1a 2.4u 1.1a 5.4u 2.1a a.4u 1.1a 3.4u 1.1a 3.4u 2.1a 1.4u f.1a 4.4u 2.1a c.4u 7.1a 7.4u 1.30 3.4u 1.30 8.4u 2.30 2.4u 2.30 m.4u 1.30 7.4u 1.30 2.4u 1.30 5.4u 2.30 9.4u 2.30 2.4u 2.30 3.4u 7.30 3.4u 4.30 2.4u 1.30 5.4u 2.30 i.4u a.42 2.4u 1.42 6.4u 3.42 3.4u 1.42 4.4u 3.42 2.4u 1.42 1.4u 1.42 2.4u 3.42 2.4u 3.42 3.4u 3.42 c.4u 4.42 5.4u 3.42 3.4u 1.42 4.4u 2.42 1.4u 6.42 1.4u e.42 l.4u 5.46 d.4u 1.46 3.4u 1.46 n.4u 1.46 g.4u 2.46 9.4u 1.46 3.4u 1.46 4.4u 7.46 2.4u 1.46 3.4u 1.46 2.4u 2.46 4.4u 2.46 a.4u 7.46 9.1w d.4u 1.1w 3.4u 1.1w n.4u 1.1w a.4u 1.1w 5.4u 2.1w 9.4u 1.1w 3.4u 1.1w 4.4u 7.1w 2.4u 5.1w 3.4u 1.1w 4.4u 2.1w a.4u 1.1w 3.4u c.2i d.4u 1.2i 3.4u 1.2i 1f.4u 1.2i 3.4u 1.2i 6.4u 4.2i g.4u 2.2i q.4u 1.3p 3.4u 1.3p i.4u 3.3p o.4u 1.3p 9.4u 1.3p 1.4u 2.3p 7.4u 3.3p 1.4u 4.3p 6.4u 1.3p 1.4u 1.3p 8.4u 6.3p a.4u 2.3p 3.4u c.4a 1m.4u 4.4t 1.4a s.4u 11.20 2.4u 1.20 1.4u 1.20 5.4u 1.20 o.4u 1.20 1.4u 1.20 n.4u 2.20 5.4u 1.20 1.4u 1.20 7.4u 1.20 a.4u 2.20 4.4u w.4b 20.4u 1.4b 10.4u 4.4b 13.4u 1.4b 10.4u 1.4b f.4u 1.4b 7.4t 4.4b 2.4u 11.2o 4g.13 12.4u 1.13 1.4u 5.13 1.4u 2.13 17.4t 1.13 4.1d 74.11 21.4u 1.11 4.4u 2.11 7.4u 1.11 1.4u 1.11 4.4u 2.11 15.4u 1.11 4.4u 2.11 x.4u 1.11 4.4u 2.11 7.4u 1.11 1.4u 1.11 4.4u 2.11 f.4u 1.11 1l.4u 1.11 4.4u 2.11 1v.4u 2.11 w.4u 3.11 q.4u 6.n 2e.4u 2.n 6.4u 2.k hs.2w t.4u 3.3f 23.4t 3.3f b.4u 7.48 m.4u 9.48 1.1f l.4t 2.4u 9.i k.4u c.3y d.4u 1.3y 3.4u 1.3y 2.4u c.1t 2m.4u 2.1t a.4u 6.1t a.4u 6.2k 2.4t 2.2k 1.4t 1.2k k.4u 6.2k 2h.4u 7.2k 17.4u 5.k 1y.4u a.23 v.4u 1.23 c.4u 4.23 c.4u 4.23 1.4u 3.23 c.40 u.4u 2.40 5.4u b.41 18.4u 4.41 q.4u 6.41 b.4u 3.41 2.1t w.h s.4u 2.h 2.1z 1r.4u 1.1z t.4u 2.1z b.4u 6.1z a.4u 6.1z e.4u 2.4s 1a.4u 2.4s c.4u k.7 25.4u 1.7 1e.3u 1s.a 1g.4u 8.a 4.22 1k.4u 3.22 f.4u 3.22 3.2x 1c.s b.4u 5.13 17.4u 2.13 3.3u 8.4u 8.4s 3.4t 1.4s d.4t 1.4s 7.4t 4.4s 1.4t 6.4s 1.4t 3.4s 2.4t 1.4u 5.21 12.19 5.s 1.21 1d.19 5.21 4.19 5.21 d.s 1.21 1y.19 1.4s 1s.21 74.19 m.4u 2.19 6.4u 2.19 12.4u 2.19 6.4u 2.19 8.4u 1.19 1.4u 1.19 1.4u 1.19 1.4u 1.19 v.4u 2.19 1h.4u 1.19 f.4u 1.19 e.4u 2.19 6.4u 1.19 j.4u 2.19 3.4u 1.19 9.4u 1.4t c.4s 2.4t 2f.4u 1.4t b.21 1.4u 2.4t b.21 1.4t f.4u 1.21 d.4u 3.4t y.4u e.4s x.4u f.4t 12.19 1.4t 3.21 2.4t 6.21 1.4t r.21 1.4t h.21 15.4t 3.4u 4.4t ii.4u m.4t b.4u l.4t ps.g 74.4t hg.4u 2.4t 3u.14 2o.21 w.p 38.4u 5.p 7.13 12.4u 1.13 1.4u 5.13 1.4u 2.47 1k.4u 7.47 2.4u e.47 1.11 n.4u 9.11 7.4u 1.11 7.4u 1.11 7.4u 1.11 7.4u 1.11 7.4u 1.11 7.4u 1.11 7.4u 1.11 7.4u 1.s w.4t 2m.4u y.1e q.4u 1.1e 2h.4u c.1e 5y.4u q.4t l.1e 1.4t 1.1e 1.4t p.1e 9.4s 4.1d 2.4t 8.1e 4.4t 4.4u 1.1i 2e.4u 2.4s 2.4t 2.1i 3.4t 1.1q 2i.4t 2.1q 3.4u 5.e 17.4u 1.1d 2m.4u 1.4t g.e w.4t 12.4u 9.4t 1.1q g.1d v.4u 1.4t 1s.1d v.4t 29.1q 1b.4t 1.1q 2g.4t 4o.1e 534.4t 1s.1e g74.4q wd.4u 3.4q 1j.4u 9.26 1c.4j 8c.4u k.s 2o.8 2g.4u 8.4t y.21 2u.4t 3.21 2a.4u k.21 f.3w 19.4u 3.4t a.4u 6.37 1k.4u 8.3i 1y.4u 8.3i c.4u 6.t w.1p 1a.4t 1.1p 1.3d 10.4u b.3d 1.1d t.4u 3.1o 26.4u 1.4t 1.1o a.4u 4.1o 2.2o v.4u 1.m 1j.4u 9.m e.4u 2.m a.4u 2.m 4.2o w.44 1v.4u o.44 5.2m n.4u a.11 6.4u 2.11 6.4u 2.11 6.4u 9.11 7.4u 1.11 7.4u 1.21 17.4t 1.21 9.19 1.21 4.4t 2.4u 4.n 28.2m 1a.4u 2.2m a.4u 6.1d 8mc.4u c.1d n.4u 4.1d 1d.4u 6is.1e a6.4u 2.1e 2y.4u 12.21 7.4u c.5 5.4u 5.1h q.4u 1.1h 5.4u 1.1h 1.4u 1.1h 2.4u 1.1h 2.4u 1.1h a.3 dq.4t 2.3 40.4u w.3 g.4s g.4t a.4u 6.4s e.s 2.4t z.4u 1.4t j.4u 1.4t 4.4u 4.3 5.4u 1.3 3r.4u 2.4t 1.4u 1.4t w.21 q.4t 6.21 q.4t b.1q a.4t 1.1q 19.4t 2.1d v.4u 3.1d 6.4u 2.1d 6.4u 2.1d 6.4u 2.1d 3.4u 3.4t 7.4u 1.4t 7.4u a.4t 5.4u 2.25 c.4u 1.25 q.4u 1.25 j.4u 1.25 2.4u 1.25 f.4u 2.25 e.4u y.25 3f.4u 5.4t 3.4u 4.4t 19.4u 3.4t 9.19 27.4u 1.4t d.4u 3.19 1.4u 1b.4t 19.4s 1.4u 3m.27 t.4u 3.l 1d.4u f.4s 1.4t r.4u 4.1n 10.4u 9.1n 3.17 r.4u 5.36 17.4u 5.4i u.4u 1.4i 1.4n 10.4u 4.4n e.4u 16.w 28.3k 1c.32 u.4u 2.32 a.4u 6.31 10.4u 4.31 10.4u 4.z 14.4u 8.1 1g.4u b.1 1.4k b.4u 1.4k f.4u 1.4k 7.4u 1.4k 2.4u 1.4k b.4u 1.4k f.4u 1.4k 7.4u 1.4k 2.4u 3.4e 1g.4u c.24 8n.4u 9.24 m.4u a.24 8.4u o.21 6.4u 1.21 16.4u 1.21 9.4u 1x.r 6.4u 2.r 1.4u 1.r 18.4u 1.r 2.4u 3.r 1.4u 2.r 1.4 m.4u 1.4 9.34 w.2s v.4u 8.2s 9.4u 1c.1g j.4u 1.1g 2.4u 5.1g 5.3a s.4u 3.3a 1.28 q.4u 5.28 1.3n q.4u 12.2h w.2g o.4u 4.2g k.4u 2.2g 1a.1s 4.4u 1.1s 2.4u 5.1s 8.4u 1.1s 3.4u 1.1s t.4u 2.1s 3.4u 4.1s a.4u 7.1s 9.4u 7.3h w.2r w.4u w.2c 13.4u 4.2c c.4u 9.6 1i.4u 3.6 7.3c m.4u 2.3c 8.38 j.4u 5.38 8.39 i.4u 7.39 4.4u c.39 7.4u 28.2z 21.4u 1j.1m 1f.4u d.1m 1f.4u 7.1m 6.3e 14.4u 8.3e a.4u 6.12 12.4u 3.12 t.4u 8.12 2.4u 5s.3 v.4u 1.4p 16.4u 1.4p 3.4u 2.4p 2.4u g.3 6.4u 8.3 9.4u x.3 6.3r 14.4u 8.3q 16.4u m.33 q.4u 12.o s.4u k.10 n.4u 9.f 26.4u 4.f 10.4u 9.f 1.1y 1v.4u a.1y 1.4u 2.3s p.4u 7.3s a.4u 6.j 1h.4u 1.j i.4u 8.29 13.4u 9.3l 2o.4u 1.3p k.4u b.1u i.4u 1.1u 1b.4u 1q.2n 7.4u 1.2n 1.4u 1.2n 4.4u 1.2n f.4u 1.2n b.4u 6.3o 1n.4u 5.3o a.4u 6.18 4.4u 1.18 8.4u 2.18 2.4u 2.18 m.4u 1.18 7.4u 1.18 2.4u 1.18 5.4u 1.4s 1.18 9.4u 2.18 2.4u 2.18 3.4u 2.18 1.4u 6.18 1.4u 5.18 7.4u 2.18 7.4u 3.18 5.4u b.4h a.4u 1.4h 1.4u 2.4h 1.4u 1.4h 12.4u 1.4h a.4u 1.4h 1.4u 2.4h 1.4u 1.4h 4.4u 1.4h a.4u 1.4h 2.4u 8.4h 2.4u t.2t 2k.4u 1.2t 5.4u u.4c 20.4u 8.4c a.4u 4m.3m 1i.4u 2.3m 12.4u y.2j 1x.4u b.2j a.4u 6.2k d.4u j.3z 1m.4u 6.3z a.4u 6.2o k.4u s.2 r.4u 2.2 f.4u 4.2 n.4u 55.v 1o.4u 2s.4l 2b.4u c.4l 1.u 7.4u 2.u 1.4u 2.u 8.4u 1.u 2.4u 1.u u.4u 1.u 2.4u 2.u c.4u 9.u a.4u 1y.2q 8.4u 2.2q 1a.4u 2.2q b.4u r.4r 20.4u 8.3t 2b.4u d.k g.35 1l.4u 7.t a.4u 2e.3l 8.4u 2g.3v y.4u e.3v a.4u 6.d 9.4u 1.d 19.4u 1.d e.4u a.d t.4u 3.2d w.4u 2.2d m.4u 1.2d e.4u 21.16 7.4u 1.16 2.4u 1.16 18.4u 3.16 1.4u 1.16 2.4u 1.16 9.4u 8.16 a.4u 6.15 6.4u 1.15 2.4u 1.15 11.4u 1.15 2.4u 1.15 6.4u 7.15 a.4u 6.4f 18.4u 4.4f a.4u 6u.2a p.4u 7.1r h.4u 1.1r 15.4u 3.1r t.4u 2d.26 1.4u f.42 1e.4u d.42 1.4o pm.4u 2u.4o 33.4u 1.4o 5.4u b.4o 5g.4u 218.q 2r.4u d.y uu.4u a.y 32z.4u 5.1j g7.4u 5a1.1b 1m.4u 1c6.8 ft.4u 7.2l v.4u 1.2l a.4u 4.2l 2.4d 27.4u 1.4d a.4u 6.9 u.4u 2.9 6.4u a.1k 1y.4u a.1k a.4u 1.1k 7.4u 1.1k l.4u 5.1k j.4u c0.1x 1m.4u 5i.2e 2j.4u 5.c p.4u 2.c p.4u 18.3b 23.4u 4.3b 1l.4u 7.3b h.4u 1s.43 1.2v 1.1e 2.1v 1.4u b.1e 7.4u 9.43 5c0.1v d2.4u 15.1v 1.43 v.4u 2p.43 37.4u 6pp.1q 4.4u 1.1q 7.4u 1.1q 2.4u 1.1q 1.1i 7z.1q 3.4u f.1i 1.4u t.1i 3.4u 2.1q 1.4u e.1q 4.4u 8.2v b0.4u 1s4.x 2z.4u 5.x d.4u 3.x 9.4u 7.x a.4u 2.x 4.4t 4.4u 318.4t 71.4u 3.4t c4.4u 6.4t n.4u f.4t h.4u f.4s 1a.4u 2.4s n.4u 9.4t 38.4u 1o.4t 6u.4u a.4t 13.4u 2.4t 1q.4s 3.4t h.4s 8.4t 2.4s 7.4t u.4s 4.4t 1p.4u l.19 1y.4u 3e.4t k.4u c.4t k.4u c.4t 2f.4u 9.4t p.4u 3r.4t 2d.4u 1.4t 1z.4u 1.4t 2.4u 2.4t 1.4u 2.4t 2.4u 2.4t 4.4u 1.4t c.4u 1.4t 1.4u 1.4t 7.4u 1.4t 1t.4u 1.4t 4.4u 2.4t 8.4u 1.4t 7.4u 1.4t s.4u 1.4t 4.4u 1.4t 5.4u 1.4t 1.4u 3.4t 7.4u 1.4t 9g.4u 2.4t 84.4u 2.4t 1e.3j i4.4u f.3j 5.4u 1.3j f.4u uo.21 v.4u 6.21 6.4u 5x.14 7.4u 1.14 h.4u 2.14 7.4u 1.14 2.4u 1.14 5.4u 5.s 1q.4u x.s 1.4u 34.1l 19.4u 3.1l e.4u 2.1l a.4u 4.1l 2.4u 8w.4g v.4u h.4m 1m.4u 5.4m 1.4u cw.2p 16.4u 5y.2y 17.4u 4.2y 1.4u 5c.45 v.4u 1.45 m.4u 8.45 2.4u 68.11 7.4u 1.11 4.4u 1.11 2.4u 1.11 f.4u 1.2f 5h.4u 2.2f g.4u 15.0 24.4u 4.0 a.4u 4.0 2.4u lt.4t 1w.4u 24.4t 1p.4u 5e.3 4.4u 1.3 r.4u 1.3 2.4u 1.3 1.4u 2.3 1.4u 1.3 a.4u 1.3 4.4u 1.3 1.4u 1.3 1.4u 6.3 1.4u 4.3 1.4u 1.3 1.4u 1.3 1.4u 1.3 3.4u 1.3 2.4u 1.3 1.4u 2.3 1.4u 1.3 1.4u 1.3 1.4u 1.3 1.4u 1.3 1.4u 1.3 2.4u 1.3 1.4u 2.3 4.4u 1.3 7.4u 1.3 4.4u 1.3 4.4u 1.3 1.4u 1.3 a.4u 1.3 h.4u 5.3 3.4u 1.3 5.4u 1.3 h.4u 1g.3 2.4u 7i.4t 18.4u 4.4t 2s.4u c.4t f.4u 2.4t f.4u 1.4t f.4u 1.4t 11.4u a.4t 4u.4u 1k.4t q.1i 1.4t 2.4u d.4t 18.4u 4.4t 9.4u 7.4t 2.4u e.4t 6.4u 4a.4t rd.4u 3.4t h.4u 3.4t d.4u 3.4t 62.4u 6.4t c.4u 4.4t 1.4u f.4t c.4u 4.4t 1k.4u 8.4t a.4u 6.4t 14.4u 8.4t u.4u 2.4t c.4u 4.4t 2.4u e.4t 9.4u 13.4t 9k.4u 8.4t e.4u 2.4t d.4u 3.4t b.4u 3.4t 1l.4u 1.4t 1.4u 4.4t g.4u 2.4t c.4u 4.4t a.4u 7.4t 43.4u 1.4t 2v.4u sl.1e wyo.4u w.1e 3dq.4u 2.1e 4ge.4u 2.1e 5rl.4u f.1e ha.4u 1wi.1e f2.4u 15u.1e 3t7.4u 5.1e 6ju.4u f5vr.4t 1.4u u.4t 2o.4u 3k.4s';

/* ======================================================================
 *  Unicode adapter.  S2 owns unicode.js / normalize.js.  When they are
 *  present we use them; otherwise we fall back to the run-length tables
 *  above, which are generated from ucd/ (Unicode 17.0.0) -- the same data
 *  HarfBuzz 14.4.0 is built against.
 * ==================================================================== */
let S2 = null;
try { S2 = await import( './unicode.js' ); } catch { S2 = null; }
let S2NORM = null;
try { S2NORM = await import( './normalize.js' ); } catch { S2NORM = null; }
/* cff.js (W3) is read-only to us; it is only needed for CFF glyph names. */
let CFFMOD = null;
try { CFFMOD = await import( './cff.js' ); } catch { CFFMOD = null; }
let GLYFMOD = null;
try { GLYFMOD = await import( './glyf.js' ); } catch { GLYFMOD = null; }
/* colr.js (W3) -- the colour tables, shared with bin/hb-vector.js so that the
 * shaper and the drawing tool resolve COLR through ONE parser. */
let COLRMOD = null;
try { COLRMOD = await import( './colr.js' ); } catch { COLRMOD = null; }
/* varfont.js (W4) -- OpenType Font Variations. */
let VF = null;
try { VF = await import( './varfont.js' ); } catch { VF = null; }

/* Per-script shapers (S1).  Each module exports an object shaped like the
 * reference's hb_ot_shaper_t, plus a shaperForScript() selector returning the
 * right one or null for the default shaper.  Script tags are passed as the
 * 4-character string ('Arab', 'Deva', ...). */
const SHAPER_MODULES = [];
for ( const m of [ './shaper-use.js', './shaper-khmer.js', './shaper-myanmar.js',
                  './shaper-hangul.js', './shaper-indic.js', './shaper-misc.js' ] ) {
  try { SHAPER_MODULES.push( await import( m ) ); } catch { /* not present yet */ }
}
/* `ctx' carries the two facts hb_ot_shaper_categorize() needs beyond the
 * script itself: whether GSUB actually selected a script for this run, and
 * whether the run is horizontal.  The cursive scripts are routed to the Arabic
 * shaper only when "(gsub_script != HB_TAG_NONE || script == Arab) &&
 * HB_DIRECTION_IS_HORIZONTAL(direction)"; otherwise they take the DEFAULT
 * shaper, not the script's own. */
export function shaperForScript( scriptTag, ctx ) {
  for ( const mod of SHAPER_MODULES ) {
    if ( mod && typeof mod.shaperForScript === 'function' ) {
      const sh = mod.shaperForScript( scriptTag, ctx );
      if ( sh ) return sh;
    }
  }
  if ( S1 && typeof S1.shaperForScript === 'function' ) return S1.shaperForScript( scriptTag, ctx );
  return null;
}

/* The 391 CFF standard strings (SID < 391), Appendix A of the CFF spec. */
const CFF_STD_STRINGS = '.notdef|space|exclam|quotedbl|numbersign|dollar|percent|ampersand|quoteright|parenleft|parenright|asterisk|plus|comma|hyphen|period|slash|zero|one|two|three|four|five|six|seven|eight|nine|colon|semicolon|less|equal|greater|question|at|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|bracketleft|backslash|bracketright|asciicircum|underscore|quoteleft|a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|braceleft|bar|braceright|asciitilde|exclamdown|cent|sterling|fraction|yen|florin|section|currency|quotesingle|quotedblleft|guillemotleft|guilsinglleft|guilsinglright|fi|fl|endash|dagger|daggerdbl|periodcentered|paragraph|bullet|quotesinglbase|quotedblbase|quotedblright|guillemotright|ellipsis|perthousand|questiondown|grave|acute|circumflex|tilde|macron|breve|dotaccent|dieresis|ring|cedilla|hungarumlaut|ogonek|caron|emdash|AE|ordfeminine|Lslash|Oslash|OE|ordmasculine|ae|dotlessi|lslash|oslash|oe|germandbls|onesuperior|logicalnot|mu|trademark|Eth|onehalf|plusminus|Thorn|onequarter|divide|brokenbar|degree|thorn|threequarters|twosuperior|registered|minus|eth|multiply|threesuperior|copyright|Aacute|Acircumflex|Adieresis|Agrave|Aring|Atilde|Ccedilla|Eacute|Ecircumflex|Edieresis|Egrave|Iacute|Icircumflex|Idieresis|Igrave|Ntilde|Oacute|Ocircumflex|Odieresis|Ograve|Otilde|Scaron|Uacute|Ucircumflex|Udieresis|Ugrave|Yacute|Ydieresis|Zcaron|aacute|acircumflex|adieresis|agrave|aring|atilde|ccedilla|eacute|ecircumflex|edieresis|egrave|iacute|icircumflex|idieresis|igrave|ntilde|oacute|ocircumflex|odieresis|ograve|otilde|scaron|uacute|ucircumflex|udieresis|ugrave|yacute|ydieresis|zcaron|exclamsmall|Hungarumlautsmall|dollaroldstyle|dollarsuperior|ampersandsmall|Acutesmall|parenleftsuperior|parenrightsuperior|twodotenleader|onedotenleader|zerooldstyle|oneoldstyle|twooldstyle|threeoldstyle|fouroldstyle|fiveoldstyle|sixoldstyle|sevenoldstyle|eightoldstyle|nineoldstyle|commasuperior|threequartersemdash|periodsuperior|questionsmall|asuperior|bsuperior|centsuperior|dsuperior|esuperior|isuperior|lsuperior|msuperior|nsuperior|osuperior|rsuperior|ssuperior|tsuperior|ff|ffi|ffl|parenleftinferior|parenrightinferior|Circumflexsmall|hyphensuperior|Gravesmall|Asmall|Bsmall|Csmall|Dsmall|Esmall|Fsmall|Gsmall|Hsmall|Ismall|Jsmall|Ksmall|Lsmall|Msmall|Nsmall|Osmall|Psmall|Qsmall|Rsmall|Ssmall|Tsmall|Usmall|Vsmall|Wsmall|Xsmall|Ysmall|Zsmall|colonmonetary|onefitted|rupiah|Tildesmall|exclamdownsmall|centoldstyle|Lslashsmall|Scaronsmall|Zcaronsmall|Dieresissmall|Brevesmall|Caronsmall|Dotaccentsmall|Macronsmall|figuredash|hypheninferior|Ogoneksmall|Ringsmall|Cedillasmall|questiondownsmall|oneeighth|threeeighths|fiveeighths|seveneighths|onethird|twothirds|zerosuperior|foursuperior|fivesuperior|sixsuperior|sevensuperior|eightsuperior|ninesuperior|zeroinferior|oneinferior|twoinferior|threeinferior|fourinferior|fiveinferior|sixinferior|seveninferior|eightinferior|nineinferior|centinferior|dollarinferior|periodinferior|commainferior|Agravesmall|Aacutesmall|Acircumflexsmall|Atildesmall|Adieresissmall|Aringsmall|AEsmall|Ccedillasmall|Egravesmall|Eacutesmall|Ecircumflexsmall|Edieresissmall|Igravesmall|Iacutesmall|Icircumflexsmall|Idieresissmall|Ethsmall|Ntildesmall|Ogravesmall|Oacutesmall|Ocircumflexsmall|Otildesmall|Odieresissmall|OEsmall|Oslashsmall|Ugravesmall|Uacutesmall|Ucircumflexsmall|Udieresissmall|Yacutesmall|Thornsmall|Ydieresissmall|001.000|001.001|001.002|001.003|Black|Bold|Book|Light|Medium|Regular|Roman|Semibold'.split( '|' );

/* hb_unicode_general_category_t, in the reference's order. */
export const GC = {
  Cc:0, Cf:1, Cn:2, Co:3, Cs:4, Ll:5, Lm:6, Lo:7, Lt:8, Lu:9,
  Mc:10, Me:11, Mn:12, Nd:13, Nl:14, No:15, Pc:16, Pd:17, Pe:18, Pf:19,
  Pi:20, Po:21, Ps:22, Sc:23, Sk:24, Sm:25, So:26, Zl:27, Zp:28, Zs:29,
};
const GC_IS_MARK = ( g ) => g === GC.Mc || g === GC.Me || g === GC.Mn;
const GC_IS_LETTER = ( g ) => g >= GC.Ll && g <= GC.Lu;

function decodeRuns( s ) {
  /* '<delta base36>.<value base36>' entries; returns {starts:Int32Array, vals:Int32Array} */
  const parts = s.split( ' ' );
  const starts = new Int32Array( parts.length );
  const vals = new Int32Array( parts.length );
  let prev = 0;
  for ( let i = 0; i < parts.length; i++ ) {
    const d = parts[ i ].indexOf( '.' );
    prev += parseInt( parts[ i ].slice( 0, d ), 36 );
    starts[ i ] = prev;
    vals[ i ] = parseInt( parts[ i ].slice( d + 1 ), 36 );
  }
  return { starts, vals };
}
let _gc = null, _ccc = null, _sc = null;
function lookupRun( tab, cp ) {
  const a = tab.starts;
  let lo = 0, hi = a.length - 1;
  while ( lo < hi ) { const m = ( lo + hi + 1 ) >> 1; if ( a[ m ] <= cp ) lo = m; else hi = m - 1; }
  return tab.vals[ lo ];
}

export function generalCategory( cp ) {
  if ( S2 && S2.generalCategory ) return S2.generalCategory( cp );
  if ( !_gc ) _gc = decodeRuns( UD_GC );
  return lookupRun( _gc, cp );
}
export function combiningClass( cp ) {
  if ( S2 && S2.combiningClass ) return S2.combiningClass( cp );
  if ( !_ccc ) _ccc = decodeRuns( UD_CCC );
  return lookupRun( _ccc, cp );
}
export function scriptOf( cp ) {
  if ( S2 && S2.script ) return S2.script( cp );
  if ( !_sc ) _sc = decodeRuns( UD_SC );
  return UD_SCRIPTS[ lookupRun( _sc, cp ) ];
}

/* hb_unicode_funcs_t::is_default_ignorable -- HarfBuzz's own hardcoded set,
 * which is NOT identical to UCD Default_Ignorable_Code_Point. */
export function isDefaultIgnorable( ch ) {
  /* HarfBuzz's set is NOT the UCD's Default_Ignorable_Code_Point: it omits
   * 115F..1160, 180F, 3164, FFA0 and 1BCA0..1BCA3.  S2 exports both; take the
   * HarfBuzz one. */
  if ( S2 && S2.isHbDefaultIgnorable ) return S2.isHbDefaultIgnorable( ch );
  const plane = ch >> 16;
  if ( plane === 0 ) {
    const page = ch >> 8;
    switch ( page ) {
      case 0x00: return ch === 0x00AD;
      case 0x03: return ch === 0x034F;
      case 0x06: return ch === 0x061C;
      case 0x17: return ch >= 0x17B4 && ch <= 0x17B5;
      case 0x18: return ch >= 0x180B && ch <= 0x180E;
      case 0x20: return ( ch >= 0x200B && ch <= 0x200F ) ||
                        ( ch >= 0x202A && ch <= 0x202E ) ||
                        ( ch >= 0x2060 && ch <= 0x206F );
      case 0xFE: return ( ch >= 0xFE00 && ch <= 0xFE0F ) || ch === 0xFEFF;
      case 0xFF: return ch >= 0xFFF0 && ch <= 0xFFF8;
      default: return false;
    }
  }
  switch ( plane ) {
    case 0x01: return ch >= 0x1D173 && ch <= 0x1D17A;
    case 0x0E: return ch >= 0xE0000 && ch <= 0xE0FFF;
    default: return false;
  }
}
export function isVariationSelector( u ) {
  return ( u >= 0xFE00 && u <= 0xFE0F ) || ( u >= 0xE0100 && u <= 0xE01EF );
}

/* hb_unicode_funcs_t::space_fallback_type */
export const SPACE_NOT_SPACE = 0, SPACE_EM = 1, SPACE_EM_2 = 2, SPACE_EM_3 = 3,
  SPACE_EM_4 = 4, SPACE_EM_5 = 5, SPACE_EM_6 = 6, SPACE_EM_16 = 16,
  SPACE_4_EM_18 = 17, SPACE = 18, SPACE_FIGURE = 19, SPACE_PUNCTUATION = 20,
  SPACE_NARROW = 21;
export function spaceFallbackType( u ) {
  switch ( u ) {
    case 0x0020: return SPACE;
    case 0x00A0: return SPACE;
    case 0x2000: return SPACE_EM_2;
    case 0x2001: return SPACE_EM;
    case 0x2002: return SPACE_EM_2;
    case 0x2003: return SPACE_EM;
    case 0x2004: return SPACE_EM_3;
    case 0x2005: return SPACE_EM_4;
    case 0x2006: return SPACE_EM_6;
    case 0x2007: return SPACE_FIGURE;
    case 0x2008: return SPACE_PUNCTUATION;
    case 0x2009: return SPACE_EM_5;
    case 0x200A: return SPACE_EM_16;
    case 0x202F: return SPACE_NARROW;
    case 0x205F: return SPACE_4_EM_18;
    case 0x3000: return SPACE_EM;
    default: return SPACE_NOT_SPACE;   /* incl. U+1680 OGHAM SPACE MARK */
  }
}

/* _hb_modified_combining_class: identity except for the Hebrew/Arabic/Syriac/
 * Indic/Thai/Lao/Tibetan reorderings HarfBuzz applies. */
const MCC_OVERRIDE = {
  10:22, 11:15, 12:16, 13:17, 14:23, 15:18, 16:19, 17:20, 18:21, 19:14,
  20:24, 21:12, 22:25, 23:13, 24:10, 25:11, 26:26,
  27:28, 28:29, 29:30, 30:31, 31:32, 32:33, 33:27, 34:34, 35:35, 36:36,
  84:4, 91:5, 103:3, 107:107, 118:118, 122:122, 129:129, 130:132, 132:131,
};
export function modifiedCombiningClass( u ) {
  if ( S2 && S2.modifiedCombiningClass ) return S2.modifiedCombiningClass( u );
  if ( u === 0x1A60 ) return 254;      /* SAKOT after tone marks */
  if ( u === 0x0FC6 ) return 254;      /* PADMA after vowel marks */
  if ( u === 0x0F39 ) return 127;      /* TSA -PHRU before U+0F74 */
  const c = combiningClass( u );
  const o = MCC_OVERRIDE[ c ];
  return o === undefined ? c : o;
}

/* ======================================================================
 *  Tags
 * ==================================================================== */
export function tagFromString( s ) {
  if ( !s ) return 0;
  const b = [ 0x20, 0x20, 0x20, 0x20 ];
  for ( let i = 0; i < 4 && i < s.length; i++ ) b[ i ] = s.charCodeAt( i ) & 0xFF;
  return ( ( b[0] << 24 ) | ( b[1] << 16 ) | ( b[2] << 8 ) | b[3] ) >>> 0;
}
export function tagToString( t ) {
  return String.fromCharCode( ( t >>> 24 ) & 0xFF, ( t >>> 16 ) & 0xFF,
                              ( t >>> 8 ) & 0xFF, t & 0xFF );
}
export const TAG_DFLT = tagFromString( 'DFLT' );
export const TAG_dflt = tagFromString( 'dflt' );
export const TAG_latn = tagFromString( 'latn' );

/* hb_script_t is the ISO-15924 tag with an initial capital, e.g. 'Latn'. */
export function scriptFromString( s ) {
  if ( !s ) return 0;
  let t = tagFromString( s );
  /* hb_script_from_iso15924_tag: capitalise first, lowercase the rest */
  let a = String.fromCharCode( ( t >>> 24 ) & 0xFF ).toUpperCase();
  let rest = tagToString( t ).slice( 1 ).toLowerCase();
  return tagFromString( a + rest );
}

/* hb_ot_old_tag_from_script: lowercase the first letter, with exceptions. */
function oldTagFromScript( script ) {
  const s = tagToString( script );
  if ( script === 0 ) return TAG_DFLT;
  if ( s === 'Zmth' ) return tagFromString( 'math' );
  if ( s === 'Hira' || s === 'Hrkt' ) return tagFromString( 'kana' );
  if ( s === 'Laoo' ) return tagFromString( 'lao ' );
  if ( s === 'Yiii' ) return tagFromString( 'yi  ' );
  if ( s === 'Nkoo' ) return tagFromString( 'nko ' );
  if ( s === 'Vaii' ) return tagFromString( 'vai ' );
  return ( script | 0x20000000 ) >>> 0;
}
const NEW_TAG = {
  Beng:'bng2', Deva:'dev2', Gujr:'gjr2', Guru:'gur2', Knda:'knd2',
  Mlym:'mlm2', Orya:'ory2', Taml:'tml2', Telu:'tel2', Mymr:'mym2',
};
function newTagFromScript( script ) {
  const n = NEW_TAG[ tagToString( script ) ];
  return n ? tagFromString( n ) : TAG_DFLT;
}
/* hb_ot_all_tags_from_script */
export function allTagsFromScript( script ) {
  const tags = [];
  const nt = newTagFromScript( script );
  if ( nt !== TAG_DFLT ) {
    if ( nt !== tagFromString( 'mym2' ) )
      tags.push( ( ( nt & 0xFFFFFF00 ) | 0x33 ) >>> 0 );   /* ...'3' */
    tags.push( nt );
  }
  const ot = oldTagFromScript( script );
  if ( ot !== TAG_DFLT ) tags.push( ot );
  return tags;
}

/* hb_ot_tags_from_language.  The full BCP-47 -> OT registry lives in
 * hb-ot-tag-table.hh (3000+ rows generated from the IANA registry).  We
 * implement the algorithmic part plus the common two-letter codes; anything
 * else falls back to "uppercase the 3-letter subtag", which is what the
 * reference does for codes not in the table.
 * TODO: the full two-letter registry (13 language-tags.tests cases). */
const OT_LANG2 = {
  aa:'AFR', ab:'ABK', af:'AFK', am:'AMH', ar:'ARA', as:'ASM', ay:'AYM',
  az:'AZE', be:'BEL', bg:'BGR', bn:'BEN', bo:'TIB', br:'BRE', bs:'BOS',
  ca:'CAT', ce:'CHE', co:'COS', cs:'CSY', cy:'WEL', da:'DAN', de:'DEU',
  dv:'DIV', dz:'DZN', el:'ELL', en:'ENG', eo:'NTO', es:'ESP', et:'ETI',
  eu:'EUQ', fa:'FAR', fi:'FIN', fj:'FJI', fo:'FOS', fr:'FRA', fy:'FRI',
  ga:'IRI', gd:'GAE', gl:'GAL', gn:'GUA', gu:'GUJ', ha:'HAU', he:'IWR',
  hi:'HIN', hr:'HRV', hu:'HUN', hy:'HYE', id:'IND', is:'ISL', it:'ITA',
  iu:'INU', ja:'JAN', jv:'JAV', ka:'KAT', kk:'KAZ', km:'KHM', kn:'KAN',
  ko:'KOR', ks:'KSH', ku:'KUR', ky:'KIR', la:'LAT', lo:'LAO', lt:'LTH',
  lv:'LVI', mk:'MKD', ml:'MLR', mn:'MNG', mr:'MAR', ms:'MLY', mt:'MTS',
  my:'BRM', ne:'NEP', nl:'NLD', no:'NOR', or:'ORI', pa:'PAN', pl:'PLK',
  ps:'PAS', pt:'PTG', ro:'ROM', ru:'RUS', sa:'SAN', sd:'SND', si:'SNH',
  sk:'SKY', sl:'SLV', so:'SML', sq:'SQI', sr:'SRB', sv:'SVE', sw:'SWK',
  ta:'TAM', te:'TEL', tg:'TAJ', th:'THA', tk:'TKM', tr:'TRK', tt:'TAT',
  uk:'UKR', ur:'URD', uz:'UZB', vi:'VIT', yi:'JII', zh:'ZHS',
};
/* hb_ot_tags_from_complex_language, the multi-subtag rules the corpus needs.
 * Checked before the plain two/three-letter registry, longest first. */
function complexLanguageTags( s ) {
  if ( s === 'zh-hant-hk' ) return [ 'ZHH ' ];
  if ( s === 'zh-hant-mo' ) return [ 'ZHTM', 'ZHH ' ];
  if ( s === 'zh-hans' ) return [ 'ZHS ' ];
  if ( s === 'zh-hant' ) return [ 'ZHT ' ];
  if ( s.startsWith( 'zh-' ) ) {
    const sub = s.split( '-' );
    if ( sub.includes( 'hk' ) ) return [ 'ZHH ' ];
    if ( sub.includes( 'mo' ) ) return [ 'ZHTM', 'ZHH ' ];
    if ( sub.includes( 'tw' ) ) return [ 'ZHT ' ];
  }
  return null;
}

/* Fifteen two-letter codes map to SEVERAL OT tags, tried in order; the font
 * picks the first it defines.  Taken from the reference's ot_languages2, which
 * also corrects a few single-tag guesses (hy is HYE0 before HYE). */
const OT_LANG2_MULTI = {
  bi: [ 'BIS ', 'CPP ' ], dv: [ 'DIV ', 'DHV ' ], ga: [ 'IRI ', 'IRT ' ],
  ho: [ 'HMO ', 'CPP ' ], ht: [ 'HAI ', 'CPP ' ], hy: [ 'HYE0', 'HYE ' ],
  id: [ 'IND ', 'MLY ' ], in: [ 'IND ', 'MLY ' ], iu: [ 'INU ', 'INUK' ],
  ko: [ 'KOR ', 'KOH ' ], ml: [ 'MAL ', 'MLR ' ], mo: [ 'MOL ', 'ROM ' ],
  nv: [ 'NAV ', 'ATH ' ], sh: [ 'BOS ', 'HRV ', 'SRB ' ], tw: [ 'TWI ', 'AKA ' ],
};

export function otTagsFromLanguage( langStr ) {
  if ( !langStr ) return [];
  const s = langStr.toLowerCase();
  const complex = complexLanguageTags( s );
  if ( complex ) return complex.map( tagFromString );
  const dash = s.indexOf( '-' );
  const first = dash < 0 ? s : s.slice( 0, dash );
  if ( first.length === 2 ) {
    const multi = OT_LANG2_MULTI[ first ];
    if ( multi ) return multi.map( tagFromString );
    const t = OT_LANG2[ first ];
    return t ? [ tagFromString( t ) ] : [];
  }
  if ( first.length === 3 ) {
    /* "Assume it's ISO-639-3 and upper-case and use it." */
    return [ ( tagFromString( first ) & ~0x20202000 ) >>> 0 ];
  }
  return [];
}

/* hb_script_get_horizontal_direction */
const RTL_SCRIPTS = new Set( [
  'Arab','Hebr','Syrc','Thaa','Cprt','Khar','Phnx','Nkoo','Lydi','Avst',
  'Armi','Phli','Prti','Sarb','Orkh','Samr','Mand','Merc','Mero','Mani',
  'Mend','Nbat','Narb','Palm','Phlp','Hatr','Adlm','Rohg','Sogo','Sogd',
  'Elym','Chrs','Yezi','Ougr','Gara','Sidt',
] );
const INVALID_DIR_SCRIPTS = new Set( [ 'Hung','Ital','Runr','Tfng' ] );
export function scriptHorizontalDirection( script ) {
  const s = tagToString( script );
  if ( RTL_SCRIPTS.has( s ) ) return DIRECTION_RTL;
  if ( INVALID_DIR_SCRIPTS.has( s ) ) return DIRECTION_INVALID;
  return DIRECTION_LTR;
}

/* hb_direction_from_string: only the first letter is examined. */
export function directionFromString( s ) {
  if ( !s || !s.length ) return DIRECTION_INVALID;
  switch ( s[ 0 ].toLowerCase() ) {
    case 'l': return DIRECTION_LTR;
    case 'r': return DIRECTION_RTL;
    case 't': return DIRECTION_TTB;
    case 'b': return DIRECTION_BTT;
    default: return DIRECTION_INVALID;
  }
}

/* ======================================================================
 *  Font: cmap, metrics, glyph names, extents.
 *  sfnt.js (W2) owns the face; it has no cmap/post reader, so those live
 *  here.  We never modify sfnt.js.
 * ==================================================================== */
const rU8  = ( d, o ) => d[ o ];
const rU16 = ( d, o ) => ( d[ o ] << 8 ) | d[ o + 1 ];
const rI16 = ( d, o ) => ( ( ( d[ o ] << 8 ) | d[ o + 1 ] ) << 16 ) >> 16;
const rU32 = ( d, o ) => ( ( ( d[ o ] << 24 ) | ( d[ o+1 ] << 16 ) |
                             ( d[ o+2 ] << 8 ) | d[ o+3 ] ) >>> 0 );

/* --- cmap subtable readers ------------------------------------------- */
function cmapLookupIn( d, off, cp ) {
  const fmt = rU16( d, off );
  switch ( fmt ) {
    case 0: {
      if ( cp > 0xFF ) return 0;
      return rU8( d, off + 6 + cp );
    }
    case 4: {
      const segX2 = rU16( d, off + 6 );
      const segCount = segX2 >> 1;
      const endBase = off + 14;
      const startBase = endBase + segX2 + 2;
      const deltaBase = startBase + segX2;
      const rangeBase = deltaBase + segX2;
      if ( cp > 0xFFFF ) return 0;
      /* binary search over endCode */
      let lo = 0, hi = segCount - 1, seg = -1;
      while ( lo <= hi ) {
        const m = ( lo + hi ) >> 1;
        if ( rU16( d, endBase + m * 2 ) < cp ) lo = m + 1;
        else { seg = m; hi = m - 1; }
      }
      if ( seg < 0 ) return 0;
      const start = rU16( d, startBase + seg * 2 );
      if ( cp < start ) return 0;
      const delta = rU16( d, deltaBase + seg * 2 );
      const ro = rU16( d, rangeBase + seg * 2 );
      let gid;
      if ( ro === 0 ) gid = ( cp + delta ) & 0xFFFF;
      else {
        const gi = rangeBase + seg * 2 + ro + ( cp - start ) * 2;
        if ( gi + 1 >= d.length ) return 0;
        gid = rU16( d, gi );
        if ( !gid ) return 0;
        gid = ( gid + delta ) & 0xFFFF;
      }
      return gid;
    }
    case 6: {
      const first = rU16( d, off + 6 );
      const count = rU16( d, off + 8 );
      if ( cp < first || cp >= first + count ) return 0;
      return rU16( d, off + 10 + ( cp - first ) * 2 );
    }
    case 10: {
      const first = rU32( d, off + 12 );
      const count = rU32( d, off + 16 );
      if ( cp < first || cp >= first + count ) return 0;
      return rU16( d, off + 20 + ( cp - first ) * 2 );
    }
    case 12: {
      const n = rU32( d, off + 12 );
      let lo = 0, hi = n - 1;
      while ( lo <= hi ) {
        const m = ( lo + hi ) >> 1;
        const g = off + 16 + m * 12;
        const s = rU32( d, g ), e = rU32( d, g + 4 );
        if ( cp < s ) hi = m - 1;
        else if ( cp > e ) lo = m + 1;
        else return ( rU32( d, g + 8 ) + ( cp - s ) ) >>> 0;
      }
      return 0;
    }
    case 13: {
      const n = rU32( d, off + 12 );
      let lo = 0, hi = n - 1;
      while ( lo <= hi ) {
        const m = ( lo + hi ) >> 1;
        const g = off + 16 + m * 12;
        const s = rU32( d, g ), e = rU32( d, g + 4 );
        if ( cp < s ) hi = m - 1;
        else if ( cp > e ) lo = m + 1;
        else return rU32( d, g + 8 ) >>> 0;
      }
      return 0;
    }
    default: return 0;
  }
}

/* format 14: variation selectors */
function cmapUVS( d, off, cp, vs ) {
  /* returns {found:bool, glyph:int, useDefault:bool} */
  const numSel = rU32( d, off + 6 );
  let lo = 0, hi = numSel - 1, rec = -1;
  while ( lo <= hi ) {
    const m = ( lo + hi ) >> 1;
    const r = off + 10 + m * 11;
    const sel = ( d[ r ] << 16 ) | ( d[ r + 1 ] << 8 ) | d[ r + 2 ];
    if ( vs < sel ) hi = m - 1;
    else if ( vs > sel ) lo = m + 1;
    else { rec = r; break; }
  }
  if ( rec < 0 ) return { found: false, glyph: 0, useDefault: false };
  const defOff = rU32( d, rec + 3 );
  const nonDefOff = rU32( d, rec + 7 );
  if ( defOff ) {
    const n = rU32( d, off + defOff );
    let a = 0, b = n - 1;
    while ( a <= b ) {
      const m = ( a + b ) >> 1;
      const g = off + defOff + 4 + m * 4;
      const s = ( d[ g ] << 16 ) | ( d[ g + 1 ] << 8 ) | d[ g + 2 ];
      const e = s + d[ g + 3 ];
      if ( cp < s ) b = m - 1;
      else if ( cp > e ) a = m + 1;
      else return { found: false, glyph: 0, useDefault: true };
    }
  }
  if ( nonDefOff ) {
    const n = rU32( d, off + nonDefOff );
    let a = 0, b = n - 1;
    while ( a <= b ) {
      const m = ( a + b ) >> 1;
      const g = off + nonDefOff + 4 + m * 5;
      const s = ( d[ g ] << 16 ) | ( d[ g + 1 ] << 8 ) | d[ g + 2 ];
      if ( cp < s ) b = m - 1;
      else if ( cp > s ) a = m + 1;
      else return { found: true, glyph: rU16( d, g + 3 ), useDefault: false };
    }
  }
  return { found: false, glyph: 0, useDefault: false };
}

/* Unicode -> MacRoman byte for a (1,0) cmap, extracted from the reference's
 * own table: note U+20AC EURO at 0xDB, i.e. the updated MacRoman rather than
 * the original currency sign.  get_glyph_from_macroman tries ASCII, then this. */
const MACROMAN_PACKED = '00A0CA 00A1C1 00A2A2 00A3A3 00A5B4 00A7A4 00A8AC 00A9A9 00AABB 00ABC7 00ACC2 00AEA8 00AFF8 00B0A1 00B1B1 00B4AB 00B5B5 00B6A6 00B7E1 00B8FC 00BABC 00BBC8 00BFC0 00C0CB 00C1E7 00C2E5 00C3CC 00C480 00C581 00C6AE 00C782 00C8E9 00C983 00CAE6 00CBE8 00CCED 00CDEA 00CEEB 00CFEC 00D184 00D2F1 00D3EE 00D4EF 00D5CD 00D685 00D8AF 00D9F4 00DAF2 00DBF3 00DC86 00DFA7 00E088 00E187 00E289 00E38B 00E48A 00E58C 00E6BE 00E78D 00E88F 00E98E 00EA90 00EB91 00EC93 00ED92 00EE94 00EF95 00F196 00F298 00F397 00F499 00F59B 00F69A 00F7D6 00F8BF 00F99D 00FA9C 00FB9E 00FC9F 00FFD8 0131F5 0152CE 0153CF 0178D9 0192C4 02C6F6 02C7FF 02D8F9 02D9FA 02DAFB 02DBFE 02DCF7 02DDFD 03A9BD 03C0B9 2013D0 2014D1 2018D4 2019D5 201AE2 201CD2 201DD3 201EE3 2020A0 2021E0 2022A5 2026C9 2030E4 2039DC 203ADD 2044DA 20ACDB 2122AA 2202B6 2206C6 220FB8 2211B7 221AC3 221EB0 222BBA 2248C5 2260AD 2264B2 2265B3 25CAD7 F8FFF0 FB01DE FB02DF';
let MACROMAN = null;
function unicodeToMacRoman( u ) {
  if ( !MACROMAN ) {
    MACROMAN = new Map();
    for ( const e of MACROMAN_PACKED.split( ' ' ) )
      MACROMAN.set( parseInt( e.slice( 0, 4 ), 16 ), parseInt( e.slice( 4 ), 16 ) );
  }
  return MACROMAN.get( u ) || 0;
}

/* --- the 258 standard Macintosh glyph names (post format 1.0 / 2.0) --- */
const MAC_GLYPH_NAMES = ( '.notdef .null nonmarkingreturn space exclam quotedbl numbersign dollar percent ampersand quotesingle parenleft parenright asterisk plus comma hyphen period slash zero one two three four five six seven eight nine colon semicolon less equal greater question at A B C D E F G H I J K L M N O P Q R S T U V W X Y Z bracketleft backslash bracketright asciicircum underscore grave a b c d e f g h i j k l m n o p q r s t u v w x y z braceleft bar braceright asciitilde Adieresis Aring Ccedilla Eacute Ntilde Odieresis Udieresis aacute agrave acircumflex adieresis atilde aring ccedilla eacute egrave ecircumflex edieresis iacute igrave icircumflex idieresis ntilde oacute ograve ocircumflex odieresis otilde uacute ugrave ucircumflex udieresis dagger degree cent sterling section bullet paragraph germandbls registered copyright trademark acute dieresis notequal AE Oslash infinity plusminus lessequal greaterequal yen mu partialdiff summation product pi integral ordfeminine ordmasculine Omega ae oslash questiondown exclamdown logicalnot radical florin approxequal Delta guillemotleft guillemotright ellipsis nonbreakingspace Agrave Atilde Otilde OE oe endash emdash quotedblleft quotedblright quoteleft quoteright divide lozenge ydieresis Ydieresis fraction currency guilsinglleft guilsinglright fi fl daggerdbl periodcentered quotesinglbase quotedblbase perthousand Acircumflex Ecircumflex Aacute Edieresis Egrave Iacute Icircumflex Idieresis Igrave Oacute Ocircumflex apple Ograve Uacute Ucircumflex Ugrave dotlessi circumflex tilde macron breve dotaccent ring cedilla hungarumlaut ogonek caron Lslash lslash Scaron scaron Zcaron zcaron brokenbar Eth eth Yacute yacute Thorn thorn minus multiply onesuperior twosuperior threesuperior onehalf onequarter threequarters franc Gbreve gbreve Idotaccent Scedilla scedilla Cacute cacute Ccaron ccaron dcroat' ).split( ' ' );

/* ---------------------------------------------------------------------
 *  createFont
 * ------------------------------------------------------------------- */
export function createFont( face, opts = {} ) {
  const d = face.data;
  const upem = face.unitsPerEM || 1000;
  const xScale = opts.xScale === undefined ? upem : opts.xScale | 0;
  const yScale = opts.yScale === undefined ? upem : opts.yScale | 0;

  /* -- em scaling ----------------------------------------------------
   * hb_font_t::em_mult(v, mult) == roundf(v * mult) with mult computed in
   * FLOAT (not double): mult = scale / (float) upem.  Math.fround()
   * reproduces the float32 rounding; roundf() is round-half-AWAY-from-zero,
   * which is NOT Math.round() for negatives (Math.round(-0.5) === -0). */
  /* --variations -> normalized F2Dot14 coords, exactly hb_font_t::coords. */
  let coords = null, hasNonzeroCoords = false;
  if ( VF && opts.variations && VF.isVariable && VF.isVariable( face ) ) {
    try {
      coords = VF.normalizeCoords( face, opts.variations );
      for ( let i = 0; i < coords.length; i++ ) if ( coords[ i ] ) { hasNonzeroCoords = true; break; }
    } catch { coords = null; hasNonzeroCoords = false; }
  }

  /* Synthetic bold: strength is the embolden ratio in scaled units.  With
   * --font-bold the advances move too; --font-grade ("in place") leaves them. */
  const xEmbolden = opts.xEmbolden || 0, yEmbolden = opts.yEmbolden || 0;
  const emboldenInPlace = !!opts.emboldenInPlace;
  const roundAway = ( v ) => v >= 0 ? Math.floor( v + 0.5 ) : -Math.floor( -v + 0.5 );
  const xStrength = roundAway( Math.abs( xScale ) * xEmbolden ) | 0;
  const yStrength = roundAway( Math.abs( yScale ) * yEmbolden ) | 0;

  const xMultF = Math.fround( xScale / upem );
  const yMultF = Math.fround( yScale / upem );
  /* See the note on roundf() in ot-layout.js: the reference's roundf is half
   * UP, not half away from zero. */
  function roundf( v ) {
    return process.env.W19_NO_ROUNDHALFUP
      ? ( v >= 0 ? Math.floor( v + 0.5 ) : -Math.floor( -v + 0.5 ) )
      : Math.floor( v + 0.5 );
  }
  function emScaleX( v ) {
    if ( xScale === upem ) return v | 0;          /* reference fast path */
    return roundf( Math.fround( Math.fround( v ) * xMultF ) ) | 0;
  }
  function emScaleY( v ) {
    if ( yScale === upem ) return v | 0;
    return roundf( Math.fround( Math.fround( v ) * yMultF ) ) | 0;
  }

  /* -- cmap ----------------------------------------------------------- */
  let cmapOff = -1, cmapSymbol = false, cmapMac = false, cmapMacRoman = false;
  let uvsOff = -1;
  const ct = face.tables.get( 'cmap' );
  if ( ct ) {
    const base = ct.offset;
    const n = rU16( d, base + 2 );
    const sub = new Map();
    for ( let i = 0; i < n; i++ ) {
      const r = base + 4 + i * 8;
      const pid = rU16( d, r ), eid = rU16( d, r + 2 );
      const o = base + rU32( d, r + 4 );
      if ( !sub.has( pid + ':' + eid ) ) sub.set( pid + ':' + eid, o );
      if ( pid === 0 && eid === 5 && rU16( d, o ) === 14 ) uvsOff = o;
    }
    const pick = ( p, e ) => sub.has( p + ':' + e ) ? sub.get( p + ':' + e ) : -1;
    /* find_best_subtable order: SYMBOL FIRST (hb issue #1918), then 32-bit,
     * then 16-bit, then MacRoman, then any other Mac. */
    let o;
    if ( ( o = pick( 3, 0 ) ) >= 0 ) { cmapOff = o; cmapSymbol = true; }
    else if ( ( o = pick( 3, 10 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 0, 6 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 0, 4 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 3, 1 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 0, 3 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 0, 2 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 0, 1 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 0, 0 ) ) >= 0 ) cmapOff = o;
    else if ( ( o = pick( 1, 0 ) ) >= 0 ) { cmapOff = o; cmapMac = true; cmapMacRoman = true; }
    else {
      for ( const [ k, v ] of sub ) if ( k.startsWith( '1:' ) ) { cmapOff = v; cmapMac = true; break; }
    }
  }

  function getNominalGlyph( cp ) {
    if ( cmapOff < 0 ) return 0;
    if ( cmapSymbol ) {
      let g = cmapLookupIn( d, cmapOff, cp );
      if ( g ) return g;
      /* _hb_symbol_pua_map: symbol fonts duplicate U+0020..U+00FF at U+F020.. */
      if ( cp <= 0x00FF ) return cmapLookupIn( d, cmapOff, 0xF000 + cp );
      return 0;
    }
    if ( cmapMac ) {
      if ( cp < 0x80 ) {
        const g = cmapLookupIn( d, cmapOff, cp );
        if ( g ) return g;
      }
      /* "Any other Mac subtable; we just map ASCII for these." */
      if ( !cmapMacRoman ) return 0;
      const c = unicodeToMacRoman( cp );
      return c ? cmapLookupIn( d, cmapOff, c ) : 0;
    }
    return cmapLookupIn( d, cmapOff, cp );
  }
  function getVariationGlyph( cp, vs ) {
    if ( uvsOff < 0 ) return { ok: false, glyph: 0, useDefault: false };
    const r = cmapUVS( d, uvsOff, cp, vs );
    if ( r.found ) return { ok: true, glyph: r.glyph, useDefault: false };
    if ( r.useDefault ) {
      const g = getNominalGlyph( cp );
      return g ? { ok: true, glyph: g, useDefault: true } : { ok: false, glyph: 0, useDefault: true };
    }
    return { ok: false, glyph: 0, useDefault: false };
  }

  /* advance += advance ? strength : 0 -- a zero advance stays zero, and
   * --font-grade (in place) leaves advances alone entirely. */
  const emboldenAdvance = ( adv, strength, scale ) => {
    if ( !strength || emboldenInPlace ) return adv;
    const st = scale >= 0 ? strength : -strength;
    return satAdd( adv, adv ? st : 0 );
  };

  /* -- advances ------------------------------------------------------- */
  const hasHmtx = !!face.tables.get( 'hmtx' );
  const hasVmtx = !!face.tables.get( 'vmtx' );
  function getGlyphHAdvance( gid ) {
    if ( !hasHmtx )
      return emboldenAdvance( emScaleX( ( upem / 2 ) | 0 ), xStrength, xScale );
    let a = 0;
    try { a = face.hmtx( gid ).advance | 0; } catch { a = 0; }
    /* hmtx::get_advance_with_var_unscaled: advance + roundf(delta). */
    if ( hasNonzeroCoords && VF ) {
      try {
        const hv = VF.readHVAR( face );
        /* hmtx::get_advance_with_var_unscaled rounds the SUM, not the delta:
         * an hmtx advance of 395 with an HVAR delta of exactly -0.5 rounds to
         * 395, where rounding the delta first gives 394. */
        if ( hv && hv.store ) {
          const dlt = VF.hvarAdvanceDelta( hv, gid, coords );
          a = process.env.W20_NO_ADVROUND ? a + VF.roundf( dlt )
                                          : VF.roundf( a + dlt );
        }
        else if ( hasGlyf && VF.readGvar && VF.readGvar( face ) ) {
          const p = glyphPhantomPoints( gid );
          a = Math.max( 0, VF.roundf( p.rightX - p.leftX ) );
        }
      } catch { /* leave the default advance */ }
    }
    return emboldenAdvance( emScaleX( a ), xStrength, xScale );
  }
  function getGlyphVAdvance( gid ) {
    if ( !hasVmtx ) {
      /* hb_ot_get_glyph_v_advances: descender - ascender off the font extents,
       * which are themselves already emboldened; the advance embolden then
       * adds the strength back, so the two cancel. */
      const e = getFontHExtents();
      return emboldenAdvance( ( e.descender - e.ascender ) | 0, yStrength, yScale );
    }
    let a = 0;
    try { a = face.vmtx( gid ).advance | 0; } catch { a = 0; }
    if ( hasNonzeroCoords && VF ) {
      try {
        const vv = VF.readVVAR( face );
        if ( vv && vv.store ) {
          const dlt = VF.hvarAdvanceDelta( vv, gid, coords );
          a = process.env.W20_NO_ADVROUND ? a + VF.roundf( dlt )
                                          : VF.roundf( a + dlt );
        }
        else if ( hasGlyf && VF.readGvar && VF.readGvar( face ) ) {
          const p = glyphPhantomPoints( gid );
          a = Math.max( 0, VF.roundf( p.topY - p.bottomY ) );
        }
      } catch { /* leave the default advance */ }
    }
    return emboldenAdvance( emScaleY( -a ), yStrength, yScale );
  }

  /* -- font h extents (hhea, or OS/2 when USE_TYPO_METRICS) ------------ */
  let _hext = null;
  function getFontHExtents() {
    if ( _hext ) return _hext;
    const hhea = face.hhea, os2 = face.os2;
    const useTypo = !!( os2 && ( ( os2.fsSelection || 0 ) & 0x80 ) );
    let asc, desc, gap;
    if ( useTypo && os2 ) {
      asc = os2.sTypoAscender; desc = os2.sTypoDescender; gap = os2.sTypoLineGap;
    } else if ( hhea ) {
      asc = hhea.Ascender !== undefined ? hhea.Ascender : hhea.ascender;
      desc = hhea.Descender !== undefined ? hhea.Descender : hhea.descender;
      gap = hhea.Line_Gap !== undefined ? hhea.Line_Gap : hhea.lineGap;
    }
    if ( asc === undefined ) {
      /* get_h_extents_with_fallback */
      const a = ( yScale * 0.8 ) | 0;
      _hext = { ascender: a, descender: ( a - yScale ) | 0, line_gap: 0 };
      return _hext;
    }
    /* _fix_ascender_descender: |ascender|, -|descender| ; em_scalef_y */
    _hext = {
      ascender  : emScaleY( Math.abs( asc ) ),
      descender : emScaleY( -Math.abs( desc ) ),
      line_gap  : emScaleY( gap || 0 ),
    };
    /* get_font_h_extents emboldens with the Y shift and has NO in-place
     * branch -- that belongs to get_font_v_extents.  The pure fallback above
     * returns early and is left alone, because the reference only emboldens
     * when the real metrics were found. */
    if ( yStrength ) _hext.ascender += yScale < 0 ? -yStrength : yStrength;
    return _hext;
  }

  /* -- glyf phantom points ---------------------------------------------
   * HarfBuzz seeds four phantom points from hmtx/vmtx and the glyph header,
   * lets gvar move them, and then reads the metrics back off them:
   *   h advance = pp_right.x - pp_left.x        v origin = pp_top.y
   *   v advance = pp_top.y  - pp_bottom.y
   * This is the only advance source for a variable glyf font with no
   * HVAR/VVAR, and the v-origin source for any vmtx+glyf font even when no
   * variation is active. */
  const hasGlyf = !!face.tables.get( 'glyf' );

  /* Build the point set once per glyph: real contour points followed by the
   * four phantoms, gvar deltas applied, and the depth-0 pp1.x shift taken
   * out.  The reference applies that shift inside Glyph::get_points, before
   * anything consumes the points, so the bbox sees shifted points.  Advances
   * (pp_right.x - pp_left.x) and the y origin are invariant under it, so one
   * shared builder serves metrics and extents alike. */
  const variedCache = new Map();

  /* Composite glyph record: one entry per component.  glyf.js flattens
   * composites, but gvar deltas for a composite apply to the COMPONENT
   * OFFSETS, not to flattened child points, so the record is parsed here. */
  const CF_ARGS_ARE_WORDS = 0x0001, CF_ARGS_ARE_XY = 0x0002,
        CF_HAVE_SCALE = 0x0008, CF_MORE = 0x0020, CF_HAVE_XY_SCALE = 0x0040,
        CF_HAVE_2X2 = 0x0080, CF_USE_MY_METRICS = 0x0200,
        CF_SCALED_OFFSET = 0x0800;
  function readComposite( gid ) {
    let loc = null;
    try { loc = face.loca( gid ); } catch { return null; }
    if ( !loc || !loc.length ) return null;
    const o = face.glyfOffset + loc.offset;
    if ( o + 10 > d.length || rI16( d, o ) >= 0 ) return null;   /* not composite */
    let p = o + 10, flags, guard = 0;
    const comps = [];
    do {
      if ( p + 4 > d.length || ++guard > 64 ) return null;
      flags = rU16( d, p );
      const index = rU16( d, p + 2 );
      p += 4;
      let a1, a2;
      if ( flags & CF_ARGS_ARE_WORDS ) {
        if ( flags & CF_ARGS_ARE_XY ) { a1 = rI16( d, p ); a2 = rI16( d, p + 2 ); }
        else { a1 = rU16( d, p ); a2 = rU16( d, p + 2 ); }
        p += 4;
      } else {
        if ( flags & CF_ARGS_ARE_XY ) { a1 = ( d[ p ] << 24 ) >> 24; a2 = ( d[ p + 1 ] << 24 ) >> 24; }
        else { a1 = d[ p ]; a2 = d[ p + 1 ]; }
        p += 2;
      }
      /* F2Dot14 -> float; matrix is [xscale, scale01, scale10, yscale] */
      let m0 = 1, m1 = 0, m2 = 0, m3 = 1;
      if ( flags & CF_HAVE_SCALE ) { m0 = m3 = rI16( d, p ) / 16384; p += 2; }
      else if ( flags & CF_HAVE_XY_SCALE ) { m0 = rI16( d, p ) / 16384; m3 = rI16( d, p + 2 ) / 16384; p += 4; }
      else if ( flags & CF_HAVE_2X2 ) {
        m0 = rI16( d, p ) / 16384; m1 = rI16( d, p + 2 ) / 16384;
        m2 = rI16( d, p + 4 ) / 16384; m3 = rI16( d, p + 6 ) / 16384; p += 8;
      }
      comps.push( { index, flags, a1, a2, m0, m1, m2, m3 } );
    } while ( flags & CF_MORE );
    return comps;
  }

  function seedPhantoms( gid ) {
    let xMin = 0, yMax = 0;
    try {
      const loc = face.loca( gid );
      if ( loc && loc.length ) {
        const o = face.glyfOffset + loc.offset;
        if ( o + 10 <= d.length ) { xMin = rI16( d, o + 2 ); yMax = rI16( d, o + 8 ); }
      }
    } catch { /* empty or unreadable glyph */ }
    let lsb = 0, hAdv = 0, tsb = 0, vAdv = 0;
    try { const m = face.hmtx( gid ); hAdv = m.advance | 0; lsb = m.lsb | 0; } catch {}
    try { const m = face.vmtx( gid ); vAdv = m.advance | 0; tsb = m.lsb | 0; } catch {}
    const vOrig = yMax + tsb;
    return { x: [ xMin - lsb, hAdv + ( xMin - lsb ), 0, 0 ],
             y: [ 0, 0, vOrig, vOrig - vAdv ] };
  }

  /* Real contour points + the four phantoms for one glyph, gvar applied.
   * Mirrors Glyph::get_points: a composite applies its own deltas to the
   * component offsets, then recurses, transforming and translating each child
   * by its DELTA-ADJUSTED offset. */
  function collectPoints( gid, depth, seen ) {
    if ( depth > 6 || seen.has( gid ) ) return null;
    seen.add( gid );
    const ph = seedPhantoms( gid );
    const comps = process.env.S3_NO_COMPVAR ? null : readComposite( gid );
    let X = [], Y = [], endPt = [];

    if ( !comps ) {
      try {
        const r = ( GLYFMOD && GLYFMOD.loadGlyfOutline )
          ? GLYFMOD.loadGlyfOutline( face, gid ) : null;
        const ol = r && r.outline;
        if ( ol && ol.n_points ) {
          for ( let i = 0; i < ol.n_points; i++ ) { X.push( ol.x[ i ] ); Y.push( ol.y[ i ] ); }
          endPt = new Array( ol.n_points ).fill( false );
          for ( let c = 0; c < ol.n_contours; c++ ) {
            const e = ol.contours[ c ];
            if ( e >= 0 && e < ol.n_points ) endPt[ e ] = true;
          }
        }
      } catch { X = []; Y = []; endPt = []; }
      const ends = [];
      for ( let i = 0; i < endPt.length; i++ ) if ( endPt[ i ] ) ends.push( i );
      try {
        const v = VF.gvarVaryLevel( face, gid, coords, X, Y, ends,
          { leftX: ph.x[ 0 ], rightX: ph.x[ 1 ], topY: ph.y[ 2 ], bottomY: ph.y[ 3 ] } );
        if ( v ) {
          X = Array.from( v.x ); Y = Array.from( v.y );
          ph.x = [ v.phantoms.leftX, v.phantoms.rightX, 0, 0 ];
          ph.y = [ 0, 0, v.phantoms.topY, v.phantoms.bottomY ];
        }
      } catch {}
    } else {
      const cx = comps.map( ( c ) => c.a1 ), cy = comps.map( ( c ) => c.a2 );
      let pts = { x: cx.concat( ph.x ), y: cy.concat( ph.y ) };
      try {
        const v = VF.gvarVaryLevel( face, gid, coords, cx, cy, null,
          { leftX: ph.x[ 0 ], rightX: ph.x[ 1 ], topY: ph.y[ 2 ], bottomY: ph.y[ 3 ] } );
        if ( v ) {
          pts = { x: Array.from( v.x ), y: Array.from( v.y ) };
          ph.x = [ v.phantoms.leftX, v.phantoms.rightX, 0, 0 ];
          ph.y = [ 0, 0, v.phantoms.topY, v.phantoms.bottomY ];
        }
      } catch {}

      for ( let i = 0; i < comps.length; i++ ) {
        const c = comps[ i ];
        const child = collectPoints( c.index, depth + 1, seen );
        if ( !child ) continue;
        let kx = child.X.slice(), ky = child.Y.slice();
        const tx = pts.x[ i ], ty = pts.y[ i ];
        const xform = () => {
          if ( c.m0 === 1 && c.m1 === 0 && c.m2 === 0 && c.m3 === 1 ) return;
          for ( let k = 0; k < kx.length; k++ ) {
            const x = kx[ k ], y = ky[ k ];
            kx[ k ] = x * c.m0 + y * c.m2;
            ky[ k ] = x * c.m1 + y * c.m3;
          }
        };
        const move = () => { for ( let k = 0; k < kx.length; k++ ) { kx[ k ] += tx; ky[ k ] += ty; } };
        if ( c.flags & CF_ARGS_ARE_XY ) {
          if ( c.flags & CF_SCALED_OFFSET ) { move(); xform(); } else { xform(); move(); }
        } else {
          /* anchored: match child point a2 onto already-placed point a1 */
          xform();
          const p1 = c.a1, p2 = c.a2;
          if ( p1 < X.length && p2 < kx.length ) {
            const dx = X[ p1 ] - kx[ p2 ], dy = Y[ p1 ] - ky[ p2 ];
            for ( let k = 0; k < kx.length; k++ ) { kx[ k ] += dx; ky[ k ] += dy; }
          }
        }
        if ( c.flags & CF_USE_MY_METRICS ) { ph.x = child.ph.x.slice(); ph.y = child.ph.y.slice(); }
        X = X.concat( kx ); Y = Y.concat( ky ); endPt = endPt.concat( child.endPt );
      }
    }
    seen.delete( gid );
    return { X, Y, endPt, ph };
  }

  function variedPoints( gid ) {
    let hit = variedCache.get( gid );
    if ( hit !== undefined ) return hit;
    const r = collectPoints( gid, 0, new Set() );
    const X = r ? r.X.concat( r.ph.x ) : [ 0, 0, 0, 0 ];
    const Y = r ? r.Y.concat( r.ph.y ) : [ 0, 0, 0, 0 ];
    const n = r ? r.X.length : 0;
    /* depth-0 shift by the updated left side bearing, applied to every point */
    const shift = X[ n ];
    if ( shift ) for ( let i = 0; i < X.length; i++ ) X[ i ] -= shift;
    hit = { X, Y, n };
    variedCache.set( gid, hit );
    return hit;
  }

  const isVaried = () => hasNonzeroCoords && VF && VF.readGvar && !!VF.readGvar( face );

  function glyphPhantomPoints( gid ) {
    if ( hasGlyf && isVaried() ) {
      const p = variedPoints( gid );
      const b = p.X.length - 4;
      return { leftX: p.X[ b ], rightX: p.X[ b + 1 ],
               topY: p.Y[ b + 2 ], bottomY: p.Y[ b + 3 ] };
    }
    const ph = seedPhantoms( gid );
    return { leftX: ph.x[ 0 ], rightX: ph.x[ 1 ], topY: ph.y[ 2 ], bottomY: ph.y[ 3 ] };
  }

  /* contour_bounds_t over the REAL points only -- phantoms go to a separate
   * sink in the reference and never enter the box.  Bounds are rounded in
   * font units first (roundf), then scaled. */
  function variedExtentsUnscaled( gid ) {
    const p = variedPoints( gid );
    if ( !p.n ) return { x_bearing: 0, y_bearing: 0, width: 0, height: 0 };
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for ( let i = 0; i < p.n; i++ ) {
      const X = p.X[ i ], Y = p.Y[ i ];
      if ( X < minX ) minX = X; if ( X > maxX ) maxX = X;
      if ( Y < minY ) minY = Y; if ( Y > maxY ) maxY = Y;
    }
    if ( minX >= maxX || minY >= maxY )
      return { x_bearing: 0, y_bearing: 0, width: 0, height: 0 };
    const xb = roundAway( minX ), yb = roundAway( maxY );
    return { x_bearing: xb, y_bearing: yb,
             width: roundAway( maxX ) - xb, height: roundAway( minY ) - yb };
  }

  /* -- legacy `kern` table (Microsoft version 0 only) -------------------
   * Header: version u16 (0), nTables u16.  Each subtable: versionZ u16,
   * length u16, format u8, coverage u8.  Coverage bit 0 = Horizontal,
   * bit 2 = CrossStream.  Apple's version 1.0 (0x00010000) uses a different
   * header and a state machine; the reference treats it as AAT, we skip it. */
  let kernSubs = undefined;
  function kernSubtables() {
    if ( kernSubs !== undefined ) return kernSubs;
    kernSubs = [];
    const t = face.tables.get( 'kern' );
    if ( !t ) return kernSubs;
    const o = t.offset, end = o + t.length;
    const major = rU16( d, o );
    if ( major === 0 ) {
      /* Microsoft version 0: version u16, nTables u16.  Subtable header is
       * versionZ u16, length u16, format u8, coverage u8 with
       * Horizontal = 0x01, CrossStream = 0x04. */
      const n = rU16( d, o + 2 );
      let p = o + 4;
      for ( let i = 0; i < n && p + 6 <= end; i++ ) {
        const length = rU16( d, p + 2 ), cov = d[ p + 5 ];
        kernSubs.push( { offset: p, body: p + 6, end: p + ( length || 6 ),
                         format: d[ p + 4 ],
                         horizontal: !!( cov & 0x01 ),
                         crossStream: !!( cov & 0x04 ), variation: false } );
        if ( !length ) break;
        p += length;
      }
    } else if ( major === 1 ) {
      /* Apple version 1.0: version u32, tableCount u32.  Subtable header is
       * length u32, coverage u8, format u8, tupleIndex u16, and the coverage
       * bits are inverted: Vertical = 0x80, CrossStream = 0x40,
       * Variation = 0x20. */
      const n = rU32( d, o + 4 );
      let p = o + 8;
      for ( let i = 0; i < n && p + 8 <= end; i++ ) {
        const length = rU32( d, p ), cov = d[ p + 4 ];
        kernSubs.push( { offset: p, body: p + 8, end: p + ( length || 8 ),
                         format: d[ p + 5 ],
                         horizontal: !( cov & 0x80 ),
                         crossStream: !!( cov & 0x40 ),
                         variation: !!( cov & 0x20 ) } );
        if ( !length ) break;
        p += length;
      }
    }
    return kernSubs;
  }
  /* Format 2's class values are byte offsets from the subtable start, so
   * HarfBuzz's offsetToIndex(l + r) reduces to reading at subtable + l + r. */
  function kernValue( st, left, right ) {
    const o = st.body;
    if ( st.format === 0 ) {
      const nPairs = rU16( d, o );
      const key = left * 65536 + right;
      let lo = 0, hi = nPairs - 1;
      while ( lo <= hi ) {
        const m = ( lo + hi ) >> 1;
        const q = o + 8 + m * 6;
        const k = rU16( d, q ) * 65536 + rU16( d, q + 2 );
        if ( key < k ) hi = m - 1;
        else if ( key > k ) lo = m + 1;
        else return rI16( d, q + 4 );
      }
      return 0;
    }
    if ( st.format === 2 ) {
      const leftOff = rU16( d, o + 2 ), rightOff = rU16( d, o + 4 );
      const arrayOff = rU16( d, o + 6 );
      const cls = ( base, g ) => {
        const b = st.offset + base;
        if ( b + 4 > d.length ) return 0;
        const first = rU16( d, b ), nG = rU16( d, b + 2 );
        if ( g < first || g >= first + nG ) return 0;
        return rU16( d, b + 4 + ( g - first ) * 2 );
      };
      /* The class values are byte offsets from the subtable start, so
       * offsetToIndex(l + r) reduces to reading at subtable + l + r -- but an
       * offset BEFORE the kerning array is out of range, and the reference
       * then yields no kerning rather than a stray value. */
      const off = cls( leftOff, left ) + cls( rightOff, right );
      if ( off < arrayOff ) return 0;
      const q = st.offset + off;
      if ( q + 2 > st.end || q + 1 >= d.length ) return 0;
      return rI16( d, q );
    }
    return 0;     /* formats 1 and 3 are AAT state machines */
  }

  /* -- VORG: the authoritative vertical origin when the table is present -- */
  let vorg = undefined;
  function readVorg() {
    vorg = null;
    const t = face.tables.get( 'VORG' );
    if ( !t ) return;
    const o = t.offset;
    if ( rU16( d, o ) !== 1 ) return;               /* majorVersion */
    const def = rI16( d, o + 4 );
    const n = rU16( d, o + 6 );
    vorg = { def, n, base: o + 8 };
  }
  function vorgYOrigin( gid ) {
    if ( vorg === undefined ) readVorg();
    if ( !vorg ) return null;
    let lo = 0, hi = vorg.n - 1;
    while ( lo <= hi ) {
      const m = ( lo + hi ) >> 1;
      const g = rU16( d, vorg.base + m * 4 );
      if ( gid < g ) hi = m - 1;
      else if ( gid > g ) lo = m + 1;
      else return rI16( d, vorg.base + m * 4 + 2 );
    }
    return vorg.def;
  }

  /* -- glyph names ---------------------------------------------------- */
  let postNames = null;
  function buildPostNames() {
    postNames = new Map();
    const pt = face.tables.get( 'post' );
    if ( !pt ) return;
    const o = pt.offset;
    const ver = rU32( d, o );
    if ( ver === 0x00010000 ) {
      for ( let i = 0; i < 258; i++ ) postNames.set( i, MAC_GLYPH_NAMES[ i ] );
      return;
    }
    if ( ver !== 0x00020000 ) return;              /* 2.5/3.0/4.0: no names */
    const num = rU16( d, o + 32 );
    const idx = [];
    for ( let i = 0; i < num; i++ ) idx.push( rU16( d, o + 34 + i * 2 ) );
    /* Pascal strings follow. */
    const strings = [];
    let p = o + 34 + num * 2;
    const end = o + pt.length;
    while ( p < end ) {
      const l = d[ p ];
      if ( p + 1 + l > end ) break;
      strings.push( String.fromCharCode.apply( null, d.subarray( p + 1, p + 1 + l ) ) );
      p += 1 + l;
    }
    for ( let i = 0; i < num; i++ ) {
      const v = idx[ i ];
      if ( v < 258 ) postNames.set( i, MAC_GLYPH_NAMES[ v ] );
      else if ( v - 258 < strings.length ) postNames.set( i, strings[ v - 258 ] );
    }
  }
  /* hb_ot_get_glyph_name falls back to the CFF charset when `post' has no
   * name.  gid -> SID via charset.sids; SID < 391 is a standard string, the
   * rest index the font's string INDEX.  CID-keyed fonts have no names. */
  let cffNames = null;
  function buildCffNames() {
    cffNames = new Map();
    if ( !face.isCFF || !CFFMOD || !CFFMOD.getCffFont ) return;
    let cff = null;
    try { cff = CFFMOD.getCffFont( face ); } catch { return; }
    if ( !cff || !cff.charset || !cff.charset.sids ) return;
    const fd = cff.top_font && cff.top_font.font_dict;
    if ( fd && fd.cid_registry !== undefined && fd.cid_registry !== 0xFFFF ) return;
    const si = cff.string_index, dd = cff.data;
    const readOff = ( k ) => {
      let v = 0;
      const q = si.start + si.hdrSize + k * si.offSize;
      for ( let j = 0; j < si.offSize; j++ ) v = ( v * 256 ) + dd[ q + j ];
      return v;
    };
    /* INDEX offsets are 1-based from dataOffset - 1 (CFF spec, INDEX Data). */
    const str = ( i ) => {
      if ( !si || !si.ok || i < 0 || i >= si.count ) return null;
      const a = si.dataOffset + readOff( i ) - 1;
      const b = si.dataOffset + readOff( i + 1 ) - 1;
      if ( a < 0 || b > dd.length || b < a ) return null;
      let out = '';
      for ( let k = a; k < b; k++ ) out += String.fromCharCode( dd[ k ] );
      return out;
    };
    const sids = cff.charset.sids;
    for ( let g = 0; g < sids.length; g++ ) {
      const sid = sids[ g ];
      const nm = sid < 391 ? CFF_STD_STRINGS[ sid ] : str( sid - 391 );
      if ( nm ) cffNames.set( g, nm );
    }
  }
  function getGlyphName( gid ) {
    if ( !postNames ) buildPostNames();
    const n = postNames.get( gid );
    if ( n !== undefined && n !== '' ) return n;
    if ( !cffNames ) buildCffNames();
    const c = cffNames.get( gid );
    if ( c !== undefined && c !== '' ) return c;
    return null;
  }
  /* hb_font_t::glyph_to_string -- "gidDDD" when there is no name. */
  function glyphToString( gid ) {
    const n = getGlyphName( gid );
    return n !== null ? n : 'gid' + gid;
  }

  /* -- extents (needs outlines; guarded so a missing module cannot crash) */
  /* hb_ot_get_glyph_extents: for `glyf` the stored per-glyph header bbox is
   * authoritative; an EMPTY glyph (loca length 0) yields all-zero extents and
   * still counts as success -- that distinction decides the vertical origin. */
  function getGlyphExtentsUnscaled( gid ) {
    /* With a variation active the static header bbox is stale. */
    if ( !face.isCFF && hasGlyf && isVaried() ) return variedExtentsUnscaled( gid );
    if ( face.isCFF ) {
      if ( !CFFMOD || !CFFMOD.loadCffOutline ) return null;
      try {
        const r = CFFMOD.loadCffOutline( face, gid );
        const o = r && r.outline;
        if ( !o || !o.n_points )
          return { x_bearing: 0, y_bearing: 0, width: 0, height: 0 };
        let xmin = Infinity, ymin = Infinity, xmax = -Infinity, ymax = -Infinity;
        for ( let i = 0; i < o.n_points; i++ ) {
          const X = o.x[ i ], Y = o.y[ i ];
          if ( X < xmin ) xmin = X; if ( X > xmax ) xmax = X;
          if ( Y < ymin ) ymin = Y; if ( Y > ymax ) ymax = Y;
        }
        return { x_bearing: xmin, y_bearing: ymax,
                 width: xmax - xmin, height: ymin - ymax };
      } catch { return null; }
    }
    let loc = null;
    try { loc = face.loca( gid ); } catch { return null; }
    /* A face with no `glyf` has no glyph outlines at all, whatever `loca`
     * happens to contain: the reference's glyf accelerator reports zero
     * glyphs and the extents query FAILS.  A CBDT/CBLC-only face reached
     * this path and invented a bounding box from stale offsets. */
    if ( !loc || ( !hasGlyf && !process.env.W20_NO_EXTGLYF ) ) return null;
    if ( !loc.length ) return { x_bearing: 0, y_bearing: 0, width: 0, height: 0 };
    const o = face.glyfOffset + loc.offset;
    if ( o + 10 > d.length ) return null;
    const xMin = rI16( d, o + 2 ), yMin = rI16( d, o + 4 );
    const xMax = rI16( d, o + 6 ), yMax = rI16( d, o + 8 );
    return { x_bearing: xMin, y_bearing: yMax,
             width: xMax - xMin, height: yMin - yMax };
  }
  /* -- colour extents ---------------------------------------------------
   * hb_ot_get_glyph_extents resolves a glyph's box in the order
   *   sbix -> CBDT -> COLR -> glyf -> CFF
   * so on a colour face the ink box is NOT the outline bounding box: a COLR
   * glyph's own outline is usually empty and the artwork lives in the paint
   * graph or in a bitmap strike.  COLR::get_extents takes the ClipBox when the
   * glyph has one and OTHERWISE TRAVERSES THE PAINT GRAPH, unioning what each
   * layer covers -- the only thing that works for a v0 face, which has no
   * ClipList at all.  Extents feed mark attachment and vertical origins, so
   * this is what makes a colour glyph position like the reference's.
   *
   * The tables are parsed by ./colr.js, the module bin/hb-vector.js paints
   * through, so there is one COLR parser in the tree and not two.
   *
   * Kill switch: S3_NO_COLOR_EXTENTS=1 restores the outline-only box. */
  const noColorExtents = !!process.env.S3_NO_COLOR_EXTENTS;

  /* hb_paint_extents_push_clip_glyph runs the draw funcs and takes the hull of
   * every point it is handed, CONTROL points included, so handing it the raw
   * outline points gives the identical box: the on-curve midpoints glyf
   * implies between two off-curve points are convex combinations of those two
   * and cannot widen the hull. */
  function colorDrawGlyph( gid, sink ) {
    if ( hasGlyf && isVaried() ) {
      const p = variedPoints( gid );
      for ( let i = 0; i < p.n; i++ )
        sink.moveTo( Math.fround( p.X[ i ] * xMultF ), Math.fround( p.Y[ i ] * yMultF ) );
      return;
    }
    let o = null;
    try {
      const r = face.isCFF
        ? ( CFFMOD && CFFMOD.loadCffOutline ? CFFMOD.loadCffOutline( face, gid ) : null )
        : ( GLYFMOD && GLYFMOD.loadGlyfOutline ? GLYFMOD.loadGlyfOutline( face, gid ) : null );
      o = r && r.outline;
    } catch { return; }
    if ( !o || !o.n_points ) return;
    for ( let i = 0; i < o.n_points; i++ )
      sink.moveTo( Math.fround( o.x[ i ] * xMultF ), Math.fround( o.y[ i ] * yMultF ) );
  }

  let colorCtx = undefined;
  function colorContext() {
    if ( colorCtx !== undefined ) return colorCtx;
    colorCtx = null;
    /* colr.js scales both axes by the single factor its callers use; under an
     * anisotropic --font-size leave the existing outline box alone rather than
     * report a box scaled on one axis. */
    if ( noColorExtents || !COLRMOD || xMultF !== yMultF ) return colorCtx;
    try {
      const colr = COLRMOD.colrInit( face );
      const sbix = COLRMOD.sbixInit( face );
      const cbdt = COLRMOD.cbdtInit( face );
      if ( !colr && !sbix && !cbdt ) return colorCtx;
      const cc = hasNonzeroCoords && coords ? Array.from( coords ) : [];
      colorCtx = {
        face, colr, sbix, cbdt,
        cpal: COLRMOD.cpalInit( face ),
        inst: colr ? COLRMOD.makeInstancer( face, colr.varStore, colr.varIdxMap, cc )
                   : ( () => 0 ),
        /* the extents funcs never read a colour; HarfBuzz passes HB_COLOR(0,0,0,0) */
        palette: 0, foreground: { r: 0, g: 0, b: 0, a: 0 },
        mult: xMultF, fwdScale: xMultF, invScale: Math.fround( 1 / xMultF ),
        paint: { drawGlyphFn: colorDrawGlyph, customPaletteColor: () => null },
        depthLeft: COLRMOD.HB_MAX_NESTING, edgeCount: COLRMOD.COLRV1_MAX_EDGES,
        layerPath: new Set(), glyphPath: new Set(),
        /* null: falling back to glyf/CFF is this module's own business, below */
        outlineExtentsFn: () => null,
      };
    } catch { colorCtx = null; }
    return colorCtx;
  }
  /* A v0 face re-walks its layer outlines per lookup; memoize per glyph. */
  const colorExtentsCache = new Map();
  function colorExtents( gid ) {
    const cx = colorContext();
    if ( !cx ) return null;
    if ( colorExtentsCache.has( gid ) ) return colorExtentsCache.get( gid );
    let e = null;
    try { e = COLRMOD.colorGlyphExtents( face, gid, xMultF, cx ); } catch { e = null; }
    colorExtentsCache.set( gid, e );
    return e;
  }

  function getGlyphExtents( gid ) {
    /* colour tables come FIRST, and already in scaled units */
    const ce = colorExtents( gid );
    if ( ce ) return emboldenExtents( { ...ce } );
    const e = getGlyphExtentsUnscaled( gid );
    if ( !e ) return null;
    if ( xScale === upem && yScale === upem ) return emboldenExtents( e );
    /* hb_font_t::scale_glyph_extents: floor the bearings, ceil the far edge,
     * all in float32 (em_fscale), then take the difference. */
    const x1 = Math.fround( Math.fround( e.x_bearing ) * xMultF );
    const y1 = Math.fround( Math.fround( e.y_bearing ) * yMultF );
    const x2 = Math.fround( Math.fround( e.x_bearing + e.width ) * xMultF );
    const y2 = Math.fround( Math.fround( e.y_bearing + e.height ) * yMultF );
    const xb = Math.floor( x1 ), yb = Math.floor( y1 );
    return emboldenExtents( { x_bearing: xb, y_bearing: yb,
                              width: Math.ceil( x2 ) - xb, height: Math.ceil( y2 ) - yb } );
  }
  /* hb_font_t::synthetic_glyph_extents */
  function emboldenExtents( e ) {
    if ( !xStrength && !yStrength ) return e;
    const yShift = yScale < 0 ? -yStrength : yStrength;
    e.y_bearing += yShift;
    e.height -= yShift;
    const xShift = xScale < 0 ? -xStrength : xStrength;
    if ( emboldenInPlace ) e.x_bearing -= ( xShift / 2 ) | 0;
    e.width += xShift;
    return e;
  }

  /* GPOS anchor format 2 names a contour point; without this ot-layout.js
   * falls back to the design coordinates, as the reference does for CFF. */
  function getGlyphContourPoint( gid, pointIndex ) {
    if ( face.isCFF || !GLYFMOD || !GLYFMOD.loadGlyfOutline ) return null;
    try {
      const r = GLYFMOD.loadGlyfOutline( face, gid );
      const o = r && r.outline;
      if ( !o || pointIndex < 0 || pointIndex >= o.n_points ) return null;
      return { x: o.x[ pointIndex ], y: o.y[ pointIndex ] };
    } catch { return null; }
  }

  return {
    face, upem, xScale, yScale, getGlyphContourPoint,
    coords, hasNonzeroCoords, vorgYOrigin, kernSubtables, kernValue,
    xStrength, yStrength, emboldenInPlace,
    glyphPhantomPoints, hasGlyf, hasVmtx,
    xPpem: opts.xPpem | 0, yPpem: opts.yPpem | 0, ptem: opts.ptem || 0,
    emScaleX, emScaleY,
    getNominalGlyph, getVariationGlyph,
    getGlyphHAdvance, getGlyphVAdvance,
    getFontHExtents, getGlyphExtents,
    getGlyphName, glyphToString,
    hasGlyph : ( cp ) => getNominalGlyph( cp ) !== 0,
  };
}

/* ======================================================================
 *  S1 adapter (ot-layout.js: GSUB/GPOS/GDEF).
 *  Contract (CONTRACT-SHAPE.md): loadLayout(face) -> tables;
 *  applyLookups(plan, buffer, font).  We additionally look for the finer
 *  entry points below and degrade gracefully when they are absent, so the
 *  no-layout families score before S1 lands.
 * ==================================================================== */
let S1 = null;
try { S1 = await import( './ot-layout.js' ); } catch { S1 = null; }

/* HB_OT_LAYOUT_GLYPH_PROPS_* */
export const GLYPH_PROPS_BASE_GLYPH = 0x02;
export const GLYPH_PROPS_LIGATURE   = 0x04;
export const GLYPH_PROPS_MARK       = 0x08;

/* unicode_props bit layout (hb-ot-layout.hh) */
const UPROPS_MASK_GEN_CAT      = 0x001F;
const UPROPS_MASK_IGNORABLE    = 0x0020;
const UPROPS_MASK_HIDDEN       = 0x0040;
const UPROPS_MASK_CONTINUATION = 0x0080;
const UPROPS_MASK_Cf_ZWJ       = 0x0100;
const UPROPS_MASK_Cf_ZWNJ      = 0x0200;
const UPROPS_MASK_Cf_VS        = 0x0400;

const infoGenCat        = ( i ) => i.unicodeProps & UPROPS_MASK_GEN_CAT;
const infoIsMark        = ( i ) => GC_IS_MARK( infoGenCat( i ) );
/* hb_glyph_info_t::is_default_ignorable is NOT just the Unicode property: the
 * reference also requires that no GSUB lookup has SUBSTITUTED the glyph.
 *
 *   return (info->unicode_props() & UPROPS_MASK_IGNORABLE) &&
 *          !_hb_glyph_info_substituted (info);
 *
 * A font that substitutes the ZWJ glyph for a real one (`terminal' in
 * in-house/default-ignorables.tests:3) has turned the ignorable into content,
 * and it must neither be hidden behind `space' nor skipped by later lookups.
 * Measured: without the second half, U+0647 U+200D shaped to
 * [space|uni0647.init] where the reference gives [terminal|uni0647.init]. */
const GLYPH_PROPS_SUBSTITUTED = 0x10;
const infoIsIgnorable   = ( i ) => !!( i.unicodeProps & UPROPS_MASK_IGNORABLE ) &&
                                   !( i.props & GLYPH_PROPS_SUBSTITUTED );
const infoIsHidden      = ( i ) => !!( i.unicodeProps & UPROPS_MASK_HIDDEN );
const infoIsContinuation= ( i ) => !!( i.unicodeProps & UPROPS_MASK_CONTINUATION );
const infoIsVS          = ( i ) => infoGenCat( i ) === GC.Cf &&
                                   !!( i.unicodeProps & UPROPS_MASK_Cf_VS );
const infoCC            = ( i ) => infoIsMark( i ) ? ( i.unicodeProps >> 8 ) : 0;
const infoIsGlyphMark   = ( i ) => !!( i.props & GLYPH_PROPS_MARK );

function setUnicodePropsOne( info, buffer ) {
  const u = info.codepoint;
  const gen = generalCategory( u );
  let props = gen;
  if ( u >= 0x80 ) {
    if ( isDefaultIgnorable( u ) ) {
      buffer.scratchFlags |= SCRATCH_HAS_DEFAULT_IGNORABLES;
      props |= UPROPS_MASK_IGNORABLE;
      if ( u === 0x200C ) props |= UPROPS_MASK_Cf_ZWNJ;
      else if ( u === 0x200D ) props |= UPROPS_MASK_Cf_ZWJ;
      else if ( ( u >= 0x180B && u <= 0x180D ) || u === 0x180F ) props |= UPROPS_MASK_HIDDEN;
      else if ( u >= 0xE0020 && u <= 0xE007F ) props |= UPROPS_MASK_HIDDEN;
      else if ( u === 0x034F ) {
        buffer.scratchFlags |= SCRATCH_HAS_CGJ;
        props |= UPROPS_MASK_HIDDEN;
      }
    }
    if ( GC_IS_MARK( gen ) ) {
      buffer.scratchFlags |= SCRATCH_HAS_CONTINUATIONS;
      props |= UPROPS_MASK_CONTINUATION;
      props |= modifiedCombiningClass( u ) << 8;
    }
  }
  if ( gen === GC.Cf && isVariationSelector( u ) ) props |= UPROPS_MASK_Cf_VS;
  info.unicodeProps = props;
  info.ignorable = !!( props & UPROPS_MASK_IGNORABLE );
  info.zwnj = !!( props & UPROPS_MASK_Cf_ZWNJ );
  info.zwj  = !!( props & UPROPS_MASK_Cf_ZWJ );
}

/* emoji-data.txt Extended_Pictographic, via S2's unicode.js. */
function isExtendedPictographic( cp ) {
  return !!( S2 && S2.isExtendedPictographic && S2.isExtendedPictographic( cp ) );
}

/* hb_set_unicode_props */
function setUnicodeProps( buffer ) {
  const info = buffer.info;
  for ( let i = 0; i < buffer.len; i++ ) {
    setUnicodePropsOne( info[ i ], buffer );
    const u = info[ i ].codepoint;
    if ( u < 0x80 ) continue;
    const gen = infoGenCat( info[ i ] );
    if ( gen === GC.Ll || gen === GC.Lu || gen === GC.Lt || gen === GC.Lo || gen === GC.Zs )
      continue;
    if ( gen === GC.Sk && u >= 0x1F3FB && u <= 0x1F3FF ) {
      info[ i ].unicodeProps |= UPROPS_MASK_CONTINUATION;
      buffer.scratchFlags |= SCRATCH_HAS_CONTINUATIONS;
    } else if ( i && u >= 0x1F1E6 && u <= 0x1F1FF ) {
      const p = info[ i - 1 ].codepoint;
      if ( p >= 0x1F1E6 && p <= 0x1F1FF && !infoIsContinuation( info[ i - 1 ] ) ) {
        info[ i ].unicodeProps |= UPROPS_MASK_CONTINUATION;
        buffer.scratchFlags |= SCRATCH_HAS_CONTINUATIONS;
      }
    } else if ( info[ i ].unicodeProps & UPROPS_MASK_Cf_ZWJ ) {
      info[ i ].unicodeProps |= UPROPS_MASK_CONTINUATION;
      buffer.scratchFlags |= SCRATCH_HAS_CONTINUATIONS;
      /* ZWJ,Extended_Pictographic keeps the grapheme together: the emoji
       * AFTER the ZWJ is also a continuation, and hb_set_unicode_props
       * advances past it so it is not reconsidered by the next iteration. */
      if ( !process.env.S3_NO_ZWJ_EXTPICT && i + 1 < buffer.len &&
           isExtendedPictographic( info[ i + 1 ].codepoint ) ) {
        i++;
        setUnicodePropsOne( info[ i ], buffer );
        info[ i ].unicodeProps |= UPROPS_MASK_CONTINUATION;
        buffer.scratchFlags |= SCRATCH_HAS_CONTINUATIONS;
      }
    } else if ( ( u >= 0xFF9E && u <= 0xFF9F ) || ( u >= 0xE0020 && u <= 0xE007F ) ) {
      info[ i ].unicodeProps |= UPROPS_MASK_CONTINUATION;
      buffer.scratchFlags |= SCRATCH_HAS_CONTINUATIONS;
    } else if ( u === 0x2044 ) {
      buffer.scratchFlags |= SCRATCH_HAS_FRACTION_SLASH;
    }
  }
}

/* hb_form_clusters */
function formClusters( buffer ) {
  if ( !( buffer.scratchFlags & SCRATCH_HAS_CONTINUATIONS ) ) return;
  for ( const [ s, e ] of buffer.graphemes( infoIsContinuation ) )
    buffer.mergeGraphemeClusters( s, e );
}

/* hb_ensure_native_direction */
function ensureNativeDirection( buffer ) {
  const direction = buffer.props.direction;
  let horizDir = scriptHorizontalDirection( buffer.props.script );
  if ( horizDir === DIRECTION_RTL && direction === DIRECTION_LTR ) {
    let foundNumber = false, foundLetter = false, foundRI = false;
    for ( let i = 0; i < buffer.len; i++ ) {
      const gc = infoGenCat( buffer.info[ i ] );
      if ( GC_IS_LETTER( gc ) ) { foundLetter = true; break; }
      else if ( gc === GC.Nd ) foundNumber = true;
      else {
        const u = buffer.info[ i ].codepoint;
        if ( u >= 0x1F1E6 && u <= 0x1F1FF ) foundRI = true;
      }
    }
    if ( ( foundNumber || foundRI ) && !foundLetter ) horizDir = DIRECTION_LTR;
  }
  if ( ( isHorizontal( direction ) && direction !== horizDir &&
         horizDir !== DIRECTION_INVALID ) ||
       ( isVertical( direction ) && direction !== DIRECTION_TTB ) ) {
    /* _hb_ot_layout_reverse_graphemes */
    buffer.reverseGroups( ( a, b ) => infoIsContinuation( b ),
                          buffer.clusterLevel === CLUSTER_LEVEL_MONOTONE_CHARACTERS );
    buffer.props.direction = reverseDirection( buffer.props.direction );
  }
}

/* hb_ot_rotate_chars: mirroring for RTL, vertical forms for TTB/BTT */
function rotateChars( plan, buffer, font, targetDirection ) {
  const count = buffer.len, info = buffer.info;
  if ( isBackward( targetDirection ) ) {
    const rtlm = plan.rtlmMask >>> 0;
    for ( let i = 0; i < count; i++ ) {
      const m = mirroring( info[ i ].codepoint );
      if ( m !== info[ i ].codepoint && font.hasGlyph( m ) ) info[ i ].codepoint = m;
      else info[ i ].mask = ( info[ i ].mask | rtlm ) >>> 0;
    }
  }
  if ( isVertical( targetDirection ) && !plan.hasVert ) {
    for ( let i = 0; i < count; i++ ) {
      const v = verticalCharFor( info[ i ].codepoint );
      if ( v !== info[ i ].codepoint && font.hasGlyph( v ) ) info[ i ].codepoint = v;
    }
  }
}
/* TODO: BidiMirroring.txt (ucd/) -- only affects RTL punctuation. */
function mirroring( cp ) { return S2 && S2.mirroring ? S2.mirroring( cp ) : cp; }
/* hb_unicode_funcs_t::vertical_char_for -- the vertical presentation form of
 * a punctuation character.  Applied to a vertical run only when the font has
 * no `vert' feature of its own to do the job. */
const VERTICAL_CHAR = new Map( [
  [ 0x2013, 0xFE32 ], [ 0x2014, 0xFE31 ], [ 0x2025, 0xFE30 ], [ 0x2026, 0xFE19 ],
  [ 0x3001, 0xFE11 ], [ 0x3002, 0xFE12 ], [ 0x3008, 0xFE3F ], [ 0x3009, 0xFE40 ],
  [ 0x300A, 0xFE3D ], [ 0x300B, 0xFE3E ], [ 0x300C, 0xFE41 ], [ 0x300D, 0xFE42 ],
  [ 0x300E, 0xFE43 ], [ 0x300F, 0xFE44 ], [ 0x3010, 0xFE3B ], [ 0x3011, 0xFE3C ],
  [ 0x3014, 0xFE39 ], [ 0x3015, 0xFE3A ], [ 0x3016, 0xFE17 ], [ 0x3017, 0xFE18 ],
  [ 0xFE4F, 0xFE34 ],
  [ 0xFF01, 0xFE15 ], [ 0xFF08, 0xFE35 ], [ 0xFF09, 0xFE36 ], [ 0xFF0C, 0xFE10 ],
  [ 0xFF1A, 0xFE13 ], [ 0xFF1B, 0xFE14 ], [ 0xFF1F, 0xFE16 ], [ 0xFF3B, 0xFE47 ],
  [ 0xFF3D, 0xFE48 ], [ 0xFF3F, 0xFE33 ], [ 0xFF5B, 0xFE37 ], [ 0xFF5D, 0xFE38 ],
] );
function verticalCharFor( cp ) {
  if ( S2 && S2.verticalCharFor ) return S2.verticalCharFor( cp );
  const v = VERTICAL_CHAR.get( cp );
  return v === undefined ? cp : v;
}

/* ---- normalization (S2 owns normalize.js) --------------------------- */
function nextChar( buffer, glyph ) {
  buffer.cur().normalizerGlyphIndex = glyph;
  buffer.nextGlyph();
}
function outputChar( buffer, unichar, glyph ) {
  buffer.cur().normalizerGlyphIndex = glyph;
  buffer.outputGlyph( unichar );
  setUnicodePropsOne( buffer.prev(), buffer );
}
let ACTIVE_SHAPER = null;     /* set by shape() for the normalizer hooks */
function decomposeUnicode( ab ) {
  if ( ACTIVE_SHAPER && ACTIVE_SHAPER.decompose ) {
    const r = ACTIVE_SHAPER.decompose( ACTIVE_SHAPER, ab );
    /* hb-ot-shape-normalize.cc takes the shaper's decompose INSTEAD of the
     * Unicode one, so a shaper that answers `false` means the character has
     * NO decomposition and must not fall through to the canonical one.  The
     * Indic shaper answers that for the four letters whose canonical
     * decomposition is a different letter plus a nukta. */
    if ( r === false ) return null;
    if ( r ) return r;
  }
  /* normalize.js folds Hangul into the canonical decomposition. */
  if ( S2NORM && S2NORM.decomposeCanonical ) return S2NORM.decomposeCanonical( ab );
  if ( S2 && S2.canonicalDecompose ) return S2.canonicalDecompose( ab );
  return null;
}
function composeUnicode( a, b, script ) {
  /* hb-ot-shape-normalize.cc:305 takes `plan->shaper->compose' INSTEAD of the
   * Unicode one when the shaper defines it, so a shaper's REFUSAL is
   * authoritative and must not fall through to canonical composition. */
  if ( ACTIVE_SHAPER && ACTIVE_SHAPER.compose )
    return ACTIVE_SHAPER.compose( ACTIVE_SHAPER, a, b ) || 0;

  /* compose_indic, compose_khmer and compose_use are character-for-character
   * identical: "Avoid recomposing split matras" -- refuse whenever `a' is a
   * mark, so a two- or three-part matra stays DECOMPOSED for reordering.
   * Without this, canonical composition puts U+0CCA + U+0CD5 back together as
   * U+0CCB and the split parts never reach the reorderer. */
  const sname = ACTIVE_SHAPER ? ACTIVE_SHAPER.name : null;
  if ( !process.env.S3_NO_SPLIT_MATRA &&
       ( sname === 'indic' || sname === 'khmer' || sname === 'use' ) ) {
    if ( GC_IS_MARK( generalCategory( a ) ) ) return 0;
    /* compose_indic's single composition-exclusion pair it does want back. */
    if ( sname === 'indic' && a === 0x09AF && b === 0x09BC ) return 0x09DF;
  }
  /* The Hebrew shaper composes the FB1D..FB4E presentation forms even though
   * Full_Composition_Exclusion lists them. */
  if ( script === 'Hebr' && S2NORM && S2NORM.composeHebrew ) {
    const h = S2NORM.composeHebrew( a, b );
    if ( h ) return h;
  }
  if ( S2NORM && S2NORM.composeCanonical ) return S2NORM.composeCanonical( a, b );
  return 0;
}
function haveCompose() { return !!( S2NORM && S2NORM.composeCanonical ); }
function decomposeRec( buffer, font, shortest, ab ) {
  const dec = decomposeUnicode( ab );
  if ( !dec ) return false;
  const a = dec[ 0 ], b = dec[ 1 ] || 0;
  let bGlyph = 0;
  if ( b ) { bGlyph = font.getNominalGlyph( b ); if ( !bGlyph ) return false; }
  const aGlyph = font.getNominalGlyph( a );
  const hasA = aGlyph !== 0;
  if ( shortest && hasA ) {
    outputChar( buffer, a, aGlyph );
    if ( b ) outputChar( buffer, b, bGlyph );
    return true;
  }
  if ( decomposeRec( buffer, font, shortest, a ) ) {
    if ( b ) outputChar( buffer, b, bGlyph );
    return true;
  }
  if ( hasA ) {
    outputChar( buffer, a, aGlyph );
    if ( b ) outputChar( buffer, b, bGlyph );
    return true;
  }
  return false;
}
function decomposeCurrentCharacter( buffer, font, shortest ) {
  const u = buffer.cur().codepoint;
  let glyph = 0;
  if ( shortest && ( glyph = font.getNominalGlyph( u ) ) ) { nextChar( buffer, glyph ); return; }
  if ( decomposeRec( buffer, font, shortest, u ) ) { buffer.skipGlyph(); return; }
  if ( !shortest && ( glyph = font.getNominalGlyph( u ) ) ) { nextChar( buffer, glyph ); return; }
  if ( infoGenCat( buffer.cur() ) === GC.Zs ) {
    const spaceType = spaceFallbackType( u );
    let spaceGlyph = font.getNominalGlyph( 0x0020 );
    if ( spaceType !== SPACE_NOT_SPACE && ( spaceGlyph || ( spaceGlyph = buffer.invisible ) ) ) {
      buffer.cur().spaceFallbackType = spaceType;
      nextChar( buffer, spaceGlyph );
      buffer.scratchFlags |= SCRATCH_HAS_SPACE_FALLBACK;
      return;
    }
  }
  if ( u === 0x2011 ) {
    const other = font.getNominalGlyph( 0x2010 );
    if ( other ) { nextChar( buffer, other ); return; }
  }
  nextChar( buffer, glyph );
}
function handleVariationSelectorCluster( buffer, font, end ) {
  const setGlyph = ( inf ) => {
    inf.normalizerGlyphIndex = font.getNominalGlyph( inf.codepoint ) || 0;
  };
  while ( buffer.idx < end - 1 && buffer.successful ) {
    if ( isVariationSelector( buffer.cur( 1 ).codepoint ) ) {
      const r = font.getVariationGlyph( buffer.cur().codepoint, buffer.cur( 1 ).codepoint );
      if ( r.ok ) {
        buffer.cur().normalizerGlyphIndex = r.glyph;
        const unicode = buffer.cur().codepoint;
        buffer.replaceGlyphs( 2, 1, [ unicode ] );
        if ( buffer.outLen ) buffer.out[ buffer.outLen - 1 ].normalizerGlyphIndex = r.glyph;
      } else {
        /* Pass the two characters on separately and let GSUB do its magic. */
        setGlyph( buffer.cur() );
        buffer.nextGlyph();
        buffer.scratchFlags |= SCRATCH_HAS_VARIATION_SELECTOR_FALLBACK;
        const vs = buffer.cur();
        /* set_variation_selector(true): general category becomes Cf and the
         * Cf_VS bit is set; set_general_category clears the top byte. */
        vs.unicodeProps = ( GC.Cf |
          ( vs.unicodeProps & ( 0xFF & ~UPROPS_MASK_GEN_CAT ) ) |
          UPROPS_MASK_Cf_VS ) >>> 0;
        if ( buffer.notFoundVariationSelector !== -1 ) {
          /* clear_default_ignorable: with a replacement glyph requested the
           * selector must survive GSUB as a real, non-skippable glyph -- that
           * is what stops an f+i ligature forming straight across it. */
          vs.unicodeProps = ( vs.unicodeProps & ~UPROPS_MASK_IGNORABLE ) >>> 0;
          vs.ignorable = false;
        }
        setGlyph( vs );
        buffer.nextGlyph();
      }
      /* Skip any further variation selectors. */
      while ( buffer.idx < end && buffer.successful &&
              isVariationSelector( buffer.cur().codepoint ) ) {
        setGlyph( buffer.cur() );
        buffer.nextGlyph();
      }
    } else {
      setGlyph( buffer.cur() );
      buffer.nextGlyph();
    }
  }
  if ( buffer.idx < end ) {
    setGlyph( buffer.cur() );
    buffer.nextGlyph();
  }
}

function normalize( plan, buffer, font ) {
  if ( !buffer.len ) return;
  ACTIVE_SHAPER = plan.shaper || null;
  const scriptName = tagToString( plan.props.script );
  /* default shaper: NORMALIZATION_MODE_AUTO resolves to COMPOSED_DIACRITICS,
   * so might_short_circuit is true and always_short_circuit is false. */
  /* hb-ot-shape-normalize.cc:309-312.  The four syllabic shapers ask for
   * COMPOSED_DIACRITICS_NO_SHORT_CIRCUIT, which turns the fast path OFF: a
   * precomposed character the font has a glyph for must still be decomposed,
   * because it is the DECOMPOSED form the cluster grammars and the reorderers
   * are written against.  Khmer's two-part matras are the clearest case -- the
   * shaper supplies the decomposition U+17C4 -> U+17C1, U+17C4 itself, and
   * short-circuiting means the left half never exists and never reorders. */
  const noShortCircuit = !process.env.W19_NO_NSC && !!( plan.shaper &&
    ( plan.shaper.normalizationPreference === 'no-short-circuit' ||
      plan.shaper.normalizationPreference === 'decomposed' ) );
  /* NORMALIZATION_MODE_NONE short-circuits everything: the Zawgyi shaper asks
   * for it precisely so that nothing is decomposed or recomposed. */
  const alwaysShortCircuit = !!( plan.shaper &&
    plan.shaper.normalizationPreference === 'none' );
  const mightShortCircuit = alwaysShortCircuit || !noShortCircuit;
  let allSimple = true;

  /* Round 1: decompose */
  buffer.clearOutput();
  let count = buffer.len;
  buffer.idx = 0;
  do {
    let end;
    for ( end = buffer.idx + 1; end < count; end++ )
      if ( infoIsMark( buffer.info[ end ] ) ) break;
    if ( end < count ) end--;            /* leave one base for the marks */
    if ( mightShortCircuit ) {
      /* font->get_nominal_glyphs() returns the count of glyphs it mapped and
       * STOPS at the first code point with no cmap entry; the decompose loop
       * below then handles that one (space fallback, U+2011, notdef). */
      let done = 0;
      const n = end - buffer.idx;
      while ( done < n ) {
        const inf = buffer.info[ buffer.idx + done ];
        const g = font.getNominalGlyph( inf.codepoint );
        if ( !g ) break;
        inf.normalizerGlyphIndex = g;
        done++;
      }
      if ( !buffer.nextGlyphs( done ) ) break;
    }
    while ( buffer.idx < end && buffer.successful )
      decomposeCurrentCharacter( buffer, font, mightShortCircuit );
    if ( buffer.idx === count || !buffer.successful ) break;
    allSimple = false;
    for ( end = buffer.idx + 1; end < count; end++ )
      if ( !infoIsMark( buffer.info[ end ] ) ) break;
    /* decompose_multi_char_cluster */
    let hasVS = false;
    for ( let i = buffer.idx; i < end; i++ )
      if ( isVariationSelector( buffer.info[ i ].codepoint ) ) { hasVS = true; break; }
    if ( hasVS ) handleVariationSelectorCluster( buffer, font, end );
    else while ( buffer.idx < end && buffer.successful )
      decomposeCurrentCharacter( buffer, font, alwaysShortCircuit );
  } while ( buffer.idx < count && buffer.successful );
  buffer.sync();

  /* Round 2: reorder marks by modified combining class (stable) */
  if ( !allSimple ) {
    count = buffer.len;
    const info = buffer.info;
    for ( let i = 0; i < count; i++ ) {
      if ( infoCC( info[ i ] ) === 0 ) continue;
      let end;
      for ( end = i + 1; end < count; end++ )
        if ( infoCC( info[ end ] ) === 0 ) break;
      if ( end - i > 32 ) { i = end; continue; }   /* HB_OT_SHAPE_MAX_COMBINING_MARKS */
      buffer.sort( i, end, ( a, b ) => infoCC( a ) - infoCC( b ) );
      if ( ACTIVE_SHAPER && ACTIVE_SHAPER.reorderMarks )
        ACTIVE_SHAPER.reorderMarks( plan, buffer, i, end );
      i = end - 1;
    }
  }

  /* harfbuzz#554.  A COMBINING GRAPHEME JOINER is HIDDEN, which is what stops
   * a lookup from stepping over it -- but only while it is doing its job.
   * Once canonical ordering has run, a CGJ that prevented NO reordering (the
   * mark after it either starts a new cluster, or already sorts at or after
   * the mark before it) has nothing left to block, and the reference un-hides
   * it so the rest of shaping treats it like any other ignorable.
   *
   * The combining classes read here are the MODIFIED ones, so this has to
   * come after round 2 has renumbered the runs it moved -- and it belongs
   * HERE, in the normalizer, rather than in any one shaper's setup_masks,
   * because it is not script-specific: a Hebrew or Latin run with an idle CGJ
   * needs it exactly as an Arabic one does.
   * W19_NO_CGJUNHIDE=1 turns it off. */
  if ( ( buffer.scratchFlags & SCRATCH_HAS_CGJ ) && !process.env.W19_NO_CGJUNHIDE ) {
    const cgjInfo = buffer.info;
    for ( let i = 1; i + 1 < buffer.len; i++ )
      if ( cgjInfo[ i ].codepoint === 0x034F &&
           ( infoCC( cgjInfo[ i + 1 ] ) === 0 ||
             infoCC( cgjInfo[ i - 1 ] ) <= infoCC( cgjInfo[ i + 1 ] ) ) )
        cgjInfo[ i ].unicodeProps =
          ( cgjInfo[ i ].unicodeProps & ~UPROPS_MASK_HIDDEN ) >>> 0;
  }

  /* Round 3: recompose.  Needs canonical composition (S2). */
  if ( !allSimple && buffer.successful && haveCompose() ) {
    buffer.clearOutput();
    count = buffer.len;
    let starter = 0;
    buffer.nextGlyph();
    while ( buffer.idx < count ) {
      if ( infoIsMark( buffer.cur() ) ) {
        if ( ( starter === buffer.outLen - 1 ||
               infoCC( buffer.prev() ) < infoCC( buffer.cur() ) ) ) {
          const composed = composeUnicode( buffer.out[ starter ].codepoint,
                                           buffer.cur().codepoint, scriptName );
          const glyph = composed ? font.getNominalGlyph( composed ) : 0;
          if ( composed && glyph ) {
            if ( !buffer.nextGlyph() ) break;
            buffer.mergeOutClusters( starter, buffer.outLen );
            buffer.outLen--;
            buffer.out[ starter ].codepoint = composed;
            buffer.out[ starter ].normalizerGlyphIndex = glyph;
            setUnicodePropsOne( buffer.out[ starter ], buffer );
            continue;
          }
        }
      }
      if ( !buffer.nextGlyph() ) break;
      if ( infoCC( buffer.prev() ) === 0 ) starter = buffer.outLen - 1;
    }
    buffer.sync();
  }
}

/* hb_ot_map_glyphs_fast */
function mapGlyphsFast( buffer ) {
  for ( let i = 0; i < buffer.len; i++ )
    buffer.info[ i ].codepoint = buffer.info[ i ].normalizerGlyphIndex >>> 0;
}

/* hb_synthesize_glyph_classes: a default-ignorable is never a mark, so that
 * IGNORE_MARKS lookups do not skip over it. */
export function synthesizeGlyphProps( inf ) {
  return ( infoGenCat( inf ) !== GC.Mn || infoIsIgnorable( inf ) )
    ? GLYPH_PROPS_BASE_GLYPH : GLYPH_PROPS_MARK;
}
/* hb_synthesize_glyph_classes: used when GDEF has no glyph classes. */
function synthesizeGlyphClasses( buffer ) {
  for ( let i = 0; i < buffer.len; i++ ) {
    const inf = buffer.info[ i ];
    inf.props = synthesizeGlyphProps( inf );
  }
}

/* ---- positioning ---------------------------------------------------- */
function positionDefault( plan, buffer, font ) {
  const direction = buffer.props.direction;
  const count = buffer.len, info = buffer.info, pos = buffer.pos;
  if ( isHorizontal( direction ) ) {
    for ( let i = 0; i < count; i++ )
      pos[ i ].xAdvance = font.getGlyphHAdvance( info[ i ].codepoint );
  } else {
    for ( let i = 0; i < count; i++ )
      pos[ i ].yAdvance = font.getGlyphVAdvance( info[ i ].codepoint );
    /* subtract_glyph_v_origins */
    for ( let i = 0; i < count; i++ ) {
      const o = glyphVOrigin( font, info[ i ].codepoint );
      pos[ i ].xOffset = satSub( pos[ i ].xOffset, o.x );
      pos[ i ].yOffset = satSub( pos[ i ].yOffset, o.y );
    }
  }
  if ( buffer.scratchFlags & SCRATCH_HAS_SPACE_FALLBACK )
    fallbackSpaces( plan, buffer, font );
}
/* Synthetic bold moves the origin too, unless it is applied in place. */
function emboldenOrigin( font, o ) {
  if ( font.emboldenInPlace ) return o;
  if ( font.xStrength ) o.x += font.xScale < 0 ? -font.xStrength : font.xStrength;
  if ( font.yStrength ) o.y += font.yScale < 0 ? -font.yStrength : font.yStrength;
  return o;
}
/* hb_ot_get_glyph_v_origins */
function glyphVOrigin( font, gid ) {
  /* x is half the horizontal advance (C integer division, truncating) */
  const adv = font.getGlyphHAdvance( gid );
  const x = ( adv / 2 ) | 0;
  /* hb_ot_get_glyph_v_origins: VORG wins whenever it is present. */
  if ( font.vorgYOrigin ) {
    const v = font.vorgYOrigin( gid );
    if ( v !== null ) return emboldenOrigin( font, { x, y: font.emScaleY( v ) } );
  }
  /* "If and only if vmtx is present and it's a glyf font, use the top
   * phantom point" -- this applies whether or not a variation is active. */
  if ( font.hasVmtx && font.hasGlyf && font.glyphPhantomPoints ) {
    const p = font.glyphPhantomPoints( gid );
    return emboldenOrigin( font, { x, y: font.emScaleY( p.topY ) } );
  }
  const fe = font.getFontHExtents();
  const fontAdvance = fe.ascender - fe.descender;
  const ext = font.getGlyphExtents( gid );
  let y;
  if ( ext ) y = ext.y_bearing + ( ( fontAdvance - -ext.height ) >> 1 );
  else y = fe.ascender;
  return emboldenOrigin( font, { x, y } );
}

/* _hb_ot_shape_fallback_spaces */
function fallbackSpaces( plan, buffer, font ) {
  const info = buffer.info, pos = buffer.pos;
  const horizontal = isHorizontal( buffer.props.direction );
  for ( let i = 0; i < buffer.len; i++ ) {
    const st = info[ i ].spaceFallbackType;
    if ( !st || st === SPACE_NOT_SPACE || st === SPACE ) {
      /* If the font had no ASCII space and the invisible glyph was used,
       * give it a 1/4 EM default advance. */
      if ( buffer.invisible && info[ i ].codepoint === buffer.invisible && st ) {
        if ( horizontal ) pos[ i ].xAdvance = ( font.xScale / 4 ) | 0;
        else pos[ i ].yAdvance = -( ( font.yScale / 4 ) | 0 );
      }
      continue;
    }
    if ( buffer.invisible && info[ i ].codepoint === buffer.invisible ) {
      if ( horizontal ) pos[ i ].xAdvance = ( font.xScale / 4 ) | 0;
      else pos[ i ].yAdvance = -( ( font.yScale / 4 ) | 0 );
    }
    switch ( st ) {
      case SPACE_EM: case SPACE_EM_2: case SPACE_EM_3: case SPACE_EM_4:
      case SPACE_EM_5: case SPACE_EM_6: case SPACE_EM_16:
        /* (scale + type/2) / type -- C integer division on int64 */
        if ( horizontal )
          pos[ i ].xAdvance = Math.trunc( ( font.xScale + ( st >> 1 ) ) / st ) | 0;
        else
          pos[ i ].yAdvance = -Math.trunc( ( font.yScale + ( st >> 1 ) ) / st ) | 0;
        break;
      case SPACE_4_EM_18:
        if ( horizontal ) pos[ i ].xAdvance = Math.trunc( font.xScale * 4 / 18 ) | 0;
        else pos[ i ].yAdvance = -Math.trunc( font.yScale * 4 / 18 ) | 0;
        break;
      case SPACE_FIGURE:
        for ( let u = 0x30; u <= 0x39; u++ ) {
          const g = font.getNominalGlyph( u );
          if ( g ) {
            if ( horizontal ) pos[ i ].xAdvance = font.getGlyphHAdvance( g );
            else pos[ i ].yAdvance = font.getGlyphVAdvance( g );
            break;
          }
        }
        break;
      case SPACE_PUNCTUATION: {
        const g = font.getNominalGlyph( 0x2E ) || font.getNominalGlyph( 0x2C );
        if ( g ) {
          if ( horizontal ) pos[ i ].xAdvance = font.getGlyphHAdvance( g );
          else pos[ i ].yAdvance = font.getGlyphVAdvance( g );
        }
        break;
      }
      case SPACE_NARROW:
        /* Half of the space width. */
        if ( horizontal ) pos[ i ].xAdvance = ( pos[ i ].xAdvance / 2 ) | 0;
        else pos[ i ].yAdvance = ( pos[ i ].yAdvance / 2 ) | 0;
        break;
      default: break;
    }
  }
}

function zeroMarkWidthsByGdef( buffer, adjustOffsets ) {
  for ( let i = 0; i < buffer.len; i++ )
    if ( infoIsGlyphMark( buffer.info[ i ] ) ) {
      const p = buffer.pos[ i ];
      if ( adjustOffsets ) {
        p.xOffset = satSub( p.xOffset, p.xAdvance );
        p.yOffset = satSub( p.yOffset, p.yAdvance );
      }
      p.xAdvance = 0; p.yAdvance = 0;
    }
}

/* GPOS::position_finish_offsets -- propagate attachment offsets */
const ATTACH_TYPE_MARK = 1, ATTACH_TYPE_CURSIVE = 2;
function propagateAttachmentOffsets( pos, len, i, direction, nesting = 16 ) {
  const chain = pos[ i ].attachChain;
  const type = pos[ i ].attachType;
  pos[ i ].attachChain = 0;
  const j = i + chain;
  if ( j < 0 || j >= len ) return;
  if ( !nesting ) return;
  if ( pos[ j ].attachChain ) propagateAttachmentOffsets( pos, len, j, direction, nesting - 1 );
  if ( type & ATTACH_TYPE_CURSIVE ) {
    if ( isHorizontal( direction ) ) pos[ i ].yOffset = satAdd( pos[ i ].yOffset, pos[ j ].yOffset );
    else pos[ i ].xOffset = satAdd( pos[ i ].xOffset, pos[ j ].xOffset );
  } else {
    if ( isHorizontal( direction ) ) pos[ i ].xOffset = satAdd( pos[ i ].xOffset, pos[ j ].xOffset );
    else pos[ i ].yOffset = satAdd( pos[ i ].yOffset, pos[ j ].yOffset );
    if ( j < i ) {
      if ( isForward( direction ) )
        for ( let k = j; k < i; k++ ) {
          pos[ i ].xOffset = satSub( pos[ i ].xOffset, pos[ k ].xAdvance );
          pos[ i ].yOffset = satSub( pos[ i ].yOffset, pos[ k ].yAdvance );
        }
      else
        for ( let k = j + 1; k < i + 1; k++ ) {
          pos[ i ].xOffset = satAdd( pos[ i ].xOffset, pos[ k ].xAdvance );
          pos[ i ].yOffset = satAdd( pos[ i ].yOffset, pos[ k ].yAdvance );
        }
    } else {
      if ( isForward( direction ) )
        for ( let k = i; k < j; k++ ) {
          pos[ i ].xOffset = satAdd( pos[ i ].xOffset, pos[ k ].xAdvance );
          pos[ i ].yOffset = satAdd( pos[ i ].yOffset, pos[ k ].yAdvance );
        }
      else
        for ( let k = i + 1; k < j + 1; k++ ) {
          pos[ i ].xOffset = satSub( pos[ i ].xOffset, pos[ k ].xAdvance );
          pos[ i ].yOffset = satSub( pos[ i ].yOffset, pos[ k ].yAdvance );
        }
    }
  }
}
function positionFinishOffsets( buffer ) {
  const pos = buffer.pos, len = buffer.len, direction = buffer.props.direction;
  if ( !( buffer.scratchFlags & SCRATCH_HAS_GPOS_ATTACHMENT ) ) return;
  if ( isForward( direction ) ) {
    for ( let i = 0; i < len; i++ ) if ( pos[ i ].attachChain ) propagateAttachmentOffsets( pos, len, i, direction );
  } else {
    for ( let i = len; i-- > 0; ) if ( pos[ i ].attachChain ) propagateAttachmentOffsets( pos, len, i, direction );
  }
}

function zeroWidthDefaultIgnorables( buffer ) {
  if ( !( buffer.scratchFlags & SCRATCH_HAS_DEFAULT_IGNORABLES ) ||
       ( buffer.flags & BUFFER_FLAG_PRESERVE_DEFAULT_IGNORABLES ) ||
       ( buffer.flags & BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES ) ) return;
  const horizontal = isHorizontal( buffer.props.direction );
  for ( let i = 0; i < buffer.len; i++ )
    if ( infoIsIgnorable( buffer.info[ i ] ) ) {
      const p = buffer.pos[ i ];
      p.xAdvance = 0; p.yAdvance = 0;
      if ( horizontal ) p.xOffset = 0; else p.yOffset = 0;
    }
}
function dealWithVariationSelectors( buffer ) {
  if ( !( buffer.scratchFlags & SCRATCH_HAS_VARIATION_SELECTOR_FALLBACK ) ||
       buffer.notFoundVariationSelector === -1 ) return;
  for ( let i = 0; i < buffer.len; i++ )
    if ( infoIsVS( buffer.info[ i ] ) ) {
      buffer.info[ i ].codepoint = buffer.notFoundVariationSelector;
      const p = buffer.pos[ i ];
      p.xAdvance = p.yAdvance = p.xOffset = p.yOffset = 0;
      buffer.info[ i ].unicodeProps &= ~UPROPS_MASK_Cf_VS;
    }
}
function hideDefaultIgnorables( buffer, font ) {
  if ( !( buffer.scratchFlags & SCRATCH_HAS_DEFAULT_IGNORABLES ) ||
       ( buffer.flags & BUFFER_FLAG_PRESERVE_DEFAULT_IGNORABLES ) ) return;
  let invisible = buffer.invisible;
  if ( !( buffer.flags & BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES ) &&
       ( invisible || ( invisible = font.getNominalGlyph( 0x20 ) ) ) ) {
    for ( let i = 0; i < buffer.len; i++ )
      if ( infoIsIgnorable( buffer.info[ i ] ) ) buffer.info[ i ].codepoint = invisible;
  } else {
    buffer.deleteGlyphsInplace( infoIsIgnorable );
  }
}

/* hb_propagate_flags */
function propagateFlags( buffer ) {
  let andMask = GLYPH_FLAG_DEFINED;
  if ( ( buffer.flags & BUFFER_FLAG_PRODUCE_UNSAFE_TO_CONCAT ) === 0 )
    andMask &= ~GLYPH_FLAG_UNSAFE_TO_CONCAT;
  const info = buffer.info;
  if ( ( buffer.flags & BUFFER_FLAG_PRODUCE_SAFE_TO_INSERT_TATWEEL ) === 0 ) {
    for ( const [ start, end ] of buffer.clusters() ) {
      if ( end - start === 1 ) { info[ start ].mask = ( info[ start ].mask & andMask ) >>> 0; continue; }
      let mask = 0;
      for ( let i = start; i < end; i++ ) mask |= info[ i ].mask;
      mask &= andMask;
      for ( let i = start; i < end; i++ ) info[ i ].mask = mask >>> 0;
    }
    return;
  }
  for ( const [ start, end ] of buffer.clusters() ) {
    let mask = 0;
    for ( let i = start; i < end; i++ ) mask |= info[ i ].mask;
    if ( mask & GLYPH_FLAG_UNSAFE_TO_BREAK ) mask &= ~GLYPH_FLAG_SAFE_TO_INSERT_TATWEEL;
    if ( mask & GLYPH_FLAG_SAFE_TO_INSERT_TATWEEL )
      mask |= GLYPH_FLAG_UNSAFE_TO_BREAK | GLYPH_FLAG_UNSAFE_TO_CONCAT;
    mask &= andMask;
    for ( let i = start; i < end; i++ ) info[ i ].mask = mask >>> 0;
  }
}


/* ======================================================================
 *  _hb_ot_shape_fallback_mark_position (hb-ot-shape-fallback.cc)
 *  Used when the font has no GPOS: nothing else will have placed the marks.
 * ==================================================================== */
const CCC_ATTACHED_BELOW_LEFT = 200, CCC_ATTACHED_BELOW = 202,
      CCC_ATTACHED_ABOVE = 214, CCC_ATTACHED_ABOVE_RIGHT = 216,
      CCC_BELOW_LEFT = 218, CCC_BELOW = 220, CCC_BELOW_RIGHT = 222,
      CCC_ABOVE_LEFT = 228, CCC_ABOVE = 230, CCC_ABOVE_RIGHT = 232,
      CCC_DOUBLE_BELOW = 233, CCC_DOUBLE_ABOVE = 234;

/* The switch is over the MODIFIED combining class (see modifiedCombiningClass),
 * which is why the Hebrew/Arabic cases read as small numbers. */
function recategorizeCombiningClass( u, klass ) {
  if ( klass >= 200 ) return klass;
  if ( ( u & ~0xFF ) === 0x0E00 ) {
    if ( klass === 0 ) {
      switch ( u ) {
        case 0x0E31: case 0x0E34: case 0x0E35: case 0x0E36: case 0x0E37:
        case 0x0E47: case 0x0E4C: case 0x0E4D: case 0x0E4E:
          klass = CCC_ABOVE_RIGHT; break;
        case 0x0EB1: case 0x0EB4: case 0x0EB5: case 0x0EB6: case 0x0EB7:
        case 0x0EBB: case 0x0ECC: case 0x0ECD:
          klass = CCC_ABOVE; break;
        case 0x0EBC: klass = CCC_BELOW; break;
      }
    } else if ( u === 0x0E3A ) klass = CCC_BELOW_RIGHT;  /* Thai virama */
  }
  switch ( klass ) {
    /* Hebrew */
    case 22: case 15: case 16: case 17: case 23: case 18: case 19:
    case 20: case 21: case 24: case 25: return CCC_BELOW;
    case 13: return CCC_ATTACHED_ABOVE;          /* rafe */
    case 10: return CCC_ABOVE_RIGHT;             /* shin dot */
    case 11: case 14: return CCC_ABOVE_LEFT;     /* sin dot, holam */
    case 26: return CCC_ABOVE;                   /* point varika */
    case 12: break;                              /* dagesh */
    /* Arabic and Syriac */
    case 28: case 29: case 31: case 32: case 27: case 34: case 35: case 36:
      return CCC_ABOVE;
    case 30: case 33: return CCC_BELOW;
    /* Thai */
    case 3: return CCC_BELOW_RIGHT;
    case 107: return CCC_ABOVE_RIGHT;
    /* Lao */
    case 118: return CCC_BELOW;
    case 122: return CCC_ABOVE;
    /* Tibetan */
    case 129: return CCC_BELOW;
    case 132: return CCC_ABOVE;
    case 131: return CCC_BELOW;
  }
  return klass;
}

function fallbackMarkRecategorize( buffer ) {
  for ( let i = 0; i < buffer.len; i++ ) {
    const inf = buffer.info[ i ];
    if ( infoGenCat( inf ) !== GC.Mn ) continue;
    const k = recategorizeCombiningClass( inf.codepoint, infoCC( inf ) );
    inf.unicodeProps = ( ( ( k << 8 ) | ( inf.unicodeProps & 0xFF ) ) >>> 0 );
  }
}

function zeroMarkAdvances( buffer, start, end, adjustOffsets ) {
  for ( let i = start; i < end; i++ )
    if ( infoGenCat( buffer.info[ i ] ) === GC.Mn ) {
      const p = buffer.pos[ i ];
      if ( adjustOffsets ) {
        p.xOffset = satSub( p.xOffset, p.xAdvance );
        p.yOffset = satSub( p.yOffset, p.yAdvance );
      }
      p.xAdvance = 0; p.yAdvance = 0;
    }
}

function positionMark( plan, font, buffer, baseExtents, i, combiningClass ) {
  const markExtents = font.getGlyphExtents( buffer.info[ i ].codepoint );
  if ( !markExtents ) return;
  const yGap = ( font.yScale / 16 ) | 0;
  const pos = buffer.pos[ i ];
  pos.xOffset = 0; pos.yOffset = 0;

  /* X */
  switch ( combiningClass ) {
    case CCC_DOUBLE_BELOW:
    case CCC_DOUBLE_ABOVE:
      if ( buffer.props.direction === DIRECTION_LTR ) {
        pos.xOffset = clampToI32( baseExtents.x_bearing + baseExtents.width -
                                  ( markExtents.width / 2 | 0 ) - markExtents.x_bearing );
        break;
      } else if ( buffer.props.direction === DIRECTION_RTL ) {
        pos.xOffset = clampToI32( baseExtents.x_bearing -
                                  ( markExtents.width / 2 | 0 ) - markExtents.x_bearing );
        break;
      }
      /* fall through */
    default:
    case CCC_ATTACHED_BELOW:
    case CCC_ATTACHED_ABOVE:
    case CCC_BELOW:
    case CCC_ABOVE:
      pos.xOffset = clampToI32( baseExtents.x_bearing +
        Math.trunc( ( baseExtents.width - markExtents.width ) / 2 ) - markExtents.x_bearing );
      break;
    case CCC_ATTACHED_BELOW_LEFT:
    case CCC_BELOW_LEFT:
    case CCC_ABOVE_LEFT:
      pos.xOffset = satSub( baseExtents.x_bearing, markExtents.x_bearing );
      break;
    case CCC_ATTACHED_ABOVE_RIGHT:
    case CCC_BELOW_RIGHT:
    case CCC_ABOVE_RIGHT:
      pos.xOffset = clampToI32( baseExtents.x_bearing + baseExtents.width -
                                markExtents.width - markExtents.x_bearing );
      break;
  }
  /* Y */
  switch ( combiningClass ) {
    case CCC_DOUBLE_BELOW:
    case CCC_BELOW_LEFT:
    case CCC_BELOW:
    case CCC_BELOW_RIGHT:
      baseExtents.height = satSub( baseExtents.height, yGap );
      /* fall through */
    case CCC_ATTACHED_BELOW_LEFT:
    case CCC_ATTACHED_BELOW:
      pos.yOffset = clampToI32( baseExtents.y_bearing + baseExtents.height -
                                markExtents.y_bearing );
      /* Never shift "below" marks up. */
      if ( ( yGap > 0 ) === ( pos.yOffset > 0 ) ) {
        baseExtents.height = satSub( baseExtents.height, pos.yOffset );
        pos.yOffset = 0;
      }
      baseExtents.height = satAdd( baseExtents.height, markExtents.height );
      break;
    case CCC_DOUBLE_ABOVE:
    case CCC_ABOVE_LEFT:
    case CCC_ABOVE:
    case CCC_ABOVE_RIGHT:
      baseExtents.y_bearing = satAdd( baseExtents.y_bearing, yGap );
      baseExtents.height = satSub( baseExtents.height, yGap );
      /* fall through */
    case CCC_ATTACHED_ABOVE:
    case CCC_ATTACHED_ABOVE_RIGHT:
      pos.yOffset = clampToI32( baseExtents.y_bearing - markExtents.y_bearing -
                                markExtents.height );
      /* Don't shift "above" marks down too much. */
      if ( ( yGap > 0 ) !== ( pos.yOffset > 0 ) ) {
        const correction = -( ( pos.yOffset / 2 ) | 0 );
        baseExtents.y_bearing = satAdd( baseExtents.y_bearing, correction );
        baseExtents.height = satSub( baseExtents.height, correction );
        pos.yOffset = satAdd( pos.yOffset, correction );
      }
      baseExtents.y_bearing = satSub( baseExtents.y_bearing, markExtents.height );
      baseExtents.height = satAdd( baseExtents.height, markExtents.height );
      break;
  }
}

function positionAroundBase( plan, font, buffer, base, end, adjustOffsets ) {
  let horizDir = DIRECTION_INVALID;
  buffer.unsafeToBreak( base, end );
  const be = font.getGlyphExtents( buffer.info[ base ].codepoint );
  if ( !be ) { zeroMarkAdvances( buffer, base + 1, end, adjustOffsets ); return; }
  const baseExtents = {
    x_bearing: 0,
    y_bearing: satAdd( be.y_bearing, buffer.pos[ base ].yOffset ),
    /* horizontal advance, not the ink box: works for zero-ink glyphs too */
    width: font.getGlyphHAdvance( buffer.info[ base ].codepoint ),
    height: be.height,
  };
  const ligId = buffer.info[ base ].ligId | 0;
  const numLigComponents = buffer.info[ base ].numComps | 0 || 1;

  let xOffset = 0, yOffset = 0;
  if ( isForward( buffer.props.direction ) ) {
    xOffset = satNeg( buffer.pos[ base ].xAdvance );
    yOffset = satNeg( buffer.pos[ base ].yAdvance );
  }
  let componentExtents = Object.assign( {}, baseExtents );
  let lastLigComponent = -1, lastCombiningClass = 255;
  let clusterExtents = Object.assign( {}, baseExtents );
  const info = buffer.info;

  for ( let i = base + 1; i < end; i++ ) {
    if ( infoCC( info[ i ] ) ) {
      if ( numLigComponents > 1 ) {
        const thisLigId = info[ i ].ligId | 0;
        let thisLigComponent = ( info[ i ].ligComp | 0 ) - 1;
        if ( !ligId || ligId !== thisLigId || thisLigComponent >= numLigComponents )
          thisLigComponent = numLigComponents - 1;
        if ( lastLigComponent !== thisLigComponent ) {
          lastLigComponent = thisLigComponent;
          lastCombiningClass = 255;
          componentExtents = Object.assign( {}, baseExtents );
          if ( horizDir === DIRECTION_INVALID ) {
            horizDir = isHorizontal( plan.props.direction ) ? plan.props.direction
                     : scriptHorizontalDirection( plan.props.script );
          }
          if ( horizDir === DIRECTION_LTR )
            componentExtents.x_bearing = clampToI32( componentExtents.x_bearing +
              Math.trunc( thisLigComponent * componentExtents.width / numLigComponents ) );
          else
            componentExtents.x_bearing = clampToI32( componentExtents.x_bearing +
              Math.trunc( ( numLigComponents - 1 - thisLigComponent ) *
                          componentExtents.width / numLigComponents ) );
          componentExtents.width = Math.trunc( componentExtents.width / numLigComponents );
        }
      }
      const thisCombiningClass = infoCC( info[ i ] );
      if ( lastCombiningClass !== thisCombiningClass ) {
        lastCombiningClass = thisCombiningClass;
        clusterExtents = Object.assign( {}, componentExtents );
      }
      positionMark( plan, font, buffer, clusterExtents, i, thisCombiningClass );
      buffer.pos[ i ].xAdvance = 0;
      buffer.pos[ i ].yAdvance = 0;
      buffer.pos[ i ].xOffset = satAdd( buffer.pos[ i ].xOffset, xOffset );
      buffer.pos[ i ].yOffset = satAdd( buffer.pos[ i ].yOffset, yOffset );
    } else {
      if ( isForward( buffer.props.direction ) ) {
        xOffset = satSub( xOffset, buffer.pos[ i ].xAdvance );
        yOffset = satSub( yOffset, buffer.pos[ i ].yAdvance );
      } else {
        xOffset = satAdd( xOffset, buffer.pos[ i ].xAdvance );
        yOffset = satAdd( yOffset, buffer.pos[ i ].yAdvance );
      }
    }
  }
}

function fallbackMarkPosition( plan, font, buffer, adjustOffsets ) {
  const info = buffer.info;
  const positionCluster = ( start, end ) => {
    if ( end - start < 2 ) return;
    for ( let i = start; i < end; i++ )
      if ( !infoIsMark( info[ i ] ) ) {
        let j;
        for ( j = i + 1; j < end; j++ ) {
          if ( infoIsHidden( info[ j ] ) || infoIsIgnorable( info[ j ] ) ) continue;
          if ( !infoIsMark( info[ j ] ) ) break;
        }
        positionAroundBase( plan, font, buffer, i, j, adjustOffsets );
        i = j - 1;
      }
  };
  let start = 0;
  for ( let i = 1; i < buffer.len; i++ )
    if ( !infoIsMark( info[ i ] ) && !infoIsHidden( info[ i ] ) &&
         !infoIsIgnorable( info[ i ] ) ) {
      positionCluster( start, i );
      start = i;
    }
  positionCluster( start, buffer.len );
}

/* hb_kern_machine_t: pair the current glyph with the next non-mark glyph,
 * scale the value, then split it as kern1 = kern >> 1, kern2 = kern - kern1,
 * giving kern1 to the left glyph's advance and kern2 to BOTH the right
 * glyph's advance and its offset. */
function applyKernTable( plan, font, buffer ) {
  const mask = plan.kernMask >>> 0;
  if ( !mask ) return;                       /* requested_kerning is false */
  const subs = font.kernSubtables();
  if ( !subs.length ) return;
  const horizontal = isHorizontal( buffer.props.direction );
  const info = buffer.info, pos = buffer.pos;
  buffer.unsafeToConcat();
  for ( const st of subs ) {
    if ( st.variation ) continue;             /* tuple kerning: not supported */
    if ( st.horizontal !== horizontal ) continue;
    if ( st.format !== 0 && st.format !== 2 ) continue;
    const crossStream = st.crossStream;
    for ( let idx = 0; idx < buffer.len; ) {
      if ( !( info[ idx ].mask & mask ) ) { idx++; continue; }
      /* skippy_iter with IgnoreMarks: marks are skipped, and so are
       * default-ignorables (SKIP_MAYBE) -- a ZWNJ between two glyphs must not
       * stop them kerning. */
      let j = idx + 1;
      while ( j < buffer.len &&
              ( ( info[ j ].props & GLYPH_PROPS_MARK ) || infoIsIgnorable( info[ j ] ) ) ) j++;
      if ( j >= buffer.len ) break;
      const raw = font.kernValue( st, info[ idx ].codepoint, info[ j ].codepoint );
      if ( raw ) {
        const k = horizontal ? font.emScaleX( raw ) : font.emScaleY( raw );
        if ( crossStream ) {
          if ( horizontal ) pos[ j ].yOffset = k; else pos[ j ].xOffset = k;
          buffer.scratchFlags |= SCRATCH_HAS_GPOS_ATTACHMENT;
        } else {
          const k1 = k >> 1, k2 = k - k1;
          if ( horizontal ) {
            pos[ idx ].xAdvance = satAdd( pos[ idx ].xAdvance, k1 );
            pos[ j ].xAdvance = satAdd( pos[ j ].xAdvance, k2 );
            pos[ j ].xOffset = satAdd( pos[ j ].xOffset, k2 );
          } else {
            pos[ idx ].yAdvance = satAdd( pos[ idx ].yAdvance, k1 );
            pos[ j ].yAdvance = satAdd( pos[ j ].yAdvance, k2 );
            pos[ j ].yOffset = satAdd( pos[ j ].yOffset, k2 );
          }
        }
      }
      idx = j;
    }
  }
}

/* ======================================================================
 *  The shape plan: which features, in which order.
 *  Order is exactly hb_ot_shape_collect_features():
 *    rvrn, <gsub pause>, direction features, frac/numr/dnom, rand,
 *    Harf/HARF, [shaper], Buzz/BUZZ, common_features, then either
 *    horizontal_features or 'vert', then the user's --features.
 * ==================================================================== */
export const F_NONE = 0, F_GLOBAL = 1, F_HAS_FALLBACK = 2, F_MANUAL_ZWNJ = 4,
  F_MANUAL_ZWJ = 8, F_GLOBAL_SEARCH = 16, F_RANDOM = 32, F_PER_SYLLABLE = 64;
const F_MANUAL_JOINERS = F_MANUAL_ZWNJ | F_MANUAL_ZWJ;
const F_GLOBAL_MANUAL_JOINERS = F_GLOBAL | F_MANUAL_JOINERS;
const F_GLOBAL_HAS_FALLBACK = F_GLOBAL | F_HAS_FALLBACK;

const COMMON_FEATURES = [
  [ 'abvm', F_GLOBAL ], [ 'blwm', F_GLOBAL ], [ 'ccmp', F_GLOBAL ],
  [ 'locl', F_GLOBAL ], [ 'mark', F_GLOBAL_MANUAL_JOINERS ],
  [ 'mkmk', F_GLOBAL_MANUAL_JOINERS ], [ 'rlig', F_GLOBAL ],
];
const HORIZONTAL_FEATURES = [
  [ 'calt', F_GLOBAL ], [ 'clig', F_GLOBAL ], [ 'curs', F_GLOBAL ],
  [ 'dist', F_GLOBAL ], [ 'kern', F_GLOBAL_HAS_FALLBACK ],
  [ 'liga', F_GLOBAL ], [ 'rclt', F_GLOBAL ],
];

/* hb_ot_shape_collect_features.  Returns the ordered feature list together
 * with the per-stage pause callbacks and the stage counts, because a complex
 * shaper (USE needs six GSUB stages, Indic similar) decides how many there
 * are -- the count cannot be hardcoded. */
export function collectFeatures( props, userFeatures, shaper ) {
  const out = [];
  let stageG = 0, stageP = 0;
  const pauses = [ [], [] ];
  const builder = {
    props, shaper,
    addFeature : ( tag, flags = F_NONE, value = 1 ) =>
      out.push( { tag, flags, value, stage: [ stageG, stageP ] } ),
    enableFeature : ( tag, flags = F_NONE, value = 1 ) =>
      out.push( { tag, flags: flags | F_GLOBAL, value, stage: [ stageG, stageP ] } ),
    addGsubPause : ( fn ) => { pauses[ 0 ][ stageG ] = fn || null; stageG++; },
    addGposPause : ( fn ) => { pauses[ 1 ][ stageP ] = fn || null; stageP++; },
  };

  builder.enableFeature( 'rvrn' );
  builder.addGsubPause( null );

  switch ( props.direction ) {
    case DIRECTION_LTR: builder.enableFeature( 'ltra' ); builder.enableFeature( 'ltrm' ); break;
    case DIRECTION_RTL: builder.enableFeature( 'rtla' ); builder.addFeature( 'rtlm' ); break;
    default: break;
  }
  builder.addFeature( 'frac' ); builder.addFeature( 'numr' ); builder.addFeature( 'dnom' );
  builder.enableFeature( 'rand', F_RANDOM, 0x7FFFFFFF );
  builder.enableFeature( 'Harf' ); builder.enableFeature( 'HARF' );

  if ( shaper && shaper.collectFeatures ) shaper.collectFeatures( builder );

  builder.enableFeature( 'Buzz' ); builder.enableFeature( 'BUZZ' );
  for ( const [ t, f ] of COMMON_FEATURES ) builder.addFeature( t, f );
  if ( isHorizontal( props.direction ) )
    for ( const [ t, f ] of HORIZONTAL_FEATURES ) builder.addFeature( t, f );
  else
    builder.enableFeature( 'vert', F_GLOBAL_SEARCH );

  for ( const uf of userFeatures ) {
    const global = uf.start === 0 && uf.end === 0xFFFFFFFF;
    builder.addFeature( uf.tag, global ? F_GLOBAL : F_NONE, uf.value );
  }
  if ( shaper && shaper.overrideFeatures ) shaper.overrideFeatures( builder );

  /* compile() closes both tables with a final pause, so the stage count is
   * one more than the number of pauses added. */
  return { features: out, pauses, numGsubStages: stageG + 1, numGposStages: stageP + 1 };
}

function directionName( d ) {
  return d === DIRECTION_RTL ? 'rtl' : d === DIRECTION_TTB ? 'ttb'
       : d === DIRECTION_BTT ? 'btt' : 'ltr';
}

/* hb_bit_storage */
function bitStorage( v ) { let n = 0; while ( v ) { n++; v >>>= 1; } return n; }

/* hb_ot_map_builder_t::compile.  Features are sorted by tag and duplicates
 * merged, then each is given its own slice of the 32-bit glyph mask.  A
 * feature that is global with max value 1 shares the single global bit; every
 * other feature gets `bit_storage(max_value)` bits of its own, so the VALUE
 * survives into the lookup (an Alternate lookup reads it back to choose which
 * alternate).  `--features` entries come last and therefore win the merge,
 * which is how `-ccmp` switches a default feature off. */
export function createShapePlan( face, props, userFeatures, layout, shaperOverride, coords ) {
  const scriptTags = allTagsFromScript( props.script ).map( tagToString );
  const languageTags = otTagsFromLanguage( props.language ).map( tagToString );
  /* hb_ot_shape_planner_t's constructor resolves the GSUB script BEFORE it
   * categorizes the shaper, because the cursive scripts fall back to the
   * default shaper when GSUB names no script at all (hb-ot-shaper.hh: "(gsub_script
   * != HB_TAG_NONE || script == HB_SCRIPT_ARABIC) && HB_DIRECTION_IS_HORIZONTAL").
   * hb_ot_layout_table_select_script sets chosen_script for the DFLT / dflt /
   * latn fallbacks too, so any of those counts as "found". */
  let gsubScriptFound = false;
  /* hb_ot_layout_table_select_script also reports WHICH tag it settled on,
   * and hb_ot_shaper_categorize() branches on that tag, not merely on whether
   * one was found: a font that answers only 'DFLT' or 'latn' takes the DEFAULT
   * shaper even for Devanagari, and one that answers a v3 tag ('dev3',
   * 'knd3', ...) takes the UNIVERSAL shaper instead of the Indic one.  The
   * fallbacks are tried in the reference's order and the first hit wins. */
  /* HB_TAG_NONE when the table names no script we asked for and offers none of
   * the three fallbacks -- including when there is no GSUB table at all.  That
   * is NOT the same as 'DFLT': a font with no GSUB still gets its script's own
   * shaper, and only a font that explicitly declares 'DFLT' or 'latn' is taken
   * to be asking for no complex shaping. */
  let gsubScript = null;
  if ( layout && layout.gsub && S1 && S1.selectScript ) {
    try {
      for ( const t of scriptTags.concat( [ 'DFLT', 'dflt', 'latn' ] ) )
        if ( S1.selectScript( layout.gsub, [ t ] ) >= 0 ) {
          gsubScript = t; gsubScriptFound = true; break;
        }
    } catch { gsubScriptFound = false; gsubScript = null; }
  }
  const shaperCtx = {
    gsubScriptFound,
    gsubScript,
    horizontal: isHorizontal( props.direction ),
  };
  /* shaperOverride is for tests only; shaping always resolves by script. */
  const shaper = shaperOverride !== undefined ? shaperOverride
               : shaperForScript( tagToString( props.script ), shaperCtx );
  const collected = collectFeatures( props, userFeatures, shaper );
  const raw = collected.features;
  const tagOf = ( t ) => typeof t === 'string' ? t : tagToString( t );

  const infos = raw.map( ( f, i ) => ( {
    tag: tagOf( f.tag ), seq: i, flags: f.flags,
    maxValue: f.value, defaultValue: ( f.flags & F_GLOBAL ) ? f.value : 0,
    stage: f.stage.slice(),
  } ) );
  infos.sort( ( x, y ) => x.tag < y.tag ? -1 : x.tag > y.tag ? 1 : x.seq - y.seq );
  const merged = [];
  for ( const f of infos ) {
    const m = merged.length ? merged[ merged.length - 1 ] : null;
    if ( !m || m.tag !== f.tag ) { merged.push( Object.assign( {}, f ) ); continue; }
    if ( f.flags & F_GLOBAL ) {
      m.flags |= F_GLOBAL; m.maxValue = f.maxValue; m.defaultValue = f.defaultValue;
    } else {
      if ( m.flags & F_GLOBAL ) m.flags ^= F_GLOBAL;
      m.maxValue = Math.max( m.maxValue, f.maxValue );
    }
    m.flags |= ( f.flags & F_HAS_FALLBACK );
    m.stage[ 0 ] = Math.min( m.stage[ 0 ], f.stage[ 0 ] );
    m.stage[ 1 ] = Math.min( m.stage[ 1 ], f.stage[ 1 ] );
  }

  const GLOBAL_SHIFT = 31, GLOBAL_MASK = 0x80000000 >>> 0;
  let globalMask = GLOBAL_MASK;
  /* next_bit starts above HB_GLYPH_FLAG_DEFINED (0x7), which owns bits 0..2. */
  let nextBit = 4;
  const featureMap = new Map();
  for ( const f of merged ) {
    const usesGlobal = ( f.flags & F_GLOBAL ) && f.maxValue === 1;
    const bitsNeeded = usesGlobal ? 0 : Math.min( 8, bitStorage( f.maxValue ) );
    if ( !f.maxValue || nextBit + bitsNeeded >= GLOBAL_SHIFT ) continue;
    let shift, mask;
    if ( usesGlobal ) { shift = GLOBAL_SHIFT; mask = GLOBAL_MASK; }
    else {
      shift = nextBit;
      mask = ( ( ( ( 1 << ( nextBit + bitsNeeded ) ) >>> 0 ) - 1 ) &
               ~( ( ( 1 << nextBit ) >>> 0 ) - 1 ) ) >>> 0;
      nextBit += bitsNeeded;
      globalMask = ( globalMask | ( ( f.defaultValue << shift ) & mask ) ) >>> 0;
    }
    featureMap.set( f.tag, { tag: f.tag, shift, mask, stage: f.stage, flags: f.flags } );
  }

  const scriptFallbackPosition = shaper ? shaper.fallbackPosition !== false : true;
  const zeroWidthMarks = ( shaper && shaper.zeroWidthMarks !== undefined )
    ? shaper.zeroWidthMarks : 'by-gdef-late';
  const hasGdefClasses = !!( layout && layout.gdef &&
    ( layout.gdef.hasGlyphClasses || layout.gdef.glyphClassDef ) );
  const plan = {
    props, userFeatures, layout, features: raw, featureMap, shaper,
    scriptTags, languageTags, globalMask,
    direction : directionName( props.direction ),
    applyGpos: !!( layout && layout.gpos ),
    zeroWidthMarksMode: zeroWidthMarks,
    /* hb_ot_shape_planner_t::compile: script_zero_marks and
     * script_fallback_position come from the shaper, and BOTH gate real
     * behaviour -- a shaper with fallback_position = false wants the marks
     * merely zeroed (and shifted back), not repositioned. */
    zeroMarks: zeroWidthMarks !== 'none',
    adjustMarkPositioningWhenZeroing: !( layout && layout.gpos ),
    fallbackMarkPositioning: !( layout && layout.gpos ) && scriptFallbackPosition,
    fallbackGlyphClasses: !hasGdefClasses,
    coords: null,
  };
  plan.adjustMarkOffsetsWhenZeroing = plan.adjustMarkPositioningWhenZeroing;
  plan.glyphPropsFallback = hasGdefClasses ? null : ( g ) => synthesizeGlyphProps( g );

  /* hb_ot_layout_table_select_script: requested tags, then DFLT, dflt, latn.
   * The aots fonts register everything under `latn' while their test text is
   * all Common, so without the latn fallback none of them match at all. */
  const scriptSearch = scriptTags.concat( [ 'DFLT', 'dflt', 'latn' ] );
  const gsubFound = new Set(), gposFound = new Set();

  /* Variations must be resolved before staging: a FeatureVariations record can
   * swap a feature's whole lookup list at this instance, and a format-2
   * condition needs the item-variation instancer. */
  const nzCoords = !!( coords && Array.prototype.some.call( coords, ( c ) => c !== 0 ) );
  installVariations( plan, face, layout, coords, nzCoords );
  const fvIndexFor = ( table ) => {
    if ( process.env.S3_NO_FV ) return FV_NOT_FOUND;   /* isolation switch */
    if ( !table || !table.featureVariations ) return FV_NOT_FOUND;
    /* NOT gated on "some axis is off its default".  ConditionAxisRange reads
     * an axis the font does not list, or one left at its default, as the
     * coordinate 0, and a range such as wght in [-1, 0.367] CONTAINS 0 -- so
     * a FeatureVariations record can, and in this corpus does, match at the
     * default instance.  Skipping the whole table whenever the coordinates
     * are all zero silently drops the variant lookups of every such font. */
    if ( process.env.W20_NO_FVDEFAULT && !nzCoords ) return FV_NOT_FOUND;
    try {
      return fvFindIndex( table.data, table.featureVariations, coords || [],
                          plan.resolveVarDelta );
    } catch { return FV_NOT_FOUND; }
  };
  const fvLookupsFor = ( table, varIdx, featureIndex ) => {
    if ( varIdx === FV_NOT_FOUND || !table.featureVariations ) return null;
    try {
      const sub = fvFindSubstitute( table.data, table.featureVariations, varIdx, featureIndex );
      return sub >= 0 ? fvFeatureLookups( table.data, sub ) : null;
    } catch { return null; }
  };

  /* hb_ot_shape_planner_t::compile: a shaper that names a gpos_tag turns GPOS
   * OFF unless that tag is the script GPOS actually selected -- the Hebrew
   * shaper does this with 'hebr'.  With GPOS off, marks fall back to being
   * zeroed, shifted, and positioned by the fallback positioner. */
  if ( shaper && shaper.gposTag && layout && layout.gpos && S1 && S1.selectScript ) {
    let chosen = null;
    try {
      const si = S1.selectScript( layout.gpos, scriptSearch );
      if ( si >= 0 && layout.gpos.scripts && layout.gpos.scripts[ si ] )
        chosen = layout.gpos.scripts[ si ].tag;
    } catch { chosen = null; }
    if ( chosen !== shaper.gposTag ) {
      plan.applyGpos = false;
      plan.adjustMarkPositioningWhenZeroing = true;
      plan.adjustMarkOffsetsWhenZeroing = true;
      plan.fallbackMarkPositioning = scriptFallbackPosition;
    }
  }

  const buildStages = ( table, tableIndex, nStages, pauses ) => {
    const empty = Array.from( { length: nStages }, ( _, i ) =>
      ( { lookups: [], pause: ( pauses && pauses[ i ] ) || null } ) );
    if ( !table || !S1 || !S1.selectScript ) return empty;
    const si = S1.selectScript( table, scriptSearch );
    if ( si < 0 ) return empty;
    const ls = S1.selectLangSys( table, si, languageTags );
    if ( !ls ) return empty;
    const perStage = Array.from( { length: nStages }, () => [] );
    const found = tableIndex === 0 ? gsubFound : gposFound;
    const varIdx = fvIndexFor( table );
    /* The LangSys requiredFeatureIndex is always applied, whatever its tag. */
    const fidx = S1.langSysFeatureIndexes ? S1.langSysFeatureIndexes( table, ls ) : null;
    if ( fidx && fidx.required !== undefined && fidx.required !== 0xFFFF ) {
      const req = fvLookupsFor( table, varIdx, fidx.required ) ||
                  S1.featureLookups( table, fidx.required ) || [];
      for ( const li of req )
        perStage[ 0 ].push( { index: li, mask: GLOBAL_MASK, autoZwnj: true,
                              autoZwj: true, perSyllable: false } );
    }
    for ( const fm of featureMap.values() ) {
      let fi = S1.findFeature( table, ls, fm.tag );
      if ( fi < 0 && ( fm.flags & F_GLOBAL_SEARCH ) ) {
        /* "If the feature is not in the LangSys, look for it in the global
         * feature list and pick one" -- how 'vert' is found in fonts that
         * list it under no script we selected. */
        const fl = table.features || [];
        for ( let k = 0; k < fl.length; k++ )
          if ( fl[ k ] && fl[ k ].tag === fm.tag ) { fi = k; break; }
      }
      if ( fi < 0 ) continue;
      found.add( fm.tag );
      const st = Math.min( fm.stage[ tableIndex ], nStages - 1 );
      const lks = fvLookupsFor( table, varIdx, fi ) || S1.featureLookups( table, fi ) || [];
      for ( const li of lks )
        perStage[ st ].push( {
          index: li, mask: fm.mask,
          autoZwnj: !( fm.flags & F_MANUAL_ZWNJ ),
          autoZwj: !( fm.flags & F_MANUAL_ZWJ ),
          /* sd-use.txt l.705-721 and l.737-765 both say the basic and
           * orthographic groups are "applied together, ONE CLUSTER AT A
           * TIME"; F_PER_SYLLABLE is how the shapers ask for that.  The flag
           * was collected and then dropped here, so every such lookup ran
           * over the whole run and could match across a cluster boundary. */
          perSyllable: !!( fm.flags & F_PER_SYLLABLE ),
        } );
    }
    /* Within a stage: LookupList order, duplicates merged by OR-ing masks. */
    return perStage.map( ( list ) => {
      list.sort( ( x, y ) => x.index - y.index );
      const out = [];
      for ( const e of list ) {
        const last = out.length ? out[ out.length - 1 ] : null;
        if ( last && last.index === e.index ) {
          last.mask = ( last.mask | e.mask ) >>> 0;
          last.autoZwnj = last.autoZwnj && e.autoZwnj;
          last.autoZwj = last.autoZwj && e.autoZwj;
          last.perSyllable = last.perSyllable || e.perSyllable;
        } else out.push( Object.assign( {}, e ) );
      }
      return { lookups: out, pause: null };
    } ).map( ( st, i ) => ( { lookups: st.lookups,
                              pause: ( pauses && pauses[ i ] ) || null } ) );
  };
  if ( layout ) {
    plan.gsubStages = buildStages( layout.gsub, 0, collected.numGsubStages,
                                   collected.pauses[ 0 ] );
    plan.gposStages = buildStages( layout.gpos, 1, collected.numGposStages,
                                   collected.pauses[ 1 ] );
  }
  /* hb_ot_map_t only ever holds features the FONT lists, so its get_1_mask
   * returns 0 for one the font does not have.  featureMap here is built
   * before the tables are consulted and therefore holds every collected
   * feature, so the presence answer has to travel separately: a shaper that
   * paints a mask for an absent feature (USE's rphf window, its topographical
   * forms) would otherwise act on a feature that cannot run. */
  plan.gsubFound = gsubFound;
  plan.gposFound = gposFound;
  /* The script tag GSUB actually settled on, or null for HB_TAG_NONE.  The
   * Indic shaper reads it to tell a second-specification font from an
   * old-specification one. */
  plan.gsubScript = gsubScript;
  /* Automatic fractions: each of frac/numr/dnom gets its value-1 mask, which
   * setupMasksFraction() then paints over the digit runs around U+2044. */
  const oneMask = ( t ) => {
    const f = featureMap.get( t );
    return f ? ( ( ( 1 << f.shift ) & f.mask ) >>> 0 ) : 0;
  };
  plan.fracMask = oneMask( 'frac' );
  plan.numrMask = oneMask( 'numr' );
  plan.dnomMask = oneMask( 'dnom' );
  plan.hasFrac = !!( plan.fracMask || ( plan.numrMask && plan.dnomMask ) );

  /* has_vert: when the font brings its own `vert' feature, the shaper must
   * NOT also swap in vertical presentation forms -- the feature does it. */
  plan.hasVert = gsubFound.has( 'vert' ) || gposFound.has( 'vert' );
  /* rtlm's value-1 mask, used as the mirroring fallback for RTL runs. */
  const rm = featureMap.get( 'rtlm' );
  plan.rtlmMask = rm ? ( ( ( 1 << rm.shift ) & rm.mask ) >>> 0 ) : 0;

  /* hb_ot_shape_planner_t::compile: the legacy `kern' table runs when the
   * font has one and GPOS did not already provide a `kern' feature. */
  const kernTag = isHorizontal( props.direction ) ? 'kern' : 'vkrn';
  const km = featureMap.get( kernTag );
  plan.kernMask = km ? km.mask : 0;
  plan.requestedKerning = !!plan.kernMask;
  const hasKernTable = !!( face && face.tables && face.tables.get( 'kern' ) );
  plan.applyKern = hasKernTable && scriptFallbackPosition &&
                   ( !gposFound.has( kernTag ) || !plan.applyGpos );

  /* ---- variations for the layout stage --------------------------------
   * A GPOS Device table may really be a VariationIndex record, which
   * resolves through GDEF's ItemVariationStore against the normalized
   * coords.  Both live here, so supply them on the plan together with a
   * ready-made resolver: ot-layout.js then never imports varfont.js.
   *
   *   plan.coords            Int32Array of F2Dot14, or null
   *   plan.varStore          parsed ItemVariationStore, or null
   *   plan.resolveVarDelta( packedVarIdx )        -> number
   *   plan.resolveVarDelta( outerIndex, inner )   -> number
   *
   * Both call shapes are accepted so the call site cannot get the packing
   * wrong; packed is (outer << 16) | inner, as varfont.js uses. Returns 0
   * when there is no store, no variation active, or the index is unknown. */
  /* NOTE: varStore/resolveVarDelta are installed earlier by
   * installVariations(); do not reset them here. */
  return plan;
}

/* hb_ot_shape_setup_masks_fraction: a FRACTION SLASH with decimal digits on
 * BOTH sides turns the run before it into a numerator and the run after into a
 * denominator.  With digits on only one side nothing is marked, but that side
 * is flagged unsafe-to-concat.  Masks swap when the direction is backward. */
export function setupMasksFraction( plan, buffer ) {
  if ( !( buffer.scratchFlags & SCRATCH_HAS_FRACTION_SLASH ) || !plan.hasFrac ) return;
  let preMask, postMask;
  if ( isForward( buffer.props.direction ) ) {
    preMask = ( plan.numrMask | plan.fracMask ) >>> 0;
    postMask = ( plan.fracMask | plan.dnomMask ) >>> 0;
  } else {
    preMask = ( plan.fracMask | plan.dnomMask ) >>> 0;
    postMask = ( plan.numrMask | plan.fracMask ) >>> 0;
  }
  const count = buffer.len, info = buffer.info;
  for ( let i = 0; i < count; i++ ) {
    if ( info[ i ].codepoint !== 0x2044 ) continue;
    let start = i, end = i + 1;
    while ( start && infoGenCat( info[ start - 1 ] ) === GC.Nd ) start--;
    while ( end < count && infoGenCat( info[ end ] ) === GC.Nd ) end++;
    if ( start === i || end === i + 1 ) {
      if ( start === i ) buffer.unsafeToConcat( start, start + 1 );
      if ( end === i + 1 ) buffer.unsafeToConcat( end - 1, end );
      continue;
    }
    buffer.unsafeToBreak( start, end );
    for ( let j = start; j < i; j++ ) info[ j ].mask = ( info[ j ].mask | preMask ) >>> 0;
    info[ i ].mask = ( info[ i ].mask | plan.fracMask ) >>> 0;
    for ( let j = i + 1; j < end; j++ ) info[ j ].mask = ( info[ j ].mask | postMask ) >>> 0;
    i = end - 1;
  }
}

/* hb_ot_shape_setup_masks: a user feature limited to a character range gets
 * its value written into its own bits over that cluster range only. */
function setupUserMasks( plan, buffer ) {
  for ( const uf of plan.userFeatures || [] ) {
    if ( uf.start === 0 && uf.end === 0xFFFFFFFF ) continue;
    const tag = typeof uf.tag === 'string' ? uf.tag : tagToString( uf.tag );
    const fm = plan.featureMap && plan.featureMap.get( tag );
    if ( !fm ) continue;
    buffer.setMasks( ( uf.value << fm.shift ) >>> 0, fm.mask, uf.start, uf.end );
  }
}

/* ======================================================================
 *  GSUB/GPOS FeatureVariations -- how a variable font changes its feature set
 *  by instance.  Layout, all offsets relative to their own struct base:
 *    FeatureVariations       version(4), u32 count @4, 8-byte records @8
 *    record                  Offset32 conditions @0, Offset32 substitutions @4
 *    ConditionSet            u16 count @0, u32 offsets @2   (AND; empty = true)
 *    Condition fmt 1         axisIndex u16 @2, min/max F2Dot14 @4,@6
 *    Condition fmt 2         defaultValue i16 @2, varIdx u32 @4
 *    Condition fmt 3/4       u8 count @2, 24-bit offsets @3  (AND / OR)
 *    Condition fmt 5         24-bit offset @2                (NOT)
 *    FeatureTableSubstitution version(4), u16 count @4, 6-byte records @6
 *    record                  featureIndex u16 @0, Offset32 feature @2
 *    Feature                 featureParams u16 @0, count u16 @2, indices @4
 * ==================================================================== */
export const FV_NOT_FOUND = 0xFFFFFFFF;
const rU24 = ( d, o ) => ( d[ o ] << 16 ) | ( d[ o + 1 ] << 8 ) | d[ o + 2 ];

function fvConditionEvaluate( d, base, coords, resolveDelta, depth ) {
  if ( depth > 8 ) return false;
  const format = rU16( d, base );
  switch ( format ) {
    case 1: {
      /* Compares RAW 2.14 ints, not floats. */
      const axis = rU16( d, base + 2 );
      const lo = rI16( d, base + 4 ), hi = rI16( d, base + 6 );
      const c = axis < coords.length ? coords[ axis ] : 0;
      return lo <= c && c <= hi;
    }
    case 2: {
      const def = rI16( d, base + 2 );
      const varIdx = rU32( d, base + 4 );
      return def + ( resolveDelta ? resolveDelta( varIdx ) : 0 ) > 0;
    }
    case 3: case 4: {
      const n = d[ base + 2 ], isAnd = format === 3;
      for ( let i = 0; i < n; i++ ) {
        const off = rU24( d, base + 3 + i * 3 );
        const r = fvConditionEvaluate( d, base + off, coords, resolveDelta, depth + 1 );
        if ( isAnd ) { if ( !r ) return false; } else if ( r ) return true;
      }
      return isAnd;
    }
    case 5:
      return !fvConditionEvaluate( d, base + rU24( d, base + 2 ), coords, resolveDelta, depth + 1 );
    default: return false;
  }
}

/* First record whose ConditionSet evaluates true wins. */
export function fvFindIndex( d, fvBase, coords, resolveDelta ) {
  if ( !fvBase ) return FV_NOT_FOUND;
  /* An empty coordinate list is not "no variations": every axis then reads as
   * 0, which is a value conditions can and do match. */
  if ( process.env.W20_NO_FVDEFAULT && ( !coords || !coords.length ) )
    return FV_NOT_FOUND;
  coords = coords || [];
  const count = rU32( d, fvBase + 4 );
  for ( let i = 0; i < count; i++ ) {
    const csOff = rU32( d, fvBase + 8 + i * 8 );
    if ( !csOff ) return i;            /* NULL ConditionSet: empty AND is true */
    const cs = fvBase + csOff;
    const n = rU16( d, cs );
    let all = true;
    for ( let k = 0; k < n; k++ ) {
      if ( !fvConditionEvaluate( d, cs + rU32( d, cs + 2 + k * 4 ),
                                 coords, resolveDelta, 0 ) ) { all = false; break; }
    }
    if ( all ) return i;
  }
  return FV_NOT_FOUND;
}

/* Offset of the substituted Feature table for this feature index, or -1. */
function fvFindSubstitute( d, fvBase, varIdx, featureIndex ) {
  const subOff = rU32( d, fvBase + 8 + varIdx * 8 + 4 );
  if ( !subOff ) return -1;
  const fts = fvBase + subOff;
  const count = rU16( d, fts + 4 );
  for ( let i = 0; i < count; i++ ) {
    const r = fts + 6 + i * 6;
    if ( rU16( d, r ) === featureIndex ) {
      const fo = rU32( d, r + 2 );
      return fo ? fts + fo : -1;
    }
  }
  return -1;
}
function fvFeatureLookups( d, featureBase ) {
  const n = rU16( d, featureBase + 2 ), out = [];
  for ( let i = 0; i < n; i++ ) out.push( rU16( d, featureBase + 4 + i * 2 ) );
  return out;
}

/* Install the variation resolver on a plan.  Kept out of shape() so there is
 * a single code path that callers building their own plan -- and the selftest
 * -- reach as well.  Deltas are only ever resolved off the default instance:
 * every region peak is non-zero, so at coord 0 each region evaluates to 0.0
 * and a "default-instance delta" cannot exist. */
export function installVariations( plan, face, layout, coords, hasNonzeroCoords ) {
  plan.coords = coords || null;
  plan.varStore = null;
  plan.resolveVarDelta = () => 0;
  if ( !VF || !hasNonzeroCoords ) return plan;
  if ( !layout || !layout.gdef || !layout.gdef.itemVarStore ) return plan;
  try {
    const store = VF.readItemVariationStore( face.data, layout.gdef.itemVarStore );
    if ( !store ) return plan;
    plan.varStore = store;
    plan.resolveVarDelta = ( a, b ) => {
      const varIdx = ( b === undefined ) ? ( a >>> 0 )
                   : ( ( ( a << 16 ) | ( b & 0xFFFF ) ) >>> 0 );
      try { return VF.ivsGetDelta( store, varIdx, coords ) || 0; } catch { return 0; }
    };
  } catch { /* leave the no-op resolver in place */ }
  return plan;
}

/* The pause half of hb_ot_map_t::apply, for a face whose GSUB table is absent
 * or unparsable.  Lookup application is the other half and has nothing to do
 * on such a face. */
function runGsubPauses( plan, buffer, font ) {
  for ( const st of ( plan.gsubStages || [] ) )
    if ( st && st.pause ) st.pause( plan, buffer, font );
}

/* ======================================================================
 *  shape()
 * ==================================================================== */
export function shape( font, buffer, userFeatures = [], shapers = null ) {
  const face = font.face;
  let layout = null;
  if ( S1 && S1.loadLayout ) { try { layout = S1.loadLayout( face ); } catch { layout = null; } }
  const plan = createShapePlan( face, buffer.props, userFeatures, layout,
                                undefined, font.coords );

  const targetDirection = buffer.props.direction;
  buffer.resetMasks( plan.globalMask );
  setUnicodeProps( buffer );
  /* hb_insert_dotted_circle needs HB_BUFFER_FLAG_BOT, which hb-shape never
   * sets, so it is a no-op for this CLI. */
  formClusters( buffer );
  ensureNativeDirection( buffer );
  if ( plan.shaper && plan.shaper.preprocessText )
    plan.shaper.preprocessText( plan, buffer, font );
  /* ensure_native_direction may have flipped the buffer; the attachment
   * resolution in ot-layout.js keys off the CURRENT direction. */
  plan.direction = directionName( buffer.props.direction );

  /* --- substitute --- */
  rotateChars( plan, buffer, font, targetDirection );
  normalize( plan, buffer, font );
  /* hb_ot_shape_setup_masks order: fractions, then the shaper, then the
   * user's ranged --features. */
  setupMasksFraction( plan, buffer );
  if ( plan.shaper && plan.shaper.setupMasks )
    plan.shaper.setupMasks( plan, buffer, font );
  setupUserMasks( plan, buffer );
  if ( plan.fallbackMarkPositioning ) fallbackMarkRecategorize( buffer );
  mapGlyphsFast( buffer );

  const adjustOffsetsWhenZeroing =
    plan.adjustMarkPositioningWhenZeroing && isForward( buffer.props.direction );
  plan.adjustMarkOffsetsWhenZeroing = adjustOffsetsWhenZeroing;

  /* ot-layout.js sizes the buffer by `buffer.info.length`; this Buffer carries
   * an explicit `len` like the reference does.  GSUB adds and removes glyphs,
   * so the two must be resynchronised across every S1 call or the positioning
   * loop walks off the end of `info`. */
  const toS1 = () => { buffer.info.length = buffer.len; };
  const fromS1 = () => {
    buffer.len = buffer.info.length;
    while ( buffer.pos.length < buffer.len ) buffer.pos.push( makePos() );
    buffer.pos.length = buffer.len;
  };

  if ( layout && S1 && S1.substituteStart ) {
    toS1();
    S1.substituteStart( buffer, layout.gdef, plan.glyphPropsFallback );
    /* hb_ot_map_t::apply walks the stage list and fires every pause whether or
     * not the face has a GSUB table -- a face with none simply contributes no
     * lookups to any stage.  substitute() returns early on such a face, so the
     * pauses have to be driven here instead.  Not a corner case: the complex
     * shapers do ALL their work in pauses (find the syllables, insert the
     * dotted circles, reorder), so on a GSUB-less font the whole shaper was
     * silently skipped.  Measured on the Chakma and Sharada faces of the
     * step-3 corpus, which carry GDEF and nothing else: every defective
     * cluster in them lost its dotted circle. */
    if ( layout.gsub && S1.substitute ) S1.substitute( plan, buffer, font );
    else if ( !process.env.W19_NO_GSUBPAUSE ) runGsubPauses( plan, buffer, font );
    fromS1();
  } else {
    synthesizeGlyphClasses( buffer );
  }

  /* --- position --- */
  buffer.clearPositions();
  positionDefault( plan, buffer, font );

  if ( layout && S1 && S1.position ) {
    /* S1.position() bundles positionStart + GPOS + zeroMarkWidths +
     * positionFinish.  The reference interleaves zero_width_default_ignorables
     * between the last two; we run it just after instead.  TODO: only matters
     * when a default-ignorable is an attachment target. */
    toS1();
    S1.position( plan, buffer, font );
    fromS1();
    /* The reference runs the `kern' table inside position(), before the
     * attachment chains are resolved; S1 bundles those, so cross-stream
     * kerning is not propagated here.  TODO if a corpus font needs it. */
    if ( plan.applyKern ) applyKernTable( plan, font, buffer );
    zeroWidthDefaultIgnorables( buffer );
    if ( plan.fallbackMarkPositioning )
      fallbackMarkPosition( plan, font, buffer, adjustOffsetsWhenZeroing );
  } else {
    for ( let i = 0; i < buffer.len; i++ ) {
      buffer.pos[ i ].attachChain = 0; buffer.pos[ i ].attachType = 0;
    }
    if ( plan.zeroMarks ) zeroMarkWidthsByGdef( buffer, adjustOffsetsWhenZeroing );
    if ( plan.applyKern ) applyKernTable( plan, font, buffer );
    zeroWidthDefaultIgnorables( buffer );
    positionFinishOffsets( buffer );
    if ( plan.fallbackMarkPositioning )
      fallbackMarkPosition( plan, font, buffer, adjustOffsetsWhenZeroing );
  }
  if ( isBackward( buffer.props.direction ) ) buffer.reverse();

  /* --- substitute post --- */
  if ( plan.shaper && plan.shaper.postprocessGlyphs )
    plan.shaper.postprocessGlyphs( plan, buffer, font );
  dealWithVariationSelectors( buffer );
  hideDefaultIgnorables( buffer, font );

  propagateFlags( buffer );
  buffer.props.direction = targetDirection;
  return buffer.successful;
}

/* hb_buffer_guess_segment_properties */
export function guessSegmentProperties( buffer ) {
  if ( buffer.props.script === 0 ) {
    for ( let i = 0; i < buffer.len; i++ ) {
      const s = scriptOf( buffer.info[ i ].codepoint );
      if ( s !== 'Zyyy' && s !== 'Zinh' && s !== 'Zzzz' ) {
        buffer.props.script = tagFromString( s );
        break;
      }
    }
  }
  if ( buffer.props.direction === DIRECTION_INVALID ) {
    buffer.props.direction = scriptHorizontalDirection( buffer.props.script );
    if ( buffer.props.direction === DIRECTION_INVALID )
      buffer.props.direction = DIRECTION_LTR;
  }
  /* language: LC_ALL=C gives the language "c", which maps to no OT tag. */
}

/* hb_feature_from_string (hb-common.cc parse_one_feature) */
export function featureFromString( str ) {
  let p = 0;
  const end = str.length;
  const f = { tag: 0, value: 1, start: 0, end: 0xFFFFFFFF };
  const space = () => { while ( p < end && ( str[ p ] === ' ' || str[ p ] === '\t' ) ) p++; };
  const ch = ( c ) => { if ( p < end && str[ p ] === c ) { p++; return true; } return false; };
  const uint = () => {
    const s = p;
    while ( p < end && str[ p ] >= '0' && str[ p ] <= '9' ) p++;
    if ( p === s ) return null;
    return parseInt( str.slice( s, p ), 10 );
  };
  /* prefix */
  if ( ch( '-' ) ) f.value = 0; else { ch( '+' ); f.value = 1; }
  /* tag */
  space();
  let quote = 0;
  if ( p < end && ( str[ p ] === "'" || str[ p ] === '"' ) ) { quote = str[ p ]; p++; }
  const ts = p;
  while ( p < end && str[ p ] !== ' ' && str[ p ] !== '=' && str[ p ] !== '[' &&
          str[ p ] !== quote ) p++;
  if ( p === ts || p - ts > 4 ) return null;
  f.tag = tagFromString( str.slice( ts, p ) );
  if ( quote ) {
    if ( p - ts !== 4 ) return null;
    if ( p === end || str[ p ] !== quote ) return null;
    p++;
  }
  /* indices */
  space();
  if ( ch( '[' ) ) {
    const s = uint();
    if ( s !== null ) f.start = s;
    if ( ch( ':' ) || ch( ';' ) ) {
      const e = uint();
      if ( e !== null ) f.end = e;
    } else if ( s !== null ) f.end = s + 1;
    if ( !ch( ']' ) ) return null;
  }
  /* postfix */
  const hadEqual = ch( '=' );
  let hadValue = false;
  const v = uint();
  if ( v !== null ) { f.value = v; hadValue = true; }
  else {
    const rest = str.slice( p ).toLowerCase();
    if ( rest.startsWith( 'on' ) ) { f.value = 1; hadValue = true; p += 2; }
    else if ( rest.startsWith( 'off' ) ) { f.value = 0; hadValue = true; p += 3; }
  }
  if ( hadEqual && !hadValue ) return null;
  space();
  if ( p !== end ) return null;
  return f;
}
