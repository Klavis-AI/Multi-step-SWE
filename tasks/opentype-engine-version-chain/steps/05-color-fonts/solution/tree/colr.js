/* colr.js -- the colour tables: CPAL, COLR (v0 layer records and the v1 paint
 * graph), sbix, CBDT/CBLC, the ItemVariationStore instancer the variable paint
 * formats index, and hb_ot_get_glyph_extents' colour dispatch
 * (sbix -> CBDT -> COLR -> outlines).
 *
 * SINGLE SOURCE OF TRUTH for COLR.  Factored out of bin/hb-vector.js verbatim
 * so that the drawing tool and the shaper measure colour glyphs through the
 * same parser: bin/hb-vector.js paints through it, shape-plan.js's font funcs
 * take glyph extents from it.  Nothing here was rewritten in the move; the only
 * edit is that colorGlyphExtents' OUTLINE fallback is now supplied by the
 * caller (ctx.outlineExtentsFn), because the two callers reach glyf/CFF through
 * different modules.
 *
 * The variation-store half is here because COLR's Var paint formats need it;
 * bin/hb-vector.js imports it back for HVAR and CFF2 as well.
 *
 * Owner: W3.  See NOTES-W3-vector.md.
 */
import { COLOR, COMPOSITE } from './svg-out.js';

/* bin/hb-vector.js's own loadFace calls it `upem'; sfnt.js's openFace calls it
 * `unitsPerEM'.  Both faces reach this module, so ask for it in one place --
 * reading the wrong name yields NaN extents, not an error. */
const faceUpem = ( f ) => f.upem || f.unitsPerEM || 1000;

const rI8  = ( d, p ) => ( d[p] << 24 ) >> 24;
const rU16 = ( d, p ) => ( d[p] << 8 ) | d[p + 1];
const rI16 = ( d, p ) => ( ( ( d[p] << 8 ) | d[p + 1] ) << 16 ) >> 16;
const rU32 = ( d, p ) => ( ( ( d[p] << 24 ) | ( d[p + 1] << 16 ) |
                             ( d[p + 2] << 8 ) | d[p + 3] ) >>> 0 );

/* ==================================================================== *
 * COLOUR: CPAL + COLR (v0 layers and v1 paint graph)
 *
 * Ports OT/Color/COLR/COLR.hh's paint_glyph traversal onto the
 * PaintSession serializer.  Notes on things that are not obvious from
 * the spec and were taken from the C:
 *
 *  - COLR v0 calls fill_glyph with NO surrounding transform, while v1
 *    wraps everything in push_font_transform(upem->scale).  That is why
 *    v0 path data and v1 clip-path data are both in font units but v1
 *    gradient coordinates are divided by the scale factor.
 *  - PaintGlyph has a fast path: when its child is PaintSolid it emits
 *    push_inverse_font_transform + fill_glyph + pop_transform and never
 *    touches <defs>.  That is the single biggest shape in the corpus.
 *  - When a v1 glyph has no ClipBox, HarfBuzz runs a *boundedness*
 *    pre-pass and DROPS the glyph entirely if the paint graph is
 *    unbounded (a colour or gradient with no enclosing clip).
 *  - get_color maps palette index 0xFFFF to the foreground colour; every
 *    other index goes through the custom-palette override first, then
 *    CPAL.  Out-of-range lookups yield transparent black, not an error.
 * ==================================================================== */

const rU24 = ( d, p ) => ( d[p] << 16 ) | ( d[p + 1] << 8 ) | d[p + 2];
/* HBFixed::to_float: ((int32_t)v + offset) * 1/(1<<bits) */
const f2dot14 = ( d, p ) => rI16( d, p ) / 16384;
const f16dot16 = ( d, p ) => ( rU32( d, p ) | 0 ) / 65536;

const HB_MAX_NESTING = 64;
const COLRV1_MAX_EDGES = 65536;

/* ==================================================================== *
 * VARIATIONS: fvar/avar normalization + ItemVariationStore instancer
 *
 * Ports ItemVarStoreInstancer (hb-ot-layout-common.hh) so COLR's Var paint
 * formats resolve their deltas.  With no --variations the coord array is
 * empty and every lookup returns 0, so non-variable output is untouched.
 *
 * Measured note: the deltas do NOT apply at the default instance.  Every
 * region in the scored COLR fonts evaluates to scalar 0.0 at coord 0, so
 * this code only changes output when --variations is actually given.
 * ==================================================================== */

const NO_VARIATION = 0xFFFFFFFF;

function parseVariations( s ) {
  const out = [];
  if ( !s ) return out;
  /* Kill switch for A/B measurement; unset = the measured behaviour. */
  const keepPadded = !!process.env.W3_KEEP_PADDED_TAGS;
  for ( const tok of s.split( ',' ) ) {
    const t = tok.trim();
    const eq = t.indexOf( '=' );
    const tagPart = eq < 0 ? t : t.slice( 0, eq );
    /* hb_variation_from_string fails on a tag carrying a space before the
     * '=', and the util drops that variation SILENTLY -- no warning.
     * Measured against the pinned reference binary on 224f3a286016.ttf:
     *   --variations="T1  =0.5"  renders byte-identical to no variations
     *   --variations="T1=0.5"    varies
     * so a space-padded tag must contribute nothing.  Accepting it (as a
     * permissive parser naturally does) applies deltas HarfBuzz never
     * applies, and the whole glyph comes out varied when it should not. */
    if ( !keepPadded && /\s/.test( tagPart ) ) continue;
    const m = t.match( /^([A-Za-z0-9 ]{1,4})\s*=?\s*(-?[0-9.]+)$/ );
    if ( !m ) continue;
    out.push( { tag: ( m[1] + '    ' ).slice( 0, 4 ), value: parseFloat( m[2] ) } );
  }
  return out;
}

/* fvar: majorVer,minorVer,axesArrayOffset,reserved,axisCount,axisSize,... */
function fvarInit( face ) {
  const t = face.tables.get( 'fvar' );
  if ( !t ) return null;
  const d = face.data, b = t.offset;
  const axesOff = rU16( d, b + 4 ), axisCount = rU16( d, b + 8 ), axisSize = rU16( d, b + 10 );
  const axes = [];
  for ( let i = 0; i < axisCount; i++ ) {
    const a = b + axesOff + i * axisSize;
    axes.push( { tag: String.fromCharCode( d[a], d[a+1], d[a+2], d[a+3] ),
                 min: ( rU32( d, a + 4 ) | 0 ) / 65536,
                 def: ( rU32( d, a + 8 ) | 0 ) / 65536,
                 max: ( rU32( d, a + 12 ) | 0 ) / 65536 } );
  }
  return { axisCount, axes };
}

/* AxisRecord::normalize_axis_value */
function normalizeAxisValue( ax, v ) {
  const def = ax.def;
  const mn = Math.min( def, ax.min ), mx = Math.max( def, ax.max );
  v = Math.max( mn, Math.min( mx, v ) );
  if ( v === def ) return 0;
  if ( v < def ) return ( v - def ) / ( def - mn );
  return ( v - def ) / ( mx - def );
}

/* avar SegmentMaps: piecewise-linear map over (from,to) F2DOT14 pairs */
function avarMapCoords( face, coords16 ) {
  const t = face.tables.get( 'avar' );
  if ( !t ) return;
  const d = face.data, b = t.offset;
  const axisCount = rU16( d, b + 6 );
  let p = b + 8;
  const n = Math.min( coords16.length, axisCount );
  for ( let i = 0; i < axisCount; i++ ) {
    const cnt = rU16( d, p );
    if ( i < n && cnt >= 2 ) {
      const v = coords16[i] / 65536;
      let out = v;
      let lo = null, hi = null;
      for ( let k = 0; k < cnt; k++ ) {
        const f = rI16( d, p + 2 + k * 4 ) / 16384;
        const to = rI16( d, p + 4 + k * 4 ) / 16384;
        if ( f === v ) { out = to; lo = hi = null; break; }
        if ( f < v ) lo = { f, to };
        else if ( hi === null ) hi = { f, to };
      }
      if ( lo && hi ) out = lo.to + ( hi.to - lo.to ) * ( v - lo.f ) / ( hi.f - lo.f );
      else if ( lo && !hi ) out = lo.to;
      else if ( !lo && hi ) out = hi.to;
      coords16[i] = Math.round( out * 65536 );
    }
    p += 2 + cnt * 4;
  }
}

/* hb_ot_var_normalize_variations, ending in the 16.16 -> 2.14 rounding */
function normalizeVariations( face, variations ) {
  const fv = fvarInit( face );
  if ( !fv ) return [];
  const coords = new Array( fv.axisCount ).fill( 0 );
  /* hb_font_set_variations -- the entry point the utils actually use -- is
   * AXIS-MAJOR: it seeds design_coords from the fvar defaults and then, for
   * each variation, scans EVERY axis, so every axis carrying the tag is set
   * (last setting wins).  A font may legally repeat an axis tag:
   * e8691822f6a7.ttf carries three axes all tagged `0001'.  A first-match-only
   * loop leaves axes 1 and 2 at their defaults and under-varies the glyph.
   * Measured, not assumed: hb-shape reaches its coords through varfont.js's
   * assign-to-every-match normalizeCoords and reproduces the recorded
   * expectation for that font byte for byte; this first-match loop did not.
   * (hb_ot_var_normalize_variations IS first-match, via fvar::find_axis_info's
   * `axes.lfind' -- but no util calls it.  Reading that one instead is how
   * this was nearly "fixed" in the wrong direction, which REGRESSES hb-shape.)
   * Kill switch: W5_NO_DUPAXIS=1 restores the first-match behaviour. */
  if ( process.env.W5_NO_DUPAXIS ) {
    for ( const v of variations ) {
      const idx = fv.axes.findIndex( ( a ) => a.tag === v.tag );
      if ( idx < 0 || idx >= coords.length ) continue;
      coords[idx] = Math.round( normalizeAxisValue( fv.axes[idx], v.value ) * 65536 );
    }
  } else {
    for ( let i = 0; i < fv.axisCount && i < coords.length; i++ )
      for ( const v of variations ) {
        if ( fv.axes[i].tag !== v.tag ) continue;
        coords[i] = Math.round( normalizeAxisValue( fv.axes[i], v.value ) * 65536 );
      }
  }
  avarMapCoords( face, coords );
  for ( let i = 0; i < coords.length; i++ ) coords[i] = ( coords[i] + 2 ) >> 2;
  return coords;
}

/* VarRegionAxis::evaluate; coord and the region bounds are raw 2.14 ints */
function varAxisEvaluate( d, p, coord ) {
  const peak = rI16( d, p + 2 );
  if ( peak === 0 || coord === peak ) return 1;
  if ( coord === 0 ) return 0;
  const start = rI16( d, p ), end = rI16( d, p + 4 );
  if ( start > peak || peak > end ) return 1;
  if ( start < 0 && end > 0 ) return 1;
  if ( coord <= start || end <= coord ) return 0;
  if ( coord < peak ) return ( coord - start ) / ( peak - start );
  return ( end - coord ) / ( end - peak );
}

/* ItemVariationStore.  base is the absolute table offset. */
function ivsInit( face, base ) {
  if ( !base ) return null;
  const d = face.data;
  if ( rU16( d, base ) !== 1 ) return null;
  const regions = base + rU32( d, base + 2 );
  const count = rU16( d, base + 6 );
  const sets = [];
  for ( let i = 0; i < count; i++ ) sets.push( base + rU32( d, base + 8 + i * 4 ) );
  return { base, regions, sets,
           axisCount: rU16( d, regions ), regionCount: rU16( d, regions + 2 ) };
}

function ivsRegionScalar( face, ivs, regionIndex, coords ) {
  if ( regionIndex >= ivs.regionCount ) return 0;
  const d = face.data;
  const axes = ivs.regions + 4 + regionIndex * ivs.axisCount * 6;
  let v = 1;
  for ( let i = 0; i < ivs.axisCount; i++ ) {
    const c = i < coords.length ? coords[i] : 0;
    const f = varAxisEvaluate( d, axes + i * 6, c );
    if ( f === 0 ) return 0;
    v *= f;
  }
  return v;
}

/* VarData::_get_delta: lcount int32, then (scount-lcount) int16, then int8 */
function ivsGetDelta( face, ivs, index, coords ) {
  if ( !ivs || !coords.length ) return 0;
  const outer = index >>> 16, inner = index & 0xFFFF;
  if ( outer >= ivs.sets.length ) return 0;
  const d = face.data, vd = ivs.sets[outer];
  const itemCount = rU16( d, vd );
  if ( inner >= itemCount ) return 0;
  const wordSizeCount = rU16( d, vd + 2 );
  const isLong = !!( wordSizeCount & 0x8000 );
  const wordCount = wordSizeCount & 0x7FFF;
  const count = rU16( d, vd + 4 );
  const regionIdx = vd + 6;
  const rowSize = ( wordCount + count ) * ( isLong ? 2 : 1 );
  const row = regionIdx + count * 2 + inner * rowSize;
  const scount = isLong ? count : wordCount;
  const lcount = isLong ? wordCount : 0;

  let delta = 0, i = 0, p = row;
  for ( ; i < lcount; i++, p += 4 ) {
    const s = ivsRegionScalar( face, ivs, rU16( d, regionIdx + i * 2 ), coords );
    if ( s ) delta += s * ( rU32( d, p ) | 0 );
  }
  for ( ; i < scount; i++, p += 2 ) {
    const s = ivsRegionScalar( face, ivs, rU16( d, regionIdx + i * 2 ), coords );
    if ( s ) delta += s * rI16( d, p );
  }
  for ( ; i < count; i++, p += 1 ) {
    const s = ivsRegionScalar( face, ivs, rU16( d, regionIdx + i * 2 ), coords );
    if ( s ) delta += s * rI8( d, p );
  }
  return delta;
}

/* DeltaSetIndexMap::map -> 16.16 outer.inner */
function dsimMap( face, off, v ) {
  if ( !off ) return v;
  const d = face.data;
  const fmt = d[off], entryFormat = d[off + 1];
  let mapCount, dataOff;
  if ( fmt === 0 ) { mapCount = rU16( d, off + 2 ); dataOff = off + 4; }
  else if ( fmt === 1 ) { mapCount = rU32( d, off + 2 ); dataOff = off + 6; }
  else return v;
  if ( !mapCount ) return v;
  if ( v >= mapCount ) v = mapCount - 1;
  const width = ( ( entryFormat >> 4 ) & 3 ) + 1;
  const innerBits = ( entryFormat & 0xF ) + 1;
  let u = 0;
  for ( let w = 0; w < width; w++ ) u = ( u << 8 ) + d[ dataOff + width * v + w ];
  const outer = u >>> innerBits;
  const inner = u & ( ( 1 << innerBits ) - 1 );
  return ( ( outer << 16 ) | inner ) >>> 0;
}

/* ItemVarStoreInstancer::operator() */
function makeInstancer( face, varStoreOff, varIdxMapOff, coords ) {
  const ivs = ivsInit( face, varStoreOff );
  if ( !ivs || !coords.length ) return () => 0;
  return ( varIdxBase, offset ) => {
    if ( varIdxBase === NO_VARIATION ) return 0;
    let idx = ( varIdxBase + ( offset || 0 ) ) >>> 0;
    if ( varIdxMapOff ) idx = dsimMap( face, varIdxMapOff, idx );
    return ivsGetDelta( face, ivs, idx, coords );
  };
}

/* ---- CPAL ---- */
function cpalInit( face ) {
  const t = face.tables.get( 'CPAL' );
  if ( !t ) return null;
  const d = face.data, b = t.offset;
  const numColors = rU16( d, b + 2 );
  const numPalettes = rU16( d, b + 4 );
  const numColorRecords = rU16( d, b + 6 );
  const recOff = rU32( d, b + 8 );
  const indices = [];
  for ( let i = 0; i < numPalettes; i++ ) indices.push( rU16( d, b + 12 + i * 2 ) );
  return { base: b, numColors, numPalettes, numColorRecords,
           records: b + recOff, indices };
}

/* CPAL::get_palette_colors + hb_array_t::operator[]: out of range is the
 * Null BGRAColor, i.e. transparent black, never an error. */
function cpalGetColor( face, cpal, paletteIndex, colorIndex ) {
  if ( !cpal || paletteIndex >= cpal.numPalettes || colorIndex >= cpal.numColors )
    return COLOR( 0, 0, 0, 0 );
  const idx = cpal.indices[ paletteIndex ] + colorIndex;
  if ( idx >= cpal.numColorRecords ) return COLOR( 0, 0, 0, 0 );
  const p = cpal.records + idx * 4;
  const d = face.data;
  /* BGRAColor: blue, green, red, alpha */
  return COLOR( d[ p + 2 ], d[ p + 1 ], d[ p ], d[ p + 3 ] );
}

/* ---- COLR ---- */
function colrInit( face ) {
  const t = face.tables.get( 'COLR' );
  if ( !t ) return null;
  const d = face.data, b = t.offset;
  const version = rU16( d, b );
  const numBaseGlyphs = rU16( d, b + 2 );
  const bgOff = rU32( d, b + 4 );
  const lyOff = rU32( d, b + 8 );
  const numLayers = rU16( d, b + 12 );
  const c = { base: b, version, numBaseGlyphs,
              baseGlyphs: bgOff ? b + bgOff : 0,
              layers: lyOff ? b + lyOff : 0, numLayers,
              baseGlyphList: 0, layerList: 0, clipList: 0,
              numBaseGlyphPaint: 0, numLayerPaint: 0 };
  if ( version >= 1 ) {
    const bgl = rU32( d, b + 14 ), ll = rU32( d, b + 18 ), cl = rU32( d, b + 22 );
    const vim = rU32( d, b + 26 ), vs = rU32( d, b + 30 );
    /* COLR::sanitize checks every v1 sub-offset against the table.  A
     * font whose varStore offset points outside the table has its whole
     * COLR rejected and renders through the monochrome draw path, so an
     * out-of-range offset here must fail the table, not just that field. */
    const lim = t.length;
    if ( bgl >= lim || ll >= lim || cl >= lim || vim >= lim || vs >= lim )
      return null;
    c.baseGlyphList = bgl ? b + bgl : 0;
    c.layerList = ll ? b + ll : 0;
    c.clipList = cl ? b + cl : 0;
    c.varIdxMap = vim ? b + vim : 0;
    c.varStore = vs ? b + vs : 0;
    if ( c.baseGlyphList ) c.numBaseGlyphPaint = rU32( d, c.baseGlyphList );
    if ( c.layerList ) c.numLayerPaint = rU32( d, c.layerList );
  }
  return c;
}

const colrHasV0 = ( c ) => !!( c && c.numBaseGlyphs );
const colrHasV1 = ( c ) => !!( c && c.version >= 1 && c.numBaseGlyphPaint > 0 );

/* SortedUnsizedArrayOf<BaseGlyphRecord>::bsearch, 6 bytes per record */
function colrBaseGlyphRecord( face, colr, gid ) {
  if ( !colr.baseGlyphs || !colr.numBaseGlyphs ) return null;
  const d = face.data;
  let lo = 0, hi = colr.numBaseGlyphs - 1;
  while ( lo <= hi ) {
    const mid = ( lo + hi ) >> 1;
    const p = colr.baseGlyphs + mid * 6;
    const g = rU16( d, p );
    if ( gid < g ) hi = mid - 1;
    else if ( gid > g ) lo = mid + 1;
    else return { glyphId: g, firstLayerIdx: rU16( d, p + 2 ), numLayers: rU16( d, p + 4 ) };
  }
  return null;
}

/* BaseGlyphList: uint32 count, then (uint16 gid, Offset32 paint) records */
function colrBaseGlyphPaint( face, colr, gid ) {
  if ( !colr.baseGlyphList || !colr.numBaseGlyphPaint ) return 0;
  const d = face.data;
  const arr = colr.baseGlyphList + 4;
  let lo = 0, hi = colr.numBaseGlyphPaint - 1;
  while ( lo <= hi ) {
    const mid = ( lo + hi ) >> 1;
    const p = arr + mid * 6;
    const g = rU16( d, p );
    if ( gid < g ) hi = mid - 1;
    else if ( gid > g ) lo = mid + 1;
    else {
      const po = rU32( d, p + 2 );
      return po ? colr.baseGlyphList + po : 0;
    }
  }
  return 0;
}

/* ClipList: uint8 format, uint32 count, (uint16 start, uint16 end,
 * Offset24 box) records sorted by startGlyphID. */
function colrGetClip( face, colr, gid ) {
  if ( !colr.clipList ) return null;
  const d = face.data;
  const n = rU32( d, colr.clipList + 1 );
  const arr = colr.clipList + 5;
  let lo = 0, hi = n - 1, rec = -1;
  while ( lo <= hi ) {
    const mid = ( lo + hi ) >> 1;
    const p = arr + mid * 7;
    const s = rU16( d, p ), e = rU16( d, p + 2 );
    if ( gid < s ) hi = mid - 1;
    else if ( gid > e ) lo = mid + 1;
    else { rec = p; break; }
  }
  if ( rec < 0 ) return null;
  const bo = rU24( d, rec + 4 );
  if ( !bo ) return null;
  const box = colr.clipList + bo;
  const fmt = d[ box ];
  if ( fmt !== 1 && fmt !== 2 ) return null;
  return { xMin: rI16( d, box + 1 ), yMin: rI16( d, box + 3 ),
           xMax: rI16( d, box + 5 ), yMax: rI16( d, box + 7 ) };
}

/* hb_paint_context_t::get_color */
function colrGetColor( ctx, colorIndex, alpha ) {
  let color, isFg;
  if ( colorIndex === 0xffff ) { color = ctx.foreground; isFg = true; }
  else {
    const custom = ctx.paint.customPaletteColor( colorIndex );
    color = custom !== null ? custom
          : cpalGetColor( ctx.face, ctx.cpal, ctx.palette, colorIndex );
    isFg = false;
  }
  const a = Math.round( color.a * Math.max( 0, Math.min( 1, alpha ) ) );
  return { color: COLOR( color.r, color.g, color.b, a ), isFg };
}

function colrReadColorLine( ctx, off, isVar ) {
  const d = ctx.face.data;
  let extend = d[ off ];
  if ( extend > 2 ) extend = 0;
  const n = rU16( d, off + 1 );
  const recSize = isVar ? 10 : 6;
  const I = ctx.inst;
  const stops = [];
  for ( let i = 0; i < n; i++ ) {
    const p = off + 3 + i * recSize;
    /* Variable<ColorStop> puts varIdxBase after the 6-byte value */
    const vb = isVar ? rU32( d, p + 6 ) : NO_VARIATION;
    const soff = ( rI16( d, p ) + I( vb, 0 ) ) / 16384;
    const alpha = ( rI16( d, p + 4 ) + I( vb, 1 ) ) / 16384;
    const c = colrGetColor( ctx, rU16( d, p + 2 ), alpha );
    stops.push( { offset: soff, color: c.color } );
  }
  return { extend, stops };
}

const SOLID_FORMATS = new Set( [ 2, 3 ] );

function colrRecurse( ctx, off ) {
  if ( ctx.depthLeft <= 0 || ctx.edgeCount <= 0 ) return;
  ctx.depthLeft--; ctx.edgeCount--;
  colrDispatch( ctx, off );
  ctx.depthLeft++;
}

function colrDispatch( ctx, off ) {
  const d = ctx.face.data;
  const P = ctx.paint;
  const I = ctx.inst;
  const fmt = d[ off ];
  /* Var formats carry varIdxBase after their fixed fields; the instancer
   * returns 0 for NO_VARIATION or when no coords are set, so the
   * non-variable path is bit-identical to before. */
  const VB = ( isVar, at ) => ( isVar ? rU32( d, off + at ) : NO_VARIATION );
  const F2 = ( at, vb, k ) => ( rI16( d, off + at ) + I( vb, k ) ) / 16384;
  const FW = ( at, vb, k ) => rI16( d, off + at ) + I( vb, k );

  switch ( fmt ) {
    case 1: {                                   /* PaintColrLayers */
      const numLayers = d[ off + 1 ];
      const first = rU32( d, off + 2 );
      for ( let i = first; i < first + numLayers; i++ ) {
        if ( i >= ctx.colr.numLayerPaint ) return;
        if ( ctx.layerPath.has( i ) ) return;    /* decycler: abort, not skip */
        ctx.layerPath.add( i );
        const po = rU32( d, ctx.colr.layerList + 4 + i * 4 );
        if ( po ) colrRecurse( ctx, ctx.colr.layerList + po );
        ctx.layerPath.delete( i );
      }
      return;
    }
    case 2: case 3: {                           /* PaintSolid */
      const vb = VB( fmt === 3, 5 );
      const c = colrGetColor( ctx, rU16( d, off + 1 ), F2( 3, vb, 0 ) );
      P.color( c.isFg, c.color );
      return;
    }
    case 4: case 5: {                           /* PaintLinearGradient */
      const clo = rU24( d, off + 1 );
      if ( !clo ) return;
      const vb = VB( fmt === 5, 16 );
      const cl = colrReadColorLine( ctx, off + clo, fmt === 5 );
      P.linearGradient( cl, FW( 4, vb, 0 ), FW( 6, vb, 1 ), FW( 8, vb, 2 ),
                        FW( 10, vb, 3 ), FW( 12, vb, 4 ), FW( 14, vb, 5 ) );
      return;
    }
    case 6: case 7: {                           /* PaintRadialGradient */
      const clo = rU24( d, off + 1 );
      if ( !clo ) return;
      const vb = VB( fmt === 7, 16 );
      const cl = colrReadColorLine( ctx, off + clo, fmt === 7 );
      P.radialGradient( cl, FW( 4, vb, 0 ), FW( 6, vb, 1 ),
                        rU16( d, off + 8 ) + I( vb, 2 ),
                        FW( 10, vb, 3 ), FW( 12, vb, 4 ),
                        rU16( d, off + 14 ) + I( vb, 5 ) );
      return;
    }
    case 8: case 9: {                           /* PaintSweepGradient */
      const clo = rU24( d, off + 1 );
      if ( !clo ) return;
      const vb = VB( fmt === 9, 12 );
      const cl = colrReadColorLine( ctx, off + clo, fmt === 9 );
      const PI = Math.fround( Math.PI );
      P.sweepGradient( cl, FW( 4, vb, 0 ), FW( 6, vb, 1 ),
                       ( F2( 8, vb, 2 ) + 1 ) * PI,
                       ( F2( 10, vb, 3 ) + 1 ) * PI );
      return;
    }
    case 10: {                                  /* PaintGlyph */
      const po = rU24( d, off + 1 );
      const gid = rU16( d, off + 4 );
      const child = po ? off + po : 0;
      /* fast path: a solid child becomes a single filled <path> */
      if ( child && ctx.depthLeft > 0 && ctx.edgeCount > 0 &&
           SOLID_FORMATS.has( d[ child ] ) ) {
        ctx.edgeCount--;
        const cf = d[ child ];
        const cvb = cf === 3 ? rU32( d, child + 5 ) : NO_VARIATION;
        const alpha = ( rI16( d, child + 3 ) + I( cvb, 0 ) ) / 16384;
        const c = colrGetColor( ctx, rU16( d, child + 1 ), alpha );
        P.pushTransform( ctx.invScale, 0, 0, ctx.invScale, 0, 0 );
        P.fillGlyph( gid, c.isFg, c.color );
        P.popTransform();
        return;
      }
      P.pushTransform( ctx.invScale, 0, 0, ctx.invScale, 0, 0 );
      P.pushClipGlyph( gid );
      P.pushTransform( ctx.fwdScale, 0, 0, ctx.fwdScale, 0, 0 );
      if ( child ) colrRecurse( ctx, child );
      P.popTransform();
      P.popClip();
      P.popTransform();
      return;
    }
    case 11: {                                  /* PaintColrGlyph */
      const gid = rU16( d, off + 1 );
      if ( ctx.glyphPath.has( gid ) ) return;
      ctx.glyphPath.add( gid );
      P.pushTransform( ctx.invScale, 0, 0, ctx.invScale, 0, 0 );
      const handled = P.colorGlyph
        ? P.colorGlyph( gid, ( g ) => colrPaintGlyphTop( ctx, g ) ) : false;
      P.popTransform();
      if ( !handled ) {
        const p = colrBaseGlyphPaint( ctx.face, ctx.colr, gid );
        const box = colrGetClip( ctx.face, ctx.colr, gid );
        if ( box ) P.pushClipRectangle( box.xMin, box.yMin, box.xMax, box.yMax );
        if ( p ) colrRecurse( ctx, p );
        if ( box ) P.popClip();
      }
      ctx.glyphPath.delete( gid );
      return;
    }
    case 12: case 13: {                         /* PaintTransform */
      const so = rU24( d, off + 1 ), to = rU24( d, off + 4 );
      let xx = 1, yx = 0, xy = 0, yy = 1, dx = 0, dy = 0;
      if ( to ) {
        const t = off + to;
        /* Var<Affine2x3> carries varIdxBase after the 24-byte value */
        const vb = fmt === 13 ? rU32( d, t + 24 ) : NO_VARIATION;
        const F = ( k ) => ( ( rU32( d, t + k * 4 ) | 0 ) + I( vb, k ) ) / 65536;
        xx = F( 0 ); yx = F( 1 ); xy = F( 2 ); yy = F( 3 ); dx = F( 4 ); dy = F( 5 );
      }
      P.pushTransform( xx, yx, xy, yy, dx, dy );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 14: case 15: {                         /* PaintTranslate */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 15, 8 );
      P.pushTransform( 1, 0, 0, 1, FW( 4, vb, 0 ), FW( 6, vb, 1 ) );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 16: case 17: {                         /* PaintScale */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 17, 8 );
      P.pushTransform( F2( 4, vb, 0 ), 0, 0, F2( 6, vb, 1 ), 0, 0 );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 18: case 19: {                         /* PaintScaleAroundCenter */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 19, 12 );
      const sx = F2( 4, vb, 0 ), sy = F2( 6, vb, 1 );
      const cx = FW( 8, vb, 2 ), cy = FW( 10, vb, 3 );
      P.pushTransform( sx, 0, 0, sy, cx ? ( 1 - sx ) * cx : 0, cy ? ( 1 - sy ) * cy : 0 );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 20: case 21: {                         /* PaintScaleUniform */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 21, 6 );
      const sc = F2( 4, vb, 0 );
      P.pushTransform( sc, 0, 0, sc, 0, 0 );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 22: case 23: {                         /* PaintScaleUniformAroundCenter */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 23, 10 );
      const sc = F2( 4, vb, 0 );
      const cx = FW( 6, vb, 1 ), cy = FW( 8, vb, 2 );
      P.pushTransform( sc, 0, 0, sc, cx ? ( 1 - sc ) * cx : 0, cy ? ( 1 - sc ) * cy : 0 );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 24: case 25: {                         /* PaintRotate */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 25, 6 );
      const a = F2( 4, vb, 0 ) * Math.fround( Math.PI );
      const c = Math.cos( a ), sn = Math.sin( a );
      P.pushTransform( c, sn, -sn, c, 0, 0 );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 26: case 27: {                         /* PaintRotateAroundCenter */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 27, 10 );
      const a = F2( 4, vb, 0 ) * Math.fround( Math.PI );
      const cx = FW( 6, vb, 1 ), cy = FW( 8, vb, 2 );
      const c = Math.cos( a ), sn = Math.sin( a );
      P.pushTransform( c, sn, -sn, c,
                       ( 1 - c ) * cx + sn * cy, -sn * cx + ( 1 - c ) * cy );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 28: case 29: {                         /* PaintSkew */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 29, 8 );
      const PI = Math.fround( Math.PI );
      const skewX = -F2( 4, vb, 0 ) * PI, skewY = F2( 6, vb, 1 ) * PI;
      P.pushTransform( 1, skewY ? Math.tan( skewY ) : 0,
                       skewX ? Math.tan( skewX ) : 0, 1, 0, 0 );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 30: case 31: {                         /* PaintSkewAroundCenter */
      const so = rU24( d, off + 1 );
      const vb = VB( fmt === 31, 12 );
      const PI = Math.fround( Math.PI );
      let skewX = -F2( 4, vb, 0 ) * PI, skewY = F2( 6, vb, 1 ) * PI;
      const cx = FW( 8, vb, 2 ), cy = FW( 10, vb, 3 );
      skewX = skewX ? Math.tan( skewX ) : 0;
      skewY = skewY ? Math.tan( skewY ) : 0;
      P.pushTransform( 1, skewY, skewX, 1,
                       cy ? -skewX * cy : 0, cx ? -skewY * cx : 0 );
      if ( so ) colrRecurse( ctx, off + so );
      P.popTransform();
      return;
    }
    case 32: {                                  /* PaintComposite */
      const so = rU24( d, off + 1 );
      let mode = d[ off + 4 ];
      const bo = rU24( d, off + 5 );
      if ( mode > 27 ) mode = COMPOSITE.CLEAR;
      P.pushGroup();
      if ( bo ) colrRecurse( ctx, off + bo );
      P.pushGroup();
      if ( so ) colrRecurse( ctx, off + so );
      P.popGroup( mode );
      P.popGroup( COMPOSITE.SRC_OVER );
      return;
    }
    default:
      return;
  }
}

/* hb_paint_bounded_context_t: does the paint graph cover a finite area? */
class BoundedPaint {
  constructor() { this.clips = 0; this.bounded = true; this.groups = []; }
  _paint() { if ( !this.clips ) this.bounded = false; }
  pushTransform() {} popTransform() {}
  pushClipGlyph() { this.clips++; }
  pushClipRectangle() { this.clips++; }
  pushClipPathEnd() { this.clips++; }
  popClip() { if ( this.clips ) this.clips--; }
  /* the bounded funcs never set fill_glyph, so the default
   * push_clip_glyph + color + pop_clip decomposition applies */
  fillGlyph() { this.clips++; this._paint(); this.clips--; }
  color() { this._paint(); }
  image() { this.clips++; this._paint(); this.clips--; return true; }
  linearGradient() { this._paint(); }
  radialGradient() { this._paint(); }
  sweepGradient() { this._paint(); }
  pushGroup() { this.groups.push( this.bounded ); this.bounded = true; }
  popGroup( mode ) {
    const src = this.bounded;
    this.bounded = this.groups.pop();
    switch ( mode ) {
      case COMPOSITE.CLEAR: this.bounded = true; break;
      case COMPOSITE.SRC: case COMPOSITE.SRC_OUT: this.bounded = src; break;
      case COMPOSITE.DEST: case COMPOSITE.DEST_OUT: break;
      case COMPOSITE.SRC_IN: case COMPOSITE.DEST_IN:
        this.bounded = this.bounded && src; break;
      default: this.bounded = this.bounded || src; break;
    }
  }
  colorGlyph() { return false; }
  customPaletteColor() { return null; }
}

/* COLR::paint_glyph */
function colrPaintGlyphTop( ctx, gid ) {
  const colr = ctx.colr, P = ctx.paint;
  if ( colr.version >= 1 ) {
    const p = colrBaseGlyphPaint( ctx.face, colr, gid );
    if ( p ) {
      let clip = true, isBounded = true;
      const box = colrGetClip( ctx.face, colr, gid );
      if ( !box ) { clip = false; isBounded = false; }
      if ( !isBounded ) {
        const bp = new BoundedPaint();
        const bctx = Object.assign( {}, ctx, { paint: bp,
          depthLeft: HB_MAX_NESTING, edgeCount: COLRV1_MAX_EDGES,
          layerPath: new Set(), glyphPath: new Set( [ gid ] ) } );
        colrDispatch( bctx, p );
        isBounded = bp.bounded;
      }
      P.pushTransform( ctx.fwdScale, 0, 0, ctx.fwdScale, 0, 0 );
      if ( clip ) P.pushClipRectangle( box.xMin, box.yMin, box.xMax, box.yMax );
      if ( isBounded ) colrRecurse( ctx, p );
      if ( clip ) P.popClip();
      P.popTransform();
      return true;
    }
  }
  const rec = colrBaseGlyphRecord( ctx.face, colr, gid );
  if ( rec ) {
    const d = ctx.face.data;
    for ( let i = 0; i < rec.numLayers; i++ ) {
      const idx = rec.firstLayerIdx + i;
      if ( idx >= colr.numLayers ) break;
      const lp = colr.layers + idx * 4;
      const c = colrGetColor( ctx, rU16( d, lp + 2 ), 1 );
      P.fillGlyph( rU16( d, lp ), c.isFg, c.color );
    }
    return true;
  }
  return false;
}

/* ==================================================================== *
 * COLOUR EXTENTS + BITMAP TABLES
 *
 * hb_ot_get_glyph_extents dispatches sbix -> CBDT -> COLR -> glyf ->
 * cff2 -> cff1, so for a colour font the viewBox is NOT the outline
 * bounding box.  Ported here because it drives every colour viewBox.
 *
 * Measured against recorded output rather than inferred: the extents
 * handed to image() are fully scaled (font units * 64) while the width
 * and height arguments are the raw PNG pixel dimensions.  For one sbix
 * case: a 300x300 PNG at ppem 300 in an upem 800 font yields
 * 300*(800/300) = 800 font units, *64 = 51200, and sx() brings it back
 * to the 800 that appears in the transform.
 *
 * sbix and CBDT apply the pixel->font-unit conversion on OPPOSITE sides
 * of scale_glyph_extents; both orders are ported literally.
 * ==================================================================== */

const newExtents = () => ( { xmin: Infinity, ymin: Infinity,
                             xmax: -Infinity, ymax: -Infinity } );
const extIsVoid = ( e ) => e.xmin > e.xmax;
const extIsEmpty = ( e ) => e.xmin >= e.xmax || e.ymin >= e.ymax;

function extAddPoint( e, x, y ) {
  if ( extIsVoid( e ) ) { e.xmin = e.xmax = x; e.ymin = e.ymax = y; return; }
  if ( x < e.xmin ) e.xmin = x;
  if ( x > e.xmax ) e.xmax = x;
  if ( y < e.ymin ) e.ymin = y;
  if ( y > e.ymax ) e.ymax = y;
}
function extUnion( a, o ) {
  if ( extIsEmpty( o ) ) return a;
  if ( extIsEmpty( a ) ) return { ...o };
  return { xmin: Math.min( a.xmin, o.xmin ), ymin: Math.min( a.ymin, o.ymin ),
           xmax: Math.max( a.xmax, o.xmax ), ymax: Math.max( a.ymax, o.ymax ) };
}
function extIntersect( a, o ) {
  if ( extIsEmpty( o ) || extIsEmpty( a ) ) return newExtents();
  return { xmin: Math.max( a.xmin, o.xmin ), ymin: Math.max( a.ymin, o.ymin ),
           xmax: Math.min( a.xmax, o.xmax ), ymax: Math.min( a.ymax, o.ymax ) };
}
/* hb_extents_t::to_glyph_extents (xneg=false, yneg=false) */
function extToGlyphExtents( e ) {
  const x0 = Math.round( e.xmin ), y0 = Math.round( e.ymin );
  const x1 = Math.round( e.xmax ), y1 = Math.round( e.ymax );
  if ( ![ x0, y0, x1, y1 ].every( isFinite ) )
    return { x_bearing: 0, y_bearing: 0, width: 0, height: 0 };
  return { x_bearing: x0, y_bearing: y1, width: x1 - x0, height: y0 - y1 };
}

/* hb_bounds_t */
const B_UNBOUNDED = 0, B_BOUNDED = 1, B_EMPTY = 2;
const boundsOf = ( e ) => ( { status: extIsEmpty( e ) ? B_EMPTY : B_BOUNDED, e } );
function boundsUnion( a, o ) {
  if ( o.status === B_UNBOUNDED ) return { status: B_UNBOUNDED, e: a.e };
  if ( o.status === B_BOUNDED ) {
    if ( a.status === B_EMPTY ) return { status: B_BOUNDED, e: { ...o.e } };
    if ( a.status === B_BOUNDED ) return { status: B_BOUNDED, e: extUnion( a.e, o.e ) };
  }
  return a;
}
function boundsIntersect( a, o ) {
  if ( o.status === B_EMPTY ) return { status: B_EMPTY, e: a.e };
  if ( o.status === B_BOUNDED ) {
    if ( a.status === B_UNBOUNDED ) return { status: B_BOUNDED, e: { ...o.e } };
    if ( a.status === B_BOUNDED ) {
      const n = extIntersect( a.e, o.e );
      return { status: extIsEmpty( n ) ? B_EMPTY : B_BOUNDED, e: n };
    }
  }
  return a;
}

/* hb_transform_t::multiply (cairo order) */
function trMultiply( a, b ) {
  return { xx: a.xx * b.xx + a.xy * b.yx, yx: a.yx * b.xx + a.yy * b.yx,
           xy: a.xx * b.xy + a.xy * b.yy, yy: a.yx * b.xy + a.yy * b.yy,
           x0: a.xx * b.x0 + a.xy * b.y0 + a.x0,
           y0: a.yx * b.x0 + a.yy * b.y0 + a.y0 };
}
function trTransformExtents( t, e ) {
  if ( extIsVoid( e ) ) return e;
  const qx = [ e.xmin, e.xmin, e.xmax, e.xmax ];
  const qy = [ e.ymin, e.ymax, e.ymin, e.ymax ];
  const out = newExtents();
  for ( let i = 0; i < 4; i++ )
    extAddPoint( out, t.xx * qx[i] + t.xy * qy[i] + t.x0,
                      t.yx * qx[i] + t.yy * qy[i] + t.y0 );
  return out;
}

/* the draw sink used by hb_paint_extents_push_clip_glyph: note it adds
 * CONTROL points as well as on-curve points, so this is a control-point
 * hull, not the true curve bounding box. */
class ExtentsSink {
  constructor() { this.e = newExtents(); }
  moveTo( x, y ) { extAddPoint( this.e, x, y ); }
  lineTo( x, y ) { extAddPoint( this.e, x, y ); }
  quadraticTo( cx, cy, x, y ) {
    extAddPoint( this.e, cx, cy ); extAddPoint( this.e, x, y );
  }
  cubicTo( a, b, c, d, x, y ) {
    extAddPoint( this.e, a, b ); extAddPoint( this.e, c, d );
    extAddPoint( this.e, x, y );
  }
  closePath() {}
}

/* hb_paint_extents_context_t, exposing the PaintSession interface */
class ExtentsPaint {
  constructor( drawFn ) {
    this.drawFn = drawFn;
    this.transforms = [ { xx: 1, yx: 0, xy: 0, yy: 1, x0: 0, y0: 0 } ];
    this.clips = [ { status: B_UNBOUNDED, e: newExtents() } ];
    this.groups = [ { status: B_EMPTY, e: newExtents() } ];
  }
  _tt() { return this.transforms[ this.transforms.length - 1 ]; }
  pushTransform( xx, yx, xy, yy, dx, dy ) {
    this.transforms.push( trMultiply( this._tt(), { xx, yx, xy, yy, x0: dx, y0: dy } ) );
  }
  popTransform() { if ( this.transforms.length > 1 ) this.transforms.pop(); }
  _pushClipExtents( e ) {
    const t = trTransformExtents( this._tt(), e );
    this.clips.push( boundsIntersect( boundsOf( t ), this.clips[ this.clips.length - 1 ] ) );
  }
  pushClipGlyph( gid ) {
    const sink = new ExtentsSink();
    if ( this.drawFn ) this.drawFn( gid, sink );
    this._pushClipExtents( sink.e );
  }
  pushClipRectangle( xmin, ymin, xmax, ymax ) {
    this._pushClipExtents( { xmin, ymin, xmax, ymax } );
  }
  pushClipPathEnd() { this._pushClipExtents( newExtents() ); }
  popClip() { if ( this.clips.length > 1 ) this.clips.pop(); }
  _paint() {
    const i = this.groups.length - 1;
    this.groups[i] = boundsUnion( this.groups[i], this.clips[ this.clips.length - 1 ] );
  }
  fillGlyph( gid ) { this.pushClipGlyph( gid ); this._paint(); this.popClip(); }
  color() { this._paint(); }
  image( blob, w, h, fmt, slant, extents ) {
    if ( !extents ) return false;   /* happens with SVG images */
    this._pushClipExtents( { xmin: extents.x_bearing,
                             ymin: extents.y_bearing + extents.height,
                             xmax: extents.x_bearing + extents.width,
                             ymax: extents.y_bearing } );
    this._paint(); this.popClip();
    return true;
  }
  linearGradient() { this._paint(); }
  radialGradient() { this._paint(); }
  sweepGradient() { this._paint(); }
  pushGroup() { this.groups.push( { status: B_EMPTY, e: newExtents() } ); }
  popGroup( mode ) {
    const src = this.groups.pop();
    const i = this.groups.length - 1;
    let bd = this.groups[i];
    switch ( mode ) {
      case COMPOSITE.CLEAR: bd = { status: B_EMPTY, e: bd.e }; break;
      case COMPOSITE.SRC: case COMPOSITE.SRC_OUT: bd = src; break;
      case COMPOSITE.DEST: case COMPOSITE.DEST_OUT: break;
      case COMPOSITE.SRC_IN: case COMPOSITE.DEST_IN:
        bd = boundsIntersect( bd, src ); break;
      default: bd = boundsUnion( bd, src ); break;
    }
    this.groups[i] = bd;
  }
  colorGlyph() { return false; }
  customPaletteColor() { return null; }
  getExtents() { return this.groups[ this.groups.length - 1 ].e; }
}

/* hb_font_t::scale_glyph_extents */
function scaleGlyphExtents( e, mult ) {
  const x1 = Math.fround( e.x_bearing * mult ), y1 = Math.fround( e.y_bearing * mult );
  const x2 = Math.fround( ( e.x_bearing + e.width ) * mult );
  const y2 = Math.fround( ( e.y_bearing + e.height ) * mult );
  const bx = Math.floor( x1 ), by = Math.floor( y1 );
  return { x_bearing: bx, y_bearing: by,
           width: Math.ceil( x2 ) - bx, height: Math.ceil( y2 ) - by };
}

/* COLR::get_extents */
function colrGlyphExtents( ctx, gid, mult ) {
  const colr = ctx.colr;
  const box = colrGetClip( ctx.face, colr, gid );
  if ( box ) {
    return scaleGlyphExtents( { x_bearing: box.xMin, y_bearing: box.yMax,
                                width: box.xMax - box.xMin,
                                height: box.yMin - box.yMax }, mult );
  }
  const ep = new ExtentsPaint( ctx.paint.drawGlyphFn );
  const ectx = Object.assign( {}, ctx, { paint: ep,
    depthLeft: HB_MAX_NESTING, edgeCount: COLRV1_MAX_EDGES,
    layerPath: new Set(), glyphPath: new Set( [ gid ] ) } );
  const ret = colrPaintGlyphTop( ectx, gid );
  if ( !ret ) return null;
  const e = ep.getExtents();
  if ( extIsVoid( e ) ) return { x_bearing: 0, y_bearing: 0, width: 0, height: 0 };
  return extToGlyphExtents( e );
}

/* maxp numGlyphs, read directly so this does not depend on loadFace */
function faceNumGlyphs( face ) {
  if ( face._numGlyphs === undefined ) {
    const t = face.tables.get( 'maxp' );
    face._numGlyphs = t ? rU16( face.data, t.offset + 4 ) : 0;
  }
  return face._numGlyphs;
}

/* ---- sbix ---- */
function sbixInit( face ) {
  const t = face.tables.get( 'sbix' );
  if ( !t ) return null;
  const d = face.data, b = t.offset;
  if ( !rU16( d, b ) ) return null;           /* has_data() = version */
  const numStrikes = rU32( d, b + 4 );
  const strikes = [];
  for ( let i = 0; i < numStrikes; i++ ) strikes.push( b + rU32( d, b + 8 + i * 4 ) );
  return { base: b, len: t.length, strikes };
}

function sbixChooseStrike( face, sb ) {
  if ( !sb.strikes.length ) return -1;
  const d = face.data;
  /* x_ppem/y_ppem are 0 here, so requested is 1<<30: the largest strike */
  const requested = 1 << 30;
  let best = 0, bestPpem = rU16( d, sb.strikes[0] );
  for ( let i = 1; i < sb.strikes.length; i++ ) {
    const ppem = rU16( d, sb.strikes[i] );
    if ( ( requested <= ppem && ppem < bestPpem ) ||
         ( requested > bestPpem && ppem > bestPpem ) ) { best = i; bestPpem = ppem; }
  }
  return best;
}

function sbixGlyphBlob( face, sb, gid ) {
  const si = sbixChooseStrike( face, sb );
  if ( si < 0 ) return null;
  const d = face.data;
  const strike = sb.strikes[ si ];
  const ppem = rU16( d, strike );
  if ( !ppem ) return null;
  const sbixLen = sb.len;
  const strikeOffset = strike - sb.base;
  let g = gid, retry = 8;
  for ( ;; ) {
    if ( g >= faceNumGlyphs( face ) ) return null;
    const o0 = rU32( d, strike + 4 + g * 4 );
    const o1 = rU32( d, strike + 4 + ( g + 1 ) * 4 );
    if ( o1 <= o0 || o1 - o0 <= 8 || o1 > sbixLen - strikeOffset ) return null;
    const gp = strike + o0;
    const len = o1 - o0 - 8;
    const gtype = rU32( d, gp + 4 );
    if ( gtype === 0x64757065 ) {            /* 'dupe' */
      if ( len >= 2 && retry-- ) { g = rU16( d, gp + 8 ); continue; }
      return null;
    }
    if ( gtype !== 0x706e6720 ) return null;  /* 'png ' */
    return { data: d.subarray( gp + 8, gp + 8 + len ),
             xOffset: rI16( d, gp ), yOffset: rI16( d, gp + 2 ), ppem };
  }
}

/* sbix get_png_extents: pixel->font units FIRST, then scale_glyph_extents */
function sbixGetExtents( face, sb, gid, mult, scale ) {
  const blob = sbixGlyphBlob( face, sb, gid );
  if ( !blob ) return null;
  const png = blob.data;
  if ( png.length < 24 ) return null;
  const width = rU32( png, 16 ), height = rU32( png, 20 );
  if ( width >= 65536 || height >= 65536 ) return null;
  let e = { x_bearing: blob.xOffset, y_bearing: height + blob.yOffset,
            width: width, height: -height };
  if ( blob.ppem && scale ) {
    const s = faceUpem( face ) / blob.ppem;
    e = { x_bearing: Math.round( e.x_bearing * s ), y_bearing: Math.round( e.y_bearing * s ),
          width: Math.round( e.width * s ), height: Math.round( e.height * s ) };
  }
  if ( scale ) e = scaleGlyphExtents( e, mult );
  return e;
}

/* ---- CBDT / CBLC ---- */
function cbdtInit( face ) {
  const cblc = face.tables.get( 'CBLC' ), cbdt = face.tables.get( 'CBDT' );
  if ( !cblc || !cbdt ) return null;
  const d = face.data;
  if ( !rU16( d, cbdt.offset ) ) return null;   /* has_data() = version.major */
  const n = rU32( d, cblc.offset + 4 );
  const sizes = [];
  for ( let i = 0; i < n; i++ ) sizes.push( cblc.offset + 8 + i * 48 );
  return { cblc: cblc.offset, cbdt: cbdt.offset, cbdtLen: cbdt.length, sizes };
}

function cbdtChooseStrike( face, cb ) {
  if ( !cb.sizes.length ) return null;
  const d = face.data;
  const requested = 1 << 30;
  const pk = ( s ) => Math.max( d[ s + 44 ], d[ s + 45 ] );
  let best = cb.sizes[0], bestPpem = pk( cb.sizes[0] );
  for ( let i = 1; i < cb.sizes.length; i++ ) {
    const ppem = pk( cb.sizes[i] );
    if ( ( requested <= ppem && ppem < bestPpem ) ||
         ( requested > bestPpem && ppem > bestPpem ) ) { best = cb.sizes[i]; bestPpem = ppem; }
  }
  return { base: best, ppemX: d[ best + 44 ], ppemY: d[ best + 45 ],
           idxArray: cb.cblc + rU32( d, best ), numTables: rU32( d, best + 8 ) };
}

function cbdtFindTable( face, strike, gid ) {
  const d = face.data;
  for ( let i = 0; i < strike.numTables; i++ ) {
    const r = strike.idxArray + i * 8;
    if ( gid >= rU16( d, r ) && gid <= rU16( d, r + 2 ) ) return r;
  }
  return 0;
}

function cbdtImageData( face, strike, rec, gid ) {
  const d = face.data;
  const first = rU16( d, rec ), last = rU16( d, rec + 2 );
  if ( gid < first || gid > last ) return null;
  const sub = strike.idxArray + rU32( d, rec + 4 );
  const idx = gid - first;
  const fmt = rU16( d, sub );
  const imageFormat = rU16( d, sub + 2 );
  const dataOff = rU32( d, sub + 4 );
  let o0, o1;
  if ( fmt === 1 ) { o0 = rU32( d, sub + 8 + idx * 4 ); o1 = rU32( d, sub + 8 + ( idx + 1 ) * 4 ); }
  else if ( fmt === 3 ) { o0 = rU16( d, sub + 8 + idx * 2 ); o1 = rU16( d, sub + 8 + ( idx + 1 ) * 2 ); }
  else return null;
  if ( o1 <= o0 ) return null;
  return { offset: dataOff + o0, length: o1 - o0, format: imageFormat };
}

/* SmallGlyphMetrics::get_extents (BigGlyphMetrics starts with it) */
function cbdtMetricsExtents( d, p ) {
  return { x_bearing: rI8( d, p + 2 ), y_bearing: rI8( d, p + 3 ),
           width: d[ p + 1 ], height: -d[ p ] };
}

/* CBDT get_extents: scale_glyph_extents FIRST, then pixel->font units */
function cbdtGetExtents( face, cb, gid, mult, scale ) {
  const strike = cbdtChooseStrike( face, cb );
  if ( !strike || !strike.ppemX || !strike.ppemY ) return null;
  const rec = cbdtFindTable( face, strike, gid );
  if ( !rec ) return null;
  const img = cbdtImageData( face, strike, rec, gid );
  if ( !img ) return null;
  if ( img.offset > cb.cbdtLen || cb.cbdtLen - img.offset < img.length ) return null;
  const d = face.data, p = cb.cbdt + img.offset;
  let e;
  if ( img.format === 17 ) { if ( img.length < 9 ) return null; e = cbdtMetricsExtents( d, p ); }
  else if ( img.format === 18 ) { if ( img.length < 12 ) return null; e = cbdtMetricsExtents( d, p ); }
  else return null;
  if ( scale ) {
    e = scaleGlyphExtents( e, mult );
    const xs = faceUpem( face ) / strike.ppemX, ys = faceUpem( face ) / strike.ppemY;
    e = { x_bearing: Math.round( e.x_bearing * xs ), y_bearing: Math.round( e.y_bearing * ys ),
          width: Math.round( e.width * xs ), height: Math.round( e.height * ys ) };
  }
  return e;
}

function cbdtReferencePng( face, cb, gid ) {
  const strike = cbdtChooseStrike( face, cb );
  if ( !strike || !strike.ppemX || !strike.ppemY ) return null;
  const rec = cbdtFindTable( face, strike, gid );
  if ( !rec ) return null;
  const img = cbdtImageData( face, strike, rec, gid );
  if ( !img ) return null;
  if ( img.offset > cb.cbdtLen || cb.cbdtLen - img.offset < img.length ) return null;
  const d = face.data, p = cb.cbdt + img.offset;
  let lenOff, hdr;
  if ( img.format === 17 ) { if ( img.length < 9 ) return null; lenOff = 5; hdr = 9; }
  else if ( img.format === 18 ) { if ( img.length < 12 ) return null; lenOff = 8; hdr = 12; }
  else if ( img.format === 19 ) { if ( img.length < 4 ) return null; lenOff = 0; hdr = 4; }
  else return null;
  const dl = rU32( d, p + lenOff );
  return d.subarray( p + hdr, p + hdr + dl );
}

/* hb_ot_paint_glyph_or_fail: COLR -> SVG -> CBDT -> sbix */
function paintGlyphOrFail( ctx, gid ) {
  if ( ctx.colr && colrPaintGlyphTop( ctx, gid ) ) return true;
  /* SVG->paint_glyph calls image() with format 'svg ' and null extents;
   * the SVG writer rejects everything but PNG, so it always falls
   * through to the bitmap tables. */
  if ( ctx.cbdt && cbdtPaintGlyph( ctx, gid ) ) return true;
  if ( ctx.sbix && sbixPaintGlyph( ctx, gid ) ) return true;
  return false;
}

function cbdtPaintGlyph( ctx, gid ) {
  const ext = colorGlyphExtents( ctx.face, gid, ctx.mult, ctx );
  if ( !ext ) return false;
  const pix = cbdtGetExtents( ctx.face, ctx.cbdt, gid, ctx.mult, false );
  if ( !pix ) return false;
  const png = cbdtReferencePng( ctx.face, ctx.cbdt, gid );
  if ( !png || !png.length ) return false;
  return ctx.paint.image( png, pix.width, -pix.height, 'png ', 0, ext );
}

function sbixPaintGlyph( ctx, gid ) {
  const ext = colorGlyphExtents( ctx.face, gid, ctx.mult, ctx );
  if ( !ext ) return false;
  const pix = sbixGetExtents( ctx.face, ctx.sbix, gid, ctx.mult, false );
  if ( !pix ) return false;
  const blob = sbixGlyphBlob( ctx.face, ctx.sbix, gid );
  if ( !blob || !blob.data.length ) return false;
  return ctx.paint.image( blob.data, pix.width, -pix.height, 'png ', 0, ext );
}

/* hb_ot_get_glyph_extents: sbix -> CBDT -> COLR -> glyf -> cff2 -> cff1 */
function colorGlyphExtents( face, gid, mult, ctx ) {
  if ( ctx ) {
    if ( ctx.sbix ) { const e = sbixGetExtents( face, ctx.sbix, gid, mult, true ); if ( e ) return e; }
    if ( ctx.cbdt ) { const e = cbdtGetExtents( face, ctx.cbdt, gid, mult, true ); if ( e ) return e; }
    if ( ctx.colr ) { const e = colrGlyphExtents( ctx, gid, mult ); if ( e ) return e; }
  }
  /* The OUTLINE fallback belongs to the caller: bin/hb-vector.js measures
   * glyf/CFF through its own draw path, shape-plan.js through glyf.js/cff.js.
   * Returning null lets each caller keep the box it already computes. */
  return ctx && ctx.outlineExtentsFn ? ctx.outlineExtentsFn( gid, mult ) : null;
}

export {
  HB_MAX_NESTING, COLRV1_MAX_EDGES,
  parseVariations, normalizeVariations, makeInstancer,
  ivsInit, ivsGetDelta, ivsRegionScalar, dsimMap,
  cpalInit, colrInit, colrHasV0, colrHasV1,
  sbixInit, cbdtInit,
  paintGlyphOrFail, colorGlyphExtents,
};
