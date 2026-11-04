'use strict';
// The share's view of the local filesystem: name resolution, opens, share
// modes, delete-on-close, DOS attributes, byte-range locks, directory
// enumeration and oplock/lease bookkeeping.
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');
const S = require('./ntstatus');
const SD = require('./sd');
const { A, SHARE, DISP, OPT, ACTION, ATTR } = require('./consts');

const EPOCH_DELTA = 116444736000000000n; // 1601-01-01 -> 1970-01-01 in 100ns
const toFileTime = (ms) => BigInt(Math.round(ms * 10000)) + EPOCH_DELTA;
const fromFileTime = (ft) => Number((BigInt(ft) - EPOCH_DELTA) / 10000n);

class SmbError extends Error {
  constructor(status) { super('NT ' + S.name(status)); this.status = status >>> 0; }
}
const fail = (status) => { throw new SmbError(status); };

function toUnix(name) {
  let s = String(name == null ? '' : name).replace(/\\/g, '/');
  while (s.startsWith('/')) s = s.slice(1);
  while (s.endsWith('/') && s.length > 1) s = s.slice(0, -1);
  return s;
}

function validName(rel, allowWildcard) {
  if (rel === '') return true;
  for (const part of rel.split('/')) {
    if (part === '' || part === '.' || part === '..') return false;
    for (const ch of part) {
      const c = ch.charCodeAt(0);
      if (c < 0x20) return false;
      if ('"<>|:'.includes(ch)) return false;
      if (!allowWildcard && (ch === '*' || ch === '?')) return false;
    }
  }
  return true;
}

// 8.3 name mangling, Samba's default "hash2" shape is not reproduced; an
// uppercase truncation is enough for the alternate-name information class.
// 8.3 alternate names: a name that already fits the 8.3 form is its own
// alternate name; longer ones get a generated short form. No specification
// fixes the generation algorithm.
function fits83(name) {
  if (name.length > 12) return false;
  const dot = name.indexOf('.');
  if (dot !== name.lastIndexOf('.')) return false;
  const base = dot < 0 ? name : name.slice(0, dot);
  const ext = dot < 0 ? '' : name.slice(dot + 1);
  if (base.length === 0 || base.length > 8 || ext.length > 3) return false;
  return !/[^A-Za-z0-9_^$~!#%&\-{}@'()]/.test(base + ext);
}
function mangle(name) {
  if (fits83(name)) return name;
  const dot = name.lastIndexOf('.');
  let base = dot > 0 ? name.slice(0, dot) : name;
  let ext = dot > 0 ? name.slice(dot + 1, dot + 4) : '';
  base = base.replace(/[^A-Za-z0-9_^$~!#%&\-{}@'()]/g, '_').toUpperCase();
  if (base.length > 8) base = base.slice(0, 6) + '~1';
  else base = base + '~1';
  if (base.length > 8) base = base.slice(0, 8);
  return ext ? base + '.' + ext.toUpperCase() : base;
}

function wildcardMatch(pattern, name) {
  if (pattern === '' || pattern === '*' || pattern === '*.*') return true;
  const p = pattern.toUpperCase(), n = name.toUpperCase();
  const m = (pi, ni) => {
    while (pi < p.length) {
      const c = p[pi];
      if (c === '*') {
        for (let k = ni; k <= n.length; k++) if (m(pi + 1, k)) return true;
        return false;
      }
      if (c === '<') { // DOS_STAR
        for (let k = ni; k <= n.length; k++) if (m(pi + 1, k)) return true;
        return false;
      }
      if (ni >= n.length) return (c === '>' || c === '"') ? m(pi + 1, ni) : false;
      if (c === '?' || c === '>') { pi++; ni++; continue; }
      if (c === '"') { if (n[ni] !== '.') return false; pi++; ni++; continue; }
      if (c !== n[ni]) return false;
      pi++; ni++;
    }
    return ni === n.length;
  };
  return m(0, 0);
}

// ---------------------------------------------------------------------------

class Node {
  constructor(key, rel) {
    this.key = key;
    this.rel = rel;
    this.opens = new Set();
    this.deletePending = false;
    this.oplock = null;      // { open, level }
    this.writeTimeTimer = null;
    this.savedWriteTime = null;
    this.writeTimeForced = false;
    this.leases = new Map(); // leaseKey(hex) -> { state, epoch, opens:Set }
    this.locks = [];         // { open, offset:BigInt, length:BigInt, exclusive, pid }
    this.notifies = [];      // pending CHANGE_NOTIFY requests on this directory
  }
}

class Share {
  constructor(name, root, opts = {}) {
    this.name = name;
    this.root = path.resolve(root);
    this.opts = opts;
    this.readOnly = !!opts.readOnly;
    this.nodes = new Map();
    this.dirCache = new Map();
    this.storeDosAttributes = !!opts.storeDosAttributes;
    this.mapArchive = opts.mapArchive === undefined ? true : !!opts.mapArchive;
    this.mapHidden = !!opts.mapHidden;
    this.mapSystem = !!opts.mapSystem;
    this.createMask = opts.createMask === undefined ? 0o744 : opts.createMask;
    this.writeTimeUpdateDelay = opts.writeTimeUpdateDelay === undefined ? 0 : opts.writeTimeUpdateDelay;
    this.ntAclSupport = opts.ntAclSupport === undefined ? true : !!opts.ntAclSupport;
    // The machine's own domain SID, in Samba's S-1-5-21-a-b-c shape. Derived
    // from the NetBIOS name so it is stable across restarts.
    const h = crypto.createHash('md5').update(opts.machineName || 'SMBSRV').digest();
    this.domainSid = opts.domainSid
      || ('S-1-5-21-' + [0, 4, 8].map((o) => h.readUInt32LE(o) & 0x7fffffff).join('-'));
    // smb.conf(5) `algorithmic rid base`; Samba refuses a value below the first
    // non-well-known RID and silently uses 1000 instead (measured: a base of 500
    // still gives uid 1000 -> RID 3000).
    const ridBase = opts.algorithmicRidBase === undefined ? 1000 : opts.algorithmicRidBase;
    this.algorithmicRidBase = Number.isFinite(ridBase) && ridBase >= 1000 ? ridBase : 1000;
    // uids the account database holds, and the uid of `guest account`.
    this.accountUids = opts.accountUids || new Set();
    this.guestUid = opts.guestUid === undefined ? null : opts.guestUid;
    this.aclMapFullControl = opts.aclMapFullControl === undefined
      ? true : !!opts.aclMapFullControl;
    this.directoryMask = opts.directoryMask === undefined ? 0o755 : opts.directoryMask;
    this.meta = new Map();   // rel(lowercase) -> { attrs, createTime, ea:Map, sd }
    this.nextFileId = 1n;
  }

  // --- name resolution ----------------------------------------------------
  real(rel) { return rel === '' ? this.root : path.join(this.root, rel); }

  // Case-insensitive lookup needs the directory's entry list. Caching it per
  // directory turns a create-many-files workload from O(n^2) into O(n): the
  // cache is trusted while the directory's mtime is unchanged, and this server's
  // own creates and deletes patch it in place.
  dirIndex(dirRel) {
    let st;
    try { st = fs.statSync(this.real(dirRel)); } catch (e) { return null; }
    const key = dirRel.toLowerCase();
    const hit = this.dirCache.get(key);
    if (hit && hit.mtimeMs === st.mtimeMs && hit.size === st.size) return hit;
    let entries;
    try { entries = fs.readdirSync(this.real(dirRel)); } catch (e) { return null; }
    const map = new Map();
    for (const e of entries) map.set(e.toLowerCase(), e);
    const rec = { map, mtimeMs: st.mtimeMs, size: st.size };
    if (this.dirCache.size > 64) this.dirCache.clear();
    this.dirCache.set(key, rec);
    return rec;
  }

  noteEntry(rel, added) {
    const dirRel = rel.includes('/') ? rel.slice(0, rel.lastIndexOf('/')) : '';
    const base = rel.includes('/') ? rel.slice(rel.lastIndexOf('/') + 1) : rel;
    const rec = this.dirCache.get(dirRel.toLowerCase());
    if (!rec) return;
    if (added) rec.map.set(base.toLowerCase(), base);
    else rec.map.delete(base.toLowerCase());
    try {
      const st = fs.statSync(this.real(dirRel));
      rec.mtimeMs = st.mtimeMs;
      rec.size = st.size;
    } catch (e) { this.dirCache.delete(dirRel.toLowerCase()); }
  }

  resolve(name, { wildcard = false } = {}) {
    const rel = toUnix(name);
    if (!validName(rel, wildcard)) fail(S.STATUS_OBJECT_NAME_INVALID);
    if (rel === '') return '';
    const parts = rel.split('/');
    let cur = '';
    for (let i = 0; i < parts.length; i++) {
      const want = parts[i];
      const dir = this.real(cur);
      void dir;
      let found = null;
      const direct = path.join(dir, want);
      if (fs.existsSync(direct)) found = want;
      else {
        const idx = this.dirIndex(cur);
        found = idx ? (idx.map.get(want.toLowerCase()) || null) : null;
      }
      if (found === null) {
        if (i === parts.length - 1) return cur === '' ? want : cur + '/' + want;
        fail(S.STATUS_OBJECT_PATH_NOT_FOUND);
      }
      cur = cur === '' ? found : cur + '/' + found;
    }
    return cur;
  }

  stat(rel) {
    try { return fs.lstatSync(this.real(rel)); } catch (e) { return null; }
  }

  metaFor(rel) {
    const k = rel.toLowerCase();
    if (!this.meta.has(k)) this.meta.set(k, this.newMeta(rel));
    const m = this.meta.get(k);
    m.rel = rel;
    return m;
  }

  // The per-path record.  `sd` is an accessor, not a plain slot: storing a
  // descriptor on an object is the VFS layer's decision, and `nt acl support`
  // with no `acl_xattr` module does not store what the client sent -- see
  // storeSecurityDescriptor().  Assigning a serialised descriptor therefore
  // yields the descriptor the server will actually report, and assigning null
  // clears it.
  newMeta(rel) {
    const share = this;
    const rec = { attrs: 0, createTime: null, ea: new Map(), rel };
    let stored = null;
    Object.defineProperty(rec, 'sd', {
      enumerable: true,
      configurable: true,
      get() { return stored; },
      set(buf) {
        if (!buf) { stored = null; return; }
        const next = share.storeSecurityDescriptor(rec.rel, buf, stored);
        stored = next;
      },
    });
    return rec;
  }

  // What gets stored when a client sets a descriptor on `rel`.
  //
  // The reference server has no place to keep an NT ACL: `nt acl support = yes`
  // converts the DACL into a POSIX ACL and re-synthesises a descriptor from that
  // ACL on the next read, so a set/get round trip is lossy in the exact way
  // SD.applyPosixAcl models.  Two consequences are visible on the wire:
  //   * a DACL that says no more than the permission bits already say is not
  //     stored at all, and the object stays mode-derived with three ACEs;
  //   * anything else comes back as the five-ACE POSIX-ACL shape.
  storeSecurityDescriptor(rel, buf, previous) {
    let incoming;
    try {
      incoming = Buffer.isBuffer(buf) ? SD.parseSecurityDescriptor(buf) : buf;
    } catch (e) {
      return previous === undefined ? null : previous;
    }
    const st = this.stat(rel);
    if (!st) return Buffer.isBuffer(buf) ? Buffer.from(buf) : SD.buildSecurityDescriptor(incoming);
    const ownerSid = incoming.owner || this.ownerSidFor(st.uid);
    const groupSid = incoming.group || this.groupSidFor(st.gid);
    const isContainer = st.isDirectory();
    const mode = st.mode & 0o777;
    // A DACL identical to the one the permission bits already imply leaves no
    // POSIX ACL behind, so nothing is stored and the object stays mode-derived.
    if (this.daclIsModeDerived(incoming, ownerSid, groupSid, mode, isContainer)) {
      return previous === undefined ? null : previous;
    }
    const r = SD.applyPosixAcl(incoming, {
      ownerSid, groupSid, mode, isContainer,
      aclMapFullControl: this.aclMapFullControl,
    });
    if (!r.applied) return previous === undefined ? null : previous;
    return SD.buildSecurityDescriptor(r.sd);
  }

  // Whether `sd`'s DACL is exactly the one `mode` already implies.
  daclIsModeDerived(sd, ownerSid, groupSid, mode, isContainer) {
    if (!sd || !sd.dacl || !sd.dacl.aces) return false;
    const want = SD.defaultSecurityDescriptor(ownerSid, groupSid, mode,
      { isContainer, aclMapFullControl: this.aclMapFullControl }).dacl.aces;
    const got = sd.dacl.aces;
    if (got.length !== want.length) return false;
    for (let i = 0; i < want.length; i++) {
      if (got[i].type !== want[i].type || (got[i].flags & 0xff) !== (want[i].flags & 0xff)) return false;
      if ((got[i].mask >>> 0) !== (want[i].mask >>> 0)) return false;
      if (!SD.sidEqual(got[i].sid, want[i].sid)) return false;
    }
    return true;
  }

  moveMeta(from, to) {
    const a = from.toLowerCase(), b = to.toLowerCase();
    if (this.meta.has(a)) {
      const m = this.meta.get(a);
      m.rel = to;
      this.meta.set(b, m);
      this.meta.delete(a);
    }
  }

  // DOS attributes are carried in the UNIX permission bits, the mapping
  // smb.conf(5) describes under `map archive`, `map hidden`, `map system` and
  // `store dos attributes`.
  attributes(rel, st) {
    const s = st || this.stat(rel);
    if (!s) return 0;
    let a = 0;
    const m = this.storeDosAttributes ? this.meta.get(rel.toLowerCase()) : null;
    const stored = (m && m.attrs) ? m : null;
    if (stored) {
      a = stored.attrs & (ATTR.READONLY | ATTR.HIDDEN | ATTR.SYSTEM | ATTR.ARCHIVE |
                          ATTR.TEMPORARY | ATTR.OFFLINE | ATTR.NOT_CONTENT_INDEXED |
                          ATTR.SPARSE_FILE);
    } else if (!s.isDirectory()) {
      if (this.mapArchive && (s.mode & 0o100)) a |= ATTR.ARCHIVE;
      if (this.mapHidden && (s.mode & 0o001)) a |= ATTR.HIDDEN;
      if (this.mapSystem && (s.mode & 0o010)) a |= ATTR.SYSTEM;
    }
    // The write bit only speaks for FILE_ATTRIBUTE_READONLY when no attribute
    // set is stored for this file; otherwise the stored set is the whole truth.
    if (!stored && !(s.mode & 0o200)) a |= ATTR.READONLY;
    if (s.isDirectory()) a |= ATTR.DIRECTORY;
    if (a === 0) a = ATTR.NORMAL;
    return a >>> 0;
  }

  // The UNIX mode a file carrying these DOS attributes gets, per the same
  // mapping; `create mask` and `directory mask` clamp it.
  modeFor(attrs, isDir) {
    let mode = isDir ? 0o777 : 0o666;
    if (this.mapArchive && (attrs & ATTR.ARCHIVE)) mode |= 0o100;
    if (this.mapHidden && (attrs & ATTR.HIDDEN)) mode |= 0o001;
    if (this.mapSystem && (attrs & ATTR.SYSTEM)) mode |= 0o010;
    if (attrs & ATTR.READONLY) mode &= ~0o222;
    mode &= isDir ? this.directoryMask : this.createMask;
    return mode;
  }

  // Re-map a file's permission bits so its DOS attributes read back as `attrs`.
  applyAttributes(rel, attrs) {
    const s = this.stat(rel);
    if (!s) return;
    // FILE_ATTRIBUTE_NORMAL is not a stored attribute: asking for it alone
    // clears the stored set and leaves the permission-bit mapping to speak.
    let settable = attrs & (ATTR.READONLY | ATTR.HIDDEN | ATTR.SYSTEM | ATTR.ARCHIVE |
                            ATTR.TEMPORARY | ATTR.OFFLINE | ATTR.NOT_CONTENT_INDEXED);
    if (settable === 0) settable = ATTR.ARCHIVE;
    if (s.isDirectory()) settable |= ATTR.DIRECTORY;
    if (this.storeDosAttributes) {
      // `store dos attributes = yes` keeps hidden/system/archive out of the
      // permission bits, so changing one does not change the file's security
      // descriptor. FILE_ATTRIBUTE_READONLY still has to reach the write bits,
      // because it is what refuses a write or a delete.
      this.metaFor(rel).attrs = settable;
      return;
    }
    let mode = s.mode & 0o7777;
    if (this.mapArchive) mode = (settable & ATTR.ARCHIVE) ? (mode | 0o100) : (mode & ~0o100);
    if (this.mapHidden) mode = (settable & ATTR.HIDDEN) ? (mode | 0o001) : (mode & ~0o001);
    if (this.mapSystem) mode = (settable & ATTR.SYSTEM) ? (mode | 0o010) : (mode & ~0o010);
    mode = (settable & ATTR.READONLY) ? (mode & ~0o222) : (mode | 0o200);
    try { fs.chmodSync(this.real(rel), mode); } catch (e) {}
  }

  times(rel, st) {
    const s = st || this.stat(rel);
    if (!s) return null;
    const m = this.metaFor(rel);
    const create = m.createTime !== null ? m.createTime : (s.birthtimeMs || s.ctimeMs);
    return {
      create: toFileTime(create), access: toFileTime(s.atimeMs),
      write: toFileTime(s.mtimeMs), change: toFileTime(s.ctimeMs),
    };
  }

  // An existing node for this path, without creating one.
  findNode(rel, st) {
    const s = st || this.stat(rel);
    const key = s ? `${s.dev}:${s.ino}` : 'p:' + rel.toLowerCase();
    return this.nodes.get(key) || null;
  }

  // The SID a uid maps to.  Three cases, all measured against Samba 4.17.12
  // (SD-NOTES.md §2.4):
  //   * the uid of `guest account` gets the domain's well-known guest RID, 501;
  //   * a uid the account database holds gets the algorithmic domain RID,
  //     `2 * uid + algorithmic rid base`;
  //   * every other uid falls back to the UNIX-user authority, S-1-22-1-<uid>,
  //     which is not a domain SID at all.
  ownerSidFor(uid) {
    if (this.guestUid !== null && uid === this.guestUid) return this.domainSid + '-501';
    if (this.accountUids.has(uid)) {
      return this.domainSid + '-' + (2 * uid + this.algorithmicRidBase);
    }
    return SD.UNIX_USER_PREFIX + uid;
  }
  // A gid is always the UNIX-group authority; it never gets a domain RID.
  groupSidFor(gid) { return SD.UNIX_GROUP_PREFIX + gid; }

  // The TOKEN an authenticated account carries on this share: its own SID, its
  // primary group's SID, and the two group memberships every session has.
  tokenFor(user) {
    if (!user) return SD.makeToken(null, [SD.WELL_KNOWN.EVERYONE], {});
    const gid = user.gid === undefined || user.gid === null ? user.uid : user.gid;
    return SD.makeToken(this.ownerSidFor(user.uid), [
      this.groupSidFor(gid),
      SD.WELL_KNOWN.EVERYONE,
      SD.WELL_KNOWN.AUTHENTICATED_USERS,
    ], {});
  }

  // The descriptor a file carries: the one that was set on it, or the one its
  // permission bits describe.
  securityDescriptorFor(rel, st) {
    const stt = st || this.stat(rel);
    if (!stt) return null;
    const m = this.meta.get(rel.toLowerCase());
    if (m && m.sd) {
      try { return SD.parseSecurityDescriptor(m.sd); } catch (e) { /* fall through */ }
    }
    return SD.defaultSecurityDescriptor(this.ownerSidFor(stt.uid),
                                        this.groupSidFor(stt.gid),
                                        stt.mode & 0o777,
                                        { isContainer: stt.isDirectory(),
                                          aclMapFullControl: this.aclMapFullControl });
  }

  // Merge the parts named by `additionalInformation` into the object's current
  // descriptor and store the result; storeSecurityDescriptor() decides what
  // "store" means.
  setSecurityDescriptor(rel, buf, additionalInformation) {
    const st = this.stat(rel);
    if (!st) return;
    const cur = this.securityDescriptorFor(rel, st);
    const incoming = SD.parseSecurityDescriptor(buf);
    const merged = SD.applySecurityInformation(cur, incoming, additionalInformation);
    merged.control |= SD.SE.SE_DACL_PROTECTED | SD.SE.SE_DACL_PRESENT;
    this.metaFor(rel).sd = SD.buildSecurityDescriptor(merged);
  }

  // --- the access check ---------------------------------------------------

  // Everything `token` may be granted on `rel`.  This is [MS-DTYP] 2.5.3.2 over
  // the object's own descriptor plus the two rights the reference server adds
  // from outside it (SD-NOTES.md §4.2):
  //   * FILE_READ_ATTRIBUTES, which every caller that can reach the object has;
  //   * DELETE, which comes from FILE_DELETE_CHILD on the *parent directory* --
  //     POSIX deletes a name from its directory, not the inode.
  effectiveAccess(rel, token, st) {
    const stt = st || this.stat(rel);
    if (!stt) return 0;
    const isDir = stt.isDirectory();
    const sd = this.securityDescriptorFor(rel, stt);
    const gm = isDir ? SD.GENERIC_MAPPING_DIRECTORY : SD.GENERIC_MAPPING_FILE;
    const r = SD.accessCheck(sd, token, SD.AM.MAXIMUM_ALLOWED, { genericMapping: gm });
    let granted = (r.status === SD.STATUS_SUCCESS ? r.granted : 0) | A.FILE_READ_ATTRIBUTES;
    if (this.parentGrantsDeleteChild(rel, token)) granted |= A.DELETE;
    return granted >>> 0;
  }

  // FILE_DELETE_CHILD on the directory `rel` lives in.  The share root itself
  // has no parent inside the share; the share's own permission bits answer for
  // it, which is what the reference server does.
  parentGrantsDeleteChild(rel, token) {
    const dirRel = rel.includes('/') ? rel.slice(0, rel.lastIndexOf('/')) : '';
    const st = this.stat(dirRel);
    if (!st) return false;
    const sd = this.securityDescriptorFor(dirRel, st);
    const r = SD.accessCheck(sd, token, SD.AM.MAXIMUM_ALLOWED,
                             { genericMapping: SD.GENERIC_MAPPING_DIRECTORY });
    return r.status === SD.STATUS_SUCCESS && (r.granted & A.FILE_DELETE_CHILD) !== 0;
  }

  // What a CREATE asking for `desiredAccess` gets: { granted, status }.
  // MAXIMUM_ALLOWED resolves to everything the descriptor allows, which is also
  // the answer the MxAc create context has to report.
  checkAccess(rel, token, desiredAccess, st) {
    const stt = st || this.stat(rel);
    if (!stt) return { granted: 0, status: S.STATUS_OBJECT_NAME_NOT_FOUND };
    const isDir = stt.isDirectory();
    const gm = isDir ? SD.GENERIC_MAPPING_DIRECTORY : SD.GENERIC_MAPPING_FILE;
    const want = SD.mapGeneric(desiredAccess, gm);
    const effective = this.effectiveAccess(rel, token, stt);
    if (want & SD.AM.MAXIMUM_ALLOWED) {
      const rest = (want & ~SD.AM.MAXIMUM_ALLOWED) >>> 0;
      if (rest & ~effective) return { granted: 0, status: S.STATUS_ACCESS_DENIED };
      return effective === 0
        ? { granted: 0, status: S.STATUS_ACCESS_DENIED }
        : { granted: effective, status: S.STATUS_SUCCESS };
    }
    if (want & ~effective) return { granted: 0, status: S.STATUS_ACCESS_DENIED };
    return { granted: want >>> 0, status: S.STATUS_SUCCESS };
  }

  // The MxAc create context ([MS-SMB2] 2.2.13.2.5) is the same answer.
  maximalAccess(rel, token, st) {
    return this.effectiveAccess(rel, token, st);
  }

  node(rel, st) {
    const s = st || this.stat(rel);
    const key = s ? `${s.dev}:${s.ino}` : 'p:' + rel.toLowerCase();
    let n = this.nodes.get(key);
    if (!n) { n = new Node(key, rel); this.nodes.set(key, n); }
    n.rel = rel;
    return n;
  }
  dropNode(n) { if (n.opens.size === 0 && n.notifies.length === 0) this.nodes.delete(n.key); }
}

// ---------------------------------------------------------------------------

class Open {
  constructor(share, rel, node, opts) {
    this.share = share;
    this.rel = rel;
    this.node = node;
    this.access = opts.access >>> 0;
    this.shareAccess = opts.shareAccess >>> 0;
    this.isDir = opts.isDir;
    this.fd = opts.fd === undefined ? null : opts.fd;
    this.deleteOnClose = !!opts.deleteOnClose;
    this.createOptions = opts.createOptions >>> 0;
    this.fileId = share.nextFileId++;
    this.oplockLevel = 0;
    this.lease = null;
    this.durable = null;
    this.dirEnum = null;
    this.session = opts.session || null;
    this.tree = opts.tree || null;
    this.closed = false;
    this.pid = opts.pid || 0;
    this.pendingWriteTime = null;
    this.sticky = { write: null, access: null, change: null, create: null, attrs: null };
  }
}

const GENERIC_MAP = (m) => {
  let a = m >>> 0;
  if (a & A.GENERIC_READ) a |= A.FILE_READ_DATA | A.FILE_READ_EA | A.FILE_READ_ATTRIBUTES | A.READ_CONTROL | A.SYNCHRONIZE;
  if (a & A.GENERIC_WRITE) a |= A.FILE_WRITE_DATA | A.FILE_APPEND_DATA | A.FILE_WRITE_EA | A.FILE_WRITE_ATTRIBUTES | A.READ_CONTROL | A.SYNCHRONIZE;
  if (a & A.GENERIC_EXECUTE) a |= A.FILE_EXECUTE | A.FILE_READ_ATTRIBUTES | A.READ_CONTROL | A.SYNCHRONIZE;
  if (a & A.GENERIC_ALL) a |= 0x001f01ff;
  a &= ~(A.GENERIC_READ | A.GENERIC_WRITE | A.GENERIC_EXECUTE | A.GENERIC_ALL);
  return a >>> 0;
};

const WRITE_BITS = A.FILE_WRITE_DATA | A.FILE_APPEND_DATA;
const READ_BITS = A.FILE_READ_DATA | A.FILE_EXECUTE;

function shareConflict(node, access, shareAccess, self) {
  for (const o of node.opens) {
    if (o === self) continue;
    if ((access & READ_BITS) && !(o.shareAccess & SHARE.READ)) return true;
    if ((access & WRITE_BITS) && !(o.shareAccess & SHARE.WRITE)) return true;
    if ((access & A.DELETE) && !(o.shareAccess & SHARE.DELETE)) return true;
    if ((o.access & READ_BITS) && !(shareAccess & SHARE.READ)) return true;
    if ((o.access & WRITE_BITS) && !(shareAccess & SHARE.WRITE)) return true;
    if ((o.access & A.DELETE) && !(shareAccess & SHARE.DELETE)) return true;
  }
  return false;
}

module.exports = {
  Share, Open, Node, SmbError, fail, toUnix, validName, mangle, fits83, wildcardMatch,
  toFileTime, fromFileTime, GENERIC_MAP, shareConflict, EPOCH_DELTA,
};
