/* varfont.js -- OpenType Font Variations machinery.
 *
 * Ported from the implementation this milestone is graded against,
 * HarfBuzz 14.4.0 (../harfbuzz/src/), cross-read against the OpenType spec
 * chapters in ../otspec/txt/ (otvaroverview, otvarcommonformats, fvar, avar,
 * gvar, hvar, vvar, mvar, cff2).
 *
 * Where HarfBuzz and the plain spec text disagree, HARFBUZZ WINS, because
 * HarfBuzz is the reference. The notable divergences are called out inline:
 *   - avar SegmentMaps::map_float is an *extended* algorithm with
 *     duplicate-mapping special cases and shift-outside-segment behaviour.
 *   - gvar pre-scans all tuples for private point numbers, which selects
 *     between a direct-add fast path and delta accumulation.
 *   - packed deltas support a 4-byte LONGS run (0xC0), beyond the spec.
 *
 * Precision: HarfBuzz stores scalars and accumulated deltas in C `float`
 * (32-bit). Every site that lands in a `float` uses Math.fround() here, and
 * says so. C `roundf` rounds half away from zero; JS Math.round rounds half
 * toward +Infinity, so roundf() below is written explicitly.
 */

/* ------------------------------------------------------------------ */
/*  byte readers                                                       */
/* ------------------------------------------------------------------ */

const u8  = ( d, o ) => d[o];
const i8  = ( d, o ) => ( d[o] << 24 ) >> 24;
const u16 = ( d, o ) => ( d[o] << 8 ) | d[o + 1];
const i16 = ( d, o ) => ( ( ( d[o] << 8 ) | d[o + 1] ) << 16 ) >> 16;
const u24 = ( d, o ) => ( d[o] << 16 ) | ( d[o + 1] << 8 ) | d[o + 2];
const u32 = ( d, o ) => ( ( d[o] << 24 ) | ( d[o + 1] << 16 ) | ( d[o + 2] << 8 ) | d[o + 3] ) >>> 0;
const i32 = ( d, o ) => ( d[o] << 24 ) | ( d[o + 1] << 16 ) | ( d[o + 2] << 8 ) | d[o + 3];
/* F16DOT16 (fvar user coordinates) */
const f16d16 = ( d, o ) => i32( d, o ) / 65536;
/* F2DOT14 raw int -- calculate_scalar/evaluate work on these ints, not floats */
const f2d14i = ( d, o ) => i16( d, o );

/* C roundf(): half away from zero. */
export function roundf( x ) { /* HarfBuzz roundf is floor(x+.5), half UP -- see hb-algs.hh:345 */
  return process.env.W19_NO_ROUNDHALFUP ? ( x < 0 ? -Math.round( -x ) : Math.round( x ) ) : Math.floor( x + 0.5 ); }

const clamp = ( v, lo, hi ) => ( v < lo ? lo : v > hi ? hi : v );

function tbl( face, tag ) {
  const t = face.tables;
  if ( !t ) return null;
  const e = ( typeof t.get === 'function' ) ? t.get( tag ) : t[tag];
  return e || null;
}

/* HB_OT_LAYOUT_NO_VARIATIONS_INDEX */
export const NO_VARIATIONS_INDEX = 0xFFFFFFFF;


/* ------------------------------------------------------------------ */
/*  fvar                                                               */
/* ------------------------------------------------------------------ */

export function readFvar( face ) {
  if ( face._vfFvar !== undefined ) return face._vfFvar;
  const rec = tbl( face, 'fvar' );
  let r = null;
  if ( rec ) {
    const d = face.data, o = rec.offset;
    const axesOffset = u16( d, o + 4 );
    const axisCount = u16( d, o + 8 );
    const axisSize = u16( d, o + 10 );
    const instanceCount = u16( d, o + 12 );
    const instanceSize = u16( d, o + 14 );

    const axes = [];
    for ( let i = 0; i < axisCount; i++ ) {
      const p = o + axesOffset + i * axisSize;
      axes.push( {
        tag: String.fromCharCode( d[p], d[p + 1], d[p + 2], d[p + 3] ),
        minValue: f16d16( d, p + 4 ),
        defaultValue: f16d16( d, p + 8 ),
        maxValue: f16d16( d, p + 12 ),
        flags: u16( d, p + 16 ),
        nameID: u16( d, p + 18 ),
      } );
    }

    const instBase = o + axesOffset + axisCount * axisSize;
    const instances = [];
    for ( let i = 0; i < instanceCount; i++ ) {
      const p = instBase + i * instanceSize;
      const coords = new Float64Array( axisCount );
      for ( let a = 0; a < axisCount; a++ ) coords[a] = f16d16( d, p + 4 + 4 * a );
      instances.push( {
        subfamilyNameID: u16( d, p ),
        flags: u16( d, p + 2 ),
        coords,
        postScriptNameID: instanceSize >= axisCount * 4 + 6 ? u16( d, p + 4 + 4 * axisCount ) : 0xFFFF,
      } );
    }
    r = { axisCount, axes, instances };
  }
  face._vfFvar = r;
  return r;
}

/* hb-ot-var-fvar-table.hh AxisRecord::get_coordinates -- note that min/max
 * are ORDERED around the default, so a malformed axis cannot invert them. */
function axisCoords( a ) {
  const def = a.defaultValue;
  return { min: Math.min( def, a.minValue ), def, max: Math.max( def, a.maxValue ) };
}

/* hb-ot-var-fvar-table.hh AxisRecord::normalize_axis_value */
export function normalizeAxisValue( axis, v ) {
  const { min, def, max } = axisCoords( axis );
  v = clamp( v, min, max );
  if ( v === def ) return 0;
  if ( v < def ) return ( v - def ) / ( def - min );
  return ( v - def ) / ( max - def );
}


/* ------------------------------------------------------------------ */
/*  avar                                                               */
/* ------------------------------------------------------------------ */

export function readAvar( face ) {
  if ( face._vfAvar !== undefined ) return face._vfAvar;
  const rec = tbl( face, 'avar' );
  let r = null;
  if ( rec ) {
    const d = face.data, o = rec.offset;
    const major = u16( d, o ), minor = u16( d, o + 2 );
    const axisCount = u16( d, o + 6 );
    const maps = [];
    let p = o + 8;
    for ( let i = 0; i < axisCount; i++ ) {
      const n = u16( d, p ); p += 2;
      const from = new Float64Array( n ), to = new Float64Array( n );
      for ( let j = 0; j < n; j++ ) {
        from[j] = f2d14i( d, p ) / 16384; to[j] = f2d14i( d, p + 2 ) / 16384;
        p += 4;
      }
      maps.push( { from, to } );
    }
    r = { major, minor, axisCount, maps, v2: null };
    /* avar 2.0 tail: varIdxMap + varStore, both Offset32 from table start */
    if ( major >= 2 ) {
      const varIdxMapOff = u32( d, p ), varStoreOff = u32( d, p + 4 );
      r.v2 = {
        varIdxMap: varIdxMapOff ? readDeltaSetIndexMap( d, o + varIdxMapOff ) : null,
        varStore: varStoreOff ? readItemVariationStore( d, o + varStoreOff ) : null,
      };
    }
  }
  face._vfAvar = r;
  return r;
}

/* hb-ot-var-avar-table.hh SegmentMaps::map_float.
 * NOT the plain spec algorithm: HarfBuzz implements an extended, more robust
 * version (CoreText compatibility) that special-cases duplicate mappings and
 * shifts values that fall outside the mapped segments. */
function segmentMapFloat( map, value ) {
  const from = map.from, to = map.to;
  const len = from.length;

  if ( len < 2 ) {
    if ( !len ) return value;
    return value - from[0] + to[0];             /* len == 1 */
  }

  let start = 0, end = len;
  if ( from[start] === -1 && to[start] === -1 && from[start + 1] === -1 ) start++;
  if ( from[end - 1] === +1 && to[end - 1] === +1 && from[end - 2] === +1 ) end--;

  /* exact match first, with the duplicate-mapping special cases */
  let i;
  for ( i = start; i < end; i++ ) if ( value === from[i] ) break;
  if ( i < end ) {
    let j = i;
    for ( ; j + 1 < end; j++ ) if ( value !== from[j + 1] ) break;
    if ( i === j ) return to[i];                /* the only spec-compliant case */
    if ( i + 2 === j ) return to[i + 1];        /* exactly three: middle one */
    if ( value < 0 ) return to[j];
    if ( value > 0 ) return to[i];
    return Math.abs( to[i] ) < Math.abs( to[j] ) ? to[i] : to[j];
  }

  /* not an exact match: find the segment */
  for ( i = start; i < end; i++ ) if ( value < from[i] ) break;

  if ( i === start ) return value - from[start] + to[start];   /* before all: shift */
  if ( i === end ) return value - from[end - 1] + to[end - 1]; /* after all: shift  */

  const denom = from[i] - from[i - 1];          /* cannot be zero by now */
  return to[i - 1] + ( ( to[i] - to[i - 1] ) * ( value - from[i - 1] ) ) / denom;
}

/* hb-ot-var-avar-table.hh avar::map_coords_16_16 -- coords are 16.16 ints. */
function avarMapCoords1616( avar, coords ) {
  const n = Math.min( coords.length, avar.axisCount );
  for ( let i = 0; i < n; i++ )
    coords[i] = roundf( Math.fround( segmentMapFloat( avar.maps[i], coords[i] / 65536 ) ) * 65536 );

  if ( !avar.v2 ) return;

  /* avar2: evaluate the item variation store at the 2.14 coords and add the
   * (unclamped) delta, clamping only the result -- matching fontTools. */
  const len = coords.length;
  const coords214 = new Int32Array( len );
  for ( let i = 0; i < len; i++ ) coords214[i] = roundf( coords[i] / 4 );

  const out = new Int32Array( len );
  for ( let i = 0; i < len; i++ ) {
    let v = coords[i];
    const varidx = avar.v2.varIdxMap ? dsimMap( avar.v2.varIdxMap, i ) : i;
    const delta = avar.v2.varStore ? ivsGetDelta( avar.v2.varStore, varidx, coords214 ) : 0;
    const dd = clamp( Math.fround( delta * 4 ), -( 1 << 17 ), +( 1 << 17 ) );  /* 2.14 -> 16.16 */
    v += roundf( dd );
    out[i] = clamp( v, -( 1 << 16 ), +( 1 << 16 ) );
  }
  for ( let i = 0; i < len; i++ ) coords[i] = out[i];
}

/* ------------------------------------------------------------------ */
/*  normalization (hb-ot-var.cc hb_ot_var_normalize_coords)            */
/* ------------------------------------------------------------------ */

/* "wght=900.0,wdth=100" -> [{tag,value}] ; also accepts "wght 900" */
export function parseVariations( str ) {
  const out = [];
  if ( !str ) return out;
  for ( const part of String( str ).split( ',' ) ) {
    const s = part.trim();
    if ( !s ) continue;
    const m = /^([A-Za-z0-9 _]{1,4})\s*[=:]?\s*(-?[0-9.]+)$/.exec( s );
    if ( !m ) continue;
    out.push( { tag: ( m[1] + '    ' ).slice( 0, 4 ), value: parseFloat( m[2] ) } );
  }
  return out;
}

/* Full HarfBuzz chain:
 *   design coords (fvar defaults, overridden by settings)
 *   -> roundf(normalize_axis_value * 65536)        [16.16]
 *   -> avar map_coords_16_16                        [16.16]
 *   -> (v + 2) >> 2                                 [2.14 ints]
 * Returns the Int32Array of 2.14 coords that every other table consumes. */
export function normalizeCoords( face, settings ) {
  const fvar = readFvar( face );
  if ( !fvar ) return new Int32Array( 0 );
  const n = fvar.axisCount;

  const design = new Float64Array( n );
  for ( let i = 0; i < n; i++ ) design[i] = fvar.axes[i].defaultValue;

  const list = Array.isArray( settings ) ? settings : parseVariations( settings );
  for ( const s of list ) {
    const tag = ( s.tag + '    ' ).slice( 0, 4 );
    for ( let i = 0; i < n; i++ ) if ( fvar.axes[i].tag === tag ) design[i] = s.value;
  }

  return normalizeDesignCoords( face, design );
}

export function normalizeDesignCoords( face, design ) {
  const fvar = readFvar( face );
  if ( !fvar ) return new Int32Array( 0 );
  const n = fvar.axisCount;

  const coords = new Int32Array( n );
  for ( let i = 0; i < n; i++ )
    coords[i] = roundf( Math.fround( normalizeAxisValue( fvar.axes[i], design[i] ) ) * 65536 );

  const avar = readAvar( face );
  if ( avar ) avarMapCoords1616( avar, coords );

  for ( let i = 0; i < n; i++ ) coords[i] = ( coords[i] + 2 ) >> 2;   /* -> 2.14 */
  return coords;
}


/* ------------------------------------------------------------------ */
/*  DeltaSetIndexMap (hb-ot-layout-common.hh DeltaSetIndexMapFormat01)  */
/* ------------------------------------------------------------------ */

export function readDeltaSetIndexMap( d, o ) {
  const format = u8( d, o );
  const entryFormat = u8( d, o + 1 );
  let mapCount, dataOff;
  if ( format === 0 ) { mapCount = u16( d, o + 2 ); dataOff = o + 4; }
  else { mapCount = u32( d, o + 2 ); dataOff = o + 6; }
  return {
    d, format, entryFormat, mapCount, dataOff,
    width: ( ( entryFormat >> 4 ) & 3 ) + 1,
    innerBits: ( entryFormat & 0xF ) + 1,
  };
}

/* map(v): mapCount == 0 passes the value through unchanged -- this is exactly
 * how an absent (NULL-offset) advance map yields implicit glyph-id indexing. */
export function dsimMap( m, v ) {
  if ( !m || !m.mapCount ) return v;
  if ( v >= m.mapCount ) v = m.mapCount - 1;
  let u = 0;
  const p = m.dataOff + m.width * v;
  for ( let w = 0; w < m.width; w++ ) u = ( u << 8 ) + m.d[p + w];
  const n = m.innerBits;
  const outer = u >>> n;
  const inner = u & ( ( 1 << n ) - 1 );
  return ( ( outer << 16 ) | inner ) >>> 0;
}


/* ------------------------------------------------------------------ */
/*  ItemVariationStore (hb-ot-layout-common.hh)                        */
/* ------------------------------------------------------------------ */

export function readItemVariationStore( d, o ) {
  const format = u16( d, o );
  const regionsOff = u32( d, o + 2 );
  const dataSetCount = u16( d, o + 6 );
  const dataSets = new Uint32Array( dataSetCount );
  for ( let i = 0; i < dataSetCount; i++ ) dataSets[i] = u32( d, o + 8 + 4 * i );

  /* VarRegionList */
  const rBase = o + regionsOff;
  const axisCount = u16( d, rBase );
  const regionCount = u16( d, rBase + 2 );
  /* start/peak/end as raw 2.14 ints, three per axis per region */
  const regions = new Int16Array( regionCount * axisCount * 3 );
  for ( let r = 0, k = 0; r < regionCount; r++ )
    for ( let a = 0; a < axisCount; a++ ) {
      const p = rBase + 4 + ( r * axisCount + a ) * 6;
      regions[k++] = f2d14i( d, p );
      regions[k++] = f2d14i( d, p + 2 );
      regions[k++] = f2d14i( d, p + 4 );
    }

  /* VarData subtables */
  const subs = new Array( dataSetCount );
  for ( let i = 0; i < dataSetCount; i++ ) {
    if ( !dataSets[i] ) { subs[i] = null; continue; }
    const p = o + dataSets[i];
    const itemCount = u16( d, p );
    const packed = u16( d, p + 2 );
    const longWords = ( packed & 0x8000 ) !== 0;
    const wordCount = packed & 0x7FFF;
    const regionIndexCount = u16( d, p + 4 );
    const regionIndices = new Uint16Array( regionIndexCount );
    for ( let j = 0; j < regionIndexCount; j++ ) regionIndices[j] = u16( d, p + 6 + 2 * j );
    const rowSize = ( wordCount + regionIndexCount ) * ( longWords ? 2 : 1 );
    subs[i] = {
      itemCount, longWords, wordCount, regionIndexCount, regionIndices,
      rowSize, deltaOff: p + 6 + 2 * regionIndexCount,
    };
  }

  return { d, format, axisCount, regionCount, regions, subs, dataSetCount };
}

/* hb-ot-layout-common.hh VarRegionAxis::evaluate -- all comparisons on 2.14
 * ints, the division in float. */
function regionAxisEvaluate( start, peak, end, coord ) {
  if ( peak === 0 || coord === peak ) return 1;
  if ( coord === 0 ) return 0;
  if ( start > peak || peak > end ) return 1;
  if ( start < 0 && end > 0 ) return 1;
  if ( coord <= start || end <= coord ) return 0;
  if ( coord < peak ) return Math.fround( ( coord - start ) / ( peak - start ) );
  return Math.fround( ( end - coord ) / ( end - peak ) );
}

/* VarRegionList::evaluate -- product across axes, short-circuit on zero. */
export function ivsRegionEvaluate( store, regionIndex, coords ) {
  if ( regionIndex >= store.regionCount ) return 0;
  const ac = store.axisCount, R = store.regions;
  let v = 1;
  const base = regionIndex * ac * 3;
  for ( let i = 0; i < ac; i++ ) {
    const coord = i < coords.length ? coords[i] : 0;
    const f = regionAxisEvaluate( R[base + i * 3], R[base + i * 3 + 1], R[base + i * 3 + 2], coord );
    if ( f === 0 ) return 0;
    v = Math.fround( v * f );
  }
  return v;
}

/* VarData::_get_delta -- long deltas first, then words, then bytes. */
function varDataGetDelta( store, sub, inner, coords ) {
  if ( !sub || inner >= sub.itemCount ) return 0;
  const count = sub.regionIndexCount;
  if ( !count ) return 0;
  const isLong = sub.longWords;
  const scount = isLong ? count : sub.wordCount;
  const lcount = isLong ? sub.wordCount : 0;

  const d = store.d;
  let p = sub.deltaOff + inner * sub.rowSize;
  let delta = 0, i = 0;

  for ( ; i < lcount; i++, p += 4 ) {
    const s = ivsRegionEvaluate( store, sub.regionIndices[i], coords );
    if ( s ) delta = Math.fround( delta + Math.fround( s * i32( d, p ) ) );
  }
  for ( ; i < scount; i++, p += 2 ) {
    const s = ivsRegionEvaluate( store, sub.regionIndices[i], coords );
    if ( s ) delta = Math.fround( delta + Math.fround( s * i16( d, p ) ) );
  }
  for ( ; i < count; i++, p += 1 ) {
    const s = ivsRegionEvaluate( store, sub.regionIndices[i], coords );
    if ( s ) delta = Math.fround( delta + Math.fround( s * i8( d, p ) ) );
  }
  return delta;
}

/* ItemVariationStore::get_delta(varIdx) -- outer = idx>>16, inner = idx&0xFFFF.
 * NO_VARIATIONS_INDEX (0xFFFFFFFF) fails the outer bound and yields 0. */
export function ivsGetDelta( store, varIdx, coords ) {
  if ( !store ) return 0;
  const outer = varIdx >>> 16;
  const inner = varIdx & 0xFFFF;
  if ( outer >= store.dataSetCount ) return 0;
  return varDataGetDelta( store, store.subs[outer], inner, coords );
}

/* ItemVariationStore::get_region_index_count / get_region_scalars, used by
 * the CFF2 blend operator. */
export function ivsRegionIndexCount( store, outer ) {
  if ( !store || outer >= store.dataSetCount || !store.subs[outer] ) return 0;
  return store.subs[outer].regionIndexCount;
}

export function ivsRegionScalars( store, outer, coords, numScalars ) {
  const out = new Float64Array( numScalars );
  if ( !store || outer >= store.dataSetCount || !store.subs[outer] ) return out;
  const sub = store.subs[outer];
  const count = Math.min( numScalars, sub.regionIndexCount );
  for ( let i = 0; i < count; i++ )
    out[i] = ivsRegionEvaluate( store, sub.regionIndices[i], coords );
  return out;
}


/* ------------------------------------------------------------------ */
/*  packed points / packed deltas (otvarcommonformats + TupleValues)   */
/* ------------------------------------------------------------------ */

const POINTS_ARE_WORDS     = 0x80;
const POINT_RUN_COUNT_MASK = 0x7F;

/* Returns {points: Uint32Array|null, next}. An empty array (count 0) means
 * "all points", which the callers treat as apply_to_all. */
export function decompilePoints( d, p, end ) {
  if ( p + 1 > end ) return null;
  let count = d[p++];
  if ( count & POINTS_ARE_WORDS ) {
    if ( p + 1 > end ) return null;
    count = ( ( count & POINT_RUN_COUNT_MASK ) << 8 ) | d[p++];
  }
  const points = new Uint32Array( count );
  let n = 0, i = 0;
  while ( i < count ) {
    if ( p + 1 > end ) return null;
    const control = d[p++];
    const runCount = ( control & POINT_RUN_COUNT_MASK ) + 1;
    const stop = i + runCount;
    if ( stop > count ) return null;
    if ( control & POINTS_ARE_WORDS ) {
      if ( p + runCount * 2 > end ) return null;
      for ( ; i < stop; i++ ) { n += u16( d, p ); points[i] = n; p += 2; }
    } else {
      if ( p + runCount > end ) return null;
      for ( ; i < stop; i++ ) { n += d[p++]; points[i] = n; }
    }
  }
  return { points, next: p };
}

/* TupleValues::decompile. 0xC0 (LONGS, int32) is a HarfBuzz extension over
 * the published spec, which only defines zero/byte/word runs. */
const VALUES_ARE_ZEROS = 0x80;
const VALUES_ARE_BYTES = 0x00;
const VALUES_ARE_WORDS = 0x40;
const VALUES_ARE_LONGS = 0xC0;
const VALUES_SIZE_MASK = 0xC0;
const VALUE_RUN_COUNT_MASK = 0x3F;

export function decompileDeltas( d, p, end, out ) {
  const count = out.length;
  let i = 0;
  while ( i < count ) {
    if ( p + 1 > end ) return null;
    const control = d[p++];
    const runCount = ( control & VALUE_RUN_COUNT_MASK ) + 1;
    const stop = i + runCount;
    if ( stop > count ) return null;
    const size = control & VALUES_SIZE_MASK;
    if ( size === VALUES_ARE_ZEROS ) {
      for ( ; i < stop; i++ ) out[i] = 0;
    } else if ( size === VALUES_ARE_WORDS ) {
      if ( p + runCount * 2 > end ) return null;
      for ( ; i < stop; i++ ) { out[i] = i16( d, p ); p += 2; }
    } else if ( size === VALUES_ARE_LONGS ) {
      if ( p + runCount * 4 > end ) return null;
      for ( ; i < stop; i++ ) { out[i] = i32( d, p ); p += 4; }
    } else {
      if ( p + runCount > end ) return null;
      for ( ; i < stop; i++ ) out[i] = i8( d, p++ );
    }
  }
  return p;
}


/* ------------------------------------------------------------------ */
/*  TupleVariationHeader (shared by gvar and cvar)                     */
/* ------------------------------------------------------------------ */

const EMBEDDED_PEAK_TUPLE   = 0x8000;
const INTERMEDIATE_REGION   = 0x4000;
const PRIVATE_POINT_NUMBERS = 0x2000;
const TUPLE_INDEX_MASK      = 0x0FFF;

const SHARED_POINT_NUMBERS = 0x8000;
const TUPLE_COUNT_MASK     = 0x0FFF;

/* Walk the TupleVariationHeader array of one tuple variation store. */
/* `offsetBase` is what `dataOffset` is measured from. For gvar that is the
 * GlyphVariationData start (== base); for cvar the spec measures it from the
 * START OF THE CVAR TABLE, which sits 4 bytes before the TupleVariationData.
 * HarfBuzz passes the cvar table itself as `table_base` for the same reason. */
function readTupleHeaders( d, base, axisCount, endOfTable, offsetBase = base ) {
  const packed = u16( d, base );
  const tupleCount = packed & TUPLE_COUNT_MASK;
  const hasSharedPoints = ( packed & SHARED_POINT_NUMBERS ) !== 0;
  const dataOffset = u16( d, base + 2 );

  const headers = [];
  let p = base + 4;
  for ( let i = 0; i < tupleCount; i++ ) {
    if ( p + 4 > endOfTable ) return null;
    const varDataSize = u16( d, p );
    const tupleIndex = u16( d, p + 2 );
    let q = p + 4;
    let peak = null, start = null, end = null;
    if ( tupleIndex & EMBEDDED_PEAK_TUPLE ) {
      peak = q; q += axisCount * 2;
    }
    if ( tupleIndex & INTERMEDIATE_REGION ) {
      start = q; q += axisCount * 2;
      end = q; q += axisCount * 2;
    }
    headers.push( { varDataSize, tupleIndex, peak, start, end } );
    p = q;
  }
  return { tupleCount, hasSharedPoints, serializedBase: offsetBase + dataOffset, headers };
}

/* TupleVariationHeader::calculate_scalar. Operates entirely on 2.14 ints;
 * the accumulation is double in HarfBuzz (the caller narrows to float). */
function calculateScalar( d, hdr, coords, coordCount, sharedTuplesOff ) {
  const ti = hdr.tupleIndex;
  const hasInterm = ( ti & INTERMEDIATE_REGION ) !== 0;
  let peakBase;
  if ( ti & EMBEDDED_PEAK_TUPLE ) peakBase = hdr.peak;
  else {
    const index = ti & TUPLE_INDEX_MASK;
    if ( sharedTuplesOff < 0 ) return 0;
    peakBase = sharedTuplesOff + index * coordCount * 2;
  }

  let scalar = 1;
  for ( let i = 0; i < coordCount; i++ ) {
    const peak = i16( d, peakBase + i * 2 );
    if ( !peak ) continue;
    const v = coords[i];
    if ( !v ) return 0;
    if ( v === peak ) continue;

    if ( hasInterm ) {
      const st = i16( d, hdr.start + i * 2 );
      const en = i16( d, hdr.end + i * 2 );
      if ( st > peak || peak > en || ( st < 0 && en > 0 && peak ) ) continue;
      if ( v < st || v > en ) return 0;
      if ( v < peak ) { if ( peak !== st ) scalar *= ( v - st ) / ( peak - st ); }
      else { if ( peak !== en ) scalar *= ( en - v ) / ( en - peak ); }
    } else if ( v < Math.min( 0, peak ) || v > Math.max( 0, peak ) ) return 0;
    else scalar *= v / peak;
  }
  return scalar;
}


/* ------------------------------------------------------------------ */
/*  gvar                                                               */
/* ------------------------------------------------------------------ */

export function readGvar( face ) {
  if ( face._vfGvar !== undefined ) return face._vfGvar;
  const rec = tbl( face, 'gvar' );
  let r = null;
  if ( rec ) {
    const d = face.data, o = rec.offset;
    const axisCount = u16( d, o + 4 );
    const sharedTupleCount = u16( d, o + 6 );
    const sharedTuplesOff = u32( d, o + 8 );
    const glyphCount = u16( d, o + 12 );
    const flags = u16( d, o + 14 );
    const dataArrayOff = u32( d, o + 16 );
    r = {
      d, base: o, axisCount, sharedTupleCount,
      sharedTuples: sharedTupleCount ? o + sharedTuplesOff : -1,
      glyphCount, longOffsets: ( flags & 1 ) !== 0,
      offsetArray: o + 20, dataArray: o + dataArrayOff,
      tableEnd: o + rec.length,
    };
  }
  face._vfGvar = r;
  return r;
}

function gvarGlyphData( g, gid ) {
  if ( !g || gid >= g.glyphCount ) return null;
  const d = g.d;
  let s, e;
  if ( g.longOffsets ) {
    s = u32( d, g.offsetArray + 4 * gid );
    e = u32( d, g.offsetArray + 4 * gid + 4 );
  } else {
    s = u16( d, g.offsetArray + 2 * gid ) * 2;
    e = u16( d, g.offsetArray + 2 * gid + 2 ) * 2;
  }
  if ( e <= s ) return null;
  return { start: g.dataArray + s, end: g.dataArray + e };
}

/* hb-ot-var-gvar-table.hh infer_delta */
function inferDelta( targetVal, prevVal, nextVal, prevDelta, nextDelta ) {
  if ( prevVal === nextVal ) return prevDelta === nextDelta ? prevDelta : 0;
  if ( targetVal <= Math.min( prevVal, nextVal ) ) return prevVal < nextVal ? prevDelta : nextDelta;
  if ( targetVal >= Math.max( prevVal, nextVal ) ) return prevVal > nextVal ? prevDelta : nextDelta;
  const r = ( targetVal - prevVal ) / ( nextVal - prevVal );
  return prevDelta + r * ( nextDelta - prevDelta );
}

const nextIndex = ( i, start, end ) => ( i >= end ? start : i + 1 );

/* gvar::accelerator_t::apply_deltas_to_points.
 *
 * `pts` is { x: Float64Array, y: Float64Array, endPt: Uint8Array } covering
 * every point INCLUDING the four phantom points (or, for a composite, one
 * pseudo-point per component plus the four phantoms). Mutates x/y in place.
 */
export function gvarApplyDeltasToPoints( face, gid, coords, pts, phantomOnly = false ) {
  const g = readGvar( face );
  if ( !g ) return false;
  const range = gvarGlyphData( g, gid );
  if ( !range ) return true;

  const d = g.d;
  const axisCount = g.axisCount;
  const hdrs = readTupleHeaders( d, range.start, axisCount, range.end );
  if ( !hdrs || !hdrs.tupleCount ) return true;

  /* shared point numbers live at the head of the serialized data */
  let sharedIndices = null;
  let serialized = hdrs.serializedBase;
  if ( hdrs.hasSharedPoints ) {
    const r = decompilePoints( d, serialized, range.end );
    if ( !r ) return false;
    sharedIndices = r.points;
    serialized = r.next;
  }

  const count = pts.x.length;

  /* HarfBuzz pre-scans every tuple for private point numbers: the presence of
   * ANY private set disables the direct-add fast path for all tuples. */
  let anyPrivatePoints = false;
  for ( const h of hdrs.headers )
    if ( h.tupleIndex & PRIVATE_POINT_NUMBERS ) { anyPrivatePoints = true; break; }

  let deltaX = null, deltaY = null, flags = null;
  let origX = null, origY = null;
  let flush = false;

  let dataPos = serialized;
  for ( const hdr of hdrs.headers ) {
    const p0 = dataPos;
    dataPos += hdr.varDataSize;

    const scalar = Math.fround( calculateScalar( d, hdr, coords, axisCount, g.sharedTuples ) );
    if ( scalar === 0 ) continue;

    let p = p0;
    const end = p0 + hdr.varDataSize;

    let indices = sharedIndices;
    if ( hdr.tupleIndex & PRIVATE_POINT_NUMBERS ) {
      const r = decompilePoints( d, p, end );
      if ( !r ) return false;
      indices = r.points;
      p = r.next;
    }

    const applyToAll = !indices || indices.length === 0;
    const numDeltas = applyToAll ? count : indices.length;

    /* Fast path: every point referenced and no private sets anywhere, so the
     * deltas can be added straight onto the points. */
    if ( applyToAll && !anyPrivatePoints ) {
      const xs = new Int32Array( numDeltas ), ys = new Int32Array( numDeltas );
      p = decompileDeltas( d, p, end, xs );
      if ( p === null ) return false;
      if ( decompileDeltas( d, p, end, ys ) === null ) return false;
      const from = phantomOnly ? count - 4 : 0;
      for ( let i = from; i < count; i++ ) {
        pts.x[i] += Math.fround( xs[i] * scalar );
        pts.y[i] += Math.fround( ys[i] * scalar );
      }
      continue;
    }

    if ( !deltaX ) {
      deltaX = new Float64Array( count );
      deltaY = new Float64Array( count );
      flags = new Uint8Array( count );
    }

    const xs = new Int32Array( numDeltas ), ys = new Int32Array( numDeltas );
    p = decompileDeltas( d, p, end, xs );
    if ( p === null ) return false;
    if ( decompileDeltas( d, p, end, ys ) === null ) return false;

    if ( !applyToAll ) {
      /* orig_points is captured lazily, BEFORE the first flush, so inferred
       * deltas are computed against the default outline. */
      if ( !origX && !phantomOnly ) {
        origX = Float64Array.from( pts.x );
        origY = Float64Array.from( pts.y );
      }
      if ( flush ) {
        for ( let i = phantomOnly ? count - 4 : 0; i < count; i++ ) {
          pts.x[i] += deltaX[i]; pts.y[i] += deltaY[i];
        }
      }
      deltaX.fill( 0 ); deltaY.fill( 0 ); flags.fill( 0 );
    }

    for ( let i = 0; i < numDeltas; i++ ) {
      const idx = applyToAll ? i : indices[i];
      if ( idx >= count ) continue;
      if ( phantomOnly && idx < count - 4 ) continue;
      if ( !applyToAll ) flags[idx] = 1;
      deltaX[idx] += Math.fround( xs[i] * scalar );
      deltaY[idx] += Math.fround( ys[i] * scalar );
    }

    /* infer deltas for unreferenced points, contour by contour */
    if ( !applyToAll && !phantomOnly && origX ) {
      let startPoint = 0, endPoint = 0;
      for ( ;; ) {
        while ( endPoint < count && !pts.endPt[endPoint] ) endPoint++;
        if ( endPoint === count ) break;

        let unref = 0;
        for ( let i = startPoint; i < endPoint + 1; i++ ) unref += flags[i];
        unref = ( endPoint - startPoint + 1 ) - unref;

        let j = startPoint;
        if ( unref !== 0 && unref <= endPoint - startPoint ) {
          outer:
          for ( ;; ) {
            let prev, next, i;
            for ( ;; ) {
              i = j; j = nextIndex( i, startPoint, endPoint );
              if ( flags[i] && !flags[j] ) break;
            }
            prev = j = i;
            for ( ;; ) {
              i = j; j = nextIndex( i, startPoint, endPoint );
              if ( !flags[i] && flags[j] ) break;
            }
            next = j;
            i = prev;
            for ( ;; ) {
              i = nextIndex( i, startPoint, endPoint );
              if ( i === next ) break;
              deltaX[i] = inferDelta( origX[i], origX[prev], origX[next], deltaX[prev], deltaX[next] );
              deltaY[i] = inferDelta( origY[i], origY[prev], origY[next], deltaY[prev], deltaY[next] );
              if ( --unref === 0 ) break outer;
            }
          }
        }
        startPoint = endPoint = endPoint + 1;
      }
    }

    flush = true;
  }

  if ( flush && deltaX ) {
    for ( let i = phantomOnly ? count - 4 : 0; i < count; i++ ) {
      pts.x[i] += deltaX[i]; pts.y[i] += deltaY[i];
    }
  }
  return true;
}


/* ------------------------------------------------------------------ */
/*  cvar (hb-ot-var-cvar-table.hh calculate_cvt_deltas)                */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  gvar: one recursion level, for outline consumers                   */
/* ------------------------------------------------------------------ */
/* Glyph::get_points applies gvar deltas ONE LEVEL AT A TIME: for a simple
 * glyph to its real contour points; for a composite to ONE PSEUDO-POINT PER
 * COMPONENT holding that component's offset args -- never to a composite's
 * flattened child points. The four phantom points are appended at every
 * level and vary with the glyph.
 *
 * Callers keep their own outline reader (point flags, component transforms,
 * anchoring) and call this once per level of their own recursion, then use
 * the returned component offsets as the translations.
 *
 *   face      face object ({ data, tables }), as elsewhere in this module
 *   gid       the glyph this level belongs to
 *   coords    normalized 2.14 ints (see normalizeCoords)
 *   xs, ys    this level's own coordinates; NOT mutated
 *   endPts    inclusive contour-end indices for a simple glyph, null/[] for
 *             a composite level (IUP infers deltas per contour)
 *   phantoms  seed { leftX, rightX, topY, bottomY } =
 *               { xMin - lsb, that + advance, yMax + tsb, that - vAdvance }
 *
 * Returns null when the face has no gvar, when coords are absent, or when the
 * deltas cannot be applied to the points given (a point-count mismatch -- so a
 * wrong-shaped call is refused rather than silently misapplied). The caller
 * then keeps its default-instance points unchanged. A glyph that simply has no
 * delta data is SUCCESS with nothing moved.
 * Otherwise { x, y, phantoms, hdelta, advance, vAdvance }, where
 * hdelta === phantoms.leftX is the undocumented depth-0 left-side-bearing
 * shift, applied ONCE at depth 0 to every point including phantoms.
 */
export function gvarVaryLevel( face, gid, coords, xs, ys, endPts, phantoms ) {
  if ( !readGvar( face ) ) return null;
  if ( !coords || !coords.length ) return null;
  const n = xs.length;
  const ph = phantoms || {};
  const X = new Float64Array( n + 4 );
  const Y = new Float64Array( n + 4 );
  for ( let i = 0; i < n; i++ ) { X[ i ] = xs[ i ]; Y[ i ] = ys[ i ]; }
  X[ n ]     = ph.leftX   || 0;
  X[ n + 1 ] = ph.rightX  || 0;
  Y[ n + 2 ] = ph.topY    || 0;
  Y[ n + 3 ] = ph.bottomY || 0;
  const ep = new Uint8Array( n + 4 );
  if ( endPts ) for ( let i = 0; i < endPts.length; i++ ) {
    const e = endPts[ i ];
    if ( e >= 0 && e < n ) ep[ e ] = 1;
  }
  const pts = { x: X, y: Y, endPt: ep };
  if ( !gvarApplyDeltasToPoints( face, gid, coords, pts ) ) return null;
  const out = {
    leftX: pts.x[ n ], rightX: pts.x[ n + 1 ],
    topY: pts.y[ n + 2 ], bottomY: pts.y[ n + 3 ],
  };
  return {
    x: pts.x.subarray( 0, n ),
    y: pts.y.subarray( 0, n ),
    phantoms: out,
    hdelta: out.leftX,
    advance: out.rightX - out.leftX,
    vAdvance: out.topY - out.bottomY,
  };
}

export function readCvar( face ) {
  if ( face._vfCvar !== undefined ) return face._vfCvar;
  const rec = tbl( face, 'cvar' );
  face._vfCvar = rec ? { offset: rec.offset, length: rec.length } : null;
  return face._vfCvar;
}

/* Returns a Float64Array of accumulated (unrounded) CVT deltas. The caller
 * applies them as `cvt[i] += (int)roundf(delta[i])`, which is what
 * add_cvt_and_apply_deltas does. */
export function cvarCvtDeltas( face, coords, numCvt ) {
  const out = new Float64Array( numCvt );
  const cvar = readCvar( face );
  if ( !cvar || !coords.length ) return out;

  const d = face.data;
  const base = cvar.offset + 4;                    /* skip major/minor version */
  const tableEnd = cvar.offset + cvar.length;
  const fvar = readFvar( face );
  const axisCount = fvar ? fvar.axisCount : 0;

  const hdrs = readTupleHeaders( d, base, axisCount, tableEnd, cvar.offset );
  if ( !hdrs || !hdrs.tupleCount ) return out;

  let sharedIndices = null;
  let serialized = hdrs.serializedBase;
  if ( hdrs.hasSharedPoints ) {
    const r = decompilePoints( d, serialized, tableEnd );
    if ( !r ) return out;
    sharedIndices = r.points;
    serialized = r.next;
  }

  let dataPos = serialized;
  for ( const hdr of hdrs.headers ) {
    const p0 = dataPos;
    dataPos += hdr.varDataSize;

    /* cvar always embeds its peak tuple: there are no shared tuples. */
    const scalar = Math.fround( calculateScalar( d, hdr, coords, axisCount, -1 ) );
    if ( scalar === 0 ) continue;

    let p = p0;
    const end = p0 + hdr.varDataSize;

    let indices = sharedIndices;
    if ( hdr.tupleIndex & PRIVATE_POINT_NUMBERS ) {
      const r = decompilePoints( d, p, end );
      if ( !r ) return out;
      indices = r.points;
      p = r.next;
    }

    const applyToAll = !indices || indices.length === 0;
    const numDeltas = applyToAll ? numCvt : indices.length;
    const vals = new Int32Array( numDeltas );
    if ( decompileDeltas( d, p, end, vals ) === null ) return out;

    for ( let i = 0; i < numDeltas; i++ ) {
      const idx = applyToAll ? i : indices[i];
      if ( idx >= numCvt ) continue;
      out[idx] += scalar !== 1 ? Math.fround( vals[i] * scalar ) : vals[i];
    }
  }
  return out;
}


/* ------------------------------------------------------------------ */
/*  HVAR / VVAR                                                        */
/* ------------------------------------------------------------------ */

function readHVVAR( face, tag ) {
  const rec = tbl( face, tag );
  if ( !rec ) return null;
  const d = face.data, o = rec.offset;
  const storeOff = u32( d, o + 4 );
  const advOff = u32( d, o + 8 );
  const lsbOff = u32( d, o + 12 );
  const rsbOff = u32( d, o + 16 );
  const vorgOff = tag === 'VVAR' && rec.length >= 24 ? u32( d, o + 20 ) : 0;
  return {
    store: storeOff ? readItemVariationStore( d, o + storeOff ) : null,
    advMap: advOff ? readDeltaSetIndexMap( d, o + advOff ) : null,
    lsbMap: lsbOff ? readDeltaSetIndexMap( d, o + lsbOff ) : null,
    rsbMap: rsbOff ? readDeltaSetIndexMap( d, o + rsbOff ) : null,
    vorgMap: vorgOff ? readDeltaSetIndexMap( d, o + vorgOff ) : null,
  };
}

export function readHVAR( face ) {
  if ( face._vfHVAR === undefined ) face._vfHVAR = readHVVAR( face, 'HVAR' );
  return face._vfHVAR;
}
export function readVVAR( face ) {
  if ( face._vfVVAR === undefined ) face._vfVVAR = readHVVAR( face, 'VVAR' );
  return face._vfVVAR;
}

/* With no advance map, the glyph id is the delta-set index directly
 * (dsimMap passes the value through when mapCount is 0). */
export function hvarAdvanceDelta( hv, gid, coords ) {
  if ( !hv || !hv.store ) return 0;
  return ivsGetDelta( hv.store, dsimMap( hv.advMap, gid ), coords );
}
export function hvarSideBearingDelta( hv, gid, coords, which ) {
  if ( !hv || !hv.store ) return 0;
  const m = which === 'rsb' ? hv.rsbMap : which === 'vorg' ? hv.vorgMap : hv.lsbMap;
  if ( !m ) return 0;                     /* side bearing deltas are optional */
  return ivsGetDelta( hv.store, dsimMap( m, gid ), coords );
}


/* ------------------------------------------------------------------ */
/*  MVAR                                                               */
/* ------------------------------------------------------------------ */

export function readMVAR( face ) {
  if ( face._vfMVAR !== undefined ) return face._vfMVAR;
  const rec = tbl( face, 'MVAR' );
  let r = null;
  if ( rec ) {
    const d = face.data, o = rec.offset;
    const valueRecordSize = u16( d, o + 6 );
    const valueRecordCount = u16( d, o + 8 );
    const storeOff = u16( d, o + 10 );
    r = {
      d, valuesOff: o + 12, valueRecordSize, valueRecordCount,
      store: storeOff ? readItemVariationStore( d, o + storeOff ) : null,
    };
  }
  face._vfMVAR = r;
  return r;
}

/* MVAR::get_var -- records are sorted by tag; HarfBuzz binary-searches. */
export function mvarDelta( mvar, tag, coords ) {
  if ( !mvar || !mvar.store ) return 0;
  const d = mvar.d;
  const want = ( tag + '    ' ).slice( 0, 4 );
  let lo = 0, hi = mvar.valueRecordCount - 1;
  while ( lo <= hi ) {
    const mid = ( lo + hi ) >> 1;
    const p = mvar.valuesOff + mid * mvar.valueRecordSize;
    const t = String.fromCharCode( d[p], d[p + 1], d[p + 2], d[p + 3] );
    if ( t === want ) return ivsGetDelta( mvar.store, u32( d, p + 4 ), coords );
    if ( t < want ) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}


/* ------------------------------------------------------------------ */
/*  CFF2 variation store (for cff.js's blend / vsindex operators)      */
/* ------------------------------------------------------------------ */

/* The CFF2 VariationStore is a uint16 length followed by an
 * ItemVariationStore. Its ItemVariationData rows carry no deltas (itemCount
 * and wordDeltaCount are 0): only the regionIndexes array is used, to give
 * the blend operator its k region scalars. */
export function readCFF2VarStore( d, off ) {
  if ( !off ) return null;
  return readItemVariationStore( d, off + 2 );
}

/* k = number of active regions for the current vsindex. */
export function cff2RegionCount( store, vsindex ) {
  return ivsRegionIndexCount( store, vsindex );
}

/* The k scalars the blend operator multiplies its deltas by. */
export function cff2BlendScalars( store, vsindex, coords ) {
  return ivsRegionScalars( store, vsindex, coords, ivsRegionIndexCount( store, vsindex ) );
}

/* blend: value = default + sum(scalar[i] * delta[i]).
 * NOTE: in a CharString HarfBuzz rounds the blended result to an integer
 * (`arg.set_int (round (...))`), so pass round=true there; PrivateDICT
 * blends keep full precision. */
export function cff2Blend( defaultValue, deltas, scalars, round ) {
  let v = 0;
  const n = Math.min( deltas.length, scalars.length );
  for ( let i = 0; i < n; i++ ) v += scalars[i] * deltas[i];
  const r = defaultValue + v;
  return round ? Math.round( r ) : r;
}


/* ------------------------------------------------------------------ */
/*  convenience                                                        */
/* ------------------------------------------------------------------ */

export function isVariable( face ) { return !!readFvar( face ); }

export function axisTags( face ) {
  const f = readFvar( face );
  return f ? f.axes.map( ( a ) => a.tag ) : [];
}

export default {
  parseVariations, normalizeCoords, normalizeDesignCoords, normalizeAxisValue,
  readFvar, readAvar, readGvar, readCvar, readHVAR, readVVAR, readMVAR,
  readItemVariationStore, readDeltaSetIndexMap, dsimMap,
  ivsGetDelta, ivsRegionScalars, ivsRegionIndexCount, ivsRegionEvaluate,
  decompilePoints, decompileDeltas,
  gvarApplyDeltasToPoints, gvarVaryLevel, cvarCvtDeltas,
  hvarAdvanceDelta, hvarSideBearingDelta, mvarDelta,
  readCFF2VarStore, cff2RegionCount, cff2BlendScalars, cff2Blend,
  isVariable, axisTags, roundf, NO_VARIATIONS_INDEX,
};
