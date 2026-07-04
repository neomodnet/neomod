#ifndef OSU_CONVARS_H
#define OSU_CONVARS_H

#include "ConVar.h"

#ifndef DEFINE_OSU_CONVARS
#include "OsuConVarDefs.h"
#endif

#define ARGB_CV_TO_COL(cv_prefix__)                                                                \
    argb(cv::cv_prefix__##_a.getInt(), cv::cv_prefix__##_r.getInt(), cv::cv_prefix__##_g.getInt(), \
         cv::cv_prefix__##_b.getInt())
#define RGB_CV_TO_COL(cv_prefix__) \
    rgb(cv::cv_prefix__##_r.getInt(), cv::cv_prefix__##_g.getInt(), cv::cv_prefix__##_b.getInt())

#endif
