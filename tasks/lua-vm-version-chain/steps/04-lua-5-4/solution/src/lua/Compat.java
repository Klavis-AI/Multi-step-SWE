package lua;

/**
 * The LUA_COMPAT_* settings of luaconf.h, resolved once from the build's macro
 * definitions. The release's own src/Makefile compiles with -DLUA_COMPAT_5_3;
 * MYCFLAGS="-ULUA_COMPAT_5_3" leaves a build with no compatibility at all.
 */
final class Compat {
    private Compat() { }

    private static final boolean C53 = Conf.on("LUA_COMPAT_5_3");

    static final boolean MATHLIB = C53 || Conf.on("LUA_COMPAT_MATHLIB");
    /** '__le' is emulated with 'not (b < a)' when a type has no '__le'. */
    static final boolean LT_LE   = C53 || Conf.on("LUA_COMPAT_LT_LE");
}
