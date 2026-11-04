'use strict';
// Connection plumbing: the direct-TCP transport of [MS-SMB2] 2.1, the server's
// shared state, and the dispatch to the SMB1 or SMB2 front end.
const net = require('net');
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');
const smbconf = require('./smbconf');
const passdb = require('./passdb');
const V = require('./vfs');
const { dialectValue } = smbconf;

const SMB1_MAGIC = Buffer.from([0xff, 0x53, 0x4d, 0x42]);
const SMB2_MAGIC = Buffer.from([0xfe, 0x53, 0x4d, 0x42]);
const SMB2_TRANSFORM = Buffer.from([0xfd, 0x53, 0x4d, 0x42]);
const SMB2_COMPRESSION = Buffer.from([0xfc, 0x53, 0x4d, 0x42]);

class Connection {
  constructor(server, socket) {
    this.server = server;
    this.socket = socket;
    this.buf = Buffer.alloc(0);
    this.proto = null;         // 'smb1' | 'smb2'
    this.dialect = 0;
    this.sessions = new Map();
    this.trees = new Map();
    this.nextSessionId = 1n;
    this.nextTreeId = 1;
    this.nextAsyncId = 1n;
    this.clientGuid = null;
    this.preauth = Buffer.alloc(64);
    this.negotiateRequest = null;
    this.negotiateResponse = null;
    this.credits = 1;
    this.pending = new Map();
    this.closed = false;
    this.cipher = 0;
    this.signAlg = 0;
    this.compressionAlgs = [];
    this.supportsChaining = false;
    this.smb1MidSeen = new Set();
    socket.on('data', (d) => this.onData(d));
    socket.on('error', () => this.destroy());
    socket.on('close', () => this.destroy());
  }

  log(...a) { if (this.server.debugLevel > 0) console.error('[smbd]', ...a); }

  onData(chunk) {
    this.buf = this.buf.length ? Buffer.concat([this.buf, chunk]) : chunk;
    for (;;) {
      if (this.buf.length < 4) return;
      const type = this.buf[0];
      const len = (this.buf[1] << 16) | (this.buf[2] << 8) | this.buf[3];
      if (type !== 0x00) {
        if (type === 0x81) { // NetBIOS session request on 139
          this.socket.write(Buffer.from([0x82, 0, 0, 0]));
          this.buf = this.buf.subarray(4 + len);
          continue;
        }
        this.destroy();
        return;
      }
      if (this.buf.length < 4 + len) return;
      const msg = this.buf.subarray(4, 4 + len);
      this.buf = this.buf.subarray(4 + len);
      try { this.dispatch(Buffer.from(msg)); }
      catch (e) { this.log('dispatch error', e && e.stack); this.destroy(); return; }
    }
  }

  dispatch(msg) {
    if (msg.length < 4) return;
    const magic = msg.subarray(0, 4);
    if (magic.equals(SMB2_MAGIC) || magic.equals(SMB2_TRANSFORM) || magic.equals(SMB2_COMPRESSION)) {
      this.server.smb2.handle(this, msg);
      return;
    }
    if (magic.equals(SMB1_MAGIC)) {
      this.server.smb1.handle(this, msg);
      return;
    }
    this.destroy();
  }

  send(payload) {
    if (this.closed) return;
    const hdr = Buffer.alloc(4);
    hdr[0] = 0;
    hdr[1] = (payload.length >> 16) & 0xff;
    hdr[2] = (payload.length >> 8) & 0xff;
    hdr[3] = payload.length & 0xff;
    this.socket.write(Buffer.concat([hdr, payload]));
  }

  destroy() {
    if (this.closed) return;
    this.closed = true;
    const fileops = require('./fileops');
    for (const t of this.trees.values()) {
      for (const o of Array.from(t.opens.values())) {
        try { fileops.closeOpen(o); } catch (e) {}
      }
    }
    if (this.s1) {
      for (const o of Array.from(this.s1.opens.values())) {
        try { fileops.closeOpen(o); } catch (e) {}
      }
      this.s1.opens.clear();
      this.s1.trees.clear();
      this.s1.searches.clear();
    }
    for (const s of this.sessions.values()) s.connections.delete(this);
    this.server.connections.delete(this);
    try { this.socket.destroy(); } catch (e) {}
  }
}

class Server {
  constructor(conf, opts = {}) {
    this.conf = conf;
    this.debugLevel = opts.debugLevel || 0;
    this.guid = crypto.randomBytes(16);
    this.startTime = Date.now();
    this.connections = new Set();
    this.shares = new Map();
    this.globalSessions = new Map();   // sessionId -> session (for multichannel binding)
    this.durables = new Map();         // durable handle store
    this.nextGlobalSession = 0x1000n;
    this.netbiosName = (conf.get('global', 'netbios name', 'SMBSRV') || 'SMBSRV').toUpperCase();
    this.workgroup = conf.get('global', 'workgroup', 'WORKGROUP');
    this.serverString = conf.get('global', 'server string', 'Samba');
    const priv = conf.get('global', 'private dir', '/var/lib/samba/private');
    this.passdbFile = passdb.parseBackend(conf.get('global', 'passdb backend', 'smbpasswd'), priv);
    this.users = passdb.load(this.passdbFile);
    this.minProtocol = dialectValue(conf.get('global', 'server min protocol', 'SMB2_02'), 0x0202);
    this.maxProtocol = dialectValue(conf.get('global', 'server max protocol', 'SMB3_11'), 0x0311);
    this.signingPolicy = String(conf.get('global', 'server signing', 'default')).toLowerCase();
    this.multichannel = conf.getBool('global', 'server multi channel support', true);
    this.leasesEnabled = conf.getBool('global', 'smb2 leases', true);
    this.durableEnabled = conf.getBool('global', 'durable handles', true);
    this.ntAclSupport = conf.getBool('global', 'nt acl support', true);
    // --- identity and guest policy -----------------------------------------
    // smb.conf(5) `map to guest` (default `never`) and `guest account`
    // (default `nobody`).  A session that `map to guest` let through carries
    // session.guest, which is what puts SMB2_SESSION_FLAG_IS_GUEST in the
    // SESSION_SETUP response and what a `guest ok = no` share refuses.
    this.mapToGuest = String(conf.get('global', 'map to guest', 'never'))
      .toLowerCase().trim();
    this.guestAccountName = String(conf.get('global', 'guest account', 'nobody')).trim();
    this.guestUser = passdb.guestAccount(this.guestAccountName);
    // The machine's own domain SID, in Samba's S-1-5-21-a-b-c shape; derived
    // from the NetBIOS name so it is stable across restarts, and shared by every
    // share so one file has one owner SID whichever share reaches it.
    const dh = crypto.createHash('md5').update(this.netbiosName).digest();
    this.domainSid = 'S-1-5-21-' + [0, 4, 8].map((o) => dh.readUInt32LE(o) & 0x7fffffff).join('-');
    // smb.conf(5) `algorithmic rid base`, the `2 * uid + base` in the owner SID.
    this.algorithmicRidBase = conf.getInt('global', 'algorithmic rid base', 1000);
    this.accountUids = passdb.accountUids(this.users);
    for (const s of conf.shares()) {
      const p = s.params.get('path');
      if (!p) continue;
      const octal = (key, dflt) => {
        const v = s.params.get(key) !== undefined ? s.params.get(key) : conf.get('global', key, undefined);
        if (v === undefined) return dflt;
        const n = parseInt(String(v).trim(), 8);
        return Number.isFinite(n) ? n : dflt;
      };
      const share = new V.Share(s.name, p, {
        storeDosAttributes: smbconf.parseBool(
          s.params.get('store dos attributes') !== undefined
            ? s.params.get('store dos attributes')
            : conf.get('global', 'store dos attributes', 'yes'), true),
        mapArchive: smbconf.parseBool(conf.get(s.name, 'map archive', 'yes'), true),
        mapHidden: smbconf.parseBool(conf.get(s.name, 'map hidden', 'no'), false),
        mapSystem: smbconf.parseBool(conf.get(s.name, 'map system', 'no'), false),
        createMask: octal('create mask', 0o744),
        directoryMask: octal('directory mask', 0o755),
        ntAclSupport: smbconf.parseBool(conf.get(s.name, 'nt acl support', 'yes'), true),
        aclMapFullControl: smbconf.parseBool(conf.get(s.name, 'acl map full control', 'yes'), true),
        machineName: this.netbiosName,
        domainSid: this.domainSid,
        algorithmicRidBase: this.algorithmicRidBase,
        accountUids: this.accountUids,
        guestUid: this.guestUser ? this.guestUser.uid : null,
        writeTimeUpdateDelay: Math.round(
          (parseInt(conf.get(s.name, 'smbd:writetimeupdatedelay', '0'), 10) || 0) / 1000),
      });
      share.readOnly = smbconf.parseBool(s.params.get('read only'), false) ||
                       !smbconf.parseBool(s.params.get('writeable') || s.params.get('writable') || 'yes', true);
      share.guestOk = smbconf.parseBool(s.params.get('guest ok'), false);
      share.encrypt = String(s.params.get('smb encrypt') || conf.get('global', 'smb encrypt', 'default')).toLowerCase();
      share.durable = smbconf.parseBool(s.params.get('durable handles'), this.durableEnabled);
      share.comment = s.params.get('comment') || '';
      share.strictLocking = String(s.params.get('strict locking') ||
        conf.get('global', 'strict locking', 'Auto')).toLowerCase()
        .replace(/^(true|1|on)$/, 'yes').replace(/^(false|0|off)$/, 'no');
      if (!['yes', 'no'].includes(share.strictLocking)) share.strictLocking = 'auto';
      this.shares.set(s.name.toLowerCase(), share);
    }
    const safeRequire = (m) => {
      try { return require(m); } catch (e) { return { handle: (c) => c.destroy() }; }
    };
    this.smb1 = safeRequire('./smb1');
    this.smb2 = safeRequire('./smb2');
  }

  // smb.conf(5) `map to guest`: resolve an account name from the session-setup
  // blob.  Returns { user, guest, status }:
  //
  //   status 'ok'            a real account; the caller must still check the
  //                          password against user.nt
  //   status 'guest'         `map to guest` caught an unknown account name (an
  //                          empty name included) and this is the guest account;
  //                          no password check applies, and the session gets
  //                          SMB2_SESSION_FLAG_IS_GUEST
  //   status 'no-such-user'  STATUS_LOGON_FAILURE
  //
  // `map to guest = bad password` additionally sends a real account with the
  // wrong password to the guest account; mapsBadPassword() says whether it does.
  lookupUser(name) {
    return passdb.resolve(this.users, name,
                          { mapToGuest: this.mapToGuest, guest: this.guestUser });
  }
  mapsBadPassword() { return passdb.mapsBadPassword(this.mapToGuest); }

  // Whether a guest session may connect to `shareName` -- smb.conf(5)
  // `guest ok` (default no).  IPC$ is always reachable.
  guestMayConnect(shareName) {
    const n = String(shareName || '').toLowerCase();
    if (n === 'ipc$') return true;
    const share = this.shares.get(n);
    return Boolean(share && share.guestOk);
  }

  encryptPolicy(shareName) {
    const share = this.shares.get(String(shareName).toLowerCase());
    let v = share ? share.encrypt : String(this.conf.get('global', 'smb encrypt', 'default')).toLowerCase();
    if (v === 'default' || v === '') v = 'desired';
    if (v === 'yes' || v === 'true' || v === 'auto' || v === 'enabled') v = 'desired';
    if (v === 'no' || v === 'false' || v === 'disabled') v = 'off';
    if (v === 'mandatory') v = 'required';
    return v;
  }
  signingRequired() {
    const p = this.signingPolicy;
    return p === 'mandatory' || p === 'required' || p === 'yes' || p === 'true';
  }
  signingEnabled() {
    const p = this.signingPolicy;
    return p !== 'disabled' && p !== 'no' && p !== 'false';
  }

  // The addresses this server is bound to, for the witness service's
  // InterfaceList ([MS-SWN] 3.1.1.1).
  listenAddresses() {
    return this.boundAddresses || [];
  }

  listen(ports, address, cb) {
    let remaining = ports.length;
    this.servers = [];
    this.boundAddresses = [address || '0.0.0.0'];
    for (const port of ports) {
      const srv = net.createServer((sock) => {
        sock.setNoDelay(true);
        const c = new Connection(this, sock);
        this.connections.add(c);
      });
      srv.on('error', (e) => { console.error('smbd: listen ' + port + ': ' + e.message); process.exit(1); });
      srv.listen(port, address, () => { if (--remaining === 0 && cb) cb(); });
      this.servers.push(srv);
    }
  }
}

module.exports = { Server, Connection, SMB1_MAGIC, SMB2_MAGIC, SMB2_TRANSFORM, SMB2_COMPRESSION };
