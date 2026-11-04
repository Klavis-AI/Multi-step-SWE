package redis;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Connection commands: PING, ECHO, AUTH, HELLO, QUIT, RESET and the CLIENT
 * container (src/networking.c, src/acl.c authCommand, src/server.c pingCommand).
 */
public final class ConnCmds {
    private ConnCmds() {}

    /* getClientType() classes, see server.h CLIENT_TYPE_*. */
    static final int TYPE_NORMAL = 0, TYPE_SLAVE = 1, TYPE_PUBSUB = 2, TYPE_MASTER = 3;

    public static void register() {
        CommandTable.reg("ping", -1, "fast", 0, 0, 0, ConnCmds::ping);
        CommandTable.reg("echo", 2, "loading stale fast", 0, 0, 0, ConnCmds::echo);
        CommandTable.reg("auth", -2, "noscript loading stale fast", 0, 0, 0, ConnCmds::auth);
        CommandTable.reg("hello", -1, "noscript loading stale fast", 0, 0, 0, ConnCmds::hello);
        CommandTable.reg("quit", -1, "noscript loading stale fast", 0, 0, 0, ConnCmds::quit);
        CommandTable.reg("reset", 1, "noscript loading stale fast", 0, 0, 0, ConnCmds::reset);
        CommandTable.reg("client", -2, "noscript loading stale", 0, 0, 0, ConnCmds::client);

        // Every CLIENT subcommand is registered with a permissive arity so that a wrong
        // argument count produces redis's own message ("wrong number of arguments for
        // 'client|kill' command") instead of the generic container message.
        CommandTable.regSub("client", "id", -2, "noscript loading stale", ConnCmds::clientId);
        CommandTable.regSub("client", "info", -2, "noscript loading stale", ConnCmds::clientInfo);
        CommandTable.regSub("client", "list", -2, "admin noscript loading stale", ConnCmds::clientList);
        CommandTable.regSub("client", "getname", -2, "noscript loading stale", ConnCmds::clientGetname);
        CommandTable.regSub("client", "setname", -2, "noscript loading stale", ConnCmds::clientSetname);
        CommandTable.regSub("client", "setinfo", -2, "noscript loading stale", ConnCmds::clientSetinfo);
        CommandTable.regSub("client", "kill", -2, "admin noscript loading stale", ConnCmds::clientKill);
        CommandTable.regSub("client", "unblock", -2, "admin noscript loading stale", ConnCmds::clientUnblock);
        CommandTable.regSub("client", "unpause", -2, "admin noscript loading stale", ConnCmds::clientUnpause);
        CommandTable.regSub("client", "pause", -2, "admin noscript loading stale", ConnCmds::clientPause);
        CommandTable.regSub("client", "no-evict", -2, "admin noscript loading stale", ConnCmds::clientNoEvict);
        CommandTable.regSub("client", "no-touch", -2, "noscript loading stale", ConnCmds::clientNoTouch);
        CommandTable.regSub("client", "reply", -2, "noscript loading stale", ConnCmds::clientReply);
        CommandTable.regSub("client", "tracking", -2, "noscript loading stale", ConnCmds::clientTracking);
        CommandTable.regSub("client", "trackinginfo", -2, "noscript loading stale", ConnCmds::clientTrackingInfo);
        CommandTable.regSub("client", "caching", -2, "noscript loading stale", ConnCmds::clientCaching);
        CommandTable.regSub("client", "getredir", -2, "noscript loading stale", ConnCmds::clientGetredir);
        CommandTable.regSub("client", "help", -2, "loading stale", ConnCmds::clientHelp);
    }

    // ------------------------------------------------------------------
    // shared helpers (also used by PubSubCmds / MultiCmds)
    // ------------------------------------------------------------------

    /** networking.c addReplyHelp(): header + one status per line + the HELP footer. */
    static void addHelp(Client c, String[] help) {
        String cmd = c.argStr(0).toUpperCase(Locale.US);
        c.addArray(help.length + 3);
        c.addSimple(cmd + " <subcommand> [<arg> [value] [opt] ...]. Subcommands are:");
        for (String h : help) c.addSimple(h);
        c.addSimple("HELP");
        c.addSimple("    Print this help.");
    }

    /** networking.c addReplySubcommandSyntaxError(). */
    static RedisError subErr(Client c, String container) {
        return new RedisError("ERR unknown subcommand or wrong number of arguments for '"
                + c.argStr(1) + "'. Try " + container.toUpperCase(Locale.US) + " HELP.");
    }

    static RedisError arity(String fullName) {
        return new RedisError("ERR wrong number of arguments for '" + fullName + "' command");
    }

    /** Set c.lastCmd to "container|sub" the way redis resolves container commands. */
    static void markSub(Client c, String container) {
        if (c.argc < 2) return;
        Map<String, Cmd> m = CommandTable.SUBS.get(container);
        if (m == null) return;
        Cmd cmd = m.get(c.argLower(1));
        if (cmd != null) c.lastCmd = cmd.fullName;
    }

    /** networking.c getClientType(). */
    static int clientType(Client cl) {
        if (cl.isReplica && !cl.isMonitor) return TYPE_SLAVE;
        if (!cl.subChannels.isEmpty() || !cl.subPatterns.isEmpty() || !cl.subShard.isEmpty())
            return TYPE_PUBSUB;
        return TYPE_NORMAL;
    }

    /** networking.c getClientTypeByName(), -1 when unknown. */
    static int typeByName(String n) {
        if (n.equalsIgnoreCase("normal")) return TYPE_NORMAL;
        if (n.equalsIgnoreCase("slave")) return TYPE_SLAVE;
        if (n.equalsIgnoreCase("replica")) return TYPE_SLAVE;
        if (n.equalsIgnoreCase("pubsub")) return TYPE_PUBSUB;
        if (n.equalsIgnoreCase("master")) return TYPE_MASTER;
        return -1;
    }

    /** True while the client is in subscriber mode (CLIENT_PUBSUB). */
    static boolean inSubscriberMode(Client c) {
        return !c.subChannels.isEmpty() || !c.subPatterns.isEmpty() || !c.subShard.isEmpty();
    }

    /** networking.c validateClientAttr(): printable ASCII, no spaces, no control chars. */
    static boolean validAttr(byte[] v) {
        for (byte b : v) {
            int x = b & 0xff;
            if (x < '!' || x > '~') return false;
        }
        return true;
    }

    static void setClientName(Client c, byte[] name) {
        if (name.length != 0 && !validAttr(name))
            throw new RedisError("ERR Client names cannot contain spaces, newlines or special characters.");
        c.name = Util.s(name);
    }

    // ------------------------------------------------------------------
    // PING / ECHO
    // ------------------------------------------------------------------
    static void ping(Client c) {
        if (c.argc > 2) throw arity("ping");
        if (inSubscriberMode(c) && c.resp == 2) {
            c.addArray(2);
            c.addBulk(Util.b("pong"));
            if (c.argc == 1) c.addBulk(Util.EMPTY);
            else c.addBulk(c.arg(1));
        } else {
            if (c.argc == 1) c.addSimple("PONG");
            else c.addBulk(c.arg(1));
        }
    }

    static void echo(Client c) { c.addBulk(c.arg(1)); }

    // ------------------------------------------------------------------
    // AUTH
    // ------------------------------------------------------------------
    static void auth(Client c) {
        Server s = Server.I;
        if (c.argc > 3) throw RedisError.syntax();
        String username;
        byte[] password;
        if (c.argc == 2) {
            AclState.User du = AclState.defaultUser;
            if (du != null && (du.flags & AclState.F_NOPASS) != 0) {
                /* acl.c authCommand(): the 2-argument form mimics the old behaviour
                 * of erroring out when the default user has no password set. */
                throw new RedisError("ERR AUTH <password> called without any password "
                        + "configured for the default user. Are you sure "
                        + "your configuration is correct?");
            }
            username = "default";
            password = c.arg(1);
        } else {
            username = c.argStr(1);
            password = c.arg(2);
        }
        if (!AclState.authenticate(s, c, username, password)) {
            /* The 2-argument form predates ACLs and still reports the old error
             * in every redis before 6.0; the user/password form is 6.0+ only. */
            throw new RedisError(c.argc == 2 ? "ERR invalid password"
                    : "WRONGPASS invalid username-password pair or user is disabled.");
        }
        c.addOk();
    }

    // ------------------------------------------------------------------
    // HELLO
    // ------------------------------------------------------------------
    static void hello(Client c) {
        Server s = Server.I;
        long ver = 0;
        int nextArg = 1;
        if (c.argc >= 2) {
            Long v = Util.string2ll(c.arg(1));
            if (v == null)
                throw new RedisError("ERR Protocol version is not an integer or out of range");
            ver = v;
            nextArg = 2;
            if (ver < 2 || ver > 3) throw new RedisError("NOPROTO unsupported protocol version");
        }

        String username = null;
        byte[] password = null;
        byte[] clientname = null;
        for (int j = nextArg; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "AUTH") && moreargs >= 2) {
                username = c.argStr(j + 1);
                password = c.arg(j + 2);
                j += 2;
            } else if (c.argIs(j, "SETNAME") && moreargs >= 1) {
                clientname = c.arg(j + 1);
                if (clientname.length != 0 && !validAttr(clientname))
                    throw new RedisError("ERR Client names cannot contain spaces, newlines or special characters.");
                j++;
            } else {
                throw new RedisError("ERR Syntax error in HELLO option '" + c.argStr(j) + "'");
            }
        }

        if (username != null) {
            if (!AclState.authenticate(s, c, username, password))
                throw new RedisError("WRONGPASS invalid username-password pair or user is disabled.");
        }

        if (!c.authenticated) {
            throw new RedisError("NOAUTH HELLO must be called with the client already authenticated, "
                    + "otherwise the HELLO <proto> AUTH <user> <pass> option can be used to "
                    + "authenticate the client and select the RESP protocol version at the same time");
        }

        if (clientname != null) setClientName(c, clientname);
        if (ver != 0) c.resp = (int) ver;

        c.addMap(7);
        c.addBulk(Util.b("server"));
        c.addBulk(Util.b("redis"));
        c.addBulk(Util.b("version"));
        c.addBulk(Util.b(Server.VERSION));
        c.addBulk(Util.b("proto"));
        c.addInt(c.resp);
        c.addBulk(Util.b("id"));
        c.addInt(c.id);
        c.addBulk(Util.b("mode"));
        c.addBulk(Util.b(s.config.getBool("cluster-enabled") ? "cluster" : "standalone"));
        c.addBulk(Util.b("role"));
        c.addBulk(Util.b(s.repl != null && s.repl.masterHost != null ? "replica" : "master"));
        c.addBulk(Util.b("modules"));
        c.addEmptyArray();
    }

    // ------------------------------------------------------------------
    // QUIT / RESET
    // ------------------------------------------------------------------
    static void quit(Client c) {
        c.addOk();
        c.closeAfterReply = true;
    }

    static void reset(Client c) {
        Server s = Server.I;
        boolean monitor = c.isMonitor;
        if (!monitor && c.isReplica)
            throw new RedisError("ERR can only reset normal client connections");
        if (monitor) {
            s.monitors.remove(c);
            c.isMonitor = false;
            c.isReplica = false;
        }
        disableTracking(c);
        c.db = 0;
        c.resp = s.config.getInt("client-default-resp") >= 3 ? 3 : 2;
        AclState.initClient(s, c);
        Multi.discard(s, c);
        s.pubsub.unsubscribeAll(s, c);
        c.name = "";
        /* lib-name / lib-ver survive a RESET: they describe the client library. */
        c.replyOff = false;
        c.replySkip = false;
        c.replySkipNext = false;
        c.noTouch = false;
        c.noEvict = false;
        c.addSimple("RESET");
    }

    // ------------------------------------------------------------------
    // CLIENT
    // ------------------------------------------------------------------
    static void client(Client c) {
        markSub(c, "client");
        CommandTable.dispatchSub(c, "client");
    }

    static void clientId(Client c) {
        if (c.argc != 2) throw arity("client|id");
        c.addInt(c.id);
    }

    static void clientInfo(Client c) {
        if (c.argc != 2) throw arity("client|info");
        /* CLIENT INFO arrived at 6.0, after catClientInfoString() had changed
         * both its field order and the meaning of qbuf, so it reports the 6.x
         * line; CLIENT LIST is pinned to the 2.8 shape it has always had here.
         * See Client.infoLineModern() for why this port has to carry two. */
        c.addVerbatim("txt", Util.b(c.infoLineModern() + "\n"));
    }

    static String allClientsInfo(Server s, int type) {
        StringBuilder sb = new StringBuilder();
        for (Client cl : new ArrayList<>(s.clients.values())) {
            if (type != -1 && clientType(cl) != type) continue;
            sb.append(cl.infoLine()).append('\n');
        }
        return sb.toString();
    }

    static void clientList(Client c) {
        Server s = Server.I;
        int type = -1;
        String o = null;
        if (c.argc == 4 && c.argIs(2, "type")) {
            type = typeByName(c.argStr(3));
            if (type == -1) throw new RedisError("ERR Unknown client type '" + c.argStr(3) + "'");
        } else if (c.argc > 3 && c.argIs(2, "id")) {
            StringBuilder sb = new StringBuilder();
            for (int j = 3; j < c.argc; j++) {
                Long cid = Util.string2ll(c.arg(j));
                if (cid == null) throw new RedisError("ERR Invalid client ID");
                Client cl = s.clients.get(cid);
                if (cl != null) sb.append(cl.infoLine()).append('\n');
            }
            o = sb.toString();
        } else if (c.argc != 2) {
            throw RedisError.syntax();
        }
        if (o == null) o = allClientsInfo(s, type);
        c.addVerbatim("txt", Util.b(o));
    }

    static void clientGetname(Client c) {
        if (c.argc != 2) throw arity("client|getname");
        if (c.name == null || c.name.isEmpty()) c.addNil();
        else c.addBulk(Util.b(c.name));
    }

    static void clientSetname(Client c) {
        if (c.argc != 3) throw arity("client|setname");
        setClientName(c, c.arg(2));
        c.addOk();
    }

    static void clientSetinfo(Client c) {
        if (c.argc != 4) throw arity("client|setinfo");
        String attr = c.argStr(2);
        byte[] val = c.arg(3);
        boolean libName = c.argIs(2, "lib-name");
        boolean libVer = c.argIs(2, "lib-ver");
        if (!libName && !libVer) throw new RedisError("ERR Unrecognized option '" + attr + "'");
        if (!validAttr(val))
            throw new RedisError("ERR " + attr + " cannot contain spaces, newlines or special characters.");
        if (libName) c.libName = Util.s(val);
        else c.libVer = Util.s(val);
        c.addOk();
    }

    /** Only users that exist may be named by CLIENT KILL USER. */
    static boolean userExists(Server s, String u) {
        if (u.equals("default")) return true;
        for (Client cl : s.clients.values()) if (u.equals(cl.user)) return true;
        return false;
    }

    static void clientKill(Client c) {
        Server s = Server.I;
        if (c.argc < 3) throw arity("client|kill");
        String addr = null, laddr = null, user = null;
        int type = -1;
        long id = 0, maxAge = 0;
        boolean skipme = true;
        boolean oldForm = c.argc == 3;

        if (oldForm) {
            addr = c.argStr(2);
            skipme = false;    // with the old form you can kill yourself
        } else {
            int i = 2;
            while (i < c.argc) {
                boolean more = c.argc > i + 1;
                if (c.argIs(i, "id") && more) {
                    Long t = Util.string2ll(c.arg(i + 1));
                    if (t == null || t < 1)
                        throw new RedisError("ERR client-id should be greater than 0");
                    id = t;
                } else if (c.argIs(i, "maxage") && more) {
                    Long t = Util.string2ll(c.arg(i + 1));
                    if (t == null)
                        throw new RedisError("ERR maxage is not an integer or out of range");
                    if (t <= 0) throw new RedisError("ERR maxage should be greater than 0");
                    maxAge = t;
                } else if (c.argIs(i, "type") && more) {
                    type = typeByName(c.argStr(i + 1));
                    if (type == -1)
                        throw new RedisError("ERR Unknown client type '" + c.argStr(i + 1) + "'");
                } else if (c.argIs(i, "addr") && more) {
                    addr = c.argStr(i + 1);
                } else if (c.argIs(i, "laddr") && more) {
                    laddr = c.argStr(i + 1);
                } else if (c.argIs(i, "user") && more) {
                    user = c.argStr(i + 1);
                    if (!userExists(s, user))
                        throw new RedisError("ERR No such user '" + user + "'");
                } else if (c.argIs(i, "skipme") && more) {
                    if (c.argIs(i + 1, "yes")) skipme = true;
                    else if (c.argIs(i + 1, "no")) skipme = false;
                    else throw RedisError.syntax();
                } else {
                    throw RedisError.syntax();
                }
                i += 2;
            }
        }

        int killed = 0;
        boolean closeThis = false;
        long nowSec = s.mstime() / 1000;
        for (Client cl : new ArrayList<>(s.clients.values())) {
            if (addr != null && !addr.equals(cl.addr)) continue;
            if (laddr != null && !laddr.equals(cl.laddr)) continue;
            if (type != -1 && clientType(cl) != type) continue;
            if (id != 0 && cl.id != id) continue;
            if (user != null && !user.equals(cl.user)) continue;
            if (cl == c && skipme) continue;
            if (maxAge != 0 && (nowSec - cl.createTime / 1000) < maxAge) continue;
            if (cl == c) closeThis = true;
            else s.freeClient(cl);
            killed++;
        }

        if (oldForm) {
            if (killed == 0) c.addError("ERR No such client");
            else c.addOk();
        } else {
            c.addInt(killed);
        }
        /* Flag ourselves only after the reply has been queued. */
        if (closeThis) c.closeAfterReply = true;
    }

    static void clientUnblock(Client c) {
        Server s = Server.I;
        if (c.argc != 3 && c.argc != 4) throw arity("client|unblock");
        boolean asError = false;
        if (c.argc == 4) {
            if (c.argIs(3, "timeout")) asError = false;
            else if (c.argIs(3, "error")) asError = true;
            else throw new RedisError("ERR CLIENT UNBLOCK reason should be TIMEOUT or ERROR");
        }
        long id = c.argLong(2);
        Client target = s.clients.get(id);
        if (target != null && target.blocked) {
            if (asError) {
                target.addError("UNBLOCKED client unblocked via CLIENT UNBLOCK");
            } else {
                Blocking.Timeout t = Blocking.TIMEOUT.get(target.blockType);
                if (t != null) t.onTimeout(s, target);
                else target.addNilArray();
            }
            s.unblockClient(target);
            s.updateInterest(target);
            c.addInt(1);
        } else {
            c.addInt(0);
        }
    }

    static void clientUnpause(Client c) {
        if (c.argc != 2) throw arity("client|unpause");
        Cluster.unpauseClients(Server.I);
        c.addOk();
    }

    static void clientPause(Client c) {
        if (c.argc != 3 && c.argc != 4) throw arity("client|pause");
        if (c.argc == 4 && !c.argIs(3, "write") && !c.argIs(3, "all"))
            throw new RedisError("ERR CLIENT PAUSE mode must be WRITE or ALL");
        Long t = Util.string2ll(c.arg(2));
        if (t == null) throw new RedisError("ERR timeout is not an integer or out of range");
        if (t < 0) throw new RedisError("ERR timeout is negative");
        /* networking.c pauseClients(): WRITE only defers write commands, which
         * needs per-command postponing; ALL (and the mode-less redis <= 5 form)
         * freezes the connection entirely. */
        if (!(c.argc == 4 && c.argIs(3, "write"))) Cluster.pauseClients(Server.I.mstime() + t);
        c.addOk();
    }

    // ------------------------------------------------------------------
    // client eviction (redis 7.0, src/evict.c evictClients())
    // ------------------------------------------------------------------
    /** INFO stats evicted_clients */
    public static long evictedClients = 0;

    /** config.c: `maxmemory-clients` is a size, or a percentage of maxmemory. */
    public static long maxmemoryClients(Server s) {
        String v = s.config.getOr("maxmemory-clients", "0");
        if (v == null) return 0;
        v = v.trim();
        if (v.endsWith("%")) {
            double pct;
            try { pct = Double.parseDouble(v.substring(0, v.length() - 1).trim()); }
            catch (NumberFormatException e) { return 0; }
            return (long) (s.config.getLong("maxmemory") * pct / 100.0);
        }
        return Config.parseMemory(v);
    }

    /**
     * evict.c evictClients(): while the total memory used by the client
     * connections is over `maxmemory-clients`, close the biggest ones.  Replicas,
     * the master link and CLIENT NO-EVICT ON connections are never evicted.
     */
    public static void evictClients(Server s) {
        /* The common case is "disabled": keep it a single map lookup. */
        String cfg = s.config.getOr("maxmemory-clients", "0");
        if (cfg == null || cfg.isEmpty() || cfg.equals("0") || cfg.equals("0%")) return;
        long limit = maxmemoryClients(s);
        if (limit <= 0) return;
        long total = 0;
        List<Client> cands = new ArrayList<>();
        for (Client cl : s.clients.values()) {
            if (cl.isReplica || cl.isMonitor) continue;
            if (s.repl != null && s.repl.isMasterClient(cl)) continue;
            total += cl.memoryUsage();
            if (!cl.noEvict) cands.add(cl);
        }
        if (total <= limit || cands.isEmpty()) return;
        cands.sort((a, b) -> Long.compare(b.memoryUsage(), a.memoryUsage()));
        for (Client cl : cands) {
            if (total <= limit) break;
            total -= cl.memoryUsage();
            s.logNotice("Evicting client: " + cl.infoLine());
            evictedClients++;
            s.freeClient(cl);
        }
    }

    static void clientNoEvict(Client c) {
        if (c.argc != 3) throw arity("client|no-evict");
        if (c.argIs(2, "on")) c.noEvict = true;
        else if (c.argIs(2, "off")) c.noEvict = false;
        else throw RedisError.syntax();
        c.addOk();
    }

    static void clientNoTouch(Client c) {
        if (c.argc != 3) throw arity("client|no-touch");
        if (c.argIs(2, "on")) c.noTouch = true;
        else if (c.argIs(2, "off")) c.noTouch = false;
        else throw RedisError.syntax();
        c.addOk();
    }

    static void clientReply(Client c) {
        if (c.argc != 3) throw arity("client|reply");
        if (c.argIs(2, "on")) {
            c.replySkip = false;
            c.replySkipNext = false;
            c.replyOff = false;
            c.addOk();
        } else if (c.argIs(2, "off")) {
            c.replyOff = true;
        } else if (c.argIs(2, "skip")) {
            if (!c.replyOff) c.replySkipNext = true;
        } else {
            throw RedisError.syntax();
        }
    }

    // ---------------- client side caching ----------------
    static void disableTracking(Client c) {
        Tracking.disableTracking(Server.I, c);
        c.trackingRedir = -1;
    }

    /** util.c stringCheckPrefix(): true when either string is a prefix of the other. */
    static boolean prefixOverlap(byte[] a, byte[] b) {
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
        return true;
    }

    static void clientTracking(Client c) {
        Server s = Server.I;
        if (c.argc < 3) throw arity("client|tracking");
        long redir = 0;
        boolean bcast = false, optin = false, optout = false, noloop = false;
        List<byte[]> prefix = new ArrayList<>();

        for (int j = 3; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "redirect") && moreargs > 0) {
                j++;
                if (redir != 0)
                    throw new RedisError("ERR A client can only redirect to a single other client");
                redir = c.argLong(j);
                if (s.clients.get(redir) == null)
                    throw new RedisError("ERR The client ID you want redirect to does not exist");
            } else if (c.argIs(j, "bcast")) {
                bcast = true;
            } else if (c.argIs(j, "optin")) {
                optin = true;
            } else if (c.argIs(j, "optout")) {
                optout = true;
            } else if (c.argIs(j, "noloop")) {
                noloop = true;
            } else if (c.argIs(j, "prefix") && moreargs > 0) {
                j++;
                prefix.add(c.arg(j));
            } else {
                throw RedisError.syntax();
            }
        }

        if (c.argIs(2, "on")) {
            if (!bcast && !prefix.isEmpty())
                throw new RedisError("ERR PREFIX option requires BCAST mode to be enabled");
            if (c.tracking && c.trackingBcast != bcast) {
                throw new RedisError("ERR You can't switch BCAST mode on/off before disabling "
                        + "tracking for this client, and then re-enabling it with a different mode.");
            }
            if (bcast && (optin || optout))
                throw new RedisError("ERR OPTIN and OPTOUT are not compatible with BCAST");
            if (optin && optout)
                throw new RedisError("ERR You can't specify both OPTIN mode and OPTOUT mode");
            if ((optin && c.trackingOptOut) || (optout && c.trackingOptIn)) {
                throw new RedisError("ERR You can't switch OPTIN/OPTOUT mode before disabling "
                        + "tracking for this client, and then re-enabling it with a different mode.");
            }
            if (bcast) checkPrefixCollisions(c, prefix);

            Tracking.enableTracking(s, c, redir, bcast, optin, optout, noloop, prefix);
        } else if (c.argIs(2, "off")) {
            disableTracking(c);
        } else {
            throw RedisError.syntax();
        }
        c.addOk();
    }

    static void checkPrefixCollisions(Client c, List<byte[]> prefixes) {
        for (int i = 0; i < prefixes.size(); i++) {
            for (byte[] have : c.trackingPrefixes) {
                if (prefixOverlap(have, prefixes.get(i))) {
                    throw new RedisError("ERR Prefix '" + Util.s(prefixes.get(i))
                            + "' overlaps with an existing prefix '" + Util.s(have)
                            + "'. Prefixes for a single client must not overlap.");
                }
            }
            for (int j = i + 1; j < prefixes.size(); j++) {
                if (prefixOverlap(prefixes.get(i), prefixes.get(j))) {
                    throw new RedisError("ERR Prefix '" + Util.s(prefixes.get(i))
                            + "' overlaps with another provided prefix '" + Util.s(prefixes.get(j))
                            + "'. Prefixes for a single client must not overlap.");
                }
            }
        }
    }

    static void clientCaching(Client c) {
        if (c.argc != 3) throw arity("client|caching");
        if (!c.tracking) {
            throw new RedisError("ERR CLIENT CACHING can be called only when the client is "
                    + "in tracking mode with OPTIN or OPTOUT mode enabled");
        }
        if (c.argIs(2, "yes")) {
            if (!c.trackingOptIn)
                throw new RedisError("ERR CLIENT CACHING YES is only valid when tracking is enabled in OPTIN mode.");
            c.trackingCachingOn = true;
        } else if (c.argIs(2, "no")) {
            if (!c.trackingOptOut)
                throw new RedisError("ERR CLIENT CACHING NO is only valid when tracking is enabled in OPTOUT mode.");
            c.trackingCachingOn = true;
        } else {
            throw RedisError.syntax();
        }
        c.addOk();
    }

    static void clientGetredir(Client c) {
        if (c.argc != 2) throw arity("client|getredir");
        c.addInt(c.tracking ? c.trackingRedir : -1);
    }

    static void clientTrackingInfo(Client c) {
        if (c.argc != 2) throw arity("client|trackinginfo");
        c.addMap(3);

        c.addBulk(Util.b("flags"));
        List<String> flags = new ArrayList<>();
        flags.add(c.tracking ? "on" : "off");
        if (c.trackingBcast) flags.add("bcast");
        if (c.trackingOptIn) {
            flags.add("optin");
            if (c.trackingCachingOn) flags.add("caching-yes");
        }
        if (c.trackingOptOut) {
            flags.add("optout");
            if (c.trackingCachingOn) flags.add("caching-no");
        }
        if (c.trackingNoloop) flags.add("noloop");
        if (c.trackingBrokenRedir) flags.add("broken_redirect");
        c.addSet(flags.size());
        for (String f : flags) c.addBulk(Util.b(f));

        c.addBulk(Util.b("redirect"));
        c.addInt(c.tracking ? c.trackingRedir : -1);

        c.addBulk(Util.b("prefixes"));
        c.addArray(c.trackingPrefixes.size());
        for (byte[] p : c.trackingPrefixes) c.addBulk(p);
    }

    static final String[] CLIENT_HELP = {
        "CACHING (YES|NO)",
        "    Enable/disable tracking of the keys for next command in OPTIN/OPTOUT modes.",
        "GETREDIR",
        "    Return the client ID we are redirecting to when tracking is enabled.",
        "GETNAME",
        "    Return the name of the current connection.",
        "ID",
        "    Return the ID of the current connection.",
        "INFO",
        "    Return information about the current client connection.",
        "KILL <ip:port>",
        "    Kill connection made from <ip:port>.",
        "KILL <option> <value> [<option> <value> [...]]",
        "    Kill connections. Options are:",
        "    * ADDR (<ip:port>|<unixsocket>:0)",
        "      Kill connections made from the specified address",
        "    * LADDR (<ip:port>|<unixsocket>:0)",
        "      Kill connections made to specified local address",
        "    * TYPE (NORMAL|MASTER|REPLICA|PUBSUB)",
        "      Kill connections by type.",
        "    * USER <username>",
        "      Kill connections authenticated by <username>.",
        "    * SKIPME (YES|NO)",
        "      Skip killing current connection (default: yes).",
        "    * ID <client-id>",
        "      Kill connections by client id.",
        "    * MAXAGE <maxage>",
        "      Kill connections older than the specified age.",
        "LIST [options ...]",
        "    Return information about client connections. Options:",
        "    * TYPE (NORMAL|MASTER|REPLICA|PUBSUB)",
        "      Return clients of specified type.",
        "UNPAUSE",
        "    Stop the current client pause, resuming traffic.",
        "PAUSE <timeout> [WRITE|ALL]",
        "    Suspend all, or just write, clients for <timeout> milliseconds.",
        "REPLY (ON|OFF|SKIP)",
        "    Control the replies sent to the current connection.",
        "SETNAME <name>",
        "    Assign the name <name> to the current connection.",
        "SETINFO <option> <value>",
        "    Set client meta attr. Options are:",
        "    * LIB-NAME: the client lib name.",
        "    * LIB-VER: the client lib version.",
        "UNBLOCK <clientid> [TIMEOUT|ERROR]",
        "    Unblock the specified blocked client.",
        "TRACKING (ON|OFF) [REDIRECT <id>] [BCAST] [PREFIX <prefix> [...]]",
        "         [OPTIN] [OPTOUT] [NOLOOP]",
        "    Control server assisted client side caching.",
        "TRACKINGINFO",
        "    Report tracking status for the current connection.",
        "NO-EVICT (ON|OFF)",
        "    Protect current client connection from eviction.",
        "NO-TOUCH (ON|OFF)",
        "    Will not touch LRU/LFU stats when this mode is on.",
    };

    static void clientHelp(Client c) {
        if (c.argc != 2) throw arity("client|help");
        addHelp(c, CLIENT_HELP);
    }
}
