// IMFByteStream implementation that interacts with the CodecController.

#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Private/Printer.h>

#include <assert.h>
#include <malloc.h>

#include <Mfapi.h>
#include <Mfidl.h>
#include <Windows.h>

#include "OHPlayerByteStream.h"

// Uncomment to enable timestamping of log messages
//#define TIMESTAMP_LOGGING

#ifdef TIMESTAMP_LOGGING
#include <chrono>

#define DBUG_F(...)                                                            \
    Log::Print("[%llu] [OHPlayerByteStream] ",                                 \
        std::chrono::high_resolution_clock::now().time_since_epoch().count()); \
    Log::Print(__VA_ARGS__)
#else
#define DBUG_F(...) Log::Print("[OHPlayerByteStream] " __VA_ARGS__)
#endif

#ifdef USE_IMFCODEC
using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

using namespace std;

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

// Object to store relevant data during a begin/end async read operation.
class ReadRequest : public IUnknown
{
public:
    ReadRequest(ULONG bytesRead);
    ~ReadRequest();

    // IUnknown Methods
    STDMETHODIMP QueryInterface(REFIID aRIID, LPVOID *aOutObject);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IUnknown ref counting.
    ULONG *iRefCount;

    ULONG iBytesRead;
};

ReadRequest::ReadRequest(ULONG bytesRead) :
    iBytesRead(bytesRead)
{
    // Allocate a variable for reference counting on a 32 bit boundary.
    iRefCount = (ULONG *)_aligned_malloc(sizeof(ULONG), 4);

    *iRefCount = 1UL;
}

ReadRequest::~ReadRequest()
{
    _aligned_free(iRefCount);
}

STDMETHODIMP_(ULONG) ReadRequest::AddRef()
{
    InterlockedIncrement(iRefCount);

    return *iRefCount;
}

STDMETHODIMP_(ULONG) ReadRequest::Release()
{
    // Decrement the object's internal counter.
    ULONG ulRefCount = InterlockedDecrement(iRefCount);

    if (ulRefCount == 0)
    {
        delete this;
    }

    return ulRefCount;
}

// IUnknown Methods
STDMETHODIMP ReadRequest::QueryInterface(REFIID aIId, void **aInterface)
{
    if (aIId == IID_IUnknown)
    {
        // Increment the reference count and return the pointer.
        *aInterface = (LPVOID)this;
        AddRef();

        return NOERROR;
    }

    *aInterface = NULL;
    return E_NOINTERFACE;
}

OHPlayerByteStream::OHPlayerByteStream(ICodecController *controller,
                                       TBool            *streamStart,
                                       TBool            *streamEnded) :
    iStreamLength(0),
    iStreamPos(0),
    iInAsyncRead(false),
    iIsRecogPhase(true),
    iRecogCachePos(0),
    iSeekExpected(false),
    iController(controller),
    iStreamStart(streamStart),
    iStreamEnded(streamEnded)
{
    // Allocate a variable for reference counting on a 32 bit boundary.
    iRefCount  = (ULONG *)_aligned_malloc(sizeof(LONG), 4);
    *iRefCount = 1UL;

    // Note stream length
    iStreamLength = controller->StreamLength();

    // Use a cache during stream format recognition to minimise seeking about
    // the physical stream.
    iRecogCache.SetBytes(0);
    try
    {
        iController->Read(iRecogCache, iRecogCache.MaxBytes());

#ifdef _DEBUG
        DBUG_F("OHPlayerByteStream: Recognition Cache [%lu]\n",
               iRecogCache.Bytes());
#endif
    }
    catch (...)
    {
        DBUG_F("OHPlayerByteStream: Unexpected recognition cache error\n");
    }
}

OHPlayerByteStream::~OHPlayerByteStream()
{
#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream Destructor\n");
#endif

    _aligned_free(iRefCount);
}

// Note the stream format recognition phase is complete.
//
// This effects how Seek() requests are processed.
void OHPlayerByteStream::RecognitionComplete()
{
#ifdef _DEBUG
    DBUG_F("RecognitionComplete\n");
#endif

    iIsRecogPhase = false;
}

// Release the recognition cache.
//
// Future operations will be performed on the physical stream.
void OHPlayerByteStream::DisableRecogCache(TBool revertStreamPos)
{
#ifdef _DEBUG
    DBUG_F("DisableRecogCache\n");
#endif

    // If required move, from the start of the stream, to the position
    // at the end of the recognition phase.
    //
    // This is done via read and discard.
    if (revertStreamPos)
    {
        TUint bytesLeft   = (TUint)iStreamPos;
        TUint bufferLimit = iRecogCache.MaxBytes();

        // Initialise the recognition cache for usage as a temp buffer.
        iRecogCache.SetBytes(0);

        while (bytesLeft > 0)
        {
            TUint leftToRead =
                (bytesLeft < bufferLimit) ? bytesLeft : bufferLimit;

            try
            {
                iController->Read(iRecogCache, leftToRead);

                bytesLeft -= iRecogCache.Bytes();

#ifdef _DEBUG
                DBUG_F("DisableRecogCache - Discarding [%lu]\n",
                       iRecogCache.Bytes());
#endif

                iRecogCache.SetBytes(0);
            }
            catch (...)
            {
                DBUG_F("DisableRecogCache - Unexpected exception "
                       "while advancing to offset [%llu]\n", iStreamPos);

                break;
            }
        }

#ifdef _DEBUG
        DBUG_F("DisableRecogCache - Stream Advanced To [%llu]\n",
               iController->StreamPos());
#endif
    }
    else
    {
        iStreamPos = 0;
    }

    // Disable the recognition cache
    iRecogCache.SetBytes(0);
    iRecogCachePos = -1;
}

// Not that a stream seek is expected and should not be ignored.
void OHPlayerByteStream::ExpectExternalSeek()
{
#ifdef _DEBUG
    DBUG_F("ExpectExternalSeek\n");
#endif

    iSeekExpected = true;
}

// IUnknown Methods.
STDMETHODIMP OHPlayerByteStream::QueryInterface(REFIID   aIId,
                                                LPVOID  *aInterface)
{
    // Always set out parameter to NULL, validating it first.
    if (!aInterface)
    {
        return E_INVALIDARG;
    }

    *aInterface = NULL;

    if (aIId == IID_IUnknown || aIId == IID_IMFByteStream)
    {
        // Increment the reference count and return the pointer.
        *aInterface = (LPVOID)this;
        AddRef();

        return NOERROR;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) OHPlayerByteStream::AddRef()
{
    InterlockedIncrement(iRefCount);

#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream AddRef [%lu]\n", *iRefCount);
#endif

    return *iRefCount;
}

STDMETHODIMP_(ULONG) OHPlayerByteStream::Release()
{
    // Decrement the object's internal counter.
    ULONG ulRefCount = InterlockedDecrement(iRefCount);

#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream Release [%lu]\n", *iRefCount);
#endif

    if (ulRefCount == 0)
    {
#ifdef _DEBUG
        DBUG_F("OHPlayerByteStream Release Delete\n");
#endif

        delete this;
    }

    return ulRefCount;
}

///////////////////////////////////////////////////////////////////////////////
// IMFByteStream Methods.
///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP OHPlayerByteStream::BeginRead(BYTE             *aBuffer,
                                           ULONG             aLength,
                                           IMFAsyncCallback *aCallback,
                                           IUnknown         *aCallerState)
{
    HRESULT         hr;
    ULONG           bytesRead;
    IMFAsyncResult *result;
    ReadRequest    *requestState;

    iInAsyncRead = true;

    // Execute a synchronous read
    Read(aBuffer, aLength, &bytesRead);

    // Create the result object to pass to EndRead()
    requestState = new ReadRequest(bytesRead);

    hr = MFCreateAsyncResult(requestState,
                             aCallback,
                             aCallerState,
                            &result);

    SafeRelease(&requestState);

    if (SUCCEEDED(hr))
    {
        // Set async operation status to the result of the read.
        result->SetStatus(S_OK);

        // Invoke the async callback to instigate the EndRead() call.
        hr = MFInvokeCallback(result);
    }

    return hr;
}

STDMETHODIMP OHPlayerByteStream::Read(BYTE  *aBuffer,
                                      ULONG  aLength,
                                      ULONG *aBytesRead)
{

    *aBytesRead = 0;

    // If we have a cache and the current stream position resides
    // within it return data from the cache.
    if (iIsRecogPhase)
    {
        if ((iStreamPos < iRecogCachePos) ||
            (iStreamPos >= iRecogCachePos + iRecogCache.Bytes()))
        {
            // To fulfill this request we need to reallocate the cache.
            iRecogCache.SetBytes(0);
            try
            {
                // Perform an out of band read on the stream.
                iController->Read(*this, iStreamPos, aLength);

                iRecogCachePos = iStreamPos;

#ifdef _DEBUG
                DBUG_F("Read: iRecogCachePos [%lld] Size [%lu]\n",
                       iRecogCachePos, iRecogCache.Bytes());
#endif
            }
            catch (...)
            {
                DBUG_F("Read: Unexpected error reallocating recognition  "
                       "cache\n");
            }
        }

        LONGLONG streamPos = iStreamPos;

        ULONG cacheByteOffset = (ULONG)(streamPos - iRecogCachePos);
        ULONG available       = (ULONG)(iRecogCache.Bytes() - cacheByteOffset);

#ifdef _DEBUG
        DBUG_F("Read: Cache Req [%lu] Avail [%lu]\n", aLength, available);
#endif

        if (aLength > available)
        {
            aLength = available;
        }

        memcpy(aBuffer, (char *)(iRecogCache.Ptr() + cacheByteOffset), aLength);

        *aBytesRead = aLength;
        iStreamPos += *aBytesRead;
    }
    else
    {
        try
        {
            Bwn inputBuffer(aBuffer, aLength);

            inputBuffer.SetBytes(0);

            if (*iStreamEnded || *iStreamStart)
            {
#ifdef _DEBUG
                DBUG_F("Read: Pre-existing exception [%d] [%d]\n",
                       *iStreamStart, *iStreamEnded);
#endif
            }
            else
            {
                // Read the requested amount of data from the physical stream.
                iController->Read(inputBuffer, inputBuffer.MaxBytes());
            }

            *aBytesRead = (ULONG)inputBuffer.Bytes();
            iStreamPos += *aBytesRead;
        }
        catch(CodecStreamStart&)
        {
    #ifdef _DEBUG
            DBUG_F("Read: CodecStreamStart Exception Caught. Bytes[%lu]\n",
                   *aBytesRead);
    #endif // _DEBUG
            *iStreamStart = true;
            return S_OK;
        }
        catch(CodecStreamEnded&)
        {
    #ifdef _DEBUG
            DBUG_F("Read: CodecStreamEnded Exception Caught. Bytes[%lu]\n",
                   *aBytesRead);
    #endif // _DEBUG
            *iStreamEnded = true;
            return S_OK;
        }
        catch(CodecStreamStopped&)
        {
    #ifdef _DEBUG
            DBUG_F("Read: CodecStreamStopped Exception Caught. Bytes[%lu]\n",
                   *aBytesRead);
    #endif // _DEBUG
            *iStreamEnded = true;
            return S_OK;
        }
    }

#ifdef _DEBUG
    DBUG_F("Read: Req[%lu] Got[%lu] Pos[%llu]\n",
           aLength, *aBytesRead, iStreamPos);
#endif

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::EndRead(IMFAsyncResult *aResult,
                                         ULONG          *aBytesRead)
{
    // Obtain the IUnknown interface of the AsyncResult
    IUnknown    *unknown;
    ReadRequest *requestState;

    HRESULT hr = aResult->GetObject(&unknown);

    if (FAILED(hr) || !unknown)
    {
        return E_INVALIDARG;
    }

    requestState = static_cast<ReadRequest*>(unknown);

    // Report result.
    *aBytesRead = requestState->iBytesRead;

    SafeRelease(&requestState);

    iInAsyncRead = false;

    hr = aResult->GetStatus();

    SafeRelease(&aResult);

    if (FAILED(hr))
    {
        DBUG_F("EndRead Returning Fail\n");
    }

    return hr;
}

STDMETHODIMP OHPlayerByteStream::Seek(MFBYTESTREAM_SEEK_ORIGIN aSeekOrigin,
                                      LONGLONG                 aSeekOffset,
                                      DWORD                    /*aSeekFlags*/,
                                      QWORD                   *aCurrentPosition)
{
    HRESULT hr = S_OK;

    // Fail this seek request if an asynchronous read is in progress.
    if (iInAsyncRead)
    {
        DBUG_F("Seek: Failed as a Async Begin/End Read is in progress\n");
        return E_INVALIDARG;
    }

    switch (aSeekOrigin)
    {
        case msoBegin:
            DBUG_F("Seek Origin: Offset [%lld]\n", aSeekOffset);
            break;
        case msoCurrent:
            DBUG_F("Seek Current: Offset [%lld]\n", aSeekOffset);

            aSeekOffset += iStreamPos;
            break;
    }

    if (aSeekOffset < iRecogCachePos ||
        aSeekOffset >= iRecogCachePos + iRecogCache.Bytes())
    {
        // A seek on the physical stream is required.
        if (iIsRecogPhase)
        {
            // During the recognition phase we fake any attempted seeks out with
            // the cache. The resulting 'read' will reallocate the cache with
            // data being returned from the new cache.
#ifdef _DEBUG
            DBUG_F("Seek: Recognition Phase Seek Faked [%lld]\n", aSeekOffset);
#endif

            *aCurrentPosition = aSeekOffset;
            iStreamPos        = aSeekOffset;
        }
        else
        {
            // The seek is deemed part of a sequence that will return is to
            // the current position before a read is executed.
            //
            // Simply update the stream position to that requested without
            // seeking on the assumption that the stream is already in the
            // correct position for the next read.
            *aCurrentPosition = aSeekOffset;

#ifdef _DEBUG
            DBUG_F("Seek: Codec instigated seek skipped [%lld]\n", aSeekOffset);
#endif
        }
    }
    else
    {
        // The seek offset resides in the allocated recognition cache.
        //
        // Update the position to the required offset.
        *aCurrentPosition = aSeekOffset;

#ifdef _DEBUG
        DBUG_F("Seek Success [From Cache] [%lld]\n", aSeekOffset);
#endif
    }

    iStreamPos = *aCurrentPosition;

    return hr;
}

STDMETHODIMP OHPlayerByteStream::SetCurrentPosition(QWORD aPosition)
{
    QWORD dummyPosition;

#ifdef _DEBUG
    DBUG_F("SetCurrentPosition %llu\n", aPosition);
#endif

    // The MSDN documentation states the following:
    //   "If the new position is larger than the length of the stream, the
    //    method returns E_INVALIDARG"
    //
    // However if this error is raised the stream decode is terminated
    // incorrectly.
#if 0
    if ((LONGLONG)aPosition > iStreamLength)
    {
        return E_INVALIDARG;
    }
#endif

    return Seek(msoBegin, aPosition, 0, &dummyPosition);
}

STDMETHODIMP OHPlayerByteStream::GetCapabilities(DWORD *aCapabilities)
{
    // Seeking disabled initially.
    *aCapabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_SEEKABLE;

#ifdef _DEBUG
    DBUG_F("GetCapabilities %d\n", *aCapabilities);
#endif

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::GetCurrentPosition(QWORD *aPosition)
{
#ifdef _DEBUG
    DBUG_F("GetCurrentPosition %llu\n", iStreamPos);
#endif

#if 0
    // Raising this error causes the decode to finish prematurely
    if (iStreamPos >= iStreamLength)
    {
        return E_INVALIDARG;
    }
#endif

    *aPosition = (QWORD)iStreamPos;

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::GetLength(QWORD *aLength)
{
    *aLength = (QWORD)iController->StreamLength();

#ifdef _DEBUG
    DBUG_F("GetLength [%lld]\n", *aLength);
#endif

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::IsEndOfStream(BOOL *aIsEndOfStream)
{
    if (iStreamPos >= iStreamLength)
    {
#ifdef _DEBUG
        DBUG_F("IsEndOfStream [%lld] [%lld] [TRUE]\n",
               iStreamPos, iStreamLength);
#endif

        *aIsEndOfStream = true;
    }
    else
    {
#ifdef _DEBUG
        DBUG_F("IsEndOfStream [%lld] [%lld] [false]\n",
               iStreamPos, iStreamLength);
#endif

        *aIsEndOfStream = false;
    }

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::Close()
{
#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream Close\n");
#endif

    return S_OK;
}

////////////////////////////////////////////////////////////////////////////////
// Unimplmented IMFByteStream methods
////////////////////////////////////////////////////////////////////////////////

STDMETHODIMP OHPlayerByteStream::BeginWrite(const BYTE       * /*aBuffer*/,
                                            ULONG              /*aLength*/,
                                            IMFAsyncCallback * /*aCallback*/,
                                            IUnknown         * /*aCallerState*/)
{
    // Write capability not available.
    assert(false);

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::Write(const BYTE * /*aBuffer*/,
                                       ULONG        /*aLength*/,
                                       ULONG      * /*aBytesWritten*/)
{
    // Write capability not available.
    assert(false);

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::EndWrite(IMFAsyncResult * /*aResult*/,
                                          ULONG          * /*aBytesWritten*/)
{
    // Write capability not available.
    assert(false);

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::SetLength(QWORD /*aLength*/)
{
    // Write capability not available.
    assert(false);

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::Flush()
{
    // Write capability not available.
    assert(false);

    return S_OK;
}

// IWriter functions
void OHPlayerByteStream::Write(TByte aValue)
{
    if (! iRecogCache.TryAppend(aValue))
    {
        DBUG_F("Write TByte: Failed to add to iRecogCache\n");
    }
}

void OHPlayerByteStream::Write(const Brx& aBuffer)
{
    if (! iRecogCache.TryAppend(aBuffer))
    {
        DBUG_F("Write Brx: Failed to add to iRecogCache\n");
    }
}

void OHPlayerByteStream::WriteFlush()
{
    DBUG_F("WriteFlush\n");
}

#endif USE_IMFCODEC
