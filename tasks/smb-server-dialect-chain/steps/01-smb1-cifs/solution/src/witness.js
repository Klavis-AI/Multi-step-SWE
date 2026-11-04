'use strict';
// The Service Witness Protocol server role, [MS-SWN].
//
// Interface ccd8c074-d0e5-4a40-92b4-d074faa6ba28 version 1.1 ([MS-SWN] 1.9 and
// appendix A).  NDR marshalling is done by hand here: transfer syntax NDR
// (little-endian), natural alignment, conformant (and conformant varying)
// arrays, unique pointers and the [string] LPWSTR form.
//
// Nothing in this module knows about SMB or about DCE/RPC PDUs: the five
// operations take an [in] stub Buffer and return an [out] stub Buffer.
// dcerpc.js wraps them in request/response PDUs, pipes.js in a named pipe.

const NUL = String.fromCharCode(0);      // avoids escape sequences in literals

const WITNESS_UUID = 'ccd8c074-d0e5-4a40-92b4-d074faa6ba28';
const WITNESS_IF_VERSION_MAJOR = 1;
const WITNESS_IF_VERSION_MINOR = 1;

// --- protocol versions ([MS-SWN] 1.7) ---------------------------------------
const WITNESS_V1 = 0x00010001;
const WITNESS_V2 = 0x00020000;
const WITNESS_UNSPECIFIED_VERSION = 0xffffffff;

// --- WITNESS_INTERFACE_INFO.State ([MS-SWN] 2.2.2.5) ------------------------
const STATE = { UNKNOWN: 0x0000, AVAILABLE: 0x0001, UNAVAILABLE: 0x00ff };
// --- WITNESS_INTERFACE_INFO.Flags ([MS-SWN] 2.2.2.5) ------------------------
const IFACE_FLAG = { IPv4: 0x00000001, IPv6: 0x00000002, INTERFACE_WITNESS: 0x00000004 };
// --- IPADDR_INFO.Flags ([MS-SWN] 2.2.2.1) -----------------------------------
const IPADDR = { V4: 0x00000001, V6: 0x00000002, ONLINE: 0x00000008, OFFLINE: 0x00000010 };
// --- RESP_ASYNC_NOTIFY.MessageType ([MS-SWN] 2.2.2.4) -----------------------
const NOTIFY = { RESOURCE_CHANGE: 1, CLIENT_MOVE: 2, SHARE_MOVE: 3, IP_CHANGE: 4 };
// --- RESOURCE_CHANGE.ChangeType ([MS-SWN] 2.2.2.3) --------------------------
const RESOURCE_STATE = { UNKNOWN: 0x00000000, AVAILABLE: 0x00000001, UNAVAILABLE: 0x000000ff };
// --- WitnessrRegisterEx Flags ([MS-SWN] 3.1.4.5) ----------------------------
const REGISTER_FLAG = { NONE: 0x00000000, IP_NOTIFICATION: 0x00000001 };

// --- Win32 error codes returned by the five operations ([MS-ERREF] 2.2) -----
const W = {
  ERROR_SUCCESS: 0x00000000,
  ERROR_ACCESS_DENIED: 0x00000005,
  ERROR_OUTOFMEMORY: 0x0000000e,
  ERROR_INVALID_PARAMETER: 0x00000057,
  ERROR_NO_MORE_ITEMS: 0x00000103,
  ERROR_NOT_FOUND: 0x00000490,
  ERROR_REVISION_MISMATCH: 0x0000051a,
  ERROR_NO_SYSTEM_RESOURCES: 0x000005aa,
  ERROR_TIMEOUT: 0x000005b4,
  ERROR_INVALID_STATE: 0x0000139f,
};

const IFACE_GROUP_NAME_CHARS = 260;      // WCHAR InterfaceGroupName[260]
const CONTEXT_HANDLE_LEN = 20;           // uint32 handle_type + GUID ([C706] 6)

// ===========================================================================
// NDR
// ===========================================================================
class NdrWriter {
  constructor(startRef) {
    this.chunks = [];
    this.len = 0;
    this.ref = startRef === undefined ? 0x00020000 : startRef;
  }
  get length() { return this.len; }
  raw(b) { if (b.length) { this.chunks.push(Buffer.from(b)); this.len += b.length; } return this; }
  pad(n) { if (n > 0) this.raw(Buffer.alloc(n)); return this; }
  align(n) { return this.pad((n - (this.len % n)) % n); }
  u8(v) { const b = Buffer.alloc(1); b.writeUInt8(v & 0xff, 0); return this.raw(b); }
  u16(v) { this.align(2); const b = Buffer.alloc(2); b.writeUInt16LE(v & 0xffff, 0); return this.raw(b); }
  u32(v) { this.align(4); const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0, 0); return this.raw(b); }
  u64(v) { this.align(8); const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(v)); return this.raw(b); }
  // 4-aligned raw block (IPv4/IPv6 travel in network byte order, see notes).
  blob(b, alignTo) { this.align(alignTo || 4); return this.raw(b); }
  refId() { const r = this.ref; this.ref += 4; return r; }
  // A [unique] pointer: 0 for NULL, otherwise a non-zero referent id.
  uniquePtr(present) { return this.u32(present ? this.refId() : 0); }
  // [string] LPWSTR: a conformant *varying* array of wchar_t, max_count ==
  // actual_count == characters including the terminating NUL, offset 0.
  wstr(s) {
    const chars = Buffer.from(String(s) + NUL, 'utf16le');
    const n = chars.length / 2;
    this.u32(n); this.u32(0); this.u32(n);
    return this.raw(chars);
  }
  // WCHAR name[N]: a fixed array, NUL filled, no conformance header.
  wfixed(s, n) {
    const b = Buffer.alloc(n * 2);
    const src = Buffer.from(String(s), 'utf16le');
    src.copy(b, 0, 0, Math.min(b.length - 2, src.length));
    return this.raw(b);
  }
  build() { return Buffer.concat(this.chunks, this.len); }
}

function ndrFault(msg) {
  const e = new Error(msg);
  e.faultStatus = 0x000006f7;            // nca_s_fault_ndr
  return e;
}

class NdrReader {
  constructor(buf) { this.b = Buffer.isBuffer(buf) ? buf : Buffer.alloc(0); this.p = 0; }
  get remaining() { return this.b.length - this.p; }
  need(n) { if (this.p + n > this.b.length) throw ndrFault('short NDR stub'); }
  align(n) { const r = this.p % n; if (r) this.p += n - r; }
  raw(n) { this.need(n); const s = this.b.subarray(this.p, this.p + n); this.p += n; return Buffer.from(s); }
  u8() { this.need(1); return this.b.readUInt8(this.p++); }
  u16() { this.align(2); this.need(2); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  u32() { this.align(4); this.need(4); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  u64() { this.align(8); this.need(8); const v = this.b.readBigUInt64LE(this.p); this.p += 8; return v; }
  uniquePtr() { return this.u32() !== 0; }
  wstr() {
    const max = this.u32(), off = this.u32(), act = this.u32();
    if (off !== 0 || act > max || act > 0x10000) throw ndrFault('bad conformant varying string');
    return trimNul(this.raw(act * 2).toString('utf16le'));
  }
  strPtr() { return this.uniquePtr() ? this.wstr() : null; }
}

function trimNul(s) { const z = s.indexOf(NUL); return z >= 0 ? s.slice(0, z) : s; }

// ===========================================================================
// Address helpers.  Both IPV4 and IPV6 travel as raw network-byte-order
// octets (see WITNESS-NOTES.md, "Fields interpreted rather than read").
// ===========================================================================
function ipv4ToBuf(s) {
  const b = Buffer.alloc(4);
  if (!s) return b;
  const parts = String(s).trim().split('.');
  if (parts.length !== 4) return b;
  for (let i = 0; i < 4; i++) {
    const n = Number(parts[i]);
    if (!Number.isInteger(n) || n < 0 || n > 255) return Buffer.alloc(4);
    b[i] = n;
  }
  return b;
}
function bufToIpv4(b) { return b[0] + '.' + b[1] + '.' + b[2] + '.' + b[3]; }

function ipv6ToBuf(s) {
  const out = Buffer.alloc(16);
  if (!s) return out;
  let str = String(s).trim();
  if (!str || str === '::') return out;
  const pct = str.indexOf('%'); if (pct >= 0) str = str.slice(0, pct);
  let tail4 = null;
  const lastColon = str.lastIndexOf(':');
  if (lastColon >= 0 && str.slice(lastColon + 1).indexOf('.') >= 0) {
    tail4 = ipv4ToBuf(str.slice(lastColon + 1));
    str = str.slice(0, lastColon + 1) + '0:0';
  }
  const halves = str.split('::');
  if (halves.length > 2) return out;
  const head = halves[0] ? halves[0].split(':') : [];
  const tail = halves.length === 2 ? (halves[1] ? halves[1].split(':') : []) : [];
  const groups = halves.length === 2
    ? head.concat(new Array(8 - head.length - tail.length).fill('0'), tail)
    : head;
  if (groups.length !== 8) return out;
  for (let i = 0; i < 8; i++) {
    const n = parseInt(groups[i] || '0', 16);
    if (!Number.isFinite(n) || n < 0 || n > 0xffff) return Buffer.alloc(16);
    out.writeUInt16BE(n, i * 2);
  }
  if (tail4) tail4.copy(out, 12);
  return out;
}
function bufToIpv6(b) {
  const g = [];
  for (let i = 0; i < 8; i++) g.push(b.readUInt16BE(i * 2).toString(16).padStart(4, '0'));
  return g.join(':');
}

// ===========================================================================
// [MS-SWN] 2.2.2 structures that live *inside* RESP_ASYNC_NOTIFY.MessageBuffer.
// Per 2.2.2 these are plain little-endian structures, not NDR: no conformance
// headers, no pointers, and (measured against Samba) no padding between
// consecutive RESOURCE_CHANGE entries.
// ===========================================================================
function encodeResourceChange(changeType, resourceName) {
  const name = Buffer.from(String(resourceName) + NUL, 'utf16le');
  const b = Buffer.alloc(8 + name.length);
  b.writeUInt32LE(b.length, 0);          // Length: whole structure, incl. header
  b.writeUInt32LE(changeType >>> 0, 4);  // ChangeType
  name.copy(b, 8);                       // ResourceName (NUL terminated)
  return b;
}
function decodeResourceChange(buf, off) {
  const p = off || 0;
  const len = buf.readUInt32LE(p);
  const type = buf.readUInt32LE(p + 4);
  return {
    length: len, changeType: type,
    resourceName: trimNul(buf.subarray(p + 8, p + len).toString('utf16le')),
    next: p + len,
  };
}

// IPADDR_INFO ([MS-SWN] 2.2.2.1): Flags, IPV4, IPV6 -> 24 bytes.
function encodeIpAddrInfo(a) {
  const b = Buffer.alloc(24);
  b.writeUInt32LE(a.flags >>> 0, 0);
  ipv4ToBuf(a.ipv4).copy(b, 4);
  ipv6ToBuf(a.ipv6).copy(b, 8);
  return b;
}
// IPADDR_INFO_LIST ([MS-SWN] 2.2.2.2): Length, Reserved, IPAddrInstances, [].
function encodeIpAddrInfoList(addrs) {
  const items = (addrs || []).map(encodeIpAddrInfo);
  const b = Buffer.alloc(12 + items.length * 24);
  b.writeUInt32LE(b.length, 0);          // Length: size of the whole list
  b.writeUInt32LE(0, 4);                 // Reserved: MUST be 0
  b.writeUInt32LE(items.length, 8);      // IPAddrInstances
  let p = 12;
  for (const it of items) { it.copy(b, p); p += 24; }
  return b;
}
function decodeIpAddrInfoList(buf) {
  const n = buf.readUInt32LE(8);
  const addrs = [];
  for (let i = 0; i < n; i++) {
    const p = 12 + i * 24;
    addrs.push({
      flags: buf.readUInt32LE(p),
      ipv4: bufToIpv4(buf.subarray(p + 4, p + 8)),
      ipv6: bufToIpv6(buf.subarray(p + 8, p + 24)),
    });
  }
  return { length: buf.readUInt32LE(0), reserved: buf.readUInt32LE(4), addrs };
}

// ===========================================================================
// NDR structures
// ===========================================================================
// WITNESS_INTERFACE_INFO ([MS-SWN] 2.2.2.5) -- 552 bytes.
function writeInterfaceInfo(w, i) {
  w.wfixed(i.interfaceGroupName || '', IFACE_GROUP_NAME_CHARS);  // WCHAR[260]
  w.u32(i.version >>> 0);                                        // ULONG Version
  w.u16(i.state & 0xffff);                                       // USHORT State
  w.blob(ipv4ToBuf(i.ipv4), 4);                                  // ULONG IPV4
  w.blob(ipv6ToBuf(i.ipv6), 4);                                  // USHORT IPV6[8]
  w.u32(i.flags >>> 0);                                          // UINT Flags
}

// WitnessrGetInterfaceList [out]: PWITNESS_INTERFACE_LIST* + DWORD return.
function encodeGetInterfaceListResponse(interfaces, werr) {
  const w = new NdrWriter();
  if (interfaces) {
    w.uniquePtr(true);                       // *InterfaceList
    w.u32(interfaces.length);                // NumberOfInterfaces
    w.uniquePtr(true);                       // [unique] InterfaceInfo
    w.u32(interfaces.length);                // conformant max_count (size_is)
    for (const i of interfaces) writeInterfaceInfo(w, i);
  } else {
    w.uniquePtr(false);
  }
  w.u32(werr >>> 0);
  return w.build();
}
function decodeGetInterfaceListResponse(stub) {
  const r = new NdrReader(stub);
  let interfaces = null;
  if (r.uniquePtr()) {
    const n = r.u32();
    interfaces = [];
    if (r.uniquePtr()) {
      const max = r.u32();
      for (let k = 0; k < Math.min(n, max); k++) {
        const name = trimNul(r.raw(IFACE_GROUP_NAME_CHARS * 2).toString('utf16le'));
        const version = r.u32();
        const state = r.u16();
        r.align(4);
        const ipv4 = bufToIpv4(r.raw(4));
        r.align(4);
        const ipv6 = bufToIpv6(r.raw(16));
        interfaces.push({
          interfaceGroupName: name, version, state, ipv4, ipv6, flags: r.u32(),
        });
      }
    }
  }
  return { interfaces, werr: r.u32() };
}

// WitnessrRegister / WitnessrRegisterEx [out]: PPCONTEXT_HANDLE + DWORD.
function encodeContextHandleResponse(handle, werr) {
  const w = new NdrWriter();
  const h = Buffer.alloc(CONTEXT_HANDLE_LEN);
  if (handle) Buffer.from(handle).copy(h, 0, 0, CONTEXT_HANDLE_LEN);
  w.blob(h, 4);
  w.u32(werr >>> 0);
  return w.build();
}
function decodeContextHandleResponse(stub) {
  const r = new NdrReader(stub);
  return { handle: r.raw(CONTEXT_HANDLE_LEN), werr: r.u32() };
}

// WitnessrRegister [in]
function encodeRegisterRequest(p) {
  const w = new NdrWriter();
  w.u32(p.version >>> 0);
  for (const s of [p.netName, p.ipAddress, p.clientComputerName]) {
    if (s === null || s === undefined) w.uniquePtr(false);
    else { w.uniquePtr(true); w.wstr(s); }
  }
  return w.build();
}
function decodeRegisterRequest(stub) {
  const r = new NdrReader(stub);
  return {
    version: r.u32(),
    netName: r.strPtr(),
    ipAddress: r.strPtr(),
    clientComputerName: r.strPtr(),
  };
}

// WitnessrRegisterEx [in]
function encodeRegisterExRequest(p) {
  const w = new NdrWriter();
  w.u32(p.version >>> 0);
  for (const s of [p.netName, p.shareName, p.ipAddress, p.clientComputerName]) {
    if (s === null || s === undefined) w.uniquePtr(false);
    else { w.uniquePtr(true); w.wstr(s); }
  }
  w.u32(p.flags >>> 0);
  w.u32(p.keepAliveTimeout >>> 0);
  return w.build();
}
function decodeRegisterExRequest(stub) {
  const r = new NdrReader(stub);
  return {
    version: r.u32(),
    netName: r.strPtr(),
    shareName: r.strPtr(),
    ipAddress: r.strPtr(),
    clientComputerName: r.strPtr(),
    flags: r.u32(),
    keepAliveTimeout: r.u32(),
  };
}

// WitnessrUnRegister / WitnessrAsyncNotify [in]: a bare context handle.
function encodeContextHandleRequest(handle) {
  const w = new NdrWriter();
  const h = Buffer.alloc(CONTEXT_HANDLE_LEN);
  if (handle) Buffer.from(handle).copy(h, 0, 0, CONTEXT_HANDLE_LEN);
  return w.blob(h, 4).build();
}
function decodeContextHandleRequest(stub) {
  return new NdrReader(stub).raw(CONTEXT_HANDLE_LEN);
}

// WitnessrUnRegister [out]: the DWORD return value only.
function encodeWerrResponse(werr) { return new NdrWriter().u32(werr >>> 0).build(); }

// WitnessrAsyncNotify [out]: PRESP_ASYNC_NOTIFY* + DWORD.
// RESP_ASYNC_NOTIFY ([MS-SWN] 2.2.2.4) is NDR: MessageBuffer is
// [size_is(Length)][unique] PBYTE, i.e. a conformant byte array whose
// max_count is Length.
function encodeAsyncNotifyResponse(resp, werr) {
  const w = new NdrWriter();
  if (resp) {
    const msg = resp.messageBuffer || Buffer.alloc(0);
    w.uniquePtr(true);                      // *pResp
    w.u32(resp.messageType >>> 0);          // MessageType
    w.u32(msg.length);                      // Length
    w.u32(resp.numberOfMessages >>> 0);     // NumberOfMessages
    w.uniquePtr(msg.length > 0);            // [unique] MessageBuffer
    if (msg.length > 0) { w.u32(msg.length); w.raw(msg); }
  } else {
    w.uniquePtr(false);
  }
  w.u32(werr >>> 0);
  return w.build();
}
function decodeAsyncNotifyResponse(stub) {
  const r = new NdrReader(stub);
  let resp = null;
  if (r.uniquePtr()) {
    const messageType = r.u32();
    const length = r.u32();
    const numberOfMessages = r.u32();
    let messageBuffer = Buffer.alloc(0);
    if (r.uniquePtr()) { const max = r.u32(); messageBuffer = r.raw(Math.min(max, length)); }
    resp = { messageType, length, numberOfMessages, messageBuffer };
  }
  return { resp, werr: r.u32() };
}

// Build a RESP_ASYNC_NOTIFY body from pending notifications.
function buildResourceChangeResponse(notifications) {
  const parts = notifications.map((n) => encodeResourceChange(
    n.newState === STATE.UNAVAILABLE ? RESOURCE_STATE.UNAVAILABLE : RESOURCE_STATE.AVAILABLE,
    n.resourceName));
  return {
    messageType: NOTIFY.RESOURCE_CHANGE,
    numberOfMessages: parts.length,
    messageBuffer: Buffer.concat(parts),
  };
}
function buildIpListResponse(messageType, addrs) {
  return {
    messageType,
    numberOfMessages: 1,                 // [MS-SWN] 3.1.4.4: always 1 here
    messageBuffer: encodeIpAddrInfoList(addrs),
  };
}

// ===========================================================================
// Server role ([MS-SWN] 3.1)
// ===========================================================================
let handleSeq = 0;
function newContextHandle() {
  // uint32 handle_type followed by a 16-byte GUID; the GUID is
  // WitnessRegistration.RegistrationKey ([MS-SWN] 3.1.4.2).
  const h = Buffer.alloc(CONTEXT_HANDLE_LEN);
  h.writeUInt32LE(0, 0);
  handleSeq = (handleSeq + 1) >>> 0;
  const r = Buffer.alloc(16);
  for (let i = 0; i < 16; i += 4) r.writeUInt32LE(Math.floor(Math.random() * 0x100000000) >>> 0, i);
  r.writeUInt32LE(handleSeq, 0);
  r[7] = (r[7] & 0x0f) | 0x40;           // version 4
  r[8] = (r[8] & 0x3f) | 0x80;           // RFC 4122 variant
  r.copy(h, 4);
  return h;
}
function handleKey(h) { return Buffer.from(h).toString('hex'); }
function isNullHandle(h) {
  for (const b of h) if (b !== 0) return false;
  return true;
}

class WitnessInterface {
  constructor(o) {
    this.interfaceGroupName = o.interfaceGroupName || o.name || '';
    this.state = o.state === undefined ? STATE.AVAILABLE : o.state;
    this.ipv4Address = o.ipv4Address || o.ipv4 || '';
    this.ipv6Address = o.ipv6Address || o.ipv6 || '';
    this.local = !!o.local;   // hosted on this server -> INTERFACE_WITNESS clear
  }
}

class WitnessRegistration {
  constructor(o) {
    this.registrationKey = newContextHandle();
    this.witnessClientName = o.clientComputerName || '';
    this.networkName = o.netName || '';
    this.ipAddress = o.ipAddress || '';
    this.witnessClientVersion = o.version >>> 0;
    this.shareName = o.shareName === undefined ? null : o.shareName;
    this.shareNameNotificationRequired = !!o.shareNameNotificationRequired;
    this.ipNotificationRequired = !!o.ipNotificationRequired;
    this.keepAliveTime = o.keepAliveTimeout >>> 0;
    this.lastUseTime = Date.now();
    this.isAsyncNotifyRegistered = false;
    this.pendingChangeNotifications = [];
    this.pendingMoveNotification = null;
    this.pendingShareMoveNotification = null;
    this.pendingIPNotification = null;
    this.waiter = null;                  // the blocked WitnessrAsyncNotify
  }
  hasPending() {
    return this.pendingChangeNotifications.length > 0 ||
           this.pendingMoveNotification !== null ||
           this.pendingShareMoveNotification !== null ||
           this.pendingIPNotification !== null;
  }
}

class WitnessService {
  constructor(opts) {
    const o = opts || {};
    // [MS-SWN] 3.1.1.1 Global
    this.interfaceList = (o.interfaces || []).map((i) => new WitnessInterface(i));
    this.witnessRegistrationList = new Map();     // handleKey -> WitnessRegistration
    this.witnessServiceVersion = o.witnessServiceVersion === undefined
      ? WITNESS_V2 : o.witnessServiceVersion >>> 0;
    this.serverGlobalName = o.serverGlobalName || '';
    // Set true to also accept WITNESS_V2 on opnum 1; [MS-SWN] 3.1.4.2 as
    // written only accepts 0x00010001 there.
    this.acceptV2OnRegister = !!o.acceptV2OnRegister;
    // Enforce NetName == ServerGlobalName; skipped when no name is configured.
    this.checkNetName = o.checkNetName === undefined ? true : !!o.checkNetName;
    this.maxRegistrations = o.maxRegistrations || 1024;
  }

  addInterface(i) { const n = new WitnessInterface(i); this.interfaceList.push(n); return n; }
  findInterfaceByGroup(name) {
    const n = String(name || '').toLowerCase();
    return this.interfaceList.filter((i) => i.interfaceGroupName.toLowerCase() === n);
  }
  findRegistration(handle) {
    if (!handle || isNullHandle(handle)) return null;
    return this.witnessRegistrationList.get(handleKey(handle)) || null;
  }

  // -- opnum 0: WitnessrGetInterfaceList ([MS-SWN] 3.1.4.1) ------------------
  witnessrGetInterfaceList() {
    if (!this.interfaceList.length) {
      return encodeGetInterfaceListResponse(null, W.ERROR_NO_MORE_ITEMS);
    }
    const out = this.interfaceList.map((i) => {
      let flags = 0;
      if (i.ipv4Address) flags |= IFACE_FLAG.IPv4;
      if (i.ipv6Address) flags |= IFACE_FLAG.IPv6;
      if (!i.local) flags |= IFACE_FLAG.INTERFACE_WITNESS;
      return {
        interfaceGroupName: i.interfaceGroupName,
        version: this.witnessServiceVersion,
        state: i.state,
        ipv4: i.ipv4Address, ipv6: i.ipv6Address,
        flags,
      };
    });
    return encodeGetInterfaceListResponse(out, W.ERROR_SUCCESS);
  }

  // -- opnum 1: WitnessrRegister ([MS-SWN] 3.1.4.2) --------------------------
  witnessrRegister(stub) {
    const p = decodeRegisterRequest(stub);
    const fail = (e) => encodeContextHandleResponse(null, e);
    const ok = p.version === WITNESS_V1 ||
               (this.acceptV2OnRegister && p.version === WITNESS_V2);
    if (!ok) return fail(W.ERROR_REVISION_MISMATCH);
    if (p.netName === null || p.ipAddress === null || p.clientComputerName === null) {
      return fail(W.ERROR_INVALID_PARAMETER);
    }
    if (this.checkNetName && this.serverGlobalName &&
        p.netName.toLowerCase() !== this.serverGlobalName.toLowerCase()) {
      return fail(W.ERROR_INVALID_PARAMETER);
    }
    if (this.witnessRegistrationList.size >= this.maxRegistrations) {
      return fail(W.ERROR_NO_SYSTEM_RESOURCES);
    }
    const reg = new WitnessRegistration({
      clientComputerName: p.clientComputerName,
      netName: p.netName, ipAddress: p.ipAddress, version: p.version,
      shareName: null, keepAliveTimeout: 0,
    });
    this.witnessRegistrationList.set(handleKey(reg.registrationKey), reg);
    return encodeContextHandleResponse(reg.registrationKey, W.ERROR_SUCCESS);
  }

  // -- opnum 2: WitnessrUnRegister ([MS-SWN] 3.1.4.3) ------------------------
  witnessrUnRegister(stub) {
    const h = decodeContextHandleRequest(stub);
    const reg = this.findRegistration(h);
    // 3.1.4.3 lists ERROR_NOT_FOUND in the return table but the processing
    // rules say ERROR_INVALID_PARAMETER for "no matching entry"; see notes.
    if (!reg) return encodeWerrResponse(W.ERROR_INVALID_PARAMETER);
    this._releaseWaiter(reg, null, W.ERROR_NOT_FOUND);
    this.witnessRegistrationList.delete(handleKey(h));
    return encodeWerrResponse(W.ERROR_SUCCESS);
  }

  // -- opnum 3: WitnessrAsyncNotify ([MS-SWN] 3.1.4.4) -----------------------
  // Blocks until something is pending, so it returns { promise, cancel }.
  witnessrAsyncNotify(stub) {
    const h = decodeContextHandleRequest(stub);
    const reg = this.findRegistration(h);
    if (!reg) return encodeAsyncNotifyResponse(null, W.ERROR_NOT_FOUND);
    if (this.witnessServiceVersion === WITNESS_V2) {
      reg.lastUseTime = Date.now();
      reg.isAsyncNotifyRegistered = true;
    }
    if (reg.hasPending()) return this._drain(reg);
    if (reg.waiter) {
      // Only one outstanding AsyncNotify per registration makes sense; the
      // older one is completed with ERROR_NOT_FOUND so nothing hangs.
      this._releaseWaiter(reg, null, W.ERROR_NOT_FOUND);
    }
    let resolve = null;
    let timer = null;
    const promise = new Promise((res) => { resolve = res; });
    const self = this;
    const finish = (stubOut) => {
      if (!reg.waiter) return;
      reg.waiter = null;
      if (timer) { clearTimeout(timer); timer = null; }
      if (self.witnessServiceVersion === WITNESS_V2) {
        reg.lastUseTime = Date.now();
        reg.isAsyncNotifyRegistered = false;
      }
      resolve(stubOut);
    };
    reg.waiter = { finish, reg };
    if (this.witnessServiceVersion === WITNESS_V2 && reg.keepAliveTime > 0) {
      // [MS-SWN] 3.1.5.2 AsyncNotify Pending Timer -> ERROR_TIMEOUT.
      timer = setTimeout(() => finish(encodeAsyncNotifyResponse(null, W.ERROR_TIMEOUT)),
                         reg.keepAliveTime * 1000);
      if (timer.unref) timer.unref();
    }
    return {
      promise,
      cancel: () => finish(encodeAsyncNotifyResponse(null, W.ERROR_NOT_FOUND)),
    };
  }

  // -- opnum 4: WitnessrRegisterEx ([MS-SWN] 3.1.4.5) ------------------------
  witnessrRegisterEx(stub) {
    const p = decodeRegisterExRequest(stub);
    const fail = (e) => encodeContextHandleResponse(null, e);
    // 3.1.4.5: "applicable only to servers that implement version 2".
    if (this.witnessServiceVersion !== WITNESS_V2) return fail(W.ERROR_REVISION_MISMATCH);
    if (p.version !== WITNESS_V2) return fail(W.ERROR_REVISION_MISMATCH);
    if (p.netName === null || p.ipAddress === null || p.clientComputerName === null) {
      return fail(W.ERROR_INVALID_PARAMETER);
    }
    if (this.checkNetName && this.serverGlobalName &&
        p.netName.toLowerCase() !== this.serverGlobalName.toLowerCase()) {
      return fail(W.ERROR_INVALID_PARAMETER);
    }
    if (this.witnessRegistrationList.size >= this.maxRegistrations) {
      return fail(W.ERROR_NO_SYSTEM_RESOURCES);
    }
    const reg = new WitnessRegistration({
      clientComputerName: p.clientComputerName,
      netName: p.netName, ipAddress: p.ipAddress, version: p.version,
      shareName: p.shareName, keepAliveTimeout: p.keepAliveTimeout,
      shareNameNotificationRequired: p.shareName !== null,
      ipNotificationRequired: !!(p.flags & REGISTER_FLAG.IP_NOTIFICATION),
    });
    this.witnessRegistrationList.set(handleKey(reg.registrationKey), reg);
    return encodeContextHandleResponse(reg.registrationKey, W.ERROR_SUCCESS);
  }

  // -- turning pending state into a RESP_ASYNC_NOTIFY -----------------------
  _drain(reg) {
    let resp = null;
    if (reg.pendingChangeNotifications.length) {
      resp = buildResourceChangeResponse(reg.pendingChangeNotifications);
      reg.pendingChangeNotifications = [];
    } else if (reg.pendingMoveNotification !== null) {
      resp = buildIpListResponse(NOTIFY.CLIENT_MOVE,
        this._addrsFor(reg.pendingMoveNotification, true));
      reg.pendingMoveNotification = null;
    } else if (reg.pendingShareMoveNotification !== null) {
      resp = buildIpListResponse(NOTIFY.SHARE_MOVE,
        this._addrsFor(reg.pendingShareMoveNotification, false));
      reg.pendingShareMoveNotification = null;
    } else if (reg.pendingIPNotification !== null) {
      resp = buildIpListResponse(NOTIFY.IP_CHANGE,
        this._addrsFor(reg.pendingIPNotification, false));
      reg.pendingIPNotification = null;
    }
    if (this.witnessServiceVersion === WITNESS_V2) {
      reg.lastUseTime = Date.now();
      reg.isAsyncNotifyRegistered = false;
    }
    return encodeAsyncNotifyResponse(resp, W.ERROR_SUCCESS);
  }

  // Interfaces of a group, as IPADDR_INFO ([MS-SWN] 3.1.4.4).  CLIENT_MOVE
  // also reports ONLINE/OFFLINE; SHARE_MOVE and IP_CHANGE do not.
  _addrsFor(groupName, withOnlineFlag) {
    const out = [];
    for (const i of this.findInterfaceByGroup(groupName)) {
      let flags = 0;
      if (withOnlineFlag) {
        if (i.state === STATE.AVAILABLE) flags |= IPADDR.ONLINE;
        else if (i.state === STATE.UNAVAILABLE) flags |= IPADDR.OFFLINE;
      }
      if (i.ipv4Address) flags |= IPADDR.V4;
      if (i.ipv6Address) flags |= IPADDR.V6;
      out.push({ flags, ipv4: i.ipv4Address, ipv6: i.ipv6Address });
    }
    return out;
  }

  _releaseWaiter(reg, resp, werr) {
    if (!reg.waiter) return false;
    reg.waiter.finish(encodeAsyncNotifyResponse(resp, werr === undefined ? W.ERROR_SUCCESS : werr));
    return true;
  }
  _wake(reg) {
    if (reg.waiter && reg.hasPending()) {
      const stubOut = this._drain(reg);
      reg.waiter.finish(stubOut);
      return true;
    }
    return false;
  }

  // -- events the SMB/server layer raises ([MS-SWN] 3.1.6) ------------------
  // 3.1.6.1: an interface changed state.
  interfaceStateChanged(groupName, newState) {
    for (const i of this.findInterfaceByGroup(groupName)) i.state = newState;
    return this.resourceChanged(groupName, newState);
  }
  // 3.1.6.3: a resource (NetName / IP / interface group) changed state.
  resourceChanged(resourceName, newState, filter) {
    let n = 0;
    for (const reg of this.witnessRegistrationList.values()) {
      if (filter && !filter(reg)) continue;
      reg.pendingChangeNotifications.push({ resourceName, newState });
      this._wake(reg); n++;
    }
    return n;
  }
  // 3.1.6.2: move the client to another resource.
  clientMove(destinationGroup, filter) {
    let n = 0;
    for (const reg of this.witnessRegistrationList.values()) {
      if (filter && !filter(reg)) continue;
      reg.pendingMoveNotification = destinationGroup;
      this._wake(reg); n++;
    }
    return n;
  }
  shareMove(destinationGroup, filter) {
    let n = 0;
    for (const reg of this.witnessRegistrationList.values()) {
      if (reg.witnessClientVersion !== WITNESS_V2 || !reg.shareNameNotificationRequired) continue;
      if (filter && !filter(reg)) continue;
      reg.pendingShareMoveNotification = destinationGroup;
      this._wake(reg); n++;
    }
    return n;
  }
  ipChange(groupName, filter) {
    let n = 0;
    for (const reg of this.witnessRegistrationList.values()) {
      if (reg.witnessClientVersion !== WITNESS_V2 || !reg.ipNotificationRequired) continue;
      if (filter && !filter(reg)) continue;
      reg.pendingIPNotification = groupName;
      this._wake(reg); n++;
    }
    return n;
  }
  // 3.1.6.5: the transport connection went away.
  connectionShutdown(filter) {
    for (const reg of Array.from(this.witnessRegistrationList.values())) {
      if (filter && !filter(reg)) continue;
      this._releaseWaiter(reg, null, W.ERROR_NOT_FOUND);
      this.witnessRegistrationList.delete(handleKey(reg.registrationKey));
    }
  }
  // 3.1.5.1 Unused Registration Timer.  Default 30 s, which is what note <7>
  // says Windows Server 2012 R2 and later use.
  expireUnusedRegistrations(timeoutMs) {
    const cutoff = Date.now() - (timeoutMs === undefined ? 30 * 1000 : timeoutMs);
    for (const reg of Array.from(this.witnessRegistrationList.values())) {
      if (!reg.isAsyncNotifyRegistered && reg.lastUseTime < cutoff) {
        this.witnessRegistrationList.delete(handleKey(reg.registrationKey));
      }
    }
  }

  // The [{uuid, version, ops}] shape DceRpcServer wants.
  interfaceDefinition() {
    const s = this;
    return {
      name: 'witness',
      uuid: WITNESS_UUID,
      version: [WITNESS_IF_VERSION_MAJOR, WITNESS_IF_VERSION_MINOR],
      ops: [
        (stub) => s.witnessrGetInterfaceList(stub),   // 0
        (stub) => s.witnessrRegister(stub),           // 1
        (stub) => s.witnessrUnRegister(stub),         // 2
        (stub) => s.witnessrAsyncNotify(stub),        // 3
        (stub) => s.witnessrRegisterEx(stub),         // 4
        // opnum 5 WitnessrUnRegisterEx is deliberately absent: DceRpcServer
        // then answers a fault with nca_s_op_rng_error, which is what
        // [MS-SWN] 3.1.4 note <3> says Windows Server 2022 and earlier do.
      ],
    };
  }
}

module.exports = {
  WITNESS_UUID, WITNESS_IF_VERSION_MAJOR, WITNESS_IF_VERSION_MINOR,
  WITNESS_V1, WITNESS_V2, WITNESS_UNSPECIFIED_VERSION,
  STATE, IFACE_FLAG, IPADDR, NOTIFY, RESOURCE_STATE, REGISTER_FLAG, W,
  CONTEXT_HANDLE_LEN, IFACE_GROUP_NAME_CHARS,
  NdrWriter, NdrReader,
  ipv4ToBuf, bufToIpv4, ipv6ToBuf, bufToIpv6,
  encodeResourceChange, decodeResourceChange,
  encodeIpAddrInfo, encodeIpAddrInfoList, decodeIpAddrInfoList,
  encodeGetInterfaceListResponse, decodeGetInterfaceListResponse,
  encodeContextHandleResponse, decodeContextHandleResponse,
  encodeContextHandleRequest, decodeContextHandleRequest,
  encodeRegisterRequest, decodeRegisterRequest,
  encodeRegisterExRequest, decodeRegisterExRequest,
  encodeWerrResponse,
  encodeAsyncNotifyResponse, decodeAsyncNotifyResponse,
  buildResourceChangeResponse, buildIpListResponse,
  newContextHandle,
  WitnessInterface, WitnessRegistration, WitnessService,
};
