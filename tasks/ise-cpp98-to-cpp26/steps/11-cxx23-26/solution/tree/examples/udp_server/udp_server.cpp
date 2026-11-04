///////////////////////////////////////////////////////////////////////////////

#include "udp_server.h"
#include "packet_defs.h"

#include <print>

//-----------------------------------------------------------------------------

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initialize()
{
    // nothing
}

//-----------------------------------------------------------------------------

void AppBusiness::finalize()
{
    string msg = std::format("{} stoped.", getAppExeName(false));
    std::println("{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::afterInit()
{
    string msg = std::format("{} started.", getAppExeName(false));
    std::println("\n{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::onInitFailed(Exception& /*e*/)
{
    string msg = std::format("fail to start {}.", getAppExeName(false));
    std::println("\n{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_UDP));
    options.setUdpServerPort(8000);
}

//-----------------------------------------------------------------------------

void AppBusiness::onRecvedUdpPacket(UdpWorkerThread& /*workerThread*/, int /*groupIndex*/, UdpPacket& packet)
{
    std::span<const std::byte> bytes = packet.getPacket();

    if (bytes.size() < sizeof(UdpPacketHeader))
    {
        logger().writeStr("Invalid packet.");
        return;
    }

    UINT actionCode = reinterpret_cast<const UdpPacketHeader*>(bytes.data())->actionCode;

    switch (actionCode)
    {
    case AC_HELLO:
        {
            HelloPacket reqPacket;
            if (reqPacket.unpack(Buffer(bytes.data(), static_cast<int>(bytes.size()))))
            {
                // 消息体是从线上读来的一段带长度的字节, 里面允许嵌 NUL。
                // 日志这一行历来就在第一个 NUL 处停住, 交给 c_str() 继续
                // 停在那里 -- 换成 string 会把 NUL 写进拼好的整行, 而
                // writeStr 又会在那里截断, 反而丢掉句末。
                logger().writeFmt("Received packet from <{}>: {}.",
                    packet.getPeerAddr().getDisplayStr(),
                    reqPacket.message.c_str());

                AckPacket ackPacket;
                ackPacket.initPacket(reqPacket.header.seqNumber, 12345);
                iseApp().udpServer().sendBuffer(
                    asBytes(ackPacket.getBuffer(), ackPacket.getSize()), packet.getPeerAddr());
            }
            break;
        }

    default:
        {
            logger().writeStr("Unknown packet.");
            break;
        }
    }
}
