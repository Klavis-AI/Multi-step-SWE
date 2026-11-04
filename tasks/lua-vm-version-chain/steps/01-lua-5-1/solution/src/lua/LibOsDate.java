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
            r.rawset("year", (double) d.getYear());
            r.rawset("month", (double) d.getMonthValue());
            r.rawset("day", (double) d.getDayOfMonth());
            r.rawset("hour", (double) d.getHour());
            r.rawset("min", (double) d.getMinute());
            r.rawset("sec", (double) d.getSecond());
            r.rawset("wday", (double) (d.getDayOfWeek().getValue() % 7 + 1));
            r.rawset("yday", (double) d.getDayOfYear());
            r.rawset("isdst", Boolean.FALSE);
            return Lib.ret(r);
        }
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < fmt.length(); i++) {
            char c = fmt.charAt(i);
            if (c != '%') { out.append(c); continue; }
            i++;
            if (i >= fmt.length()) break;
            switch (fmt.charAt(i)) {
                case 'a': out.append(cap(d.getDayOfWeek().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US))); break;
                case 'A': out.append(cap(d.getDayOfWeek().getDisplayName(java.time.format.TextStyle.FULL, java.util.Locale.US))); break;
                case 'b': case 'h': out.append(cap(d.getMonth().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US))); break;
                case 'B': out.append(cap(d.getMonth().getDisplayName(java.time.format.TextStyle.FULL, java.util.Locale.US))); break;
                case 'c': out.append(String.format("%s %s %2d %02d:%02d:%02d %d",
                        cap(d.getDayOfWeek().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US)),
                        cap(d.getMonth().getDisplayName(java.time.format.TextStyle.SHORT, java.util.Locale.US)),
                        d.getDayOfMonth(), d.getHour(), d.getMinute(), d.getSecond(), d.getYear())); break;
                case 'd': out.append(String.format("%02d", d.getDayOfMonth())); break;
                case 'H': out.append(String.format("%02d", d.getHour())); break;
                case 'I': out.append(String.format("%02d", (d.getHour() % 12 == 0) ? 12 : d.getHour() % 12)); break;
                case 'j': out.append(String.format("%03d", d.getDayOfYear())); break;
                case 'M': out.append(String.format("%02d", d.getMinute())); break;
                case 'm': out.append(String.format("%02d", d.getMonthValue())); break;
                case 'p': out.append(d.getHour() < 12 ? "AM" : "PM"); break;
                case 'S': out.append(String.format("%02d", d.getSecond())); break;
                case 'w': out.append(d.getDayOfWeek().getValue() % 7); break;
                case 'x': out.append(String.format("%02d/%02d/%02d", d.getMonthValue(), d.getDayOfMonth(), d.getYear() % 100)); break;
                case 'X': out.append(String.format("%02d:%02d:%02d", d.getHour(), d.getMinute(), d.getSecond())); break;
                case 'y': out.append(String.format("%02d", d.getYear() % 100)); break;
                case 'Y': out.append(d.getYear()); break;
                case 'Z': out.append(d.getZone().getId()); break;
                case '%': out.append('%'); break;
                default: throw Lib.argError(0, "invalid conversion specifier '%" + fmt.charAt(i) + "'");
            }
        }
        return Lib.ret(LuaString.of(out.toString()));
    }

    private static String cap(String s) {
        return s.substring(0, 1).toUpperCase() + s.substring(1).toLowerCase();
    }
}
