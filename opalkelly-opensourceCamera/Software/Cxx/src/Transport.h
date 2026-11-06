/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Transport.h — build the right transport for a board.
//
// The device-type seam: an open okCFrontPanel plus its interface type yields one of the two
// concrete transports. Everything above consumes the IAxiLite / IAxiStream interfaces and is
// otherwise identical across both boards.

#pragma once

#include <memory>

#include "okFrontPanel.h"

#include "Axi.h"
#include "AxiOverAxiDataPort.h"
#include "AxiOverClassicDataPort.h"

namespace okcli {

struct Transport {
    std::unique_ptr<IAxiLite>   axiLite;
    std::unique_ptr<IAxiStream> axiStream;
};

// Build the transport for an already-open, already-configured device. `isGen3` selects the
// native AXI path (SZG-HUB1450) vs. the Classic bridge path (XEM8320). The returned transport
// borrows data ports owned by `fp`, so it must not outlive `fp`.
inline Transport makeTransport(OpalKelly::FrontPanel& fp, bool isGen3) {
    Transport t;
    if (isGen3) {
        OpalKelly::FPGADataPortAXI* port = nullptr;
        if (static_cast<int>(fp.GetFPGADataPortAXI(port)) != 0 || port == nullptr)
            throw AxiError("GetFPGADataPortAXI failed (is the AXI gateware loaded?)");
        t.axiLite   = std::make_unique<AxiLiteOverAxiDataPort>(port);
        t.axiStream = std::make_unique<AxiStreamOverAxiDataPort>(port);
    } else {
        OpalKelly::FPGADataPortClassic* port = nullptr;
        if (static_cast<int>(fp.GetFPGADataPortClassic(port)) != 0 || port == nullptr)
            throw AxiError("GetFPGADataPortClassic failed");
        t.axiLite   = std::make_unique<AxiLiteOverClassicDataPort>(port);
        t.axiStream = std::make_unique<AxiStreamOverClassicDataPort>(port);
    }
    return t;
}

}  // namespace okcli
