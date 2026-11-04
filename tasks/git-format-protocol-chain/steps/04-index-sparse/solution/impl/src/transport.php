<?php
// Transports: file://, ssh://, git:// and smart HTTP, on the client side
// (gitprotocol-pack(5), gitprotocol-http(5)).

class RemoteConn {
    /** @var resource */ public $in;    // read from the remote
    /** @var resource */ public $out;   // write to the remote
    public $proc = null;
    public array $pipes = [];
    public ?Transport $transport = null;
    /** v2: the server's capability advertisement, read once per connection. */
    public array $v2Caps = [];
    public bool $v2CapsRead = false;
    /** v2: symref targets reported by ls-refs. */
    public array $symrefs = [];
    /** Signal end-of-request to the peer, as git does after a push. */
    public function shutdownWrite() {
        if (!is_resource($this->out)) return;
        @fflush($this->out);
        $meta = @stream_get_meta_data($this->out);
        if (isset($meta['stream_type']) && strpos($meta['stream_type'], 'tcp') !== false) {
            @stream_socket_shutdown($this->out, STREAM_SHUT_WR);
            return;
        }
        if ($this->out !== $this->in) @fclose($this->out);
    }

    public function close() {
        if (is_resource($this->out) && $this->out !== $this->in) @fclose($this->out);
        if (is_resource($this->in)) @fclose($this->in);
        if ($this->proc !== null) @proc_close($this->proc);
    }
}

abstract class Transport {
    public Repo $repo;
    public string $url;
    public array $advertisedRefs = [];
    public array $advertisedCaps = [];
    public function __construct(Repo $repo, string $url) { $this->repo = $repo; $this->url = $url; }

    abstract public function connect(string $service, int $protocolVersion = 0): RemoteConn;

    public static function open(Repo $repo, string $url): Transport {
        if (preg_match('#^(https?)://#', $url)) return new HttpTransport($repo, $url);
        if (strncmp($url, 'git://', 6) === 0) return new GitDaemonTransport($repo, $url);
        if (strncmp($url, 'ssh://', 6) === 0) return new SshTransport($repo, $url);
        if (preg_match('#^[^/]+@[^/:]+:#', $url)) return new SshTransport($repo, $url);
        if (strncmp($url, 'file://', 7) === 0) return new LocalTransport($repo, substr($url, 7));
        return new LocalTransport($repo, $url);
    }

    /** Read a ref advertisement into advertisedRefs/advertisedCaps. */
    public function readAdvertisement(RemoteConn $c): array {
        $refs = [];
        $first = true;
        $sawService = false;
        while (true) {
            $line = pkt_read($c->in);
            if ($line === false) break;
            if ($line === null) {
                // smart HTTP sends a flush after the "# service=" line; the
                // advertisement itself follows (gitprotocol-http(5))
                if ($sawService && !$refs) { $sawService = false; continue; }
                break;
            }
            if (is_int($line)) continue;
            $line = rtrim($line, "\n");
            if ($line === '' ) continue;
            if (strncmp($line, '# service=', 10) === 0) { $first = true; $sawService = true; continue; }
            if (preg_match('/^version \d+$/', $line)) continue;
            if ($first) {
                [$val, $caps] = split_caps($line);
                $this->advertisedCaps = $caps;
                $line = $val;
                $first = false;
            }
            $sp = strpos($line, ' ');
            if ($sp === false) continue;
            $oid = substr($line, 0, $sp);
            $name = substr($line, $sp + 1);
            if ($name === 'capabilities^{}') continue;
            $refs[$name] = $oid;
        }
        $this->advertisedRefs = $refs;
        return $refs;
    }

    /**
     * Send one protocol v2 command and return a stream holding the response.
     * A stream transport writes into the open connection; smart HTTP turns each
     * command into its own POST (gitprotocol-v2(5), "HTTP Transport").
     */
    public function sendV2(RemoteConn $c, string $body, string $service = 'git-upload-pack') {
        fwrite($c->out, $body);
        fflush($c->out);
        return $c->in;
    }

    public function symrefHead(): ?string {
        foreach ($this->advertisedCaps as $cap) {
            if (strncmp($cap, 'symref=HEAD:', 12) === 0) return substr($cap, 12);
        }
        return null;
    }
}

class LocalTransport extends Transport {
    public string $path;
    public function __construct(Repo $repo, string $path) {
        parent::__construct($repo, $path);
        $this->path = $path;
    }
    public function connect(string $service, int $protocolVersion = 0): RemoteConn {
        $self = git_self_command();
        $sub = substr($service, 4);   // git-upload-pack -> upload-pack
        $cmd = $self . ' ' . escapeshellarg($sub) . ' ' . escapeshellarg($this->path);
        $env = null;
        if ($protocolVersion >= 2) {
            $env = array_merge($_ENV ?: [], getenv() ?: [], ['GIT_PROTOCOL' => 'version=2']);
        }
        $desc = [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => STDERR];
        $proc = proc_open($cmd, $desc, $pipes, null, $env);
        if (!is_resource($proc)) die_git("cannot run $sub");
        $c = new RemoteConn();
        $c->proc = $proc;
        $c->pipes = $pipes;
        $c->out = $pipes[0];
        $c->in = $pipes[1];
        $c->transport = $this;
        return $c;
    }
}

class SshTransport extends Transport {
    public function connect(string $service, int $protocolVersion = 0): RemoteConn {
        $url = $this->url;
        $user = null; $host = null; $port = null; $path = null;
        if (preg_match('#^ssh://(?:([^@/]+)@)?([^/:]+)(?::(\d+))?(/.*)$#', $url, $m)) {
            $user = $m[1] ?: null; $host = $m[2]; $port = $m[3] ?: null; $path = $m[4];
        } elseif (preg_match('#^(?:([^@]+)@)?([^:]+):(.*)$#', $url, $m)) {
            $user = $m[1] ?: null; $host = $m[2]; $path = $m[3];
        } else {
            die_git("unable to parse ssh url '$url'");
        }
        $sshCmd = getenv('GIT_SSH_COMMAND');
        if ($sshCmd === false || $sshCmd === '') {
            $cfg = $this->repo->config->get('core.sshcommand', null);
            $sshCmd = ($cfg !== null && !is_bool($cfg)) ? $cfg : 'ssh';
        }
        $args = $sshCmd;
        if ($port !== null) $args .= ' -p ' . escapeshellarg($port);
        $args .= ' ' . escapeshellarg(($user !== null ? $user . '@' : '') . $host);
        $remote = $service . " '" . str_replace("'", "'\\''", $path) . "'";
        if ($protocolVersion >= 2) {
            $args .= ' ' . escapeshellarg('env GIT_PROTOCOL=version=2 ' . $remote);
        } else {
            $args .= ' ' . escapeshellarg($remote);
        }
        $desc = [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => STDERR];
        $proc = proc_open($args, $desc, $pipes);
        if (!is_resource($proc)) die_git("cannot run ssh");
        $c = new RemoteConn();
        $c->proc = $proc; $c->pipes = $pipes;
        $c->out = $pipes[0]; $c->in = $pipes[1];
        $c->transport = $this;
        return $c;
    }
}

class GitDaemonTransport extends Transport {
    public function connect(string $service, int $protocolVersion = 0): RemoteConn {
        if (!preg_match('#^git://([^/:]+)(?::(\d+))?(/.*)$#', $this->url, $m)) {
            die_git("unable to parse git url '{$this->url}'");
        }
        $host = $m[1];
        $port = $m[2] !== '' ? (int)$m[2] : 9418;
        $errno = 0; $errstr = '';
        $sock = @stream_socket_client("tcp://$host:$port", $errno, $errstr, 30);
        if ($sock === false) {
            // git reports the address it tried and the errno text
            die_git("unable to connect to $host:\n$host" . "[0: $host]: errno=" . $errstr);
        }
        $req = $service . ' ' . $m[3] . "\0host=" . $host . "\0";
        if ($protocolVersion >= 2) $req .= "\0version=2\0";
        pkt_write($sock, $req);
        $c = new RemoteConn();
        $c->in = $sock; $c->out = $sock;
        $c->transport = $this;
        return $c;
    }
}

/** Smart HTTP: one GET for the advertisement, one POST per request. */
class HttpTransport extends Transport {
    private array $lastHeaders = [];
    public int $protocolVersion = 0;

    public function connect(string $service, int $protocolVersion = 0): RemoteConn {
        $this->protocolVersion = $protocolVersion;
        $url = rtrim($this->url, '/') . '/info/refs?service=' . $service;
        $extra = [];
        if ($protocolVersion >= 2) $extra[] = 'Git-Protocol: version=2';
        [$status, $headers, $body] = http_request($url, 'GET', null, null, $extra);
        if ($status !== 200) die_git("repository '" . $this->url . "' not found");
        $c = new RemoteConn();
        $mem = fopen('php://memory', 'r+b');
        fwrite($mem, $body);
        rewind($mem);
        $c->in = $mem;
        $c->out = fopen('php://memory', 'r+b');
        $c->transport = $this;
        return $c;
    }

    public function sendV2(RemoteConn $c, string $body, string $service = 'git-upload-pack') {
        return $this->post($service, $body);
    }

    /** POST a complete request body and return the response as a stream. */
    public function post(string $service, string $body) {
        $url = rtrim($this->url, '/') . '/' . $service;
        $extra = ['Content-Type: application/x-' . $service . '-request',
                  'Accept: application/x-' . $service . '-result'];
        if ($this->protocolVersion >= 2) $extra[] = 'Git-Protocol: version=2';
        [$status, $headers, $resp] = http_request($url, 'POST', $body, null, $extra);
        if ($status !== 200) die_git("RPC failed; HTTP $status");
        $mem = fopen('php://memory', 'r+b');
        fwrite($mem, $resp);
        rewind($mem);
        return $mem;
    }
}

/** Minimal HTTP/1.1 client over a stream socket. */
function http_request(string $url, string $method, ?string $body, $unused = null, array $extraHeaders = []): array {
    if (!preg_match('#^(https?)://([^/:]+)(?::(\d+))?(/.*)?$#', $url, $m)) {
        die_git("unable to parse url '$url'");
    }
    $scheme = $m[1]; $host = $m[2];
    $port = $m[3] !== '' ? (int)$m[3] : ($scheme === 'https' ? 443 : 80);
    $path = $m[4] !== '' ? $m[4] : '/';
    $target = $scheme === 'https' ? "ssl://$host:$port" : "tcp://$host:$port";
    $errno = 0; $errstr = '';
    $sock = @stream_socket_client($target, $errno, $errstr, 60);
    if ($sock === false) die_git("unable to access '$url': $errstr");
    $req = "$method $path HTTP/1.1\r\nHost: $host" . ($port !== 80 && $port !== 443 ? ":$port" : '') . "\r\n";
    $req .= "User-Agent: " . git_agent() . "\r\n";
    $req .= "Accept-Encoding: identity\r\n";
    foreach ($extraHeaders as $h) $req .= $h . "\r\n";
    if ($body !== null) {
        $req .= 'Content-Length: ' . strlen($body) . "\r\n";
    }
    $req .= "Connection: close\r\n\r\n";
    fwrite($sock, $req);
    if ($body !== null) fwrite($sock, $body);
    $resp = '';
    while (!feof($sock)) {
        $chunk = fread($sock, 65536);
        if ($chunk === false || $chunk === '') break;
        $resp .= $chunk;
    }
    fclose($sock);
    $split = strpos($resp, "\r\n\r\n");
    if ($split === false) return [0, [], ''];
    $head = substr($resp, 0, $split);
    $payload = substr($resp, $split + 4);
    $lines = explode("\r\n", $head);
    $statusLine = array_shift($lines);
    $status = 0;
    if (preg_match('#^HTTP/1\.[01] (\d+)#', $statusLine, $sm)) $status = (int)$sm[1];
    $headers = [];
    foreach ($lines as $l) {
        $c = strpos($l, ':');
        if ($c === false) continue;
        $headers[strtolower(substr($l, 0, $c))] = trim(substr($l, $c + 1));
    }
    if (isset($headers['transfer-encoding']) && strtolower($headers['transfer-encoding']) === 'chunked') {
        $payload = http_dechunk($payload);
    }
    return [$status, $headers, $payload];
}

function http_dechunk(string $s): string {
    $out = '';
    $pos = 0;
    while ($pos < strlen($s)) {
        $eol = strpos($s, "\r\n", $pos);
        if ($eol === false) break;
        $sizeHex = trim(substr($s, $pos, $eol - $pos));
        $semi = strpos($sizeHex, ';');
        if ($semi !== false) $sizeHex = substr($sizeHex, 0, $semi);
        $size = hexdec($sizeHex);
        $pos = $eol + 2;
        if ($size === 0) break;
        $out .= substr($s, $pos, $size);
        $pos += $size + 2;
    }
    return $out;
}

/** How to re-invoke ourselves for local transports and hooks. */
function git_self_command(): string {
    $exec = git_exec_path();
    $candidate = $exec . '/git';
    if (is_file($candidate)) return escapeshellarg($candidate);
    return escapeshellarg(PHP_BINARY) . ' ' . escapeshellarg(dirname(__DIR__) . '/src/boot.php');
}

/**
 * The protocol version this build will actually speak.
 *
 * `protocol.version` may name a version this milestone does not implement; a
 * client then simply uses the newest one it has, exactly as it would against a
 * server that does not offer the newer one.
 */
function negotiated_protocol_version(Repo $repo): int {
    $want = $repo->config->getInt('protocol.version', 0);
    if ($want >= 2 && !function_exists('v2_ls_refs')) return 1;
    return max(0, $want);
}
