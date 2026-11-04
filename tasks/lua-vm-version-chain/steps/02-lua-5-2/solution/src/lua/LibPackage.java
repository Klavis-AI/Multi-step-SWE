package lua;

import java.nio.file.Files;
import java.nio.file.Paths;

/** package/require of Lua 5.1, restricted to Lua source modules. */
final class LibPackage {

    /** Builds the package.loaded-qualified names luaL_argerror falls back on. */
    static void indexNames(LuaTable loaded) {
        Object[] kv = loaded.next(null);
        while (kv != null) {
            String mod = String.valueOf(kv[0]);
            if (kv[1] instanceof LuaTable) {
                LuaTable m = (LuaTable) kv[1];
                Object[] e = m.next(null);
                while (e != null) {
                    if (e[1] instanceof Builtin && !Interp.globalNames.containsKey(e[1])) {
                        // pushglobalfuncname drops the "_G." prefix, so a base
                        // function is named plainly wherever it is found.
                        String q = mod.equals("_G") ? String.valueOf(e[0]) : mod + "." + e[0];
                        Interp.globalNames.put(e[1], q);
                    }
                    e = m.next(e[0]);
                }
            }
            kv = loaded.next(kv[0]);
        }
    }

    static void open(LuaTable g) {
        LuaTable pkg = new LuaTable();
        g.rawset("package", pkg);
        LuaTable loaded = new LuaTable();
        pkg.rawset("loaded", loaded);
        pkg.rawset("preload", new LuaTable());
        String path = System.getenv("LUA_PATH");
        if (path == null) path = "./?.lua;./?/init.lua";
        pkg.rawset("path", LuaString.of(path));
        String cpath = System.getenv("LUA_CPATH");
        if (cpath == null) cpath = "./?.so";
        pkg.rawset("cpath", LuaString.of(cpath));
        pkg.rawset("config", LuaString.of("/\n;\n?\n!\n-\n"));
        LuaTable loaders = new LuaTable();
        pkg.rawset("searchers", loaders);
        if (Compat.LOADERS) pkg.rawset("loaders", loaders);
        Interp.registry.rawset("_LOADED", loaded);

        for (String n : new String[]{"string", "table", "math", "io", "os", "debug", "coroutine", "bit32"}) {
            Object v = g.rawget(n);
            if (v != null) loaded.rawset(n, v);
        }
        loaded.rawset("_G", g);
        loaded.rawset("package", pkg);

        indexNames(loaded);

        Lib.reg(g, "require", a -> {
            LuaString name = Lib.checkString(a, 0);
            Object v = loaded.rawget(name);
            if (v != null) return Lib.ret(v);
            LuaTable preload = (LuaTable) pkg.rawget("preload");
            Object pre = preload.rawget(name);
            Object res;
            if (pre != null) {
                res = Interp.first(Interp.call(pre, new Object[]{name}));
            } else {
                String p = pkg.rawget("path").toString();
                String fname = name.toString().replace('.', '/');
                java.nio.file.Path found = null;
                StringBuilder tried = new StringBuilder();
                tried.append("\n\tno field package.preload['").append(name).append("']");
                for (String tpl : p.split(";")) {
                    if (tpl.isEmpty()) continue;
                    String cand = tpl.replace("?", fname);
                    tried.append("\n\tno file '").append(cand).append("'");
                    if (Files.isReadable(Paths.get(cand))) { found = Paths.get(cand); break; }
                }
                if (found == null) {
                    // the C searcher can never succeed here, but its attempts are
                    // part of the message the manual describes
                    for (String tpl : pkg.rawget("cpath").toString().split(";")) {
                        if (tpl.isEmpty()) continue;
                        tried.append("\n\tno file '").append(tpl.replace("?", fname)).append("'");
                    }
                    throw Interp.libError("module '" + name + "' not found:" + tried);
                }
                try {
                    Object[] lr = LibBase.doLoad(Files.readAllBytes(found), "@" + found);
                    if (lr[0] == null) throw new LuaError(lr[1]);
                    res = Interp.first(Interp.call(lr[0], new Object[]{name}));
                } catch (java.io.IOException e) {
                    throw Interp.libError("cannot read " + found);
                }
            }
            if (res != null) loaded.rawset(name, res);
            else if (loaded.rawget(name) == null) loaded.rawset(name, Boolean.TRUE);
            return Lib.ret(loaded.rawget(name));
        });

        if (Compat.MODULE) Lib.reg(g, "module", a -> {
            LuaString name = Lib.checkString(a, 0);
            Object m = loaded.rawget(name);
            if (!(m instanceof LuaTable)) {
                m = new LuaTable();
                loaded.rawset(name, m);
                Interp.globals.rawset(name, m);
            }
            LuaTable mt = (LuaTable) m;
            if (mt.rawget("_NAME") == null) {
                mt.rawset("_NAME", name);
                mt.rawset("_M", mt);
                mt.rawset("_PACKAGE", LuaString.of(""));
            }
            Frame f = Interp.frameAt(1);
            if (f != null && !f.isC() && f.cl.upvals.length > 0) f.cl.upvals[0].v = mt;
            for (int i = 1; i < a.length; i++) Interp.call(a[i], new Object[]{mt});
            return Lib.NONE;
        });

        if (Compat.MODULE) Lib.reg(pkg, "seeall", a -> {
            LuaTable m = Lib.checkTable(a, 0);
            LuaTable mt = m.metatable;
            if (mt == null) { mt = new LuaTable(); m.metatable = mt; }
            mt.rawset("__index", Interp.globals);
            return Lib.NONE;
        });

        // package.searchpath: the first name in the path that names a readable
        // file, or nil and the list of names that were tried.
        Lib.reg(pkg, "searchpath", a -> {
            String name = Lib.checkString(a, 0).toString();
            String p = Lib.checkString(a, 1).toString();
            String sep = Lib.optString(a, 2, ".").toString();
            String rep = Lib.optString(a, 3, "/").toString();
            if (!sep.isEmpty()) name = name.replace(sep, rep);
            StringBuilder tried = new StringBuilder();
            for (String tmpl : p.split(";", -1)) {
                if (tmpl.isEmpty()) continue;
                String fn = tmpl.replace("?", name);
                java.io.File f = new java.io.File(fn);
                if (f.canRead()) return Lib.ret(LuaString.of(fn));
                tried.append("\n\tno file '").append(fn).append("'");
            }
            return Lib.ret(null, LuaString.of(tried.toString()));
        });

        Lib.reg(pkg, "loadlib", a -> Lib.ret(null, LuaString.of("dynamic libraries not enabled")));
    }
}
