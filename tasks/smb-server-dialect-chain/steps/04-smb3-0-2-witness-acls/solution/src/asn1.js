'use strict';
// Minimal DER encoder/decoder for the SPNEGO tokens of [MS-SPNG]/RFC 4178.

function encLen(n) {
  if (n < 0x80) return Buffer.from([n]);
  const b = [];
  let v = n;
  while (v > 0) { b.unshift(v & 0xff); v >>>= 8; }
  return Buffer.from([0x80 | b.length, ...b]);
}
function tlv(tag, payload) {
  const p = Buffer.isBuffer(payload) ? payload : Buffer.concat(payload);
  return Buffer.concat([Buffer.from([tag]), encLen(p.length), p]);
}
function oid(dotted) {
  const parts = dotted.split('.').map(Number);
  const out = [parts[0] * 40 + parts[1]];
  for (let i = 2; i < parts.length; i++) {
    let v = parts[i];
    const chunk = [v & 0x7f];
    v >>>= 7;
    while (v > 0) { chunk.unshift((v & 0x7f) | 0x80); v >>>= 7; }
    out.push(...chunk);
  }
  return tlv(0x06, Buffer.from(out));
}
function octets(buf) { return tlv(0x04, buf); }
function enumerated(v) { return tlv(0x0a, Buffer.from([v])); }
function seq(items) { return tlv(0x30, items); }
function ctx(n, items) { return tlv(0xa0 | n, items); }

function decode(buf, pos = 0) {
  if (pos >= buf.length) return null;
  const tag = buf[pos++];
  let len = buf[pos++];
  if (len & 0x80) {
    const n = len & 0x7f;
    len = 0;
    for (let i = 0; i < n; i++) len = (len << 8) | buf[pos++];
  }
  const value = buf.subarray(pos, pos + len);
  return { tag, len, value, end: pos + len };
}
function* children(buf) {
  let pos = 0;
  while (pos < buf.length) {
    const n = decode(buf, pos);
    if (!n) break;
    yield n;
    pos = n.end;
  }
}
function decodeOid(value) {
  const first = value[0];
  const parts = [Math.floor(first / 40), first % 40];
  let v = 0;
  for (let i = 1; i < value.length; i++) {
    v = (v << 7) | (value[i] & 0x7f);
    if (!(value[i] & 0x80)) { parts.push(v); v = 0; }
  }
  return parts.join('.');
}

module.exports = { tlv, oid, octets, enumerated, seq, ctx, decode, children, decodeOid, encLen };
