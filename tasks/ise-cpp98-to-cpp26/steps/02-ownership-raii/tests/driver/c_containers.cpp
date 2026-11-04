///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for the container classes in ise_classes.h:
// PointerList, PropertyList, Strings and StrList.
//
// PointerList holds void*, and a real address is different on every run, so
// every case here stores small integers cast to POINTER and prints them back
// as integers. Nothing prints an address.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <string>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

namespace {

// Small integers stand in for pointers, so the transcript is reproducible.
inline POINTER tok(int n) { return reinterpret_cast<POINTER>(static_cast<long>(n)); }
inline int untok(POINTER p) { return static_cast<int>(reinterpret_cast<long>(p)); }

std::string dumpList(const PointerList& list)
{
    std::string result;
    for (int i = 0; i < list.getCount(); i++)
    {
        if (i > 0) result += " ";
        result += intToStr(untok(const_cast<PointerList&>(list)[i]));
    }
    return result;
}

std::string dumpStrings(const Strings& s)
{
    std::string result;
    for (int i = 0; i < s.getCount(); i++)
    {
        if (i > 0) result += "|";
        result += s[i];
    }
    return result;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// PointerList

DRV_CASE(containers_pointer_list_basics)
{
    PointerList list;
    outf("empty: count=%d isEmpty=%d", list.getCount(), (list.isEmpty() ? 1 : 0));

    for (int i = 1; i <= 5; i++)
    {
        int index = list.add(tok(i));
        outf("add %d -> index %d", i, index);
    }
    outf("after adds: [%s]", dumpList(list).c_str());

    outf("first=%d last=%d", untok(list.first()), untok(list.last()));
    outf("indexOf 3 = %d, indexOf 99 = %d", list.indexOf(tok(3)), list.indexOf(tok(99)));

    list.insert(0, tok(10));
    list.insert(3, tok(20));
    list.insert(list.getCount(), tok(30));
    outf("after inserts: [%s]", dumpList(list).c_str());

    outf("remove 20 -> %d", list.remove(tok(20)));
    outf("remove absent -> %d", list.remove(tok(777)));
    outf("after removes: [%s]", dumpList(list).c_str());

    list.del(0);
    outf("after del 0: [%s]", dumpList(list).c_str());

    POINTER taken = list.extract(tok(30));
    outf("extract 30 -> %d, list [%s]", untok(taken), dumpList(list).c_str());
    outf("extract absent -> %d", untok(list.extract(tok(777))));

    list.clear();
    outf("after clear: count=%d isEmpty=%d", list.getCount(), (list.isEmpty() ? 1 : 0));
}

DRV_CASE(containers_pointer_list_move_and_resize)
{
    PointerList list;
    for (int i = 0; i < 6; i++) list.add(tok(i));

    // move() is a rotation of the span between the two indices, not a swap;
    // which one it is matters to anything that relies on the relative order of
    // the elements in between.
    list.move(0, 3);
    outf("move 0->3: [%s]", dumpList(list).c_str());
    list.move(5, 1);
    outf("move 5->1: [%s]", dumpList(list).c_str());
    list.move(2, 2);
    outf("move 2->2: [%s]", dumpList(list).c_str());

    // resize() grows with NULLs and truncates.
    list.resize(8);
    outf("resize 8: count=%d [%s]", list.getCount(), dumpList(list).c_str());
    list.resize(3);
    outf("resize 3: count=%d [%s]", list.getCount(), dumpList(list).c_str());
    list.resize(0);
    outf("resize 0: count=%d", list.getCount());
}

DRV_CASE(containers_pointer_list_duplicates_and_null)
{
    PointerList list;

    // NULL is a legal element; indexOf and remove must not treat it as a
    // sentinel for "not found".
    list.add(NULL);
    list.add(tok(1));
    list.add(NULL);
    outf("count=%d [%s]", list.getCount(), dumpList(list).c_str());
    outf("indexOf NULL = %d", list.indexOf(NULL));
    int removed = list.remove(NULL);
    outf("remove NULL = %d, now [%s]", removed, dumpList(list).c_str());

    // Duplicates are permitted; indexOf and remove both find the first.
    PointerList dup;
    dup.add(tok(7));
    dup.add(tok(8));
    dup.add(tok(7));
    outf("dup indexOf 7 = %d", dup.indexOf(tok(7)));
    int dupRemoved = dup.remove(tok(7));
    outf("dup remove 7 = %d, now [%s]", dupRemoved, dumpList(dup).c_str());

    // Assignment copies the elements, not a reference to them.
    PointerList copy;
    copy = dup;
    dup.clear();
    outf("copy survives source clear: [%s], source [%s]",
        dumpList(copy).c_str(), dumpList(dup).c_str());

    // A list grown well past its initial capacity keeps its order.
    PointerList big;
    for (int i = 0; i < 1000; i++) big.add(tok(i));
    bool ordered = true;
    for (int i = 0; i < 1000; i++)
        if (untok(big[i]) != i) ordered = false;
    outf("1000 elements ordered: %d, count=%d, last=%d",
        (ordered ? 1 : 0), big.getCount(), untok(big.last()));
}

///////////////////////////////////////////////////////////////////////////////
// PropertyList

DRV_CASE(containers_property_list_basics)
{
    PropertyList props;
    outf("empty: count=%d isEmpty=%d propString=[%s]",
        props.getCount(), (props.isEmpty() ? 1 : 0), props.getPropString().c_str());

    props.add("alpha", "1");
    props.add("beta", "two");
    props.add("gamma", "");
    outf("count=%d propString=[%s]", props.getCount(), props.getPropString().c_str());

    string value;
    outf("getValue beta = %d [%s]", (props.getValue("beta", value) ? 1 : 0), value.c_str());
    value = "untouched";
    outf("getValue missing = %d [%s]",
        (props.getValue("missing", value) ? 1 : 0), value.c_str());

    outf("nameExists alpha = %d, ALPHA = %d, missing = %d",
        (props.nameExists("alpha") ? 1 : 0),
        (props.nameExists("ALPHA") ? 1 : 0),
        (props.nameExists("missing") ? 1 : 0));
    outf("indexOf beta = %d, indexOf BeTa = %d, indexOf missing = %d",
        props.indexOf("beta"), props.indexOf("BeTa"), props.indexOf("missing"));

    outf("item 1: name=[%s] value=[%s]",
        props.getItem(1).name.c_str(), props.getItem(1).value.c_str());

    // Re-adding an existing name replaces rather than appending, and the
    // comparison is case-insensitive, so the stored spelling is worth pinning.
    props.add("ALPHA", "9");
    outf("after re-add: count=%d propString=[%s]",
        props.getCount(), props.getPropString().c_str());

    outf("remove beta = %d", (props.remove("beta") ? 1 : 0));
    outf("remove missing = %d", (props.remove("missing") ? 1 : 0));
    outf("after removes: [%s]", props.getPropString().c_str());

    props.clear();
    outf("after clear: count=%d", props.getCount());
}

DRV_CASE(containers_property_list_subscript)
{
    PropertyList props;

    // operator[] creates the entry when it is missing, which means a read
    // through it is also a write.
    props["one"] = "1";
    outf("after write: count=%d [%s]", props.getCount(), props.getPropString().c_str());

    string read = props["absent"];
    outf("read of absent: value=[%s] count now %d", read.c_str(), props.getCount());

    props["ONE"] = "uno";
    outf("case-insensitive write: count=%d [%s]",
        props.getCount(), props.getPropString().c_str());
}

DRV_CASE(containers_property_list_quoting)
{
    // The property string format is name=value pairs separated by commas, with
    // values that contain a reserved character quoted. Round-tripping values
    // that contain the separators is the whole point of the format.
    static const char *VALUES[] =
    {
        "plain",
        "has space",
        "has,comma",
        "has=equals",
        "has\"quote",
        "has\"two\"quotes",
        ",leading-comma",
        "trailing-comma,",
        "",
        " ",
        "\t tab \t",
        "a\nnewline"
    };

    for (int i = 0; i < DRV_COUNT(VALUES); i++)
    {
        PropertyList props;
        props.add("k", VALUES[i]);
        string encoded = props.getPropString();

        PropertyList back;
        back.setPropString(encoded);
        string decoded;
        bool found = back.getValue("k", decoded);

        outf("in=[%s] enc=[%s] found=%d out=[%s] same=%d",
            esc(VALUES[i]).c_str(), esc(encoded).c_str(),
            (found ? 1 : 0), esc(decoded).c_str(),
            (found && decoded == VALUES[i] ? 1 : 0));
    }
}

DRV_CASE(containers_property_list_malformed_input)
{
    // setPropString against input the encoder would never have produced.
    static const char *INPUTS[] =
    {
        "",
        ",",
        ",,,",
        "=",
        "a",
        "a=",
        "=b",
        "a=b",
        "a=b,c=d",
        "a=b,,c=d",
        " a = b ",
        "a=b=c",
        "a=\"unterminated",
        "a=\"quoted,value\",b=2",
        "a=\"\"\"\"",
        "dup=1,dup=2",
        "DUP=1,dup=2"
    };

    for (int i = 0; i < DRV_COUNT(INPUTS); i++)
    {
        PropertyList props;
        try
        {
            props.setPropString(INPUTS[i]);
            outf("in=[%s] count=%d re-encoded=[%s]",
                esc(INPUTS[i]).c_str(), props.getCount(),
                esc(props.getPropString()).c_str());
        }
        catch (Exception& e)
        {
            outf("in=[%s] throw: %s", esc(INPUTS[i]).c_str(), e.getErrorMessage().c_str());
        }
    }
}

DRV_CASE(containers_property_list_reserved_names)
{
    // A name containing the name/value separator would make the encoding
    // ambiguous. Whether that is refused, accepted, or silently mangled is a
    // property of this implementation.
    static const char *NAMES[] = { "a=b", "a,b", "a\"b", "", " ", "a b" };

    for (int i = 0; i < DRV_COUNT(NAMES); i++)
    {
        PropertyList props;
        try
        {
            props.add(NAMES[i], "v");
            outf("name=[%s] count=%d enc=[%s]",
                esc(NAMES[i]).c_str(), props.getCount(),
                esc(props.getPropString()).c_str());
        }
        catch (Exception& e)
        {
            outf("name=[%s] throw: %s", esc(NAMES[i]).c_str(), e.getErrorMessage().c_str());
        }
    }

    // Copying must be deep: clearing the source must not empty the copy.
    PropertyList source;
    source.add("x", "1");
    source.add("y", "2");
    PropertyList copy(source);
    PropertyList assigned;
    assigned = source;
    source.clear();
    outf("copy=[%s] assigned=[%s] source=[%s]",
        copy.getPropString().c_str(), assigned.getPropString().c_str(),
        source.getPropString().c_str());
}

///////////////////////////////////////////////////////////////////////////////
// StrList

DRV_CASE(containers_strlist_basics)
{
    StrList list;
    outf("empty: count=%d capacity=%d isEmpty=%d",
        list.getCount(), list.getCapacity(), (list.isEmpty() ? 1 : 0));

    outf("add -> %d", list.add("banana"));
    outf("add -> %d", list.add("apple"));
    outf("add -> %d", list.add("cherry"));
    outf("contents: [%s]", dumpStrings(list).c_str());

    outf("indexOf apple = %d, APPLE = %d, missing = %d",
        list.indexOf("apple"), list.indexOf("APPLE"), list.indexOf("missing"));
    outf("exists cherry = %d", (list.exists("cherry") ? 1 : 0));

    list.insert(1, "date");
    outf("after insert: [%s]", dumpStrings(list).c_str());

    list.exchange(0, 3);
    outf("after exchange 0<->3: [%s]", dumpStrings(list).c_str());

    list.move(0, 2);
    outf("after move 0->2: [%s]", dumpStrings(list).c_str());

    list.setString(0, "replaced");
    outf("after setString: [%s]", dumpStrings(list).c_str());

    list.del(1);
    outf("after del 1: [%s] count=%d", dumpStrings(list).c_str(), list.getCount());

    list.clear();
    outf("after clear: count=%d", list.getCount());
}

DRV_CASE(containers_strlist_sorting)
{
    StrList list;
    list.add("Banana");
    list.add("apple");
    list.add("Cherry");
    list.add("apple");
    list.add("date");

    outf("unsorted: [%s]", dumpStrings(list).c_str());

    // Default comparison is case-insensitive, so the sort order of mixed-case
    // entries is not the byte order.
    outf("caseSensitive default = %d", (list.getCaseSensitive() ? 1 : 0));
    list.sort();
    outf("sorted (default): [%s]", dumpStrings(list).c_str());

    StrList cs;
    cs.setCaseSensitive(true);
    cs.add("Banana");
    cs.add("apple");
    cs.add("Cherry");
    cs.add("date");
    cs.sort();
    outf("sorted (case sensitive): [%s]", dumpStrings(cs).c_str());

    // setSorted(true) keeps the list ordered as it is added to, and changes
    // where add() puts a new string.
    StrList kept;
    kept.setSorted(true);
    outf("add to sorted list: %d", kept.add("m"));
    outf("add to sorted list: %d", kept.add("c"));
    outf("add to sorted list: %d", kept.add("z"));
    outf("add to sorted list: %d", kept.add("a"));
    outf("kept sorted: [%s]", dumpStrings(kept).c_str());

    // find() is only meaningful on a sorted list; on an unsorted one it is
    // still callable, and what it reports is worth pinning.
    int index = -1;
    bool found = kept.find("c", index);
    outf("find c in sorted = %d at %d", (found ? 1 : 0), index);
    index = -1;
    found = kept.find("q", index);
    outf("find q in sorted = %d at %d", (found ? 1 : 0), index);
    index = -1;
    found = list.find("date", index);
    outf("find in unsorted = %d at %d", (found ? 1 : 0), index);
}

DRV_CASE(containers_strlist_duplicates)
{
    // The three duplicate policies, each on a sorted list where duplicates are
    // detectable.
    static const int MODES[] =
        { DRV_CLASS_ENUM_INT(StrList, DUPLICATE_MODE, DM_IGNORE),
          DRV_CLASS_ENUM_INT(StrList, DUPLICATE_MODE, DM_ACCEPT),
          DRV_CLASS_ENUM_INT(StrList, DUPLICATE_MODE, DM_ERROR) };
    static const char *NAMES[] = { "DM_IGNORE", "DM_ACCEPT", "DM_ERROR" };

    for (int m = 0; m < DRV_COUNT(NAMES); m++)
    {
        StrList list;
        list.setSorted(true);
        list.setDupMode(static_cast<StrList::DUPLICATE_MODE>(MODES[m]));
        list.add("b");
        list.add("a");
        try
        {
            int index = list.add("a");
            outf("%s: second add -> %d, contents [%s]",
                NAMES[m], index, dumpStrings(list).c_str());
        }
        catch (Exception& e)
        {
            outf("%s: throw: %s", NAMES[m], e.getErrorMessage().c_str());
        }
    }

    // On an unsorted list the policy has nothing to detect, so duplicates land
    // regardless of the mode.
    StrList unsorted;
    unsorted.setDupMode(DRV_CLASS_ENUM(StrList, DUPLICATE_MODE, DM_IGNORE));
    unsorted.add("a");
    unsorted.add("a");
    outf("unsorted with DM_IGNORE: [%s]", dumpStrings(unsorted).c_str());
}

DRV_CASE(containers_strlist_data_pointers)
{
    StrList list;
    list.add("one", tok(11));
    list.add("two", tok(22));
    list.insert(1, "onehalf", tok(15));

    for (int i = 0; i < list.getCount(); i++)
        outf("[%d] %s -> %d", i, list[i].c_str(), untok(list.getData(i)));

    outf("indexOfData 22 = %d, missing = %d",
        list.indexOfData(tok(22)), list.indexOfData(tok(99)));

    list.setData(0, tok(111));
    outf("after setData: [0] -> %d", untok(list.getData(0)));

    // add() without data leaves it NULL.
    list.add("three");
    outf("no data -> %d", untok(list.getData(list.getCount() - 1)));

    // Exchanging must carry the data with the string.
    list.exchange(0, 1);
    outf("after exchange: [0]=%s->%d [1]=%s->%d",
        list[0].c_str(), untok(list.getData(0)),
        list[1].c_str(), untok(list.getData(1)));

    // Sorting moves the data too, which is easy to get wrong.
    StrList s;
    s.add("c", tok(3));
    s.add("a", tok(1));
    s.add("b", tok(2));
    s.sort();
    for (int i = 0; i < s.getCount(); i++)
        outf("sorted [%d] %s -> %d", i, s[i].c_str(), untok(s.getData(i)));
}

DRV_CASE(containers_strlist_out_of_range)
{
    StrList list;
    list.add("only");

    static const int INDICES[] = { -1, 1, 100 };
    for (int i = 0; i < DRV_COUNT(INDICES); i++)
    {
        try
        {
            outf("getString(%d) = [%s]", INDICES[i], list.getString(INDICES[i]).c_str());
        }
        catch (Exception& e)
        {
            outf("getString(%d) throw: %s", INDICES[i], e.getErrorMessage().c_str());
        }
    }

    for (int i = 0; i < DRV_COUNT(INDICES); i++)
    {
        try
        {
            list.del(INDICES[i]);
            outf("del(%d) ok, count now %d", INDICES[i], list.getCount());
        }
        catch (Exception& e)
        {
            outf("del(%d) throw: %s", INDICES[i], e.getErrorMessage().c_str());
        }
    }

    try
    {
        list.insert(-1, "x");
        outf("insert(-1) ok, count now %d", list.getCount());
    }
    catch (Exception& e)
    {
        outf("insert(-1) throw: %s", e.getErrorMessage().c_str());
    }

    try
    {
        list.exchange(0, 50);
        outf("exchange(0,50) ok");
    }
    catch (Exception& e)
    {
        outf("exchange(0,50) throw: %s", e.getErrorMessage().c_str());
    }
}

DRV_CASE(containers_strings_name_value)
{
    StrList list;
    list.add("host=localhost");
    list.add("port=8080");
    list.add("novalue");
    list.add("=orphan");
    list.add("a=b=c");

    for (int i = 0; i < list.getCount(); i++)
        outf("[%d] name=[%s] value=[%s]",
            i, list.getName(i).c_str(), list.getValue(i).c_str());

    outf("getValue(host) = [%s]", list.getValue("host").c_str());
    outf("getValue(HOST) = [%s]", list.getValue("HOST").c_str());
    outf("getValue(missing) = [%s]", list.getValue("missing").c_str());
    outf("indexOfName(port) = %d, missing = %d",
        list.indexOfName("port"), list.indexOfName("missing"));

    // Setting an existing name rewrites it in place; setting a new one appends.
    list.setValue("port", "9090");
    list.setValue("newkey", "newvalue");
    outf("after setValue: [%s]", dumpStrings(list).c_str());

    // Setting a name to the empty string removes the entry rather than storing
    // an empty value, which is the behaviour a caller is most likely to trip on.
    list.setValue("host", "");
    outf("after empty setValue: [%s]", dumpStrings(list).c_str());

    outf("combineNameValue = [%s]", list.combineNameValue("k", "v").c_str());

    // A different separator changes all of the above.
    StrList colon;
    colon.setNameValueSeparator(':');
    colon.add("host:localhost");
    colon.add("port=8080");
    outf("colon sep: name0=[%s] value0=[%s] name1=[%s] value1=[%s]",
        colon.getName(0).c_str(), colon.getValue(0).c_str(),
        colon.getName(1).c_str(), colon.getValue(1).c_str());
}

DRV_CASE(containers_strings_text_roundtrip)
{
    StrList list;
    list.add("first");
    list.add("");
    list.add("third line");

    outf("getText = [%s]", esc(list.getText()).c_str());
    outf("lineBreak = [%s]", esc(list.getLineBreak()).c_str());

    StrList back;
    back.setText(list.getText().c_str());
    outf("roundtrip count=%d [%s]", back.getCount(), dumpStrings(back).c_str());

    // A custom line break changes both directions.
    StrList custom;
    custom.setLineBreak("<>");
    custom.add("a");
    custom.add("b");
    outf("custom getText = [%s]", esc(custom.getText()).c_str());

    StrList customBack;
    customBack.setLineBreak("<>");
    customBack.setText("a<>b<>c");
    outf("custom setText count=%d [%s]",
        customBack.getCount(), dumpStrings(customBack).c_str());

    // setText against input with the other platform's line ending, with a
    // trailing break, and with none at all.
    static const char *TEXTS[] = { "a\nb\n", "a\nb", "a\r\nb\r\n", "\n", "", "\n\n\n" };
    for (int i = 0; i < DRV_COUNT(TEXTS); i++)
    {
        StrList t;
        t.setText(TEXTS[i]);
        outf("setText [%s] -> count=%d [%s]",
            esc(TEXTS[i]).c_str(), t.getCount(), esc(dumpStrings(t)).c_str());
    }
}

DRV_CASE(containers_strings_comma_text)
{
    StrList list;
    list.add("plain");
    list.add("has space");
    list.add("has,comma");
    list.add("has\"quote");
    list.add("");

    string encoded = list.getCommaText();
    outf("getCommaText = [%s]", esc(encoded).c_str());

    StrList back;
    back.setCommaText(encoded.c_str());
    outf("roundtrip count=%d [%s]", back.getCount(), esc(dumpStrings(back)).c_str());

    // A single empty string is the special case in getDelimitedText, and what
    // the baseline produces for it is not what the format calls for: the
    // implementation writes string(getQuoteChar(), 2), which builds N copies of
    // byte 2 rather than two copies of the quote character. Recorded as it is.
    StrList single;
    single.add("");
    outf("single empty: [%s]", esc(single.getCommaText()).c_str());
    outf("single empty length: %d", static_cast<int>(single.getCommaText().length()));

    StrList empty;
    outf("no elements: [%s]", esc(empty.getCommaText()).c_str());

    // A custom delimiter and quote character.
    StrList custom;
    custom.setDelimiter(';');
    custom.setQuoteChar('\'');
    custom.add("a;b");
    custom.add("c'd");
    outf("custom delimited = [%s]", esc(custom.getDelimitedText()).c_str());

    StrList customBack;
    customBack.setDelimiter(';');
    customBack.setQuoteChar('\'');
    customBack.setDelimitedText(custom.getDelimitedText().c_str());
    outf("custom roundtrip count=%d [%s]",
        customBack.getCount(), esc(dumpStrings(customBack)).c_str());

    static const char *INPUTS[] =
    {
        "", ",", "a,b", "a, b", " a , b ", "\"a,b\",c",
        "\"unterminated", "a,,b", ",a", "a,"
    };
    for (int i = 0; i < DRV_COUNT(INPUTS); i++)
    {
        StrList t;
        try
        {
            t.setCommaText(INPUTS[i]);
            outf("setCommaText [%s] -> count=%d [%s]",
                esc(INPUTS[i]).c_str(), t.getCount(), esc(dumpStrings(t)).c_str());
        }
        catch (Exception& e)
        {
            outf("setCommaText [%s] throw: %s",
                esc(INPUTS[i]).c_str(), e.getErrorMessage().c_str());
        }
    }
}

DRV_CASE(containers_strings_copy_and_equals)
{
    StrList source;
    source.add("a", tok(1));
    source.add("b", tok(2));
    source.setCaseSensitive(true);
    source.setDelimiter(';');

    StrList copyCtor(source);
    outf("copy-ctor: [%s] data0=%d caseSensitive=%d delimiter=[%c]",
        dumpStrings(copyCtor).c_str(), untok(copyCtor.getData(0)),
        (copyCtor.getCaseSensitive() ? 1 : 0), copyCtor.getDelimiter());

    StrList assigned;
    assigned = source;
    outf("assigned: [%s] data1=%d", dumpStrings(assigned).c_str(),
        untok(assigned.getData(1)));

    outf("equals self = %d, equals copy = %d",
        (source.equals(source) ? 1 : 0), (source.equals(assigned) ? 1 : 0));

    assigned.add("c");
    outf("equals after divergence = %d", (source.equals(assigned) ? 1 : 0));

    StrList reordered;
    reordered.add("b");
    reordered.add("a");
    outf("equals when reordered = %d", (source.equals(reordered) ? 1 : 0));

    // addStrings appends rather than replacing.
    StrList target;
    target.add("x");
    target.addStrings(source);
    outf("addStrings: [%s]", dumpStrings(target).c_str());

    // Clearing the source must not disturb a copy taken earlier.
    source.clear();
    outf("copy after source clear: [%s]", dumpStrings(copyCtor).c_str());
}

DRV_CASE(containers_strings_file_roundtrip)
{
    std::string path = drv::tmpPath("strings.txt");

    StrList list;
    list.add("line one");
    list.add("");
    list.add("line three");

    outf("saveToFile = %d", (list.saveToFile(path.c_str()) ? 1 : 0));
    outf("bytes on disk = %d", static_cast<int>(getFileSize(path)));

    StrList back;
    outf("loadFromFile = %d", (back.loadFromFile(path.c_str()) ? 1 : 0));
    outf("count=%d [%s]", back.getCount(), esc(dumpStrings(back)).c_str());

    StrList missing;
    missing.add("stale");
    bool ok = missing.loadFromFile(drv::tmpPath("nope.txt").c_str());
    outf("load missing = %d, count now %d", (ok ? 1 : 0), missing.getCount());

    // Through a stream rather than a file name.
    MemoryStream stream;
    outf("saveToStream = %d", (list.saveToStream(stream) ? 1 : 0));
    outf("stream size = %d", static_cast<int>(stream.getSize()));
    stream.setPosition(0);
    StrList fromStream;
    outf("loadFromStream = %d", (fromStream.loadFromStream(stream) ? 1 : 0));
    outf("from stream count=%d [%s]",
        fromStream.getCount(), esc(dumpStrings(fromStream)).c_str());
}
