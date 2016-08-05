#pragma once

#include "WWMFResampler.h"

namespace OpenHome {
namespace Media {

class ProcessorPcmBufWASAPI : public ProcessorPcmBufTest
{
public:
    ProcessorPcmBufWASAPI();
    void SetBitDepth(TUint bitDepth);
private: // from IPcmProcessor
    void ProcessFragment8(const Brx& aData, TUint aNumChannels);
    void ProcessFragment16(const Brx& aData, TUint aNumChannels);
    void ProcessFragment24(const Brx& aData, TUint aNumChannels);
    void ProcessFragment32(const Brx& aData, TUint aNumChannels);
private:
    TUint iBitDepth;
};

} // namespace Media
} // namespace OpenHome
