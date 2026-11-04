<?php
// git-daemon(1) and git-http-backend(1): the two servers that carry the pack
// protocol over git:// and smart HTTP.

function cmd_daemon(array $argv): int {
    $listen = '127.0.0.1'; $port = 9418; $basePath = null; $exportAll = false;
    $reuseaddr = false; $verbose = false; $detach = false; $pidFile = null;
    $enable = ['upload-pack' => true, 'upload-archive' => false, 'receive-pack' => false];
    $dirs = [];
    $informativeErrors = false;
    foreach ($argv as $a) {
        switch (true) {
            case strncmp($a, '--listen=', 9) === 0: $listen = substr($a, 9); break;
            case strncmp($a, '--port=', 7) === 0: $port = (int)substr($a, 7); break;
            case strncmp($a, '--base-path=', 12) === 0: $basePath = substr($a, 12); break;
            case $a === '--export-all': $exportAll = true; break;
            case $a === '--reuseaddr': $reuseaddr = true; break;
            case $a === '--verbose': $verbose = true; break;
            case $a === '--detach': $detach = true; break;
            case $a === '--informative-errors': $informativeErrors = true; break;
            case strncmp($a, '--pid-file=', 11) === 0: $pidFile = substr($a, 11); break;
            case strncmp($a, '--enable=', 9) === 0: $enable[substr($a, 9)] = true; break;
            case strncmp($a, '--disable=', 10) === 0: $enable[substr($a, 10)] = false; break;
            case strncmp($a, '--timeout=', 10) === 0 || strncmp($a, '--init-timeout=', 15) === 0: break;
            case strncmp($a, '--max-connections=', 18) === 0: break;
            case $a === '--syslog' || $a === '--log-destination=stderr': break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $dirs[] = $a;
        }
    }
    $ctx = stream_context_create(['socket' => ['so_reuseaddr' => true, 'backlog' => 64]]);
    $errno = 0; $errstr = '';
    $server = @stream_socket_server("tcp://$listen:$port", $errno, $errstr,
        STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $ctx);
    if ($server === false) die_git("unable to listen on $listen:$port: $errstr");
    if ($detach) {
        $pid = function_exists('pcntl_fork') ? pcntl_fork() : -1;
        if ($pid > 0) { if ($pidFile !== null) write_file_atomic($pidFile, $pid . "\n"); exit(0); }
        if (function_exists('posix_setsid')) @posix_setsid();
        // release the inherited standard streams, or whoever started us waits
        @fclose(STDIN);
        $null = @fopen('/dev/null', 'r+');
        if ($null !== false) {
            @fclose(STDOUT);
            @fclose(STDERR);
            @fopen('/dev/null', 'w');
            @fopen('/dev/null', 'w');
        }
    } elseif ($pidFile !== null) {
        write_file_atomic($pidFile, getmypid() . "\n");
    }
    if ($verbose) fwrite(STDERR, "[" . getmypid() . "] Ready to rumble\n");
    if (function_exists('pcntl_signal')) {
        @pcntl_signal(SIGCHLD, SIG_IGN);
    }
    while (true) {
        $conn = @stream_socket_accept($server, -1);
        if ($conn === false) continue;
        $pid = function_exists('pcntl_fork') ? pcntl_fork() : -1;
        if ($pid === 0) {
            fclose($server);
            daemon_serve_connection($conn, $basePath, $exportAll, $enable, $dirs, $verbose);
            exit(0);
        }
        if ($pid < 0) {
            daemon_serve_connection($conn, $basePath, $exportAll, $enable, $dirs, $verbose);
        }
        fclose($conn);
    }
    return 0;
}

function daemon_serve_connection($conn, ?string $basePath, bool $exportAll, array $enable, array $dirs, bool $verbose) {
    stream_set_timeout($conn, 60);
    $line = pkt_read($conn);
    if ($line === false || $line === null || is_int($line)) { fclose($conn); return; }
    $parts = explode("\0", $line);
    $request = rtrim($parts[0], "\n");
    $extra = array_slice($parts, 1);
    $protocol = 0;
    foreach ($extra as $e) {
        if (strncmp($e, 'version=', 8) === 0) $protocol = (int)substr($e, 8);
    }
    $sp = strpos($request, ' ');
    if ($sp === false) { fclose($conn); return; }
    $service = substr($request, 0, $sp);
    $path = substr($request, $sp + 1);
    if (!isset($enable[substr($service, 4)]) || !$enable[substr($service, 4)]) {
        pkt_write($conn, "ERR service not enabled: $service\n");
        fclose($conn);
        return;
    }
    $full = $basePath !== null ? rtrim($basePath, '/') . '/' . ltrim($path, '/') : $path;
    $full = path_normalize($full);
    $gitdir = null;
    if (Repo::isGitDirectory($full)) $gitdir = $full;
    elseif (Repo::isGitDirectory($full . '/.git')) $gitdir = $full . '/.git';
    elseif (Repo::isGitDirectory($full . '.git')) $gitdir = $full . '.git';
    if ($gitdir === null) {
        pkt_write($conn, "ERR access denied or repository not exported: $path\n");
        fclose($conn);
        return;
    }
    if (!$exportAll && !is_file($gitdir . '/git-daemon-export-ok')) {
        pkt_write($conn, "ERR access denied or repository not exported: $path\n");
        fclose($conn);
        return;
    }
    $repo = Repo::open($gitdir, null);
    if ($protocol >= 2 && $service === 'git-upload-pack' && function_exists('upload_pack_v2_serve')) {
        write_v2_capability_advertisement($conn, $repo);
        upload_pack_v2_serve($repo, $conn, $conn, false);
    } elseif ($service === 'git-upload-pack') {
        upload_pack_serve($repo, $conn, $conn, false, true);
    } else {
        receive_pack_serve($repo, $conn, $conn, false, true);
    }
    fflush($conn);
    fclose($conn);
}

function cmd_http_backend(array $argv): int {
    $method = getenv('REQUEST_METHOD') ?: 'GET';
    $pathInfo = getenv('PATH_INFO') ?: '';
    $query = getenv('QUERY_STRING') ?: '';
    $root = getenv('GIT_PROJECT_ROOT') ?: getcwd();
    $exportAll = getenv('GIT_HTTP_EXPORT_ALL') !== false;
    $contentType = getenv('CONTENT_TYPE') ?: '';
    parse_str($query, $q);

    $service = $q['service'] ?? null;
    $repoPath = $pathInfo;
    $handler = null;
    if (preg_match('#^(.*)/info/refs$#', $pathInfo, $m)) { $repoPath = $m[1]; $handler = 'info-refs'; }
    elseif (preg_match('#^(.*)/git-upload-pack$#', $pathInfo, $m)) { $repoPath = $m[1]; $handler = 'upload-pack'; }
    elseif (preg_match('#^(.*)/git-receive-pack$#', $pathInfo, $m)) { $repoPath = $m[1]; $handler = 'receive-pack'; }
    elseif (preg_match('#^(.*)/(HEAD|objects/info/[^/]+)$#', $pathInfo, $m)) { $repoPath = $m[1]; $handler = 'file'; }
    elseif (preg_match('#^(.*)/(objects/[0-9a-f]{2}/[0-9a-f]+|objects/pack/pack-[0-9a-f]+\.(pack|idx))$#', $pathInfo, $m)) {
        $repoPath = $m[1]; $handler = 'file';
    }
    $full = rtrim($root, '/') . '/' . ltrim($repoPath, '/');
    $gitdir = null;
    if (Repo::isGitDirectory($full)) $gitdir = $full;
    elseif (Repo::isGitDirectory($full . '/.git')) $gitdir = $full . '/.git';
    elseif (Repo::isGitDirectory($full . '.git')) $gitdir = $full . '.git';
    if ($gitdir === null || $handler === null) {
        http_backend_error(404, 'Not Found');
        return 0;
    }
    if (!$exportAll && !is_file($gitdir . '/git-daemon-export-ok')) {
        http_backend_error(403, 'Forbidden');
        return 0;
    }
    $repo = Repo::open($gitdir, null);
    $protocolHeader = getenv('HTTP_GIT_PROTOCOL') ?: '';
    $version = 0;
    foreach (explode(':', $protocolHeader) as $p) {
        if (strncmp($p, 'version=', 8) === 0) $version = (int)substr($p, 8);
    }
    if ($handler === 'info-refs' && $service !== null) {
        $buf = fopen('php://memory', 'r+b');
        pkt_write($buf, "# service=$service\n");
        pkt_flush($buf);
        if ($version >= 2 && $service === 'git-upload-pack' && function_exists('write_v2_capability_advertisement')) {
            write_v2_capability_advertisement($buf, $repo);
        } else {
            write_advertisement($buf, $repo, $service);
        }
        rewind($buf);
        $body = stream_get_contents($buf);
        http_backend_headers('application/x-' . $service . '-advertisement', strlen($body));
        out_flush();
        fwrite(STDOUT, $body);
        return 0;
    }
    if ($handler === 'info-refs') {
        // dumb protocol fallback
        $body = '';
        foreach ($repo->refs->listRefs('refs/') as $n => $o) $body .= "$o\t$n\n";
        http_backend_headers('text/plain; charset=utf-8', strlen($body));
        out_flush();
        fwrite(STDOUT, $body);
        return 0;
    }
    if ($handler === 'file') {
        $file = $gitdir . '/' . $m[2];
        if (!is_file($file)) { http_backend_error(404, 'Not Found'); return 0; }
        $data = (string)file_get_contents($file);
        $ct = 'text/plain';
        if (substr($file, -5) === '.pack') $ct = 'application/x-git-packed-objects';
        elseif (substr($file, -4) === '.idx') $ct = 'application/x-git-packed-objects-toc';
        elseif (strpos($file, '/objects/') !== false) $ct = 'application/x-git-loose-object';
        http_backend_headers($ct, strlen($data));
        out_flush();
        fwrite(STDOUT, $data);
        return 0;
    }
    // RPC
    $body = stream_get_contents(STDIN);
    if (getenv('HTTP_CONTENT_ENCODING') === 'gzip' || getenv('CONTENT_ENCODING') === 'gzip') {
        $un = @gzdecode($body);
        if ($un !== false) $body = $un;
    }
    $in = fopen('php://memory', 'r+b');
    fwrite($in, $body);
    rewind($in);
    $outBuf = fopen('php://memory', 'r+b');
    if ($handler === 'upload-pack') {
        if ($version >= 2 && function_exists('upload_pack_v2_serve')) {
            upload_pack_v2_serve($repo, $in, $outBuf, true);
        } else {
            upload_pack_serve($repo, $in, $outBuf, true, false);
        }
        $ct = 'application/x-git-upload-pack-result';
    } else {
        receive_pack_serve($repo, $in, $outBuf, true, false);
        $ct = 'application/x-git-receive-pack-result';
    }
    rewind($outBuf);
    $payload = stream_get_contents($outBuf);
    http_backend_headers($ct, strlen($payload));
    out_flush();
    fwrite(STDOUT, $payload);
    return 0;
}

function http_backend_headers(string $contentType, int $length) {
    echo "Status: 200 OK\r\n";
    echo "Content-Type: $contentType\r\n";
    echo "Content-Length: $length\r\n";
    echo "Expires: Fri, 01 Jan 1980 00:00:00 GMT\r\n";
    echo "Pragma: no-cache\r\n";
    echo "Cache-Control: no-cache, max-age=0, must-revalidate\r\n";
    echo "\r\n";
}

function http_backend_error(int $code, string $text) {
    echo "Status: $code $text\r\n";
    echo "Content-Type: text/plain\r\n";
    echo "\r\n";
    echo "$text\n";
}
