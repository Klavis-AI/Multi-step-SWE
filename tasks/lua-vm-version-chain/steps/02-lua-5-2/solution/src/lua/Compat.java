package lua;

/**
 * The LUA_COMPAT_* settings of luaconf.h, resolved once from the build's macro
 * definitions. The release's own src/Makefile compiles with -DLUA_COMPAT_ALL,
 * so everything here is on by default and MYCFLAGS="-ULUA_COMPAT_ALL" turns the
 * whole group off, exactly as it does for the reference build.
 */
final class Compat {
    private Compat() { }

    private static final boolean ALL = Conf.on("LUA_COMPAT_ALL");

    static final boolean UNPACK     = ALL || Conf.on("LUA_COMPAT_UNPACK");
    static final boolean LOADERS    = ALL || Conf.on("LUA_COMPAT_LOADERS");
    static final boolean LOG10      = ALL || Conf.on("LUA_COMPAT_LOG10");
    static final boolean LOADSTRING = ALL || Conf.on("LUA_COMPAT_LOADSTRING");
    static final boolean MAXN       = ALL || Conf.on("LUA_COMPAT_MAXN");
    static final boolean MODULE     = ALL || Conf.on("LUA_COMPAT_MODULE");
}
