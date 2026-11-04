/*
 * se_bbox - SE_AddObjectWithBoundingBox.
 *
 * usage: se_bbox <xosc> <length> <width> <height> <cx> <cy> <cz> <scale_mode>
 *
 * Loads a scenario, adds ONE externally controlled object whose bounding box
 * is supplied by the caller rather than read from a catalogue, gives it a
 * position, and reads the object's state back out. The bounding box the
 * library hands back is the observable: the dimensions and the centre offset
 * must be the ones that went in.
 *
 * A second object is added without a bounding box through SE_AddObject, so
 * one run shows both the explicit-box form and the plain form and a stub that
 * echoes a single constant cannot satisfy both.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiLib.hpp"

static void dump(const char *prefix, int id)
{
    SE_ScenarioObjectState st;
    std::memset(&st, 0, sizeof(st));
    int rc = SE_GetObjectState(id, &st);
    std::printf("%s_state_rc=%d\n", prefix, rc);
    if (rc != 0)
    {
        return;
    }
    std::printf("%s_id=%d\n", prefix, st.id);
    std::printf("%s_length=%.4f\n", prefix, static_cast<double>(st.length));
    std::printf("%s_width=%.4f\n", prefix, static_cast<double>(st.width));
    std::printf("%s_height=%.4f\n", prefix, static_cast<double>(st.height));
    std::printf("%s_cx=%.4f\n", prefix, static_cast<double>(st.centerOffsetX));
    std::printf("%s_cy=%.4f\n", prefix, static_cast<double>(st.centerOffsetY));
    std::printf("%s_cz=%.4f\n", prefix, static_cast<double>(st.centerOffsetZ));
    std::printf("%s_type=%d\n", prefix, st.objectType);
    std::printf("%s_category=%d\n", prefix, st.objectCategory);
}

int main(int argc, char **argv)
{
    if (argc != 9)
    {
        std::fprintf(stderr, "usage: %s <xosc> <length> <width> <height> <cx> <cy> <cz> <scale_mode>\n", argv[0]);
        return 2;
    }

    SE_OSCBoundingBox bb;
    bb.dimensions_.length_ = static_cast<float>(std::atof(argv[2]));
    bb.dimensions_.width_  = static_cast<float>(std::atof(argv[3]));
    bb.dimensions_.height_ = static_cast<float>(std::atof(argv[4]));
    bb.center_.x_          = static_cast<float>(std::atof(argv[5]));
    bb.center_.y_          = static_cast<float>(std::atof(argv[6]));
    bb.center_.z_          = static_cast<float>(std::atof(argv[7]));
    int scale_mode         = std::atoi(argv[8]);

    /* Before a scenario exists there is nothing to add the object to. */
    std::printf("add_before_init=%d\n", SE_AddObjectWithBoundingBox("early", 1, 1, 0, 0, bb, scale_mode));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    std::printf("nobj_before=%d\n", SE_GetNumberOfObjects());

    int id_bb = SE_AddObjectWithBoundingBox("bbox_obj", 1 /* VEHICLE */, 1 /* CAR */, 0 /* role */, 0 /* model */, bb, scale_mode);
    std::printf("bb_add_id=%d\n", id_bb);
    std::printf("bb_add_ok=%d\n", id_bb >= 0 ? 1 : 0);

    /* The plain form takes no bounding box at all. */
    int id_plain = SE_AddObject("plain_obj", 1, 1, 0, 0);
    std::printf("plain_add_id=%d\n", id_plain);

    std::printf("nobj_after=%d\n", SE_GetNumberOfObjects());

    SE_ReportObjectPos(id_bb, 0.0f, 30.0f, -1.5f, 0.0f, 0.0f, 0.0f, 0.0f);
    SE_StepDT(0.05f);

    dump("bb", id_bb);
    dump("plain", id_plain);

    const char *n = SE_GetObjectName(id_bb);
    std::printf("bb_name=%s\n", n != nullptr ? n : "(null)");
    const char *pn = SE_GetObjectName(id_plain);
    std::printf("plain_name=%s\n", pn != nullptr ? pn : "(null)");

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}
