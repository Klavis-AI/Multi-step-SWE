/****************************************************************************\
*                                                                            *
*  ISE (Iris Server Engine) Project                                          *
*  http://github.com/haoxingeng/ise                                          *
*                                                                            *
*  Copyright 2013 HaoXinGeng (haoxingeng@gmail.com)                          *
*  All rights reserved.                                                      *
*                                                                            *
*  Licensed under the Apache License, Version 2.0 (the "License");           *
*  you may not use this file except in compliance with the License.          *
*  You may obtain a copy of the License at                                   *
*                                                                            *
*      http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                            *
*  Unless required by applicable law or agreed to in writing, software       *
*  distributed under the License is distributed on an "AS IS" BASIS,         *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
*  See the License for the specific language governing permissions and       *
*  limitations under the License.                                            *
*                                                                            *
\****************************************************************************/

///////////////////////////////////////////////////////////////////////////////
// 文件名称: ise_inspector.cpp
// 功能描述: 服务器状态监视器
///////////////////////////////////////////////////////////////////////////////

#include "ise/main/ise_inspector.h"
#include "ise/main/ise_sys_utils.h"

#ifdef ISE_WINDOWS
#include "Psapi.h"
#pragma comment (lib, "psapi.lib")
#endif

using namespace ise;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// class ServerInspector

IseServerInspector::IseServerInspector(int serverPort) :
    serverPort_(serverPort),
    predefinedInspector_(new PredefinedInspector())
{
    httpServer_.setHttpSessionCallback(
        [this](const HttpRequest& request, HttpResponse& response)
        { onHttpSession(request, response); });
    add(predefinedInspector_->getItems());
}

//-----------------------------------------------------------------------------

void IseServerInspector::add(const string& category, const string& command,
    const OutputCallback& outputCallback, const string& help)
{
    AutoLocker locker(mutex_);

    CommandItem item(category, command, outputCallback, help);
    inspectInfo_[category][command] = item;
}

//-----------------------------------------------------------------------------

void IseServerInspector::add(const CommandItems& items)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const CommandItem& item = items[i];
        add(item.category, item.command, item.outputCallback, item.help);
    }
}

//-----------------------------------------------------------------------------

void IseServerInspector::getTcpServerOptions(int /*serverIndex*/, TcpServerOptions& options)
{
    options.serverPort = serverPort_;
    options.eventLoopCount = 1;
}

//-----------------------------------------------------------------------------

void IseServerInspector::onTcpConnected(const TcpConnectionPtr& connection)
{
    httpServer_.onTcpConnected(connection);
}

//-----------------------------------------------------------------------------

void IseServerInspector::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    httpServer_.onTcpDisconnected(connection);
}

//-----------------------------------------------------------------------------

void IseServerInspector::onTcpRecvComplete(const TcpConnectionPtr& connection,
    std::span<const std::byte> packet, const Context& context)
{
    httpServer_.onTcpRecvComplete(connection, packet, context);
}

//-----------------------------------------------------------------------------

void IseServerInspector::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& context)
{
    httpServer_.onTcpSendComplete(connection, context);
}

//-----------------------------------------------------------------------------

string IseServerInspector::outputHelpPage()
{
    string result;

    for (auto& [category, commandList] : inspectInfo_)
    {
        for (auto& [command, commandItem] : commandList)
        {

            string help;
            if (!commandItem.help.empty())
                help = std::format("<span style=\"color:#cfcfcf\">({})</span>", commandItem.help);

            string path = std::format("/{}/{}", category, command);
            string line = std::format("<a href=\"{}\">[{}]</a> {}", path, path, help);
            if (!result.empty()) result += "</br>";
            result += line;
        }

        result += "</br>";
    }

    return result;
}

//-----------------------------------------------------------------------------

// Example: /category/command?arg1=value&arg2=value
bool IseServerInspector::parseCommandUrl(const HttpRequest& request,
    string& category, string& command, PropertyList& argList)
{
    bool result = false;
    string url = request.getUrl();
    string argStr;
    StrList strList;

    if (!url.empty() && url[0] == '/')
        url.erase(0, 1);

    // find the splitter char ('?')
    string::size_type argPos = url.find('?');
    bool hasArgs = (argPos != string::npos);
    if (hasArgs)
    {
        argStr = url.substr(argPos + 1);
        url.erase(argPos);
    }

    // parse the string before the '?'
    splitString(url, '/', strList, true);
    if (strList.getCount() >= 2)
    {
        category = strList[0];
        command = strList[1];
        result = true;
    }

    // parse the args
    if (result)
    {
        argList.clear();
        splitString(argStr, '&', strList, true);
        for (int i = 0; i < strList.getCount(); ++i)
        {
            StrList parts;
            splitString(strList[i], '=', parts, true);
            if (parts.getCount() == 2 && !parts[0].empty())
            {
                argList.add(parts[0], parts[1]);
            }
        }
    }

    return result;
}

//-----------------------------------------------------------------------------

IseServerInspector::CommandItem* IseServerInspector::findCommandItem(
    const string& category, const string& command)
{
    CommandItem *result = nullptr;

    if (auto i = inspectInfo_.find(category); i != inspectInfo_.end())
    {
        CommandList& commandList = i->second;
        if (auto j = commandList.find(command); j != commandList.end())
            result = &j->second;
    }

    return result;
}

//-----------------------------------------------------------------------------

void IseServerInspector::onHttpSession(const HttpRequest& request, HttpResponse& response)
{
    response.setCacheControl("no-cache");
    response.setPragma(response.getCacheControl());
    response.setContentType("text/plain");

    if (request.getUrl() == "/")
    {
        AutoLocker locker(mutex_);
        string s = outputHelpPage();

        response.setStatusCode(200);
        response.setContentType("text/html");
        response.getContentStream()->write(s.c_str(), static_cast<int>(s.length()));
    }
    else
    {
        AutoLocker locker(mutex_);
        string category, command;
        CommandItem *commandItem;
        PropertyList argList;

        if (parseCommandUrl(request, category, command, argList) &&
            (commandItem = findCommandItem(category, command)))
        {
            string contentType = response.getContentType();
            string s = commandItem->outputCallback(argList, contentType);

            response.setStatusCode(200);
            response.setContentType(contentType);
            response.getContentStream()->write(s.c_str(), static_cast<int>(s.length()));
        }
        else
        {
            string s = "Not Found";
            response.setStatusCode(404);
            response.getContentStream()->write(s.c_str(), static_cast<int>(s.length()));
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// class PredefinedInspector

#ifdef ISE_WINDOWS

IseServerInspector::CommandItems PredefinedInspector::getItems() const
{
    typedef IseServerInspector::CommandItem CommandItem;
    typedef IseServerInspector::CommandItems CommandItems;

    string category("process");
    CommandItems items;

    items.push_back(CommandItem(category, "basic_info", PredefinedInspector::getBasicInfo, "show the basic info."));

    return items;
}

string PredefinedInspector::getBasicInfo(const PropertyList& argList,
    string& contentType)
{
    contentType = "text/plain";

    DWORD procId = ::GetCurrentProcessId();
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_INFORMATION, false, procId);
    size_t memorySize = 0;

    PROCESS_MEMORY_COUNTERS pmc = {0};
    pmc.cb = sizeof(pmc);
    if (::GetProcessMemoryInfo(handle, &pmc, pmc.cb))
        memorySize = pmc.WorkingSetSize;

    ::CloseHandle(handle);

    StrList strList;
    strList.add(formatString("path: %s", getAppExeName().c_str()));
    strList.add(formatString("pid: %u", procId));
    strList.add(formatString("memory: %s bytes", addThousandSep(memorySize).c_str()));

    return strList.getText();
}

#endif

#ifdef ISE_LINUX

IseServerInspector::CommandItems PredefinedInspector::getItems() const
{
    typedef IseServerInspector::CommandItem CommandItem;
    typedef IseServerInspector::CommandItems CommandItems;

    string category("proc");
    CommandItems items;

    items.push_back(CommandItem(category, "basic_info", PredefinedInspector::getBasicInfo, "show the basic info."));
    items.push_back(CommandItem(category, "status", PredefinedInspector::getProcStatus, "print /proc/self/status."));
    items.push_back(CommandItem(category, "opened_file_count", PredefinedInspector::getOpenedFileCount, "count /proc/self/fd."));
    items.push_back(CommandItem(category, "thread_count", PredefinedInspector::getThreadCount, "count /proc/self/task."));

    return items;
}

string PredefinedInspector::getBasicInfo(const PropertyList& /*argList*/,
    string& contentType)
{
    contentType = "text/plain";

    StrList strList;
    strList.add(std::format("path: {}", getAppExeName()));
    strList.add(std::format("pid: {}", ::getpid()));

    return strList.getText();
}

string PredefinedInspector::getProcStatus(const PropertyList& /*argList*/,
    string& contentType)
{
    contentType = "text/plain";

    StrList strList;
    if (strList.loadFromFile("/proc/self/status"))
        return strList.getText();
    else
        return "";
}

string PredefinedInspector::getOpenedFileCount(const PropertyList& /*argList*/,
    string& contentType)
{
    contentType = "text/plain";

    int count = 0;
    FileFindResult findResult;
    findFiles("/proc/self/fd/*", FA_ANY_FILE, findResult);
    for (size_t i = 0; i < findResult.size(); ++i)
    {
        const string& name = findResult[i].fileName;
        if (isInt64Str(name))
            ++count;
    }

    return intToStr(count);
}

string PredefinedInspector::getThreadCount(const PropertyList& /*argList*/,
    string& contentType)
{
    contentType = "text/plain";

    int count = 0;
    FileFindResult findResult;
    findFiles("/proc/self/task/*", FA_ANY_FILE, findResult);
    for (size_t i = 0; i < findResult.size(); ++i)
    {
        const string& name = findResult[i].fileName;
        if (isInt64Str(name))
            ++count;
    }

    return intToStr(count);
}

#endif


///////////////////////////////////////////////////////////////////////////////

} // namespace ise
