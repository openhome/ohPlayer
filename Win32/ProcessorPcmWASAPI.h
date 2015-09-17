#pragma once

#include "WWMFResampler.h"

namespace OpenHome {
namespace Media {

class ProcessorPcmBufWASAPI : public ProcessorPcmBufTest
{
public:
    ProcessorPcmBufWASAPI();
private: // from IPcmProcessor
    void ProcessFragment8(const Brx& aData, TUint aNumChannels);
    void ProcessFragment16(const Brx& aData, TUint aNumChannels);
    void ProcessFragment24(const Brx& aData, TUint aNumChannels);
    void ProcessFragment32(const Brx& aData, TUint aNumChannels);
    void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    void ProcessSample24(const TByte* aSample, TUint aNumChannels);
    void ProcessSample32(const TByte* aSample, TUint aNumChannels);
};

} // namespace Media
} // namespace OpenHome
