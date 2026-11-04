'use strict';
// smb.conf(5) parser: sections, `key = value`, continuation lines, include,
// comments (# and ;), whitespace/case-insensitive parameter names.
const fs = require('fs');
const path = require('path');

const DIALECTS = {
  CORE: 0x0001, COREPLUS: 0x0002, LANMAN1: 0x0003, LANMAN2: 0x0004,
  NT1: 0x0005, SMB1: 0x0005, SMB2_02: 0x0202, SMB2_10: 0x0210, SMB2: 0x0210,
  SMB2_22: 0x0222, SMB2_24: 0x0224, SMB3_00: 0x0300, SMB3: 0x0300,
  SMB3_02: 0x0302, SMB3_10: 0x0310, SMB3_11: 0x0311,
};

function normKey(k) { return k.toLowerCase().replace(/\s+/g, ' ').trim(); }

function parseBool(v, dflt = false) {
  if (v === undefined || v === null) return dflt;
  const s = String(v).trim().toLowerCase();
  if (['yes', 'true', '1', 'on', 'y'].includes(s)) return true;
  if (['no', 'false', '0', 'off', 'n'].includes(s)) return false;
  return dflt;
}

class Conf {
  constructor() { this.sections = new Map(); this.order = []; }
  section(name) {
    const k = name.toLowerCase();
    if (!this.sections.has(k)) { this.sections.set(k, { name, params: new Map() }); this.order.push(k); }
    return this.sections.get(k);
  }
  set(sect, key, val) { this.section(sect).params.set(normKey(key), val); }
  // Look up `key` in `sect`, falling back to [global].
  get(sect, key, dflt) {
    const k = normKey(key);
    const s = this.sections.get(String(sect).toLowerCase());
    if (s && s.params.has(k)) return s.params.get(k);
    const g = this.sections.get('global');
    if (g && g.params.has(k)) return g.params.get(k);
    return dflt;
  }
  getBool(sect, key, dflt) { return parseBool(this.get(sect, key, undefined), dflt); }
  getInt(sect, key, dflt) {
    const v = this.get(sect, key, undefined);
    if (v === undefined) return dflt;
    const n = parseInt(String(v).trim(), 10);
    return Number.isFinite(n) ? n : dflt;
  }
  shares() {
    return this.order.filter((k) => k !== 'global' && k !== 'default')
      .map((k) => this.sections.get(k));
  }
  hasShare(name) {
    const k = String(name).toLowerCase();
    return k !== 'global' && k !== 'default' && this.sections.has(k);
  }
}

function loadInto(conf, file, seen) {
  const real = path.resolve(file);
  if (seen.has(real)) return;
  seen.add(real);
  let text;
  try { text = fs.readFileSync(real, 'utf8'); } catch (e) { return; }
  const lines = text.split(/\r?\n/);
  let cur = 'global';
  for (let i = 0; i < lines.length; i++) {
    let line = lines[i];
    while (line.endsWith('\\') && i + 1 < lines.length) { line = line.slice(0, -1) + lines[++i]; }
    line = line.trim();
    if (!line || line.startsWith('#') || line.startsWith(';')) continue;
    const m = /^\[(.*)\]$/.exec(line);
    if (m) { cur = m[1].trim(); conf.section(cur); continue; }
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const key = normKey(line.slice(0, eq));
    const val = line.slice(eq + 1).trim();
    if (key === 'include') { loadInto(conf, val, seen); continue; }
    conf.set(cur, key, val);
  }
}

function load(file, overrides = []) {
  const conf = new Conf();
  conf.section('global');
  loadInto(conf, file, new Set());
  for (const o of overrides) {
    const eq = o.indexOf('=');
    if (eq < 0) continue;
    let key = o.slice(0, eq).trim();
    const val = o.slice(eq + 1).trim();
    let sect = 'global';
    const colon = key.indexOf(':');
    if (colon > 0 && conf.hasShare(key.slice(0, colon))) { sect = key.slice(0, colon); key = key.slice(colon + 1); }
    conf.set(sect, key, val);
  }
  return conf;
}

function dialectValue(name, dflt) {
  if (name === undefined || name === null) return dflt;
  const k = String(name).trim().toUpperCase().replace(/[ -]/g, '_');
  return DIALECTS[k] !== undefined ? DIALECTS[k] : dflt;
}

module.exports = { load, Conf, DIALECTS, dialectValue, parseBool, normKey };
