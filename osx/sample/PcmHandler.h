#ifndef HEADER_PCM_HANDLER_OSX
#define HEADER_PCM_HANDLER_OSX


#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>


namespace OpenHome {
namespace Media {

class OsxPcmProcessor : public IPcmProcessor
{
public:
    OsxPcmProcessor() : IPcmProcessor(), iSampleBufferLock("SBLK"), iSemHaveData("SDAT", 0)
    {
        /* TODO: sort out better tuning for the sample buffer
           based on the data being played */
        /* buffer for 1 second of 16-bit stereo 48kHz audio */
        iBuffsize = 2 * 3 * 48000;
        iBuff = (TByte *)malloc(iBuffsize);
        iReadIndex = iWriteIndex = iBytesToRead = 0;
    }
    
    ~OsxPcmProcessor()
    {
        free(iBuff);
    }
    
    /**
     * Called once per call to MsgPlayable::Read.
     *
     * Will be called before any calls to ProcessFragment or ProcessSample.
     */
    virtual void BeginBlock() {}
    
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
    virtual TBool ProcessFragment(const Brx& aData, TByte aSampleSize, TUint aNumChannels)
    {
        AutoMutex _(iSampleBufferLock);

        iFrameSize = aSampleSize * aNumChannels;
        
        /* figure out how much data we can copy between the current start point and the end of the buffer */
        TUint32 blocksize = fmin(aData.Bytes(), iBuffsize - iWriteIndex);
        Log::Print("ProcessFragment - data buffer has : %d bytes\n", aData.Bytes());
        Log::Print("Processing : %d bytes starting with %.2x%.2x%.2x%.2x\n", blocksize,
                   aData.Ptr()[0],aData.Ptr()[1],aData.Ptr()[2],aData.Ptr()[3]);

        memcpy(&iBuff[iWriteIndex], aData.Ptr(), blocksize);
        iBytesToRead += blocksize;
        iWriteIndex += blocksize;
        if(blocksize < aData.Bytes())
        {
            /* more to copy at the start of our ring buffer */
            iWriteIndex = 0;
            blocksize = aData.Bytes()- blocksize;
            memcpy(&iBuff[iWriteIndex], aData.Ptr(), blocksize);
            iBytesToRead += blocksize;
            iWriteIndex += blocksize;
        }
        
        /* indicate that data is available */
        iSemHaveData.Clear();
        if(iBytesToRead > 0)
            iSemHaveData.Signal();
        
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
    virtual TBool ProcessFragment8(const Brx& aData, TUint aNumChannels)
    {
        return ProcessFragment(aData, 1, aNumChannels);
    }
    
    virtual TBool ProcessFragment16(const Brx& aData, TUint aNumChannels)
    {
        return ProcessFragment(aData, 2, aNumChannels);
    }
    virtual TBool ProcessFragment24(const Brx& aData, TUint aNumChannels)
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
    virtual void ProcessSample(const TByte* aSample, const TUint8 aSampleSize, TUint aNumChannels)
    {
        AutoMutex _(iSampleBufferLock);
        
        iFrameSize = aSampleSize * aNumChannels;
        
        /* figure out how much data we can copy between the current start point and the end of the buffer */
        TUint32 dataSize = iFrameSize;
        Log::Print("Processing sample: %d bytes\n", dataSize);
        
        TUint32 blocksize = fmin(dataSize, iBuffsize - iWriteIndex);
        memcpy(&iBuff[iWriteIndex], aSample, blocksize);
        iBytesToRead += blocksize;
        iWriteIndex += blocksize;
        if(blocksize < dataSize)
        {
            /* more to copy at the start of our ring buffer */
            iWriteIndex = 0;
            blocksize = dataSize - blocksize;
            memcpy(&iBuff[iWriteIndex], aSample, blocksize);
            iBytesToRead += blocksize;
            iWriteIndex += blocksize;
        }
        
        /* indicate that data is available */
        iSemHaveData.Clear();
        iSemHaveData.Signal();
    }
    
    /**
     * Process a single sample of audio.
     *
     * Data is packed and big endian.
     * Bit depth is indicated in function name; number of channels is passed as a parameter.
     *
     * @param aSample  Pcm data for a single sample.  Length will be (bitDepth * numChannels).
     */
    virtual void ProcessSample8(const TByte* aSample, TUint aNumChannels)
    {
        ProcessSample(aSample, 1, aNumChannels);
    }
    virtual void ProcessSample16(const TByte* aSample, TUint aNumChannels)
    {
        ProcessSample(aSample, 2, aNumChannels);
    }
    virtual void ProcessSample24(const TByte* aSample, TUint aNumChannels)
    {
        ProcessSample(aSample, 3, aNumChannels);
    }
    
    /**
     * Fill a host audio buffer with data from our sample buffer.
     *
     * Sample buffer data is packed and big endian.
     *
     * @param inBuffer  the host buffer to fill.
     */
    void fillBuffer(AudioQueueBufferRef inBuffer)
    {
        /* Wait around for data to arrive. You may want to add a timeout here */
        iSemHaveData.Wait();
        
        /* lock the sample buffer while we copy data to the host */
        iSampleBufferLock.Wait();
        
        /* copy as much data as we can from the sample buffer
           to the host buffer */
        TUint32 totalBytesToCopy = fmin(iBytesToRead, inBuffer->mAudioDataBytesCapacity);
        if(totalBytesToCopy == 0)
        {
            iSampleBufferLock.Signal();
            return;
        }
        
        /* round the available space down to a whole sample boundary */
        totalBytesToCopy -= (totalBytesToCopy % iFrameSize);
        inBuffer->mAudioDataByteSize = totalBytesToCopy;
        
        /* check if we have a single run of bytes in our ring buffer,
           or if we'll need to copy in two runs */
        TUint32 bytesToCopy = fmin(totalBytesToCopy, iBuffsize - iReadIndex);
        memcpy(inBuffer->mAudioData, iBuff + iReadIndex, bytesToCopy);
        Log::Print("Copied: %d bytes to host buffer\n", bytesToCopy);
        TByte *ptr = (TByte *)inBuffer->mAudioData;
        Log::Print("copying: %d bytes starting with %.2x%.2x%.2x%.2x\n", bytesToCopy,
                   ptr[0],ptr[1],ptr[2],ptr[3]);
        if(bytesToCopy < totalBytesToCopy)
        {
            memcpy(((TByte *)inBuffer->mAudioData) + bytesToCopy, iBuff, totalBytesToCopy - bytesToCopy);
            Log::Print("Copied another: %d bytes to host buffer\n", totalBytesToCopy - bytesToCopy);
            iReadIndex = totalBytesToCopy - bytesToCopy;
        }
        else
        {
            /* advance iStart to the end of the data we've copied */
            iReadIndex += totalBytesToCopy;
        }
        
        /* and remember how many bytes we've actually got left to write */
        iBytesToRead -= totalBytesToCopy;
        
        /* if we still have data remaining then signal that it is so */
        iSemHaveData.Clear();
        if(iBytesToRead > 0)
            iSemHaveData.Signal();
        
        /* release our lock */
        iSampleBufferLock.Signal();

    }

    /**
     * Called once per call to MsgPlayable::Read.
     *
     * No more calls to ProcessFragment or ProcessSample will be made after this.
     */
    virtual void EndBlock() {}
    
private:
    TByte   *iBuff;
    TUint32 iBuffsize;
    TUint32 iReadIndex;
    TUint32 iWriteIndex;
    TUint32 iBytesToRead;
    TUint8  iFrameSize;
    Mutex   iSampleBufferLock;
    Semaphore iSemHaveData;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PCM_HANDLER_OSX
