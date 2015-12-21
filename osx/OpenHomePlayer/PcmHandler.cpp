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
    while((iBytesToRead > 0) && iOutputActive)
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
            remains = (MsgPlayable *)msg->Split(iBytesToRead);
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
 * Gives the processor a chance to copy memory in a single block.
 *
 * Is not guaranteed to be called so all processors must implement ProcessSample.
 * Bit depth is indicated in function name; number of channels is passed as a parameter.
 *
 * @param aData         Packed big endian pcm data.  Will always be a complete number of samples.
 * @param aNumChannels  Number of channels.
 */
void OsxPcmProcessor::ProcessFragment(const Brx& aData, TByte aSampleSize, TUint aNumChannels)
{
    AutoMutex _(iSampleBufferLock);

    iFrameSize = aSampleSize * aNumChannels;

    /* figure out how much data we can copy between the current start point and the end of the buffer */
    TUint32 blocksize = fmin(aData.Bytes(), iBuffsize - iWriteIndex);

    memcpy(&(((char *)iBuff->mAudioData)[iWriteIndex]), aData.Ptr(), blocksize);
    iBytesToRead -= blocksize;
    iWriteIndex += blocksize;
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


/**
 * Process a single sample of audio.
 *
 * Data is packed and big endian.
 * Bit depth is indicated in function name; number of channels is passed as a parameter.
 *
 * @param aSample  Pcm data for a single sample.  Length will be (bitDepth * numChannels).
 */
void OsxPcmProcessor::ProcessSample(const TByte* aSample, const TUint8 aSampleSize, TUint aNumChannels)
{
    AutoMutex _(iSampleBufferLock);

    iFrameSize = aSampleSize * aNumChannels;

    /* figure out how much data we can copy between the current start point and the end of the buffer */
    TUint32 dataSize = iFrameSize;

    TUint32 blocksize = fmin(dataSize, iBuffsize - iWriteIndex);
    memcpy(&(((char *)iBuff->mAudioData)[iWriteIndex]), aSample, blocksize);
    iBytesToRead -= blocksize;
    iWriteIndex += blocksize;
}


void OsxPcmProcessor::ProcessSample8(const TByte* aSample, TUint aNumChannels)
{
    ProcessSample(aSample, 1, aNumChannels);
}
void OsxPcmProcessor::ProcessSample16(const TByte* aSample, TUint aNumChannels)
{
    ProcessSample(aSample, 2, aNumChannels);
}
void OsxPcmProcessor::ProcessSample24(const TByte* aSample, TUint aNumChannels)
{
    ProcessSample(aSample, 3, aNumChannels);
}
void OsxPcmProcessor::ProcessSample32(const TByte* aSample, TUint aNumChannels)
{
    ProcessSample(aSample, 4, aNumChannels);
}
