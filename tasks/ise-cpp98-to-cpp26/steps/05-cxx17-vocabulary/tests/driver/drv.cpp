///////////////////////////////////////////////////////////////////////////////
// The driver's entry point.
//
// ise_application.cpp defines main() itself, so a driver cannot supply its
// own: it has to be an ise application. That is the better shape anyway --
// the cases then run with the framework fully initialised, and the startup
// and shutdown paths of IseApplication are exercised on the way in and out.
//
// Run it with --nodaemon; the default is to fork into the background.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace ise;

namespace drv {

std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

//-----------------------------------------------------------------------------

void out(const std::string& line)
{
    std::cout << line << "\n";
}

void outf(const char *format, ...)
{
    std::string result;
    va_list argList;
    va_start(argList, format);
    formatStringV(result, format, argList);
    va_end(argList);
    out(result);
}

//-----------------------------------------------------------------------------

std::string esc(const char *data, int size)
{
    static const char *HEX = "0123456789abcdef";
    std::string result;
    for (int i = 0; i < size; i++)
    {
        unsigned char ch = static_cast<unsigned char>(data[i]);
        if (ch == '\\')
            result += "\\\\";
        else if (ch >= 0x20 && ch < 0x7F)
            result += static_cast<char>(ch);
        else
        {
            result += "\\x";
            result += HEX[(ch >> 4) & 0x0F];
            result += HEX[ch & 0x0F];
        }
    }
    return result;
}

std::string esc(const std::string& value)
{
    return esc(value.data(), static_cast<int>(value.size()));
}

//-----------------------------------------------------------------------------

std::string tmpDir()
{
    return "./drvtmp";
}

std::string tmpPath(const std::string& name)
{
    return tmpDir() + "/" + name;
}

} // namespace drv

///////////////////////////////////////////////////////////////////////////////

namespace {

bool byName(const drv::Case& a, const drv::Case& b)
{
    return std::strcmp(a.name, b.name) < 0;
}

void runAllCases()
{
    const char *filter = ::getenv("ISEDRV_ONLY");
    std::string only = (filter != NULL ? filter : "");

    deleteDir(drv::tmpDir(), true);
    forceDirectories(drv::tmpDir());

    std::vector<drv::Case> cases = drv::registry();
    std::sort(cases.begin(), cases.end(), byName);

    int ran = 0;
    for (size_t i = 0; i < cases.size(); i++)
    {
        const drv::Case& item = cases[i];
        if (!only.empty() && std::string(item.name).find(only) != 0)
            continue;
        ran++;
        std::cout << "### " << item.name << "\n";
        if (item.unscored != NULL)
            std::cout << "### unscored: " << item.unscored << "\n";
        try
        {
            item.fn();
        }
        catch (Exception& e)
        {
            // An escaping exception is a result like any other, and must not
            // take the rest of the run with it: a case that stopped the
            // process would silently remove every later case from the
            // denominator.
            // getErrorMessage() and not makeLogStr(): the latter appends the
            // __FILE__ and __LINE__ of the throw site, which move whenever the
            // source moves and would make every case that throws a false
            // difference.
            std::cout << "!! ise::Exception: " << e.getErrorMessage() << "\n";
        }
        catch (std::exception& e)
        {
            std::cout << "!! std::exception: " << e.what() << "\n";
        }
        catch (...)
        {
            std::cout << "!! unknown exception\n";
        }
        std::cout.flush();
    }

    std::cout << "### done " << ran << " cases\n";
    std::cout.flush();
    deleteDir(drv::tmpDir(), true);
}

class DrvBusiness : public IseBusiness
{
public:
    virtual void initIseOptions(IseOptions& options)
    {
        // No servers: the driver drives the library directly.
        options.setServerType(0);
        options.setAssistorThreadCount(0);
    }

    virtual string getAppVersion() { return "isedrv-1.0"; }

    virtual void afterInit()
    {
        // Two jobs, never both in one process: the transcript the behaviour
        // suite compares, and the workload callgrind counts. Keeping the bench
        // out of the ordinary run keeps it out of the recorded fixtures, and
        // keeps the counted run down to the library work plus the framework's
        // own start-up.
        if (::getenv("ISEDRV_BENCH") != NULL)
        {
            drv::runBench();
            std::exit(0);
        }

        runAllCases();

        // The framework's run() would otherwise wait for a signal. Leaving
        // through exit() keeps the transcript to exactly the cases plus the
        // startup lines the application itself logged.
        std::exit(0);
    }
};

} // namespace

IseBusiness* createIseBusinessObject()
{
    // Before the framework starts a thread, because localtime_r() reads the
    // zone on every call and setenv() from one thread while another is inside
    // it is a data race. Every instant the driver prints comes from a fixed
    // epoch, so the zone is the only input to a date line that the machine
    // would otherwise get to choose.
    //
    // This is not enough on its own to make dates repeatable: encodeDateTime()
    // reads an indeterminate tm_isdst and its answer moves by an hour between
    // runs of the same binary. See classes_datetime_encode_indeterminate, and
    // NOTES-authoring.md B11.
    setenv("TZ", "UTC", 1);
    tzset();

    return new DrvBusiness();
}
