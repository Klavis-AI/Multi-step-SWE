package redis;

import java.security.MessageDigest;
import java.util.*;

/**
 * The ACL subsystem (src/acl.c): users, selectors, command/key/channel rules,
 * the ACL log and the permission checks performed by the dispatcher.
 *
 * The model is a straight port of redis 8.0.6: a user owns an ordered list of
 * selectors, the first of which is the "root" selector holding the permissions
 * written outside of parentheses.
 */
public final class AclState {
    private AclState() {}

    /* Acl.enabled() gates the dispatcher check. The default user is created as
     * "on nopass ~* &* +@all", so keeping the check always armed behaves exactly
     * like redis with no ACL configured. */
    public static boolean enabled = true;

    // ------------------------------------------------------------------
    // command metadata
    // ------------------------------------------------------------------
    public static final int BITS = 1024;
    private static final int WORDS = BITS / 64;
    /** the reserved bit set by +@all so we can tell "can run future commands" */
    private static final int FUTURE_BIT = BITS - 1;

    /** ACL key permission bits (ACL_READ_PERMISSION / ACL_WRITE_PERMISSION) */
    static final int PERM_READ = 1, PERM_WRITE = 2, PERM_ALL = 3;

    /** keyspec flags */
    static final int K_ACCESS = 1, K_INSERT = 2, K_DELETE = 4, K_UPDATE = 8,
                     K_NOTKEY = 16, K_VARFLAGS = 32, K_INCOMPLETE = 64;

    /** channel spec flags (CMD_CHANNEL_*) */
    static final int CH_PATTERN = 1, CH_PUBLISH = 2, CH_SUBSCRIBE = 4, CH_UNSUBSCRIBE = 8;

    static final class KeySpec {
        int flags;
        int bsType;          // 0 index, 1 keyword, 2 unknown
        int bsIndex;
        String bsKeyword; int bsStartFrom;
        int fkType;          // 0 range, 1 keynum, 2 unknown
        int lastkey, keystep, limit;
        int keynumidx, firstkey;
    }

    static final class CmdMeta {
        final String fullname;
        CmdMeta(String n) { fullname = n; }
        int id = -1;
        int cats;
        boolean noAuth;
        List<KeySpec> specs = new ArrayList<>();
        String getKeys;      // name of the redis getkeys_proc, or null
        boolean hasSubs;
    }

    static final LinkedHashMap<String, CmdMeta> META = new LinkedHashMap<>();
    static final List<CmdMeta> BY_ID = new ArrayList<>();
    private static boolean inited = false;

    /** commands_with_channels[] (src/db.c) */
    private static final Object[][] CHANNELS = {
        {"subscribe", CH_SUBSCRIBE, 1, -1},
        {"ssubscribe", CH_SUBSCRIBE, 1, -1},
        {"unsubscribe", CH_UNSUBSCRIBE, 1, -1},
        {"sunsubscribe", CH_UNSUBSCRIBE, 1, -1},
        {"psubscribe", CH_PATTERN | CH_SUBSCRIBE, 1, -1},
        {"punsubscribe", CH_PATTERN | CH_UNSUBSCRIBE, 1, -1},
        {"publish", CH_PUBLISH, 1, 1},
        {"spublish", CH_PUBLISH, 1, 1},
    };

    static synchronized void initMeta() {
        if (inited) return;
        inited = true;
        for (String line : AclTables.CAT_DATA) {
            StringTokenizer st = new StringTokenizer(line, " ");
            while (st.hasMoreTokens()) {
                String name = st.nextToken();
                String m = st.nextToken();
                CmdMeta cm = new CmdMeta(name);
                if (m.endsWith("!")) { cm.noAuth = true; m = m.substring(0, m.length() - 1); }
                cm.cats = (int) Long.parseLong(m, 16);
                META.put(name, cm);
            }
        }
        for (String line : AclTables.KS_DATA) {
            for (String tok : line.split(" ")) {
                if (tok.isEmpty()) continue;
                int eq = tok.indexOf('=');
                String name = tok.substring(0, eq);
                String rest = tok.substring(eq + 1);
                CmdMeta cm = META.get(name);
                if (cm == null) continue;
                int sl = rest.lastIndexOf('/');
                if (sl >= 0) { cm.getKeys = rest.substring(sl + 1); rest = rest.substring(0, sl); }
                if (!rest.isEmpty()) for (String sp : rest.split(";")) cm.specs.add(parseSpec(sp));
            }
        }
        for (String n : META.keySet()) {
            int p = n.indexOf('|');
            if (p > 0) {
                CmdMeta parent = META.get(n.substring(0, p));
                if (parent != null) parent.hasSubs = true;
            }
        }
        int id = 0;
        for (CmdMeta cm : META.values()) { cm.id = id++; BY_ID.add(cm); }
    }

    private static KeySpec parseSpec(String s) {
        KeySpec k = new KeySpec();
        int i = 0;
        while (i < s.length()) {
            char ch = s.charAt(i);
            if (ch == 'R') k.flags |= K_ACCESS;
            else if (ch == 'I') k.flags |= K_INSERT;
            else if (ch == 'D') k.flags |= K_DELETE;
            else if (ch == 'U') k.flags |= K_UPDATE;
            else if (ch == 'N') k.flags |= K_NOTKEY;
            else if (ch == 'V') k.flags |= K_VARFLAGS;
            else if (ch == 'C') k.flags |= K_INCOMPLETE;
            else if (ch == '-') { /* no flags */ }
            else break;
            i++;
        }
        int colon = s.indexOf(':', i);
        String bs = s.substring(i, colon);
        String fk = s.substring(colon + 1);
        if (bs.charAt(0) == 'i') { k.bsType = 0; k.bsIndex = Integer.parseInt(bs.substring(1)); }
        else if (bs.charAt(0) == 'k') {
            k.bsType = 1;
            int c = bs.indexOf(',');
            k.bsStartFrom = Integer.parseInt(bs.substring(1, c));
            k.bsKeyword = bs.substring(c + 1);
        } else k.bsType = 2;
        if (fk.charAt(0) == 'r') {
            k.fkType = 0;
            String[] p = fk.substring(1).split(",");
            k.lastkey = Integer.parseInt(p[0]); k.keystep = Integer.parseInt(p[1]); k.limit = Integer.parseInt(p[2]);
        } else if (fk.charAt(0) == 'n') {
            k.fkType = 1;
            String[] p = fk.substring(1).split(",");
            k.keynumidx = Integer.parseInt(p[0]); k.keystep = Integer.parseInt(p[1]); k.firstkey = Integer.parseInt(p[2]);
        } else k.fkType = 2;
        return k;
    }

    /** ACLLookupCommand(): resolves "cmd" and "cmd|sub" against the ACL table. */
    static CmdMeta lookupCmd(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        int bars = 0;
        for (int i = 0; i < n.length(); i++) if (n.charAt(i) == '|') bars++;
        if (bars > 1) return null;
        return META.get(n);
    }

    static long catFlag(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        for (int i = 0; i < AclTables.CATS.length; i++) if (AclTables.CATS[i].equals(n)) return 1L << i;
        return 0;
    }

    // ------------------------------------------------------------------
    // key patterns / selectors / users
    // ------------------------------------------------------------------
    static final class KeyPat {
        int flags; final String pattern;
        KeyPat(String p, int f) { pattern = p; flags = f; }
        String describe() {
            if (flags == PERM_ALL) return "~" + pattern;
            if (flags == PERM_READ) return "%R~" + pattern;
            return "%W~" + pattern;
        }
    }

    public static final class Selector {
        boolean root;
        boolean allkeys, allchannels, allcommands;
        final long[] cmds = new long[WORDS];
        Map<Integer, List<String>> firstArgs = null;
        final List<KeyPat> patterns = new ArrayList<>();
        final List<String> channels = new ArrayList<>();
        final List<String> rules = new ArrayList<>();

        Selector copy() {
            Selector d = new Selector();
            d.root = root; d.allkeys = allkeys; d.allchannels = allchannels; d.allcommands = allcommands;
            System.arraycopy(cmds, 0, d.cmds, 0, WORDS);
            if (firstArgs != null) {
                d.firstArgs = new HashMap<>();
                for (Map.Entry<Integer, List<String>> e : firstArgs.entrySet())
                    d.firstArgs.put(e.getKey(), new ArrayList<>(e.getValue()));
            }
            for (KeyPat p : patterns) d.patterns.add(new KeyPat(p.pattern, p.flags));
            d.channels.addAll(channels);
            d.rules.addAll(rules);
            return d;
        }
        boolean getBit(int id) {
            if (id < 0 || id >= BITS) return false;
            return (cmds[id / 64] & (1L << (id % 64))) != 0;
        }
        void setBit(int id, boolean v) {
            if (id < 0 || id >= BITS) return;
            if (v) cmds[id / 64] |= (1L << (id % 64));
            else { cmds[id / 64] &= ~(1L << (id % 64)); allcommands = false; }
        }
        boolean futureCommands() { return getBit(FUTURE_BIT); }
        String rulesString() { return String.join(" ", rules); }
    }

    static final int F_ENABLED = 1, F_DISABLED = 2, F_NOPASS = 4, F_SANITIZE_SKIP = 8, F_SANITIZE = 16;
    private static final String[] FLAG_NAMES = {"on", "off", "nopass", "skip-sanitize-payload", "sanitize-payload"};
    private static final int[] FLAG_BITS = {F_ENABLED, F_DISABLED, F_NOPASS, F_SANITIZE_SKIP, F_SANITIZE};

    public static final class User {
        public String name;
        int flags = F_DISABLED | F_SANITIZE;
        final List<String> passwords = new ArrayList<>();
        final List<Selector> selectors = new ArrayList<>();

        User(String n) { name = n; selectors.add(newSelector(true)); }
        Selector root() { return selectors.get(0); }
        void copyFrom(User src) {
            flags = src.flags;
            passwords.clear(); passwords.addAll(src.passwords);
            selectors.clear();
            for (Selector s : src.selectors) selectors.add(s.copy());
        }
    }

    /** username -> user, in insertion order (redis uses a rax, i.e. sorted; see users()) */
    public static final TreeMap<String, User> USERS = new TreeMap<>();
    public static User defaultUser;

    /** server.acl_pubsub_default */
    static boolean pubsubDefaultAllChannels() {
        Server s = Server.I;
        if (s == null) return false;
        return "allchannels".equalsIgnoreCase(s.config.getOr("acl-pubsub-default", "resetchannels"));
    }

    static Selector newSelector(boolean root) {
        Selector sel = new Selector();
        sel.root = root;
        if (pubsubDefaultAllChannels()) sel.allchannels = true;
        return sel;
    }

    // ------------------------------------------------------------------
    // password helpers
    // ------------------------------------------------------------------
    static String hashPassword(byte[] clear) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] d = md.digest(clear);
            StringBuilder sb = new StringBuilder(64);
            for (byte b : d) sb.append(Character.forDigit((b >> 4) & 0xf, 16)).append(Character.forDigit(b & 0xf, 16));
            return sb.toString();
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    static boolean validHash(String h) {
        if (h.length() != 64) return false;
        for (int i = 0; i < 64; i++) {
            char c = h.charAt(i);
            if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) return false;
        }
        return true;
    }

    static boolean hasSpaces(String s) {
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == 0 || Character.isWhitespace(c)) return true;
        }
        return false;
    }

    // ------------------------------------------------------------------
    // ACLSetUser / ACLSetSelector
    // ------------------------------------------------------------------
    /** thrown instead of C's errno */
    static final class AclError extends RuntimeException {
        final String msg;
        AclError(String m) { super(m, null, false, false); msg = m; }
    }

    static final String E_NOENT = "Unknown command or category name in ACL";
    static final String E_INVAL = "Syntax error";
    static final String E_EXIST = "Adding a pattern after the * pattern (or the "
            + "'allkeys' flag) is not valid and does not have any "
            + "effect. Try 'resetkeys' to start with an empty "
            + "list of patterns";
    static final String E_ISDIR = "Adding a pattern after the * pattern (or the "
            + "'allchannels' flag) is not valid and does not have any "
            + "effect. Try 'resetchannels' to start with an empty "
            + "list of channels";
    static final String E_NODEV = "The password you are trying to remove from the user does not exist";
    static final String E_BADMSG = "The password hash must be exactly 64 characters and contain "
            + "only lowercase hexadecimal characters";
    static final String E_CHILD = "Allowing first-arg of a subcommand is not supported";

    static void setSelectorAll(Selector sel, boolean allow) {
        Arrays.fill(sel.cmds, allow ? -1L : 0L);
        sel.allcommands = allow;
        sel.rules.clear();
        sel.firstArgs = null;
    }

    static void resetFirstArgs(Selector sel, int id) {
        if (sel.firstArgs != null) sel.firstArgs.remove(id);
    }

    static void addFirstArg(Selector sel, int id, String sub) {
        if (sel.firstArgs == null) sel.firstArgs = new HashMap<>();
        List<String> l = sel.firstArgs.computeIfAbsent(id, x -> new ArrayList<>());
        for (String s : l) if (s.equalsIgnoreCase(sub)) return;
        l.add(sub);
    }

    static void changePerm(Selector sel, CmdMeta cm, boolean allow) {
        sel.setBit(cm.id, allow);
        resetFirstArgs(sel, cm.id);
        if (cm.hasSubs) {
            String prefix = cm.fullname + "|";
            for (CmdMeta sub : META.values())
                if (sub.fullname.startsWith(prefix)) sel.setBit(sub.id, allow);
        }
    }

    /** ACLUpdateCommandRules() */
    static void updateRules(Selector sel, String rule, boolean allow) {
        String nr = rule.toLowerCase(Locale.ROOT);
        for (Iterator<String> it = sel.rules.iterator(); it.hasNext(); ) {
            String ex = it.next().substring(1);
            if (ex.equals(nr) || (ex.length() > nr.length() && ex.startsWith(nr) && ex.charAt(nr.length()) == '|'))
                it.remove();
        }
        sel.rules.add((allow ? "+" : "-") + nr);
    }

    static void setSelectorCategory(Selector sel, String category, boolean allow) {
        long cflag = catFlag(category.substring(1));
        if (cflag == 0) throw new AclError(E_NOENT);
        updateRules(sel, category, allow);
        for (CmdMeta cm : META.values()) if ((cm.cats & cflag) != 0) changePerm(sel, cm, allow);
    }

    /** ACLSetSelector() */
    static void setSelector(Selector sel, String op) {
        if (op.equalsIgnoreCase("allkeys") || op.equals("~*")) {
            sel.allkeys = true; sel.patterns.clear();
        } else if (op.equalsIgnoreCase("resetkeys")) {
            sel.allkeys = false; sel.patterns.clear();
        } else if (op.equalsIgnoreCase("allchannels") || op.equals("&*")) {
            sel.allchannels = true; sel.channels.clear();
        } else if (op.equalsIgnoreCase("resetchannels")) {
            sel.allchannels = false; sel.channels.clear();
        } else if (op.equalsIgnoreCase("allcommands") || op.equalsIgnoreCase("+@all")) {
            setSelectorAll(sel, true);
        } else if (op.equalsIgnoreCase("nocommands") || op.equalsIgnoreCase("-@all")) {
            setSelectorAll(sel, false);
        } else if (op.charAt(0) == '~' || op.charAt(0) == '%') {
            if (sel.allkeys) throw new AclError(E_EXIST);
            int flags = 0;
            int offset = 1;
            if (op.charAt(0) == '%') {
                boolean permOk = true;
                for (; offset < op.length(); offset++) {
                    char ch = Character.toUpperCase(op.charAt(offset));
                    if (ch == 'R' && (flags & PERM_READ) == 0) flags |= PERM_READ;
                    else if (ch == 'W' && (flags & PERM_WRITE) == 0) flags |= PERM_WRITE;
                    else if (op.charAt(offset) == '~') { offset++; break; }
                    else { permOk = false; break; }
                }
                if (flags == 0 || !permOk) throw new AclError(E_INVAL);
            } else {
                flags = PERM_ALL;
            }
            String pat = op.substring(offset);
            if (hasSpaces(pat)) throw new AclError(E_INVAL);
            KeyPat found = null;
            for (KeyPat p : sel.patterns) if (p.pattern.equals(pat)) { found = p; break; }
            if (found == null) sel.patterns.add(new KeyPat(pat, flags));
            else found.flags |= flags;
            sel.allkeys = false;
        } else if (op.charAt(0) == '&') {
            if (sel.allchannels) throw new AclError(E_ISDIR);
            String pat = op.substring(1);
            if (hasSpaces(pat)) throw new AclError(E_INVAL);
            if (!sel.channels.contains(pat)) sel.channels.add(pat);
            sel.allchannels = false;
        } else if (op.charAt(0) == '+' && (op.length() < 2 || op.charAt(1) != '@')) {
            String body = op.substring(1);
            int bar = body.lastIndexOf('|');
            if (bar < 0) {
                CmdMeta cm = lookupCmd(body);
                if (cm == null) throw new AclError(E_NOENT);
                changePerm(sel, cm, true);
                updateRules(sel, cm.fullname, true);
            } else {
                String parentName = body.substring(0, bar);
                String sub = body.substring(bar + 1);
                CmdMeta cm = lookupCmd(parentName);
                if (cm == null) throw new AclError(E_NOENT);
                if (cm.fullname.indexOf('|') >= 0) throw new AclError(E_CHILD);
                if (sub.isEmpty()) throw new AclError(E_INVAL);
                if (cm.hasSubs) {
                    CmdMeta full = lookupCmd(body);
                    if (full == null) throw new AclError(E_NOENT);
                    changePerm(sel, full, true);
                } else {
                    addFirstArg(sel, cm.id, sub);
                }
                updateRules(sel, body, true);
            }
        } else if (op.charAt(0) == '-' && (op.length() < 2 || op.charAt(1) != '@')) {
            CmdMeta cm = lookupCmd(op.substring(1));
            if (cm == null) throw new AclError(E_NOENT);
            changePerm(sel, cm, false);
            updateRules(sel, cm.fullname, false);
        } else if ((op.charAt(0) == '+' || op.charAt(0) == '-') && op.length() > 1 && op.charAt(1) == '@') {
            setSelectorCategory(sel, op.substring(1), op.charAt(0) == '+');
        } else {
            throw new AclError(E_INVAL);
        }
    }

    static Selector selectorFromOpSet(String op) {
        Selector sel = newSelector(false);
        String trimmed = op.substring(1, op.length() - 1);
        for (String o : Config.splitArgs(trimmed)) setSelector(sel, o);
        return sel;
    }

    /** ACLSetUser() */
    static void setUser(User u, String op) {
        if (op.isEmpty()) return;
        if (op.equalsIgnoreCase("on")) { u.flags |= F_ENABLED; u.flags &= ~F_DISABLED; }
        else if (op.equalsIgnoreCase("off")) { u.flags |= F_DISABLED; u.flags &= ~F_ENABLED; }
        else if (op.equalsIgnoreCase("skip-sanitize-payload")) { u.flags |= F_SANITIZE_SKIP; u.flags &= ~F_SANITIZE; }
        else if (op.equalsIgnoreCase("sanitize-payload")) { u.flags &= ~F_SANITIZE_SKIP; u.flags |= F_SANITIZE; }
        else if (op.equalsIgnoreCase("nopass")) { u.flags |= F_NOPASS; u.passwords.clear(); }
        else if (op.equalsIgnoreCase("resetpass")) { u.flags &= ~F_NOPASS; u.passwords.clear(); }
        else if (op.charAt(0) == '>' || op.charAt(0) == '#') {
            String np;
            if (op.charAt(0) == '>') np = hashPassword(Util.b(op.substring(1)));
            else {
                np = op.substring(1);
                if (!validHash(np)) throw new AclError(E_BADMSG);
            }
            if (!u.passwords.contains(np)) u.passwords.add(np);
            u.flags &= ~F_NOPASS;
        } else if (op.charAt(0) == '<' || op.charAt(0) == '!') {
            String dp;
            if (op.charAt(0) == '<') dp = hashPassword(Util.b(op.substring(1)));
            else {
                dp = op.substring(1);
                if (!validHash(dp)) throw new AclError(E_BADMSG);
            }
            if (!u.passwords.remove(dp)) throw new AclError(E_NODEV);
        } else if (op.charAt(0) == '(' && op.charAt(op.length() - 1) == ')') {
            u.selectors.add(selectorFromOpSet(op));
        } else if (op.equalsIgnoreCase("clearselectors")) {
            while (u.selectors.size() > 1) u.selectors.remove(u.selectors.size() - 1);
        } else if (op.equalsIgnoreCase("reset")) {
            setUser(u, "resetpass");
            setUser(u, "resetkeys");
            setUser(u, "resetchannels");
            if (pubsubDefaultAllChannels()) setUser(u, "allchannels");
            setUser(u, "off");
            setUser(u, "sanitize-payload");
            setUser(u, "clearselectors");
            setUser(u, "-@all");
        } else {
            setSelector(u.root(), op);
        }
    }

    // ------------------------------------------------------------------
    // describe
    // ------------------------------------------------------------------
    static String describeCommandRules(Selector sel) {
        StringBuilder sb = new StringBuilder(sel.futureCommands() ? "+@all" : "-@all");
        if (!sel.rules.isEmpty()) sb.append(' ').append(sel.rulesString());
        return sb.toString();
    }

    static String describeKeys(Selector sel) {
        if (sel.allkeys) return "~*";
        StringBuilder sb = new StringBuilder();
        for (KeyPat p : sel.patterns) {
            if (sb.length() > 0) sb.append(' ');
            sb.append(p.describe());
        }
        return sb.toString();
    }

    static String describeChannels(Selector sel) {
        if (sel.allchannels) return "&*";
        StringBuilder sb = new StringBuilder();
        for (String ch : sel.channels) {
            if (sb.length() > 0) sb.append(' ');
            sb.append('&').append(ch);
        }
        return sb.toString();
    }

    static String describeSelector(Selector sel) {
        StringBuilder res = new StringBuilder();
        if (sel.allkeys) res.append("~* ");
        else for (KeyPat p : sel.patterns) res.append(p.describe()).append(' ');
        if (sel.allchannels) res.append("&* ");
        else {
            res.append("resetchannels ");
            for (String ch : sel.channels) res.append('&').append(ch).append(' ');
        }
        res.append(describeCommandRules(sel));
        return res.toString();
    }

    public static String describeUser(User u) {
        StringBuilder res = new StringBuilder();
        for (int j = 0; j < FLAG_NAMES.length; j++)
            if ((u.flags & FLAG_BITS[j]) != 0) res.append(FLAG_NAMES[j]).append(' ');
        for (String p : u.passwords) res.append('#').append(p).append(' ');
        boolean first = true;
        for (Selector sel : u.selectors) {
            String d = describeSelector(sel);
            if (first) { res.append(d); first = false; }
            else res.append(" (").append(d).append(')');
        }
        return res.toString();
    }

    // ------------------------------------------------------------------
    // ACLMergeSelectorArguments / ACLStringSetUser
    // ------------------------------------------------------------------
    static List<String> mergeSelectorArguments(List<String> argv, int[] invalidIdx) {
        List<String> out = new ArrayList<>();
        int open = -1;
        StringBuilder sel = null;
        for (int j = 0; j < argv.size(); j++) {
            String op = argv.get(j);
            if (open == -1 && !op.isEmpty() && op.charAt(0) == '(' && op.charAt(op.length() - 1) != ')') {
                sel = new StringBuilder(op);
                open = j;
                continue;
            }
            if (open != -1) {
                sel.append(' ').append(op);
                if (!op.isEmpty() && op.charAt(op.length() - 1) == ')') {
                    open = -1;
                    out.add(sel.toString());
                }
                continue;
            }
            out.add(op);
        }
        if (open != -1) { invalidIdx[0] = open; return null; }
        return out;
    }

    /** ACLStringSetUser(): stage every change on a temp user, apply atomically. */
    public static String stringSetUser(Server s, User u, String username, List<String> args) {
        int[] bad = new int[1];
        List<String> merged = mergeSelectorArguments(args, bad);
        if (merged == null)
            return "Unmatched parenthesis in acl selector starting at '" + args.get(bad[0]) + "'.";
        User temp = new User(username);
        if (u != null) temp.copyFrom(u);
        for (String op : merged) {
            try {
                setUser(temp, op);
            } catch (AclError e) {
                return "Error in ACL SETUSER modifier '" + op + "': " + e.msg;
            } catch (RuntimeException e) {
                return "Error in ACL SETUSER modifier '" + op + "': " + E_INVAL;
            }
        }
        if (u != null) killPubsubClientsIfNeeded(s, temp, u);
        if (u == null) {
            u = new User(username);
            USERS.put(username, u);
        }
        u.copyFrom(temp);
        return null;
    }

    // ------------------------------------------------------------------
    // pub/sub client eviction after a permission change
    // ------------------------------------------------------------------
    static List<String> upcomingChannelList(User nu, User orig) {
        for (Selector sel : nu.selectors) if (sel.allchannels) return null;
        List<String> upcoming = new ArrayList<>();
        for (Selector sel : nu.selectors) upcoming.addAll(sel.channels);
        boolean match = true;
        for (Selector sel : orig.selectors) {
            if (sel.allchannels) { match = false; break; }
            for (String ch : sel.channels) if (!upcoming.contains(ch)) { match = false; break; }
            if (!match) break;
        }
        return match ? null : upcoming;
    }

    static boolean channelAllowed(List<String> ref, byte[] chan, boolean isPattern) {
        for (String pat : ref) {
            if (isPattern) { if (pat.equals(Util.s(chan))) return true; }
            else if (Util.stringmatch(Util.b(pat), chan, false)) return true;
        }
        return false;
    }

    static boolean shouldKillPubsubClient(Client c, List<String> upcoming) {
        if (c.subChannels.isEmpty() && c.subPatterns.isEmpty() && c.subShard.isEmpty()) return false;
        for (Key k : c.subPatterns) if (!channelAllowed(upcoming, k.b, true)) return true;
        for (Key k : c.subChannels) if (!channelAllowed(upcoming, k.b, false)) return true;
        for (Key k : c.subShard) if (!channelAllowed(upcoming, k.b, false)) return true;
        return false;
    }

    static void killPubsubClientsIfNeeded(Server s, User nu, User orig) {
        if (s == null) return;
        List<String> channels = upcomingChannelList(nu, orig);
        if (channels == null) return;
        for (Client c : new ArrayList<>(s.clients.values())) {
            if (!orig.name.equals(c.user)) continue;
            if (shouldKillPubsubClient(c, channels)) deauthAndClose(s, c);
        }
    }

    public static void deauthAndClose(Server s, Client c) {
        c.user = "default";
        c.authenticated = false;
        c.closeAfterReply = true;
        if (!s.clientsToClose.contains(c)) s.clientsToClose.add(c);
    }

    public static void freeUserAndKillClients(Server s, User u) {
        for (Client c : new ArrayList<>(s.clients.values()))
            if (u.name.equals(c.user)) deauthAndClose(s, c);
    }

    // ------------------------------------------------------------------
    // key extraction (src/db.c getKeysFromCommandWithSpecs)
    // ------------------------------------------------------------------
    static final class KeyRef {
        final int pos; final int flags;
        KeyRef(int p, int f) { pos = p; flags = f; }
    }

    static int getKeysUsingKeySpecs(CmdMeta cm, byte[][] argv, int argc, List<KeyRef> out, int arity) {
        for (KeySpec spec : cm.specs) {
            if ((spec.flags & K_NOTKEY) != 0) continue;
            long first = 0, last, step;
            if (spec.bsType == 0) {
                first = spec.bsIndex;
            } else if (spec.bsType == 1) {
                int startIndex = spec.bsStartFrom > 0 ? spec.bsStartFrom : argc + spec.bsStartFrom;
                int endIndex = spec.bsStartFrom > 0 ? argc - 1 : 1;
                for (int i = startIndex; i != endIndex; i = startIndex <= endIndex ? i + 1 : i - 1) {
                    if (i >= argc || i < 1) break;
                    if (Util.equalsIgnoreCase(argv[i], spec.bsKeyword)) { first = i + 1; break; }
                }
                if (first == 0) continue;
            } else {
                out.clear(); return -1;
            }
            if (spec.fkType == 0) {
                step = spec.keystep;
                if (spec.lastkey >= 0) last = first + spec.lastkey;
                else if (spec.limit == 0) last = argc + spec.lastkey;
                else last = first + ((argc - first) / spec.limit + spec.lastkey);
            } else if (spec.fkType == 1) {
                step = spec.keystep;
                if (spec.keynumidx >= argc || first + spec.keynumidx >= argc) { out.clear(); return -1; }
                Long nk = Util.string2ll(argv[(int) first + spec.keynumidx]);
                if (nk == null || nk < 0) { out.clear(); return -1; }
                first += spec.firstkey;
                last = first + nk - 1;
            } else {
                out.clear(); return -1;
            }
            if (last >= argc || last < first || first >= argc) { out.clear(); return -1; }
            for (long i = first; i <= last; i += step) {
                if (i >= argc || i < first) { if (arity < 0) continue; else break; }
                out.add(new KeyRef((int) i, spec.flags));
            }
            if ((spec.flags & K_INCOMPLETE) != 0) { out.clear(); return -1; }
        }
        return out.size();
    }

    private static void genericGetKeys(int storeKeyOfs, int keyCountOfs, int firstKeyOfs, int keyStep,
                                       byte[][] argv, int argc, List<KeyRef> out) {
        int num;
        try { num = Integer.parseInt(Util.s(argv[keyCountOfs]).trim()); }
        catch (NumberFormatException e) { num = 0; }
        if (num < 1 || num > (argc - firstKeyOfs) / keyStep) return;
        for (int i = 0; i < num; i++) out.add(new KeyRef(firstKeyOfs + i * keyStep, 0));
        if (storeKeyOfs != 0) out.add(new KeyRef(storeKeyOfs, 0));
    }

    static List<KeyRef> getKeys(CmdMeta cm, byte[][] argv, int argc, int arity) {
        List<KeyRef> out = new ArrayList<>();
        boolean hasKeySpec = false, hasVarFlags = false;
        for (KeySpec sp : cm.specs) {
            if ((sp.flags & K_NOTKEY) == 0) hasKeySpec = true;
            if ((sp.flags & K_VARFLAGS) != 0) hasVarFlags = true;
        }
        if (hasKeySpec && !hasVarFlags) {
            if (getKeysUsingKeySpecs(cm, argv, argc, out, arity) >= 0) return out;
            out.clear();
        }
        String gk = cm.getKeys;
        if (gk == null) return out;
        switch (gk) {
            case "setGetKeys": {
                int flags = K_UPDATE;
                for (int i = 3; i < argc; i++)
                    if (Util.equalsIgnoreCase(argv[i], "get")) { flags = K_ACCESS | K_UPDATE; break; }
                out.add(new KeyRef(1, flags));
                break;
            }
            case "bitfieldGetKeys": {
                boolean readonly = true;
                for (int i = 2; i < argc; i++) {
                    int remargs = argc - i - 1;
                    if (Util.equalsIgnoreCase(argv[i], "get") && remargs >= 2) i += 2;
                    else if ((Util.equalsIgnoreCase(argv[i], "set") || Util.equalsIgnoreCase(argv[i], "incrby"))
                            && remargs >= 3) { readonly = false; break; }
                    else if (Util.equalsIgnoreCase(argv[i], "overflow") && remargs >= 1) i += 1;
                    else { readonly = false; break; }
                }
                out.add(new KeyRef(1, readonly ? K_ACCESS : (K_ACCESS | K_UPDATE)));
                break;
            }
            case "sortROGetKeys":
                out.add(new KeyRef(1, K_ACCESS));
                break;
            case "sortGetKeys": {
                out.add(new KeyRef(1, K_ACCESS));
                int storePos = -1;
                String[][] skiplist = {{"limit", "2"}, {"get", "1"}, {"by", "1"}};
                for (int i = 2; i < argc; i++) {
                    for (String[] sk : skiplist) {
                        if (Util.equalsIgnoreCase(argv[i], sk[0])) { i += Integer.parseInt(sk[1]); break; }
                        else if (Util.equalsIgnoreCase(argv[i], "store") && i + 1 < argc) { storePos = i + 1; break; }
                    }
                }
                if (storePos >= 0) out.add(new KeyRef(storePos, K_UPDATE));
                break;
            }
            case "migrateGetKeys": {
                int first = 3, num = 1;
                String[][] skip = {{"copy", "0"}, {"replace", "0"}, {"auth", "1"}, {"auth2", "2"}};
                if (argc > 6) {
                    for (int i = 6; i < argc; i++) {
                        if (Util.equalsIgnoreCase(argv[i], "keys")) {
                            if (argv[3].length > 0) num = 0;
                            else { first = i + 1; num = argc - first; }
                            break;
                        }
                        for (String[] sk : skip)
                            if (Util.equalsIgnoreCase(argv[i], sk[0])) { i += Integer.parseInt(sk[1]); break; }
                    }
                }
                for (int i = 0; i < num; i++) out.add(new KeyRef(first + i, K_ACCESS | K_DELETE));
                break;
            }
            case "evalGetKeys": case "blmpopGetKeys": case "functionGetKeys":
                genericGetKeys(0, 2, 3, 1, argv, argc, out); break;
            case "zunionInterDiffGetKeys": case "sintercardGetKeys": case "lmpopGetKeys": case "zmpopGetKeys":
                genericGetKeys(0, 1, 2, 1, argv, argc, out); break;
            case "zunionInterDiffStoreGetKeys":
                genericGetKeys(1, 2, 3, 1, argv, argc, out); break;
            default:
                break;
        }
        return out;
    }

    static boolean commandHasKeys(CmdMeta cm) {
        if (cm.getKeys != null) return true;
        for (KeySpec sp : cm.specs) if ((sp.flags & K_NOTKEY) == 0) return true;
        return false;
    }

    static Object[] channelSpec(String name) {
        for (Object[] cs : CHANNELS) if (cs[0].equals(name)) return cs;
        return null;
    }

    // ------------------------------------------------------------------
    // permission checks
    // ------------------------------------------------------------------
    public static final int ACL_OK = 0, DENIED_CMD = 1, DENIED_KEY = 2, DENIED_CHANNEL = 3, DENIED_AUTH = 4;

    static int selectorCheckKey(Selector sel, byte[] key, int keyspecFlags) {
        if (sel.allkeys) return ACL_OK;
        int keyFlags = 0;
        if ((keyspecFlags & K_ACCESS) != 0) keyFlags |= PERM_READ;
        if ((keyspecFlags & (K_INSERT | K_DELETE | K_UPDATE)) != 0) keyFlags |= PERM_WRITE;
        for (KeyPat p : sel.patterns) {
            if ((p.flags & keyFlags) != keyFlags) continue;
            if (Util.stringmatch(Util.b(p.pattern), key, false)) return ACL_OK;
        }
        return DENIED_KEY;
    }

    static boolean selectorHasUnrestrictedKeyAccess(Selector sel, int flags) {
        if (sel.allkeys) return true;
        int access = 0;
        if ((flags & K_ACCESS) != 0) access |= PERM_READ;
        if ((flags & (K_INSERT | K_DELETE | K_UPDATE)) != 0) access |= PERM_WRITE;
        for (KeyPat p : sel.patterns) {
            if ((p.flags & access) != access) continue;
            if (p.pattern.equals("*")) return true;
        }
        return false;
    }

    static int selectorCheckCmd(Selector sel, CmdMeta cm, boolean parent, byte[][] argv, int argc,
                                int[] idxptr, List<KeyRef>[] cache, int arity) {
        if (!sel.allcommands && !cm.noAuth) {
            if (!sel.getBit(cm.id)) {
                List<String> fa = sel.firstArgs == null ? null : sel.firstArgs.get(cm.id);
                if (argc < 2 || fa == null) return DENIED_CMD;
                int idx = parent ? 2 : 1;
                boolean ok = false;
                if (idx < argc) for (String s : fa) if (Util.equalsIgnoreCase(argv[idx], s)) { ok = true; break; }
                if (!ok) return DENIED_CMD;
            }
        }
        if (!sel.allkeys && commandHasKeys(cm)) {
            if (cache[0] == null) cache[0] = getKeys(cm, argv, argc, arity);
            for (KeyRef kr : cache[0]) {
                int ret = selectorCheckKey(sel, argv[kr.pos], kr.flags);
                if (ret != ACL_OK) { idxptr[0] = kr.pos; return ret; }
            }
        }
        if (!sel.allchannels) {
            Object[] cs = channelSpec(cm.fullname);
            if (cs != null && (((Integer) cs[1]) & (CH_PUBLISH | CH_SUBSCRIBE)) != 0) {
                int flags = (Integer) cs[1];
                int start = (Integer) cs[2];
                int count = (Integer) cs[3];
                int stop = count == -1 ? argc : start + count;
                if (stop > argc) stop = argc;
                boolean isPattern = (flags & CH_PATTERN) != 0;
                for (int i = start; i < stop; i++) {
                    if (!channelAllowed(sel.channels, argv[i], isPattern)) { idxptr[0] = i; return DENIED_CHANNEL; }
                }
            }
        }
        return ACL_OK;
    }

    @SuppressWarnings("unchecked")
    static int checkAllUserCommandPerm(User u, CmdMeta cm, boolean parent, byte[][] argv, int argc,
                                       int[] idxptr, int arity) {
        if (u == null) return ACL_OK;
        int relevant = DENIED_CMD;
        int lastIdx = 0;
        List<KeyRef>[] cache = new List[1];
        int[] local = new int[1];
        for (Selector sel : u.selectors) {
            local[0] = 0;
            int r = selectorCheckCmd(sel, cm, parent, argv, argc, local, cache, arity);
            if (r == ACL_OK) return ACL_OK;
            if (r > relevant || (r == relevant && local[0] > lastIdx)) { relevant = r; lastIdx = local[0]; }
        }
        idxptr[0] = lastIdx;
        return relevant;
    }

    public static int checkChannelPerm(User u, byte[] channel, boolean isPattern) {
        if (u == null) return ACL_OK;
        for (Selector sel : u.selectors) {
            if (sel.allchannels) return ACL_OK;
            if (channelAllowed(sel.channels, channel, isPattern)) return ACL_OK;
        }
        return DENIED_CHANNEL;
    }

    public static int checkKeyPerm(User u, byte[] key, int flags) {
        if (u == null) return ACL_OK;
        for (Selector sel : u.selectors) if (selectorCheckKey(sel, key, flags) == ACL_OK) return ACL_OK;
        return DENIED_KEY;
    }

    /** ACLUserCheckCmdWithUnrestrictedKeyAccess() -- used by SORT's BY/GET check */
    @SuppressWarnings("unchecked")
    public static boolean checkCmdWithUnrestrictedKeyAccess(User u, CmdMeta cm, byte[][] argv, int argc, int flags) {
        if (u == null) return true;
        List<KeyRef>[] cache = new List[1];
        int[] local = new int[1];
        for (Selector sel : u.selectors) {
            int r = selectorCheckCmd(sel, cm, false, argv, argc, local, cache, -1);
            if (r == ACL_OK && selectorHasUnrestrictedKeyAccess(sel, flags)) return true;
        }
        return false;
    }

    /** db.c getKeysFromCommand(): the argv positions holding keys. */
    public static List<Integer> keyPositions(Cmd cmd, byte[][] argv, int argc) {
        initMeta();
        List<Integer> out = new ArrayList<>();
        CmdMeta cm = resolve(cmd, argv, argc);
        if (cm == null) return out;
        for (KeyRef kr : getKeys(cm, argv, argc, cmd.arity)) out.add(kr.pos);
        return out;
    }

    /** sort.c: BY/GET patterns need unrestricted read access to the key space. */
    public static boolean userHasFullKeyAccess(Client c, Cmd cmd) {
        initMeta();
        User u = userOf(c);
        if (u == null) return true;
        CmdMeta cm = resolve(cmd, c.argv, c.argc);
        if (cm == null) return true;
        return checkCmdWithUnrestrictedKeyAccess(u, cm, c.argv, c.argc, K_ACCESS);
    }

    public static User user(String name) { return USERS.get(name); }

    public static User userOf(Client c) {
        if (c == null) return defaultUser;
        User u = USERS.get(c.user);
        return u != null ? u : defaultUser;
    }

    static String aclErrorMessage(int res, User u, String cmdName, byte[] erroredVal, boolean verbose) {
        switch (res) {
            case DENIED_CMD:
                return "User " + u.name + " has no permissions to run the '" + cmdName + "' command";
            case DENIED_KEY:
                return verbose ? "User " + u.name + " has no permissions to access the '"
                        + Util.s(erroredVal) + "' key" : "No permissions to access a key";
            case DENIED_CHANNEL:
                return verbose ? "User " + u.name + " has no permissions to access the '"
                        + Util.s(erroredVal) + "' channel" : "No permissions to access a channel";
        }
        return "No permissions";
    }

    /** Resolve the ACL command (subcommand included) for a dispatched command. */
    public static CmdMeta resolve(Cmd cmd, byte[][] argv, int argc) {
        initMeta();
        String name = cmd.fullName;
        if (argc >= 2 && !name.contains("|")) {
            CmdMeta sub = META.get(name + "|" + Util.lower(argv[1]));
            if (sub != null) return sub;
        }
        return META.get(name);
    }

    /**
     * The dispatcher hook: returns null when the command may run, or the error
     * reply (without the leading '-') when it may not.
     */
    public static String check(Server s, Client c, Cmd cmd) {
        return check(s, c, cmd, c, c.inMulti ? "multi" : "toplevel");
    }

    public static String check(Server s, Client c, Cmd cmd, Client real, String ctx) {
        initMeta();
        User u = userOf(c);
        if (u == null) return null;
        CmdMeta cm = resolve(cmd, c.argv, c.argc);
        if (cm == null) return null;
        boolean parent = cm.fullname.indexOf('|') >= 0;
        int[] idx = new int[1];
        int r = checkAllUserCommandPerm(u, cm, parent, c.argv, c.argc, idx, cmd.arity);
        if (r == ACL_OK) return null;
        byte[] obj = idx[0] < c.argc ? c.argv[idx[0]] : Util.EMPTY;
        addLogEntry(s, real != null ? real : c, r, ctx, u.name,
                r == DENIED_CMD ? Util.b(cm.fullname) : obj);
        return "NOPERM " + aclErrorMessage(r, u, cm.fullname, obj, false);
    }

    /** The EXEC-time re-check (multi.c). */
    public static String checkExec(Server s, Client c, Cmd cmd) {
        initMeta();
        User u = userOf(c);
        if (u == null) return null;
        CmdMeta cm = resolve(cmd, c.argv, c.argc);
        if (cm == null) return null;
        boolean parent = cm.fullname.indexOf('|') >= 0;
        int[] idx = new int[1];
        int r = checkAllUserCommandPerm(u, cm, parent, c.argv, c.argc, idx, cmd.arity);
        if (r == ACL_OK) return null;
        String reason;
        switch (r) {
            case DENIED_CMD: reason = "no permission to execute the command or subcommand"; break;
            case DENIED_KEY: reason = "no permission to touch the specified keys"; break;
            case DENIED_CHANNEL: reason = "no permission to access one of the channels used as arguments"; break;
            default: reason = "no permission"; break;
        }
        byte[] obj = idx[0] < c.argc ? c.argv[idx[0]] : Util.EMPTY;
        addLogEntry(s, c, r, "multi", u.name, r == DENIED_CMD ? Util.b(cm.fullname) : obj);
        return "NOPERM ACLs rules changed between the moment the transaction was accumulated and the "
                + "EXEC call. This command is no longer allowed for the following reason: " + reason;
    }

    /** The Lua redis.call() check (script.c scriptVerifyACL()). */
    public static String checkScript(Server s, Client fake, Client caller, Cmd cmd) {
        initMeta();
        User u = userOf(fake);
        if (u == null) return null;
        CmdMeta cm = resolve(cmd, fake.argv, fake.argc);
        if (cm == null) return null;
        boolean parent = cm.fullname.indexOf('|') >= 0;
        int[] idx = new int[1];
        int r = checkAllUserCommandPerm(u, cm, parent, fake.argv, fake.argc, idx, cmd.arity);
        if (r == ACL_OK) return null;
        byte[] obj = idx[0] < fake.argc ? fake.argv[idx[0]] : Util.EMPTY;
        addLogEntry(s, caller, r, "lua", u.name, r == DENIED_CMD ? Util.b(cm.fullname) : obj);
        return "ACL failure in script: " + aclErrorMessage(r, u, cm.fullname, obj, false);
    }

    // ------------------------------------------------------------------
    // authentication
    // ------------------------------------------------------------------
    /** true when the default user cannot authenticate new connections implicitly */
    public static boolean authRequired(Server s, Client c) {
        initMeta();
        if (defaultUser == null) return false;
        boolean need = ((defaultUser.flags & F_NOPASS) == 0) || ((defaultUser.flags & F_DISABLED) != 0);
        return need && !c.authenticated;
    }

    public static void initClient(Server s, Client c) {
        initMeta();
        c.user = "default";
        c.authenticated = defaultUser != null
                && (defaultUser.flags & F_NOPASS) != 0 && (defaultUser.flags & F_DISABLED) == 0;
    }

    /** ACLCheckUserCredentials() */
    public static boolean checkCredentials(String username, byte[] password) {
        User u = USERS.get(username);
        if (u == null) return false;
        if ((u.flags & F_DISABLED) != 0) return false;
        if ((u.flags & F_NOPASS) != 0) return true;
        String hashed = hashPassword(password);
        for (String p : u.passwords) if (p.equals(hashed)) return true;
        return false;
    }

    /** authenticate or record the failure in the ACL log; returns true on success */
    public static boolean authenticate(Server s, Client c, String username, byte[] password) {
        initMeta();
        if (checkCredentials(username, password)) {
            c.authenticated = true;
            c.user = username;
            return true;
        }
        addLogEntry(s, c, DENIED_AUTH, c.inMulti ? "multi" : "toplevel", username,
                c.argc > 0 ? c.argv[0] : Util.b("AUTH"));
        return false;
    }

    /** CONFIG SET requirepass -> ACLUpdateDefaultUserPassword() */
    public static void updateDefaultUserPassword(String password) {
        initMeta();
        if (defaultUser == null) return;
        setUser(defaultUser, "resetpass");
        if (password != null && !password.isEmpty()) setUser(defaultUser, ">" + password);
        else setUser(defaultUser, "nopass");
    }

    // ------------------------------------------------------------------
    // ACL log
    // ------------------------------------------------------------------
    public static final class LogEntry {
        public long count = 1;
        public int reason;
        public String context;
        public String object;
        public String username;
        public long ctime;
        public String cinfo;
        public long entryId;
        public long timestampCreated;
    }

    public static final LinkedList<LogEntry> LOG = new LinkedList<>();
    public static long logEntryCount = 0;
    public static long deniedAuth = 0, deniedCmd = 0, deniedKey = 0, deniedChannel = 0;
    private static final long LOG_GROUPING_MAX_TIME_DELTA = 60000;

    static void updateInfoMetrics(int reason) {
        switch (reason) {
            case DENIED_AUTH: deniedAuth++; break;
            case DENIED_CMD: deniedCmd++; break;
            case DENIED_KEY: deniedKey++; break;
            case DENIED_CHANNEL: deniedChannel++; break;
            default: break;
        }
    }

    static long maxLen(Server s) {
        if (s == null) return 128;
        return s.config.getLong("acllog-max-len");
    }

    static void trimLog(Server s) {
        while (LOG.size() > maxLen(s)) LOG.removeLast();
    }

    public static void addLogEntry(Server s, Client c, int reason, String ctx, String username, byte[] object) {
        updateInfoMetrics(reason);
        if (maxLen(s) == 0) { trimLog(s); return; }
        LogEntry le = new LogEntry();
        le.reason = reason;
        le.context = ctx;
        le.username = username;
        le.object = object == null ? "" : Util.s(object);
        le.ctime = System.currentTimeMillis();
        le.timestampCreated = le.ctime;
        le.entryId = logEntryCount;
        le.cinfo = c != null ? c.infoLine() : "";
        int toscan = 10;
        LogEntry match = null;
        for (Iterator<LogEntry> it = LOG.iterator(); it.hasNext() && toscan-- > 0; ) {
            LogEntry cur = it.next();
            if (cur.reason == le.reason && cur.context.equals(le.context)
                    && Math.abs(cur.ctime - le.ctime) <= LOG_GROUPING_MAX_TIME_DELTA
                    && cur.object.equals(le.object) && cur.username.equals(le.username)) {
                match = cur;
                it.remove();
                break;
            }
        }
        if (match != null) {
            match.cinfo = le.cinfo;
            match.ctime = le.ctime;
            match.count++;
            LOG.addFirst(match);
        } else {
            logEntryCount++;
            LOG.addFirst(le);
            trimLog(s);
        }
    }

    // ------------------------------------------------------------------
    // startup / file loading
    // ------------------------------------------------------------------
    public static void createDefaultUser() {
        User u = new User("default");
        setUser(u, "+@all");
        setUser(u, "~*");
        setUser(u, "&*");
        setUser(u, "on");
        setUser(u, "nopass");
        USERS.put("default", u);
        defaultUser = u;
    }

    public static void init(Server s) {
        initMeta();
        USERS.clear();
        LOG.clear();
        createDefaultUser();
    }

    /** ACLLoadUsersAtStartup() */
    public static void loadUsersAtStartup(Server s) {
        init(s);
        String aclfile = s.config.getOr("aclfile", "");
        List<List<String>> lines = s.config.userLines;
        if (aclfile != null && !aclfile.isEmpty() && !lines.isEmpty()) {
            System.err.println("Configuring Redis with users defined in redis.conf and at the same setting "
                    + "an ACL file path is invalid.");
            System.exit(1);
        }
        Set<String> seen = new HashSet<>();
        for (List<String> ln : lines) {
            if (ln.isEmpty()) continue;
            String username = ln.get(0);
            if (!seen.add(username)) {
                System.err.println("\n*** FATAL CONFIG FILE ERROR (Redis " + Server.VERSION + ") ***");
                System.err.println("Reading the configuration file, at line 0");
                System.err.println(">>> 'user " + String.join(" ", ln) + "'");
                System.err.println("Duplicate user found. A user can only be defined once in config files");
                System.exit(1);
            }
            if (hasSpaces(username)) {
                System.err.println("Spaces not allowed in ACL usernames");
                System.exit(1);
            }
            User u = USERS.get(username);
            if (u == null) { u = new User(username); USERS.put(username, u); }
            else setUser(u, "reset");
            if (u.name.equals("default")) defaultUser = u;
            int[] bad = new int[1];
            List<String> merged = mergeSelectorArguments(ln.subList(1, ln.size()), bad);
            if (merged == null) {
                System.err.println("Unmatched parenthesis in selector definition.");
                System.exit(1);
            }
            for (String op : merged) {
                try {
                    setUser(u, op);
                } catch (AclError e) {
                    System.err.println("Error loading ACL rule '" + op + "' for the user named '"
                            + username + "': " + e.msg);
                    System.exit(1);
                }
            }
        }
        if (aclfile != null && !aclfile.isEmpty()) {
            String errors = loadFromFile(s, aclfile);
            if (errors != null) {
                System.err.println("Aborting Redis startup because of ACL errors: " + errors);
                System.exit(1);
            }
        }
        /* requirepass, when set, becomes the default user's password. */
        String rp = s.config.getOr("requirepass", "");
        if (rp != null && !rp.isEmpty() && lines.isEmpty() && (aclfile == null || aclfile.isEmpty()))
            updateDefaultUserPassword(rp);
    }

    /** ACLLoadFromFile() */
    public static String loadFromFile(Server s, String filename) {
        initMeta();
        List<String> lines;
        try {
            lines = java.nio.file.Files.readAllLines(s.resolve(filename),
                    java.nio.charset.StandardCharsets.ISO_8859_1);
        } catch (Exception e) {
            return "Error loading ACLs, opening file '" + filename + "': No such file or directory";
        }
        StringBuilder errors = new StringBuilder();
        TreeMap<String, User> newUsers = new TreeMap<>();
        for (int i = 0; i < lines.size(); i++) {
            int linenum = i + 1;
            String line = lines.get(i).trim();
            if (line.isEmpty()) continue;
            List<String> argv;
            try { argv = Config.splitArgs(line); }
            catch (RuntimeException e) {
                errors.append(filename).append(':').append(linenum).append(": unbalanced quotes in acl line. ");
                continue;
            }
            if (argv.isEmpty()) continue;
            if (!argv.get(0).equals("user") || argv.size() < 2) {
                errors.append(filename).append(':').append(linenum)
                      .append(" should start with user keyword followed by the username. ");
                continue;
            }
            if (hasSpaces(argv.get(1))) {
                errors.append('\'').append(filename).append(':').append(linenum)
                      .append(": username '").append(argv.get(1)).append("' contains invalid characters. ");
                continue;
            }
            String username = argv.get(1);
            if (newUsers.containsKey(username)) {
                errors.append("WARNING: Duplicate user '").append(username)
                      .append("' found on line ").append(linenum).append(". ");
                continue;
            }
            User u = new User(username);
            newUsers.put(username, u);
            int[] bad = new int[1];
            List<String> merged = mergeSelectorArguments(argv.subList(2, argv.size()), bad);
            if (merged == null) {
                errors.append(filename).append(':').append(linenum)
                      .append(": Unmatched parenthesis in selector definition.");
                merged = new ArrayList<>();
            }
            boolean syntaxError = false;
            for (String op : merged) {
                try {
                    setUser(u, op.trim());
                } catch (AclError e) {
                    if (e.msg.equals(E_NOENT)) {
                        errors.append(filename).append(':').append(linenum)
                              .append(": Error in applying operation '").append(op).append("': ")
                              .append(e.msg).append(". ");
                    } else if (!syntaxError) {
                        errors.append(filename).append(':').append(linenum).append(": ")
                              .append(e.msg).append(". ");
                        syntaxError = true;
                    }
                }
            }
        }
        if (errors.length() != 0) {
            return errors + "WARNING: ACL errors detected, no change to the previously active ACL rules was performed";
        }
        User newDefault = newUsers.get("default");
        if (newDefault == null) {
            newDefault = new User("default");
            setUser(newDefault, "+@all");
            setUser(newDefault, "~*");
            setUser(newDefault, "&*");
            setUser(newDefault, "on");
            setUser(newDefault, "nopass");
        }
        if (defaultUser == null) createDefaultUser();
        defaultUser.copyFrom(newDefault);
        newUsers.put("default", defaultUser);

        /* Rebind or disconnect the clients authenticated with the old users. */
        if (s != null) {
            Map<String, List<String>> userChannels = new HashMap<>();
            for (Client c : new ArrayList<>(s.clients.values())) {
                if (s.repl != null && s.repl.isMasterClient(c)) continue;
                User original = USERS.get(c.user);
                User nu = newUsers.get(c.user);
                List<String> channels = null;
                boolean computed = false;
                if (nu != null && original != null) {
                    if (userChannels.containsKey(nu.name)) { channels = userChannels.get(nu.name); computed = true; }
                    else {
                        channels = upcomingChannelList(nu, original);
                        userChannels.put(nu.name, channels);
                        computed = true;
                    }
                }
                if (nu == null || (computed && channels != null && shouldKillPubsubClient(c, channels))) {
                    deauthAndClose(s, c);
                }
            }
        }
        USERS.clear();
        USERS.putAll(newUsers);
        return null;
    }

    /** ACLSaveToFile() */
    public static boolean saveToFile(Server s, String filename) {
        StringBuilder sb = new StringBuilder();
        for (User u : USERS.values())
            sb.append("user ").append(u.name).append(' ').append(describeUser(u)).append('\n');
        try {
            java.nio.file.Path p = s.resolve(filename);
            java.nio.file.Files.write(p, Util.b(sb.toString()));
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}
