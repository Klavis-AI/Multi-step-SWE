///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_inspector.h"

///////////////////////////////////////////////////////////////////////////////

const int SERVER_PORT = 8080;

ServerModule_Inspector::ServerModule_Inspector() :
    IseServerInspector(SERVER_PORT)
{
    add("test", "info1",
        [this](const PropertyList& args, string& contentType)
        { return getInfo1(args, contentType); },
        "click this to get info1.");
    add("test", "info2",
        [this](const PropertyList& args, string& contentType)
        { return getInfo2(args, contentType); },
        "click this to get info2.");
    add("test", "info3",
        [this](const PropertyList& args, string& contentType)
        { return getInfo3(args, contentType); },
        "click this to test args.");
}

string ServerModule_Inspector::getInfo1(const PropertyList& /*args*/, string& contentType)
{
    contentType = "text/plain";
    return "this is info1.";
}

string ServerModule_Inspector::getInfo2(const PropertyList& /*args*/, string& contentType)
{
    contentType = "text/html";
    return "this is <span style=\"color:red\">info2</span>.";
}

string ServerModule_Inspector::getInfo3(const PropertyList& args, string& contentType)
{
    if (args.isEmpty())
    {
        contentType = "text/html";
        string tip = "<a href=\"/test/info3?arg1=value1&arg2=value2\">click this url.</a></br>";
        return tip;
    }
    else
    {
        contentType = "text/plain";

        string arg1, arg2;
        args.getValue("arg1", arg1);
        args.getValue("arg2", arg2);

        return std::format(
            "arg1: {}, arg2: {}",
            arg1,
            arg2
            );
    }
}
