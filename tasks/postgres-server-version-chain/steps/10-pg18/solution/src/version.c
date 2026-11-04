/* version.c - the major/minor version this build reports. */
#include "pgvc.h"

const char *PGVC_VERSION_TEXT =
#if PGVC_MAJOR == 96
    "9.6.24";
#elif PGVC_MAJOR == 10
    "10.21";
#elif PGVC_MAJOR == 11
    "11.16";
#elif PGVC_MAJOR == 12
    "12.22";
#elif PGVC_MAJOR == 13
    "13.23";
#elif PGVC_MAJOR == 14
    "14.24";
#elif PGVC_MAJOR == 15
    "15.19";
#elif PGVC_MAJOR == 16
    "16.15";
#elif PGVC_MAJOR == 17
    "17.11";
#else
    "18.6";
#endif

const char *PGVC_VERSION_NUM =
#if PGVC_MAJOR == 96
    "90624";
#elif PGVC_MAJOR == 10
    "100021";
#elif PGVC_MAJOR == 11
    "110016";
#elif PGVC_MAJOR == 12
    "120022";
#elif PGVC_MAJOR == 13
    "130023";
#elif PGVC_MAJOR == 14
    "140024";
#elif PGVC_MAJOR == 15
    "150019";
#elif PGVC_MAJOR == 16
    "160015";
#elif PGVC_MAJOR == 17
    "170011";
#else
    "180006";
#endif

const int PGVC_MAJOR_NUM = PGVC_MAJOR;
