/*
 * se_variables - the OpenSCENARIO *variable* namespace published through the
 * scenario-engine C ABI.
 *
 * usage: se_variables <xosc> <mode>
 *   mode = read     | typed readers against the values declared in the file
 *          write    | typed writers, read back through the matching reader
 *          struct   | the SE_Variable struct forms
 *          split    | variables are not parameters, in both directions
 *          story    | a variable the storyboard assigns during the run
 *          initargs | the same typed readers, on a scenario started through
 *                     SE_InitWithArgs from a path that carries repeated spaces
 *
 * Nothing here prints an expected value: every line is what the library
 * returned. The driver never dereferences a pointer the library did not fill,
 * so a failing return code is reported rather than crashing the process.
 *
 * The struct mode deliberately exercises only the integer, double and boolean
 * variants of SE_GetVariable/SE_SetVariable. The struct's `value` is a void*
 * and, for a string variable, upstream reinterprets it as a pointer to a C++
 * std::string rather than to a C string -- that is not a stable C ABI and it
 * is not graded.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "esminiLib.hpp"
/* TEST_INTERFACE.md section 3 requires each header to be includable on its own
 * AND both together from one C++17 translation unit. Including only one header
 * per driver would leave the second half of that requirement unchecked, so this
 * driver takes both. The road-manager entry points are not called here. */
#include "esminiRMLib.hpp"

static const char *kVariableNames[] = {"CruiseCount", "SegmentIndex", "HeadwayTarget", "ManoeuvreTag", "AssistEnabled", "AssistArmed", "PhaseTag"};
static const int   kNumVariableNames = 7;

/* Report a getter as <tag>_rc plus, only when it succeeded, <tag>. */
static void get_int(const char *tag, const char *name)
{
    int v  = 0;
    int rc = SE_GetVariableInt(name, &v);
    std::printf("%s_rc=%d\n", tag, rc);
    if (rc == 0)
        std::printf("%s=%d\n", tag, v);
}

static void get_double(const char *tag, const char *name)
{
    double v  = 0.0;
    int    rc = SE_GetVariableDouble(name, &v);
    std::printf("%s_rc=%d\n", tag, rc);
    if (rc == 0)
        std::printf("%s=%.9f\n", tag, v);
}

static void get_string(const char *tag, const char *name)
{
    const char *v  = nullptr;
    int         rc = SE_GetVariableString(name, &v);
    std::printf("%s_rc=%d\n", tag, rc);
    if (rc == 0 && v != nullptr)
        std::printf("%s=%s\n", tag, v);
}

static void get_bool(const char *tag, const char *name)
{
    bool v  = false;
    int  rc = SE_GetVariableBool(name, &v);
    std::printf("%s_rc=%d\n", tag, rc);
    if (rc == 0)
        std::printf("%s=%d\n", tag, v ? 1 : 0);
}

static int mode_read()
{
    /* The four typed readers against the four declared types. */
    get_int("cruise", "CruiseCount");
    get_int("segment", "SegmentIndex");
    get_double("headway", "HeadwayTarget");
    get_string("manoeuvre", "ManoeuvreTag");
    get_string("phase", "PhaseTag");
    get_bool("assist_on", "AssistEnabled");
    get_bool("assist_armed", "AssistArmed");

    /* Asking for an existing variable through the reader for the wrong type is
     * an error, not a coercion. */
    get_double("cruise_as_double", "CruiseCount");
    get_int("headway_as_int", "HeadwayTarget");
    get_string("assist_as_string", "AssistEnabled");
    get_bool("manoeuvre_as_bool", "ManoeuvreTag");

    /* A name no declaration block mentions. */
    get_int("absent_int", "NoSuchVariable");
    get_double("absent_double", "NoSuchVariable");
    get_string("absent_string", "NoSuchVariable");
    get_bool("absent_bool", "NoSuchVariable");

    return 0;
}

static int mode_write()
{
    /* Each setter is used twice with two different values, so the reader is
     * reporting what was written rather than a constant. */
    std::printf("set_int_a_rc=%d\n", SE_SetVariableInt("CruiseCount", 42));
    get_int("cruise_a", "CruiseCount");
    std::printf("set_int_b_rc=%d\n", SE_SetVariableInt("CruiseCount", -900));
    get_int("cruise_b", "CruiseCount");

    std::printf("set_double_a_rc=%d\n", SE_SetVariableDouble("HeadwayTarget", 0.125));
    get_double("headway_a", "HeadwayTarget");
    std::printf("set_double_b_rc=%d\n", SE_SetVariableDouble("HeadwayTarget", -987.654321));
    get_double("headway_b", "HeadwayTarget");

    std::printf("set_string_a_rc=%d\n", SE_SetVariableString("ManoeuvreTag", "hold_lane"));
    get_string("manoeuvre_a", "ManoeuvreTag");
    std::printf("set_string_b_rc=%d\n", SE_SetVariableString("ManoeuvreTag", "merge_right"));
    get_string("manoeuvre_b", "ManoeuvreTag");

    std::printf("set_bool_a_rc=%d\n", SE_SetVariableBool("AssistEnabled", false));
    get_bool("assist_on_a", "AssistEnabled");
    std::printf("set_bool_b_rc=%d\n", SE_SetVariableBool("AssistArmed", true));
    get_bool("assist_armed_b", "AssistArmed");

    /* Writing one variable must not disturb another. */
    get_int("segment_after", "SegmentIndex");
    get_string("phase_after", "PhaseTag");

    /* A write does not retype the variable: the wrong-type reader still fails
     * after the value has been replaced. */
    get_double("cruise_as_double_after", "CruiseCount");

    /* Writing a name that was never declared creates nothing. */
    std::printf("absent_set_int_rc=%d\n", SE_SetVariableInt("NoSuchVariable", 1));
    std::printf("absent_set_double_rc=%d\n", SE_SetVariableDouble("NoSuchVariable", 1.0));
    std::printf("absent_set_string_rc=%d\n", SE_SetVariableString("NoSuchVariable", "x"));
    std::printf("absent_set_bool_rc=%d\n", SE_SetVariableBool("NoSuchVariable", true));
    get_int("absent_after_set", "NoSuchVariable");

    return 0;
}

static int mode_struct()
{
    {
        int         iv = 0;
        SE_Variable v;
        v.name  = "CruiseCount";
        v.value = &iv;
        std::printf("struct_get_int_rc=%d\n", SE_GetVariable(&v));
        std::printf("struct_get_int=%d\n", iv);
        iv = 101;
        std::printf("struct_set_int_rc=%d\n", SE_SetVariable(v));
        get_int("struct_int_after", "CruiseCount");
    }
    {
        double      dv = 0.0;
        SE_Variable v;
        v.name  = "HeadwayTarget";
        v.value = &dv;
        std::printf("struct_get_double_rc=%d\n", SE_GetVariable(&v));
        std::printf("struct_get_double=%.9f\n", dv);
        dv = -3.5;
        std::printf("struct_set_double_rc=%d\n", SE_SetVariable(v));
        get_double("struct_double_after", "HeadwayTarget");
    }
    {
        /* A boolean variable is read back through a caller's `bool *`, and the
         * library writes through it at exactly that width -- one byte. The flag
         * is therefore given three canary bytes immediately behind it, in a
         * struct so their placement is the ABI's and not the optimiser's: a
         * build that stored the boolean as an int and wrote four bytes through
         * the pointer would scribble over them. Reported as a 0/1 flag rather
         * than as the bytes, so the case never has to know their values. */
        struct BoolProbe
        {
            bool          bv;
            unsigned char canary[3];
        } probe;
        probe.bv        = false;
        probe.canary[0] = 0xA5;
        probe.canary[1] = 0x5A;
        probe.canary[2] = 0xC3;

        SE_Variable v;
        v.name  = "AssistEnabled";
        v.value = &probe.bv;
        std::printf("struct_get_bool_rc=%d\n", SE_GetVariable(&v));
        std::printf("struct_get_bool=%d\n", probe.bv ? 1 : 0);
        std::printf("struct_bool_canary=%d\n",
                    (probe.canary[0] == 0xA5 && probe.canary[1] == 0x5A && probe.canary[2] == 0xC3) ? 1 : 0);
        probe.bv = false;
        std::printf("struct_set_bool_rc=%d\n", SE_SetVariable(v));
        get_bool("struct_bool_after", "AssistEnabled");
    }
    {
        /* The struct form reads the same store the typed setters write. */
        std::printf("pre_seed_rc=%d\n", SE_SetVariableInt("SegmentIndex", 7654));
        int         iv = 0;
        SE_Variable v;
        v.name  = "SegmentIndex";
        v.value = &iv;
        std::printf("struct_sees_typed_rc=%d\n", SE_GetVariable(&v));
        std::printf("struct_sees_typed=%d\n", iv);
    }
    {
        int         iv = 0;
        SE_Variable v;
        v.name  = "NoSuchVariable";
        v.value = &iv;
        std::printf("struct_get_absent_rc=%d\n", SE_GetVariable(&v));
        std::printf("struct_set_absent_rc=%d\n", SE_SetVariable(v));
    }
    return 0;
}

static int mode_split()
{
    double      d = 0.0;
    const char *s = nullptr;

    /* A parameter reader must not find a variable. */
    std::printf("param_reads_variable_rc=%d\n", SE_GetParameterDouble("HeadwayTarget", &d));
    std::printf("param_reads_variable_str_rc=%d\n", SE_GetParameterString("ManoeuvreTag", &s));
    std::printf("param_writes_variable_rc=%d\n", SE_SetParameterDouble("HeadwayTarget", 11.0));

    /* ...and a variable reader must not find a parameter. */
    std::printf("variable_reads_param_rc=%d\n", SE_GetVariableDouble("EgoStartSpeed", &d));
    std::printf("variable_reads_param_str_rc=%d\n", SE_GetVariableString("ParamOnlyTag", &s));
    std::printf("variable_writes_param_rc=%d\n", SE_SetVariableDouble("EgoStartSpeed", 11.0));

    /* Each namespace does answer for its own names, with the declared values. */
    d = 0.0;
    std::printf("param_reads_param_rc=%d\n", SE_GetParameterDouble("EgoStartSpeed", &d));
    std::printf("param_reads_param=%.9f\n", d);
    s = nullptr;
    std::printf("param_reads_param_str_rc=%d\n", SE_GetParameterString("ParamOnlyTag", &s));
    std::printf("param_reads_param_str=%s\n", s ? s : "(null)");
    get_double("variable_reads_variable", "HeadwayTarget");
    get_string("variable_reads_variable_str", "ManoeuvreTag");

    /* The parameter enumeration reports only the parameter namespace: the
     * fixture declares two parameters and seven variables. */
    int n = SE_GetNumberOfParameters();
    std::printf("nparams=%d\n", n);
    int leaked = 0;
    for (int i = 0; i < n; i++)
    {
        int         type = -1;
        const char *name = SE_GetParameterName(i, &type);
        if (name == nullptr)
        {
            continue;
        }
        for (int k = 0; k < kNumVariableNames; k++)
        {
            if (std::strcmp(name, kVariableNames[k]) == 0)
            {
                leaked = 1;
            }
        }
    }
    std::printf("variable_names_in_parameter_list=%d\n", leaked);

    /* Writing a variable does not graft it onto the parameter namespace. */
    std::printf("set_variable_rc=%d\n", SE_SetVariableDouble("HeadwayTarget", 9.5));
    d = 0.0;
    std::printf("param_reads_variable_after_set_rc=%d\n", SE_GetParameterDouble("HeadwayTarget", &d));
    std::printf("nparams_after_set=%d\n", SE_GetNumberOfParameters());
    get_double("variable_after_set", "HeadwayTarget");

    return 0;
}

static int mode_story()
{
    /* The fixture's storyboard assigns PhaseTag at 2 s through a global
     * VariableAction. Sampling before and after that instant gets two
     * different answers out of one entry point. */
    get_string("phase_t0", "PhaseTag");

    for (int i = 0; i < 10; i++)
    {
        SE_StepDT(0.1f);
    }
    std::printf("time_a=%.3f\n", SE_GetSimulationTimeDouble());
    get_string("phase_t1", "PhaseTag");

    for (int i = 0; i < 30; i++)
    {
        SE_StepDT(0.1f);
    }
    std::printf("time_b=%.3f\n", SE_GetSimulationTimeDouble());
    get_string("phase_t4", "PhaseTag");

    /* The other variables are untouched by that action. */
    get_int("cruise_end", "CruiseCount");
    get_string("manoeuvre_end", "ManoeuvreTag");

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s <xosc> <read|write|struct|split|story|initargs>\n", argv[0]);
        return 2;
    }
    const char *mode = argv[2];

    /* Before any scenario is loaded there is no variable namespace to read. */
    get_int("preinit", "CruiseCount");
    std::printf("preinit_set_rc=%d\n", SE_SetVariableInt("CruiseCount", 1));

    /* Initialisation reports failure for a scenario it cannot open, rather
     * than reporting success and running nothing. Probed here, before the real
     * init, so the successful path below is unaffected; only the `read` mode
     * asks, so the other modes keep their existing line-for-line output. */
    if (std::strcmp(mode, "read") == 0)
    {
        std::printf("init_bad=%d\n", SE_Init("no_such_scenario_9f31c4.xosc", 0, 0, 0, 0));
    }

    int rc;
    if (std::strcmp(mode, "initargs") == 0)
    {
        /* Each element of the vector is ONE argument. The scenario path the
         * case hands over carries repeated spaces, so a build that re-splits
         * an element on ' ' turns one path into several fragments and never
         * finds the file. Nothing here is expected of the caller beyond
         * quoting, which C already does. */
        const char *args[] = {"esmini", "--osc", argv[1], "--headless", "--fixed_timestep", "0.05"};
        rc                 = SE_InitWithArgs(6, args);
        std::printf("initargs_rc=%d\n", rc);
    }
    else
    {
        rc = SE_Init(argv[1], 0, 0, 0, 0);
    }
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int r = 0;
    if (std::strcmp(mode, "read") == 0)
        r = mode_read();
    else if (std::strcmp(mode, "write") == 0)
        r = mode_write();
    else if (std::strcmp(mode, "struct") == 0)
        r = mode_struct();
    else if (std::strcmp(mode, "split") == 0)
        r = mode_split();
    else if (std::strcmp(mode, "story") == 0)
        r = mode_story();
    else if (std::strcmp(mode, "initargs") == 0)
        r = mode_read();
    else
    {
        std::fprintf(stderr, "unknown mode %s\n", mode);
        return 2;
    }

    SE_Close();
    std::printf("survived=1\n");
    return r;
}
