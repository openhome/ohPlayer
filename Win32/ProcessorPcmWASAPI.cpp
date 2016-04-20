#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
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

ProcessorPcmBufWASAPI::ProcessorPcmBufWASAPI() : iBitDepth(0)
{
}

void ProcessorPcmBufWASAPI::SetBitDepth(TUint bitDepth)
{
    iBitDepth = bitDepth;
}

void ProcessorPcmBufWASAPI::ProcessFragment8(const Brx& aData,
                                             TUint /*aNumChannels*/)
{
    ProcessFragment(aData);
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
    ASSERTS();
    TByte *nData;
    TUint  bytes;

    bytes = aData.Bytes();

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = (TByte *)nData;
    TByte *endp = ptr1 + bytes;

    ASSERT(bytes % 4 == 0);

    // Little endian byte order required by native audio.
    TUint outBytes = 0;

    while (ptr1 < endp)
    {
        // The pipeline may generate 32 bit PCM.
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

void ProcessorPcmBufWASAPI::ProcessSample8(const TByte* aSample,
                                           TUint        aNumChannels)
{
    TByte sample[8]  = { 0 };
    TUint sampleSize = 1;

    for (TUint i=0; i<aNumChannels; i++) {
        sample[0] = *aSample;

        aSample++;
        Brn sampleBuf(sample, sampleSize);
        ProcessFragment(sampleBuf);
    }
}

void ProcessorPcmBufWASAPI::ProcessSample16(const TByte* aSample,
                                            TUint        aNumChannels)
{
    TByte sample[8]  = { 0 };
    TUint sampleSize = 2;

    for (TUint i=0; i<aNumChannels; i++) {
        sample[0] = *(aSample + 1);
        sample[1] = *aSample;

        aSample += 2;

        Brn sampleBuf(sample, sampleSize);
        ProcessFragment(sampleBuf);
    }

}

void ProcessorPcmBufWASAPI::ProcessSample24(const TByte* aSample,
                                            TUint        aNumChannels)
{
    TByte sample[8]  = { 0 };
    TUint sampleSize = 3;

    for (TUint i=0; i<aNumChannels; i++) {
        sample[0] = *(aSample + 2);
        sample[1] = *(aSample + 1);
        sample[2] = *aSample;

        aSample += 3;

        Brn sampleBuf(sample, sampleSize);
        ProcessFragment(sampleBuf);
    }
}

void ProcessorPcmBufWASAPI::ProcessSample32(const TByte* aSample,
                                            TUint        aNumChannels)
{
    TByte sample[8]  = { 0 };

    for (TUint i=0; i<aNumChannels; i++) {
        TUint outBytes = 0;
        TUint index    = 0;

        // The pipeline may generate 32 bit PCM.
        // This must be converted to the same format as the audio stream.
        switch (iBitDepth)
        {
            case 32:
            {
                sample[index++] = *(aSample + 3);
                outBytes++;
                // fallthrough
            }
            case 24:
            {
                sample[index++] = *(aSample + 2);
                outBytes++;
                // fallthrough
            }
            case 16:
            {
                sample[index++] = *(aSample + 1);
                outBytes++;
                // fallthrough
            }
            case 8:
            {
                sample[index++] = *(aSample + 0);
                outBytes++;
                break;
            }
        }

        aSample += 4;

        Brn sampleBuf(sample, outBytes);
        ProcessFragment(sampleBuf);
    }
}
