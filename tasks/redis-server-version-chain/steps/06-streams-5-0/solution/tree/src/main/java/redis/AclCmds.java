package redis;

import java.util.*;

/** The ACL container command (src/acl.c aclCommand()). */
public final class AclCmds {
    private AclCmds() {}

    public static void register() {
        CommandTable.reg("acl", -2, "", 0, 0, 0, c -> CommandTable.dispatchSub(c, "acl"));
        CommandTable.regSub("acl", "cat", -2, "noscript loading stale", AclCmds::cat);
        CommandTable.regSub("acl", "deluser", -3, "admin noscript loading stale", AclCmds::deluser);
        CommandTable.regSub("acl", "dryrun", -4, "admin noscript loading stale", AclCmds::dryrun);
        CommandTable.regSub("acl", "genpass", -2, "noscript loading stale", AclCmds::genpass);
        CommandTable.regSub("acl", "getuser", 3, "admin noscript loading stale", AclCmds::getuser);
        CommandTable.regSub("acl", "help", 2, "loading stale", AclCmds::help);
        CommandTable.regSub("acl", "list", 2, "admin noscript loading stale", AclCmds::list);
        CommandTable.regSub("acl", "load", 2, "admin noscript loading stale", AclCmds::load);
        CommandTable.regSub("acl", "log", -2, "admin noscript loading stale", AclCmds::log);
        CommandTable.regSub("acl", "save", 2, "admin noscript loading stale", AclCmds::save);
        CommandTable.regSub("acl", "setuser", -3, "admin noscript loading stale", AclCmds::setuser);
        CommandTable.regSub("acl", "users", 2, "admin noscript loading stale", AclCmds::users);
        CommandTable.regSub("acl", "whoami", 2, "noscript loading stale", AclCmds::whoami);
    }

    /** networking.c addReplySubcommandSyntaxError() */
    static RedisError syntaxError(Client c) {
        return new RedisError("ERR unknown subcommand or wrong number of arguments for '"
                + Server.trunc(c.argStr(1), 128) + "'. Try ACL HELP.");
    }

    static void whoami(Client c) {
        c.addBulk(c.user == null || c.user.isEmpty() ? "default" : c.user);
    }

    static void list(Client c) {
        AclState.initMeta();
        c.addArray(AclState.USERS.size());
        for (AclState.User u : AclState.USERS.values())
            c.addBulk(Util.b("user " + u.name + " " + AclState.describeUser(u)));
    }

    static void users(Client c) {
        AclState.initMeta();
        c.addArray(AclState.USERS.size());
        for (String n : AclState.USERS.keySet()) c.addBulk(n);
    }

    static void cat(Client c) {
        AclState.initMeta();
        if (c.argc == 2) {
            c.addArray(AclTables.CATS.length);
            for (String cat : AclTables.CATS) c.addBulk(cat);
            return;
        }
        if (c.argc != 3) throw syntaxError(c);
        long flag = AclState.catFlag(c.argStr(2));
        if (flag == 0)
            throw new RedisError("ERR Unknown category '" + Server.trunc(c.argStr(2), 128) + "'");
        List<String> names = new ArrayList<>();
        for (AclState.CmdMeta cm : AclState.META.values()) {
            if ((cm.cats & flag) == 0) continue;
            if (!implemented(cm.fullname)) continue;
            names.add(cm.fullname);
        }
        c.addArray(names.size());
        for (String n : names) c.addBulk(n);
    }

    /** Only advertise commands this server actually implements. */
    static boolean implemented(String fullname) {
        int bar = fullname.indexOf('|');
        if (bar < 0) return CommandTable.TABLE.containsKey(fullname);
        Map<String, Cmd> m = CommandTable.SUBS.get(fullname.substring(0, bar));
        return m != null && m.containsKey(fullname.substring(bar + 1));
    }

    static void getuser(Client c) {
        AclState.initMeta();
        AclState.User u = AclState.USERS.get(c.argStr(2));
        if (u == null) { c.addNil(); return; }
        c.addMap(6);
        c.addBulk("flags");
        List<String> flags = new ArrayList<>();
        if ((u.flags & AclState.F_ENABLED) != 0) flags.add("on");
        if ((u.flags & AclState.F_DISABLED) != 0) flags.add("off");
        if ((u.flags & AclState.F_NOPASS) != 0) flags.add("nopass");
        if ((u.flags & AclState.F_SANITIZE_SKIP) != 0) flags.add("skip-sanitize-payload");
        if ((u.flags & AclState.F_SANITIZE) != 0) flags.add("sanitize-payload");
        c.addSet(flags.size());
        for (String f : flags) c.addBulk(f);
        c.addBulk("passwords");
        c.addArray(u.passwords.size());
        for (String p : u.passwords) c.addBulk(p);
        AclState.Selector root = u.root();
        c.addBulk("commands");
        c.addBulk(AclState.describeCommandRules(root));
        c.addBulk("keys");
        c.addBulk(AclState.describeKeys(root));
        c.addBulk("channels");
        c.addBulk(AclState.describeChannels(root));
        c.addBulk("selectors");
        c.addArray(u.selectors.size() - 1);
        for (int i = 1; i < u.selectors.size(); i++) {
            AclState.Selector sel = u.selectors.get(i);
            c.addMap(3);
            c.addBulk("commands");
            c.addBulk(AclState.describeCommandRules(sel));
            c.addBulk("keys");
            c.addBulk(AclState.describeKeys(sel));
            c.addBulk("channels");
            c.addBulk(AclState.describeChannels(sel));
        }
    }

    static void setuser(Client c) {
        Server s = Server.I;
        AclState.initMeta();
        String username = c.argStr(2);
        if (AclState.hasSpaces(username))
            throw new RedisError("ERR Usernames can't contain spaces or null characters");
        AclState.User u = AclState.USERS.get(username);
        List<String> args = new ArrayList<>();
        for (int i = 3; i < c.argc; i++) args.add(c.argStr(i));
        String err = AclState.stringSetUser(s, u, username, args);
        if (err == null) c.addOk();
        else c.addError("ERR " + err.replace('\r', ' ').replace('\n', ' '));
    }

    static void deluser(Client c) {
        Server s = Server.I;
        AclState.initMeta();
        for (int j = 2; j < c.argc; j++)
            if (c.argStr(j).equals("default"))
                throw new RedisError("ERR The 'default' user cannot be removed");
        int deleted = 0;
        for (int j = 2; j < c.argc; j++) {
            AclState.User u = AclState.USERS.remove(c.argStr(j));
            if (u != null) { AclState.freeUserAndKillClients(s, u); deleted++; }
        }
        c.addInt(deleted);
    }

    static void genpass(Client c) {
        long bits = 256;
        if (c.argc == 3) bits = c.argLong(2);
        else if (c.argc != 2) throw syntaxError(c);
        if (bits <= 0 || bits > 4096)
            throw new RedisError("ERR ACL GENPASS argument must be the number of "
                    + "bits for the output password, a positive number up to 4096");
        int chars = (int) ((bits + 3) / 4);
        c.addBulk(Server.randomHex(chars));
    }

    static void load(Client c) {
        Server s = Server.I;
        String f = s.config.getOr("aclfile", "");
        if (f == null || f.isEmpty()) throw noAclFile();
        String errors = AclState.loadFromFile(s, f);
        if (errors == null) c.addOk();
        else c.addError("ERR " + errors.replace('\r', ' ').replace('\n', ' '));
    }

    static void save(Client c) {
        Server s = Server.I;
        String f = s.config.getOr("aclfile", "");
        if (f == null || f.isEmpty()) throw noAclFile();
        if (AclState.saveToFile(s, f)) c.addOk();
        else c.addError("ERR There was an error trying to save the ACLs. "
                + "Please check the server logs for more information");
    }

    static RedisError noAclFile() {
        return new RedisError("ERR This Redis instance is not configured to use an ACL file. "
                + "You may want to specify users via the ACL SETUSER command and then issue a "
                + "CONFIG REWRITE (assuming you have a Redis configuration file set) in order "
                + "to store users in the Redis configuration.");
    }

    static void log(Client c) {
        Server s = Server.I;
        long count = 10;
        if (c.argc == 3) {
            if (c.argIs(2, "reset")) { AclState.LOG.clear(); c.addOk(); return; }
            count = c.argLong(2);
            if (count < 0) count = 0;
        } else if (c.argc != 2) {
            throw syntaxError(c);
        }
        if (count > AclState.LOG.size()) count = AclState.LOG.size();
        c.addArray((int) count);
        long now = System.currentTimeMillis();
        int emitted = 0;
        for (AclState.LogEntry le : AclState.LOG) {
            if (emitted++ >= count) break;
            c.addMap(10);
            c.addBulk("count"); c.addInt(le.count);
            c.addBulk("reason"); c.addBulk(reasonName(le.reason));
            c.addBulk("context"); c.addBulk(le.context);
            c.addBulk("object"); c.addBulk(Util.b(le.object));
            c.addBulk("username"); c.addBulk(Util.b(le.username));
            c.addBulk("age-seconds"); c.addDouble((now - le.ctime) / 1000.0);
            c.addBulk("client-info"); c.addBulk(Util.b(le.cinfo));
            c.addBulk("entry-id"); c.addInt(le.entryId);
            c.addBulk("timestamp-created"); c.addInt(le.timestampCreated);
            c.addBulk("timestamp-last-updated"); c.addInt(le.ctime);
        }
    }

    static String reasonName(int r) {
        switch (r) {
            case AclState.DENIED_CMD: return "command";
            case AclState.DENIED_KEY: return "key";
            case AclState.DENIED_CHANNEL: return "channel";
            case AclState.DENIED_AUTH: return "auth";
            default: return "unknown";
        }
    }

    static void dryrun(Client c) {
        AclState.initMeta();
        AclState.User u = AclState.USERS.get(c.argStr(2));
        if (u == null) throw new RedisError("ERR User '" + c.argStr(2) + "' not found");
        String name = c.argLower(3);
        Cmd cmd = CommandTable.lookup(name);
        if (cmd == null) throw new RedisError("ERR Command '" + c.argStr(3) + "' not found");
        int argc = c.argc - 3;
        if ((cmd.arity > 0 && cmd.arity != argc) || (argc < -cmd.arity))
            throw new RedisError("ERR wrong number of arguments for '" + cmd.name + "' command");
        byte[][] argv = new byte[argc][];
        System.arraycopy(c.argv, 3, argv, 0, argc);
        AclState.CmdMeta cm = AclState.resolve(cmd, argv, argc);
        if (cm == null) throw new RedisError("ERR Command '" + c.argStr(3) + "' not found");
        boolean parent = cm.fullname.indexOf('|') >= 0;
        int[] idx = new int[1];
        int r = AclState.checkAllUserCommandPerm(u, cm, parent, argv, argc, idx, cmd.arity);
        if (r != AclState.ACL_OK) {
            byte[] obj = idx[0] < argc ? argv[idx[0]] : Util.EMPTY;
            c.addBulk(Util.b(AclState.aclErrorMessage(r, u, cm.fullname, obj, true)));
            return;
        }
        c.addOk();
    }

    static void help(Client c) {
        String[] h = {
            "CAT [<category>]",
            "    List all commands that belong to <category>, or all command categories",
            "    when no category is specified.",
            "DELUSER <username> [<username> ...]",
            "    Delete a list of users.",
            "DRYRUN <username> <command> [<arg> ...]",
            "    Returns whether the user can execute the given command without executing the command.",
            "GETUSER <username>",
            "    Get the user's details.",
            "GENPASS [<bits>]",
            "    Generate a secure 256-bit user password. The optional `bits` argument can",
            "    be used to specify a different size.",
            "LIST",
            "    Show users details in config file format.",
            "LOAD",
            "    Reload users from the ACL file.",
            "LOG [<count> | RESET]",
            "    Show the ACL log entries.",
            "SAVE",
            "    Save the current config to the ACL file.",
            "SETUSER <username> <attribute> [<attribute> ...]",
            "    Create or modify a user with the specified attributes.",
            "USERS",
            "    List all the registered usernames.",
            "WHOAMI",
            "    Return the current connection username.",
            "HELP",
            "    Print this help.",
        };
        c.addArray(h.length + 1);
        c.addSimple("ACL <subcommand> [<arg> [value] [opt] ...]. Subcommands are:");
        for (String l : h) c.addSimple(l);
    }
}
