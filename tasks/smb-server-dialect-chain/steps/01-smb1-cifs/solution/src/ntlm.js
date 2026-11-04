'use strict';
// NTLMSSP ([MS-NLMP]) inside SPNEGO ([MS-SPNG]).
const crypto = require('crypto');
const asn1 = require('./asn1');
const { rc4, hmacMd5, md4 } = require('./crypt');

const OID_SPNEGO = '1.3.6.1.5.5.2';
const OID_NTLMSSP = '1.3.6.1.4.1.311.2.2.10';
const SIG = Buffer.from('NTLMSSP\0', 'latin1');

const F = {
  NEGOTIATE_UNICODE: 0x00000001,
  NEGOTIATE_OEM: 0x00000002,
  REQUEST_TARGET: 0x00000004,
  NEGOTIATE_SIGN: 0x00000010,
  NEGOTIATE_SEAL: 0x00000020,
  NEGOTIATE_NTLM: 0x00000200,
  NEGOTIATE_ALWAYS_SIGN: 0x00008000,
  TARGET_TYPE_DOMAIN: 0x00010000,
  TARGET_TYPE_SERVER: 0x00020000,
  NEGOTIATE_EXTENDED_SESSIONSECURITY: 0x00080000,
  NEGOTIATE_TARGET_INFO: 0x00800000,
  NEGOTIATE_VERSION: 0x02000000,
  NEGOTIATE_128: 0x20000000,
  NEGOTIATE_KEY_EXCH: 0x40000000,
  NEGOTIATE_56: 0x80000000,
};

const uni = (s) => Buffer.from(s, 'utf16le');

function avPair(id, data) {
  const b = Buffer.alloc(4 + data.length);
  b.writeUInt16LE(id, 0); b.writeUInt16LE(data.length, 2);
  data.copy(b, 4);
  return b;
}
function filetimeNow() {
  const b = Buffer.alloc(8);
  b.writeBigUInt64LE(BigInt(Date.now()) * 10000n + 116444736000000000n);
  return b;
}

function buildChallenge(netbiosName, dnsName, challenge, negFlags) {
  const target = uni(netbiosName);
  const info = Buffer.concat([
    avPair(2, uni(netbiosName)),      // MsvAvNbDomainName
    avPair(1, uni(netbiosName)),      // MsvAvNbComputerName
    avPair(4, uni(dnsName)),          // MsvAvDnsDomainName
    avPair(3, uni(dnsName)),          // MsvAvDnsComputerName
    avPair(7, filetimeNow()),         // MsvAvTimestamp
    avPair(0, Buffer.alloc(0)),       // MsvAvEOL
  ]);
  const hdr = Buffer.alloc(56);
  SIG.copy(hdr, 0);
  hdr.writeUInt32LE(2, 8);
  const targetOff = 56, infoOff = 56 + target.length;
  hdr.writeUInt16LE(target.length, 12); hdr.writeUInt16LE(target.length, 14); hdr.writeUInt32LE(targetOff, 16);
  hdr.writeUInt32LE(negFlags >>> 0, 20);
  challenge.copy(hdr, 24);
  hdr.writeUInt16LE(info.length, 40); hdr.writeUInt16LE(info.length, 42); hdr.writeUInt32LE(infoOff, 44);
  // Version: 6.1 build 7601, NTLM revision 15
  hdr.writeUInt8(6, 48); hdr.writeUInt8(1, 49); hdr.writeUInt16LE(7601, 50); hdr.writeUInt8(15, 55);
  return Buffer.concat([hdr, target, info]);
}

function readField(buf, off) {
  const len = buf.readUInt16LE(off);
  const pos = buf.readUInt32LE(off + 4);
  if (pos + len > buf.length) return Buffer.alloc(0);
  return buf.subarray(pos, pos + len);
}

function parseAuthenticate(buf) {
  if (buf.length < 52 || !buf.subarray(0, 8).equals(SIG) || buf.readUInt32LE(8) !== 3) return null;
  const lm = readField(buf, 12);
  const nt = readField(buf, 20);
  const domain = readField(buf, 28);
  const user = readField(buf, 36);
  const workstation = readField(buf, 44);
  const sessionKey = buf.length >= 60 ? readField(buf, 52) : Buffer.alloc(0);
  const flags = buf.length >= 64 ? buf.readUInt32LE(60) : 0;
  let mic = null;
  const minOff = Math.min(
    ...[12, 20, 28, 36, 44, 52].map((o) => (buf.length > o + 7 ? buf.readUInt32LE(o + 4) : 0xffff))
      .filter((v) => v > 0)
  );
  if (buf.length >= 88 && minOff >= 88) mic = buf.subarray(72, 88);
  return {
    lm, nt, domain: domain.toString('utf16le'), user: user.toString('utf16le'),
    workstation: workstation.toString('utf16le'), sessionKey, flags, mic,
  };
}

function ntowfv2(ntHash, user, domain) {
  return hmacMd5(ntHash, uni(user.toUpperCase() + domain));
}

// Returns {ok, sessionKey} for an NTLMv2 (or NTLMv1 ESS) AUTHENTICATE_MESSAGE.
function verify(auth, challenge, ntHash, domainHint) {
  const nt = auth.nt;
  let base = null;
  if (nt.length > 24) {
    const proof = nt.subarray(0, 16);
    const temp = nt.subarray(16);
    for (const dom of [auth.domain, domainHint || '', '']) {
      const k = ntowfv2(ntHash, auth.user, dom);
      const want = hmacMd5(k, Buffer.concat([challenge, temp]));
      if (want.equals(proof)) { base = hmacMd5(k, proof); break; }
    }
  } else if (nt.length === 24) {
    // NTLMv1 with extended session security: NTProof = DESL(NTHash, MD5(chal|clientchal)[0:8])
    return { ok: false, sessionKey: null, reason: 'ntlmv1-unsupported' };
  }
  if (!base) return { ok: false, sessionKey: null, reason: 'bad-password' };
  let key = base;
  if (auth.flags & F.NEGOTIATE_KEY_EXCH && auth.sessionKey.length === 16) {
    key = rc4(base, auth.sessionKey);
  }
  return { ok: true, sessionKey: key };
}

// --- SPNEGO wrapping -------------------------------------------------------

function spnegoInit(mechs) {
  const mechList = asn1.seq(mechs.map((m) => asn1.oid(m)));
  const inner = asn1.ctx(0, [asn1.seq([asn1.ctx(0, [mechList])])]);
  return asn1.tlv(0x60, [asn1.oid(OID_SPNEGO), inner]);
}
function spnegoResp(negState, supportedMech, responseToken, mic) {
  const items = [];
  if (negState !== null && negState !== undefined) items.push(asn1.ctx(0, [asn1.enumerated(negState)]));
  if (supportedMech) items.push(asn1.ctx(1, [asn1.oid(supportedMech)]));
  if (responseToken) items.push(asn1.ctx(2, [asn1.octets(responseToken)]));
  if (mic) items.push(asn1.ctx(3, [asn1.octets(mic)]));
  return asn1.ctx(1, [asn1.seq(items)]);
}

// Pull the NTLMSSP message out of a raw or SPNEGO-wrapped blob.
function unwrap(blob) {
  if (!blob || blob.length === 0) return { kind: 'empty' };
  if (blob.length >= 8 && blob.subarray(0, 8).equals(SIG)) {
    return { kind: 'raw', token: blob, mechList: null };
  }
  const top = asn1.decode(blob, 0);
  if (!top) return { kind: 'unknown' };
  if (top.tag === 0x60) {
    // GSSAPI InitialContextToken: OID then NegotiationToken
    let mechListDer = null, token = null;
    for (const c of asn1.children(top.value)) {
      if (c.tag === 0xa0) {
        const s = asn1.decode(c.value, 0);
        for (const f of asn1.children(s.value)) {
          if (f.tag === 0xa0) { mechListDer = f.value; }
          if (f.tag === 0xa2) { const o = asn1.decode(f.value, 0); token = o.value; }
        }
      }
    }
    return { kind: 'spnego-init', token, mechListDer };
  }
  if (top.tag === 0xa1) {
    const s = asn1.decode(top.value, 0);
    let token = null, mic = null, negState = null;
    for (const f of asn1.children(s.value)) {
      if (f.tag === 0xa0) { const e = asn1.decode(f.value, 0); negState = e.value[0]; }
      if (f.tag === 0xa2) { const o = asn1.decode(f.value, 0); token = o.value; }
      if (f.tag === 0xa3) { const o = asn1.decode(f.value, 0); mic = o.value; }
    }
    return { kind: 'spnego-resp', token, mic, negState };
  }
  return { kind: 'unknown' };
}

module.exports = {
  F, OID_SPNEGO, OID_NTLMSSP, SIG, uni, buildChallenge, parseAuthenticate,
  verify, ntowfv2, spnegoInit, spnegoResp, unwrap, md4,
};
