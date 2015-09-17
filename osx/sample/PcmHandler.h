#ifndef HEADER_PCM_HANDLER_OSX
#define HEADER_PCM_HANDLER_OSX


#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <AudioToolbox/AudioQueue.h>

namespace OpenHome {
namespace Media {

class OsxPcmProcessor : public IPcmProcessor
{
public:
    OsxPcmProcessor();
    ~OsxPcmProcessor() {};

    void enqueue(MsgPlayable *msg);
    MsgPlayable * dequeue();
    bool isEmpty();

    /**
     * Set the buffer to be used for packet reading
     */
    void setBuffer(AudioQueueBufferRef buf);

    /**
     * Fill the current audio output buffer using data
     * read from the PipelineAnimator
     */
    void fillBuffer(AudioQueueBufferRef inBuffer);

    /**
     * Set the audio output state.
     * If output is false then we will terminate any
     * outstanding data buffer filling and push the buffer
     */
    void setOutputActive(bool active);

    /**
     * get the size of data in the buffer
     */
    TUint32 size() { return iWriteIndex; }

    /**
     * quit outstanding processing
     */
    void quit();

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
     */
    virtual void ProcessFragment(const Brx& aData, TByte aSampleSize, TUint aNumChannels);

    /**
     * Optional function.  Gives the processor a chance to copy memory in a single block.
     *
     * Is not guaranteed to be called so all processors must implement ProcessSample.
     * Bit depth is indicated in function name; number of channels is passed as a parameter.
     *
     * @param aData         Packed big endian pcm data.  Will always be a complete number of samples.
     * @param aNumChannels  Number of channels.
     */
    virtual void ProcessFragment8(const Brx& aData, TUint aNumChannels);
    virtual void ProcessFragment16(const Brx& aData, TUint aNumChannels);
    virtual void ProcessFragment24(const Brx& aData, TUint aNumChannels);
    virtual void ProcessFragment32(const Brx& aData, TUint aNumChannels);

    /**
     * Process a single sample of audio.
     *
     * Data is packed and big endian.
     * Bit depth is indicated in function name; number of channels is passed as a parameter.
     *
     * @param aSample  Pcm data for a single sample.  Length will be (bitDepth * numChannels).
     */
    virtual void ProcessSample(const TByte* aSample, const TUint8 aSampleSize, TUint aNumChannels);

    /**
     * Process a single sample of audio.
     *
     * Data is packed and big endian.
     * Bit depth is indicated in function name; number of channels is passed as a parameter.
     *
     * @param aSample  Pcm data for a single sample.  Length will be (bitDepth * numChannels).
     */
    virtual void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample24(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample32(const TByte* aSample, TUint aNumChannels);

    /**
     * Called once per call to MsgPlayable::Read.
     *
     * No more calls to ProcessFragment or ProcessSample will be made after this.
     */
    virtual void EndBlock() {}

private:
    AudioQueueBufferRef   iBuff;
    TUint32 iBuffsize;
    TUint32 iReadIndex;
    TUint32 iWriteIndex;
    TUint32 iBytesToRead;
    TUint8  iFrameSize;
    Mutex iSampleBufferLock;
    Mutex iOutputLock;
    Semaphore iSemHostReady;
    Semaphore iSemQueueGuard;
    MsgQueue queue;
    bool    iOutputActive;
    bool    iQuit;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PCM_HANDLER_OSX
