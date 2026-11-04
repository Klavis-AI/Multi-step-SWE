#!/usr/bin/env node
/****************************************************************************
 *
 * bin/hb-subset.js  --  W4
 *
 *   Port of HarfBuzz 14.4.0's `hb-subset' tool.
 *
 *     util/hb-subset.cc          main, --batch
 *     src/hb-subset-input.cc     the option set and its defaults
 *     src/hb-subset.cc           hb_subset_or_fail
 *
 *   Option syntax follows spec/hb-subset-help.txt.  The set-valued options
 *   (--unicodes, --gids, --layout-features, --drop-tables, --name-IDs,
 *   --layout-scripts) accept `*' for "everything" and a `-=' spelling that
 *   subtracts from the current set.
 */

'use strict';

import { readFileSync, writeFileSync } from 'node:fs';
import process from 'node:process';
import { subsetFont } from '../subset.js';

function parseNumList( s, hex ) {
  /* "0-5,7,10-12" or "U+0041,U+0042" or "*" */
  if ( s === '*' ) return '*';
  const out = new Set();
  for ( let part of s.split( /[,\s]+/ ) ) {
    if ( !part ) continue;
    part = part.trim();
    const m = part.match( /^(?:U\+)?([0-9A-Fa-f]+)\s*-\s*(?:U\+)?([0-9A-Fa-f]+)$/ );
    if ( m ) {
      const a = parseInt( m[1], hex ? 16 : 10 ), b = parseInt( m[2], hex ? 16 : 10 );
      for ( let v = a; v <= b; v++ ) out.add( v );
      continue;
    }
    const one = part.replace( /^U\+/i, '' );
    const v = parseInt( one, hex ? 16 : 10 );
    if ( !Number.isNaN( v ) ) out.add( v );
  }
  return out;
}

function parseTagList( s ) {
  if ( s === '*' ) return '*';
  const out = new Set();
  for ( const t of s.split( /[,\s]+/ ) ) if ( t ) out.add( ( t + '    ' ).slice( 0, 4 ) );
  return out;
}

/* A table tag as written on the command line is padded to 4 chars, so
 * `--drop-tables=cvt' means the table `cvt '. */
export function parseArgs( argv ) {
  const o = {
    unicodes: null, unicodesMinus: null, gids: null, text: '',
    layoutFeatures: null, dropTables: null, nameIds: null,
    noHinting: false, retainGids: false, glyphNames: false,
    nameLegacy: false, noPruneUnicodeRanges: false, passthrough: false,
    variations: null, output: null, fontFile: null, faceIndex: 0,
    batch: false, positional: [],
  };
  for ( let i = 0; i < argv.length; i++ ) {
    const a = argv[i];
    const eq = a.indexOf( '=' );
    const key = eq >= 0 ? a.slice( 0, eq ) : a;
    const val = eq >= 0 ? a.slice( eq + 1 ) : null;
    const need = () => ( val !== null ? val : argv[++i] );
    switch ( key ) {
      case '--unicodes': case '-u': o.unicodes = parseNumList( need(), true ); break;
      case '--unicodes-': o.unicodesMinus = parseNumList( need(), true ); break;
      case '--gids': case '-g': o.gids = parseNumList( need(), false ); break;
      case '--text': case '-t': o.text += need(); break;
      case '--layout-features': o.layoutFeatures = parseTagList( need() ); break;
      case '--drop-tables': o.dropTables = parseTagList( need() ); break;
      case '--name-IDs': o.nameIds = parseNumList( need(), false ); break;
      case '--no-hinting': o.noHinting = true; break;
      case '--retain-gids': o.retainGids = true; break;
      case '--glyph-names': o.glyphNames = true; break;
      case '--name-legacy': o.nameLegacy = true; break;
      case '--no-prune-unicode-ranges': o.noPruneUnicodeRanges = true; break;
      case '--passthrough-tables': o.passthrough = true; break;
      case '--variations': o.variations = need(); break;
      case '--output-file': case '-o': o.output = need(); break;
      case '--font-file': o.fontFile = need(); break;
      case '--face-index': case '-y': o.faceIndex = parseInt( need(), 10 ) || 0; break;
      case '--batch': o.batch = true; break;
      case '--num-iterations': case '-n': need(); break;
      case '--help': case '-h': o.help = true; break;
      default:
        if ( a.startsWith( '-' ) ) break;      /* ignore unknown flags, as hb does */
        o.positional.push( a );
    }
  }
  if ( !o.fontFile && o.positional.length ) o.fontFile = o.positional.shift();
  if ( o.positional.length && !o.text ) o.text = o.positional.join( ' ' );
  return o;
}

function runOne( o ) {
  const data = new Uint8Array( readFileSync( o.fontFile ) );
  /* hb-subset's default when nothing is asked for is the empty glyph set. */
  const out = subsetFont( data, o );
  if ( o.output && o.output !== '-' ) writeFileSync( o.output, out );
  else process.stdout.write( Buffer.from( out ) );
}

function main() {
  const argv = process.argv.slice( 2 );
  const o = parseArgs( argv );
  if ( o.help ) { process.stdout.write( 'Usage:\n  hb-subset [OPTION?] [FONT-FILE] [TEXT]\n' ); return; }
  if ( o.batch ) {
    const lines = readFileSync( 0, 'utf8' ).split( '\n' );
    for ( const line of lines ) {
      const t = line.trim();
      if ( !t ) continue;
      const parts = t.match( /(?:[^\s"]+|"[^"]*")+/g ) || [];
      const oo = parseArgs( parts.map( p => p.replace( /^"|"$/g, '' ) ) );
      try { runOne( oo ); } catch ( e ) { process.stderr.write( String( e && e.message ) + '\n' ); }
    }
    return;
  }
  if ( !o.fontFile ) { process.stderr.write( 'hb-subset: no font file\n' ); process.exit( 1 ); }
  runOne( o );
}

/* Only act as a tool when invoked directly; importing this module (the
 * selftest does) must not run a subset. */
import { pathToFileURL } from 'node:url';
if ( process.argv[1] && import.meta.url === pathToFileURL( process.argv[1] ).href ) main();
