///////////////////////////////////////////////////////////////////////////////
// The frozen declarations of ise/main, written down as code a compiler checks.
//
// This translation unit registers no cases and runs nothing. Every function
// below is a compile-time assertion: a pointer-to-member whose type is spelled
// out in full, initialised from the library's own declaration. If a parameter
// type, a return type or a public typedef moves, the initialisation is
// ill-formed and the driver does not build -- which is exactly what the
// instruction says must not happen to a caller written against today's
// headers.
//
// The behaviour suite already pins the declarations it calls. It calls the
// containers; it does not call a connection factory, a packet queue or a
// server-module list, because none of those can be reached without a network
// or a running application. Those are the declarations most likely to move
// under a milestone about ownership, and they are the ones this file is for.
//
// What is deliberately NOT pinned here. The instruction releases exactly three
// declarations from the freeze, and these are those three:
//
//   * `ObjectType*& getItem(int)` and `ObjectType*& operator[](int)` on the
//     object containers. A list whose elements are owned by handles has no
//     `ObjectType*` object to refer to, so they may hand out the pointer
//     itself instead of a reference to it.
//   * `TcpConnectionPtr` and `ConnContextPtr`. They name Boost's counted
//     handle today, and the instruction requires that family to be gone; the
//     standard counted handle takes their place under the same two names.
//   * `iseThrowDataAlgoException`. It throws a `new`-allocated exception by
//     pointer, which is an allocation the milestone does not let stand and no
//     handle can hold. A pointer-to-member cannot see what a function throws,
//     so nothing in this file would have pinned it in any case.
//
// Storage is not a declaration this file pins, and the instruction says so:
// private members, the private typedefs that name their types, and the
// private helpers that exist only to serve them are exactly where the
// ownership change is supposed to go. Every pin below is on a public or a
// protected declaration.
//
// Deliberately written in the subset both -std=gnu++98 and -std=c++26 accept,
// like the rest of the driver.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"
#include "ise/main/ise_server_tcp.h"
#include "ise/main/ise_server_udp.h"

#include "boost/type_traits/has_virtual_destructor.hpp"

using namespace ise;

namespace {

// A compile-time assertion that works in every dialect this driver is built
// in: a zero-or-negative array bound is ill-formed everywhere.
#define DRV_PIN_ASSERT(tag, expr) typedef char PinAssert_##tag[(expr) ? 1 : -1]

///////////////////////////////////////////////////////////////////////////////
// The connection factories.
//
// `createConnection` and `acceptConnection` are protected virtuals -- the
// extension points a user of this library overrides. A signature that moves
// under them is worse than a compile error at the call site: in C++98 an
// override that no longer matches is a new function, it compiles, and the
// user's connection type silently stops being created. Each is taken through
// a derived class, which is the only place the language allows a protected
// member's address to be formed, and which is also the position a real caller
// is in.

struct PinBaseTcpClient : public BaseTcpClient
{
    static void pin()
    {
        BaseTcpConnection* (PinBaseTcpClient::*create)() =
            &PinBaseTcpClient::createConnection;
        (void)create;
    }
};

struct PinBaseTcpServer : public BaseTcpServer
{
    static void pin()
    {
        BaseTcpConnection* (PinBaseTcpServer::*create)(SOCKET) =
            &PinBaseTcpServer::createConnection;
        void (PinBaseTcpServer::*accept)(BaseTcpConnection*) =
            &PinBaseTcpServer::acceptConnection;
        (void)create;
        (void)accept;
    }
};

struct PinTcpClient : public TcpClient
{
    static void pin()
    {
        BaseTcpConnection* (PinTcpClient::*create)() =
            &PinTcpClient::createConnection;
        (void)create;
    }
};

struct PinTcpServer : public TcpServer
{
    static void pin()
    {
        BaseTcpConnection* (PinTcpServer::*create)(SOCKET) =
            &PinTcpServer::createConnection;
        void (PinTcpServer::*accept)(BaseTcpConnection*) =
            &PinTcpServer::acceptConnection;
        (void)create;
        (void)accept;
    }
};

struct PinHttpTcpClient : public HttpTcpClient
{
    static void pin()
    {
        BaseTcpConnection* (PinHttpTcpClient::*create)() =
            &PinHttpTcpClient::createConnection;
        (void)create;
    }
};

///////////////////////////////////////////////////////////////////////////////
// The two server callbacks.
//
// These are public typedefs, and a typedef is the one place a signature can
// move without any declaration appearing to change: `setCreateConnCallback`
// reads the same in both trees. Handing each one a function with the pinned
// parameter list is what makes the typedef's own shape the thing being
// checked.

void pinnedCreateConn(BaseTcpServer* /*tcpServer*/, SOCKET /*socketHandle*/,
    BaseTcpConnection*& /*connection*/)
{
}

void pinnedAcceptConn(BaseTcpServer* /*tcpServer*/,
    BaseTcpConnection* /*connection*/)
{
}

void pinServerCallbacks(BaseTcpServer& server)
{
    BaseTcpServer::TcpSvrCreateConnCallback onCreate = &pinnedCreateConn;
    BaseTcpServer::TcpSvrAcceptConnCallback onAccept = &pinnedAcceptConn;
    server.setCreateConnCallback(onCreate);
    server.setAcceptConnCallback(onAccept);
}

///////////////////////////////////////////////////////////////////////////////
// The event-loop registration and the UDP request queue.
//
// Both are public, so both are taken directly. `extractPacket` is the
// declaration this milestone is most likely to want to change -- handing
// ownership back to the caller is exactly what a handle return type is for --
// and it is the declaration the instruction keeps.

void pinEventLoopList()
{
    bool (TcpEventLoopList::*reg)(BaseTcpConnection*, int) =
        &TcpEventLoopList::registerToEventLoop;
    (void)reg;
}

void pinUdpRequestQueue()
{
    void (UdpRequestQueue::*add)(UdpPacket*) = &UdpRequestQueue::addPacket;
    UdpPacket* (UdpRequestQueue::*extract)() = &UdpRequestQueue::extractPacket;
    (void)add;
    (void)extract;
}

///////////////////////////////////////////////////////////////////////////////
// The server-module list.
//
// `IseServerModuleList` is a public typedef that appears in the signature of
// `createServerModules`, which every application built on this library
// overrides -- the fifteen example programs in this tree among them. The
// `push_back` is the pin that matters: it is the line every one of those
// overrides is written as, and it stops compiling the moment the element type
// stops being a raw pointer.

struct PinSvrModBusiness : public IseSvrModBusiness
{
    static void pin()
    {
        void (PinSvrModBusiness::*create)(IseServerModuleList&) =
            &PinSvrModBusiness::createServerModules;
        (void)create;
    }
};

void pinServerModuleList()
{
    IseServerModuleList list;
    IseServerModule* module = NULL;
    list.push_back(module);

    void (IseServerModuleMgr::*init)(const IseServerModuleList&) =
        &IseServerModuleMgr::initServerModuleList;
    (void)init;
}

///////////////////////////////////////////////////////////////////////////////
// The object containers.
//
// The behaviour suite drives these through `ObjectList<T>`, so their answers
// are already pinned; what it cannot state is the shape of the declarations
// that produced those answers. `add` still takes a raw pointer with an
// allow-duplicate flag, `extract` still hands a raw pointer back, `del` still
// destroys. The member-pointer types name `CustomObjectList`, because that is
// where the members are declared and what `ObjectList`'s using-declarations
// re-export.

class PinObject
{
public:
    virtual ~PinObject() {}
};

void pinObjectList()
{
    typedef CustomObjectList<PinObject> Base;

    int (Base::*add)(PinObject*, bool) = &ObjectList<PinObject>::add;
    int (Base::*remove)(PinObject*) = &ObjectList<PinObject>::remove;
    PinObject* (Base::*extractItem)(PinObject*) = &ObjectList<PinObject>::extract;
    PinObject* (Base::*extractAt)(int) = &ObjectList<PinObject>::extract;
    void (Base::*del)(int) = &ObjectList<PinObject>::del;
    void (Base::*insert)(int, PinObject*) = &ObjectList<PinObject>::insert;
    int (Base::*indexOf)(PinObject*) const = &ObjectList<PinObject>::indexOf;
    void (Base::*setOwns)(bool) = &ObjectList<PinObject>::setOwnsObjects;

    (void)add;
    (void)remove;
    (void)extractItem;
    (void)extractAt;
    (void)del;
    (void)insert;
    (void)indexOf;
    (void)setOwns;
}

void pinPointerList()
{
    int (PointerList::*add)(POINTER) = &PointerList::add;
    void (PointerList::*insert)(int, POINTER) = &PointerList::insert;
    void (PointerList::*del)(int) = &PointerList::del;
    int (PointerList::*remove)(POINTER) = &PointerList::remove;
    POINTER (PointerList::*extract)(POINTER) = &PointerList::extract;

    (void)add;
    (void)insert;
    (void)del;
    (void)remove;
    (void)extract;
}

///////////////////////////////////////////////////////////////////////////////
// Deleting an HTTP response through a pointer to one of its bases.
//
// This pins the property, not the declaration, and the difference is worth
// writing down. `boost::has_virtual_destructor<HttpResponse>` stays true if
// `virtual ~HttpResponse();` is removed from `HttpResponse` itself, because
// `HttpEntityHeaderInfo` declares one two levels up and `HttpResponse`
// inherits it -- so this assertion does not catch that removal on its own.
// What it does catch is the rewrite that reaches for value semantics and
// takes the virtual destructor out of the base as well. Nothing in this tree
// derives from `HttpResponse`, so nothing here would notice; a user who
// deletes their own subclass through a `HttpEntityHeaderInfo*` gets undefined
// behaviour with no diagnostic anywhere.

DRV_PIN_ASSERT(HttpResponseVirtualDtor,
    boost::has_virtual_destructor<HttpResponse>::value);

///////////////////////////////////////////////////////////////////////////////
// The content stream of an HTTP response: borrowed or owned, decided at the
// call, with a raw pointer either way.

void pinHttpResponse(HttpResponse& response, Stream* stream)
{
    void (HttpResponse::*setStream)(Stream*, bool) =
        &HttpResponse::setContentStream;
    Stream* (HttpResponse::*getStream)() const =
        &HttpResponse::getContentStream;
    (void)setStream;
    (void)getStream;
    response.setContentStream(stream, false);
}

} // namespace

namespace {

// Nothing calls these; naming them once keeps a compiler from warning that a
// static member function or a file-scope function is unused, and keeps the
// pins in one list a reader can count.
void (*const PINS[])() =
{
    &PinBaseTcpClient::pin,
    &PinBaseTcpServer::pin,
    &PinTcpClient::pin,
    &PinTcpServer::pin,
    &PinHttpTcpClient::pin,
    &PinSvrModBusiness::pin,
    &pinEventLoopList,
    &pinUdpRequestQueue,
    &pinServerModuleList,
    &pinObjectList,
    &pinPointerList
};

void (*const UNUSED_PINS)() = PINS[0];

} // namespace

namespace drv {

// Referenced by nothing; exists so the two pins that need an argument are not
// dead code the compiler may warn about.
void apiPinNeverCalled(BaseTcpServer& server, HttpResponse& response,
    Stream* stream)
{
    pinServerCallbacks(server);
    pinHttpResponse(response, stream);
    (void)UNUSED_PINS;
}

} // namespace drv
