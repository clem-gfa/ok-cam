# Copyright (c) 2026 Opal Kelly Incorporated
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

set XF_VITIS_VISION_PATH "C:/work/Vitis_Libraries/vision/L1/include"
set XPART xcau10p-ffvb676-1-e

open_project -reset isp_accel.prj

add_files "xf_isp_accel.cpp" \
    -cflags "-I${XF_VITIS_VISION_PATH} -std=c++0x"

set_top ISPPipeline_accel

open_solution -reset sol1

set_part $XPART

csynth_design

export_design

exit
