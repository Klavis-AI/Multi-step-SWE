package lua;

/**
 * The LUA_COMPAT_* settings of luaconf.h, resolved once from the build's macro
 * definitions. 5.5 has two: LUA_COMPAT_GLOBAL, which luaconf.h defines as 1 so
 * that `global` stays an ordinary name unless the build says
 * -DLUA_COMPAT_GLOBAL=0, and LUA_COMPAT_MATHLIB, which is off.
 */
final class Compat {
    private Compat() { }

    /** `global` is a reserved word only when this is off. */
    static final boolean GLOBAL  = Conf.onByDefault("LUA_COMPAT_GLOBAL");
    static final boolean MATHLIB = Conf.on("LUA_COMPAT_MATHLIB");
}
