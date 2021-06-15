#include "PcmHandler.h"
#include <OpenHome/Private/Printer.h>

using namespace OpenHome;
using namespace OpenHome::Media;

#define DBG(_x)
//#define DBG(_x)   DBG(_x)

OsxPcmProcessor::OsxPcmProcessor() : IPcmProcessor()
, iSampleBufferLock("SBLK")
, iOutputLock("OPLK")
, iSemHostReady("HRDY", 0)
, iSemQueueGuard("QGUARD", 0)
, iQuit(false)
{
    iReadIndex = iWriteIndex = iBytesToRead = 0;
}

void OsxPcmProcessor::enqueue(MsgPlayable *msg)
{
    DBG(("OsxPcmProcessor::enqueue - wait host ready\n"));
    iSemHostReady.Wait();
    iSemHostReady.Clear();
    DBG(("OsxPcmProcessor::enqueue - got host ready\n"));

    if(!iQuit)
    {
        DBG(("OsxPcmProcessor::enqueue - queue message\n"));
        queue.Enqueue(msg);
        DBG(("OsxPcmProcessor::enqueue - queued message\n"));

        // Allow the reader access to the message queue.
        iSemQueueGuard.Signal();
    }
}

MsgPlayable * OsxPcmProcessor::dequeue()
{
    MsgPlayable *msg = static_cast<MsgPlayable *>(queue.Dequeue());

    return msg;
}

TBool OsxPcmProcessor::isEmpty()
{
    return queue.IsEmpty();
}

void OsxPcmProcessor::quit()
{
    iQuit = true;

    DBG(("OsxPcmProcessor::quit - signalling host ready\n"));
    // signal pending enqueue operations that we're finishing
    iSemHostReady.Signal();
}

/**
 * Set the buffer to be used for packet reading
 */
void OsxPcmProcessor::setBuffer(AudioQueueBufferRef buf)
{
    iBuff = buf;
    iBuffsize = buf->mAudioDataBytesCapacity;
    buf->mAudioDataByteSize = 0;
    iReadIndex = iWriteIndex = 0;
    iBytesToRead = iBuffsize;
}

void OsxPcmProcessor::setOutputActive(bool active)
{
    AutoMutex _(iOutputLock);

    iOutputActive = active;

    if (active == false)
    {
        // Allow the fillBuffer loop to exit early without attempting
        // to dequeue more data and blocking.
        iSemQueueGuard.Signal();
    }
}

void OsxPcmProcessor::fillBuffer(AudioQueueBufferRef inBuffer)
{
    MsgPlayable *remains = nil;

    DBG(("fillBuffer: %d messages in queue\n", queue.NumMsgs()));
    DBG(("fillBuffer: buffer capacity is %d bytes\n", inBuffer->mAudioDataBytesCapacity));

    if (iOutputActive == false)
    {
        return;
    }

    // if no data is available then signal the animator to provide some
    if(queue.NumMsgs() == 0)
    {
        DBG(("fillBuffer: no messages - signalling animator\n"));
        iSemHostReady.Signal();
    }

    // loop round processing data until the buffer is full
    // or until we have been signalled to stop
    while((iBytesToRead > kMsgSplitBoundary) && iOutputActive)
    {
        // This will be signalled when data has been queued OR the output
        // is no longer active.
        iSemQueueGuard.Wait();
        iSemQueueGuard.Clear();

        // If the outptu is no longe active exit without attempting to
        // read more data.
        if (iOutputActive == false)
        {
            break;
        }

        DBG(("fillBuffer: dequeue a message\n"));
        // if no data is available then signal the animator to provide some
        MsgPlayable *msg = dequeue();
        DBG(("fillBuffer: dequeued message with %d bytes\n", msg->Bytes()));

        // read the packet, release and remove
        if(msg->Bytes() > iBytesToRead)
        {
            // Ensure the buffer is split on a frame boundary.
            //
            // This is required as 32 bit Pcm can be injected into any stream
            // by the ramper.
            //
            // The underlying MsgPlayable format is not available to us here.
            TUint bytes = iBytesToRead - (iBytesToRead % kMsgSplitBoundary);

            remains = (MsgPlayable *)msg->Split(bytes);
        }
        // read the packet, release and remove
        msg->Read(*this);
        msg->RemoveRef();

        if(remains == nil)
        {
            DBG(("fillBuffer: more space available - signal the animator\n"));
            iSemHostReady.Signal();
        }
        else
        {
            DBG(("fillBuffer: fillBuffer - buffer full: remainder is %d bytes\n", remains->Bytes()));
        }
    }

    // requeue the remaining bytes
    if(remains != nil)
    {
        DBG(("fillBuffer: packet was too big. enqueue remainder\n"));
        queue.EnqueueAtHead(remains);
        DBG(("fillBuffer: enqueued remainder\n" ));
        iSemQueueGuard.Signal();
    }
    else
    {
        DBG(("fillBuffer: signal host for more data\n" ));

        if (iOutputActive)
        {
            iSemHostReady.Signal();
        }
    }

    inBuffer->mAudioDataByteSize = size();
    DBG(("fillBuffer: buffer filled with %d bytes\n", inBuffer->mAudioDataByteSize ));
}

/**
 * Convert the supplied 32 bit big endian pcm to the bit depth required by
 * the current audio queue.
 *
 * @param aData         Packed big endian pcm data.
 *                      Will always be a complete number of samples.
 * @param aSampleSize   Number of bytes in a single sample.
 * @param aNumChannels  Number of channels.
 */
void OsxPcmProcessor::convertPcm(const Brx& aData,
                                 TByte      aSampleSize,
                                 TUint      aNumChannels)
{

    // The following should only apply to auto-generated 32 bit pcm.
    ASSERT(aSampleSize == 4);

    // Calculate the size of the data after conversion.
    TUint  bytes = 0;

    switch (iSampleBytes)
    {
        case 1: // 8 bit
        {
            bytes = aData.Bytes() / 4;
            break;
        }
        case 2: // 16 bit
        {
            bytes = aData.Bytes() / 2;
            break;
        }
        case 3: // 24 bit
        {
            bytes = (aData.Bytes() * 3) / 4;
            break;
        }
    }

    /*
     * Figure out how much data we can copy between the current
     * start point and the end of the buffer
     *
     * Ensure only complete frames are copied.
     */
    TUint32 blocksize = fmin(bytes, iBuffsize - iWriteIndex);

    TUint32 rem = blocksize % (iSampleBytes * iNumChannels);
    blocksize   = blocksize - rem;

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = &(((TByte *)iBuff->mAudioData)[iWriteIndex]);
    TByte *endp = ptr1 + blocksize;

    // Convert the required amount of input data and store in the curernt
    // audio queue buffer.
    while (ptr1 < endp)
    {
        switch (iSampleBytes)
        {
            case 1:
            {
                *ptr1++ = *(ptr+0);
                break;
            }
            case 2:
            {
                *ptr1++ = *(ptr+0);
                *ptr1++ = *(ptr+1);
                break;
            }
            case 3:
            {
                *ptr1++ = *(ptr+0);
                *ptr1++ = *(ptr+1);
                *ptr1++ = *(ptr+2);
                break;
            }
        }

        ptr += 4;
    }

    iBytesToRead -= blocksize;
    iWriteIndex  += blocksize;
}

void OsxPcmProcessor::ProcessSilence(const Brx &aData,
                                     TUint aNumChannels,
                                     TUint aSampleSizeBytes)
{
    ProcessFragment(aData, aNumChannels, aSampleSizeBytes);
}


/**
 * Gives the processor a chance to copy memory in a single block.
 *
 * Is not guaranteed to be called so all processors must implement ProcessSample.
 * Bit depth is indicated in function name; number of channels is passed as a parameter.
 *
 * @param aData         Packed big endian pcm data.  Will always be a complete number of samples.
 * @param aNumChannels  Number of channels.
 */
void OsxPcmProcessor::ProcessFragment(const Brx& aData,
                                      TUint aNumChannels,
                                      TUint aSampleSizeBytes)
{
    AutoMutex _(iSampleBufferLock);

    // Check the supplied data matches the audio queue format.
    // (This may not be the case for the 32 bit pcm generated by the ramper).
    if (aSampleSizeBytes == iSampleBytes)
    {
        /*
         * Figure out how much data we can copy between the current
         * start point and the end of the buffer
         */
        TUint32 blocksize = fmin(aData.Bytes(), iBuffsize - iWriteIndex);

        memcpy(&(((char *)iBuff->mAudioData)[iWriteIndex]),
               aData.Ptr(),
               blocksize);

        iBytesToRead -= blocksize;
        iWriteIndex  += blocksize;
    }
    else
    {
        // If not convert it ...
        convertPcm(aData, aSampleSizeBytes, aNumChannels);
    }
}

/**
 * Optional function.  Gives the processor a chance to copy memory in a single block.
 *
 * Is not guaranteed to be called so all processors must implement ProcessSample.
 * Bit depth is indicated in function name; number of channels is passed as a parameter.
 *
 * @param aData         Packed big endian pcm data.  Will always be a complete number of samples.
 * @param aNumChannels  Number of channels.
 *
 * @return  true if the fragment was processed (meaning that ProcessSample will not be called for aData);
 *          false otherwise (meaning that ProcessSample will be called for each sample in aData).
 */
void OsxPcmProcessor::ProcessFragment8(const Brx& aData, TUint aNumChannels)
{
    ProcessFragment(aData, 1, aNumChannels);
}

void OsxPcmProcessor::ProcessFragment16(const Brx& aData, TUint aNumChannels)
{
    ProcessFragment(aData, 2, aNumChannels);
}
void OsxPcmProcessor::ProcessFragment24(const Brx& aData, TUint aNumChannels)
{
    ProcessFragment(aData, 3, aNumChannels);
}
void OsxPcmProcessor::ProcessFragment32(const Brx& aData, TUint aNumChannels)
{
    ProcessFragment(aData, 4, aNumChannels);
}
