'use strict';
// smbd(8)-compatible entry point.
const fs = require('fs');
const path = require('path');
const smbconf = require('./smbconf');

function usage(code) {
  process.stderr.write(
    'Usage: smbd [-D|--daemon] [-i|--interactive] [-F|--foreground] [--no-process-group]\n' +
    '        [-p|--port=STRING] [-d|--debuglevel=DEBUGLEVEL] [--debug-stdout]\n' +
    '        [-s|--configfile=CONFIGFILE] [--option=name=value]\n' +
    '        [-l|--log-basename=LOGFILEBASE] [-V|--version]\n');
  process.exit(code);
}

function main(argv) {
  let configFile = '/etc/samba/smb.conf';
  let ports = null;
  let debugLevel = 0;
  const overrides = [];
  let logBasename = null;
  let debugStdout = false;
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const eat = () => { const v = argv[++i]; if (v === undefined) usage(1); return v; };
    if (a === '-F' || a === '--foreground' || a === '-D' || a === '--daemon' ||
        a === '-i' || a === '--interactive' || a === '--no-process-group' ||
        a === '-P' || a === '--profiling-level' || a === '--leak-report' ||
        a === '--leak-report-full') continue;
    if (a === '--debug-stdout') { debugStdout = true; continue; }
    if (a === '-V' || a === '--version') { process.stdout.write('Version 4.17.12\n'); process.exit(0); }
    if (a === '-?' || a === '--help' || a === '--usage') usage(0);
    if (a === '-b' || a === '--build-options') { process.stdout.write('Build options\n'); process.exit(0); }
    let m;
    if ((m = /^--configfile=(.*)$/.exec(a))) { configFile = m[1]; continue; }
    if (a === '-s') { configFile = eat(); continue; }
    if ((m = /^-s(.+)$/.exec(a))) { configFile = m[1]; continue; }
    if ((m = /^--port=(.*)$/.exec(a))) { ports = m[1]; continue; }
    if (a === '-p') { ports = eat(); continue; }
    if ((m = /^-p(.+)$/.exec(a))) { ports = m[1]; continue; }
    if ((m = /^--debuglevel=(.*)$/.exec(a))) { debugLevel = parseInt(m[1], 10) || 0; continue; }
    if (a === '-d') { debugLevel = parseInt(eat(), 10) || 0; continue; }
    if ((m = /^-d(.+)$/.exec(a))) { debugLevel = parseInt(m[1], 10) || 0; continue; }
    if ((m = /^--option=(.*)$/.exec(a))) { overrides.push(m[1]); continue; }
    if (a === '--option') { overrides.push(eat()); continue; }
    if ((m = /^--log-basename=(.*)$/.exec(a))) { logBasename = m[1]; continue; }
    if (a === '-l') { logBasename = eat(); continue; }
    usage(1);
  }

  const conf = smbconf.load(configFile, overrides);
  const { Server } = require('./server');
  const server = new Server(conf, { debugLevel });

  if (ports === null) ports = String(conf.get('global', 'smb ports', '445 139'));
  const portList = ports.split(/[\s,]+/).filter(Boolean).map(Number).filter((n) => n > 0);
  if (portList.length === 0) portList.push(445);

  let address = '0.0.0.0';
  if (conf.getBool('global', 'bind interfaces only', false)) {
    const ifaces = String(conf.get('global', 'interfaces', '')).split(/[\s,]+/).filter(Boolean);
    for (const i of ifaces) {
      const ip = i.split('/')[0];
      if (/^\d+\.\d+\.\d+\.\d+$/.test(ip)) { address = ip; break; }
    }
  }

  server.listen(portList, address, () => {
    if (debugLevel > 0 || debugStdout) console.error(`smbd: listening on ${address}:${portList.join(',')}`);
  });
  process.on('SIGTERM', () => process.exit(0));
  process.on('SIGINT', () => process.exit(0));
}

main(process.argv.slice(2));
