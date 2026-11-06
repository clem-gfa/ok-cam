#include "xf_histogram_accel_config.h"
#include "imgproc/xf_duplicateimage.hpp"

static constexpr int __XF_DEPTH =
    (HEIGHT * WIDTH * (XF_PIXELWIDTH(IN_TYPE, NPPCX)) / 8) /
    (INPUT_PTR_WIDTH / 8);

static constexpr int __XF_DEPTH_PTR =
    (256 * (XF_CHANNELS(IN_TYPE, NPPCX)));


static void write_histogram(OutHistStrm_t& m_axis_hist,
                            unsigned int histogram_local[256 * XF_CHANNELS(IN_TYPE, NPPCX)]) {
#pragma HLS INLINE off

    const int CH        = XF_CHANNELS(IN_TYPE, NPPCX);
    const int HIST_SIZE = 256 * CH;
    const int PACK_SIZE = 3;
    const int OUT_PKTS  = HIST_SIZE / PACK_SIZE;

    for (int i = 0; i < OUT_PKTS; i++) {
#pragma HLS PIPELINE II=1
        ap_uint<96> packed = 0;
        packed.range(31, 0)  = histogram_local[i * 3 + 0];
        packed.range(63, 32) = histogram_local[i * 3 + 1];
        packed.range(95, 64) = histogram_local[i * 3 + 2];

        HistPkt_t pkt;
        pkt.data = packed;
        pkt.keep = -1;
        pkt.last = (i == OUT_PKTS - 1);

        m_axis_hist << pkt;
    }
}


static void process_one_frame(InVideoStrm_t&  s_axis_video,
                              OutVideoStrm_t& m_axis_video,
                              OutHistStrm_t&  m_axis_hist,
                              int rows,
                              int cols) {
#pragma HLS INLINE off
#pragma HLS DATAFLOW

    // One input image → two consumers, must fan-out explicitly
    xf::cv::Mat<IN_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_IN> imgInput(rows, cols);
    xf::cv::Mat<IN_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_IN> imgVideo(rows, cols);
    xf::cv::Mat<IN_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_IN> imgHist(rows, cols);

    xf::cv::AXIvideo2xfMat(s_axis_video, imgInput);

    // Fan-out
    xf::cv::duplicateMat(imgInput, imgVideo, imgHist);

    // Video passthrough
    xf::cv::xfMat2AXIvideo(imgVideo, m_axis_video);

    unsigned int histogram_local[256 * XF_CHANNELS(IN_TYPE, NPPCX)];
    #pragma HLS bind_storage variable=histogram_local type=ram_1p impl=bram
    
    xf::cv::calcHist<IN_TYPE,
                     HEIGHT,
                     WIDTH,
                     NPPCX,
                     XF_USE_URAM,
                     XF_CV_DEPTH_IN>(
        imgHist, histogram_local);

    // Stream histogram out
    write_histogram(m_axis_hist, histogram_local);
}


void histogram_accel(InVideoStrm_t& s_axis_video,
                     OutHistStrm_t& m_axis_hist,
                     OutVideoStrm_t& m_axis_video,
                     int rows,
                     int cols) {
#pragma HLS INTERFACE axis      port=s_axis_video register
#pragma HLS INTERFACE axis      port=m_axis_hist  register
#pragma HLS INTERFACE axis      port=m_axis_video register

#pragma HLS INTERFACE s_axilite port=rows
#pragma HLS INTERFACE s_axilite port=cols
#pragma HLS INTERFACE s_axilite port=return

    process_one_frame(s_axis_video, m_axis_video, m_axis_hist, rows, cols);
}
