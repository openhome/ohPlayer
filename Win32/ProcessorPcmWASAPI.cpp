#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>
#include <OpenHome/Private/Printer.h>

#include "MemoryCheck.h"
#include "ProcessorPcmWASAPI.h"
#include "WWMFResampler.h"

using namespace OpenHome;
using namespace OpenHome::Media;

// WASAPI based implementation of the ProcessorPcmBuf interface
//
// Takes PCM data from the pipeline and formats it for the WASAPI
// audio subsystem.

ProcessorPcmBufWASAPI::ProcessorPcmBufWASAPI()
    : iBuf(DecodedAudio::kMaxBytes)
{ }


const Brx& ProcessorPcmBufWASAPI::Buf() const
{
    return iBuf;
}

const TByte* ProcessorPcmBufWASAPI::Ptr() const
{
    return iBuf.Ptr();
}

void ProcessorPcmBufWASAPI::SetBitDepth(TUint aBitDepth)
{
    iBitDepth = aBitDepth;
}


void ProcessorPcmBufWASAPI::BeginBlock()
{
    iBuf.SetBytes(0);
}

void ProcessorPcmBufWASAPI::ProcessSilence(const Brx& aData, 
                                           TUint aNumChannels, 
                                           TUint aSubsampleBytes)
{
    ProcessFragment(aData, aNumChannels, aSubsampleBytes);
}

void ProcessorPcmBufWASAPI::EndBlock()
{ }

void ProcessorPcmBufWASAPI::Flush()
{ }


void ProcessorPcmBufWASAPI::CheckSize(TUint aAdditionalBytes)
{
    while (iBuf.Bytes() + aAdditionalBytes > iBuf.MaxBytes()) {
        const TUint size = iBuf.MaxBytes() + DecodedAudio::kMaxBytes;
        iBuf.Grow(size);
    }
}

void ProcessorPcmBufWASAPI::ProcessFragment(const Brx& aData)
{
    CheckSize(aData.Bytes());
    iBuf.Append(aData);
}


void ProcessorPcmBufWASAPI::ProcessFragment(const Brx& aData, 
                                            TUint aNumChannels, 
                                            TUint /*aSubsampleBytes*/)
{
    switch (iBitDepth)
    {
        case 8: {
            ProcessFragment(aData);
            break;
        }
        case 16: {
            ProcessFragment16(aData, aNumChannels);
            break;
        }
        case 24: {
            ProcessFragment24(aData, aNumChannels);
            break;
        }
        case 32: {
            ProcessFragment32(aData, aNumChannels);
            break;
        }
        default: {
            ASSERT_VA(false, "%s", "Unknown bit depth.");
            break;
        }  
    }
}

void ProcessorPcmBufWASAPI::ProcessFragment16(const Brx& aData,
                                              TUint      /*aNumChannels*/)
{
    TByte *nData;
    TUint  bytes;

    bytes = aData.Bytes();

    nData = new TByte[bytes];

    if (nData == NULL)
    {
        return;
    }

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = (TByte *)nData;
    TByte *endp = ptr1 + bytes;

    ASSERT(bytes % 2 == 0);

    // Little endian byte order required by native audio.
    while (ptr1 < endp)
    {
        *ptr1++ = *(ptr+1);
        *ptr1++ = *(ptr);

        ptr +=2;
    }

    Brn fragment(nData, bytes);
    ProcessFragment(fragment);

    delete[] nData;
}

void ProcessorPcmBufWASAPI::ProcessFragment24(const Brx& aData,
                                              TUint      /*aNumChannels*/)
{
    TByte *nData;
    TUint  bytes;

    bytes = aData.Bytes();

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = (TByte *)nData;
    TByte *endp = ptr1 + bytes;

    ASSERT(bytes % 3 == 0);

    // Little endian byte order required by native audio.
    while (ptr1 < endp)
    {
        *ptr1++ = *(ptr+2);
        *ptr1++ = *(ptr+1);
        *ptr1++ = *(ptr+0);

        ptr += 3;
    }

    Brn fragment(nData, bytes);
    ProcessFragment(fragment);

    delete[] nData;
}

void ProcessorPcmBufWASAPI::ProcessFragment32(const Brx& aData,
                                              TUint      /*aNumChannels*/)
{
    TByte *nData;
    TUint  bytes;

    bytes = aData.Bytes();

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *endp = ptr + bytes;
    TByte *ptr1 = (TByte *)nData;

    ASSERT(bytes % 4 == 0);

    // Little endian byte order required by native audio.
    TUint outBytes = 0;

    while (ptr < endp)
    {
        // The pipeline may auto-generate 32 bit PCM.
        // This must be converted to the same format as the audio stream.
        switch (iBitDepth)
        {
            case 32:
            {
                *ptr1++ = *(ptr+3);
                outBytes++;
                // fallthrough
            }
            case 24:
            {
                *ptr1++ = *(ptr+2);
                outBytes++;
                // fallthrough
            }
            case 16:
            {
                *ptr1++ = *(ptr+1);
                outBytes++;
                // fallthrough
            }
            case 8:
            {
                *ptr1++ = *(ptr+0);
                outBytes++;
                break;
            }
        }

        ptr += 4;
    }

    Brn fragment(nData, outBytes);
    ProcessFragment(fragment);

    delete[] nData;
}
