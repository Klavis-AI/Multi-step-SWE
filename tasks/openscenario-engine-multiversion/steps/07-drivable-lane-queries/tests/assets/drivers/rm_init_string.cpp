/*
 * rm_init_string - loading a road network from memory instead of from a file.
 *
 * usage: rm_init_string <odr> <road_id> <s>
 *
 * The file is read by the driver, never by the library, and the resulting
 * buffer is handed over. Everything the road manager reports afterwards is
 * then compared against what the same network reports when it is loaded from
 * the path, so the check is "the two loaders produce the same network" rather
 * than "the string loader produced some number".
 */
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "esminiRMLib.hpp"

struct Snapshot
{
    int   nroads    = -99;
    int   first_id  = -99;
    float length    = -99.0f;
    int   n_any     = -99;
    int   n_driving = -99;
    float width     = -99.0f;
    float x         = -99.0f;
    float y         = -99.0f;
};

static Snapshot take(id_t road, float s)
{
    Snapshot snap;
    snap.nroads   = RM_GetNumberOfRoads();
    snap.first_id = static_cast<int>(RM_GetIdOfRoadFromIndex(0));
    snap.length   = RM_GetRoadLength(road);
    snap.n_any    = RM_GetRoadNumberOfLanes(road, s, -1);
    /* any-drivable mask */
    snap.n_driving = RM_GetRoadNumberOfLanes(road, s, 1966594);
    RM_GetLaneWidthByRoadId(road, -1, s, &snap.width);

    int h = RM_CreatePosition();
    RM_SetLanePosition(h, road, -1, 0.0f, s, true);
    RM_PositionData d;
    RM_GetPositionData(h, &d);
    snap.x = d.x;
    snap.y = d.y;
    return snap;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <s>\n", argv[0]);
        return 2;
    }
    const id_t  road = static_cast<id_t>(std::atoi(argv[2]));
    const float s    = static_cast<float>(std::atof(argv[3]));

    std::ifstream in(argv[1]);
    if (!in)
    {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string xml = buf.str();
    std::printf("xml_bytes=%d\n", static_cast<int>(xml.size()));

    /* Reference: the same network, loaded from the path. */
    std::printf("file_init=%d\n", RM_Init(argv[1]));
    Snapshot from_file = take(road, s);
    RM_Close();

    /* Under test: the same network, loaded from memory. */
    int rc = RM_InitWithString(xml.c_str());
    std::printf("string_init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    Snapshot from_string = take(road, s);

    std::printf("nroads=%d\n", from_string.nroads);
    std::printf("first_id=%d\n", from_string.first_id);
    std::printf("road_length=%.3f\n", static_cast<double>(from_string.length));
    std::printf("n_any=%d\n", from_string.n_any);
    std::printf("n_driving=%d\n", from_string.n_driving);
    std::printf("lane_width=%.4f\n", static_cast<double>(from_string.width));
    std::printf("pos_x=%.3f\n", static_cast<double>(from_string.x));
    std::printf("pos_y=%.3f\n", static_cast<double>(from_string.y));

    int same = (from_file.nroads == from_string.nroads && from_file.first_id == from_string.first_id &&
                from_file.length == from_string.length && from_file.n_any == from_string.n_any &&
                from_file.n_driving == from_string.n_driving && from_file.width == from_string.width &&
                from_file.x == from_string.x && from_file.y == from_string.y)
                   ? 1
                   : 0;
    std::printf("matches_file_load=%d\n", same);
    RM_Close();

    /* A buffer that is not a road network is an error, not a crash, and not a
     * silently empty network. */
    std::printf("garbage_rc=%d\n", RM_InitWithString("this is not an OpenDRIVE document"));
    RM_Close();
    std::printf("empty_rc=%d\n", RM_InitWithString(""));
    RM_Close();

    std::printf("survived=1\n");
    return 0;
}
