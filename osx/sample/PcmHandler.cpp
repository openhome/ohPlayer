#include "PcmHandler.h"

using namespace OpenHome;
using namespace OpenHome::Media;


OsxPcmProcessor::OsxPcmProcessor() : IPcmProcessor()
, iSampleBufferLock("SBLK")
, iOutputLock("OPLK")
, iSemHostReady("HRDY", 0)
{
    iReadIndex = iWriteIndex = iBytesToRead = 0;
}

void OsxPcmProcessor::enqueue(MsgPlayable *msg)
{
    iSemHostReady.Wait();
    iSemHostReady.Clear();
    queue.Enqueue(msg);
}

MsgPlayable * OsxPcmProcessor::dequeue()
{
    MsgPlayable *msg = static_cast<MsgPlayable *>(queue.Dequeue());
    
    return msg;
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
}

void OsxPcmProcessor::fillBuffer(AudioQueueBufferRef inBuffer)
{
    Msg *remains = nil;

    // if no data is available then signal the animator to provide some
    if(queue.NumMsgs() == 0)
        iSemHostReady.Signal();
    
    setOutputActive(true);
    // loop round processing data until the buffer is full
    // or until we have been signalled to stop
    while((iBytesToRead > 0) && iOutputActive)
    {
        // if no data is available then signal the animator to provide some
        MsgPlayable *msg = dequeue();
        
        // read the packet, release and remove
        if(msg->Bytes() > iBytesToRead)
            remains = msg->Split(iBytesToRead);
        msg->Read(*this);
        msg->RemoveRef();
        
        if(remains == nil)
            iSemHostReady.Signal();
    }

    // requeue the remaining bytes
    if(remains != nil)
        queue.EnqueueAtHead(remains);

    iSemHostReady.Signal();

    inBuffer->mAudioDataByteSize = size();
}

/**
 * Gives the processor a chance to copy memory in a single block.
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
TBool OsxPcmProcessor::ProcessFragment(const Brx& aData, TByte aSampleSize, TUint aNumChannels)
{
    AutoMutex _(iSampleBufferLock);
    
    iFrameSize = aSampleSize * aNumChannels;
    
    /* figure out how much data we can copy between the current start point and the end of the buffer */
    TUint32 blocksize = fmin(aData.Bytes(), iBuffsize - iWriteIndex);
    
    memcpy(&(((char *)iBuff->mAudioData)[iWriteIndex]), aData.Ptr(), blocksize);
    iBytesToRead -= blocksize;
    iWriteIndex += blocksize;
    
    return true;
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
TBool OsxPcmProcessor::ProcessFragment8(const Brx& aData, TUint aNumChannels)
{
    return ProcessFragment(aData, 1, aNumChannels);
}

TBool OsxPcmProcessor::ProcessFragment16(const Brx& aData, TUint aNumChannels)
{
    return ProcessFragment(aData, 2, aNumChannels);
}
TBool OsxPcmProcessor::ProcessFragment24(const Brx& aData, TUint aNumChannels)
{
    return ProcessFragment(aData, 3, aNumChannels);
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



