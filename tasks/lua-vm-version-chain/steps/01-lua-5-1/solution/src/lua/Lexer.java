package lua;

import java.io.ByteArrayOutputStream;
import java.util.HashMap;
import java.util.Map;

/** The Lua 5.1 lexical analyser, working directly on the chunk's bytes. */
final class Lexer {
    // token codes
    static final int TK_EOF = 0, TK_NAME = 1, TK_NUMBER = 2, TK_STRING = 3, TK_OP = 4, TK_KEYWORD = 5;

    static final String[] KEYWORDS = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
        "true", "until", "while"
    };
    private static final Map<String, Boolean> KW = new HashMap<>();
    static { for (String k : KEYWORDS) KW.put(k, Boolean.TRUE); }

    final byte[] src;
    final String chunkname;      // MAXSRC form, used in lexical and syntax errors
    int pos;
    int line = 1;

    int type;                 // current token
    String text;              // name / keyword / operator text
    double num;
    LuaString str;
    int tline;                // line the current token starts on
    String raw;               // raw source text of the current token
    int lastline;             // line of the previously consumed token

    // one-token lookahead
    private boolean hasAhead;
    private int aType, aLine;
    private String aText, aRaw;
    private double aNum;
    private LuaString aStr;

    Lexer(byte[] src, String rawname) {
        this.src = src;
        this.chunkname = LibBase.displayName(rawname, 80);
        this.pos = 0;
        next();
    }

    LuaError error(String msg, String near) {
        String t = near == null ? tokenText() : near;
        return Interp.syntaxError(chunkname + ":" + line + ": " + msg + " near '" + t + "'");
    }

    LuaError errorAt(String msg) { return error(msg, null); }

    String tokenText() {
        switch (type) {
            case TK_EOF: return "<eof>";
            case TK_NUMBER: case TK_STRING: case TK_NAME:
                return raw != null ? raw : text;
            default: return text;
        }
    }

    void next() {
        lastline = tline;
        if (hasAhead) {
            hasAhead = false;
            type = aType; text = aText; num = aNum; str = aStr; tline = aLine; raw = aRaw;
            return;
        }
        scan();
    }

    int peek() {
        if (!hasAhead) {
            int sType = type; String sText = text; double sNum = num; LuaString sStr = str;
            int sLine = tline; String sRaw = raw;
            scan();
            aType = type; aText = text; aNum = num; aStr = str; aLine = tline; aRaw = raw;
            hasAhead = true;
            type = sType; text = sText; num = sNum; str = sStr; tline = sLine; raw = sRaw;
        }
        return aType;
    }

    String peekText() { peek(); return aText; }

    private int cur() { return pos < src.length ? (src[pos] & 0xff) : -1; }

    private int at(int k) { return pos + k < src.length ? (src[pos + k] & 0xff) : -1; }

    private void newline() {
        int c = cur();
        pos++;
        int d = cur();
        if ((d == '\n' || d == '\r') && d != c) pos++;
        line++;
    }

    private void scan() {
        for (;;) {
            int c = cur();
            if (c < 0) { type = TK_EOF; text = "<eof>"; tline = line; return; }
            if (c == '\n' || c == '\r') { newline(); continue; }
            if (c == ' ' || c == '\t' || c == '\f' || c == 0x0b) { pos++; continue; }
            if (c == '-' && at(1) == '-') {
                pos += 2;
                if (cur() == '[') {
                    int lvl = longBracketLevel();
                    if (lvl >= 0) { readLongString(lvl, true); continue; }
                }
                while (cur() >= 0 && cur() != '\n' && cur() != '\r') pos++;
                continue;
            }
            break;
        }
        tline = line;
        int tokenStart = pos;
        int c = cur();
        if (isNameStart(c)) {
            int start = pos;
            while (isNamePart(cur())) pos++;
            text = new String(src, start, pos - start, java.nio.charset.StandardCharsets.ISO_8859_1);
            raw = text;
            type = KW.containsKey(text) ? TK_KEYWORD : TK_NAME;
            return;
        }
        if (c >= '0' && c <= '9' || (c == '.' && at(1) >= '0' && at(1) <= '9')) {
            readNumber();
            raw = new String(src, tokenStart, pos - tokenStart, java.nio.charset.StandardCharsets.ISO_8859_1);
            return;
        }
        switch (c) {
            case '"': case '\'':
                readString(c);
                raw = new String(src, tokenStart, pos - tokenStart, java.nio.charset.StandardCharsets.ISO_8859_1);
                return;
            case '[': {
                int lvl = longBracketLevel();
                if (lvl >= 0) {
                    str = readLongString(lvl, false);
                    type = TK_STRING;
                    raw = new String(src, tokenStart, pos - tokenStart, java.nio.charset.StandardCharsets.ISO_8859_1);
                    return;
                }
                pos++; type = TK_OP; text = "["; return;
            }
            case '=':
                pos++;
                if (cur() == '=') { pos++; text = "=="; } else text = "=";
                type = TK_OP; return;
            case '~':
                pos++;
                if (cur() == '=') { pos++; type = TK_OP; text = "~="; return; }
                type = TK_OP; text = "~"; return;
            case '<':
                pos++;
                if (cur() == '=') { pos++; text = "<="; } else text = "<";
                type = TK_OP; return;
            case '>':
                pos++;
                if (cur() == '=') { pos++; text = ">="; } else text = ">";
                type = TK_OP; return;
            case '.':
                pos++;
                if (cur() == '.') {
                    pos++;
                    if (cur() == '.') { pos++; text = "..."; } else text = "..";
                } else text = ".";
                type = TK_OP; return;
            default:
                pos++;
                type = TK_OP;
                text = String.valueOf((char) c);
                raw = text;
                return;
        }
    }

    /** At a '[': returns the level of a long bracket, or -1 if this is not one. */
    private int longBracketLevel() {
        int p = pos + 1, lvl = 0;
        while (p < src.length && src[p] == '=') { lvl++; p++; }
        if (p < src.length && src[p] == '[') return lvl;
        return -1;
    }

    private LuaString readLongString(int lvl, boolean comment) {
        pos += 2 + lvl;                       // skip [==[
        if (cur() == '\r' || cur() == '\n') newline();
        int start = pos;
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (;;) {
            int c = cur();
            if (c < 0) {
                throw error(comment ? "unfinished long comment" : "unfinished long string", "<eof>");
            }
            if (c == ']') {
                int p = pos + 1, l = 0;
                while (p < src.length && src[p] == '=') { l++; p++; }
                if (l == lvl && p < src.length && src[p] == ']') {
                    pos = p + 1;
                    return LuaString.of(out.toByteArray());
                }
                out.write(c); pos++;
            } else if (c == '\n' || c == '\r') {
                out.write('\n');
                newline();
            } else {
                out.write(c); pos++;
            }
        }
    }

    private void readString(int quote) {
        pos++;
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (;;) {
            int c = cur();
            if (c < 0) throw error("unfinished string", "<eof>");
            // A newline ends the token, so the "near" text is the delimiter and
            // the characters decoded so far, which is what the token buffer
            // holds -- not the raw source, which may still contain escapes.
            if (c == '\n' || c == '\r') {
                throw error("unfinished string", ((char) quote)
                        + new String(out.toByteArray(), java.nio.charset.StandardCharsets.ISO_8859_1));
            }
            if (c == quote) { pos++; break; }
            if (c == '\\') {
                pos++;
                int e = cur();
                switch (e) {
                    case 'a': out.write(7); pos++; break;
                    case 'b': out.write(8); pos++; break;
                    case 'f': out.write(12); pos++; break;
                    case 'n': out.write('\n'); pos++; break;
                    case 'r': out.write('\r'); pos++; break;
                    case 't': out.write('\t'); pos++; break;
                    case 'v': out.write(11); pos++; break;
                    case '\\': out.write('\\'); pos++; break;
                    case '"': out.write('"'); pos++; break;
                    case '\'': out.write('\''); pos++; break;
                    case '\n': case '\r': newline(); out.write('\n'); break;
                    case -1: throw error("unfinished string", "<eof>");
                    default:
                        if (e >= '0' && e <= '9') {
                            int v = 0, n = 0;
                            while (n < 3 && cur() >= '0' && cur() <= '9') { v = v * 10 + (cur() - '0'); pos++; n++; }
                            if (v > 255) throw error("escape sequence too large", "\\" + v);
                            out.write(v);
                        } else {
                            // Lua 5.1 leaves any other escaped character as itself
                            out.write(e); pos++;
                        }
                }
            } else {
                out.write(c); pos++;
            }
        }
        str = LuaString.of(out.toByteArray());
        type = TK_STRING;
    }

    private void readNumber() {
        int start = pos;
        if (cur() == '0' && (at(1) == 'x' || at(1) == 'X')) {
            pos += 2;
            while (NumFmt.isHex((char) Math.max(cur(), 0)) || cur() == '.' || cur() == 'p' || cur() == 'P'
                   || ((cur() == '+' || cur() == '-') && (at(-1) == 'p' || at(-1) == 'P'))) pos++;
        } else {
            while (true) {
                int c = cur();
                if (c >= '0' && c <= '9' || c == '.') { pos++; continue; }
                if (c == 'e' || c == 'E') {
                    pos++;
                    if (cur() == '+' || cur() == '-') pos++;
                    continue;
                }
                break;
            }
        }
        while (isNamePart(cur())) pos++;                 // swallow "1e" / "3a" so the error text matches
        String s = new String(src, start, pos - start, java.nio.charset.StandardCharsets.ISO_8859_1);
        Double d = NumFmt.strtod(s);
        if (d == null) {
            type = TK_NUMBER; num = 0;
            throw Interp.syntaxError(chunkname + ":" + line + ": malformed number near '" + s + "'");
        }
        num = d;
        type = TK_NUMBER;
    }

    static boolean isNameStart(int c) {
        return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    static boolean isNamePart(int c) {
        return isNameStart(c) || (c >= '0' && c <= '9');
    }
}
