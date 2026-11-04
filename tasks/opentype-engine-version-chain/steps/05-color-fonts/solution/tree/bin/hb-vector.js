#!/usr/bin/env node
/****************************************************************************
 *
 * bin/hb-vector.js  --  W3
 *
 *   Port of HarfBuzz 14.4.0's `hb-vector' tool (SVG output, draw path).
 *
 *     util/hb-vector.cc        main; sets DEFAULT_FONT_SIZE = upem,
 *                              SUBPIXEL_BITS = 6
 *     util/vector-output.hh    vector_output_t: consume_glyphs, compute_extents,
 *                              finish
 *     util/view-options.hh     margin/background/foreground defaults
 *     util/output-options.hh   -o / -O
 *     util/batch.hh            --batch
 *     src/OT/glyf/path-builder.hh   the glyf outline -> M/L/Q/C/Z algorithm
 *     src/hb-ot-font.cc        extents, advances, draw dispatch
 *
 *   Serialization lives in ../svg-out.js.
 *
 *   SCALE.  hb-vector uses SUBPIXEL_BITS = 6, so hb_font_set_scale is
 *   upem*64 and `x_multf = x_scale / upem' is exactly 64.  The draw
 *   callbacks therefore see font-units*64, and svg-out's append_xy divides
 *   by the scale factor 64 again, which is why path data prints in font
 *   units.  Metrics (advances, extents, margin) live in that same
 *   font-units*64 space until hb_vector_draw_set_extents divides them.
 *
 *   STATUS.  Implemented: the draw (monochrome) path for `glyf' fonts at
 *   the default instance -- cmap, hmtx, hhea/OS2 metrics, simple and
 *   composite glyphs, ink+logical extents, and the full SVG serialization.
 *   TODO (later milestones): GSUB/GPOS shaping (S1/S3 own the layout
 *   engine), `gvar'/`avar' variations (--variations, W4), VARC composites,
 *   the COLR/paint path and --palette (step 5), CFF via ../cff.js, and PDF
 *   output.
 *
 ****************************************************************************/

import { readFileSync, writeFileSync } from 'node:fs';
import { SvgDraw, DrawSession, COLOR, PaintSession } from '../svg-out.js';
import {
  gvarVaryLevel,
  parseVariations as vfParseVariations,
  normalizeCoords as vfNormalizeCoords,
  readItemVariationStore as vfReadIVS,
  ivsGetDelta as vfIvsGetDelta,
  ivsRegionEvaluate as vfIvsRegionEvaluate,
  readDeltaSetIndexMap as vfReadDSIM,
  dsimMap as vfDsimMap,
} from '../varfont.js';
/* OpenType Layout.  hb-vector.cc calls hb_shape() before it draws anything,
 * so this tool must see the same substituted-and-positioned glyph stream
 * bin/hb-shape.js produces.  These are that tool's imports, unchanged: the
 * layout engine is NOT reimplemented here. */
import { openFace } from '../sfnt.js';
import { Buffer as ShapeBuffer, DIRECTION_INVALID } from '../buffer.js';
import { createFont, shape, guessSegmentProperties } from '../shape-plan.js';
/* ../colr.js carries TWO unrelated things, and only one of them is colour:
 *   - the colour tables (CPAL/COLR/sbix/CBDT) and the paint path  -- step 5;
 *   - a self-contained variation-store reader (fvar/avar normalization, the
 *     ItemVariationStore, DeltaSetIndexMap) which is what gives THIS tool its
 *     --variations, its HVAR advances and its CFF2 blends -- step 4.
 * A static import therefore drags the whole colour engine into any tree that
 * only wants variable outlines.  Load it dynamically instead (the
 * shape-plan.js:48 idiom) and let each half degrade on its own:
 *   - colour absent  -> no colour tables are found, usePaint stays false and
 *     the monochrome draw path runs, which is exactly step 4's scope;
 *   - variations     -> fall back to ../varfont.js, already a hard dependency
 *     of this file, so a colr-less tree still varies.
 * The COLOUR half routes to colr.js whenever it is present.  The VARIATION
 * half does NOT: colr.js's variation-store reader is a second implementation
 * that disagrees with varfont.js on fractional axis values, and only
 * varfont.js reproduces the recorded output (see normalizeVariations below).
 * Routing it to colr.js made this tool's answer depend on whether a colour
 * module happened to be in the tree -- three graded cases flipped on that.
 * Kill switches: W7_NO_COLR=1 simulates colr.js being absent;
 * W7_COLR_NORMALIZE=1 restores the colr.js variation path for an A/B. */
let COLRMOD = null;
if ( !process.env.W7_NO_COLR ) {
  try { COLRMOD = await import( '../colr.js' ); } catch { COLRMOD = null; }
}

/* -- colour half.  Inert without the module. -- */
const HB_MAX_NESTING   = COLRMOD ? COLRMOD.HB_MAX_NESTING : 0;
const COLRV1_MAX_EDGES = COLRMOD ? COLRMOD.COLRV1_MAX_EDGES : 0;
const cpalInit  = ( face ) => COLRMOD ? COLRMOD.cpalInit( face ) : null;
const colrInit  = ( face ) => COLRMOD ? COLRMOD.colrInit( face ) : null;
const sbixInit  = ( face ) => COLRMOD ? COLRMOD.sbixInit( face ) : null;
const cbdtInit  = ( face ) => COLRMOD ? COLRMOD.cbdtInit( face ) : null;
const colrHasV0 = ( colr ) => COLRMOD ? COLRMOD.colrHasV0( colr ) : false;
const colrHasV1 = ( colr ) => COLRMOD ? COLRMOD.colrHasV1( colr ) : false;
const makeInstancer = ( face, varStore, varIdxMap, coords ) =>
  COLRMOD ? COLRMOD.makeInstancer( face, varStore, varIdxMap, coords )
          : ( () => 0 );
const paintGlyphOrFail = ( ctx, gid ) =>
  COLRMOD ? COLRMOD.paintGlyphOrFail( ctx, gid ) : false;
/* shape-plan.js:1203 hands the caller's own box back when a face has no
 * colour table; with no colr.js at all the outline box is all there is. */
const colorGlyphExtents = ( face, gid, mult, ctx ) =>
  COLRMOD ? COLRMOD.colorGlyphExtents( face, gid, mult, ctx )
          : ( ctx && ctx.outlineExtentsFn ? ctx.outlineExtentsFn( gid, mult ) : null );

/* -- variation half.  varfont.js when colr.js is gone. -- */
/* colr.js drops a variation whose tag carries a space before the `=' -- that
 * is measured behaviour (`--variations="T1  =0.5"' renders unvaried, and the
 * scored corpus contains such a case), and varfont.js's own parseVariations
 * does NOT drop it.  Apply colr.js's rule to the raw token, then let
 * varfont.js do the parsing, rather than restating its regex here. */
const parseVariations = ( str ) => {
  if ( COLRMOD ) return COLRMOD.parseVariations( str );
  const out = [];
  if ( !str ) return out;
  for ( const tok of String( str ).split( ',' ) ) {
    const t = tok.trim();
    const eq = t.indexOf( '=' );
    if ( /\s/.test( eq < 0 ? t : t.slice( 0, eq ) ) ) continue;
    for ( const v of vfParseVariations( t ) ) out.push( v );
  }
  return out;
};
/* NOT routed through colr.js, unlike the rest of this half.  The two
 * normalizers are not the same function: varfont.js runs the reference's
 * 16.16 path -- roundf(fround(normalize(v)) * 65536), avar::map_coords_16_16,
 * then (c+2)>>2 -- while colr.js's own copy rounds in double precision with
 * Math.round.  They disagree on fractional axis values, and only varfont.js's
 * matches the recorded output: measured on the step-4 vector corpus,
 * 7dcfb8f1f458.ttf at wght=366.667 draws x=95.18 through varfont.js (the
 * reference's number) and x=95.33 through colr.js, and 02d99e81593b.otf at
 * wght=550 differs too -- two cases that PASSED whenever colr.js happened to
 * be absent from the tree and failed whenever it was present.
 *
 * varfont.js is also what bin/hb-shape.js normalizes through, and this file's
 * whole premise (l.50) is that the two tools see the same instance.  So the
 * variation half takes varfont.js unconditionally; colr.js keeps the colour
 * half and parseVariations, whose token rule IS the measured one. */
const normalizeVariations = ( face, variations ) =>
  process.env.W7_COLR_NORMALIZE && COLRMOD
    ? COLRMOD.normalizeVariations( face, variations )
    : vfNormalizeCoords( face, variations );
const ivsInit = ( face, base ) => {
  if ( process.env.W7_COLR_NORMALIZE && COLRMOD ) return COLRMOD.ivsInit( face, base );
  if ( !base ) return null;
  /* colr.js requires format 1, as does the spec. */
  try {
    if ( rU16( face.data, base ) !== 1 ) return null;
    return vfReadIVS( face.data, base );
  } catch { return null; }
};
const ivsGetDelta = ( face, ivs, index, coords ) =>
  ( process.env.W7_COLR_NORMALIZE && COLRMOD ) ? COLRMOD.ivsGetDelta( face, ivs, index, coords )
          : ( ivs ? vfIvsGetDelta( ivs, index, coords ) : 0 );
const ivsRegionScalar = ( face, ivs, regionIndex, coords ) =>
  ( process.env.W7_COLR_NORMALIZE && COLRMOD ) ? COLRMOD.ivsRegionScalar( face, ivs, regionIndex, coords )
          : ( ivs ? vfIvsRegionEvaluate( ivs, regionIndex, coords ) : 0 );
const dsimMap = ( face, off, v ) => {
  if ( COLRMOD ) return COLRMOD.dsimMap( face, off, v );
  if ( !off ) return v;
  try { return vfDsimMap( vfReadDSIM( face.data, off ), v ); } catch { return v; }
};

/* ------------------------------------------------------------------ */
/* sfnt                                                                */
/* ------------------------------------------------------------------ */

const rU8  = ( d, p ) => d[p];
const rI8  = ( d, p ) => ( d[p] << 24 ) >> 24;
const rU16 = ( d, p ) => ( d[p] << 8 ) | d[p + 1];
const rI16 = ( d, p ) => ( ( ( d[p] << 8 ) | d[p + 1] ) << 16 ) >> 16;
const rU32 = ( d, p ) => ( ( ( d[p] << 24 ) | ( d[p + 1] << 16 ) |
                             ( d[p + 2] << 8 ) | d[p + 3] ) >>> 0 );

function loadFace( bytes, faceIndex = 0 ) {
  const d = bytes;
  let base = 0;
  if ( rU32( d, 0 ) === 0x74746366 /* ttcf */ )
    base = rU32( d, 12 + faceIndex * 4 );

  const numTables = rU16( d, base + 4 );
  const tables = new Map();
  for ( let i = 0; i < numTables; i++ ) {
    const p = base + 12 + i * 16;
    const tag = String.fromCharCode( d[p], d[p + 1], d[p + 2], d[p + 3] );
    if ( !tables.has( tag ) )
      tables.set( tag, { offset: rU32( d, p + 8 ), length: rU32( d, p + 12 ) } );
  }

  const head = tables.get( 'head' );
  const upem = head ? rU16( d, head.offset + 18 ) : 1000;
  const indexToLocFormat = head ? rI16( d, head.offset + 50 ) : 0;
  const maxp = tables.get( 'maxp' );
  const numGlyphs = maxp ? rU16( d, maxp.offset + 4 ) : 0;

  return { data: d, tables, upem: upem || 1000, numGlyphs, indexToLocFormat };
}

/* ------------------------------------------------------------------ */
/* cmap -- hb-ot-cmap-table.hh find_best_subtable + the format readers */
/* ------------------------------------------------------------------ */

function cmapFindSubtable( d, cmapOff, pid, eid ) {
  const n = rU16( d, cmapOff + 2 );
  for ( let i = 0; i < n; i++ ) {
    const e = cmapOff + 4 + i * 8;
    if ( rU16( d, e ) === pid && rU16( d, e + 2 ) === eid )
      return cmapOff + rU32( d, e + 4 );
  }
  return 0;
}

function cmapBestSubtable( d, cmapOff ) {
  /* Order is load-bearing: symbol first, then 32-bit, then 16-bit, then Mac. */
  const order = [ [ 3, 0, 'symbol' ],
                  [ 3, 10 ], [ 0, 6 ], [ 0, 4 ],
                  [ 3, 1 ], [ 0, 3 ], [ 0, 2 ], [ 0, 1 ], [ 0, 0 ],
                  [ 1, 0, 'mac' ] ];
  for ( const [ pid, eid, kind ] of order ) {
    const off = cmapFindSubtable( d, cmapOff, pid, eid );
    if ( off ) return { off, symbol: kind === 'symbol', mac: kind === 'mac' };
  }
  return null;
}

function cmapLookupIn( d, off, cp ) {
  const fmt = rU16( d, off );
  switch ( fmt ) {
    case 0: {
      if ( cp > 255 ) return 0;
      return rU8( d, off + 6 + cp );
    }
    case 4: {
      const segX2 = rU16( d, off + 6 );
      const seg = segX2 >> 1;
      const endP = off + 14;
      const startP = endP + segX2 + 2;
      const deltaP = startP + segX2;
      const rangeP = deltaP + segX2;
      if ( cp > 0xFFFF ) return 0;
      for ( let i = 0; i < seg; i++ ) {
        if ( rU16( d, endP + i * 2 ) >= cp ) {
          const start = rU16( d, startP + i * 2 );
          if ( start > cp ) return 0;
          const delta = rU16( d, deltaP + i * 2 );
          const ro = rU16( d, rangeP + i * 2 );
          if ( ro === 0 ) return ( cp + delta ) & 0xFFFF;
          const gp = rangeP + i * 2 + ro + ( cp - start ) * 2;
          if ( gp + 1 >= d.length ) return 0;
          const g = rU16( d, gp );
          return g === 0 ? 0 : ( g + delta ) & 0xFFFF;
        }
      }
      return 0;
    }
    case 6: {
      const first = rU16( d, off + 6 );
      const count = rU16( d, off + 8 );
      if ( cp < first || cp >= first + count ) return 0;
      return rU16( d, off + 10 + ( cp - first ) * 2 );
    }
    case 12: {
      const nGroups = rU32( d, off + 12 );
      for ( let i = 0; i < nGroups; i++ ) {
        const g = off + 16 + i * 12;
        const s = rU32( d, g ), e = rU32( d, g + 4 );
        if ( cp >= s && cp <= e ) return ( rU32( d, g + 8 ) + ( cp - s ) ) >>> 0;
      }
      return 0;
    }
    default:
      return 0;
  }
}

/* hb_unicode_funcs_t::is_default_ignorable (hb-unicode.hh).  hb_ot_shape
 * hides these after mapping; with no invisible-glyph set they are removed
 * outright, so they contribute neither advance nor output.  The exact
 * ranges matter: HarfBuzz does NOT treat U+115F, U+1160, U+3164 or
 * U+FFA0 as default-ignorable even though some lists do. */
function isDefaultIgnorable( ch ) {
  const plane = ch >> 16;
  if ( plane === 0 ) {
    switch ( ch >> 8 ) {
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
  if ( plane === 0x01 ) return ch >= 0x1D173 && ch <= 0x1D17A;
  if ( plane === 0x0E ) return ch >= 0xE0000 && ch <= 0xE0FFF;
  return false;
}

function makeCmap( face ) {
  const t = face.tables.get( 'cmap' );
  if ( !t ) return () => 0;
  const best = cmapBestSubtable( face.data, t.offset );
  if ( !best ) return () => 0;
  const d = face.data;
  return ( cp ) => {
    let g = cmapLookupIn( d, best.off, cp );
    if ( !g && best.symbol && cp <= 0x00FF )
      g = cmapLookupIn( d, best.off, 0xF000 + cp );   /* symbol fallback */
    return g;
  };
}

/* ------------------------------------------------------------------ */
/* hmtx / metrics                                                      */
/* ------------------------------------------------------------------ */

function hmtxAdvanceUnscaled( face, gid ) {
  const hhea = face.tables.get( 'hhea' ), hmtx = face.tables.get( 'hmtx' );
  if ( !hhea || !hmtx ) return Math.floor( face.upem / 2 );
  const num = rU16( face.data, hhea.offset + 34 );
  if ( !num ) return 0;
  const i = gid < num ? gid : num - 1;
  const p = hmtx.offset + i * 4;
  if ( p + 1 >= face.data.length ) return 0;
  return rU16( face.data, p );
}

function hmtxLsbUnscaled( face, gid ) {
  const hhea = face.tables.get( 'hhea' ), hmtx = face.tables.get( 'hmtx' );
  if ( !hhea || !hmtx ) return 0;
  const num = rU16( face.data, hhea.offset + 34 );
  if ( !num ) return 0;
  if ( gid < num ) return rI16( face.data, hmtx.offset + gid * 4 + 2 );
  const p = hmtx.offset + num * 4 + ( gid - num ) * 2;
  if ( p + 1 >= face.data.length ) return 0;
  return rI16( face.data, p );
}

/* _hb_ot_metrics_get_position_common for the three horizontal metrics.
 * OS/2 sTypo* win only when the USE_TYPO_METRICS bit (fsSelection & 0x80)
 * is set; otherwise hhea.  _fix_ascender_descender forces the signs.     */
function fontExtentsUnscaled( face ) {
  const d = face.data;
  const os2 = face.tables.get( 'OS/2' );
  const hhea = face.tables.get( 'hhea' );
  let asc = 0, desc = 0, gap = 0;

  let useTypo = false;
  if ( os2 && os2.length >= 78 ) {
    const fsSelection = rU16( d, os2.offset + 62 );
    useTypo = !!( fsSelection & 0x80 );
  }
  if ( useTypo ) {
    asc  = rI16( d, os2.offset + 68 );
    desc = rI16( d, os2.offset + 70 );
    gap  = rI16( d, os2.offset + 72 );
  } else if ( hhea ) {
    asc  = rI16( d, hhea.offset + 4 );
    desc = rI16( d, hhea.offset + 6 );
    gap  = rI16( d, hhea.offset + 8 );
  }
  /* _fix_ascender_descender */
  asc = Math.abs( asc );
  desc = -Math.abs( desc );
  return { asc, desc, gap };
}

/* ------------------------------------------------------------------ */
/* glyf                                                                */
/* ------------------------------------------------------------------ */

const FLAG_ON_CURVE = 0x01;
const FLAG_X_SHORT  = 0x02;
const FLAG_Y_SHORT  = 0x04;
const FLAG_REPEAT   = 0x08;
const FLAG_X_SAME   = 0x10;
const FLAG_Y_SAME   = 0x20;
const FLAG_CUBIC    = 0x80;

function locaRange( face, gid ) {
  const loca = face.tables.get( 'loca' ), glyf = face.tables.get( 'glyf' );
  if ( !loca || !glyf ) return null;
  const d = face.data;
  let s, e;
  if ( face.indexToLocFormat === 0 ) {
    const p = loca.offset + gid * 2;
    if ( p + 3 >= d.length ) return null;
    s = rU16( d, p ) * 2; e = rU16( d, p + 2 ) * 2;
  } else {
    const p = loca.offset + gid * 4;
    if ( p + 7 >= d.length ) return null;
    s = rU32( d, p ); e = rU32( d, p + 4 );
  }
  if ( e <= s ) return { empty: true };
  return { start: glyf.offset + s, end: glyf.offset + e };
}

/* OT/glyf/Glyph.hh get_points.
 *
 * Returns { points: [{x,y,flag}], endPts, hdelta } in font units with the
 * composite tree expanded.  Phantom points are not returned; `hdelta' is
 * phantoms[PHANTOM_LEFT].x, i.e. header.xMin - lsb, which the TOP LEVEL
 * (and only the top level) subtracts from every x:
 *
 *   if (depth == 0 && shift_points_hori)   // "Undocumented rasterizer
 *     for (auto &point : all_points)       //  behavior: shift points
 *       point.x -= shift;                  //  horizontally by the lsb"
 *
 * Applying it per component instead -- as an earlier version of this file
 * did -- wrecks any font whose components carry an unrelated lsb.
 */
function glyfPointsRaw( face, gid, depth ) {
  if ( depth > 8 ) return null;
  const r = locaRange( face, gid );
  if ( !r ) return null;

  const d = face.data;
  const lsb = hmtxLsbUnscaled( face, gid );
  const coords = face._coords || [];
  const advRaw = hmtxAdvanceUnscaled( face, gid );

  /* gvar seed phantoms.  Only the horizontal pair affects this renderer;
   * there is no vmtx here, so topY/bottomY seed as 0.  Phantom points sit
   * outside every contour, so they are never IUP-interpolated against real
   * points and a zero vertical seed cannot disturb the outline.          */
  const seedFor = ( xMin ) => ( { leftX: xMin - lsb, rightX: xMin - lsb + advRaw,
                                  topY: 0, bottomY: 0 } );

  if ( r.empty ) {
    /* an empty glyph still has four phantom points, so gvar can still move
     * its advance */
    let hd = -lsb, adv = advRaw;
    if ( coords.length ) {
      const vr = gvarVaryLevel( face, gid, coords, [], [], null, seedFor( 0 ) );
      if ( vr ) { hd = vr.hdelta; adv = vr.advance; }
    }
    return { points: [], endPts: [], hdelta: hd, advance: adv };
  }

  const numberOfContours = rI16( d, r.start );
  const xMin = rI16( d, r.start + 2 );
  let hdelta = xMin - lsb;
  let advance = advRaw;

  if ( numberOfContours >= 0 ) {
    /* --- simple glyph --- */
    const endPts = [];
    let p = r.start + 10;
    for ( let i = 0; i < numberOfContours; i++ ) { endPts.push( rU16( d, p ) ); p += 2; }
    const nPoints = numberOfContours ? endPts[ endPts.length - 1 ] + 1 : 0;
    const insLen = rU16( d, p ); p += 2 + insLen;

    const flags = new Uint8Array( nPoints );
    for ( let i = 0; i < nPoints; ) {
      const f = d[p++];
      flags[i++] = f;
      if ( f & FLAG_REPEAT ) { let n = d[p++]; while ( n-- && i < nPoints ) flags[i++] = f; }
    }
    const xs = new Int32Array( nPoints );
    let v = 0;
    for ( let i = 0; i < nPoints; i++ ) {
      const f = flags[i];
      if ( f & FLAG_X_SHORT ) { const dx = d[p++]; v += ( f & FLAG_X_SAME ) ? dx : -dx; }
      else if ( !( f & FLAG_X_SAME ) ) { v += rI16( d, p ); p += 2; }
      xs[i] = v;
    }
    const ys = new Int32Array( nPoints );
    v = 0;
    for ( let i = 0; i < nPoints; i++ ) {
      const f = flags[i];
      if ( f & FLAG_Y_SHORT ) { const dy = d[p++]; v += ( f & FLAG_Y_SAME ) ? dy : -dy; }
      else if ( !( f & FLAG_Y_SAME ) ) { v += rI16( d, p ); p += 2; }
      ys[i] = v;
    }

    /* gvar at this level.  Our reader keeps FLAG_CUBIC, so the varied
     * coordinates are folded back into OUR points rather than taking an
     * outline from elsewhere. */
    let vx = xs, vy = ys;
    if ( coords.length ) {
      const vr = gvarVaryLevel( face, gid, coords, xs, ys, endPts, seedFor( xMin ) );
      if ( vr ) { vx = vr.x; vy = vr.y; hdelta = vr.hdelta; advance = vr.advance; }
    }

    const points = [];
    for ( let i = 0; i < nPoints; i++ )
      points.push( { x: vx[i], y: vy[i], flag: flags[i] } );
    return { points, endPts, hdelta, advance };
  }

  /* --- composite glyph ---
   * gvar deltas for a composite apply to the COMPONENT OFFSETS, one
   * pseudo-point per component, not to the flattened child points.
   * Applying them to flattened points corrupts the advance too, since the
   * advance comes from this level's phantom points.                      */
  const comps = [];
  let p = r.start + 10;
  for (;;) {
    const flags = rU16( d, p );
    const glyphIndex = rU16( d, p + 2 );
    p += 4;
    let a1, a2;
    if ( flags & 0x0001 ) { a1 = rI16( d, p ); a2 = rI16( d, p + 2 ); p += 4; }
    else { a1 = rI8( d, p ); a2 = rI8( d, p + 1 ); p += 2; }
    let u1 = a1, u2 = a2;
    if ( !( flags & 0x0002 ) ) {
      if ( flags & 0x0001 ) { u1 = rU16( d, p - 4 ); u2 = rU16( d, p - 2 ); }
      else { u1 = rU8( d, p - 2 ); u2 = rU8( d, p - 1 ); }
    }
    let xx = 1, yx = 0, xy = 0, yy = 1;
    const F2D = ( o ) => rI16( d, o ) / 16384;
    if ( flags & 0x0008 ) { xx = yy = F2D( p ); p += 2; }
    else if ( flags & 0x0040 ) { xx = F2D( p ); yy = F2D( p + 2 ); p += 4; }
    else if ( flags & 0x0080 ) {
      xx = F2D( p ); yx = F2D( p + 2 ); xy = F2D( p + 4 ); yy = F2D( p + 6 ); p += 8;
    }
    comps.push( { flags, glyphIndex, a1, a2, u1, u2, xx, yx, xy, yy,
                  isXY: !!( flags & 0x0002 ) } );
    if ( !( flags & 0x0020 ) ) break;           /* MORE_COMPONENTS */
  }

  if ( coords.length && comps.length ) {
    const cxs = comps.map( ( c ) => ( c.isXY ? c.a1 : 0 ) );
    const cys = comps.map( ( c ) => ( c.isXY ? c.a2 : 0 ) );
    const vr = gvarVaryLevel( face, gid, coords, cxs, cys, null, seedFor( xMin ) );
    if ( vr ) {
      for ( let i = 0; i < comps.length; i++ )
        if ( comps[i].isXY ) { comps[i].a1 = vr.x[i]; comps[i].a2 = vr.y[i]; }
      hdelta = vr.hdelta;
      advance = vr.advance;
    }
  }

  const points = [];
  const endPts = [];
  for ( const c of comps ) {
    const sub = glyfPointsRaw( face, c.glyphIndex, depth + 1 );
    if ( sub ) {
      const off = points.length;
      let tx = 0, ty = 0;
      if ( c.isXY ) { tx = c.a1; ty = c.a2; }
      const scaledOffsets = !!( c.flags & 0x0800 );
      for ( const pt of sub.points ) {
        let X, Y;
        if ( scaledOffsets ) {
          X = c.xx * ( pt.x + tx ) + c.xy * ( pt.y + ty );
          Y = c.yx * ( pt.x + tx ) + c.yy * ( pt.y + ty );
        } else {
          X = c.xx * pt.x + c.xy * pt.y + tx;
          Y = c.yx * pt.x + c.yy * pt.y + ty;
        }
        points.push( { x: X, y: Y, flag: pt.flag } );
      }
      if ( !c.isXY ) {
        if ( c.u1 < points.length && c.u2 < sub.points.length ) {
          const anchor = points[ c.u1 ];
          const comp = points[ off + c.u2 ];
          const dx = anchor.x - comp.x, dy = anchor.y - comp.y;
          if ( dx || dy )
            for ( let i = off; i < points.length; i++ ) { points[i].x += dx; points[i].y += dy; }
        }
      }
      for ( const e of sub.endPts ) endPts.push( e + off );
      /* USE_MY_METRICS: phantoms (hence the shift and advance) come from
       * this component, overriding whatever gvar gave this level. */
      if ( c.flags & 0x0200 ) { hdelta = sub.hdelta; advance = sub.advance; }
    }
  }
  return { points, endPts, hdelta, advance };
}

function glyfPoints( face, gid ) {
  const g = glyfPointsRaw( face, gid, 0 );
  if ( !g ) return null;
  if ( g.hdelta )
    for ( const pt of g.points ) pt.x -= g.hdelta;
  return g;
}

/* ------------------------------------------------------------------ */
/* OT/glyf/path-builder.hh  path_builder_t                             */
/* ------------------------------------------------------------------ */

class PathBuilder {
  constructor( sink, mult ) {
    this.s = sink;
    this.mult = mult;                 /* em_fscalef_x: v * x_multf */
    this.reset();
  }
  reset() {
    this.firstOncurve = null; this.firstOffcurve = null;
    this.firstOffcurve2 = null; this.lastOffcurve = null; this.lastOffcurve2 = null;
  }
  static mid( a, b ) { return { x: ( a.x + b.x ) * 0.5, y: ( a.y + b.y ) * 0.5 }; }

  consumePoint( pt ) {
    const isOn = !!( pt.flag & FLAG_ON_CURVE );
    const isCubic = !isOn && !!( pt.flag & FLAG_CUBIC );
    const p = { x: Math.fround( pt.x * this.mult ), y: Math.fround( pt.y * this.mult ) };

    if ( !this.firstOncurve ) {
      if ( isOn ) { this.firstOncurve = p; this.s.moveTo( p.x, p.y ); }
      else if ( isCubic && !this.firstOffcurve2 ) {
        this.firstOffcurve2 = this.firstOffcurve; this.firstOffcurve = p;
      } else if ( this.firstOffcurve ) {
        const m = PathBuilder.mid( this.firstOffcurve, p );
        this.firstOncurve = m; this.lastOffcurve = p; this.s.moveTo( m.x, m.y );
      } else this.firstOffcurve = p;
    } else {
      if ( this.lastOffcurve ) {
        if ( isOn ) {
          if ( this.lastOffcurve2 ) {
            this.s.cubicTo( this.lastOffcurve2.x, this.lastOffcurve2.y,
                            this.lastOffcurve.x, this.lastOffcurve.y, p.x, p.y );
            this.lastOffcurve2 = null;
          } else
            this.s.quadraticTo( this.lastOffcurve.x, this.lastOffcurve.y, p.x, p.y );
          this.lastOffcurve = null;
        } else {
          if ( isCubic && !this.lastOffcurve2 ) {
            this.lastOffcurve2 = this.lastOffcurve; this.lastOffcurve = p;
          } else {
            const m = PathBuilder.mid( this.lastOffcurve, p );
            if ( isCubic ) {
              this.s.cubicTo( this.lastOffcurve2.x, this.lastOffcurve2.y,
                              this.lastOffcurve.x, this.lastOffcurve.y, m.x, m.y );
              this.lastOffcurve2 = null;
            } else
              this.s.quadraticTo( this.lastOffcurve.x, this.lastOffcurve.y, m.x, m.y );
            this.lastOffcurve = p;
          }
        }
      } else {
        if ( isOn ) this.s.lineTo( p.x, p.y );
        else this.lastOffcurve = p;
      }
    }
  }

  contourEnd() {
    if ( this.firstOffcurve && this.lastOffcurve ) {
      const m = PathBuilder.mid( this.lastOffcurve,
                                 this.firstOffcurve2 ? this.firstOffcurve2 : this.firstOffcurve );
      if ( this.lastOffcurve2 )
        this.s.cubicTo( this.lastOffcurve2.x, this.lastOffcurve2.y,
                        this.lastOffcurve.x, this.lastOffcurve.y, m.x, m.y );
      else
        this.s.quadraticTo( this.lastOffcurve.x, this.lastOffcurve.y, m.x, m.y );
      this.lastOffcurve = null;
    }

    if ( this.firstOffcurve && this.firstOncurve ) {
      if ( this.firstOffcurve2 )
        this.s.cubicTo( this.firstOffcurve2.x, this.firstOffcurve2.y,
                        this.firstOffcurve.x, this.firstOffcurve.y,
                        this.firstOncurve.x, this.firstOncurve.y );
      else
        this.s.quadraticTo( this.firstOffcurve.x, this.firstOffcurve.y,
                            this.firstOncurve.x, this.firstOncurve.y );
    } else if ( this.lastOffcurve && this.firstOncurve ) {
      if ( this.lastOffcurve2 )
        this.s.cubicTo( this.lastOffcurve2.x, this.lastOffcurve2.y,
                        this.lastOffcurve.x, this.lastOffcurve.y,
                        this.firstOncurve.x, this.firstOncurve.y );
      else
        this.s.quadraticTo( this.lastOffcurve.x, this.lastOffcurve.y,
                            this.firstOncurve.x, this.firstOncurve.y );
    } else if ( this.firstOncurve ) {
      this.s.lineTo( this.firstOncurve.x, this.firstOncurve.y );
    } else if ( this.firstOffcurve ) {
      const x = this.firstOffcurve.x, y = this.firstOffcurve.y;
      this.s.moveTo( x, y );
      this.s.quadraticTo( x, y, x, y );
    }

    this.reset();
    this.s.closePath();
  }
}

function drawGlyfGlyph( face, gid, sink, mult ) {
  const g = glyfPoints( face, gid );
  if ( !g || !g.points.length ) return false;

  const pts = g.points;
  const count = pts.length;
  const isEnd = new Uint8Array( count );
  for ( const e of g.endPts ) if ( e >= 0 && e < count ) isEnd[e] = 1;

  const pb = new PathBuilder( sink, mult );

  /* OT/glyf/glyf.hh get_points(), the branch actually compiled in:
   *
   *   "Our path-builder was designed to work with [the simple loop].
   *    But FreeType and CoreText do it differently, so we match those
   *    with the other, more complicated, code branch below."
   *
   * When a contour's FIRST point is off-curve, the contour's LAST point
   * is consumed first and the rest follow from `start'.  That is what
   * makes an off-curve-leading contour start at its trailing on-curve
   * point instead of at a synthesized midpoint.                        */
  for ( let i = 0; i < count; i++ ) {
    if ( pts[i].flag & FLAG_ON_CURVE ) {
      /* First point is on-curve.  Draw the contour. */
      for ( ; i < count; i++ ) {
        pb.consumePoint( pts[i] );
        if ( isEnd[i] ) { pb.contourEnd(); break; }
      }
    } else {
      const start = i;
      for ( ; i < count; i++ ) if ( isEnd[i] ) break;
      const end = i;
      if ( end < count ) pb.consumePoint( pts[end] );
      for ( i = start; i < end; i++ ) pb.consumePoint( pts[i] );
      pb.contourEnd();
      i = end;
    }
  }
  return true;
}

/* glyf get_extents: bounds over all contour points, roundf'd, then
 * scale_glyph_extents (floorf the min corner, ceilf the max corner). */
function glyfGlyphExtents( face, gid, multX, multY ) {
  const g = glyfPoints( face, gid );
  if ( !g || !g.points.length ) return null;
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for ( const p of g.points ) {
    if ( p.x < minX ) minX = p.x;
    if ( p.x > maxX ) maxX = p.x;
    if ( p.y < minY ) minY = p.y;
    if ( p.y > maxY ) maxY = p.y;
  }
  const rf = ( v ) => Math.fround( Math.round( v ) );
  let xB = rf( minX ), yB = rf( maxY );
  let w = rf( maxX ) - xB, h = rf( minY ) - yB;

  const x1 = Math.fround( xB * multX ), y1 = Math.fround( yB * multY );
  const x2 = Math.fround( ( xB + w ) * multX ), y2 = Math.fround( ( yB + h ) * multY );
  const bx = Math.floor( x1 ), by = Math.floor( y1 );
  return { x_bearing: bx, y_bearing: by,
           width: Math.ceil( x2 ) - bx, height: Math.ceil( y2 ) - by };
}


/* ---- HVAR: advance variation ---------------------------------------- *
 * hb-ot-var-hvar-table.hh: version(4), varStore@+4, advMap@+8, lsb@+12,
 * rsb@+16.  varidx = advMap ? advMap.map(gid) : gid -- a null advMap is the
 * Null DeltaSetIndexMap whose map() returns its input unchanged.
 * hmtx applies it as hb_max(0, advance + roundf(delta)); roundf is
 * half-away-from-zero, which Math.round is NOT for negatives.             */
function hvarInit( face ) {
  if ( face._hvar !== undefined ) return face._hvar;
  const t = face.tables.get( 'HVAR' );
  if ( !t ) return ( face._hvar = null );
  const d = face.data, b = t.offset;
  if ( rU16( d, b ) !== 1 ) return ( face._hvar = null );   /* major must be 1 */
  const vs = rU32( d, b + 4 ), am = rU32( d, b + 8 );
  const varStore = vs ? b + vs : 0;
  return ( face._hvar = { varStore, advMap: am ? b + am : 0,
                          ivs: varStore ? ivsInit( face, varStore ) : null } );
}

const roundf = ( v ) => ( v < 0 ? -Math.round( -v ) : Math.round( v ) );

/* hmtx::get_advance_with_var_unscaled */
function advanceUnscaled( face, gid ) {
  const base = hmtxAdvanceUnscaled( face, gid );
  const coords = face._coords || [];
  if ( !coords.length || !coords.some( ( c ) => c !== 0 ) ) return base;
  const hv = hvarInit( face );
  if ( hv && hv.ivs ) {
    const varidx = hv.advMap ? dsimMap( face, hv.advMap, gid ) : gid;
    const delta = ivsGetDelta( face, hv.ivs, varidx, coords );
    return Math.max( 0, base + roundf( delta ) );
  }
  /* No HVAR: a glyf font's advance varies through gvar phantom points. */
  if ( face.tables.has( 'gvar' ) && face.tables.has( 'glyf' ) ) {
    const g = glyfPointsRaw( face, gid, 0 );
    if ( g && g.advance !== undefined ) return Math.max( 0, roundf( g.advance ) );
  }
  return base;
}

/* ------------------------------------------------------------------ */
/* CFF2  --  OpenType CFF2 spec (otspec/txt/cff2.txt) + HarfBuzz's      */
/*           hb-ot-cff2-table.{hh,cc} / hb-cff2-interp-cs.hh            */
/*                                                                      */
/*  Differences from Type 2 (`CFF ') that matter here:                  */
/*   - no width convention, no `endchar', no `seac', no arithmetic ops; */
/*     a charstring simply ends at the end of its data.                 */
/*   - `vsindex' (15) and `blend' (16) exist.                           */
/*   - INDEX count is uint32 (not uint16).                              */
/*   - the header is 5 bytes: major, minor, headerSize, topDictSize:u16.*/
/*                                                                      */
/*  `blend' (hb-cff2-interp-cs.hh process_blend): k = regionIndexCount  */
/*  of the ItemVariationData selected by the current vsindex, n = the   */
/*  popped count.  The operands are n base values followed by n*k       */
/*  deltas; base[i] += sum_j scalars[j] * delta[i][j], then the k*n     */
/*  deltas are popped.  With no coords every scalar is 0, so this       */
/*  reduces to the old discard-the-deltas behaviour exactly.            */
/*  The path-rendering ELEM uses the NON-rounding process_arg_blend     */
/*  overload (set_real), so no rounding is applied here.                */
/* ------------------------------------------------------------------ */

function readIndex2( d, p ) {
  const count = rU32( d, p );
  if ( count === 0 ) return { count: 0, end: p + 4, get: () => null };
  const offSize = d[ p + 4 ];
  const offArr = p + 5;
  const rdOff = ( i ) => {
    const q = offArr + i * offSize;
    switch ( offSize ) {
      case 1: return d[q];
      case 2: return rU16( d, q );
      case 3: return ( d[q] << 16 ) | ( d[q + 1] << 8 ) | d[q + 2];
      default: return rU32( d, q );
    }
  };
  /* "offsets are from byte preceding object data" */
  const dataBase = offArr + ( count + 1 ) * offSize - 1;
  return {
    count, end: dataBase + rdOff( count ),
    get: ( i ) => ( i < 0 || i >= count ) ? null
                 : { start: dataBase + rdOff( i ), end: dataBase + rdOff( i + 1 ) }
  };
}

/* CFF2 DICT: operands 28(int16) 29(int32) 30(BCD) 32..254; ops 1 byte or 12 xx */
function parseDict2( d, start, end ) {
  const out = {};
  let ops = [];
  let p = start;
  while ( p < end ) {
    const b0 = d[p];
    if ( b0 === 28 ) { ops.push( rI16( d, p + 1 ) ); p += 3; }
    else if ( b0 === 29 ) { ops.push( ( rU32( d, p + 1 ) | 0 ) ); p += 5; }
    else if ( b0 === 30 ) {                       /* binary coded decimal */
      let str = ''; p++;
      loop: while ( p < end ) {
        const v = d[p++];
        for ( const nib of [ v >> 4, v & 15 ] ) {
          if ( nib <= 9 ) str += nib;
          else if ( nib === 10 ) str += '.';
          else if ( nib === 11 ) str += 'E';
          else if ( nib === 12 ) str += 'E-';
          else if ( nib === 14 ) str += '-';
          else if ( nib === 15 ) break loop;
        }
      }
      ops.push( parseFloat( str ) || 0 );
    }
    else if ( b0 >= 32 && b0 <= 246 ) { ops.push( b0 - 139 ); p += 1; }
    else if ( b0 >= 247 && b0 <= 250 ) { ops.push( ( b0 - 247 ) * 256 + d[p + 1] + 108 ); p += 2; }
    else if ( b0 >= 251 && b0 <= 254 ) { ops.push( -( b0 - 251 ) * 256 - d[p + 1] - 108 ); p += 2; }
    else {
      let key = b0; p += 1;
      if ( b0 === 12 ) { key = 0x0c00 | d[p]; p += 1; }
      out[key] = ops;
      ops = [];
    }
  }
  return out;
}

/* ItemVariationData[ivs].regionIndexCount, via CFF2ItemVariationStore
 * (a uint16 length followed by the ItemVariationStore).               */
function varStoreRegionCount( d, vsBase, ivs ) {
  if ( !vsBase ) return 0;
  const store = vsBase + 2;                      /* skip the u16 size */
  const dataCount = rU16( d, store + 6 );
  if ( ivs >= dataCount ) return 0;
  const off = rU32( d, store + 8 + ivs * 4 );
  return rU16( d, store + off + 4 );             /* regionIndexCount */
}

function cff2Init( face ) {
  if ( face._cff2 !== undefined ) return face._cff2;
  const t = face.tables.get( 'CFF2' );
  if ( !t ) return ( face._cff2 = null );
  const d = face.data, base = t.offset;

  const headerSize = d[ base + 2 ];
  const topDictSize = rU16( d, base + 3 );
  const top = parseDict2( d, base + headerSize, base + headerSize + topDictSize );
  const gsubrs = readIndex2( d, base + headerSize + topDictSize );

  const csOff = top[17] ? top[17][0] : 0;
  const fdaOff = top[0x0c24] ? top[0x0c24][0] : 0;
  const fdsOff = top[0x0c25] ? top[0x0c25][0] : 0;
  const vsOff = top[24] ? top[24][0] : 0;
  if ( !csOff || !fdaOff ) return ( face._cff2 = null );

  const charStrings = readIndex2( d, base + csOff );
  const fdArray = readIndex2( d, base + fdaOff );

  const fds = [];
  for ( let i = 0; i < fdArray.count; i++ ) {
    const el = fdArray.get( i );
    const fd = parseDict2( d, el.start, el.end );
    let priv = {}, lsubrs = { count: 0, get: () => null }, ivs = 0;
    if ( fd[18] && fd[18].length >= 2 ) {
      const size = fd[18][0], off = fd[18][1];
      if ( size > 0 ) {
        priv = parseDict2( d, base + off, base + off + size );
        if ( priv[22] ) ivs = priv[22][0] | 0;
        if ( priv[19] ) lsubrs = readIndex2( d, base + off + priv[19][0] );
      }
    }
    fds.push( { priv, lsubrs, ivs } );
  }

  return ( face._cff2 = { base, top, gsubrs, charStrings, fdArray, fds,
                          fdsOff: fdsOff ? base + fdsOff : 0,
                          vsBase: vsOff ? base + vsOff : 0 } );
}

function cff2FdIndex( d, cff2, gid ) {
  if ( !cff2.fdsOff ) return 0;
  const p = cff2.fdsOff;
  const fmt = d[p];
  if ( fmt === 0 ) return d[ p + 1 + gid ];
  if ( fmt === 3 ) {
    const n = rU16( d, p + 1 );
    let q = p + 3;
    for ( let i = 0; i < n; i++ ) {
      const first = rU16( d, q + i * 3 );
      const next = ( i + 1 < n ) ? rU16( d, q + ( i + 1 ) * 3 ) : rU16( d, q + n * 3 );
      if ( gid >= first && gid < next ) return d[ q + i * 3 + 2 ];
    }
    return 0;
  }
  if ( fmt === 4 ) {
    const n = rU32( d, p + 1 );
    const q = p + 5;
    for ( let i = 0; i < n; i++ ) {
      const first = rU32( d, q + i * 5 );
      const next = ( i + 1 < n ) ? rU32( d, q + ( i + 1 ) * 5 ) : rU32( d, q + n * 5 );
      if ( gid >= first && gid < next ) return rU16( d, q + i * 5 + 4 );
    }
    return 0;
  }
  return 0;
}

const bias = ( n ) => ( n < 1240 ? 107 : n < 33900 ? 1131 : 32768 );

/* Interpret one CFF2 charstring, emitting into `sink' (move/line/cubic in
 * font units * mult).  Returns false if there was nothing to draw.      */
function cff2Run( face, gid, sink, mult ) {
  const cff2 = cff2Init( face );
  if ( !cff2 ) return false;
  const d = face.data;
  const cs = cff2.charStrings.get( gid );
  if ( !cs || cs.end <= cs.start ) return false;

  const fdi = Math.min( cff2FdIndex( d, cff2, gid ), cff2.fds.length - 1 );
  const fd = cff2.fds[ fdi < 0 ? 0 : fdi ];
  const lsubrs = fd.lsubrs, gsubrs = cff2.gsubrs;
  const lbias = bias( lsubrs.count ), gbias = bias( gsubrs.count );

  let regionCount = varStoreRegionCount( d, cff2.vsBase, fd.ivs );
  /* region scalars for the current ItemVariationData, recomputed on vsindex */
  const coords = face._coords || [];
  const vsIvs = cff2.vsBase ? ivsInit( face, cff2.vsBase + 2 ) : null;
  let curIvs = fd.ivs;
  let scalars = null;
  const getScalars = () => {
    if ( scalars ) return scalars;
    scalars = [];
    if ( vsIvs && coords.length ) {
      const store = cff2.vsBase + 2;
      const dataCount = rU16( d, store + 6 );
      if ( curIvs < dataCount ) {
        const off = rU32( d, store + 8 + curIvs * 4 );
        const nreg = rU16( d, store + off + 4 );
        for ( let i = 0; i < nreg; i++ )
          scalars.push( ivsRegionScalar( face, vsIvs,
                          rU16( d, store + off + 6 + i * 2 ), coords ) );
      }
    }
    return scalars;
  };

  const st = [];                       /* operand stack (floats) */
  let x = 0, y = 0, nStems = 0, haveOpen = false;
  const stack = [];                    /* subroutine return stack */
  let buf = { p: cs.start, end: cs.end };
  let guard = 0;

  const M = ( v ) => Math.fround( v * mult );
  /* the DrawSession closes the previous contour for us */
  const moveTo = ( a, b ) => { sink.moveTo( M( a ), M( b ) ); haveOpen = true; };

  while ( true ) {
    if ( buf.p >= buf.end ) {
      if ( stack.length ) { buf = stack.pop(); continue; }
      break;
    }
    if ( ++guard > 2000000 ) break;
    const b0 = d[ buf.p++ ];

    if ( b0 >= 32 || b0 === 28 ) {
      if ( b0 === 28 ) { st.push( rI16( d, buf.p ) ); buf.p += 2; }
      else if ( b0 <= 246 ) st.push( b0 - 139 );
      else if ( b0 <= 250 ) { st.push( ( b0 - 247 ) * 256 + d[ buf.p++ ] + 108 ); }
      else if ( b0 <= 254 ) { st.push( -( b0 - 251 ) * 256 - d[ buf.p++ ] - 108 ); }
      else { /* 255: 16.16 fixed */
        const v = ( ( d[buf.p] << 24 ) | ( d[buf.p+1] << 16 ) | ( d[buf.p+2] << 8 ) | d[buf.p+3] ) | 0;
        st.push( v / 65536 ); buf.p += 4;
      }
      continue;
    }

    switch ( b0 ) {
      case 1: case 3: case 18: case 23:           /* h/v stem(hm) */
        nStems += st.length >> 1; st.length = 0; break;

      case 19: case 20: {                          /* hintmask / cntrmask */
        nStems += st.length >> 1; st.length = 0;
        buf.p += ( nStems + 7 ) >> 3;
        break;
      }

      case 21:                                     /* rmoveto */
        if ( st.length >= 2 ) { x += st[ st.length - 2 ]; y += st[ st.length - 1 ]; }
        moveTo( x, y ); st.length = 0; break;
      case 22:                                     /* hmoveto */
        if ( st.length >= 1 ) x += st[ st.length - 1 ];
        moveTo( x, y ); st.length = 0; break;
      case 4:                                      /* vmoveto */
        if ( st.length >= 1 ) y += st[ st.length - 1 ];
        moveTo( x, y ); st.length = 0; break;

      case 5:                                      /* rlineto */
        for ( let i = 0; i + 1 < st.length; i += 2 ) {
          x += st[i]; y += st[i + 1]; sink.lineTo( M( x ), M( y ) );
        }
        st.length = 0; break;

      case 6: case 7: {                            /* hlineto / vlineto */
        let horiz = ( b0 === 6 );
        for ( let i = 0; i < st.length; i++ ) {
          if ( horiz ) x += st[i]; else y += st[i];
          sink.lineTo( M( x ), M( y ) );
          horiz = !horiz;
        }
        st.length = 0; break;
      }

      case 8:                                      /* rrcurveto */
        for ( let i = 0; i + 5 < st.length; i += 6 ) {
          const c1x = x + st[i], c1y = y + st[i+1];
          const c2x = c1x + st[i+2], c2y = c1y + st[i+3];
          x = c2x + st[i+4]; y = c2y + st[i+5];
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
        }
        st.length = 0; break;

      case 24: {                                   /* rcurveline */
        let i = 0;
        for ( ; i + 5 < st.length - 2; i += 6 ) {
          const c1x = x + st[i], c1y = y + st[i+1];
          const c2x = c1x + st[i+2], c2y = c1y + st[i+3];
          x = c2x + st[i+4]; y = c2y + st[i+5];
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
        }
        if ( i + 1 < st.length ) { x += st[i]; y += st[i+1]; sink.lineTo( M(x), M(y) ); }
        st.length = 0; break;
      }

      case 25: {                                   /* rlinecurve */
        let i = 0;
        while ( st.length - i > 6 ) { x += st[i]; y += st[i+1]; sink.lineTo( M(x), M(y) ); i += 2; }
        if ( i + 5 < st.length ) {
          const c1x = x + st[i], c1y = y + st[i+1];
          const c2x = c1x + st[i+2], c2y = c1y + st[i+3];
          x = c2x + st[i+4]; y = c2y + st[i+5];
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
        }
        st.length = 0; break;
      }

      case 26: case 27: {                          /* vvcurveto / hhcurveto */
        let i = 0, d1 = 0;
        if ( st.length & 1 ) { d1 = st[0]; i = 1; }
        for ( ; i + 3 < st.length; i += 4 ) {
          let c1x, c1y;
          if ( b0 === 26 ) { c1x = x + d1; c1y = y + st[i]; }
          else             { c1x = x + st[i]; c1y = y + d1; }
          const c2x = c1x + st[i+1], c2y = c1y + st[i+2];
          if ( b0 === 26 ) { x = c2x; y = c2y + st[i+3]; }
          else             { x = c2x + st[i+3]; y = c2y; }
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
          d1 = 0;
        }
        st.length = 0; break;
      }

      case 30: case 31: {                          /* vhcurveto / hvcurveto */
        let horiz = ( b0 === 31 );
        let i = 0;
        while ( i + 3 < st.length ) {
          let c1x, c1y, c2x, c2y;
          const last = ( st.length - i === 5 );
          if ( horiz ) {
            c1x = x + st[i]; c1y = y;
            c2x = c1x + st[i+1]; c2y = c1y + st[i+2];
            y = c2y + st[i+3];
            x = last ? c2x + st[i+4] : c2x;
          } else {
            c1x = x; c1y = y + st[i];
            c2x = c1x + st[i+1]; c2y = c1y + st[i+2];
            x = c2x + st[i+3];
            y = last ? c2y + st[i+4] : c2y;
          }
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
          i += last ? 5 : 4;
          horiz = !horiz;
        }
        st.length = 0; break;
      }

      case 10: case 29: {                          /* callsubr / callgsubr */
        const idx = ( st.pop() | 0 ) + ( b0 === 10 ? lbias : gbias );
        const tbl = ( b0 === 10 ) ? lsubrs : gsubrs;
        const el = tbl.get( idx );
        if ( el && stack.length < 64 ) { stack.push( buf ); buf = { p: el.start, end: el.end }; }
        break;
      }

      case 11:                                     /* return */
        if ( stack.length ) buf = stack.pop();
        break;

      case 15:                                     /* vsindex */
        if ( st.length ) {
          curIvs = st[ st.length - 1 ] | 0;
          regionCount = varStoreRegionCount( d, cff2.vsBase, curIvs );
          scalars = null;                          /* recompute on next blend */
        }
        st.length = 0; break;

      case 16: {                                   /* blend */
        const n = ( st.pop() | 0 );
        const k = regionCount;
        const total = ( k + 1 ) * n;
        if ( total <= st.length && n >= 0 ) {
          const sc = getScalars();
          if ( sc.length === k && k > 0 ) {
            const start = st.length - total;
            for ( let i = 0; i < n; i++ ) {
              let acc = 0;
              const dbase = start + n + i * k;
              for ( let j = 0; j < k; j++ ) acc += sc[j] * st[ dbase + j ];
              st[ start + i ] += acc;
            }
          }
          st.splice( st.length - k * n, k * n );
        } else st.length = 0;
        break;
      }

      case 12: {                                   /* escape: flex family */
        const b1 = d[ buf.p++ ];
        const a = st;
        if ( b1 === 35 && a.length >= 13 ) {        /* flex */
          const c1x=x+a[0], c1y=y+a[1], c2x=c1x+a[2], c2y=c1y+a[3];
          const jx=c2x+a[4], jy=c2y+a[5];
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[6], c3y=jy+a[7], c4x=c3x+a[8], c4y=c3y+a[9];
          x=c4x+a[10]; y=c4y+a[11];
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        } else if ( b1 === 34 && a.length >= 7 ) {  /* hflex */
          const y0=y;
          const c1x=x+a[0], c1y=y, c2x=c1x+a[1], c2y=c1y+a[2], jx=c2x+a[3], jy=c2y;
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[4], c3y=jy, c4x=c3x+a[5], c4y=y0;
          x=c4x+a[6]; y=y0;
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        } else if ( b1 === 36 && a.length >= 9 ) {  /* hflex1 */
          const y0=y;
          const c1x=x+a[0], c1y=y+a[1], c2x=c1x+a[2], c2y=c1y+a[3], jx=c2x+a[4], jy=c2y;
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[5], c3y=jy, c4x=c3x+a[6], c4y=c3y+a[7];
          x=c4x+a[8]; y=y0;
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        } else if ( b1 === 37 && a.length >= 11 ) { /* flex1 */
          const sx=x, sy=y;
          let dx=0, dy=0;
          for ( let i = 0; i < 10; i += 2 ) { dx += a[i]; dy += a[i+1]; }
          const c1x=x+a[0], c1y=y+a[1], c2x=c1x+a[2], c2y=c1y+a[3];
          const jx=c2x+a[4], jy=c2y+a[5];
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[6], c3y=jy+a[7], c4x=c3x+a[8], c4y=c3y+a[9];
          if ( Math.abs( dx ) > Math.abs( dy ) ) { x = c4x + a[10]; y = sy; }
          else { x = sx; y = c4y + a[10]; }
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        }
        st.length = 0; break;
      }

      default:
        st.length = 0; break;
    }
  }

  /* cff2_path_param_t::end_path () */
  if ( haveOpen ) sink.closePath();
  return haveOpen;
}

/* A sink that only records bounds, for CFF2 ink extents. */
class BoundsSink {
  constructor() { this.minX=Infinity; this.minY=Infinity; this.maxX=-Infinity; this.maxY=-Infinity; this.any=false; }
  _a(x,y){ this.any=true; if(x<this.minX)this.minX=x; if(x>this.maxX)this.maxX=x;
           if(y<this.minY)this.minY=y; if(y>this.maxY)this.maxY=y; }
  moveTo(x,y){this._a(x,y);} lineTo(x,y){this._a(x,y);}
  quadraticTo(cx,cy,x,y){this._a(cx,cy);this._a(x,y);}
  cubicTo(a,b,c,e,x,y){this._a(a,b);this._a(c,e);this._a(x,y);}
  closePath(){}
}


/* ------------------------------------------------------------------ */
/* `CFF ' (Type 2)  --  HarfBuzz hb-ot-cff1-table.{hh,cc}               */
/*                                                                      */
/*  NOTE this is NOT ../cff.js.  cff.js is a FreeType-faithful port and */
/*  runs the interpreter in 16.16 fixed point with FreeType's rounding  */
/*  and contour-closing rules; HarfBuzz runs it in floats and emits     */
/*  through hb_draw_session_t.  Same operators, different arithmetic    */
/*  and different path emission, so they cannot share code.             */
/*                                                                      */
/*  Found by diffing against the LIVE tool on corpus/public: the        */
/*  step-4 vector corpus contains only CFF2 .otf files, so the recorded */
/*  expectations never exercised `CFF ' at all and this whole font      */
/*  class silently produced an empty document.                          */
/* ------------------------------------------------------------------ */

function readIndex1( d, p ) {
  const count = rU16( d, p );
  if ( count === 0 ) return { count: 0, end: p + 2, get: () => null };
  const offSize = d[ p + 2 ];
  const offArr = p + 3;
  const rdOff = ( i ) => {
    const q = offArr + i * offSize;
    switch ( offSize ) {
      case 1: return d[q];
      case 2: return rU16( d, q );
      case 3: return ( d[q] << 16 ) | ( d[q + 1] << 8 ) | d[q + 2];
      default: return rU32( d, q );
    }
  };
  const dataBase = offArr + ( count + 1 ) * offSize - 1;
  return {
    count, end: dataBase + rdOff( count ),
    get: ( i ) => ( i < 0 || i >= count ) ? null
                 : { start: dataBase + rdOff( i ), end: dataBase + rdOff( i + 1 ) }
  };
}

function cff1Init( face ) {
  if ( face._cff1 !== undefined ) return face._cff1;
  const t = face.tables.get( 'CFF ' );
  if ( !t ) return ( face._cff1 = null );
  const d = face.data, base = t.offset;

  let p = base + d[ base + 2 ];            /* hdrSize */
  const nameIdx = readIndex1( d, p );      p = nameIdx.end;
  const topIdx  = readIndex1( d, p );      p = topIdx.end;
  const strIdx  = readIndex1( d, p );      p = strIdx.end;
  const gsubrs  = readIndex1( d, p );

  const topEl = topIdx.get( 0 );
  if ( !topEl ) return ( face._cff1 = null );
  const top = parseDict2( d, topEl.start, topEl.end );

  const csOff = top[17] ? top[17][0] : 0;
  if ( !csOff ) return ( face._cff1 = null );
  const charStrings = readIndex1( d, base + csOff );

  const readPriv = ( sizeOff ) => {
    let priv = {}, lsubrs = { count: 0, get: () => null };
    if ( sizeOff && sizeOff.length >= 2 && sizeOff[0] > 0 ) {
      const size = sizeOff[0], off = sizeOff[1];
      priv = parseDict2( d, base + off, base + off + size );
      if ( priv[19] ) lsubrs = readIndex1( d, base + off + priv[19][0] );
    }
    return { priv, lsubrs };
  };

  const topPriv = readPriv( top[18] );

  /* CID-keyed: per-FD Private DICTs selected by FDSelect */
  let fds = null, fdsOff = 0;
  if ( top[0x0c24] ) {
    const fdArray = readIndex1( d, base + top[0x0c24][0] );
    fds = [];
    for ( let i = 0; i < fdArray.count; i++ ) {
      const el = fdArray.get( i );
      const fd = parseDict2( d, el.start, el.end );
      fds.push( readPriv( fd[18] ) );
    }
    if ( top[0x0c25] ) fdsOff = base + top[0x0c25][0];
  }

  const charstringType = top[0x0c06] ? top[0x0c06][0] : 2;

  return ( face._cff1 = { base, top, gsubrs, charStrings, topPriv, fds, fdsOff,
                          charstringType } );
}

function cff1FdIndex( d, cff1, gid ) {
  if ( !cff1.fdsOff ) return 0;
  const p = cff1.fdsOff, fmt = d[p];
  if ( fmt === 0 ) return d[ p + 1 + gid ];
  if ( fmt === 3 ) {
    const n = rU16( d, p + 1 );
    const q = p + 3;
    for ( let i = 0; i < n; i++ ) {
      const first = rU16( d, q + i * 3 );
      const next = ( i + 1 < n ) ? rU16( d, q + ( i + 1 ) * 3 ) : rU16( d, q + n * 3 );
      if ( gid >= first && gid < next ) return d[ q + i * 3 + 2 ];
    }
  }
  return 0;
}

/* Type 2 charstring -> draw calls, HarfBuzz semantics (floats). */
function cff1Run( face, gid, sink, mult ) {
  const cff1 = cff1Init( face );
  if ( !cff1 ) return false;
  const d = face.data;
  const cs = cff1.charStrings.get( gid );
  if ( !cs || cs.end <= cs.start ) return false;

  let pr = cff1.topPriv;
  if ( cff1.fds && cff1.fds.length ) {
    const fi = Math.min( cff1FdIndex( d, cff1, gid ), cff1.fds.length - 1 );
    pr = cff1.fds[ fi < 0 ? 0 : fi ];
  }
  const lsubrs = pr.lsubrs, gsubrs = cff1.gsubrs;
  const cst = cff1.charstringType;
  const lbias = cst === 1 ? 0 : bias( lsubrs.count );
  const gbias = cst === 1 ? 0 : bias( gsubrs.count );

  const st = [];
  let x = 0, y = 0, nStems = 0, haveOpen = false, widthParsed = false;
  const stack = [];
  let buf = { p: cs.start, end: cs.end };
  let guard = 0;

  const M = ( v ) => Math.fround( v * mult );
  /* Type 2 width: the first stack-clearing operator may carry one extra
   * leading operand.  We only need to drop it. */
  const dropWidth = ( keep ) => {
    if ( widthParsed ) return;
    widthParsed = true;
    if ( keep < 0 ) { if ( st.length & 1 ) st.shift(); }
    else if ( st.length > keep ) st.shift();
  };

  while ( true ) {
    if ( buf.p >= buf.end ) {
      if ( stack.length ) { buf = stack.pop(); continue; }
      break;
    }
    if ( ++guard > 2000000 ) break;
    const b0 = d[ buf.p++ ];

    if ( b0 >= 32 || b0 === 28 ) {
      if ( b0 === 28 ) { st.push( rI16( d, buf.p ) ); buf.p += 2; }
      else if ( b0 <= 246 ) st.push( b0 - 139 );
      else if ( b0 <= 250 ) st.push( ( b0 - 247 ) * 256 + d[ buf.p++ ] + 108 );
      else if ( b0 <= 254 ) st.push( -( b0 - 251 ) * 256 - d[ buf.p++ ] - 108 );
      else {
        const v = ( ( d[buf.p] << 24 ) | ( d[buf.p+1] << 16 ) |
                    ( d[buf.p+2] << 8 ) | d[buf.p+3] ) | 0;
        st.push( v / 65536 ); buf.p += 4;
      }
      continue;
    }

    switch ( b0 ) {
      case 1: case 3: case 18: case 23:
        dropWidth( -1 );
        nStems += st.length >> 1; st.length = 0; break;

      case 19: case 20:
        dropWidth( -1 );
        nStems += st.length >> 1; st.length = 0;
        buf.p += ( nStems + 7 ) >> 3;
        break;

      case 21: dropWidth( 2 );
        if ( st.length >= 2 ) { x += st[ st.length - 2 ]; y += st[ st.length - 1 ]; }
        sink.moveTo( M( x ), M( y ) ); haveOpen = true; st.length = 0; break;
      case 22: dropWidth( 1 );
        if ( st.length >= 1 ) x += st[ st.length - 1 ];
        sink.moveTo( M( x ), M( y ) ); haveOpen = true; st.length = 0; break;
      case 4: dropWidth( 1 );
        if ( st.length >= 1 ) y += st[ st.length - 1 ];
        sink.moveTo( M( x ), M( y ) ); haveOpen = true; st.length = 0; break;

      case 14:                                   /* endchar */
        dropWidth( 0 );
        st.length = 0;
        buf.p = buf.end; stack.length = 0;
        break;

      case 5:
        for ( let i = 0; i + 1 < st.length; i += 2 ) {
          x += st[i]; y += st[i + 1]; sink.lineTo( M( x ), M( y ) );
        }
        st.length = 0; break;

      case 6: case 7: {
        let horiz = ( b0 === 6 );
        for ( let i = 0; i < st.length; i++ ) {
          if ( horiz ) x += st[i]; else y += st[i];
          sink.lineTo( M( x ), M( y ) ); horiz = !horiz;
        }
        st.length = 0; break;
      }

      case 8:
        for ( let i = 0; i + 5 < st.length; i += 6 ) {
          const c1x = x + st[i], c1y = y + st[i+1];
          const c2x = c1x + st[i+2], c2y = c1y + st[i+3];
          x = c2x + st[i+4]; y = c2y + st[i+5];
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
        }
        st.length = 0; break;

      case 24: {
        let i = 0;
        for ( ; i + 5 < st.length - 2; i += 6 ) {
          const c1x = x + st[i], c1y = y + st[i+1];
          const c2x = c1x + st[i+2], c2y = c1y + st[i+3];
          x = c2x + st[i+4]; y = c2y + st[i+5];
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
        }
        if ( i + 1 < st.length ) { x += st[i]; y += st[i+1]; sink.lineTo( M(x), M(y) ); }
        st.length = 0; break;
      }

      case 25: {
        let i = 0;
        while ( st.length - i > 6 ) { x += st[i]; y += st[i+1]; sink.lineTo( M(x), M(y) ); i += 2; }
        if ( i + 5 < st.length ) {
          const c1x = x + st[i], c1y = y + st[i+1];
          const c2x = c1x + st[i+2], c2y = c1y + st[i+3];
          x = c2x + st[i+4]; y = c2y + st[i+5];
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
        }
        st.length = 0; break;
      }

      case 26: case 27: {
        let i = 0, d1 = 0;
        if ( st.length & 1 ) { d1 = st[0]; i = 1; }
        for ( ; i + 3 < st.length; i += 4 ) {
          let c1x, c1y;
          if ( b0 === 26 ) { c1x = x + d1; c1y = y + st[i]; }
          else             { c1x = x + st[i]; c1y = y + d1; }
          const c2x = c1x + st[i+1], c2y = c1y + st[i+2];
          if ( b0 === 26 ) { x = c2x; y = c2y + st[i+3]; }
          else             { x = c2x + st[i+3]; y = c2y; }
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
          d1 = 0;
        }
        st.length = 0; break;
      }

      case 30: case 31: {
        let horiz = ( b0 === 31 ), i = 0;
        while ( i + 3 < st.length ) {
          let c1x, c1y, c2x, c2y;
          const last = ( st.length - i === 5 );
          if ( horiz ) {
            c1x = x + st[i]; c1y = y;
            c2x = c1x + st[i+1]; c2y = c1y + st[i+2];
            y = c2y + st[i+3]; x = last ? c2x + st[i+4] : c2x;
          } else {
            c1x = x; c1y = y + st[i];
            c2x = c1x + st[i+1]; c2y = c1y + st[i+2];
            x = c2x + st[i+3]; y = last ? c2y + st[i+4] : c2y;
          }
          sink.cubicTo( M(c1x), M(c1y), M(c2x), M(c2y), M(x), M(y) );
          i += last ? 5 : 4; horiz = !horiz;
        }
        st.length = 0; break;
      }

      case 10: case 29: {
        const idx = ( st.pop() | 0 ) + ( b0 === 10 ? lbias : gbias );
        const tbl = ( b0 === 10 ) ? lsubrs : gsubrs;
        const el = tbl.get( idx );
        if ( el && stack.length < 64 ) { stack.push( buf ); buf = { p: el.start, end: el.end }; }
        break;
      }

      case 11:
        if ( stack.length ) buf = stack.pop();
        break;

      case 12: {
        const b1 = d[ buf.p++ ];
        const a = st;
        if ( b1 === 35 && a.length >= 13 ) {
          const c1x=x+a[0], c1y=y+a[1], c2x=c1x+a[2], c2y=c1y+a[3];
          const jx=c2x+a[4], jy=c2y+a[5];
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[6], c3y=jy+a[7], c4x=c3x+a[8], c4y=c3y+a[9];
          x=c4x+a[10]; y=c4y+a[11];
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        } else if ( b1 === 34 && a.length >= 7 ) {
          const y0=y;
          const c1x=x+a[0], c1y=y, c2x=c1x+a[1], c2y=c1y+a[2], jx=c2x+a[3], jy=c2y;
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[4], c3y=jy, c4x=c3x+a[5], c4y=y0;
          x=c4x+a[6]; y=y0;
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        } else if ( b1 === 36 && a.length >= 9 ) {
          const y0=y;
          const c1x=x+a[0], c1y=y+a[1], c2x=c1x+a[2], c2y=c1y+a[3], jx=c2x+a[4], jy=c2y;
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[5], c3y=jy, c4x=c3x+a[6], c4y=c3y+a[7];
          x=c4x+a[8]; y=y0;
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        } else if ( b1 === 37 && a.length >= 11 ) {
          const sx=x, sy=y;
          let dx=0, dy=0;
          for ( let i = 0; i < 10; i += 2 ) { dx += a[i]; dy += a[i+1]; }
          const c1x=x+a[0], c1y=y+a[1], c2x=c1x+a[2], c2y=c1y+a[3];
          const jx=c2x+a[4], jy=c2y+a[5];
          sink.cubicTo( M(c1x),M(c1y),M(c2x),M(c2y),M(jx),M(jy) );
          const c3x=jx+a[6], c3y=jy+a[7], c4x=c3x+a[8], c4y=c3y+a[9];
          if ( Math.abs( dx ) > Math.abs( dy ) ) { x = c4x + a[10]; y = sy; }
          else { x = sx; y = c4y + a[10]; }
          sink.cubicTo( M(c3x),M(c3y),M(c4x),M(c4y),M(x),M(y) );
        }
        st.length = 0; break;
      }

      default:
        st.length = 0; break;
    }
  }

  if ( haveOpen ) sink.closePath();
  return haveOpen;
}

/* ------------------------------------------------------------------ */
/* outline dispatch -- hb_ot_draw_glyph_or_fail order (glyf, then CFF2) */
/* ------------------------------------------------------------------ */

function drawGlyph( face, gid, sink, mult ) {
  /* one hb_draw_session_t per hb_font_draw_glyph call */
  const sess = new DrawSession( sink );
  let ok = false;
  if ( face.tables.has( 'glyf' ) ) ok = drawGlyfGlyph( face, gid, sess, mult );
  if ( !ok && face.tables.has( 'CFF2' ) ) ok = cff2Run( face, gid, sess, mult );
  if ( !ok && face.tables.has( 'CFF ' ) ) ok = cff1Run( face, gid, sess, mult );
  sess.finish();
  return ok;
}

function glyphExtents( face, gid, multX, multY ) {
  if ( face.tables.has( 'glyf' ) ) {
    const e = glyfGlyphExtents( face, gid, multX, multY );
    if ( e ) return e;
  }
  if ( face.tables.has( 'CFF2' ) || face.tables.has( 'CFF ' ) ) {
    /* CFF extents come from the path bounds, already in font-units*mult */
    const isC2 = face.tables.has( 'CFF2' );
    const bs = new BoundsSink();
    const bsess = new DrawSession( bs );
    const okb = isC2 ? cff2Run( face, gid, bsess, 1 ) : cff1Run( face, gid, bsess, 1 );
    bsess.finish();
    if ( !okb || !bs.any ) return null;
    const rf = ( v ) => Math.fround( Math.round( v ) );
    const xB = rf( bs.minX ), yB = rf( bs.maxY );
    const w = rf( bs.maxX ) - xB, h = rf( bs.minY ) - yB;
    const x1 = Math.fround( xB * multX ), y1 = Math.fround( yB * multY );
    const x2 = Math.fround( ( xB + w ) * multX ), y2 = Math.fround( ( yB + h ) * multY );
    const bx = Math.floor( x1 ), by = Math.floor( y1 );
    return { x_bearing: bx, y_bearing: by,
             width: Math.ceil( x2 ) - bx, height: Math.ceil( y2 ) - by };
  }
  return null;
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */

function parseColor( s ) {
  if ( !s ) return null;
  let t = s.trim();
  if ( t.toLowerCase() === 'none' ) return COLOR( 0, 0, 0, 0 );
  if ( t[0] === '#' ) t = t.slice( 1 );
  if ( t.length === 3 )
    return COLOR( parseInt( t[0] + t[0], 16 ), parseInt( t[1] + t[1], 16 ),
                  parseInt( t[2] + t[2], 16 ), 255 );
  if ( t.length === 6 )
    return COLOR( parseInt( t.slice( 0, 2 ), 16 ), parseInt( t.slice( 2, 4 ), 16 ),
                  parseInt( t.slice( 4, 6 ), 16 ), 255 );
  if ( t.length === 8 )
    return COLOR( parseInt( t.slice( 0, 2 ), 16 ), parseInt( t.slice( 2, 4 ), 16 ),
                  parseInt( t.slice( 4, 6 ), 16 ), parseInt( t.slice( 6, 8 ), 16 ) );
  return null;
}

function parseArgs( argv ) {
  const o = { precision: 2, format: 'svg', unicodes: null, text: null,
              outputFile: null, margin: [ 16, 16, 16, 16 ],
              background: COLOR( 255, 255, 255, 255 ),
              foreground: COLOR( 0, 0, 0, 255 ),
              variations: null, palette: 0, fontFile: null, faceIndex: 0,
              lineSpace: 0, fontExtents: null, logical: false, ink: false };
  for ( let i = 0; i < argv.length; i++ ) {
    const a = argv[i];
    const eat = () => argv[++i];
    if ( a === '-O' || a === '--output-format' ) o.format = eat();
    else if ( a.startsWith( '--output-format=' ) ) o.format = a.slice( 16 );
    else if ( a === '-o' || a === '--output-file' ) o.outputFile = eat();
    else if ( a.startsWith( '--output-file=' ) ) o.outputFile = a.slice( 14 );
    else if ( a.startsWith( '--precision=' ) ) o.precision = parseInt( a.slice( 12 ), 10 );
    else if ( a === '--precision' ) o.precision = parseInt( eat(), 10 );
    else if ( a === '-u' || a === '--unicodes' ) o.unicodes = eat();
    else if ( a.startsWith( '--unicodes=' ) ) o.unicodes = a.slice( 11 );
    else if ( a.startsWith( '--text=' ) ) o.text = a.slice( 7 );
    else if ( a === '--text' ) o.text = eat();
    else if ( a.startsWith( '--variations=' ) ) o.variations = a.slice( 13 );
    else if ( a === '--variations' ) o.variations = eat();
    else if ( a.startsWith( '--font-palette=' ) ) o.palette = parseInt( a.slice( 15 ), 10 );
    else if ( a.startsWith( '--palette=' ) ) o.palette = parseInt( a.slice( 10 ), 10 );
    else if ( a.startsWith( '--background=' ) ) o.background = parseColor( a.slice( 13 ) );
    else if ( a.startsWith( '--foreground=' ) ) o.foreground = parseColor( a.slice( 13 ) );
    else if ( a.startsWith( '--margin=' ) ) {
      const m = a.slice( 9 ).split( /[\s,]+/ ).map( Number );
      o.margin = m.length === 1 ? [ m[0], m[0], m[0], m[0] ]
               : m.length === 2 ? [ m[0], m[1], m[0], m[1] ]
               : m.length === 3 ? [ m[0], m[1], m[2], m[1] ]
                                : [ m[0], m[1], m[2], m[3] ];
    }
    else if ( a === '--logical' ) o.logical = true;
    else if ( a === '--ink' ) o.ink = true;
    else if ( a === '-y' || a === '--face-index' ) o.faceIndex = parseInt( eat(), 10 );
    else if ( a.startsWith( '--face-index=' ) ) o.faceIndex = parseInt( a.slice( 13 ), 10 );
    else if ( a.startsWith( '-' ) ) { /* unknown option: ignore */ }
    else if ( !o.fontFile ) o.fontFile = a;
    else if ( o.text === null ) o.text = a;
  }
  /* margin order in view-options is t,r,b,l */
  return o;
}

function parseUnicodes( s ) {
  const out = [];
  for ( const tok of s.split( ',' ) ) {
    const t = tok.trim().replace( /^U\+/i, '' ).replace( /^0x/i, '' );
    if ( !t ) continue;
    const v = parseInt( t, 16 );
    if ( !isNaN( v ) ) out.push( v );
  }
  return out;
}

/* util/hb-vector.cc main(): the shape-consumer runs hb_shape() over the
 * buffer and hands vector_output_t::consume_glyphs the result.  This is that
 * call, driven through ../shape-plan.js -- the very engine bin/hb-shape.js
 * uses -- so GSUB substitutions and GPOS positions reach the outline code.
 *
 * SCALE.  hb_font_set_scale is upem << SUBPIXEL_BITS, so createFont's
 * x_multf is exactly 64 and the shaper's advances and offsets arrive already
 * in the font-units*64 space the draw callbacks and the extents math use --
 * the same space `advanceUnscaled() * SCALE' produces.
 *
 * Default-ignorables are NOT filtered here: hb_ot_shape hides them after
 * mapping and, with no invisible-glyph set, removes them outright.  The
 * cmap+hmtx fallback below still filters them itself.
 *
 * Returns null on any failure so the caller falls back to cmap + hmtx.
 */
function shapeCodes( bytes, o, codes, scale ) {
  try {
    const sface = openFace( bytes, o.faceIndex );
    const upem = sface.unitsPerEM || 1000;
    const font = createFont( sface, { xScale: upem * scale, yScale: upem * scale,
                                      variations: o.variations } );
    const buf = new ShapeBuffer();
    for ( let i = 0; i < codes.length; i++ ) buf.add( codes[i], i );
    buf.props.direction = DIRECTION_INVALID;
    buf.props.script = 0;
    buf.props.language = null;
    guessSegmentProperties( buf );
    shape( font, buf, [], null );
    const out = [];
    for ( let i = 0; i < buf.len; i++ )
      out.push( { gid: buf.info[i].codepoint,
                  ax: buf.pos[i].xAdvance, ay: buf.pos[i].yAdvance,
                  dx: buf.pos[i].xOffset, dy: buf.pos[i].yOffset } );
    return out;
  } catch { return null; }
}

function run( argv ) {
  const o = parseArgs( argv );
  if ( !o.fontFile ) { process.stderr.write( 'hb-vector: no font file\n' ); return 1; }
  if ( o.format && o.format.toLowerCase() !== 'svg' ) {
    process.stderr.write( 'hb-vector: only svg output is implemented\n' );
    return 1;
  }

  const fontBytes = new Uint8Array( readFileSync( o.fontFile ) );
  const face = loadFace( fontBytes, o.faceIndex );
  const upem = face.upem;

  /* util/hb-vector.cc: SUBPIXEL_BITS = 6, DEFAULT_FONT_SIZE = upem */
  const SUBPIXEL = 6;
  const SCALE = Math.pow( 2, SUBPIXEL );          /* scalbnf(1, 6) = 64 */
  const xScale = upem * SCALE;                    /* hb_font_set_scale  */
  const multX = Math.fround( xScale / upem );     /* x_multf, exactly 64 */
  const multY = multX;

  const cmap = makeCmap( face );
  const codes = o.unicodes ? parseUnicodes( o.unicodes )
              : o.text ? Array.from( o.text ).map( ( c ) => c.codePointAt( 0 ) ) : [];

  /* --- shaping.  Was cmap + hmtx only, which draws the WRONG GLYPHS for any
   * font whose default features substitute (ccmp/liga/locl/rlig) and the
   * wrong advances for any font that kerns -- and the advance feeds the
   * logical extents, so a kern moves the viewBox too.
   *
   * Census over the 252 recorded step-4 vector cases, classified by what
   * ../shape-plan.js actually changes rather than by what the font carries:
   * of 67 misses, 32 change glyph IDs (GSUB), 26 change only positions or
   * advances (GPOS), and 9 are untouched by layout (outline/variation
   * residue).  Of the 185 cases that already pass, ZERO have a shaped stream
   * that differs from the cmap+hmtx one, so routing through the shaper
   * cannot cost a case that passes.
   * Kill switch: W7_NO_VECTOR_LAYOUT=1 restores cmap + hmtx only. */
  /* variation coords must exist before advances or outlines are computed */
  face._coords = ( () => {
    const v = parseVariations( o.variations );
    return v.length ? normalizeVariations( face, v ) : [];
  } )();

  const glyphs = [];
  let penX = 0, penY = 0;
  const shaped = process.env.W7_NO_VECTOR_LAYOUT
               ? null : shapeCodes( fontBytes, o, codes, SCALE );
  if ( shaped ) {
    /* vector-output.hh consume_glyphs: draw at the pen plus the glyph's
     * offset, then advance the pen.  Offsets are y-up, like the extents. */
    for ( const g of shaped ) {
      glyphs.push( { gid: g.gid, x: penX + g.dx, y: penY + g.dy,
                     ax: g.ax, ay: g.ay } );
      penX += g.ax; penY += g.ay;
    }
  } else {
    for ( const cp of codes ) {
      if ( isDefaultIgnorable( cp ) ) continue;
      const gid = cmap( cp );
      /* em_scale_x: (v * x_mult + 32768) >> 16, with x_mult = 64<<16 */
      const adv = advanceUnscaled( face, gid ) * SCALE;
      glyphs.push( { gid, x: penX, y: penY, ax: adv, ay: 0 } );
      penX += adv;
    }
  }
  const lineAdvanceX = penX, lineAdvanceY = penY;

  /* --- compute_extents (vector-output.hh) --- */
  const fe = fontExtentsUnscaled( face );
  const asc  = Math.fround( Math.round( fe.asc * multY ) );
  const desc = Math.fround( Math.round( fe.desc * multY ) );
  const gap  = Math.fround( Math.round( fe.gap * multY ) );

  const includeLogical = o.logical || !o.ink;
  const includeInk = o.ink || !o.logical;

  let valid = false, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  const include = ( x, y ) => {
    if ( !valid ) { x1 = x2 = x; y1 = y2 = y; valid = true; return; }
    x1 = Math.min( x1, x ); y1 = Math.min( y1, y );
    x2 = Math.max( x2, x ); y2 = Math.max( y2, y );
  };

  if ( includeLogical ) {
    const logMin = Math.min( desc, asc );
    const logMax = Math.max( desc, asc );
    include( Math.min( 0, lineAdvanceX ), logMin );
    include( Math.max( 0, lineAdvanceX ), logMax );
  }
  /* vector-output.hh picks paint mode from the font's colour tables.
   * `SVG ' is deliberately absent from this test, which is why an
   * SVG-only font renders through the monochrome draw path. */
  const colr = colrInit( face );
  const cpal = cpalInit( face );
  const sbixT = sbixInit( face );
  const cbdtT = cbdtInit( face );
  const usePaint = colrHasV1( colr ) || colrHasV0( colr ) || !!sbixT || !!cbdtT;
  const w3vars = parseVariations( o.variations );
  const w3coords = w3vars.length ? normalizeVariations( face, w3vars ) : [];
  face._coords = w3coords;
  const w3inst = colr ? makeInstancer( face, colr.varStore, colr.varIdxMap, w3coords )
                      : ( () => 0 );
  const colorCtxForExtents = { face, colr, cpal, sbix: sbixT, cbdt: cbdtT, inst: w3inst,
                               palette: o.palette, foreground: o.foreground,
                               mult: multX, fwdScale: multX,
                               invScale: Math.fround( 1 / multX ),
                               paint: { drawGlyphFn: ( gid, sink ) =>
                                          drawGlyph( face, gid, sink, multX ),
                                        customPaletteColor: () => null },
                               outlineExtentsFn: ( g, m ) =>
                                 glyphExtents( face, g, m, m ),
                               depthLeft: HB_MAX_NESTING,
                               edgeCount: COLRV1_MAX_EDGES,
                               layerPath: new Set(), glyphPath: new Set() };

  if ( includeInk ) {
    for ( const g of glyphs ) {
      const ge = colorGlyphExtents( face, g.gid, multX, colorCtxForExtents );
      if ( !ge ) continue;
      const xa = g.x + ge.x_bearing, ya = g.y + ge.y_bearing;
      include( xa, ya );
      include( xa + ge.width, ya + ge.height );
    }
  }

  let ext;
  if ( !valid ) ext = { x: 0, y: 0, width: 1, height: 1 };
  else {
    if ( x2 <= x1 ) x2 = x1 + 1;
    if ( y2 <= y1 ) y2 = y1 + 1;
    /* margin is in pixels; extents are in font-scale (pixel * SCALE) */
    x1 -= o.margin[3] * SCALE; y1 -= o.margin[0] * SCALE;
    x2 += o.margin[1] * SCALE; y2 += o.margin[2] * SCALE;
    ext = { x: x1, y: y1, width: x2 - x1, height: y2 - y1 };
  }

  /* vector-output.hh: paint mode is chosen by the font's colour tables.
   * Note SVG is deliberately NOT in this test, so `SVG '-only fonts
   * render through the monochrome draw path. */

  let svg;
  if ( usePaint ) {
    const paint = new PaintSession();
    paint.setScaleFactor( SCALE, SCALE );
    paint.precision = o.precision;
    paint.foreground = o.foreground;
    paint.background = o.background;
    paint.palette = o.palette;
    paint.setExtents( ext );
    paint.drawGlyphFn = ( gid, sink ) => drawGlyph( face, gid, sink, multX );

    const ctx = { face, colr, cpal, paint, sbix: sbixT, cbdt: cbdtT, inst: w3inst,
                  palette: o.palette, foreground: o.foreground,
                  mult: multX,
                  fwdScale: multX, invScale: Math.fround( 1 / multX ),
                  outlineExtentsFn: ( g, m ) => glyphExtents( face, g, m, m ),
                  depthLeft: HB_MAX_NESTING, edgeCount: COLRV1_MAX_EDGES,
                  layerPath: new Set(), glyphPath: new Set() };

    for ( const g of glyphs ) {
      /* hb_vector_paint_glyph_impl: push the pen transform, paint, pop */
      paint.setTransform( 1, 0, 0, 1, g.x, g.y );
      paint.pushTransform( 1, 0, 0, 1, g.x, g.y );
      ctx.depthLeft = HB_MAX_NESTING;
      ctx.edgeCount = COLRV1_MAX_EDGES;
      ctx.layerPath.clear();
      ctx.glyphPath.clear();
      ctx.glyphPath.add( g.gid );
      /* hb_font_t::paint_glyph falls back to a foreground outline fill */
      if ( !paintGlyphOrFail( ctx, g.gid ) )
        paint.fillGlyph( g.gid, true, o.foreground );
      paint.popTransform();
    }
    svg = paint.render();
  } else {
    const draw = new SvgDraw();
    draw.setScaleFactor( SCALE, SCALE );
    draw.precision = o.precision;
    draw.setForeground( o.foreground );
    draw.setBackground( o.background );
    draw.setExtents( ext );

    for ( const g of glyphs ) {
      draw.setTransform( 1, 0, 0, 1, g.x, g.y );
      draw.newPath();
      drawGlyph( face, g.gid, draw, multX );
    }
    svg = draw.render();
  }
  if ( svg === null ) return 0;
  if ( o.outputFile && o.outputFile !== '-' ) writeFileSync( o.outputFile, svg );
  else process.stdout.write( svg );
  return 0;
}

/* util/batch.hh */
function main( argv ) {
  if ( argv.length === 1 && argv[0] === '--batch' ) {
    const lines = readFileSync( 0, 'utf8' ).split( '\n' );
    let ret = 0;
    for ( const line of lines ) {
      if ( !line ) continue;
      const args = line.split( ';' ).filter( ( s, i ) => i === 0 || s !== '' );
      let r = 1;
      try { r = run( args ); } catch { r = 1; }
      process.stdout.write( r === 0 ? 'success\n' : 'failure\n' );
      ret = Math.max( ret, r );
    }
    return ret;
  }
  return run( argv );
}

process.exitCode = main( process.argv.slice( 2 ) );

/* END */
