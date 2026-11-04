/* ot-layout.js -- OpenType Layout: GSUB, GPOS and GDEF.
 *
 * Implemented from the specification, not from any existing engine.
 * Normative text lives in ../otspec/txt/ :
 *
 *   chapter2.txt  "OpenType Layout Common Table Formats"
 *                 ScriptList / Script / LangSys (l.95-232)
 *                 FeatureList / Feature + FeatureParams (l.252-340)
 *                 LookupList / Lookup + LookupFlag enumeration (l.341-493)
 *                 Coverage formats 1-2 (l.495-588)
 *                 ClassDef formats 1-2 (l.589-687)
 *                 Sequence context formats 1-3 (l.688-979)
 *                 Chained sequence context formats 1-3 (l.980-1349)
 *                 Device / VariationIndex (l.1350-1458)
 *   gsub.txt      lookup types 1-8 (l.246-717)
 *   gpos.txt      lookup types 1-9 (l.285-1083)
 *                 ValueRecord + ValueFormat (l.1088-1218)
 *                 Anchor formats 1-3 (l.1219-1329)
 *                 MarkArray / MarkRecord (l.1330-1373)
 *   gdef.txt      GlyphClassDef, AttachList, LigCaretList,
 *                 MarkAttachClassDef, MarkGlyphSets (l.99-476)
 *
 * Exports (see NOTES-S1.md for the full data-shape contract):
 *
 *   loadLayout( face )                 -> { gsub, gpos, gdef, face }
 *   applyLookups( plan, buffer, font ) -> buffer          (GSUB then GPOS)
 *   substitute( plan, buffer, font )   -> buffer          (GSUB only)
 *   position( plan, buffer, font )     -> buffer          (GPOS only)
 *
 * plus the pieces a shape plan needs to decide *which* lookups run:
 *   selectScript, selectLangSys, langSysFeatureIndexes, findFeature,
 *   featureLookups, collectLookups
 * and the buffer-level helpers the shaper stage boundaries need:
 *   substituteStart, positionStart, positionFinish, zeroMarkWidths.
 *
 * ---------------------------------------------------------------------------
 * JavaScript has no integers.  Every place where the reference relies on a
 * fixed-width quantity carries a comment saying so; the collected list is in
 * NOTES-S1.md.  The recurring ones are:
 *   - all font data is read big-endian out of a Uint8Array with explicit
 *     bounds checks, so a truncated or fuzzed table yields 0 rather than
 *     `undefined' (which would poison arithmetic with NaN);
 *   - int16 values are sign-extended with `( x << 16 ) >> 16';
 *   - glyph ids are 16-bit and wrap modulo 65536 (GSUB SingleSubst format 1);
 *   - the em scale uses a 64-bit intermediate in the reference, so the `>> 16'
 *     there is a floor-division by 65536, NOT JavaScript's 32-bit `>>'.
 */

/* ------------------------------------------------------------------ */
/*  big-endian readers, bounds-checked                                 */
/* ------------------------------------------------------------------ */

/* Every reader returns 0 for an out-of-range offset.  Layout tables are the
 * most attacker-visible part of a font; a short read must degrade to "this
 * subtable does nothing", never to an exception or to NaN. */

function rU8( d, p ) {
  return ( p >= 0 && p < d.length ) ? d[p] : 0;
}

function rU16( d, p ) {
  return ( p >= 0 && p + 1 < d.length ) ? ( ( d[p] << 8 ) | d[p + 1] ) : 0;
}

function rI16( d, p ) {
  /* int16 -> int32 sign extension; JS numbers are doubles, so this must be
   * explicit. */
  return ( rU16( d, p ) << 16 ) >> 16;
}

function rU32( d, p ) {
  if ( p < 0 || p + 3 >= d.length ) return 0;
  /* `>>> 0' keeps the result an unsigned 32-bit value: `|' alone would make
   * an offset >= 0x80000000 negative. */
  return ( ( d[p] << 24 ) | ( d[p + 1] << 16 ) | ( d[p + 2] << 8 ) | d[p + 3] ) >>> 0;
}

function readTag( d, p ) {
  /* A Tag is four bytes, used verbatim as a Latin-1 string ('DFLT', 'latn'). */
  return String.fromCharCode( rU8( d, p ), rU8( d, p + 1 ),
                              rU8( d, p + 2 ), rU8( d, p + 3 ) );
}


/* ------------------------------------------------------------------ */
/*  constants                                                          */
/* ------------------------------------------------------------------ */

/* chapter2.txt l.430-471, "LookupFlag bit enumeration". */
export const LookupFlag = {
  RIGHT_TO_LEFT         : 0x0001,
  IGNORE_BASE_GLYPHS    : 0x0002,
  IGNORE_LIGATURES      : 0x0004,
  IGNORE_MARKS          : 0x0008,
  IGNORE_FLAGS          : 0x000E,
  USE_MARK_FILTERING_SET: 0x0010,
  RESERVED              : 0x00E0,
  MARK_ATTACHMENT_TYPE  : 0xFF00,
};

/* gdef.txt l.237-257, "GlyphClassDef enumeration". */
export const GlyphClass = {
  UNCLASSIFIED: 0,
  BASE_GLYPH  : 1,
  LIGATURE    : 2,
  MARK        : 3,
  COMPONENT   : 4,
};

/* The properties the shaper derives from GDEF for each buffer glyph.  The
 * three class bits are deliberately laid out to coincide with the matching
 * LookupFlag IGNORE_* bits, so "does this lookup flag skip this glyph" is a
 * single AND (chapter2.txt l.477: "lookups must ignore glyphs of the
 * respective type").  The upper byte carries the GDEF mark attachment class,
 * again aligned with LookupFlag.MARK_ATTACHMENT_TYPE. */
export const GlyphProps = {
  BASE_GLYPH          : 0x0002,
  LIGATURE            : 0x0004,
  MARK                : 0x0008,
  CLASS_MASK          : 0x000E,
  /* Bookkeeping bits, not from GDEF: they record what layout did to the
   * glyph.  LIGATED is needed because a ligated default-ignorable must stop
   * being skippable, and MULTIPLIED because only the first glyph of a
   * Multiple substitution may take a mark attachment. */
  SUBSTITUTED         : 0x0010,
  LIGATED             : 0x0020,
  MULTIPLIED          : 0x0040,
  PRESERVE            : 0x0070,
  MARK_ATTACHMENT_MASK: 0xFF00,
};

const ATTACH_NONE    = 0;
const ATTACH_MARK    = 1;
const ATTACH_CURSIVE = 2;

/* gpos.txt l.1167-1217, "ValueFormat flags". */
const VF_X_PLACEMENT        = 0x0001;
const VF_Y_PLACEMENT        = 0x0002;
const VF_X_ADVANCE          = 0x0004;
const VF_Y_ADVANCE          = 0x0008;
const VF_X_PLACEMENT_DEVICE = 0x0010;
const VF_Y_PLACEMENT_DEVICE = 0x0020;
const VF_X_ADVANCE_DEVICE   = 0x0040;
const VF_Y_ADVANCE_DEVICE   = 0x0080;
const VF_DEVICE_MASK        = 0x00F0;

/* Recursion and work limits.  The specification places no bound on nested
 * lookups, so a hostile font can otherwise make a contextual lookup recurse
 * into itself forever; the tools must not hang on a fuzzed font. */
const MAX_NESTING     = 6;
const MAX_CONTEXT_LEN = 64;
const MAX_LIG_ID      = 0xFFFF;


/* ------------------------------------------------------------------ */
/*  Coverage -- chapter2.txt l.495-588                                 */
/* ------------------------------------------------------------------ */

/* Returns the Coverage Index of `gid', or -1 when the glyph is not covered.
 * `off' is an absolute offset into `d'; 0 means "no table". */
function coverageIndex( d, off, gid ) {
  if ( !off ) return -1;

  const fmt = rU16( d, off );

  if ( fmt === 1 ) {
    /* "The glyph indices must be in numerical order for binary searching of
     * the list." (l.511) */
    const n = rU16( d, off + 2 );
    let lo = 0, hi = n - 1;
    while ( lo <= hi ) {
      const mid = ( lo + hi ) >> 1;
      const g = rU16( d, off + 4 + mid * 2 );
      if ( gid < g ) hi = mid - 1;
      else if ( gid > g ) lo = mid + 1;
      else return mid;
    }
    return -1;
  }

  if ( fmt === 2 ) {
    /* "Coverage Index (glyphID) = startCoverageIndex + glyphID -
     * startGlyphID" (l.541); ranges are sorted and non-overlapping. */
    const n = rU16( d, off + 2 );
    let lo = 0, hi = n - 1;
    while ( lo <= hi ) {
      const mid = ( lo + hi ) >> 1;
      const p = off + 4 + mid * 6;
      const s = rU16( d, p ), e = rU16( d, p + 2 );
      if ( gid < s ) hi = mid - 1;
      else if ( gid > e ) lo = mid + 1;
      else return rU16( d, p + 4 ) + gid - s;
    }
    return -1;
  }

  return -1;
}

/* Number of glyphs a Coverage table covers -- needed to bound the parallel
 * arrays some subtables index by Coverage Index. */
function coverageCount( d, off ) {
  if ( !off ) return 0;
  const fmt = rU16( d, off );
  if ( fmt === 1 ) return rU16( d, off + 2 );
  if ( fmt === 2 ) {
    const n = rU16( d, off + 2 );
    let total = 0;
    for ( let i = 0; i < n; i++ ) {
      const p = off + 4 + i * 6;
      total += rU16( d, p + 2 ) - rU16( d, p ) + 1;
    }
    return total;
  }
  return 0;
}


/* ------------------------------------------------------------------ */
/*  ClassDef -- chapter2.txt l.589-687                                 */
/* ------------------------------------------------------------------ */

/* "All glyphs not assigned to a class fall into Class 0." (l.595) */
function classOf( d, off, gid ) {
  if ( !off ) return 0;

  const fmt = rU16( d, off );

  if ( fmt === 1 ) {
    const start = rU16( d, off + 2 );
    const n     = rU16( d, off + 4 );
    const i     = gid - start;
    return ( i >= 0 && i < n ) ? rU16( d, off + 6 + i * 2 ) : 0;
  }

  if ( fmt === 2 ) {
    const n = rU16( d, off + 2 );
    let lo = 0, hi = n - 1;
    while ( lo <= hi ) {
      const mid = ( lo + hi ) >> 1;
      const p = off + 4 + mid * 6;
      const s = rU16( d, p ), e = rU16( d, p + 2 );
      if ( gid < s ) hi = mid - 1;
      else if ( gid > e ) lo = mid + 1;
      else return rU16( d, p + 4 );
    }
  }

  return 0;
}


/* ------------------------------------------------------------------ */
/*  Device / VariationIndex -- chapter2.txt l.1350-1458                */
/* ------------------------------------------------------------------ */

/* The pixel correction a Device table prescribes at `ppem'.  Returns 0 for a
 * VariationIndex table (0x8000): those need an ItemVariationStore and a
 * variation instance, which this engine does not resolve -- see NOTES-S1.md.
 * A Device table is also inert when the client has no ppem, which is the case
 * for an unhinted, unrounded shaping run. */
function devicePixels( d, off, ppem ) {
  if ( !off || !ppem ) return 0;

  const startSize = rU16( d, off );
  const endSize   = rU16( d, off + 2 );
  const fmt       = rU16( d, off + 4 );

  if ( fmt < 1 || fmt > 3 ) return 0;           /* 0x8000 lands here too */
  if ( ppem < startSize || ppem > endSize ) return 0;

  /* "Two bits can represent a number in the range {-2, -1, 0, or 1}, four
   * bits {-8 to 7}, eight bits {-128 to 127}" -- deltaFormat n packs
   * 1 << n bits per value, most significant first (l.1361, l.1399). */
  const bits = 1 << fmt;
  const idx  = ppem - startSize;
  const bit  = idx * bits;

  const word  = rU16( d, off + 6 + ( bit >> 4 ) * 2 );
  const shift = 16 - ( ( bit & 15 ) + bits );
  let   v     = ( word >> shift ) & ( ( 1 << bits ) - 1 );

  /* Sign-extend the packed field explicitly: it is a signed n-bit value. */
  if ( v & ( 1 << ( bits - 1 ) ) ) v -= ( 1 << bits );
  return v;
}

/* Device correction converted from pixels to the caller's units.  The
 * reference computes `pixels * (int64_t) scale / ppem' -- a 64-bit product
 * and a C division that truncates toward zero. */
function deviceDelta( d, off, ppem, scale, resolve, emScale, emScaleF ) {
  if ( !off ) return 0;

  /* A VariationIndex is a Device table with deltaFormat 0x8000 that carries a
   * delta-set index pair instead of packed pixels (chapter2.txt l.1436-1457).
   * Its delta is in DESIGN units, so it is em-scaled like the coordinate it
   * adjusts, not divided by ppem the way a pixel correction is.  The store
   * lives in GDEF and belongs to the shape plan, which supplies the
   * resolver; with no store the resolver returns 0 and this is inert. */
  if ( rU16( d, off + 4 ) === 0x8000 ) {
    if ( !resolve || !emScale ) return 0;
    const outer = rU16( d, off );
    const inner = rU16( d, off + 2 );
    const delta = resolve( outer, inner );
    if ( !delta ) return 0;
    /* The interpolated delta is a REAL number (a sum of scalar*delta terms),
     * and it is consumed as an output-unit adjustment.  Rounding it to a whole
     * design unit here and only then scaling throws away every fractional bit
     * at an INTERMEDIATE step: at hb-vector's scale (upem*64, so multF == 64)
     * `round(-0.28) * 64' is 0 where `roundf(-0.28 * 64)' is -18/64 of a unit.
     * otvaroverview.txt:277 "any rounding be done at the last point before a
     * value is used" and :656 "rounding should be done only when the final
     * result is used, and may retain greater fractional bit-depth than that of
     * the data type of the item to which deltas are applied".
     * So: multiply in float, round ONCE, at the point of use --
     * hb_font_t::em_scalef_x, not em_scale_x.  At hb-shape's scale (multF == 1)
     * the two agree exactly, which is why only the vector tool moved. */
    if ( emScaleF && !W15_NO_VARIDX_FLOAT_SCALE ) return emScaleF( delta );
    return emScale( Math.round( delta ) );
  }

  const px = devicePixels( d, off, ppem );
  if ( !px ) return 0;
  return Math.trunc( px * scale / ppem );
}


/* ------------------------------------------------------------------ */
/*  em scale                                                           */
/* ------------------------------------------------------------------ */

/* Font-unit -> output-unit conversion.  `mult' is ( scale << 16 ) / upem,
 * computed once per font. */
function scaleMult( scale, upem ) {
  if ( !upem ) return 65536;
  /* `(int64_t) scale << 16' then a truncating division. */
  return Math.trunc( scale * 65536 / upem );
}

/* hb_font_t::x_multf / y_multf -- the FLOAT (32-bit) multiplier that
 * em_scalef_* uses, as distinct from the 16.16 integer `mult' em_scale_*
 * uses.  hb-font.hh: `x_multf = (float) x_scale / upem'. */
function scaleMultF( scale, upem ) {
  if ( !upem ) return 1;
  return Math.fround( scale / upem );
}

/* hb_font_t::em_multf(v, mult) == (hb_position_t) roundf (em_fmultf (v, mult))
 * with em_fmultf(v, mult) == v * mult, ALL IN FLOAT.  roundf() is
 * half-away-from-zero, which Math.round() is not for negatives. */
function emMultF( v, multF ) {
  const p = Math.fround( Math.fround( v ) * multF );
  return p >= 0 ? Math.floor( p + 0.5 ) : -Math.floor( -p + 0.5 );
}

/* W15 kill switch: restore the pre-change VariationIndex scaling, which
 * rounded the interpolated delta to whole DESIGN units before scaling it to
 * output units.  Default ACTIVE (i.e. the float path runs). */
const W15_NO_VARIDX_FLOAT_SCALE = !!process.env.W15_NO_VARIDX_FLOAT_SCALE;

/* roundf(): half AWAY FROM ZERO.  Math.round() is half-UP, so it disagrees
 * with C on every negative .5 -- and anchor differences are signed. */
function roundf( v ) {
  // HarfBuzz REDEFINES roundf (hb-algs.hh:345-350) as floor(x + .5) -- half UP,
  // NOT the C library's half-away-from-zero.  The two agree on every positive
  // value and differ by exactly one on every negative exact .5, which signed
  // anchor differences reach (base.y - mark.y = -187.5 -> HB -187, C -188).
  return process.env.W19_NO_ROUNDHALFUP
    ? ( v >= 0 ? Math.floor( v + 0.5 ) : -Math.floor( -v + 0.5 ) )
    : Math.floor( v + 0.5 );
}

/* hb_font_t::em_fscale_* == em_fmult(v, multf) == (float) v * multf.
 * No rounding: the caller rounds, ONCE, and only when it has the value it
 * actually wants (see getAnchor()). */
function emFScale( v, multF ) {
  return Math.fround( Math.fround( v ) * multF );
}

function emMult( v, mult ) {
  /* ( v * (int64_t) mult + 32768 ) >> 16.  The `>> 16' is an arithmetic shift
   * of a 64-bit value, i.e. floor-division by 65536; JavaScript's `>>' is
   * 32-bit and would overflow, so it is written out.  v is int16 and mult
   * fits in 32 bits, so the product is at most 2^47 and exact as a double. */
  return Math.floor( ( v * mult + 32768 ) / 65536 );
}


/* ------------------------------------------------------------------ */
/*  table parsing                                                      */
/* ------------------------------------------------------------------ */

/* GSUB header gsub.txt l.173-244, GPOS header gpos.txt l.212-283 -- the two
 * are identical apart from the lookup type enumerations. */
function parseLayoutTable( d, base, length, kind ) {
  const major = rU16( d, base );
  if ( major !== 1 ) return null;
  const minor = rU16( d, base + 2 );

  const scriptListOff  = rU16( d, base + 4 );
  const featureListOff = rU16( d, base + 6 );
  const lookupListOff  = rU16( d, base + 8 );
  /* Version 1.1 adds a 32-bit offset to the FeatureVariations table. */
  const fvOff = minor >= 1 ? rU32( d, base + 10 ) : 0;

  const t = {
    kind, data: d, base, length, major, minor,
    scriptListBase : scriptListOff  ? base + scriptListOff  : 0,
    featureListBase: featureListOff ? base + featureListOff : 0,
    lookupListBase : lookupListOff  ? base + lookupListOff  : 0,
    featureVariations: fvOff ? base + fvOff : 0,
    scripts : [],
    features: [],
    lookups : [],
  };

  /* ScriptList -- chapter2.txt l.116-148. */
  if ( t.scriptListBase ) {
    const sl = t.scriptListBase;
    const n  = rU16( d, sl );
    for ( let i = 0; i < n; i++ ) {
      const p   = sl + 2 + i * 6;
      const off = rU16( d, p + 4 );
      if ( !off ) continue;
      t.scripts.push( { tag: readTag( d, p ), offset: sl + off } );
    }
  }

  /* FeatureList -- chapter2.txt l.265-339.  featureParamsOffset is kept as an
   * absolute offset; its interpretation is per-feature ('size', 'ssXX',
   * 'cvXX') and is the caller's business. */
  if ( t.featureListBase ) {
    const fl = t.featureListBase;
    const n  = rU16( d, fl );
    for ( let i = 0; i < n; i++ ) {
      const p   = fl + 2 + i * 6;
      const off = rU16( d, p + 4 );
      const fo  = fl + off;
      const par = rU16( d, fo );
      const cnt = rU16( d, fo + 2 );
      const lookups = [];
      for ( let k = 0; k < cnt; k++ ) lookups.push( rU16( d, fo + 4 + k * 2 ) );
      t.features.push( {
        tag: readTag( d, p ), offset: fo,
        featureParams: par ? fo + par : 0,
        lookups,
      } );
    }
  }

  /* LookupList -- chapter2.txt l.347-417.  markFilteringSet is present only
   * when USE_MARK_FILTERING_SET is set, and sits after the subtable array. */
  if ( t.lookupListBase ) {
    const ll = t.lookupListBase;
    const n  = rU16( d, ll );
    for ( let i = 0; i < n; i++ ) {
      const off = rU16( d, ll + 2 + i * 2 );
      if ( !off ) { t.lookups.push( null ); continue; }
      const lo   = ll + off;
      const type = rU16( d, lo );
      const flag = rU16( d, lo + 2 );
      const cnt  = rU16( d, lo + 4 );
      const subtables = [];
      for ( let k = 0; k < cnt; k++ ) subtables.push( lo + rU16( d, lo + 6 + k * 2 ) );
      const mfs = ( flag & LookupFlag.USE_MARK_FILTERING_SET )
        ? rU16( d, lo + 6 + cnt * 2 ) : 0;
      t.lookups.push( { index: i, offset: lo, type, flag,
                        markFilteringSet: mfs, subtables, resolved: null } );
    }
  }

  return t;
}

/* GDEF -- gdef.txt l.101-231.  Three versions: 1.0, 1.2 adds markGlyphSets,
 * 1.3 adds a 32-bit itemVarStore offset. */
function parseGdef( d, base, length ) {
  const major = rU16( d, base );
  if ( major !== 1 ) return null;
  const minor = rU16( d, base + 2 );

  const gc  = rU16( d, base + 4 );
  const al  = rU16( d, base + 6 );
  const lcl = rU16( d, base + 8 );
  const mac = rU16( d, base + 10 );
  const mgs = minor >= 2 ? rU16( d, base + 12 ) : 0;
  const ivs = minor >= 3 ? rU32( d, base + 14 ) : 0;

  return {
    data: d, base, length, major, minor,
    glyphClassDef     : gc  ? base + gc  : 0,
    attachList        : al  ? base + al  : 0,
    ligCaretList      : lcl ? base + lcl : 0,
    markAttachClassDef: mac ? base + mac : 0,
    markGlyphSets     : mgs ? base + mgs : 0,
    itemVarStore      : ivs ? base + ivs : 0,
    hasGlyphClasses   : !!gc,
    hasMarkSets       : !!mgs,
  };
}


/* ------------------------------------------------------------------ */
/*  GDEF queries -- gdef.txt                                           */
/* ------------------------------------------------------------------ */

export function glyphClass( gdef, gid ) {
  if ( !gdef ) return GlyphClass.UNCLASSIFIED;
  return classOf( gdef.data, gdef.glyphClassDef, gid );
}

export function markAttachClass( gdef, gid ) {
  if ( !gdef ) return 0;
  return classOf( gdef.data, gdef.markAttachClassDef, gid );
}

/* MarkGlyphSets -- gdef.txt l.451-475.  Note the coverage offsets are
 * Offset32, not Offset16. */
export function markSetCovers( gdef, setIndex, gid ) {
  if ( !gdef || !gdef.markGlyphSets ) return false;
  const d = gdef.data, off = gdef.markGlyphSets;
  if ( rU16( d, off ) !== 1 ) return false;
  const n = rU16( d, off + 2 );
  if ( setIndex >= n ) return false;
  const cov = rU32( d, off + 4 + setIndex * 4 );
  if ( !cov ) return false;
  return coverageIndex( d, off + cov, gid ) >= 0;
}

/* The glyph properties the shaper derives from GDEF -- gdef.txt l.59-64 and
 * chapter2.txt l.477-483.  Class 4 (component) maps to no property bit, so no
 * IGNORE_* flag ever skips a component glyph. */
export function glyphPropsFor( gdef, gid ) {
  switch ( glyphClass( gdef, gid ) ) {
    case GlyphClass.BASE_GLYPH: return GlyphProps.BASE_GLYPH;
    case GlyphClass.LIGATURE  : return GlyphProps.LIGATURE;
    case GlyphClass.MARK      :
      return GlyphProps.MARK | ( ( markAttachClass( gdef, gid ) & 0xFF ) << 8 );
    default: return 0;
  }
}

/* AttachList -- gdef.txt l.269-310.  Contour point indices for a glyph, in
 * increasing order, or null. */
export function attachPoints( gdef, gid ) {
  if ( !gdef || !gdef.attachList ) return null;
  const d = gdef.data, off = gdef.attachList;
  const cov = rU16( d, off );
  const i = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( i < 0 || i >= rU16( d, off + 2 ) ) return null;
  const ap = rU16( d, off + 4 + i * 2 );
  if ( !ap ) return null;
  const n = rU16( d, off + ap );
  const out = [];
  for ( let k = 0; k < n; k++ ) out.push( rU16( d, off + ap + 2 + k * 2 ) );
  return out;
}

/* LigCaretList -- gdef.txt l.318-439.  Caret coordinates for a ligature, in
 * font units.  CaretValueFormat2 names a contour point, which needs the
 * glyph outline; the caller may pass `getContourCoord( gid, pointIndex )'. */
export function ligatureCarets( gdef, gid, opts ) {
  if ( !gdef || !gdef.ligCaretList ) return null;
  const d = gdef.data, off = gdef.ligCaretList;
  const cov = rU16( d, off );
  const i = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( i < 0 || i >= rU16( d, off + 2 ) ) return null;

  const lg = rU16( d, off + 4 + i * 2 );
  if ( !lg ) return null;
  const lgo = off + lg;
  const n = rU16( d, lgo );
  const ppem  = ( opts && opts.ppem )  || 0;
  const scale = ( opts && opts.scale ) || 0;

  const out = [];
  for ( let k = 0; k < n; k++ ) {
    const cv = rU16( d, lgo + 2 + k * 2 );
    if ( !cv ) { out.push( 0 ); continue; }
    const c = lgo + cv;
    const fmt = rU16( d, c );
    if ( fmt === 1 ) {
      out.push( rI16( d, c + 2 ) );
    } else if ( fmt === 2 ) {
      const pt = rU16( d, c + 2 );
      out.push( opts && opts.getContourCoord
        ? ( opts.getContourCoord( gid, pt ) | 0 ) : 0 );
    } else if ( fmt === 3 ) {
      const dev = rU16( d, c + 4 );
      out.push( rI16( d, c + 2 ) +
                ( dev ? deviceDelta( d, c + dev, ppem, scale ) : 0 ) );
    } else {
      out.push( 0 );
    }
  }
  return out;
}


/* ------------------------------------------------------------------ */
/*  loadLayout                                                         */
/* ------------------------------------------------------------------ */

/* `face' is the object sfnt.js openFace() returns: it must carry `data' (the
 * whole file) and `tables' (a Map of tag -> { offset, length }). */
export function loadLayout( face ) {
  const d = face && face.data;
  const tabs = face && face.tables;
  if ( !d || !tabs ) return { gsub: null, gpos: null, gdef: null, face };

  const pick = ( tag ) => {
    const e = tabs.get( tag );
    if ( !e || e.offset >= d.length ) return null;
    return e;
  };

  let gsub = null, gpos = null, gdef = null;
  const eSub = pick( 'GSUB' ), ePos = pick( 'GPOS' ), eDef = pick( 'GDEF' );

  /* A broken layout table must leave the rest of the font usable. */
  try { if ( eSub ) gsub = parseLayoutTable( d, eSub.offset, eSub.length, 'GSUB' ); }
  catch { gsub = null; }
  try { if ( ePos ) gpos = parseLayoutTable( d, ePos.offset, ePos.length, 'GPOS' ); }
  catch { gpos = null; }
  try { if ( eDef ) gdef = parseGdef( d, eDef.offset, eDef.length ); }
  catch { gdef = null; }

  return { gsub, gpos, gdef, face };
}


/* ------------------------------------------------------------------ */
/*  script / language / feature selection                              */
/* ------------------------------------------------------------------ */

/* chapter2.txt l.95-114.  Returns the index into table.scripts of the first
 * tag in `tags' that the font defines, or -1. */
export function selectScript( table, tags ) {
  if ( !table ) return -1;
  const list = Array.isArray( tags ) ? tags : [ tags ];
  for ( const tag of list ) {
    for ( let i = 0; i < table.scripts.length; i++ )
      if ( table.scripts[i].tag === tag ) return i;
  }
  return -1;
}

/* chapter2.txt l.156-193.  Returns { offset, isDefault } for the language
 * system, falling back to DefaultLangSys ("may be NULL"). */
export function selectLangSys( table, scriptIndex, tags ) {
  if ( !table ) return null;
  const s = table.scripts[ scriptIndex ];
  if ( !s ) return null;
  const d = table.data, so = s.offset;

  const list = tags ? ( Array.isArray( tags ) ? tags : [ tags ] ) : [];
  const n = rU16( d, so + 2 );
  for ( const tag of list ) {
    for ( let i = 0; i < n; i++ ) {
      const p = so + 4 + i * 6;
      if ( readTag( d, p ) !== tag ) continue;
      const off = rU16( d, p + 4 );
      if ( off ) return { offset: so + off, isDefault: false, tag };
    }
  }

  const def = rU16( d, so );
  if ( def ) return { offset: so + def, isDefault: true, tag: 'dflt' };
  return null;
}

/* chapter2.txt l.205-232.  requiredFeatureIndex is 0xFFFF when absent. */
export function langSysFeatureIndexes( table, langSys ) {
  if ( !table || !langSys ) return { required: 0xFFFF, features: [] };
  const d = table.data, off = langSys.offset;
  const required = rU16( d, off + 2 );
  const n = rU16( d, off + 4 );
  const features = [];
  for ( let i = 0; i < n; i++ ) features.push( rU16( d, off + 6 + i * 2 ) );
  return { required, features };
}

/* Find the FeatureList index of `tag' among the features a language system
 * enables.  Feature tables are per script/language, so the same tag can occur
 * several times in the FeatureList (chapter2.txt l.261). */
export function findFeature( table, langSys, tag ) {
  if ( !table ) return -1;
  const { required, features } = langSysFeatureIndexes( table, langSys );
  if ( required !== 0xFFFF ) {
    const f = table.features[ required ];
    if ( f && f.tag === tag ) return required;
  }
  for ( const fi of features ) {
    const f = table.features[ fi ];
    if ( f && f.tag === tag ) return fi;
  }
  return -1;
}

export function featureLookups( table, featureIndex ) {
  const f = table && table.features[ featureIndex ];
  return f ? f.lookups.slice() : [];
}

/* Convenience for a shape plan: the lookup indices a set of feature tags
 * pulls in, deduplicated and sorted into LookupList order -- chapter2.txt
 * l.315 ("the client arranges the indices numerically into their LookupList
 * order") and l.374-376. */
export function collectLookups( table, scriptTags, langTags, featureTags ) {
  if ( !table ) return [];
  const si = selectScript( table, scriptTags && scriptTags.length ? scriptTags
                                                                 : [ 'DFLT', 'dflt' ] );
  if ( si < 0 ) return [];
  const ls = selectLangSys( table, si, langTags );
  if ( !ls ) return [];
  const seen = new Set();
  for ( const tag of featureTags ) {
    const fi = findFeature( table, ls, tag );
    if ( fi < 0 ) continue;
    for ( const li of featureLookups( table, fi ) ) seen.add( li );
  }
  return [ ...seen ].sort( ( a, b ) => a - b );
}


/* ------------------------------------------------------------------ */
/*  subtable resolution (Extension) -- gsub.txt l.613-649,             */
/*                                     gpos.txt l.1046-1082           */
/* ------------------------------------------------------------------ */

/* "Proceed as though the Lookup table's lookupType field were set to the
 * extensionLookupType of the subtables, and as though each extension subtable
 * replaced the type 7/9 subtable that referenced it." */
function resolveSubtables( table, lk ) {
  if ( lk.resolved ) return lk.resolved;

  const d = table.data;
  const extType = table.kind === 'GSUB' ? 7 : 9;
  const out = [];

  for ( const off of lk.subtables ) {
    let type = lk.type, o = off;
    if ( type === extType ) {
      /* ExtensionFormat1: format, extensionLookupType, Offset32. */
      if ( rU16( d, o ) !== 1 ) continue;
      const et = rU16( d, o + 2 );
      /* "The extensionLookupType field must be set to any lookup type other
       * than 7/9" -- refuse to chain extensions. */
      if ( et === extType || et === 0 ) continue;
      const eo = rU32( d, o + 4 );
      type = et;
      o = o + eo;
    }
    out.push( { type, offset: o } );
  }

  lk.resolved = out;
  return out;
}

/* A lookup is a reverse one when its (resolved) type is GSUB 8; the whole
 * pass then runs back to front -- gsub.txt l.653 and l.665. */
function lookupIsReverse( table, lk ) {
  if ( table.kind !== 'GSUB' ) return false;
  const subs = resolveSubtables( table, lk );
  return subs.length > 0 && subs[0].type === 8;
}


/* ------------------------------------------------------------------ */
/*  buffer helpers                                                     */
/* ------------------------------------------------------------------ */

function newPos() {
  return { xAdvance: 0, yAdvance: 0, xOffset: 0, yOffset: 0,
           attachChain: 0, attachType: ATTACH_NONE };
}

/* The buffer this module operates on is the shared shape defined in
 * NOTES-S1.md: `info' is an array of glyph records whose `codepoint' holds
 * the *glyph id* once cmap has run, and `pos' the parallel positions.  The
 * fields layout needs but the caller may not have set are filled in here. */
function ensureBuffer( buffer ) {
  if ( !buffer.info ) buffer.info = [];
  if ( !buffer.pos ) buffer.pos = [];
  const info = buffer.info;
  while ( buffer.pos.length < info.length ) buffer.pos.push( newPos() );
  buffer.pos.length = info.length;
  for ( let i = 0; i < info.length; i++ ) {
    const g = info[i];
    if ( g.mask === undefined ) g.mask = 1;
    if ( g.props === undefined ) g.props = 0;
    if ( g.ligId === undefined ) g.ligId = 0;
    if ( g.ligComp === undefined ) g.ligComp = 0;
    if ( g.numComps === undefined ) g.numComps = 1;
    const p = buffer.pos[i];
    if ( p.xAdvance === undefined ) p.xAdvance = 0;
    if ( p.yAdvance === undefined ) p.yAdvance = 0;
    if ( p.xOffset === undefined ) p.xOffset = 0;
    if ( p.yOffset === undefined ) p.yOffset = 0;
    if ( p.attachChain === undefined ) p.attachChain = 0;
    if ( p.attachType === undefined ) p.attachType = ATTACH_NONE;
  }
  return buffer;
}

/* Start of the substitution stage: give every glyph its GDEF properties and
 * clear the ligature bookkeeping.  `fallback( info )' lets the shaper supply
 * properties for a font with no GlyphClassDef (gdef.txt l.64: "If the font
 * does not include a GlyphClassDef table, the client will need to define and
 * maintain this information"). */
export function substituteStart( buffer, gdef, fallback ) {
  ensureBuffer( buffer );
  const info = buffer.info;
  for ( let i = 0; i < info.length; i++ ) {
    const g = info[i];
    let props = glyphPropsFor( gdef, g.codepoint );
    if ( !props && fallback ) props = fallback( g ) | 0;
    g.props   = props;
    g.ligId   = 0;
    g.ligComp = 0;
    g.numComps = 1;
  }
  return buffer;
}

/* hb_ot_zero_width_default_ignorables.  The flag values are buffer.js's
 * SCRATCH_HAS_DEFAULT_IGNORABLES (0x02) and BUFFER_FLAG_PRESERVE /
 * REMOVE_DEFAULT_IGNORABLES (0x04 / 0x08), spelled out because this module
 * imports nothing.  `ignorable' is the Unicode property; a glyph a GSUB
 * lookup has substituted has become content and keeps its advance. */
function zeroWidthDefaultIgnorables( buffer ) {
  const scratch = buffer.scratchFlags | 0, flags = buffer.flags | 0;
  if ( !( scratch & 0x02 ) || ( flags & 0x04 ) || ( flags & 0x08 ) ) return;
  const info = buffer.info, pos = buffer.pos;
  const dir = buffer.props ? buffer.props.direction : undefined;
  const horizontal = dir === undefined ? true : ( dir & ~1 ) === 4;
  const n = Math.min( info.length, pos.length );
  for ( let i = 0; i < n; i++ ) {
    if ( !info[i].ignorable || ( info[i].props & GlyphProps.SUBSTITUTED ) ) continue;
    const p = pos[i];
    p.xAdvance = 0; p.yAdvance = 0;
    if ( horizontal ) p.xOffset = 0; else p.yOffset = 0;
  }
}

/* Start of the positioning stage: GPOS builds attachment chains from
 * scratch, and a chain left over from a previous run would be re-applied. */
export function positionStart( buffer ) {
  ensureBuffer( buffer );
  for ( const p of buffer.pos ) { p.attachChain = 0; p.attachType = ATTACH_NONE; }
  return buffer;
}

/* Mark glyphs are non-spacing (gdef.txt l.253: "Mark glyph (non-spacing
 * combining glyph)"), but GPOS itself never touches their advance -- the
 * attachment subtables are explicit that "advances of both glyphs are not
 * affected" (gpos.txt l.661, l.765, l.878).  Dropping the advance is
 * therefore a separate, GDEF-driven step of the shaper, and it must run
 * BEFORE the attachment offsets are resolved: a mark that hangs off another
 * mark cancels the advances in between, and those are already zero by then.
 * (Measured: base+mark2+mark1 yields an x offset of -100, not -1600.)
 *
 * `adjustOffsets' additionally slides the mark back over the glyph it
 * follows; that is what a font with no GPOS needs, since nothing else will
 * have placed the mark. */
export function zeroMarkWidths( buffer, adjustOffsets ) {
  ensureBuffer( buffer );
  const info = buffer.info, pos = buffer.pos;
  for ( let i = 0; i < info.length; i++ ) {
    if ( !( info[i].props & GlyphProps.MARK ) ) continue;
    if ( adjustOffsets ) {
      pos[i].xOffset -= pos[i].xAdvance;
      pos[i].yOffset -= pos[i].yAdvance;
    }
    pos[i].xAdvance = 0;
    pos[i].yAdvance = 0;
  }
  return buffer;
}

/* Resolve the attachment chains GPOS built.
 *
 * A mark records its anchor difference relative to the *origin* of the glyph
 * it attaches to, but the serializer walks a running pen; the offset must
 * therefore also cancel the advances of everything between the two
 * (gpos.txt l.665: "positioning the mark with respect to the final pen point
 * (advance) position of the base glyph").  A cursive chain instead only
 * carries the cross-stream component forward (gpos.txt l.599: "the last glyph
 * in the connected sequence keeps its initial position in the cross-stream
 * direction ... and the cross-stream positions of the preceding, connected
 * glyphs are adjusted"). */
const INT32_MIN = -2147483648, INT32_MAX = 2147483647;
const satAdd = ( a, b ) => Math.max( INT32_MIN, Math.min( INT32_MAX, a + b ) );
const satSub = ( a, b ) => Math.max( INT32_MIN, Math.min( INT32_MAX, a - b ) );

export function positionFinish( buffer, direction ) {
  ensureBuffer( buffer );
  const pos = buffer.pos;
  const len = pos.length;
  const horizontal = direction !== 'ttb' && direction !== 'btt';
  const forward    = direction !== 'rtl' && direction !== 'btt';

  /* A MARK inherits only the offset of its parent that runs ALONG the text
   * (x for horizontal, y for vertical).  The cross-stream component belongs
   * to the cursive chain alone: `propagate_attachment_offsets' adds
   * pos[j].y_offset for a CURSIVE link but pos[j].x_offset for a MARK link
   * when the direction is horizontal.  Adding both put every mark whose base
   * sat on a cursive chain out by exactly that base's y -- e.g. uni064F on a
   * uniFEE4 lifted to y=295 came out at 473 where HarfBuzz gives 178, and the
   * SAME mark/base pair produced two different mark offsets in one line
   * according to where the cursive chain had lifted the base.  The anchor
   * difference is already measured from the base's own outline, so the lift
   * must not be counted twice.
   *
   * The `j > i' arm cannot arise from OpenType (a mark always follows its
   * base) but is what the reference does for `kerx', and costs nothing. */
  const resolve = ( i, depth ) => {
    const p = pos[i];
    const chain = p.attachChain, type = p.attachType;
    p.attachChain = 0;
    const j = i + chain;
    if ( j < 0 || j >= len ) return;
    if ( depth > MAX_NESTING * 8 ) return;

    if ( pos[j].attachChain ) resolve( j, depth + 1 );

    if ( type & ATTACH_CURSIVE ) {
      if ( horizontal ) p.yOffset = satAdd( p.yOffset, pos[j].yOffset );
      else              p.xOffset = satAdd( p.xOffset, pos[j].xOffset );
      return;
    }

    if ( horizontal ) p.xOffset = satAdd( p.xOffset, pos[j].xOffset );
    else              p.yOffset = satAdd( p.yOffset, pos[j].yOffset );

    if ( j < i ) {
      if ( forward )
        for ( let k = j; k < i; k++ ) {
          p.xOffset = satSub( p.xOffset, pos[k].xAdvance );
          p.yOffset = satSub( p.yOffset, pos[k].yAdvance );
        }
      else
        for ( let k = j + 1; k < i + 1; k++ ) {
          p.xOffset = satAdd( p.xOffset, pos[k].xAdvance );
          p.yOffset = satAdd( p.yOffset, pos[k].yAdvance );
        }
    } else {
      if ( forward )
        for ( let k = i; k < j; k++ ) {
          p.xOffset = satAdd( p.xOffset, pos[k].xAdvance );
          p.yOffset = satAdd( p.yOffset, pos[k].yAdvance );
        }
      else
        for ( let k = i + 1; k < j + 1; k++ ) {
          p.xOffset = satSub( p.xOffset, pos[k].xAdvance );
          p.yOffset = satSub( p.yOffset, pos[k].yAdvance );
        }
    }
  };

  /* harfbuzz#5514: the walk runs with the text, so a chain is resolved in the
   * order its links were built. */
  if ( forward ) {
    for ( let i = 0; i < len; i++ ) if ( pos[i].attachChain ) resolve( i, 0 );
  } else {
    for ( let i = len; i-- > 0; ) if ( pos[i].attachChain ) resolve( i, 0 );
  }
  return buffer;
}


/* ------------------------------------------------------------------ */
/*  the apply context                                                  */
/* ------------------------------------------------------------------ */

function normalizeFont( font, face ) {
  const upem = ( font && font.upem ) ||
               ( face && face.unitsPerEM ) || 1000;
  const xScale = ( font && font.xScale !== undefined ) ? font.xScale : upem;
  const yScale = ( font && font.yScale !== undefined ) ? font.yScale : upem;
  return {
    raw   : font || null,
    face,
    upem,
    xScale, yScale,
    xPpem : ( font && font.xPpem ) | 0,
    yPpem : ( font && font.yPpem ) | 0,
    xMult : scaleMult( xScale, upem ),
    yMult : scaleMult( yScale, upem ),
    xMultF: scaleMultF( xScale, upem ),
    yMultF: scaleMultF( yScale, upem ),
    getGlyphContourPoint: font && font.getGlyphContourPoint || null,
  };
}

class ApplyContext {
  constructor( table, buffer, font, opts ) {
    this.table  = table;
    this.d      = table.data;
    this.buffer = buffer;
    this.font   = font;
    this.gdef   = opts.gdef || null;
    this.layout = opts.layout || null;
    /* Supplied by the shape plan; resolves a VariationIndex delta-set pair. */
    this.resolveVarDelta = opts.resolveVarDelta || null;

    this.direction  = opts.direction || 'ltr';
    this.horizontal = this.direction !== 'ttb' && this.direction !== 'btt';
    this.forward    = this.direction !== 'rtl' && this.direction !== 'btt';
    this.isGpos     = table.kind === 'GPOS';

    this.lookupMask  = 1;
    this.lookupFlag  = 0;
    this.markSet     = 0;
    this.lookupIndex = -1;
    this.autoZwnj    = true;
    this.autoZwj     = true;

    this.idx     = 0;
    this.nesting = 0;
    /* A per-run operation budget; a hostile font must not be able to make a
     * contextual lookup do unbounded work. */
    /* chapter2.txt l.374 requires the client to process a lookup "over each
     * glyph in the sequence to which the feature has been applied", and only
     * "after that lookup has been processed over the entire glyph sequence"
     * to move on to the next one.  This counter is a runaway guard, not a
     * spec behaviour -- but at 1024 it trips during an ORDINARY run on any
     * font with many subtables, and once it trips applySubtables() returns
     * false for everything and applyLookup() breaks out, so every remaining
     * lookup silently does nothing at all.  That is the opposite of l.374.
     *
     * b895f8ff0649.ttf (Hebrew) has 48 GPOS lookups, one of them with 39
     * subtables; the guard fires part-way through `mark' and the whole `mkmk'
     * feature is then skipped, which is why a second mark on one base stays
     * stacked at the first mark's x instead of being pulled off it.  The
     * buffer's own guard next door is 0x3FFFFFFF (buffer.js l.142), so the
     * two limits were never consistent with each other.
     *
     * Raised to the same shape the length guard on the next line already
     * uses: a fixed floor plus a per-glyph factor, with the factor on ops
     * rather than the length factor it had been given.
     * W13_NO_OPSBUDGET=1 restores the old 1024 / x64 guard. */
    this.ops     = process.env.W13_NO_OPSBUDGET
                 ? Math.max( 1024, buffer.info.length * 64 )
                 : Math.max( 16384, buffer.info.length * 1024 );
    this.maxLen  = Math.max( 16384, buffer.info.length * 64 );
    this.ligIdCounter = opts.ligIdCounter || { n: 0 };
  }

  setLookup( lk, mask, autoZwnj, autoZwj, perSyllable ) {
    this.lookupIndex = lk.index;
    this.lookupFlag  = lk.flag;
    this.markSet     = lk.markFilteringSet;
    this.lookupMask  = mask === undefined ? 1 : mask;
    this.autoZwnj    = autoZwnj === undefined ? true : !!autoZwnj;
    this.autoZwj     = autoZwj  === undefined ? true : !!autoZwj;
    /* sd-use.txt l.705-721: features "applied together, one cluster at a
     * time" must not match across a cluster boundary. */
    this.perSyllable = !!perSyllable;
  }

  nextLigId() {
    this.ligIdCounter.n = ( this.ligIdCounter.n + 1 ) & MAX_LIG_ID;
    if ( !this.ligIdCounter.n ) this.ligIdCounter.n = 1;
    return this.ligIdCounter.n;
  }
}

/* chapter2.txt l.477-483.  IGNORE_MARKS supersedes a mark filtering set or a
 * mark attachment class; a mark filtering set supersedes a mark attachment
 * class. */
function checkGlyphProperty( c, info, flag, markSet ) {
  const props = info.props;

  /* "lookups must ignore glyphs of the respective type" -- the three IGNORE_*
   * bits line up with the three class bits by construction. */
  if ( props & flag & LookupFlag.IGNORE_FLAGS ) return false;

  if ( props & GlyphProps.MARK ) {
    if ( flag & LookupFlag.USE_MARK_FILTERING_SET )
      return markSetCovers( c.gdef, markSet, info.codepoint );
    const mac = flag & LookupFlag.MARK_ATTACHMENT_TYPE;
    if ( mac ) return ( props & GlyphProps.MARK_ATTACHMENT_MASK ) === mac;
  }

  return true;
}

/* Whether a glyph may be stepped over while matching.
 *   2 -- always skip (a lookup flag excludes it)
 *   1 -- skip unless it happens to match the pattern (a default ignorable)
 *   0 -- never skip
 *
 * Default ignorables (ZWNJ, ZWJ, the variation selectors, ...) sit in the
 * buffer but must not break up a context; a ligated one has become real
 * content and stops being skippable.  Whether a ZWNJ or a ZWJ specifically
 * may be skipped depends on the matcher: joiner-sensitive features need to
 * see them in the input, but no feature wants them to break a backtrack or
 * lookahead context. */
const SKIP_NO = 0, SKIP_MAYBE = 1, SKIP_YES = 2;

function maySkip( c, info, flag, markSet, ignoreZwnj, ignoreZwj, ignoreHidden ) {
  if ( !checkGlyphProperty( c, info, flag, markSet ) ) return SKIP_YES;
  /* _hb_glyph_info_is_default_ignorable(): the Unicode property AND no GSUB
   * lookup having substituted the glyph.  LIGATED alone is too narrow -- a
   * Single substitution turns an ignorable into content without ligating it,
   * and SUBSTITUTED is the bit the reference actually tests. */
  /* ...and NOT HIDDEN.  The reference's skipping test is
   * _hb_glyph_info_is_default_ignorable_AND_NOT_HIDDEN: a COMBINING GRAPHEME
   * JOINER, a Mongolian free variation selector or a TAG character is hidden
   * from the OUTPUT like any other ignorable, but a lookup must still see it,
   * which is the whole point of U+034F (harfbuzz#554) -- it exists to stop the
   * two glyphs it sits between from being treated as adjacent.  Skipping it
   * let `uni064E' and `uni0651' ligate across a CGJ that the reference keeps
   * apart.  UPROPS_MASK_HIDDEN is shape-plan.js's 0x0040; spelled out because
   * this module imports nothing.  W18_NO_HIDDENSKIP=1 restores the old test. */
  if ( info.ignorable &&
       ( ignoreZwnj || !info.zwnj ) &&
       ( ignoreZwj  || !info.zwj ) &&
       ( ignoreHidden || !( info.unicodeProps & 0x0040 ) ||
         !!process.env.W18_NO_HIDDENSKIP ) &&
       !( info.props & GlyphProps.SUBSTITUTED ) )
    return SKIP_MAYBE;
  return SKIP_NO;
}

/* A matcher bundles the lookup's skipping rules with an optional pattern
 * test.  `match' is null when the caller only wants "the next glyph that this
 * lookup does not skip" (mark attachment does exactly that). */
function makeMatcher( c, contextMatch, match, values ) {
  const anchor = c.buffer.info[ c.idx ];
  return {
    c, match, values,
    /* The cluster this match is anchored in, captured when the matcher is
     * built, exactly as the reference captures it when the iterator resets. */
    perSyllable: !!c.perSyllable,
    syllable: anchor ? anchor.syllable : undefined,
    flag: c.lookupFlag, markSet: c.markSet,
    /* A GPOS lookup never wants a ZWNJ to break a pair, and neither does any
     * backtrack/lookahead context; ZWJ is skippable in any context. */
    ignoreZwnj: c.isGpos || ( contextMatch && c.autoZwnj ),
    ignoreZwj : contextMatch || c.autoZwj,
    /* A hidden ignorable (CGJ and friends) is stepped over during GPOS but is
     * REAL INPUT during GSUB -- that is what U+034F is for. */
    ignoreHidden: !!c.isGpos,
  };
}

function matcherTest( m, info, value ) {
  if ( !m.match ) return SKIP_MAYBE;             /* "any glyph" */
  return m.match( m.c, info, value ) ? SKIP_YES : SKIP_NO;
}

/* Step forward to the next glyph matching `value'; -1 when the pattern breaks
 * before one is found. */
function skippyNext( m, from, value, stop ) {
  const info = m.c.buffer.info;
  const end  = stop === undefined ? info.length : Math.min( stop, info.length );
  let i = from;
  while ( i + 1 < end ) {
    i++;
    const g = info[i];
    if ( m.perSyllable && m.syllable !== undefined && g.syllable !== m.syllable )
      return -1;
    const skip = maySkip( m.c, g, m.flag, m.markSet, m.ignoreZwnj, m.ignoreZwj,
                          m.ignoreHidden );
    if ( skip === SKIP_YES ) continue;
    const hit = matcherTest( m, g, value );
    if ( hit === SKIP_YES || ( hit === SKIP_MAYBE && skip === SKIP_NO ) ) return i;
    if ( skip === SKIP_NO ) return -1;
  }
  return -1;
}

function skippyPrev( m, from, value ) {
  const info = m.c.buffer.info;
  let i = from;
  while ( i > 0 ) {
    i--;
    const g = info[i];
    if ( m.perSyllable && m.syllable !== undefined && g.syllable !== m.syllable )
      return -1;
    const skip = maySkip( m.c, g, m.flag, m.markSet, m.ignoreZwnj, m.ignoreZwj,
                          m.ignoreHidden );
    if ( skip === SKIP_YES ) continue;
    const hit = matcherTest( m, g, value );
    if ( hit === SKIP_YES || ( hit === SKIP_MAYBE && skip === SKIP_NO ) ) return i;
    if ( skip === SKIP_NO ) return -1;
  }
  return -1;
}


/* ------------------------------------------------------------------ */
/*  input / backtrack / lookahead matching                             */
/* ------------------------------------------------------------------ */

/* Match `count' glyphs starting at the current position (count includes the
 * current glyph, which the caller has already tested against the Coverage
 * table).  Returns the absolute positions of the matched glyphs and the index
 * one past the last, or null.
 *
 * The ligature-component test implements the requirement that a ligature's
 * pieces stay together: once the first glyph of the sequence is itself part
 * of a previously formed ligature component, every other glyph must belong to
 * the same component, and a glyph that belongs to some *other* ligature must
 * not be swept into this one (gpos.txt l.757: "the text-layout client must
 * keep track of associations of marks to particular ligature-glyph
 * components"). */
function matchInput( c, count, match, values ) {
  if ( count > MAX_CONTEXT_LEN ) return null;
  const info = c.buffer.info;
  const m = makeMatcher( c, false, match, values );

  const positions = [ c.idx ];
  let totalComponents = info[ c.idx ].numComps;

  const firstLigId   = info[ c.idx ].ligId;
  const firstLigComp = info[ c.idx ].ligComp;
  /* Tri-state: whether a glyph already attached to the first component may be
   * absorbed.  It may, but only once we know the first glyph is a ligature
   * base rather than a component of someone else's ligature. */
  let ligbase = 0;            /* 0 unchecked, 1 may not skip, 2 may skip */

  let i = c.idx;
  for ( let k = 1; k < count; k++ ) {
    i = skippyNext( m, i, values ? values[k - 1] : undefined );
    if ( i < 0 ) return null;

    const g = info[i];
    const thisLigId = g.ligId, thisLigComp = g.ligComp;

    if ( firstLigId && firstLigComp ) {
      /* The first glyph is attached to an earlier ligature component; the
       * rest must be attached to the very same one. */
      if ( firstLigId !== thisLigId || firstLigComp !== thisLigComp ) return null;
    } else if ( thisLigId && thisLigComp && thisLigId !== firstLigId ) {
      /* This glyph belongs to a different ligature -- unless it is attached
       * to the first glyph itself, which only happens when the first glyph is
       * a ligature we are allowed to extend. */
      if ( ligbase === 0 ) {
        const found = ligIdAttachesTo( info, c.idx, thisLigId );
        ligbase = found ? 2 : 1;
      }
      if ( ligbase === 1 ) return null;
    }

    positions.push( i );
    totalComponents += g.numComps;
  }

  return { positions, end: i + 1, totalComponents };
}

/* Does any glyph before `at' carry `ligId' as a ligature it started?  Used
 * only by the component test above. */
function ligIdAttachesTo( info, at, ligId ) {
  for ( let i = at; i >= 0; i-- ) {
    if ( info[i].ligId === ligId && !info[i].ligComp ) return true;
    if ( info[i].ligId !== ligId ) return false;
  }
  return false;
}

/* chapter2.txt l.990: "the backtrack sequence is given in reverse logical
 * order: if the current glyph is at position i, the backtrack sequence begins
 * at i-1 and decrements offset values moving away from i." */
function matchBacktrack( c, count, match, values ) {
  if ( count > MAX_CONTEXT_LEN ) return false;
  const m = makeMatcher( c, true, match, values );
  let i = c.idx;
  for ( let k = 0; k < count; k++ ) {
    i = skippyPrev( m, i, values ? values[k] : undefined );
    if ( i < 0 ) return false;
  }
  return true;
}

/* The lookahead sequence begins after the input sequence and increases in
 * logical order (chapter2.txt l.990). */
function matchLookahead( c, count, match, values, startAt ) {
  if ( count > MAX_CONTEXT_LEN ) return false;
  const m = makeMatcher( c, true, match, values );
  let i = startAt - 1;
  for ( let k = 0; k < count; k++ ) {
    i = skippyNext( m, i, values ? values[k] : undefined );
    if ( i < 0 ) return false;
  }
  return true;
}

/* Pattern tests for the three subtable format families. */
function matchGlyph( c, info, gid )   { return info.codepoint === gid; }
function matchClass( c, info, spec )  {
  return classOf( c.d, spec.off, info.codepoint ) === spec.klass;
}
function matchCoverage( c, info, off ) {
  return coverageIndex( c.d, off, info.codepoint ) >= 0;
}


/* ------------------------------------------------------------------ */
/*  nested lookups -- chapter2.txt l.726-748, l.838                    */
/* ------------------------------------------------------------------ */

/* Apply the SequenceLookupRecords of a matched context.
 *
 * "All of the sequence lookup records are processed in order, and each
 * applies to the results of the actions indicated by the preceding record"
 * and "the sequence position index in each sequence lookup record is relative
 * to the glyph sequence as modified by the actions of preceding
 * SequenceLookupRecords" (chapter2.txt l.529, l.838).
 *
 * So the recorded positions have to be re-based after every nested lookup
 * that changes the buffer length: a ligature inside the context shortens it
 * and shifts everything after, a multiple substitution lengthens it and the
 * glyphs it produced take sequence positions of their own. */
function applySequenceLookups( c, records, matched ) {
  let positions = matched.positions.slice();
  let count = positions.length;
  let end = matched.end;

  for ( const rec of records ) {
    let idx = rec.sequenceIndex;
    if ( idx >= count ) continue;
    if ( positions[idx] >= c.buffer.info.length ) continue;
    if ( c.ops-- <= 0 ) break;

    const before = c.buffer.info.length;
    const savedIdx = c.idx;
    c.idx = positions[idx];
    recurse( c, rec.lookupListIndex );
    c.idx = savedIdx;

    const delta = c.buffer.info.length - before;
    if ( !delta ) continue;

    end += delta;
    if ( end < positions[idx] ) { end = positions[idx]; break; }

    let next = idx + 1;
    if ( delta > 0 ) {
      if ( count + delta > MAX_CONTEXT_LEN ) break;
      /* The nested lookup produced extra glyphs at this position; they become
       * sequence positions idx+1 .. idx+delta, and everything after shifts. */
      const tail = positions.slice( next ).map( ( p ) => p + delta );
      const grown = [];
      for ( let k = 0; k < delta; k++ ) grown.push( positions[ idx + k ] + 1 );
      positions = positions.slice( 0, next ).concat( grown, tail );
      count += delta;
    } else {
      /* delta < 0: the nested lookup consumed glyphs.  Drop the sequence
       * positions it swallowed and shift the rest back. */
      const drop = Math.min( -delta, count - next );
      positions.splice( next, drop );
      count -= drop;
      for ( let k = next; k < count; k++ ) positions[k] += delta;
    }
  }

  /* "the client advances to the glyph position immediately following the
   * matched input sequence; in particular, the client does not advance past
   * the matched lookahead sequence" (chapter2.txt l.718). */
  c.idx = Math.min( Math.max( end, 0 ), c.buffer.info.length );
}

function recurse( c, lookupIndex ) {
  if ( c.nesting >= MAX_NESTING ) return false;
  const lk = c.table.lookups[ lookupIndex ];
  if ( !lk ) return false;

  /* "Lookup flags in the main lookup table are not considered while the
   * nested lookups are being processed.  Instead, lookup flags in the nested
   * lookups are considered." (chapter2.txt l.493) */
  const saved = { flag: c.lookupFlag, markSet: c.markSet, index: c.lookupIndex };
  c.lookupFlag  = lk.flag;
  c.markSet     = lk.markFilteringSet;
  c.lookupIndex = lookupIndex;
  c.nesting++;

  let applied = false;
  const info = c.buffer.info[ c.idx ];
  /* The nested lookup still only applies where its own flags allow. */
  if ( info && checkGlyphProperty( c, info, lk.flag, lk.markFilteringSet ) )
    applied = applySubtables( c, lk );

  c.nesting--;
  c.lookupFlag  = saved.flag;
  c.markSet     = saved.markSet;
  c.lookupIndex = saved.index;
  return applied;
}


/* ------------------------------------------------------------------ */
/*  shared context subtable readers                                    */
/* ------------------------------------------------------------------ */

function readSeqLookupRecords( d, off, count ) {
  const out = [];
  for ( let i = 0; i < count; i++ ) {
    out.push( { sequenceIndex : rU16( d, off + i * 4 ),
                lookupListIndex: rU16( d, off + i * 4 + 2 ) } );
  }
  return out;
}

/* SequenceContext formats 1-3 -- chapter2.txt l.749-979.  Shared verbatim by
 * GSUB type 5 and GPOS type 7. */
function applyContext( c, off ) {
  const d = c.d;
  const fmt = rU16( d, off );
  const gid = c.buffer.info[ c.idx ].codepoint;

  if ( fmt === 1 ) {
    const cov = rU16( d, off + 2 );
    const ci = coverageIndex( d, cov ? off + cov : 0, gid );
    if ( ci < 0 ) return false;
    const n = rU16( d, off + 4 );
    if ( ci >= n ) return false;
    const rsOff = rU16( d, off + 6 + ci * 2 );
    if ( !rsOff ) return false;
    const rs = off + rsOff;

    const ruleCount = rU16( d, rs );
    for ( let r = 0; r < ruleCount; r++ ) {
      const ro = rU16( d, rs + 2 + r * 2 );
      if ( !ro ) continue;
      const rule = rs + ro;
      const glyphCount = rU16( d, rule );
      const lookupCount = rU16( d, rule + 2 );
      if ( glyphCount < 1 ) continue;
      /* inputSequence[glyphCount - 1], starting with the second glyph. */
      const values = [];
      for ( let k = 0; k < glyphCount - 1; k++ )
        values.push( rU16( d, rule + 4 + k * 2 ) );
      const matched = matchInput( c, glyphCount, matchGlyph, values );
      if ( !matched ) continue;
      const recs = readSeqLookupRecords( d, rule + 4 + ( glyphCount - 1 ) * 2,
                                         lookupCount );
      applySequenceLookups( c, recs, matched );
      return true;                       /* first matching rule wins (l.805) */
    }
    return false;
  }

  if ( fmt === 2 ) {
    const cov = rU16( d, off + 2 );
    if ( coverageIndex( d, cov ? off + cov : 0, gid ) < 0 ) return false;
    const cdOff = rU16( d, off + 4 );
    const cd = cdOff ? off + cdOff : 0;
    const klass = classOf( d, cd, gid );
    const n = rU16( d, off + 6 );
    if ( klass >= n ) return false;
    const rsOff = rU16( d, off + 8 + klass * 2 );
    if ( !rsOff ) return false;
    const rs = off + rsOff;

    const ruleCount = rU16( d, rs );
    for ( let r = 0; r < ruleCount; r++ ) {
      const ro = rU16( d, rs + 2 + r * 2 );
      if ( !ro ) continue;
      const rule = rs + ro;
      const glyphCount = rU16( d, rule );
      const lookupCount = rU16( d, rule + 2 );
      if ( glyphCount < 1 ) continue;
      const values = [];
      for ( let k = 0; k < glyphCount - 1; k++ )
        values.push( { off: cd, klass: rU16( d, rule + 4 + k * 2 ) } );
      const matched = matchInput( c, glyphCount, matchClass, values );
      if ( !matched ) continue;
      const recs = readSeqLookupRecords( d, rule + 4 + ( glyphCount - 1 ) * 2,
                                         lookupCount );
      applySequenceLookups( c, recs, matched );
      return true;
    }
    return false;
  }

  if ( fmt === 3 ) {
    const glyphCount  = rU16( d, off + 2 );
    const lookupCount = rU16( d, off + 4 );
    if ( glyphCount < 1 ) return false;
    const covs = [];
    for ( let k = 0; k < glyphCount; k++ )
      covs.push( off + rU16( d, off + 6 + k * 2 ) );
    if ( coverageIndex( d, covs[0], gid ) < 0 ) return false;
    const matched = matchInput( c, glyphCount, matchCoverage, covs.slice( 1 ) );
    if ( !matched ) return false;
    const recs = readSeqLookupRecords( d, off + 6 + glyphCount * 2, lookupCount );
    applySequenceLookups( c, recs, matched );
    return true;
  }

  return false;
}

/* ChainedSequenceContext formats 1-3 -- chapter2.txt l.980-1348.  Shared by
 * GSUB type 6 and GPOS type 8. */
function applyChainContext( c, off ) {
  const d = c.d;
  const fmt = rU16( d, off );
  const gid = c.buffer.info[ c.idx ].codepoint;

  if ( fmt === 1 ) {
    const cov = rU16( d, off + 2 );
    const ci = coverageIndex( d, cov ? off + cov : 0, gid );
    if ( ci < 0 ) return false;
    if ( ci >= rU16( d, off + 4 ) ) return false;
    const rsOff = rU16( d, off + 6 + ci * 2 );
    if ( !rsOff ) return false;
    const rs = off + rsOff;

    const ruleCount = rU16( d, rs );
    for ( let r = 0; r < ruleCount; r++ ) {
      const ro = rU16( d, rs + 2 + r * 2 );
      if ( !ro ) continue;
      let p = rs + ro;

      const backCount = rU16( d, p ); p += 2;
      const back = [];
      for ( let k = 0; k < backCount; k++ ) { back.push( rU16( d, p ) ); p += 2; }

      const inputCount = rU16( d, p ); p += 2;
      if ( inputCount < 1 ) continue;
      const input = [];
      for ( let k = 0; k < inputCount - 1; k++ ) { input.push( rU16( d, p ) ); p += 2; }

      const aheadCount = rU16( d, p ); p += 2;
      const ahead = [];
      for ( let k = 0; k < aheadCount; k++ ) { ahead.push( rU16( d, p ) ); p += 2; }

      const lookupCount = rU16( d, p ); p += 2;

      const matched = matchInput( c, inputCount, matchGlyph, input );
      if ( !matched ) continue;
      if ( !matchBacktrack( c, backCount, matchGlyph, back ) ) continue;
      if ( !matchLookahead( c, aheadCount, matchGlyph, ahead, matched.end ) ) continue;

      applySequenceLookups( c, readSeqLookupRecords( d, p, lookupCount ), matched );
      return true;
    }
    return false;
  }

  if ( fmt === 2 ) {
    const cov = rU16( d, off + 2 );
    if ( coverageIndex( d, cov ? off + cov : 0, gid ) < 0 ) return false;
    const bcdOff = rU16( d, off + 4 );
    const icdOff = rU16( d, off + 6 );
    const lcdOff = rU16( d, off + 8 );
    const bcd = bcdOff ? off + bcdOff : 0;
    const icd = icdOff ? off + icdOff : 0;
    const lcd = lcdOff ? off + lcdOff : 0;

    const klass = classOf( d, icd, gid );
    if ( klass >= rU16( d, off + 10 ) ) return false;
    const rsOff = rU16( d, off + 12 + klass * 2 );
    if ( !rsOff ) return false;
    const rs = off + rsOff;

    const ruleCount = rU16( d, rs );
    for ( let r = 0; r < ruleCount; r++ ) {
      const ro = rU16( d, rs + 2 + r * 2 );
      if ( !ro ) continue;
      let p = rs + ro;

      const backCount = rU16( d, p ); p += 2;
      const back = [];
      for ( let k = 0; k < backCount; k++ ) { back.push( { off: bcd, klass: rU16( d, p ) } ); p += 2; }

      const inputCount = rU16( d, p ); p += 2;
      if ( inputCount < 1 ) continue;
      const input = [];
      for ( let k = 0; k < inputCount - 1; k++ ) { input.push( { off: icd, klass: rU16( d, p ) } ); p += 2; }

      const aheadCount = rU16( d, p ); p += 2;
      const ahead = [];
      for ( let k = 0; k < aheadCount; k++ ) { ahead.push( { off: lcd, klass: rU16( d, p ) } ); p += 2; }

      const lookupCount = rU16( d, p ); p += 2;

      const matched = matchInput( c, inputCount, matchClass, input );
      if ( !matched ) continue;
      if ( !matchBacktrack( c, backCount, matchClass, back ) ) continue;
      if ( !matchLookahead( c, aheadCount, matchClass, ahead, matched.end ) ) continue;

      applySequenceLookups( c, readSeqLookupRecords( d, p, lookupCount ), matched );
      return true;
    }
    return false;
  }

  if ( fmt === 3 ) {
    let p = off + 2;
    const backCount = rU16( d, p ); p += 2;
    const back = [];
    for ( let k = 0; k < backCount; k++ ) { back.push( off + rU16( d, p ) ); p += 2; }

    const inputCount = rU16( d, p ); p += 2;
    if ( inputCount < 1 ) return false;
    const input = [];
    for ( let k = 0; k < inputCount; k++ ) { input.push( off + rU16( d, p ) ); p += 2; }

    const aheadCount = rU16( d, p ); p += 2;
    const ahead = [];
    for ( let k = 0; k < aheadCount; k++ ) { ahead.push( off + rU16( d, p ) ); p += 2; }

    const lookupCount = rU16( d, p ); p += 2;

    if ( coverageIndex( d, input[0], gid ) < 0 ) return false;
    const matched = matchInput( c, inputCount, matchCoverage, input.slice( 1 ) );
    if ( !matched ) return false;
    if ( !matchBacktrack( c, backCount, matchCoverage, back ) ) return false;
    if ( !matchLookahead( c, aheadCount, matchCoverage, ahead, matched.end ) ) return false;

    applySequenceLookups( c, readSeqLookupRecords( d, p, lookupCount ), matched );
    return true;
  }

  return false;
}


/* ------------------------------------------------------------------ */
/*  GSUB -- gsub.txt                                                   */
/* ------------------------------------------------------------------ */

/* Replace the current glyph, keeping its cluster.  The new glyph takes its
 * GDEF properties from the font; the bookkeeping bits survive, since they
 * record history, not class. */
function setGlyphClassAfterSubst( c, i, gid, guess, ligated, component ) {
  const g = c.buffer.info[i];
  let props = g.props | GlyphProps.SUBSTITUTED;
  if ( ligated ) {
    props |= GlyphProps.LIGATED;
    /* Only the *last* transformation counts: ligating something that was
     * produced by a multiple substitution makes it a ligature again. */
    props &= ~GlyphProps.MULTIPLIED;
  }
  if ( component ) props |= GlyphProps.MULTIPLIED;

  if ( c.gdef && c.gdef.hasGlyphClasses ) {
    props = ( props & GlyphProps.PRESERVE ) | glyphPropsFor( c.gdef, gid );
  } else if ( guess ) {
    props = ( props & ( GlyphProps.PRESERVE | GlyphProps.MARK_ATTACHMENT_MASK ) ) | guess;
  }
  g.props = props;
}

function replaceGlyph( c, gid ) {
  c.buffer.info[ c.idx ].codepoint = gid;
  setGlyphClassAfterSubst( c, c.idx, gid, 0, false, false );
}

/* gsub.txt l.246-315. */
function applySingleSubst( c, off ) {
  const d = c.d;
  const fmt = rU16( d, off );
  const gid = c.buffer.info[ c.idx ].codepoint;
  const cov = rU16( d, off + 2 );
  const ci = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( ci < 0 ) return false;

  let out;
  if ( fmt === 1 ) {
    /* "Addition of deltaGlyphID is modulo 65536.  If the result ... is less
     * than zero, add 65536 to obtain a valid glyph ID." (l.254) */
    out = ( gid + rI16( d, off + 4 ) ) & 0xFFFF;
  } else if ( fmt === 2 ) {
    if ( ci >= rU16( d, off + 4 ) ) return false;
    out = rU16( d, off + 6 + ci * 2 );
  } else {
    return false;
  }

  replaceGlyph( c, out );
  c.idx++;
  return true;
}

/* gsub.txt l.317-375.  "The use of multiple substitution for deletion of an
 * input glyph is prohibited.  The glyphCount value must always be greater
 * than 0." -- a zero count is treated as a deletion anyway, since the buffer
 * must stay consistent whatever the font says. */
function applyMultipleSubst( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const gid = c.buffer.info[ c.idx ].codepoint;
  const cov = rU16( d, off + 2 );
  const ci = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( ci < 0 ) return false;
  if ( ci >= rU16( d, off + 4 ) ) return false;
  const sOff = rU16( d, off + 6 + ci * 2 );
  if ( !sOff ) return false;
  const seq = off + sOff;

  const n = rU16( d, seq );
  if ( n === 0 ) {
    c.buffer.info.splice( c.idx, 1 );
    c.buffer.pos.splice( c.idx, 1 );
    return true;
  }
  if ( c.buffer.info.length + n - 1 > c.maxLen ) return false;

  const cur = c.buffer.info[ c.idx ];
  const curPos = c.buffer.pos[ c.idx ];

  if ( n === 1 ) {
    /* A one-glyph "sequence" is an in-place replacement and must not be
     * marked as multiplied -- nothing was actually split. */
    replaceGlyph( c, rU16( d, seq + 2 ) );
    c.idx++;
    return true;
  }

  /* A ligature that is being decomposed goes back to being a base glyph. */
  const guess = ( cur.props & GlyphProps.LIGATURE ) ? GlyphProps.BASE_GLYPH : 0;
  const ligId = cur.ligId;

  const infos = [], poss = [];
  for ( let k = 0; k < n; k++ ) {
    const g = Object.assign( {}, cur );
    g.codepoint = rU16( d, seq + 2 + k * 2 );
    /* Each produced glyph is component k of the thing it came from, so a
     * mark can still find the right piece -- but ONLY when the glyph being
     * split is not itself attached to a ligature.  harfbuzz#3069: "If is
     * attached to a ligature, don't disturb that", i.e. the reference sets
     * the component props only `if (!lig_id)'.  Splitting a MARK that a
     * ligature had tagged (lig id 1, component 2) renumbered it to component
     * 0, and MarkLigPos then read the ids as mismatched and fell back to the
     * ligature's LAST component -- a damma on lam_lam_hehar landed at
     * @-914,156 where the reference gives @-216,196.
     * W18_NO_MULTLIGPROPS=1 restores the unconditional renumbering. */
    if ( !ligId || process.env.W18_NO_MULTLIGPROPS ) {
      g.ligId   = ligId;
      g.ligComp = k;
      g.numComps = 1;
    }
    infos.push( g );
    poss.push( k === 0 ? curPos : newPos() );
  }

  c.buffer.info.splice( c.idx, 1, ...infos );
  c.buffer.pos.splice( c.idx, 1, ...poss );

  for ( let k = 0; k < n; k++ )
    setGlyphClassAfterSubst( c, c.idx + k, infos[k].codepoint, guess, false, true );

  c.idx += n;
  return true;
}

/* gsub.txt l.377-433.  Which alternate is chosen comes from the feature
 * value the client asked for: the shape plan gives each feature a slice of
 * the glyph mask, so the value sits in the masked bits. */
function applyAlternateSubst( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const gid = c.buffer.info[ c.idx ].codepoint;
  const cov = rU16( d, off + 2 );
  const ci = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( ci < 0 ) return false;
  if ( ci >= rU16( d, off + 4 ) ) return false;
  const aOff = rU16( d, off + 6 + ci * 2 );
  if ( !aOff ) return false;
  const set = off + aOff;

  const n = rU16( d, set );
  if ( !n ) return false;

  const mask = c.lookupMask;
  /* The feature value is stored left-shifted into the feature's own bits;
   * shifting it back down needs the index of the lowest set bit. */
  let shift = 0;
  if ( mask ) { let m = mask; while ( !( m & 1 ) ) { m >>>= 1; shift++; } }
  const altIndex = ( ( c.buffer.info[ c.idx ].mask & mask ) >>> shift );

  /* Value 0 means the feature is off at this glyph, so nothing is chosen. */
  if ( altIndex === 0 || altIndex > n ) return false;

  replaceGlyph( c, rU16( d, set + 2 + ( altIndex - 1 ) * 2 ) );
  c.idx++;
  return true;
}

/* gsub.txt l.435-517.
 *
 * "When a string of glyphs can be replaced with a single ligature glyph, the
 * first glyph is substituted with the ligature.  The remaining glyphs in the
 * string are deleted, this does not include those glyphs that are skipped as
 * a result of lookup flags." (gsub.txt l.53)
 *
 * The glyphs the lookup flags skipped stay in the buffer, and each records
 * which ligature component it now sits after, so that a later MarkLigPos can
 * put it on the right piece. */
/* _hb_glyph_info_get_lig_num_comps_in_ligation (harfbuzz#4969).
 *
 * The pieces a MultipleSubst produced after the first belong to the SAME
 * ligature component as the first piece -- mark attachment only ever offers
 * the first piece as a base -- so they contribute no component of their own
 * when the pieces are swallowed by a ligature.  Counting them made a font
 * whose `lam' is split into `_lam' + `_lam.bowl' present four components to a
 * three-component ligature, and every mark after the split then landed one
 * component too far along (@33,333 where HarfBuzz gives @22,222).
 *
 * This is NOT the same as the plain component count: `matched.totalComponents'
 * (the value stored on the ligature itself) still counts every matched glyph. */
function numCompsInLigation( g ) {
  if ( ( g.props & GlyphProps.MULTIPLIED ) && g.ligComp ) return 0;
  return g.numComps;
}

function ligate( c, matched, ligGlyph, componentCount ) {
  const info = c.buffer.info, pos = c.buffer.pos;
  const positions = matched.positions;
  const count = positions.length;

  /* Cluster merging.  harfbuzz/docs/usermanual-clusters.xml, "A clustering
   * example for levels 0 and 1": "When two or more clusters merge, the
   * resulting merged cluster takes as its cluster value the minimum of the
   * incoming cluster values", and, of its worked example, "the entirety of
   * cluster 3 merges into cluster 1, not just the D0 glyph".
   *
   * Without this the ligature record merely inherits the first component's
   * cluster, because it replaces that component in place -- which is exactly
   * the LEVEL 2 rule the same document states ("the cluster value of the first
   * glyph is retained as the cluster value for the resulting ligature ... no
   * subsequent characters -- including marks and modifiers -- are affected").
   * So every level-0/1 run was numbered as if it were level 2, and a mark
   * trailing an absorbed component kept its own cluster number.
   * mergeClusters() is itself a no-op above level 1, so the level-2 behaviour
   * stays correct because the buffer enforces it, not because the call is
   * omitted.
   *
   * buffer.len is the PRE-substitution length until shape-plan.js resyncs it
   * when the stage returns, while `info` has already shrunk under earlier
   * ligatures in this same lookup.  _mergeClustersImpl's "extend end" walk is
   * bounded by len, so it would index past the array and read undefined.
   * Sync it for the duration of the call only. */
  if ( !process.env.W8_NO_LIGCLUSTER &&
       typeof c.buffer.mergeClusters === 'function' ) {
    /* ensureBuffer() accepts a plain object as a buffer, so the Buffer methods
     * cannot be assumed -- selftest-s1.mjs drives applyLookups with one. */
    const savedLen = c.buffer.len;
    c.buffer.len = info.length;
    c.buffer.mergeClusters( positions[0], matched.end );
    c.buffer.len = savedLen;
  }

  /* A base plus only marks is still a base, not a ligature: subsequent marks
   * must keep attaching to it. */
  let isMarkLigature = !!( info[ positions[0] ].props & GlyphProps.MARK );
  let isBaseLigature = !!( info[ positions[0] ].props & GlyphProps.BASE_GLYPH );
  for ( let k = 1; k < count; k++ ) {
    if ( !( info[ positions[k] ].props & GlyphProps.MARK ) ) {
      isBaseLigature = false; isMarkLigature = false; break;
    }
  }
  const isLigature = !isBaseLigature && !isMarkLigature;

  const ligId = isLigature ? c.nextLigId() : 0;
  const klass = isLigature ? GlyphProps.LIGATURE : 0;

  let lastLigId     = info[ positions[0] ].ligId;
  let lastNumComps  = info[ positions[0] ].numComps;
  let componentsSoFar = lastNumComps;

  /* The ligature replaces the first component in place. */
  const first = positions[0];
  info[ first ].codepoint = ligGlyph;
  if ( isLigature ) {
    info[ first ].ligId    = ligId;
    info[ first ].ligComp  = 0;
    info[ first ].numComps = matched.totalComponents;
  }
  setGlyphClassAfterSubst( c, first, ligGlyph, klass, true, false );

  /* Walk the span, tagging the skipped glyphs and collecting the components
   * to delete. */
  const remove = [];
  for ( let k = 1; k < count; k++ ) {
    for ( let i = positions[ k - 1 ] + 1; i < positions[k]; i++ ) {
      if ( isLigature ) {
        let thisComp = info[i].ligComp;
        if ( thisComp === 0 ) thisComp = lastNumComps;
        info[i].ligId   = ligId;
        info[i].ligComp = componentsSoFar - lastNumComps +
                          Math.min( thisComp, lastNumComps );
      }
    }
    lastLigId    = info[ positions[k] ].ligId;
    lastNumComps = numCompsInLigation( info[ positions[k] ] );
    componentsSoFar += lastNumComps;
    remove.push( positions[k] );
  }

  /* "If a ligature is formed of components some of which are themselves
   * ligatures, and those had marks attached to *their* components, the marks
   * have to be moved to the new ligature's component positions -- and they may
   * follow the last component of the whole sequence, so look forward for
   * them."  Without this pass such a mark keeps the inner ligature's id and
   * component and MarkLigPos then falls back to the last component. */
  if ( !isMarkLigature && lastLigId ) {
    for ( let i = positions[ count - 1 ] + 1; i < info.length; i++ ) {
      if ( info[i].ligId !== lastLigId ) break;
      const thisComp = info[i].ligComp;
      if ( !thisComp ) break;
      info[i].ligId   = ligId;
      info[i].ligComp = componentsSoFar - lastNumComps +
                        Math.min( thisComp, lastNumComps );
    }
  }

  /* Delete back to front so the earlier indices stay valid. */
  for ( let k = remove.length - 1; k >= 0; k-- ) {
    c.buffer.info.splice( remove[k], 1 );
    c.buffer.pos.splice( remove[k], 1 );
  }

  /* Carry on just past the span, which has shrunk by the components we
   * removed. */
  c.idx = matched.end - remove.length;
  if ( c.idx <= first ) c.idx = first + 1;
}

function applyLigatureSubst( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const gid = c.buffer.info[ c.idx ].codepoint;
  const cov = rU16( d, off + 2 );
  const ci = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( ci < 0 ) return false;
  if ( ci >= rU16( d, off + 4 ) ) return false;
  const lsOff = rU16( d, off + 6 + ci * 2 );
  if ( !lsOff ) return false;
  const set = off + lsOff;

  const n = rU16( d, set );
  /* "The order in the Ligature offset array defines the preference for using
   * the ligatures" (l.474) -- first match wins. */
  for ( let k = 0; k < n; k++ ) {
    const lo = rU16( d, set + 2 + k * 2 );
    if ( !lo ) continue;
    const lig = set + lo;
    const ligGlyph = rU16( d, lig );
    const compCount = rU16( d, lig + 2 );
    if ( compCount < 1 ) continue;

    if ( compCount === 1 ) {
      /* Degenerate one-component "ligature": a plain replacement. */
      replaceGlyph( c, ligGlyph );
      c.idx++;
      return true;
    }

    /* componentGlyphIDs[componentCount - 1], starting with the second. */
    const comps = [];
    for ( let j = 0; j < compCount - 1; j++ )
      comps.push( rU16( d, lig + 4 + j * 2 ) );

    const matched = matchInput( c, compCount, matchGlyph, comps );
    if ( !matched ) continue;

    ligate( c, matched, ligGlyph, compCount );
    return true;
  }
  return false;
}

/* gsub.txt l.651-716.  One glyph in, one glyph out, matched back to front by
 * the caller; the buffer length never changes, so no positions shift. */
function applyReverseChainSubst( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const gid = c.buffer.info[ c.idx ].codepoint;

  const cov = rU16( d, off + 2 );
  const ci = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( ci < 0 ) return false;

  let p = off + 4;
  const backCount = rU16( d, p ); p += 2;
  const back = [];
  for ( let k = 0; k < backCount; k++ ) { back.push( off + rU16( d, p ) ); p += 2; }

  const aheadCount = rU16( d, p ); p += 2;
  const ahead = [];
  for ( let k = 0; k < aheadCount; k++ ) { ahead.push( off + rU16( d, p ) ); p += 2; }

  const n = rU16( d, p ); p += 2;
  if ( ci >= n ) return false;

  if ( !matchBacktrack( c, backCount, matchCoverage, back ) ) return false;
  /* The input is a single glyph, so the lookahead starts right after it. */
  if ( !matchLookahead( c, aheadCount, matchCoverage, ahead, c.idx + 1 ) ) return false;

  replaceGlyph( c, rU16( d, p + ci * 2 ) );
  /* The caller steps backwards; do not move idx here. */
  return true;
}


/* ------------------------------------------------------------------ */
/*  GPOS -- gpos.txt                                                   */
/* ------------------------------------------------------------------ */

function valueRecordSize( format ) {
  let n = 0;
  for ( let b = 1; b <= 0x80; b <<= 1 ) if ( format & b ) n++;
  return n * 2;
}

/* gpos.txt l.1096-1218.  Fields appear in the fixed order of the ValueRecord
 * definition; "if the associated ValueFormat flags indicate that a field is
 * not present, then the next present field follows immediately".
 * `devBase' is the "immediate parent table" the Device offsets are measured
 * from (l.1126). */
function applyValueRecord( c, format, off, devBase, posIndex ) {
  if ( !format ) return false;
  const d = c.d, f = c.font;
  const p = c.buffer.pos[ posIndex ];
  let q = off;
  let changed = false;

  if ( format & VF_X_PLACEMENT ) { p.xOffset  += emMult( rI16( d, q ), f.xMult ); q += 2; changed = true; }
  if ( format & VF_Y_PLACEMENT ) { p.yOffset  += emMult( rI16( d, q ), f.yMult ); q += 2; changed = true; }
  if ( format & VF_X_ADVANCE )   {
    if ( c.horizontal ) { p.xAdvance += emMult( rI16( d, q ), f.xMult ); changed = true; }
    q += 2;
  }
  if ( format & VF_Y_ADVANCE )   {
    if ( !c.horizontal ) { p.yAdvance += emMult( rI16( d, q ), f.yMult ); changed = true; }
    q += 2;
  }

  if ( !( format & VF_DEVICE_MASK ) ) return changed;

  /* Device corrections are inert without a ppem, which is the normal case for
   * an unrounded shaping run; the offsets are still stepped over. */
  const xPpem = f.xPpem, yPpem = f.yPpem;

  /* A Device table needs a ppem to say anything; a VariationIndex does not,
   * so these are no longer gated on ppem. */
  const R = c.resolveVarDelta;
  const sx = ( v ) => emMult( v, f.xMult ), sy = ( v ) => emMult( v, f.yMult );
  const sxf = ( v ) => emMultF( v, f.xMultF ), syf = ( v ) => emMultF( v, f.yMultF );

  if ( format & VF_X_PLACEMENT_DEVICE ) {
    const o = rU16( d, q ); q += 2;
    if ( o ) { const v = deviceDelta( d, devBase + o, xPpem, f.xScale, R, sx, sxf );
               if ( v ) { p.xOffset += v; changed = true; } }
  }
  if ( format & VF_Y_PLACEMENT_DEVICE ) {
    const o = rU16( d, q ); q += 2;
    if ( o ) { const v = deviceDelta( d, devBase + o, yPpem, f.yScale, R, sy, syf );
               if ( v ) { p.yOffset += v; changed = true; } }
  }
  if ( format & VF_X_ADVANCE_DEVICE ) {
    const o = rU16( d, q ); q += 2;
    if ( o && c.horizontal ) { const v = deviceDelta( d, devBase + o, xPpem, f.xScale, R, sx, sxf );
               if ( v ) { p.xAdvance += v; changed = true; } }
  }
  if ( format & VF_Y_ADVANCE_DEVICE ) {
    const o = rU16( d, q ); q += 2;
    if ( o && !c.horizontal ) { const v = deviceDelta( d, devBase + o, yPpem, f.yScale, R, sy, syf );
               if ( v ) { p.yAdvance += v; changed = true; } }
  }

  return changed;
}

/* gpos.txt l.1219-1329.  Returns the anchor in output units, as a FLOAT.
 *
 * The reference's Anchor::get_anchor hands back `float' -- em_fscale_*, which
 * does not round -- and its callers round ONCE, on the value they actually
 * want: MarkArray::apply rounds `base_x - mark_x', CursivePos rounds
 * `entry_x - exit_x'.  Scaling each anchor to an integer here and subtracting
 * afterwards rounds TWICE and lands a unit out whenever the two halves round
 * the same way: on 0a2d222ab5ac.ttf at --font-size=1000 a 5-unit difference is
 * 5*1000/2048 = 2.44, which the reference rounds once to 2 while two roundings
 * of 2 and 3 give 3.  That one unit was 29 of step 3's misses, all of them
 * offset-only and every other glyph and cluster already correct.
 * W18_NO_ANCHORFLOAT=1 restores the per-anchor integer scaling. */
const ANCHOR_INT = !!process.env.W18_NO_ANCHORFLOAT;
const anchorX = ( d, off, f ) => ANCHOR_INT ? emMult( rI16( d, off + 2 ), f.xMult )
                                            : emFScale( rI16( d, off + 2 ), f.xMultF );
const anchorY = ( d, off, f ) => ANCHOR_INT ? emMult( rI16( d, off + 4 ), f.yMult )
                                            : emFScale( rI16( d, off + 4 ), f.yMultF );

function getAnchor( c, off, gid ) {
  const d = c.d, f = c.font;
  if ( !off ) return { x: 0, y: 0 };
  const fmt = rU16( d, off );

  if ( fmt === 1 ) {
    return { x: anchorX( d, off, f ), y: anchorY( d, off, f ) };
  }

  if ( fmt === 2 ) {
    /* "AnchorFormat2 also provides an index to a glyph contour point ... the
     * anchor point will provide the final positioning data FOR A GIVEN PPEM
     * SIZE."  That last clause is the whole rule: the contour point is a
     * hinted, size-specific position, so the reference consults it only when
     * the font has a ppem, and then only for the axis whose ppem is set --
     *   ret = (x_ppem || y_ppem) && get_glyph_contour_point_for_origin(...);
     *   *x = ret && x_ppem ? cx : em_fscale_x (xCoordinate);
     *   *y = ret && y_ppem ? cy : em_fscale_y (yCoordinate);
     * hb-shape leaves ppem at 0 unless asked, so an unscaled run must use the
     * DESIGN coordinates.  Taking the contour point regardless read a cursive
     * entry of (130,200) off the outline where the anchor says (182,57), which
     * moved both the advance and the whole cursive chain hanging off it.
     * W18_NO_ANCHOR2PPEM=1 restores the unconditional contour point. */
    const xPpem = f.xPpem | 0, yPpem = f.yPpem | 0;
    const legacy = !!process.env.W18_NO_ANCHOR2PPEM;
    if ( f.getGlyphContourPoint && ( legacy || xPpem || yPpem ) ) {
      const pt = f.getGlyphContourPoint( gid, rU16( d, off + 6 ) );
      if ( pt ) return {
        x: ( legacy || xPpem ) ? ( pt.x | 0 ) : anchorX( d, off, f ),
        y: ( legacy || yPpem ) ? ( pt.y | 0 ) : anchorY( d, off, f ),
      };
    }
    return { x: anchorX( d, off, f ), y: anchorY( d, off, f ) };
  }

  if ( fmt === 3 ) {
    let x = anchorX( d, off, f );
    let y = anchorY( d, off, f );
    const xd = rU16( d, off + 6 ), yd = rU16( d, off + 8 );
    const R = c.resolveVarDelta;
    if ( xd ) x += deviceDelta( d, off + xd, f.xPpem, f.xScale, R,
                                ( v ) => emMult( v, f.xMult ),
                                ( v ) => emMultF( v, f.xMultF ) );
    if ( yd ) y += deviceDelta( d, off + yd, f.yPpem, f.yScale, R,
                                ( v ) => emMult( v, f.yMult ),
                                ( v ) => emMultF( v, f.yMultF ) );
    return { x, y };
  }

  return { x: 0, y: 0 };
}

/* gpos.txt l.285-374. */
function applySinglePos( c, off ) {
  const d = c.d;
  const fmt = rU16( d, off );
  const gid = c.buffer.info[ c.idx ].codepoint;
  const cov = rU16( d, off + 2 );
  const ci = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( ci < 0 ) return false;

  const format = rU16( d, off + 4 );

  if ( fmt === 1 ) {
    applyValueRecord( c, format, off + 6, off, c.idx );
  } else if ( fmt === 2 ) {
    const n = rU16( d, off + 6 );
    if ( ci >= n ) return false;
    const size = valueRecordSize( format );
    applyValueRecord( c, format, off + 8 + ci * size, off, c.idx );
  } else {
    return false;
  }

  c.idx++;
  return true;
}

/* gpos.txt l.376-588.
 *
 * "If valueFormat2 is set to 0, then the second glyph of the pair is the
 * 'next' glyph for which a lookup should be performed" (l.392) -- otherwise
 * the pair is consumed whole. */
function applyPairPos( c, off ) {
  const d = c.d;
  const fmt = rU16( d, off );
  const gid = c.buffer.info[ c.idx ].codepoint;
  const cov = rU16( d, off + 2 );
  const ci = coverageIndex( d, cov ? off + cov : 0, gid );
  if ( ci < 0 ) return false;

  /* The second glyph of the pair is the next one this lookup does not skip. */
  const m = makeMatcher( c, false, null, null );
  const j = skippyNext( m, c.idx, undefined );
  if ( j < 0 ) return false;

  const vf1 = rU16( d, off + 4 );
  const vf2 = rU16( d, off + 6 );
  const size1 = valueRecordSize( vf1 );
  const size2 = valueRecordSize( vf2 );
  const second = c.buffer.info[j].codepoint;

  if ( fmt === 1 ) {
    if ( ci >= rU16( d, off + 8 ) ) return false;
    const psOff = rU16( d, off + 10 + ci * 2 );
    if ( !psOff ) return false;
    const ps = off + psOff;

    const n = rU16( d, ps );
    const recSize = 2 + size1 + size2;
    /* "records sorted by the glyph ID of the second glyph" (l.435). */
    let lo = 0, hi = n - 1, found = -1;
    while ( lo <= hi ) {
      const mid = ( lo + hi ) >> 1;
      const g = rU16( d, ps + 2 + mid * recSize );
      if ( second < g ) hi = mid - 1;
      else if ( second > g ) lo = mid + 1;
      else { found = mid; break; }
    }
    if ( found < 0 ) return false;

    const rec = ps + 2 + found * recSize + 2;
    /* Device offsets in a PairPosFormat1 ValueRecord are measured from the
     * PairSet table (l.1126). */
    applyValueRecord( c, vf1, rec, ps, c.idx );
    applyValueRecord( c, vf2, rec + size1, ps, j );

    c.idx = vf2 ? j + 1 : j;
    return true;
  }

  if ( fmt === 2 ) {
    const cd1Off = rU16( d, off + 8 );
    const cd2Off = rU16( d, off + 10 );
    const c1n = rU16( d, off + 12 );
    const c2n = rU16( d, off + 14 );
    const k1 = classOf( d, cd1Off ? off + cd1Off : 0, gid );
    const k2 = classOf( d, cd2Off ? off + cd2Off : 0, second );
    if ( k1 >= c1n || k2 >= c2n ) return false;

    const recSize = size1 + size2;
    const rec = off + 16 + ( k1 * c2n + k2 ) * recSize;
    applyValueRecord( c, vf1, rec, off, c.idx );
    applyValueRecord( c, vf2, rec + size1, off, j );

    c.idx = vf2 ? j + 1 : j;
    return true;
  }

  return false;
}

/* gpos.txt l.589-655.
 *
 * The current glyph is the *second* of the pair: it must carry an entry
 * anchor, and the previous non-skipped glyph must carry an exit anchor. */
function applyCursivePos( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const info = c.buffer.info, pos = c.buffer.pos;
  const j = c.idx;
  const cov = rU16( d, off + 2 );
  const covAbs = cov ? off + cov : 0;
  const n = rU16( d, off + 4 );

  const cj = coverageIndex( d, covAbs, info[j].codepoint );
  if ( cj < 0 || cj >= n ) return false;
  const entryOff = rU16( d, off + 6 + cj * 4 );
  if ( !entryOff ) return false;

  const m = makeMatcher( c, false, null, null );
  const i = skippyPrev( m, j, undefined );
  if ( i < 0 ) return false;

  const cinx = coverageIndex( d, covAbs, info[i].codepoint );
  if ( cinx < 0 || cinx >= n ) return false;
  const exitOff = rU16( d, off + 6 + cinx * 4 + 2 );
  if ( !exitOff ) return false;

  const exit  = getAnchor( c, off + exitOff,  info[i].codepoint );
  const entry = getAnchor( c, off + entryOff, info[j].codepoint );

  /* "For adjustments in the line-layout direction, the layout engine adjusts
   * the advance of the first glyph (in logical order)." (l.595)  Which glyph
   * that is depends on the run direction, not on the lookup flag. */
  let dd;
  if ( c.horizontal ) {
    if ( c.forward ) {
      pos[i].xAdvance = roundf( exit.x ) + pos[i].xOffset;
      dd = roundf( entry.x ) + pos[j].xOffset;
      pos[j].xAdvance -= dd;
      pos[j].xOffset  -= dd;
    } else {
      dd = roundf( exit.x ) + pos[i].xOffset;
      pos[i].xAdvance -= dd;
      pos[i].xOffset  -= dd;
      pos[j].xAdvance = roundf( entry.x ) + pos[j].xOffset;
    }
  } else {
    if ( c.forward ) {
      pos[i].yAdvance = roundf( exit.y ) + pos[i].yOffset;
      dd = roundf( entry.y ) + pos[j].yOffset;
      pos[j].yAdvance -= dd;
      pos[j].yOffset  -= dd;
    } else {
      dd = roundf( exit.y ) + pos[i].yOffset;
      pos[i].yAdvance -= dd;
      pos[i].yOffset  -= dd;
      pos[j].yAdvance = roundf( entry.y ) + pos[j].yOffset;
    }
  }

  /* "if the RIGHT_TO_LEFT flag is clear, the second glyph is adjusted to
   * align anchors with the first glyph; if the RIGHT_TO_LEFT flag is set, the
   * first glyph is adjusted to align anchors with the second glyph" (l.597).
   * The adjusted glyph hangs off the other one, so that a whole connected run
   * accumulates (l.599). */
  const rtl = !!( c.lookupFlag & LookupFlag.RIGHT_TO_LEFT );

  /* Exactly ONE glyph of a connected sequence keeps its initial cross-stream
   * position, and every other connected glyph is adjusted relative to it
   * (l.599).  Model that as a tree whose root is the unadjusted glyph:
   * `child' is the one the flag says to adjust, `parent' the one it hangs
   * off. */
  let child = i, parent = j;
  /* roundf (entry_x - exit_x): again ONE rounding, of the difference. */
  let xOff = roundf( entry.x - exit.x ), yOff = roundf( entry.y - exit.y );
  if ( !rtl ) { child = j; parent = i; xOff = -xOff; yOff = -yOff; }

  /* If `child' was ALREADY the root of a chain -- a lookup with the other
   * RIGHT_TO_LEFT setting ran earlier over the same run -- then it no longer
   * keeps its initial position, and the sequence would end up with TWO fixed
   * glyphs, which l.599 does not allow.  Re-root that sub-chain onto the new
   * parent: reverse every link and negate the offset it carried, which leaves
   * every relative placement unchanged. */
  if ( !process.env.S1_NO_CURSIVECHAIN )
    reverseCursiveMinorOffset( pos, child, c.horizontal, parent );

  pos[child].attachType  = ATTACH_CURSIVE;
  pos[child].attachChain = parent - child;
  if ( c.horizontal ) pos[child].yOffset = yOff;
  else                pos[child].xOffset = xOff;

  /* A parent still pointing back at this child would leave the two rooted on
   * each other: a connected sequence with NO fixed glyph.  Break it -- and
   * give the parent its INITIAL cross-stream position back, because l.599
   * says the glyph that ends the connected sequence "keeps its initial
   * position in the cross-stream direction relative to the baseline".  The
   * offset it is carrying was written by an earlier cursive lookup that made
   * it somebody's child; it is no longer a child, so that displacement has to
   * go.  Only cleared when the offset really did come from a cursive lookup
   * (attachType), so a SinglePos adjustment on a glyph that was never
   * cursively attached is left alone. */
  if ( !process.env.S1_NO_CURSIVECHAIN &&
       pos[parent].attachChain === -pos[child].attachChain ) {
    pos[parent].attachChain = 0;
    if ( pos[parent].attachType === ATTACH_CURSIVE ) {
      if ( c.horizontal ) pos[parent].yOffset = 0;
      else                pos[parent].xOffset = 0;
      pos[parent].attachType = ATTACH_NONE;
    }
  }

  c.idx++;
  return true;
}

/* Re-root the cursive sub-chain hanging off `i' so that it hangs off
 * `newParent' instead.  Each link is reversed and the offset it contributed
 * is negated, which keeps every relative placement identical while moving the
 * one glyph that "keeps its initial position" (gpos.txt l.599) to the far end
 * of the connected sequence.  Stops if `newParent' is already on the path,
 * since that link is about to be overwritten by the caller. */
function reverseCursiveMinorOffset( pos, i, horizontal, newParent ) {
  const chain = pos[i].attachChain, type = pos[i].attachType;
  if ( !chain || type !== ATTACH_CURSIVE ) return;
  pos[i].attachChain = 0;
  const j = i + chain;
  if ( j < 0 || j >= pos.length || j === newParent ) return;

  reverseCursiveMinorOffset( pos, j, horizontal, newParent );

  if ( horizontal ) pos[j].yOffset = -pos[i].yOffset;
  else              pos[j].xOffset = -pos[i].xOffset;
  pos[j].attachChain = -chain;
  pos[j].attachType  = type;
}

/* The common tail of the three mark attachment types (gpos.txt l.1330-1373
 * for the MarkArray, l.735/l.852/l.946 for the per-class anchor arrays).
 * `baseAnchorOff' is the absolute offset of the anchor the base offers to
 * this mark's class, or 0.
 *
 * "the mark placement is adjusted so that the mark anchor is aligned with the
 * base anchor ... Placement of the base glyph and advances of both glyphs are
 * not affected." */
function attachMark( c, markAnchorOff, baseAnchorOff, baseIndex ) {
  const info = c.buffer.info, pos = c.buffer.pos;
  const i = c.idx;

  const mark = getAnchor( c, markAnchorOff, info[i].codepoint );
  const base = getAnchor( c, baseAnchorOff, info[ baseIndex ].codepoint );

  /* MarkArray::apply -- resolve_cross_offset().  The cross-stream half of the
   * base's position is folded into the mark HERE, at attachment time, by
   * walking up the base's CURSIVE chain; positionFinish() then carries only
   * the along-stream half forward.  Doing it the other way round (letting
   * positionFinish add both halves) is wrong in both directions at once: a
   * base cursively lifted AFTER this lookup ran would move its mark with it
   * (uni064F on a uniFEE4 lifted to y=295 came out at 473 where HarfBuzz gives
   * 178), while a base already displaced BEFORE this lookup ran would leave
   * its mark behind. */
  const horizontal = c.horizontal !== false;
  let cross = horizontal ? pos[ baseIndex ].yOffset : pos[ baseIndex ].xOffset;
  for ( let gp = baseIndex, guard = pos.length;
        ( pos[gp].attachType & ATTACH_CURSIVE ) && guard-- > 0; ) {
    const chain = pos[gp].attachChain;
    if ( !chain ) break;
    const parent = gp + chain;
    /* The reference reads this index as unsigned, so a chain pointing before
     * the buffer ends the walk rather than wrapping into it. */
    if ( parent < 0 || parent >= pos.length ) break;
    gp = parent;
    cross = satAdd( cross, horizontal ? pos[gp].yOffset : pos[gp].xOffset );
  }

  /* MarkArray::apply: o.x_offset = roundf (base_x - mark_x).  One rounding,
   * of the difference -- see getAnchor(). */
  pos[i].xOffset = roundf( base.x - mark.x );
  pos[i].yOffset = roundf( base.y - mark.y );
  if ( horizontal ) pos[i].yOffset = satAdd( pos[i].yOffset, cross );
  else              pos[i].xOffset = satAdd( pos[i].xOffset, cross );
  pos[i].attachType  = ATTACH_MARK;
  pos[i].attachChain = baseIndex - i;

  c.idx++;
  return true;
}

/* Read a MarkRecord: { markClass, anchor offset }. */
function markRecord( c, markArrayOff, markIndex ) {
  const d = c.d;
  if ( markIndex >= rU16( d, markArrayOff ) ) return null;
  const p = markArrayOff + 2 + markIndex * 4;
  const anchor = rU16( d, p + 2 );
  return { klass: rU16( d, p ), anchor: anchor ? markArrayOff + anchor : 0 };
}

/* gpos.txt l.657-751.  "To identify the base glyph that combines with a mark,
 * the text-processing client must look backward in the glyph string from the
 * mark to the preceding base glyph." */
function applyMarkBasePos( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const info = c.buffer.info;
  const gid = info[ c.idx ].codepoint;

  const markCov = rU16( d, off + 2 );
  const mi = coverageIndex( d, markCov ? off + markCov : 0, gid );
  if ( mi < 0 ) return false;

  /* Look back for a non-mark glyph, whatever the lookup's own flags say:
   * the base is by definition the nearest thing that is not a mark. */
  const baseCov = rU16( d, off + 4 );
  const covOff  = baseCov ? off + baseCov : 0;
  const saveFlag = c.lookupFlag, saveSet = c.markSet;
  c.lookupFlag = LookupFlag.IGNORE_MARKS; c.markSet = 0;
  const m = makeMatcher( c, false, null, null );
  c.lookupFlag = saveFlag; c.markSet = saveSet;

  /* MarkBasePosFormat1::accept (harfbuzz#740, #1020, #4124): a glyph produced
   * by a multiple substitution only offers its attachment on the FIRST piece,
   * unless the run of pieces was broken -- a mark inside the sequence, a
   * different lig id, or a non-consecutive component number all mean the piece
   * stands on its own.  A rejected piece is stepped over only when it is not
   * itself in the base coverage. */
  const accept = ( k ) =>
    !( info[k].props & GlyphProps.MULTIPLIED ) || !info[k].ligComp ||
    ( k === 0 || !!( info[ k - 1 ].props & GlyphProps.MARK ) ||
      !( info[ k - 1 ].props & GlyphProps.MULTIPLIED ) ||
      info[k].ligId !== info[ k - 1 ].ligId ||
      info[k].ligComp !== info[ k - 1 ].ligComp + 1 );

  let j = skippyPrev( m, c.idx, undefined );
  while ( j >= 0 && !accept( j ) &&
          coverageIndex( d, covOff, info[j].codepoint ) < 0 )
    j = skippyPrev( m, j, undefined );
  if ( j < 0 ) return false;

  const bi = coverageIndex( d, covOff, info[j].codepoint );
  if ( bi < 0 ) return false;

  const classCount = rU16( d, off + 6 );
  const markArrOff = rU16( d, off + 8 );
  const baseArrOff = rU16( d, off + 10 );
  if ( !markArrOff || !baseArrOff ) return false;
  const markArray = off + markArrOff;
  const baseArray = off + baseArrOff;

  const rec = markRecord( c, markArray, mi );
  if ( !rec || rec.klass >= classCount ) return false;
  if ( bi >= rU16( d, baseArray ) ) return false;

  /* BaseRecord: baseAnchorOffsets[markClassCount], from the BaseArray. */
  const a = rU16( d, baseArray + 2 + ( bi * classCount + rec.klass ) * 2 );
  /* AnchorMatrix::get_anchor sets *found only when the matrix cell is a
   * NON-NULL offset, and the caller bails on !found so that a later subtable
   * still gets its turn.  A null cell is "this base offers nothing to this
   * mark class", NOT "attach at the origin": treating it as an anchor of
   * (0,0) attached every Malayalam virama to its base at minus the mark's own
   * anchor.  W18_NO_NULLANCHOR=1 restores the attach-at-origin reading. */
  if ( !a && !process.env.W18_NO_NULLANCHOR ) return false;
  return attachMark( c, rec.anchor, a ? baseArray + a : 0, j );
}

/* gpos.txt l.753-868.
 *
 * "For a given mark assigned to a particular class, the appropriate base
 * attachment point is determined by which ligature component the mark is
 * associated with."  A mark that was swept inside the ligature carries the
 * component it follows; one that merely trails the ligature belongs to the
 * last component. */
function applyMarkLigPos( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const info = c.buffer.info;
  const gid = info[ c.idx ].codepoint;

  const markCov = rU16( d, off + 2 );
  const mi = coverageIndex( d, markCov ? off + markCov : 0, gid );
  if ( mi < 0 ) return false;

  const ligCov = rU16( d, off + 4 );
  const covOff = ligCov ? off + ligCov : 0;
  const saveFlag = c.lookupFlag, saveSet = c.markSet;
  c.lookupFlag = LookupFlag.IGNORE_MARKS; c.markSet = 0;
  const m = makeMatcher( c, false, null, null );
  c.lookupFlag = saveFlag; c.markSet = saveSet;

  /* MarkLigPosFormat1::accept (harfbuzz#4969).  Only the FIRST piece of a
   * multiple substitution is a candidate: the later pieces may have been
   * ligated into a preceding ligature, and the mark belongs on that ligature,
   * not on the piece that happens to sit in front of it.  A rejected piece is
   * stepped over only when it is not itself in the ligature coverage. */
  let j = skippyPrev( m, c.idx, undefined );
  while ( j >= 0 &&
          ( info[j].props & GlyphProps.MULTIPLIED ) && info[j].ligComp &&
          coverageIndex( d, covOff, info[j].codepoint ) < 0 )
    j = skippyPrev( m, j, undefined );
  if ( j < 0 ) return false;

  const li = coverageIndex( d, covOff, info[j].codepoint );
  if ( li < 0 ) return false;

  const classCount = rU16( d, off + 6 );
  const markArrOff = rU16( d, off + 8 );
  const ligArrOff  = rU16( d, off + 10 );
  if ( !markArrOff || !ligArrOff ) return false;
  const markArray = off + markArrOff;
  const ligArray  = off + ligArrOff;

  const rec = markRecord( c, markArray, mi );
  if ( !rec || rec.klass >= classCount ) return false;
  if ( li >= rU16( d, ligArray ) ) return false;

  const laOff = rU16( d, ligArray + 2 + li * 2 );
  if ( !laOff ) return false;
  const ligAttach = ligArray + laOff;

  const compCount = rU16( d, ligAttach );
  if ( !compCount ) return false;

  const ligId    = info[j].ligId;
  const markId   = info[ c.idx ].ligId;
  const markComp = info[ c.idx ].ligComp;
  let comp;
  if ( ligId && ligId === markId && markComp > 0 )
    comp = Math.min( compCount, markComp ) - 1;
  else
    comp = compCount - 1;

  /* ComponentRecord: ligatureAnchorOffsets[markClassCount], from the
   * LigatureAttach table. */
  const a = rU16( d, ligAttach + 2 + ( comp * classCount + rec.klass ) * 2 );
  /* AnchorMatrix::get_anchor sets *found only when the matrix cell is a
   * NON-NULL offset, and the caller bails on !found so that a later subtable
   * still gets its turn.  A null cell is "this base offers nothing to this
   * mark class", NOT "attach at the origin": treating it as an anchor of
   * (0,0) attached every Malayalam virama to its base at minus the mark's own
   * anchor.  W18_NO_NULLANCHOR=1 restores the attach-at-origin reading. */
  if ( !a && !process.env.W18_NO_NULLANCHOR ) return false;
  return attachMark( c, rec.anchor, a ? ligAttach + a : 0, j );
}

/* gpos.txt l.870-962.
 *
 * "The mark2 glyph that combines with a mark1 glyph is the glyph preceding
 * the mark1 glyph in glyph string order (skipping glyphs according to
 * LookupFlags)." */
function applyMarkMarkPos( c, off ) {
  const d = c.d;
  if ( rU16( d, off ) !== 1 ) return false;
  const info = c.buffer.info;
  const gid = info[ c.idx ].codepoint;

  const m1Cov = rU16( d, off + 2 );
  const mi = coverageIndex( d, m1Cov ? off + m1Cov : 0, gid );
  if ( mi < 0 ) return false;

  /* Unlike mark-to-base this keeps the lookup's mark filtering, but must not
   * skip marks -- the thing being attached to is itself a mark. */
  const saveFlag = c.lookupFlag;
  c.lookupFlag = saveFlag & ~LookupFlag.IGNORE_FLAGS;
  const m = makeMatcher( c, false, null, null );
  const j = skippyPrev( m, c.idx, undefined );
  c.lookupFlag = saveFlag;
  if ( j < 0 ) return false;

  if ( !( info[j].props & GlyphProps.MARK ) ) return false;

  /* Both marks must belong to the same ligature component, or to no ligature
   * at all; otherwise they decorate different pieces and must not stack. */
  const cur = info[ c.idx ], prev = info[j];
  if ( cur.ligId === prev.ligId ) {
    if ( cur.ligId && cur.ligComp !== prev.ligComp ) return false;
  } else {
    /* A mark attached to nothing may still stack on a ligature's last
     * component. */
    if ( ( cur.ligId && !cur.ligComp ) || ( prev.ligId && !prev.ligComp ) ) {
      /* fall through: one of them is a ligature base */
    } else if ( cur.ligId || prev.ligId ) {
      return false;
    }
  }

  const m2Cov = rU16( d, off + 4 );
  const bi = coverageIndex( d, m2Cov ? off + m2Cov : 0, prev.codepoint );
  if ( bi < 0 ) return false;

  const classCount = rU16( d, off + 6 );
  const m1ArrOff = rU16( d, off + 8 );
  const m2ArrOff = rU16( d, off + 10 );
  if ( !m1ArrOff || !m2ArrOff ) return false;
  const mark1Array = off + m1ArrOff;
  const mark2Array = off + m2ArrOff;

  const rec = markRecord( c, mark1Array, mi );
  if ( !rec || rec.klass >= classCount ) return false;
  if ( bi >= rU16( d, mark2Array ) ) return false;

  const a = rU16( d, mark2Array + 2 + ( bi * classCount + rec.klass ) * 2 );
  /* AnchorMatrix::get_anchor sets *found only when the matrix cell is a
   * NON-NULL offset, and the caller bails on !found so that a later subtable
   * still gets its turn.  A null cell is "this base offers nothing to this
   * mark class", NOT "attach at the origin": treating it as an anchor of
   * (0,0) attached every Malayalam virama to its base at minus the mark's own
   * anchor.  W18_NO_NULLANCHOR=1 restores the attach-at-origin reading. */
  if ( !a && !process.env.W18_NO_NULLANCHOR ) return false;
  return attachMark( c, rec.anchor, a ? mark2Array + a : 0, j );
}


/* ------------------------------------------------------------------ */
/*  subtable dispatch                                                  */
/* ------------------------------------------------------------------ */

/* chapter2.txt l.380: "If a Lookup table has multiple subtables, the subtables
 * are processed in order ... If the glyph sequence does match the pattern of
 * a subtable, then the operations of that subtable are performed and
 * processing of the lookup is finished -- no further subtables are processed
 * for that position." */
function applySubtables( c, lk ) {
  const subs = resolveSubtables( c.table, lk );
  for ( const s of subs ) {
    if ( c.ops-- <= 0 ) return false;
    let ok = false;
    if ( c.isGpos ) {
      switch ( s.type ) {
        case 1: ok = applySinglePos( c, s.offset ); break;
        case 2: ok = applyPairPos( c, s.offset ); break;
        case 3: ok = applyCursivePos( c, s.offset ); break;
        case 4: ok = applyMarkBasePos( c, s.offset ); break;
        case 5: ok = applyMarkLigPos( c, s.offset ); break;
        case 6: ok = applyMarkMarkPos( c, s.offset ); break;
        case 7: ok = applyContext( c, s.offset ); break;
        case 8: ok = applyChainContext( c, s.offset ); break;
        default: ok = false;
      }
    } else {
      switch ( s.type ) {
        case 1: ok = applySingleSubst( c, s.offset ); break;
        case 2: ok = applyMultipleSubst( c, s.offset ); break;
        case 3: ok = applyAlternateSubst( c, s.offset ); break;
        case 4: ok = applyLigatureSubst( c, s.offset ); break;
        case 5: ok = applyContext( c, s.offset ); break;
        case 6: ok = applyChainContext( c, s.offset ); break;
        case 8: ok = applyReverseChainSubst( c, s.offset ); break;
        default: ok = false;
      }
    }
    if ( ok ) return true;
  }
  return false;
}

/* One lookup over the whole buffer.  chapter2.txt l.374: "the client processes
 * that lookup over each glyph in the sequence to which the feature has been
 * applied.  After that lookup has been processed over the entire glyph
 * sequence, it then processes the next lookup." */
export function applyLookup( c, lookupIndex, mask, autoZwnj, autoZwj,
                             perSyllable ) {
  const lk = c.table.lookups[ lookupIndex ];
  if ( !lk ) return;
  /* W12_NO_PER_SYLLABLE restores the old whole-run application. */
  c.setLookup( lk, mask, autoZwnj, autoZwj,
               !process.env.W12_NO_PER_SYLLABLE && perSyllable );

  if ( lookupIsReverse( c.table, lk ) ) {
    /* "processing of input glyph sequence goes from end to start" and the
     * substitution never changes the buffer length, so a plain backward walk
     * is enough (gsub.txt l.653, l.665). */
    for ( let i = c.buffer.info.length - 1; i >= 0; i-- ) {
      c.idx = i;
      const g = c.buffer.info[i];
      if ( !g ) continue;
      if ( ( g.mask & c.lookupMask ) &&
           checkGlyphProperty( c, g, c.lookupFlag, c.markSet ) )
        applySubtables( c, lk );
      if ( c.ops <= 0 ) break;
    }
    return;
  }

  let i = 0;
  while ( i < c.buffer.info.length ) {
    c.idx = i;
    const g = c.buffer.info[i];
    let applied = false;
    if ( ( g.mask & c.lookupMask ) &&
         checkGlyphProperty( c, g, c.lookupFlag, c.markSet ) )
      applied = applySubtables( c, lk );

    /* A subtable that applied has already moved idx past everything it
     * consumed (chapter2.txt l.378). */
    if ( applied && c.idx > i ) i = c.idx;
    else i++;

    if ( c.ops <= 0 ) break;
  }
}


/* ------------------------------------------------------------------ */
/*  would-substitute query                                             */
/* ------------------------------------------------------------------ */

/* Does this lookup substitute exactly this glyph sequence, with no other
 * context?  The Indic shaper needs it to read a font's DYNAMIC consonant
 * properties: sd-devanagari.txt l.159-179 says a consonant is classified as
 * having a half form, a reph, a below-base form and so on by applying `half',
 * `rphf', `blwf', `pref' or `pstf' to a two-glyph sequence and seeing whether
 * a substitution occurs.  No buffer is involved and nothing is modified.
 *
 * Only the lookup types that can consume a whole sequence answer true: Single
 * (one glyph) and Ligature (a run).  Contextual types are deliberately not
 * probed -- the classification is defined as a context-free substitution. */
/* would_match_input(): the input sequence is `glyphs' EXACTLY -- same length,
 * every glyph after the first matching the rule's own value.  The first glyph
 * is not re-checked; the coverage or class lookup that selected the rule set
 * already answered for it. */
function wouldMatchInput( count, glyphs, valueAt, matches ) {
  if ( count !== glyphs.length ) return false;
  for ( let i = 1; i < count; i++ )
    if ( !matches( glyphs[i], valueAt( i - 1 ) ) ) return false;
  return true;
}

/* A ContextSubst/ChainContextSubst rule set, walked for would_apply.  `ruleAt'
 * hands back one rule's absolute offset; `readRule' turns that into
 * { count, valueAt, back, ahead }. */
function wouldApplyRuleSet( d, setOff, readRule, glyphs, matches, zeroContext ) {
  if ( !setOff ) return false;
  const n = rU16( d, setOff );
  for ( let k = 0; k < n; k++ ) {
    const ro = rU16( d, setOff + 2 + k * 2 );
    if ( !ro ) continue;
    const r = readRule( setOff + ro );
    if ( !r ) continue;
    /* chain_context_would_apply_lookup: with zero context, only a rule that
     * asks for NO backtrack and NO lookahead can be judged from the glyph run
     * alone; without it the two are taken on trust. */
    if ( zeroContext && ( r.back || r.ahead ) ) continue;
    if ( wouldMatchInput( r.count, glyphs, r.valueAt, matches ) ) return true;
  }
  return false;
}

export function wouldSubstitute( table, lookupIndex, glyphs, zeroContext = true ) {
  if ( !table || table.kind !== 'GSUB' ) return false;
  const lk = table.lookups[ lookupIndex ];
  if ( !lk || !glyphs || !glyphs.length ) return false;
  const d = table.data;
  const eqGlyph = ( g, v ) => g === v;
  /* W18_NO_CTXWOULDSUB=1 answers for SingleSubst and LigatureSubst only, the
   * way this function used to -- which made a `pref' built out of chained
   * contexts look like a feature that would substitute nothing. */
  const simpleOnly = !!process.env.W18_NO_CTXWOULDSUB;

  for ( const s of resolveSubtables( table, lk ) ) {
    /* MultipleSubst, AlternateSubst and ReverseChainSingleSubst all answer the
     * same question as SingleSubst: one glyph, is it covered. */
    if ( simpleOnly && s.type !== 1 && s.type !== 4 ) continue;

    if ( s.type === 2 || s.type === 3 || s.type === 8 ) {
      if ( glyphs.length !== 1 ) continue;
      const off = s.offset;
      const cov = rU16( d, off + 2 );
      if ( coverageIndex( d, cov ? off + cov : 0, glyphs[0] ) >= 0 ) return true;
      continue;
    }

    if ( s.type === 5 ) {
      const off = s.offset, fmt = rU16( d, off );
      if ( fmt === 1 || fmt === 2 ) {
        let idx;
        let matches = eqGlyph;
        if ( fmt === 1 ) {
          const cov = rU16( d, off + 2 );
          idx = coverageIndex( d, cov ? off + cov : 0, glyphs[0] );
          if ( idx < 0 ) continue;
        } else {
          const cov = rU16( d, off + 2 );
          if ( coverageIndex( d, cov ? off + cov : 0, glyphs[0] ) < 0 ) continue;
          const cdOff = rU16( d, off + 4 );
          const cd = cdOff ? off + cdOff : 0;
          idx = classOf( d, cd, glyphs[0] );
          matches = ( g, v ) => classOf( d, cd, g ) === v;
        }
        if ( idx >= rU16( d, off + ( fmt === 1 ? 4 : 6 ) ) ) continue;
        const so = rU16( d, off + ( fmt === 1 ? 6 : 8 ) + idx * 2 );
        if ( !so ) continue;
        const readRule = ( ro ) => ( {
          count: rU16( d, ro ), back: 0, ahead: 0,
          valueAt: ( i ) => rU16( d, ro + 4 + i * 2 ),
        } );
        if ( wouldApplyRuleSet( d, off + so, readRule, glyphs, matches, false ) )
          return true;
        continue;
      }
      if ( fmt === 3 ) {
        const count = rU16( d, off + 2 );
        const covAt = ( i ) => { const o = rU16( d, off + 6 + i * 2 ); return o ? off + o : 0; };
        if ( count !== glyphs.length ) continue;
        let ok = true;
        for ( let i = 0; i < count; i++ )
          if ( coverageIndex( d, covAt( i ), glyphs[i] ) < 0 ) { ok = false; break; }
        if ( ok ) return true;
        continue;
      }
      continue;
    }

    if ( s.type === 6 ) {
      const off = s.offset, fmt = rU16( d, off );
      if ( fmt === 1 || fmt === 2 ) {
        let idx;
        let matches = eqGlyph;
        let cdIn = 0;
        if ( fmt === 1 ) {
          const cov = rU16( d, off + 2 );
          idx = coverageIndex( d, cov ? off + cov : 0, glyphs[0] );
          if ( idx < 0 ) continue;
        } else {
          const cov = rU16( d, off + 2 );
          if ( coverageIndex( d, cov ? off + cov : 0, glyphs[0] ) < 0 ) continue;
          const o = rU16( d, off + 6 );          /* inputClassDef */
          cdIn = o ? off + o : 0;
          idx = classOf( d, cdIn, glyphs[0] );
          matches = ( g, v ) => classOf( d, cdIn, g ) === v;
        }
        const setCountOff = fmt === 1 ? off + 4 : off + 10;
        const setArrOff   = fmt === 1 ? off + 6 : off + 12;
        if ( idx >= rU16( d, setCountOff ) ) continue;
        const so = rU16( d, setArrOff + idx * 2 );
        if ( !so ) continue;
        /* ChainRule: backtrack[], input[], lookahead[], then the records. */
        const readRule = ( ro ) => {
          const bc = rU16( d, ro );
          const inOff = ro + 2 + bc * 2;
          const ic = rU16( d, inOff );
          const laOff = inOff + 2 + ( ic ? ic - 1 : 0 ) * 2;
          const lac = rU16( d, laOff );
          return { count: ic, back: bc, ahead: lac,
                   valueAt: ( i ) => rU16( d, inOff + 2 + i * 2 ) };
        };
        if ( wouldApplyRuleSet( d, off + so, readRule, glyphs, matches, zeroContext ) )
          return true;
        continue;
      }
      if ( fmt === 3 ) {
        const bc = rU16( d, off + 2 );
        const inOff = off + 4 + bc * 2;
        const ic = rU16( d, inOff );
        const laOff = inOff + 2 + ic * 2;
        const lac = rU16( d, laOff );
        if ( zeroContext && ( bc || lac ) ) continue;
        if ( ic !== glyphs.length ) continue;
        let ok = true;
        for ( let i = 0; i < ic; i++ ) {
          const o = rU16( d, inOff + 2 + i * 2 );
          if ( coverageIndex( d, o ? off + o : 0, glyphs[i] ) < 0 ) { ok = false; break; }
        }
        if ( ok ) return true;
        continue;
      }
      continue;
    }

    if ( s.type === 1 ) {
      if ( glyphs.length !== 1 ) continue;
      const off = s.offset;
      const cov = rU16( d, off + 2 );
      if ( coverageIndex( d, cov ? off + cov : 0, glyphs[0] ) >= 0 ) return true;
      continue;
    }
    if ( s.type === 4 ) {
      const off = s.offset;
      if ( rU16( d, off ) !== 1 ) continue;
      const cov = rU16( d, off + 2 );
      const ci = coverageIndex( d, cov ? off + cov : 0, glyphs[0] );
      if ( ci < 0 || ci >= rU16( d, off + 4 ) ) continue;
      const lsOff = rU16( d, off + 6 + ci * 2 );
      if ( !lsOff ) continue;
      const set = off + lsOff;
      const n = rU16( d, set );
      for ( let k = 0; k < n; k++ ) {
        const lo = rU16( d, set + 2 + k * 2 );
        if ( !lo ) continue;
        const lig = set + lo;
        const compCount = rU16( d, lig + 2 );
        if ( compCount !== glyphs.length ) continue;
        let ok = true;
        for ( let j = 1; j < compCount; j++ )
          if ( rU16( d, lig + 4 + ( j - 1 ) * 2 ) !== glyphs[j] ) { ok = false; break; }
        if ( ok ) return true;
      }
      continue;
    }
  }
  return false;
}

/* The same question asked of every lookup a feature owns. */
export function featureWouldSubstitute( table, langSys, tag, glyphs,
                                        zeroContext = true ) {
  if ( !table ) return false;
  const fi = findFeature( table, langSys, tag );
  if ( fi < 0 ) return false;
  for ( const li of featureLookups( table, fi ) )
    if ( wouldSubstitute( table, li, glyphs, zeroContext ) ) return true;
  return false;
}


/* ------------------------------------------------------------------ */
/*  plan driving                                                       */
/* ------------------------------------------------------------------ */

/* A stage list is either given directly as plan.gsubStages / plan.gposStages
 * -- an array of { lookups: [ { index, mask, autoZwnj, autoZwj } ], pause }
 * -- or as the flat plan.gsubLookups / plan.gposLookups. */
function stagesOf( plan, which ) {
  const staged = plan[ which + 'Stages' ];
  if ( Array.isArray( staged ) ) return staged;
  const flat = plan[ which + 'Lookups' ];
  if ( Array.isArray( flat ) ) return [ { lookups: flat, pause: null } ];
  return [];
}

function normalizeEntry( e ) {
  if ( typeof e === 'number' ) return { index: e, mask: 1 };
  return e;
}

function makeContext( table, buffer, font, plan, layout ) {
  return new ApplyContext( table, buffer, font, {
    gdef     : layout.gdef,
    layout,
    direction: plan.direction || buffer.direction || 'ltr',
    resolveVarDelta: typeof plan.resolveVarDelta === 'function'
      ? plan.resolveVarDelta : null,
  } );
}

export function substitute( plan, buffer, font ) {
  const layout = plan.layout || plan;
  ensureBuffer( buffer );
  if ( !layout.gsub ) return buffer;

  const stages = stagesOf( plan, 'gsub' );
  if ( !stages.length ) return buffer;

  const f = normalizeFont( font, layout.face );
  const c = makeContext( layout.gsub, buffer, f, plan, layout );
  c.ligIdCounter = plan._ligIdCounter || ( plan._ligIdCounter = { n: 0 } );

  for ( const st of stages ) {
    for ( const raw of ( st.lookups || [] ) ) {
      const e = normalizeEntry( raw );
      applyLookup( c, e.index, e.mask, e.autoZwnj, e.autoZwj,
                   e.perSyllable );
    }
    if ( st.pause ) st.pause( plan, buffer, font );
  }
  return buffer;
}

export function position( plan, buffer, font ) {
  const layout = plan.layout || plan;
  ensureBuffer( buffer );

  const direction = plan.direction || buffer.direction || 'ltr';
  positionStart( buffer );

  /* hb_ot_position_complex zeroes mark widths TWICE over, and which of the two
   * fires is the shaper's choice: BY_GDEF_EARLY runs before GPOS, BY_GDEF_LATE
   * after it.  Only the late arm existed here, so USE and Myanmar -- the two
   * shapers that ask for `early' -- had their marks' advances still in place
   * while GPOS ran, and any lookup that reads an advance saw the wrong one.
   * W18_NO_ZEROMARKEARLY=1 collapses both back onto the late arm. */
  const zeroModeSel = process.env.S1_NO_ZEROMARKFIX
    ? plan.zeroMarkWidths : plan.zeroWidthMarksMode;
  const zeroEarly = !process.env.W18_NO_ZEROMARKEARLY &&
                    zeroModeSel === 'by-gdef-early';
  if ( zeroEarly ) zeroMarkWidths( buffer, !!plan.adjustMarkOffsetsWhenZeroing );

  if ( layout.gpos ) {
    const stages = stagesOf( plan, 'gpos' );
    if ( stages.length ) {
      const f = normalizeFont( font, layout.face );
      const c = makeContext( layout.gpos, buffer, f, plan, layout );
      for ( const st of stages ) {
        for ( const raw of ( st.lookups || [] ) ) {
          const e = normalizeEntry( raw );
          applyLookup( c, e.index, e.mask, e.autoZwnj, e.autoZwj,
                   e.perSyllable );
        }
        if ( st.pause ) st.pause( plan, buffer, font );
      }
    }
  }

  /* Drop the marks' advances before the attachment chains are resolved -- see
   * zeroMarkWidths() for why the order matters. */
  /* The plan field is `zeroWidthMarksMode` (shape-plan.js sets that and the
   * derived boolean `zeroMarks`); `plan.zeroMarkWidths` has never existed, so
   * this test was `undefined !== 'none'` -- always true -- and the shaper's
   * setting was silently ignored for every script.  The Indic shaper asks for
   * 'none' precisely because Kannada's two-part matra pieces U+0CCA and
   * U+0CD5 are SPACING marks (gc=Mc) that GDEF nonetheless classes MARK:
   * zeroing them drops advances of 2009 and 843 and collapses the pieces onto
   * one x position. */
  if ( zeroModeSel !== 'none' && !zeroEarly )
    zeroMarkWidths( buffer, !!plan.adjustMarkOffsetsWhenZeroing );

  /* hb_ot_position_complex's order is fixed and the comment there says so:
   * position_finish_advances, then zero_width_default_ignorables, THEN
   * position_finish_offsets.  A default-ignorable still carrying its glyph's
   * advance while the attachment chains are resolved is counted by the walk
   * that cancels the advances between a mark and its base, so every mark
   * whose base is separated from it by a ZWNJ, ZWJ or CGJ comes out displaced
   * by that glyph's width -- @869,717 where the reference gives @369,717 for a
   * ZWNJ of width 500, and @1475 where it gives @727 for a CGJ of width 748.
   * The caller zeroes them again afterwards, which is a no-op; what matters is
   * that it has happened by the time the chains are resolved.
   *
   * Done here, with the flag values inline, because this module deliberately
   * imports nothing: shape-plan.js imports the shapers, which import this. */
  if ( !process.env.W18_NO_EARLY_ZEROIGN ) zeroWidthDefaultIgnorables( buffer );

  positionFinish( buffer, direction );
  return buffer;
}

/* The whole layout stage: substitution then positioning.
 *
 * `plan' carries the lookups to run (see NOTES-S1.md); `buffer' is mutated in
 * place and returned; `font' supplies the scale and, optionally, outline
 * access for anchor format 2. */
export function applyLookups( plan, buffer, font ) {
  const layout = plan.layout || plan;
  ensureBuffer( buffer );

  if ( plan.setGlyphProps !== false )
    substituteStart( buffer, layout.gdef, plan.glyphPropsFallback );

  substitute( plan, buffer, font );
  position( plan, buffer, font );
  return buffer;
}

export default { loadLayout, applyLookups, substitute, position };
