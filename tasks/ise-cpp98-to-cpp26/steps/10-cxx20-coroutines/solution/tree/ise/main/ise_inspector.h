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
// ise_inspector.h
///////////////////////////////////////////////////////////////////////////////

#ifndef _ISE_INSPECTOR_H_
#define _ISE_INSPECTOR_H_

#include "ise/main/ise_options.h"
#include "ise/main/ise_classes.h"
#include "ise/main/ise_exceptions.h"
#include "ise/main/ise_server_tcp.h"
#include "ise/main/ise_http.h"
#include "ise/main/ise_application.h"
#include "ise/main/ise_svr_mod.h"

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// classes

class IseServerInspector;
class PredefinedInspector;

///////////////////////////////////////////////////////////////////////////////
// class IseServerInspector

class IseServerInspector : public IseServerModule
{
public:
    typedef std::function<string (const PropertyList& args, string& contentType)> OutputCallback;

    struct CommandItem
    {
    public:
        string category;
        string command;
        OutputCallback outputCallback;
        string help;
    public:
        CommandItem() {}
        CommandItem(const string& category, const string& command,
            const OutputCallback& outputCallback, const string& help)
        {
            this->category = category;
            this->command = command;
            this->outputCallback = outputCallback;
            this->help = help;
        }
    };

    typedef std::vector<CommandItem> CommandItems;

public:
    IseServerInspector(int serverPort);

    void add(
        const string& category,
        const string& command,
        const OutputCallback& outputCallback,
        const string& help);

    void add(const CommandItems& items);

public:
    int getTcpServerCount() override { return 1; }
    void getTcpServerOptions(int serverIndex, TcpServerOptions& options) override;

    ConnectionTask onTcpConnection(TcpConnectionPtr connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;

private:
    string outputHelpPage();
    bool parseCommandUrl(const HttpRequest& request, string& category,
        string& command, PropertyList& argList);
    CommandItem* findCommandItem(const string& category, const string& command);

    void onHttpSession(const HttpRequest& request, HttpResponse& response);

private:
    typedef std::map<string, CommandItem> CommandList;    // <command, CommandItem>
    typedef std::map<string, CommandList> InspectInfo;    // <category, CommandList>

    HttpServer httpServer_;
    int serverPort_;
    InspectInfo inspectInfo_;
    Mutex mutex_;
    std::unique_ptr<PredefinedInspector> predefinedInspector_;
};

///////////////////////////////////////////////////////////////////////////////
// class PredefinedInspector

class PredefinedInspector : boost::noncopyable
{
public:
    IseServerInspector::CommandItems getItems() const;
private:

#ifdef ISE_WINDOWS
    static string getBasicInfo(const PropertyList& argList, string& contentType);
#endif

#ifdef ISE_LINUX
    static string getBasicInfo(const PropertyList& argList, string& contentType);
    static string getProcStatus(const PropertyList& argList, string& contentType);
    static string getOpenedFileCount(const PropertyList& argList, string& contentType);
    static string getThreadCount(const PropertyList& argList, string& contentType);
#endif
};

///////////////////////////////////////////////////////////////////////////////

} // namespace ise

#endif // _ISE_INSPECTOR_H_
