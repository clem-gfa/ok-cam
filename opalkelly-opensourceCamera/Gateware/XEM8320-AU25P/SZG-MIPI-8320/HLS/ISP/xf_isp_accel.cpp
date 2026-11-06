#include "xf_isp_accel_config.h"
#include <cstdint>
#include <stdio.h>

static constexpr int __XF_DEPTH_PTR = (768 * (XF_CHANNELS(XF_SRC_T, XF_NPPC)));
static bool flag;

static uint32_t hist0[3][256];
static uint32_t hist1[3][256];

void ISPpipeline(InVideoStrm_t&  s_axis_video,
                 OutVideoStrm_t& m_axis_video,
                 unsigned short  height,
                 unsigned short  width,
                 uint8_t         rgain,
                 uint8_t         bgain,
                 uint8_t         ggain,
                 uint32_t        hist0[3][256],
                 uint32_t        hist1[3][256],
                 uint8_t         awbThreshold) {
#pragma HLS INLINE OFF

    xf::cv::Mat<XF_SRC_T, XF_HEIGHT, XF_WIDTH, XF_NPPC, 2> imgInput1(height, width);
    xf::cv::Mat<XF_SRC_T, XF_HEIGHT, XF_WIDTH, XF_NPPC, 2> bpc_out(height, width);
    xf::cv::Mat<XF_SRC_T, XF_HEIGHT, XF_WIDTH, XF_NPPC, 2> gain_out(height, width);
    xf::cv::Mat<XF_DST_T, XF_HEIGHT, XF_WIDTH, XF_NPPC, 2> demosaic_out(height, width);
    xf::cv::Mat<XF_DST_T, XF_HEIGHT, XF_WIDTH, XF_NPPC, 2> impop(height, width);
    xf::cv::Mat<XF_DST_T, XF_HEIGHT, XF_WIDTH, XF_NPPC, 2> awb(height, width);

#pragma HLS stream variable=bpc_out.data      dim=1 depth=2
#pragma HLS stream variable=gain_out.data     dim=1 depth=2
#pragma HLS stream variable=demosaic_out.data dim=1 depth=2
#pragma HLS stream variable=imgInput1.data    dim=1 depth=2
#pragma HLS stream variable=impop.data        dim=1 depth=2

#pragma HLS DATAFLOW

    float    inputMin  = 0.0f;
    float    inputMax  = 255.0f;
    float    outputMin = 0.0f;
    float    outputMax = 255.0f;
    uint16_t bformat   = XF_BAYER_PATTERN;

    xf::cv::AXIvideo2xfMat(s_axis_video, imgInput1);

    xf::cv::badpixelcorrection<
        XF_SRC_T, XF_HEIGHT, XF_WIDTH, XF_NPPC,
        0, 0>(
        imgInput1, bpc_out);

    xf::cv::gaincontrol<
        XF_SRC_T, XF_HEIGHT, XF_WIDTH, XF_NPPC>(
        bpc_out, gain_out, rgain, bgain, ggain, bformat);

    xf::cv::demosaicing<
        XF_SRC_T, XF_DST_T, XF_HEIGHT, XF_WIDTH, XF_NPPC,
        0, 2, 2>(
        gain_out, demosaic_out, bformat);

    xf::cv::AWBhistogram<
        XF_DST_T, XF_DST_T, XF_HEIGHT, XF_WIDTH, XF_NPPC,
        XF_USE_URAM, 1, 256, 2, 2>(
        demosaic_out, impop, hist0, awbThreshold,
        inputMin, inputMax, outputMin, outputMax);

    xf::cv::AWBNormalization<
        XF_DST_T, XF_DST_T, XF_HEIGHT, XF_WIDTH, XF_NPPC,
        1, 256, 2, 2>(
        impop, awb, hist1, awbThreshold,
        inputMin, inputMax, outputMin, outputMax);

    xf::cv::xfMat2AXIvideo(awb, m_axis_video);
}

void ISPPipeline_accel(InVideoStrm_t&  s_axis_video,
                       OutVideoStrm_t& m_axis_video,
                       uint32_t        height,
                       uint32_t        width,
                       unsigned int    rgain,
                       unsigned int    ggain,
                       unsigned int    bgain,
                       uint32_t        awbThreshold) {

#pragma HLS INTERFACE axis      port=&s_axis_video       register
#pragma HLS INTERFACE axis      port=&m_axis_video       register

#pragma HLS INTERFACE s_axilite port=rgain        bundle=control
#pragma HLS INTERFACE s_axilite port=bgain        bundle=control
#pragma HLS INTERFACE s_axilite port=ggain        bundle=control
#pragma HLS INTERFACE s_axilite port=height       bundle=control
#pragma HLS INTERFACE s_axilite port=width        bundle=control
#pragma HLS INTERFACE s_axilite port=awbThreshold bundle=control
#pragma HLS INTERFACE s_axilite port=return       bundle=control

#pragma HLS ARRAY_PARTITION variable=hist0 complete dim=1
#pragma HLS ARRAY_PARTITION variable=hist1 complete dim=1

    if (!flag) {
        ISPpipeline(s_axis_video, m_axis_video,
                    height, width,
                    rgain, bgain, ggain,
                    hist0, hist1,
                    awbThreshold);
        flag = 1;
    } else {
        ISPpipeline(s_axis_video, m_axis_video,
                    height, width,
                    rgain, bgain, ggain,
                    hist1, hist0,
                    awbThreshold);
        flag = 0;
    }
}
