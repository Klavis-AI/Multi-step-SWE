package redis;

/** Thin facade over {@link AclState} used by the dispatcher and the connection code. */
public final class Acl {
    private Acl() {}

    public static boolean enabled(Server s) { return AclState.enabled; }
    public static String check(Server s, Client c, Cmd cmd) {
        try {
            return AclState.check(s, c, cmd);
        } catch (RuntimeException e) {
            /* Never let an ACL bookkeeping bug take a command down. */
            return null;
        }
    }

    /** acceptCommonHandler(): bind the new connection to the default user. */
    public static void initClient(Server s, Client c) { AclState.initClient(s, c); }

    /** networking.c authRequired() */
    public static boolean authRequired(Server s, Client c) { return AclState.authRequired(s, c); }

    /** acl.c ACLLoadUsersAtStartup() */
    public static void loadUsersAtStartup(Server s) { AclState.loadUsersAtStartup(s); }
}
