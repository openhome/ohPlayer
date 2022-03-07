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
      * Note the number of bytes in an audio sample and the number of channels
      * in a frame.
      *
      * This is used to allow auto-generated 32 bit pcm to converted to
      * match the current audio stream.
      */
    void setStreamFormat(TUint8 aSampleBytes, TUint aNumChannels)
         { iSampleBytes = aSampleBytes; iNumChannels = aNumChannels; }

    /**
     * get the size of data in the buffer
     */
    TUint32 size() { return iWriteIndex; }

    /**
     * quit outstanding processing
     */
    void quit();

    /**
     * Convert the supplied 32 bit big endian pcm to the bit depth required by
     * the current audio queue.
     *
     * @param aData         Packed big endian pcm data.
     *                      Will always be a complete number of samples.
     * @param aSampleSize   Number of bytes in a single sample.
     * @param aNumChannels  Number of channels.
     */
    void convertPcm(const Brx& aData, TByte aSampleSize, TUint aNumChannels);

    /**
     * Called once per call to MsgPlayable::Read.
     *
     * Will be called before any calls to ProcessFragment or ProcessSample.
     */
    virtual void BeginBlock() override {}

    /**
     * Gives the processor a chance to copy memory in a single block.
     *
     * Is not guaranteed to be called so all processors must implement ProcessSample.
     * Bit depth is indicated in function name; number of channels is passed as a parameter.
     *
     * @param aData         Packed big endian pcm data.  Will always be a complete number of samples.
     * @param aNumChannels  Number of channels.
     */
    virtual void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSampleSizeBytes) override;
    
    virtual void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSampleSizeBytes) override;

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
     * Called once per call to MsgPlayable::Read.
     *
     * No more calls to ProcessFragment or ProcessSample will be made after this.
     */
    virtual void EndBlock() override {}

    /**
     * If this is called, the processor should pass on any buffered audio.
     */
    virtual void Flush() override {}

private:
    // The lowest common multiple of the available sample sizes  (1/2/3/4 bytes)
    // multiplied by the maximum number of channels.
    static const TUint kMsgSplitBoundary = 12 * 2;

    AudioQueueBufferRef   iBuff;
    TUint32 iBuffsize;
    TUint32 iReadIndex;
    TUint32 iWriteIndex;
    TUint32 iBytesToRead;
    TUint8  iSampleBytes;
    TUint8  iNumChannels;
    TUint8  iBitDepth;
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
