/* sfnt.js -- the SFNT reader: ../freetype/src/sfnt/{sfobjs.c,ttload.c,ttmtx.c}
 * and ../freetype/src/truetype/ttpload.c.
 *
 * C function -> JS function map
 *   sfnt_open_font                -> openFont      (ttcf collections)
 *   tt_face_load_font_dir         -> loadFontDir   (+ check_table_dir rules)
 *   tt_face_load_generic_header   -> loadHead
 *   tt_face_load_maxp             -> loadMaxp
 *   tt_face_load_hhea (ttmtx.c)   -> loadHhea
 *   tt_face_load_os2              -> loadOs2
 *   tt_face_load_name             -> loadName
 *   tt_face_get_name (sfobjs.c)   -> ttFaceGetName
 *   tt_name_ascii_from_utf16/other-> nameFromUtf16 / nameFromOther
 *   the GET_NAME block of sfnt_load_face -> pickNames
 *   tt_face_load_loca / tt_face_get_location (ttpload.c) -> loadLoca / face.loca
 *   tt_face_get_metrics (ttmtx.c) -> face.hmtx / face.vmtx
 *   the "Set up metrics" block of sfnt_load_face -> rootMetrics
 */

/* tttags.h */
const TTAG = ( s ) => ( s.charCodeAt( 0 ) << 24 >>> 0 ) + ( s.charCodeAt( 1 ) << 16 ) +
                      ( s.charCodeAt( 2 ) << 8 ) + s.charCodeAt( 3 );
const TTAG_ttcf = TTAG( 'ttcf' );
const TTAG_OTTO = TTAG( 'OTTO' );
const TTAG_true = TTAG( 'true' );
const TTAG_typ1 = TTAG( 'typ1' );
/* Mac-only sfnt wrappers that sfnt_open_font also accepts */
const TTAG_0xA5kbd = 0xA56B6264;
const TTAG_0xA5lst = 0xA56C7374;

/* ttnameid.h */
const TT_PLATFORM_APPLE_UNICODE = 0;
const TT_PLATFORM_MACINTOSH     = 1;
const TT_PLATFORM_ISO           = 2;
const TT_PLATFORM_MICROSOFT     = 3;

const TT_MAC_ID_ROMAN           = 0;
const TT_MAC_LANGID_ENGLISH     = 0;

const TT_MS_ID_SYMBOL_CS        = 0;
const TT_MS_ID_UNICODE_CS       = 1;
const TT_MS_ID_UCS_4            = 10;

const TT_NAME_ID_FONT_FAMILY           = 1;
const TT_NAME_ID_FONT_SUBFAMILY        = 2;
const TT_NAME_ID_TYPOGRAPHIC_FAMILY    = 16;
const TT_NAME_ID_TYPOGRAPHIC_SUBFAMILY = 17;
const TT_NAME_ID_WWS_FAMILY            = 21;
const TT_NAME_ID_WWS_SUBFAMILY         = 22;

/* big-endian readers */
const rU8  = ( d, p ) => d[p];
const rU16 = ( d, p ) => ( d[p] << 8 ) | d[p + 1];
const rS16 = ( d, p ) => ( ( ( d[p] << 8 ) | d[p + 1] ) << 16 ) >> 16;
const rU32 = ( d, p ) => ( ( d[p] << 24 ) | ( d[p + 1] << 16 ) |
                           ( d[p + 2] << 8 ) | d[p + 3] ) >>> 0;
const rS32 = ( d, p ) => ( ( d[p] << 24 ) | ( d[p + 1] << 16 ) |
                           ( d[p + 2] << 8 ) | d[p + 3] ) | 0;

class SfntError extends Error {
  constructor( code, msg ) { super( msg || ( 'sfnt error 0x' + code.toString( 16 ) ) ); this.code = code; }
}

/* ======================================================================
 *  sfnt_open_font: TTC container handling
 * ==================================================================== */
function openFont( d ) {
  if ( d.length < 4 )
    throw new SfntError( 0x02 /* Unknown_File_Format */ );

  const tag = rU32( d, 0 );

  if ( tag !== 0x00010000 && tag !== TTAG_ttcf && tag !== TTAG_OTTO &&
       tag !== TTAG_true  && tag !== TTAG_typ1 && tag !== 0x00020000 &&
       tag !== TTAG_0xA5kbd && tag !== TTAG_0xA5lst )
    throw new SfntError( 0x02 );

  if ( tag === TTAG_ttcf ) {
    const count = rS32( d, 8 );                 /* FT_FRAME_LONG( count ) */
    if ( count === 0 )
      throw new SfntError( 0x08 /* Invalid_Table */ );
    if ( count > Math.floor( d.length / ( 28 + 4 ) ) )
      throw new SfntError( 0x0B /* Array_Too_Large */ );

    const offsets = [];
    for ( let n = 0; n < count; n++ )
      offsets.push( rU32( d, 12 + 4 * n ) );
    return { count, offsets };
  }

  /* not a collection: a single "subfont" at offset 0 */
  return { count: 1, offsets: [ 0 ] };
}

/* ======================================================================
 *  tt_face_load_font_dir (+ check_table_dir's sanitising rules)
 * ==================================================================== */
function loadFontDir( d, base ) {
  const format_tag = rU32( d, base );
  const num_tables = rU16( d, base + 4 );

  const tables = new Map();
  let valid_entries = 0;

  for ( let nn = 0; nn < num_tables; nn++ ) {
    const o = base + 12 + 16 * nn;
    if ( o + 16 > d.length ) break;            /* "can read only N tables" */

    const tagN   = rU32( d, o );
    let   offset = rU32( d, o + 8 );
    let   length = rU32( d, o + 12 );
    const tag    = String.fromCharCode( d[o], d[o + 1], d[o + 2], d[o + 3] );

    /* ignore invalid tables that can't be sanitized */
    if ( offset > d.length )
      continue;
    if ( length > d.length - offset ) {
      if ( tag === 'hmtx' || tag === 'vmtx' )
        length = ( d.length - offset ) & ~3;   /* multiple of 4 */
      else
        continue;
    }

    /* ignore duplicate tables -- the first one wins */
    if ( tables.has( tag ) )
      continue;

    tables.set( tag, { offset, length, tag: tagN } );
    valid_entries++;
  }

  if ( !valid_entries )
    throw new SfntError( 0x02 /* Unknown_File_Format */ );

  return { format_tag, tables };
}

/* ======================================================================
 *  head / maxp / hhea / OS/2
 * ==================================================================== */
function loadHead( d, t ) {
  const p = t.offset;
  return {
    Table_Version      : rU32( d, p ),
    Font_Revision      : rU32( d, p + 4 ),
    CheckSum_Adjust    : rS32( d, p + 8 ),
    Magic_Number       : rS32( d, p + 12 ),
    Flags              : rU16( d, p + 16 ),
    Units_Per_EM       : rU16( d, p + 18 ),
    xMin               : rS16( d, p + 36 ),
    yMin               : rS16( d, p + 38 ),
    xMax               : rS16( d, p + 40 ),
    yMax               : rS16( d, p + 42 ),
    Mac_Style          : rU16( d, p + 44 ),
    Lowest_Rec_PPEM    : rU16( d, p + 46 ),
    Font_Direction     : rS16( d, p + 48 ),
    Index_To_Loc_Format: rS16( d, p + 50 ),
    Glyph_Data_Format  : rS16( d, p + 52 ),
  };
}

function loadMaxp( d, t ) {
  const p = t.offset;
  const version   = rS32( d, p );
  const numGlyphs = rU16( d, p + 4 );

  const m = {
    version, numGlyphs,
    maxPoints: 0, maxContours: 0, maxCompositePoints: 0,
    maxCompositeContours: 0, maxZones: 0, maxTwilightPoints: 0,
    maxStorage: 0, maxFunctionDefs: 0, maxInstructionDefs: 0,
    maxStackElements: 0, maxSizeOfInstructions: 0,
    maxComponentElements: 0, maxComponentDepth: 0,
  };

  if ( version >= 0x10000 && t.length >= 32 ) {
    m.maxPoints             = rU16( d, p + 6 );
    m.maxContours           = rU16( d, p + 8 );
    m.maxCompositePoints    = rU16( d, p + 10 );
    m.maxCompositeContours  = rU16( d, p + 12 );
    m.maxZones              = rU16( d, p + 14 );
    m.maxTwilightPoints     = rU16( d, p + 16 );
    m.maxStorage            = rU16( d, p + 18 );
    m.maxFunctionDefs       = rU16( d, p + 20 );
    m.maxInstructionDefs    = rU16( d, p + 22 );
    m.maxStackElements      = rU16( d, p + 24 );
    m.maxSizeOfInstructions = rU16( d, p + 26 );
    m.maxComponentElements  = rU16( d, p + 28 );
    m.maxComponentDepth     = rU16( d, p + 30 );

    if ( m.maxFunctionDefs < 64 ) m.maxFunctionDefs = 64;
    if ( m.maxTwilightPoints > 0xFFFF - 4 ) m.maxTwilightPoints = 0xFFFF - 4;
  }
  return m;
}

/* A TrueType-tagged face the truetype driver refuses outright -- one with no
 * `head' -- never reaches sfnt_load_face, so `head' and `maxp' may BOTH be
 * absent while the face is still built for shaping consumers (see `headless'
 * in openFace).  HarfBuzz shapes such a face at a 1000-unit em: `get_upem()'
 * in hb-ot-head-table.hh answers 1000 unless 16 <= unitsPerEM <= 16384. */
function defaultHead() {
  return {
    Table_Version      : 0,
    Font_Revision      : 0,
    CheckSum_Adjust    : 0,
    Magic_Number       : 0,
    Flags              : 0,
    Units_Per_EM       : 1000,
    xMin               : 0,
    yMin               : 0,
    xMax               : 0,
    yMax               : 0,
    Mac_Style          : 0,
    Lowest_Rec_PPEM    : 0,
    Font_Direction     : 0,
    Index_To_Loc_Format: 0,
    Glyph_Data_Format  : 0,
  };
}

function defaultMaxp() {
  return {
    version: 0, numGlyphs: 0,
    maxPoints: 0, maxContours: 0, maxCompositePoints: 0,
    maxCompositeContours: 0, maxZones: 0, maxTwilightPoints: 0,
    maxStorage: 0, maxFunctionDefs: 0, maxInstructionDefs: 0,
    maxStackElements: 0, maxSizeOfInstructions: 0,
    maxComponentElements: 0, maxComponentDepth: 0,
  };
}

function loadHhea( d, t ) {
  const p = t.offset;
  return {
    Version               : rU32( d, p ),
    Ascender              : rS16( d, p + 4 ),
    Descender             : rS16( d, p + 6 ),
    Line_Gap              : rS16( d, p + 8 ),
    advance_Width_Max     : rU16( d, p + 10 ),
    min_Left_Side_Bearing : rS16( d, p + 12 ),
    min_Right_Side_Bearing: rS16( d, p + 14 ),
    xMax_Extent           : rS16( d, p + 16 ),
    caret_Slope_Rise      : rS16( d, p + 18 ),
    caret_Slope_Run       : rS16( d, p + 20 ),
    caret_Offset          : rS16( d, p + 22 ),
    metric_Data_Format    : rS16( d, p + 32 ),
    number_Of_HMetrics    : rU16( d, p + 34 ),
  };
}

function loadOs2( d, t ) {
  /* FreeType reads a 78-byte frame; any short read means "table missing". */
  if ( !t || t.length < 78 || t.offset + 78 > d.length )
    return { version: 0xFFFF };
  const p = t.offset;
  return {
    version          : rU16( d, p ),
    xAvgCharWidth    : rS16( d, p + 2 ),
    usWeightClass    : rU16( d, p + 4 ),
    usWidthClass     : rU16( d, p + 6 ),
    fsType           : rS16( d, p + 8 ),
    yStrikeoutSize   : rS16( d, p + 26 ),
    yStrikeoutPos    : rS16( d, p + 28 ),
    sFamilyClass     : rS16( d, p + 30 ),
    fsSelection      : rU16( d, p + 62 ),
    usFirstCharIndex : rU16( d, p + 64 ),
    usLastCharIndex  : rU16( d, p + 66 ),
    sTypoAscender    : rS16( d, p + 68 ),
    sTypoDescender   : rS16( d, p + 70 ),
    sTypoLineGap     : rS16( d, p + 72 ),
    usWinAscent      : rU16( d, p + 74 ),
    usWinDescent     : rU16( d, p + 76 ),
  };
}

/* ======================================================================
 *  tt_face_load_name
 * ==================================================================== */
function loadName( d, t ) {
  if ( !t ) return { names: [], numNameRecords: 0 };

  const table_pos = t.offset;
  const table_len = t.length;

  if ( table_pos + 6 > d.length ) return { names: [], numNameRecords: 0 };

  const format         = rU16( d, table_pos );
  const numNameRecords = rU16( d, table_pos + 2 );
  const storageOffset  = rU16( d, table_pos + 4 );

  let storage_start = table_pos + 6 + 12 * numNameRecords;
  const storage_limit = table_pos + table_len;

  if ( storage_start > storage_limit )
    return { names: [], numNameRecords: 0 };   /* Name_Table_Missing */

  let numLangTagRecords = 0;
  const langTags = [];

  if ( format === 1 ) {
    if ( storage_start + 2 > d.length )
      return { names: [], numNameRecords: 0 };
    numLangTagRecords = rU16( d, storage_start );
    const langBase = storage_start + 2;
    storage_start += 2 + 4 * numLangTagRecords;

    for ( let i = 0; i < numLangTagRecords; i++ ) {
      const o = langBase + 4 * i;
      if ( o + 4 > d.length ) { langTags.push( { stringLength: 0 } ); continue; }
      let stringLength = rU16( d, o );
      let stringOffset = rU16( d, o + 2 ) + table_pos + storageOffset;
      if ( stringOffset < storage_start || stringOffset + stringLength > storage_limit )
        stringLength = 0;
      langTags.push( { stringLength, stringOffset } );
    }
  }

  const names = [];
  for ( let i = 0; i < numNameRecords; i++ ) {
    const o = table_pos + 6 + 12 * i;
    if ( o + 12 > d.length ) continue;

    const platformID   = rU16( d, o );
    const encodingID   = rU16( d, o + 2 );
    const languageID   = rU16( d, o + 4 );
    const nameID       = rU16( d, o + 6 );
    const stringLength = rU16( d, o + 8 );
    const stringOffset = rU16( d, o + 10 ) + table_pos + storageOffset;

    if ( stringLength === 0 ) continue;
    if ( stringOffset < storage_start || stringOffset + stringLength > storage_limit )
      continue;
    if ( format === 1 && languageID >= 0x8000 ) {
      if ( languageID - 0x8000 >= numLangTagRecords ||
           !langTags[languageID - 0x8000].stringLength )
        continue;
    }

    names.push( { platformID, encodingID, languageID, nameID,
                  stringLength, stringOffset } );
  }

  return { format, names, numNameRecords: names.length };
}

/* sfobjs.c: tt_name_ascii_from_utf16 */
function nameFromUtf16( d, rec ) {
  const len = Math.floor( rec.stringLength / 2 );
  let s = '';
  for ( let n = 0; n < len; n++ ) {
    let code = rU16( d, rec.stringOffset + 2 * n );
    if ( code === 0 ) break;
    if ( code < 32 || code > 127 ) code = 0x3F;   /* '?' */
    s += String.fromCharCode( code );
  }
  return s;
}

/* sfobjs.c: tt_name_ascii_from_other */
function nameFromOther( d, rec ) {
  let s = '';
  for ( let n = 0; n < rec.stringLength; n++ ) {
    let code = d[rec.stringOffset + n];
    if ( code === 0 ) break;
    if ( code < 32 || code > 127 ) code = 0x3F;
    s += String.fromCharCode( code );
  }
  return s;
}

/* ======================================================================
 *  tt_face_get_name (sfobjs.c) -- the platform/encoding preference order
 * ==================================================================== */
function ttFaceGetName( d, nameTable, nameid ) {
  let found_apple_roman   = -1;
  let found_apple_english = -1;
  let found_win           = -1;
  let found_unicode       = -1;
  let is_english          = 0;

  const names = nameTable.names;

  for ( let n = 0; n < names.length; n++ ) {
    const rec = names[n];
    if ( rec.nameID !== nameid || rec.stringLength <= 0 ) continue;

    switch ( rec.platformID ) {
    case TT_PLATFORM_APPLE_UNICODE:
    case TT_PLATFORM_ISO:
      found_unicode = n;
      break;

    case TT_PLATFORM_MACINTOSH:
      if ( rec.languageID === TT_MAC_LANGID_ENGLISH )
        found_apple_english = n;
      else if ( rec.encodingID === TT_MAC_ID_ROMAN )
        found_apple_roman = n;
      break;

    case TT_PLATFORM_MICROSOFT:
      /* we only take a non-English name when nothing else is available */
      if ( found_win === -1 || ( rec.languageID & 0x3FF ) === 0x009 ) {
        switch ( rec.encodingID ) {
        case TT_MS_ID_SYMBOL_CS:
        case TT_MS_ID_UNICODE_CS:
        case TT_MS_ID_UCS_4:
          is_english = ( rec.languageID & 0x3FF ) === 0x009 ? 1 : 0;
          found_win  = n;
          break;
        default:
          break;
        }
      }
      break;

    default:
      break;
    }
  }

  let found_apple = found_apple_roman;
  if ( found_apple_english >= 0 )
    found_apple = found_apple_english;

  /* favour Windows-encoded names when available (and English) */
  let convert = null, rec = null;

  if ( found_win >= 0 && !( found_apple >= 0 && !is_english ) ) {
    rec = names[found_win];
    switch ( rec.encodingID ) {
    case TT_MS_ID_UNICODE_CS:
    case TT_MS_ID_SYMBOL_CS:
    case TT_MS_ID_UCS_4:
      convert = nameFromUtf16;
      break;
    default:
      convert = null;
    }
  } else if ( found_apple >= 0 ) {
    rec     = names[found_apple];
    convert = nameFromOther;
  } else if ( found_unicode >= 0 ) {
    rec     = names[found_unicode];
    convert = nameFromUtf16;
  }

  if ( rec && convert )
    return convert( d, rec );

  return null;
}

/* the GET_NAME block at the end of sfnt_load_face */
function pickNames( d, nameTable, os2 ) {
  const GET = ( id ) => ttFaceGetName( d, nameTable, id );

  let family = null, style = null;

  if ( os2.version !== 0xFFFF && ( os2.fsSelection & 256 ) ) {
    /* WWS-only face */
    family = GET( TT_NAME_ID_TYPOGRAPHIC_FAMILY );
    if ( !family ) family = GET( TT_NAME_ID_FONT_FAMILY );

    style = GET( TT_NAME_ID_TYPOGRAPHIC_SUBFAMILY );
    if ( !style ) style = GET( TT_NAME_ID_FONT_SUBFAMILY );
  } else {
    family = GET( TT_NAME_ID_WWS_FAMILY );
    if ( !family ) family = GET( TT_NAME_ID_TYPOGRAPHIC_FAMILY );
    if ( !family ) family = GET( TT_NAME_ID_FONT_FAMILY );

    style = GET( TT_NAME_ID_WWS_SUBFAMILY );
    if ( !style ) style = GET( TT_NAME_ID_TYPOGRAPHIC_SUBFAMILY );
    if ( !style ) style = GET( TT_NAME_ID_FONT_SUBFAMILY );
  }

  return { familyName: family, styleName: style };
}

/* ======================================================================
 *  loca / hmtx
 * ==================================================================== */
function locaInfo( d, tables, head, numGlyphs ) {
  const glyfT = tables.get( 'glyf' );
  const glyf_len    = glyfT ? glyfT.length : 0;
  const glyf_offset = glyfT ? glyfT.offset : 0;

  const locaT = tables.get( 'loca' );
  if ( !locaT )
    return { num_locations: 0, loca_offset: 0, shift: 1, glyf_len, glyf_offset };

  const shift = head.Index_To_Loc_Format !== 0 ? 2 : 1;
  let table_len = locaT.length;
  if ( table_len > 0x10000 * Math.pow( 2, shift ) )
    table_len = 0x10000 * Math.pow( 2, shift );

  let num_locations = table_len >> shift;

  /* "we only handle the case where `maxp' gives a larger value" */
  if ( num_locations !== numGlyphs + 1 && num_locations < numGlyphs + 1 ) {
    const new_loca_len = ( numGlyphs + 1 ) << shift;
    const pos = locaT.offset;
    let dist = 0x7FFFFFFF, found = false;
    for ( const [ , e ] of tables ) {
      const diff = e.offset - pos;
      if ( diff > 0 && diff < dist ) { dist = diff; found = true; }
    }
    if ( !found ) dist = d.length - pos;
    if ( new_loca_len <= dist ) {
      num_locations = numGlyphs + 1;
      table_len     = new_loca_len;
    }
  }

  return { num_locations, loca_offset: locaT.offset, shift, glyf_len, glyf_offset };
}

/* ======================================================================
 *  openFace
 * ==================================================================== */
export function openFace( u8, faceIndex = 0 ) {
  const d = u8;

  const ttc = openFont( d );
  const numFaces = ttc.count;

  let idx = Math.abs( faceIndex ) & 0xFFFF;
  if ( idx >= ttc.count )
    throw new SfntError( 0x06 /* Invalid_Argument */ );

  const base = ttc.offsets[idx];
  const { format_tag, tables } = loadFontDir( d, base );

  const headT = tables.get( 'head' ) || tables.get( 'bhed' );

  /* Kill switch, in the manner of `S3_NO_ZWJ_EXTPICT' and `S1_NO_ZEROMARKFIX':
   * `S3_NO_BITMAPFACE=1' restores the two pre-fix `throw's below, so the
   * bitmap-only-face deferral can be measured A/B from a single build. */
  const noBitmapFace = !!process.env.S3_NO_BITMAPFACE;

  /* cffobjs.c cff_face_init:
   *     error = face->goto_table( face, TTAG_head, stream, 0 );
   *     if ( !error ) { pure_cff = 0; sfnt->load_face( ... ); }
   *     else          { /* no `head' -> a PURE CFF font *\/ ... }
   * so an `OTTO' file whose `head' table is missing -- or was dropped by the
   * table-directory sanitiser because its length runs past EOF -- is NOT an
   * error: FreeType loads it as a bare CFF and takes numGlyphs, unitsPerEM
   * and the names from the CFF itself.  The caller completes such a face
   * (see `pureCFF' below); sfnt.js does not depend on cff.js. */
  if ( !headT && format_tag === TTAG_OTTO ) {
    if ( !tables.has( 'CFF ' ) && !tables.has( 'CFF2' ) )
      throw new SfntError( 0x8E /* Table_Missing */ );

    return {
      numFaces: 1, faceIndex: idx, numGlyphs: 0,
      unitsPerEM: 1000,
      familyName: null, styleName: null,
      isCFF: true, pureCFF: true,
      data: d, tables,
      indexToLocFormat: 0,
      loca: () => ( { offset: 0, length: 0 } ),
      hmtx: () => ( { advance: 0, lsb: 0 } ),
      vmtx: () => ( { advance: 0, lsb: 0 } ),
      verticalInfo: false,
      head: null, vhea: null,
      /* pure CFF never runs sfnt_load_face, so `face->horizontal' and
       * `face->os2' stay zeroed / absent. */
      hhea: { Ascender: 0, Descender: 0, Line_Gap: 0,
              advance_Width_Max: 0, number_Of_HMetrics: 0 },
      os2: { version: 0xFFFF },
      maxpFields: { numGlyphs: 0 },
      formatTag: format_tag,
      glyfOffset: 0, glyfLen: 0, numLocations: 0,
      ascender: 0, descender: 0, height: 0,
      maxAdvanceWidth: 0,
      bbox: { xMin: 0, yMin: 0, xMax: 0, yMax: 0 },
      cvt: null, fpgm: null, prep: null,
      cffTable: tables.get( 'CFF ' ) || tables.get( 'CFF2' ) || null,
      nameTable: { names: [], numNameRecords: 0 },
    };
  }

  /* A TrueType-tagged face with no usable `head' is refused by the truetype
   * driver, and the cff driver then declines it too (it accepts only `OTTO'),
   * so FT_Open_Face falls through to its "no driver is able to handle this
   * format" tail: Unknown_File_Format.  An `OTTO' face without `head' took the
   * pure-CFF branch above and never reaches this line.
   *
   * We must NOT throw here, for the same reason the outline-less `OTTO' below
   * is not thrown on.  A CBDT/CBLC bitmap-only colour face legitimately has no
   * `glyf' and -- in two of the step-5 corpus fonts -- no `head' either, and
   * HarfBuzz opens it: numGlyphs comes from `maxp', the em from
   * `head::get_upem()' (1000), and the advances from `hmtx'.  So the face is
   * built and the refusal is DEFERRED into `outlineError' below, which
   * `bin/ftlint.js' reports at open time with this very code.
   *
   * tt_face_init fails BEFORE sfnt_load_face runs for such a face, so none of
   * the checks sfnt_load_face performs can be the error FreeType reports.  They
   * are therefore SKIPPED for a `headless' face rather than left to throw a
   * different code: seven fuzzed fonts in the robust corpus are `head'-less
   * with no `maxp' either, and would otherwise report Invalid_Table (0x08)
   * where FreeType reports Unknown_File_Format (0x02). */
  const headless = !headT;
  if ( headless && noBitmapFace )
    throw new SfntError( 0x02 /* Unknown_File_Format */ );
  const head = headless ? defaultHead() : loadHead( d, headT );

  if ( !headless && ( head.Units_Per_EM < 16 || head.Units_Per_EM > 16384 ) )
    throw new SfntError( 0x08 /* Invalid_Table */ );

  const maxpT = tables.get( 'maxp' );
  if ( !maxpT && !headless ) throw new SfntError( 0x08 );
  const maxp = maxpT ? loadMaxp( d, maxpT ) : defaultMaxp();

  const hheaT = tables.get( 'hhea' );
  const hhea  = hheaT ? loadHhea( d, hheaT )
                      : { Ascender: 0, Descender: 0, Line_Gap: 0,
                          advance_Width_Max: 0, number_Of_HMetrics: 0 };

  const vheaT = tables.get( 'vhea' );
  const vmtxT = tables.get( 'vmtx' );
  const vhea  = vheaT ? loadHhea( d, vheaT ) : null;
  const verticalInfo = !!( vheaT && vmtxT );

  /* sfnt_load_face:
   *     LOADM_( hhea, 0 );
   *     if ( !error ) { LOADM_( hmtx, 0 );
   *                     if ( FT_ERR_EQ( error, Table_Missing ) )
   *                       error = FT_THROW( Hmtx_Table_Missing ); }
   * An over-long `hmtx' is TRUNCATED by the table-directory sanitiser rather
   * than dropped, so this fires only when the table is genuinely absent.  It
   * sits after the `head'/unitsPerEM and `maxp' checks because sfnt_load_face
   * loads those first -- and is skipped for a `headless' face, which never
   * reaches sfnt_load_face at all. */
  if ( hheaT && !tables.has( 'hmtx' ) && !headless )
    throw new SfntError( 0x93 /* Hmtx_Table_Missing */ );

  const os2 = loadOs2( d, tables.get( 'OS/2' ) );

  const nameTable = loadName( d, tables.get( 'name' ) );
  const { familyName, styleName } = pickNames( d, nameTable, os2 );

  const numGlyphs = maxp.numGlyphs;

  const li = locaInfo( d, tables, head, numGlyphs );

  const hmtxT = tables.get( 'hmtx' );
  const horz_metrics_offset = hmtxT ? hmtxT.offset : 0;
  const horz_metrics_size   = hmtxT ? hmtxT.length : 0;
  const vert_metrics_offset = vmtxT ? vmtxT.offset : 0;
  const vert_metrics_size   = vmtxT ? vmtxT.length : 0;

  /* Driver selection, exactly as FT_New_Face performs it.
   *
   * ttobjs.c tt_face_init accepts ONLY the format tags 0x00010000,
   * 0x00020000, `true', 0xA5kbd and 0xA5lst, so an `OTTO' file is always
   * refused by the `truetype' driver and falls through to the `cff' one;
   * cffobjs.c cff_face_init then requires format_tag == TTAG_OTTO and goes on
   * to load the `CFF ' table, which yields Table_Missing (0x8E, "SFNT font
   * table missing") when there is none.
   *
   * Conversely a file carrying a TrueType format tag is claimed by the
   * `truetype' driver and rendered from `glyf' EVEN IF it also contains a
   * stray `CFF ' table -- the presence of a `CFF ' table is NOT what selects
   * the CFF driver. */
  const isCFF = format_tag === TTAG_OTTO;

  /* cff_face_init loads the CFF part after sfnt_load_face:
   *     error = face->goto_table( face, TTAG_CFF2, stream, 0 );
   *     if ( FT_ERR_EQ( error, Table_Missing ) )
   *       error = face->goto_table( face, TTAG_CFF, stream, 0 );
   *     if ( error ) goto Exit;
   * so FreeType does refuse an `OTTO' face carrying neither `CFF ' nor
   * `CFF2' (Table_Missing, 0x8E) -- verified against ftlint for all three
   * such fonts in the corpora.
   *
   * We must NOT throw here, though.  A shape-only OpenType face (layout
   * tables, a `cmap', no outlines at all) is legitimate and HarfBuzz opens
   * it, so the face object is built and the failure is DEFERRED to whoever
   * actually asks for an outline.  `bin/ftlint.js' consults `outlineError'
   * at open time and reproduces FreeType's message exactly, so ftlint stays
   * byte-identical while shaping consumers get a usable face.
   *
   * The `headless' face above is the same shape-only case, and is recorded
   * here rather than thrown on for the same reason. */
  let outlineError = 0;
  if ( isCFF && !tables.has( 'CFF ' ) && !tables.has( 'CFF2' ) )
    outlineError = 0x8E; /* Table_Missing */
  else if ( headless )
    outlineError = 0x02; /* Unknown_File_Format */

  /* ttobjs.c tt_face_init -> ttpload.c tt_face_load_loca:
   *     if ( face->glyf_len && FT_ERR_EQ( error, Table_Missing ) ) goto Exit;
   *     if ( face->glyf_len == 0 && FT_ERR_EQ( error, Locations_Missing ) )
   *       goto Bad_Format;
   * so a TrueType-tagged face with neither `glyf' nor `loca' is refused by the
   * truetype driver; the cff driver then refuses it too (it takes only
   * `OTTO'), and FT_New_Face reports Unknown_File_Format.
   *
   * The deferral applies here too, and for the same reason: the CBDT/CBLC
   * bitmap-only colour faces of the step-5 corpus carry neither `glyf' nor
   * `loca', HarfBuzz shapes them from `cmap' and `hmtx', and only a request
   * for an OUTLINE may fail.  This is the TrueType half of the deferral that
   * was applied to the CFF path above and never to this one. */
  if ( !isCFF && !tables.has( 'loca' ) ) {
    if ( noBitmapFace )
      throw new SfntError( li.glyf_len === 0 ? 0x02 /* Unknown_File_Format */
                                             : 0x8E /* Table_Missing */ );
    if ( !outlineError )
      outlineError = li.glyf_len === 0 ? 0x02 /* Unknown_File_Format */
                                       : 0x8E /* Table_Missing */;
  }

  /* "Set up metrics" in sfnt_load_face */
  let ascender, descender, height;
  if ( os2.version !== 0xFFFF && ( os2.fsSelection & 128 ) ) {
    ascender  = os2.sTypoAscender;
    descender = os2.sTypoDescender;
    height    = ascender - descender + os2.sTypoLineGap;
  } else {
    ascender  = hhea.Ascender;
    descender = hhea.Descender;
    height    = ascender - descender + hhea.Line_Gap;

    if ( !( ascender || descender ) && os2.version !== 0xFFFF ) {
      if ( os2.sTypoAscender || os2.sTypoDescender ) {
        ascender  = os2.sTypoAscender;
        descender = os2.sTypoDescender;
        height    = ascender - descender + os2.sTypoLineGap;
      } else {
        ascender  = os2.usWinAscent;
        descender = -os2.usWinDescent;
        height    = ascender - descender;
      }
    }
  }

  /* tt_face_get_metrics (ttmtx.c) */
  function metricsFor( vertical, gindex ) {
    const header     = vertical ? vhea : hhea;
    let   table_pos  = vertical ? vert_metrics_offset : horz_metrics_offset;
    const table_size = vertical ? vert_metrics_size   : horz_metrics_size;
    const table_end  = table_pos + table_size;

    let abearing = 0, aadvance = 0;
    const k = header ? header.number_Of_HMetrics : 0;

    if ( k > 0 ) {
      if ( gindex < k ) {
        table_pos += 4 * gindex;
        if ( table_pos + 4 > table_end ) return { advance: 0, lsb: 0 };
        aadvance = rU16( d, table_pos );
        abearing = rS16( d, table_pos + 2 );
      } else {
        table_pos += 4 * ( k - 1 );
        if ( table_pos + 2 > table_end ) return { advance: 0, lsb: 0 };
        aadvance = rU16( d, table_pos );

        table_pos += 4 + 2 * ( gindex - k );
        if ( table_pos + 2 > table_end ) abearing = 0;
        else abearing = rS16( d, table_pos );
      }
    } else {
      return { advance: 0, lsb: 0 };
    }

    return { advance: aadvance, lsb: abearing };
  }

  /* tt_face_get_location (ttpload.c) */
  function loca( gindex ) {
    let pos1 = 0, pos2 = 0;

    if ( gindex < li.num_locations ) {
      if ( head.Index_To_Loc_Format !== 0 ) {
        const p       = li.loca_offset + gindex * 4;
        const p_limit = li.loca_offset + li.num_locations * 4;
        pos1 = rU32( d, p );
        pos2 = pos1;
        if ( p + 8 <= p_limit ) pos2 = rU32( d, p + 4 );
      } else {
        const p       = li.loca_offset + gindex * 2;
        const p_limit = li.loca_offset + li.num_locations * 2;
        pos1 = rU16( d, p );
        pos2 = pos1;
        if ( p + 4 <= p_limit ) pos2 = rU16( d, p + 2 );
        pos1 *= 2;
        pos2 *= 2;
      }
    }

    if ( pos1 > li.glyf_len )
      return { offset: 0, length: 0 };

    if ( pos2 > li.glyf_len ) {
      if ( gindex === li.num_locations - 2 )
        pos2 = li.glyf_len;
      else
        return { offset: 0, length: 0 };
    }

    const length = pos2 >= pos1 ? pos2 - pos1 : li.glyf_len - pos1;
    return { offset: pos1, length };
  }

  const face = {
    numFaces, faceIndex: idx, numGlyphs,
    unitsPerEM: head.Units_Per_EM,
    familyName, styleName,
    isCFF,
    /* non-zero when no outline source is present; see above */
    outlineError,
    data: d,
    tables,
    indexToLocFormat: head.Index_To_Loc_Format,
    loca,
    hmtx: ( g ) => metricsFor( 0, g ),
    vmtx: ( g ) => metricsFor( 1, g ),
    verticalInfo,
    head, hhea, vhea, os2,
    maxpFields: maxp,
    formatTag: format_tag,
    glyfOffset: li.glyf_offset,
    glyfLen   : li.glyf_len,
    numLocations: li.num_locations,
    ascender, descender, height,
    maxAdvanceWidth: hhea.advance_Width_Max,
    bbox: { xMin: head.xMin, yMin: head.yMin, xMax: head.xMax, yMax: head.yMax },
    /* W4 (tthint.js) */
    cvt : tables.get( 'cvt ' ) || null,
    fpgm: tables.get( 'fpgm' ) || null,
    prep: tables.get( 'prep' ) || null,
    /* W3 (cff.js) */
    cffTable: tables.get( 'CFF ' ) || tables.get( 'CFF2' ) || null,
    nameTable,
  };

  return face;
}

export { SfntError, ttFaceGetName };
export default openFace;
