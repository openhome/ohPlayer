#pragma once

#include "WWMFResampler.h"

namespace OpenHome {
namespace Media {

class ProcessorPcmBufWASAPI : public ProcessorPcmBuf
{
public:
    ProcessorPcmBufWASAPI(bool resamplingInput, WWMFResampler &resampler);
private: // from IPcmProcessor
    TBool ProcessFragment8(const Brx& aData, TUint aNumChannels);
    TBool ProcessFragment16(const Brx& aData, TUint aNumChannels);
    TBool ProcessFragment24(const Brx& aData, TUint aNumChannels);
    void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    void ProcessSample24(const TByte* aSample, TUint aNumChannels);

    bool          iResamplingInput;  // True if the input should be passed
                                     // through the resampler.
    WWMFResampler &iResampler;       // Resample agent
};

} // namespace Media
} // namespace OpenHome
