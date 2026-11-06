#ifndef _XF_HISTOGRAM_CONFIG_H_
#define _XF_HISTOGRAM_CONFIG_H_

#include "hls_stream.h"
#include "ap_int.h"

#include "common/xf_common.hpp"
#include "common/xf_utility.hpp"
#include "common/xf_infra.hpp"
#include "common/xf_axi_io.hpp"
#include "imgproc/xf_histogram.hpp"
#include "imgproc/xf_duplicateimage.hpp"

#define HEIGHT 1080
#define WIDTH 1920

#define XF_CV_DEPTH_IN 2
#define XF_USE_URAM 0

#define GRAY 1
#define RGB 0
#define NPPCX XF_NPPC4

#define IN_TYPE XF_8UC3

#define INPUT_PTR_WIDTH 24

#define INVIDEO_WIDTH (XF_PIXELWIDTH(IN_TYPE, NPPCX) * XF_NPIXPERCYCLE(NPPCX))

typedef ap_axiu<INVIDEO_WIDTH, 1, 1, 1> InVideoStrmBus_t;
typedef hls::stream<InVideoStrmBus_t>   InVideoStrm_t;
typedef ap_axiu<INVIDEO_WIDTH, 1, 1, 1> OutVideoStrmBus_t;
typedef hls::stream<OutVideoStrmBus_t> OutVideoStrm_t;

#define HIST_WIDTH 96

typedef ap_axiu<HIST_WIDTH, 1, 1, 1> HistPkt_t;
typedef hls::stream<HistPkt_t>       OutHistStrm_t;

void histogram_accel(InVideoStrm_t& s_axis_video,
                     OutHistStrm_t& m_axis_hist,
                     OutVideoStrm_t& m_axis_video,
                     int rows,
                     int cols);

#endif
// _XF_HISTOGRAM_CONFIG_H_
