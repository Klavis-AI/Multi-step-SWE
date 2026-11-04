'use strict';
// Primitives the SMB stack needs that node:crypto does not provide directly.
const crypto = require('crypto');

// --- RC4 (NTLMSSP key exchange); OpenSSL 3 keeps RC4 in the legacy provider.
function rc4(key, data) {
  const s = new Uint8Array(256);
  for (let i = 0; i < 256; i++) s[i] = i;
  let j = 0;
  for (let i = 0; i < 256; i++) {
    j = (j + s[i] + key[i % key.length]) & 0xff;
    const t = s[i]; s[i] = s[j]; s[j] = t;
  }
  const out = Buffer.alloc(data.length);
  let i = 0; j = 0;
  for (let n = 0; n < data.length; n++) {
    i = (i + 1) & 0xff;
    j = (j + s[i]) & 0xff;
    const t = s[i]; s[i] = s[j]; s[j] = t;
    out[n] = data[n] ^ s[(s[i] + s[j]) & 0xff];
  }
  return out;
}

// --- MD4 (NT hash), removed from OpenSSL 3's default provider.
function md4(data) {
  const msg = Buffer.from(data);
  const bitLen = BigInt(msg.length) * 8n;
  const padLen = ((msg.length + 8) % 64 === 0) ? 0 : 64 - ((msg.length + 8) % 64);
  const buf = Buffer.concat([msg, Buffer.from([0x80]), Buffer.alloc(padLen === 0 ? 63 : padLen - 1), Buffer.alloc(8)]);
  buf.writeUInt32LE(Number(bitLen & 0xffffffffn), buf.length - 8);
  buf.writeUInt32LE(Number((bitLen >> 32n) & 0xffffffffn), buf.length - 4);
  let [a, b, c, d] = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476];
  const rol = (x, n) => ((x << n) | (x >>> (32 - n))) >>> 0;
  for (let off = 0; off < buf.length; off += 64) {
    const X = new Array(16);
    for (let i = 0; i < 16; i++) X[i] = buf.readUInt32LE(off + i * 4);
    const [aa, bb, cc, dd] = [a, b, c, d];
    const F = (x, y, z) => (x & y) | (~x & z);
    const G = (x, y, z) => (x & y) | (x & z) | (y & z);
    const H = (x, y, z) => x ^ y ^ z;
    const r1 = [3, 7, 11, 19];
    for (let i = 0; i < 16; i++) {
      const k = i, s = r1[i % 4];
      if (i % 4 === 0) a = rol((a + F(b, c, d) + X[k]) >>> 0, s);
      else if (i % 4 === 1) d = rol((d + F(a, b, c) + X[k]) >>> 0, s);
      else if (i % 4 === 2) c = rol((c + F(d, a, b) + X[k]) >>> 0, s);
      else b = rol((b + F(c, d, a) + X[k]) >>> 0, s);
    }
    const r2 = [3, 5, 9, 13];
    const o2 = [0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15];
    for (let i = 0; i < 16; i++) {
      const k = o2[i], s = r2[i % 4];
      if (i % 4 === 0) a = rol((a + G(b, c, d) + X[k] + 0x5a827999) >>> 0, s);
      else if (i % 4 === 1) d = rol((d + G(a, b, c) + X[k] + 0x5a827999) >>> 0, s);
      else if (i % 4 === 2) c = rol((c + G(d, a, b) + X[k] + 0x5a827999) >>> 0, s);
      else b = rol((b + G(c, d, a) + X[k] + 0x5a827999) >>> 0, s);
    }
    const r3 = [3, 9, 11, 15];
    const o3 = [0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15];
    for (let i = 0; i < 16; i++) {
      const k = o3[i], s = r3[i % 4];
      if (i % 4 === 0) a = rol((a + H(b, c, d) + X[k] + 0x6ed9eba1) >>> 0, s);
      else if (i % 4 === 1) d = rol((d + H(a, b, c) + X[k] + 0x6ed9eba1) >>> 0, s);
      else if (i % 4 === 2) c = rol((c + H(d, a, b) + X[k] + 0x6ed9eba1) >>> 0, s);
      else b = rol((b + H(c, d, a) + X[k] + 0x6ed9eba1) >>> 0, s);
    }
    a = (a + aa) >>> 0; b = (b + bb) >>> 0; c = (c + cc) >>> 0; d = (d + dd) >>> 0;
  }
  const out = Buffer.alloc(16);
  out.writeUInt32LE(a, 0); out.writeUInt32LE(b, 4); out.writeUInt32LE(c, 8); out.writeUInt32LE(d, 12);
  return out;
}

const hmacMd5 = (key, data) => crypto.createHmac('md5', key).update(data).digest();
const hmacSha256 = (key, data) => crypto.createHmac('sha256', key).update(data).digest();
const sha512 = (data) => crypto.createHash('sha512').update(data).digest();

// --- AES-CMAC (RFC 4493): SMB 3.0/3.0.2 signing. Not in node:crypto.
function aesEcbBlock(key, block) {
  const c = crypto.createCipheriv('aes-128-ecb', key, null);
  c.setAutoPadding(false);
  return Buffer.concat([c.update(block), c.final()]);
}
function shiftLeft1(buf) {
  const out = Buffer.alloc(buf.length);
  let carry = 0;
  for (let i = buf.length - 1; i >= 0; i--) {
    out[i] = ((buf[i] << 1) & 0xff) | carry;
    carry = (buf[i] & 0x80) ? 1 : 0;
  }
  return out;
}
function cmacSubkeys(key) {
  const L = aesEcbBlock(key, Buffer.alloc(16));
  let K1 = shiftLeft1(L);
  if (L[0] & 0x80) K1[15] ^= 0x87;
  let K2 = shiftLeft1(K1);
  if (K1[0] & 0x80) K2[15] ^= 0x87;
  return [K1, K2];
}
function aesCmac(key, msg) {
  const [K1, K2] = cmacSubkeys(key);
  const n = Math.ceil(msg.length / 16);
  let last;
  const complete = (n > 0) && (msg.length % 16 === 0);
  if (n === 0) {
    last = Buffer.concat([Buffer.from([0x80]), Buffer.alloc(15)]);
    for (let i = 0; i < 16; i++) last[i] ^= K2[i];
  } else if (complete) {
    last = Buffer.from(msg.subarray((n - 1) * 16, n * 16));
    for (let i = 0; i < 16; i++) last[i] ^= K1[i];
  } else {
    const rest = msg.subarray((n - 1) * 16);
    last = Buffer.concat([rest, Buffer.from([0x80]), Buffer.alloc(15 - rest.length)]);
    for (let i = 0; i < 16; i++) last[i] ^= K2[i];
  }
  let X = Buffer.alloc(16);
  const blocks = Math.max(n - 1, 0);
  for (let i = 0; i < blocks; i++) {
    const Y = Buffer.alloc(16);
    for (let j = 0; j < 16; j++) Y[j] = X[j] ^ msg[i * 16 + j];
    X = aesEcbBlock(key, Y);
  }
  const Y = Buffer.alloc(16);
  for (let j = 0; j < 16; j++) Y[j] = X[j] ^ last[j];
  return aesEcbBlock(key, Y);
}

// --- AES-128-GMAC over AAD only: SMB 3.1.1 AES-GMAC signing.
function aesGmac(key, nonce, msg) {
  const c = crypto.createCipheriv('aes-128-gcm', key, nonce, { authTagLength: 16 });
  c.setAAD(msg);
  c.final();
  return c.getAuthTag();
}

// --- SP800-108 CTR-mode KDF with HMAC-SHA256 ([MS-SMB2] 3.1.4.2).
function kdfCounter(key, label, context, lenBits = 128) {
  const buf = Buffer.concat([
    Buffer.from([0, 0, 0, 1]),
    Buffer.from(label), Buffer.from([0]),
    Buffer.from(context),
    Buffer.from([0, 0, (lenBits >> 8) & 0xff, lenBits & 0xff]),
  ]);
  return hmacSha256(key, buf).subarray(0, lenBits / 8);
}

module.exports = { rc4, md4, hmacMd5, hmacSha256, sha512, aesCmac, aesGmac, kdfCounter, aesEcbBlock };
