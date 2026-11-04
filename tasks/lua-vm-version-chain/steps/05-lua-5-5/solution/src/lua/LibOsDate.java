package lua;

import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.time.temporal.ChronoField;

/** os.date, with the strftime conversions the manual lists. */
final class LibOsDate {

    static Object[] date(Object[] a) {
        String fmt = Lib.optString(a, 0, "%c").toString();
        long t = (long) Lib.optNumber(a, 1, System.currentTimeMillis() / 1000.0);
        boolean utc = false;
        if (fmt.startsWith("!")) { utc = true; fmt = fmt.substring(1); }
        ZonedDateTime d = java.time.Instant.ofEpochSecond(t)
                .atZone(utc ? ZoneId.of("UTC") : ZoneId.systemDefault());
        if (fmt.startsWith("*t")) {
            LuaTable r = new LuaTable();
            r.rawset("year", (long) d.getYear());
            r.rawset("month", (long) d.getMonthValue());
            r.rawset("day", (long) d.getDayOfMonth());
            r.rawset("hour", (long) d.getHour());
            r.rawset("min", (long) d.getMinute());
            r.rawset("sec", (long) d.getSecond());
            r.rawset("wday", (long) (d.getDayOfWeek().getValue() % 7 + 1));
            r.rawset("yday", (long) d.getDayOfYear());
            r.rawset("isdst", Boolean.FALSE);
            return Lib.ret(r);
        }
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < fmt.length(); i++) {
            char c = fmt.charAt(i);
            if (c != '%') { out.append(c); continue; }
            // checkoption: the C99 option list, with 'E' and 'O' as modifiers.
            // Anything else reports the rest of the format as the specifier.
            String rest = fmt.substring(i + 1);
            String spec = option(rest);
            if (spec == null) throw Lib.argError(0, "invalid conversion specifier '%" + rest + "'");
            i += spec.length();
            out.append(conv(spec.charAt(spec.length() - 1), d, utc));
        }
        return Lib.ret(LuaString.of(out.toString()));
    }

    private static final String ONE = "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%";
    private static final String AFTER_E = "cCxXyY";
    private static final String AFTER_O = "deHImMSuUVwWy";

    /** The conversion specifier at the head of `rest`, or null when there is none. */
    private static String option(String rest) {
        if (rest.isEmpty()) return null;
        char c = rest.charAt(0);
        if (c == 'E' || c == 'O') {
            String allowed = (c == 'E') ? AFTER_E : AFTER_O;
            if (rest.length() >= 2 && allowed.indexOf(rest.charAt(1)) >= 0) return rest.substring(0, 2);
            return null;
        }
        return ONE.indexOf(c) >= 0 ? rest.substring(0, 1) : null;
    }

    /** One strftime conversion, in the "C" locale. */
    private static String conv(char c, ZonedDateTime d, boolean utc) {
        switch (c) {
            case 'a': return cap(d.getDayOfWeek().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US));
            case 'A': return cap(d.getDayOfWeek().getDisplayName(java.time.format.TextStyle.FULL, java.util.Locale.US));
            case 'b': case 'h': return cap(d.getMonth().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US));
            case 'B': return cap(d.getMonth().getDisplayName(java.time.format.TextStyle.FULL, java.util.Locale.US));
            case 'c': return String.format("%s %s %2d %02d:%02d:%02d %d",
                    cap(d.getDayOfWeek().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US)),
                    cap(d.getMonth().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US)),
                    d.getDayOfMonth(), d.getHour(), d.getMinute(), d.getSecond(), d.getYear());
            case 'C': return String.format("%02d", d.getYear() / 100);
            case 'd': return String.format("%02d", d.getDayOfMonth());
            case 'D': return String.format("%02d/%02d/%02d", d.getMonthValue(), d.getDayOfMonth(), Math.floorMod(d.getYear(), 100));
            case 'e': return String.format("%2d", d.getDayOfMonth());
            case 'F': return String.format("%04d-%02d-%02d", d.getYear(), d.getMonthValue(), d.getDayOfMonth());
            case 'g': return String.format("%02d", Math.floorMod(d.get(java.time.temporal.IsoFields.WEEK_BASED_YEAR), 100));
            case 'G': return String.valueOf(d.get(java.time.temporal.IsoFields.WEEK_BASED_YEAR));
            case 'H': return String.format("%02d", d.getHour());
            case 'I': return String.format("%02d", (d.getHour() % 12 == 0) ? 12 : d.getHour() % 12);
            case 'j': return String.format("%03d", d.getDayOfYear());
            case 'm': return String.format("%02d", d.getMonthValue());
            case 'M': return String.format("%02d", d.getMinute());
            case 'n': return "\n";
            case 'p': return d.getHour() < 12 ? "AM" : "PM";
            case 'r': return String.format("%02d:%02d:%02d %s",
                    (d.getHour() % 12 == 0) ? 12 : d.getHour() % 12, d.getMinute(), d.getSecond(),
                    d.getHour() < 12 ? "AM" : "PM");
            case 'R': return String.format("%02d:%02d", d.getHour(), d.getMinute());
            case 'S': return String.format("%02d", d.getSecond());
            case 't': return "\t";
            case 'T': return String.format("%02d:%02d:%02d", d.getHour(), d.getMinute(), d.getSecond());
            case 'u': return String.valueOf(d.getDayOfWeek().getValue());
            case 'U': return String.format("%02d", (d.getDayOfYear() + 6 - (d.getDayOfWeek().getValue() % 7)) / 7);
            case 'V': return String.format("%02d", d.get(java.time.temporal.IsoFields.WEEK_OF_WEEK_BASED_YEAR));
            case 'w': return String.valueOf(d.getDayOfWeek().getValue() % 7);
            case 'W': return String.format("%02d", (d.getDayOfYear() + 7 - d.getDayOfWeek().getValue()) / 7);
            case 'x': return String.format("%02d/%02d/%02d", d.getMonthValue(), d.getDayOfMonth(), Math.floorMod(d.getYear(), 100));
            case 'X': return String.format("%02d:%02d:%02d", d.getHour(), d.getMinute(), d.getSecond());
            case 'y': return String.format("%02d", Math.floorMod(d.getYear(), 100));
            case 'Y': return String.valueOf(d.getYear());
            case 'z': {
                int off = d.getOffset().getTotalSeconds();
                return String.format("%s%02d%02d", off < 0 ? "-" : "+", Math.abs(off) / 3600, (Math.abs(off) / 60) % 60);
            }
            case 'Z': return utc ? "GMT"
                    : java.util.TimeZone.getTimeZone(d.getZone()).getDisplayName(
                        d.getZone().getRules().isDaylightSavings(d.toInstant()),
                        java.util.TimeZone.SHORT, java.util.Locale.US);
            default: return "%";
        }
    }

    private static String cap(String s) {
        return s.substring(0, 1).toUpperCase() + s.substring(1).toLowerCase();
    }
}
