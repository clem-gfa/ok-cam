#ifndef _XF_ISP_CONFIG_PARAMS_H_
#define _XF_ISP_CONFIG_PARAMS_H_

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
#define XF_NPPC          XF_NPPC4
#define XF_WIDTH         2304
#define XF_HEIGHT        1296
#define XF_BAYER_PATTERN XF_BAYER_GR

#define XF_SRC_T         XF_8UC1
#define XF_DST_T         XF_8UC3

#define WB_TYPE          XF_WB_SIMPLE
#define SIN_CHANNEL_TYPE XF_8UC1

#define XF_AXI_GBR       0
#define XF_USE_URAM      0

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "hls_stream.h"
#include "ap_int.h"
#include "ap_axi_sdata.h"

#include "common/xf_common.hpp"
#include "common/xf_utility.hpp"
#include "common/xf_infra.hpp"
#include "common/xf_axi_io.hpp"

#include "imgproc/xf_bpc.hpp"
#include "imgproc/xf_black_level.hpp"
#include "imgproc/xf_gaincontrol.hpp"
#include "imgproc/xf_colorcorrectionmatrix.hpp"
#include "imgproc/xf_gammacorrection.hpp"
#include "imgproc/xf_histogram.hpp"
#include "imgproc/xf_quantizationdithering.hpp"
#include "imgproc/xf_aec.hpp"
#include "imgproc/xf_autowhitebalance.hpp"
#include "imgproc/xf_demosaicing.hpp"

// -----------------------------------------------------------------------------
// Derived macros
// -----------------------------------------------------------------------------
#define _DATA_WIDTH_(_T, _N)  (XF_PIXELWIDTH(_T, _N) * XF_NPIXPERCYCLE(_N))
#define _BYTE_ALIGN_(_N)      (((_N) + 7) / 8 * 8)

#define IN_DATA_WIDTH         _DATA_WIDTH_(XF_SRC_T, XF_NPPC)
#define OUT_DATA_WIDTH        _DATA_WIDTH_(XF_DST_T, XF_NPPC)

#define AXI_WIDTH_IN          _BYTE_ALIGN_(IN_DATA_WIDTH)
#define AXI_WIDTH_OUT         _BYTE_ALIGN_(OUT_DATA_WIDTH)

#define NR_COMPONENTS         3

constexpr int Q_VAL = 1 << XF_DTPIXELDEPTH(XF_SRC_T, XF_NPPC);

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------
typedef ap_axiu<AXI_WIDTH_IN,  1, 1, 1> InVideoStrmBus_t;
typedef ap_axiu<AXI_WIDTH_OUT, 1, 1, 1> OutVideoStrmBus_t;

typedef hls::stream<InVideoStrmBus_t>  InVideoStrm_t;
typedef hls::stream<OutVideoStrmBus_t> OutVideoStrm_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t bayer_phase;
} HW_STRUCT_REG;

// -----------------------------------------------------------------------------
// Top-level prototype
// -----------------------------------------------------------------------------
void ISPPipeline_accel(InVideoStrm_t&  s_axis_video,
                       OutVideoStrm_t& m_axis_video,
                       unsigned int    rgain,
                       unsigned int    ggain,
                       unsigned int    bgain,
                       uint32_t        height,
                       uint32_t        width,
                       uint8_t         blackLevelCorrection,
                       uint32_t        thresh);

#endif // _XF_ISP_CONFIG_PARAMS_H_
