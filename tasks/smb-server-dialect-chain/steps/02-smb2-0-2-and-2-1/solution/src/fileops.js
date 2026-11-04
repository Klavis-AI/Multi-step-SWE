'use strict';
// Dialect-independent file semantics shared by the SMB1 and SMB2 front ends.
const fs = require('fs');
const path = require('path');
const S = require('./ntstatus');
const { A, SHARE, DISP, OPT, ACTION, ATTR } = require('./consts');
const V = require('./vfs');
const { fail, SmbError, GENERIC_MAP, shareConflict, toFileTime } = V;

function isDirOpt(opts) { return (opts & OPT.DIRECTORY_FILE) !== 0; }

const SETTABLE = ATTR.READONLY | ATTR.HIDDEN | ATTR.SYSTEM | ATTR.ARCHIVE |
                 ATTR.TEMPORARY | ATTR.OFFLINE | ATTR.NOT_CONTENT_INDEXED;

// The attribute set a file ends up with after a create, an overwrite or a
// set-attributes request: FILE_ATTRIBUTE_NORMAL is dropped and a file always
// gains FILE_ATTRIBUTE_ARCHIVE.
function settleAttrs(requested, isDir) {
  let a = requested & SETTABLE;
  if (isDir) return (a | ATTR.DIRECTORY) >>> 0;
  return (a | ATTR.ARCHIVE) >>> 0;
}

const DEBUG_CREATE = process.env.SMBD_DEBUG_CREATE === '1';
function createFile(share, req) {
  if (DEBUG_CREATE) {
    try { require('fs').appendFileSync('/tmp/create.log', JSON.stringify({
      name: req.name, da: (req.desiredAccess>>>0).toString(16), sa: req.shareAccess,
      disp: req.createDisposition, opts: (req.createOptions>>>0).toString(16),
      attr: (req.fileAttributes>>>0).toString(16) }) + '\n'); } catch (e) {}
  }
  const {
    name, desiredAccess = 0, fileAttributes = 0, shareAccess = 0,
    createDisposition = DISP.OPEN, createOptions = 0, session = null, tree = null, pid = 0,
  } = req;

  const rel = share.resolve(name);
  if (rel !== '' && !V.validName(rel, false)) fail(S.STATUS_OBJECT_NAME_INVALID);
  let st = share.stat(rel);
  const exists = st !== null;
  const wantDir = isDirOpt(createOptions);
  const noDir = (createOptions & OPT.NON_DIRECTORY_FILE) !== 0;

  if (exists && st.isDirectory() && noDir) fail(S.STATUS_FILE_IS_A_DIRECTORY);
  if (exists && !st.isDirectory() && wantDir) fail(S.STATUS_NOT_A_DIRECTORY);

  let access = GENERIC_MAP(desiredAccess);
  if (access & A.MAXIMUM_ALLOWED) {
    access = (access & ~A.MAXIMUM_ALLOWED) | 0x001f01ff;
    const WRITES = A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.FILE_WRITE_EA |
                   A.DELETE | A.FILE_WRITE_ATTRIBUTES | A.WRITE_DAC | A.WRITE_OWNER;
    if (share.readOnly) access &= ~WRITES;
    // MAXIMUM_ALLOWED grants what the object actually permits: a read-only file
    // grants no write access ([MS-SMB2] 2.2.13.1.1).
    const est = share.stat(rel);
    if (est && !est.isDirectory() && (share.attributes(rel, est) & ATTR.READONLY)) {
      access &= ~(A.FILE_WRITE_DATA | A.FILE_APPEND_DATA);
    }
  }
  if (share.readOnly && (access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.DELETE))) {
    fail(S.STATUS_ACCESS_DENIED);
  }
  // Rights the share's security descriptor grants; anything else is refused.
  const GRANTED = 0x001f01ff;
  if (access & A.ACCESS_SYSTEM_SECURITY) fail(S.STATUS_PRIVILEGE_NOT_HELD);
  if (access & ~(GRANTED | A.MAXIMUM_ALLOWED)) fail(S.STATUS_ACCESS_DENIED);

  // FILE_DELETE_ON_CLOSE requires DELETE access in the same request.
  if ((createOptions & OPT.DELETE_ON_CLOSE) && !(access & (A.DELETE | A.GENERIC_ALL)) &&
      !(desiredAccess & A.MAXIMUM_ALLOWED)) {
    fail(S.STATUS_INVALID_PARAMETER);
  }
  // A read-only file cannot be opened for write, overwritten, or deleted, and
  // asking to create one read-only *and* delete it on close is refused.
  if ((createOptions & OPT.DELETE_ON_CLOSE) && (fileAttributes & ATTR.READONLY)) {
    fail(S.STATUS_CANNOT_DELETE);
  }
  if (exists && !st.isDirectory()) {
    const attrs = share.attributes(rel, st);
    if ((attrs & ATTR.READONLY) && (access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA))) fail(S.STATUS_ACCESS_DENIED);
    if ((attrs & ATTR.READONLY) && (createOptions & OPT.DELETE_ON_CLOSE)) fail(S.STATUS_CANNOT_DELETE);
    if ((attrs & ATTR.READONLY) && (createDisposition === DISP.OVERWRITE || createDisposition === DISP.OVERWRITE_IF || createDisposition === DISP.SUPERSEDE)) fail(S.STATUS_ACCESS_DENIED);
  }

  let node = exists ? share.node(rel, st) : null;
  if (node && node.deletePending) fail(S.STATUS_DELETE_PENDING);

  let action;
  switch (createDisposition) {
    case DISP.SUPERSEDE:   action = exists ? ACTION.SUPERSEDED : ACTION.CREATED; break;
    case DISP.OPEN:        if (!exists) fail(S.STATUS_OBJECT_NAME_NOT_FOUND); action = ACTION.OPENED; break;
    case DISP.CREATE:      if (exists) fail(S.STATUS_OBJECT_NAME_COLLISION); action = ACTION.CREATED; break;
    case DISP.OPEN_IF:     action = exists ? ACTION.OPENED : ACTION.CREATED; break;
    case DISP.OVERWRITE:   if (!exists) fail(S.STATUS_OBJECT_NAME_NOT_FOUND); action = ACTION.OVERWRITTEN; break;
    case DISP.OVERWRITE_IF: action = exists ? ACTION.OVERWRITTEN : ACTION.CREATED; break;
    default: fail(S.STATUS_INVALID_PARAMETER);
  }
  if (!exists && share.readOnly) fail(S.STATUS_ACCESS_DENIED);

  if (exists && node && shareConflict(node, access, shareAccess, null)) fail(S.STATUS_SHARING_VIOLATION);

  const real = share.real(rel);
  const isDir = exists ? st.isDirectory() : wantDir;
  let fd = null;

  if (!exists) {
    const parent = path.dirname(rel);
    if (parent !== '.' && parent !== rel) {
      const pst = share.stat(parent === '.' ? '' : parent);
      if (!pst) fail(S.STATUS_OBJECT_PATH_NOT_FOUND);
    }
    // A newly created or truncated file always carries FILE_ATTRIBUTE_ARCHIVE,
    // and FILE_ATTRIBUTE_NORMAL is not a stored attribute.
    const wanted = settleAttrs(fileAttributes, isDir);
    const mode = share.modeFor(isDir ? (wanted | ATTR.DIRECTORY) : wanted, isDir);
    if (isDir) {
      try { fs.mkdirSync(real, mode); } catch (e) { fail(mapErrno(e)); }
      try { fs.chmodSync(real, mode); } catch (e) {}
    } else {
      try { fd = fs.openSync(real, 'wx+', mode); } catch (e) { fail(mapErrno(e)); }
      try { fs.fchmodSync(fd, mode); } catch (e) {}
    }
    share.noteEntry(rel, true);
    const m = share.metaFor(rel);
    m.createTime = Date.now();
    m.attrs = wanted | (isDir ? ATTR.DIRECTORY : 0);
    st = share.stat(rel);
  } else if (!isDir) {
    const rw = (access & (A.FILE_WRITE_DATA | A.FILE_APPEND_DATA)) ? 'r+' : 'r';
    try { fd = fs.openSync(real, rw); }
    catch (e) {
      if (e.code === 'EACCES' && rw === 'r+') fail(S.STATUS_ACCESS_DENIED);
      try { fd = fs.openSync(real, 'r'); } catch (e2) { fail(mapErrno(e2)); }
    }
    if (action === ACTION.OVERWRITTEN || action === ACTION.SUPERSEDED) {
      try { fs.ftruncateSync(fd, 0); } catch (e) { fail(mapErrno(e)); }
      share.applyAttributes(rel, settleAttrs(fileAttributes, false));
      if (action === ACTION.SUPERSEDED) share.metaFor(rel).createTime = Date.now();
    }
  }

  st = share.stat(rel);
  node = share.node(rel, st);
  const preStat = share.stat(rel);
  const open = new V.Open(share, rel, node, {
    access, shareAccess, isDir, fd,
    deleteOnClose: (createOptions & OPT.DELETE_ON_CLOSE) !== 0,
    createOptions, session, tree, pid,
  });
  open.deleteOnCloseOption = open.deleteOnClose;
  open.baseWriteTime = preStat ? preStat.mtimeMs : null;
  open.baseAccessTime = preStat ? preStat.atimeMs : null;
  node.opens.add(open);
  // FILE_DELETE_ON_CLOSE marks the handle, not the file: the file's
  // delete-pending state is only set when that handle is closed
  // ([MS-FSCC] 2.4.11, [MS-SMB2] 3.3.5.9).
  return { open, action };
}

// Windows and Samba do not publish a file's new write time immediately: the
// update is deferred until the handle closes or `write time update delay`
// elapses, and an explicit set by the client cancels the deferral.
function touchWriteTime(open) {
  const node = open.node;
  const share = open.share;
  if (node.writeTimeForced) return;
  if (!(share.writeTimeUpdateDelay > 0)) return;
  const real = share.real(open.rel);
  if (node.writeTimeTimer) {
    if (node.savedWriteTime != null) {
      try {
        fs.utimesSync(real, (node.savedAccessTime != null ? node.savedAccessTime : node.savedWriteTime) / 1000,
                      node.savedWriteTime / 1000);
      } catch (e) {}
    }
    return;
  }
  // The write time visible to clients is the one the file carried before the
  // first write through this handle.
  if (open.baseWriteTime == null) return;
  node.savedWriteTime = open.baseWriteTime;
  node.savedAccessTime = open.baseAccessTime;
  try {
    fs.utimesSync(real, (node.savedAccessTime != null ? node.savedAccessTime : node.savedWriteTime) / 1000,
                  node.savedWriteTime / 1000);
  } catch (e) {}
  node.writeTimeTimer = setTimeout(() => {
    node.writeTimeTimer = null;
    node.savedWriteTime = null;
    node.savedAccessTime = null;
    if (node.writeTimeForced) return;
    const now = Date.now() / 1000;
    const cur = share.stat(open.rel);
    try { fs.utimesSync(real, cur ? cur.atimeMs / 1000 : now, now); } catch (e) {}
  }, share.writeTimeUpdateDelay);
  if (node.writeTimeTimer.unref) node.writeTimeTimer.unref();
}

function flushWriteTime(node, share, rel) {
  if (!node.writeTimeTimer) return;
  clearTimeout(node.writeTimeTimer);
  node.writeTimeTimer = null;
  node.savedWriteTime = null;
  const savedAccess = node.savedAccessTime;
  node.savedAccessTime = null;
  if (node.writeTimeForced) return;
  const now = Date.now() / 1000;
  const cur = share.stat(rel);
  try {
    fs.utimesSync(share.real(rel),
                  savedAccess != null ? savedAccess / 1000 : (cur ? cur.atimeMs / 1000 : now),
                  now);
  } catch (e) {}
}

function mapErrno(e) {
  switch (e && e.code) {
    case 'ENOENT': return S.STATUS_OBJECT_NAME_NOT_FOUND;
    case 'EEXIST': return S.STATUS_OBJECT_NAME_COLLISION;
    case 'EACCES': case 'EPERM': return S.STATUS_ACCESS_DENIED;
    case 'ENOTDIR': return S.STATUS_OBJECT_PATH_NOT_FOUND;
    case 'EISDIR': return S.STATUS_FILE_IS_A_DIRECTORY;
    case 'ENOTEMPTY': return S.STATUS_DIRECTORY_NOT_EMPTY;
    case 'ENOSPC': return S.STATUS_DISK_FULL;
    case 'EINVAL': return S.STATUS_INVALID_PARAMETER;
    case 'EBADF': return S.STATUS_INVALID_HANDLE;
    default: return S.STATUS_UNSUCCESSFUL;
  }
}

function closeOpen(open) {
  if (open.closed) return;
  open.closed = true;
  const share = open.share, node = open.node;
  if (open.deleteOnClose || open.deleteOnCloseOption) node.deletePending = true;
  flushWriteTime(node, share, open.rel);
  applySticky(open);
  if (open.fd !== null) { try { fs.closeSync(open.fd); } catch (e) {} open.fd = null; }
  node.opens.delete(open);
  node.locks = node.locks.filter((l) => l.open !== open);
  wakeLockWaiters(node);
  for (const [k, l] of node.leases) { l.opens.delete(open); if (l.opens.size === 0) node.leases.delete(k); }
  if (node.oplock && node.oplock.open === open) node.oplock = null;
  if (node.deletePending && node.opens.size === 0) {
    try {
      if (open.isDir) fs.rmdirSync(share.real(open.rel));
      else fs.unlinkSync(share.real(open.rel));
      share.noteEntry(open.rel, false);
      share.meta.delete(open.rel.toLowerCase());
    } catch (e) { /* directory not empty: the delete simply does not happen */ }
    node.deletePending = false;
  }
  share.dropNode(node);
}

function applySticky(open) {
  const s = open.sticky;
  if (s.attrs !== null) open.share.metaFor(open.rel).attrs = s.attrs;
  if (s.write !== null || s.access !== null) {
    const st = open.share.stat(open.rel);
    if (st) {
      const at = s.access !== null ? V.fromFileTime(s.access) / 1000 : st.atimeMs / 1000;
      const mt = s.write !== null ? V.fromFileTime(s.write) / 1000 : st.mtimeMs / 1000;
      try { fs.utimesSync(open.share.real(open.rel), at, mt); } catch (e) {}
    }
  }
  if (s.create !== null) open.share.metaFor(open.rel).createTime = V.fromFileTime(s.create);
  open.sticky = { write: null, access: null, change: null, create: null, attrs: null };
}

// --- byte-range locks -------------------------------------------------------
//
// [MS-CIFS] 3.3.5.4 / [MS-SMB2] 3.3.5.14 byte-range lock semantics as Windows
// and Samba implement them: two read locks never conflict; a read lock may
// stack on a write lock held by the *same* owner (same lock context and same
// open); anything else conflicts when the ranges overlap.

function overlaps(a, b) {
  if (a.length === 0n || b.length === 0n) return false;
  return a.offset < b.offset + b.length && b.offset < a.offset + a.length;
}
function sameOwner(a, b) { return a.open === b.open && a.pid === b.pid; }
function conflicts(existing, want) {
  if (!existing.exclusive && !want.exclusive) return false;
  if (existing.exclusive && !want.exclusive && sameOwner(existing, want)) return false;
  return overlaps(existing, want);
}

function lockRange(open, offset, length, exclusive, pid) {
  const want = { open, offset, length, exclusive, pid };
  for (const l of open.node.locks) if (conflicts(l, want)) return false;
  open.node.locks.push(want);
  return true;
}
// Blocked lock requests parked on a node until a conflicting lock goes away.
function addLockWaiter(node, fn) {
  if (!node.lockWaiters) node.lockWaiters = [];
  node.lockWaiters.push(fn);
  return () => {
    if (!node.lockWaiters) return;
    const i = node.lockWaiters.indexOf(fn);
    if (i >= 0) node.lockWaiters.splice(i, 1);
  };
}
function wakeLockWaiters(node) {
  if (!node.lockWaiters || node.lockWaiters.length === 0) return;
  const waiters = node.lockWaiters;
  node.lockWaiters = [];
  for (const fn of waiters) {
    try { if (!fn()) { if (!node.lockWaiters) node.lockWaiters = []; node.lockWaiters.push(fn); } }
    catch (e) { /* the waiter's connection went away */ }
  }
}

function unlockRange(open, offset, length, pid) {
  const node = open.node;
  const i = node.locks.findIndex((l) => l.open === open && l.pid === pid &&
    l.offset === offset && l.length === length);
  if (i < 0) return false;
  node.locks.splice(i, 1);
  wakeLockWaiters(node);
  return true;
}
// Records the failing range so the second identical failure can be reported as
// STATUS_FILE_LOCK_CONFLICT rather than STATUS_LOCK_NOT_GRANTED.
function lockFailureStatus(open, offset, length, pid) {
  const last = open.lastLockFailure;
  const same = last && last.offset === offset && last.length === length && last.pid === pid;
  open.lastLockFailure = { offset, length, pid };
  return same ? S.STATUS_FILE_LOCK_CONFLICT : S.STATUS_LOCK_NOT_GRANTED;
}
// Data-access check honouring the share's `strict locking` setting.
function lockConflict(open, offset, length, write) {
  if (length === 0n) return false;
  const strict = open.share.strictLocking;
  if (strict === 'no') return false;
  if (strict !== 'yes' && open.node.locks.length === 0) return false;
  const want = { open, offset, length, exclusive: !!write, pid: open.pid };
  for (const l of open.node.locks) {
    if (l.open === open) continue;
    if (!l.exclusive && !write) continue;
    if (overlaps(l, want)) return true;
  }
  return false;
}

// --- directory enumeration --------------------------------------------------

function listDirectory(share, rel, pattern, includeDots = true) {
  let entries;
  try { entries = fs.readdirSync(share.real(rel)); } catch (e) { fail(mapErrno(e)); }
  entries.sort((a, b) => (a.toLowerCase() < b.toLowerCase() ? -1 : a.toLowerCase() > b.toLowerCase() ? 1 : 0));
  const names = [];
  if (includeDots) { names.push('.'); names.push('..'); }
  names.push(...entries);
  const out = [];
  for (const n of names) {
    if (!V.wildcardMatch(pattern, n)) continue;
    const childRel = (n === '.' ) ? rel : (n === '..' ? path.dirname(rel === '' ? '' : rel).replace(/^\.$/, '') : (rel === '' ? n : rel + '/' + n));
    const st = share.stat(childRel);
    if (!st) continue;
    out.push({ name: n, rel: childRel, st });
  }
  return out;
}

module.exports = {
  createFile, closeOpen, applySticky, mapErrno, settleAttrs,
  touchWriteTime, flushWriteTime,
  lockRange, unlockRange, lockConflict, lockFailureStatus, listDirectory,
  addLockWaiter, wakeLockWaiters,
};
