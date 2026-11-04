'use strict';
// [MS-FSCC] file and volume information classes, shared by both front ends.
const fs = require('fs');
const path = require('path');
const S = require('./ntstatus');
const { ATTR } = require('./consts');
const V = require('./vfs');
const { fail } = V;

const uni = (s) => Buffer.from(s, 'utf16le');
const alloc = (size, eof) => (BigInt(eof) + 511n) / 512n * 512n;

const FILE = {
  Directory: 1, FullDirectory: 2, BothDirectory: 3, Basic: 4, Standard: 5, Internal: 6,
  Ea: 7, Access: 8, Name: 9, Rename: 10, Link: 11, Names: 12, Disposition: 13, Position: 14,
  FullEa: 15, Mode: 16, Alignment: 17, All: 18, Allocation: 19, EndOfFile: 20,
  AlternateName: 21, Stream: 22, Pipe: 23, PipeLocal: 24, PipeRemote: 25,
  Compression: 28, NetworkOpen: 34, AttributeTag: 35, IdBothDirectory: 37,
  IdFullDirectory: 38, ValidDataLength: 39, ShortName: 40, NormalizedName: 48,
  IdInformation: 59, DispositionInformationEx: 64, RenameInformationEx: 65,
};
const FS = {
  Volume: 1, Label: 2, Size: 3, Device: 4, Attribute: 5, Control: 6, FullSize: 7,
  ObjectId: 8, DriverPath: 9, VolumeFlags: 10, SectorSize: 11,
};

function fileIdOf(st) {
  return BigInt(st.ino) & 0xffffffffffffffffn;
}

function basic(share, rel, st) {
  const t = share.times(rel, st);
  const b = Buffer.alloc(40);
  b.writeBigUInt64LE(t.create, 0);
  b.writeBigUInt64LE(t.access, 8);
  b.writeBigUInt64LE(t.write, 16);
  b.writeBigUInt64LE(t.change, 24);
  b.writeUInt32LE(share.attributes(rel, st), 32);
  return b;
}
function standard(share, rel, st, node) {
  const b = Buffer.alloc(24);
  const eof = st.isDirectory() ? 0n : BigInt(st.size);
  b.writeBigUInt64LE(st.isDirectory() ? 0n : alloc(0, eof), 0);
  b.writeBigUInt64LE(eof, 8);
  // A delete-pending file reports no links left ([MS-FSCC] 2.4.41).
  const pending = !!(node && node.deletePending);
  b.writeUInt32LE(pending ? 0 : (st.isDirectory() ? 1 : Math.max(1, st.nlink)), 16);
  b.writeUInt8(pending ? 1 : 0, 20);
  b.writeUInt8(st.isDirectory() ? 1 : 0, 21);
  return b;
}
function internal(st) { const b = Buffer.alloc(8); b.writeBigUInt64LE(fileIdOf(st)); return b; }
function eaInfo() { const b = Buffer.alloc(4); return b; }
function accessInfo(access) { const b = Buffer.alloc(4); b.writeUInt32LE(access >>> 0); return b; }
function positionInfo(pos) { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(pos)); return b; }
function modeInfo(mode) { const b = Buffer.alloc(4); b.writeUInt32LE(mode >>> 0); return b; }
function alignmentInfo() { const b = Buffer.alloc(4); return b; }

function nameInfo(name) {
  const n = uni(name);
  const b = Buffer.alloc(4 + n.length);
  b.writeUInt32LE(n.length, 0);
  n.copy(b, 4);
  return b;
}

function allInfo(share, rel, st, open, name) {
  const bi = basic(share, rel, st);
  const si = standard(share, rel, st, open ? open.node : null);
  const ni = nameInfo(name);
  const b = Buffer.alloc(40 + 24 + 8 + 4 + 4 + 8 + 4 + ni.length);
  bi.copy(b, 0);
  si.copy(b, 40);
  internal(st).copy(b, 64);         // InternalInformation
  b.writeUInt32LE(0, 72);           // EaSize
  b.writeUInt32LE(open ? (open.access >>> 0) : 0x001f01ff, 76); // AccessFlags
  b.writeBigUInt64LE(0n, 80);       // CurrentByteOffset
  b.writeUInt32LE(0, 88);           // Mode
  b.writeUInt32LE(0, 92);           // AlignmentRequirement
  // NameInformation
  const out = Buffer.concat([b.subarray(0, 96), ni]);
  return out;
}

function networkOpen(share, rel, st) {
  const t = share.times(rel, st);
  const b = Buffer.alloc(56);
  b.writeBigUInt64LE(t.create, 0);
  b.writeBigUInt64LE(t.access, 8);
  b.writeBigUInt64LE(t.write, 16);
  b.writeBigUInt64LE(t.change, 24);
  const eof = st.isDirectory() ? 0n : BigInt(st.size);
  b.writeBigUInt64LE(st.isDirectory() ? 0n : alloc(0, eof), 32);
  b.writeBigUInt64LE(eof, 40);
  b.writeUInt32LE(share.attributes(rel, st), 48);
  return b;
}
function attributeTag(share, rel, st) {
  const b = Buffer.alloc(8);
  b.writeUInt32LE(share.attributes(rel, st), 0);
  return b;
}
function compression(st) {
  const b = Buffer.alloc(16);
  b.writeBigUInt64LE(BigInt(st.isDirectory() ? 0 : st.size), 0);
  b.writeUInt16LE(0, 8); // COMPRESSION_FORMAT_NONE
  return b;
}
function streamInfo(share, rel, st) {
  if (st.isDirectory()) return Buffer.alloc(0);
  const n = uni('::$DATA');
  const b = Buffer.alloc(24 + n.length);
  b.writeUInt32LE(0, 0);
  b.writeUInt32LE(n.length, 4);
  b.writeBigUInt64LE(BigInt(st.size), 8);
  b.writeBigUInt64LE(alloc(0, BigInt(st.size)), 16);
  n.copy(b, 24);
  return b;
}
function idInfo(st) {
  const b = Buffer.alloc(24);
  b.writeBigUInt64LE(BigInt(st.dev), 0);
  b.writeBigUInt64LE(fileIdOf(st), 8);
  return b;
}

// --- directory entry encoders ----------------------------------------------

function dirEntry(cls, share, ent, index, isLast, offsetBase) {
  const st = ent.st;
  const t = share.times(ent.rel, st);
  const attrs = share.attributes(ent.rel, st);
  const eof = st.isDirectory() ? 0n : BigInt(st.size);
  const name = uni(ent.name);
  let head, shortNameLen = 0;
  switch (cls) {
    case FILE.Directory: head = 64; break;
    case FILE.FullDirectory: head = 68; break;
    case FILE.IdFullDirectory: head = 80; break;
    case FILE.BothDirectory: head = 94; break;
    case FILE.IdBothDirectory: head = 104; break;
    case FILE.Names: head = 12; break;
    default: fail(S.STATUS_INVALID_INFO_CLASS);
  }
  const size = head + name.length;
  const padded = (size + 7) & ~7;
  const b = Buffer.alloc(isLast ? size : padded);
  b.writeUInt32LE(isLast ? 0 : padded, 0);
  b.writeUInt32LE(index, 4);
  if (cls === FILE.Names) {
    b.writeUInt32LE(name.length, 8);
    name.copy(b, 12);
    return b;
  }
  b.writeBigUInt64LE(t.create, 8);
  b.writeBigUInt64LE(t.access, 16);
  b.writeBigUInt64LE(t.write, 24);
  b.writeBigUInt64LE(t.change, 32);
  b.writeBigUInt64LE(eof, 40);
  b.writeBigUInt64LE(st.isDirectory() ? 0n : alloc(0, eof), 48);
  b.writeUInt32LE(attrs, 56);
  b.writeUInt32LE(name.length, 60);
  if (cls === FILE.Directory) { name.copy(b, 64); return b; }
  b.writeUInt32LE(0, 64); // EaSize
  if (cls === FILE.FullDirectory) { name.copy(b, 68); return b; }
  if (cls === FILE.IdFullDirectory) {
    b.writeUInt32LE(0, 68);
    b.writeBigUInt64LE(fileIdOf(st), 72);
    name.copy(b, 80);
    return b;
  }
  // Both / IdBoth carry the 8.3 short name
  const short = (ent.name.length > 12 || /[^A-Za-z0-9_^$~!#%&\-{}@'()\. ]/.test(ent.name)) ? uni(V.mangle(ent.name)) : Buffer.alloc(0);
  b.writeUInt8(Math.min(short.length, 24), 68);
  b.writeUInt8(0, 69);
  short.copy(b, 70, 0, Math.min(short.length, 24));
  if (cls === FILE.BothDirectory) { name.copy(b, 94); return b; }
  b.writeUInt16LE(0, 94);
  b.writeBigUInt64LE(fileIdOf(st), 96);
  name.copy(b, 104);
  return b;
}

// --- volume information ----------------------------------------------------

function fsInfo(cls, share, label) {
  switch (cls) {
    case FS.Volume: {
      const n = uni(share && share.name ? share.name : label);
      const b = Buffer.alloc(18 + n.length);
      b.writeBigUInt64LE(V.toFileTime(Date.now()), 0);
      b.writeUInt32LE(0xdeadbeef, 8);
      b.writeUInt32LE(n.length, 12);
      b.writeUInt8(0, 16);
      n.copy(b, 18);
      return b;
    }
    case FS.Size: {
      const b = Buffer.alloc(24);
      b.writeBigUInt64LE(1048576n, 0);
      b.writeBigUInt64LE(524288n, 8);
      b.writeUInt32LE(8, 16);
      b.writeUInt32LE(512, 20);
      return b;
    }
    case FS.FullSize: {
      const b = Buffer.alloc(32);
      b.writeBigUInt64LE(1048576n, 0);
      b.writeBigUInt64LE(524288n, 8);
      b.writeBigUInt64LE(524288n, 16);
      b.writeUInt32LE(8, 24);
      b.writeUInt32LE(512, 28);
      return b;
    }
    case FS.Device: {
      const b = Buffer.alloc(8);
      b.writeUInt32LE(0x00000007, 0); // FILE_DEVICE_DISK
      b.writeUInt32LE(0x00000020, 4); // FILE_DEVICE_IS_MOUNTED
      return b;
    }
    case FS.Attribute: {
      const n = uni('NTFS');
      const b = Buffer.alloc(12 + n.length);
      b.writeUInt32LE(0x0000006f, 0); // CASE_SENSITIVE_SEARCH|CASE_PRESERVED|UNICODE|PERSISTENT_ACLS|FILE_COMPRESSION
      b.writeUInt32LE(255, 4);
      b.writeUInt32LE(n.length, 8);
      n.copy(b, 12);
      return b;
    }
    case FS.ObjectId: return Buffer.alloc(64);
    case FS.SectorSize: {
      const b = Buffer.alloc(28);
      b.writeUInt32LE(512, 0); b.writeUInt32LE(512, 4); b.writeUInt32LE(512, 8);
      b.writeUInt32LE(512, 12); b.writeUInt32LE(1, 16); b.writeUInt32LE(0, 20); b.writeUInt32LE(0, 24);
      return b;
    }
    default: fail(S.STATUS_INVALID_INFO_CLASS);
  }
}

// The SMB1 information levels of [MS-CIFS] 2.2.8.3, whose layouts differ from
// the [MS-FSCC] classes of the same name.
function smb1All(share, rel, st, open, name) {
  const t = share.times(rel, st);
  const n = uni(name);
  const b = Buffer.alloc(72 + n.length);
  b.writeBigUInt64LE(t.create, 0);
  b.writeBigUInt64LE(t.access, 8);
  b.writeBigUInt64LE(t.write, 16);
  b.writeBigUInt64LE(t.change, 24);
  b.writeUInt32LE(share.attributes(rel, st), 32);
  const eof = st.isDirectory() ? 0n : BigInt(st.size);
  b.writeBigUInt64LE(st.isDirectory() ? 0n : alloc(0, eof), 40);
  b.writeBigUInt64LE(eof, 48);
  const node = open ? open.node : null;
  const pending = !!(node && node.deletePending);
  b.writeUInt32LE(pending ? 0 : (st.isDirectory() ? 1 : Math.max(1, st.nlink)), 56);
  b.writeUInt8(pending ? 1 : 0, 60);
  b.writeUInt8(st.isDirectory() ? 1 : 0, 61);
  b.writeUInt32LE(0, 64);          // EaSize
  b.writeUInt32LE(n.length, 68);   // FileNameLength
  n.copy(b, 72);
  return b;
}
function smb1Standard(share, rel, st, node) {
  const b = Buffer.alloc(22);
  const eof = st.isDirectory() ? 0n : BigInt(st.size);
  b.writeBigUInt64LE(st.isDirectory() ? 0n : alloc(0, eof), 0);
  b.writeBigUInt64LE(eof, 8);
  const pending = !!(node && node.deletePending);
  b.writeUInt32LE(pending ? 0 : (st.isDirectory() ? 1 : Math.max(1, st.nlink)), 16);
  b.writeUInt8(pending ? 1 : 0, 20);
  b.writeUInt8(st.isDirectory() ? 1 : 0, 21);
  return b;
}

module.exports = {
  smb1All, smb1Standard,
  FILE, FS, basic, standard, internal, eaInfo, accessInfo, positionInfo, modeInfo,
  alignmentInfo, nameInfo, allInfo, networkOpen, attributeTag, compression,
  streamInfo, idInfo, dirEntry, fsInfo, fileIdOf, uni, alloc,
};
