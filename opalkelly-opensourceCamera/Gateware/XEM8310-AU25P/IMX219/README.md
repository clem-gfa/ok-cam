# XEM8310-AU25P + Raspberry Pi Camera Module 2 (IMX219) on the DARWIN carrier board

Port of `XEM8320-AU25P/SZG-MIPI-8320`. Differences from the XEM8320 design:

- Constraints: `xem8310-au25p.xdc` (DDR4 on banks 66/67, 32 bits; pins from the XEM8310-A03 schematic).
- MIPI D-PHY on bank 67 (MC3): CLK `A17/A18`, DATA0 `B15/A15`, DATA1 `A19/A20`; line rate 912 Mb/s.
- I2C (`B84_L1N`/`B84_L9P`) and `pcam_power_en` (`B84_L3N`) in bank 84 at **LVCMOS18**.
- **VIO2 must be set to 1.8 V** on the XEM8310 (the DARWIN level translators are powered from +1.8 V).

Not yet verified on hardware / to check before building:

- `CONFIG.BOARD {XEM8310-AU25P}` is accepted by the FrontPanel Vivado IP Core (version-dependent).
- `C_HS_SETTLE_NS` (set to 120 ns, mid-range for 912 Mb/s) - tune with an ILA on the CSI-2 RX.
- ISP Bayer pattern (`HLS/ISP/xf_isp_accel_config.h`, still `XF_BAYER_BG` from the OV5640 design).
- DDR4 IP parameters for 2 x MT40A512M16LY-075 (32-bit) after regenerating the IP.

---

The HLS IP cores must be generated before the bitstream.


The HLS sources use the AMD Xilinx Vitis Vision Library, which is a separate download and is
not included here. Clone it first:

   git clone https://github.com/Xilinx/Vitis_Libraries.git

Then edit `XF_VITIS_VISION_PATH` at the top of each `run_hls.tcl` to point at your copy's
`vision/L1/include` directory. The value committed here is the path it was last built from and
will not exist on your machine.


To generate the HLS IP cores:

1. Open a shell with the HLS tools on the PATH by sourcing the
   install's settings64 script:

      Vitis Unified (Vivado/Vitis 2023.2 or later):
         Linux:    source <Xilinx install>/Vitis/<version>/settings64.sh
         Windows:  <Xilinx install>\Vitis\<version>\settings64.bat

      Vitis HLS (Vivado/Vitis prior to 2023.2):
         Linux:    source <Xilinx install>/Vitis_HLS/<version>/settings64.sh
         Windows:  <Xilinx install>\Vitis_HLS\<version>\settings64.bat

2. From within each HLS subfolder (HLS/ISP and HLS/histogram), run
   the HLS command for your installed tool version:

      Vitis Unified (Vivado/Vitis 2023.2 or later):
         vitis-run --mode hls --tcl run_hls.tcl

      Vitis HLS (Vivado/Vitis prior to 2023.2):
         vitis_hls run_hls.tcl


Once the HLS IP cores are generated, generate the bitstream:

1. Set the `fpdir` variable to point to your FrontPanel Vivado IP Core
   distribution directory.

   From the Vivado TCL console:
      set fpdir <path_to_FrontPanel_Vivado_IP_Core>

2. Change directory to this project folder:
      cd <path to this directory>

3. Source the project creation script:
      source project.tcl

4. Generate the bitstream.


Copyright (c) 2025-2026 Opal Kelly Incorporated
