/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// CameraConfig.h — device/camera selection logic.
//
// (DeviceConfiguration, BITFILE_MAP, CAMERA_MODE_MAP, productModelToCameraModel,
// determineConfiguration). Given a board product name + the camera reported on its
// SYZYGY port, this decides which DeviceConfiguration we are in, which bitfile to load,
// and which camera mode the pipeline runs in.
//
// Two boards are supported, distinguished by FrontPanel device interface:
//   * XEM8320-AU25P  — a *classic* device (USB3); AXI is tunneled over the classic data
//                      port via the FrontPanelToAxiLiteBridge gateware.
//   * SZG-HUB1450-AU10P — a *GEN3/AXI* device; AXI is driven natively.
// "Is this the Hub?" is keyed off the FrontPanel device interface == GEN3
// (okEDeviceInterface == okDEVICEINTERFACE_GEN3).

#pragma once

#include <string>

#include "okFrontPanel.h"

#include "CameraTypes.h"  // CameraMode

namespace okcli {

// The supported device configurations.
enum class DeviceConfiguration {
    XEM8320_SZG_Camera,  // XEM8320-AU25P + SZG-CAMERA (AR0330), classic bridge, szgcam bitfile
    XEM8320_PCAM,        // XEM8320-AU25P + SZG-MIPI/Pcam (OV5640), classic bridge, pcam bitfile
    XEM8320_TPG,         // XEM8320-AU25P, no camera, classic bridge, nocam bitfile, TPG mode
    HUB1450_SZG_Camera,  // SZG-HUB1450-AU10P + SZG-CAMERA (AR0330), AXI driver, szgcam bitfile
    HUB1450_PCAM,        // SZG-HUB1450-AU10P + SZG-MIPI/Pcam (OV5640), AXI driver, pcam bitfile
    HUB1450_TPG,         // SZG-HUB1450-AU10P, no camera, AXI driver, nocam bitfile, TPG mode
};

// CameraMode (szgcam/pcam/tpg) is defined in CameraTypes.h.

// The exact strings the gateware/EEPROM reports for SYZYGY0_PRODUCT_MODEL. Anything else is an
// unsupported/absent camera.
namespace CameraProductModel {
constexpr char SZG_CAMERA_AR0330[] = "SZG-CAMERA-AR0330";
constexpr char POD_CAMERA_AR0330[] = "POD-CAMERA-AR0330";
constexpr char SZG_MIPI_8320[]     = "SZG-MIPI-8320";
}  // namespace CameraProductModel

// Board product names as reported by okTDeviceInfo.productName.
constexpr char kProductXEM8320[]  = "XEM8320-AU25P";
constexpr char kProductHUB1450[]  = "SZG-HUB1450-AU10P";

// The SYZYGY setting key + port the camera lives on (Port "A" for both boards).
constexpr char kSyzygyProductModelKey[] = "SYZYGY0_PRODUCT_MODEL";

// productModelToCameraModel(): returns true and leaves `model` set to the recognized camera
// product string, or returns false for an empty/unsupported model.
inline bool productModelToCameraModel(const std::string& modelName, std::string& model) {
    if (modelName == CameraProductModel::SZG_CAMERA_AR0330 ||
        modelName == CameraProductModel::POD_CAMERA_AR0330 ||
        modelName == CameraProductModel::SZG_MIPI_8320) {
        model = modelName;
        return true;
    }
    return false;
}

// determineConfiguration(): the core selection logic. `cameraModel` is the recognized
// camera string ("" if none/unsupported); `isGen3` is true for the SZG-HUB1450 (AXI device).
inline DeviceConfiguration determineConfiguration(const std::string& cameraModel, bool isGen3) {
    if (!cameraModel.empty()) {
        if (isGen3) {
            if (cameraModel == CameraProductModel::SZG_MIPI_8320) return DeviceConfiguration::HUB1450_PCAM;
            return DeviceConfiguration::HUB1450_SZG_Camera;
        }
        if (cameraModel == CameraProductModel::SZG_MIPI_8320) return DeviceConfiguration::XEM8320_PCAM;
        return DeviceConfiguration::XEM8320_SZG_Camera;
    }
    if (isGen3) return DeviceConfiguration::HUB1450_TPG;
    return DeviceConfiguration::XEM8320_TPG;
}

// Bitfile path per configuration. Paths are relative to the bitfiles root (board subdir + file).
inline std::string bitfileFor(DeviceConfiguration config) {
    switch (config) {
        case DeviceConfiguration::XEM8320_SZG_Camera: return "XEM8320-AU25P/camera_szgcam.bit";
        case DeviceConfiguration::XEM8320_PCAM:       return "XEM8320-AU25P/camera_pcam.bit";
        case DeviceConfiguration::XEM8320_TPG:        return "XEM8320-AU25P/camera_nocam.bit";
        case DeviceConfiguration::HUB1450_SZG_Camera: return "SZG-HUB1450-AU10P/camera_szgcam.bit";
        case DeviceConfiguration::HUB1450_PCAM:       return "SZG-HUB1450-AU10P/camera_pcam.bit";
        case DeviceConfiguration::HUB1450_TPG:        return "SZG-HUB1450-AU10P/camera_nocam.bit";
    }
    return "";
}

// Per-board bitfile subdirectory (matches BITFILE_MAP prefixes). "" if unknown board.
inline std::string boardSubdir(const std::string& productName) {
    if (productName == kProductXEM8320) return "XEM8320-AU25P";
    if (productName == kProductHUB1450) return "SZG-HUB1450-AU10P";
    return "";
}

// The no-camera/TPG bitfile for a board — which provides the
// AXI infrastructure (FrontPanelToAxiLiteBridge + TPG) without depending on any sensor.
inline std::string nocamBitfile(const std::string& productName) {
    const std::string sub = boardSubdir(productName);
    return sub.empty() ? "" : sub + "/camera_nocam.bit";
}

// Camera mode per configuration.
inline CameraMode cameraModeFor(DeviceConfiguration config) {
    switch (config) {
        case DeviceConfiguration::XEM8320_SZG_Camera:
        case DeviceConfiguration::HUB1450_SZG_Camera: return CameraMode::SzgCam;
        case DeviceConfiguration::XEM8320_PCAM:
        case DeviceConfiguration::HUB1450_PCAM:       return CameraMode::Pcam;
        case DeviceConfiguration::XEM8320_TPG:
        case DeviceConfiguration::HUB1450_TPG:        return CameraMode::Tpg;
    }
    return CameraMode::Tpg;
}

// --- small display helpers ----------------------------------------------------------------

inline const char* toString(DeviceConfiguration c) {
    switch (c) {
        case DeviceConfiguration::XEM8320_SZG_Camera: return "XEM8320_SZG_Camera";
        case DeviceConfiguration::XEM8320_PCAM:       return "XEM8320_PCAM";
        case DeviceConfiguration::XEM8320_TPG:        return "XEM8320_TPG";
        case DeviceConfiguration::HUB1450_SZG_Camera: return "HUB1450_SZG_Camera";
        case DeviceConfiguration::HUB1450_PCAM:       return "HUB1450_PCAM";
        case DeviceConfiguration::HUB1450_TPG:        return "HUB1450_TPG";
    }
    return "?";
}

inline const char* toString(CameraMode m) {
    switch (m) {
        case CameraMode::SzgCam: return "szgcam";
        case CameraMode::Pcam:   return "pcam";
        case CameraMode::Tpg:    return "tpg";
    }
    return "?";
}

// Friendly, customer-facing peripheral name for a camera mode (used in GUI labels).
inline const char* cameraModeFriendly(CameraMode m) {
    switch (m) {
        case CameraMode::SzgCam: return "SZG-Camera";
        case CameraMode::Pcam:   return "SZG-MIPI-8320";
        case CameraMode::Tpg:    return "Test Pattern";
    }
    return "?";
}

// okEDeviceInterface is `typedef int` in this header config, so compare against the
// always-defined OK_INTERFACE_* macros (the okDEVICEINTERFACE_* enumerators are only
// present in the other build branch).
inline const char* interfaceName(okEDeviceInterface iface) {
    switch (iface) {
        case OK_INTERFACE_USB2: return "USB2";
        case OK_INTERFACE_USB3: return "USB3";
        case OK_INTERFACE_PCIE: return "PCIE";
        case OK_INTERFACE_GEN3: return "GEN3";
        default:                return "UNKNOWN";
    }
}

}  // namespace okcli
