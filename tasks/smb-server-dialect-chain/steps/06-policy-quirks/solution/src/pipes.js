'use strict';
// The named-pipe surface of IPC$: what a CREATE on \pipe\witness opens and
// what WRITE, READ and FSCTL_PIPE_TRANSCEIVE drive.
//
// There is deliberately no SMB framing in this file.  Every entry point takes
// and returns plain Buffers plus an NTSTATUS, so the SMB1 and SMB2 front ends
// can both use it.  Pipe semantics follow [MS-SMB2] 2.2.13 (CREATE against
// IPC$), 3.3.5.9, 2.2.31 (FSCTL_PIPE_TRANSCEIVE) and [MS-CIFS] 2.2.4.x.
//
// A witness pipe is a message-mode, byte-duplex pipe: one WRITE carries one or
// more whole DCE/RPC PDUs, one READ returns at most one PDU, and a READ whose
// buffer is too small gets the head of the message plus STATUS_BUFFER_OVERFLOW.

const dcerpc = require('./dcerpc');
const witnessMod = require('./witness');

// NTSTATUS values used here; same numbers as golden/src/ntstatus.js
// ([MS-ERREF] 2.3.1).  Kept local so this module stands alone.
const STATUS_SUCCESS = 0x00000000;
const STATUS_PENDING = 0x00000103;
const STATUS_BUFFER_OVERFLOW = 0x80000005;
const STATUS_INVALID_PARAMETER = 0xc000000d;
const STATUS_INVALID_DEVICE_REQUEST = 0xc0000010;
const STATUS_OBJECT_NAME_INVALID = 0xc0000033;
const STATUS_OBJECT_NAME_NOT_FOUND = 0xc0000034;
const STATUS_PIPE_EMPTY = 0xc00000d9;
const STATUS_PIPE_BROKEN = 0xc000014b;
const STATUS_PIPE_DISCONNECTED = 0xc00000b0;
const STATUS_CANCELLED = 0xc0000120;

// [MS-FSCC] 2.4.x pipe attributes, reported by the SMB layer in a QUERY_INFO.
const PIPE_TYPE_MESSAGE = 0x00000001;
const PIPE_READMODE_MESSAGE = 0x00000001;

const DEFAULT_MAX_INPUT = 4 * 1024 * 1024;   // cap on unparsed WRITE backlog

// ---------------------------------------------------------------------------
// Name handling.  A CREATE against IPC$ may present the pipe as "witness",
// "\pipe\witness", "PIPE\witness" or "\PIPE\witness"; SMB1 NT_CREATE_ANDX uses
// the leading-backslash form.  Everything else must fail with
// STATUS_OBJECT_NAME_NOT_FOUND.
// ---------------------------------------------------------------------------
function normalizePipeName(name) {
  if (typeof name !== 'string') return null;
  let s = name.replace(/\//g, '\\');
  s = s.replace(/^\\+/, '');
  const low = s.toLowerCase();
  if (low === 'pipe' || low === '') return null;
  if (low.startsWith('pipe\\')) s = s.slice(5);
  s = s.replace(/^\\+/, '');
  if (!s || s.indexOf('\\') >= 0) return null;    // no sub-paths under \pipe
  return s.toLowerCase();
}

// ---------------------------------------------------------------------------
// NamedPipe
// ---------------------------------------------------------------------------
class NamedPipe {
  constructor(name, rpc, opts) {
    const o = opts || {};
    this.name = name;                   // normalized, e.g. 'witness'
    this.rpc = rpc;                     // a DceRpcServer
    this.inBuf = Buffer.alloc(0);       // bytes written but not yet a whole PDU
    this.outQueue = [];                 // complete messages waiting for READ
    this.partial = null;                // head of a message a short READ split
    this.readWaiter = null;             // a READ or TRANSCEIVE that went pending
    this.rpcWaiter = null;              // the pending DceRpcServer call
    this.closed = false;
    this.maxInput = o.maxInput || DEFAULT_MAX_INPUT;
    this.registry = o.registry || null;
    this.context = o.context || null;   // whatever the SMB layer wants to keep
    this.pipeType = PIPE_TYPE_MESSAGE;
    this.readMode = PIPE_READMODE_MESSAGE;
  }

  // -- WRITE ---------------------------------------------------------------
  // Returns { status, written }.  Any DCE/RPC PDU that becomes complete is
  // dispatched and its response queued for the next READ.
  write(buf) {
    if (this.closed) return { status: STATUS_PIPE_BROKEN, written: 0 };
    const data = Buffer.isBuffer(buf) ? buf : Buffer.alloc(0);
    if (this.inBuf.length + data.length > this.maxInput) {
      return { status: STATUS_INVALID_PARAMETER, written: 0 };
    }
    this.inBuf = this.inBuf.length ? Buffer.concat([this.inBuf, data]) : Buffer.from(data);
    this._drainInput();
    return { status: STATUS_SUCCESS, written: data.length };
  }

  _drainInput() {
    for (;;) {
      if (this.inBuf.length < dcerpc.HDR_LEN) return;
      const hdr = dcerpc.parseHeader(this.inBuf);
      if (!hdr || hdr.fragLength < dcerpc.HDR_LEN) {
        // Not a PDU at all: drop the backlog rather than spin forever.
        this.inBuf = Buffer.alloc(0);
        return;
      }
      if (this.inBuf.length < hdr.fragLength) return;
      const pdu = this.inBuf.subarray(0, hdr.fragLength);
      this.inBuf = Buffer.from(this.inBuf.subarray(hdr.fragLength));
      this._dispatch(Buffer.from(pdu));
    }
  }

  _dispatch(pdu) {
    const handle = this.rpc.handleAsync(pdu, (err, resp, frags) => {
      this.rpcWaiter = null;
      if (this.closed) return;
      if (err || !resp) return;                 // nothing to answer (auth3, ...)
      for (const f of (frags && frags.length ? frags : [resp])) this.outQueue.push(f);
      this._serviceReadWaiter();
    });
    if (handle && handle.cancel) this.rpcWaiter = handle;
  }

  // -- READ ----------------------------------------------------------------
  // read(max)      -> { status, data } or { status: STATUS_PENDING }
  // read(max, cb)  -> same, but when it returns STATUS_PENDING the real
  //                   result arrives as cb(result) and the return value
  //                   carries a cancel() for SMB2 CANCEL / pipe close.
  read(max, cb) {
    if (this.closed) return { status: STATUS_PIPE_BROKEN, data: Buffer.alloc(0) };
    const n = (max === undefined || max === null) ? 0xffffffff : (max >>> 0);
    const ready = this._takeMessage(n);
    if (ready) return ready;
    if (!cb) return { status: STATUS_PENDING, data: Buffer.alloc(0) };
    return this._waitForMessage(n, cb);
  }

  _takeMessage(max) {
    let msg = this.partial;
    if (msg === null || msg === undefined) {
      if (!this.outQueue.length) return null;
      msg = this.outQueue.shift();
    }
    this.partial = null;
    if (max === 0) {
      // A zero-length READ on a non-empty message is a buffer overflow.
      this.partial = msg;
      return { status: STATUS_BUFFER_OVERFLOW, data: Buffer.alloc(0) };
    }
    if (msg.length <= max) return { status: STATUS_SUCCESS, data: msg };
    this.partial = Buffer.from(msg.subarray(max));
    return { status: STATUS_BUFFER_OVERFLOW, data: Buffer.from(msg.subarray(0, max)) };
  }

  _waitForMessage(max, cb) {
    const prev = this.readWaiter;
    if (prev) prev.deliver({ status: STATUS_CANCELLED, data: Buffer.alloc(0) });
    let done = false;
    const waiter = {
      max,
      deliver: (res) => {
        if (done) return;
        done = true;
        if (this.readWaiter === waiter) this.readWaiter = null;
        cb(res);
      },
    };
    this.readWaiter = waiter;
    return {
      status: STATUS_PENDING,
      data: Buffer.alloc(0),
      cancel: () => {
        if (done) return;
        if (this.rpcWaiter && this.rpcWaiter.cancel) {
          const w = this.rpcWaiter; this.rpcWaiter = null;
          try { w.cancel(); } catch (e) { /* ignore */ }
        }
        waiter.deliver({ status: STATUS_CANCELLED, data: Buffer.alloc(0) });
      },
    };
  }

  _serviceReadWaiter() {
    if (!this.readWaiter) return;
    const res = this._takeMessage(this.readWaiter.max);
    if (res) this.readWaiter.deliver(res);
  }

  // -- FSCTL_PIPE_TRANSCEIVE ([MS-SMB2] 2.2.31, 3.3.5.15.11) ---------------
  // One write immediately followed by one read on the same handle.
  transceive(buf, maxOut, cb) {
    if (this.closed) return { status: STATUS_PIPE_BROKEN, data: Buffer.alloc(0) };
    const w = this.write(buf);
    if (w.status !== STATUS_SUCCESS) return { status: w.status, data: Buffer.alloc(0) };
    return this.read(maxOut === undefined ? 0xffffffff : maxOut, cb);
  }

  // -- FSCTL_PIPE_PEEK ------------------------------------------------------
  // Raw numbers only; the SMB layer builds FILE_PIPE_PEEK_BUFFER from them.
  peek(max) {
    const msg = this.partial !== null && this.partial !== undefined
      ? this.partial : (this.outQueue.length ? this.outQueue[0] : null);
    let total = msg ? msg.length : 0;
    for (let i = (this.partial ? 0 : 1); i < this.outQueue.length; i++) total += this.outQueue[i].length;
    const n = max === undefined ? (msg ? msg.length : 0) : Math.min(max >>> 0, msg ? msg.length : 0);
    return {
      readDataAvailable: total,
      messageLength: msg ? msg.length : 0,
      data: msg ? Buffer.from(msg.subarray(0, n)) : Buffer.alloc(0),
    };
  }

  available() {
    let total = this.partial ? this.partial.length : 0;
    for (const m of this.outQueue) total += m.length;
    return total;
  }

  // -- CLOSE ---------------------------------------------------------------
  close() {
    if (this.closed) return;
    this.closed = true;
    if (this.rpcWaiter && this.rpcWaiter.cancel) {
      const w = this.rpcWaiter; this.rpcWaiter = null;
      try { w.cancel(); } catch (e) { /* ignore */ }
    }
    if (this.readWaiter) {
      this.readWaiter.deliver({ status: STATUS_PIPE_DISCONNECTED, data: Buffer.alloc(0) });
    }
    this.outQueue.length = 0;
    this.partial = null;
    this.inBuf = Buffer.alloc(0);
    try { this.rpc.cancelAll(); } catch (e) { /* ignore */ }
    if (this.registry) this.registry._closed(this);
  }
}

// ---------------------------------------------------------------------------
// PipeRegistry: the set of pipes IPC$ offers.  Each open gets its own
// DceRpcServer (a bind is per file handle) but they share the service objects.
// ---------------------------------------------------------------------------
class PipeRegistry {
  constructor(opts) {
    const o = opts || {};
    this.factories = new Map();
    this.open_ = new Set();
    this.witness = o.witness || null;
    if (this.witness !== false) {
      if (!this.witness) this.witness = new witnessMod.WitnessService(o.witnessOptions || {});
      this.registerWitness(this.witness);
    }
  }

  // Make \pipe\witness available, backed by `service` (a WitnessService).
  registerWitness(service) {
    this.witness = service;
    this.register('witness', () => new dcerpc.DceRpcServer([service.interfaceDefinition()], {
      secondaryAddress: '\\PIPE\\witness',
    }));
    return this;
  }

  // register(name, () => DceRpcServer)
  register(name, factory) {
    const n = normalizePipeName(name);
    if (!n) throw new Error('bad pipe name: ' + name);
    this.factories.set(n, factory);
    return this;
  }

  has(name) {
    const n = normalizePipeName(name);
    return !!(n && this.factories.has(n));
  }

  // open(name, ctx) -> { status, pipe }
  // STATUS_OBJECT_NAME_NOT_FOUND for every pipe this server does not offer.
  open(name, ctx) {
    const n = normalizePipeName(name);
    if (n === null) {
      // "\pipe" on its own, or a path with a directory component.
      return { status: STATUS_OBJECT_NAME_INVALID, pipe: null };
    }
    const factory = this.factories.get(n);
    if (!factory) return { status: STATUS_OBJECT_NAME_NOT_FOUND, pipe: null };
    const pipe = new NamedPipe(n, factory(), { registry: this, context: ctx });
    this.open_.add(pipe);
    return { status: STATUS_SUCCESS, pipe };
  }

  _closed(pipe) { this.open_.delete(pipe); }

  closeAll() { for (const p of Array.from(this.open_)) p.close(); }
}

module.exports = {
  NamedPipe, PipeRegistry, normalizePipeName,
  STATUS_SUCCESS, STATUS_PENDING, STATUS_BUFFER_OVERFLOW,
  STATUS_INVALID_PARAMETER, STATUS_INVALID_DEVICE_REQUEST,
  STATUS_OBJECT_NAME_INVALID, STATUS_OBJECT_NAME_NOT_FOUND,
  STATUS_PIPE_EMPTY, STATUS_PIPE_BROKEN, STATUS_PIPE_DISCONNECTED, STATUS_CANCELLED,
  PIPE_TYPE_MESSAGE, PIPE_READMODE_MESSAGE,
};
