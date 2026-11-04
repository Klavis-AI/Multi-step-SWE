///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for Url and Packet.
//
// Packet's read/write primitives are protected, so the cases reach them
// through a concrete subclass, which is also how a caller would.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <string>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

namespace {

std::string dumpUrl(const Url& url)
{
    std::string result;
    result += "proto=[" + url.getProtocol() + "]";
    result += " user=[" + url.getUserName() + "]";
    result += " pass=[" + url.getPassword() + "]";
    result += " host=[" + url.getHost() + "]";
    result += " port=[" + url.getPort() + "]";
    result += " path=[" + url.getPath() + "]";
    result += " file=[" + url.getFileName() + "]";
    result += " params=[" + url.getParams() + "]";
    result += " mark=[" + url.getBookmark() + "]";
    return result;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// Url

DRV_CASE(url_parse_well_formed)
{
    static const char *URLS[] =
    {
        "http://www.example.com/",
        "http://www.example.com/index.html",
        "http://www.example.com:8080/a/b/c.html",
        "https://user:secret@host.example.org:443/deep/path/file.txt?x=1&y=2#frag",
        "ftp://files.example.net/pub/",
        "http://example.com/a/b/",
        "http://example.com",
        "http://example.com?query=only",
        "http://example.com#markonly",
        "http://[2001:db8::1]:8080/v6/path.html",
        "http://[::1]/",
        "/local/path/file.txt",
        "relative/file.txt",
        "file.txt"
    };

    for (int i = 0; i < DRV_COUNT(URLS); i++)
    {
        Url url(URLS[i]);
        outf("in=[%s]", URLS[i]);
        outf("  %s", dumpUrl(url).c_str());
        outf("  rebuilt=[%s] same=%d", url.getUrl().c_str(),
            (url.getUrl() == std::string(URLS[i]) ? 1 : 0));
    }
}

DRV_CASE(url_parse_malformed)
{
    // Input no encoder would produce. None of these may crash, and what each
    // one decomposes into is this implementation's answer.
    static const char *URLS[] =
    {
        "",
        "://",
        "http://",
        "http:///",
        "http://:8080/",
        "http://@host/",
        "http://:@host/",
        "http://user@host/",
        "http://user:pass:extra@host/",
        "http://host:/",
        "http://host:notaport/",
        "http://host:99999999/",
        "http://[unclosed/",
        "http://unopened]/",
        "http://[]/",
        "?",
        "#",
        "??a?b",
        "##a#b",
        "http://a://b/c",
        "HTTP://UPPER.EXAMPLE.COM/Path",
        "http://host/a?b#c?d",
        "http://host/a#b?c"
    };

    for (int i = 0; i < DRV_COUNT(URLS); i++)
    {
        Url url;
        url.setUrl(URLS[i]);
        outf("in=[%s]", esc(URLS[i]).c_str());
        outf("  %s", dumpUrl(url).c_str());
    }
}

DRV_CASE(url_parts_and_assembly)
{
    Url url("https://bob:pw@example.com:8443/dir/sub/page.html?a=1#top");

    // getUrl(parts) selects which components are rendered. Each flag on its
    // own, then a few combinations.
    struct { UINT mask; const char *name; } PARTS[] =
    {
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_PROTOCOL), "PROTOCOL" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_HOST), "HOST" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_PORT), "PORT" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_PATH), "PATH" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_FILENAME), "FILENAME" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_BOOKMARK), "BOOKMARK" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_USERNAME), "USERNAME" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_PASSWORD), "PASSWORD" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_PARAMS), "PARAMS" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_ALL), "ALL" },
        { 0, "NONE" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_HOST)
          | DRV_CLASS_ENUM_INT(Url, URL_PART, URL_PATH)
          | DRV_CLASS_ENUM_INT(Url, URL_PART, URL_FILENAME), "HOST|PATH|FILENAME" },
        { DRV_CLASS_ENUM_INT(Url, URL_PART, URL_PROTOCOL)
          | DRV_CLASS_ENUM_INT(Url, URL_PART, URL_HOST), "PROTOCOL|HOST" }
    };

    for (int i = 0; i < 13; i++)
        outf("%-20s -> [%s]", PARTS[i].name, url.getUrl(PARTS[i].mask).c_str());

    // Built up field by field rather than parsed.
    Url built;
    built.setProtocol("http");
    built.setHost("h");
    outf("host only: [%s]", built.getUrl().c_str());
    built.setPort("81");
    outf("with port: [%s]", built.getUrl().c_str());
    built.setPath("/p/");
    outf("with path: [%s]", built.getUrl().c_str());
    built.setFileName("f.txt");
    outf("with file: [%s]", built.getUrl().c_str());
    built.setParams("q=1");
    outf("with params: [%s]", built.getUrl().c_str());
    built.setBookmark("b");
    outf("with bookmark: [%s]", built.getUrl().c_str());
    built.setUserName("u");
    outf("with user: [%s]", built.getUrl().c_str());
    built.setPassword("p");
    outf("with password: [%s]", built.getUrl().c_str());

    // A password with no user name, which the parser refuses to produce.
    Url passOnly;
    passOnly.setProtocol("http");
    passOnly.setHost("h");
    passOnly.setPassword("p");
    outf("password without user: [%s]", passOnly.getUrl().c_str());

    Url copy(url);
    outf("copy: [%s]", copy.getUrl().c_str());
    Url assigned;
    assigned = url;
    assigned.clear();
    outf("cleared: [%s] original still [%s]",
        assigned.getUrl().c_str(), url.getUrl().c_str());
}

///////////////////////////////////////////////////////////////////////////////
// Packet

namespace {

// A packet with one field of every primitive kind, so a round trip exercises
// the whole read/write surface.
class SamplePacket : public Packet
{
public:
    SamplePacket() :
        i8(0), i16(0), i32(0), i64(0), flag(false) {}

    INT8 i8;
    INT16 i16;
    INT32 i32;
    INT64 i64;
    bool flag;
    string text;
    string blob;

protected:
    virtual void doPack()
    {
        writeINT8(i8);
        writeINT16(i16);
        writeINT32(i32);
        writeINT64(i64);
        writeBool(flag);
        writeString(text);
        writeBlob(const_cast<char*>(blob.data()), static_cast<int>(blob.size()));
    }

    virtual void doUnpack()
    {
        readINT8(i8);
        readINT16(i16);
        readINT32(i32);
        readINT64(i64);
        flag = readBool();
        readString(text);
        readBlob(blob);
    }
};

std::string describe(const SamplePacket& p)
{
    std::string result;
    result += "i8=" + intToStr(static_cast<INT64>(p.i8));
    result += " i16=" + intToStr(static_cast<INT64>(p.i16));
    result += " i32=" + intToStr(static_cast<INT64>(p.i32));
    result += " i64=" + intToStr(p.i64);
    result += " flag=" + std::string(p.flag ? "1" : "0");
    result += " text=[" + drv::esc(p.text) + "]";
    result += " blob=[" + drv::esc(p.blob) + "]";
    return result;
}

} // namespace

DRV_CASE(packet_roundtrip)
{
    SamplePacket source;
    source.i8 = -128;
    source.i16 = 32767;
    source.i32 = -2147483647 - 1;
    source.i64 = 9223372036854775807LL;
    source.flag = true;
    source.text = "hello \xE4\xB8\x96";
    source.blob.assign("bin\0ary", 7);

    outf("before pack: isPacked=%d size=%d",
        (source.IsPacked() ? 1 : 0), source.getSize());
    outf("pack -> %d", (source.pack() ? 1 : 0));
    outf("after pack: isPacked=%d size=%d",
        (source.IsPacked() ? 1 : 0), source.getSize());
    outf("wire bytes: [%s]", esc(source.getBuffer(), source.getSize()).c_str());

    SamplePacket target;
    bool ok = target.unpack(source.getBuffer(), source.getSize());
    outf("unpack -> %d, isAvailable=%d", (ok ? 1 : 0), (target.isAvailable() ? 1 : 0));
    outf("fields: %s", describe(target).c_str());
    outf("identical: %d", (describe(target) == describe(source) ? 1 : 0));

    // Through a Buffer rather than a raw pointer.
    Buffer wire(source.getBuffer(), source.getSize());
    SamplePacket viaBuffer;
    outf("unpack(Buffer) -> %d", (viaBuffer.unpack(wire) ? 1 : 0));
    outf("fields: %s", describe(viaBuffer).c_str());

    source.clear();
    outf("after clear: size=%d isPacked=%d",
        source.getSize(), (source.IsPacked() ? 1 : 0));
}

DRV_CASE(packet_defaults_and_empty)
{
    SamplePacket zero;
    bool packed = zero.pack();
    outf("pack defaults -> %d, size=%d", (packed ? 1 : 0), zero.getSize());
    outf("wire: [%s]", esc(zero.getBuffer(), zero.getSize()).c_str());

    SamplePacket back;
    outf("unpack -> %d", (back.unpack(zero.getBuffer(), zero.getSize()) ? 1 : 0));
    outf("fields: %s", describe(back).c_str());

    // ensurePacked() packs on demand and is idempotent.
    SamplePacket lazy;
    lazy.i32 = 7;
    lazy.ensurePacked();
    int firstSize = lazy.getSize();
    lazy.ensurePacked();
    outf("ensurePacked: size %d then %d, same=%d",
        firstSize, lazy.getSize(), (firstSize == lazy.getSize() ? 1 : 0));

    // The empty and NULL buffers live in packet_unpack_zero_length_buffer:
    // both reach a memmove the standard says may not take a null pointer, so
    // neither answer may be scored (NOTES B17).
    SamplePacket fromNegative;
    outf("unpack negative size -> %d",
        (fromNegative.unpack(const_cast<char*>("abc"), -1) ? 1 : 0));
}

// Kept for the coverage and for the reader: Packet::unpack(buffer, 0) is an
// ordinary call -- an empty packet -- and it reaches
// memmove(stream_->getMemory(), buffer, 0) with a destination that setSize(0)
// left null. Passing a null buffer puts a null in the other argument. Both are
// nonnull-attribute violations, which is why the answers are printed and not
// scored.
DRV_UNSCORED_CASE(packet_unpack_zero_length_buffer,
    "Packet::unpack with bytes==0 memmoves from/to a null pointer (B17)")
{
    SamplePacket fromEmpty;
    bool emptyOk = fromEmpty.unpack(const_cast<char*>(""), 0);
    outf("unpack empty -> %d, isAvailable=%d",
        (emptyOk ? 1 : 0), (fromEmpty.isAvailable() ? 1 : 0));

    SamplePacket fromNull;
    outf("unpack NULL -> %d", (fromNull.unpack(NULL, 0) ? 1 : 0));
}

DRV_CASE(packet_truncated_and_corrupt)
{
    SamplePacket source;
    source.i32 = 0x11223344;
    source.i64 = 0x5566778899AABBCCLL;
    source.text = "the quick brown fox";
    source.blob = "0123456789";
    source.pack();

    int full = source.getSize();
    outf("full size = %d", full);

    // Every prefix of the wire form, at a few interesting lengths: a packet
    // that is cut short must report failure rather than read past the end.
    // The empty prefix is not here: unpack(buffer, 0) reaches a memmove with a
    // null destination, so its answer is printed unscored by
    // packet_unpack_zero_length_buffer instead (NOTES B17).
    static const int CUTS[] = { 1, 2, 4, 8, 15, 16, 20, 30, 40 };
    for (int i = 0; i < DRV_COUNT(CUTS); i++)
    {
        if (CUTS[i] >= full) continue;
        SamplePacket target;
        bool ok = target.unpack(source.getBuffer(), CUTS[i]);
        outf("cut to %d -> %d, isAvailable=%d", CUTS[i], (ok ? 1 : 0),
            (target.isAvailable() ? 1 : 0));
    }

    // A length prefix claiming far more than the buffer holds is the classic
    // way to make a decoder allocate or read wildly.
    Buffer corrupt(source.getBuffer(), full);
    // The string's length prefix sits after the five fixed fields: 1 + 2 + 4 +
    // 8 + 1 bytes.
    int lengthOffset = 1 + 2 + 4 + 8 + 1;
    corrupt[lengthOffset + 0] = static_cast<char>(0xFF);
    corrupt[lengthOffset + 1] = static_cast<char>(0xFF);
    corrupt[lengthOffset + 2] = static_cast<char>(0xFF);
    corrupt[lengthOffset + 3] = static_cast<char>(0x7F);

    SamplePacket target;
    bool ok = target.unpack(corrupt.data(), full);
    outf("huge length prefix -> %d, isAvailable=%d",
        (ok ? 1 : 0), (target.isAvailable() ? 1 : 0));

    // A negative length prefix.
    Buffer negative(source.getBuffer(), full);
    negative[lengthOffset + 0] = static_cast<char>(0xFF);
    negative[lengthOffset + 1] = static_cast<char>(0xFF);
    negative[lengthOffset + 2] = static_cast<char>(0xFF);
    negative[lengthOffset + 3] = static_cast<char>(0xFF);

    SamplePacket fromNegative;
    ok = fromNegative.unpack(negative.data(), full);
    outf("negative length prefix -> %d, isAvailable=%d",
        (ok ? 1 : 0), (fromNegative.isAvailable() ? 1 : 0));
}

DRV_CASE(packet_string_and_blob_sizes)
{
    // Strings and blobs at the sizes that catch off-by-one and alignment
    // mistakes: empty, one byte, and either side of a kilobyte.
    static const int SIZES[] = { 0, 1, 2, 255, 256, 1023, 1024, 1025, 65535, 65536 };

    for (int i = 0; i < DRV_COUNT(SIZES); i++)
    {
        SamplePacket source;
        source.text.assign(SIZES[i], 'T');
        source.blob.assign(SIZES[i], '\xAB');
        source.pack();

        SamplePacket target;
        bool ok = target.unpack(source.getBuffer(), source.getSize());
        outf("size %d: wire=%d unpack=%d text=%d blob=%d intact=%d",
            SIZES[i], source.getSize(), (ok ? 1 : 0),
            static_cast<int>(target.text.size()),
            static_cast<int>(target.blob.size()),
            (target.text == source.text && target.blob == source.blob ? 1 : 0));
    }

    // Every byte value survives a blob round trip.
    SamplePacket all;
    std::string every;
    for (int i = 0; i < 256; i++)
        every += static_cast<char>(i);
    all.blob = every;
    all.pack();
    SamplePacket back;
    back.unpack(all.getBuffer(), all.getSize());
    outf("all 256 byte values intact: %d", (back.blob == every ? 1 : 0));
}
