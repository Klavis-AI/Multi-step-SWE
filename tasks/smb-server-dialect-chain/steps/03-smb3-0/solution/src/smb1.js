'use strict';
// SMB1/CIFS front end: [MS-CIFS] and [MS-SMB], NT LM 0.12 dialect.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const S = require('./ntstatus');
const { A, SHARE, DISP, OPT, ACTION, ATTR, OPLOCK } = require('./consts');
const V = require('./vfs');
const F = require('./fileops');
const I = require('./info');
const ntlm = require('./ntlm');
const SD = require('./sd');
const { fail, SmbError } = V;

const CMD = {
  CREATE_DIRECTORY: 0x00, DELETE_DIRECTORY: 0x01, OPEN: 0x02, CREATE: 0x03, CLOSE: 0x04,
  FLUSH: 0x05, DELETE: 0x06, RENAME: 0x07, QUERY_INFORMATION: 0x08, SET_INFORMATION: 0x09,
  READ: 0x0a, WRITE: 0x0b, LOCK_BYTE_RANGE: 0x0c, UNLOCK_BYTE_RANGE: 0x0d, CREATE_TEMPORARY: 0x0e,
  CREATE_NEW: 0x0f, CHECK_DIRECTORY: 0x10, PROCESS_EXIT: 0x11, SEEK: 0x12,
  LOCK_AND_READ: 0x13, WRITE_AND_UNLOCK: 0x14, READ_RAW: 0x1a, WRITE_RAW: 0x1d,
  SET_INFORMATION2: 0x22, QUERY_INFORMATION2: 0x23, LOCKING_ANDX: 0x24, TRANSACTION: 0x25,
  TRANSACTION_SECONDARY: 0x26, IOCTL: 0x27, COPY: 0x29, MOVE: 0x2a, ECHO: 0x2b,
  WRITE_AND_CLOSE: 0x2c, OPEN_ANDX: 0x2d, READ_ANDX: 0x2e, WRITE_ANDX: 0x2f,
  NEW_FILE_SIZE: 0x30, CLOSE_AND_TREE_DISC: 0x31, TRANSACTION2: 0x32,
  TRANSACTION2_SECONDARY: 0x33, FIND_CLOSE2: 0x34, FIND_NOTIFY_CLOSE: 0x35,
  TREE_CONNECT: 0x70, TREE_DISCONNECT: 0x71, NEGOTIATE: 0x72, SESSION_SETUP_ANDX: 0x73,
  LOGOFF_ANDX: 0x74, TREE_CONNECT_ANDX: 0x75, QUERY_INFORMATION_DISK: 0x80, SEARCH: 0x81,
  FIND: 0x82, FIND_UNIQUE: 0x83, FIND_CLOSE: 0x84, NT_TRANSACT: 0xa0,
  NT_TRANSACT_SECONDARY: 0xa1, NT_CREATE_ANDX: 0xa2, NT_CANCEL: 0xa4, NT_RENAME: 0xa5,
  OPEN_PRINT_FILE: 0xc0, NO_ANDX: 0xff,
};
const FLAGS = { REPLY: 0x80, OPBATCH: 0x40, OPLOCK: 0x20, CANONICAL: 0x10, CASELESS: 0x08 };
const FLAGS2 = {
  LONG_NAMES: 0x0001, EAS: 0x0002, SIGNATURE: 0x0004, SIGNATURE_REQUIRED: 0x0010,
  IS_LONG_NAME: 0x0040, EXTENDED_SECURITY: 0x0800, DFS: 0x1000, PAGING_IO: 0x2000,
  NT_STATUS: 0x4000, UNICODE: 0x8000,
};
const CAP = {
  RAW_MODE: 0x0001, MPX_MODE: 0x0002, UNICODE: 0x0004, LARGE_FILES: 0x0008, NT_SMBS: 0x0010,
  RPC_REMOTE_APIS: 0x0020, STATUS32: 0x0040, LEVEL_II_OPLOCKS: 0x0080, LOCK_AND_READ: 0x0100,
  NT_FIND: 0x0200, DFS: 0x1000, INFOLEVEL_PASSTHRU: 0x2000, LARGE_READX: 0x4000,
  LARGE_WRITEX: 0x8000, EXTENDED_SECURITY: 0x80000000,
};
const T2 = {
  OPEN2: 0x00, FIND_FIRST2: 0x01, FIND_NEXT2: 0x02, QUERY_FS_INFORMATION: 0x03,
  SET_FS_INFORMATION: 0x04, QUERY_PATH_INFORMATION: 0x05, SET_PATH_INFORMATION: 0x06,
  QUERY_FILE_INFORMATION: 0x07, SET_FILE_INFORMATION: 0x08, FSCTL: 0x09, IOCTL2: 0x0a,
  FIND_NOTIFY_FIRST: 0x0b, FIND_NOTIFY_NEXT: 0x0c, CREATE_DIRECTORY: 0x0d,
  SESSION_SETUP: 0x0e, GET_DFS_REFERRAL: 0x10, REPORT_DFS_INCONSISTENCY: 0x11,
};
const NT = {
  CREATE: 0x0001, IOCTL: 0x0002, SET_SECURITY_DESC: 0x0003, NOTIFY_CHANGE: 0x0004,
  RENAME: 0x0005, QUERY_SECURITY_DESC: 0x0006,
};

const MAX_BUFFER = 65535;
const uni = (s) => Buffer.from(s, 'utf16le');

// --- per-connection SMB1 state ---------------------------------------------

function st(conn) {
  if (!conn.s1) {
    conn.s1 = {
      uidNext: 100, tidNext: 1, fidNext: 1, sidNext: 1,
      sessions: new Map(), trees: new Map(), opens: new Map(), searches: new Map(),
      challenge: null, pendingAuth: new Map(), mechListDer: null,
    };
  }
  return conn.s1;
}

// --- header ----------------------------------------------------------------

function parseHeader(msg) {
  return {
    command: msg[4],
    status: msg.readUInt32LE(5),
    flags: msg[9],
    flags2: msg.readUInt16LE(10),
    pidHigh: msg.readUInt16LE(12),
    tid: msg.readUInt16LE(24),
    pidLow: msg.readUInt16LE(26),
    uid: msg.readUInt16LE(28),
    mid: msg.readUInt16LE(30),
  };
}

class Reply {
  constructor(h, command) {
    this.h = h;
    this.command = command === undefined ? h.command : command;
    this.status = 0;
    this.words = Buffer.alloc(0);
    this.bytes = Buffer.alloc(0);
    this.andx = null;
    this.flags2 = h.flags2;
  }
}

function buildHeader(h, command, status, flags2) {
  const b = Buffer.alloc(32);
  b[0] = 0xff; b[1] = 0x53; b[2] = 0x4d; b[3] = 0x42;
  b[4] = command;
  b.writeUInt32LE(status >>> 0, 5);
  b[9] = FLAGS.REPLY | (h.flags & (FLAGS.CASELESS | FLAGS.CANONICAL));
  b.writeUInt16LE(flags2 === undefined ? h.flags2 : flags2, 10);
  b.writeUInt16LE(h.pidHigh, 12);
  b.writeUInt16LE(h.tid, 24);
  b.writeUInt16LE(h.pidLow, 26);
  b.writeUInt16LE(h.uid, 28);
  b.writeUInt16LE(h.mid, 30);
  return b;
}

function assemble(h, parts) {
  // parts: [{command, status, words, bytes, isAndx}] -- first sets the header.
  const first = parts[0];
  const head = buildHeader(h, first.command, first.status, first.flags2);
  const chunks = [head];
  let offset = 32;
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i];
    const next = parts[i + 1];
    const words = Buffer.from(p.words);
    if (p.isAndx && words.length >= 4) {
      words[0] = next ? next.command : 0xff;
      words[1] = 0;
      // AndXOffset patched after we know the size
    }
    const body = Buffer.alloc(1 + words.length + 2 + p.bytes.length);
    body[0] = words.length / 2;
    words.copy(body, 1);
    body.writeUInt16LE(p.bytes.length, 1 + words.length);
    p.bytes.copy(body, 3 + words.length);
    p._offset = offset;
    p._body = body;
    chunks.push(body);
    offset += body.length;
  }
  // patch AndXOffset now that offsets are known
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i], next = parts[i + 1];
    if (p.isAndx && p._body.length >= 5) {
      p._body.writeUInt16LE(next ? next._offset : 0, 3);
    }
  }
  return Buffer.concat(chunks);
}

const DOS_ERRORS = new Set([
  S.STATUS_SMB_BAD_UID, S.STATUS_SMB_BAD_TID, S.STATUS_SMB_BAD_COMMAND,
  S.STATUS_SMB_USE_STANDARD, S.STATUS_INVALID_SMB, S.STATUS_DOS_INVALID_PARAMETER,
]);
function errorReply(conn, h, status, command) {
  const dos = DOS_ERRORS.has(status >>> 0);
  const flags2 = dos ? (h.flags2 & ~FLAGS2.NT_STATUS) : (h.flags2 | FLAGS2.NT_STATUS);
  const head = buildHeader(h, command === undefined ? h.command : command, status, flags2);
  return Buffer.concat([head, Buffer.from([0, 0, 0])]);
}

// --- string helpers ---------------------------------------------------------

function readSmbString(buf, off, unicode, base) {
  if (unicode) {
    if (((base + off) & 1) !== 0) off += 1;
    let end = off;
    while (end + 1 < buf.length && !(buf[end] === 0 && buf[end + 1] === 0)) end += 2;
    return { value: buf.subarray(off, end).toString('utf16le'), next: end + 2 };
  }
  let end = off;
  while (end < buf.length && buf[end] !== 0) end++;
  return { value: buf.subarray(off, end).toString('latin1'), next: end + 1 };
}
function writeSmbString(s, unicode, padTo) {
  const body = unicode ? Buffer.concat([uni(s), Buffer.from([0, 0])]) : Buffer.from(s + '\0', 'latin1');
  if (unicode && padTo) return Buffer.concat([Buffer.alloc(1), body]);
  return body;
}

// --- main dispatch ----------------------------------------------------------

function handle(conn, msg) {
  if (msg.length < 33) { conn.destroy(); return; }
  const h = parseHeader(msg);
  const s = st(conn);

  // An SMB1 NEGOTIATE is the only command accepted before the dialect is fixed.
  if (conn.proto === 'smb2') { conn.destroy(); return; }
  if (conn.proto === null && h.command !== CMD.NEGOTIATE) { conn.destroy(); return; }

  let parts;
  try {
    parts = dispatchChain(conn, h, msg, 32);
  } catch (e) {
    if (e instanceof SmbError) {
      if (process.env.SMBD_DEBUG_CREATE === '1') {
        try { fs.appendFileSync('/tmp/create.log', '  -> cmd=0x' + h.command.toString(16) +
          ' ' + S.name(e.status) + ' @' + (e.stack || '').split('\n')[1] + '\n'); } catch (x) {}
      }
      conn.send(errorReply(conn, h, e.status)); return;
    }
    conn.log('smb1 error', e && e.stack);
    conn.send(errorReply(conn, h, S.STATUS_UNSUCCESSFUL));
    return;
  }
  if (parts === null) return;      // handled asynchronously / no reply
  if (!Array.isArray(parts)) parts = [parts];
  conn.send(assemble(h, parts));
}

function dispatchChain(conn, h, msg, off) {
  const out = [];
  let command = h.command;
  let guard = 0;
  while (off < msg.length && guard++ < 8) {
    const wordCount = msg[off];
    const words = msg.subarray(off + 1, off + 1 + wordCount * 2);
    const byteCount = msg.readUInt16LE(off + 1 + wordCount * 2);
    const bytes = msg.subarray(off + 3 + wordCount * 2, off + 3 + wordCount * 2 + byteCount);
    const bytesBase = off + 3 + wordCount * 2;
    const part = dispatchOne(conn, h, command, words, bytes, msg, off, bytesBase);
    if (part === null) return null;
    out.push(part);
    if (!part.isAndx || words.length < 4) break;
    const nextCmd = words[0];
    const nextOff = words.readUInt16LE(2);
    if (nextCmd === 0xff || nextOff === 0 || nextOff >= msg.length) break;
    command = nextCmd;
    off = nextOff;
  }
  return out;
}

function reply(command, words, bytes, status, isAndx, flags2) {
  return {
    command, status: status || 0,
    words: words || Buffer.alloc(0), bytes: bytes || Buffer.alloc(0),
    isAndx: !!isAndx, flags2,
  };
}

function dispatchOne(conn, h, command, words, bytes, msg, off, bytesBase) {
  const s = st(conn);
  switch (command) {
    case CMD.NEGOTIATE: return doNegotiate(conn, h, bytes);
    case CMD.SESSION_SETUP_ANDX: return doSessionSetup(conn, h, words, bytes, bytesBase);
    case CMD.LOGOFF_ANDX: return doLogoff(conn, h, words);
    case CMD.TREE_CONNECT_ANDX: return doTreeConnectAndx(conn, h, words, bytes, bytesBase);
    case CMD.TREE_DISCONNECT: return doTreeDisconnect(conn, h);
    case CMD.ECHO: return doEcho(conn, h, words, bytes);
    case CMD.NT_CREATE_ANDX: {
      const t0 = tree(conn, h);
      if (!t0.share) fail(S.STATUS_INVALID_DEVICE_REQUEST);
      const unicode0 = (h.flags2 & FLAGS2.UNICODE) !== 0;
      const nameLength0 = words.readUInt16LE(5);
      let nb0 = (unicode0 && (bytesBase & 1) !== 0) ? 1 : 0;
      const name0 = (unicode0 ? bytes.subarray(nb0, nb0 + nameLength0).toString('utf16le')
                              : bytes.subarray(nb0, nb0 + nameLength0).toString('latin1'))
                    .replace(/\0+$/, '');
      return withOplockBreak(conn, h, t0.share, name0,
        () => doNtCreateAndx(conn, h, words, bytes, msg, bytesBase));
    }
    case CMD.OPEN_ANDX: return doOpenAndx(conn, h, words, bytes, msg, bytesBase);
    case CMD.CLOSE: return doClose(conn, h, words);
    case CMD.FLUSH: return doFlush(conn, h, words);
    case CMD.READ_ANDX: return doReadAndx(conn, h, words, bytes);
    case CMD.WRITE_ANDX: return doWriteAndx(conn, h, words, bytes, msg, off);
    case CMD.WRITE_AND_CLOSE: return doWriteAndClose(conn, h, words, bytes, msg, off);
    case CMD.READ: return doReadCore(conn, h, words);
    case CMD.WRITE: return doWriteCore(conn, h, words, bytes);
    case CMD.DELETE: return doDelete(conn, h, words, bytes, bytesBase);
    case CMD.DELETE_DIRECTORY: return doDeleteDirectory(conn, h, bytes, bytesBase);
    case CMD.CREATE_DIRECTORY: return doCreateDirectory(conn, h, bytes, bytesBase);
    case CMD.CHECK_DIRECTORY: return doCheckDirectory(conn, h, bytes, bytesBase);
    case CMD.RENAME: return doRename(conn, h, words, bytes, bytesBase);
    case CMD.NT_RENAME: return doNtRename(conn, h, words, bytes, bytesBase);
    case CMD.QUERY_INFORMATION: return doQueryInformation(conn, h, bytes, bytesBase);
    case CMD.SET_INFORMATION: return doSetInformation(conn, h, words, bytes, bytesBase);
    case CMD.QUERY_INFORMATION2: return doQueryInformation2(conn, h, words);
    case CMD.SET_INFORMATION2: return doSetInformation2(conn, h, words);
    case CMD.QUERY_INFORMATION_DISK: return doQueryInformationDisk(conn, h);
    case CMD.SEEK: return doSeek(conn, h, words);
    case CMD.LOCKING_ANDX: return doLockingAndx(conn, h, words, bytes);
    case CMD.LOCK_BYTE_RANGE: return doLockByteRange(conn, h, words, true);
    case CMD.UNLOCK_BYTE_RANGE: return doLockByteRange(conn, h, words, false);
    case CMD.TRANSACTION2: return doTrans2(conn, h, words, bytes, msg, bytesBase);
    case CMD.NT_TRANSACT: return doNtTrans(conn, h, words, bytes, msg, off);
    case CMD.TRANSACTION: return doTrans(conn, h, words, bytes, msg, off);
    case CMD.FIND_CLOSE2: return doFindClose2(conn, h, words);
    case CMD.NT_CANCEL: return null;
    case CMD.PROCESS_EXIT: return doProcessExit(conn, h);
    case CMD.TREE_CONNECT: fail(S.STATUS_SMB_USE_STANDARD); break;
    default:
      // [MS-CIFS] 2.2.4: an unimplemented command is ERRSRV/ERRbadcmd.
      fail(S.STATUS_SMB_BAD_COMMAND);
  }
  return null;
}

// --- authentication / session ----------------------------------------------

function session(conn, h) {
  const s = st(conn);
  const sess = s.sessions.get(h.uid);
  if (!sess || !sess.authenticated) fail(S.STATUS_SMB_BAD_UID);
  return sess;
}
function tree(conn, h) {
  const s = st(conn);
  session(conn, h);
  const t = s.trees.get(h.tid);
  if (!t || t.uid !== h.uid) fail(S.STATUS_INVALID_HANDLE);
  return t;
}
// A file handle is valid only on the tree connect and session that opened it.
function openOf(conn, h, fid) {
  const s = st(conn);
  const sess = session(conn, h);
  const t = s.trees.get(h.tid);
  if (!t || t.uid !== h.uid) fail(S.STATUS_SMB_BAD_TID);
  const o = s.opens.get(fid);
  if (!o || o.closed) fail(S.STATUS_INVALID_HANDLE);
  if (o.tree !== t) fail(S.STATUS_INVALID_HANDLE);
  if (o.session !== sess) fail(S.STATUS_INVALID_HANDLE);
  return o;
}

function doNegotiate(conn, h, bytes) {
  const server = conn.server;
  const dialects = [];
  let p = 0;
  while (p < bytes.length) {
    if (bytes[p] !== 0x02) break;
    p++;
    let end = p;
    while (end < bytes.length && bytes[end] !== 0) end++;
    dialects.push(bytes.subarray(p, end).toString('latin1'));
    p = end + 1;
  }
  conn.smb1Dialects = dialects;

  // The SMB 2.??? / SMB 2.002 entries make this a multi-protocol negotiate.
  const wants2 = dialects.some((d) => d === 'SMB 2.???' || d === 'SMB 2.002');
  if (wants2 && server.maxProtocol >= 0x0202 && conn.server.smb2.negotiateFromSmb1) {
    return conn.server.smb2.negotiateFromSmb1(conn, h, dialects);
  }

  if (dialects.length === 0) fail(S.STATUS_DOS_INVALID_PARAMETER);
  const idx = dialects.indexOf('NT LM 0.12');
  if (idx < 0 || server.minProtocol > 0x0005 || server.maxProtocol < 0x0005) {
    const w = Buffer.alloc(2);
    w.writeUInt16LE(0xffff, 0);
    return reply(CMD.NEGOTIATE, w, Buffer.alloc(0), 0, false);
  }
  conn.proto = 'smb1';
  conn.dialect = 0x0005;
  const s = st(conn);
  s.challenge = crypto.randomBytes(8);

  let securityMode = 0x01; // USER_SECURITY
  if (server.signingEnabled()) securityMode |= 0x04;
  if (server.signingRequired()) securityMode |= 0x08;

  let caps = CAP.UNICODE | CAP.LARGE_FILES | CAP.NT_SMBS | CAP.RPC_REMOTE_APIS | CAP.STATUS32 |
             CAP.LEVEL_II_OPLOCKS | CAP.LOCK_AND_READ | CAP.NT_FIND | CAP.INFOLEVEL_PASSTHRU |
             CAP.LARGE_READX | CAP.LARGE_WRITEX | CAP.EXTENDED_SECURITY;

  const w = Buffer.alloc(34);
  w.writeUInt16LE(idx, 0);
  w.writeUInt8(securityMode, 2);
  w.writeUInt16LE(50, 3);           // MaxMpxCount
  w.writeUInt16LE(1, 5);            // MaxNumberVcs
  w.writeUInt32LE(MAX_BUFFER, 7);   // MaxBufferSize
  w.writeUInt32LE(65536, 11);       // MaxRawSize
  w.writeUInt32LE(0, 15);           // SessionKey
  w.writeUInt32LE(caps >>> 0, 19);  // Capabilities
  w.writeBigUInt64LE(V.toFileTime(Date.now()), 23);
  w.writeInt16LE(0, 31);            // ServerTimeZone
  w.writeUInt8(0, 33);              // ChallengeLength (extended security)

  const blob = ntlm.spnegoInit([ntlm.OID_NTLMSSP]);
  const b = Buffer.concat([conn.server.guid, blob]);
  const flags2 = (h.flags2 | FLAGS2.NT_STATUS | FLAGS2.EXTENDED_SECURITY | FLAGS2.LONG_NAMES) & 0xffff;
  return reply(CMD.NEGOTIATE, w, b, 0, false, flags2);
}

function doSessionSetup(conn, h, words, bytes, bytesBase) {
  const s = st(conn);
  const server = conn.server;
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  if (words.length < 24) fail(S.STATUS_INVALID_PARAMETER);
  const extended = (h.flags2 & FLAGS2.EXTENDED_SECURITY) !== 0 && words.length === 24;
  let blob, capabilities = 0;
  if (extended) {
    const blobLen = words.readUInt16LE(14);
    capabilities = words.readUInt32LE(20);
    blob = bytes.subarray(0, blobLen);
  } else {
    fail(S.STATUS_LOGON_FAILURE);
  }

  const uid = h.uid || s.uidNext++;
  let sess = s.sessions.get(uid);
  if (!sess) { sess = { uid, user: null, sessionKey: null, authenticated: false, ntlmChallenge: null, mechListDer: null }; s.sessions.set(uid, sess); }

  const parsed = ntlm.unwrap(blob);
  let token = parsed.token;
  if (parsed.mechListDer) sess.mechListDer = parsed.mechListDer;
  if (!token) fail(S.STATUS_LOGON_FAILURE);

  const isNegotiate = token.length >= 12 && token.subarray(0, 8).equals(ntlm.SIG) && token.readUInt32LE(8) === 1;
  const isAuth = token.length >= 12 && token.subarray(0, 8).equals(ntlm.SIG) && token.readUInt32LE(8) === 3;

  if (isNegotiate) {
    const clientFlags = token.length >= 16 ? token.readUInt32LE(12) : 0;
    sess.ntlmChallenge = crypto.randomBytes(8);
    let negFlags = ntlm.F.NEGOTIATE_UNICODE | ntlm.F.REQUEST_TARGET | ntlm.F.NEGOTIATE_NTLM |
      ntlm.F.NEGOTIATE_ALWAYS_SIGN | ntlm.F.TARGET_TYPE_SERVER |
      ntlm.F.NEGOTIATE_EXTENDED_SESSIONSECURITY | ntlm.F.NEGOTIATE_TARGET_INFO |
      ntlm.F.NEGOTIATE_VERSION | ntlm.F.NEGOTIATE_128 | ntlm.F.NEGOTIATE_56;
    if (clientFlags & ntlm.F.NEGOTIATE_KEY_EXCH) negFlags |= ntlm.F.NEGOTIATE_KEY_EXCH;
    if (clientFlags & ntlm.F.NEGOTIATE_SIGN) negFlags |= ntlm.F.NEGOTIATE_SIGN;
    if (clientFlags & ntlm.F.NEGOTIATE_SEAL) negFlags |= ntlm.F.NEGOTIATE_SEAL;
    const challenge = ntlm.buildChallenge(server.netbiosName, server.netbiosName.toLowerCase(), sess.ntlmChallenge, negFlags >>> 0);
    const out = (parsed.kind === 'raw') ? challenge
      : ntlm.spnegoResp(1, ntlm.OID_NTLMSSP, challenge, null);
    return sessionSetupReply(conn, h, uid, out, S.STATUS_MORE_PROCESSING_REQUIRED, unicode, 0);
  }
  if (isAuth) {
    const auth = ntlm.parseAuthenticate(token);
    if (!auth) fail(S.STATUS_LOGON_FAILURE);
    const user = server.users.get(String(auth.user).toLowerCase());
    if (!user || user.disabled || !user.nt) fail(S.STATUS_LOGON_FAILURE);
    const r = ntlm.verify(auth, sess.ntlmChallenge, user.nt, server.netbiosName);
    if (!r.ok) fail(S.STATUS_LOGON_FAILURE);
    sess.user = user;
    sess.sessionKey = r.sessionKey;
    sess.authenticated = true;
    const out = (parsed.kind === 'raw') ? Buffer.alloc(0) : ntlm.spnegoResp(0, null, null, null);
    return sessionSetupReply(conn, h, uid, out, 0, unicode, 0);
  }
  fail(S.STATUS_LOGON_FAILURE);
}

function sessionSetupReply(conn, h, uid, blob, status, unicode, action) {
  const w = Buffer.alloc(8);
  w[0] = 0xff; w[1] = 0;
  w.writeUInt16LE(0, 2);
  w.writeUInt16LE(action, 4);
  w.writeUInt16LE(blob.length, 6);
  const strs = Buffer.concat([
    writeSmbString('Unix', unicode, true),
    writeSmbString('Samba 4.17.12', unicode, false),
    writeSmbString(conn.server.workgroup, unicode, false),
  ]);
  const b = Buffer.concat([blob, strs]);
  h.uid = uid;
  const r = reply(CMD.SESSION_SETUP_ANDX, w, b, status, true, h.flags2 | FLAGS2.NT_STATUS);
  return r;
}

function doLogoff(conn, h, words) {
  const s = st(conn);
  const sess = s.sessions.get(h.uid);
  if (sess) {
    for (const [tid, t] of Array.from(s.trees)) if (t.uid === h.uid) s.trees.delete(tid);
    for (const [fid, o] of Array.from(s.opens)) if (o.session === sess) { F.closeOpen(o); s.opens.delete(fid); }
    s.sessions.delete(h.uid);
  }
  const w = Buffer.alloc(4);
  w[0] = 0xff;
  return reply(CMD.LOGOFF_ANDX, w, Buffer.alloc(0), 0, true);
}

function doTreeConnectAndx(conn, h, words, bytes, bytesBase) {
  const s = st(conn);
  const sess = session(conn, h);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  const passLen = words.readUInt16LE(6);
  let p = passLen;
  const pathRes = readSmbString(bytes, p, unicode, bytesBase);
  const unc = pathRes.value;
  const service = readSmbString(bytes, pathRes.next, false, bytesBase).value;
  const m = /\\\\[^\\]+\\(.+)$/.exec(unc.replace(/\//g, '\\'));
  const shareName = m ? m[1] : unc;
  const isIpc = shareName.toUpperCase() === 'IPC$';
  const share = conn.server.shares.get(shareName.toLowerCase());
  if (!share && !isIpc) fail(S.STATUS_BAD_NETWORK_NAME);
  // [MS-CIFS] 2.2.4.55.1: the Service string must name this share's type, or
  // be the wildcard '?????'.
  const wantService = isIpc ? 'IPC' : 'A:';
  if (service && service !== '?????' && service.toUpperCase() !== wantService) {
    fail(S.STATUS_BAD_DEVICE_TYPE);
  }
  const tid = s.tidNext++;
  s.trees.set(tid, { tid, uid: h.uid, share: share || null, name: shareName, isIpc, opens: new Map() });
  h.tid = tid;
  const w = Buffer.alloc(14);
  w[0] = 0xff; w[1] = 0;
  w.writeUInt16LE(0, 2);
  w.writeUInt16LE(0x0001, 4);   // OptionalSupport: SMB_SUPPORT_SEARCH_BITS
  w.writeUInt32LE(isIpc ? 0x001f00a9 : (share.readOnly ? 0x001200a9 : 0x001f01ff), 6);
  w.writeUInt32LE(0, 10);
  const b = Buffer.concat([
    Buffer.from((isIpc ? 'IPC' : 'A:') + '\0', 'latin1'),
    writeSmbString(isIpc ? '' : 'NTFS', unicode, true),
  ]);
  return reply(CMD.TREE_CONNECT_ANDX, w, b, 0, true, h.flags2 | FLAGS2.NT_STATUS);
}

function doTreeDisconnect(conn, h) {
  const s = st(conn);
  const t = s.trees.get(h.tid);
  if (t) {
    for (const [fid, o] of Array.from(s.opens)) if (o.tree === t) { F.closeOpen(o); s.opens.delete(fid); }
    s.trees.delete(h.tid);
  }
  return reply(CMD.TREE_DISCONNECT, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doEcho(conn, h, words, bytes) {
  const count = words.length >= 2 ? words.readUInt16LE(0) : 1;
  for (let i = 1; i < count; i++) {
    const w = Buffer.alloc(2); w.writeUInt16LE(i, 0);
    conn.send(assemble(h, [reply(CMD.ECHO, w, bytes, 0, false)]));
  }
  const w = Buffer.alloc(2); w.writeUInt16LE(count, 0);
  return reply(CMD.ECHO, w, bytes, 0, false);
}

// --- opens ------------------------------------------------------------------

// [MS-CIFS] 3.3.5.51: an exclusive or batch oplock is granted only to a sole
// opener; any later opener gets level II, and the exclusive holder is broken
// down to level II first.
function grantOplock(conn, open, wantExclusive, wantBatch) {
  if (!wantExclusive || open.isDir) return 0;
  const node = open.node;
  const others = Array.from(node.opens).filter((o) => o !== open);
  if (others.length === 0) {
    open.oplockLevel = wantBatch ? OPLOCK.BATCH : OPLOCK.EXCLUSIVE;
    node.oplock = { open, level: open.oplockLevel, conn };
    return wantBatch ? 2 : 1;
  }
  open.oplockLevel = OPLOCK.II;
  return 3;
}

// Send an unsolicited LOCKING_ANDX oplock break to the holder and wait for the
// acknowledgement (or the break timeout) before the conflicting open proceeds.
function breakOplock(share, node, newLevel, done) {
  const held = node.oplock;
  if (!held || held.level === OPLOCK.II || held.level === OPLOCK.NONE) { done(); return; }
  const conn = held.conn;
  const open = held.open;
  if (!conn || conn.closed) { node.oplock = null; done(); return; }
  const w = Buffer.alloc(16);
  w[0] = 0xff; w[1] = 0; w.writeUInt16LE(0, 2);
  w.writeUInt16LE(open.smb1Fid, 4);
  w.writeUInt8(0x02, 6);            // LOCKING_ANDX_OPLOCK_RELEASE
  w.writeUInt8(newLevel, 7);        // 1 = level II, 0 = none
  w.writeUInt32LE(0, 8);
  w.writeUInt16LE(0, 12);
  w.writeUInt16LE(0, 14);
  const hdr = { command: CMD.LOCKING_ANDX, flags: 0, flags2: FLAGS2.NT_STATUS | FLAGS2.UNICODE,
                pidHigh: 0, tid: open.tree ? open.tree.tid : 0, pidLow: 0xffff,
                uid: open.session ? open.session.uid : 0, mid: 0xffff };
  conn.send(assemble(hdr, [reply(CMD.LOCKING_ANDX, w, Buffer.alloc(0), 0, true)]));
  let finished = false;
  const finish = () => { if (finished) return; finished = true; clearTimeout(timer); done(); };
  const timer = setTimeout(finish, 3000);
  if (timer.unref) timer.unref();
  node.oplockAck = finish;
}

function registerOpen(conn, h, t, open) {
  const s = st(conn);
  const fid = s.fidNext++ & 0xffff;
  open.smb1Fid = fid;
  s.opens.set(fid, open);
  t.opens.set(fid, open);
  return fid;
}

function requireShare(conn, h) {
  const t = tree(conn, h);
  if (!t.share) fail(S.STATUS_INVALID_DEVICE_REQUEST);
  return t;
}

// Before a conflicting open is allowed to proceed, an exclusive or batch
// oplock held by another opener is broken down to level II.
function withOplockBreak(conn, h, share, name, fn) {
  let rel;
  try { rel = share.resolve(name); } catch (e) { return fn(); }
  const stt = share.stat(rel);
  if (!stt) return fn();
  const node = share.findNode(rel, stt);
  if (!node || !node.oplock || node.oplock.level === OPLOCK.II) return fn();
  breakOplock(share, node, 1, () => {
    let parts;
    try { parts = fn(); }
    catch (e) {
      if (e instanceof SmbError) { conn.send(errorReply(conn, h, e.status)); return; }
      conn.send(errorReply(conn, h, S.STATUS_UNSUCCESSFUL)); return;
    }
    if (parts) conn.send(assemble(h, Array.isArray(parts) ? parts : [parts]));
  });
  return null;
}

function doNtCreateAndx(conn, h, words, bytes, msg, bytesBase) {
  const t = requireShare(conn, h);
  const sess = session(conn, h);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  if (words.length < 46) fail(S.STATUS_INVALID_PARAMETER);
  const nameLength = words.readUInt16LE(5);
  const flags = words.readUInt32LE(7);
  const desiredAccess = words.readUInt32LE(15);
  const extFileAttributes = words.readUInt32LE(27);
  const shareAccess = words.readUInt32LE(31);
  const createDisposition = words.readUInt32LE(35);
  const createOptions = words.readUInt32LE(39);
  const nameStart = bytesBase;
  let nb = 0;
  if (unicode && ((nameStart) & 1) !== 0) nb = 1;
  const raw = bytes.subarray(nb, nb + nameLength);
  let name = unicode ? raw.toString('utf16le') : raw.toString('latin1');
  name = name.replace(/\0+$/, '');

  // [MS-CIFS] 2.2.4.64.1: NT_CREATE_REQUEST_OPLOCK is 0x02 and
  // NT_CREATE_REQUEST_OPBATCH is 0x04.
  const requestExclusive = (flags & 0x00000002) !== 0;
  const requestBatch = (flags & 0x00000004) !== 0;

  const { open, action } = F.createFile(t.share, {
    name, desiredAccess, fileAttributes: extFileAttributes, shareAccess,
    createDisposition, createOptions, session: sess, tree: t, pid: (h.pidHigh << 16) | h.pidLow,
  });
  const fid = registerOpen(conn, h, t, open);

  const oplockLevel = grantOplock(conn, open, requestExclusive, requestBatch);

  const stt = t.share.stat(open.rel);
  const times = t.share.times(open.rel, stt);
  const attrs = t.share.attributes(open.rel, stt);
  const eof = stt && !open.isDir ? BigInt(stt.size) : 0n;

  const w = Buffer.alloc(68);
  w[0] = 0xff; w[1] = 0; w.writeUInt16LE(0, 2);
  w.writeUInt8(oplockLevel, 4);
  w.writeUInt16LE(fid, 5);
  w.writeUInt32LE(action, 7);
  w.writeBigUInt64LE(times.create, 11);
  w.writeBigUInt64LE(times.access, 19);
  w.writeBigUInt64LE(times.write, 27);
  w.writeBigUInt64LE(times.change, 35);
  w.writeUInt32LE(attrs, 43);
  w.writeBigUInt64LE(open.isDir ? 0n : I.alloc(0, eof), 47);
  w.writeBigUInt64LE(eof, 55);
  w.writeUInt16LE(open.isDir ? 0 : 0, 63);   // ResourceType: FileTypeDisk
  w.writeUInt16LE(0, 65);                    // NMPipeStatus
  w.writeUInt8(open.isDir ? 1 : 0, 67);
  return reply(CMD.NT_CREATE_ANDX, w, Buffer.alloc(0), 0, true);
}

function doOpenAndx(conn, h, words, bytes, msg, bytesBase) {
  const t = requireShare(conn, h);
  const sess = session(conn, h);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  if (words.length < 30) fail(S.STATUS_INVALID_PARAMETER);
  const openFlags = words.readUInt16LE(4);
  const accessMode = words.readUInt16LE(6);
  const searchAttrs = words.readUInt16LE(8);
  const fileAttrs = words.readUInt16LE(10);
  const openFunction = words.readUInt16LE(16);
  const nameStart = bytesBase;
  let nb = (unicode && (nameStart & 1) !== 0) ? 1 : 0;
  const nres = readSmbString(bytes, nb, unicode, nameStart);
  const name = nres.value;
  const mode = accessMode & 0x7;
  const share = (accessMode >> 4) & 0x7;
  let desiredAccess = A.FILE_READ_DATA | A.FILE_READ_ATTRIBUTES | A.FILE_READ_EA | A.SYNCHRONIZE | A.READ_CONTROL;
  if (mode === 1) desiredAccess = A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.FILE_WRITE_ATTRIBUTES | A.FILE_WRITE_EA | A.SYNCHRONIZE | A.READ_CONTROL;
  else if (mode === 2) desiredAccess |= A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.FILE_WRITE_ATTRIBUTES | A.FILE_WRITE_EA;
  const shareAccess = share === 0 ? 0 : share === 1 ? SHARE.WRITE : share === 2 ? SHARE.READ : (SHARE.READ | SHARE.WRITE);
  let disp = DISP.OPEN;
  const openMode = openFunction & 0x3;
  const createFlag = (openFunction & 0x10) !== 0;
  if (openMode === 0) disp = createFlag ? DISP.CREATE : DISP.OPEN;
  else if (openMode === 1) disp = createFlag ? DISP.OPEN_IF : DISP.OPEN;
  else if (openMode === 2) disp = createFlag ? DISP.OVERWRITE_IF : DISP.OVERWRITE;
  const { open, action } = F.createFile(t.share, {
    name, desiredAccess, fileAttributes: fileAttrs, shareAccess,
    createDisposition: disp, createOptions: OPT.NON_DIRECTORY_FILE, session: sess, tree: t,
  });
  const fid = registerOpen(conn, h, t, open);
  const stt = t.share.stat(open.rel);
  const w = Buffer.alloc(30);
  w[0] = 0xff; w[1] = 0; w.writeUInt16LE(0, 2);
  w.writeUInt16LE(fid, 4);
  w.writeUInt16LE(t.share.attributes(open.rel, stt), 6);
  w.writeUInt32LE(Math.floor(stt.mtimeMs / 1000), 8);
  w.writeUInt32LE(Number(stt.size & 0xffffffff), 12);
  w.writeUInt16LE(mode === 0 ? 0 : 2, 16);
  w.writeUInt16LE(0, 18);
  w.writeUInt16LE(0, 20);
  w.writeUInt16LE(action === ACTION.CREATED ? 2 : action === ACTION.OVERWRITTEN ? 3 : 1, 22);
  w.writeUInt32LE(0, 24);
  w.writeUInt16LE(0, 28);
  return reply(CMD.OPEN_ANDX, w, Buffer.alloc(0), 0, true);
}

function doClose(conn, h, words) {
  const s = st(conn);
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  if (words.length >= 6) {
    const t = words.readUInt32LE(2);
    if (t !== 0 && t !== 0xffffffff) {
      try { fs.utimesSync(o.share.real(o.rel), t, t); } catch (e) {}
      o.sticky.write = null;
    }
  }
  F.closeOpen(o);
  s.opens.delete(fid);
  if (o.tree) o.tree.opens.delete(fid);
  return reply(CMD.CLOSE, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doFlush(conn, h, words) {
  const s = st(conn);
  const fid = words.readUInt16LE(0);
  if (fid === 0xffff) {
    for (const o of s.opens.values()) if (o.fd !== null) { try { fs.fsyncSync(o.fd); } catch (e) {} }
  } else {
    const o = openOf(conn, h, fid);
    if (o.fd !== null) { try { fs.fsyncSync(o.fd); } catch (e) {} }
  }
  return reply(CMD.FLUSH, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doReadAndx(conn, h, words) {
  const fid = words.readUInt16LE(4);
  const o = openOf(conn, h, fid);
  if (o.isDir) fail(S.STATUS_INVALID_DEVICE_REQUEST);
  if (!(o.access & (A.FILE_READ_DATA | A.FILE_EXECUTE))) fail(S.STATUS_ACCESS_DENIED);
  let offset = BigInt(words.readUInt32LE(6));
  let maxCount = words.readUInt16LE(10);
  const maxHigh = words.length >= 16 ? words.readUInt32LE(14) : 0;
  if (words.length >= 24) offset |= BigInt(words.readUInt32LE(20)) << 32n;
  if (maxHigh > 0 && maxHigh !== 0xffffffff) maxCount |= (maxHigh & 0xffff) << 16;
  if (maxCount > 0x10000) maxCount = 0x10000;
  if (F.lockConflict(o, offset, BigInt(maxCount), false)) fail(S.STATUS_FILE_LOCK_CONFLICT);
  const buf = Buffer.alloc(maxCount);
  let n = 0;
  try { n = fs.readSync(o.fd, buf, 0, maxCount, Number(offset)); } catch (e) { n = 0; }
  const w = Buffer.alloc(24);
  w[0] = 0xff; w[1] = 0; w.writeUInt16LE(0, 2);
  w.writeUInt16LE(0, 4);          // Available
  w.writeUInt16LE(0, 6);          // DataCompactionMode
  w.writeUInt16LE(0, 8);          // Reserved1
  w.writeUInt16LE(n & 0xffff, 10);
  // DataOffset is patched by the caller-independent layout below
  w.writeUInt16LE(0, 12);
  w.writeUInt16LE((n >> 16) & 0xffff, 14);
  const pad = Buffer.alloc(1);
  const dataOffset = 32 + 1 + 24 + 2 + pad.length;
  w.writeUInt16LE(dataOffset, 12);
  return reply(CMD.READ_ANDX, w, Buffer.concat([pad, buf.subarray(0, n)]), 0, true);
}

function doWriteAndx(conn, h, words, bytes, msg, off) {
  const fid = words.readUInt16LE(4);
  const o = openOf(conn, h, fid);
  if (!(o.access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA))) fail(S.STATUS_ACCESS_DENIED);
  let offset = BigInt(words.readUInt32LE(6));
  const writeMode = words.readUInt16LE(14);
  const dataLenHigh = words.readUInt16LE(18);
  const dataLen = words.readUInt16LE(20) | (dataLenHigh << 16);
  const dataOffset = words.readUInt16LE(22);
  if (words.length >= 28) offset |= BigInt(words.readUInt32LE(24)) << 32n;
  const data = msg.subarray(dataOffset, dataOffset + dataLen);
  if (F.lockConflict(o, offset, BigInt(dataLen), true)) fail(S.STATUS_FILE_LOCK_CONFLICT);
  let n = 0;
  if (dataLen > 0) {
    try { n = fs.writeSync(o.fd, data, 0, data.length, Number(offset)); }
    catch (e) { fail(F.mapErrno(e)); }
    F.touchWriteTime(o);
  }
  const w = Buffer.alloc(12);
  w[0] = 0xff; w[1] = 0; w.writeUInt16LE(0, 2);
  w.writeUInt16LE(n & 0xffff, 4);
  w.writeUInt16LE(0, 6);
  w.writeUInt16LE((n >> 16) & 0xffff, 8);
  w.writeUInt16LE(0, 10);
  return reply(CMD.WRITE_ANDX, w, Buffer.alloc(0), 0, true);
}

function doWriteAndClose(conn, h, words, bytes, msg, off) {
  const s = st(conn);
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const count = words.readUInt16LE(2);
  const offset = words.readUInt32LE(4);
  const mtime = words.readUInt32LE(8);
  const dataStart = off + 3 + words.length + 1;
  const data = msg.subarray(dataStart, dataStart + count);
  let n = 0;
  if (count > 0) { try { n = fs.writeSync(o.fd, data, 0, data.length, offset); } catch (e) { fail(F.mapErrno(e)); } }
  else { try { fs.ftruncateSync(o.fd, offset); } catch (e) {} }
  F.touchWriteTime(o);
  F.closeOpen(o);
  s.opens.delete(fid);
  if (mtime) { try { fs.utimesSync(o.share.real(o.rel), mtime, mtime); } catch (e) {} }
  const w = Buffer.alloc(2);
  w.writeUInt16LE(n, 0);
  return reply(CMD.WRITE_AND_CLOSE, w, Buffer.alloc(0), 0, false);
}

function doReadCore(conn, h, words) {
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const count = words.readUInt16LE(2);
  const offset = words.readUInt32LE(4);
  const buf = Buffer.alloc(Math.min(count, 0xffff));
  let n = 0;
  try { n = fs.readSync(o.fd, buf, 0, buf.length, offset); } catch (e) { n = 0; }
  const w = Buffer.alloc(10);
  w.writeUInt16LE(n, 0);
  const data = Buffer.concat([Buffer.from([0x01]), (() => { const l = Buffer.alloc(2); l.writeUInt16LE(n, 0); return l; })(), buf.subarray(0, n)]);
  return reply(CMD.READ, w, data, 0, false);
}

function doWriteCore(conn, h, words, bytes) {
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const count = words.readUInt16LE(2);
  const offset = words.readUInt32LE(4);
  const data = bytes.subarray(3, 3 + count);
  let n = 0;
  if (count === 0) { try { fs.ftruncateSync(o.fd, offset); } catch (e) {} }
  else { try { n = fs.writeSync(o.fd, data, 0, data.length, offset); } catch (e) { fail(F.mapErrno(e)); } }
  F.touchWriteTime(o);
  const w = Buffer.alloc(2);
  w.writeUInt16LE(n, 0);
  return reply(CMD.WRITE, w, Buffer.alloc(0), 0, false);
}

// --- path operations --------------------------------------------------------

function pathFromBytes(h, bytes, base) {
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  if (bytes.length < 1) fail(S.STATUS_INVALID_PARAMETER);
  if (bytes[0] !== 0x04 && bytes[0] !== 0x05) fail(S.STATUS_INVALID_PARAMETER);
  const r = readSmbString(bytes, 1, unicode, base);
  return { name: r.value, next: r.next };
}

// `read only = yes` refuses every modifying request, but not with one status.
// Samba 4.17.12 answers the core command set -- DELETE, DELETE_DIRECTORY,
// CREATE_DIRECTORY, RENAME, NT_RENAME, SET_INFORMATION, SET_INFORMATION2 --
// with STATUS_MEDIA_WRITE_PROTECTED, and the TRANS2 layer -- SET_PATH_INFO,
// SET_FILE_INFO, TRANS2_CREATE_DIRECTORY -- with STATUS_ACCESS_DENIED.  Opening
// for write is refused by the access check in fileops.js, also ACCESS_DENIED.
function refuseIfReadOnly(t) {
  if (t.share.readOnly) fail(S.STATUS_MEDIA_WRITE_PROTECTED);
}

function refuseTrans2IfReadOnly(t) {
  if (t.share.readOnly) fail(S.STATUS_ACCESS_DENIED);
}

function doDelete(conn, h, words, bytes, bytesBase) {
  const t = requireShare(conn, h);
  refuseIfReadOnly(t);
  const searchAttrs = words.length >= 2 ? words.readUInt16LE(0) : 0;
  const { name } = pathFromBytes(h, bytes, bytesBase);
  const base = path.basename(name.replace(/\\/g, '/'));
  const dir = V.toUnix(name.replace(/\\/g, '/').replace(/[^\/]*$/, ''));
  let deleted = 0;
  if (/[*?]/.test(base)) {
    const dirRel = t.share.resolve(dir);
    const list = F.listDirectory(t.share, dirRel, base, false);
    for (const e of list) {
      if (e.st.isDirectory()) continue;
      const n = t.share.node(e.rel, e.st);
      if (V.shareConflict(n, A.DELETE, 0, null)) fail(S.STATUS_SHARING_VIOLATION);
      if (n.deletePending) fail(S.STATUS_DELETE_PENDING);
      try { fs.unlinkSync(t.share.real(e.rel)); deleted++; t.share.noteEntry(e.rel, false); t.share.meta.delete(e.rel.toLowerCase()); } catch (err) {}
    }
    if (deleted === 0) fail(S.STATUS_NO_SUCH_FILE);
  } else {
    const rel = t.share.resolve(name);
    const stt = t.share.stat(rel);
    if (!stt) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
    if (stt.isDirectory()) fail(S.STATUS_FILE_IS_A_DIRECTORY);
    if ((t.share.attributes(rel, stt) & ATTR.READONLY)) fail(S.STATUS_CANNOT_DELETE);
    const n = t.share.node(rel, stt);
    if (V.shareConflict(n, A.DELETE, 0, null)) fail(S.STATUS_SHARING_VIOLATION);
    if (n.deletePending) fail(S.STATUS_DELETE_PENDING);
    try { fs.unlinkSync(t.share.real(rel)); } catch (e) { fail(F.mapErrno(e)); }
    t.share.noteEntry(rel, false);
    t.share.meta.delete(rel.toLowerCase());
  }
  return reply(CMD.DELETE, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doDeleteDirectory(conn, h, bytes, bytesBase) {
  const t = requireShare(conn, h);
  refuseIfReadOnly(t);
  const { name } = pathFromBytes(h, bytes, bytesBase);
  const rel = t.share.resolve(name);
  const stt = t.share.stat(rel);
  if (!stt) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  if (!stt.isDirectory()) fail(S.STATUS_NOT_A_DIRECTORY);
  try { fs.rmdirSync(t.share.real(rel)); } catch (e) { fail(F.mapErrno(e)); }
  t.share.noteEntry(rel, false);
  t.share.meta.delete(rel.toLowerCase());
  return reply(CMD.DELETE_DIRECTORY, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doCreateDirectory(conn, h, bytes, bytesBase) {
  const t = requireShare(conn, h);
  if (t.share.readOnly) fail(S.STATUS_MEDIA_WRITE_PROTECTED);
  const { name } = pathFromBytes(h, bytes, bytesBase);
  const rel = t.share.resolve(name);
  if (t.share.stat(rel)) fail(S.STATUS_OBJECT_NAME_COLLISION);
  try { fs.mkdirSync(t.share.real(rel)); } catch (e) { fail(F.mapErrno(e)); }
  t.share.noteEntry(rel, true);
  t.share.metaFor(rel).createTime = Date.now();
  return reply(CMD.CREATE_DIRECTORY, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doCheckDirectory(conn, h, bytes, bytesBase) {
  const t = requireShare(conn, h);
  const { name } = pathFromBytes(h, bytes, bytesBase);
  const rel = t.share.resolve(name);
  const stt = t.share.stat(rel);
  if (!stt) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  if (!stt.isDirectory()) fail(S.STATUS_NOT_A_DIRECTORY);
  return reply(CMD.CHECK_DIRECTORY, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function renameFile(share, oldName, newName, replaceIfExists) {
  const oldRel = share.resolve(oldName);
  const oldSt = share.stat(oldRel);
  if (!oldSt) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  const newRel = share.resolve(newName);
  const newSt = share.stat(newRel);
  if (newSt && oldRel.toLowerCase() !== newRel.toLowerCase()) {
    if (!replaceIfExists) fail(S.STATUS_OBJECT_NAME_COLLISION);
    const n = share.node(newRel, newSt);
    if (n.opens.size > 0) fail(S.STATUS_ACCESS_DENIED);
  }
  const node = share.node(oldRel, oldSt);
  const DATA = A.FILE_READ_DATA | A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.FILE_EXECUTE;
  for (const o of node.opens) {
    if ((o.access & DATA) && !(o.shareAccess & SHARE.DELETE)) fail(S.STATUS_SHARING_VIOLATION);
  }
  try { fs.renameSync(share.real(oldRel), share.real(newRel)); } catch (e) { fail(F.mapErrno(e)); }
  share.noteEntry(oldRel, false);
  share.noteEntry(newRel, true);
  share.moveMeta(oldRel, newRel);
  // Renaming a file marks it as needing archiving.
  if (!oldSt.isDirectory()) {
    share.applyAttributes(newRel, (share.attributes(newRel) | ATTR.ARCHIVE) & ~ATTR.NORMAL);
  }
  for (const o of node.opens) o.rel = newRel;
  node.rel = newRel;
}

function doRename(conn, h, words, bytes, bytesBase) {
  const t = requireShare(conn, h);
  refuseIfReadOnly(t);
  const first = pathFromBytes(h, bytes, bytesBase);
  const rest = bytes.subarray(first.next);
  const second = pathFromBytes(h, rest, bytesBase + first.next);
  renameFile(t.share, first.name, second.name, false);
  return reply(CMD.RENAME, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doNtRename(conn, h, words, bytes, bytesBase) {
  const t = requireShare(conn, h);
  refuseIfReadOnly(t);
  const infoLevel = words.length >= 4 ? words.readUInt16LE(2) : 0x0103;
  const first = pathFromBytes(h, bytes, bytesBase);
  const rest = bytes.subarray(first.next);
  const second = pathFromBytes(h, rest, bytesBase + first.next);
  if (infoLevel === 0x0104) { // create hard link
    try { fs.linkSync(t.share.real(t.share.resolve(first.name)), t.share.real(t.share.resolve(second.name))); }
    catch (e) { fail(F.mapErrno(e)); }
  } else {
    renameFile(t.share, first.name, second.name, false);
  }
  return reply(CMD.NT_RENAME, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function unixTime32(ms) { return Math.floor(ms / 1000) >>> 0; }

function doQueryInformation(conn, h, bytes, bytesBase) {
  const t = requireShare(conn, h);
  const { name } = pathFromBytes(h, bytes, bytesBase);
  const rel = t.share.resolve(name);
  const stt = t.share.stat(rel);
  if (!stt) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  const w = Buffer.alloc(20);
  w.writeUInt16LE(t.share.attributes(rel, stt) & 0xffff, 0);
  w.writeUInt32LE(unixTime32(stt.mtimeMs), 2);
  w.writeUInt32LE(stt.isDirectory() ? 0 : (Number(stt.size) >>> 0), 6);
  return reply(CMD.QUERY_INFORMATION, w, Buffer.alloc(0), 0, false);
}

function doSetInformation(conn, h, words, bytes, bytesBase) {
  const t = requireShare(conn, h);
  refuseIfReadOnly(t);
  const attrs = words.readUInt16LE(0);
  const mtime = words.readUInt32LE(2);
  const { name } = pathFromBytes(h, bytes, bytesBase);
  const rel = t.share.resolve(name);
  const stt = t.share.stat(rel);
  if (!stt) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  // SMB_COM_SET_INFORMATION ([MS-CIFS] 2.2.4.9): an attribute field of 0 means
  // FILE_ATTRIBUTE_NORMAL, which clears the read-only bit.
  t.share.applyAttributes(rel, attrs);
  if (mtime !== 0 && mtime !== 0xffffffff) { try { fs.utimesSync(t.share.real(rel), mtime, mtime); } catch (e) {} }
  return reply(CMD.SET_INFORMATION, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doQueryInformation2(conn, h, words) {
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const stt = o.share.stat(o.rel);
  const w = Buffer.alloc(22);
  const cm = o.share.metaFor(o.rel).createTime || stt.ctimeMs;
  w.writeUInt16LE(dosDate(cm), 0); w.writeUInt16LE(dosTime(cm), 2);
  w.writeUInt16LE(dosDate(stt.atimeMs), 4); w.writeUInt16LE(dosTime(stt.atimeMs), 6);
  w.writeUInt16LE(dosDate(stt.mtimeMs), 8); w.writeUInt16LE(dosTime(stt.mtimeMs), 10);
  w.writeUInt32LE(Number(stt.size) >>> 0, 12);
  w.writeUInt32LE(Number(stt.size) >>> 0, 16);
  w.writeUInt16LE(o.share.attributes(o.rel, stt) & 0xffff, 20);
  return reply(CMD.QUERY_INFORMATION2, w, Buffer.alloc(0), 0, false);
}

function doSetInformation2(conn, h, words) {
  refuseIfReadOnly(requireShare(conn, h));
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  return reply(CMD.SET_INFORMATION2, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doQueryInformationDisk(conn, h) {
  requireShare(conn, h);
  const w = Buffer.alloc(10);
  w.writeUInt16LE(2048, 0);
  w.writeUInt16LE(8, 2);
  w.writeUInt16LE(512, 4);
  w.writeUInt16LE(1024, 6);
  w.writeUInt16LE(0, 8);
  return reply(CMD.QUERY_INFORMATION_DISK, w, Buffer.alloc(0), 0, false);
}

function doSeek(conn, h, words) {
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const mode = words.readUInt16LE(2);
  const off = words.readInt32LE(4);
  const stt = o.share.stat(o.rel);
  let pos = off;
  if (mode === 2) pos = Number(stt.size) + off;
  const w = Buffer.alloc(4);
  w.writeUInt32LE(pos >>> 0, 0);
  return reply(CMD.SEEK, w, Buffer.alloc(0), 0, false);
}

function dosDate(ms) {
  const d = new Date(ms);
  return (((d.getFullYear() - 1980) & 0x7f) << 9) | (((d.getMonth() + 1) & 0xf) << 5) | (d.getDate() & 0x1f);
}
function dosTime(ms) {
  const d = new Date(ms);
  return ((d.getHours() & 0x1f) << 11) | ((d.getMinutes() & 0x3f) << 5) | ((d.getSeconds() >> 1) & 0x1f);
}

// --- locking ----------------------------------------------------------------

function doLockingAndx(conn, h, words, bytes) {
  const fid = words.readUInt16LE(4);
  const typeOfLock = words[6];
  const newOplockLevel = words[7];
  const timeout = words.readUInt32LE(8);
  const nUnlocks = words.readUInt16LE(12);
  const nLocks = words.readUInt16LE(14);
  const large = (typeOfLock & 0x10) !== 0;
  const oplockRelease = (typeOfLock & 0x02) !== 0;
  const s = st(conn);
  const o = s.opens.get(fid);

  if (oplockRelease) {
    if (o) {
      o.oplockLevel = newOplockLevel === 1 ? OPLOCK.II : OPLOCK.NONE;
      if (o.node.oplock && o.node.oplock.open === o) {
        if (newOplockLevel === 0) o.node.oplock = null;
        else o.node.oplock.level = OPLOCK.II;
      }
      if (o.node.oplockAck) { const f = o.node.oplockAck; o.node.oplockAck = null; f(); }
    }
    return null; // oplock break acknowledgements get no response
  }
  if (!o || o.closed) fail(S.STATUS_INVALID_HANDLE);

  const rec = large ? 20 : 10;
  const pid = (h.pidHigh << 16) | h.pidLow;
  let p = 0;
  for (let i = 0; i < nUnlocks; i++, p += rec) {
    const r = readRange(bytes, p, large);
    if (!F.unlockRange(o, r.offset, r.length, r.pid)) fail(S.STATUS_RANGE_NOT_LOCKED);
  }
  const exclusive = (typeOfLock & 0x01) === 0;
  const ranges = [];
  for (let i = 0; i < nLocks; i++, p += rec) ranges.push(readRange(bytes, p, large));

  const okReply = () => {
    const w = Buffer.alloc(4);
    w[0] = 0xff; w[1] = 0; w.writeUInt16LE(0, 2);
    return reply(CMD.LOCKING_ANDX, w, Buffer.alloc(0), 0, true);
  };
  const attempt = () => {
    const granted = [];
    for (const r of ranges) {
      if (!F.lockRange(o, r.offset, r.length, exclusive, r.pid)) {
        for (const g of granted) F.unlockRange(o, g.offset, g.length, g.pid);
        return r;
      }
      granted.push(r);
    }
    return null;
  };
  const blocked = attempt();
  if (blocked === null) return okReply();
  // [MS-CIFS] 2.2.4.32.1: a non-zero Timeout makes this a blocking lock
  // request; 0xFFFFFFFF waits indefinitely.
  if (timeout === 0) fail(F.lockFailureStatus(o, blocked.offset, blocked.length, blocked.pid));
  let done = false;
  let cancel = null;
  const timer = setTimeout(() => {
    if (done) return;
    done = true;
    if (cancel) cancel();
    F.lockFailureStatus(o, blocked.offset, blocked.length, blocked.pid);
    if (!conn.closed) conn.send(errorReply(conn, h, S.STATUS_FILE_LOCK_CONFLICT, CMD.LOCKING_ANDX));
  }, timeout === 0xffffffff ? 0x7fffffff : timeout);
  cancel = F.addLockWaiter(o.node, () => {
    if (done || conn.closed || o.closed) { done = true; clearTimeout(timer); return true; }
    if (attempt() !== null) return false;
    done = true;
    clearTimeout(timer);
    conn.send(assemble(h, [okReply()]));
    return true;
  });
  return null;
}

function readRange(bytes, p, large) {
  if (large) {
    return {
      pid: bytes.readUInt16LE(p),
      offset: (BigInt(bytes.readUInt32LE(p + 4)) << 32n) | BigInt(bytes.readUInt32LE(p + 8)),
      length: (BigInt(bytes.readUInt32LE(p + 12)) << 32n) | BigInt(bytes.readUInt32LE(p + 16)),
    };
  }
  return {
    pid: bytes.readUInt16LE(p),
    offset: BigInt(bytes.readUInt32LE(p + 2)),
    length: BigInt(bytes.readUInt32LE(p + 6)),
  };
}

function doLockByteRange(conn, h, words, lock) {
  const fid = words.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const count = BigInt(words.readUInt32LE(2));
  const offset = BigInt(words.readUInt32LE(6));
  const pid = (h.pidHigh << 16) | h.pidLow;
  if (lock) { if (!F.lockRange(o, offset, count, true, pid)) fail(F.lockFailureStatus(o, offset, count, pid)); }
  else if (!F.unlockRange(o, offset, count, pid)) fail(S.STATUS_RANGE_NOT_LOCKED);
  return reply(lock ? CMD.LOCK_BYTE_RANGE : CMD.UNLOCK_BYTE_RANGE, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

// SMB_COM_PROCESS_EXIT ([MS-CIFS] 2.2.4.20): close every file this PID opened
// on this session.
function doProcessExit(conn, h) {
  const s = st(conn);
  const pid = (h.pidHigh << 16) | h.pidLow;
  for (const [fid, o] of Array.from(s.opens)) {
    if (o.session && o.session.uid !== h.uid) continue;
    F.closeOpen(o);
    s.opens.delete(fid);
    if (o.tree) o.tree.opens.delete(fid);
  }
  for (const [sid, sr] of Array.from(s.searches)) s.searches.delete(sid);
  return reply(CMD.PROCESS_EXIT, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}

function doFindClose2(conn, h, words) {
  const s = st(conn);
  s.searches.delete(words.readUInt16LE(0));
  return reply(CMD.FIND_CLOSE2, Buffer.alloc(0), Buffer.alloc(0), 0, false);
}


// --- transactions -----------------------------------------------------------

function alignUp(n, a) { return (n + a - 1) & ~(a - 1); }

function transReply(command, setup, params, data, status, flags2) {
  const setupWords = Buffer.from(setup || []);
  const w = Buffer.alloc(20 + setupWords.length);
  const bytesStart = 32 + 1 + w.length + 2;
  const paramOffset = alignUp(bytesStart, 4);
  const pad1 = paramOffset - bytesStart;
  const dataOffset = alignUp(paramOffset + params.length, 4);
  const pad2 = dataOffset - (paramOffset + params.length);
  w.writeUInt16LE(params.length, 0);
  w.writeUInt16LE(data.length, 2);
  w.writeUInt16LE(0, 4);
  w.writeUInt16LE(params.length, 6);
  w.writeUInt16LE(params.length ? paramOffset : 0, 8);
  w.writeUInt16LE(0, 10);
  w.writeUInt16LE(data.length, 12);
  w.writeUInt16LE(data.length ? dataOffset : 0, 14);
  w.writeUInt16LE(0, 16);
  w.writeUInt8(setupWords.length / 2, 18);
  w.writeUInt8(0, 19);
  setupWords.copy(w, 20);
  const bytes = Buffer.concat([Buffer.alloc(pad1), params, Buffer.alloc(pad2), data]);
  return reply(command, w, bytes, status || 0, false, flags2);
}

function ntTransReply(setup, params, data, status) {
  const setupWords = Buffer.from(setup || []);
  const w = Buffer.alloc(36 + setupWords.length);
  const bytesStart = 32 + 1 + w.length + 2;
  const paramOffset = alignUp(bytesStart, 4);
  const pad1 = paramOffset - bytesStart;
  const dataOffset = alignUp(paramOffset + params.length, 4);
  const pad2 = dataOffset - (paramOffset + params.length);
  w.writeUInt32LE(params.length, 3);
  w.writeUInt32LE(data.length, 7);
  w.writeUInt32LE(params.length, 11);
  w.writeUInt32LE(params.length ? paramOffset : 0, 15);
  w.writeUInt32LE(0, 19);
  w.writeUInt32LE(data.length, 23);
  w.writeUInt32LE(data.length ? dataOffset : 0, 27);
  w.writeUInt32LE(0, 31);
  w.writeUInt8(setupWords.length / 2, 35);
  setupWords.copy(w, 36);
  const bytes = Buffer.concat([Buffer.alloc(pad1), params, Buffer.alloc(pad2), data]);
  return reply(CMD.NT_TRANSACT, w, bytes, status || 0, false);
}

function doTrans2(conn, h, words, bytes, msg, off) {
  if (words.length < 28) fail(S.STATUS_INVALID_PARAMETER);
  const totalParam = words.readUInt16LE(0);
  const totalData = words.readUInt16LE(2);
  const maxParam = words.readUInt16LE(4);
  const maxData = words.readUInt16LE(6);
  const paramCount = words.readUInt16LE(18);
  const paramOffset = words.readUInt16LE(20);
  const dataCount = words.readUInt16LE(22);
  const dataOffset = words.readUInt16LE(24);
  const setupCount = words[26];
  const setup = words.subarray(28, 28 + setupCount * 2);
  const sub = setupCount > 0 ? setup.readUInt16LE(0) : 0;
  const params = msg.subarray(paramOffset, paramOffset + paramCount);
  const data = msg.subarray(dataOffset, dataOffset + dataCount);
  if (paramCount < totalParam || dataCount < totalData) {
    const s = st(conn);
    s.transBuf = { sub, params: Buffer.from(params), data: Buffer.from(data), totalParam, totalData, maxParam, maxData };
    return null;
  }
  return trans2Do(conn, h, sub, params, data, maxParam, maxData);
}

function trans2Do(conn, h, sub, params, data, maxParam, maxData) {
  switch (sub) {
    case T2.FIND_FIRST2: return t2FindFirst(conn, h, params, maxData);
    case T2.FIND_NEXT2: return t2FindNext(conn, h, params, maxData);
    case T2.QUERY_FS_INFORMATION: return t2QueryFs(conn, h, params, maxData);
    case T2.QUERY_PATH_INFORMATION: return t2QueryPath(conn, h, params, maxData);
    case T2.QUERY_FILE_INFORMATION: return t2QueryFile(conn, h, params, maxData);
    case T2.SET_PATH_INFORMATION: return t2SetPath(conn, h, params, data);
    case T2.SET_FILE_INFORMATION: return t2SetFile(conn, h, params, data);
    case T2.CREATE_DIRECTORY: return t2CreateDirectory(conn, h, params);
    case T2.SET_FS_INFORMATION: fail(S.STATUS_ACCESS_DENIED); break;
    case T2.GET_DFS_REFERRAL: fail(S.STATUS_FS_DRIVER_REQUIRED); break;
    default: fail(S.STATUS_NOT_IMPLEMENTED);
  }
  return null;
}

const FIND_LEVEL_MAP = {
  0x0101: I.FILE.Directory, 0x0102: I.FILE.FullDirectory, 0x0103: I.FILE.Names,
  0x0104: I.FILE.BothDirectory, 0x0105: I.FILE.IdFullDirectory, 0x0106: I.FILE.IdBothDirectory,
};

function standardFindEntry(share, ent, resumeKeys, index, unicode) {
  const stt = ent.st;
  const cm = share.metaFor(ent.rel).createTime || stt.ctimeMs;
  const nameBuf = unicode ? uni(ent.name) : Buffer.from(ent.name, 'latin1');
  const head = resumeKeys ? 27 : 23;
  const b = Buffer.alloc(head + nameBuf.length + (unicode ? 2 : 1));
  let p = 0;
  if (resumeKeys) { b.writeUInt32LE(index, 0); p = 4; }
  b.writeUInt16LE(dosDate(cm), p); b.writeUInt16LE(dosTime(cm), p + 2);
  b.writeUInt16LE(dosDate(stt.atimeMs), p + 4); b.writeUInt16LE(dosTime(stt.atimeMs), p + 6);
  b.writeUInt16LE(dosDate(stt.mtimeMs), p + 8); b.writeUInt16LE(dosTime(stt.mtimeMs), p + 10);
  b.writeUInt32LE(stt.isDirectory() ? 0 : (Number(stt.size) >>> 0), p + 12);
  b.writeUInt32LE(stt.isDirectory() ? 0 : (Number(stt.size) >>> 0), p + 16);
  b.writeUInt16LE(share.attributes(ent.rel, stt) & 0xffff, p + 20);
  b.writeUInt8(nameBuf.length, p + 22);
  nameBuf.copy(b, p + 23);
  return b;
}

function buildFindEntries(conn, h, share, search, maxData, level, singleEntry) {
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  const cls = FIND_LEVEL_MAP[level];
  const chunks = [];
  let count = 0, lastNameOffset = 0, total = 0;
  const limit = Math.max(maxData, 512);
  while (search.pos < search.entries.length) {
    const ent = search.entries[search.pos];
    let buf;
    if (cls !== undefined) buf = I.dirEntry(cls, share, ent, search.pos + 1, false);
    else if (level === 0x0001 || level === 0x0002) buf = standardFindEntry(share, ent, search.resumeKeys, search.pos + 1, unicode);
    else fail(S.STATUS_INVALID_LEVEL);
    if (total + buf.length > limit && count > 0) break;
    lastNameOffset = total;
    chunks.push(buf);
    total += buf.length;
    count++;
    search.pos++;
    if (singleEntry) break;
    if (count >= search.maxCount) break;
  }
  if (chunks.length > 0 && cls !== undefined) {
    const last = chunks[chunks.length - 1];
    last.writeUInt32LE(0, 0);
  }
  return { data: Buffer.concat(chunks), count, lastNameOffset, eof: search.pos >= search.entries.length };
}

function t2FindFirst(conn, h, params, maxData) {
  const t = requireShare(conn, h);
  const s = st(conn);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  const searchAttrs = params.readUInt16LE(0);
  const searchCount = params.readUInt16LE(2);
  const flags = params.readUInt16LE(4);
  const level = params.readUInt16LE(6);
  const nameRes = readSmbString(params, 12, unicode, 0);
  let pattern = nameRes.value.replace(/\//g, '\\');
  // A trailing path separator is dropped before the directory and the search
  // pattern are split apart, so `\\data\\gamma\\` searches \\data for `gamma`.
  while (pattern.length > 1 && pattern.endsWith('\\')) pattern = pattern.slice(0, -1);
  let dir = '';
  const idx = pattern.lastIndexOf('\\');
  if (idx >= 0) { dir = pattern.slice(0, idx); pattern = pattern.slice(idx + 1); }
  if (pattern === '') pattern = '*';
  const dirRel = t.share.resolve(dir || '');
  const dst = t.share.stat(dirRel);
  if (!dst) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  if (!dst.isDirectory()) fail(S.STATUS_INVALID_PARAMETER);
  if (!V.validName(V.toUnix(pattern), true)) fail(S.STATUS_OBJECT_NAME_INVALID);
  let entries = F.listDirectory(t.share, dirRel, pattern, true);
  entries = entries.filter((e) => filterBySearchAttrs(t.share, e, searchAttrs));
  if (entries.length === 0) fail(S.STATUS_NO_SUCH_FILE);
  const sid = s.sidNext++ & 0xffff;
  const search = {
    sid, entries, pos: 0, level, dirRel, pattern,
    maxCount: searchCount || 1, resumeKeys: (flags & 0x04) !== 0,
  };
  s.searches.set(sid, search);
  const r = buildFindEntries(conn, h, t.share, search, maxData, level, false);
  const p = Buffer.alloc(10);
  p.writeUInt16LE(sid, 0);
  p.writeUInt16LE(r.count, 2);
  p.writeUInt16LE(r.eof ? 1 : 0, 4);
  p.writeUInt16LE(0, 6);
  p.writeUInt16LE(r.lastNameOffset, 8);
  if (r.eof && (flags & 0x03)) s.searches.delete(sid);
  return transReply(CMD.TRANSACTION2, [], p, r.data, 0);
}

// [MS-CIFS] 2.2.6.2: an entry is excluded when it carries a hidden, system or
// directory attribute the client did not ask for.
function filterBySearchAttrs(share, ent, searchAttrs) {
  const mask = ATTR.HIDDEN | ATTR.SYSTEM | ATTR.DIRECTORY;
  const a = share.attributes(ent.rel, ent.st);
  return ((a & ~searchAttrs) & mask) === 0;
}

function t2FindNext(conn, h, params, maxData) {
  const t = requireShare(conn, h);
  const s = st(conn);
  const sid = params.readUInt16LE(0);
  const searchCount = params.readUInt16LE(2);
  const level = params.readUInt16LE(4);
  const flags = params.readUInt16LE(10);
  const search = s.searches.get(sid);
  if (!search) fail(S.STATUS_NO_MORE_FILES);
  search.maxCount = searchCount || 1;
  search.level = level;
  const r = buildFindEntries(conn, h, t.share, search, maxData, level, false);
  const p = Buffer.alloc(8);
  p.writeUInt16LE(r.count, 0);
  p.writeUInt16LE(r.eof ? 1 : 0, 2);
  p.writeUInt16LE(0, 4);
  p.writeUInt16LE(r.lastNameOffset, 6);
  if (r.count === 0) { s.searches.delete(sid); fail(S.STATUS_NO_MORE_FILES); }
  if (r.eof && (flags & 0x03)) s.searches.delete(sid);
  return transReply(CMD.TRANSACTION2, [], p, r.data, 0);
}

function t2QueryFs(conn, h, params, maxData) {
  const t = requireShare(conn, h);
  const level = params.readUInt16LE(0);
  let data;
  if (level >= 1000) {
    try { data = I.fsInfo(level - 1000, t.share, t.name); }
    catch (e) {
      if (e instanceof SmbError && e.status === S.STATUS_INVALID_INFO_CLASS) fail(S.STATUS_INVALID_LEVEL);
      throw e;
    }
  }
  else switch (level) {
    case 0x0001: { // SMB_INFO_ALLOCATION
      data = Buffer.alloc(18);
      data.writeUInt32LE(0, 0); data.writeUInt32LE(8, 4);
      data.writeUInt32LE(1048576, 8); data.writeUInt32LE(524288, 12);
      data.writeUInt16LE(512, 16);
      break;
    }
    case 0x0002: { // SMB_INFO_VOLUME
      const n = Buffer.from(t.share ? t.share.name : t.name, 'latin1');
      data = Buffer.alloc(5 + n.length);
      data.writeUInt32LE(0xdeadbeef, 0);
      data.writeUInt8(n.length, 4);
      n.copy(data, 5);
      break;
    }
    case 0x0102: data = I.fsInfo(I.FS.Volume, t.share, t.name); break;
    case 0x0103: data = I.fsInfo(I.FS.Size, t.share, t.name); break;
    case 0x0104: data = I.fsInfo(I.FS.Device, t.share, t.name); break;
    case 0x0105: data = I.fsInfo(I.FS.Attribute, t.share, t.name); break;
    default: fail(S.STATUS_INVALID_LEVEL);
  }
  return transReply(CMD.TRANSACTION2, [], Buffer.alloc(0), data, 0);
}

function queryInfoBuffer(share, rel, stt, open, level, name, unicode) {
  if (level >= 1000) {
    // A TRANS2 request names an information *level*; an unknown one is
    // STATUS_INVALID_LEVEL even when it maps to a pass-through class.
    try { return passthroughQuery(share, rel, stt, open, level - 1000, name); }
    catch (e) {
      if (e instanceof SmbError && e.status === S.STATUS_INVALID_INFO_CLASS) fail(S.STATUS_INVALID_LEVEL);
      throw e;
    }
  }
  const cm = share.metaFor(rel).createTime || stt.ctimeMs;
  switch (level) {
    case 0x0001: case 0x0002: {
      const b = Buffer.alloc(level === 0x0002 ? 26 : 22);
      b.writeUInt16LE(dosDate(cm), 0); b.writeUInt16LE(dosTime(cm), 2);
      b.writeUInt16LE(dosDate(stt.atimeMs), 4); b.writeUInt16LE(dosTime(stt.atimeMs), 6);
      b.writeUInt16LE(dosDate(stt.mtimeMs), 8); b.writeUInt16LE(dosTime(stt.mtimeMs), 10);
      b.writeUInt32LE(stt.isDirectory() ? 0 : (Number(stt.size) >>> 0), 12);
      b.writeUInt32LE(stt.isDirectory() ? 0 : (Number(stt.size) >>> 0), 16);
      b.writeUInt16LE(share.attributes(rel, stt) & 0xffff, 20);
      return b;
    }
    case 0x0006: return Buffer.alloc(0); // SMB_INFO_IS_NAME_VALID
    // [MS-CIFS] 2.2.8.3.1/2.2.8.3.2 as Samba emits them: the basic level stops
    // after ExtFileAttributes, the standard level carries two trailing
    // reserved bytes.
    case 0x0101: return I.basic(share, rel, stt).subarray(0, 36);
    case 0x0102: return Buffer.concat([
      I.smb1Standard(share, rel, stt, open ? open.node : share.findNode(rel, stt)),
      Buffer.alloc(2)]);
    case 0x0103: return Buffer.alloc(4);
    case 0x0104: return I.nameInfo('\\' + rel.replace(/\//g, '\\'));
    case 0x0107: return I.smb1All(share, rel, stt, open, '\\' + rel.replace(/\//g, '\\'));
    case 0x0108: return I.nameInfo(V.mangle(path.basename(rel)));
    case 0x0109: return I.streamInfo(share, rel, stt);
    case 0x010b: return I.compression(stt);
    default: fail(S.STATUS_INVALID_LEVEL);
  }
}

function passthroughQuery(share, rel, stt, open, cls, name) {
  const node = open ? open.node : share.node(rel, stt);
  switch (cls) {
    case I.FILE.Basic: return Buffer.concat([I.basic(share, rel, stt), Buffer.alloc(4)]);
    case I.FILE.Standard: return I.standard(share, rel, stt, node);
    case I.FILE.Internal: return I.internal(stt);
    case I.FILE.Ea: return Buffer.alloc(4);
    case I.FILE.Access: return I.accessInfo(open ? open.access : 0x001f01ff);
    case I.FILE.Name: return I.nameInfo('\\' + rel.replace(/\//g, '\\'));
    case I.FILE.NormalizedName: return I.nameInfo('\\' + rel.replace(/\//g, '\\'));
    case I.FILE.All: return I.allInfo(share, rel, stt, open, '\\' + rel.replace(/\//g, '\\'));
    case I.FILE.AlternateName: return I.nameInfo(V.mangle(path.basename(rel)));
    case I.FILE.Stream: return I.streamInfo(share, rel, stt);
    case I.FILE.Compression: return I.compression(stt);
    case I.FILE.NetworkOpen: return I.networkOpen(share, rel, stt);
    case I.FILE.AttributeTag: return I.attributeTag(share, rel, stt);
    case I.FILE.Position: return I.positionInfo(0);
    case I.FILE.Mode: return I.modeInfo(open ? (open.createOptions & 0x0000103e) : 0);
    case I.FILE.Alignment: return I.alignmentInfo();
    case I.FILE.IdInformation: return I.idInfo(stt);
    case I.FILE.EndOfFile: { const b = Buffer.alloc(8); b.writeBigUInt64LE(stt.isDirectory() ? 0n : BigInt(stt.size)); return b; }
    case I.FILE.Allocation: { const b = Buffer.alloc(8); b.writeBigUInt64LE(stt.isDirectory() ? 0n : I.alloc(0, BigInt(stt.size))); return b; }
    default: fail(S.STATUS_INVALID_INFO_CLASS);
  }
}

function t2QueryPath(conn, h, params, maxData) {
  const t = requireShare(conn, h);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  const level = params.readUInt16LE(0);
  const name = readSmbString(params, 6, unicode, 0).value;
  if (!V.validName(V.toUnix(name), false)) fail(S.STATUS_OBJECT_NAME_INVALID);
  const rel = t.share.resolve(name);
  const stt = t.share.stat(rel);
  if (!stt) fail(rel.includes('/') && !t.share.stat(path.dirname(rel)) ? S.STATUS_OBJECT_PATH_NOT_FOUND : S.STATUS_OBJECT_NAME_NOT_FOUND);
  const existing = t.share.findNode(rel, stt);
  if (existing && existing.deletePending) fail(S.STATUS_DELETE_PENDING);
  const data = queryInfoBuffer(t.share, rel, stt, null, level, name, unicode);
  const p = Buffer.alloc(2);
  let status = 0;
  let out = data;
  if (maxData > 0 && data.length > maxData) { out = data.subarray(0, maxData); status = S.STATUS_BUFFER_OVERFLOW; }
  return transReply(CMD.TRANSACTION2, [], p, out, status);
}

function t2QueryFile(conn, h, params, maxData) {
  const t = requireShare(conn, h);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  const fid = params.readUInt16LE(0);
  const level = params.readUInt16LE(2);
  const o = openOf(conn, h, fid);
  const stt = o.share.stat(o.rel);
  if (!stt) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  const data = queryInfoBuffer(o.share, o.rel, stt, o, level, o.rel, unicode);
  const p = Buffer.alloc(2);
  let status = 0;
  let out = data;
  if (maxData > 0 && data.length > maxData) { out = data.subarray(0, maxData); status = S.STATUS_BUFFER_OVERFLOW; }
  return transReply(CMD.TRANSACTION2, [], p, out, status);
}

function applyBasic(share, rel, open, data) {
  const create = data.readBigUInt64LE(0);
  const access = data.readBigUInt64LE(8);
  const write = data.readBigUInt64LE(16);
  const change = data.readBigUInt64LE(24);
  const attrs = data.length >= 36 ? data.readUInt32LE(32) : 0;
  const stt = share.stat(rel);
  let at = stt.atimeMs / 1000, mt = stt.mtimeMs / 1000, touched = false;
  if (access > 0n && access !== 0xffffffffffffffffn) { at = V.fromFileTime(access) / 1000; touched = true; }
  if (write > 0n && write !== 0xffffffffffffffffn) {
    mt = V.fromFileTime(write) / 1000; touched = true;
    // An explicit write time cancels any deferred update on this file.
    const node = open ? open.node : share.findNode(rel);
    if (node) {
      if (node.writeTimeTimer) { clearTimeout(node.writeTimeTimer); node.writeTimeTimer = null; }
      node.savedWriteTime = null;
      node.writeTimeForced = true;
    }
  }
  if (touched) { try { fs.utimesSync(share.real(rel), at, mt); } catch (e) {} }
  if (create > 0n && create !== 0xffffffffffffffffn) share.metaFor(rel).createTime = V.fromFileTime(create);
  if (attrs !== 0) share.applyAttributes(rel, attrs);
}

function setInfoBuffer(conn, h, share, rel, open, level, data) {
  const cls = level >= 1000 ? level - 1000 : null;
  if (cls === null) {
    switch (level) {
      case 0x0001: { // SMB_INFO_STANDARD
        return;
      }
      case 0x0101: applyBasic(share, rel, open, data); return;
      case 0x0102: { // disposition
        if (!open) fail(S.STATUS_INVALID_PARAMETER);
        setDisposition(open, data[0] !== 0);
        return;
      }
      case 0x0103: { // allocation
        if (!open) fail(S.STATUS_INVALID_PARAMETER);
        const size = Number(data.readBigUInt64LE(0));
        try { fs.ftruncateSync(open.fd, size); } catch (e) { fail(F.mapErrno(e)); }
        F.touchWriteTime(open);
        return;
      }
      case 0x0104: { // end of file
        if (!open) fail(S.STATUS_INVALID_PARAMETER);
        if (!(open.access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA))) fail(S.STATUS_ACCESS_DENIED);
        const size = Number(data.readBigUInt64LE(0));
        try { fs.ftruncateSync(open.fd, size); } catch (e) { fail(F.mapErrno(e)); }
        F.touchWriteTime(open);
        return;
      }
      default: fail(S.STATUS_INVALID_LEVEL);
    }
  }
  switch (cls) {
    case I.FILE.Basic: applyBasic(share, rel, open, data); return;
    case I.FILE.Disposition: {
      if (!open) fail(S.STATUS_INVALID_PARAMETER);
      setDisposition(open, data[0] !== 0);
      return;
    }
    case I.FILE.Allocation: {
      if (!open) fail(S.STATUS_INVALID_PARAMETER);
      try { fs.ftruncateSync(open.fd, Number(data.readBigUInt64LE(0))); } catch (e) { fail(F.mapErrno(e)); }
      F.touchWriteTime(open);
      return;
    }
    case I.FILE.EndOfFile: {
      if (!open) fail(S.STATUS_INVALID_PARAMETER);
      if (!(open.access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA))) fail(S.STATUS_ACCESS_DENIED);
      try { fs.ftruncateSync(open.fd, Number(data.readBigUInt64LE(0))); } catch (e) { fail(F.mapErrno(e)); }
      F.touchWriteTime(open);
      return;
    }
    case I.FILE.Rename: {
      const replace = data[0] !== 0;
      const nameLen = data.readUInt32LE(16);
      const newName = data.subarray(20, 20 + nameLen).toString('utf16le').replace(/\0+$/, '');
      renameFile(share, rel, newName, replace);
      if (open) open.rel = share.resolve(newName);
      return;
    }
    case I.FILE.Position: return;
    case I.FILE.Mode: return;
    case I.FILE.ValidDataLength: return;
    case I.FILE.ShortName: fail(S.STATUS_ACCESS_DENIED); return;
    default: fail(S.STATUS_INVALID_INFO_CLASS);
  }
}

function setDisposition(open, del) {
  if (del) {
    const stt = open.share.stat(open.rel);
    if (stt && (open.share.attributes(open.rel, stt) & ATTR.READONLY)) fail(S.STATUS_CANNOT_DELETE);
    if (!(open.access & A.DELETE)) fail(S.STATUS_ACCESS_DENIED);
    if (open.isDir) {
      let entries = [];
      try { entries = fs.readdirSync(open.share.real(open.rel)); } catch (e) {}
      if (entries.length > 0) fail(S.STATUS_DIRECTORY_NOT_EMPTY);
    }
    open.deleteOnClose = true;
    open.node.deletePending = true;
  } else {
    open.deleteOnClose = false;
    open.node.deletePending = false;
  }
}

function t2SetPath(conn, h, params, data) {
  const t = requireShare(conn, h);
  refuseTrans2IfReadOnly(t);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  const level = params.readUInt16LE(0);
  const name = readSmbString(params, 6, unicode, 0).value;
  const rel = t.share.resolve(name);
  if (!t.share.stat(rel)) fail(S.STATUS_OBJECT_NAME_NOT_FOUND);
  setInfoBuffer(conn, h, t.share, rel, null, level, data);
  return transReply(CMD.TRANSACTION2, [], Buffer.alloc(2), Buffer.alloc(0), 0);
}

function t2SetFile(conn, h, params, data) {
  const t = requireShare(conn, h);
  refuseTrans2IfReadOnly(t);
  const fid = params.readUInt16LE(0);
  const level = params.readUInt16LE(2);
  const o = openOf(conn, h, fid);
  setInfoBuffer(conn, h, o.share, o.rel, o, level, data);
  return transReply(CMD.TRANSACTION2, [], Buffer.alloc(2), Buffer.alloc(0), 0);
}

function t2CreateDirectory(conn, h, params) {
  const t = requireShare(conn, h);
  refuseTrans2IfReadOnly(t);
  const unicode = (h.flags2 & FLAGS2.UNICODE) !== 0;
  const name = readSmbString(params, 4, unicode, 0).value;
  const rel = t.share.resolve(name);
  if (t.share.stat(rel)) fail(S.STATUS_OBJECT_NAME_COLLISION);
  try { fs.mkdirSync(t.share.real(rel)); } catch (e) { fail(F.mapErrno(e)); }
  t.share.noteEntry(rel, true);
  return transReply(CMD.TRANSACTION2, [], Buffer.alloc(2), Buffer.alloc(0), 0);
}

// --- NT_TRANSACT ------------------------------------------------------------

function doNtTrans(conn, h, words, bytes, msg, off) {
  if (words.length < 38) fail(S.STATUS_INVALID_PARAMETER);
  const paramCount = words.readUInt32LE(19);
  const paramOffset = words.readUInt32LE(23);
  const dataCount = words.readUInt32LE(27);
  const dataOffset = words.readUInt32LE(31);
  const setupCount = words[35];
  const func = words.readUInt16LE(36);
  const setup = words.subarray(38, 38 + setupCount * 2);
  const params = msg.subarray(paramOffset, paramOffset + paramCount);
  const data = msg.subarray(dataOffset, dataOffset + dataCount);
  const maxParam = words.readUInt32LE(11);
  const maxData = words.readUInt32LE(15);
  switch (func) {
    case NT.CREATE: return ntCreate(conn, h, params, data);
    case NT.NOTIFY_CHANGE: return ntNotifyChange(conn, h, setup);
    case NT.QUERY_SECURITY_DESC: return ntQuerySd(conn, h, params, maxData);
    case NT.SET_SECURITY_DESC: return ntSetSd(conn, h, params, data);
    case NT.RENAME: fail(S.STATUS_NOT_IMPLEMENTED); break;
    case NT.IOCTL: fail(S.STATUS_NOT_SUPPORTED); break;
    default: fail(S.STATUS_NOT_IMPLEMENTED);
  }
  return null;
}

function ntCreate(conn, h, params, data) {
  const t = requireShare(conn, h);
  const sess = session(conn, h);
  const flags = params.readUInt32LE(0);
  const desiredAccess = params.readUInt32LE(8);
  const extFileAttributes = params.readUInt32LE(20);
  const shareAccess = params.readUInt32LE(24);
  const createDisposition = params.readUInt32LE(28);
  const createOptions = params.readUInt32LE(32);
  const sdLength = params.readUInt32LE(36);
  const eaLength = params.readUInt32LE(40);
  const nameLength = params.readUInt32LE(44);
  const name = params.subarray(53, 53 + nameLength).toString('utf16le').replace(/\0+$/, '');
  const requestExclusive = (flags & 0x00000002) !== 0;
  const requestBatch = (flags & 0x00000004) !== 0;
  const { open, action } = F.createFile(t.share, {
    name, desiredAccess, fileAttributes: extFileAttributes, shareAccess,
    createDisposition, createOptions, session: sess, tree: t,
  });
  const fid = registerOpen(conn, h, t, open);
  const oplockLevel = grantOplock(conn, open, requestExclusive, requestBatch);
  const stt = t.share.stat(open.rel);
  const times = t.share.times(open.rel, stt);
  const eof = stt && !open.isDir ? BigInt(stt.size) : 0n;
  const p = Buffer.alloc(69);
  p.writeUInt8(oplockLevel, 0);
  p.writeUInt8(0, 1);
  p.writeUInt16LE(fid, 2);
  p.writeUInt32LE(action, 4);
  p.writeUInt32LE(0, 8);
  p.writeBigUInt64LE(times.create, 12);
  p.writeBigUInt64LE(times.access, 20);
  p.writeBigUInt64LE(times.write, 28);
  p.writeBigUInt64LE(times.change, 36);
  p.writeUInt32LE(t.share.attributes(open.rel, stt), 44);
  p.writeBigUInt64LE(open.isDir ? 0n : I.alloc(0, eof), 48);
  p.writeBigUInt64LE(eof, 56);
  p.writeUInt16LE(0, 64);
  p.writeUInt16LE(0, 66);
  p.writeUInt8(open.isDir ? 1 : 0, 68);
  return ntTransReply([], p, Buffer.alloc(0), 0);
}

function ntNotifyChange(conn, h, setup) {
  const t = requireShare(conn, h);
  const fid = setup.readUInt16LE(4);
  const o = openOf(conn, h, fid);
  // Registered and answered when the directory changes; nothing is emitted for
  // an idle directory, which is what a change-notify request means.
  o.node.notifies.push({ conn, h, fid });
  return null;
}

// NT_TRANSACT_QUERY_SECURITY_DESC ([MS-CIFS] 2.2.7.6): the self-relative
// SECURITY_DESCRIPTOR of [MS-DTYP] 2.4.6, filtered by SecurityInformation.
function ntQuerySd(conn, h, params, maxData) {
  const t = requireShare(conn, h);
  const fid = params.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const info = params.length >= 8 ? params.readUInt32LE(4) : 0x07;
  if (!(o.access & A.READ_CONTROL)) fail(S.STATUS_ACCESS_DENIED);
  if ((info & 0x08) && !(o.access & A.ACCESS_SYSTEM_SECURITY)) fail(S.STATUS_ACCESS_DENIED);
  const stt = o.share.stat(o.rel);
  const full = o.share.securityDescriptorFor(o.rel, stt);
  const sd = SD.buildSecurityDescriptor(SD.filterSecurityInformation(full, info || 0x07));
  const p = Buffer.alloc(4);
  p.writeUInt32LE(sd.length, 0);
  if (maxData < sd.length) return ntTransReply([], p, Buffer.alloc(0), S.STATUS_BUFFER_TOO_SMALL);
  return ntTransReply([], p, sd, 0);
}

function ntSetSd(conn, h, params, data) {
  const t = requireShare(conn, h);
  const fid = params.readUInt16LE(0);
  const o = openOf(conn, h, fid);
  const info = params.readUInt32LE(4);
  if ((info & 0x04) && !(o.access & A.WRITE_DAC)) fail(S.STATUS_ACCESS_DENIED);
  if ((info & (0x01 | 0x02)) && !(o.access & A.WRITE_OWNER)) fail(S.STATUS_ACCESS_DENIED);
  if ((info & 0x08) && !(o.access & A.ACCESS_SYSTEM_SECURITY)) fail(S.STATUS_PRIVILEGE_NOT_HELD);
  try { o.share.setSecurityDescriptor(o.rel, Buffer.from(data), info); }
  catch (e) { fail(S.STATUS_INVALID_SECURITY_DESCR); }
  return ntTransReply([], Buffer.alloc(0), Buffer.alloc(0), 0);
}

// --- SMB_COM_TRANSACTION (named pipes / RAP) --------------------------------

function doTrans(conn, h, words, bytes, msg, off) {
  fail(S.STATUS_NOT_SUPPORTED);
}

module.exports = {
  handle, CMD, FLAGS, FLAGS2, CAP, T2, NT, st, session, tree, openOf, reply,
  assemble, buildHeader, readSmbString, writeSmbString, requireShare,
  renameFile, registerOpen, dosDate, dosTime, parseHeader,
};
