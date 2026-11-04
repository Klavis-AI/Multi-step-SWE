package redis;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/**
 * redis.conf: the parameter table, the file/command-line parser and CONFIG GET/SET.
 * The parameter names, aliases, types and defaults are those of redis's own
 * src/config.c standardConfig table.
 */
public final class Config {
    public static final class Param {
        public final String name, alias, kind;
        public final String def;
        public String value;
        Param(String n, String a, String k, String d) { name = n; alias = a; kind = k; def = d; value = d; }
    }

    public final LinkedHashMap<String, Param> params = new LinkedHashMap<>();
    private final Map<String, String> aliasToName = new HashMap<>();
    /** parameters that may legally repeat in the file, accumulated in order */
    public final List<String> saveParams = new ArrayList<>();
    /** "sentinel <option> <args...>" directives, in file order (sentinel mode) */
    public final List<List<String>> sentinelDirectives = new ArrayList<>();
    /** "user <name> <rules...>" directives, collected for the ACL subsystem */
    public final List<List<String>> userLines = new ArrayList<>();
    public String configFile = null;

    public Config() {
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                Config.class.getResourceAsStream("/config_defaults.tsv"), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] f = line.split("\t", -1);
                String name = f[0], alias = f[1], kind = f[2], def = f.length > 3 ? f[3] : "";
                Param p = new Param(name, alias, kind, def);
                params.put(name, p);
                if (!alias.isEmpty()) aliasToName.put(alias, name);
            }
        } catch (Exception e) {
            throw new RuntimeException("cannot load config table", e);
        }
    }

    public Param find(String name) {
        String n = name.toLowerCase();
        Param p = params.get(n);
        if (p == null) {
            String real = aliasToName.get(n);
            if (real != null) p = params.get(real);
        }
        return p;
    }

    public boolean has(String name) { return find(name) != null; }

    public String get(String name) {
        Param p = find(name);
        return p == null ? null : p.value;
    }

    public String getOr(String name, String dflt) {
        String v = get(name);
        return v == null ? dflt : v;
    }

    public long getLong(String name) {
        String v = get(name);
        if (v == null || v.isEmpty()) return 0;
        return parseMemory(v);
    }

    public int getInt(String name) { return (int) getLong(name); }

    public boolean getBool(String name) {
        String v = get(name);
        return "yes".equalsIgnoreCase(v) || "1".equals(v) || "true".equalsIgnoreCase(v);
    }

    public double getDouble(String name) {
        String v = get(name);
        try { return v == null || v.isEmpty() ? 0 : Double.parseDouble(v); } catch (Exception e) { return 0; }
    }

    public void set(String name, String value) {
        Param p = find(name);
        if (p == null) {
            Param np = new Param(name.toLowerCase(), "", "string", "");
            np.value = value;
            params.put(np.name, np);
            return;
        }
        p.value = value;
    }

    /** "1gb", "100mb", "512kb", "1k" ... plus plain integers. */
    public static long parseMemory(String s) {
        s = s.trim();
        if (s.isEmpty()) return 0;
        String low = s.toLowerCase();
        long mul = 1;
        String num = low;
        String[][] units = {{"kb", "1024"}, {"mb", "1048576"}, {"gb", "1073741824"},
                            {"k", "1000"}, {"m", "1000000"}, {"g", "1000000000"}, {"b", "1"}};
        for (String[] u : units) {
            if (low.endsWith(u[0])) {
                mul = Long.parseLong(u[1]);
                num = low.substring(0, low.length() - u[0].length()).trim();
                break;
            }
        }
        try { return (long) (Double.parseDouble(num) * mul); }
        catch (NumberFormatException e) { return 0; }
    }

    /** Split a config line the way redis's sdssplitargs does. */
    public static List<String> splitArgs(String line) {
        List<String> out = new ArrayList<>();
        int i = 0, n = line.length();
        while (true) {
            while (i < n && Character.isWhitespace(line.charAt(i))) i++;
            if (i >= n) break;
            StringBuilder cur = new StringBuilder();
            boolean inq = false, insq = false, done = false;
            while (!done) {
                if (inq) {
                    if (i >= n) throw new IllegalArgumentException("unbalanced quotes");
                    char c = line.charAt(i);
                    if (c == '\\' && i + 3 < n && line.charAt(i + 1) == 'x'
                            && isHex(line.charAt(i + 2)) && isHex(line.charAt(i + 3))) {
                        cur.append((char) Integer.parseInt(line.substring(i + 2, i + 4), 16));
                        i += 4;
                        continue;
                    } else if (c == '\\' && i + 1 < n) {
                        char e = line.charAt(i + 1);
                        char v;
                        switch (e) {
                            case 'n': v = '\n'; break;
                            case 'r': v = '\r'; break;
                            case 't': v = '\t'; break;
                            case 'b': v = '\b'; break;
                            case 'a': v = 7; break;
                            default: v = e;
                        }
                        cur.append(v);
                        i += 2;
                        continue;
                    } else if (c == '"') {
                        i++;
                        if (i < n && !Character.isWhitespace(line.charAt(i)))
                            throw new IllegalArgumentException("closing quote must be followed by space");
                        done = true;
                        continue;
                    }
                    cur.append(c); i++;
                } else if (insq) {
                    if (i >= n) throw new IllegalArgumentException("unbalanced quotes");
                    char c = line.charAt(i);
                    if (c == '\\' && i + 1 < n && line.charAt(i + 1) == '\'') { cur.append('\''); i += 2; continue; }
                    if (c == '\'') {
                        i++;
                        if (i < n && !Character.isWhitespace(line.charAt(i)))
                            throw new IllegalArgumentException("closing quote must be followed by space");
                        done = true;
                        continue;
                    }
                    cur.append(c); i++;
                } else {
                    if (i >= n) { done = true; continue; }
                    char c = line.charAt(i);
                    if (Character.isWhitespace(c) || c == 0) { done = true; continue; }
                    if (c == '"') { inq = true; i++; continue; }
                    if (c == '\'') { insq = true; i++; continue; }
                    cur.append(c); i++;
                }
            }
            out.add(cur.toString());
        }
        return out;
    }

    private static boolean isHex(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    /** Load a config file; every line is "<param> <args...>". */
    public void loadFile(String path) throws IOException {
        configFile = new File(path).getAbsolutePath();
        List<String> lines = Files.readAllLines(Paths.get(path), StandardCharsets.ISO_8859_1);
        for (String raw : lines) {
            String line = raw.trim();
            if (line.isEmpty() || line.charAt(0) == '#') continue;
            List<String> a = splitArgs(line);
            if (a.isEmpty()) continue;
            applyDirective(a.get(0), a.subList(1, a.size()));
        }
    }

    /** Apply "--name v1 v2" style overrides from argv. */
    public void loadArgs(List<String> args) {
        String cur = null;
        List<String> vals = new ArrayList<>();
        for (String a : args) {
            if (a.startsWith("--") && a.length() > 2 && !isNumericArg(a)) {
                if (cur != null) applyDirective(cur, vals);
                cur = a.substring(2);
                vals = new ArrayList<>();
            } else if (cur != null) {
                vals.add(a);
            }
        }
        if (cur != null) applyDirective(cur, vals);
    }

    private static boolean isNumericArg(String a) {
        // "--1" is not a directive; redis treats a leading '-' digit as a value
        return a.length() > 2 && Character.isDigit(a.charAt(2));
    }

    public void applyDirective(String name, List<String> vals) {
        String n = name.toLowerCase();
        String joined = String.join(" ", vals);
        if (n.equals("save")) {
            if (vals.isEmpty() || (vals.size() == 1 && vals.get(0).isEmpty())) saveParams.clear();
            else for (int i = 0; i + 1 < vals.size(); i += 2) saveParams.add(vals.get(i) + " " + vals.get(i + 1));
            set("save", String.join(" ", saveParams));
            return;
        }
        if (n.equals("sentinel")) { sentinelDirectives.add(new ArrayList<>(vals)); return; }
        if (n.equals("include")) return;
        if (n.equals("loadmodule")) return;
        if (n.equals("user")) { userLines.add(new ArrayList<>(vals)); return; }
        if (n.equals("slaveof") || n.equals("replicaof")) { set("replicaof", joined); return; }
        set(n, joined);
    }
}
