package lua;

/**
 * The LUA_COMPAT_* settings of luaconf.h, resolved once from the build's macro
 * definitions. The release's own src/Makefile compiles with -DLUA_COMPAT_5_2,
 * which turns on the 5.2 group; -DLUA_COMPAT_5_1 adds the 5.1 group on top, and
 * MYCFLAGS="-ULUA_COMPAT_5_2" leaves a build with no compatibility at all.
 */
final class Compat {
    private Compat() { }

    private static final boolean C52 = Conf.on("LUA_COMPAT_5_2");
    private static final boolean C51 = Conf.on("LUA_COMPAT_5_1");

    static final boolean MATHLIB    = C52 || C51 || Conf.on("LUA_COMPAT_MATHLIB");
    static final boolean BITLIB     = C52 || Conf.on("LUA_COMPAT_BITLIB");
    static final boolean IPAIRS     = C52 || Conf.on("LUA_COMPAT_IPAIRS");
    static final boolean UNPACK     = C51 || Conf.on("LUA_COMPAT_UNPACK");
    static final boolean LOADERS    = C51 || Conf.on("LUA_COMPAT_LOADERS");
    static final boolean LOADSTRING = C51 || Conf.on("LUA_COMPAT_LOADSTRING");
    static final boolean MAXN       = C51 || Conf.on("LUA_COMPAT_MAXN");
    static final boolean MODULE     = C51 || Conf.on("LUA_COMPAT_MODULE");
    /** Integral floats print without the ".0" that marks them as floats. */
    static final boolean FLOATSTRING = Conf.on("LUA_COMPAT_FLOATSTRING");
}
