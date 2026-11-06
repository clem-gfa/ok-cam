-- Copyright (c) 2026 Opal Kelly Incorporated
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

-- camera_axi.lua — server-side Lua for the AXI camera gateware, for FrontPanel-over-IP.
--
-- Implements the camera init + capture flow on the device using the okFP (okCFrontPanel) FrontPanel
-- primitives, so the chatty camera init + capture run server-side over FPoIP instead of one network
-- round-trip per AXI transaction. The host binds okFP to the open device and calls these functions.
--
-- Sections, in order:
--   AXI-Lite bridge + AXI-IIC
--   AR0330 (Syzygy) bring-up
--   IP drivers + capture sequencer
--   OV5640 (Pcam) bring-up

Sleep = OpalKelly.Sleep

-- =====================================================================================
-- AXI-Lite over the FrontPanel->AXI classic bridge.
-- =====================================================================================
BRIDGE_WI_ADDRESS    = 0x1d   -- WireIn: AXI byte address
BRIDGE_WI_DATA       = 0x1e   -- WireIn: write data
BRIDGE_WI_TIMEOUT    = 0x1f   -- WireIn: hardware timeout (FP clock periods)
BRIDGE_WO_DATA       = 0x3e   -- WireOut: read data
BRIDGE_WO_STATUS     = 0x3f   -- WireOut: status
BRIDGE_TI_OPERATION  = 0x5f   -- TriggerIn: kick a transaction
BRIDGE_TI_WRITE_BIT  = 0
BRIDGE_TI_READ_BIT   = 1
AXI_RESET_WI         = 0x00   -- WireIn: AXI system reset (axi_reset module)

STATUS_BUSY_MASK      = 0x01
STATUS_RESPONSE_SHIFT = 1
STATUS_RESPONSE_MASK  = 0x07
RESP_OKAY = 0
RESP_SLVERR = 2
RESP_DECERR = 3
RESP_HWTIMEOUT = 4

NS_PER_FP_CLK = 9.920
HW_TIMEOUT_MS = 3000
SW_TIMEOUT_TRIES = 500   -- * 10ms = 5000ms

-- Configure the bridge hardware timeout. Call once after load (Setup()).
function AxiSetTimeout()
  local periods = math.floor((HW_TIMEOUT_MS * 1000000) / NS_PER_FP_CLK)
  okFP:SetWireInValue(BRIDGE_WI_TIMEOUT, periods, 0xFFFFFFFF)
  okFP:UpdateWireIns()
end

function AxiPollReady(op, addr)
  okFP:UpdateWireOuts()
  local status = okFP:GetWireOutValue(BRIDGE_WO_STATUS)
  local tries = 0
  while (status & STATUS_BUSY_MASK) ~= 0 do
    tries = tries + 1
    if tries > SW_TIMEOUT_TRIES then
      error(string.format("AXI %s timed out at 0x%x", op, addr))
    end
    Sleep(10)
    okFP:UpdateWireOuts()
    status = okFP:GetWireOutValue(BRIDGE_WO_STATUS)
  end
  local resp = (status >> STATUS_RESPONSE_SHIFT) & STATUS_RESPONSE_MASK
  if resp ~= RESP_OKAY then
    error(string.format("AXI %s response %d at 0x%x", op, resp, addr))
  end
end

function AxiWrite32(addr, value)
  okFP:SetWireInValue(BRIDGE_WI_ADDRESS, addr, 0xFFFFFFFF)
  okFP:SetWireInValue(BRIDGE_WI_DATA, value, 0xFFFFFFFF)
  okFP:UpdateWireIns()
  okFP:ActivateTriggerIn(BRIDGE_TI_OPERATION, BRIDGE_TI_WRITE_BIT)
  AxiPollReady("write", addr)
end

function AxiRead32(addr)
  okFP:SetWireInValue(BRIDGE_WI_ADDRESS, addr, 0xFFFFFFFF)
  okFP:UpdateWireIns()
  okFP:ActivateTriggerIn(BRIDGE_TI_OPERATION, BRIDGE_TI_READ_BIT)
  AxiPollReady("read", addr)
  okFP:UpdateWireOuts()
  return okFP:GetWireOutValue(BRIDGE_WO_DATA)
end

function AxiResetSystem()
  okFP:SetWireInValue(AXI_RESET_WI, 1, 0xFFFFFFFF)
  okFP:UpdateWireIns()
  okFP:SetWireInValue(AXI_RESET_WI, 0, 0xFFFFFFFF)
  okFP:UpdateWireIns()
end

-- =====================================================================================
-- AXI-IIC (AMD AXI IIC, dynamic mode) over AXI-Lite.
-- 16-bit reg / 16-bit data (AR0330), 16-bit reg / 8-bit data (OV5640).
-- =====================================================================================
IIC_BASE            = 0x40800000
XIIC_RESETR_OFFSET  = 0x40
XIIC_CR_REG_OFFSET  = 0x100
XIIC_SR_REG_OFFSET  = 0x104
XIIC_DTR_REG_OFFSET = 0x108
XIIC_DRR_REG_OFFSET = 0x10c
XIIC_RFD_REG_OFFSET = 0x120
XIIC_RESET_MASK            = 0x0a
XIIC_CR_ENABLE_DEVICE_MASK = 0x01
XIIC_CR_TX_FIFO_RESET_MASK = 0x02
XIIC_SR_BUS_BUSY_MASK      = 0x04
XIIC_SR_RX_FIFO_EMPTY_MASK = 0x40
XIIC_SR_TX_FIFO_EMPTY_MASK = 0x80
XIIC_TX_DYN_START_MASK     = 0x100
XIIC_TX_DYN_STOP_MASK      = 0x200
IIC_RX_FIFO_DEPTH          = 16

function I2CInit()
  AxiWrite32(IIC_BASE + XIIC_RESETR_OFFSET, XIIC_RESET_MASK)
  AxiWrite32(IIC_BASE + XIIC_RFD_REG_OFFSET, IIC_RX_FIFO_DEPTH - 1)
  AxiWrite32(IIC_BASE + XIIC_CR_REG_OFFSET, XIIC_CR_TX_FIFO_RESET_MASK)
  AxiWrite32(IIC_BASE + XIIC_CR_REG_OFFSET, XIIC_CR_ENABLE_DEVICE_MASK)
  local st = AxiRead32(IIC_BASE + XIIC_SR_REG_OFFSET)
  local expected = XIIC_SR_RX_FIFO_EMPTY_MASK | XIIC_SR_TX_FIFO_EMPTY_MASK
  if (st & expected) ~= expected then
    error(string.format("AXI IIC dynamic init failed. Status=0x%x", st))
  end
end

function I2CWaitBusFree(timeoutMs)
  timeoutMs = timeoutMs or 1000
  for _ = 1, timeoutMs do
    local sr = AxiRead32(IIC_BASE + XIIC_SR_REG_OFFSET)
    if (sr & XIIC_SR_BUS_BUSY_MASK) == 0 then return true end
    Sleep(1)
  end
  return false
end

-- bytes: a Lua array (1-based) of byte values.
function I2CDynSend(dev7bit, bytes, sendStop)
  local addrByte = (dev7bit << 1) | 0
  AxiWrite32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_START_MASK | addrByte)
  for i = 1, #bytes do
    if i == #bytes and sendStop then
      AxiWrite32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_STOP_MASK | bytes[i])
    else
      AxiWrite32(IIC_BASE + XIIC_DTR_REG_OFFSET, bytes[i])
    end
  end
end

function I2CDynRecv(dev7bit, count, timeoutMs)
  timeoutMs = timeoutMs or 1000
  local addrByte = (dev7bit << 1) | 1
  AxiWrite32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_START_MASK | addrByte)
  AxiWrite32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_STOP_MASK | count)
  local result = {}
  local tries = timeoutMs
  while #result < count do
    if tries <= 0 then error(string.format("I2C recv timed out (%d/%d)", #result, count)) end
    local sr = AxiRead32(IIC_BASE + XIIC_SR_REG_OFFSET)
    if (sr & XIIC_SR_RX_FIFO_EMPTY_MASK) == 0 then
      local b = AxiRead32(IIC_BASE + XIIC_DRR_REG_OFFSET)
      result[#result + 1] = b & 0xff
    else
      Sleep(1); tries = tries - 1
    end
  end
  return result
end

function I2CWrite16(dev7bit, reg, data)
  if not I2CWaitBusFree() then error("I2C bus never freed before write16") end
  I2CDynSend(dev7bit, { (reg >> 8) & 0xff, reg & 0xff, (data >> 8) & 0xff, data & 0xff }, true)
end

function I2CRead16(dev7bit, reg)
  if not I2CWaitBusFree() then error("I2C bus never freed before read16") end
  I2CDynSend(dev7bit, { (reg >> 8) & 0xff, reg & 0xff }, false)
  local rx = I2CDynRecv(dev7bit, 2)
  if not I2CWaitBusFree() then error("I2C bus did not free after read16") end
  return (rx[1] << 8) | rx[2]
end

function I2CWrite8(dev7bit, reg, data)
  if not I2CWaitBusFree() then error("I2C bus never freed before write8") end
  I2CDynSend(dev7bit, { (reg >> 8) & 0xff, reg & 0xff, data & 0xff }, true)
end

function I2CRead8(dev7bit, reg)
  if not I2CWaitBusFree() then error("I2C bus never freed before read8") end
  I2CDynSend(dev7bit, { (reg >> 8) & 0xff, reg & 0xff }, false)
  local rx = I2CDynRecv(dev7bit, 1)
  if not I2CWaitBusFree() then error("I2C bus did not free after read8") end
  return rx[1]
end

-- =====================================================================================
-- AR0330 (SZG-CAMERA / Syzygy) sensor control.
-- 16-bit reg / 16-bit data on I2C device 0x10 (7-bit).
-- =====================================================================================
AR0330_DEV = 0x10
AR0330_REG_CHIP_VERSION        = 0x3000
AR0330_REG_Y_ADDR_END          = 0x3006
AR0330_REG_X_ADDR_END          = 0x3008
AR0330_REG_LINE_LENGTH_PCK     = 0x300c
AR0330_REG_COARSE_INTEGRATION  = 0x3012
AR0330_REG_MODE_SELECT         = 0x301c
AR0330_REG_VT_PIX_CLK_DIV      = 0x302a
AR0330_REG_PRE_PLL_CLK_DIV     = 0x302e
AR0330_REG_PLL_MULTIPLIER      = 0x3030
AR0330_REG_OP_PIX_CLK_DIV      = 0x3036
AR0330_REG_OP_SYS_CLK_DIV      = 0x3038
AR0330_REG_ANALOG_GAIN         = 0x3060
AR0330_REG_SMIA_TEST           = 0x3064
AR0330_REG_DATAPATH_SELECT     = 0x306e
AR0330_REG_TEST_PATTERN_MODE   = 0x3070
AR0330_REG_X_ODD_INC           = 0x30a2
AR0330_REG_Y_ODD_INC           = 0x30a6
AR0330_REG_DATA_FORMAT_BITS    = 0x31ac
AR0330_REG_HISPI_CONTROL_STATUS = 0x31c6
AR0330_REG_COMPRESSION         = 0x31d0

AR0330_FULL_COLS = 2304
AR0330_FULL_ROWS = 1296

-- 1080p30 register set.
function AR0330SetupOptimized()
  I2CWrite16(AR0330_DEV, AR0330_REG_HISPI_CONTROL_STATUS, 0x8400)  -- hispi_control
  I2CWrite16(AR0330_DEV, AR0330_REG_SMIA_TEST, 0x1802)             -- disable embedded data
  I2CWrite16(AR0330_DEV, AR0330_REG_DATA_FORMAT_BITS, 0x0a0a)      -- data width
  I2CWrite16(AR0330_DEV, AR0330_REG_COMPRESSION, 0x0000)           -- disable compression
  I2CWrite16(AR0330_DEV, AR0330_REG_DATAPATH_SELECT, 0x0210)       -- datapath select
  I2CWrite16(AR0330_DEV, AR0330_REG_VT_PIX_CLK_DIV, 0x0005)
  I2CWrite16(AR0330_DEV, AR0330_REG_PRE_PLL_CLK_DIV, 0x0002)
  I2CWrite16(AR0330_DEV, AR0330_REG_PLL_MULTIPLIER, 0x0028)
  I2CWrite16(AR0330_DEV, AR0330_REG_OP_SYS_CLK_DIV, 0x0001)
  I2CWrite16(AR0330_DEV, AR0330_REG_OP_PIX_CLK_DIV, 0x000a)        -- op_pix_clk_div (data width)
  I2CWrite16(AR0330_DEV, AR0330_REG_COARSE_INTEGRATION, 0x0400)    -- exposure (400 sensor+lens)
  I2CWrite16(AR0330_DEV, AR0330_REG_ANALOG_GAIN, 0x0018)           -- ISO 400
  I2CWrite16(AR0330_DEV, AR0330_REG_TEST_PATTERN_MODE, 0x0000)     -- disable test pattern
  I2CWrite16(AR0330_DEV, AR0330_REG_MODE_SELECT, 0x0100)           -- enable streaming
end

function AR0330Initialize()
  I2CInit()
  AR0330SetupOptimized()
end

function AR0330ReadChipId()
  I2CInit()
  return I2CRead16(AR0330_DEV, AR0330_REG_CHIP_VERSION)
end

-- Exposure in ms -> COARSE_INTEGRATION_TIME row periods.
function AR0330SetExposure(exposureMs)
  local pixClkNs = 34.0
  local lineLengthPck = I2CRead16(AR0330_DEV, AR0330_REG_LINE_LENGTH_PCK)
  local integ = math.floor((exposureMs * 1000000.0) / (lineLengthPck * pixClkNs))
  I2CWrite16(AR0330_DEV, AR0330_REG_COARSE_INTEGRATION, integ & 0xffff)
end

-- AR0330 always captures the full sensor area; output resolution is via skips.
function AR0330SetSize()
  I2CWrite16(AR0330_DEV, AR0330_REG_X_ADDR_END, (AR0330_FULL_COLS + 6 - 1) & 0xffff)
  I2CWrite16(AR0330_DEV, AR0330_REG_Y_ADDR_END, (AR0330_FULL_ROWS + 124 - 1) & 0xffff)
end

local function ar0330OddInc(skip)
  if skip == 0 then return 1 elseif skip == 1 then return 3 elseif skip == 2 then return 5 end
  error("AR0330 unsupported skip " .. tostring(skip))
end

function AR0330SetSkips(xSkip, ySkip)
  I2CWrite16(AR0330_DEV, AR0330_REG_X_ODD_INC, ar0330OddInc(xSkip))
  I2CWrite16(AR0330_DEV, AR0330_REG_Y_ODD_INC, ar0330OddInc(ySkip))
end

-- =====================================================================================
-- IP drivers (TPG / ISP / histogram / stream-switch / video DMA).
-- =====================================================================================
TPG_BASE = 0x59400000
TPG_CONTROL = 0x00; TPG_ACTIVE_HEIGHT = 0x10; TPG_ACTIVE_WIDTH = 0x18
TPG_BG_PATTERN_ID = 0x20; TPG_MOTION_SPEED = 0x38; TPG_ENABLE_INPUT = 0x98
TPG_START = 0x01; TPG_AUTO_RESTART = 0x80
TPG_PATTERN_PASSTHROUGH = 0x00

function TpgSetResolution(w, h)
  AxiWrite32(TPG_BASE + TPG_ACTIVE_WIDTH, w)
  AxiWrite32(TPG_BASE + TPG_ACTIVE_HEIGHT, h)
end
function TpgSetPattern(p) AxiWrite32(TPG_BASE + TPG_BG_PATTERN_ID, p) end
function TpgSetMotionSpeed(s) AxiWrite32(TPG_BASE + TPG_MOTION_SPEED, s) end
function TpgStart(enableInput)
  local ctrl = AxiRead32(TPG_BASE + TPG_CONTROL)
  AxiWrite32(TPG_BASE + TPG_CONTROL, ctrl | TPG_START | TPG_AUTO_RESTART)
  AxiWrite32(TPG_BASE + TPG_ENABLE_INPUT, enableInput and 1 or 0)
end
function TpgStop()
  local ctrl = AxiRead32(TPG_BASE + TPG_CONTROL)
  AxiWrite32(TPG_BASE + TPG_CONTROL, ctrl & ~(TPG_START | TPG_AUTO_RESTART))
end

ISP_BASE = 0x4ce00000
ISP_CTRL = 0x00; ISP_HEIGHT = 0x10; ISP_WIDTH = 0x18
ISP_RGAIN = 0x20; ISP_GGAIN = 0x28; ISP_BGAIN = 0x30; ISP_AWB_THRESH = 0x38
ISP_START = 0x01; ISP_AUTO_RESTART = 0x80

function IspInitialize(w, h, awb, rg, gg, bg)
  AxiWrite32(ISP_BASE + ISP_AWB_THRESH, awb)
  AxiWrite32(ISP_BASE + ISP_RGAIN, rg)
  AxiWrite32(ISP_BASE + ISP_GGAIN, gg)
  AxiWrite32(ISP_BASE + ISP_BGAIN, bg)
  AxiWrite32(ISP_BASE + ISP_HEIGHT, h)
  AxiWrite32(ISP_BASE + ISP_WIDTH, w)
end
function IspStart()
  local ctrl = AxiRead32(ISP_BASE + ISP_CTRL)
  AxiWrite32(ISP_BASE + ISP_CTRL, ctrl | ISP_START | ISP_AUTO_RESTART)
end
function IspStop()
  local ctrl = AxiRead32(ISP_BASE + ISP_CTRL)
  AxiWrite32(ISP_BASE + ISP_CTRL, ctrl & ~(ISP_START | ISP_AUTO_RESTART))
end

HIST_BASE = 0x51000000
HIST_CTRL = 0x00; HIST_ROWS = 0x10; HIST_COLS = 0x18
HIST_START = 0x01; HIST_AUTO_RESTART = 0x80
function HistInitialize(rows, cols)
  AxiWrite32(HIST_BASE + HIST_ROWS, rows)
  AxiWrite32(HIST_BASE + HIST_COLS, cols)
end
function HistStart()
  local ctrl = AxiRead32(HIST_BASE + HIST_CTRL)
  AxiWrite32(HIST_BASE + HIST_CTRL, ctrl | HIST_START | HIST_AUTO_RESTART)
end
function HistStop()
  local ctrl = AxiRead32(HIST_BASE + HIST_CTRL)
  AxiWrite32(HIST_BASE + HIST_CTRL, ctrl & ~(HIST_START | HIST_AUTO_RESTART))
end

SWITCH_BASE = 0x55200000
SWITCH_CTRL = 0x00; SWITCH_MI0_MUX = 0x40; SWITCH_CTRL_UPDATE = 0x02
function SwitchSetSlave(slave)
  AxiWrite32(SWITCH_BASE + SWITCH_MI0_MUX, slave)
  AxiWrite32(SWITCH_BASE + SWITCH_CTRL, SWITCH_CTRL_UPDATE)
end

VDMA_BASE = 0x44a00000
VDMA_MM2S_CR = 0x00; VDMA_MM2S_SR = 0x04; VDMA_PARKPTR = 0x28
VDMA_S2MM_CR = 0x30; VDMA_S2MM_SR = 0x34
VDMA_MM2S_VSIZE = 0x50; VDMA_MM2S_HSIZE = 0x54; VDMA_MM2S_STRIDE = 0x58
VDMA_MM2S_ADDR1 = 0x5c; VDMA_MM2S_ADDR2 = 0x60; VDMA_MM2S_ADDR3 = 0x64
VDMA_S2MM_VSIZE = 0xa0; VDMA_S2MM_HSIZE = 0xa4; VDMA_S2MM_STRIDE = 0xa8
VDMA_S2MM_ADDR1 = 0xac; VDMA_S2MM_ADDR2 = 0xb0; VDMA_S2MM_ADDR3 = 0xb4
VDMA_CR_RUNSTOP = 0x01; VDMA_CR_RESET = 0x04

local function vdmaPollHalted(srAddr, timeoutMs)
  for _ = 1, timeoutMs do
    if (AxiRead32(srAddr) & 0x1) ~= 0 then return true end
    Sleep(1)
  end
  return false
end
function VdmaStopWrite()
  local cr = AxiRead32(VDMA_BASE + VDMA_S2MM_CR)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_CR, cr & ~VDMA_CR_RUNSTOP)
  return vdmaPollHalted(VDMA_BASE + VDMA_S2MM_SR, 1000)
end
function VdmaStopRead()
  local cr = AxiRead32(VDMA_BASE + VDMA_MM2S_CR)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_CR, cr & ~VDMA_CR_RUNSTOP)
  return vdmaPollHalted(VDMA_BASE + VDMA_MM2S_SR, 1000)
end
function VdmaSoftReset()
  AxiWrite32(VDMA_BASE + VDMA_MM2S_CR, VDMA_CR_RESET)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_CR, VDMA_CR_RESET)
end
function VdmaStartWrite(widthBytes, h, b0, b1, b2)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_CR, 0x8b)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_HSIZE, widthBytes)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_STRIDE, widthBytes)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_ADDR1, b0)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_ADDR2, b1)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_ADDR3, b2)
  AxiWrite32(VDMA_BASE + VDMA_S2MM_VSIZE, h)   -- writing VSIZE starts S2MM
end
function VdmaStartRead(widthBytes, h, b0, b1, b2)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_CR, 0x8b)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_HSIZE, widthBytes)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_STRIDE, widthBytes)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_ADDR1, b0)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_ADDR2, b1)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_ADDR3, b2)
  local pp = AxiRead32(VDMA_BASE + VDMA_PARKPTR)
  AxiWrite32(VDMA_BASE + VDMA_PARKPTR, (pp & ~0xf) | 0x1)   -- park on frame 1
  AxiWrite32(VDMA_BASE + VDMA_MM2S_VSIZE, h)   -- writing VSIZE starts MM2S
end
function VdmaClearStatus()
  AxiWrite32(VDMA_BASE + VDMA_S2MM_SR, 0xffffffff)
  AxiWrite32(VDMA_BASE + VDMA_MM2S_SR, 0xffffffff)
end
function VdmaGetWriteStatus() return AxiRead32(VDMA_BASE + VDMA_S2MM_SR) end

-- =====================================================================================
-- Capture pipeline sequencer + capture.
-- =====================================================================================
BYTES_PER_PIXEL = 3
DDR_BASE = 0x80000000
HIST_SAMPLES = 256 * 3      -- 768 u32
STREAM_PIPE = 0xa0
STREAM_BLOCK = 1024

function SeqStopPipeline()
  VdmaStopWrite()
  VdmaStopRead()
  TpgStop(); IspStop(); HistStop()
  AxiResetSystem()
  VdmaSoftReset()
  Sleep(100)
end

function SeqConfigureIPs(w, h)
  TpgSetResolution(w, h)
  TpgSetPattern(g_pattern)
  TpgSetMotionSpeed(g_motionSpeed)
  IspInitialize(w, h, g_awb, g_r, g_g, g_b)
  HistInitialize(h, w)        -- rows=height, cols=width
end

function SeqFlushFrame(w, h)
  local frameSize = w * h * BYTES_PER_PIXEL
  SwitchSetSlave(0)
  local buf = OpalKelly.Buffer(frameSize)
  okFP:ReadFromBlockPipeOut(STREAM_PIPE, STREAM_BLOCK, frameSize, buf)
  local histBytes = HIST_SAMPLES * 4
  SwitchSetSlave(1)
  local hbuf = OpalKelly.Buffer(histBytes)
  okFP:ReadFromBlockPipeOut(STREAM_PIPE, STREAM_BLOCK, histBytes, hbuf)
end

function SeqStartupPipeline(widthBytes, h, b0, b1, b2, w, enableInput)
  VdmaStartWrite(widthBytes, h, b0, b1, b2)   -- S2MM first
  TpgStart(enableInput)
  IspStart()
  HistStart()
  VdmaStartRead(widthBytes, h, b0, b1, b2)     -- then MM2S
  for _ = 1, 10 do
    Sleep(50)
    if (VdmaGetWriteStatus() & (1 << 4)) ~= 0 then break end  -- expected VDMAIntErr
  end
  VdmaClearStatus()
  SeqFlushFrame(w, h)                           -- buffer 0 never written in triple-buffer mode
end

function SeqReconfigure(w, h, enableInput)
  local widthBytes = w * BYTES_PER_PIXEL
  local frameSize = widthBytes * h
  local b0 = DDR_BASE
  local b1 = b0 + frameSize
  local b2 = b1 + frameSize
  SeqStopPipeline()
  I2CInit()                  -- re-initialize I2C
  SeqConfigureIPs(w, h)
  SeqStartupPipeline(widthBytes, h, b0, b1, b2, w, enableInput)
end

-- Capture one frame: route video (SI0), read image; then read histogram (SI1). Returns the GBR
-- image Buffer (byte0=G, byte1=B, byte2=R) and the histogram Buffer (HIST_SAMPLES u32, GBR order).
function SeqCaptureFrame()
  local frameSize = g_width * g_height * BYTES_PER_PIXEL
  SwitchSetSlave(0)
  local img = OpalKelly.Buffer(frameSize)
  okFP:ReadFromBlockPipeOut(STREAM_PIPE, STREAM_BLOCK, frameSize, img)
  SwitchSetSlave(1)
  local hbuf = OpalKelly.Buffer(HIST_SAMPLES * 4)
  okFP:ReadFromBlockPipeOut(STREAM_PIPE, STREAM_BLOCK, HIST_SAMPLES * 4, hbuf)
  return img, hbuf
end

-- Full AR0330 bring-up + pipeline (szgcam).
function CameraStartAR0330(w, h, expMs, rg, gg, bg, awb)
  g_width = w; g_height = h
  g_pattern = TPG_PATTERN_PASSTHROUGH
  g_motionSpeed = 3
  g_r = rg; g_g = gg; g_b = bg; g_awb = awb
  g_exposureMs = expMs; g_exposure = expMs
  AR0330Initialize()         -- sensor I2C init + setup (streaming) BEFORE the stream reset
  AR0330SetExposure(expMs)
  AR0330SetSize()
  AR0330SetSkips(0, 0)
  SeqReconfigure(w, h, true) -- enableInput=true (camera passthrough)
  AR0330SetExposure(expMs)   -- re-apply after reset
  return 1
end

-- =====================================================================================
-- OV5640 (SZG-MIPI / Pcam) sensor control. 16-bit reg / 8-bit data
-- on I2C device 0x3c (7-bit).
-- =====================================================================================
OV5640_DEV = 0x3c
OV5640_REG_CHIP_ID_HI = 0x300a
OV5640_REG_CHIP_ID_LO = 0x300b

local function ov5640WriteSeq(seq)
  for _, kv in ipairs(seq) do I2CWrite8(OV5640_DEV, kv[1], kv[2]) end
end
local function ov8(reg, data) I2CWrite8(OV5640_DEV, reg, data) end

function Ov5640Init()
  ov5640WriteSeq({
    {0x3008,0x42},{0x3103,0x03},{0x3017,0x00},{0x3018,0x00},{0x3034,0x18},
    {0x3035,0x11},{0x3036,0x38},{0x3037,0x11},{0x3108,0x01},{0x303d,0x10},
    {0x303b,0x19},{0x3630,0x2e},{0x3631,0x0e},{0x3632,0xe2},{0x3633,0x23},
    {0x3621,0xe0},{0x3704,0xa0},{0x3703,0x5a},{0x3715,0x78},{0x3717,0x01},
    {0x370b,0x60},{0x3705,0x1a},{0x3905,0x02},{0x3906,0x10},{0x3901,0x0a},
    {0x3731,0x02},{0x3600,0x37},{0x3601,0x33},{0x302d,0x60},{0x3620,0x52},
    {0x371b,0x20},{0x471c,0x50},{0x3a13,0x43},{0x3a18,0x00},{0x3a19,0xf8},
    {0x3635,0x13},{0x3636,0x06},{0x3634,0x44},{0x3622,0x01},{0x3c01,0x34},
    {0x3c04,0x28},{0x3c05,0x98},{0x3c06,0x00},{0x3c07,0x08},{0x3c08,0x00},
    {0x3c09,0x1c},{0x3c0a,0x9c},{0x3c0b,0x40},{0x503d,0x00},{0x3820,0x46},
    {0x300e,0x45},{0x4800,0x14},{0x302e,0x08},{0x4300,0x6f},{0x501f,0x01},
    {0x4713,0x03},{0x4407,0x04},{0x440e,0x00},{0x460b,0x35},{0x460c,0x20},
    {0x3824,0x01},{0x5000,0x07},{0x5001,0x03},
  })
end

function Ov5640AwbInit()
  ov8(0x3008, 0x42)  -- power down
  ov5640WriteSeq({
    {0x3406,0x00},{0x5192,0x04},{0x5191,0xf8},{0x518d,0x26},{0x518f,0x42},
    {0x518e,0x2b},{0x5190,0x42},{0x518b,0xd0},{0x518c,0xbd},{0x5187,0x18},
    {0x5188,0x18},{0x5189,0x56},{0x518a,0x5c},{0x5186,0x1c},{0x5181,0x50},
    {0x5184,0x20},{0x5182,0x11},{0x5183,0x00},
  })
  ov8(0x3008, 0x02)  -- power on
end

function Ov5640Setup1080p()
  ov8(0x3008, 0x42)
  ov8(0x3035, 0x21); ov8(0x3036, 0x69); ov8(0x3037, 0x05); ov8(0x3108, 0x11); ov8(0x3034, 0x1a)
  ov8(0x3800, (336 >> 8) & 0x0f); ov8(0x3801, 336 & 0xff)      -- crop (336,426)-(2287,1529)
  ov8(0x3802, (426 >> 8) & 0x07); ov8(0x3803, 426 & 0xff)
  ov8(0x3804, (2287 >> 8) & 0x0f); ov8(0x3805, 2287 & 0xff)
  ov8(0x3806, (1529 >> 8) & 0x07); ov8(0x3807, 1529 & 0xff)
  ov8(0x3810, (16 >> 8) & 0x0f); ov8(0x3811, 16 & 0xff)        -- offset (16,12)
  ov8(0x3812, (12 >> 8) & 0x07); ov8(0x3813, 12 & 0xff)
  ov8(0x3808, (1920 >> 8) & 0x0f); ov8(0x3809, 1920 & 0xff)    -- size 1920x1080
  ov8(0x380a, (1080 >> 8) & 0x7f); ov8(0x380b, 1080 & 0xff)
  ov8(0x380c, (2500 >> 8) & 0x1f); ov8(0x380d, 2500 & 0xff)    -- HTS=2500 VTS=1120
  ov8(0x380e, (1120 >> 8) & 0xff); ov8(0x380f, 1120 & 0xff)
  ov8(0x3814, 0x11); ov8(0x3815, 0x11); ov8(0x3821, 0x00)      -- no binning/mirror
  ov8(0x4837, 24); ov8(0x3618, 0x00); ov8(0x3612, 0x59); ov8(0x3708, 0x64); ov8(0x3709, 0x52)
  ov8(0x370c, 0x03); ov8(0x4300, 0x00); ov8(0x501f, 0x03)      -- RGB output via ISP
  ov8(0x3008, 0x02)
end

-- 1280x720 60fps (3:1 binning both directions, vertical mirror).
function Ov5640Setup720p()
  ov8(0x3008, 0x42)
  ov8(0x3035, 0x21); ov8(0x3036, 0x46); ov8(0x3037, 0x05); ov8(0x3108, 0x11); ov8(0x3034, 0x1a)
  ov8(0x3800, (0 >> 8) & 0x0f); ov8(0x3801, 0 & 0xff)          -- crop (0,8)-(2619,1947)
  ov8(0x3802, (8 >> 8) & 0x07); ov8(0x3803, 8 & 0xff)
  ov8(0x3804, (2619 >> 8) & 0x0f); ov8(0x3805, 2619 & 0xff)
  ov8(0x3806, (1947 >> 8) & 0x07); ov8(0x3807, 1947 & 0xff)
  ov8(0x3810, (0 >> 8) & 0x0f); ov8(0x3811, 0 & 0xff)          -- offset (0,0)
  ov8(0x3812, (0 >> 8) & 0x07); ov8(0x3813, 0 & 0xff)
  ov8(0x3808, (1280 >> 8) & 0x0f); ov8(0x3809, 1280 & 0xff)    -- size 1280x720
  ov8(0x380a, (720 >> 8) & 0x7f); ov8(0x380b, 720 & 0xff)
  ov8(0x380c, (1896 >> 8) & 0x1f); ov8(0x380d, 1896 & 0xff)    -- HTS=1896 VTS=984
  ov8(0x380e, (984 >> 8) & 0xff); ov8(0x380f, 984 & 0xff)
  ov8(0x3814, 0x31); ov8(0x3815, 0x31); ov8(0x3821, 0x01)      -- 3:1 binning, v-mirror
  ov8(0x4837, 36); ov8(0x3618, 0x00); ov8(0x3612, 0x59); ov8(0x3708, 0x64); ov8(0x3709, 0x52)
  ov8(0x370c, 0x03); ov8(0x4300, 0x00); ov8(0x501f, 0x03)      -- RGB output via ISP
  ov8(0x3008, 0x02)
end

function Ov5640SetResolution(w, h)
  if w == 1920 and h == 1080 then Ov5640Setup1080p()
  elseif w == 1280 and h == 720 then Ov5640Setup720p()
  else error(string.format("Unsupported OV5640 resolution: %dx%d", w, h)) end
end

-- AEC luminance target 0..247, power-cycled so the AEC latches + MIPI re-establishes after reset.
function Ov5640SetExposure(target)
  local v = math.max(0, math.min(247, math.floor(target)))
  ov8(0x3008, 0x42)
  ov8(0x3a0f, (v + 8) & 0xff); ov8(0x3a10, v & 0xff)
  ov8(0x3a1b, (v + 8) & 0xff); ov8(0x3a1e, v & 0xff)
  ov8(0x3008, 0x02)
end

function Ov5640Initialize()
  I2CInit()
  Ov5640Init()
  Ov5640AwbInit()
  Ov5640Setup1080p()
end

function Ov5640ReadChipId()
  I2CInit()
  local hi = I2CRead8(OV5640_DEV, OV5640_REG_CHIP_ID_HI)
  local lo = I2CRead8(OV5640_DEV, OV5640_REG_CHIP_ID_LO)
  return (hi << 8) | lo
end

-- Full OV5640 bring-up + pipeline (pcam).
function CameraStartOV5640(w, h, expTarget, rg, gg, bg, awb)
  g_width = w; g_height = h
  g_pattern = TPG_PATTERN_PASSTHROUGH
  g_motionSpeed = 3
  g_r = rg; g_g = gg; g_b = bg; g_awb = awb
  g_exposure = expTarget
  Ov5640Initialize()         -- sensor I2C init + setup (streaming) BEFORE the stream reset
  Ov5640SetExposure(expTarget)
  SeqReconfigure(w, h, true)
  Ov5640SetExposure(expTarget)  -- re-apply after reset
  return 1
end

-- ===== Camera-agnostic dispatch =====
function CameraStart(kind, w, h, exp, rg, gg, bg, awb)
  if kind == "szgcam" then return CameraStartAR0330(w, h, exp, rg, gg, bg, awb)
  elseif kind == "pcam" then return CameraStartOV5640(w, h, exp, rg, gg, bg, awb)
  else error("CameraStart: unknown camera kind '" .. tostring(kind) .. "'") end
end

function ReadChipId(kind)
  if kind == "szgcam" then return AR0330ReadChipId()
  elseif kind == "pcam" then return Ov5640ReadChipId()
  else error("ReadChipId: unknown camera kind '" .. tostring(kind) .. "'") end
end

-- Live setting updates (no pipeline reset), for the GUI sliders over the scripted path.
function SetGainsLive(rg, gg, bg)
  AxiWrite32(ISP_BASE + ISP_RGAIN, rg)
  AxiWrite32(ISP_BASE + ISP_GGAIN, gg)
  AxiWrite32(ISP_BASE + ISP_BGAIN, bg)
  return 1
end
function SetAwbLive(awb) AxiWrite32(ISP_BASE + ISP_AWB_THRESH, awb); return 1 end
function SetExposureLive(kind, exp)
  g_exposure = exp
  if kind == "szgcam" then AR0330SetExposure(exp)
  elseif kind == "pcam" then Ov5640SetExposure(exp) end
  return 1
end

-- Live TPG pattern (0x00 passthrough, 0x01 horizontal ramp, ...). Persists via g_pattern.
function SetPatternLive(p)
  g_pattern = p
  TpgSetPattern(p)
  return 1
end

-- Live TPG motion speed (animates the ramp/pattern). Persists via g_motionSpeed.
function SetMotionSpeedLive(s)
  g_motionSpeed = s
  TpgSetMotionSpeed(s)
  return 1
end

-- Live resolution change. The pipeline is live, so quiesce first, then fully re-target the sensor
-- (pcam: full re-init at the new size + re-apply exposure; szgcam: skips), then reconfigure the
-- pipeline. Mirrors the known-good CameraStart bring-up. g_pattern is preserved.
function SetResolutionLive(kind, w, h, xskip, yskip)
  SeqStopPipeline()
  if kind == "pcam" then
    Ov5640SetResolution(w, h)
    Ov5640SetExposure(g_exposure)
  elseif kind == "szgcam" then
    AR0330SetSkips(xskip, yskip)
  end
  g_width = w; g_height = h
  SeqReconfigure(w, h, true)
  -- Re-apply exposure AFTER the reset (as CameraStart does); this re-latches the sensor stream so
  -- the first frames are live, not blank.
  if kind == "pcam" then Ov5640SetExposure(g_exposure)
  elseif kind == "szgcam" then AR0330SetExposure(g_exposure) end
  return 1
end

-- Capture a frame after warmupFrames discarded server-side (lets the AEC settle WITHOUT transferring
-- each warmup frame over FPoIP — only the final frame crosses the network). Returns the GBR Buffer.
function CaptureFrame(warmupFrames)
  for _ = 1, warmupFrames do SeqCaptureFrame() end
  return SeqCaptureFrame()
end
CaptureAR0330Frame = CaptureFrame  -- alias

-- Called once by the C++ host after LoadFile, before any AXI op.
function Setup()
  AxiSetTimeout()
  return 1
end
