///////////////////////////////////////////////////////////////////////////////
// ise_sys_utils.cpp -- conversions, string helpers, path and file helpers.
//
// Every input is a literal. The only state any of these functions touch is
// the temporary directory the runner creates and removes.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>

using namespace ise;
using namespace drv;

namespace {

// The inputs every string->number conversion is asked about. Empty, signed,
// padded, over-long, wrong base, and both int32 and int64 boundaries either
// side.
const char *NUMERIC_INPUTS[] = {
    "", " ", "0", "-0", "+0", "1", "-1", "+1", "007", " 42", "42 ", " 42 ",
    "4 2", "0x10", "0X10", "010", "1_000", "1,000", "1.0", ".5", "5.", "1e3",
    "-1e3", "1e309", "nan", "NaN", "inf", "-inf", "abc", "12abc", "abc12",
    "2147483647", "2147483648", "-2147483648", "-2147483649", "4294967295",
    "4294967296", "9223372036854775807", "9223372036854775808",
    "-9223372036854775808", "-9223372036854775809",
    "18446744073709551615", "99999999999999999999999999",
    "true", "TRUE", "True", "false", "FALSE", "yes", "no", "on", "off",
    "-", "+", "--1", "  ", "\t7", "7\n"
};
const int NUMERIC_COUNT = sizeof(NUMERIC_INPUTS) / sizeof(NUMERIC_INPUTS[0]);

} // namespace

//-----------------------------------------------------------------------------

DRV_CASE(sysutils_is_int_str)
{
    for (int i = 0; i < NUMERIC_COUNT; i++)
        outf("isIntStr(%s) = %d", esc(NUMERIC_INPUTS[i]).c_str(),
             isIntStr(NUMERIC_INPUTS[i]) ? 1 : 0);
}

DRV_CASE(sysutils_is_int64_str)
{
    for (int i = 0; i < NUMERIC_COUNT; i++)
        outf("isInt64Str(%s) = %d", esc(NUMERIC_INPUTS[i]).c_str(),
             isInt64Str(NUMERIC_INPUTS[i]) ? 1 : 0);
}

DRV_CASE(sysutils_is_float_str)
{
    for (int i = 0; i < NUMERIC_COUNT; i++)
        outf("isFloatStr(%s) = %d", esc(NUMERIC_INPUTS[i]).c_str(),
             isFloatStr(NUMERIC_INPUTS[i]) ? 1 : 0);
}

DRV_CASE(sysutils_is_bool_str)
{
    for (int i = 0; i < NUMERIC_COUNT; i++)
        outf("isBoolStr(%s) = %d", esc(NUMERIC_INPUTS[i]).c_str(),
             isBoolStr(NUMERIC_INPUTS[i]) ? 1 : 0);
}

DRV_CASE(sysutils_str_to_int)
{
    for (int i = 0; i < NUMERIC_COUNT; i++)
        outf("strToInt(%s, -7) = %d", esc(NUMERIC_INPUTS[i]).c_str(),
             strToInt(NUMERIC_INPUTS[i], -7));
}

DRV_CASE(sysutils_str_to_int64)
{
    for (int i = 0; i < NUMERIC_COUNT; i++)
    {
        INT64 value = strToInt64(NUMERIC_INPUTS[i], -7);
        outf("strToInt64(%s, -7) = %s", esc(NUMERIC_INPUTS[i]).c_str(),
             intToStr(value).c_str());
    }
}

DRV_CASE(sysutils_str_to_bool)
{
    for (int i = 0; i < NUMERIC_COUNT; i++)
        outf("strToBool(%s, false) = %d / (.., true) = %d",
             esc(NUMERIC_INPUTS[i]).c_str(),
             strToBool(NUMERIC_INPUTS[i], false) ? 1 : 0,
             strToBool(NUMERIC_INPUTS[i], true) ? 1 : 0);
}

DRV_CASE(sysutils_str_to_float)
{
    // Printed through floatToStr with a fixed format so the rendering, not
    // the double's exact bits, is what is compared.
    for (int i = 0; i < NUMERIC_COUNT; i++)
        outf("strToFloat(%s, -7) = %s", esc(NUMERIC_INPUTS[i]).c_str(),
             floatToStr(strToFloat(NUMERIC_INPUTS[i], -7), "%.6g").c_str());
}

DRV_CASE(sysutils_int_to_str)
{
    static const int VALUES[] = {0, 1, -1, 7, -7, 1000, -1000,
                                 2147483647, -2147483647 - 1};
    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
        outf("intToStr(int %d) = %s", VALUES[i], intToStr(VALUES[i]).c_str());

    static const INT64 WIDE[] = {
        0, 1, -1, 4294967295LL, 4294967296LL, -4294967296LL,
        9223372036854775807LL, -9223372036854775807LL - 1};
    for (size_t i = 0; i < sizeof(WIDE) / sizeof(WIDE[0]); i++)
        out("intToStr(INT64) = " + intToStr(WIDE[i]));
}

DRV_CASE(sysutils_float_to_str)
{
    static const double VALUES[] = {0.0, -0.0, 1.0, -1.0, 0.5, 1.0 / 3.0,
                                    1e-7, 1e7, 1e20, -1e20, 1234.5678};
    static const char *FORMATS[] = {"%f", "%.2f", "%g", "%e", "%.0f"};
    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
        for (size_t j = 0; j < sizeof(FORMATS) / sizeof(FORMATS[0]); j++)
            outf("floatToStr(%s) = %s", FORMATS[j],
                 floatToStr(VALUES[i], FORMATS[j]).c_str());
}

DRV_CASE(sysutils_bool_to_str)
{
    out("boolToStr(true,false)  = " + boolToStr(true, false));
    out("boolToStr(false,false) = " + boolToStr(false, false));
    out("boolToStr(true,true)   = " + boolToStr(true, true));
    out("boolToStr(false,true)  = " + boolToStr(false, true));
}

//-----------------------------------------------------------------------------

DRV_CASE(sysutils_format_string)
{
    out(esc(formatString("")));
    out(esc(formatString("plain")));
    out(esc(formatString("%d %i %u", -5, -5, 5u)));
    out(esc(formatString("%s|%s", "", "x")));
    out(esc(formatString("[%5d][%-5d][%05d]", 42, 42, 42)));
    out(esc(formatString("%c%c%c", 'a', '\t', 'z')));
    out(esc(formatString("%x %X %o", 255, 255, 8)));
    out(esc(formatString("%%")));
    out(esc(formatString("%.3f", 1.0 / 3.0)));
    // Two conversions with literal text wedged between them. This line read
    // "%p-less %s" with one argument until sanitize.py reached it: %p is a
    // conversion, so it ate the argument and printed the address of a string
    // literal, and the %s behind it read a vararg slot nothing had written.
    // The address was identical across runs of one binary and would have moved
    // under any edit to the tree, so the expectation would have failed every
    // correct migration (NOTES B16).
    out(esc(formatString("%s-and-%s", "ok", "more")));
    // Longer than any plausible internal stack buffer: the function has to
    // grow, which is the branch worth exercising.
    std::string big(5000, 'q');
    std::string result = formatString("<%s>", big.c_str());
    outf("long result size = %d, head = %s, tail = %s",
         static_cast<int>(result.size()), result.substr(0, 4).c_str(),
         result.substr(result.size() - 4).c_str());
}

DRV_CASE(sysutils_same_text_and_compare)
{
    static const char *PAIRS[][2] = {
        {"", ""}, {"", "a"}, {"a", ""}, {"a", "a"}, {"a", "A"},
        {"abc", "ABC"}, {"abc", "abd"}, {"abd", "abc"}, {"abc", "abcd"},
        {"\xC3\xA9", "\xC3\x89"}, {"a b", "a  b"}, {"1", "1"}
    };
    for (size_t i = 0; i < sizeof(PAIRS) / sizeof(PAIRS[0]); i++)
        outf("sameText(%s,%s)=%d compareText=%d",
             esc(PAIRS[i][0]).c_str(), esc(PAIRS[i][1]).c_str(),
             sameText(PAIRS[i][0], PAIRS[i][1]) ? 1 : 0,
             compareText(PAIRS[i][0], PAIRS[i][1]));
}

DRV_CASE(sysutils_trim_case)
{
    static const char *INPUTS[] = {
        "", " ", "   ", "a", " a", "a ", "  a  ", "\ta\t", "\na\n",
        "\r\n a b \r\n", "a b", "\x01 a \x1F", "MiXeD", "\xC3\xA9t\xC3\xA9",
        "123abcXYZ_-", "  \t\r\n  "
    };
    for (size_t i = 0; i < sizeof(INPUTS) / sizeof(INPUTS[0]); i++)
        outf("in=%s trim=%s upper=%s lower=%s", esc(INPUTS[i]).c_str(),
             esc(trimString(INPUTS[i])).c_str(),
             esc(upperCase(INPUTS[i])).c_str(),
             esc(lowerCase(INPUTS[i])).c_str());
}

DRV_CASE(sysutils_replace_string)
{
    struct Row { const char *src, *from, *to; bool all, caseSens; };
    static const Row ROWS[] = {
        {"aaa", "a", "b", false, true},
        {"aaa", "a", "b", true, true},
        {"aaa", "aa", "b", true, true},
        // Note: an empty oldPattern makes the baseline loop forever, so it
        // is not an input any expectation can be recorded for.
        {"aaa", "aaaa", "b", true, true},
        {"abcABC", "abc", "x", true, true},
        {"abcABC", "abc", "x", true, false},
        {"abcABC", "ABC", "x", true, false},
        {"", "a", "b", true, true},
        {"aaa", "a", "", true, true},
        {"aaa", "a", "aa", true, true},
        {"hello world", "o", "0", true, true},
        {"hello world", "world", "there", false, true}
    };
    for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++)
        outf("replace(%s,%s,%s,all=%d,cs=%d) = %s",
             esc(ROWS[i].src).c_str(), esc(ROWS[i].from).c_str(),
             esc(ROWS[i].to).c_str(), ROWS[i].all ? 1 : 0,
             ROWS[i].caseSens ? 1 : 0,
             esc(repalceString(ROWS[i].src, ROWS[i].from, ROWS[i].to,
                               ROWS[i].all, ROWS[i].caseSens)).c_str());
}

DRV_CASE(sysutils_split_string)
{
    static const char *INPUTS[] = {
        "", ",", "a", "a,b", "a,b,", ",a,b", ",,", "a, b , c",
        " a , b ", "a,,b", "one", "x,y,z,w,v"
    };
    for (size_t i = 0; i < sizeof(INPUTS) / sizeof(INPUTS[0]); i++)
    {
        for (int trim = 0; trim <= 1; trim++)
        {
            StrList list;
            splitString(INPUTS[i], ',', list, trim != 0);
            std::string line = formatString("split(%s,trim=%d) count=%d:",
                                            esc(INPUTS[i]).c_str(), trim,
                                            list.getCount());
            for (int k = 0; k < list.getCount(); k++)
                line += " [" + esc(list[k]) + "]";
            out(line);
        }
    }
}

DRV_CASE(sysutils_split_string_to_int)
{
    static const char *INPUTS[] = {
        "", "1", "1,2,3", "1, 2 ,3", "1,,3", "a,b", "1,a,3",
        "2147483647,2147483648", "-1,-2"
    };
    for (size_t i = 0; i < sizeof(INPUTS) / sizeof(INPUTS[0]); i++)
    {
        IntegerArray values;
        splitStringToInt(INPUTS[i], ',', values);
        std::string line = formatString("splitToInt(%s) count=%d:",
                                        esc(INPUTS[i]).c_str(),
                                        static_cast<int>(values.size()));
        for (size_t k = 0; k < values.size(); k++)
            line += " " + intToStr(values[k]);
        out(line);
    }
}

DRV_CASE(sysutils_fetch_str)
{
    static const char *INPUTS[] = {
        "", " ", "a b c", "  a  b  ", "a", "a ", " a", "a,b"
    };
    for (size_t i = 0; i < sizeof(INPUTS) / sizeof(INPUTS[0]); i++)
    {
        for (int del = 0; del <= 1; del++)
        {
            std::string rest = INPUTS[i];
            std::string first = fetchStr(rest, ' ', del != 0);
            outf("fetchStr(%s,del=%d) -> %s rest=%s",
                 esc(INPUTS[i]).c_str(), del, esc(first).c_str(),
                 esc(rest).c_str());
        }
    }
    // The call that consumes `commas` is a statement of its own: it and a read
    // of `commas` in the same argument list would be evaluated in whichever
    // order the compiler chose.
    std::string commas = "a,b,c";
    std::string head = fetchStr(commas, ',', true);
    outf("fetchStr(comma) -> %s rest=%s", esc(head).c_str(), esc(commas).c_str());
}

DRV_CASE(sysutils_thousand_sep)
{
    static const INT64 VALUES[] = {
        0, 1, 12, 123, 1234, 12345, 123456, 1234567, -1, -1234, -1234567,
        1000000000LL, 9223372036854775807LL, -9223372036854775807LL - 1
    };
    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
        out("addThousandSep(" + intToStr(VALUES[i]) + ") = " +
            addThousandSep(VALUES[i]));
}

DRV_CASE(sysutils_quoted_str)
{
    static const char *INPUTS[] = {
        "", "a", "a'b", "'", "''", "a\"b", "  spaced  ", "line\nbreak",
        "'quoted'", "tail'", "'head"
    };
    for (size_t i = 0; i < sizeof(INPUTS) / sizeof(INPUTS[0]); i++)
    {
        std::string quoted = getQuotedStr(INPUTS[i], '\'');
        const char *cursor = quoted.c_str();
        std::string extracted = extractQuotedStr(cursor, '\'');
        outf("in=%s quoted=%s extracted=%s rest=%s dequoted=%s",
             esc(INPUTS[i]).c_str(), esc(quoted).c_str(),
             esc(extracted).c_str(), esc(cursor).c_str(),
             esc(getDequotedStr(quoted.c_str(), '\'')).c_str());
    }
    // A string that is not quoted at all, and one whose quote never closes.
    const char *unquoted = "bare text";
    const char *cursor = unquoted;
    std::string bare = extractQuotedStr(cursor, '\'');
    outf("extract(bare) = %s rest=%s", esc(bare).c_str(), esc(cursor).c_str());
    const char *unclosed = "'never ends";
    cursor = unclosed;
    std::string open = extractQuotedStr(cursor, '\'');
    outf("extract(unclosed) = %s rest=%s", esc(open).c_str(), esc(cursor).c_str());
}

//-----------------------------------------------------------------------------

DRV_CASE(sysutils_path_strings)
{
    static const char *PATHS[] = {
        "", "/", "a", "a.txt", "/a/b/c.txt", "/a/b/", "./rel.tar.gz",
        "../up.txt", "no_ext", ".hidden", "/a.b/c", "/a/b/.hidden",
        "trailing/", "//double//slash//x.y", "/a/b/c.", "/a/b/.",
        "name.with.many.dots.ext"
    };
    for (size_t i = 0; i < sizeof(PATHS) / sizeof(PATHS[0]); i++)
        outf("p=%s path=%s name=%s ext=%s chg=%s withSlash=%s withoutSlash=%s",
             esc(PATHS[i]).c_str(),
             esc(extractFilePath(PATHS[i])).c_str(),
             esc(extractFileName(PATHS[i])).c_str(),
             esc(extractFileExt(PATHS[i])).c_str(),
             esc(changeFileExt(PATHS[i], ".new")).c_str(),
             esc(pathWithSlash(PATHS[i])).c_str(),
             esc(pathWithoutSlash(PATHS[i])).c_str());
}

DRV_CASE(sysutils_file_lifecycle)
{
    std::string dir = tmpPath("files");
    outf("createDir = %d", createDir(dir) ? 1 : 0);
    outf("createDir again = %d", createDir(dir) ? 1 : 0);
    outf("directoryExists = %d", directoryExists(dir) ? 1 : 0);

    std::string file = dir + "/one.txt";
    outf("fileExists before = %d", fileExists(file) ? 1 : 0);
    {
        FileStream stream;
        outf("open create = %d",
             stream.open(file, FM_CREATE | FM_SHARE_DENY_NONE) ? 1 : 0);
        const char *payload = "0123456789";
        outf("write = %d", stream.write(payload, 10));
        outf("size = %s", intToStr(stream.getSize()).c_str());
    }
    outf("fileExists after = %d", fileExists(file) ? 1 : 0);
    outf("getFileSize = %s", intToStr(getFileSize(file)).c_str());
    outf("getFileSize(missing) = %s",
         intToStr(getFileSize(dir + "/nope.txt")).c_str());

    std::string renamed = dir + "/two.txt";
    outf("renameFile = %d", renameFile(file, renamed) ? 1 : 0);
    outf("old exists = %d, new exists = %d",
         fileExists(file) ? 1 : 0, fileExists(renamed) ? 1 : 0);
    outf("renameFile(missing) = %d",
         renameFile(file, dir + "/three.txt") ? 1 : 0);

    outf("deleteFile = %d", deleteFile(renamed) ? 1 : 0);
    outf("deleteFile again = %d", deleteFile(renamed) ? 1 : 0);
    outf("removeFile(missing) = %d", removeFile(renamed) ? 1 : 0);

    outf("forceDirectories deep = %d",
         forceDirectories(dir + "/x/y/z") ? 1 : 0);
    outf("deep exists = %d", directoryExists(dir + "/x/y/z") ? 1 : 0);
    outf("deleteDir non-recursive = %d", deleteDir(dir, false) ? 1 : 0);
    outf("deleteDir recursive = %d", deleteDir(dir, true) ? 1 : 0);
    outf("dir exists at end = %d", directoryExists(dir) ? 1 : 0);
}

DRV_CASE(sysutils_find_files)
{
    std::string dir = tmpPath("find");
    forceDirectories(dir + "/sub");
    static const char *NAMES[] = {"b.txt", "a.txt", "c.dat", ".dot"};
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
    {
        FileStream stream;
        stream.open(std::string(dir) + "/" + NAMES[i],
                    FM_CREATE | FM_SHARE_DENY_NONE);
        stream.write(NAMES[i], static_cast<int>(strlen(NAMES[i])));
    }

    FileFindResult found;
    findFiles(dir + "/*", FA_ANY_FILE, found);
    // Directory order is the filesystem's business, not the framework's.
    std::vector<std::string> lines;
    for (size_t i = 0; i < found.size(); i++)
        lines.push_back(formatString("%s size=%s isDir=%d",
                                     found[i].fileName.c_str(),
                                     intToStr(found[i].fileSize).c_str(),
                                     (found[i].attr & FA_DIRECTORY) ? 1 : 0));
    std::sort(lines.begin(), lines.end());
    outf("count = %d", static_cast<int>(lines.size()));
    for (size_t i = 0; i < lines.size(); i++)
        out("  " + lines[i]);

    FileFindResult none;
    findFiles(dir + "/*.missing", FA_ANY_FILE, none);
    outf("no-match count = %d", static_cast<int>(none.size()));
    deleteDir(dir, true);
}

//-----------------------------------------------------------------------------

DRV_CASE(sysutils_sys_error_messages)
{
    static const int CODES[] = {0, 1, 2, 9, 13, 22, 32, 110, 111};
    for (size_t i = 0; i < sizeof(CODES) / sizeof(CODES[0]); i++)
        outf("sysErrorMessage(%d) = %s", CODES[i],
             esc(sysErrorMessage(CODES[i])).c_str());
}

DRV_CASE(sysutils_tick_diff)
{
    // Pure arithmetic over the tick counter, including the wrap the running
    // clock would take days to reach.
    struct Row { UINT64 older, newer; };
    static const Row ROWS[] = {
        {0, 0}, {0, 1}, {1, 0}, {100, 250},
        {0xFFFFFFFFULL, 0x100000000ULL},
        {0xFFFFFFFFFFFFFFFFULL, 0},
        {0xFFFFFFFFFFFFFF00ULL, 0xFF}
    };
    for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++)
        out("getTickDiff(" + intToStr(static_cast<INT64>(ROWS[i].older)) +
            ", " + intToStr(static_cast<INT64>(ROWS[i].newer)) + ") = " +
            intToStr(static_cast<INT64>(
                getTickDiff(ROWS[i].older, ROWS[i].newer))));
}

DRV_CASE(sysutils_random_list_is_a_permutation)
{
    // The values a PRNG produces are not part of the framework's contract and
    // a migration is free to change the generator. That the result is a
    // permutation of [start, end] is the contract, so that is what is scored.
    const int START = 10, END = 29, COUNT = END - START + 1;
    std::vector<int> values(COUNT, 0);
    randomize();
    generateRandomList(START, END, &values[0]);
    std::vector<int> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    bool ok = true;
    for (int i = 0; i < COUNT; i++)
        if (sorted[i] != START + i)
            ok = false;
    outf("count=%d permutation=%d", COUNT, ok ? 1 : 0);

    bool inRange = true;
    for (int i = 0; i < 500; i++)
    {
        int value = getRandom(5, 9);
        if (value < 5 || value > 9)
            inRange = false;
    }
    outf("getRandom(5,9) stayed in range = %d", inRange ? 1 : 0);
}

DRV_CASE(sysutils_app_paths_shape)
{
    // The absolute path depends on where the binary was built, so only its
    // shape is scored: that the three functions agree with each other.
    std::string exeWithPath = getAppExeName(true);
    std::string exeBare = getAppExeName(false);
    std::string path = getAppPath();
    outf("bare has no slash = %d",
         exeBare.find('/') == std::string::npos ? 1 : 0);
    outf("full ends with bare = %d",
         (exeWithPath.size() >= exeBare.size() &&
          exeWithPath.compare(exeWithPath.size() - exeBare.size(),
                              exeBare.size(), exeBare) == 0) ? 1 : 0);
    outf("path is a prefix of full = %d",
         exeWithPath.compare(0, path.size(), path) == 0 ? 1 : 0);
    outf("path ends with slash = %d",
         (!path.empty() && path[path.size() - 1] == '/') ? 1 : 0);
    std::string sub = getAppSubPath("data");
    outf("subpath tail = %s",
         esc(sub.substr(path.size())).c_str());
}
