/* se_geo_pos.cpp - SE_GetGeoPos, new at this step's target.
 *
 * usage: se_geo_pos <scenario.xosc>
 */
#include <cstdio>
#include <cstdlib>
#include "esminiLib.hpp"

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <scenario>\n", argv[0]); return 2; }

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    SE_StepDT(0.05f);

    double lat = -999.0, lon = -999.0;
    int r = SE_GetGeoPos(0, &lat, &lon);
    std::printf("rc0=%d lat0=%.6f lon0=%.6f\n", r, lat, lon);

    /* A null lat or lon pointer is a documented failure. */
    double scratch = -999.0;
    std::printf("null_lat=%d\n", SE_GetGeoPos(0, nullptr, &scratch));
    std::printf("null_lon=%d\n", SE_GetGeoPos(0, &scratch, nullptr));

    /* An unknown object id is a documented failure and must not touch the
     * caller's storage. */
    double lat2 = -999.0, lon2 = -999.0;
    int rbad = SE_GetGeoPos(99, &lat2, &lon2);
    std::printf("badobj=%d badobj_lat_untouched=%d badobj_lon_untouched=%d\n",
                rbad, lat2 == -999.0, lon2 == -999.0);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}
