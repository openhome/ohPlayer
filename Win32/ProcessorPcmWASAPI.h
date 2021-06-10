#pragma once

#include "WWMFResampler.h"

namespace OpenHome {
namespace Media {

class ProcessorPcmBufWASAPI : public IPcmProcessor
{
public:
    ProcessorPcmBufWASAPI();

    const Brx& Buf() const;
    const TByte* Ptr() const;

    void SetBitDepth(TUint aBitDepth);

private: // from IPcmProcessor
    void BeginBlock() override;
    void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void EndBlock() override;
    void Flush() override;

private:
    void CheckSize(TUint aAdditionalBytes);
    void ProcessFragment(const Brx& aData);

    void ProcessFragment16(const Brx& aData, TUint aNumChannels);
    void ProcessFragment24(const Brx& aData, TUint aNumChannels);
    void ProcessFragment32(const Brx& aData, TUint aNumChannels);

private:
    Bwh iBuf;
    TUint iBitDepth;
};

} // namespace Media
} // namespace OpenHome
