'use strict';
// ---------------------------------------------------------------------------
// SMB 2 front end -- dialects 2.0.2 (0x0202) and 2.1 (0x0210).
//
// [MS-SMB2] section references are given throughout.  The SMB 3.x additions
// (negotiate contexts, AES-CMAC/GMAC signing, transform-header encryption)
// are deliberately absent; the hooks they need -- DIALECTS, signBytes(),
// verifySignature() and the create-context table -- are structured so that a
// later pass can add them without reshaping the dispatcher.
// ---------------------------------------------------------------------------
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const S = require('./ntstatus');
const { A, SHARE, DISP, OPT, ACTION, ATTR, OPLOCK, LEASE } = require('./consts');
const V = require('./vfs');
const OPS = require('./fileops');
const INFO = require('./info');
const ntlm = require('./ntlm');
const { hmacSha256, sha512, aesCmac, aesGmac, kdfCounter } = require('./crypt');
const PIPES = require('./pipes');
const SD = require('./sd');

const { SmbError, fail, toFileTime, fromFileTime } = V;

// --- protocol constants ----------------------------------------------------

const MAGIC = Buffer.from([0xfe, 0x53, 0x4d, 0x42]);
const HDR = 64;

const CMD = {
  NEGOTIATE: 0x0000, SESSION_SETUP: 0x0001, LOGOFF: 0x0002, TREE_CONNECT: 0x0003,
  TREE_DISCONNECT: 0x0004, CREATE: 0x0005, CLOSE: 0x0006, FLUSH: 0x0007,
  READ: 0x0008, WRITE: 0x0009, LOCK: 0x000a, IOCTL: 0x000b, CANCEL: 0x000c,
  ECHO: 0x000d, QUERY_DIRECTORY: 0x000e, CHANGE_NOTIFY: 0x000f,
  QUERY_INFO: 0x0010, SET_INFO: 0x0011, OPLOCK_BREAK: 0x0012,
};
const CMD_NAME = {};
for (const [k, v] of Object.entries(CMD)) CMD_NAME[v] = k;

const FLAG = {
  SERVER_TO_REDIR: 0x00000001, ASYNC_COMMAND: 0x00000002,
  RELATED_OPERATIONS: 0x00000004, SIGNED: 0x00000008,
  PRIORITY_MASK: 0x00000070, DFS_OPERATIONS: 0x10000000, REPLAY_OPERATION: 0x20000000,
};

// [MS-SMB2] 2.2.3 / 2.2.4 SecurityMode
const SEC_SIGNING_ENABLED = 0x0001;
const SEC_SIGNING_REQUIRED = 0x0002;

// [MS-SMB2] 2.2.4 Capabilities
const CAP = {
  DFS: 0x00000001, LEASING: 0x00000002, LARGE_MTU: 0x00000004,
  MULTI_CHANNEL: 0x00000008, PERSISTENT_HANDLES: 0x00000010,
  DIRECTORY_LEASING: 0x00000020, ENCRYPTION: 0x00000040,
};

// Dialect table -- a 3.x pass appends its entries here.
const DIALECTS = [
  { value: 0x0202, name: 'SMB 2.0.2', maxTrans: 65536, maxRead: 65536, maxWrite: 65536,
    leasing: false, largeMtu: false, sign: 'hmac-sha256' },
  // Samba's lp_smb2_max_{trans,read,write} default to 8 MiB and are clamped to
  // 64 KiB only while the connection cannot do multi-credit (i.e. below 2.1).
  { value: 0x0210, name: 'SMB 2.1', maxTrans: 8388608, maxRead: 8388608, maxWrite: 8388608,
    leasing: true, largeMtu: true, sign: 'hmac-sha256' },
  { value: 0x0300, name: 'SMB 3.0', maxTrans: 8388608, maxRead: 8388608, maxWrite: 8388608,
    leasing: true, largeMtu: true, sign: 'cmac', crypto: true },
  { value: 0x0302, name: 'SMB 3.0.2', maxTrans: 8388608, maxRead: 8388608, maxWrite: 8388608,
    leasing: true, largeMtu: true, sign: 'cmac', crypto: true, unbuffered: true },
  { value: 0x0311, name: 'SMB 3.1.1', maxTrans: 8388608, maxRead: 8388608, maxWrite: 8388608,
    leasing: true, largeMtu: true, sign: 'cmac', crypto: true, unbuffered: true,
    negContexts: true },
];
const MAX_SUPPORTED = 0x0311;
// [MS-SMB2] 1.7 / 2.2.4: the revision the server echoes when the client asked
// for "SMB 2.???" in an SMB1 multi-protocol negotiate.
const DIALECT_WILDCARD = 0x02ff;
const dialectInfo = (v) => DIALECTS.find((d) => d.value === v) || DIALECTS[0];

// [MS-SMB2] 2.2.10 ShareFlags / ShareType
const SHARE_TYPE_DISK = 0x01, SHARE_TYPE_PIPE = 0x02;
const SHAREFLAG_MANUAL_CACHING = 0x00000000;
const SHAREFLAG_ENCRYPT_DATA = 0x00008000;

// --- SMB 3.x ---------------------------------------------------------------

// [MS-SMB2] 2.2.3.1 negotiate context types
const NEGCTX = {
  PREAUTH: 0x0001, ENCRYPTION: 0x0002, COMPRESSION: 0x0003, NETNAME: 0x0005,
  TRANSPORT: 0x0006, RDMA_TRANSFORM: 0x0007, SIGNING: 0x0008,
};
const HASH_SHA512 = 0x0001;                                   // 2.2.3.1.1
const CIPHER_CCM = 0x0001, CIPHER_GCM = 0x0002;               // 2.2.3.1.2
const COMPRESS = { NONE: 0, LZNT1: 1, LZ77: 2, LZ77_HUFF: 3, PATTERN_V1: 4 }; // 2.2.3.1.3
const SIGNALG_HMAC_SHA256 = 0x0000, SIGNALG_CMAC = 0x0001, SIGNALG_GMAC = 0x0002;

// [MS-SMB2] 2.2.41 SMB2_TRANSFORM_HEADER / 2.2.42 compression transform
const TRANSFORM_MAGIC = Buffer.from([0xfd, 0x53, 0x4d, 0x42]);
const COMPRESSION_MAGIC = Buffer.from([0xfc, 0x53, 0x4d, 0x42]);
const TRANSFORM_HDR = 52;
const TRANSFORM_FLAG_ENCRYPTED = 0x0001;

const SESSION_FLAG_ENCRYPT_DATA = 0x0004;                     // 2.2.6 SessionFlags
const STATUS_SMB_NO_PREAUTH_INTEGRITY_HASH_OVERLAP = 0xc05d0000;

// SP800-108 labels/contexts ([MS-SMB2] 3.1.4.2); the trailing NUL is part of
// the label, the KDF appends its own separator byte on top.
const L = (t) => Buffer.from(t, 'latin1');
const KDF_SIGN_30 = L('SMB2AESCMAC\0'), KDF_SIGN_30_CTX = L('SmbSign\0');
const KDF_CCM_30 = L('SMB2AESCCM\0');
const KDF_C2S_30 = L('ServerIn \0'), KDF_S2C_30 = L('ServerOut\0');
const KDF_APP_30 = L('SMB2APP\0'), KDF_APP_30_CTX = L('SmbRpc\0');
const KDF_SIGN_311 = L('SMBSigningKey\0');
const KDF_C2S_311 = L('SMBC2SCipherKey\0'), KDF_S2C_311 = L('SMBS2CCipherKey\0');
const KDF_APP_311 = L('SMBAppKey\0');

// [MS-SMB2] 2.2.13.2 create-context tags
const CTX = {
  EA_BUFFER: 'ExtA', SD_BUFFER: 'SecD', DURABLE_REQ: 'DHnQ', DURABLE_RECONNECT: 'DHnC',
  ALLOCATION_SIZE: 'AlSi', MAX_ACCESS: 'MxAc', TIMEWARP: 'TWrp', QUERY_ON_DISK_ID: 'QFid',
  LEASE: 'RqLs', DURABLE_REQ_V2: 'DH2Q', DURABLE_RECONNECT_V2: 'DH2C',
};

// [MS-SMB2] 2.2.31 CtlCode values
const FSCTL = {
  DFS_GET_REFERRALS: 0x00060194, DFS_GET_REFERRALS_EX: 0x000601b0,
  IS_PATHNAME_VALID: 0x0009002c, GET_COMPRESSION: 0x0009003c,
  SET_COMPRESSION: 0x0009c040, GET_OBJECT_ID: 0x0009009c,
  CREATE_OR_GET_OBJECT_ID: 0x000900c0, SET_SPARSE: 0x000900c4,
  SET_ZERO_DATA: 0x000980c8, QUERY_ALLOCATED_RANGES: 0x000940cf,
  SET_REPARSE_POINT: 0x000900a4, GET_REPARSE_POINT: 0x000900a8,
  DELETE_REPARSE_POINT: 0x000900ac, SET_OBJECT_ID: 0x00090098,
  SET_OBJECT_ID_EXTENDED: 0x000900bc, DELETE_OBJECT_ID: 0x900a0,
  FILE_LEVEL_TRIM: 0x00098208, RECALL_FILE: 0x00090117,
  GET_INTEGRITY_INFORMATION: 0x0009027c, SET_INTEGRITY_INFORMATION: 0x0009c280,
  DUPLICATE_EXTENTS_TO_FILE: 0x00098344, OFFLOAD_READ: 0x00094264,
  OFFLOAD_WRITE: 0x00098268,
  PIPE_PEEK: 0x0011400c, PIPE_WAIT: 0x00110018, PIPE_TRANSCEIVE: 0x0011c017,
  SRV_REQUEST_RESUME_KEY: 0x00140078, SRV_COPYCHUNK: 0x001440f2,
  SRV_COPYCHUNK_WRITE: 0x001480f2, SRV_ENUMERATE_SNAPSHOTS: 0x00144064,
  SRV_READ_HASH: 0x001441bb, LMR_REQUEST_RESILIENCY: 0x001401d4,
  QUERY_NETWORK_INTERFACE_INFO: 0x001401fc, VALIDATE_NEGOTIATE_INFO: 0x00140204,
};
const IOCTL_IS_FSCTL = 0x00000001;

// [MS-SMB2] 2.2.37 InfoType
const INFO_FILE = 0x01, INFO_FILESYSTEM = 0x02, INFO_SECURITY = 0x03, INFO_QUOTA = 0x04;

// [MS-SMB2] 2.2.33 Flags
const QD_RESTART_SCANS = 0x01, QD_RETURN_SINGLE_ENTRY = 0x02,
      QD_INDEX_SPECIFIED = 0x04, QD_REOPEN = 0x10;

// [MS-SMB2] 2.2.35 CompletionFilter
const NOTIFY_WATCH_TREE = 0x0001;

const LOCK_SHARED = 0x01, LOCK_EXCLUSIVE = 0x02, LOCK_UNLOCK = 0x04,
      LOCK_FAIL_IMMEDIATELY = 0x10;

const SESSION_FLAG_IS_GUEST = 0x0001, SESSION_FLAG_IS_NULL = 0x0002;
const SESSION_FLAG_BINDING = 0x01;      // request flag, [MS-SMB2] 2.2.5

// Not present in ntstatus.js (shared with the SMB1 front end): keep local.
const STATUS_BAD_IMPERSONATION_LEVEL = 0xc00000a5;
const STATUS_TOO_MANY_OPENED_FILES = 0xc000011f;

const DURABLE_TIMEOUT_MS = 16000;      // [MS-SMB2] 3.3.1.1 durable open scavenger
const OPLOCK_BREAK_WAIT_MS = 1500;

// --- small helpers ---------------------------------------------------------

const uni = (s) => Buffer.from(String(s), 'utf16le');
const unistr = (b) => b.toString('utf16le');
const u32 = (v) => (v >>> 0);
const nowFt = () => toFileTime(Date.now());

function pad8(n) { return (n + 7) & ~7; }

function guidStr(b) { return Buffer.from(b).toString('hex'); }

function readGuid(buf, off) {
  if (!buf || buf.length < off + 16) return Buffer.alloc(16);
  return Buffer.from(buf.subarray(off, off + 16));
}

// ---------------------------------------------------------------------------
// Per-connection SMB2 state
// ---------------------------------------------------------------------------

class Session {
  constructor(server, conn, id) {
    this.id = id;
    this.server = server;
    this.conn = conn;
    this.connections = new Set([conn]);
    this.state = 'in-progress';       // 'in-progress' | 'valid' | 'expired'
    this.challenge = null;
    this.negFlags = 0;
    this.sessionKey = null;
    this.signingKey = null;
    this.encryptKey = null;          // server -> client ([MS-SMB2] 3.1.4.2)
    this.decryptKey = null;          // client -> server
    this.applicationKey = null;
    this.preauth = null;             // 3.1.1 Session.PreauthIntegrityHashValue
    this.encryptData = false;
    this.signingRequired = false;
    this.user = null;
    this.trees = new Map();
    this.opens = new Map();           // volatile id (string) -> Open
    this.flags = 0;
  }
}

class Tree {
  constructor(id, share, session, opts = {}) {
    this.id = id;
    this.share = share;
    this.session = session;
    this.opens = new Map();           // volatile id (string) -> Open
    this.isIpc = !!opts.isIpc;
    this.encryptData = !!opts.encryptData;
    this.maximalAccess = opts.maximalAccess || 0x001f01ff;
    this.name = opts.name || (share ? share.name : 'IPC$');
  }
}

function cstate(conn) {
  if (conn.smb2state) return conn.smb2state;
  const st = {
    negotiated: false,
    dialectInfo: DIALECTS[0],
    maxTrans: 65536, maxRead: 65536, maxWrite: 65536,
    supportsMultiCredit: false,
    async: new Map(),               // asyncId(string) -> { cancel, mid }
    byMid: new Map(),               // messageId(string) -> async entry
    opens: new Map(),               // volatile id (string) -> Open
    nextPersistent: 1n,
    creditsGranted: 1,
    credits: 1,                     // outstanding credits (the sequence window)
    midSeen: new Set(),             // MessageIds consumed inside the window
    midHigh: 0n,
    breakWaiters: [],
    // --- 3.x
    preauth: Buffer.alloc(64),      // Connection.PreauthIntegrityHashValue
    cipherId: 0,                    // 0 until a cipher is agreed
    signingAlgo: SIGNALG_CMAC,
    compressionAlgs: [],
    clientCaps: 0,
    clientDialects: [],
    encryptionOffered: false,       // client is willing to encrypt
  };
  conn.smb2state = st;
  // Preserve durable opens before server.js tears the connection down.
  const origDestroy = conn.destroy.bind(conn);
  conn.destroy = function () {
    if (conn.closed) return;
    try { onDisconnect(conn); } catch (e) { /* best effort */ }
    origDestroy();
  };
  return st;
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

function parseHeader(b) {
  return {
    structureSize: b.readUInt16LE(4),
    creditCharge: b.readUInt16LE(6),
    status: b.readUInt32LE(8),
    command: b.readUInt16LE(12),
    creditRequest: b.readUInt16LE(14),
    flags: b.readUInt32LE(16),
    nextCommand: b.readUInt32LE(20),
    messageId: b.readBigUInt64LE(24),
    asyncId: b.readBigUInt64LE(32),
    treeId: b.readUInt32LE(36),
    sessionId: b.readBigUInt64LE(40),
    signature: Buffer.from(b.subarray(48, 64)),
  };
}

function buildHeader(o) {
  const h = Buffer.alloc(HDR);
  MAGIC.copy(h, 0);
  h.writeUInt16LE(64, 4);
  h.writeUInt16LE(o.creditCharge || 0, 6);
  h.writeUInt32LE(u32(o.status), 8);
  h.writeUInt16LE(o.command, 12);
  h.writeUInt16LE(o.credits || 1, 14);
  h.writeUInt32LE(u32(o.flags), 16);
  h.writeUInt32LE(u32(o.nextCommand || 0), 20);
  h.writeBigUInt64LE(BigInt(o.messageId || 0n), 24);
  if (o.flags & FLAG.ASYNC_COMMAND) {
    h.writeBigUInt64LE(BigInt(o.asyncId || 0n), 32);
  } else {
    h.writeUInt32LE(0, 32);
    h.writeUInt32LE(u32(o.treeId || 0), 36);
  }
  h.writeBigUInt64LE(BigInt(o.sessionId || 0n), 40);
  return h;
}

// --- signing ---------------------------------------------------------------
// The hook a 3.x pass extends: pick the MAC by Connection.Dialect.

function macFor(conn, key, buf) {
  const st = conn.smb2state;
  const di = st ? st.dialectInfo : DIALECTS[0];
  let alg = di.sign;
  if (di.value >= 0x0311 && st && st.signingAlgo === SIGNALG_GMAC) alg = 'gmac';
  switch (alg) {
    case 'cmac':
      return aesCmac(key, buf);
    case 'gmac': {
      // [MS-SMB2] 3.1.4.1: nonce = MessageId then a flag word carrying
      // "is a response" and "is SMB2 CANCEL".
      const nonce = Buffer.alloc(12);
      nonce.writeBigUInt64LE(buf.readBigUInt64LE(24), 0);
      const flags = buf.readUInt32LE(16), cmd = buf.readUInt16LE(12);
      nonce.writeUInt32LE(((flags & FLAG.SERVER_TO_REDIR) ? 1 : 0) |
                          (cmd === CMD.CANCEL ? 2 : 0), 8);
      return aesGmac(key, nonce, buf);
    }
    case 'hmac-sha256':
    default:
      return hmacSha256(key, buf).subarray(0, 16);
  }
}

// Sign `buf` (a complete PDU: 64-byte header + body) in place.
function signMessage(conn, session, buf) {
  if (!session || !session.signingKey) return;
  buf.writeUInt32LE(u32(buf.readUInt32LE(16) | FLAG.SIGNED), 16);
  buf.fill(0, 48, 64);
  macFor(conn, session.signingKey, buf).copy(buf, 48);
}

function verifySignature(conn, session, pdu, hdr) {
  if (!session || !session.signingKey) return false;
  const copy = Buffer.from(pdu);
  copy.fill(0, 48, 64);
  const want = macFor(conn, session.signingKey, copy);
  return want.equals(hdr.signature);
}

// ---------------------------------------------------------------------------
// Negotiate contexts ([MS-SMB2] 2.2.3.1 / 2.2.4)
// ---------------------------------------------------------------------------

function parseNegotiateContexts(buf, count) {
  const out = [];
  let off = 0;
  for (let i = 0; i < count; i++) {
    if (off + 8 > buf.length) break;
    const type = buf.readUInt16LE(off);
    const len = buf.readUInt16LE(off + 2);
    if (off + 8 + len > buf.length) break;
    out.push({ type, data: Buffer.from(buf.subarray(off + 8, off + 8 + len)) });
    off = pad8(off + 8 + len);
  }
  return out;
}

function buildNegotiateContexts(list) {
  let blob = Buffer.alloc(0);
  for (let i = 0; i < list.length; i++) {
    const h = Buffer.alloc(8);
    h.writeUInt16LE(list[i].type, 0);
    h.writeUInt16LE(list[i].data.length, 2);
    blob = Buffer.concat([blob, h, list[i].data]);
    if (i !== list.length - 1) blob = Buffer.concat([blob, Buffer.alloc(pad8(blob.length) - blob.length)]);
  }
  return blob;
}

const u16list = (buf, off, n) => {
  const a = [];
  for (let i = 0; i < n && off + i * 2 + 2 <= buf.length; i++) a.push(buf.readUInt16LE(off + i * 2));
  return a;
};

function u16blob(values) {
  const b = Buffer.alloc(values.length * 2);
  values.forEach((v, i) => b.writeUInt16LE(v, i * 2));
  return b;
}

// ---------------------------------------------------------------------------
// Session keys ([MS-SMB2] 3.1.4.2) and the SMB2_TRANSFORM_HEADER (2.2.41)
// ---------------------------------------------------------------------------

function deriveSessionKeys(conn, session) {
  const di = cstate(conn).dialectInfo;
  const key = session.sessionKey;
  if (di.value >= 0x0311) {
    const ctx = session.preauth || cstate(conn).preauth;
    session.signingKey = kdfCounter(key, KDF_SIGN_311, ctx, 128);
    session.decryptKey = kdfCounter(key, KDF_C2S_311, ctx, 128);
    session.encryptKey = kdfCounter(key, KDF_S2C_311, ctx, 128);
    session.applicationKey = kdfCounter(key, KDF_APP_311, ctx, 128);
  } else if (di.value >= 0x0300) {
    session.signingKey = kdfCounter(key, KDF_SIGN_30, KDF_SIGN_30_CTX, 128);
    session.decryptKey = kdfCounter(key, KDF_CCM_30, KDF_C2S_30, 128);
    session.encryptKey = kdfCounter(key, KDF_CCM_30, KDF_S2C_30, 128);
    session.applicationKey = kdfCounter(key, KDF_APP_30, KDF_APP_30_CTX, 128);
  } else {
    // SMB 2.x: Session.SigningKey is the NTLM session key itself.
    session.signingKey = Buffer.from(key);
    session.applicationKey = Buffer.from(key);
    session.decryptKey = null;
    session.encryptKey = null;
  }
}

function aeadEncrypt(gcm, key, nonce, aad, pt) {
  const c = crypto.createCipheriv(gcm ? 'aes-128-gcm' : 'aes-128-ccm', key, nonce,
                                  { authTagLength: 16 });
  c.setAAD(aad, { plaintextLength: pt.length });
  const ct = Buffer.concat([c.update(pt), c.final()]);
  return { ct, tag: c.getAuthTag() };
}

function aeadDecrypt(gcm, key, nonce, aad, ct, tag) {
  const d = crypto.createDecipheriv(gcm ? 'aes-128-gcm' : 'aes-128-ccm', key, nonce,
                                    { authTagLength: 16 });
  d.setAuthTag(tag);
  d.setAAD(aad, { plaintextLength: ct.length });
  return Buffer.concat([d.update(ct), d.final()]);
}

// Wrap one complete PDU (or compound chain) in a transform header.
function encryptPdu(conn, session, pdu) {
  const st = cstate(conn);
  const gcm = st.cipherId === CIPHER_GCM;
  const nlen = gcm ? 12 : 11;
  const nonce = crypto.randomBytes(nlen);
  const h = Buffer.alloc(TRANSFORM_HDR);
  TRANSFORM_MAGIC.copy(h, 0);
  nonce.copy(h, 20);
  h.writeUInt32LE(pdu.length, 36);
  h.writeUInt16LE(0, 40);
  h.writeUInt16LE(TRANSFORM_FLAG_ENCRYPTED, 42);
  h.writeBigUInt64LE(session.id, 44);
  const { ct, tag } = aeadEncrypt(gcm, session.encryptKey, nonce, h.subarray(20, 52), pdu);
  tag.copy(h, 4);
  return Buffer.concat([h, ct]);
}

// Returns { pdu, session } or null when the message cannot be unwrapped.
function decryptTransform(conn, msg) {
  if (msg.length < TRANSFORM_HDR) return null;
  const st = cstate(conn);
  const flags = msg.readUInt16LE(42);
  if (flags !== TRANSFORM_FLAG_ENCRYPTED) return null;
  const sessionId = msg.readBigUInt64LE(44);
  const session = conn.sessions.get(sessionId.toString()) ||
                  conn.server.globalSessions.get(sessionId.toString());
  if (!session || !session.decryptKey) return null;
  const gcm = st.cipherId === CIPHER_GCM;
  const nlen = gcm ? 12 : 11;
  const orig = msg.readUInt32LE(36);
  try {
    const pdu = aeadDecrypt(gcm, session.decryptKey, msg.subarray(20, 20 + nlen),
                            msg.subarray(20, 52), msg.subarray(TRANSFORM_HDR), msg.subarray(4, 20));
    if (pdu.length !== orig) return null;
    return { pdu, session };
  } catch (e) {
    return null;
  }
}

// [MS-XCA] 2.4 plain LZ77.  Throws on a malformed stream; the caller drops the
// connection, which is what a bad transform deserves.
function lz77Decompress(src, hint) {
  const out = [];
  let flags = 0, flagCount = 0, ip = 0, lastHalf = -1;
  const need = (n) => { if (ip + n > src.length) throw new Error('lz77: truncated'); };
  for (;;) {
    if (flagCount === 0) {
      if (ip === src.length) break;
      need(4);
      flags = src.readUInt32LE(ip); ip += 4; flagCount = 32;
    }
    flagCount--;
    if (((flags >>> flagCount) & 1) === 0) {
      if (ip === src.length) break;
      out.push(src[ip++]);
    } else {
      if (ip === src.length) break;               // clean end of stream
      need(2);
      const m = src.readUInt16LE(ip); ip += 2;
      let len = m % 8;
      const off = ((m >>> 3) >>> 0) + 1;
      if (len === 7) {
        if (lastHalf < 0) { need(1); len = src[ip] & 0x0f; lastHalf = ip; ip += 1; }
        else { len = src[lastHalf] >>> 4; lastHalf = -1; }
        if (len === 15) {
          need(1); len = src[ip]; ip += 1;
          if (len === 255) {
            need(2); len = src.readUInt16LE(ip); ip += 2;
            if (len === 0) { need(4); len = src.readUInt32LE(ip); ip += 4; }
            if (len < 22) throw new Error('lz77: bad length');
            len -= 22;
          }
          len += 15;
        }
        len += 7;
      }
      len += 3;
      if (off > out.length) throw new Error('lz77: offset before start');
      for (let i = 0; i < len; i++) out.push(out[out.length - off]);
      if (hint && out.length > hint + 0x10000) throw new Error('lz77: runaway');
    }
  }
  return Buffer.from(out);
}

// [MS-SMB2] 2.2.42.2.2 SMB2_COMPRESSION_PATTERN_PAYLOAD_V1.
function patternV1Decompress(src) {
  if (src.length < 8) throw new Error('pattern_v1: short');
  const reps = src.readUInt32LE(4);
  if (reps > 0x1000000) throw new Error('pattern_v1: runaway');
  return Buffer.alloc(reps, src[0]);
}

function decompressSegment(alg, src, hint) {
  switch (alg) {
    case COMPRESS.NONE: return Buffer.from(src);
    case COMPRESS.LZ77: return lz77Decompress(src, hint);
    case COMPRESS.PATTERN_V1: return patternV1Decompress(src);
    default: throw new Error('compression: unsupported algorithm ' + alg);
  }
}

// Unwrap an SMB2 COMPRESSION_TRANSFORM_HEADER into the plain message, or null.
function decompressTransform(conn, msg) {
  try {
    if (msg.length < 8) return null;
    const originalSize = msg.readUInt32LE(4);
    if (originalSize > 16 * 1024 * 1024) return null;
    const flagsAt10 = msg.length >= 12 ? msg.readUInt16LE(10) : 0;
    if (msg.length >= 16 && flagsAt10 === 0x0000) {
      // 2.2.42.1 unchained: an uncompressed prefix, then one compressed run.
      const alg = msg.readUInt16LE(8);
      const off = msg.readUInt32LE(12);
      const payload = msg.subarray(16);
      if (off > payload.length) return null;
      const plain = Buffer.concat([payload.subarray(0, off),
                                   decompressSegment(alg, payload.subarray(off), originalSize)]);
      // The spec's OriginalCompressedSegmentSize is read by implementations
      // either as the whole message or as the compressed run alone; accept both.
      if (plain.length !== originalSize && plain.length - off !== originalSize) return null;
      return plain;
    }
    // 2.2.42.2 chained: a run of payload headers after the 8-byte prefix.
    const parts = [];
    let off = 8;
    while (off + 8 <= msg.length) {
      const alg = msg.readUInt16LE(off);
      const len = msg.readUInt32LE(off + 4);
      let dataOff = off + 8;
      if (alg !== COMPRESS.NONE && alg !== COMPRESS.PATTERN_V1) {
        if (dataOff + 4 > msg.length) return null;
        dataOff += 4;                       // OriginalPayloadSize
      }
      const end = off + 8 + len;
      if (end > msg.length || dataOff > end) return null;
      parts.push(decompressSegment(alg, msg.subarray(dataOff, end), originalSize));
      off = end;
    }
    if (!parts.length) return null;
    const plain = Buffer.concat(parts);
    if (plain.length !== originalSize) return null;
    return plain;
  } catch (e) {
    try { conn.log('smb2: decompress failed', e && e.message); } catch (e2) {}
    return null;
  }
}

// Put one response on the wire, encrypting it when the session demands it.
function deliver(conn, session, buf, encrypt) {
  if (conn.closed) return;
  if (encrypt && session && session.encryptKey) {
    try { conn.send(encryptPdu(conn, session, buf)); return; }
    catch (e) { conn.log('smb2: encrypt failed', e && e.message); }
  }
  conn.send(buf);
}

// --- error body ([MS-SMB2] 2.2.2) ------------------------------------------

function errorBody(data) {
  const extra = data && data.length ? data.length : 0;
  const b = Buffer.alloc(8 + Math.max(1, extra));
  b.writeUInt16LE(9, 0);
  b.writeUInt8(0, 2);
  b.writeUInt8(0, 3);
  b.writeUInt32LE(extra, 4);
  if (extra) data.copy(b, 8);
  return b;
}

// ---------------------------------------------------------------------------
// Dispatcher: compounding, credits, signing, async interim responses
// ---------------------------------------------------------------------------

function splitChain(msg) {
  const out = [];
  let off = 0;
  for (;;) {
    if (off + HDR > msg.length) break;
    const hdr = parseHeader(msg.subarray(off, msg.length));
    let len = hdr.nextCommand >>> 0;
    if (len === 0 || off + len > msg.length || len < HDR) len = msg.length - off;
    out.push({
      hdr,
      pdu: msg.subarray(off, off + len),
      body: msg.subarray(off + HDR, off + len),
      last: hdr.nextCommand === 0,
    });
    if (hdr.nextCommand === 0) break;
    off += hdr.nextCommand;
  }
  return out;
}

// "smb2 max credits" bounds the outstanding window ([MS-SMB2] 3.3.1.1).
// Measured against Samba 4.17: NEGOTIATE and every SESSION_SETUP that does not
// complete the session get one credit each; from the response that establishes
// the session onwards the client is handed what it asks for, until the total
// outstanding reaches the maximum.
const SMB2_MAX_CREDITS = 8192;

function chargeOf(st, hdr) {
  return st.supportsMultiCredit ? Math.max(1, hdr.creditCharge) : 1;
}

function hasValidSession(conn) {
  for (const sess of conn.sessions.values()) if (sess.state === 'valid') return true;
  return false;
}

function creditGrantFor(conn, hdr, established) {
  const st = cstate(conn);
  const charge = chargeOf(st, hdr);
  let grant = established ? Math.max(1, hdr.creditRequest) : charge;
  if (st.credits + grant > SMB2_MAX_CREDITS) grant = Math.max(0, SMB2_MAX_CREDITS - st.credits);
  if (grant < 1 && st.credits < 1) grant = 1;
  return grant;
}

// Consume the MessageId range a request charges.  A charge the connection has
// no credits for, or an id that has already been used, is fatal: Samba drops
// the connection rather than answering ([MS-SMB2] 3.3.5.2.3).  SMB2 CANCEL is
// exempt -- it deliberately carries the MessageId it is cancelling.
const MID_WINDOW = 8192n;

function consumeSequence(conn, hdr) {
  const st = cstate(conn);
  const charge = chargeOf(st, hdr);
  if (charge > st.credits) return false;
  st.credits -= charge;
  if (hdr.command === CMD.CANCEL) return true;
  const mid = hdr.messageId;
  if (st.midHigh > MID_WINDOW && mid < st.midHigh - MID_WINDOW) return false;
  for (let i = 0n; i < BigInt(charge); i++) {
    if (st.midSeen.has((mid + i).toString())) return false;
  }
  for (let i = 0n; i < BigInt(charge); i++) st.midSeen.add((mid + i).toString());
  const top = mid + BigInt(charge);
  if (top > st.midHigh) st.midHigh = top;
  if (st.midSeen.size > Number(MID_WINDOW) * 2) {
    const low = st.midHigh > MID_WINDOW ? st.midHigh - MID_WINDOW : 0n;
    for (const k of Array.from(st.midSeen)) if (BigInt(k) < low) st.midSeen.delete(k);
  }
  return true;
}

function buildResponse(req, status, body, opts = {}) {
  const b = body || Buffer.alloc(0);
  let flags = FLAG.SERVER_TO_REDIR;
  if (opts.async) flags |= FLAG.ASYNC_COMMAND;
  if (req.related) flags |= FLAG.RELATED_OPERATIONS;
  if (req.hdr.flags & FLAG.PRIORITY_MASK) flags |= (req.hdr.flags & FLAG.PRIORITY_MASK);
  const h = buildHeader({
    creditCharge: req.hdr.creditCharge,
    status,
    command: req.hdr.command,
    credits: opts.credits === undefined ? req.credits : opts.credits,
    flags,
    messageId: req.hdr.messageId,
    asyncId: opts.async ? req.asyncId : 0n,
    treeId: req.responseTreeId !== undefined ? req.responseTreeId : req.hdr.treeId,
    sessionId: req.responseSessionId !== undefined ? req.responseSessionId : req.hdr.sessionId,
  });
  return Buffer.concat([h, b]);
}

function sendOne(conn, session, buf, forceSign, encrypt) {
  if (forceSign || (session && session.signingKey && session.wantSign)) signMessage(conn, session, buf);
  deliver(conn, session, buf, encrypt);
}

function startAsync(req, onCancel) {
  const conn = req.conn, st = cstate(conn);
  if (req.asyncEntry) return req.asyncEntry;
  const asyncId = conn.nextAsyncId++;
  req.asyncId = asyncId;
  const entry = { asyncId, mid: req.hdr.messageId, req, done: false, cancel: onCancel || null };
  st.async.set(asyncId.toString(), entry);
  st.byMid.set(req.hdr.messageId.toString(), entry);
  req.asyncEntry = entry;
  req.asyncInterim = buildResponse(req, S.STATUS_PENDING, errorBody(null), { async: true });
  if (onCancel && onCancel.now) {
    // The handler is about to block; the client has to see STATUS_PENDING now,
    // not when the answer is finally ready ([MS-SMB2] 3.3.4.2).
    entry.interimSent = true;
    deliver(conn, req.session, req.asyncInterim, !!req.encrypted || !!req.encryptTree);
    req.asyncInterim = null;
  }
  return entry;
}

function finishAsync(req, status, body) {
  const conn = req.conn, st = cstate(conn);
  const e = req.asyncEntry;
  if (!e || e.done || conn.closed) return;
  e.done = true;
  st.async.delete(e.asyncId.toString());
  if (st.byMid.get(req.hdr.messageId.toString()) === e) st.byMid.delete(req.hdr.messageId.toString());
  const b = (status >>> 0) >= 0x80000000 && (!body || body.length === 0) ? errorBody(null) : body;
  const buf = buildResponse(req, status, b, { async: true, credits: 0 });
  // The interim STATUS_PENDING travels in the compound response that is still
  // being assembled; the final answer must not overtake it.
  const enc = !!req.encrypted || !!req.encryptTree;
  if (!e.interimSent) { e.finalPending = () => sendOne(conn, req.session, buf, false, enc); return; }
  sendOne(conn, req.session, buf, false, enc);
}

// ---------------------------------------------------------------------------

// The only entry point server.js calls.  It must never throw: an exception
// escaping here would tear down the connection dispatcher.
function handle(conn, msg) {
  try {
    if (!msg || msg.length < 4) { conn.destroy(); return; }
    let encSession = null;
    if (msg.subarray(0, 4).equals(COMPRESSION_MAGIC)) {
      // [MS-SMB2] 2.2.42: unwrap, then treat the payload as a normal message.
      cstate(conn);
      const plain = decompressTransform(conn, msg);
      if (!plain) { conn.destroy(); return; }
      msg = plain;
    }
    if (msg.length >= 4 && msg.subarray(0, 4).equals(TRANSFORM_MAGIC)) {
      cstate(conn);
      const dec = decryptTransform(conn, msg);
      if (!dec) { conn.destroy(); return; }
      msg = dec.pdu; encSession = dec.session;
    }
    if (msg.length < 4 || !msg.subarray(0, 4).equals(MAGIC)) { conn.destroy(); return; }
    cstate(conn);
    const p = processChain(conn, msg, encSession);
    if (p && typeof p.catch === 'function') {
      p.catch((e) => {
        try { conn.log('smb2: chain error', e && e.stack); } catch (e2) {}
        try { conn.destroy(); } catch (e2) {}
      });
    }
  } catch (e) {
    try { conn.log('smb2: fatal', e && e.stack); } catch (e2) {}
    try { conn.destroy(); } catch (e2) {}
  }
}

function dumpLen(conn, msg) {
  if (conn.server.debugLevel > 2) conn.log('smb2: pdu ' + msg.length + ' bytes');
}

async function processChain(conn, msg, encSession) {
  const st = cstate(conn);
  const elems = splitChain(msg);
  const chain = {
    session: null, sessionId: 0n, tree: null, treeId: 0,
    fileId: null, prevStatus: S.STATUS_SUCCESS, prevHadFileId: false, fileIdErr: 0,
    haveSession: false, haveTree: false, broken: 0,
  };
  const out = [];
  const interims = [];
  for (let i = 0; i < elems.length; i++) {
    const el = elems[i];
    if (!consumeSequence(conn, el.hdr)) { conn.destroy(); return; }
    const related = i > 0 && (el.hdr.flags & FLAG.RELATED_OPERATIONS) !== 0;
    const req = {
      conn, server: conn.server, hdr: el.hdr, body: el.body, pdu: el.pdu,
      related, chain, index: i, session: null, tree: null,
      credits: creditGrantFor(conn, el.hdr, hasValidSession(conn)),
      asyncEntry: null, asyncId: 0n, asyncInterim: null,
      signRequest: (el.hdr.flags & FLAG.SIGNED) !== 0,
      encrypted: !!encSession, encryptTree: false, onResponse: null,
    };
    let status, body = null;
    if (conn.server.debugLevel > 1) {
      conn.log('smb2: >> ' + (CMD_NAME[el.hdr.command] || el.hdr.command) +
               ' mid=' + el.hdr.messageId + ' flags=0x' + el.hdr.flags.toString(16) +
               ' sid=' + el.hdr.sessionId + ' tid=' + el.hdr.treeId +
               ' cc=' + el.hdr.creditCharge + ' cr=' + el.hdr.creditRequest +
               ' next=' + el.hdr.nextCommand + ' len=' + el.pdu.length);
    }
    try {
      const r = await runOne(req);
      if (r === null) { continue; }                 // CANCEL: no response at all
      status = r.status; body = r.body;
    } catch (e) {
      if (e instanceof SmbError) { status = e.status; body = null; }
      else { conn.log('smb2: handler error', e && e.stack); status = S.STATUS_INTERNAL_ERROR; body = null; }
    }
    // The response that completes a session setup already grants in full.
    if (el.hdr.command === CMD.SESSION_SETUP && (status >>> 0) === S.STATUS_SUCCESS &&
        req.session && req.session.state === 'valid') {
      req.credits = creditGrantFor(conn, el.hdr, true);
    }
    st.credits += req.credits;
    chain.prevStatus = status;
    // [MS-SMB2] 3.3.5.2.7.2: when an operation fails without leaving a FileId
    // behind, every later related operation that would have inherited that
    // FileId is answered with the very same error.
    if ((status >>> 0) !== S.STATUS_SUCCESS && !chain.fileId) chain.fileIdErr = status;
    else if ((status >>> 0) === S.STATUS_SUCCESS) chain.fileIdErr = 0;
    if (conn.server.debugLevel > 1) {
      conn.log('smb2: << ' + (CMD_NAME[el.hdr.command] || el.hdr.command) +
               ' mid=' + el.hdr.messageId + ' -> ' + S.name(status) +
               ' bodylen=' + (body ? body.length : 0));
    }
    if (req.asyncEntry) {
      if (req.asyncInterim) {
        out.push({ buf: req.asyncInterim, session: req.session, sign: req.wantSign, req });
        interims.push(req.asyncEntry);
      }
      // A handler that announced STATUS_PENDING and then finished anyway (an
      // open that had to wait for an oplock break) sends its real answer behind
      // the interim.
      if ((status >>> 0) !== S.STATUS_PENDING) finishAsync(req, status, body);
      continue;
    }
    if ((status >>> 0) !== S.STATUS_SUCCESS &&
        (status >>> 0) !== S.STATUS_MORE_PROCESSING_REQUIRED &&
        (status >>> 0) !== S.STATUS_BUFFER_OVERFLOW &&
        (status >>> 0) !== S.STATUS_NOTIFY_ENUM_DIR &&
        (status >>> 0) !== S.STATUS_NO_MORE_FILES && (body === null || body === undefined)) {
      body = errorBody(null);
    }
    if (body === null || body === undefined) body = errorBody(null);
    out.push({ buf: buildResponse(req, status, body), session: req.session, sign: req.wantSign, req });
  }
  if (out.length === 0) return;
  // Chain the responses together: NextCommand + 8-byte alignment, then sign
  // each PDU individually ([MS-SMB2] 3.3.4.1.1).
  const parts = [];
  for (let i = 0; i < out.length; i++) {
    let buf = out[i].buf;
    if (i < out.length - 1) {
      const padded = pad8(buf.length);
      const nb = Buffer.alloc(padded);
      buf.copy(nb, 0);
      nb.writeUInt32LE(padded, 20);
      buf = nb;
    }
    if (out[i].sign && out[i].session && out[i].session.signingKey) signMessage(conn, out[i].session, buf);
    // 3.1.1 folds the exact bytes of NEGOTIATE and the intermediate
    // SESSION_SETUP responses into the pre-auth chain ([MS-SMB2] 3.3.5.5.3).
    if (out[i].req && out[i].req.onResponse) { try { out[i].req.onResponse(buf); } catch (e) {} }
    parts.push(buf);
  }
  let encrypt = !!encSession;
  let encFor = encSession;
  for (const o of out) {
    if (o.req && o.req.encryptTree && o.session) { encrypt = true; encFor = encFor || o.session; }
  }
  if (encrypt && !encFor) {
    for (const o of out) if (o.session) { encFor = o.session; break; }
  }
  deliver(conn, encFor, Buffer.concat(parts), encrypt && !!(encFor && encFor.encryptKey));
  for (const e of interims) {
    e.interimSent = true;
    if (e.finalPending) { const f = e.finalPending; e.finalPending = null; f(); }
  }
}

const NO_SESSION_CMDS = new Set([CMD.NEGOTIATE, CMD.SESSION_SETUP, CMD.ECHO, CMD.CANCEL]);
const NO_TREE_CMDS = new Set([CMD.NEGOTIATE, CMD.SESSION_SETUP, CMD.LOGOFF, CMD.ECHO,
                              CMD.CANCEL, CMD.TREE_CONNECT]);

async function runOne(req) {
  const conn = req.conn, st = cstate(conn), hdr = req.hdr, chain = req.chain;

  if (hdr.structureSize !== 64) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (!(hdr.command in CMD_NAME)) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  // Samba does not answer anything that arrives before a successful NEGOTIATE;
  // it tears the connection down ([MS-SMB2] 3.3.5.2.7).
  if (hdr.command !== CMD.NEGOTIATE && !st.negotiated) { conn.destroy(); return null; }

  // --- session -------------------------------------------------------------
  let sessionId = hdr.sessionId;
  if (req.related) {
    if (!chain.haveSession) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    sessionId = chain.sessionId;
    req.responseSessionId = sessionId;
  }
  let session = sessionId !== 0n ? conn.sessions.get(sessionId.toString()) : null;
  if (sessionId !== 0n && !session && hdr.command !== CMD.NEGOTIATE) {
    if (hdr.command === CMD.CANCEL) return null;
    // A session-bind attempt names a session that lives on another connection;
    // below SMB 3.0 binding is not available at all ([MS-SMB2] 3.3.5.5.1).
    if (hdr.command === CMD.SESSION_SETUP && req.body.length >= 3 &&
        (req.body.readUInt8(2) & SESSION_FLAG_BINDING) &&
        conn.server.globalSessions.has(sessionId.toString())) {
      return { status: S.STATUS_REQUEST_NOT_ACCEPTED, body: null };
    }
    return { status: S.STATUS_USER_SESSION_DELETED, body: null };
  }
  req.session = session;
  if (session) { chain.haveSession = true; chain.sessionId = sessionId; chain.session = session; }
  req.responseSessionId = sessionId;

  // --- signature -----------------------------------------------------------
  // SESSION_SETUP is verified inside its own handler, after the new session
  // key has been derived ([MS-SMB2] 3.3.5.5.3).
  if (session && session.signingKey && hdr.command !== CMD.CANCEL &&
      hdr.command !== CMD.SESSION_SETUP) {
    if (req.signRequest) {
      if (!verifySignature(conn, session, req.pdu, hdr)) {
        return { status: S.STATUS_ACCESS_DENIED, body: null };
      }
      req.wantSign = true;
    } else if (session.signingRequired && session.state === 'valid') {
      return { status: S.STATUS_ACCESS_DENIED, body: null };
    }
  }
  if (session && session.signingRequired) req.wantSign = true;
  if (session) session.wantSign = req.wantSign;

  if (!NO_SESSION_CMDS.has(hdr.command)) {
    if (!session) return { status: S.STATUS_USER_SESSION_DELETED, body: null };
    if (session.state !== 'valid') return { status: S.STATUS_ACCESS_DENIED, body: null };
  }
  // Once a session is marked for encryption nothing may arrive in the clear on
  // it; only NEGOTIATE and SESSION_SETUP are outside the transform.
  if (session && session.encryptData && !req.encrypted &&
      hdr.command !== CMD.NEGOTIATE && hdr.command !== CMD.SESSION_SETUP) {
    return { status: S.STATUS_ACCESS_DENIED, body: null };
  }

  // --- tree ----------------------------------------------------------------
  let treeId = hdr.treeId;
  if (req.related) {
    if (!NO_TREE_CMDS.has(hdr.command) && !chain.haveTree) {
      return { status: S.STATUS_INVALID_PARAMETER, body: null };
    }
    treeId = chain.treeId;
    req.responseTreeId = treeId;
  }
  if (!NO_TREE_CMDS.has(hdr.command)) {
    const tree = session ? session.trees.get(treeId) : null;
    if (!tree) return { status: S.STATUS_NETWORK_NAME_DELETED, body: null };
    req.tree = tree;
    req.encryptTree = !!tree.encryptData;
    chain.haveTree = true; chain.treeId = treeId; chain.tree = tree;
  }
  req.responseTreeId = treeId;

  const fn = HANDLERS[hdr.command];
  if (!fn) return { status: S.STATUS_NOT_IMPLEMENTED, body: null };
  return await fn(req);
}

// ---------------------------------------------------------------------------
// NEGOTIATE ([MS-SMB2] 2.2.3 / 2.2.4, 3.3.5.4)
// ---------------------------------------------------------------------------

function serverDialectRange(server) {
  let min = server.minProtocol, max = server.maxProtocol;
  if (min < 0x0202) min = 0x0202;
  if (max > MAX_SUPPORTED) max = MAX_SUPPORTED;
  const out = [];
  for (const d of DIALECTS) if (d.value >= min && d.value <= max) out.push(d.value);
  return out;
}

function securityModeOf(server) {
  let mode = 0;
  if (server.signingEnabled()) mode |= SEC_SIGNING_ENABLED;
  if (server.signingRequired()) mode |= SEC_SIGNING_REQUIRED;
  if (mode === 0) mode = SEC_SIGNING_ENABLED;
  return mode;
}

// Capabilities Samba advertises at 2.x.  SMB2_GLOBAL_CAP_DFS tracks
// "host msdfs", which defaults to yes, so it is always on; the individual
// shares stay non-DFS (TREE_CONNECT keeps ShareFlags/Capabilities at 0).
function negotiateCaps(server, di, st) {
  let caps = CAP.DFS;
  if (di.leasing && server.leasesEnabled) caps |= CAP.LEASING;
  if (di.largeMtu) caps |= CAP.LARGE_MTU;
  // SMB2_GLOBAL_CAP_ENCRYPTION is mirrored back only at 3.0/3.0.2, and only to
  // a client that offered it while the configuration allows encryption; 3.1.1
  // settles the cipher in a negotiate context instead ([MS-SMB2] 3.3.5.4).
  if (di.value >= 0x0300 && di.value < 0x0311 && server.encryptPolicy() !== 'off' &&
      st && (st.clientCaps & CAP.ENCRYPTION)) {
    caps |= CAP.ENCRYPTION;
  }
  // Samba 4.17 leaves MULTI_CHANNEL and DIRECTORY_LEASING clear here even with
  // "server multi channel support = yes" -- matched against the recording.
  return caps >>> 0;
}

// [MS-SMB2] 2.2.4.  `revision` is what goes on the wire and is not always
// di.value: the multi-protocol negotiate answers "SMB 2.???" with 0x02FF while
// still sizing the response from the 2.1 dialect.
function negotiateBody(server, di, revision, st, ctxs) {
  const sec = ntlm.spnegoInit([ntlm.OID_NTLMSSP]);
  let body = Buffer.alloc(64 + sec.length);
  body.writeUInt16LE(65, 0);
  body.writeUInt16LE(securityModeOf(server), 2);
  body.writeUInt16LE(revision, 4);
  body.writeUInt16LE(0, 6);
  Buffer.from(server.guid).copy(body, 8);
  body.writeUInt32LE(negotiateCaps(server, di, st), 24);
  body.writeUInt32LE(di.maxTrans, 28);
  body.writeUInt32LE(di.maxRead, 32);
  body.writeUInt32LE(di.maxWrite, 36);
  body.writeBigUInt64LE(nowFt(), 40);
  body.writeBigUInt64LE(toFileTime(server.startTime), 48);
  body.writeUInt16LE(HDR + 64, 56);
  body.writeUInt16LE(sec.length, 58);
  body.writeUInt32LE(0, 60);
  sec.copy(body, 64);
  if (ctxs && ctxs.length) {
    // The context list starts 8-byte aligned from the start of the SMB2
    // message ([MS-SMB2] 2.2.4).
    const end = HDR + body.length;
    const ctxOff = pad8(end);
    body = Buffer.concat([body, Buffer.alloc(ctxOff - end), buildNegotiateContexts(ctxs)]);
    body.writeUInt16LE(ctxs.length, 6);       // NegotiateContextCount
    body.writeUInt32LE(ctxOff, 60);           // NegotiateContextOffset
  }
  return body;
}

// Pin the connection to `di` once a dialect has actually been agreed.
function adoptDialect(conn, di) {
  const st = cstate(conn);
  conn.proto = 'smb2';
  conn.dialect = di.value;
  st.negotiated = true;
  st.dialectInfo = di;
  st.maxTrans = di.maxTrans; st.maxRead = di.maxRead; st.maxWrite = di.maxWrite;
  st.supportsMultiCredit = di.largeMtu;
  conn.supportsChaining = true;
}

async function doNegotiate(req) {
  const conn = req.conn, st = cstate(conn), b = req.body, server = req.server;
  // A second NEGOTIATE on an already negotiated connection is fatal: Samba
  // drops the connection instead of answering ([MS-SMB2] 3.3.5.4).
  if (st.negotiated) { conn.destroy(); return null; }
  if (b.length < 36 || b.readUInt16LE(0) !== 36) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const count = b.readUInt16LE(2);
  const clientSecMode = b.readUInt16LE(4);
  const clientCaps = b.readUInt32LE(8);
  conn.clientGuid = readGuid(b, 12);
  const inCtxOff = b.readUInt32LE(28), inCtxCount = b.readUInt16LE(32);
  // DialectCount == 0 is a malformed request, not an unsupported one.
  if (count === 0) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const offered = [];
  for (let i = 0; i < count && 36 + i * 2 + 2 <= b.length; i++) offered.push(b.readUInt16LE(36 + i * 2));

  const mine = serverDialectRange(server);
  let chosen = 0;
  for (const d of offered) if (mine.includes(d) && d > chosen) chosen = d;
  if (!chosen) return { status: S.STATUS_NOT_SUPPORTED, body: null };

  const di = dialectInfo(chosen);
  st.clientCaps = clientCaps >>> 0;
  st.clientDialects = offered.slice();

  // --- 3.1.1 negotiate contexts ([MS-SMB2] 2.2.3.1, 3.3.5.4)
  let respCtxs = null;
  if (di.negContexts) {
    const rel = inCtxOff - HDR;
    const cbuf = (inCtxCount && rel >= 0 && rel <= b.length) ? b.subarray(rel) : Buffer.alloc(0);
    const inCtxs = parseNegotiateContexts(cbuf, inCtxCount);
    const pre = inCtxs.find((c) => c.type === NEGCTX.PREAUTH);
    if (!pre || pre.data.length < 4) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    const hashes = u16list(pre.data, 4, pre.data.readUInt16LE(0));
    if (hashes.indexOf(HASH_SHA512) < 0) {
      return { status: STATUS_SMB_NO_PREAUTH_INTEGRITY_HASH_OVERLAP, body: null };
    }
    respCtxs = [{
      type: NEGCTX.PREAUTH,
      data: Buffer.concat([u16blob([1, 32, HASH_SHA512]), crypto.randomBytes(32)]),
    }];
    const enc = inCtxs.find((c) => c.type === NEGCTX.ENCRYPTION);
    if (enc && enc.data.length >= 2 && server.encryptPolicy() !== 'off') {
      const want = u16list(enc.data, 2, enc.data.readUInt16LE(0));
      const cipher = want.indexOf(CIPHER_GCM) >= 0 ? CIPHER_GCM
                   : (want.indexOf(CIPHER_CCM) >= 0 ? CIPHER_CCM : 0);
      st.cipherId = cipher;
      st.encryptionOffered = cipher !== 0;
      respCtxs.push({ type: NEGCTX.ENCRYPTION, data: u16blob([1, cipher]) });
    }
    // [MS-SMB2] 2.2.4.1.7: answer a signing-capabilities context with the one
    // algorithm chosen, preferring AES-GMAC.  Without the context the client
    // stays on AES-CMAC.
    const sgn = inCtxs.find((c) => c.type === NEGCTX.SIGNING);
    if (sgn && sgn.data.length >= 2) {
      const want = u16list(sgn.data, 2, sgn.data.readUInt16LE(0));
      const algo = want.indexOf(SIGNALG_GMAC) >= 0 ? SIGNALG_GMAC
                 : (want.indexOf(SIGNALG_CMAC) >= 0 ? SIGNALG_CMAC : SIGNALG_HMAC_SHA256);
      st.signingAlgo = algo;
      respCtxs.push({ type: NEGCTX.SIGNING, data: u16blob([1, algo]) });
    }
    // A compression context is echoed only when the client asked for one.
    const cmp = inCtxs.find((c) => c.type === NEGCTX.COMPRESSION);
    if (cmp && cmp.data.length >= 8) {
      const want = u16list(cmp.data, 8, cmp.data.readUInt16LE(0));
      const agreed = [COMPRESS.LZ77, COMPRESS.PATTERN_V1].filter((a) => want.indexOf(a) >= 0);
      st.compressionAlgs = agreed;
      const d = Buffer.alloc(8 + agreed.length * 2);
      d.writeUInt16LE(agreed.length, 0);
      d.writeUInt32LE(0, 4);
      agreed.forEach((a, i) => d.writeUInt16LE(a, 8 + i * 2));
      respCtxs.push({ type: NEGCTX.COMPRESSION, data: d });
    }
  } else if (di.value >= 0x0300) {
    // 3.0 / 3.0.2 always use AES-128-CCM when both sides offer encryption.
    st.cipherId = CIPHER_CCM;
    st.encryptionOffered = server.encryptPolicy() !== 'off' &&
        (clientCaps & CAP.ENCRYPTION) !== 0;
  }

  adoptDialect(conn, di);
  conn.clientSecurityMode = clientSecMode;
  conn.negotiateRequest = Buffer.from(req.pdu);

  const body = negotiateBody(server, di, chosen, st, respCtxs);
  conn.negotiateResponse = Buffer.from(body);
  if (di.value >= 0x0311) {
    // Connection.PreauthIntegrityHashValue over the request, then the response
    // exactly as it goes out ([MS-SMB2] 3.3.5.4).
    st.preauth = sha512(Buffer.concat([Buffer.alloc(64), req.pdu]));
    req.onResponse = (buf) => { st.preauth = sha512(Buffer.concat([st.preauth, buf])); };
  }
  conn.log('smb2: negotiated dialect 0x' + chosen.toString(16) +
           ' clientcaps=0x' + st.clientCaps.toString(16) +
           ' cipher=' + st.cipherId + ' enc=' + st.encryptionOffered);
  return { status: S.STATUS_SUCCESS, body };
}

// ---------------------------------------------------------------------------
// Multi-protocol negotiate ([MS-SMB2] 3.3.5.3.1)
//
// smb1.js hands us an SMB_COM_NEGOTIATE whose dialect list mentions SMB2 and
// uses whatever we return as its own reply; returning null means "no SMB1
// reply was produced here", so we put the SMB2 NEGOTIATE response on the wire
// ourselves.  "SMB 2.???" is answered with revision 0x02FF, which asks the
// client to follow up with a real SMB2 NEGOTIATE, so the connection is switched
// to SMB2 but is *not* marked negotiated.  "SMB 2.002" without the wildcard
// settles the dialect immediately.
// ---------------------------------------------------------------------------
function negotiateFromSmb1(conn, h, dialects) {
  const server = conn.server;
  const st = cstate(conn);
  const list = Array.isArray(dialects) ? dialects : [];
  const mine = serverDialectRange(server);
  let di = null, revision = 0;
  if (list.indexOf('SMB 2.???') >= 0 && mine.indexOf(0x0210) >= 0) {
    di = dialectInfo(0x0210);
    revision = DIALECT_WILDCARD;
  } else if (list.indexOf('SMB 2.002') >= 0 && mine.indexOf(0x0202) >= 0) {
    di = dialectInfo(0x0202);
    revision = 0x0202;
  } else if (list.indexOf('SMB 2.???') >= 0 && mine.indexOf(0x0202) >= 0) {
    // The wildcard is on offer but 2.1 is out of the configured range; the
    // best SMB2 dialect left is 2.0.2.
    di = dialectInfo(0x0202);
    revision = 0x0202;
  }
  if (!di) {
    // No SMB2 dialect is acceptable.  Answer the way Samba answers a negotiate
    // it cannot satisfy: DialectIndex 0xFFFF, WordCount 1.
    const w = Buffer.alloc(2);
    w.writeUInt16LE(0xffff, 0);
    return { command: 0x72, status: 0, words: w, bytes: Buffer.alloc(0), isAndx: false };
  }

  conn.proto = 'smb2';
  conn.supportsChaining = true;
  if (revision === DIALECT_WILDCARD) {
    // Only the transport flips; the dialect is settled by the follow-up
    // SMB2 NEGOTIATE.  Size the response from 2.1 the way Samba does.
    st.dialectInfo = di;
    st.maxTrans = di.maxTrans; st.maxRead = di.maxRead; st.maxWrite = di.maxWrite;
    st.supportsMultiCredit = di.largeMtu;
  } else {
    adoptDialect(conn, di);
  }

  const body = negotiateBody(server, di, revision, st, null);
  conn.negotiateResponse = Buffer.from(body);
  const head = buildHeader({
    status: S.STATUS_SUCCESS, command: CMD.NEGOTIATE, credits: 1,
    flags: FLAG.SERVER_TO_REDIR, messageId: 0n, treeId: 0, sessionId: 0n,
  });
  conn.send(Buffer.concat([head, body]));
  conn.log('smb2: multi-protocol negotiate -> dialect 0x' + revision.toString(16));
  return null;
}

// ---------------------------------------------------------------------------
// SESSION_SETUP ([MS-SMB2] 2.2.5 / 2.2.6, 3.3.5.5)
// ---------------------------------------------------------------------------

// [MS-NLMP] 2.2.2.10: the NTLMv2 response carries a blob whose AV_PAIR list has
// to terminate inside it.  A truncated blob is a malformed request rather than
// a wrong password, and Samba answers STATUS_INVALID_PARAMETER for it.
function ntlmv2BlobOk(nt) {
  if (nt.length <= 24) return true;          // LM/NTLMv1: no blob to walk
  const blob = nt.subarray(16);
  if (blob.length < 32) return false;        // fixed header plus the AvEOL pair
  let off = 28;
  for (;;) {
    if (off + 4 > blob.length) return false;
    const id = blob.readUInt16LE(off), len = blob.readUInt16LE(off + 2);
    off += 4;
    if (id === 0) return len === 0;
    if (off + len > blob.length) return false;
    off += len;
  }
}

const NTLM_WANT = (ntlm.F.NEGOTIATE_UNICODE | ntlm.F.REQUEST_TARGET | ntlm.F.NEGOTIATE_SIGN |
  ntlm.F.NEGOTIATE_NTLM | ntlm.F.NEGOTIATE_ALWAYS_SIGN | ntlm.F.NEGOTIATE_EXTENDED_SESSIONSECURITY |
  ntlm.F.NEGOTIATE_TARGET_INFO | ntlm.F.NEGOTIATE_VERSION | ntlm.F.NEGOTIATE_128 |
  ntlm.F.NEGOTIATE_KEY_EXCH | ntlm.F.NEGOTIATE_56) >>> 0;

function sessionSetupBody(flags, sec) {
  const b = Buffer.alloc(8 + Math.max(1, sec.length));
  b.writeUInt16LE(9, 0);
  b.writeUInt16LE(flags, 2);
  b.writeUInt16LE(sec.length ? HDR + 8 : 0, 4);
  b.writeUInt16LE(sec.length, 6);
  if (sec.length) sec.copy(b, 8);
  return b;
}

async function doSessionSetup(req) {
  const conn = req.conn, server = req.server, b = req.body;
  if (b.length < 24 || b.readUInt16LE(0) !== 25) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const flags = b.readUInt8(2);
  const secMode = b.readUInt8(3);
  const previousSessionId = b.readBigUInt64LE(16);
  // SMB2_SESSION_FLAG_BINDING is a 3.x feature; at 2.x it must be rejected
  // with STATUS_REQUEST_NOT_ACCEPTED ([MS-SMB2] 3.3.5.5.1).
  if (flags & SESSION_FLAG_BINDING) {
    if (cstate(conn).dialectInfo.value < 0x0300 || !req.server.multichannel) {
      return { status: S.STATUS_REQUEST_NOT_ACCEPTED, body: null };
    }
    // The session already lives on this connection, so there is nothing to
    // bind ([MS-SMB2] 3.3.5.5.1).
    if (req.session) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  }
  const secOff = b.readUInt16LE(12), secLen = b.readUInt16LE(14);
  const start = secOff - HDR;
  const blob = (secLen && start >= 0 && start + secLen <= b.length)
    ? b.subarray(start, start + secLen) : Buffer.alloc(0);

  let session = req.session;
  if (!session) {
    if (req.hdr.sessionId !== 0n) return { status: S.STATUS_USER_SESSION_DELETED, body: null };
    const id = server.nextGlobalSession++;
    session = new Session(server, conn, id);
    conn.sessions.set(id.toString(), session);
    server.globalSessions.set(id.toString(), session);
    req.session = session;
    req.responseSessionId = id;
  } else if (session.state === 'valid') {
    // Re-authentication on an established session: start over.
    session.state = 'in-progress';
  }
  session.requestedSecMode = secMode;

  // 3.1.1 pre-auth integrity: every SESSION_SETUP request, and every response
  // that is not the last one, joins the chain ([MS-SMB2] 3.3.5.5.3).
  const preauth311 = cstate(conn).dialectInfo.value >= 0x0311;
  if (preauth311) {
    session.preauth = sha512(Buffer.concat([session.preauth || cstate(conn).preauth,
                                            Buffer.from(req.pdu)]));
  }

  const un = ntlm.unwrap(blob);
  let token = un.token;
  if (un.kind === 'empty' || !token || token.length < 12) {
    return { status: S.STATUS_LOGON_FAILURE, body: null };
  }
  if (!token.subarray(0, 8).equals(ntlm.SIG)) return { status: S.STATUS_LOGON_FAILURE, body: null };
  const msgType = token.readUInt32LE(8);
  const wrapped = un.kind !== 'raw';

  if (msgType === 1) {
    const clientFlags = token.length >= 16 ? token.readUInt32LE(12) : 0;
    let neg = (clientFlags & NTLM_WANT) >>> 0;
    neg |= (ntlm.F.NEGOTIATE_UNICODE | ntlm.F.REQUEST_TARGET | ntlm.F.NEGOTIATE_NTLM |
            ntlm.F.NEGOTIATE_ALWAYS_SIGN | ntlm.F.NEGOTIATE_EXTENDED_SESSIONSECURITY |
            ntlm.F.NEGOTIATE_TARGET_INFO | ntlm.F.TARGET_TYPE_SERVER | ntlm.F.NEGOTIATE_VERSION) >>> 0;
    neg = neg >>> 0;
    session.negFlags = neg;
    session.challenge = crypto.randomBytes(8);
    const chal = ntlm.buildChallenge(server.netbiosName, server.workgroup, session.challenge, neg);
    const out = wrapped ? ntlm.spnegoResp(1, ntlm.OID_NTLMSSP, chal, null) : chal;
    if (preauth311) {
      req.onResponse = (buf) => {
        session.preauth = sha512(Buffer.concat([session.preauth, buf]));
      };
    }
    return { status: S.STATUS_MORE_PROCESSING_REQUIRED, body: sessionSetupBody(0, out) };
  }

  if (msgType !== 3) return { status: S.STATUS_LOGON_FAILURE, body: null };
  const auth = ntlm.parseAuthenticate(token);
  if (!auth || !session.challenge) return { status: S.STATUS_LOGON_FAILURE, body: null };
  const uname = String(auth.user || '').toLowerCase();
  if (req.server.debugLevel > 1) {
    req.conn.log('smb2: ssetup auth user=' + JSON.stringify(auth.user) +
                 ' dom=' + JSON.stringify(auth.domain) +
                 ' lm=' + auth.lm.length + ' nt=' + auth.nt.length +
                 ' flags=0x' + (auth.flags >>> 0).toString(16));
  }
  // An NTLMSSP AUTHENTICATE with no name and no responses is an anonymous
  // logon; it succeeds and yields a null session ([MS-NLMP] 3.2.5.1.2,
  // [MS-SMB2] 2.2.6 SMB2_SESSION_FLAG_IS_NULL).  "map to guest" governs failed
  // passwords, not this.
  // An empty name is an unknown name too, so under `map to guest = bad user` it
  // maps to guest rather than to the anonymous null session `never` produces
  // ([MS-SMB2] 2.2.6 SessionFlags; smb.conf(5) `map to guest`).
  const anonymous = uname === '' && auth.nt.length === 0 && auth.lm.length <= 1;
  const look = server.lookupUser(uname);
  const guest = look.status === 'guest';
  const nullSession = anonymous && !guest;
  const user = guest ? look.user
    : (nullSession ? { name: '', uid: 0, nt: null, anonymous: true } : look.user);
  if (!user || (!nullSession && !guest && (!user.nt || user.disabled))) {
    return { status: S.STATUS_LOGON_FAILURE, body: null };
  }
  if (!nullSession && !guest && !ntlmv2BlobOk(auth.nt)) {
    return { status: S.STATUS_INVALID_PARAMETER, body: null };
  }
  if (!nullSession && !guest) {
    const v = ntlm.verify(auth, session.challenge, user.nt, server.workgroup);
    if (!v.ok) {
      // `map to guest = bad password` catches a real account with the wrong
      // password as well; anything else is a logon failure.
      if (!server.mapsBadPassword() || !server.guestUser) {
        return { status: S.STATUS_LOGON_FAILURE, body: null };
      }
      session.guestFallback = true;
    } else {
      session.ntlmSessionKey = v.sessionKey;
    }
  }
  if (nullSession || guest || session.guestFallback) session.ntlmSessionKey = Buffer.alloc(16);

  const asGuest = guest || !!session.guestFallback;
  session.user = asGuest && server.guestUser ? server.guestUser : user;
  session.guest = asGuest;
  if (asGuest) session.flags |= SESSION_FLAG_IS_GUEST;
  else if (nullSession) session.flags |= SESSION_FLAG_IS_NULL;
  // [MS-SMB2] 3.3.5.5.3 steps 3/7/8: a re-authentication of an established
  // session keeps Session.SessionKey and every key derived from it, so the
  // signatures either side is already using stay valid across the exchange.
  if (!session.sessionKey) {
    const nk = session.ntlmSessionKey;
    session.sessionKey = Buffer.alloc(16);
    nk.copy(session.sessionKey, 0, 0, Math.min(16, nk.length));
    // 2.x keeps the raw NTLM session key; 3.x derives ([MS-SMB2] 3.1.4.2).
    // A null or guest session has no usable key, so nothing is signed on it.
    if (nullSession || asGuest) {
      session.signingKey = null; session.encryptKey = null; session.decryptKey = null;
    } else {
      deriveSessionKeys(conn, session);
    }
  }
  if (server.encryptPolicy() === 'required') {
    // [MS-SMB2] 3.3.5.5.3: with encryption required for the whole server, a
    // session that cannot be encrypted is refused and not left behind -- a
    // client that tries to use it afterwards is told the session is gone.
    if (!canEncrypt(conn, session)) {
      closeSession(server, session);
      conn.sessions.delete(session.id.toString());
      return { status: S.STATUS_ACCESS_DENIED, body: null };
    }
    session.encryptData = true;
    session.flags |= SESSION_FLAG_ENCRYPT_DATA;
  }
  session.signingRequired = !nullSession && !asGuest && (server.signingRequired() ||
      (session.requestedSecMode & SEC_SIGNING_REQUIRED) !== 0);
  session.state = 'valid';

  if (req.signRequest && !verifySignature(conn, session, req.pdu, req.hdr)) {
    session.state = 'in-progress';
    session.signingKey = null;
    return { status: S.STATUS_ACCESS_DENIED, body: null };
  }
  req.wantSign = true;
  session.wantSign = true;
  // A client that reconnects names the session it is replacing; that one is
  // torn down ([MS-SMB2] 3.3.5.5.3).
  if (previousSessionId !== 0n && previousSessionId !== session.id) {
    const prev = req.server.globalSessions.get(previousSessionId.toString());
    if (prev && prev.user && prev.user.name === user.name) closeSession(req.server, prev);
  }
  conn.log('smb2: session ' + session.id + ' authenticated as ' + user.name);

  const out = wrapped ? ntlm.spnegoResp(0, null, null, null) : Buffer.alloc(0);
  return { status: S.STATUS_SUCCESS, body: sessionSetupBody(session.flags, out) };
}

// ---------------------------------------------------------------------------
// LOGOFF / TREE_CONNECT / TREE_DISCONNECT / ECHO / CANCEL
// ---------------------------------------------------------------------------

function simple4(code) {
  const b = Buffer.alloc(4);
  b.writeUInt16LE(code, 0);
  return b;
}

function closeSession(server, session) {
  for (const c of session.connections) {
    for (const t of Array.from(session.trees.values())) closeTree(c, t);
  }
  for (const t of Array.from(session.trees.values())) {
    for (const o of Array.from(t.opens.values())) { try { removeOpen(session.conn, o); } catch (e) {} }
  }
  session.trees.clear();
  session.opens.clear();
  session.state = 'expired';
  server.globalSessions.delete(session.id.toString());
  for (const c of session.connections) c.sessions.delete(session.id.toString());
  session.connections.clear();
}

async function doLogoff(req) {
  const session = req.session;
  if (!session) return { status: S.STATUS_USER_SESSION_DELETED, body: null };
  closeSession(req.server, session);
  req.conn.sessions.delete(session.id.toString());
  return { status: S.STATUS_SUCCESS, body: simple4(4) };
}

function closeTree(conn, tree) {
  for (const o of Array.from(tree.opens.values())) {
    try { removeOpen(conn, o); } catch (e) {}
  }
  tree.opens.clear();
  conn.trees.delete(tree.id);
}

// Samba reports the standard rights in full even on a read-only share (the
// read-only check happens at open time, not here); only the file-specific half
// is trimmed to the read subset.  Measured: 0x001F00A9.
// A connection can encrypt once a 3.x dialect agreed a cipher, both sides were
// willing, and the session has derived its keys.
function canEncrypt(conn, session) {
  const st = cstate(conn);
  if (st.dialectInfo.value < 0x0300) return false;
  if (conn.server.encryptPolicy() === 'off') return false;
  // The client has to be willing as well: a cipher agreed in a 3.1.1 negotiate
  // context is not enough if SMB2_GLOBAL_CAP_ENCRYPTION was never offered.
  if (!st.cipherId || !(st.clientCaps & CAP.ENCRYPTION)) return false;
  return !!(session && session.encryptKey);
}

const READONLY_MAXIMAL = (A.FILE_READ_DATA | A.FILE_READ_EA | A.FILE_EXECUTE |
  A.FILE_READ_ATTRIBUTES | A.DELETE | A.READ_CONTROL | A.WRITE_DAC | A.WRITE_OWNER |
  A.SYNCHRONIZE) >>> 0;

async function doTreeConnect(req) {
  const conn = req.conn, server = req.server, b = req.body;
  if (!req.session || req.session.state !== 'valid') return { status: S.STATUS_USER_SESSION_DELETED, body: null };
  if (b.length < 8 || b.readUInt16LE(0) !== 9) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const off = b.readUInt16LE(4), len = b.readUInt16LE(6);
  const start = off - HDR;
  if (start < 0 || len === 0 || start + len > b.length) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const full = unistr(b.subarray(start, start + len));
  const parts = full.replace(/^\\\\/, '').split('\\');
  const name = parts.length >= 2 ? parts[parts.length - 1] : full;
  if (!name) return { status: S.STATUS_BAD_NETWORK_NAME, body: null };

  const lname = name.toLowerCase();
  // smb.conf(5) `guest ok`: a guest session only reaches a share that allows it.
  if (req.session.guest && !server.guestMayConnect(name)) {
    return { status: S.STATUS_ACCESS_DENIED, body: null };
  }
  let share = null, isIpc = false;
  if (lname === 'ipc$') isIpc = true;
  else {
    share = server.shares.get(lname);
    if (!share) return { status: S.STATUS_BAD_NETWORK_NAME, body: null };
    if (!fs.existsSync(share.root)) return { status: S.STATUS_BAD_NETWORK_NAME, body: null };
  }

  // [MS-SMB2] 3.3.5.7: a share that requires encryption is refused outright to
  // a connection that cannot encrypt, and otherwise carries ENCRYPT_DATA.
  let shareFlags = SHAREFLAG_MANUAL_CACHING;
  let encryptData = false;
  if (server.encryptPolicy(name) === 'required') {
    if (!canEncrypt(conn, req.session)) return { status: S.STATUS_ACCESS_DENIED, body: null };
    encryptData = true;
    shareFlags |= SHAREFLAG_ENCRYPT_DATA;
  }

  const id = conn.nextTreeId++;
  const maximal = isIpc ? 0x001f00a9 : (share.readOnly ? READONLY_MAXIMAL : 0x001f01ff);
  const tree = new Tree(id, share, req.session, { isIpc, maximalAccess: maximal, name, encryptData });
  req.session.trees.set(id, tree);
  conn.trees.set(id, tree);
  req.responseTreeId = id;
  req.tree = tree;
  req.chain.haveTree = true; req.chain.treeId = id; req.chain.tree = tree;

  const body = Buffer.alloc(16);
  body.writeUInt16LE(16, 0);
  body.writeUInt8(isIpc ? SHARE_TYPE_PIPE : SHARE_TYPE_DISK, 2);
  body.writeUInt8(0, 3);
  body.writeUInt32LE(shareFlags, 4);
  body.writeUInt32LE(0, 8);
  body.writeUInt32LE(maximal, 12);
  return { status: S.STATUS_SUCCESS, body };
}

async function doTreeDisconnect(req) {
  if (!req.tree) return { status: S.STATUS_NETWORK_NAME_DELETED, body: null };
  req.session.trees.delete(req.tree.id);
  closeTree(req.conn, req.tree);
  return { status: S.STATUS_SUCCESS, body: simple4(4) };
}

async function doEcho(req) {
  return { status: S.STATUS_SUCCESS, body: simple4(4) };
}

async function doCancel(req) {
  const st = cstate(req.conn);
  let entry = null;
  if (req.hdr.flags & FLAG.ASYNC_COMMAND) entry = st.async.get(req.hdr.asyncId.toString());
  if (!entry) entry = st.byMid.get(req.hdr.messageId.toString());
  if (entry && entry.cancel) { try { entry.cancel(); } catch (e) {} }
  return null;
}

// ---------------------------------------------------------------------------
// Oplocks and leases ([MS-SMB2] 2.2.23, 3.3.1.4, 3.3.4.6, 3.3.4.7)
// ---------------------------------------------------------------------------

const LEASE_BREAK_ACK_REQUIRED = 0x01;
const LEASE_BREAK_IN_PROGRESS = 0x02;
     // SMB2_LEASE_FLAG_BREAK_IN_PROGRESS
// An unacknowledged lease break takes effect on its own after this long; a
// later acknowledgement is then refused ([MS-SMB2] 3.3.2.2).
const LEASE_BREAK_TIMEOUT_MS = 3000;

// Arm (or re-arm) the timer that makes an unacknowledged break take effect.
function armLeaseBreak(lease) {
  if (lease.breakTimer) clearTimeout(lease.breakTimer);
  lease.breakTimer = setTimeout(() => {
    lease.breakTimer = null;
    if (!lease.breaking) return;
    setLeaseState(lease, 0);         // [MS-SMB2] 3.3.2.2: the lease is revoked
    lease.breaking = false;
    const w = (lease.waiters || []).slice();
    lease.waiters = [];
    for (const f of w) { try { f(); } catch (e) {} }
  }, LEASE_BREAK_TIMEOUT_MS);
  if (lease.breakTimer.unref) lease.breakTimer.unref();
}

// [MS-FSA] 2.1.5.1 path syntax: empty, "." and ".." components are a syntax
// error, which is a different status from an illegal character.
function pathSyntaxStatus(name) {
  const rel = V.toUnix(name);
  if (rel === '') return 0;
  for (const part of rel.split('/')) {
    if (part === '' || part === '.' || part === '..') return S.STATUS_OBJECT_PATH_SYNTAX_BAD;
  }
  return 0;
}

// --- 8.3 name mangling -----------------------------------------------------
// vfs.mangle() produces a plain truncation, which is not reversible: two
// different long names can map onto the same short name and a short name is
// indistinguishable from an ordinary one.  Like Samba's "hash2" scheme we use
// <2 chars><4 base-36 hash chars>~1[.EXT], which always carries a tilde, so a
// name arriving from the client can be recognised as mangled.
const B36 = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ';
const CLEAN83 = /[^A-Za-z0-9_^$!#%&\-{}@'()]/g;

function is83(name) {
  if (name === '.' || name === '..') return true;
  if (/[^A-Za-z0-9_^$~!#%&\-{}@'().]/.test(name)) return false;
  const dot = name.indexOf('.');
  if (dot !== name.lastIndexOf('.')) return false;
  if (dot === 0) return false;
  const base = dot < 0 ? name : name.slice(0, dot);
  const ext = dot < 0 ? '' : name.slice(dot + 1);
  return base.length >= 1 && base.length <= 8 && ext.length <= 3;
}

function shortNameOf(name) {
  const dot = name.lastIndexOf('.');
  let base = (dot > 0 ? name.slice(0, dot) : name).replace(CLEAN83, '').toUpperCase();
  const ext = (dot > 0 ? name.slice(dot + 1) : '').replace(CLEAN83, '').toUpperCase().slice(0, 3);
  if (base.length === 0) base = 'X';
  const h = crypto.createHash('md5').update(Buffer.from(name, 'utf8')).digest();
  let v = h.readUInt32BE(0) % (36 * 36 * 36 * 36);
  let tag = '';
  for (let i = 0; i < 4; i++) { tag = B36[v % 36] + tag; v = Math.floor(v / 36); }
  const short = base.slice(0, 2) + tag + '~1';
  return ext ? short + '.' + ext : short;
}

// A name that already fits 8.3 is its own alternate name, verbatim -- Samba
// does not upper-case it.  Only mangled names are generated (and
// patchShortName leaves the short-name field of a directory entry empty for
// an 8.3 name, so listings are unaffected).
function altNameOf(name) { return is83(name) ? name : shortNameOf(name); }

// Cache of the 8.3 names handed out, so a name the client sends back can be
// un-mangled even after the long name has gone.
function rememberMangled(share, longName) {
  if (!longName || longName === '.' || longName === '..' || is83(longName)) return;
  if (!share._mangleCache) share._mangleCache = new Map();
  share._mangleCache.set(shortNameOf(longName).toUpperCase(), longName);
}

// Resolve a client-supplied name, falling back to a reverse 8.3 lookup so a
// name we handed out as the short name in a directory listing can be reopened.
function resolveName(share, name, canCreate) {
  const rel = share.resolve(name);
  if (rel === '' || share.stat(rel)) return rel;
  const base = path.basename(rel);
  const dirRel = path.dirname(rel) === '.' ? '' : path.dirname(rel);
  if (base.length > 12 || base.indexOf('~') < 1) return rel;
  const want = base.toUpperCase();
  let cand = null;
  const cached = share._mangleCache ? share._mangleCache.get(want) : null;
  if (cached) cand = dirRel === '' ? cached : dirRel + '/' + cached;
  if (!cand) {
    let entries;
    try { entries = fs.readdirSync(share.real(dirRel)); } catch (e) { return rel; }
    for (const e of entries) {
      if (!is83(e) && shortNameOf(e).toUpperCase() === want) {
        cand = dirRel === '' ? e : dirRel + '/' + e; break;
      }
    }
  }
  if (!cand) return rel;
  // A request that may create must never be redirected onto a file that is
  // already there; only a stale 8.3 alias is un-mangled in that case.
  if (canCreate && share.stat(cand)) return rel;
  return cand;
}

function nodeOf(share, rel) {
  const st = share.stat(rel);
  const key = st ? `${st.dev}:${st.ino}` : 'p:' + rel.toLowerCase();
  return share.nodes.get(key) || null;
}

function connOfOpen(open) {
  const s = open.session;
  if (!s) return null;
  if (s.conn && !s.conn.closed) return s.conn;
  for (const c of s.connections) if (!c.closed) return c;
  return null;
}

function sendOplockBreakNotify(open, newLevel) {
  const conn = connOfOpen(open);
  if (!conn) return false;
  const body = Buffer.alloc(24);
  body.writeUInt16LE(24, 0);
  body.writeUInt8(newLevel, 2);
  body.writeUInt8(0, 3);
  body.writeUInt32LE(0, 4);
  body.writeBigUInt64LE(open.persistentId, 8);
  body.writeBigUInt64LE(open.fileId, 16);
  const h = buildHeader({
    status: 0, command: CMD.OPLOCK_BREAK, credits: 0, flags: FLAG.SERVER_TO_REDIR,
    messageId: 0xffffffffffffffffn, treeId: 0,
    sessionId: open.session ? open.session.id : 0n,
  });
  conn.send(Buffer.concat([h, body]));
  return true;
}

// [MS-SMB2] 2.2.13.2.10 / 3.3.1.4: a v2 lease carries an epoch, and Samba
// 4.17.12 grants a new one at 1 rather than 0 and advances it whenever the
// lease state actually moves -- a request for the state the lease already has,
// or for less than it has, leaves both alone.  A v1 lease context has no epoch
// field at all, so its epoch is never anything but 0.  (Measured: grant R = 1,
// then raising the same key to RWH = 2.)
function setLeaseState(lease, next) {
  const cur = lease.state >>> 0;
  next = next >>> 0;
  if (cur === next) return;
  lease.state = next;
  if (lease.v2) lease.epoch = ((lease.epoch || 0) + 1) & 0xffff;
}

function sendLeaseBreakNotify(conn, leaseKey, curState, newState, ackRequired, epoch) {
  if (conn && conn.server && conn.server.debugLevel > 1) {
    conn.log('smb2: lease-break ' + guidStr(leaseKey).slice(0, 8) +
             ' ' + curState + '->' + newState + ' ack=' + (ackRequired ? 1 : 0));
  }
  if (!conn || conn.closed) return false;
  const body = Buffer.alloc(44);
  body.writeUInt16LE(44, 0);
  body.writeUInt16LE(epoch || 0, 2);
  body.writeUInt32LE(ackRequired ? LEASE_BREAK_ACK_REQUIRED : 0, 4);
  Buffer.from(leaseKey).copy(body, 8);
  body.writeUInt32LE(curState >>> 0, 24);
  body.writeUInt32LE(newState >>> 0, 28);
  body.writeUInt32LE(0, 32);      // BreakReason
  body.writeUInt32LE(0, 36);      // AccessMaskHint
  body.writeUInt32LE(0, 40);      // ShareMaskHint
  const h = buildHeader({
    status: 0, command: CMD.OPLOCK_BREAK, credits: 0, flags: FLAG.SERVER_TO_REDIR,
    messageId: 0xffffffffffffffffn, treeId: 0, sessionId: 0n,
  });
  conn.send(Buffer.concat([h, body]));
  return true;
}

// Samba draws the "stat open" line in two places: an open that asks for nothing
// beyond attribute access breaks no oplock (is_oplock_stat_open), and the same
// set plus READ_CONTROL breaks no lease (is_lease_stat_open).
const OPLOCK_STAT_BITS = (A.SYNCHRONIZE | A.FILE_READ_ATTRIBUTES | A.FILE_WRITE_ATTRIBUTES) >>> 0;
const LEASE_STAT_BITS = (OPLOCK_STAT_BITS | A.READ_CONTROL) >>> 0;
const onlyBits = (access, bits) => {
  const a = access >>> 0;
  return (a & bits) !== 0 && (a & ~bits) === 0;
};
const isOplockStatOpen = (access) => onlyBits(access, OPLOCK_STAT_BITS);
const isLeaseStatOpen = (access) => onlyBits(access, LEASE_STAT_BITS);

function writeishAccess(access, disp, createOptions) {
  if (access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.DELETE |
                A.FILE_WRITE_ATTRIBUTES | A.WRITE_DAC | A.WRITE_OWNER)) return true;
  if (disp === DISP.SUPERSEDE || disp === DISP.OVERWRITE || disp === DISP.OVERWRITE_IF) return true;
  if (createOptions & OPT.DELETE_ON_CLOSE) return true;
  return false;
}

// Break every oplock/lease on `node` that conflicts with the incoming open.
// Returns a promise that settles once the holders have acknowledged (or the
// acknowledgement timer expires).
function breakForNewOpen(node, opts) {
  if (!node) return null;
  if (opts.oplockStatOpen && opts.leaseStatOpen) return null;   // disturbs nothing
  // An open that is going to be told the file is already going away disturbs
  // nothing either.
  if (opts.deletePending) return null;
  const waits = [];
  const heavy = writeishAccess(opts.access, opts.disposition, opts.createOptions);
  // Samba's delay_for_oplock() picks the break level from the disposition
  // alone (`will_overwrite`): an opener that merely opens the file knocks a
  // batch/exclusive oplock down to level II, never straight to none, however
  // much access it asked for.
  const willOverwrite = opts.disposition === DISP.SUPERSEDE ||
    opts.disposition === DISP.OVERWRITE || opts.disposition === DISP.OVERWRITE_IF;

  // --- oplocks
  const ol = node.oplock;
  // On a sharing violation only a batch oplock is worth breaking: its holder
  // caches handles and can close the file to let the opener in, which an
  // exclusive holder cannot.
  const skipForShare = opts.sharingViolation && (!ol || ol.level !== OPLOCK.BATCH);
  if (!opts.oplockStatOpen && !skipForShare && ol && ol.open && !ol.open.closed &&
      (ol.level === OPLOCK.EXCLUSIVE || ol.level === OPLOCK.BATCH)) {
    const target = willOverwrite ? OPLOCK.NONE : OPLOCK.II;
    node.oplock = null;
    ol.open.oplockLevel = target;
    ol.open.oplockBreaking = target;
    if (target === OPLOCK.II) node.oplockII = (node.oplockII || new Set()).add(ol.open);
    if (sendOplockBreakNotify(ol.open, target)) {
      waits.push(waitFor(ol.open, 'oplockAck'));
    }
  } else if (ol && ol.open && ol.open.closed) {
    node.oplock = null;
  }

  // --- leases
  // The holder keeps everything but write caching; an overwriting open takes
  // handle caching away too ([MS-SMB2] 3.3.4.7, matching Samba's
  // delay_for_oplock()).  Measured truth table from smb2.lease.break:
  //   held R -> R, RH -> RH, RW -> R, RHW -> RH for a plain open.
  for (const [key, lease] of (opts.leaseStatOpen ? [] : Array.from(node.leases))) {
    if (opts.leaseKeyHex && key === opts.leaseKeyHex) continue;
    if (!Array.from(lease.opens).some((o) => !o.closed)) continue;
    const cur = lease.state >>> 0;
    if (opts.sharingViolation && !(cur & LEASE.HANDLE)) continue;
    const next = (willOverwrite ? (cur & ~(LEASE.WRITE | LEASE.HANDLE))
                                : (cur & ~LEASE.WRITE)) >>> 0;
    if (next === cur) continue;               // nothing to take away: stay quiet
    if (lease.breaking) { waits.push(waitForLease(lease)); continue; }
    const holder = lease.opens.size ? lease.opens.values().next().value : null;
    const c = holder ? connOfOpen(holder) : null;
    const ackRequired = (cur & (LEASE.WRITE | LEASE.HANDLE)) !== 0;
    lease.breakingTo = next;
    lease.breaking = ackRequired;
    if (!ackRequired) setLeaseState(lease, next);
    if (sendLeaseBreakNotify(c, lease.key, cur, next, ackRequired,
                             lease.v2 ? lease.epoch : 0)) {
      if (ackRequired) { armLeaseBreak(lease); waits.push(waitForLease(lease)); }
    } else {
      setLeaseState(lease, next); lease.breaking = false;
    }
  }
  if (!waits.length) return null;
  return Promise.all(waits);
}

// A write from a handle that does not itself hold the caching right takes read
// caching away from everyone else: level II oplocks drop to none and every
// other lease breaks to none ([MS-SMB2] 3.3.4.6 / 3.3.4.7).  Measured against
// Samba: a lease at R breaks to 0 with no acknowledgement asked for, one at RH
// breaks to 0 with SMB2_NOTIFY_BREAK_LEASE_FLAG_ACK_REQUIRED set.
function breakForWrite(open) {
  const node = open.node;
  if (!node) return;
  const dbgConn = connOfOpen(open);
  if (dbgConn && dbgConn.server.debugLevel > 1) {
    dbgConn.log('smb2: write-break from key=' + (open.leaseKeyHex || '-').slice(0, 8) +
                ' leases=' + Array.from(node.leases.entries())
                  .map(([k, l]) => k.slice(0, 8) + ':' + l.state + (l.breaking ? 'B' : '')).join(','));
  }
  // Level II is read caching: a write invalidates it for every holder, the
  // writer's own handle included ("self break to none").
  if (node.oplockII) {
    for (const o of Array.from(node.oplockII)) {
      node.oplockII.delete(o);
      if (o.closed) continue;
      o.oplockLevel = OPLOCK.NONE;
      sendOplockBreakNotify(o, OPLOCK.NONE);
    }
  }
  const ol = node.oplock;
  if (ol && ol.open && ol.open !== open && !ol.open.closed) {
    node.oplock = null;
    ol.open.oplockLevel = OPLOCK.NONE;
    ol.open.oplockBreaking = OPLOCK.NONE;
    sendOplockBreakNotify(ol.open, OPLOCK.NONE);
  }
  for (const [key, lease] of Array.from(node.leases)) {
    if (open.leaseKeyHex && key === open.leaseKeyHex) continue;
    if (!Array.from(lease.opens).some((o) => !o.closed)) continue;
    const cur = lease.state >>> 0;
    if (cur === 0 || lease.breaking) continue;
    const holder = lease.opens.values().next().value;
    const c = holder ? connOfOpen(holder) : null;
    const ackRequired = (cur & (LEASE.WRITE | LEASE.HANDLE)) !== 0;
    lease.breakingTo = 0;
    lease.breaking = ackRequired;
    if (!ackRequired) setLeaseState(lease, 0);
    if (sendLeaseBreakNotify(c, lease.key, cur, 0, ackRequired,
                             lease.v2 ? lease.epoch : 0)) {
      if (ackRequired) armLeaseBreak(lease);
    } else {
      setLeaseState(lease, 0); lease.breaking = false;
    }
  }
}

function waitFor(open, flag) {
  return new Promise((resolve) => {
    open[flag + 'Waiters'] = open[flag + 'Waiters'] || [];
    const t = setTimeout(() => { finish(); }, OPLOCK_BREAK_WAIT_MS);
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      clearTimeout(t);
      const list = open[flag + 'Waiters'] || [];
      const i = list.indexOf(finish);
      if (i >= 0) list.splice(i, 1);
      resolve();
    };
    open[flag + 'Waiters'].push(finish);
  });
}

function waitForLease(lease) {
  return new Promise((resolve) => {
    lease.waiters = lease.waiters || [];
    let done = false;
    const t = setTimeout(() => {
      // [MS-SMB2] 3.3.2.2 Lease Break Acknowledgment Timer: an unanswered
      // break revokes the lease outright, and a later acknowledgement is
      // refused.
      if (lease.breaking) {
        setLeaseState(lease, 0);
        lease.breaking = false;
        if (lease.breakTimer) { clearTimeout(lease.breakTimer); lease.breakTimer = null; }
      }
      finish();
    }, OPLOCK_BREAK_WAIT_MS);
    const finish = () => {
      if (done) return;
      done = true;
      clearTimeout(t);
      const i = (lease.waiters || []).indexOf(finish);
      if (i >= 0) lease.waiters.splice(i, 1);
      resolve();
    };
    lease.waiters.push(finish);
  });
}

function fireWaiters(holder, flag) {
  const list = holder[flag + 'Waiters'];
  if (!list) return;
  holder[flag + 'Waiters'] = [];
  for (const f of list) { try { f(); } catch (e) {} }
}

// Decide what oplock/lease the new open gets.
function grantOplock(req, open, requested, leaseCtx) {
  const node = open.node, conn = req.conn, st = cstate(conn);
  // A stat open breaks nothing, but it still counts as a holder when deciding
  // what the next open may cache.
  const others = [];
  for (const o of node.opens) if (o !== open && !o.closed) others.push(o);

  // A directory lease needs SMB2_GLOBAL_CAP_DIRECTORY_LEASING, which this
  // server does not advertise, so a directory gets no lease at all.
  if (leaseCtx && st.dialectInfo.leasing && req.server.leasesEnabled && !open.isDir) {
    const keyHex = guidStr(leaseCtx.key);
    let lease = node.leases.get(keyHex);
    let want = leaseCtx.state & (LEASE.READ | LEASE.HANDLE | LEASE.WRITE);
    if (want & LEASE.WRITE) want |= LEASE.READ;
    if (want & LEASE.HANDLE) want |= LEASE.READ;
    let granted = want;
    // Any other handle on the file that is not part of this lease means write
    // caching cannot be handed out: smb2.lease.break's table is exactly
    // "granted = requested & ~WRITE" whenever a foreign holder exists.
    const foreignHolders = others.filter((o) => o.leaseKeyHex !== keyHex);
    const foreignHolder = foreignHolders.length > 0;
    if (foreignHolder) granted = want & ~LEASE.WRITE;
    // A holder that is not itself a lease -- a plain open or a classic oplock --
    // cannot participate in handle caching, so nobody gets it either.
    if (foreignHolders.some((o) => !o.leaseKeyHex)) granted &= ~LEASE.HANDLE;
    if (!lease) {
      lease = { key: Buffer.from(leaseCtx.key), state: granted,
                epoch: leaseCtx.v2 ? 1 : 0,
                v2: !!leaseCtx.v2, opens: new Set(), breaking: false,
                node, keyHex };
      node.leases.set(keyHex, lease);
    } else {
      // A second open under the same key never breaks the lease; it joins it
      // ([MS-SMB2] 3.3.5.9.11).  A lease that was broken all the way to none
      // caches nothing, so this open re-acquires from scratch; otherwise the
      // lease may only be raised while nothing else holds the file, and the
      // caller is handed whatever the lease was broken down to.
      if ((lease.state >>> 0) === 0) {
        setLeaseState(lease, granted >>> 0);
      } else if (!foreignHolder) {
        // Only a request that is a strict superset of what the lease already
        // caches raises it; anything else leaves the lease where it is.
        // Measured: RH then RW stays RH, RH then RHW becomes RHW.
        const cur = lease.state >>> 0, want2 = granted >>> 0;
        if ((cur & ~want2) === 0) setLeaseState(lease, want2);
      }
      granted = lease.state;
    }
    lease.opens.add(open);
    open.lease = lease;
    open.leaseKeyHex = keyHex;
    open.oplockLevel = OPLOCK.LEASE;
    if (req.server.debugLevel > 1) {
      conn.log('smb2: lease ' + keyHex.slice(0, 8) + ' on ' + open.rel +
               ' want=' + leaseCtx.state + ' granted=' + granted +
               ' foreign=' + (foreignHolder ? 'Y' : 'n'));
    }
    return { level: OPLOCK.LEASE, lease, granted };
  }

  let level = OPLOCK.NONE;
  if (open.isDir) return { level: OPLOCK.NONE };
  // What other lease keys still cache decides what a classic oplock request can
  // get: handle caching elsewhere blocks it outright, read caching elsewhere
  // caps it at level II.  Measured against Samba with every combination of
  // (foreign lease state, requested oplock).
  let foreignLeaseState = 0;
  for (const [k, l] of node.leases) {
    if (k === open.leaseKeyHex) continue;
    if (Array.from(l.opens).some((o) => o !== open && !o.closed)) {
      foreignLeaseState |= l.state;
    }
  }
  if (requested !== OPLOCK.NONE && foreignLeaseState) {
    level = (foreignLeaseState & LEASE.HANDLE) ? OPLOCK.NONE : OPLOCK.II;
  } else if (requested === OPLOCK.BATCH || requested === OPLOCK.EXCLUSIVE) {
    level = others.length === 0 ? requested : OPLOCK.NONE;
  } else if (requested === OPLOCK.II) {
    level = (others.length === 0 || !node.oplock) ? OPLOCK.II : OPLOCK.NONE;
  }
  if (level === OPLOCK.BATCH || level === OPLOCK.EXCLUSIVE) node.oplock = { open, level };
  // Level II holders are tracked so a later write can break them all at once.
  if (level === OPLOCK.II) node.oplockII = (node.oplockII || new Set()).add(open);
  open.oplockLevel = level;
  return { level };
}

// ---------------------------------------------------------------------------
// Create contexts
// ---------------------------------------------------------------------------

function parseCreateContexts(buf) {
  const out = [];
  let off = 0;
  while (off + 16 <= buf.length) {
    const next = buf.readUInt32LE(off);
    const nameOff = buf.readUInt16LE(off + 4);
    const nameLen = buf.readUInt16LE(off + 6);
    const dataOff = buf.readUInt16LE(off + 10);
    const dataLen = buf.readUInt32LE(off + 12);
    const name = (nameLen && off + nameOff + nameLen <= buf.length)
      ? buf.subarray(off + nameOff, off + nameOff + nameLen).toString('latin1') : '';
    const data = (dataLen && off + dataOff + dataLen <= buf.length)
      ? buf.subarray(off + dataOff, off + dataOff + dataLen) : Buffer.alloc(0);
    out.push({ name, data });
    if (next === 0) break;
    off += next;
  }
  return out;
}

function buildCreateContexts(list) {
  if (!list.length) return Buffer.alloc(0);
  const parts = [];
  for (let i = 0; i < list.length; i++) {
    const name = Buffer.from(list[i].name, 'latin1');
    const data = list[i].data;
    const nameOff = 16;
    const dataOff = pad8(nameOff + name.length);
    const total = dataOff + data.length;
    const size = (i === list.length - 1) ? total : pad8(total);
    const b = Buffer.alloc(size);
    b.writeUInt32LE(i === list.length - 1 ? 0 : size, 0);
    b.writeUInt16LE(nameOff, 4);
    b.writeUInt16LE(name.length, 6);
    b.writeUInt16LE(0, 8);
    b.writeUInt16LE(data.length ? dataOff : 0, 10);
    b.writeUInt32LE(data.length, 12);
    name.copy(b, nameOff);
    data.copy(b, dataOff);
    parts.push(b);
  }
  return Buffer.concat(parts);
}

// ---------------------------------------------------------------------------
// Open bookkeeping and durable handles ([MS-SMB2] 3.3.5.9.6 - 3.3.5.9.12)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Named pipes on IPC$ ([MS-SMB2] 2.2.13, 3.3.5.9).  pipes.js owns the pipe
// semantics and dcerpc.js/witness.js the payload; this is only the SMB2 plumbing.
// ---------------------------------------------------------------------------

function pipeRegistry(server) {
  if (!server._pipeRegistry) {
    // [MS-SWN] 3.1.4.1: a witness service whose InterfaceList is empty answers
    // WitnessrGetInterfaceList with ERROR_NO_MORE_ITEMS, so the service has to
    // know the interfaces it is reachable on. They are the ones this server is
    // bound to, named after the NetBIOS name the configuration gives it.
    const conf = server.conf;
    const netbios = (conf && conf.get('global', 'netbios name', '')) || 'SMBSRV';
    const bound = [];
    for (const addr of server.listenAddresses ? server.listenAddresses() : []) {
      bound.push(addr);
    }
    if (!bound.length) bound.push('127.0.0.1');
    const interfaces = bound.map((addr) => ({
      interfaceGroupName: netbios,
      state: 1,                                     // AVAILABLE
      ipv4Address: addr.includes(':') ? '' : addr,
      ipv6Address: addr.includes(':') ? addr : '',
      local: true,
    }));
    server._pipeRegistry = new PIPES.PipeRegistry({
      witnessOptions: { interfaces, serverGlobalName: netbios },
    });
  }
  return server._pipeRegistry;
}

// Pipe handles get volatile ids from their own range so they can never collide
// with a share's.
function nextPipeFileId(server) {
  if (!server._pipeFileId) server._pipeFileId = 0x40000000n;
  server._pipeFileId += 1n;
  return server._pipeFileId;
}

function pipeCreateResponse(req, open) {
  const body = Buffer.alloc(89);
  body.writeUInt16LE(89, 0);
  body.writeUInt8(OPLOCK.NONE, 2);
  body.writeUInt32LE(ACTION.OPENED, 4);
  body.writeUInt32LE(ATTR.NORMAL, 56);
  body.writeBigUInt64LE(open.persistentId, 64);
  body.writeBigUInt64LE(open.fileId, 72);
  req.chain.fileId = { p: open.persistentId, v: open.fileId };
  req.chain.prevHadFileId = true;
  return { status: S.STATUS_SUCCESS, body };
}

function openPipe(req, name, desiredAccess, shareAccess) {
  const r = pipeRegistry(req.server).open(name, { session: req.session, tree: req.tree });
  if (r.status !== 0 || !r.pipe) {
    return { status: r.status === PIPES.STATUS_OBJECT_NAME_INVALID
               ? S.STATUS_OBJECT_NAME_INVALID : S.STATUS_OBJECT_NAME_NOT_FOUND, body: null };
  }
  const access = (V.GENERIC_MAP(desiredAccess) |
                  A.FILE_READ_DATA | A.FILE_WRITE_DATA | A.FILE_READ_ATTRIBUTES) >>> 0;
  const open = {
    isPipe: true, pipe: r.pipe, isDir: false, closed: false, fd: null,
    share: null, rel: name, node: null, dirEnum: null, lease: null, durable: null,
    access, shareAccess: (shareAccess >>> 0) || (SHARE.READ | SHARE.WRITE | SHARE.DELETE),
    fileId: nextPipeFileId(req.server), oplockLevel: OPLOCK.NONE, createOptions: 0,
    session: req.session, tree: req.tree, position: 0n,
    sticky: { write: null, access: null, change: null, create: null, attrs: null },
  };
  r.pipe.context = open;
  registerOpen(req, open);
  return pipeCreateResponse(req, open);
}

// Build the READ response body for `data`.
function readBody(data) {
  const body = Buffer.alloc(16 + Math.max(1, data.length));
  body.writeUInt16LE(17, 0);
  body.writeUInt8(HDR + 16, 2);
  body.writeUInt8(0, 3);
  body.writeUInt32LE(data.length, 4);
  body.writeUInt32LE(0, 8);
  body.writeUInt32LE(0, 12);
  data.copy(body, 16);
  return body;
}

function pipeStatus(v) { return (v >>> 0) === 0 ? S.STATUS_SUCCESS : (v >>> 0); }

function nextPersistent(server) {
  if (!server._smb2Persistent) server._smb2Persistent = 0x100n;
  return server._smb2Persistent++;
}

function registerOpen(req, open) {
  const conn = req.conn, st = cstate(conn);
  open.persistentId = nextPersistent(req.server);
  open.conn = conn;
  open.tree = req.tree;
  open.session = req.session;
  const k = open.fileId.toString();
  req.tree.opens.set(k, open);
  req.session.opens.set(k, open);
  st.opens.set(k, open);
  return open;
}

function lookupOpen(req, persistent, volatileId) {
  const st = cstate(req.conn);
  const open = st.opens.get(volatileId.toString());
  if (!open || open.closed) return null;
  if (open.persistentId !== persistent) return null;
  if (req.tree && open.tree && open.tree !== req.tree) return null;
  return open;
}

function removeOpen(conn, open) {
  if (!open || open.closed) return;
  const st = cstate(conn);
  const k = open.fileId.toString();
  if (open.isPipe) {
    open.closed = true;
    if (open.tree) open.tree.opens.delete(k);
    if (open.session) open.session.opens.delete(k);
    st.opens.delete(k);
    try { open.pipe.close(); } catch (e) { /* best effort */ }
    return;
  }
  if (open.tree) open.tree.opens.delete(k);
  if (open.session) open.session.opens.delete(k);
  st.opens.delete(k);
  if (open.durable) {
    const dk = open.persistentId.toString();
    const ent = conn.server.durables.get(dk);
    if (ent && ent.open === open) { if (ent.timer) clearTimeout(ent.timer); conn.server.durables.delete(dk); }
  }
  const node = open.node, share = open.share;
  const last = node.opens.size === 1;
  // fileops marks the file delete-pending when the handle closes, so consult
  // the handle's own flags as well.
  const willDelete = node.deletePending || open.deleteOnClose || open.deleteOnCloseOption;
  const wasDeleting = !!willDelete && last;
  const rel = open.rel, isDir = open.isDir;
  // A v1 lease lives exactly as long as the handles that hold it; leaving a
  // stale entry behind would make the next open under that key inherit a state
  // it never asked for.
  if (open.lease) {
    open.lease.opens.delete(open);
    if (!Array.from(open.lease.opens).some((o) => !o.closed) && open.leaseKeyHex) {
      if (open.lease.waiters) {
        const w = open.lease.waiters.slice();
        open.lease.waiters = [];
        for (const f of w) { try { f(); } catch (e) {} }
      }
      // Only drop the entry if it is still this lease: a lease that was broken
      // to none is retired and the key may already name a newer one.
      if (node.leases.get(open.leaseKeyHex) === open.lease) {
        node.leases.delete(open.leaseKeyHex);
      }
    }
    open.lease = null;
  }
  dropDirWatch(open);
  OPS.closeOpen(open);
  if (last) resetWriteTimeState(share, rel);
  retryLockWaiters(node);
  if (wasDeleting && !share.stat(rel)) {
    notifyChange(share, rel, ACT.REMOVED, isDir ? NOTIFY.DIR_NAME : NOTIFY.FILE_NAME);
  }
}

// Move durable opens aside when the connection dies; the scavenger drops them
// after DURABLE_TIMEOUT_MS.
function onDisconnect(conn) {
  const st = conn.smb2state;
  if (!st) return;
  for (const e of st.async.values()) { e.done = true; if (e.cancel) { try { e.cancel(); } catch (x) {} } }
  st.async.clear(); st.byMid.clear();
  const server = conn.server;
  for (const open of Array.from(st.opens.values())) {
    if (!open.durable || open.closed) { continue; }
    detachOpen(conn, open);
    open.disconnected = true;
    const key = open.persistentId.toString();
    const ent = server.durables.get(key);
    if (!ent) continue;
    ent.timer = setTimeout(() => {
      const cur = server.durables.get(key);
      if (cur && cur.open === open) {
        server.durables.delete(key);
        try { OPS.closeOpen(open); } catch (e) {}
      }
    }, DURABLE_TIMEOUT_MS);
    if (ent.timer.unref) ent.timer.unref();
  }
}

// Take an open off the connection/session/tree tables without closing it.
function detachOpen(conn, open) {
  const st = cstate(conn);
  const k = open.fileId.toString();
  if (open.tree) open.tree.opens.delete(k);
  if (open.session) open.session.opens.delete(k);
  st.opens.delete(k);
  dropDirWatch(open);
}

// Remember a granted durable handle immediately: a client may reconnect before
// the server has even noticed that the old connection went away.
function registerDurable(req, open) {
  const conn = req.conn;
  req.server.durables.set(open.persistentId.toString(), {
    open, timer: null,
    clientGuid: conn.clientGuid ? guidStr(conn.clientGuid) : '',
    createGuid: open.createGuid ? guidStr(open.createGuid) : '',
    shareName: open.share.name.toLowerCase(), rel: open.rel,
  });
}

function reclaimDurable(req, fileIdPersistent, createGuid, name) {
  const server = req.server, conn = req.conn;
  const ent = server.durables.get(fileIdPersistent.toString());
  if (!ent || ent.open.closed) return null;
  if (ent.clientGuid && conn.clientGuid && ent.clientGuid !== guidStr(conn.clientGuid)) return null;
  if (createGuid && ent.createGuid && guidStr(createGuid) !== ent.createGuid) return null;
  if (req.tree.share && ent.shareName !== req.tree.share.name.toLowerCase()) return null;
  if (ent.timer) { clearTimeout(ent.timer); ent.timer = null; }
  const open = ent.open;
  // A reconnect is only valid once the connection that owned the handle has
  // gone; reconnecting on the live connection must fail ([MS-SMB2] 3.3.5.9.7).
  if (open.conn === conn && !open.disconnected) return null;
  if (open.conn && open.conn !== conn) { try { detachOpen(open.conn, open); } catch (e) {} }
  open.disconnected = false;
  open.session = req.session;
  open.tree = req.tree;
  open.conn = conn;
  const st = cstate(conn);
  const k = open.fileId.toString();
  req.tree.opens.set(k, open);
  req.session.opens.set(k, open);
  st.opens.set(k, open);
  ent.clientGuid = conn.clientGuid ? guidStr(conn.clientGuid) : '';
  server.durables.set(fileIdPersistent.toString(), ent);
  return open;
}

// ---------------------------------------------------------------------------
// CREATE ([MS-SMB2] 2.2.13 / 2.2.14, 3.3.5.9)
// ---------------------------------------------------------------------------

// [MS-FSCC] FILE_GENERIC_* minus the standard rights, which are added below.
const GENERIC_READ_BITS = (A.FILE_READ_DATA | A.FILE_READ_EA | A.FILE_READ_ATTRIBUTES) >>> 0;
const GENERIC_WRITE_BITS = (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.FILE_WRITE_EA |
  A.FILE_WRITE_ATTRIBUTES) >>> 0;

// Samba answers MxAc from the file's own POSIX mode (posix_acls.c maps r/w/x to
// FILE_GENERIC_READ / FILE_GENERIC_WRITE / FILE_EXECUTE) and adds DELETE plus
// WRITE_DAC for the owner; READ_CONTROL and SYNCHRONIZE are always present.
// The share tree belongs to the authenticated account, so the owner triad is
// the caller's: a 0666 file answers 0x0017019F, a 0777 directory 0x001701BF.
function posixMaximalAccess(st) {
  if (!st || typeof st.mode !== 'number') return 0;
  const mode = st.mode & 0o777;
  let m = A.READ_CONTROL | A.WRITE_DAC | A.SYNCHRONIZE;
  if (mode & 0o400) m |= GENERIC_READ_BITS;
  if (mode & 0o200) m |= GENERIC_WRITE_BITS | A.DELETE;
  if (mode & 0o100) m |= A.FILE_EXECUTE;
  return m >>> 0;
}

function maximalAccessFor(req, open) {
  let m = req.tree.maximalAccess >>> 0;
  if (open) {
    // A stored descriptor governs on its own; the POSIX mapping below only
    // applies while the object still has no explicit ACL.
    const acl = checkObjectAccess(req, open.share, open.rel, A.MAXIMUM_ALLOWED, open.isDir);
    if (acl) return (acl.status === SD.STATUS_SUCCESS ? acl.granted : 0) >>> 0;
    const st = open.share.stat(open.rel);
    const posix = posixMaximalAccess(st);
    if (posix) m &= posix;
    const attrs = open.share.attributes(open.rel, st);
    if (attrs & ATTR.READONLY) m &= ~(A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.DELETE);
  }
  return m >>> 0;
}

async function doCreate(req) {
  const b = req.body, conn = req.conn, st = cstate(conn), share = req.tree.share;
  if (b.length < 56 || b.readUInt16LE(0) !== 57) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const requestedOplock = b.readUInt8(3);
  const impersonation = b.readUInt32LE(4);
  const desiredAccess = b.readUInt32LE(24);
  const fileAttributes = b.readUInt32LE(28);
  const shareAccess = b.readUInt32LE(32);
  const disposition = b.readUInt32LE(36);
  const createOptions = b.readUInt32LE(40);
  const nameOff = b.readUInt16LE(44), nameLen = b.readUInt16LE(46);
  const ctxOff = b.readUInt32LE(48), ctxLen = b.readUInt32LE(52);

  if (impersonation > 4) return { status: STATUS_BAD_IMPERSONATION_LEVEL, body: null };
  let name = '';
  if (nameLen) {
    const s = nameOff - HDR;
    if (s < 0 || s + nameLen > b.length) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    name = unistr(b.subarray(s, s + nameLen));
  }
  if (name.includes('\0')) return { status: S.STATUS_OBJECT_NAME_INVALID, body: null };
  // IPC$ has no file system behind it: a CREATE there names a pipe.
  if (req.tree.isIpc) return openPipe(req, name, desiredAccess, shareAccess);
  if (name.startsWith('\\')) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const syn = pathSyntaxStatus(name);
  if (syn) return { status: syn, body: null };
  if (req.server.debugLevel > 1) {
    conn.log('smb2: create name=' + JSON.stringify(name) + ' disp=' + disposition +
             ' access=0x' + desiredAccess.toString(16) + ' share=' + shareAccess +
             ' opts=0x' + createOptions.toString(16) + ' oplock=' + requestedOplock);
  }
  let ctxs = [];
  if (ctxLen) {
    const s = ctxOff - HDR;
    if (s < 0 || s + ctxLen > b.length) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    ctxs = parseCreateContexts(b.subarray(s, s + ctxLen));
  }
  const byName = new Map();
  for (const c of ctxs) if (!byName.has(c.name)) byName.set(c.name, c.data);

  if ((createOptions & (OPT.DIRECTORY_FILE | OPT.NON_DIRECTORY_FILE)) ===
      (OPT.DIRECTORY_FILE | OPT.NON_DIRECTORY_FILE)) {
    return { status: S.STATUS_INVALID_PARAMETER, body: null };
  }
  if (createOptions & 0xff000000) return { status: S.STATUS_INVALID_PARAMETER, body: null };

  // --- durable reconnect goes first: it bypasses the normal open path
  const dhnc = byName.get(CTX.DURABLE_RECONNECT);
  const dh2c = byName.get(CTX.DURABLE_RECONNECT_V2);
  if (dhnc || dh2c) {
    const d = dhnc || dh2c;
    // [MS-SMB2] 2.2.13.2.4 / 2.2.13.2.12: DHnC is 16 bytes, DH2C is 36; any
    // other size is rejected before the handle is even looked for.
    if (dh2c ? d.length !== 36 : d.length !== 16) {
      return { status: S.STATUS_INVALID_PARAMETER, body: null };
    }
    const persistent = d.readBigUInt64LE(0);
    const createGuid = dh2c && d.length >= 32 ? readGuid(d, 16) : null;
    const open = reclaimDurable(req, persistent, createGuid, name);
    if (!open) return { status: S.STATUS_OBJECT_NAME_NOT_FOUND, body: null };
    const resp = [];
    if (dh2c) { const r = Buffer.alloc(8); r.writeUInt32LE(0, 0); r.writeUInt32LE(0, 4); resp.push({ name: CTX.DURABLE_RECONNECT_V2, data: r }); }
    return createResponse(req, open, ACTION.OPENED, resp, open.oplockLevel || OPLOCK.NONE, open.lease);
  }

  // --- lease / oplock request
  let leaseCtx = null;
  const rq = byName.get(CTX.LEASE);
  if (rq && rq.length >= 32 && requestedOplock === OPLOCK.LEASE && st.dialectInfo.leasing) {
    leaseCtx = { key: readGuid(rq, 0), state: rq.readUInt32LE(16), v2: rq.length >= 52 };
  }
  if (requestedOplock === OPLOCK.LEASE && !leaseCtx) {
    // lease requested but no (usable) context: fall back to no oplock
  }

  // --- break conflicting oplocks/leases before the open is attempted
  let rel = null;
  const canCreate = disposition !== DISP.OPEN && disposition !== DISP.OVERWRITE;
  try { rel = resolveName(share, name, canCreate); } catch (e) { if (e instanceof SmbError) return { status: e.status, body: null }; throw e; }
  // The disposition check outranks the file/directory type check
  // ([MS-FSA] 2.1.5.1: "open if exists" is evaluated before the type test).
  if (disposition === DISP.CREATE && share.stat(rel)) {
    return { status: S.STATUS_OBJECT_NAME_COLLISION, body: null };
  }
  const existing = nodeOf(share, rel);
  const mappedAccess = V.GENERIC_MAP(desiredAccess);
  // [MS-FSA] 2.1.5.1.2: an open that asks for none of the data/delete rights
  // is a "stat open" and takes no part in share-mode checking (bug 14375).
  const dataBits0 = (A.FILE_READ_DATA | A.FILE_WRITE_DATA | A.FILE_APPEND_DATA |
                     A.FILE_EXECUTE | A.DELETE | A.MAXIMUM_ALLOWED) >>> 0;
  const isStatOpen = ((mappedAccess | (desiredAccess & A.MAXIMUM_ALLOWED)) & dataBits0) === 0;
  const effShareAccess = isStatOpen ? (SHARE.READ | SHARE.WRITE | SHARE.DELETE) : shareAccess;

  const brk = breakForNewOpen(existing, {
    access: mappedAccess, disposition, createOptions,
    oplockStatOpen: isOplockStatOpen(mappedAccess),
    leaseStatOpen: isLeaseStatOpen(mappedAccess),
    // An open that is going to be refused for share-mode reasons only disturbs
    // a holder that could resolve it by closing, i.e. one caching handles.
    sharingViolation: !!(existing && !isStatOpen &&
                         V.shareConflict(existing, mappedAccess, effShareAccess, null)),
    deletePending: !!(existing && existing.deletePending),
    leaseKeyHex: leaseCtx ? guidStr(leaseCtx.key) : null,
  });
  if (brk) {
    // The open has to wait for a break to be acknowledged: announce it as
    // pending so the client can cancel it ([MS-SMB2] 3.3.4.2).
    const onCancel = () => { req.cancelled = true; };
    onCancel.now = true;              // the interim goes out before we block
    startAsync(req, onCancel);
    await brk;
    if (req.cancelled) return { status: S.STATUS_CANCELLED, body: null };
  }
  if (conn.closed) return { status: S.STATUS_CONNECTION_DISCONNECTED, body: null };

  // [MS-DTYP] 2.5.3: an object that carries a stored descriptor has its DACL
  // consulted before the open is attempted.
  const wantsMaximum = (desiredAccess & A.MAXIMUM_ALLOWED) !== 0;
  let grantedByAcl = null;
  const aclCheck = checkObjectAccess(req, share, rel,
    wantsMaximum ? A.MAXIMUM_ALLOWED : (mappedAccess & ~A.MAXIMUM_ALLOWED),
    (createOptions & OPT.DIRECTORY_FILE) !== 0);
  if (aclCheck) {
    if (aclCheck.status !== SD.STATUS_SUCCESS) return { status: S.STATUS_ACCESS_DENIED, body: null };
    if (wantsMaximum) grantedByAcl = aclCheck.granted;
  }

  let made;
  try {
    made = OPS.createFile(share, {
      name: rel, desiredAccess, fileAttributes, shareAccess: effShareAccess,
      createDisposition: disposition, createOptions,
      session: req.session, tree: req.tree, pid: 0,
    });
  } catch (e) {
    if (e instanceof SmbError) return { status: e.status, body: null };
    return { status: OPS.mapErrno(e), body: null };
  }
  const open = made.open;
  if (grantedByAcl !== null) open.access = grantedByAcl >>> 0;
  open.oplockStatOpen = isOplockStatOpen(open.access);
  open.leaseStatOpen = isLeaseStatOpen(open.access);
  registerOpen(req, open);
  if ((createOptions & OPT.DIRECTORY_FILE) && !open.isDir) {
    removeOpen(conn, open);
    return { status: S.STATUS_NOT_A_DIRECTORY, body: null };
  }

  // --- extended attributes supplied at create time
  const secd = byName.get(CTX.SD_BUFFER);
  if (secd && secd.length >= 20 && made.action !== ACTION.OPENED) {
    // SMB2_CREATE_SD_BUFFER ([MS-SMB2] 2.2.13.2.2): the descriptor the client
    // wants the new object to carry.
    try {
      const incoming = SD.parseSecurityDescriptor(secd);
      const si = (incoming.owner ? SD.SI.OWNER_SECURITY_INFORMATION : 0) |
                 (incoming.group ? SD.SI.GROUP_SECURITY_INFORMATION : 0) |
                 (incoming.dacl ? SD.SI.DACL_SECURITY_INFORMATION : 0);
      if (si) storeDescriptor(req, share, open.rel, secd, si);
    } catch (e) { /* a malformed SecD is ignored, as Samba does */ }
  }
  const ea = byName.get(CTX.EA_BUFFER);
  if (ea && ea.length >= 8) { try { applyEaBuffer(share, open.rel, ea); } catch (e) {} }
  const alsi = byName.get(CTX.ALLOCATION_SIZE);
  if (alsi && alsi.length >= 8 && !open.isDir) {
    const want = alsi.readBigUInt64LE(0);
    // Samba pre-allocates the blocks and keeps the file size at zero, so the
    // allocation shows up in the info classes without EndOfFile moving.  The
    // information layer derives allocation from EndOfFile alone, so the
    // requested size is remembered here and folded in on the way out.
    if (made.action !== ACTION.OPENED && want > 0n && want <= MAX_FILE_SIZE) {
      share.metaFor(open.rel).allocSize = want;
    }
  }

  const granted = grantOplock(req, open, requestedOplock, leaseCtx);

  // --- durable request
  const resp = [];
  const dhnq = byName.get(CTX.DURABLE_REQ);
  const dh2q = byName.get(CTX.DURABLE_REQ_V2);
  const durableOk = share.durable !== false && req.server.durableEnabled &&
      (granted.level === OPLOCK.BATCH ||
       (granted.level === OPLOCK.LEASE && (granted.granted & LEASE.HANDLE)));
  // Samba processes a v2 durable request whatever the dialect; the version gate
  // lives in the client, not the server (measured: granted at 2.0.2 and 2.1).
  if (dh2q) {
    if (dh2q.length >= 32) open.createGuid = readGuid(dh2q, 16);
    if (durableOk) {
      open.durable = true;
      registerDurable(req, open);
      const r = Buffer.alloc(8);
      r.writeUInt32LE(DURABLE_TIMEOUT_MS, 0);
      r.writeUInt32LE(0, 4);
      resp.push({ name: CTX.DURABLE_REQ_V2, data: r });
    }
  } else if (dhnq && durableOk) {
    open.durable = true;
    registerDurable(req, open);
    resp.push({ name: CTX.DURABLE_REQ, data: Buffer.alloc(8) });
  }

  if (byName.has(CTX.MAX_ACCESS)) {
    const r = Buffer.alloc(8);
    r.writeUInt32LE(0, 0);
    r.writeUInt32LE(maximalAccessFor(req, open), 4);
    resp.push({ name: CTX.MAX_ACCESS, data: r });
  }
  if (byName.has(CTX.QUERY_ON_DISK_ID)) {
    const stt = share.stat(open.rel);
    const r = Buffer.alloc(32);
    if (stt) { r.writeBigUInt64LE(INFO.fileIdOf(stt), 0); r.writeBigUInt64LE(BigInt(stt.dev) & 0xffffffffffffffffn, 8); }
    resp.push({ name: CTX.QUERY_ON_DISK_ID, data: r });
  }
  if (granted.level === OPLOCK.LEASE && granted.lease) {
    const r = Buffer.alloc(leaseCtx && leaseCtx.v2 ? 52 : 32);
    Buffer.from(granted.lease.key).copy(r, 0);
    r.writeUInt32LE(granted.granted >>> 0, 16);
    // SMB2_LEASE_FLAG_BREAK_IN_PROGRESS ([MS-SMB2] 2.2.13.2.8)
    if (granted.lease.breaking) r.writeUInt32LE(LEASE_BREAK_IN_PROGRESS, 20);
    // Epoch is the last field of the v2 context ([MS-SMB2] 2.2.13.2.10: key 16,
    // state 4, flags 4, duration 8, parent key 16, epoch 2, reserved 2).
    if (leaseCtx && leaseCtx.v2) r.writeUInt16LE((granted.lease.epoch || 0) & 0xffff, 48);
    resp.push({ name: CTX.LEASE, data: r });
  }

  if (made.action === ACTION.CREATED) {
    // Make CreationTime exactly equal the initial write time, as a real file
    // system does; the timestamp tests compare them for equality.
    const cst = share.stat(open.rel);
    if (cst) share.metaFor(open.rel).createTime = cst.mtimeMs;
    notifyChange(share, open.rel, ACT.ADDED, open.isDir ? NOTIFY.DIR_NAME : NOTIFY.FILE_NAME);
    if (!open.isDir) notifyChange(share, open.rel, ACT.MODIFIED, NOTIFY.ATTRIBUTES);
  }
  req.chain.fileId = { p: open.persistentId, v: open.fileId };
  req.chain.prevHadFileId = true;
  return createResponse(req, open, made.action, resp, granted.level, granted.lease, granted.granted);
}

function createResponse(req, open, action, ctxList, oplockLevel, lease, leaseState) {
  const share = open.share;
  const stt = viewStat(share, open.rel, share.stat(open.rel));
  const t = share.times(open.rel, stt) || { create: 0n, access: 0n, write: 0n, change: 0n };
  const attrs = share.attributes(open.rel, stt);
  const eof = stt && !stt.isDirectory() ? BigInt(stt.size) : 0n;
  const ctxBuf = buildCreateContexts(ctxList || []);
  const body = Buffer.alloc(88 + Math.max(1, ctxBuf.length));
  body.writeUInt16LE(89, 0);
  body.writeUInt8(oplockLevel === undefined ? 0 : oplockLevel, 2);
  body.writeUInt8(0, 3);
  body.writeUInt32LE(action, 4);
  body.writeBigUInt64LE(t.create, 8);
  body.writeBigUInt64LE(t.access, 16);
  body.writeBigUInt64LE(t.write, 24);
  body.writeBigUInt64LE(t.change, 32);
  body.writeBigUInt64LE(eof === 0n ? 0n : INFO.alloc(0, eof), 40);
  body.writeBigUInt64LE(eof, 48);
  body.writeUInt32LE(attrs, 56);
  body.writeUInt32LE(0, 60);
  body.writeBigUInt64LE(open.persistentId, 64);
  body.writeBigUInt64LE(open.fileId, 72);
  body.writeUInt32LE(ctxBuf.length ? HDR + 88 : 0, 80);
  body.writeUInt32LE(ctxBuf.length, 84);
  if (ctxBuf.length) ctxBuf.copy(body, 88);
  req.chain.fileId = { p: open.persistentId, v: open.fileId };
  req.chain.prevHadFileId = true;
  return { status: S.STATUS_SUCCESS, body };
}

function applyEaBuffer(share, rel, buf) {
  const meta = share.metaFor(rel);
  let off = 0;
  while (off + 8 <= buf.length) {
    const next = buf.readUInt32LE(off);
    const nameLen = buf.readUInt8(off + 5);
    const valLen = buf.readUInt16LE(off + 6);
    const nm = buf.subarray(off + 8, off + 8 + nameLen).toString('latin1');
    const val = buf.subarray(off + 9 + nameLen, off + 9 + nameLen + valLen);
    if (nm) meta.ea.set(nm.toUpperCase(), Buffer.from(val));
    if (!next) break;
    off += next;
  }
}

// ---------------------------------------------------------------------------
// Deferred write-time updates ([MS-FSA] 2.1.4.17, Samba's "write time update
// delay").  A write does not move the reported write time immediately: the
// update is deferred by two seconds, and a FLUSH, a SET_INFO or a CLOSE
// flushes it early.  Once flushed, further writes on the same file do not
// re-arm the timer, but a CLOSE after such a write stamps the current time.
//
// The state lives on the per-file metadata (not on the Node) so that it
// survives the last handle being closed and is visible to path-based queries.
// ---------------------------------------------------------------------------

const WRITE_TIME_DELAY_MS = 2000;
const INT64_MAX = 0x7fffffffffffffffn;
const MAX_FILE_SIZE = 0xfffffff0000n;    // Samba's maximum supported file size

// Offsets are signed 64-bit on the wire; negative values and sums that
// overflow are STATUS_INVALID_PARAMETER (see smb2.rw.invalid).
function offsetCheck(offset, length) {
  if (offset > INT64_MAX) return S.STATUS_INVALID_PARAMETER;
  if (offset + length > INT64_MAX) return S.STATUS_INVALID_PARAMETER;
  return 0;
}

const NT_OMIT = 0n;
const NT_FREEZE = 0xffffffffffffffffn;   // -1: suppress implicit updates
const NT_THAW = 0xfffffffffffffffen;     // -2: resume implicit updates

// fromFileTime() truncates to whole milliseconds, which does not survive the
// round trip back through toFileTime(); keep the fractional part.
const ftToMs = (ft) => Number(BigInt(ft) - V.EPOCH_DELTA) / 10000;

// The write time the client should see, or null when the on-disk mtime is it.
function overriddenWrite(m) {
  if (!m) return null;
  if (m.tsWrite !== undefined) return m.tsWrite;
  if (m.wtPending && m.wtPreValue !== undefined) return m.wtPreValue;
  if (m.wtStored !== undefined) return m.wtStored;
  return null;
}

function reportedWrite(share, rel) {
  const m = share.meta.get(rel.toLowerCase());
  const o = overriddenWrite(m);
  if (o !== null) return o;
  const st = share.stat(rel);
  return st ? toFileTime(st.mtimeMs) : nowFt();
}

function markWriteTime(open) {
  const share = open.share, rel = open.rel;
  const m = share.metaFor(rel);
  if (m.tsWrite !== undefined) return;          // client pinned the write time
  m.wtDirty = true;
  if (m.wtTriggered) return;
  m.wtTriggered = true;
  m.wtPending = true;
  m.wtPreValue = reportedWrite(share, rel);
  m.wtTimer = setTimeout(() => { try { flushWriteTime(share, rel); } catch (e) {} },
                         WRITE_TIME_DELAY_MS);
  if (m.wtTimer.unref) m.wtTimer.unref();
}

function flushWriteTime(share, rel) {
  const m = share.meta.get(rel.toLowerCase());
  if (!m) return;
  if (m.wtTimer) { clearTimeout(m.wtTimer); m.wtTimer = null; }
  m.wtPending = false;
  m.wtPreValue = undefined;
  if (!m.wtDirty) return;
  m.wtDirty = false;
  if (m.tsWrite !== undefined) return;
  m.wtStored = nowFt();
  const st = share.stat(rel);
  if (st) {
    try { fs.utimesSync(share.real(rel), st.atimeMs / 1000, fromFileTime(m.wtStored) / 1000); }
    catch (e) { /* host fs cannot represent it; the cached value still answers */ }
  }
}

function resetWriteTimeState(share, rel) {
  const m = share.meta.get(rel.toLowerCase());
  if (!m) return;
  if (m.wtTimer) { clearTimeout(m.wtTimer); m.wtTimer = null; }
  m.wtTriggered = false;
  m.wtPending = false;
  m.wtPreValue = undefined;
}

// Return a stat whose timestamps reflect the delayed-update rule and any time
// the client pinned with FileBasicInformation.
function viewStat(share, rel, st) {
  if (!st) return st;
  const m = share.meta.get(String(rel).toLowerCase());
  if (!m) return st;
  const props = {};
  const w = overriddenWrite(m);
  if (w !== null) props.mtimeMs = { value: ftToMs(w), enumerable: true, writable: true };
  if (m.tsAccess !== undefined) {
    props.atimeMs = { value: ftToMs(m.tsAccess), enumerable: true, writable: true };
  }
  if (m.tsChange !== undefined) {
    props.ctimeMs = { value: ftToMs(m.tsChange), enumerable: true, writable: true };
  }
  if (Object.keys(props).length === 0) return st;
  return Object.create(st, props);
}

// ---------------------------------------------------------------------------
// CLOSE / FLUSH ([MS-SMB2] 2.2.15 - 2.2.18)
// ---------------------------------------------------------------------------

function readFileId(req, buf, off) {
  const p = buf.readBigUInt64LE(off);
  const v = buf.readBigUInt64LE(off + 8);
  if (p === 0xffffffffffffffffn && v === 0xffffffffffffffffn) {
    if (req.related && req.chain.fileId) return req.chain.fileId;
    // The predecessor that should have produced this FileId failed: repeat its
    // status ([MS-SMB2] 3.3.5.2.7.2).
    if (req.related && req.chain.fileIdErr) fail(req.chain.fileIdErr);
    return null;
  }
  return { p, v };
}

function openFromRequest(req, buf, off) {
  const id = readFileId(req, buf, off);
  if (!id) fail(S.STATUS_FILE_CLOSED);
  const open = lookupOpen(req, id.p, id.v);
  if (!open) fail(S.STATUS_FILE_CLOSED);
  req.chain.fileId = id;
  req.chain.prevHadFileId = true;
  return open;
}

async function doClose(req) {
  const b = req.body;
  if (b.length < 24 || b.readUInt16LE(0) !== 24) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const flags = b.readUInt16LE(2);
  const open = openFromRequest(req, b, 8);
  if (open.isPipe) {
    removeOpen(req.conn, open);
    req.chain.fileId = null;
    const pb = Buffer.alloc(60);
    pb.writeUInt16LE(60, 0);
    return { status: S.STATUS_SUCCESS, body: pb };
  }
  const share = open.share;
  OPS.applySticky(open);
  flushWriteTime(share, open.rel);
  const st = viewStat(share, open.rel, share.stat(open.rel));
  const body = Buffer.alloc(60);
  body.writeUInt16LE(60, 0);
  if ((flags & 0x0001) && st) {
    const t = share.times(open.rel, st);
    body.writeUInt16LE(0x0001, 2);
    body.writeBigUInt64LE(t.create, 8);
    body.writeBigUInt64LE(t.access, 16);
    body.writeBigUInt64LE(t.write, 24);
    body.writeBigUInt64LE(t.change, 32);
    const eof = st.isDirectory() ? 0n : BigInt(st.size);
    body.writeBigUInt64LE(eof === 0n ? 0n : INFO.alloc(0, eof), 40);
    body.writeBigUInt64LE(eof, 48);
    body.writeUInt32LE(share.attributes(open.rel, st), 56);
  }
  removeOpen(req.conn, open);
  req.chain.fileId = null;
  return { status: S.STATUS_SUCCESS, body };
}

async function doFlush(req) {
  const b = req.body;
  if (b.length < 24 || b.readUInt16LE(0) !== 24) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const open = openFromRequest(req, b, 8);
  if (open.isPipe) return { status: S.STATUS_SUCCESS, body: simple4(4) };
  if (open.fd !== null) { try { fs.fsyncSync(open.fd); } catch (e) {} }
  flushWriteTime(open.share, open.rel);
  return { status: S.STATUS_SUCCESS, body: simple4(4) };
}

// ---------------------------------------------------------------------------
// READ / WRITE ([MS-SMB2] 2.2.19 - 2.2.22, 3.3.5.12 / 3.3.5.13)
// ---------------------------------------------------------------------------

async function doRead(req) {
  const b = req.body, st = cstate(req.conn);
  if (b.length < 48 || b.readUInt16LE(0) !== 49) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const length = b.readUInt32LE(4);
  const offset = b.readBigUInt64LE(8);
  const open = openFromRequest(req, b, 16);
  const minimum = b.readUInt32LE(32);
  if (open.isPipe) return pipeRead(req, open, length);
  if (length > st.maxRead) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (open.isDir) return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
  if (!(open.access & (A.FILE_READ_DATA | A.FILE_EXECUTE))) return { status: S.STATUS_ACCESS_DENIED, body: null };
  if (rwConflict(open, offset, BigInt(length), false)) {
    return { status: S.STATUS_FILE_LOCK_CONFLICT, body: null };
  }
  const bad = offsetCheck(offset, BigInt(length));
  if (bad) return { status: bad, body: null };
  if (length > 0 && offset > BigInt(Number.MAX_SAFE_INTEGER)) return { status: S.STATUS_END_OF_FILE, body: null };
  const buf = Buffer.alloc(length);
  let n = 0;
  try { n = length ? fs.readSync(open.fd, buf, 0, length, Number(offset)) : 0; }
  catch (e) {
    if (e && (e.code === 'EINVAL' || e.code === 'EOVERFLOW')) return { status: S.STATUS_END_OF_FILE, body: null };
    return { status: OPS.mapErrno(e), body: null };
  }
  if (n === 0 && length > 0) return { status: S.STATUS_END_OF_FILE, body: null };
  if (n < minimum) return { status: S.STATUS_END_OF_FILE, body: null };
  open.position = offset + BigInt(n);
  const body = Buffer.alloc(16 + Math.max(1, n));
  body.writeUInt16LE(17, 0);
  body.writeUInt8(HDR + 16, 2);
  body.writeUInt8(0, 3);
  body.writeUInt32LE(n, 4);
  body.writeUInt32LE(0, 8);
  body.writeUInt32LE(0, 12);
  buf.copy(body, 16, 0, n);
  return { status: S.STATUS_SUCCESS, body };
}

async function doWrite(req) {
  const b = req.body, st = cstate(req.conn);
  if (b.length < 48 || b.readUInt16LE(0) !== 49) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const dataOff = b.readUInt16LE(2);
  const length = b.readUInt32LE(4);
  const offset = b.readBigUInt64LE(8);
  const open = openFromRequest(req, b, 16);
  const flags = b.readUInt32LE(44);
  if (length > st.maxWrite) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const s = dataOff - HDR;
  if (s < 0 || s + length > b.length) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (open.isPipe) {
    const w = open.pipe.write(b.subarray(s, s + length));
    if (pipeStatus(w.status) !== S.STATUS_SUCCESS) return { status: pipeStatus(w.status), body: null };
    const wb = Buffer.alloc(17);
    wb.writeUInt16LE(17, 0);
    wb.writeUInt32LE(w.written, 4);
    return { status: S.STATUS_SUCCESS, body: wb };
  }
  if (open.isDir) return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
  if (!(open.access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA))) {
    return { status: S.STATUS_ACCESS_DENIED, body: null };
  }
  if (open.share.readOnly) return { status: S.STATUS_ACCESS_DENIED, body: null };
  if (rwConflict(open, offset, BigInt(length), true)) {
    return { status: S.STATUS_FILE_LOCK_CONFLICT, body: null };
  }
  // The break travels after the write response, the way Samba orders it: the
  // client arms its expectation once the write has been answered.
  if (length > 0) setImmediate(() => { try { breakForWrite(open); } catch (e) {} });
  const badw = offsetCheck(offset, BigInt(length));
  if (badw) return { status: badw, body: null };
  if (length > 0 && offset + BigInt(length) > MAX_FILE_SIZE) {
    return { status: S.STATUS_INVALID_PARAMETER, body: null };
  }
  if (length === 0) {
    const zb = Buffer.alloc(17);
    zb.writeUInt16LE(17, 0);
    return { status: S.STATUS_SUCCESS, body: zb };
  }
  markWriteTime(open);
  let n = 0;
  try { n = length ? fs.writeSync(open.fd, b, s, length, Number(offset)) : 0; }
  catch (e) {
    if (e && (e.code === 'EINVAL' || e.code === 'EFBIG' || e.code === 'EOVERFLOW')) {
      return { status: S.STATUS_DISK_FULL, body: null };
    }
    return { status: OPS.mapErrno(e), body: null };
  }
  open.position = offset + BigInt(n);
  if (flags & 0x00000001) { try { fs.fsyncSync(open.fd); } catch (e) {} }  // WRITE_THROUGH
  notifyChange(open.share, open.rel, ACT.MODIFIED, NOTIFY.SIZE | NOTIFY.LAST_WRITE);
  const body = Buffer.alloc(17);
  body.writeUInt16LE(17, 0);
  body.writeUInt16LE(0, 2);
  body.writeUInt32LE(n, 4);
  body.writeUInt32LE(0, 8);
  body.writeUInt16LE(0, 12);
  body.writeUInt16LE(0, 14);
  return { status: S.STATUS_SUCCESS, body };
}

// ---------------------------------------------------------------------------
// LOCK ([MS-SMB2] 2.2.26 / 2.2.27, 3.3.5.14)
//
// The byte-range engine lives here rather than in fileops because the SMB2
// rules differ from the SMB1 ones fileops implements: zero-length locks have
// their own conflict rule, a shared lock may be stacked on the same handle's
// exclusive lock, and mixing lock and unlock elements has a defined effect.
// ---------------------------------------------------------------------------

const STATUS_INVALID_LOCK_RANGE = 0xc000001a;
const UINT64_MAX = 0xffffffffffffffffn;


// A zero-length lock at X is only crossed by a range that strictly spans X.
function rangesConflict(o1, l1, o2, l2) {
  if (l1 === 0n && l2 === 0n) return false;
  if (l1 === 0n) return o2 < o1 && o1 < o2 + l2;
  if (l2 === 0n) return o1 < o2 && o2 < o1 + l1;
  return o1 < o2 + l2 && o2 < o1 + l1;
}

function rwConflict(open, offset, length, write) {
  if (length === 0n) return false;
  for (const l of open.node.locks) {
    if (l.open === open) continue;
    if (!rangesConflict(offset, length, l.offset, l.length)) continue;
    if (l.exclusive || write) return true;
  }
  return false;
}

function takeLock(open, el) {
  const node = open.node;
  const ex = (el.flags & LOCK_EXCLUSIVE) !== 0;
  for (const l of node.locks) {
    if (!rangesConflict(el.offset, el.length, l.offset, l.length)) continue;
    if (ex) return false;                             // exclusive excludes everything
    if (l.exclusive && l.open !== open) return false; // shared vs a foreign exclusive
  }
  node.locks.push({ open, offset: el.offset, length: el.length, exclusive: ex, pid: 0 });
  return true;
}

// One unlock removes one stacked lock, exclusive layers first.
function dropLock(open, el) {
  const locks = open.node.locks;
  let idx = -1;
  for (let i = 0; i < locks.length; i++) {
    const l = locks[i];
    if (l.open !== open || l.offset !== el.offset || l.length !== el.length) continue;
    if (l.exclusive) { idx = i; break; }
    if (idx < 0) idx = i;
  }
  if (idx < 0) return false;
  locks.splice(idx, 1);
  return true;
}

function retryLockWaiters(node) {
  if (!node || !node.smb2LockWaiters || node.smb2LockWaiters.length === 0) return;
  for (const w of node.smb2LockWaiters.slice()) w.attempt();
}

// A READ on a pipe hands back one message, waiting for it if need be.
function pipeRead(req, open, length) {
  return new Promise((resolve) => {
    let settled = false;
    const finish = (res) => {
      if (settled) return;
      settled = true;
      const stt = pipeStatus(res.status);
      if (stt !== S.STATUS_SUCCESS && stt !== S.STATUS_BUFFER_OVERFLOW) {
        resolve({ status: stt, body: null });
      } else {
        resolve({ status: stt, body: readBody(res.data || Buffer.alloc(0)) });
      }
    };
    const r = open.pipe.read(length, (res) => {
      if (req.asyncEntry) {
        const stt = pipeStatus(res.status);
        const body = (stt === S.STATUS_SUCCESS || stt === S.STATUS_BUFFER_OVERFLOW)
          ? readBody(res.data || Buffer.alloc(0)) : null;
        finishAsync(req, stt, body);
      } else {
        finish(res);
      }
    });
    if (pipeStatus(r.status) !== S.STATUS_PENDING) { finish(r); return; }
    // Nothing queued yet: go asynchronous and let the callback answer.
    const onCancel = () => { if (r.cancel) { try { r.cancel(); } catch (e) {} } };
    onCancel.now = true;
    startAsync(req, onCancel);
    settled = true;
    resolve({ status: S.STATUS_PENDING, body: null });
  });
}

async function doLock(req) {
  const b = req.body;
  if (b.length < 24 || b.readUInt16LE(0) !== 48) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const count = b.readUInt16LE(2);
  const open = openFromRequest(req, b, 8);
  if (count === 0) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (24 + count * 24 > b.length) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const els = [];
  for (let i = 0; i < count; i++) {
    const o = 24 + i * 24;
    els.push({
      offset: b.readBigUInt64LE(o), length: b.readBigUInt64LE(o + 8),
      flags: b.readUInt32LE(o + 16),
    });
  }
  for (const l of els) {
    const kind = l.flags & (LOCK_SHARED | LOCK_EXCLUSIVE | LOCK_UNLOCK);
    if (kind !== LOCK_SHARED && kind !== LOCK_EXCLUSIVE && kind !== LOCK_UNLOCK) {
      return { status: S.STATUS_INVALID_PARAMETER, body: null };
    }
    if ((l.flags & LOCK_UNLOCK) && (l.flags & LOCK_FAIL_IMMEDIATELY)) {
      return { status: S.STATUS_INVALID_PARAMETER, body: null };
    }
    if (l.length > 0n && l.offset + l.length - 1n > UINT64_MAX) {
      return { status: STATUS_INVALID_LOCK_RANGE, body: null };
    }
  }
  const firstIsUnlock = (els[0].flags & LOCK_UNLOCK) !== 0;

  if (firstIsUnlock) {
    // Unlocks take effect up to the first element that is not an unlock; the
    // request as a whole is then rejected ([MS-SMB2] 3.3.5.14.1).
    for (const l of els) {
      if (!(l.flags & LOCK_UNLOCK)) {
        retryLockWaiters(open.node);
        return { status: S.STATUS_INVALID_PARAMETER, body: null };
      }
      if (!dropLock(open, l)) {
        retryLockWaiters(open.node);
        return { status: S.STATUS_RANGE_NOT_LOCKED, body: null };
      }
    }
    retryLockWaiters(open.node);
    return { status: S.STATUS_SUCCESS, body: simple4(4) };
  }

  // A multi-element lock request must have SMB2_LOCKFLAG_FAIL_IMMEDIATELY set
  // on every element ([MS-SMB2] 3.3.5.14).
  if (count > 1) {
    for (const l of els) {
      if (!(l.flags & LOCK_UNLOCK) && !(l.flags & LOCK_FAIL_IMMEDIATELY)) {
        return { status: S.STATUS_INVALID_PARAMETER, body: null };
      }
    }
  }

  const tryAll = () => {
    const taken = [];
    for (const l of els) {
      if (l.flags & LOCK_UNLOCK) {                  // lock followed by unlock
        for (const t of taken) dropLock(open, t);
        return 'mixed';
      }
      if (!takeLock(open, l)) {
        for (const t of taken) dropLock(open, t);
        return false;
      }
      taken.push(l);
    }
    return true;
  };

  const first = tryAll();
  if (first === 'mixed') return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (first === true) return { status: S.STATUS_SUCCESS, body: simple4(4) };
  if (els.every((l) => (l.flags & LOCK_FAIL_IMMEDIATELY) !== 0)) {
    return { status: S.STATUS_LOCK_NOT_GRANTED, body: null };
  }

  // Blocking lock: interim STATUS_PENDING now, the real answer later.
  const node = open.node;
  node.smb2LockWaiters = node.smb2LockWaiters || [];
  const waiter = { attempt: null, timer: null };
  const entry = startAsync(req, () => {
    const i = node.smb2LockWaiters.indexOf(waiter);
    if (i >= 0) node.smb2LockWaiters.splice(i, 1);
    clearInterval(waiter.timer);
    finishAsync(req, S.STATUS_CANCELLED, null);
  });
  waiter.attempt = () => {
    if (entry.done) return;
    const finish = (status, body) => {
      const i = node.smb2LockWaiters.indexOf(waiter);
      if (i >= 0) node.smb2LockWaiters.splice(i, 1);
      clearInterval(waiter.timer);
      finishAsync(req, status, body);
    };
    if (open.closed || req.conn.closed) { finish(S.STATUS_RANGE_NOT_LOCKED, null); return; }
    if (tryAll() === true) finish(S.STATUS_SUCCESS, simple4(4));
  };
  waiter.timer = setInterval(() => waiter.attempt(), 40);
  if (waiter.timer.unref) waiter.timer.unref();
  node.smb2LockWaiters.push(waiter);
  return { status: S.STATUS_PENDING, body: null };
}

// ---------------------------------------------------------------------------
// CHANGE_NOTIFY ([MS-SMB2] 2.2.35 / 2.2.36, 3.3.5.19)
//
// Once a directory handle has been used for a CHANGE_NOTIFY it keeps a change
// buffer for the life of the handle: changes are accumulated even when no
// request is outstanding, and the next request drains the whole buffer.  If
// the accumulated records do not fit in OutputBufferLength the buffer is
// dropped and STATUS_NOTIFY_ENUM_DIR tells the client to re-enumerate.
// ---------------------------------------------------------------------------

const STATUS_NOTIFY_CLEANUP = 0x0000010b;

const NOTIFY = {
  FILE_NAME: 0x00000001, DIR_NAME: 0x00000002, ATTRIBUTES: 0x00000004,
  SIZE: 0x00000008, LAST_WRITE: 0x00000010, LAST_ACCESS: 0x00000020,
  CREATION: 0x00000040, EA: 0x00000080, SECURITY: 0x00000100,
  STREAM_NAME: 0x00000200, STREAM_SIZE: 0x00000400, STREAM_WRITE: 0x00000800,
};
const ACT = { ADDED: 1, REMOVED: 2, MODIFIED: 3, RENAMED_OLD: 4, RENAMED_NEW: 5 };

function watchers(share) {
  if (!share._watchers) share._watchers = [];
  return share._watchers;
}

function notifyState(open) {
  if (!open.notify) {
    open.notify = { changes: [], filter: 0, watchTree: false, req: null, entry: null, maxOut: 0 };
    watchers(open.share).push(open);
    open.node.notifies.push(open.notify);
  }
  return open.notify;
}

function dropDirWatch(open) {
  const ns = open.notify;
  if (!ns) return;
  const list = watchers(open.share);
  const i = list.indexOf(open);
  if (i >= 0) list.splice(i, 1);
  const j = open.node.notifies.indexOf(ns);
  if (j >= 0) open.node.notifies.splice(j, 1);
  if (ns.entry && !ns.entry.done) {
    const req = ns.req;
    ns.req = null; ns.entry = null;
    finishAsync(req, STATUS_NOTIFY_CLEANUP, notifyBody(Buffer.alloc(0)));
  }
  open.notify = null;
}

function notifyBody(data) {
  const b = Buffer.alloc(8 + Math.max(1, data.length));
  b.writeUInt16LE(9, 0);
  b.writeUInt16LE(data.length ? HDR + 8 : 0, 2);
  b.writeUInt32LE(data.length, 4);
  if (data.length) data.copy(b, 8);
  return b;
}

// FILE_NOTIFY_INFORMATION ([MS-FSCC] 2.7.1); entries are 4-byte aligned.
function marshalChanges(changes) {
  const parts = [];
  let total = 0;
  for (let i = 0; i < changes.length; i++) {
    const n = uni(changes[i].name.replace(/\//g, '\\'));
    const size = 12 + n.length;
    const padded = (i === changes.length - 1) ? size : ((size + 3) & ~3);
    const b = Buffer.alloc(padded);
    b.writeUInt32LE(i === changes.length - 1 ? 0 : padded, 0);
    b.writeUInt32LE(changes[i].action, 4);
    b.writeUInt32LE(n.length, 8);
    n.copy(b, 12);
    parts.push(b);
    total += padded;
  }
  return Buffer.concat(parts, total);
}

function deliverNotify(open) {
  const ns = open.notify;
  if (!ns || !ns.entry || ns.entry.done || ns.changes.length === 0) return;
  const req = ns.req;
  const changes = ns.changes;
  const maxOut = ns.maxOut;
  ns.changes = [];
  ns.req = null; ns.entry = null;
  const data = marshalChanges(changes);
  if (data.length > maxOut) finishAsync(req, S.STATUS_NOTIFY_ENUM_DIR, notifyBody(Buffer.alloc(0)));
  else finishAsync(req, S.STATUS_SUCCESS, notifyBody(data));
}

// `rel` is the share-relative path that changed.
function notifyChange(share, rel, action, filterBits, extra) {
  const list = share._watchers;
  if (!list || list.length === 0) return;
  for (const open of list.slice()) {
    const ns = open.notify;
    if (!ns) continue;
    const dir = open.rel;
    const prefix = dir === '' ? '' : dir + '/';
    if (!String(rel).startsWith(prefix)) continue;
    const sub = String(rel).slice(prefix.length);
    if (sub === '') continue;
    if (!ns.watchTree && sub.includes('/')) continue;
    if ((ns.filter & filterBits) === 0) continue;
    ns.changes.push({ action, name: sub });
    if (extra) ns.changes.push({ action: extra.action, name: extra.name });
    if (ns.changes.length > 1000) ns.changes = ns.changes.slice(0, 1000);
    deliverNotify(open);
  }
}

async function doChangeNotify(req) {
  const b = req.body;
  if (b.length < 32 || b.readUInt16LE(0) !== 32) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const flags = b.readUInt16LE(2);
  const maxOut = b.readUInt32LE(4);
  const open = openFromRequest(req, b, 8);
  const filter = b.readUInt32LE(24);
  if (!open.isDir) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (!(open.access & A.FILE_READ_DATA)) return { status: S.STATUS_ACCESS_DENIED, body: null };
  if (maxOut > cstate(req.conn).maxTrans) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (filter === 0 || (filter & ~0x00000fff)) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (open.notify && open.notify.entry && !open.notify.entry.done) {
    return { status: S.STATUS_INVALID_PARAMETER, body: null };
  }

  const ns = notifyState(open);
  if (req.server.debugLevel > 1) {
    req.conn.log('smb2: notify dir=' + JSON.stringify(open.rel) + ' maxout=' + maxOut +
                 ' filter=0x' + filter.toString(16) + ' buffered=' + ns.changes.length);
  }
  ns.filter = filter;
  ns.watchTree = (flags & NOTIFY_WATCH_TREE) !== 0;
  ns.maxOut = maxOut;

  if (ns.changes.length) {
    const changes = ns.changes;
    ns.changes = [];
    const data = marshalChanges(changes);
    if (data.length > maxOut) return { status: S.STATUS_NOTIFY_ENUM_DIR, body: notifyBody(Buffer.alloc(0)) };
    return { status: S.STATUS_SUCCESS, body: notifyBody(data) };
  }

  const entry = startAsync(req, () => {
    if (ns.entry === entry) { ns.req = null; ns.entry = null; }
    finishAsync(req, S.STATUS_CANCELLED, notifyBody(Buffer.alloc(0)));
  });
  ns.req = req;
  ns.entry = entry;
  return { status: S.STATUS_PENDING, body: null };
}

// info.dirEntry() fills the 8.3 field with vfs.mangle(); replace it with the
// reversible form so a listing and a FileAlternateNameInformation query agree.
function patchShortName(cls, buf, name) {
  if (cls !== INFO.FILE.BothDirectory && cls !== INFO.FILE.IdBothDirectory) return;
  if (buf.length < 94) return;
  buf.fill(0, 68, 94);
  if (is83(name)) return;
  const s = uni(shortNameOf(name));
  const n = Math.min(s.length, 24);
  buf.writeUInt8(n, 68);
  s.copy(buf, 70, 0, n);
}

const DIR_CLASSES = new Set([
  INFO.FILE.Directory, INFO.FILE.FullDirectory, INFO.FILE.BothDirectory,
  INFO.FILE.IdFullDirectory, INFO.FILE.IdBothDirectory, INFO.FILE.Names,
]);

async function doQueryDirectory(req) {
  const b = req.body, st = cstate(req.conn);
  if (b.length < 32 || b.readUInt16LE(0) !== 33) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const cls = b.readUInt8(2);
  const flags = b.readUInt8(3);
  const fileIndex = b.readUInt32LE(4);
  const open = openFromRequest(req, b, 8);
  const nameOff = b.readUInt16LE(24), nameLen = b.readUInt16LE(26);
  const outLen = b.readUInt32LE(28);
  if (!open.isDir) return { status: S.STATUS_NOT_SUPPORTED, body: null };
  if (outLen > st.maxTrans) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  if (!DIR_CLASSES.has(cls)) return { status: S.STATUS_INVALID_INFO_CLASS, body: null };
  if (!(open.access & A.FILE_READ_DATA)) return { status: S.STATUS_ACCESS_DENIED, body: null };

  // An empty search pattern is a malformed request, not "match everything"
  // ([MS-SMB2] 3.3.5.18); smbclient's `ls dir\\` relies on the rejection.
  if (nameLen === 0) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  let pattern = null;
  if (nameLen) {
    const s = nameOff - HDR;
    if (s < 0 || s + nameLen > b.length) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    pattern = unistr(b.subarray(s, s + nameLen));
    if (pattern.includes('\\') || pattern.includes('/')) return { status: S.STATUS_OBJECT_NAME_INVALID, body: null };
    if (!V.validName(pattern, true)) return { status: S.STATUS_OBJECT_NAME_INVALID, body: null };
  }

  const restart = (flags & (QD_RESTART_SCANS | QD_REOPEN)) !== 0;
  let en = open.dirEnum;
  if (!en || restart || (pattern !== null && pattern !== en.pattern)) {
    const pat = pattern === null ? (en ? en.pattern : '*') : pattern;
    let list;
    try { list = OPS.listDirectory(open.share, open.rel, pat, true); }
    catch (e) { if (e instanceof SmbError) return { status: e.status, body: null }; throw e; }
    en = { pattern: pat, list, index: 0, served: false };
    open.dirEnum = en;
  }
  if ((flags & QD_INDEX_SPECIFIED) && fileIndex > 0 && fileIndex <= en.list.length) en.index = fileIndex;

  while (en.index < en.list.length && !open.share.stat(en.list[en.index].rel)) en.index++;
  if (en.index >= en.list.length) {
    return { status: en.served ? S.STATUS_NO_MORE_FILES : S.STATUS_NO_SUCH_FILE, body: null };
  }

  const bufs = [];
  let total = 0, taken = 0, lastEnt = null, lastIdx = 0;
  while (en.index < en.list.length) {
    const ent = en.list[en.index];
    // Re-stat: entries deleted since the scan started must not be handed back.
    const fresh = open.share.stat(ent.rel);
    if (!fresh) { en.index++; continue; }
    rememberMangled(open.share, ent.name);
    const e2 = { name: ent.name, rel: ent.rel, st: viewStat(open.share, ent.rel, fresh) };
    let eb;
    try { eb = INFO.dirEntry(cls, open.share, e2, 0, false); }
    catch (e) { if (e instanceof SmbError) return { status: e.status, body: null }; throw e; }
    patchShortName(cls, eb, ent.name);
    if (total + eb.length > outLen) break;
    bufs.push(eb); total += eb.length; taken++;
    lastEnt = e2; lastIdx = 0;
    en.index++;
    if (flags & QD_RETURN_SINGLE_ENTRY) break;
  }
  if (taken === 0) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
  bufs[bufs.length - 1] = INFO.dirEntry(cls, open.share, lastEnt, lastIdx, true);
  patchShortName(cls, bufs[bufs.length - 1], lastEnt.name);
  const data = Buffer.concat(bufs);
  en.served = true;
  const body = Buffer.alloc(8 + Math.max(1, data.length));
  body.writeUInt16LE(9, 0);
  body.writeUInt16LE(HDR + 8, 2);
  body.writeUInt32LE(data.length, 4);
  data.copy(body, 8);
  return { status: S.STATUS_SUCCESS, body };
}

// ---------------------------------------------------------------------------
// Security descriptors ([MS-DTYP] 2.4.6) -- minimal but well-formed
// ---------------------------------------------------------------------------

// Identity and the access check both live in vfs.js so that SMB1 and SMB2
// answer with the same SIDs for the same file (SD-NOTES.md 7.1); the only thing
// this layer adds is who the caller is.

// This server does not impersonate, so anything it creates is owned by the
// account it runs as.  Report the authenticated user instead -- that is the
// ownership an impersonating server would have left behind.
const SERVER_UID = typeof process.getuid === 'function' ? process.getuid() : 0;

function identityFor(req, st) {
  const user = req.session && req.session.user;
  const uid = user && typeof user.uid === 'number' ? user.uid : 1000;
  const fuid = st && typeof st.uid === 'number' ? st.uid : uid;
  const fgid = st && typeof st.gid === 'number' ? st.gid : uid;
  if (fuid === SERVER_UID) return { owner: uid, group: uid, substituted: true };
  return { owner: fuid, group: fgid, substituted: false };
}

function callerToken(req, share) {
  return share.tokenFor(req.session ? req.session.user : null);
}

// What governs the object.  A descriptor that was set on it, or one derived
// from a file we did not create, comes straight from vfs.js so the answer is
// byte-identical to the SMB1 one; only a file this server created itself needs
// its ownership substituted.
function effectiveSd(req, share, rel, st) {
  const t = st || share.stat(rel);
  const meta = share.meta.get(String(rel).toLowerCase());
  const who = identityFor(req, t);
  if ((meta && meta.sd) || !who.substituted) return share.securityDescriptorFor(rel, t);
  return SD.defaultSecurityDescriptor(share.ownerSidFor(who.owner), share.groupSidFor(who.group),
                                      t ? (t.mode & 0o777) : 0o644,
                                      { isContainer: !!(t && t.isDirectory()),
                                        aclMapFullControl: share.aclMapFullControl });
}

// Merge `buf` into what the object carries and store it.  Assigning to the
// meta record is what runs vfs.js's NT-ACL-to-POSIX-ACL conversion, so the
// merge is done here where the substituted ownership is known.
function storeDescriptor(req, share, rel, buf, si) {
  const incoming = SD.parseSecurityDescriptor(buf);
  const merged = SD.applySecurityInformation(
    effectiveSd(req, share, rel, share.stat(rel)), incoming, si);
  merged.control |= SD.SE.SE_DACL_PROTECTED | SD.SE.SE_DACL_PRESENT;
  share.metaFor(rel).sd = SD.buildSecurityDescriptor(merged);
}

function securityDescriptor(req, share, rel, st, want) {
  const sd = effectiveSd(req, share, rel, st);
  if (!sd) return null;
  return SD.buildSecurityDescriptor(SD.filterSecurityInformation(sd, want));
}

// The [MS-DTYP] 2.5.3 access check for `desired`, or null when the object has
// no explicit descriptor and its permission bits still speak for it.
function checkObjectAccess(req, share, rel, desired, isContainer) {
  const meta = share.meta.get(String(rel).toLowerCase());
  if (!meta || !meta.sd) return null;
  const st = share.stat(rel);
  if (!st) return null;
  const token = callerToken(req, share);
  const maxAllowed = (desired & A.MAXIMUM_ALLOWED) !== 0;
  const r = share.checkAccess(rel, token, maxAllowed ? A.MAXIMUM_ALLOWED : (desired >>> 0), st);
  return { status: r.status === S.STATUS_SUCCESS ? SD.STATUS_SUCCESS : r.status,
           granted: r.granted >>> 0 };
}

// [MS-FSCC] fixed sizes: the smallest buffer each information class fits in.
const FILE_FIXED = {
  4: 40, 5: 24, 6: 8, 7: 4, 8: 4, 9: 4, 14: 8, 15: 8, 16: 4, 17: 4,
  18: 104, 21: 8, 22: 32, 28: 16, 34: 56, 35: 8, 48: 8,
};
const FS_FIXED = { 1: 24, 3: 24, 4: 8, 5: 16, 6: 48, 7: 32, 8: 64, 11: 28 };
const ATTR_CLASSES = new Set([4, 18, 34, 35]);

function smbPath(rel) { return '\\' + String(rel).replace(/\//g, '\\'); }

// Byte offset of AllocationSize inside the info classes that carry one.
const ALLOC_AT = {
  [INFO.FILE.Standard]: 0, [INFO.FILE.All]: 40, [INFO.FILE.NetworkOpen]: 32,
};

function patchAllocation(share, rel, cls, buf) {
  const at = ALLOC_AT[cls];
  if (at === undefined || !buf || buf.length < at + 8) return buf;
  const meta = share.meta.get(String(rel).toLowerCase());
  const want = meta && meta.allocSize ? BigInt(meta.allocSize) : 0n;
  if (want > buf.readBigUInt64LE(at)) buf.writeBigUInt64LE(want, at);
  return buf;
}

function fileInfoBuffer(req, open, cls) {
  const share = open.share;
  const raw = share.stat(open.rel);
  if (!raw) fail(S.STATUS_FILE_CLOSED);
  const st = viewStat(share, open.rel, raw);
  if (ALLOC_AT[cls] !== undefined) {
    return patchAllocation(share, open.rel, cls, fileInfoBufferRaw(req, open, cls, st));
  }
  return fileInfoBufferRaw(req, open, cls, st);
}

function fileInfoBufferRaw(req, open, cls, st) {
  const share = open.share;
  switch (cls) {
    case INFO.FILE.Basic: return INFO.basic(share, open.rel, st);
    case INFO.FILE.Standard: return INFO.standard(share, open.rel, st, open.node);
    case INFO.FILE.Internal: return INFO.internal(st);
    case INFO.FILE.Ea: return Buffer.alloc(4);
    case INFO.FILE.Access: return INFO.accessInfo(open.access);
    case INFO.FILE.Name: return INFO.nameInfo(smbPath(open.rel));
    case INFO.FILE.Position: return INFO.positionInfo(open.position || 0);
    case INFO.FILE.Mode: return INFO.modeInfo(open.modeFlags || 0);
    case INFO.FILE.Alignment: return INFO.alignmentInfo();
    case INFO.FILE.All: {
      const buf = INFO.allInfo(share, open.rel, st, open, smbPath(open.rel));
      buf.writeBigUInt64LE(BigInt(open.position || 0), 80);
      buf.writeUInt32LE(open.modeFlags || 0, 88);
      buf.writeUInt8(open.node && open.node.deletePending ? 1 : 0, 60);
      return buf;
    }
    case INFO.FILE.AlternateName: {
      const base = path.basename(open.rel) || '';
      rememberMangled(share, base);
      return INFO.nameInfo(altNameOf(base));
    }
    case INFO.FILE.Stream: return INFO.streamInfo(share, open.rel, st);
    case INFO.FILE.Compression: return INFO.compression(st);
    case INFO.FILE.NetworkOpen: return INFO.networkOpen(share, open.rel, st);
    case INFO.FILE.AttributeTag: return INFO.attributeTag(share, open.rel, st);
    case INFO.FILE.FullEa: return eaListBuffer(share, open.rel);
    case INFO.FILE.IdInformation: fail(S.STATUS_NOT_SUPPORTED); break;
    case INFO.FILE.NormalizedName: return INFO.nameInfo(String(open.rel).replace(/\//g, '\\'));
    default: fail(S.STATUS_INVALID_INFO_CLASS);
  }
  return Buffer.alloc(0);
}

function eaListBuffer(share, rel) {
  const meta = share.metaFor(rel);
  if (!meta.ea || meta.ea.size === 0) return Buffer.alloc(0);
  const items = [];
  const keys = Array.from(meta.ea.keys());
  for (let i = 0; i < keys.length; i++) {
    const nm = Buffer.from(keys[i], 'latin1');
    const val = meta.ea.get(keys[i]);
    const size = 9 + nm.length + val.length;
    const padded = (i === keys.length - 1) ? size : pad8(size);
    const b = Buffer.alloc(padded);
    b.writeUInt32LE(i === keys.length - 1 ? 0 : padded, 0);
    b.writeUInt8(0, 4);
    b.writeUInt8(nm.length, 5);
    b.writeUInt16LE(val.length, 6);
    nm.copy(b, 8);
    val.copy(b, 9 + nm.length);
    items.push(b);
  }
  return Buffer.concat(items);
}

function queryBody(data, status) {
  const b = Buffer.alloc(8 + Math.max(1, data.length));
  b.writeUInt16LE(9, 0);
  b.writeUInt16LE(HDR + 8, 2);
  b.writeUInt32LE(data.length, 4);
  data.copy(b, 8);
  return b;
}

async function doQueryInfo(req) {
  const b = req.body;
  if (b.length < 40 || b.readUInt16LE(0) !== 41) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const infoType = b.readUInt8(2);
  const cls = b.readUInt8(3);
  const outLen = b.readUInt32LE(4);
  const addl = b.readUInt32LE(16);
  const open = openFromRequest(req, b, 24);
  if (open.isPipe) return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
  const share = open.share;
  if (req.server.debugLevel > 1) {
    req.conn.log('smb2: qinfo type=' + infoType + ' cls=' + cls + ' out=' + outLen + ' addl=0x' + addl.toString(16));
  }

  // Quota support is compiled out; Samba rejects the handle rather than the
  // class ([MS-SMB2] 3.3.5.20.4 / smbd_smb2_query_info).
  if (infoType === INFO_QUOTA) return { status: S.STATUS_INVALID_HANDLE, body: null };

  if (infoType === INFO_FILE) {
    if (ATTR_CLASSES.has(cls) && !(open.access & A.FILE_READ_ATTRIBUTES)) {
      return { status: S.STATUS_ACCESS_DENIED, body: null };
    }
    if (cls === INFO.FILE.FullEa && !(open.access & A.FILE_READ_EA)) {
      return { status: S.STATUS_ACCESS_DENIED, body: null };
    }
    let data;
    try { data = fileInfoBuffer(req, open, cls); }
    catch (e) { if (e instanceof SmbError) return { status: e.status, body: null }; throw e; }
    const fixed = FILE_FIXED[cls] !== undefined ? FILE_FIXED[cls] : data.length;
    if (outLen === 0 || outLen < fixed) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
    if (data.length > outLen) {
      return { status: S.STATUS_BUFFER_OVERFLOW, body: queryBody(data.subarray(0, outLen)) };
    }
    return { status: S.STATUS_SUCCESS, body: queryBody(data) };
  }

  if (infoType === INFO_FILESYSTEM) {
    let data;
    try { data = INFO.fsInfo(cls, share, share.name.toUpperCase()); }
    catch (e) { if (e instanceof SmbError) return { status: e.status, body: null }; throw e; }
    const fixed = FS_FIXED[cls] !== undefined ? FS_FIXED[cls] : data.length;
    if (outLen === 0 || outLen < fixed) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
    if (data.length > outLen) {
      return { status: S.STATUS_BUFFER_OVERFLOW, body: queryBody(data.subarray(0, outLen)) };
    }
    return { status: S.STATUS_SUCCESS, body: queryBody(data) };
  }

  if (infoType === INFO_SECURITY) {
    if (addl & 0x00000008) return { status: S.STATUS_ACCESS_DENIED, body: null };  // SACL
    if (!(open.access & A.READ_CONTROL)) return { status: S.STATUS_ACCESS_DENIED, body: null };
    const st = share.stat(open.rel);
    const sd = securityDescriptor(req, share, open.rel, st, addl || 0x07);
    if (outLen < sd.length) {
      const need = Buffer.alloc(4);
      need.writeUInt32LE(sd.length, 0);
      return { status: S.STATUS_BUFFER_TOO_SMALL, body: errorBody(need) };
    }
    return { status: S.STATUS_SUCCESS, body: queryBody(sd) };
  }
  return { status: S.STATUS_INVALID_PARAMETER, body: null };
}

// ---------------------------------------------------------------------------
// SET_INFO ([MS-SMB2] 2.2.39 / 2.2.40, 3.3.5.21)
// ---------------------------------------------------------------------------

function applyTime(open, field, ft) {
  if (ft === NT_OMIT || ft === NT_FREEZE || ft === NT_THAW) return;
  const share = open.share, rel = open.rel;
  const m = share.metaFor(rel);
  if (field === 'write') {
    if (m.wtTimer) { clearTimeout(m.wtTimer); m.wtTimer = null; }
    m.wtPending = false; m.wtPreValue = undefined; m.wtDirty = false;
    m.wtStored = undefined;
    m.tsWrite = ft;
  } else if (field === 'access') {
    m.tsAccess = ft;
  } else if (field === 'change') {
    m.tsChange = ft;
  } else if (field === 'create') {
    m.createTime = ftToMs(ft);
    open.sticky.create = null;
  }
  if (field === 'write' || field === 'access') {
    const st = share.stat(rel);
    if (st) {
      const at = m.tsAccess !== undefined ? fromFileTime(m.tsAccess) / 1000 : st.atimeMs / 1000;
      const mt = m.tsWrite !== undefined ? fromFileTime(m.tsWrite) / 1000 : st.mtimeMs / 1000;
      try { fs.utimesSync(share.real(rel), at, mt); }
      catch (e) { /* out of range for the host fs; the cached value answers */ }
    }
  }
}

function doRename(req, open, data, replaceDefault) {
  const share = open.share;
  if (data.length < 20) fail(S.STATUS_INFO_LENGTH_MISMATCH);
  const replace = data.readUInt8(0) !== 0;
  const nameLen = data.readUInt32LE(16);
  if (20 + nameLen > data.length) fail(S.STATUS_INFO_LENGTH_MISMATCH);
  let target = unistr(data.subarray(20, 20 + nameLen));
  if (!(open.access & A.DELETE)) fail(S.STATUS_ACCESS_DENIED);
  if (share.readOnly) fail(S.STATUS_ACCESS_DENIED);
  for (const o of open.node.opens) {
    if (o === open) continue;
    if (!(o.shareAccess & SHARE.DELETE)) fail(S.STATUS_ACCESS_DENIED);
  }
  // A rename removes the name from the parent directory, so it is checked as
  // an open of the parent for DELETE without sharing the delete right.
  const parentRel = path.dirname(open.rel) === '.' ? '' : path.dirname(open.rel);
  const pnode = nodeOf(share, parentRel);
  if (pnode && V.shareConflict(pnode, A.DELETE, SHARE.READ | SHARE.WRITE, null)) {
    fail(S.STATUS_SHARING_VIOLATION);
  }
  // A directory cannot be renamed while anything below it is open.
  if (open.isDir) {
    const prefix = open.rel === '' ? '' : open.rel + '/';
    for (const n of share.nodes.values()) {
      if (n === open.node || n.opens.size === 0) continue;
      if (n.rel.startsWith(prefix)) fail(S.STATUS_ACCESS_DENIED);
    }
  }
  const dst = share.resolve(target);
  if (dst === '') fail(S.STATUS_OBJECT_NAME_INVALID);
  if (dst.toLowerCase() === open.rel.toLowerCase() && dst !== open.rel) {
    // case-only rename: go straight through
  } else {
    const ex = share.stat(dst);
    if (ex) {
      if (!replace) fail(S.STATUS_OBJECT_NAME_COLLISION);
      const n = nodeOf(share, dst);
      if (n && n.opens.size) fail(S.STATUS_ACCESS_DENIED);
      if (ex.isDirectory()) fail(S.STATUS_ACCESS_DENIED);
      try { fs.unlinkSync(share.real(dst)); } catch (e) { fail(OPS.mapErrno(e)); }
    }
  }
  const parent = path.dirname(dst);
  if (parent !== '.' && !share.stat(parent)) fail(S.STATUS_OBJECT_PATH_NOT_FOUND);
  try { fs.renameSync(share.real(open.rel), share.real(dst)); }
  catch (e) { fail(OPS.mapErrno(e)); }
  const from = open.rel;
  share.moveMeta(from, dst);
  // Renaming a file sets the archive bit (and clears NORMAL); directories are
  // left alone.  smb1.js's doRename does the same.
  if (!open.isDir) {
    share.applyAttributes(dst, (share.attributes(dst) | ATTR.ARCHIVE) & ~ATTR.NORMAL);
  }
  const nameFilter = open.isDir ? NOTIFY.DIR_NAME : NOTIFY.FILE_NAME;
  if (path.dirname(from) === path.dirname(dst)) {
    notifyChange(share, from, ACT.RENAMED_OLD, nameFilter,
                 { action: ACT.RENAMED_NEW, name: path.basename(dst) });
  } else {
    notifyChange(share, from, ACT.REMOVED, nameFilter);
    notifyChange(share, dst, ACT.ADDED, nameFilter);
  }
  for (const o of open.node.opens) o.rel = dst;
  open.node.rel = dst;
}

async function doSetInfo(req) {
  const b = req.body;
  if (b.length < 32 || b.readUInt16LE(0) !== 33) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const infoType = b.readUInt8(2);
  const cls = b.readUInt8(3);
  const bufLen = b.readUInt32LE(4);
  const bufOff = b.readUInt16LE(8);
  const addl = b.readUInt32LE(12);
  const open = openFromRequest(req, b, 16);
  if (open.isPipe) return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
  const share = open.share;
  const s = bufOff - HDR;
  if (bufLen && (s < 0 || s + bufLen > b.length)) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const data = bufLen ? b.subarray(s, s + bufLen) : Buffer.alloc(0);
  if (req.server.debugLevel > 1) {
    req.conn.log('smb2: sinfo type=' + infoType + ' cls=' + cls + ' len=' + bufLen);
  }
  const ok = { status: S.STATUS_SUCCESS, body: (() => { const x = Buffer.alloc(2); x.writeUInt16LE(2, 0); return x; })() };

  try {
    if (infoType === INFO_QUOTA) return { status: S.STATUS_INVALID_INFO_CLASS, body: null };
    if (infoType === INFO_FILESYSTEM) return { status: S.STATUS_ACCESS_DENIED, body: null };
    if (infoType === INFO_SECURITY) {
      // [MS-SMB2] 3.3.5.21.2: each part of the descriptor has its own right --
      // WRITE_DAC for the DACL, WRITE_OWNER for the owner or group,
      // ACCESS_SYSTEM_SECURITY for the SACL.
      const si = addl >>> 0;
      if ((si & (SD.SI.OWNER_SECURITY_INFORMATION | SD.SI.GROUP_SECURITY_INFORMATION)) &&
          !(open.access & A.WRITE_OWNER)) {
        return { status: S.STATUS_ACCESS_DENIED, body: null };
      }
      if ((si & SD.SI.DACL_SECURITY_INFORMATION) && !(open.access & A.WRITE_DAC)) {
        return { status: S.STATUS_ACCESS_DENIED, body: null };
      }
      if ((si & (SD.SI.SACL_SECURITY_INFORMATION | SD.SI.LABEL_SECURITY_INFORMATION)) &&
          !(open.access & A.ACCESS_SYSTEM_SECURITY)) {
        return { status: S.STATUS_ACCESS_DENIED, body: null };
      }
      if (si === 0) return ok;
      try { storeDescriptor(req, share, open.rel, data, si); }
      catch (e) { return { status: S.STATUS_INVALID_SECURITY_DESCR, body: null }; }
      return ok;
    }
    if (infoType !== INFO_FILE) return { status: S.STATUS_INVALID_PARAMETER, body: null };

    switch (cls) {
      case INFO.FILE.Basic: {
        if (data.length < 36) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
        if (!(open.access & A.FILE_WRITE_ATTRIBUTES)) return { status: S.STATUS_ACCESS_DENIED, body: null };
        if (share.readOnly) return { status: S.STATUS_ACCESS_DENIED, body: null };
        // Any SET_FILE_BASIC_INFORMATION -- even an all-zero one -- flushes a
        // pending write-time update ([MS-FSA] 2.1.4.17 / Windows behaviour).
        flushWriteTime(share, open.rel);
        applyTime(open, 'create', data.readBigUInt64LE(0));
        applyTime(open, 'access', data.readBigUInt64LE(8));
        applyTime(open, 'write', data.readBigUInt64LE(16));
        applyTime(open, 'change', data.readBigUInt64LE(24));
        const at = data.readUInt32LE(32);
        if ((at & ATTR.TEMPORARY) && open.isDir) return { status: S.STATUS_INVALID_PARAMETER, body: null };
        if (at & (ATTR.DIRECTORY | ATTR.VOLUME | ATTR.DEVICE)) return { status: S.STATUS_INVALID_PARAMETER, body: null };
        if (at !== 0) {
          const meta = share.metaFor(open.rel);
          let keep = at & (ATTR.READONLY | ATTR.HIDDEN | ATTR.SYSTEM | ATTR.ARCHIVE |
                           ATTR.TEMPORARY | ATTR.OFFLINE | ATTR.NOT_CONTENT_INDEXED | ATTR.SPARSE_FILE);
          if (open.isDir) keep |= ATTR.DIRECTORY;
          meta.attrs = keep || ATTR.NORMAL;
          open.sticky.attrs = meta.attrs;
          const real = share.real(open.rel);
          try {
            const st = share.stat(open.rel);
            if (st && !open.isDir) {
              const mode = (at & ATTR.READONLY) ? (st.mode & ~0o222) : (st.mode | 0o200);
              fs.chmodSync(real, mode & 0o7777);
            }
          } catch (e) {}
          notifyChange(share, open.rel, ACT.MODIFIED, NOTIFY.ATTRIBUTES);
        }
        return ok;
      }
      case INFO.FILE.Rename:
      case INFO.FILE.RenameInformationEx:
        doRename(req, open, data);
        return ok;
      case INFO.FILE.Link: {
        if (data.length < 20) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
        const replace = data.readUInt8(0) !== 0;
        const nameLen = data.readUInt32LE(16);
        const target = unistr(data.subarray(20, 20 + nameLen));
        const dst = share.resolve(target);
        if (share.stat(dst) && !replace) return { status: S.STATUS_OBJECT_NAME_COLLISION, body: null };
        try { fs.linkSync(share.real(open.rel), share.real(dst)); }
        catch (e) { return { status: OPS.mapErrno(e), body: null }; }
        notifyChange(share, dst, ACT.ADDED, NOTIFY.FILE_NAME);
        return ok;
      }
      case INFO.FILE.Disposition:
      case INFO.FILE.DispositionInformationEx: {
        if (data.length < 1) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
        const del = cls === INFO.FILE.Disposition ? data.readUInt8(0) !== 0 : (data.readUInt32LE(0) & 1) !== 0;
        if (!(open.access & A.DELETE)) return { status: S.STATUS_ACCESS_DENIED, body: null };
        if (del) {
          if (share.readOnly) return { status: S.STATUS_CANNOT_DELETE, body: null };
          const st = share.stat(open.rel);
          const attrs = share.attributes(open.rel, st);
          if (attrs & ATTR.READONLY) return { status: S.STATUS_CANNOT_DELETE, body: null };
          if (open.isDir) {
            let kids = [];
            try { kids = fs.readdirSync(share.real(open.rel)); } catch (e) {}
            if (kids.length) return { status: S.STATUS_DIRECTORY_NOT_EMPTY, body: null };
          }
        }
        open.node.deletePending = del;
        open.deleteOnClose = del;
        return ok;
      }
      case INFO.FILE.Position: {
        if (data.length < 8) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
        open.position = data.readBigUInt64LE(0);
        return ok;
      }
      case INFO.FILE.Mode: {
        if (data.length < 4) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
        open.modeFlags = data.readUInt32LE(0);
        return ok;
      }
      case INFO.FILE.Allocation:
      case INFO.FILE.EndOfFile: {
        if (data.length < 8) return { status: S.STATUS_INFO_LENGTH_MISMATCH, body: null };
        if (open.isDir) return { status: S.STATUS_INVALID_PARAMETER, body: null };
        if (!(open.access & A.FILE_WRITE_DATA)) return { status: S.STATUS_ACCESS_DENIED, body: null };
        if (share.readOnly) return { status: S.STATUS_ACCESS_DENIED, body: null };
        const want = data.readBigUInt64LE(0);
        const st = share.stat(open.rel);
        if (cls === INFO.FILE.EndOfFile || (st && want < BigInt(st.size))) {
          try { fs.ftruncateSync(open.fd, Number(want)); }
          catch (e) { return { status: OPS.mapErrno(e), body: null }; }
        }
        markWriteTime(open);
        flushWriteTime(share, open.rel);
        notifyChange(share, open.rel, ACT.MODIFIED, NOTIFY.SIZE | NOTIFY.LAST_WRITE);
        return ok;
      }
      case INFO.FILE.FullEa: {
        if (!(open.access & A.FILE_WRITE_EA)) return { status: S.STATUS_ACCESS_DENIED, body: null };
        applyEaBuffer(share, open.rel, data);
        return ok;
      }
      case INFO.FILE.ValidDataLength: return ok;
      case INFO.FILE.ShortName: return { status: S.STATUS_ACCESS_DENIED, body: null };
      default:
        return { status: S.STATUS_INVALID_INFO_CLASS, body: null };
    }
  } catch (e) {
    if (e instanceof SmbError) return { status: e.status, body: null };
    throw e;
  }
}

// ---------------------------------------------------------------------------
// IOCTL ([MS-SMB2] 2.2.31 / 2.2.32, 3.3.5.15)
// ---------------------------------------------------------------------------

// FSCTL_PIPE_TRANSCEIVE: one write followed by one read on the same handle.
function pipeTransceive(req, open, id, input, maxOut) {
  if (req.server.debugLevel > 1) {
    req.conn.log('smb2: pipe transceive in=' + input.length + ' maxout=' + maxOut +
                 ' head=' + input.subarray(0, 24).toString('hex'));
  }
  return new Promise((resolve) => {
    let settled = false;
    const answer = (res) => {
      if (settled) return;
      settled = true;
      const stt = pipeStatus(res.status);
      if (stt !== S.STATUS_SUCCESS && stt !== S.STATUS_BUFFER_OVERFLOW) {
        resolve({ status: stt, body: null });
      } else {
        resolve({ status: stt,
                  body: ioctlResponse(req, FSCTL.PIPE_TRANSCEIVE, id, res.data || Buffer.alloc(0)) });
      }
    };
    const r = open.pipe.transceive(input, maxOut, (res) => {
      if (req.asyncEntry) {
        const stt = pipeStatus(res.status);
        const body = (stt === S.STATUS_SUCCESS || stt === S.STATUS_BUFFER_OVERFLOW)
          ? ioctlResponse(req, FSCTL.PIPE_TRANSCEIVE, id, res.data || Buffer.alloc(0)) : null;
        finishAsync(req, stt, body);
      } else {
        answer(res);
      }
    });
    if (pipeStatus(r.status) !== S.STATUS_PENDING) { answer(r); return; }
    const onCancel = () => { if (r.cancel) { try { r.cancel(); } catch (e) {} } };
    onCancel.now = true;
    startAsync(req, onCancel);
    settled = true;
    resolve({ status: S.STATUS_PENDING, body: null });
  });
}

function ioctlResponse(req, ctlCode, fileId, out, flags) {
  const data = out || Buffer.alloc(0);
  const body = Buffer.alloc(48 + Math.max(1, data.length));
  body.writeUInt16LE(49, 0);
  body.writeUInt16LE(0, 2);
  body.writeUInt32LE(ctlCode >>> 0, 4);
  body.writeBigUInt64LE(fileId.p, 8);
  body.writeBigUInt64LE(fileId.v, 16);
  body.writeUInt32LE(HDR + 48, 24);
  body.writeUInt32LE(0, 28);
  body.writeUInt32LE(data.length ? HDR + 48 : 0, 32);
  body.writeUInt32LE(data.length, 36);
  body.writeUInt32LE(flags || 0, 40);
  body.writeUInt32LE(0, 44);
  if (data.length) data.copy(body, 48);
  return body;
}

const PIPE_FSCTLS = new Set([FSCTL.PIPE_PEEK, FSCTL.PIPE_WAIT, FSCTL.PIPE_TRANSCEIVE]);

async function doIoctl(req) {
  const b = req.body, st = cstate(req.conn);
  if (b.length < 56 || b.readUInt16LE(0) !== 57) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const ctlCode = b.readUInt32LE(4);
  let id = readFileId(req, b, 8);
  const inOff = b.readUInt32LE(24), inCount = b.readUInt32LE(28);
  const maxOut = b.readUInt32LE(44);
  const flags = b.readUInt32LE(48);
  const so = inOff - HDR;
  const input = (inCount && so >= 0 && so + inCount <= b.length) ? b.subarray(so, so + inCount) : Buffer.alloc(0);
  const anyId = { p: 0xffffffffffffffffn, v: 0xffffffffffffffffn };
  if (req.server.debugLevel > 1) {
    req.conn.log('smb2: ioctl code=0x' + ctlCode.toString(16) + ' in=' + inCount +
                 ' maxout=' + maxOut + ' flags=' + flags);
  }

  if (!(flags & IOCTL_IS_FSCTL) && ctlCode !== FSCTL.PIPE_TRANSCEIVE) {
    return { status: S.STATUS_NOT_SUPPORTED, body: null };
  }

  // File-less FSCTLs
  if (ctlCode === FSCTL.DFS_GET_REFERRALS || ctlCode === FSCTL.DFS_GET_REFERRALS_EX) {
    // DFS is advertised at NEGOTIATE but no share is a DFS root, so every path
    // looked up here is simply absent.
    return { status: S.STATUS_NOT_FOUND, body: null };
  }
  if (ctlCode === FSCTL.VALIDATE_NEGOTIATE_INFO) {
    // [MS-SMB2] 2.2.31.4 / 3.3.5.15.12: repeat what was negotiated so the
    // client can prove the NEGOTIATE exchange was not tampered with.  Anything
    // that does not match what this connection actually saw is an attack, and
    // Samba drops the connection rather than answering.
    const cst = cstate(req.conn);
    if (cst.dialectInfo.value < 0x0300) return { status: S.STATUS_FILE_CLOSED, body: null };
    if (input.length < 24) { req.conn.destroy(); return null; }
    const wantCaps = input.readUInt32LE(0);
    const wantGuid = input.subarray(4, 20);
    const wantSecMode = input.readUInt16LE(20);
    const wantCount = input.readUInt16LE(22);
    const wantDialects = u16list(input, 24, wantCount);
    const same = wantCaps === (cst.clientCaps >>> 0) &&
      Buffer.from(req.conn.clientGuid || Buffer.alloc(16)).equals(wantGuid) &&
      wantSecMode === (req.conn.clientSecurityMode & 0xffff) &&
      wantCount === cst.clientDialects.length &&
      wantDialects.every((d, i) => d === cst.clientDialects[i]);
    if (!same) { req.conn.destroy(); return null; }
    const out = Buffer.alloc(24);
    out.writeUInt32LE(negotiateCaps(req.server, cst.dialectInfo, cst), 0);
    Buffer.from(req.server.guid).copy(out, 4);
    out.writeUInt16LE(securityModeOf(req.server), 20);
    out.writeUInt16LE(cst.dialectInfo.value, 22);
    if (maxOut < out.length) return { status: S.STATUS_BUFFER_TOO_SMALL, body: null };
    return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, anyId, out) };
  }
  if (ctlCode === FSCTL.QUERY_NETWORK_INTERFACE_INFO) {
    // [MS-SMB2] 2.2.32.5 NETWORK_INTERFACE_INFO: one loopback entry.
    const cst = cstate(req.conn);
    if (cst.dialectInfo.value < 0x0300) return { status: S.STATUS_NOT_SUPPORTED, body: null };
    const out = Buffer.alloc(152);
    out.writeUInt32LE(0, 0);            // Next: last (and only) entry
    out.writeUInt32LE(1, 4);            // IfIndex
    out.writeUInt32LE(0, 8);            // Capability: no RSS, no RDMA
    out.writeUInt32LE(0, 12);           // Reserved
    out.writeBigUInt64LE(1000000000n, 16);   // LinkSpeed, 1 Gbit/s
    out.writeUInt16LE(0x0002, 24);      // SOCKADDR_IN: AF_INET
    out.writeUInt16LE(0, 26);           // Port
    out.writeUInt8(127, 28); out.writeUInt8(0, 29); out.writeUInt8(0, 30); out.writeUInt8(1, 31);
    if (maxOut < out.length) return { status: S.STATUS_BUFFER_TOO_SMALL, body: null };
    return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, anyId, out) };
  }
  // Against a disk share each of the three answers differently in Samba 4.17
  // (measured): WAIT -> INVALID_PARAMETER, PEEK -> INVALID_DEVICE_REQUEST,
  // TRANSCEIVE -> NOT_SUPPORTED.  On IPC$ they reach the pipe below.
  if (PIPE_FSCTLS.has(ctlCode) && !(req.tree && req.tree.isIpc)) {
    if (ctlCode === FSCTL.PIPE_WAIT) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    if (ctlCode === FSCTL.PIPE_TRANSCEIVE) return { status: S.STATUS_NOT_SUPPORTED, body: null };
    return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
  }

  if (!id) return { status: S.STATUS_FILE_CLOSED, body: null };
  const needsHandle = !(id.p === 0xffffffffffffffffn && id.v === 0xffffffffffffffffn);
  const open = needsHandle ? lookupOpen(req, id.p, id.v) : null;
  if (needsHandle && !open) return { status: S.STATUS_FILE_CLOSED, body: null };
  if (open) { req.chain.fileId = id; req.chain.prevHadFileId = true; }
  const share = open ? open.share : req.tree.share;

  if (open && open.isPipe) {
    switch (ctlCode) {
      case FSCTL.PIPE_TRANSCEIVE:
        return pipeTransceive(req, open, id, input, maxOut);
      case FSCTL.PIPE_WAIT:
        return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, Buffer.alloc(0)) };
      case FSCTL.PIPE_PEEK: {
        // FILE_PIPE_PEEK_BUFFER ([MS-FSCC] 2.3.39)
        const pk = open.pipe.peek(maxOut > 16 ? maxOut - 16 : 0);
        const out = Buffer.alloc(16 + pk.data.length);
        out.writeUInt32LE(1, 0);                       // NamedPipeState: connected
        out.writeUInt32LE(pk.readDataAvailable, 4);
        out.writeUInt32LE(0, 8);
        out.writeUInt32LE(pk.messageLength, 12);
        pk.data.copy(out, 16);
        return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, out) };
      }
      default:
        return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
    }
  }

  switch (ctlCode) {
    case FSCTL.SRV_ENUMERATE_SNAPSHOTS: {
      // SRV_SNAPSHOT_ARRAY: NumberOfSnapShots, NumberOfSnapShotsReturned, SnapShotArraySize
      const out = Buffer.alloc(16);
      out.writeUInt32LE(0, 0);
      out.writeUInt32LE(0, 4);
      out.writeUInt32LE(0, 8);
      if (maxOut < 16) return { status: S.STATUS_BUFFER_TOO_SMALL, body: null };
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, out) };
    }
    case FSCTL.CREATE_OR_GET_OBJECT_ID:
    case FSCTL.GET_OBJECT_ID: {
      if (!open) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      const stt = share.stat(open.rel);
      const out = Buffer.alloc(64);
      if (stt) { out.writeBigUInt64LE(INFO.fileIdOf(stt), 0); out.writeBigUInt64LE(BigInt(stt.dev) & 0xffffffffffffffffn, 8); }
      if (maxOut < 64) return { status: S.STATUS_BUFFER_TOO_SMALL, body: null };
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, out) };
    }
    case FSCTL.SET_SPARSE: {
      if (!open) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (open.isDir) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      const on = input.length === 0 ? true : input.readUInt8(0) !== 0;
      const meta = share.metaFor(open.rel);
      meta.attrs = on ? (meta.attrs | ATTR.SPARSE_FILE) : (meta.attrs & ~ATTR.SPARSE_FILE);
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, Buffer.alloc(0)) };
    }
    case FSCTL.SET_ZERO_DATA: {
      if (!open || open.isDir) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (!(open.access & A.FILE_WRITE_DATA)) return { status: S.STATUS_ACCESS_DENIED, body: null };
      if (input.length < 16) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      const from = input.readBigUInt64LE(0), to = input.readBigUInt64LE(8);
      if (to > from) {
        const st2 = share.stat(open.rel);
        const eof = st2 ? BigInt(st2.size) : 0n;
        const end = to > eof ? eof : to;
        if (end > from) {
          const zero = Buffer.alloc(Number(end - from));
          try { fs.writeSync(open.fd, zero, 0, zero.length, Number(from)); }
          catch (e) { return { status: OPS.mapErrno(e), body: null }; }
        }
      }
      markWriteTime(open);
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, Buffer.alloc(0)) };
    }
    case FSCTL.QUERY_ALLOCATED_RANGES: {
      if (!open || open.isDir) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (input.length < 16) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      const from = input.readBigUInt64LE(0), len = input.readBigUInt64LE(8);
      const stt = share.stat(open.rel);
      const eof = stt ? BigInt(stt.size) : 0n;
      let out = Buffer.alloc(0);
      if (eof > from) {
        const end = (from + len) > eof ? eof : (from + len);
        if (end > from) {
          out = Buffer.alloc(16);
          out.writeBigUInt64LE(from, 0);
          out.writeBigUInt64LE(end - from, 8);
        }
      }
      if (out.length > maxOut) return { status: S.STATUS_BUFFER_TOO_SMALL, body: null };
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, out) };
    }
    case FSCTL.GET_COMPRESSION: {
      if (!open) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (maxOut < 2) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      const out = Buffer.alloc(2);
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, out) };
    }
    case FSCTL.SET_COMPRESSION: {
      if (!open) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (input.length < 2) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (input.readUInt16LE(0) !== 0) return { status: S.STATUS_NOT_SUPPORTED, body: null };
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, Buffer.alloc(0)) };
    }
    case FSCTL.SRV_REQUEST_RESUME_KEY: {
      if (!open) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (maxOut < 32) return { status: S.STATUS_BUFFER_TOO_SMALL, body: null };
      const out = Buffer.alloc(32);
      out.writeBigUInt64LE(open.persistentId, 0);
      out.writeBigUInt64LE(open.fileId, 8);
      out.writeBigUInt64LE(0xa5a5a5a5a5a5a5a5n, 16);
      out.writeUInt32LE(0, 24);
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, out) };
    }
    case FSCTL.SRV_COPYCHUNK:
    case FSCTL.SRV_COPYCHUNK_WRITE: {
      if (!open) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      if (input.length < 32) return { status: S.STATUS_INVALID_PARAMETER, body: null };
      const srcP = input.readBigUInt64LE(0), srcV = input.readBigUInt64LE(8);
      const chunkCount = input.readUInt32LE(24);
      const src = lookupOpen(req, srcP, srcV);
      const outErr = Buffer.alloc(12);
      outErr.writeUInt32LE(16, 0); outErr.writeUInt32LE(1048576, 4); outErr.writeUInt32LE(16777216, 8);
      // A resume key that names no open handle is not a recoverable error:
      // Samba drops the connection rather than answering.
      if (!src) { req.conn.destroy(); return null; }
      if (chunkCount > 16 || 32 + chunkCount * 24 > input.length) {
        return { status: S.STATUS_INVALID_PARAMETER, body: ioctlResponse(req, ctlCode, id, outErr) };
      }
      if (!(src.access & (A.FILE_READ_DATA | A.FILE_EXECUTE))) return { status: S.STATUS_ACCESS_DENIED, body: null };
      if (!(open.access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA))) return { status: S.STATUS_ACCESS_DENIED, body: null };
      let written = 0;
      for (let i = 0; i < chunkCount; i++) {
        const o = 32 + i * 24;
        const sOff = input.readBigUInt64LE(o);
        const tOff = input.readBigUInt64LE(o + 8);
        const len = input.readUInt32LE(o + 16);
        if (len > 1048576) return { status: S.STATUS_INVALID_PARAMETER, body: ioctlResponse(req, ctlCode, id, outErr) };
        const tmp = Buffer.alloc(len);
        let n = 0;
        try { n = len ? fs.readSync(src.fd, tmp, 0, len, Number(sOff)) : 0; }
        catch (e) { return { status: OPS.mapErrno(e), body: null }; }
        if (n < len) return { status: S.STATUS_INVALID_VIEW_SIZE || S.STATUS_INVALID_PARAMETER, body: null };
        try { if (n) fs.writeSync(open.fd, tmp, 0, n, Number(tOff)); }
        catch (e) { return { status: OPS.mapErrno(e), body: null }; }
        written += n;
      }
      markWriteTime(open);
      const out = Buffer.alloc(12);
      out.writeUInt32LE(chunkCount, 0);
      out.writeUInt32LE(written, 4);
      out.writeUInt32LE(written, 8);
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, out) };
    }
    case FSCTL.IS_PATHNAME_VALID:
      return { status: S.STATUS_SUCCESS, body: ioctlResponse(req, ctlCode, id, Buffer.alloc(0)) };
    case FSCTL.GET_REPARSE_POINT:
      return { status: S.STATUS_NOT_A_REPARSE_POINT, body: null };
    case FSCTL.SET_REPARSE_POINT:
    case FSCTL.DELETE_REPARSE_POINT:
      return { status: S.STATUS_NOT_SUPPORTED, body: null };
    case FSCTL.LMR_REQUEST_RESILIENCY:
      return { status: S.STATUS_NOT_SUPPORTED, body: null };
    case FSCTL.SRV_READ_HASH:
      return { status: S.STATUS_NOT_SUPPORTED, body: null };
    case FSCTL.FILE_LEVEL_TRIM:
      return { status: S.STATUS_NOT_SUPPORTED, body: null };
    case FSCTL.GET_INTEGRITY_INFORMATION:
    case FSCTL.SET_INTEGRITY_INFORMATION:
    case FSCTL.DUPLICATE_EXTENTS_TO_FILE:
    case FSCTL.OFFLOAD_READ:
    case FSCTL.OFFLOAD_WRITE:
    case FSCTL.RECALL_FILE:
      return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
    default:
      return { status: S.STATUS_INVALID_DEVICE_REQUEST, body: null };
  }
}

// ---------------------------------------------------------------------------
// OPLOCK_BREAK acknowledgement ([MS-SMB2] 2.2.24 / 2.2.25, 3.3.5.22)
// ---------------------------------------------------------------------------

function findLease(server, keyHex) {
  for (const share of server.shares.values()) {
    for (const node of share.nodes.values()) {
      const l = node.leases.get(keyHex);
      if (l) return { lease: l, node };
    }
  }
  return null;
}

async function doOplockBreak(req) {
  const b = req.body;
  if (b.length < 2) return { status: S.STATUS_INVALID_PARAMETER, body: null };
  const size = b.readUInt16LE(0);

  if (size === 24) {
    if (b.length < 24) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    const level = b.readUInt8(2);
    const open = openFromRequest(req, b, 8);
    if (level !== OPLOCK.NONE && level !== OPLOCK.II) {
      return { status: S.STATUS_INVALID_PARAMETER, body: null };
    }
    if (open.oplockBreaking === undefined && open.oplockLevel === OPLOCK.NONE) {
      return { status: S.STATUS_INVALID_OPLOCK_PROTOCOL, body: null };
    }
    open.oplockLevel = level;
    open.oplockBreaking = undefined;
    if (level === OPLOCK.NONE && open.node.oplockII) open.node.oplockII.delete(open);
    fireWaiters(open, 'oplockAck');
    const body = Buffer.alloc(24);
    body.writeUInt16LE(24, 0);
    body.writeUInt8(level, 2);
    body.writeUInt8(0, 3);
    body.writeUInt32LE(0, 4);
    body.writeBigUInt64LE(open.persistentId, 8);
    body.writeBigUInt64LE(open.fileId, 16);
    return { status: S.STATUS_SUCCESS, body };
  }

  if (size === 36) {
    if (b.length < 36) return { status: S.STATUS_INVALID_PARAMETER, body: null };
    const key = readGuid(b, 8);
    const state = b.readUInt32LE(24);
    const keyHex = guidStr(key);
    const found = findLease(req.server, keyHex);
    if (req.server.debugLevel > 1) {
      req.conn.log('smb2: lease-ack ' + keyHex.slice(0, 8) + ' state=' + state +
                   ' found=' + (!!found) + ' breaking=' + (found ? found.lease.breaking : '-') +
                   ' to=' + (found ? found.lease.breakingTo : '-'));
    }
    if (!found) return { status: S.STATUS_OBJECT_NAME_NOT_FOUND, body: null };
    const lease = found.lease;
    if (!lease.breaking) return { status: S.STATUS_UNSUCCESSFUL, body: null };
    const target = lease.breakingTo === undefined ? 0 : lease.breakingTo;
    if ((state & ~target) !== 0) return { status: S.STATUS_REQUEST_NOT_ACCEPTED, body: null };
    setLeaseState(lease, state >>> 0);
    lease.breaking = false;
    if (lease.breakTimer) { clearTimeout(lease.breakTimer); lease.breakTimer = null; }
    if (lease.waiters) { const w = lease.waiters.slice(); lease.waiters = []; for (const f of w) { try { f(); } catch (e) {} } }
    const body = Buffer.alloc(36);
    body.writeUInt16LE(36, 0);
    body.writeUInt16LE(0, 2);
    body.writeUInt32LE(0, 4);
    Buffer.from(key).copy(body, 8);
    body.writeUInt32LE(lease.state, 24);
    body.writeBigUInt64LE(0n, 28);
    return { status: S.STATUS_SUCCESS, body };
  }
  return { status: S.STATUS_INVALID_PARAMETER, body: null };
}

// ---------------------------------------------------------------------------

const HANDLERS = {
  [CMD.NEGOTIATE]: doNegotiate,
  [CMD.SESSION_SETUP]: doSessionSetup,
  [CMD.LOGOFF]: doLogoff,
  [CMD.TREE_CONNECT]: doTreeConnect,
  [CMD.TREE_DISCONNECT]: doTreeDisconnect,
  [CMD.CREATE]: doCreate,
  [CMD.CLOSE]: doClose,
  [CMD.FLUSH]: doFlush,
  [CMD.READ]: doRead,
  [CMD.WRITE]: doWrite,
  [CMD.LOCK]: doLock,
  [CMD.IOCTL]: doIoctl,
  [CMD.CANCEL]: doCancel,
  [CMD.ECHO]: doEcho,
  [CMD.QUERY_DIRECTORY]: doQueryDirectory,
  [CMD.CHANGE_NOTIFY]: doChangeNotify,
  [CMD.QUERY_INFO]: doQueryInfo,
  [CMD.SET_INFO]: doSetInfo,
  [CMD.OPLOCK_BREAK]: doOplockBreak,
};

module.exports = {
  handle, negotiateFromSmb1, signMessage, verifySignature, DIALECTS, CMD, FLAG, CTX, FSCTL,
  buildHeader, parseHeader, MAX_SUPPORTED, DIALECT_WILDCARD,
};
