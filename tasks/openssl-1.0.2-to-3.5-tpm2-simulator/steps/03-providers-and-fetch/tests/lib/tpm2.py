#!/usr/bin/env python3
"""Enough of the TPM 2.0 wire format to drive the simulator over its own port.

The simulator is the only thing this codebase builds that has an interface a
user could name, so the end-to-end half of the behaviour suite speaks to it
the way anything else would: the TCP framing in src/simulator/TpmTcpProtocol.h
wrapping the command buffers of TCG TPM 2.0 rev 01.16.  Nothing here knows
what any answer should be -- expectations come from recording the baseline.

Two properties of this simulator shape everything below.

  * main() calls TPM_Manufacture() unconditionally at every process start
    (src/simulator/TPMCmds.c:141-155), so the NVChip file left by a previous
    process is wiped.  Every process is a factory-fresh TPM, which is what
    lets a session recover from a crash by restarting rather than by trying
    to unwind state.

  * Manufacture draws the primary seeds and the three hierarchy proofs from
    CryptGenerateRandom (src/tpm/subsystem/Hierarchy.c), so anything derived
    from a real hierarchy differs between processes.  Callers stay in the
    NULL hierarchy, where the spec says the ticket is empty, and record sizes
    or return codes for the few answers that are genuinely random.
"""
import os
import socket
import struct
import subprocess
import time

# --- TCP framing (src/simulator/TpmTcpProtocol.h) --------------------------
SIGNAL_POWER_ON = 1
SIGNAL_POWER_OFF = 2
SEND_COMMAND = 8
SIGNAL_NV_ON = 11
REMOTE_HANDSHAKE = 15
SESSION_END = 20
STOP = 21

# --- structure tags, handles, return codes ---------------------------------
ST_NO_SESSIONS = 0x8001
ST_SESSIONS = 0x8002
ST_HASHCHECK = 0x8024
RH_OWNER = 0x40000001
RH_NULL = 0x40000007
RS_PW = 0x40000009
SE_HMAC, SE_POLICY, SE_TRIAL = 0x00, 0x01, 0x03

# --- algorithms ------------------------------------------------------------
ALG = {
    "RSA": 0x0001, "SHA1": 0x0004, "HMAC": 0x0005, "AES": 0x0006,
    "MGF1": 0x0007, "KEYEDHASH": 0x0008, "XOR": 0x000A, "SHA256": 0x000B,
    "SHA384": 0x000C, "SHA512": 0x000D, "NULL": 0x0010, "SM3_256": 0x0012,
    "SM4": 0x0013, "RSASSA": 0x0014, "RSAES": 0x0015, "RSAPSS": 0x0016,
    "OAEP": 0x0017, "ECDSA": 0x0018, "ECDH": 0x0019, "SM2": 0x001B,
    "ECSCHNORR": 0x001C, "ECMQV": 0x001D, "KDF1_SP800_56a": 0x0020,
    "KDF2": 0x0021, "KDF1_SP800_108": 0x0022, "ECC": 0x0023,
    "SYMCIPHER": 0x0025, "CTR": 0x0040, "OFB": 0x0041, "CBC": 0x0042,
    "CFB": 0x0043, "ECB": 0x0044,
}
CURVE = {"NIST_P192": 0x0001, "NIST_P224": 0x0002, "NIST_P256": 0x0003,
         "NIST_P384": 0x0004, "NIST_P521": 0x0005, "BN_P256": 0x0010,
         "BN_P638": 0x0011, "SM2_P256": 0x0020}
DIGEST_SIZE = {"SHA1": 20, "SHA256": 32, "SHA384": 48, "SHA512": 64}

# --- command codes (src/tpm/include/CommandAttributeData.c) ----------------
CC = {
    "Clear": 0x0126, "PCR_Event": 0x013C, "PCR_Reset": 0x013D,
    "SequenceComplete": 0x013E, "IncrementalSelfTest": 0x0142,
    "SelfTest": 0x0143, "Startup": 0x0144, "Shutdown": 0x0145,
    "StirRandom": 0x0146, "ECDH_ZGen": 0x0154, "HMAC": 0x0155,
    "RSA_Decrypt": 0x0159, "HMAC_Start": 0x015B, "SequenceUpdate": 0x015C,
    "Sign": 0x015D, "ContextLoad": 0x0161, "ContextSave": 0x0162,
    "ECDH_KeyGen": 0x0163,
    "EncryptDecrypt": 0x0164, "FlushContext": 0x0165, "LoadExternal": 0x0167,
    "MakeCredential": 0x0168, "PolicyAuthValue": 0x016B,
    "PolicyCommandCode": 0x016C, "PolicyCounterTimer": 0x016D,
    "PolicyCpHash": 0x016E, "PolicyLocality": 0x016F,
    "PolicyNameHash": 0x0170, "PolicyOR": 0x0171, "ReadPublic": 0x0173,
    "RSA_Encrypt": 0x0174, "StartAuthSession": 0x0176,
    "VerifySignature": 0x0177, "ECC_Parameters": 0x0178,
    "GetCapability": 0x017A, "GetRandom": 0x017B, "GetTestResult": 0x017C,
    "Hash": 0x017D, "PCR_Read": 0x017E, "PolicyPCR": 0x017F,
    "PolicyRestart": 0x0180, "ReadClock": 0x0181, "PCR_Extend": 0x0182,
    "EventSequenceComplete": 0x0185, "HashSequenceStart": 0x0186,
    "PolicyPhysicalPresence": 0x0187, "PolicyDuplicationSelect": 0x0188,
    "PolicyGetDigest": 0x0189, "TestParms": 0x018A, "Commit": 0x018B,
    "PolicyPassword": 0x018C, "ZGen_2Phase": 0x018D, "EC_Ephemeral": 0x018E,
}

# --- object attributes -----------------------------------------------------
A_FIXEDTPM = 1 << 1
A_FIXEDPARENT = 1 << 4
A_SENSITIVEDATAORIGIN = 1 << 5
A_USERWITHAUTH = 1 << 6
A_NODA = 1 << 10
A_RESTRICTED = 1 << 16
A_DECRYPT = 1 << 17
A_SIGN = 1 << 18

# An externally supplied key cannot claim the TPM made it, so fixedTPM,
# fixedParent and sensitiveDataOrigin are all CLEAR.  decrypt and sign are
# both SET, which for RSA and ECC forces the key's own scheme to be NULL and
# leaves the scheme choice to each command -- exactly what a corpus wants.
ATTR_ANY = A_USERWITHAUTH | A_NODA | A_DECRYPT | A_SIGN
ATTR_SIGN = A_USERWITHAUTH | A_NODA | A_SIGN
# A symmetric key does both directions through the decrypt attribute alone;
# setting sign as well is TPM_RC_ATTRIBUTES on the public area.
ATTR_SYM = A_USERWITHAUTH | A_NODA | A_DECRYPT

# NV is a file at a fixed absolute path, not one per working directory.
NV_FILE = "/tmp/NVChip"

# How many times to re-draw a startup that lost the entropy coin toss.
#
# src/platform/Entropy.c:57 is a hand-written stand-in for Windows rand_s.  It
# reads ONE byte into a uint32_t and returns success, so the upper three bytes
# are whatever was last in that stack slot -- unchanged for the duration of the
# collection loop.  _plat__GetEntropy:121 then applies the FIPS 140-2 annex C
# continuous test and fails, stickily, if two consecutive draws are equal.
# Equality is therefore decided by one byte: p = 1/256 per comparison, fifteen
# comparisons to fill MAX_RNG_ENTROPY_SIZE, so 1 - (255/256)**15 = 5.7% of
# _cpri__RngStartup calls fail.  Measured over 40 starts of the untouched
# baseline: 4 aborted.  This is the baseline's behaviour, not the harness's,
# and it is not fixed here -- it is retried around, so that what a case records
# is the case and not the coin.
ENTROPY_TRIES = 8


class Dead(Exception):
    """The simulator stopped answering: socket error, timeout, or exit."""


class Flaky(Exception):
    """The run lost a coin toss the port has no control over; try again."""


# --- marshalling -----------------------------------------------------------
def u8(v):
    return struct.pack(">B", v)


def u16(v):
    return struct.pack(">H", v)


def u32(v):
    return struct.pack(">I", v)


def b2(x):
    """TPM2B: a UINT16 length followed by that many bytes."""
    return struct.pack(">H", len(x)) + x


def unb2(buf, off=0):
    n = struct.unpack_from(">H", buf, off)[0]
    return buf[off + 2:off + 2 + n], off + 2 + n


def h(x):
    return bytes.fromhex(x)


def alg(name):
    """Algorithm id by name, or a literal if the caller wants a bad one."""
    return name if isinstance(name, int) else ALG[name]


def pcr_select(indices, bank="SHA256"):
    """TPML_PCR_SELECTION over one bank."""
    mask = bytearray(3)
    for i in indices:
        mask[i // 8] |= 1 << (i % 8)
    return u32(1) + u16(alg(bank)) + u8(3) + bytes(mask)


def sym_def_object(algo="NULL", bits=None, mode=None):
    """TPMT_SYM_DEF_OBJECT; NULL carries no further fields."""
    if alg(algo) == ALG["NULL"]:
        return u16(ALG["NULL"])
    return u16(alg(algo)) + u16(bits) + u16(alg(mode))


def public(typ, nalg, attrs, parms, unique, policy=b""):
    """TPM2B_PUBLIC around a TPMT_PUBLIC."""
    return b2(u16(alg(typ)) + u16(alg(nalg)) + u32(attrs) + b2(policy)
              + parms + unique)


def sensitive(typ, secret, seed=b"", auth=b""):
    """TPM2B_SENSITIVE around a TPMT_SENSITIVE."""
    return b2(u16(alg(typ)) + b2(auth) + b2(seed) + b2(secret))


def rsa_parms(bits, exponent, scheme="NULL", shash="NULL",
              symmetric=None):
    """TPMS_RSA_PARMS.  exponent 0 means "the default", which is 65537."""
    sym = sym_def_object() if symmetric is None else symmetric
    sch = u16(alg(scheme))
    if alg(scheme) not in (ALG["NULL"],):
        sch += u16(alg(shash))
    return sym + sch + u16(bits) + u32(exponent)


def ecc_parms(curve, scheme="NULL", shash="NULL", kdf="NULL",
              symmetric=None):
    """TPMS_ECC_PARMS."""
    sym = sym_def_object() if symmetric is None else symmetric
    sch = u16(alg(scheme))
    if alg(scheme) != ALG["NULL"]:
        sch += u16(alg(shash))
    k = u16(alg(kdf))
    if alg(kdf) != ALG["NULL"]:
        k += u16(alg(shash))
    return sym + sch + u16(curve if isinstance(curve, int)
                           else CURVE[curve]) + k


def keyedhash_parms(scheme="HMAC", shash="SHA256"):
    """TPMS_KEYEDHASH_PARMS."""
    s = u16(alg(scheme))
    if alg(scheme) == ALG["HMAC"]:
        s += u16(alg(shash))
    elif alg(scheme) == ALG["XOR"]:
        s += u16(alg(shash)) + u16(ALG["MGF1"])
    return s


def null_ticket():
    """TPMT_TK_HASHCHECK for a digest the TPM did not produce.

    The hierarchy field has to be a real handle; 0 is not one, and Sign
    rejects the whole ticket as a malformed third parameter rather than
    saying anything about the hierarchy.
    """
    return u16(ST_HASHCHECK) + u32(RH_NULL) + b2(b"")


def pw_session(auth=b""):
    """One password authorisation area."""
    return u32(RS_PW) + b2(b"") + u8(0) + b2(auth)


# --- the simulator itself --------------------------------------------------
class Tpm:
    """One simulator process, powered on and started up.

    Every method raises Dead rather than returning a sentinel when the
    process stops answering: a corpus that treats "no answer" as an ordinary
    result records a crash as if it were behaviour.
    """

    def __init__(self, binary, port, workdir, timeout=30.0, ports=None):
        self.binary = binary
        self.port = port
        # A callable handing out an unused port pair, if one is available.
        # TcpServer.c:150 binds without SO_REUSEADDR, and a simulator killed
        # while holding an accepted connection leaves that connection in
        # TIME_WAIT for a minute, so rebinding the same port is "Bind error" --
        # indistinguishable, from out here, from a simulator that cannot start.
        # Retrying on a fresh port is what keeps those two apart.
        self.ports = ports
        self.workdir = workdir
        self.timeout = timeout
        self.proc = None
        self.sock = None
        self.starts = 0

    # -- lifecycle --
    def start(self, tries=ENTROPY_TRIES):
        """Power on a factory-blank TPM listening on self.port.

        _plat__NVEnable opens "/tmp/NVChip" by absolute path on anything but
        Windows (src/platform/NVMem.c:119), so the working directory does not
        separate one process's NV from another's, and NVMem.c:151 asserts the
        file is exactly NV_MEMORY_SIZE -- a process killed mid-write leaves a
        file that aborts the next one with SIGABRT before it ever listens.
        Removing the file is what makes a session independent of the sessions
        before it.  One consequence: two recorders cannot share a container.

        The retry is for a different failure.  See ENTROPY_TRIES: roughly one
        start in seventeen aborts inside TPM_Manufacture before the socket is
        ever opened, for reasons that have nothing to do with the case about
        to run.  Each attempt is an independent draw, so retrying converts a
        6% per-process flake into a negligible one while leaving a port that
        cannot start at all failing every attempt.
        """
        os.makedirs(self.workdir, exist_ok=True)
        self.log = open(os.path.join(self.workdir, "sim.log"), "wb")
        last = None
        for attempt in range(tries):
            try:
                self._spawn()
                self.starts = attempt + 1
                return
            except Dead as e:
                last = e
                self._kill()
        raise Dead("%s (%d attempts)" % (last, tries))

    def _spawn(self):
        try:
            os.remove(NV_FILE)
        except FileNotFoundError:
            pass
        if self.ports is not None:
            self.port = self.ports()
        self.proc = subprocess.Popen(
            [self.binary, str(self.port)], cwd=self.workdir,
            stdin=subprocess.DEVNULL, stdout=self.log, stderr=self.log)
        # The platform server runs on its own thread on port+1 and is not
        # listening merely because the command port is; signalling too early
        # is a connection refused that looks like a crash.
        self._wait_for_port(self.port)
        self._wait_for_port(self.port + 1)
        self._signal(SIGNAL_POWER_ON)
        self._signal(SIGNAL_NV_ON)
        self.sock = socket.create_connection(("127.0.0.1", self.port),
                                             timeout=self.timeout)
        self.sock.settimeout(self.timeout)
        self._send(u32(REMOTE_HANDSHAKE) + u32(1))
        self._u32(), self._u32(), self._u32()     # version, flags, ack

    def stop(self):
        for closer in (self._say_goodbye, self._kill):
            try:
                closer()
            except Exception:
                pass
        if self.log:
            self.log.close()
            self.log = None

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def _say_goodbye(self):
        # TPM_STOP, not TPM_SESSION_END.  SESSION_END ends the connection and
        # leaves the simulator listening for another one, so the process would
        # only ever die of the SIGKILL below -- and a process killed with
        # SIGKILL runs no atexit handler, so a --coverage build of this tree
        # dumps no profile and probe/covunion.sh sees none of this half.  STOP
        # makes TpmServer() return FALSE and main() return.  It exits 1, not 0,
        # because main() in TPMCmds.c falls off the end of an int function;
        # nothing here reads the status.
        if self.sock:
            try:
                self.sock.sendall(u32(STOP))
            finally:
                self.sock.close()
                self.sock = None
        if self.proc:
            try:
                self.proc.wait(timeout=5)
            except Exception:
                pass

    def _kill(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
        if self.proc:
            self.proc.wait(timeout=10)
        self.proc = None

    def _wait_for_port(self, port, limit=20.0):
        deadline = time.time() + limit
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise Dead("simulator exited %s before listening"
                           % self.proc.returncode)
            try:
                socket.create_connection(("127.0.0.1", port), 0.2).close()
                return
            except OSError:
                time.sleep(0.01)
        raise Dead("simulator never listened on %d" % port)

    # -- raw io --
    def _send(self, blob):
        try:
            self.sock.sendall(blob)
        except OSError as e:
            raise Dead("send: %s" % e)

    def _read(self, n):
        buf = b""
        while len(buf) < n:
            try:
                chunk = self.sock.recv(n - len(buf))
            except OSError as e:
                raise Dead("recv: %s" % e)
            if not chunk:
                raise Dead("closed after %d/%d bytes" % (len(buf), n))
            buf += chunk
        return buf

    def _u32(self):
        return struct.unpack(">I", self._read(4))[0]

    def _signal(self, code):
        s = socket.create_connection(("127.0.0.1", self.port + 1),
                                     timeout=self.timeout)
        s.settimeout(self.timeout)
        try:
            s.sendall(u32(code))
            struct.unpack(">I", s.recv(4))[0]
            s.sendall(u32(SESSION_END))
        except OSError as e:
            raise Dead("platform signal %d: %s" % (code, e))
        finally:
            s.close()

    # -- commands --
    def raw(self, blob, locality=0):
        """Send an arbitrary command buffer; return the whole response.

        The length the framing declares is taken from the buffer, so a
        command whose own commandSize field lies still reaches the TPM --
        that is the point of the malformed cases.
        """
        self._send(u32(SEND_COMMAND) + u8(locality) + u32(len(blob)) + blob)
        n = self._u32()
        if n > (1 << 22):
            raise Dead("absurd response length %d" % n)
        rsp = self._read(n)
        self._u32()                                # trailing ack
        return rsp

    def cmd(self, cc, payload=b"", handles=(), auth=None, locality=0,
            tag=None, rhandles=0):
        """Marshal, send, and split the response into (rc, parameters).

        With auth, the response carries parameterSize before the parameters
        and the session's own auth area after them; slicing to the end of
        the buffer instead reads that trailing area as part of the last
        parameter, which shows up as a corrupt signature rather than as a
        framing mistake.

        A command that answers with handles puts them ahead of that
        parameterSize (Part 1 "Response Buffer"), so rhandles says how many
        to keep in front of the split.  Getting it wrong is quiet: the
        caller reads parameterSize as the handle and the TPM rejects a
        later use of handle 0 with an ordinary error code.
        """
        code = cc if isinstance(cc, int) else CC[cc]
        if tag is None:
            tag = ST_SESSIONS if auth is not None else ST_NO_SESSIONS
        body = b"".join(u32(x) for x in handles)
        if auth is not None:
            body += u32(len(auth)) + auth
        body += payload
        blob = u16(tag) + u32(10 + len(body)) + u32(code) + body
        rsp = self.raw(blob, locality)
        if len(rsp) < 10:
            raise Dead("runt response (%d bytes)" % len(rsp))
        rtag, size, rc = struct.unpack(">HII", rsp[:10])
        params = rsp[10:]
        if rc == 0 and rtag == ST_SESSIONS:
            head, params = params[:4 * rhandles], params[4 * rhandles:]
            n = struct.unpack(">I", params[:4])[0]
            params = head + params[4:4 + n]
        return rc, params

    # -- conveniences the case table leans on --
    def startup(self, kind=0):
        """TPM2_Startup, refusing to continue on an entropy-shim failure.

        CryptUtilStartup calls _cpri__Startup a second time
        (src/tpm/crypt/CryptUtil.c:1794), so the same coin is flipped again
        here; losing puts the TPM into failure mode, where every later case
        answers TPM_RC_FAILURE.  That is a whole session of wrong answers from
        one unlucky byte, so it is raised rather than recorded, and the driver
        runs the session again on a new process.
        """
        rc = self.cmd("Startup", u16(kind))[0]
        if kind == 0 and rc != 0:
            raise Flaky("Startup(CLEAR) rc=0x%x" % rc)
        return rc

    def load_external(self, sens, pub, hierarchy=RH_NULL):
        """(rc, handle, name); handle is None unless the load succeeded."""
        rc, p = self.cmd("LoadExternal", sens + pub + u32(hierarchy))
        if rc:
            return rc, None, b""
        return rc, struct.unpack(">I", p[:4])[0], unb2(p, 4)[0]

    def flush(self, handle):
        return self.cmd("FlushContext", u32(handle))[0]
