#pragma once

namespace OpenHome {
namespace Media {

class ProcessorPcmBufWASAPI : public ProcessorPcmBuf
{
public:
    ProcessorPcmBufWASAPI(bool duplicateChannel);
private: // from IPcmProcessor
    TBool ProcessFragment8(const Brx& aData, TUint aNumChannels);
    TBool ProcessFragment16(const Brx& aData, TUint aNumChannels);
    TBool ProcessFragment24(const Brx& aData, TUint aNumChannels);
    void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    void ProcessSample24(const TByte* aSample, TUint aNumChannels);

    bool iDuplicateChannel; // True if a mono input should be converted to streoe.
};

} // namespace Media
} // namespace OpenHome
