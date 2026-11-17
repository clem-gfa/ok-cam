// Minimal stand-in for the FrontPanel SDK header (okFrontPanel.h), ONLY for the hardware-free tests: it declares the
// few FPGADataPortClassic calls that AudioPipe.h uses, so that the code can be compiled and exercised without the SDK.
// The real SDK header is used for the application itself.
#pragma once
#include <cstdint>
#include <map>

namespace OpalKelly {
class FPGADataPortClassic {
public:
    virtual ~FPGADataPortClassic() = default;
    virtual int SetWireInValue(int ep, unsigned long val, unsigned long mask) = 0;
    virtual int UpdateWireIns() = 0;
    virtual int UpdateWireOuts() = 0;
    virtual unsigned long GetWireOutValue(int ep) = 0;
    virtual long ReadFromBlockPipeOut(int epAddr, int blockSize, long length, unsigned char* data) = 0;
};
}  // namespace OpalKelly
