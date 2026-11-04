'use strict';
// smbpasswd(5): name:uid:LANMAN hash:NT hash:flags:LCT-<hex time>
//
// Also the small amount of NSS this server needs.  Samba's passdb sits on top of
// the UNIX account database: an smbpasswd entry whose name has no `/etc/passwd`
// account is not a usable account, and a uid with no smbpasswd entry has no
// domain RID.  Both facts are visible on the wire, in the owner SID a file's
// descriptor carries, so they are modelled here rather than assumed away.
const fs = require('fs');

const PASSWD_FILE = process.env.SMBD_PASSWD_FILE || '/etc/passwd';

function parseBackend(spec, privateDir) {
  const s = String(spec || '').trim();
  const m = /^smbpasswd(?::(.*))?$/i.exec(s);
  if (m) return m[1] ? m[1].trim() : (privateDir ? privateDir + '/smbpasswd' : '/etc/samba/smbpasswd');
  if (s.startsWith('/')) return s;
  return privateDir ? privateDir + '/smbpasswd' : '/etc/samba/smbpasswd';
}

// --- the UNIX account database --------------------------------------------

let unixCache = null;

function unixAccounts() {
  if (unixCache) return unixCache;
  const byName = new Map(), byUid = new Map();
  let text = '';
  try { text = fs.readFileSync(PASSWD_FILE, 'utf8'); } catch (e) { text = ''; }
  for (const line of text.split(/\r?\n/)) {
    if (!line || line.startsWith('#')) continue;
    const f = line.split(':');
    if (f.length < 4) continue;
    const uid = parseInt(f[2], 10);
    const gid = parseInt(f[3], 10);
    if (!Number.isFinite(uid) || !Number.isFinite(gid)) continue;
    const rec = { name: f[0], uid, gid };
    byName.set(f[0].toLowerCase(), rec);
    if (!byUid.has(uid)) byUid.set(uid, rec);
  }
  unixCache = { byName, byUid };
  return unixCache;
}

function unixUser(name) {
  return unixAccounts().byName.get(String(name || '').toLowerCase()) || null;
}
function unixUserByUid(uid) {
  return unixAccounts().byUid.get(uid) || null;
}
// Only used by the tests that need a second view of the file.
function forgetUnixAccounts() { unixCache = null; }

// --- smbpasswd(5) ----------------------------------------------------------

function load(file) {
  const users = new Map();
  let text = '';
  try { text = fs.readFileSync(file, 'utf8'); } catch (e) { return users; }
  for (const line of text.split(/\r?\n/)) {
    if (!line || line.startsWith('#')) continue;
    const f = line.split(':');
    if (f.length < 4) continue;
    const name = f[0];
    const uid = parseInt(f[1], 10);
    const lm = /^[0-9A-Fa-f]{32}$/.test(f[2]) ? Buffer.from(f[2], 'hex') : null;
    const nt = /^[0-9A-Fa-f]{32}$/.test(f[3]) ? Buffer.from(f[3], 'hex') : null;
    const flags = (f[4] || '[U          ]').replace(/[\[\] ]/g, '');
    // smbpasswd(5) carries the uid itself; the UNIX account, when the host has
    // one, is where the primary gid comes from.  Samba additionally discards an
    // smbpasswd line whose name has no UNIX account at all -- not modelled,
    // because this server's account database is the smbpasswd file and the
    // hosts it runs on do not carry a matching /etc/passwd entry (SD-NOTES.md
    // §5.7).
    const unix = unixUser(name);
    const theUid = unix ? unix.uid : (Number.isFinite(uid) ? uid : 0);
    users.set(name.toLowerCase(), {
      name, uid: theUid,
      gid: unix ? unix.gid : theUid,
      hasUnixAccount: Boolean(unix),
      lm, nt,
      disabled: flags.includes('D'), noPassword: flags.includes('N'),
    });
  }
  return users;
}

// The uids that carry a domain RID: the ones the account database holds.
// Everything else falls back to the UNIX-user authority, S-1-22-1-<uid>.
function accountUids(users) {
  const out = new Set();
  for (const u of users.values()) if (Number.isFinite(u.uid)) out.add(u.uid);
  return out;
}

// The account `guest account` names, as a user record.  It is not in
// smbpasswd(5) and has no password: it is reached only through `map to guest`.
function guestAccount(name) {
  const nm = String(name || 'nobody');
  const unix = unixUser(nm);
  if (!unix) return null;
  return { name: unix.name, uid: unix.uid, gid: unix.gid, hasUnixAccount: true,
           lm: null, nt: null, disabled: false, noPassword: true, guest: true };
}

// smb.conf(5) `map to guest`.  Returns { user, guest, status } where `status` is
// one of 'ok', 'guest' or 'no-such-user'; the caller still has to verify the
// password for a non-guest answer.
//
//   never             an unknown user is a logon failure (the default)
//   bad user          an unknown user becomes the guest account
//   bad password      a bad password becomes the guest account too
//   bad uid           only meaningful for a domain member; treated as `never`
//
// An empty user name counts as an unknown user: the reference server maps an
// NTLMSSP AUTHENTICATE with no name to the guest account under `bad user`,
// rather than to the anonymous null session it produces under `never`.
function resolve(users, name, opts = {}) {
  const policy = String(opts.mapToGuest || 'never').toLowerCase().trim();
  const key = String(name || '').toLowerCase();
  const found = key ? users.get(key) : null;
  if (found && found.nt && !found.disabled) return { user: found, guest: false, status: 'ok' };
  if (found && !found.disabled && found.noPassword) return { user: found, guest: false, status: 'ok' };
  const mapsBadUser = policy === 'bad user' || policy === 'bad password' ||
                      policy === 'bad uid';
  if (!found && mapsBadUser && opts.guest) {
    return { user: opts.guest, guest: true, status: 'guest' };
  }
  return { user: found || null, guest: false, status: found ? 'ok' : 'no-such-user' };
}

// Whether `map to guest` also catches a correct account with a wrong password.
function mapsBadPassword(policy) {
  return String(policy || 'never').toLowerCase().trim() === 'bad password';
}

module.exports = {
  load, parseBackend, accountUids, guestAccount, resolve, mapsBadPassword,
  unixUser, unixUserByUid, forgetUnixAccounts, PASSWD_FILE,
};
