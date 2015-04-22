#ifndef HEADER_PROCESSOR_PCM_FOR_WASAPI
#define HEADER_PROCESSOR_PCM_FOR_WASAPI

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

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

    bool _DuplicateChannel;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PROCESSOR_PCM_FOR_WASAPI
