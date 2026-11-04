/* shaper-misc.js -- the small per-script shapers: Thai/Lao today, with Khmer,
 * Myanmar and Hangul still to come (see NOTES-S1-scripts.md, "Not done").
 *
 * Normative sources: ../otspec/txt/sd-thai.txt, sd-lao.txt (and sd-khmer.txt,
 * sd-myanmar.txt, sd-hangul.txt for the parts not yet written).
 *
 * As with shaper-use.js this module must not import shape-plan.js: that
 * module imports the shapers, and an ES-module cycle would leave the bindings
 * uninitialised.
 */

import { GC, generalCategory, joiningType, joiningGroup, JT } from './unicode.js';
import { makeInfo, makePos, mergeClusterRange, DIRECTION_RTL } from './buffer.js';
import { selectScript, selectLangSys, findFeature, featureWouldSubstitute,
         GlyphProps } from './ot-layout.js';

const F_NONE = 0, F_GLOBAL = 1, F_HAS_FALLBACK = 2, F_MANUAL_ZWNJ = 4, F_MANUAL_ZWJ = 8,
      F_PER_SYLLABLE = 64;

/* ------------------------------------------------------------------ */
/*  Thai / Lao                                                         */
/* ------------------------------------------------------------------ */

/* SARA AM (Thai U+0E33, Lao U+0EB3) is encoded as a single character but is
 * drawn as a NIKHAHIT above the consonant plus a spacing SARA AA after it.
 * The shaper decomposes it and then moves the nikhahit to the LEFT of any
 * tone marks that already sit above the same consonant, so that the tone mark
 * ends up outermost.
 *
 * Measured against the reference: U+0E01 U+0E31 U+0E33 shapes to
 *   uni0E01 uni0E4D uni0E31 uni0E32
 * i.e. SARA AM became NIKHAHIT + SARA AA and the NIKHAHIT jumped back over
 * the MAI HAN AKAT, with the whole run merged into one cluster. */

const isSaraAm   = ( u ) => u === 0x0E33 || u === 0x0EB3;
/* Thai and Lao sit 0x80 apart, so the same arithmetic serves both. */
const nikhahitOf = ( u ) => u - 0x0E33 + 0x0E4D;
const saraAaOf   = ( u ) => u - 1;

/* IS_ABOVE_BASE_MARK: the marks a nikhahit reorders in front of.  The
 * reference names the set once, as the characters Uniscribe was measured to
 * reorder --
 *   Thai: <0E31, 0E34..0E37,      0E47..0E4E>
 *   Lao:  <0EB1, 0EB4..0EB7, 0EBB, 0EC8..0ECD>
 * -- and the Lao column is the Thai one plus 0x80 EXCEPT for U+0EBB LAO VOWEL
 * SIGN MAI KON, which has no Thai counterpart and folds to 0x0E3B.  That
 * fourth range was missing, so a nikhahit from a Lao SARA AM stopped in front
 * of U+0EBB instead of stepping over it.  W18_NO_LAOMAIKON=1 drops it again. */
function isToneMark( u ) {
  const v = u & ~0x0080;                       /* fold Lao onto Thai */
  return ( v >= 0x0E34 && v <= 0x0E37 ) ||
         ( v >= 0x0E47 && v <= 0x0E4E ) ||
         v === 0x0E31 ||
         ( v === 0x0E3B && !process.env.W18_NO_LAOMAIKON );
}

function preprocessText( plan, buffer, font ) {
  const info = buffer.info;
  const n = info.length;
  let any = false;
  for ( let i = 0; i < n; i++ ) if ( isSaraAm( info[ i ].codepoint ) ) { any = true; break; }
  if ( !any ) return;

  const out = [];
  /* Half-open spans [start, end) that one SARA AM produced, so their clusters
   * can be merged once the whole run is rebuilt. */
  const spans = [];

  for ( let i = 0; i < n; i++ ) {
    const g = info[ i ];
    if ( !isSaraAm( g.codepoint ) ) { out.push( g ); continue; }

    const u = g.codepoint;
    const nik = Object.assign( {}, g );
    nik.codepoint = nikhahitOf( u );
    const aa = Object.assign( {}, g );
    aa.codepoint = saraAaOf( u );

    /* Walk back over any tone marks already emitted for this consonant and
     * insert the nikhahit before them, so the tone mark stays outermost. */
    let at = out.length;
    while ( at > 0 && isToneMark( out[ at - 1 ].codepoint ) ) at--;
    out.splice( at, 0, nik );
    out.push( aa );

    /* Merge clusters ONLY when the nikhahit actually jumped back over a tone
     * mark.  A reorder is what makes the cluster numbers non-monotonic, so it
     * is what has to be repaired; a SARA AM that simply decomposed in place
     * leaves both pieces on their own cluster and needs no merge.  Measured
     * against the reference on `U+0020 U+0E33' -> [0=0|0=1|0=1] (both levels)
     * and `U+0020 U+0E34 U+0E33' -> [0=0|0=2|0=1|0=2] at cluster-level 2 and
     * [0=0|0=1|0=1|0=1] at level 1 -- i.e. no merge in the first, and a merge
     * that does NOT reach the preceding base in the second.  The old span
     * started at `at - 1' and swallowed that base in both.
     * `out.length - 2' is where the nikhahit would sit had it not moved. */
    if ( !process.env.S1_NO_THAIAMCLUSTER ) {
      /* `out.length - 2' is where the nikhahit would sit had it not moved. */
      spans.push( { base: at > 0 ? at - 1 : 0, start: at, end: out.length,
                    reordered: at < out.length - 2 } );
    } else {
      spans.push( { base: at > 0 ? at - 1 : 0, start: at, end: out.length,
                    reordered: true, legacy: true } );
    }
  }

  info.length = 0;
  for ( const g of out ) info.push( g );
  if ( buffer.len !== undefined ) buffer.len = info.length;
  /* buffer.pos is left alone: S3 clears and rebuilds it after substitution,
   * and truncating it here loses the mark offsets GPOS later expects. */

  /* Through the buffer, not by hand -- the hand-rolled min-loop merged at
   * EVERY cluster level, and merging is level-dependent.  Two independent
   * merges, each gated by the buffer on the level group it belongs to:
   *
   *   * the nikhahit is a combining mark, so it joins the grapheme of the
   *     glyph before the insertion point: mergeGraphemeClusters, live only at
   *     the GRAPHEMES levels (0 and 3);
   *   * a nikhahit that jumped back over a tone mark leaves the cluster
   *     numbers non-monotonic, which has to be repaired: mergeClusters, live
   *     only at the MONOTONE levels (0 and 1).
   *
   * Verified against all nine reference answers in cluster.tests, which cover
   * every level for `U+0020 U+0E33' and `U+0020 U+0E34 U+0E33':
   *   level 0  -> 0,0,0    and 0,0,0,0      (both merges)
   *   level 1  -> 0,1,1    and 0,1,1,1      (monotone only)
   *   level 2  -> 0,1,1    and 0,2,1,2      (neither)
   *   level 3  -> 0,0,0    and 0,0,0,0      (graphemes only) */
  for ( const sp of spans ) {
    if ( sp.legacy ) {
      let min = info[ sp.base ].cluster;
      for ( let i = sp.base + 1; i < sp.end; i++ )
        if ( info[ i ].cluster < min ) min = info[ i ].cluster;
      for ( let i = sp.base; i < sp.end; i++ ) info[ i ].cluster = min;
      continue;
    }
    if ( sp.start > 0 ) buffer.mergeGraphemeClusters( sp.base, sp.end );
    if ( sp.reordered ) buffer.mergeClusters( sp.start, sp.end );
  }
}

/* Thai has no script-specific GSUB features beyond the common set; the
 * reference relies on the default feature collection. */
function collectFeatures( /* b */ ) {}

export const thaiShaper = {
  name: 'thai',
  collectFeatures,
  preprocessText,
  /* The reference does not insert a dotted circle for Thai -- measured: a
   * lone U+0E34 in a font that HAS U+25CC still shapes to one glyph. */
  normalizationPreference: 'default',
  zeroWidthMarks: 'by-gdef-late',
  /* false, not true: the reference's Thai shaper opts OUT of fallback mark
   * positioning, and the flag also gates the legacy `kern' table.  With true
   * the mark lands at @0,128; the reference zeroes it and shifts it back to
   * @-1229,0.  Verified against ../ft.sh below the fix. */
  fallbackPosition: false,
  gposTag: null,
};

/* ------------------------------------------------------------------ */
/*  Arabic joining                                                     */
/* ------------------------------------------------------------------ */

/* sd-arabic.txt l.83-113.  "The contextual analysis engine determines the
 * correct contextual form the character should take, based on the character
 * before and after it.  The contextual shape maps to an OTL feature for that
 * form (isol, init, medi, fina)."
 *
 * Which form a character takes follows from the Unicode joining types in
 * ../ucd/ArabicShaping.txt (S2 exposes them as joiningType()):
 *
 *   D  dual-joining      -- can join on both sides
 *   R  right-joining     -- can join only to the preceding character
 *   L  left-joining      -- can join only to the following character
 *   C  join-causing      -- ZWJ and tatweel; joins on both sides
 *   T  transparent       -- marks; stepped over entirely
 *   U  non-joining       -- breaks the connection (ZWNJ, space, ...)
 *
 * A character joins BACKWARD when the previous non-transparent character can
 * join forward (D, L or C) and it can itself join backward (D, R or C); it
 * joins FORWARD under the mirrored condition.  The four combinations select
 * fina, medi, init and isol. */

const joinsForward  = ( t ) => t === JT.D || t === JT.L || t === JT.C;
const joinsBackward = ( t ) => t === JT.D || t === JT.R || t === JT.C;

/* The order is the one the specification fixes at l.103-109, extended with the
 * three Syriac-only forms the same table names: fin2 and fin3 for the two
 * final alaph shapes, med2 for the medial one.  They cost nothing in a font
 * that does not register them, and without them a Syriac font's alaph never
 * reaches its own lookups.
 *
 * All seven stay in ONE gsub stage.  Giving each its own stage was measured
 * against the graded corpus and LOST (step 2 0.965094 -> 0.964453), so the
 * one-stage arrangement is what the reference's output is consistent with;
 * do not split them again without re-measuring. */
const ARABIC_JOIN_FEATURES = [ 'isol', 'fina', 'fin2', 'fin3', 'medi', 'med2', 'init' ];

function arabicCollectFeatures( b ) {
  /* sd-arabic.txt fixes the order at l.101-109: ccmp, then isol, fina, medi,
   * init.  l.385 is explicit about why -- ccmp "is implemented before any
   * other features", because it is what decomposes a character into the glyph
   * the joining lookups are actually written against.
   *
   * The generic common set does carry ccmp and locl, but it is added AFTER
   * shaper.collectFeatures(), so relying on it put ccmp one stage LATER than
   * the joining forms: isol/fina/medi/init ran at gsub stage 2 against the raw
   * cmap glyph `Beh`, matched nothing, and only then did ccmp (stage 3) turn
   * `Beh` into `Behx` + `OneDotBelowNS`.  The result was a correct joining
   * mask on a glyph no joining lookup covers.  Enabling them here puts them in
   * the earlier stage, the way the Indic shaper already does.
   *
   * Each joining form stays NON-global so it keeps its own mask bit and
   * applies only to the glyphs setupMasks() marks. */
  /* `stch' goes first and owns the first stage by itself, so that the pause
   * that follows sees exactly what THAT feature multiplied -- see
   * recordStch().  W18_NO_STCH=1 leaves the feature and the pause out. */
  if ( !process.env.W18_NO_STCH ) {
    b.enableFeature( 'stch' );
    b.addGsubPause( recordStch );
  }
  /* Both are MANUAL_ZWJ here.  "Normally, Unicode says a ZWNJ means don't
   * ligate.  In Arabic script however, it says a ZWJ should ALSO mean don't
   * ligate" -- so a ZWJ has to stay visible to these lookups instead of being
   * stepped over, and a contextual ccmp run stops at it.  Without the flag an
   * enclosing-number sequence swallowed the ZWJ and enclosed one digit too
   * many.  W18_NO_ARABCCMPZWJ=1 drops back to the automatic joiners. */
  if ( !process.env.S1_NO_ARABCCMP ) {
    const jf = process.env.W18_NO_ARABCCMPZWJ ? F_NONE : F_MANUAL_ZWJ;
    b.enableFeature( 'ccmp', jf );
    b.enableFeature( 'locl', jf );
  }
  b.addGsubPause( null );
  for ( const t of ARABIC_JOIN_FEATURES ) b.addFeature( t, F_MANUAL_ZWJ );
  b.addGsubPause( null );

  /* The ligating tail, in the shaper's OWN stages rather than left to the
   * generic common/horizontal lists.  Two things follow from putting it here:
   * the features run in the "shaping" phase, BEFORE mark/mkmk/kern and before
   * whatever else the generic lists add, and every one of them is MANUAL_ZWJ --
   * "Unicode says a ZWNJ means don't ligate; in Arabic script it says a ZWJ
   * should also mean don't ligate".  Leaving calt to the horizontal list ran
   * it a whole phase late and a lam that the font's calt reshapes kept its
   * plain .init form.  W18_NO_ARABTAIL=1 restores the generic placement. */
  if ( !process.env.W18_NO_ARABTAIL ) {
    b.addGsubPause( null );                       /* deallocate_buffer_var */
    b.enableFeature( 'rlig', F_MANUAL_ZWJ | F_HAS_FALLBACK );
    /* arabic_fallback_shape's pause; the reference adds it for Arabic only. */
    const ARAB = ( ( 0x41 << 24 ) | ( 0x72 << 16 ) | ( 0x61 << 8 ) | 0x62 ) >>> 0;
    if ( !b.props || ( b.props.script >>> 0 ) === ARAB ) b.addGsubPause( null );
    b.enableFeature( 'calt', F_MANUAL_ZWJ );
    /* harfbuzz#1573: the pause is added because `rclt' has not been added yet. */
    b.addGsubPause( null );
    b.enableFeature( 'liga', F_MANUAL_ZWJ );
    b.enableFeature( 'clig', F_MANUAL_ZWJ );
    b.enableFeature( 'mset', F_MANUAL_ZWJ );
    return;
  }
  /* l.123: mark positioning via substitution. */
  b.addFeature( 'mset' );
}

/* arabic_joining()'s state machine.  The FORM each character takes is decided
 * below by the prev/next walk, which is measured and stays; what the machine
 * is needed for here is the BUFFER FLAGS, which depend on its state and not on
 * the form: a position where the machine could have taken a different decision
 * had the text continued is "unsafe to concat", and one where it fixed the
 * previous character's form is "safe to insert tatweel".  Neither is derivable
 * from the chosen forms alone, which is why the table is here.
 *
 * Columns are jt_U, jt_L, jt_R, jt_D, jg_ALAPH, jg_DALATH_RISH; each cell is
 * [ does this state give the PREVIOUS character an action?, next state ]. */
const ARABIC_STATE_TABLE = [
  /* 0: prev was U, not willing to join */
  [ [ 0, 0 ], [ 0, 2 ], [ 0, 1 ], [ 0, 2 ], [ 0, 1 ], [ 0, 6 ] ],
  /* 1: prev was R or ISOL/ALAPH, not willing to join */
  [ [ 0, 0 ], [ 0, 2 ], [ 0, 1 ], [ 0, 2 ], [ 0, 5 ], [ 0, 6 ] ],
  /* 2: prev was D/L in ISOL form, willing to join */
  [ [ 0, 0 ], [ 0, 2 ], [ 1, 1 ], [ 1, 3 ], [ 1, 4 ], [ 1, 6 ] ],
  /* 3: prev was D in FINA form, willing to join */
  [ [ 0, 0 ], [ 0, 2 ], [ 1, 1 ], [ 1, 3 ], [ 1, 4 ], [ 1, 6 ] ],
  /* 4: prev was FINA ALAPH, not willing to join */
  [ [ 0, 0 ], [ 0, 2 ], [ 1, 1 ], [ 1, 2 ], [ 1, 5 ], [ 1, 6 ] ],
  /* 5: prev was FIN2/FIN3 ALAPH, not willing to join */
  [ [ 0, 0 ], [ 0, 2 ], [ 1, 1 ], [ 1, 2 ], [ 1, 5 ], [ 1, 6 ] ],
  /* 6: prev was DALATH/RISH, not willing to join */
  [ [ 0, 0 ], [ 0, 2 ], [ 0, 1 ], [ 0, 2 ], [ 0, 5 ], [ 0, 6 ] ],
];
const JOIN_COL_T = 7;            /* transparent: stepped over entirely */

/* get_joining_type(): the state machine's column.  C joins like D, and the two
 * Syriac joining GROUPS take columns of their own whatever their joining type
 * says -- that is how alaph's four final forms are told apart. */
function joinColumn( cp ) {
  const g = joiningGroup( cp );
  if ( g === 'Alaph' ) return 4;
  if ( g === 'Dalath_Rish' ) return 5;
  const t = joiningType( cp );
  if ( t === JT.T ) return JOIN_COL_T;
  if ( t === JT.C ) return JT.D;
  return t;
}

/* The flag half of arabic_joining().  Sets no masks: it only records where a
 * caller may cut or splice the text.  W18_NO_ARABJOINFLAGS=1 turns it off. */
function arabicJoiningFlags( buffer ) {
  const info = buffer.info;
  const n = info.length;
  let prev = -1, state = 0;

  const ctx = buffer.context || [ [], [] ];
  for ( const cp of ( ctx[0] || [] ) ) {
    const t = joinColumn( cp );
    if ( t === JOIN_COL_T ) continue;
    state = ARABIC_STATE_TABLE[ state ][ t ][ 1 ];
    break;
  }

  for ( let i = 0; i < n; i++ ) {
    const t = joinColumn( info[i].codepoint );
    if ( t === JOIN_COL_T ) continue;
    const e = ARABIC_STATE_TABLE[ state ][ t ];
    if ( e[0] && prev >= 0 ) {
      buffer.safeToInsertTatweel( prev, i + 1 );
    } else if ( prev < 0 ) {
      if ( t >= JT.R ) buffer.unsafeToConcatFromOutbuffer( 0, i + 1 );
    } else if ( t >= JT.R || ( state >= 2 && state <= 5 ) ) {
      buffer.unsafeToConcat( prev, i + 1 );
    }
    prev = i;
    state = e[1];
  }

  for ( const cp of ( ctx[1] || [] ) ) {
    const t = joinColumn( cp );
    if ( t === JOIN_COL_T ) continue;
    const e = ARABIC_STATE_TABLE[ state ][ t ];
    if ( e[0] && prev >= 0 ) buffer.safeToInsertTatweel( prev, n );
    else if ( prev >= 0 && state >= 2 && state <= 5 ) buffer.unsafeToConcat( prev, n );
    break;
  }
}

/* Decide each character's contextual form and set the matching feature bit. */
function arabicSetupMasks( plan, buffer, font ) {
  const info = buffer.info;
  const n = info.length;
  if ( !n ) return;

  /* Runs BEFORE the early returns below: the reference's arabic_joining() is
   * unconditional, and a font that registers none of isol/fina/medi/init still
   * owes its caller correct unsafe-to-concat positions. */
  if ( !process.env.W18_NO_ARABJOINFLAGS &&
       typeof buffer.safeToInsertTatweel === 'function' ) arabicJoiningFlags( buffer );

  const fm = plan.featureMap;
  if ( !fm ) return;
  const maskOf = ( tag ) => {
    const e = fm.get( tag );
    return e ? e.mask : 0;
  };
  const M = {
    isol: maskOf( 'isol' ), fina: maskOf( 'fina' ),
    medi: maskOf( 'medi' ), init: maskOf( 'init' ),
  };
  if ( !( M.isol || M.fina || M.medi || M.init ) ) return;

  const jt = new Int16Array( n );
  for ( let i = 0; i < n; i++ ) jt[ i ] = joiningType( info[ i ].codepoint );

  /* Transparent characters are stepped over when looking either way.
   *
   * These walk the array directly, NOT flipped for a right-to-left run.  That
   * is deliberate and was measured: ensure_native_direction only reverses the
   * buffer when its direction differs from the script's native one, so an
   * Arabic run is still in LOGICAL order here.  Making the walk
   * direction-aware "so that RTL reads the other way" drops step 2 from
   * 1026/1362 to 966/1362 and takes arabic-normalization from 61 back to 31.
   * Do not reintroduce it. */
  const prevOf = ( i ) => { for ( let k = i - 1; k >= 0; k-- ) if ( jt[ k ] !== JT.T ) return k; return -1; };
  const nextOf = ( i ) => { for ( let k = i + 1; k < n; k++ ) if ( jt[ k ] !== JT.T ) return k; return -1; };

  /* The item CONTEXT joins too.  ../spec/hb-shape-help.txt l.113-116 defines
   * --text-before / --text-after / --unicodes-before / --unicodes-after as
   * "Set text context before/after each line": those characters are not
   * shaped and never reach the output, but they are still the "character
   * before and after" that sd-arabic.txt l.86 makes the contextual analysis
   * turn on -- "The contextual analysis engine determines the correct
   * contextual form the character should take, based on the character before
   * and after it."  Nothing in that sentence is limited to the shaped item.
   *
   * So a run that starts or ends on the item boundary still joins across it:
   * with kaf as pre-context the first teh is medi, not init.  Only the
   * NEAREST non-transparent context character on each side can matter, for
   * the same reason prevOf/nextOf stop at the first non-transparent glyph.
   * bin/hb-shape.js l.400-418 stores context[0] nearest-first and context[1]
   * furthest-last, so both are scanned forward from index 0.
   *
   * W13_NO_JOINCTX=1 restores the context-blind behaviour. */
  let ctxPrev = -1, ctxNext = -1;
  if ( !process.env.W13_NO_JOINCTX && buffer.context ) {
    const nearestJT = ( arr ) => {
      if ( !arr ) return -1;
      for ( let k = 0; k < arr.length; k++ ) {
        const ct = joiningType( arr[ k ] );
        if ( ct !== JT.T ) return ct;
      }
      return -1;
    };
    ctxPrev = nearestJT( buffer.context[ 0 ] );
    ctxNext = nearestJT( buffer.context[ 1 ] );
  }

  for ( let i = 0; i < n; i++ ) {
    const t = jt[ i ];
    if ( t === JT.T ) continue;                  /* marks take no form */

    const p = prevOf( i ), q = nextOf( i );
    const pt = p >= 0 ? jt[ p ] : ctxPrev;
    const qt = q >= 0 ? jt[ q ] : ctxNext;
    const back = pt >= 0 && joinsForward( pt ) && joinsBackward( t );
    const fwd  = qt >= 0 && joinsBackward( qt ) && joinsForward( t );

    let tag;
    if ( back && fwd ) tag = 'medi';
    else if ( back )   tag = 'fina';
    else if ( fwd )    tag = 'init';
    else               tag = 'isol';

    info[ i ].mask = ( info[ i ].mask | M[ tag ] ) >>> 0;
  }
}

/* The modified combining class as shape-plan.js stores it on an info record:
 * the general category in bits 0-4 and the class in bits 8+, and zero for
 * anything that is not a Unicode mark (shape-plan.js l.1306-1321, l.1344). */
const UPROPS_GEN_CAT = 0x001F;
const ccOf = ( inf ) => {
  const g = inf.unicodeProps & UPROPS_GEN_CAT;
  return ( g === GC.Mn || g === GC.Mc || g === GC.Me ) ? ( inf.unicodeProps >>> 8 ) : 0;
};
const setCC = ( inf, cc ) => {
  inf.unicodeProps = ( ( inf.unicodeProps & 0xFF ) | ( cc << 8 ) ) >>> 0;
};

/* Arabic mark ORDER, after canonical ordering has already sorted the run.
 *
 * steps/02/instruction.md S1 puts "the mark ordering and normalization those
 * scripts require" in scope for Arabic, and sd-arabic.txt l.88 makes the
 * shaper responsible for verifying "valid diacritic combinations" during the
 * analysis.  Canonical ordering (UAX #15, applied by shape-plan.js normalize()
 * round 2) sorts marks by combining class, which for Arabic puts the
 * BELOW/ABOVE positional marks -- ../ucd/UnicodeData.txt gives U+0655 and
 * U+0656 class 220 (below) and U+0653, U+0654, U+0658 class 230 (above) --
 * AFTER the vowel/shadda marks that carry the Arabic-specific classes 27-35.
 * That is the wrong order for rendering: the positional marks sit nearest the
 * base and the font's mark lookups are written against them in that position.
 *
 * So the class-220 run, then the class-230 run, is moved back to the front of
 * the mark sequence.  Each moved run is renumbered to a class below every
 * Arabic class (22 for 220, 26 for 230, both under the 27-35 band that
 * shape-plan.js l.185-190 maps the Arabic classes into) so that the sequence
 * is still sorted afterwards and round 3's blocking rule still holds.
 *
 * Called from shape-plan.js l.1680 with [start,end) the run of non-starters.
 * W13_NO_ARABMARKORDER=1 leaves the canonical order alone. */
/* UTR #53 Table 1, "modifier combining marks": the ONLY marks this reordering
 * moves.  A class-220 or class-230 mark that is not on this list stays where
 * canonical ordering put it -- the report's rule is about these specific
 * modifier marks, not about the positional classes as a whole.  Moving every
 * class-220/230 mark instead reordered whole diacritic stacks that the
 * reference leaves alone. */
const MODIFIER_COMBINING_MARKS = new Set( [
  0x0654, /* ARABIC HAMZA ABOVE */
  0x0655, /* ARABIC HAMZA BELOW */
  0x0658, /* ARABIC MARK NOON GHUNNA */
  0x06DC, /* ARABIC SMALL HIGH SEEN */
  0x06E3, /* ARABIC SMALL LOW SEEN */
  0x06E7, /* ARABIC SMALL HIGH YEH */
  0x06E8, /* ARABIC SMALL HIGH NOON */
  0x08CA, /* ARABIC SMALL HIGH FARSI YEH */
  0x08CB, /* ARABIC SMALL HIGH YEH BARREE WITH TWO DOTS BELOW */
  0x08CD, /* ARABIC SMALL HIGH ZAH */
  0x08CE, /* ARABIC LARGE ROUND DOT ABOVE */
  0x08CF, /* ARABIC LARGE ROUND DOT BELOW */
  0x08D3, /* ARABIC SMALL LOW WAW */
  0x08F3, /* ARABIC SMALL HIGH WAW */
] );

function arabicReorderMarks( plan, buffer, start, end ) {
  if ( process.env.W13_NO_ARABMARKORDER ) return;
  const legacy = !!process.env.W18_NO_UTR53FIX;
  const info = buffer.info;
  const isMcm = ( inf ) => legacy || MODIFIER_COMBINING_MARKS.has( inf.codepoint );
  let st = start, i = start;
  for ( let cc = 220; cc <= 230; cc += 10 ) {
    while ( i < end && ccOf( info[ i ] ) < cc ) i++;
    if ( i === end ) break;
    if ( ccOf( info[ i ] ) > cc ) continue;
    let j = i;
    while ( j < end && ccOf( info[ j ] ) === cc && isMcm( info[ j ] ) ) j++;
    if ( i === j ) continue;

    /* A run that is ALREADY leading is not a special case: the two shifts
     * below are no-ops for it, but the cluster merge and the RENUMBERING still
     * have to happen, because what the renumbering buys is that the sequence
     * stays sorted for the normalizer's CGJ handling.  Skipping it left a
     * leading class-230 run numbered 230 in front of the Arabic 27-35 band. */
    if ( legacy && i === st ) {
      if ( st > 0 ) buffer.mergeClusters( st - 1, j );
      st = j;
      i = j;
      continue;
    }
    buffer.mergeClusters( st, j );
    const run = info.slice( i, j );
    for ( let k = i - 1; k >= st; k-- ) info[ k + ( j - i ) ] = info[ k ];
    for ( let k = 0; k < run.length; k++ ) info[ st + k ] = run[ k ];
    const newStart = st + ( j - i );
    const newCC = cc === 220 ? 22 : 26;
    while ( st < newStart ) { setCC( info[ st ], newCC ); st++; }
    i = j;
  }
}

/* ------------------------------------------------------------------ */
/*  stch -- stretching a subtending mark to cover its word              */
/* ------------------------------------------------------------------ */

/* sd-syriac: a font may give a subtending mark (Syriac abbreviation mark, or
 * an Arabic one such as U+0891 PIASTRE MARK ABOVE) a `stch' Multiple
 * substitution that breaks the glyph into tiles: head, repeating middles,
 * tail.  The shaper then repeats the REPEATING tiles as often as it takes to
 * span the rest of the word, and places them by hand.
 *
 * This is done generically -- Syriac and the Arabic subtending marks share
 * one implementation -- in two phases: a GSUB pause right after the `stch'
 * feature records which glyphs came out of it (recordStch), and a
 * postprocess-glyphs pass does the geometry once the advances are final
 * (applyStch).  The odd/even component index is what distinguishes a
 * repeating tile from a fixed one, so the record must happen while the
 * component numbers are still the ones `stch' itself produced. */

const STCH_FIXED = 1, STCH_REPEATING = 2;
const STCH_MAX_GLYPHS = 256;

const I32_MIN = -2147483648, I32_MAX = 2147483647;
const clampI32 = ( v ) => Math.max( I32_MIN, Math.min( I32_MAX, v ) );
const sAdd = ( a, b ) => clampI32( a + b );
const sSub = ( a, b ) => clampI32( a - b );

/* HB_ARABIC_GENERAL_CATEGORY_IS_WORD, in this engine's GC numbering: the
 * categories that still count as "the word the mark stretches over".
 * Punctuation, separators and the other controls end the run. */
const STCH_WORD_CATS = new Set( [
  GC.Cn, GC.Co, GC.Lm, GC.Lo, GC.Mc, GC.Me, GC.Mn,
  GC.Nd, GC.Nl, GC.No, GC.Sc, GC.Sk, GC.Sm, GC.So,
] );

const UPROPS_MASK_IGNORABLE = 0x0020;
const stchAction   = ( inf ) => inf.stchAction | 0;
const isStchTile   = ( inf ) => { const a = inf.stchAction | 0;
                                  return a === STCH_FIXED || a === STCH_REPEATING; };
const stchIgnorable = ( inf ) => !!( inf.unicodeProps & UPROPS_MASK_IGNORABLE ) &&
                                 !( inf.props & GlyphProps.SUBSTITUTED );
const stchWord      = ( inf ) => STCH_WORD_CATS.has( inf.unicodeProps & UPROPS_GEN_CAT );

/* arabic_shape_plan_t::has_stch -- only a font that actually registers `stch'
 * gets this treatment.  Without the test every MultipleSubst output in the
 * first GSUB stage would be read as a tile. */
function planHasStch( plan ) {
  if ( plan._arabicHasStch !== undefined ) return plan._arabicHasStch;
  let has = false;
  const gsub = plan.layout && plan.layout.gsub;
  if ( gsub ) {
    const si = selectScript( gsub, plan.scriptTags );
    if ( si >= 0 )
      has = findFeature( gsub, selectLangSys( gsub, si, plan.languageTags ),
                         'stch' ) >= 0;
  }
  plan._arabicHasStch = has;
  return has;
}

/* record_stch: the pause that runs immediately after `stch'.  Anything the
 * feature multiplied is a tile; odd components repeat, even ones are fixed. */
function recordStch( plan, buffer, font ) {
  if ( !planHasStch( plan ) ) return false;
  const info = buffer.info;
  for ( let i = 0; i < info.length; i++ ) {
    if ( !( info[i].props & GlyphProps.MULTIPLIED ) ) continue;
    info[i].stchAction = ( info[i].ligComp % 2 ) ? STCH_REPEATING : STCH_FIXED;
    buffer.arabicHasStch = true;
  }
  return false;
}

/* apply_stch: two passes over the buffer, the first only counting how many
 * extra glyphs the repeats need so the second can write into a buffer that is
 * already the right size. */
function applyStch( plan, buffer, font ) {
  if ( !buffer.arabicHasStch ) return;
  buffer.arabicHasStch = false;

  /* The buffer is in visual order by now, and `plan.props.direction' is the
   * direction the caller asked for -- NOT plan.direction, which shape() has
   * already rewritten to the script's native direction. */
  const rtl = plan.props && plan.props.direction === DIRECTION_RTL;
  if ( !rtl ) buffer.reverse();

  const sign = ( font && font.xScale < 0 ) ? -1 : 1;
  let extraGlyphsNeeded = 0;

  for ( let step = 0; step <= 1; step++ ) {          /* 0 MEASURE, 1 CUT */
    const count = buffer.len;
    const info = buffer.info, pos = buffer.pos;
    const newLen = count + extraGlyphsNeeded;
    if ( step === 1 ) {
      while ( info.length < newLen ) info.push( makeInfo() );
      while ( pos.length  < newLen ) pos.push( makePos() );
    }
    let j = newLen;                                  /* write head, descending */
    let i = count;
    while ( i ) {
      if ( !isStchTile( info[ i - 1 ] ) ) {
        if ( step === 1 ) { --j; info[j] = info[ i - 1 ]; pos[j] = pos[ i - 1 ]; }
        i--;
        continue;
      }

      /* Yay, justification! */
      let wTotal = 0, wFixed = 0, wRepeating = 0, nFixed = 0, nRepeating = 0;
      const end = i;
      while ( i && isStchTile( info[ i - 1 ] ) ) {
        i--;
        const width = font.getGlyphHAdvance( info[i].codepoint );
        if ( stchAction( info[i] ) === STCH_FIXED ) { wFixed = sAdd( wFixed, width ); nFixed++; }
        else { wRepeating = sAdd( wRepeating, width ); nRepeating++; }
      }
      const start = i;
      let context = i;
      while ( context && !isStchTile( info[ context - 1 ] ) &&
              ( stchIgnorable( info[ context - 1 ] ) || stchWord( info[ context - 1 ] ) ) ) {
        context--;
        wTotal = sAdd( wTotal, pos[ context ].xAdvance );
      }

      /* How many extra times each repeating tile has to appear. */
      let nCopies = 0;
      let wRemainingSigned = wTotal - wFixed;
      let wRepeatingSigned = wRepeating;
      if ( sign < 0 ) { wRemainingSigned = -wRemainingSigned; wRepeatingSigned = -wRepeatingSigned; }
      let wRemaining = sSub( wTotal, wFixed );
      if ( wRemainingSigned > wRepeatingSigned && wRepeatingSigned > 0 )
        nCopies = Math.trunc( wRemainingSigned / wRepeatingSigned ) - 1;

      /* One more repeat, squeezed together, often fits better than a gap. */
      let extraRepeatOverlap = 0;
      const shortfall = wRemainingSigned - wRepeatingSigned * ( nCopies + 1 );
      if ( shortfall > 0 && nRepeating > 0 ) {
        ++nCopies;
        const excess = ( nCopies + 1 ) * wRepeatingSigned - wRemainingSigned;
        if ( excess > 0 ) {
          extraRepeatOverlap = clampI32( Math.trunc( excess / ( nCopies * nRepeating ) ) );
          wRemaining = 0;
        }
      }

      let maxCopies = 0;
      if ( nRepeating > 0 ) {
        const baseGlyphs = nFixed + nRepeating;
        if ( baseGlyphs < STCH_MAX_GLYPHS )
          maxCopies = Math.trunc( ( STCH_MAX_GLYPHS - baseGlyphs ) / nRepeating );
      }
      nCopies = Math.min( nCopies, maxCopies );

      if ( step === 0 ) {
        extraGlyphsNeeded += nCopies * nRepeating;
      } else {
        buffer.unsafeToBreak( context, end );
        let xOffset = Math.trunc( wRemaining / 2 );
        for ( let k = end; k > start; k-- ) {
          const width = font.getGlyphHAdvance( info[ k - 1 ].codepoint );
          let repeat = 1;
          if ( stchAction( info[ k - 1 ] ) === STCH_REPEATING ) repeat += nCopies;
          pos[ k - 1 ].xAdvance = 0;
          for ( let n = 0; n < repeat; n++ ) {
            if ( rtl ) {
              xOffset = sSub( xOffset, width );
              if ( n > 0 ) xOffset = sAdd( xOffset, extraRepeatOverlap );
            }
            pos[ k - 1 ].xOffset = xOffset;
            /* Append a copy.  These must be fresh records: every repeat of a
             * tile carries its own offset, and sharing one object would give
             * them all the last one. */
            --j;
            info[j] = Object.assign( {}, info[ k - 1 ] );
            pos[j]  = Object.assign( {}, pos[ k - 1 ] );
            if ( !rtl ) {
              xOffset = sAdd( xOffset, width );
              if ( n > 0 ) xOffset = sSub( xOffset, extraRepeatOverlap );
            }
          }
        }
      }
      i = start;
    }
    if ( step === 1 ) {
      buffer.len = newLen;
      info.length = newLen;
      pos.length = newLen;
    }
  }

  if ( !rtl ) buffer.reverse();
}

function arabicPostprocessGlyphs( plan, buffer, font ) {
  if ( process.env.W18_NO_STCH ) return;
  applyStch( plan, buffer, font );
}

export const arabicShaper = {
  name: 'arabic',
  collectFeatures: arabicCollectFeatures,
  setupMasks: arabicSetupMasks,
  reorderMarks: arabicReorderMarks,
  postprocessGlyphs: arabicPostprocessGlyphs,
  normalizationPreference: 'default',
  zeroWidthMarks: 'by-gdef-late',
  fallbackPosition: true,
  gposTag: null,
};

/* ------------------------------------------------------------------ */
/*  Dispatch                                                           */
/* ------------------------------------------------------------------ */

/* Every script hb_ot_shaper_categorize() routes to the Arabic shaper, not just
 * Arabic itself: the cursive scripts share the isol/fina/medi/init joining
 * machinery and the joining types in ../ucd/ArabicShaping.txt already describe
 * all of them (Adlam's D/R/T entries, Phags-pa's, Mongolian's).  Measured:
 * with these on the default path, phags-pa shaped uniA85F/uniA861/uniA849
 * unjoined in EVERY position and Adlam produced no .init/.medi/.fina form at
 * all, because nothing ever set the joining masks.
 *
 * The condition is the reference's own: a non-Arabic cursive script falls back
 * to the DEFAULT shaper -- not to USE, and not to this one -- when GSUB names
 * no script for the run, and every one of them falls back when the run is not
 * horizontal. */
const ARABIC_SCRIPTS = new Set( [
  'Arab',                       /* Unicode 1.1 */
  'Mong', 'Syrc',               /* Unicode 3.0 */
  'Nkoo', 'Phag',               /* Unicode 5.0 */
  'Mand',                       /* Unicode 6.0 */
  'Mani', 'Phlp',               /* Unicode 7.0 */
  'Adlm',                       /* Unicode 9.0 */
  'Rohg', 'Sogd',               /* Unicode 11.0 */
  'Chrs',                       /* Unicode 12.0 */
  'Ougr',                       /* Unicode 14.0 */
  'Gara',                       /* Unicode 16.0 */
] );

export function shaperForScript( scriptTag, ctx ) {
  if ( scriptTag === 'Thai' || scriptTag === 'Laoo' ) return thaiShaper;
  if ( ARABIC_SCRIPTS.has( scriptTag ) ) {
    if ( process.env.W14_NO_CURSIVE_SCRIPTS && scriptTag !== 'Arab' ) return null;
    const horizontal = !ctx || ctx.horizontal !== false;
    const found = !ctx || ctx.gsubScriptFound !== false;
    return ( horizontal && ( scriptTag === 'Arab' || found ) ) ? arabicShaper : null;
  }
  return null;
}

export default thaiShaper;
