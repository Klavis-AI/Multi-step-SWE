'use strict';
// Connection-oriented DCE/RPC as carried over an SMB named pipe.
//
// Wire formats come from [C706] chapter 12 (the connection-oriented PDU
// encodings) with the extensions in [MS-RPCE] sections 2.2.2 and 3.3.1.5.
// This module knows nothing about SMB: every entry point takes and returns
// Buffers that hold whole PDUs, exactly the bytes that travel in the data
// section of a pipe WRITE / READ or of FSCTL_PIPE_TRANSCEIVE.
//
// Only the standard library is used (no require at all, in fact).

// --- PDU types ([C706] 12.1.2.1, PTYPE field) -------------------------------
const PTYPE = {
  REQUEST: 0, PING: 1, RESPONSE: 2, FAULT: 3, WORKING: 4, NOCALL: 5,
  REJECT: 6, ACK: 7, CL_CANCEL: 8, FACK: 9, CANCEL_ACK: 10,
  BIND: 11, BIND_ACK: 12, BIND_NAK: 13, ALTER_CONTEXT: 14,
  ALTER_CONTEXT_RESP: 15, AUTH3: 16, SHUTDOWN: 17, CO_CANCEL: 18,
  ORPHANED: 19, RTS: 20,
};

// --- pfc_flags ([C706] 12.6.3.1) --------------------------------------------
const PFC = {
  FIRST_FRAG: 0x01,
  LAST_FRAG: 0x02,
  PENDING_CANCEL: 0x04,      // == PFC_SUPPORT_HEADER_SIGN on bind/alter_context
  RESERVED_1: 0x08,
  CONC_MPX: 0x10,
  DID_NOT_EXECUTE: 0x20,
  MAYBE: 0x40,
  OBJECT_UUID: 0x80,
};

// --- p_cont_def_result_t ([C706] 12.6.3.1, [MS-RPCE] 2.2.2.4) ---------------
const RESULT = {
  ACCEPTANCE: 0, USER_REJECTION: 1, PROVIDER_REJECTION: 2, NEGOTIATE_ACK: 3,
};
// --- p_provider_reason_t ----------------------------------------------------
const REASON = {
  NOT_SPECIFIED: 0,
  ABSTRACT_SYNTAX_NOT_SUPPORTED: 1,
  PROPOSED_TRANSFER_SYNTAXES_NOT_SUPPORTED: 2,
  LOCAL_LIMIT_EXCEEDED: 3,
};
// --- p_reject_reason_t (bind_nak) ------------------------------------------
const NAK_REASON = {
  NOT_SPECIFIED: 0,
  TEMPORARY_CONGESTION: 1,
  LOCAL_LIMIT_EXCEEDED: 2,
  CALLED_PADDR_UNKNOWN: 3,
  PROTOCOL_VERSION_NOT_SUPPORTED: 4,
  DEFAULT_CONTEXT_NOT_SUPPORTED: 5,
  USER_DATA_NOT_READABLE: 6,
  NO_PSAP_AVAILABLE: 7,
};

// --- fault status codes ([C706] appendix E, [MS-RPCE] 3.1.1.5.5) ------------
const FAULT = {
  nca_s_fault_other: 0x00000001,
  nca_s_fault_ndr: 0x000006f7,          // RPC_S_INVALID_TAG / bad stub data
  nca_s_fault_int_div_by_zero: 0x1c000001,
  nca_s_fault_addr_error: 0x1c000002,
  nca_s_fault_context_mismatch: 0x1c00001a,
  nca_s_op_rng_error: 0x1c010002,       // RPC_S_PROCNUM_OUT_OF_RANGE
  nca_s_unk_if: 0x1c010003,             // RPC_S_UNKNOWN_IF
  nca_s_proto_error: 0x1c01000b,
  nca_s_out_args_too_big: 0x1c010013,
  nca_s_fault_remote_no_memory: 0x1c010017,
  nca_s_unsupported_type: 0x1c010017,
};

const HDR_LEN = 16;                     // common header, [C706] 12.6.3.1
const DREP_LE = Buffer.from([0x10, 0x00, 0x00, 0x00]); // little-endian, ASCII, IEEE

// Transfer syntaxes.
const NDR32_UUID = '8a885d04-1ceb-11c9-9fe8-08002b104860';
const NDR32_VERSION = 2;                // major 2, minor 0
const NDR64_UUID = '71710533-beba-4937-8319-b5dbef9ccc36';
const NDR64_VERSION = 1;
// [MS-RPCE] 3.3.1.5.3: bind time feature negotiation syntax, prefix + flags.
const BIND_TIME_FEATURE_PREFIX = '6cb71c2c-9812-4540';

// ---------------------------------------------------------------------------
// UUID helpers.  On the wire a uuid_t is data1 (LE u32), data2 (LE u16),
// data3 (LE u16), data4[8] (as-is) -- [MS-DTYP] 2.3.4.2 GUID.
// ---------------------------------------------------------------------------
function uuidToBuf(str) {
  const hex = String(str).replace(/[{}\-]/g, '');
  if (hex.length !== 32 || /[^0-9a-fA-F]/.test(hex)) throw new Error('bad uuid: ' + str);
  const f = Buffer.from(hex, 'hex');
  const b = Buffer.alloc(16);
  b.writeUInt32LE(f.readUInt32BE(0), 0);
  b.writeUInt16LE(f.readUInt16BE(4), 4);
  b.writeUInt16LE(f.readUInt16BE(6), 6);
  f.copy(b, 8, 8, 16);
  return b;
}
function bufToUuid(buf, off) {
  const o = off || 0;
  const p = (n, w) => n.toString(16).padStart(w, '0');
  return p(buf.readUInt32LE(o), 8) + '-' + p(buf.readUInt16LE(o + 4), 4) + '-' +
         p(buf.readUInt16LE(o + 6), 4) + '-' +
         buf.subarray(o + 8, o + 10).toString('hex') + '-' +
         buf.subarray(o + 10, o + 16).toString('hex');
}
// p_syntax_id_t: uuid_t if_uuid; unsigned32 if_version.  if_version packs the
// minor version in the high 16 bits and the major version in the low 16.
function synVersion(major, minor) { return (((minor >>> 0) << 16) | ((major >>> 0) & 0xffff)) >>> 0; }
function synMajor(v) { return v & 0xffff; }
function synMinor(v) { return (v >>> 16) & 0xffff; }
function buildSyntax(uuid, major, minor) {
  const b = Buffer.alloc(20);
  uuidToBuf(uuid).copy(b, 0);
  b.writeUInt32LE(synVersion(major, minor === undefined ? 0 : minor), 16);
  return b;
}
function parseSyntax(buf, off) {
  const v = buf.readUInt32LE(off + 16);
  return { uuid: bufToUuid(buf, off), version: v, major: synMajor(v), minor: synMinor(v) };
}

// ---------------------------------------------------------------------------
// Common header ([C706] 12.6.3.1)
// ---------------------------------------------------------------------------
function buildHeader(opts) {
  const b = Buffer.alloc(HDR_LEN);
  b.writeUInt8(opts.rpcVers === undefined ? 5 : opts.rpcVers, 0);
  b.writeUInt8(opts.rpcVersMinor === undefined ? 0 : opts.rpcVersMinor, 1);
  b.writeUInt8(opts.ptype & 0xff, 2);
  b.writeUInt8(opts.pfcFlags & 0xff, 3);
  (opts.drep || DREP_LE).copy(b, 4, 0, 4);
  b.writeUInt16LE(opts.fragLength & 0xffff, 8);
  b.writeUInt16LE((opts.authLength || 0) & 0xffff, 10);
  b.writeUInt32LE(opts.callId >>> 0, 12);
  return b;
}
function parseHeader(buf) {
  if (!Buffer.isBuffer(buf) || buf.length < HDR_LEN) return null;
  const drep = buf.subarray(4, 8);
  const h = {
    rpcVers: buf.readUInt8(0),
    rpcVersMinor: buf.readUInt8(1),
    ptype: buf.readUInt8(2),
    pfcFlags: buf.readUInt8(3),
    drep: Buffer.from(drep),
    littleEndian: ((drep[0] >> 4) & 0x0f) === 1,
    fragLength: buf.readUInt16LE(8),
    authLength: buf.readUInt16LE(10),
    callId: buf.readUInt32LE(12),
  };
  if (!h.littleEndian) {
    // Big-endian NDR from the caller is legal but nothing here produces or
    // consumes it; report it so the caller can answer nca_s_proto_error.
    h.fragLength = buf.readUInt16BE(8);
    h.authLength = buf.readUInt16BE(10);
    h.callId = buf.readUInt32BE(12);
  }
  return h;
}

// ---------------------------------------------------------------------------
// bind / alter_context ([C706] 12.6.4.3)
// ---------------------------------------------------------------------------
function parseBind(buf) {
  const h = parseHeader(buf);
  if (!h) return null;
  if (h.ptype !== PTYPE.BIND && h.ptype !== PTYPE.ALTER_CONTEXT) return null;
  if (buf.length < HDR_LEN + 12) return null;
  const out = {
    header: h,
    maxXmitFrag: buf.readUInt16LE(16),
    maxRecvFrag: buf.readUInt16LE(18),
    assocGroupId: buf.readUInt32LE(20),
    contexts: [],
  };
  const nCtx = buf.readUInt8(24);
  let p = 28;                                   // n_context_elem, reserved, reserved2
  for (let i = 0; i < nCtx; i++) {
    if (p + 24 > buf.length) return null;
    const contId = buf.readUInt16LE(p);
    const nXfer = buf.readUInt8(p + 2);
    const abstract = parseSyntax(buf, p + 4);
    p += 24;
    const xfers = [];
    for (let j = 0; j < nXfer; j++) {
      if (p + 20 > buf.length) return null;
      xfers.push(parseSyntax(buf, p));
      p += 20;
    }
    out.contexts.push({ contextId: contId, abstract, transferSyntaxes: xfers });
  }
  if (h.authLength) {
    const astart = h.fragLength - h.authLength - 8;
    if (astart >= 0 && astart + 8 + h.authLength <= buf.length) {
      out.auth = {
        type: buf.readUInt8(astart),
        level: buf.readUInt8(astart + 1),
        padLength: buf.readUInt8(astart + 2),
        contextId: buf.readUInt32LE(astart + 4),
        token: Buffer.from(buf.subarray(astart + 8, astart + 8 + h.authLength)),
      };
    }
  }
  return out;
}

// port_any_t sec_addr: u_int16 length; char port_spec[length] (NUL included).
function buildBindAck(opts) {
  const sec = Buffer.from(String(opts.secondaryAddress || '') + '\0', 'latin1');
  const results = opts.results || [];
  let len = HDR_LEN + 8 + 2 + sec.length;
  const pad = (4 - (len % 4)) % 4;                 // restore 4-octet alignment
  len += pad + 4 + results.length * 24;
  const b = Buffer.alloc(len);
  buildHeader({
    ptype: opts.ptype === undefined ? PTYPE.BIND_ACK : opts.ptype,
    pfcFlags: opts.pfcFlags === undefined ? (PFC.FIRST_FRAG | PFC.LAST_FRAG) : opts.pfcFlags,
    fragLength: len, authLength: 0, callId: opts.callId,
  }).copy(b, 0);
  b.writeUInt16LE(opts.maxXmitFrag & 0xffff, 16);
  b.writeUInt16LE(opts.maxRecvFrag & 0xffff, 18);
  b.writeUInt32LE(opts.assocGroupId >>> 0, 20);
  b.writeUInt16LE(sec.length, 24);
  sec.copy(b, 26);
  let p = 26 + sec.length + pad;
  b.writeUInt8(results.length, p);
  b.writeUInt8(0, p + 1);
  b.writeUInt16LE(0, p + 2);
  p += 4;
  for (const r of results) {
    b.writeUInt16LE(r.result >>> 0, p);
    b.writeUInt16LE(r.reason >>> 0, p + 2);
    if (r.transferSyntax) r.transferSyntax.copy(b, p + 4, 0, 20);
    p += 24;
  }
  return b;
}

function parseBindAck(buf) {
  const h = parseHeader(buf);
  if (!h || (h.ptype !== PTYPE.BIND_ACK && h.ptype !== PTYPE.ALTER_CONTEXT_RESP)) return null;
  const secLen = buf.readUInt16LE(24);
  let p = 26 + secLen;
  p += (4 - (p % 4)) % 4;
  const n = buf.readUInt8(p);
  const results = [];
  p += 4;
  for (let i = 0; i < n; i++) {
    results.push({
      result: buf.readUInt16LE(p),
      reason: buf.readUInt16LE(p + 2),
      transferSyntax: parseSyntax(buf, p + 4),
    });
    p += 24;
  }
  return {
    header: h,
    maxXmitFrag: buf.readUInt16LE(16),
    maxRecvFrag: buf.readUInt16LE(18),
    assocGroupId: buf.readUInt32LE(20),
    secondaryAddress: buf.subarray(26, 26 + Math.max(0, secLen - 1)).toString('latin1'),
    results,
  };
}

// bind_nak ([C706] 12.6.4.4): u_int16 reject_reason, then
// p_rt_versions_supported_t { u_int8 n_protocols; version_t p[n]; }.
function buildBindNak(opts) {
  const versions = opts.versions || [{ major: 5, minor: 0 }];
  let len = HDR_LEN + 2 + 1 + versions.length * 2;
  const pad = (4 - (len % 4)) % 4;
  len += pad;
  const b = Buffer.alloc(len);
  buildHeader({
    ptype: PTYPE.BIND_NAK, pfcFlags: PFC.FIRST_FRAG | PFC.LAST_FRAG,
    fragLength: len, authLength: 0, callId: opts.callId,
  }).copy(b, 0);
  b.writeUInt16LE((opts.reason || 0) >>> 0, 16);
  b.writeUInt8(versions.length, 18);
  let p = 19;
  for (const v of versions) { b.writeUInt8(v.major, p); b.writeUInt8(v.minor, p + 1); p += 2; }
  return b;
}

// ---------------------------------------------------------------------------
// request / response / fault ([C706] 12.6.4.9, 12.6.4.10, 12.6.4.7)
// ---------------------------------------------------------------------------
function parseRequest(buf) {
  const h = parseHeader(buf);
  if (!h || h.ptype !== PTYPE.REQUEST) return null;
  if (buf.length < HDR_LEN + 8) return null;
  const allocHint = buf.readUInt32LE(16);
  const contextId = buf.readUInt16LE(20);
  const opnum = buf.readUInt16LE(22);
  let p = 24;
  let object = null;
  if (h.pfcFlags & PFC.OBJECT_UUID) {
    if (buf.length < p + 16) return null;
    object = bufToUuid(buf, p);
    p += 16;
  }
  const end = Math.max(p, Math.min(buf.length, h.fragLength) - h.authLength -
                          (h.authLength ? 8 : 0));
  return {
    header: h, allocHint, contextId, opnum, object,
    stub: Buffer.from(buf.subarray(p, end)),
    first: !!(h.pfcFlags & PFC.FIRST_FRAG),
    last: !!(h.pfcFlags & PFC.LAST_FRAG),
  };
}

function buildRequest(opts) {
  const stub = opts.stub || Buffer.alloc(0);
  const obj = opts.object ? uuidToBuf(opts.object) : null;
  const len = HDR_LEN + 8 + (obj ? 16 : 0) + stub.length;
  const b = Buffer.alloc(len);
  let flags = opts.pfcFlags === undefined ? (PFC.FIRST_FRAG | PFC.LAST_FRAG) : opts.pfcFlags;
  if (obj) flags |= PFC.OBJECT_UUID;
  buildHeader({
    ptype: PTYPE.REQUEST, pfcFlags: flags, fragLength: len,
    authLength: 0, callId: opts.callId,
  }).copy(b, 0);
  b.writeUInt32LE(opts.allocHint === undefined ? stub.length : opts.allocHint >>> 0, 16);
  b.writeUInt16LE((opts.contextId || 0) & 0xffff, 20);
  b.writeUInt16LE(opts.opnum & 0xffff, 22);
  let p = 24;
  if (obj) { obj.copy(b, p); p += 16; }
  stub.copy(b, p);
  return b;
}

function buildResponse(opts) {
  const stub = opts.stub || Buffer.alloc(0);
  const len = HDR_LEN + 8 + stub.length;
  const b = Buffer.alloc(len);
  buildHeader({
    ptype: PTYPE.RESPONSE,
    pfcFlags: opts.pfcFlags === undefined ? (PFC.FIRST_FRAG | PFC.LAST_FRAG) : opts.pfcFlags,
    fragLength: len, authLength: 0, callId: opts.callId,
  }).copy(b, 0);
  b.writeUInt32LE(opts.allocHint === undefined ? stub.length : opts.allocHint >>> 0, 16);
  b.writeUInt16LE((opts.contextId || 0) & 0xffff, 20);
  b.writeUInt8((opts.cancelCount || 0) & 0xff, 22);
  b.writeUInt8(0, 23);
  stub.copy(b, 24);
  return b;
}

function parseResponse(buf) {
  const h = parseHeader(buf);
  if (!h || h.ptype !== PTYPE.RESPONSE) return null;
  return {
    header: h,
    allocHint: buf.readUInt32LE(16),
    contextId: buf.readUInt16LE(20),
    cancelCount: buf.readUInt8(22),
    stub: Buffer.from(buf.subarray(24, Math.min(buf.length, h.fragLength))),
    first: !!(h.pfcFlags & PFC.FIRST_FRAG),
    last: !!(h.pfcFlags & PFC.LAST_FRAG),
  };
}

// fault: alloc_hint, p_cont_id, cancel_count, reserved, status, reserved2[4].
function buildFault(opts) {
  const stub = opts.stub || Buffer.alloc(0);
  const len = HDR_LEN + 16 + stub.length;
  const b = Buffer.alloc(len);
  buildHeader({
    ptype: PTYPE.FAULT,
    pfcFlags: (opts.pfcFlags === undefined ? (PFC.FIRST_FRAG | PFC.LAST_FRAG) : opts.pfcFlags) |
              (opts.didNotExecute ? PFC.DID_NOT_EXECUTE : 0),
    fragLength: len, authLength: 0, callId: opts.callId,
  }).copy(b, 0);
  b.writeUInt32LE(opts.allocHint === undefined ? stub.length : opts.allocHint >>> 0, 16);
  b.writeUInt16LE((opts.contextId || 0) & 0xffff, 20);
  b.writeUInt8((opts.cancelCount || 0) & 0xff, 22);
  b.writeUInt8(0, 23);
  b.writeUInt32LE(opts.status >>> 0, 24);
  b.writeUInt32LE(0, 28);
  stub.copy(b, 32);
  return b;
}

function parseFault(buf) {
  const h = parseHeader(buf);
  if (!h || h.ptype !== PTYPE.FAULT) return null;
  return {
    header: h,
    allocHint: buf.readUInt32LE(16),
    contextId: buf.readUInt16LE(20),
    cancelCount: buf.readUInt8(22),
    status: buf.readUInt32LE(24),
  };
}

// ---------------------------------------------------------------------------
// Fragmentation.  A response stub longer than the negotiated max_xmit_frag is
// split; the first piece carries PFC_FIRST_FRAG, the last PFC_LAST_FRAG.
// ---------------------------------------------------------------------------
function fragmentResponse(stub, opts) {
  const maxFrag = Math.max(HDR_LEN + 8 + 8, opts.maxXmitFrag || 4280);
  const room = maxFrag - (HDR_LEN + 8);
  const out = [];
  let off = 0;
  do {
    const n = Math.min(room, stub.length - off);
    const first = off === 0;
    const last = off + n >= stub.length;
    out.push(buildResponse({
      callId: opts.callId, contextId: opts.contextId,
      allocHint: stub.length - off,
      pfcFlags: (first ? PFC.FIRST_FRAG : 0) | (last ? PFC.LAST_FRAG : 0),
      stub: stub.subarray(off, off + n),
    }));
    off += n;
  } while (off < stub.length);
  return out;
}

// ---------------------------------------------------------------------------
// DceRpcServer
//
//   new DceRpcServer([{ uuid, version: [major, minor] | 'x.y' | major,
//                       ops: [fn, ...] }], opts)
//
// An op is called as fn(stub, ctx) and returns either
//   - a Buffer          : the [out] stub, answered immediately;
//   - a Promise<Buffer> : answered when it settles (WitnessrAsyncNotify);
//   - { promise, cancel }: same, plus a way to abandon the call.
// Throwing (or rejecting with) an object carrying .faultStatus produces a
// fault PDU with that status.
// ---------------------------------------------------------------------------
class DceRpcServer {
  constructor(interfaces, opts) {
    const o = opts || {};
    this.interfaces = [];
    for (const i of interfaces || []) this.addInterface(i);
    this.maxXmitFrag = o.maxXmitFrag || 4280;
    this.maxRecvFrag = o.maxRecvFrag || 4280;
    this.assocGroupId = o.assocGroupId || 0x00005354;
    this.secondaryAddress = o.secondaryAddress || '\\PIPE\\ntsvcs';
    this.contexts = new Map();      // p_cont_id -> { iface, transferSyntax }
    this.bound = false;
    this.inFrags = new Map();       // call_id -> { opnum, contextId, chunks }
    this.pending = new Map();       // call_id -> { cancel }
    this.callCount = 0;
  }

  addInterface(i) {
    let major = 0, minor = 0;
    if (Array.isArray(i.version)) { major = i.version[0]; minor = i.version[1] || 0; }
    else if (typeof i.version === 'string') {
      const m = /^(\d+)(?:\.(\d+))?$/.exec(i.version.trim());
      major = m ? Number(m[1]) : 0; minor = m && m[2] !== undefined ? Number(m[2]) : 0;
    } else { major = Number(i.version) || 0; minor = Number(i.minor) || 0; }
    this.interfaces.push({
      uuid: String(i.uuid).toLowerCase(), major, minor,
      ops: i.ops || [], name: i.name || String(i.uuid),
    });
    return this;
  }

  findInterface(uuid, major) {
    const u = String(uuid).toLowerCase();
    for (const i of this.interfaces) {
      if (i.uuid === u && (major === undefined || i.major === major)) return i;
    }
    return null;
  }

  // -- bind / alter_context --------------------------------------------------
  _doBind(pdu, buf) {
    const b = parseBind(buf);
    if (!b) return buildBindNak({ callId: pdu.callId, reason: NAK_REASON.NOT_SPECIFIED });
    if (pdu.rpcVers !== 5 || pdu.rpcVersMinor !== 0) {
      return buildBindNak({
        callId: pdu.callId, reason: NAK_REASON.PROTOCOL_VERSION_NOT_SUPPORTED,
        versions: [{ major: 5, minor: 0 }],
      });
    }
    if (!b.contexts.length) {
      return buildBindNak({ callId: pdu.callId, reason: NAK_REASON.NOT_SPECIFIED });
    }
    const ndrSyntax = buildSyntax(NDR32_UUID, NDR32_VERSION, 0);
    const zeroSyntax = Buffer.alloc(20);
    const results = [];
    let anyAccepted = false;
    for (const c of b.contexts) {
      // [MS-RPCE] 3.3.1.5.3: the bind time feature negotiation pseudo-syntax.
      const btf = c.transferSyntaxes.find((t) => t.uuid.startsWith(BIND_TIME_FEATURE_PREFIX));
      if (btf && c.transferSyntaxes.length === 1) {
        const ack = Buffer.alloc(20);
        uuidToBuf(btf.uuid).copy(ack, 0);
        // Echo the requested feature bits back in the low half of data4.
        results.push({ result: RESULT.NEGOTIATE_ACK, reason: 0, transferSyntax: ack });
        continue;
      }
      const iface = this.findInterface(c.abstract.uuid, c.abstract.major);
      if (!iface) {
        results.push({
          result: RESULT.PROVIDER_REJECTION,
          reason: REASON.ABSTRACT_SYNTAX_NOT_SUPPORTED,
          transferSyntax: zeroSyntax,
        });
        continue;
      }
      const ndr = c.transferSyntaxes.find(
        (t) => t.uuid === NDR32_UUID && t.major === NDR32_VERSION);
      if (!ndr) {
        results.push({
          result: RESULT.PROVIDER_REJECTION,
          reason: REASON.PROPOSED_TRANSFER_SYNTAXES_NOT_SUPPORTED,
          transferSyntax: zeroSyntax,
        });
        continue;
      }
      this.contexts.set(c.contextId, { iface, transferSyntax: NDR32_UUID });
      results.push({ result: RESULT.ACCEPTANCE, reason: 0, transferSyntax: ndrSyntax });
      anyAccepted = true;
    }
    // Note that a bind whose every presentation context was refused still gets
    // a bind_ack carrying those rejections: per [C706] 12.6.4.4 a bind_nak is
    // only for protocol-level refusals (wrong RPC version, no contexts at all).
    if (anyAccepted) this.bound = true;
    if (b.maxXmitFrag) this.maxXmitFrag = Math.min(this.maxXmitFrag, b.maxXmitFrag);
    if (b.maxRecvFrag) this.maxRecvFrag = Math.min(this.maxRecvFrag, b.maxRecvFrag);
    if (b.assocGroupId) this.assocGroupId = b.assocGroupId;
    return buildBindAck({
      callId: pdu.callId,
      ptype: pdu.ptype === PTYPE.ALTER_CONTEXT ? PTYPE.ALTER_CONTEXT_RESP : PTYPE.BIND_ACK,
      maxXmitFrag: this.maxXmitFrag, maxRecvFrag: this.maxRecvFrag,
      assocGroupId: this.assocGroupId,
      // [C706] 12.6.4.4: alter_context_resp carries an empty secondary address.
      secondaryAddress: pdu.ptype === PTYPE.ALTER_CONTEXT ? '' : this.secondaryAddress,
      results,
    });
  }

  _fault(callId, contextId, status, didNotExecute) {
    return buildFault({
      callId, contextId, status, didNotExecute: didNotExecute !== false,
    });
  }

  // Answer everything that can be answered without waiting.  Returns null when
  // the call went pending (use handleAsync for those) or when the PDU needs no
  // answer at all (co_cancel, orphaned, a non-final request fragment).
  handle(requestBytes) {
    let out = null;
    this.handleAsync(requestBytes, (err, resp) => { if (!err) out = resp; }, true);
    return out;
  }

  // handleAsync(requestBytes, cb) -> { cancel() } | null
  // cb(err, responseBytes) is called exactly once unless the call is cancelled.
  handleAsync(requestBytes, cb, syncOnly) {
    // cb(err, responseBytes, fragments) -- fragments is the same response cut
    // into PDUs, which is what a message-mode named pipe puts on the wire.
    const done = (buf, frags) => { if (cb) cb(null, buf, frags || (buf ? [buf] : [])); };
    const pdu = parseHeader(requestBytes);
    if (!pdu) { done(null); return null; }
    if (pdu.rpcVers !== 5) {
      done(this._fault(pdu.callId, 0, FAULT.nca_s_proto_error));
      return null;
    }
    if (!pdu.littleEndian) {
      done(this._fault(pdu.callId, 0, FAULT.nca_s_proto_error));
      return null;
    }
    if (pdu.fragLength > requestBytes.length) { done(null); return null; }

    switch (pdu.ptype) {
      case PTYPE.BIND:
      case PTYPE.ALTER_CONTEXT:
        done(this._doBind(pdu, requestBytes));
        return null;
      case PTYPE.AUTH3:
        done(null);                       // no answer is sent for auth3
        return null;
      case PTYPE.CO_CANCEL:
      case PTYPE.ORPHANED: {
        const p = this.pending.get(pdu.callId);
        if (p && p.cancel) p.cancel();
        done(null);
        return null;
      }
      case PTYPE.REQUEST:
        break;
      default:
        done(this._fault(pdu.callId, 0, FAULT.nca_s_proto_error));
        return null;
    }

    const req = parseRequest(requestBytes);
    if (!req) { done(this._fault(pdu.callId, 0, FAULT.nca_s_proto_error)); return null; }

    // Reassemble a fragmented request.
    let stub = req.stub;
    if (!req.last || !req.first) {
      let acc = this.inFrags.get(pdu.callId);
      if (req.first || !acc) {
        acc = { opnum: req.opnum, contextId: req.contextId, chunks: [] };
        this.inFrags.set(pdu.callId, acc);
      }
      acc.chunks.push(req.stub);
      if (!req.last) { done(null); return null; }
      this.inFrags.delete(pdu.callId);
      stub = Buffer.concat(acc.chunks);
      req.opnum = acc.opnum;
      req.contextId = acc.contextId;
    }

    const ctx = this.contexts.get(req.contextId);
    if (!ctx) {
      done(this._fault(pdu.callId, req.contextId, FAULT.nca_s_fault_context_mismatch));
      return null;
    }
    const op = ctx.iface.ops[req.opnum];
    if (typeof op !== 'function') {
      done(this._fault(pdu.callId, req.contextId, FAULT.nca_s_op_rng_error));
      return null;
    }

    this.callCount++;
    const callCtx = {
      server: this, callId: pdu.callId, contextId: req.contextId,
      opnum: req.opnum, iface: ctx.iface, object: req.object,
      allocHint: req.allocHint, pfcFlags: pdu.pfcFlags,
    };

    const reply = (outStub) => {
      const frags = fragmentResponse(outStub || Buffer.alloc(0), {
        callId: pdu.callId, contextId: req.contextId, maxXmitFrag: this.maxXmitFrag,
      });
      done(frags.length === 1 ? frags[0] : Buffer.concat(frags), frags);
    };
    const faultOut = (e) => {
      const status = (e && e.faultStatus) ? e.faultStatus : FAULT.nca_s_fault_other;
      done(this._fault(pdu.callId, req.contextId, status, false));
    };

    let r;
    try { r = op(stub, callCtx); }
    catch (e) { faultOut(e); return null; }

    if (Buffer.isBuffer(r)) { reply(r); return null; }
    if (r && typeof r.then === 'function') r = { promise: r, cancel: r.cancel };
    if (!r || typeof r.promise !== 'object' || typeof r.promise.then !== 'function') {
      // An op that returns nothing at all is a server bug; answer a fault
      // rather than leaving the caller hanging.
      done(this._fault(pdu.callId, req.contextId, FAULT.nca_s_fault_other, false));
      return null;
    }
    if (syncOnly) {
      // handle() cannot wait.  Abandon the call and report "no answer yet".
      if (r.cancel) { try { r.cancel(); } catch (e) { /* ignore */ } }
      r.promise.then(() => {}, () => {});
      return null;
    }
    const entry = { cancel: r.cancel };
    this.pending.set(pdu.callId, entry);
    let settled = false;
    r.promise.then((outStub) => {
      if (settled) return;
      settled = true;
      this.pending.delete(pdu.callId);
      reply(outStub);
    }, (e) => {
      if (settled) return;
      settled = true;
      this.pending.delete(pdu.callId);
      faultOut(e);
    });
    return {
      cancel: () => {
        if (settled) return;
        settled = true;
        this.pending.delete(pdu.callId);
        if (r.cancel) { try { r.cancel(); } catch (e) { /* ignore */ } }
        if (cb) cb(null, this._fault(pdu.callId, req.contextId, FAULT.nca_s_fault_other, false));
      },
    };
  }

  // Cancel every call still waiting (pipe closed, tree disconnected, ...).
  cancelAll() {
    for (const [, p] of this.pending) { if (p.cancel) { try { p.cancel(); } catch (e) {} } }
    this.pending.clear();
    this.inFrags.clear();
  }
}

module.exports = {
  PTYPE, PFC, RESULT, REASON, NAK_REASON, FAULT, HDR_LEN, DREP_LE,
  NDR32_UUID, NDR32_VERSION, NDR64_UUID, NDR64_VERSION, BIND_TIME_FEATURE_PREFIX,
  uuidToBuf, bufToUuid, buildSyntax, parseSyntax, synVersion, synMajor, synMinor,
  buildHeader, parseHeader,
  parseBind, buildBindAck, parseBindAck, buildBindNak,
  parseRequest, buildRequest, buildResponse, parseResponse, buildFault, parseFault,
  fragmentResponse,
  DceRpcServer,
};
