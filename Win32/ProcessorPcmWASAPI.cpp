#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>

#include "ProcessorPcmWASAPI.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
   #ifndef DBG_NEW
      #define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
      #define new DBG_NEW
   #endif
#endif  // _DEBUG

using namespace OpenHome;
using namespace OpenHome::Media;

// ProcessorPcmBufWASAP

ProcessorPcmBufWASAPI::ProcessorPcmBufWASAPI(bool duplicateChannel) :
    _DuplicateChannel(duplicateChannel)
{
}

TBool ProcessorPcmBufWASAPI::ProcessFragment8(const Brx& aData, TUint /*aNumChannels*/)
{
    if (!_DuplicateChannel)
    {
        ProcessFragment(aData);
        return true;
    }
    else
    {
        TByte *nData;
        TUint bytes;

        bytes = aData.Bytes() * 2;

        nData = new TByte[bytes];

        if (nData == NULL)
        {
            return false;
        }

        TByte *ptr  = (TByte *)(aData.Ptr() + 0);
        TByte *ptr1 = (TByte *)nData;
        TByte *endp = ptr1 + bytes;

        // Little endian byte order required by native audio.
        while (ptr1 < endp)
        {
            *ptr1++ = *(ptr);
            *ptr1++ = *(ptr);

            ptr++;
        }

        Brn fragment(nData, bytes);
        ProcessFragment(fragment);
        delete nData;

        return true;
    }
}

TBool ProcessorPcmBufWASAPI::ProcessFragment16(const Brx& aData, TUint /*aNumChannels*/)
{
    TByte *nData;
    TUint bytes;

    bytes = aData.Bytes();

    if (_DuplicateChannel)
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];

    if (nData == NULL)
    {
        return false;
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

        if (_DuplicateChannel)
        {
            *ptr1++ = *(ptr+1);
            *ptr1++ = *(ptr);
        }

        ptr +=2;
    }

    Brn fragment(nData, bytes);
    ProcessFragment(fragment);
    delete nData;

    return true;
}

TBool ProcessorPcmBufWASAPI::ProcessFragment24(const Brx& aData, TUint /*aNumChannels*/)
{
    TByte *nData;
    TUint  bytes;

    bytes = aData.Bytes();

    if (_DuplicateChannel)
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];

    if (nData == NULL)
    {
        return false;
    }

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

        if (_DuplicateChannel)
        {
            *ptr1++ = *(ptr+2);
            *ptr1++ = *(ptr+1);
            *ptr1++ = *(ptr+0);
        }

        ptr += 3;
    }

    Brn fragment(nData, bytes);
    ProcessFragment(fragment);
    delete nData;

    return true;
}

void ProcessorPcmBufWASAPI::ProcessSample8(const TByte* aSample, TUint aNumChannels)
{
    TByte sample[8]  = { 0 };
    TUint sampleSize = 1;

    if (_DuplicateChannel)
    {
        sampleSize *= 2;
    }

    for (TUint i=0; i<aNumChannels; i++) {
        sample[0] = *aSample;

        if (_DuplicateChannel)
        {
            sample[1] = *aSample;
        }

        aSample++;
        Brn sampleBuf(sample, sampleSize);
        ProcessFragment(sampleBuf);
    }
}

void ProcessorPcmBufWASAPI::ProcessSample16(const TByte* aSample, TUint aNumChannels)
{
    TByte sample[8]  = { 0 };
    TUint sampleSize = 2;

    if (_DuplicateChannel)
    {
        sampleSize *= 2;
    }

    for (TUint i=0; i<aNumChannels; i++) {
        sample[0] = *(aSample + 1);
        sample[1] = *aSample;

        if (_DuplicateChannel)
        {
            sample[2] = *(aSample + 1);
            sample[3] = *aSample;
        }

        aSample += 2;

        Brn sampleBuf(sample, sampleSize);
        ProcessFragment(sampleBuf);
    }
}

void ProcessorPcmBufWASAPI::ProcessSample24(const TByte* aSample, TUint aNumChannels)
{
    TByte sample[8]  = { 0 };
    TUint sampleSize = 3;

    if (_DuplicateChannel)
    {
        sampleSize *= 2;
    }

    for (TUint i=0; i<aNumChannels; i++) {
        sample[0] = *(aSample + 2);
        sample[1] = *(aSample + 1);
        sample[2] = *aSample;

        if (_DuplicateChannel)
        {
            sample[3] = *(aSample + 2);
            sample[4] = *(aSample + 1);
            sample[5] = *aSample;
        }

        aSample += 3;

        Brn sampleBuf(sample, sampleSize);
        ProcessFragment(sampleBuf);
    }
}
