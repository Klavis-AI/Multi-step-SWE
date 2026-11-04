'use strict';
// Windows security descriptors and the access-check model: [MS-DTYP] 2.4.2 (SID),
// 2.4.3 (ACCESS_MASK), 2.4.4 (ACE), 2.4.5 (ACL), 2.4.6 (SECURITY_DESCRIPTOR),
// 2.5.3.2 (access check) and 2.5.3.4 (inheritance).  Pure data model: no I/O, no
// SMB knowledge, no dependencies outside this file.

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// [MS-DTYP] 2.4.4.1 AceType.
const ACE_TYPE = {
  ACCESS_ALLOWED: 0x00,
  ACCESS_DENIED: 0x01,
  SYSTEM_AUDIT: 0x02,
  SYSTEM_ALARM: 0x03,
  ACCESS_ALLOWED_COMPOUND: 0x04,
  ACCESS_ALLOWED_OBJECT: 0x05,
  ACCESS_DENIED_OBJECT: 0x06,
  SYSTEM_AUDIT_OBJECT: 0x07,
  SYSTEM_ALARM_OBJECT: 0x08,
  ACCESS_ALLOWED_CALLBACK: 0x09,
  ACCESS_DENIED_CALLBACK: 0x0a,
  ACCESS_ALLOWED_CALLBACK_OBJECT: 0x0b,
  ACCESS_DENIED_CALLBACK_OBJECT: 0x0c,
  SYSTEM_AUDIT_CALLBACK: 0x0d,
  SYSTEM_ALARM_CALLBACK: 0x0e,
  SYSTEM_AUDIT_CALLBACK_OBJECT: 0x0f,
  SYSTEM_ALARM_CALLBACK_OBJECT: 0x10,
  SYSTEM_MANDATORY_LABEL: 0x11,
  SYSTEM_RESOURCE_ATTRIBUTE: 0x12,
  SYSTEM_SCOPED_POLICY_ID: 0x13,
};

// Types whose body is Mask + Flags + ObjectType + InheritedObjectType + Sid.
const OBJECT_ACE_TYPES = new Set([
  ACE_TYPE.ACCESS_ALLOWED_OBJECT, ACE_TYPE.ACCESS_DENIED_OBJECT,
  ACE_TYPE.SYSTEM_AUDIT_OBJECT, ACE_TYPE.SYSTEM_ALARM_OBJECT,
  ACE_TYPE.ACCESS_ALLOWED_CALLBACK_OBJECT, ACE_TYPE.ACCESS_DENIED_CALLBACK_OBJECT,
  ACE_TYPE.SYSTEM_AUDIT_CALLBACK_OBJECT, ACE_TYPE.SYSTEM_ALARM_CALLBACK_OBJECT,
]);
const ALLOW_ACE_TYPES = new Set([
  ACE_TYPE.ACCESS_ALLOWED, ACE_TYPE.ACCESS_ALLOWED_CALLBACK,
]);
const DENY_ACE_TYPES = new Set([
  ACE_TYPE.ACCESS_DENIED, ACE_TYPE.ACCESS_DENIED_CALLBACK,
]);

// [MS-DTYP] 2.4.4.1 AceFlags.
const ACE_FLAG = {
  OBJECT_INHERIT_ACE: 0x01,
  CONTAINER_INHERIT_ACE: 0x02,
  NO_PROPAGATE_INHERIT_ACE: 0x04,
  INHERIT_ONLY_ACE: 0x08,
  INHERITED_ACE: 0x10,
  SUCCESSFUL_ACCESS_ACE_FLAG: 0x40,
  FAILED_ACCESS_ACE_FLAG: 0x80,
};

// [MS-DTYP] 2.4.4.3 Flags (object ACEs).
const ACE_OBJECT_TYPE_PRESENT = 0x00000001;
const ACE_INHERITED_OBJECT_TYPE_PRESENT = 0x00000002;

// [MS-DTYP] 2.4.5 AclRevision.
const ACL_REVISION = 0x02;
const ACL_REVISION_DS = 0x04;

// [MS-DTYP] 2.4.6 Control.  The section's bit diagram numbers bits most
// significant first, so SR (bit 0 of the diagram) is 0x8000 and OD (bit 15) is
// 0x0001.  Confirmed against Samba's decoder: a descriptor with Control 0x9004
// is printed by `ndrdump security security_descriptor` as
// DACL_PRESENT | DACL_PROTECTED | SELF_RELATIVE.
const SE = {
  SE_OWNER_DEFAULTED: 0x0001,
  SE_GROUP_DEFAULTED: 0x0002,
  SE_DACL_PRESENT: 0x0004,
  SE_DACL_DEFAULTED: 0x0008,
  SE_SACL_PRESENT: 0x0010,
  SE_SACL_DEFAULTED: 0x0020,
  SE_DACL_TRUSTED: 0x0040,
  SE_SERVER_SECURITY: 0x0080,
  SE_DACL_AUTO_INHERIT_REQ: 0x0100,
  SE_SACL_AUTO_INHERIT_REQ: 0x0200,
  SE_DACL_AUTO_INHERITED: 0x0400,
  SE_SACL_AUTO_INHERITED: 0x0800,
  SE_DACL_PROTECTED: 0x1000,
  SE_SACL_PROTECTED: 0x2000,
  SE_RM_CONTROL_VALID: 0x4000,
  SE_SELF_RELATIVE: 0x8000,
};

// SECURITY_INFORMATION ([MS-SMB2] 2.2.37 AdditionalInformation / [MS-DTYP] 2.4.7).
const SI = {
  OWNER_SECURITY_INFORMATION: 0x00000001,
  GROUP_SECURITY_INFORMATION: 0x00000002,
  DACL_SECURITY_INFORMATION: 0x00000004,
  SACL_SECURITY_INFORMATION: 0x00000008,
  LABEL_SECURITY_INFORMATION: 0x00000010,
  ATTRIBUTE_SECURITY_INFORMATION: 0x00000020,
  SCOPE_SECURITY_INFORMATION: 0x00000040,
  BACKUP_SECURITY_INFORMATION: 0x00010000,
};

// ACCESS_MASK ([MS-DTYP] 2.4.3; file-specific bits from [MS-SMB2] 2.2.13.1.1/.2).
const AM = {
  FILE_READ_DATA: 0x00000001,
  FILE_WRITE_DATA: 0x00000002,
  FILE_APPEND_DATA: 0x00000004,
  FILE_READ_EA: 0x00000008,
  FILE_WRITE_EA: 0x00000010,
  FILE_EXECUTE: 0x00000020,
  FILE_DELETE_CHILD: 0x00000040,
  FILE_READ_ATTRIBUTES: 0x00000080,
  FILE_WRITE_ATTRIBUTES: 0x00000100,
  DELETE: 0x00010000,
  READ_CONTROL: 0x00020000,
  WRITE_DAC: 0x00040000,
  WRITE_OWNER: 0x00080000,
  SYNCHRONIZE: 0x00100000,
  ACCESS_SYSTEM_SECURITY: 0x01000000,
  MAXIMUM_ALLOWED: 0x02000000,
  GENERIC_ALL: 0x10000000,
  GENERIC_EXECUTE: 0x20000000,
  GENERIC_WRITE: 0x40000000,
  GENERIC_READ: 0x80000000,
};
AM.STANDARD_RIGHTS_ALL = 0x001f0000;
AM.FILE_ALL_ACCESS = 0x001f01ff;
// The three composites [MS-SMB2] 2.2.13.1.1 spells out for GENERIC_*.
AM.FILE_GENERIC_READ =
  AM.READ_CONTROL | AM.FILE_READ_DATA | AM.FILE_READ_ATTRIBUTES | AM.FILE_READ_EA | AM.SYNCHRONIZE;      // 0x00120089
AM.FILE_GENERIC_WRITE =
  AM.READ_CONTROL | AM.FILE_WRITE_DATA | AM.FILE_WRITE_ATTRIBUTES | AM.FILE_WRITE_EA
  | AM.FILE_APPEND_DATA | AM.SYNCHRONIZE;                                                                // 0x00120116
AM.FILE_GENERIC_EXECUTE =
  AM.READ_CONTROL | AM.FILE_READ_ATTRIBUTES | AM.FILE_EXECUTE | AM.SYNCHRONIZE;                          // 0x001200a0

// GENERIC_MAPPING ([MS-DTYP] 2.4.3): what GR/GW/GX/GA expand to for this
// resource manager.  Files and directories differ only in that a directory's
// "write" also carries FILE_DELETE_CHILD.
const GENERIC_MAPPING_FILE = Object.freeze({
  GenericRead: AM.FILE_GENERIC_READ,
  GenericWrite: AM.FILE_GENERIC_WRITE,
  GenericExecute: AM.FILE_GENERIC_EXECUTE,
  GenericAll: AM.FILE_ALL_ACCESS,
});
const GENERIC_MAPPING_DIRECTORY = Object.freeze({
  GenericRead: AM.FILE_GENERIC_READ,
  GenericWrite: AM.FILE_GENERIC_WRITE | AM.FILE_DELETE_CHILD,
  GenericExecute: AM.FILE_GENERIC_EXECUTE,
  GenericAll: AM.FILE_ALL_ACCESS,
});

// Well-known SIDs ([MS-DTYP] 2.4.2.4) this module refers to by name.
const WELL_KNOWN = {
  NULL: 'S-1-0-0',
  EVERYONE: 'S-1-1-0',
  CREATOR_OWNER: 'S-1-3-0',
  CREATOR_GROUP: 'S-1-3-1',
  OWNER_RIGHTS: 'S-1-3-4',
  NT_AUTHORITY: 'S-1-5',
  PRINCIPAL_SELF: 'S-1-5-10',
  AUTHENTICATED_USERS: 'S-1-5-11',
  LOCAL_SYSTEM: 'S-1-5-18',
  BUILTIN_ADMINISTRATORS: 'S-1-5-32-544',
  BUILTIN_USERS: 'S-1-5-32-545',
};
// Samba's UNIX identity authority (S-1-22-1-<uid>, S-1-22-2-<gid>); it is not
// in [MS-DTYP] 2.4.2.4 but the reference server emits it as the group SID.
const UNIX_USER_PREFIX = 'S-1-22-1-';
const UNIX_GROUP_PREFIX = 'S-1-22-2-';

const STATUS_SUCCESS = 0x00000000;
const STATUS_ACCESS_DENIED = 0xc0000022;
const STATUS_INVALID_SECURITY_DESCR = 0xc0000079;

class SdError extends Error {}
function bad(msg) { throw new SdError(msg); }

// ---------------------------------------------------------------------------
// SID ([MS-DTYP] 2.4.2)
// ---------------------------------------------------------------------------

// A SID is modelled as { revision, authority, subAuthorities: [uint32] }.
// `authority` is the 6-byte IdentifierAuthority as a Number (< 2^48, safe).

function sidFromString(s) {
  if (typeof s !== 'string') bad('sidFromString: not a string');
  const parts = s.trim().split('-');
  if (parts.length < 3 || parts[0] !== 'S' && parts[0] !== 's') bad('bad SID string: ' + s);
  const revision = Number(parts[1]);
  if (!Number.isInteger(revision) || revision < 0 || revision > 255) bad('bad SID revision: ' + s);
  let authority;
  const a = parts[2];
  if (/^0[xX][0-9a-fA-F]{1,12}$/.test(a)) authority = Number.parseInt(a.slice(2), 16);
  else if (/^[0-9]+$/.test(a)) authority = Number(a);
  else bad('bad SID identifier authority: ' + s);
  if (!Number.isSafeInteger(authority) || authority < 0 || authority > 0xffffffffffff) {
    bad('SID identifier authority out of range: ' + s);
  }
  const subAuthorities = [];
  for (let i = 3; i < parts.length; i++) {
    if (!/^[0-9]+$/.test(parts[i])) bad('bad SID sub-authority: ' + s);
    const v = Number(parts[i]);
    if (!Number.isInteger(v) || v < 0 || v > 0xffffffff) bad('SID sub-authority out of range: ' + s);
    subAuthorities.push(v >>> 0);
  }
  if (subAuthorities.length > 15) bad('SID has more than 15 sub-authorities: ' + s);
  return { revision, authority, subAuthorities };
}

function sidToString(sid) {
  const o = toSidObject(sid);
  const auth = o.authority > 0xffffffff
    ? '0x' + o.authority.toString(16).padStart(12, '0')
    : String(o.authority);
  let s = 'S-' + o.revision + '-' + auth;
  for (const sub of o.subAuthorities) s += '-' + (sub >>> 0);
  return s;
}

// Parse one SID out of `buf` at `off`.  Returns the SID object with a
// non-enumerable-free `size` field giving its wire length.
function parseSid(buf, off = 0) {
  if (!Buffer.isBuffer(buf)) bad('parseSid: not a Buffer');
  if (off < 0 || off + 8 > buf.length) bad('parseSid: truncated SID header at ' + off);
  const revision = buf.readUInt8(off);
  const count = buf.readUInt8(off + 1);
  if (revision !== 1) bad('parseSid: SID Revision is ' + revision + ', MUST be 1');
  if (count > 15) bad('parseSid: SubAuthorityCount ' + count + ' exceeds 15');
  const size = 8 + 4 * count;
  if (off + size > buf.length) bad('parseSid: truncated SID body at ' + off);
  // IdentifierAuthority is 6 bytes, big-endian ([MS-DTYP] 2.4.2.2).
  const authority = buf.readUIntBE(off + 2, 6);
  const subAuthorities = [];
  for (let i = 0; i < count; i++) subAuthorities.push(buf.readUInt32LE(off + 8 + 4 * i) >>> 0);
  const sid = { revision, authority, subAuthorities, size };
  sid.text = sidToString(sid);
  return sid;
}

function toSidObject(sid) {
  if (typeof sid === 'string') return sidFromString(sid);
  if (Buffer.isBuffer(sid)) return parseSid(sid, 0);
  if (sid && typeof sid === 'object' && Array.isArray(sid.subAuthorities)) return sid;
  bad('not a SID: ' + JSON.stringify(sid));
  return null;
}

// Serialise a SID.  Accepts the textual form, a SID object or a raw Buffer.
function buildSid(sid) {
  const o = toSidObject(sid);
  const n = o.subAuthorities.length;
  if (n > 15) bad('buildSid: more than 15 sub-authorities');
  const buf = Buffer.alloc(8 + 4 * n);
  buf.writeUInt8(o.revision === undefined ? 1 : o.revision, 0);
  buf.writeUInt8(n, 1);
  buf.writeUIntBE(o.authority, 2, 6);
  for (let i = 0; i < n; i++) buf.writeUInt32LE(o.subAuthorities[i] >>> 0, 8 + 4 * i);
  return buf;
}

function sidEqual(a, b) {
  if (a === null || a === undefined || b === null || b === undefined) return false;
  return sidToString(a) === sidToString(b);
}

const sidLength = (sid) => 8 + 4 * toSidObject(sid).subAuthorities.length;

// ---------------------------------------------------------------------------
// ACE ([MS-DTYP] 2.4.4) and ACL ([MS-DTYP] 2.4.5)
// ---------------------------------------------------------------------------

// An ACE is modelled as
//   { type, flags, mask, sid (textual),
//     objectFlags?, objectType?, inheritedObjectType?,   // object ACE types
//     extra?: Buffer }                                   // trailing/callback data
// `extra` carries the bytes the AceSize covers beyond the fields above; 2.4.4.1
// says AceSize MAY exceed the sum of the individual fields and that the surplus
// "is not interpreted and MUST be ignored" -- we keep it so a round trip is
// byte-identical.

function parseAce(buf, off) {
  if (off + 4 > buf.length) bad('parseAce: truncated ACE header at ' + off);
  const type = buf.readUInt8(off);
  const flags = buf.readUInt8(off + 1);
  const size = buf.readUInt16LE(off + 2);
  if (size < 4) bad('parseAce: AceSize ' + size + ' < 4 at ' + off);
  if (size % 4 !== 0) bad('parseAce: AceSize ' + size + ' is not a multiple of 4 at ' + off);
  if (off + size > buf.length) bad('parseAce: ACE at ' + off + ' overruns the buffer');
  const end = off + size;
  const ace = { type, flags, size };
  let p = off + 4;
  if (p + 4 > end) bad('parseAce: no Mask in ACE at ' + off);
  ace.mask = buf.readUInt32LE(p) >>> 0;
  p += 4;
  if (OBJECT_ACE_TYPES.has(type)) {
    if (p + 4 > end) bad('parseAce: no object Flags in ACE at ' + off);
    ace.objectFlags = buf.readUInt32LE(p) >>> 0;
    p += 4;
    if (ace.objectFlags & ACE_OBJECT_TYPE_PRESENT) {
      if (p + 16 > end) bad('parseAce: truncated ObjectType at ' + off);
      ace.objectType = guidToString(buf, p); p += 16;
    }
    if (ace.objectFlags & ACE_INHERITED_OBJECT_TYPE_PRESENT) {
      if (p + 16 > end) bad('parseAce: truncated InheritedObjectType at ' + off);
      ace.inheritedObjectType = guidToString(buf, p); p += 16;
    }
  }
  const sid = parseSid(buf, p);
  ace.sid = sidToString(sid);
  p += sid.size;
  if (p < end) ace.extra = Buffer.from(buf.subarray(p, end));
  return ace;
}

function aceLength(ace) {
  let n = 8 + sidLength(ace.sid);
  if (OBJECT_ACE_TYPES.has(ace.type)) {
    n += 4;
    const of = ace.objectFlags >>> 0;
    if (of & ACE_OBJECT_TYPE_PRESENT) n += 16;
    if (of & ACE_INHERITED_OBJECT_TYPE_PRESENT) n += 16;
  }
  if (ace.extra) n += ace.extra.length;
  return n;
}

function buildAce(ace) {
  const size = aceLength(ace);
  if (size % 4 !== 0) bad('buildAce: computed AceSize ' + size + ' is not a multiple of 4');
  if (size > 0xffff) bad('buildAce: ACE too large');
  const buf = Buffer.alloc(size);
  buf.writeUInt8(ace.type & 0xff, 0);
  buf.writeUInt8(ace.flags & 0xff, 1);
  buf.writeUInt16LE(size, 2);
  buf.writeUInt32LE(ace.mask >>> 0, 4);
  let p = 8;
  if (OBJECT_ACE_TYPES.has(ace.type)) {
    const of = ace.objectFlags >>> 0;
    buf.writeUInt32LE(of, 8); p = 12;
    if (of & ACE_OBJECT_TYPE_PRESENT) { guidFromString(ace.objectType).copy(buf, p); p += 16; }
    if (of & ACE_INHERITED_OBJECT_TYPE_PRESENT) { guidFromString(ace.inheritedObjectType).copy(buf, p); p += 16; }
  }
  buildSid(ace.sid).copy(buf, p);
  p += sidLength(ace.sid);
  if (ace.extra) ace.extra.copy(buf, p);
  return buf;
}

// GUIDs in object ACEs are the usual little-endian-first-three-fields layout.
function guidToString(b, o) {
  const h = (x) => x.toString(16).padStart(2, '0');
  const p1 = b.readUInt32LE(o).toString(16).padStart(8, '0');
  const p2 = b.readUInt16LE(o + 4).toString(16).padStart(4, '0');
  const p3 = b.readUInt16LE(o + 6).toString(16).padStart(4, '0');
  let p4 = ''; for (let i = 8; i < 10; i++) p4 += h(b[o + i]);
  let p5 = ''; for (let i = 10; i < 16; i++) p5 += h(b[o + i]);
  return [p1, p2, p3, p4, p5].join('-');
}
function guidFromString(s) {
  const t = String(s).replace(/[{}]/g, '').split('-');
  if (t.length !== 5) bad('bad GUID: ' + s);
  const b = Buffer.alloc(16);
  b.writeUInt32LE(parseInt(t[0], 16) >>> 0, 0);
  b.writeUInt16LE(parseInt(t[1], 16) & 0xffff, 4);
  b.writeUInt16LE(parseInt(t[2], 16) & 0xffff, 6);
  Buffer.from(t[3], 'hex').copy(b, 8);
  Buffer.from(t[4], 'hex').copy(b, 10);
  return b;
}

// Parse an ACL.  Returns { revision, sbz1, size, aceCount, sbz2, aces, trailing? }.
function parseAcl(buf, off = 0) {
  if (!Buffer.isBuffer(buf)) bad('parseAcl: not a Buffer');
  if (off < 0 || off + 8 > buf.length) bad('parseAcl: truncated ACL header at ' + off);
  const revision = buf.readUInt8(off);
  const sbz1 = buf.readUInt8(off + 1);
  const size = buf.readUInt16LE(off + 2);
  const aceCount = buf.readUInt16LE(off + 4);
  const sbz2 = buf.readUInt16LE(off + 6);
  if (size < 8) bad('parseAcl: AclSize ' + size + ' < 8');
  if (off + size > buf.length) bad('parseAcl: ACL at ' + off + ' overruns the buffer');
  const aces = [];
  let p = off + 8;
  for (let i = 0; i < aceCount; i++) {
    if (p >= off + size) bad('parseAcl: AceCount ' + aceCount + ' exceeds AclSize');
    const ace = parseAce(buf, p);
    aces.push(ace);
    p += ace.size;
  }
  const acl = { revision, sbz1, size, aceCount, sbz2, aces };
  if (p < off + size) acl.trailing = Buffer.from(buf.subarray(p, off + size));
  return acl;
}

function aclLength(aces, opts = {}) {
  let n = 8;
  for (const a of aces) n += aceLength(a);
  if (opts.trailing) n += opts.trailing.length;
  return n;
}

// Build an ACL.  `aces` may also be an object as returned by parseAcl, in which
// case its revision / reserved fields / trailing bytes are preserved.
function buildAcl(aces, opts = {}) {
  let list = aces;
  let o = opts;
  if (aces && !Array.isArray(aces) && Array.isArray(aces.aces)) {
    list = aces.aces;
    o = Object.assign({ revision: aces.revision, sbz1: aces.sbz1, sbz2: aces.sbz2,
      trailing: aces.trailing }, opts);
  }
  if (!Array.isArray(list)) bad('buildAcl: aces must be an array');
  let revision = o.revision;
  if (revision === undefined) {
    revision = list.some((a) => OBJECT_ACE_TYPES.has(a.type)) ? ACL_REVISION_DS : ACL_REVISION;
  }
  const size = aclLength(list, o);
  if (size > 0xffff) bad('buildAcl: ACL too large');
  const buf = Buffer.alloc(size);
  buf.writeUInt8(revision & 0xff, 0);
  buf.writeUInt8((o.sbz1 || 0) & 0xff, 1);
  buf.writeUInt16LE(size, 2);
  buf.writeUInt16LE(list.length, 4);
  buf.writeUInt16LE((o.sbz2 || 0) & 0xffff, 6);
  let p = 8;
  for (const a of list) { const b = buildAce(a); b.copy(buf, p); p += b.length; }
  if (o.trailing) o.trailing.copy(buf, p);
  return buf;
}

// ---------------------------------------------------------------------------
// SECURITY_DESCRIPTOR ([MS-DTYP] 2.4.6), self-relative form
// ---------------------------------------------------------------------------

const SD_HEADER_LEN = 20;

// Returns { revision, sbz1, control, owner, group, sacl, dacl, layout }.
//   owner/group : textual SID, or null when the offset is 0
//   dacl/sacl   : parsed ACL object, or null when absent (NULL ACL)
//   layout      : the section names in ascending offset order, so that
//                 buildSecurityDescriptor can reproduce the original packing.
function parseSecurityDescriptor(buf) {
  if (!Buffer.isBuffer(buf)) bad('parseSecurityDescriptor: not a Buffer');
  if (buf.length < SD_HEADER_LEN) bad('parseSecurityDescriptor: shorter than the 20-byte header');
  const revision = buf.readUInt8(0);
  const sbz1 = buf.readUInt8(1);
  const control = buf.readUInt16LE(2);
  if (revision !== 1) bad('parseSecurityDescriptor: Revision is ' + revision + ', MUST be 1');
  if (!(control & SE.SE_SELF_RELATIVE)) {
    bad('parseSecurityDescriptor: SE_SELF_RELATIVE is clear; only the self-relative form is on the wire');
  }
  const offsetOwner = buf.readUInt32LE(4);
  const offsetGroup = buf.readUInt32LE(8);
  const offsetSacl = buf.readUInt32LE(12);
  const offsetDacl = buf.readUInt32LE(16);
  const sd = { revision, sbz1, control, owner: null, group: null, sacl: null, dacl: null };
  const seen = [];
  if (offsetOwner) { sd.owner = sidToString(parseSid(buf, offsetOwner)); seen.push(['owner', offsetOwner]); }
  if (offsetGroup) { sd.group = sidToString(parseSid(buf, offsetGroup)); seen.push(['group', offsetGroup]); }
  // 2.4.6: OffsetSacl "must be a valid offset if the SP flag is set; if the SP
  // flag is not set, this field MUST be set to zero".  A present-but-NULL ACL is
  // the SP/DP bit set with the offset 0, which is distinct from the bit clear.
  if (offsetSacl) { sd.sacl = parseAcl(buf, offsetSacl); seen.push(['sacl', offsetSacl]); }
  if (offsetDacl) { sd.dacl = parseAcl(buf, offsetDacl); seen.push(['dacl', offsetDacl]); }
  seen.sort((a, b) => a[1] - b[1]);
  sd.layout = seen.map((e) => e[0]);
  return sd;
}

const DEFAULT_LAYOUT = ['owner', 'group', 'sacl', 'dacl'];

// Build the self-relative form.  `control` defaults to SE_SELF_RELATIVE plus the
// DACL/SACL present bits implied by the sections supplied; SE_SELF_RELATIVE is
// always forced on because this is the only form we emit.
function buildSecurityDescriptor(sd) {
  if (!sd || typeof sd !== 'object') bad('buildSecurityDescriptor: not an object');
  const owner = sd.owner ? buildSid(sd.owner) : null;
  const group = sd.group ? buildSid(sd.group) : null;
  const sacl = sd.sacl ? buildAcl(sd.sacl) : null;
  const dacl = sd.dacl ? buildAcl(sd.dacl) : null;
  let control = sd.control === undefined || sd.control === null
    ? (dacl ? SE.SE_DACL_PRESENT : 0) | (sacl ? SE.SE_SACL_PRESENT : 0)
    : sd.control & 0xffff;
  control = (control | SE.SE_SELF_RELATIVE) & 0xffff;
  const layout = [];
  for (const name of (sd.layout && sd.layout.length ? sd.layout : DEFAULT_LAYOUT)) {
    if (!layout.includes(name)) layout.push(name);
  }
  for (const name of DEFAULT_LAYOUT) if (!layout.includes(name)) layout.push(name);
  const parts = { owner, group, sacl, dacl };
  const offsets = { owner: 0, group: 0, sacl: 0, dacl: 0 };
  let total = SD_HEADER_LEN;
  for (const name of layout) {
    const b = parts[name];
    if (!b) continue;
    offsets[name] = total;
    total += b.length;
  }
  const buf = Buffer.alloc(total);
  buf.writeUInt8(sd.revision === undefined ? 1 : sd.revision & 0xff, 0);
  buf.writeUInt8((sd.sbz1 || 0) & 0xff, 1);
  buf.writeUInt16LE(control, 2);
  buf.writeUInt32LE(offsets.owner, 4);
  buf.writeUInt32LE(offsets.group, 8);
  buf.writeUInt32LE(offsets.sacl, 12);
  buf.writeUInt32LE(offsets.dacl, 16);
  for (const name of layout) {
    const b = parts[name];
    if (b) b.copy(buf, offsets[name]);
  }
  return buf;
}

// Does this descriptor carry a DACL at all?  A NULL DACL (SE_DACL_PRESENT clear)
// grants everything; an empty DACL (present, zero ACEs) grants nothing.
const hasDacl = (sd) => Boolean(sd && (sd.control & SE.SE_DACL_PRESENT) && sd.dacl);

// ---------------------------------------------------------------------------
// Access check ([MS-DTYP] 2.5.3.2)
// ---------------------------------------------------------------------------

// TOKEN shape: { userSid, groupSids: [...], privileges: { SeSecurityPrivilege,
// SeTakeOwnershipPrivilege, ... } }.  `privileges` may be null to model the
// "Token.Privileges is NULL" case the pseudocode tests for.
function makeToken(userSid, groupSids, privileges) {
  return {
    userSid: userSid ? sidToString(userSid) : null,
    groupSids: (groupSids || []).map(sidToString),
    privileges: privileges === undefined ? {} : privileges,
  };
}

// [MS-DTYP] 2.5.3.1.1 SidInToken.
function sidInToken(token, sidToTest, principalSelfSubstitute) {
  if (!token || !sidToTest) return false;
  let want = sidToString(sidToTest);
  if (want === WELL_KNOWN.PRINCIPAL_SELF && principalSelfSubstitute) {
    want = sidToString(principalSelfSubstitute);
  }
  if (token.userSid && sidToString(token.userSid) === want) return true;
  for (const g of token.groupSids || []) if (sidToString(g) === want) return true;
  return false;
}

// [MS-DTYP] 2.4.3: generic bits are mapped only on the *requested* mask, never
// on the masks already stored in ACEs.
function mapGeneric(mask, gm) {
  let a = mask >>> 0;
  if (a & AM.GENERIC_READ) a |= gm.GenericRead;
  if (a & AM.GENERIC_WRITE) a |= gm.GenericWrite;
  if (a & AM.GENERIC_EXECUTE) a |= gm.GenericExecute;
  if (a & AM.GENERIC_ALL) a |= gm.GenericAll;
  a &= ~(AM.GENERIC_READ | AM.GENERIC_WRITE | AM.GENERIC_EXECUTE | AM.GENERIC_ALL);
  return a >>> 0;
}

// accessCheck(sd, token, desiredAccess, options) -> { granted, status }
//
// options:
//   genericMapping  GENERIC_MAPPING to expand GR/GW/GX/GA with
//                   (default GENERIC_MAPPING_FILE)
//   principalSelf   SID substituted for S-1-5-10 ([MS-DTYP] 2.5.3.1.1)
//   objectTree      OBJECT_TYPE_LIST; when absent, object ACEs cannot match a
//                   node and are skipped, as 2.5.3.2's pseudocode implies
//   ownerRights     'always' (default, matches the reference server) or
//                   'ms-dtyp-literal' (withhold the implicit owner grant when
//                   the DACL already carries an ACE for the owner)
function accessCheck(sd, token, desiredAccess, options = {}) {
  const gm = options.genericMapping || GENERIC_MAPPING_FILE;
  const principalSelf = options.principalSelf || null;
  const privileges = token && token.privileges ? token.privileges : null;

  let remaining = mapGeneric(desiredAccess, gm);
  let granted = 0;
  let allowed = 0;
  let denied = 0;

  // ACCESS_SYSTEM_SECURITY is never satisfied from the DACL; it takes
  // SeSecurityPrivilege, and without it the whole request fails.
  if (remaining & AM.ACCESS_SYSTEM_SECURITY) {
    if (privileges && privileges.SeSecurityPrivilege) {
      remaining &= ~AM.ACCESS_SYSTEM_SECURITY;
      granted |= AM.ACCESS_SYSTEM_SECURITY;
      if (remaining === 0) return { granted: granted >>> 0, status: STATUS_SUCCESS };
    } else {
      return { granted: 0, status: STATUS_ACCESS_DENIED };
    }
  }

  if ((remaining & AM.WRITE_OWNER) && privileges) {
    if (privileges.SeTakeOwnershipPrivilege) {
      remaining &= ~AM.WRITE_OWNER;
      granted |= AM.WRITE_OWNER;
    }
  }

  // "the owner of an object is always granted READ_CONTROL and WRITE_DAC".
  // 2.5.3.2 guards this with "IF DACL does not contain ACEs from object owner",
  // but the reference server grants it unconditionally (evidence in SD-NOTES.md:
  // the owner of a mode 0000 file, whose own ACE has mask 0, is still given both
  // rights).  `options.ownerRights` selects between the two readings.  Either
  // way an OWNER_RIGHTS (S-1-3-4) ACE suppresses the grant, per 2.4.2.4.
  if (sd && sd.owner && sidInToken(token, sd.owner, principalSelf)) {
    const aces = hasDacl(sd) ? sd.dacl.aces : [];
    const ownerRightsAce = aces.some((a) => sidToString(a.sid) === WELL_KNOWN.OWNER_RIGHTS);
    const ownerAce = aces.some((a) => sidEqual(a.sid, sd.owner));
    const literal = options.ownerRights === 'ms-dtyp-literal';
    if (!ownerRightsAce && !(literal && ownerAce)) {
      remaining &= ~(AM.READ_CONTROL | AM.WRITE_DAC);
      granted |= AM.READ_CONTROL | AM.WRITE_DAC;
    }
  }

  const maxAllowedMode = (remaining & AM.MAXIMUM_ALLOWED) !== 0;
  if (maxAllowedMode) remaining &= ~AM.MAXIMUM_ALLOWED;

  // "No DACL in the security descriptor implies that there is no policy in place
  // to govern access to the object; any access check will succeed."
  if (!hasDacl(sd)) {
    if (maxAllowedMode) {
      granted |= gm.GenericAll | AM.READ_CONTROL | AM.WRITE_DAC | AM.WRITE_OWNER
        | AM.DELETE | AM.SYNCHRONIZE;
      return { granted: granted >>> 0, status: STATUS_SUCCESS };
    }
    granted |= remaining;
    return { granted: granted >>> 0, status: STATUS_SUCCESS };
  }

  for (const ace of sd.dacl.aces) {
    if (ace.flags & ACE_FLAG.INHERIT_ONLY_ACE) continue;
    const isObject = OBJECT_ACE_TYPES.has(ace.type);
    // Without an object tree an object ACE can never locate a node, so 2.5.3.2's
    // "IF ACE.Object is contained in LocalTree" is false and the ACE is skipped.
    if (isObject && !options.objectTree) continue;

    if (ALLOW_ACE_TYPES.has(ace.type)) {
      if (!sidInToken(token, ace.sid, principalSelf)) continue;
      if (maxAllowedMode) {
        allowed |= ace.mask;
      } else {
        // The pseudocode removes the bits from RemainingAccess and only then
        // ANDs RemainingAccess with the ACE mask, which can never contribute.
        // We accumulate first; see SD-NOTES.md.
        granted |= remaining & ace.mask;
        remaining &= ~ace.mask;
      }
    } else if (DENY_ACE_TYPES.has(ace.type)) {
      if ((ace.mask >>> 0) === 0) continue;          // "IF ACE.AccessMask equals 0 ... Break"
      if (!sidInToken(token, ace.sid, principalSelf)) continue;
      if (maxAllowedMode) {
        denied |= ace.mask;
      } else if (remaining & ace.mask) {
        return { granted: 0, status: STATUS_ACCESS_DENIED };
      }
    }
    // Audit / alarm / label / resource-attribute ACEs never appear in a DACL and
    // contribute nothing to the decision.
  }

  if (maxAllowedMode) {
    granted |= (allowed & ~denied);
    granted >>>= 0;
    return granted !== 0
      ? { granted, status: STATUS_SUCCESS }
      : { granted: 0, status: STATUS_ACCESS_DENIED };
  }

  if (remaining === 0) return { granted: granted >>> 0, status: STATUS_SUCCESS };
  return { granted: 0, status: STATUS_ACCESS_DENIED };
}

// ---------------------------------------------------------------------------
// Inheritance ([MS-DTYP] 2.5.3.4)
// ---------------------------------------------------------------------------

// 2.5.3.4.3 ContainsInheritableACEs.
function containsInheritableAces(acl) {
  if (!acl || !acl.aces) return false;
  return acl.aces.some((a) => a.flags
    & (ACE_FLAG.CONTAINER_INHERIT_ACE | ACE_FLAG.OBJECT_INHERIT_ACE));
}

const cloneAce = (a) => Object.assign({}, a, a.extra ? { extra: Buffer.from(a.extra) } : {});

// 2.5.3.4.4 ComputeInheritedACLfromParent.
function computeInheritedAclFromParent(acl, isContainer, objectTypes) {
  const explicit = [];
  const inheritable = [];
  if (!acl || !acl.aces) return { aces: [] };
  for (const ace of acl.aces) {
    if (ace.flags & ACE_FLAG.INHERIT_ONLY_ACE) continue;
    const applies = ((ace.flags & ACE_FLAG.CONTAINER_INHERIT_ACE) && isContainer)
      || ((ace.flags & ACE_FLAG.OBJECT_INHERIT_ACE) && !isContainer);
    if (!applies) continue;
    if (ace.type === ACE_TYPE.ACCESS_ALLOWED || ace.type === ACE_TYPE.ACCESS_DENIED
        || ace.type === ACE_TYPE.SYSTEM_AUDIT || ace.type === ACE_TYPE.SYSTEM_ALARM) {
      const n = cloneAce(ace);
      n.flags = ACE_FLAG.INHERITED_ACE;     // "Clear NewACE.AceFlags ... = INHERITED_ACE"
      explicit.push(n);
    } else if (OBJECT_ACE_TYPES.has(ace.type)) {
      if (objectTypes && ace.objectType && objectTypes.includes(ace.objectType)) {
        const n = cloneAce(ace);
        n.flags = ACE_FLAG.INHERITED_ACE;
        explicit.push(n);
      }
    }
  }
  if (isContainer) {
    for (const ace of acl.aces) {
      if (ace.flags & ACE_FLAG.NO_PROPAGATE_INHERIT_ACE) continue;
      if (!(ace.flags & (ACE_FLAG.CONTAINER_INHERIT_ACE | ACE_FLAG.OBJECT_INHERIT_ACE))) continue;
      const n = cloneAce(ace);
      n.flags = (ace.flags | ACE_FLAG.INHERITED_ACE | ACE_FLAG.INHERIT_ONLY_ACE) & 0xff;
      inheritable.push(n);
    }
  }
  return { aces: explicit.concat(inheritable) };
}

// 2.5.3.4.5 ComputeInheritedACLfromCreator.
function computeInheritedAclFromCreator(acl, isContainer, objectTypes) {
  const explicit = [];
  const inheritable = [];
  if (!acl || !acl.aces) return { aces: [] };
  for (const ace of acl.aces) {
    const applies = ((ace.flags & ACE_FLAG.CONTAINER_INHERIT_ACE) && isContainer)
      || ((ace.flags & ACE_FLAG.OBJECT_INHERIT_ACE) && !isContainer);
    if (!applies) continue;
    if (OBJECT_ACE_TYPES.has(ace.type)) {
      if (!(objectTypes && ace.objectType && objectTypes.includes(ace.objectType))) continue;
    }
    const n = cloneAce(ace);
    n.flags = 0;                             // "Set NewACE.AceFlags to NULL"
    explicit.push(n);
  }
  if (isContainer) {
    for (const ace of acl.aces) {
      if (!(ace.flags & (ACE_FLAG.CONTAINER_INHERIT_ACE | ACE_FLAG.OBJECT_INHERIT_ACE))) continue;
      const n = cloneAce(ace);
      n.flags = (ace.flags | ACE_FLAG.INHERIT_ONLY_ACE) & 0xff;
      inheritable.push(n);
    }
  }
  return { aces: explicit.concat(inheritable) };
}

// 2.5.3.4.6 PreProcessACLfromCreator.
function preProcessAclFromCreator(acl) {
  if (!acl || !acl.aces) return null;
  return { aces: acl.aces.filter((a) => !(a.flags & ACE_FLAG.INHERITED_ACE)).map(cloneAce) };
}

// 2.5.3.4.7 PostProcessACL.
const COPY_ALL = 'CopyAllAces';
const COPY_INHERITED = 'CopyInheritedAces';
const COPY_EXPLICIT = 'CopyExplicitAces';

function postProcessAcl(acl, copyFilter, owner, group, gm) {
  if (!acl || !acl.aces) return null;
  const out = [];
  for (const ace of acl.aces) {
    let copy = false;
    if (copyFilter === COPY_ALL) copy = true;
    else if (copyFilter === COPY_INHERITED) copy = (ace.flags & ACE_FLAG.INHERITED_ACE) !== 0;
    else if (copyFilter === COPY_EXPLICIT) copy = (ace.flags & ACE_FLAG.INHERITED_ACE) === 0;
    if (!copy) continue;
    const n = cloneAce(ace);
    const s = sidToString(n.sid);
    if (s === WELL_KNOWN.CREATOR_OWNER && owner) n.sid = sidToString(owner);
    else if (s === WELL_KNOWN.CREATOR_GROUP && group) n.sid = sidToString(group);
    let m = n.mask >>> 0;
    // 2.5.3.4.7 adds the mapped bits but, unlike 2.4.3's request-side mapping,
    // does not say to clear GR/GW/GX.  We follow it literally; see SD-NOTES.md.
    if (m & AM.GENERIC_READ) m |= gm.GenericRead;
    if (m & AM.GENERIC_WRITE) m |= gm.GenericWrite;
    if (m & AM.GENERIC_EXECUTE) m |= gm.GenericExecute;
    n.mask = m >>> 0;
    out.push(n);
  }
  return { aces: out };
}

// 2.5.3.4.1 CreateSecurityDescriptor, reduced to the file-server case: the
// creator supplies no descriptor, so both ACLs come from the parent.
//
//   inherit(parentSd, isContainer, creatorOwner, creatorGroup, options)
//
// options:
//   genericMapping  default GENERIC_MAPPING_DIRECTORY when isContainer
//   creatorSd       a CreatorDescriptor, if the client supplied one on CREATE
//   autoInheritFlags  { DACL_AUTO_INHERIT, SACL_AUTO_INHERIT, DEFAULT_DESCRIPTOR_FOR_OBJECT }
//   objectTypes     array of GUID strings for object-ACE filtering
//   collapse        merge an effective ACE with its INHERIT_ONLY twin when the
//                   masks are identical (the 2.5.3.4.4 summary table's form);
//                   default false, i.e. the pseudocode's literal output
function inherit(parentSd, isContainer, creatorOwner, creatorGroup, options = {}) {
  const gm = options.genericMapping
    || (isContainer ? GENERIC_MAPPING_DIRECTORY : GENERIC_MAPPING_FILE);
  const flags = Object.assign({ DACL_AUTO_INHERIT: true, SACL_AUTO_INHERIT: true,
    DEFAULT_DESCRIPTOR_FOR_OBJECT: false }, options.autoInheritFlags || {});
  const creator = options.creatorSd || null;
  const objectTypes = options.objectTypes || null;

  const owner = (creator && creator.owner) || (creatorOwner ? sidToString(creatorOwner) : null);
  const group = (creator && creator.group) || (creatorGroup ? sidToString(creatorGroup) : null);

  const dacl = computeAcl('dacl', parentSd, creator, flags, isContainer, objectTypes,
    gm, owner, group, options);
  const sacl = computeAcl('sacl', parentSd, creator, flags, isContainer, objectTypes,
    gm, owner, group, options);

  let control = SE.SE_SELF_RELATIVE;
  if (dacl.acl) {
    control |= SE.SE_DACL_PRESENT;
    if (dacl.autoInherited) control |= SE.SE_DACL_AUTO_INHERITED;
  }
  if (sacl.acl) {
    control |= SE.SE_SACL_PRESENT;
    if (sacl.autoInherited) control |= SE.SE_SACL_AUTO_INHERITED;
  }
  return {
    revision: 1, sbz1: 0, control,
    owner, group,
    dacl: dacl.acl ? { revision: ACL_REVISION, aces: dacl.acl.aces } : null,
    sacl: sacl.acl ? { revision: ACL_REVISION, aces: sacl.acl.aces } : null,
    layout: DEFAULT_LAYOUT.slice(),
  };
}

// 2.5.3.4.2 ComputeACL.
function computeAcl(which, parentSd, creator, flags, isContainer, objectTypes, gm,
                    owner, group, options) {
  const parentAcl = parentSd ? parentSd[which] : null;
  const creatorAcl = creator ? creator[which] : null;
  const parentControl = parentSd ? (parentSd.control || 0) : 0;
  const creatorControl = creator ? (creator.control || 0) : 0;
  const autoFlag = which === 'dacl' ? flags.DACL_AUTO_INHERIT : flags.SACL_AUTO_INHERIT;
  const protectedFlag = which === 'dacl' ? SE.SE_DACL_PROTECTED : SE.SE_SACL_PROTECTED;
  void parentControl;

  if (containsInheritableAces(parentAcl)) {
    if (!creatorAcl || flags.DEFAULT_DESCRIPTOR_FOR_OBJECT) {
      const next = computeInheritedAclFromParent(parentAcl, isContainer, objectTypes);
      const final = postProcessAcl(next, COPY_INHERITED, owner, group, gm);
      return { acl: maybeCollapse(final, options), autoInherited: autoFlag };
    }
    const pre = preProcessAclFromCreator(creatorAcl);
    const tmp = computeInheritedAclFromCreator(pre, isContainer, objectTypes);
    let autoInherited = false;
    if (!(creatorControl & protectedFlag) && autoFlag) {
      const fromParent = computeInheritedAclFromParent(parentAcl, isContainer, objectTypes);
      tmp.aces = tmp.aces.concat(fromParent.aces);
      autoInherited = true;
    }
    const processed = postProcessAcl(tmp, COPY_INHERITED, owner, group, gm);
    return { acl: maybeCollapse(processed, options), autoInherited };
  }

  // Parent has nothing inheritable.
  let tmp = null;
  if (!creatorAcl) {
    if (which === 'dacl' && options.defaultDacl) tmp = { aces: options.defaultDacl.aces || [] };
  } else {
    tmp = preProcessAclFromCreator(creatorAcl);
  }
  const processed = postProcessAcl(tmp, COPY_ALL, owner, group, gm);
  return { acl: processed, autoInherited: false };
}

// Optional post-step: 2.5.3.4.4's summary table shows a container inheriting a
// CI ACE as a single ACE, while its own pseudocode emits an effective copy plus
// an INHERIT_ONLY copy.  The surrounding note says the pair only arises "when an
// inheritable ACE contains generic information"; with no generic bits the two
// are redundant, so merging them reproduces the table.
function maybeCollapse(acl, options) {
  if (!acl || !options || !options.collapse) return acl;
  const out = [];
  const used = new Set();
  for (let i = 0; i < acl.aces.length; i++) {
    if (used.has(i)) continue;
    const a = acl.aces[i];
    if (a.flags & ACE_FLAG.INHERIT_ONLY_ACE) { out.push(a); continue; }
    let merged = a;
    for (let j = i + 1; j < acl.aces.length; j++) {
      if (used.has(j)) continue;
      const b = acl.aces[j];
      if (!(b.flags & ACE_FLAG.INHERIT_ONLY_ACE)) continue;
      if (b.type !== a.type || (b.mask >>> 0) !== (a.mask >>> 0) || !sidEqual(b.sid, a.sid)) continue;
      merged = Object.assign({}, a, {
        flags: (a.flags | (b.flags & ~ACE_FLAG.INHERIT_ONLY_ACE)) & 0xff,
      });
      used.add(j);
      break;
    }
    out.push(merged);
  }
  return { aces: out };
}

// ---------------------------------------------------------------------------
// Default descriptor for a POSIX mode
// ---------------------------------------------------------------------------

// Map one rwx triple to an ACCESS_MASK.  r/w/x become FILE_GENERIC_READ /
// FILE_GENERIC_WRITE / FILE_GENERIC_EXECUTE, which is what smb.conf(5) says
// `nt acl support` does.  Two refinements match the reference server exactly and
// are evidenced in SD-NOTES.md:
//   * a directory's write also carries FILE_DELETE_CHILD;
//   * with `acl map full control = yes` (the default) an rwx triple becomes full
//     control rather than the OR of the three.
function maskForTriple(bits, isContainer, aclMapFullControl) {
  const b = bits & 7;
  if (b === 0) return 0;
  if (b === 7 && aclMapFullControl) {
    return isContainer ? AM.FILE_ALL_ACCESS : (AM.FILE_ALL_ACCESS & ~AM.DELETE);
  }
  const gm = isContainer ? GENERIC_MAPPING_DIRECTORY : GENERIC_MAPPING_FILE;
  let m = 0;
  if (b & 4) m |= gm.GenericRead;
  if (b & 2) m |= gm.GenericWrite;
  if (b & 1) m |= gm.GenericExecute;
  return m >>> 0;
}

// defaultSecurityDescriptor(ownerSid, groupSid, mode, options)
//
// options:
//   isContainer         the object is a directory (default false)
//   aclMapFullControl   smb.conf(5) `acl map full control` (default true)
//   everyoneSid         SID for the "other" triple (default S-1-1-0)
//   aceFlags            AceFlags put on the three ACEs (default 0)
//   control             override the Control word
function defaultSecurityDescriptor(ownerSid, groupSid, mode, options = {}) {
  const isContainer = Boolean(options.isContainer);
  const full = options.aclMapFullControl === undefined ? true : Boolean(options.aclMapFullControl);
  const everyone = options.everyoneSid || WELL_KNOWN.EVERYONE;
  const aceFlags = (options.aceFlags || 0) & 0xff;
  const m = mode >>> 0;
  const aces = [
    { type: ACE_TYPE.ACCESS_ALLOWED, flags: aceFlags, sid: sidToString(ownerSid),
      mask: maskForTriple((m >> 6) & 7, isContainer, full) },
    { type: ACE_TYPE.ACCESS_ALLOWED, flags: aceFlags, sid: sidToString(groupSid),
      mask: maskForTriple((m >> 3) & 7, isContainer, full) },
    { type: ACE_TYPE.ACCESS_ALLOWED, flags: aceFlags, sid: sidToString(everyone),
      mask: maskForTriple(m & 7, isContainer, full) },
  ];
  const control = options.control === undefined
    ? (SE.SE_SELF_RELATIVE | SE.SE_DACL_PRESENT | SE.SE_DACL_PROTECTED)
    : options.control;
  return {
    revision: 1, sbz1: 0, control,
    owner: sidToString(ownerSid),
    group: sidToString(groupSid),
    sacl: null,
    dacl: { revision: ACL_REVISION, aces },
    layout: DEFAULT_LAYOUT.slice(),
  };
}

// ---------------------------------------------------------------------------
// The POSIX-ACL round trip
// ---------------------------------------------------------------------------
//
// `nt acl support = yes` with no `acl_xattr` module does not store the client's
// descriptor: it converts the DACL into a POSIX ACL and regenerates a descriptor
// from that ACL on the next read.  The conversion is lossy in a very specific
// way, all of it measured against Samba 4.17.12 (SD-NOTES.md §2.3):
//
//   * an ACE contributes `r` if its mask carries FILE_READ_DATA or FILE_READ_EA,
//     `w` for FILE_WRITE_DATA / FILE_APPEND_DATA / FILE_WRITE_EA and `x` for
//     FILE_EXECUTE.  Nothing else contributes -- DELETE, READ_CONTROL,
//     SYNCHRONIZE, FILE_READ_ATTRIBUTES, FILE_WRITE_ATTRIBUTES and
//     FILE_DELETE_CHILD are all dropped;
//   * only ACEs whose trustee is the owner, the owning group or Everyone map at
//     all.  Any other SID (S-1-5-18 was the one tested) is discarded;
//   * a deny ACE subtracts its bits from the same class;
//   * a missing `user::` entry keeps the object's current owner bits, a missing
//     `group::` copies `other::` when there is one and is otherwise empty, and a
//     missing `other::` is empty;
//   * the owner is then forced to keep `r` on a file and `rwx` on a directory;
//   * a DACL with no mappable ACE at all is not applied: the object keeps the
//     permission bits and the descriptor it already had.
//
// The regenerated descriptor has five ACEs, in the order the reference server
// emits them: `user::`, `group::`, the named group entry, the named user entry,
// then `other::`.

const POSIX_R = 4, POSIX_W = 2, POSIX_X = 1;

// The rwx an ACCESS_MASK is worth to Samba's POSIX-ACL backend.
function posixPermsForMask(mask) {
  const m = mask >>> 0;
  let p = 0;
  if (m & (AM.FILE_READ_DATA | AM.FILE_READ_EA)) p |= POSIX_R;
  if (m & (AM.FILE_WRITE_DATA | AM.FILE_APPEND_DATA | AM.FILE_WRITE_EA)) p |= POSIX_W;
  if (m & AM.FILE_EXECUTE) p |= POSIX_X;
  return p;
}

// applyPosixAcl(sd, options) -> { applied, mode, sd }
//
// `sd` is the descriptor the client asked for.  The result is what the server
// stores and will report back; `applied: false` means the DACL mapped to
// nothing, so neither the mode nor the stored descriptor changes.
//
// options:
//   ownerSid, groupSid   the object's owner and group (required)
//   everyoneSid          SID for the "other" class (default S-1-1-0)
//   mode                 the object's current permission bits (required)
//   isContainer          the object is a directory
//   aclMapFullControl    smb.conf(5) `acl map full control` (default true)
function applyPosixAcl(sd, options = {}) {
  const isContainer = Boolean(options.isContainer);
  const full = options.aclMapFullControl === undefined ? true : Boolean(options.aclMapFullControl);
  const owner = options.ownerSid;
  const group = options.groupSid;
  const everyone = options.everyoneSid || WELL_KNOWN.EVERYONE;
  const mode = (options.mode || 0) >>> 0;
  const unchanged = { applied: false, mode: mode & 0o777, sd: null };
  if (!hasDacl(sd) || !owner || !group) return unchanged;

  const allow = { owner: 0, group: 0, other: 0 };
  const deny = { owner: 0, group: 0, other: 0 };
  const seen = { owner: false, group: false, other: false };
  const granted = { owner: false, group: false, other: false };

  for (const ace of sd.dacl.aces) {
    if (ace.flags & ACE_FLAG.INHERIT_ONLY_ACE) continue;
    let cls = null;
    if (sidEqual(ace.sid, owner)) cls = 'owner';
    else if (sidEqual(ace.sid, group)) cls = 'group';
    else if (sidEqual(ace.sid, everyone)) cls = 'other';
    if (cls === null) continue;                       // unmappable trustee
    const p = posixPermsForMask(ace.mask);
    if (ALLOW_ACE_TYPES.has(ace.type)) {
      allow[cls] |= p;
      seen[cls] = true;
      granted[cls] = true;
    } else if (DENY_ACE_TYPES.has(ace.type)) {
      // [MS-DTYP] 2.4.5 canonical order puts every deny ahead of every allow.
      // A DACL that breaks it for a class it has already granted is refused by
      // the reference server, which reports success and changes nothing.
      if (granted[cls]) return unchanged;
      deny[cls] |= p;
      seen[cls] = true;
    }
  }
  if (!seen.owner && !seen.group && !seen.other) return unchanged;

  const perms = { owner: 0, group: 0, other: 0 };
  perms.other = seen.other ? ((allow.other & ~deny.other) & 7) : 0;
  perms.group = seen.group ? ((allow.group & ~deny.group) & 7) : perms.other;
  perms.owner = seen.owner ? ((allow.owner & ~deny.owner) & 7) : ((mode >> 6) & 7);
  perms.owner |= isContainer ? (POSIX_R | POSIX_W | POSIX_X) : POSIX_R;

  // Writing a POSIX ACL leaves a mask entry of rwx behind, and the mask is what
  // the group field of st_mode then reports.
  const newMode = ((perms.owner & 7) << 6) | 0o070 | (perms.other & 7);
  const maskOf = (p) => maskForTriple(p, isContainer, full);
  const mkAce = (sid, p) => ({ type: ACE_TYPE.ACCESS_ALLOWED, flags: 0,
    sid: sidToString(sid), mask: maskOf(p) });
  return {
    applied: true,
    mode: newMode,
    sd: {
      revision: 1, sbz1: 0,
      control: SE.SE_SELF_RELATIVE | SE.SE_DACL_PRESENT | SE.SE_DACL_PROTECTED,
      owner: sidToString(owner),
      group: sidToString(group),
      sacl: null,
      dacl: {
        revision: ACL_REVISION,
        aces: [mkAce(owner, perms.owner), mkAce(group, perms.group),
          mkAce(group, perms.group), mkAce(owner, perms.owner),
          mkAce(everyone, perms.other)],
      },
      layout: DEFAULT_LAYOUT.slice(),
    },
  };
}

// ---------------------------------------------------------------------------
// SECURITY_INFORMATION helpers (QUERY_INFO / SET_INFO plumbing)
// ---------------------------------------------------------------------------

// Project `sd` down to the parts the caller asked for, clearing the Control bits
// for the parts withheld ([MS-SMB2] 2.2.37 AdditionalInformation).
function filterSecurityInformation(sd, si) {
  const want = si >>> 0;
  const out = {
    revision: sd.revision === undefined ? 1 : sd.revision,
    sbz1: sd.sbz1 || 0,
    control: sd.control & 0xffff,
    owner: null, group: null, sacl: null, dacl: null,
    layout: sd.layout ? sd.layout.slice() : DEFAULT_LAYOUT.slice(),
  };
  if (want & SI.OWNER_SECURITY_INFORMATION) out.owner = sd.owner;
  else out.control &= ~SE.SE_OWNER_DEFAULTED;
  if (want & SI.GROUP_SECURITY_INFORMATION) out.group = sd.group;
  else out.control &= ~SE.SE_GROUP_DEFAULTED;
  // The reference server keeps SE_DACL_PROTECTED / SE_DACL_AUTO_INHERITED in the
  // reply even when the DACL itself is withheld, and only drops the PRESENT bit
  // (evidence: the AdditionalInformation=1 and =2 captures in SD-NOTES.md, where
  // Control is 0x9000 rather than 0x8000).
  if (want & SI.DACL_SECURITY_INFORMATION) out.dacl = sd.dacl;
  else out.control &= ~(SE.SE_DACL_PRESENT | SE.SE_DACL_DEFAULTED);
  if (want & (SI.SACL_SECURITY_INFORMATION | SI.LABEL_SECURITY_INFORMATION)) out.sacl = sd.sacl;
  else out.control &= ~(SE.SE_SACL_PRESENT | SE.SE_SACL_DEFAULTED);
  return out;
}

// Merge the parts named by `si` from `incoming` into `current`.
function applySecurityInformation(current, incoming, si) {
  const want = si >>> 0;
  const base = current || { revision: 1, sbz1: 0, control: SE.SE_SELF_RELATIVE,
    owner: null, group: null, sacl: null, dacl: null };
  const out = Object.assign({}, base, { layout: DEFAULT_LAYOUT.slice() });
  out.control = base.control & 0xffff;
  if (want & SI.OWNER_SECURITY_INFORMATION) out.owner = incoming.owner;
  if (want & SI.GROUP_SECURITY_INFORMATION) out.group = incoming.group;
  if (want & SI.DACL_SECURITY_INFORMATION) {
    out.dacl = incoming.dacl;
    out.control &= ~(SE.SE_DACL_PRESENT | SE.SE_DACL_DEFAULTED | SE.SE_DACL_AUTO_INHERITED
      | SE.SE_DACL_PROTECTED);
    out.control |= incoming.control & (SE.SE_DACL_PRESENT | SE.SE_DACL_DEFAULTED
      | SE.SE_DACL_AUTO_INHERITED | SE.SE_DACL_PROTECTED);
  }
  if (want & (SI.SACL_SECURITY_INFORMATION | SI.LABEL_SECURITY_INFORMATION)) {
    out.sacl = incoming.sacl;
    out.control &= ~(SE.SE_SACL_PRESENT | SE.SE_SACL_DEFAULTED | SE.SE_SACL_AUTO_INHERITED
      | SE.SE_SACL_PROTECTED);
    out.control |= incoming.control & (SE.SE_SACL_PRESENT | SE.SE_SACL_DEFAULTED
      | SE.SE_SACL_AUTO_INHERITED | SE.SE_SACL_PROTECTED);
  }
  out.control |= SE.SE_SELF_RELATIVE;
  return out;
}

// Reorder a DACL into the canonical form of [MS-DTYP] 2.4.5: explicit denies,
// explicit allows, then inherited ACEs in the order they were inherited.
function canonicalizeAces(aces) {
  const explicitDeny = [], explicitAllow = [], inherited = [];
  for (const a of aces) {
    if (a.flags & ACE_FLAG.INHERITED_ACE) inherited.push(a);
    else if (DENY_ACE_TYPES.has(a.type) || a.type === ACE_TYPE.ACCESS_DENIED_OBJECT) explicitDeny.push(a);
    else explicitAllow.push(a);
  }
  return explicitDeny.concat(explicitAllow, inherited);
}

// ---------------------------------------------------------------------------
// Comparison helper
// ---------------------------------------------------------------------------

// Replace machine-specific SIDs with a stable placeholder so two descriptors
// produced on different machines can be compared.  Well-known SIDs and the UNIX
// authorities keep their textual form; anything under S-1-5-21 becomes
// "S-1-5-21-<domain>-<rid>".
function normaliseSid(s) {
  if (s === null || s === undefined) return null;
  const o = toSidObject(s);
  if (o.revision === 1 && o.authority === 5 && o.subAuthorities.length >= 2
      && o.subAuthorities[0] === 21) {
    return 'S-1-5-21-<domain>-' + o.subAuthorities[o.subAuthorities.length - 1];
  }
  return sidToString(o);
}

function controlNames(control) {
  const out = [];
  for (const [k, v] of Object.entries(SE)) if (control & v) out.push(k);
  return out.sort();
}

// normaliseForComparison(sd) -> plain JSON-serialisable object.
function normaliseForComparison(sd) {
  const d = Buffer.isBuffer(sd) ? parseSecurityDescriptor(sd) : sd;
  const acl = (a, present) => {
    if (!present) return { present: false, aces: null };
    if (!a) return { present: true, null: true, aces: null };
    return {
      present: true,
      null: false,
      revision: a.revision,
      aceCount: a.aces.length,
      aces: a.aces.map((e) => ({
        type: e.type,
        flags: e.flags,
        mask: '0x' + ((e.mask >>> 0).toString(16).padStart(8, '0')),
        sid: normaliseSid(e.sid),
      })),
    };
  };
  return {
    revision: d.revision === undefined ? 1 : d.revision,
    control: '0x' + ((d.control & 0xffff).toString(16).padStart(4, '0')),
    controlFlags: controlNames(d.control),
    ownerPresent: Boolean(d.owner),
    owner: normaliseSid(d.owner),
    groupPresent: Boolean(d.group),
    group: normaliseSid(d.group),
    dacl: acl(d.dacl, Boolean(d.control & SE.SE_DACL_PRESENT)),
    sacl: acl(d.sacl, Boolean(d.control & SE.SE_SACL_PRESENT)),
  };
}

module.exports = {
  // constants
  ACE_TYPE, ACE_FLAG, ACL_REVISION, ACL_REVISION_DS, SE, SI, AM, WELL_KNOWN,
  ACE_OBJECT_TYPE_PRESENT, ACE_INHERITED_OBJECT_TYPE_PRESENT,
  UNIX_USER_PREFIX, UNIX_GROUP_PREFIX,
  GENERIC_MAPPING_FILE, GENERIC_MAPPING_DIRECTORY,
  STATUS_SUCCESS, STATUS_ACCESS_DENIED, STATUS_INVALID_SECURITY_DESCR,
  SdError,
  // SID
  parseSid, buildSid, sidToString, sidFromString, sidEqual, sidLength,
  // ACE / ACL
  parseAce, buildAce, aceLength, parseAcl, buildAcl, aclLength, canonicalizeAces,
  // descriptor
  parseSecurityDescriptor, buildSecurityDescriptor, hasDacl,
  filterSecurityInformation, applySecurityInformation,
  // algorithms
  makeToken, sidInToken, mapGeneric, accessCheck,
  inherit, containsInheritableAces, computeInheritedAclFromParent,
  computeInheritedAclFromCreator, preProcessAclFromCreator, postProcessAcl,
  COPY_ALL, COPY_INHERITED, COPY_EXPLICIT,
  defaultSecurityDescriptor, maskForTriple,
  applyPosixAcl, posixPermsForMask,
  // comparison
  normaliseForComparison, normaliseSid,
};
