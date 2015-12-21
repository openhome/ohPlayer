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
    iRefCount = (ULONG *)_aligned_malloc(sizeof(LONG), 4);

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
                                       BOOL             *streamStart,
                                       BOOL             *streamEnded) :
    iStreamLength(0),
    iStreamPos(0),
    iInAsyncRead(FALSE),
    iIsRecogPhase(TRUE),
    iRecogSeekOutwithCache(FALSE),
    iSeekExpected(FALSE),
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
    iController->Read(iRecogCache, iRecogCache.MaxBytes());
    iRecogCacheBytes = iRecogCache.Bytes();
}

OHPlayerByteStream::~OHPlayerByteStream()
{
#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream Destructor\n");
#endif

    _aligned_free(iRefCount);
}

// Note the stream fomat recognition phase is complete.
//
// This effects how Seek() requests are processed.
void OHPlayerByteStream::RecognitionComplete()
{
#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream::RecognitionComplete\n");
#endif

    iIsRecogPhase = FALSE;
}

// Release the recogniiton cache.
//
// Future seeks will be on the physical stream.
void OHPlayerByteStream::DisableRecogCache()
{
#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream::DisableRecogCache\n");
#endif

    iStreamPos = 0;

    // Disable the cache.
    if (iRecogCacheBytes > 0)
    {
        iRecogCacheBytes = 0;
    }
}

// Not that a stream seek is expected and should not be ignored.
void OHPlayerByteStream::ExpectExternalSeek()
{
#ifdef _DEBUG
    DBUG_F("OHPlayerByteStream::ExpectExternalSeek\n");
#endif

    iSeekExpected = TRUE;
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

    iInAsyncRead = TRUE;

    // Execute a synchronous read
    hr = Read(aBuffer, aLength, &bytesRead);

    // Create the result object to pass to EndRead()
    requestState = new ReadRequest(bytesRead);

    hr = MFCreateAsyncResult(requestState,
                             aCallback,
                             aCallerState,
                            &result);


    SafeRelease(&requestState);

    // Set async operation status to the result of the read.
    result->SetStatus(S_OK);

    if (SUCCEEDED(hr))
    {
        // Invoke the async callback to instigate the EndRead() call.
        hr = MFInvokeCallback(result);
    }

    return S_OK;
}

STDMETHODIMP OHPlayerByteStream::Read(BYTE  *aBuffer,
                                      ULONG  aLength,
                                      ULONG *aBytesRead)
{

    // If we have a cache and the current stream position resides
    // within it return data from the cache.
    if (iStreamPos < (LONGLONG)iRecogCacheBytes)
    {
        ULONG available = (ULONG)(iRecogCacheBytes - iStreamPos);

        if (aLength > available)
        {
            aLength = available;
        }

        memcpy(aBuffer, (char *)(iRecogCache.Ptr() + iStreamPos), aLength);

        *aBytesRead = aLength;

#ifdef _DEBUG
        DBUG_F("Read: Cache  [%lu]\n", *aBytesRead);
#endif
    }
    else
    {
        Bwn inputBuffer(aBuffer, aLength);

        inputBuffer.SetBytes(0);

        try
        {
            // Read the requested amount of data from teh physical stream.
            iController->Read(inputBuffer, inputBuffer.MaxBytes());
        }
        catch(CodecStreamStart&)
        {
    #ifdef _DEBUG
            DBUG_F("Read: CodecStreamStart Exception Caught\n");
    #endif // _DEBUG
            *iStreamStart = TRUE;
            return S_OK;
        }
        catch(CodecStreamEnded&)
        {
    #ifdef _DEBUG
            DBUG_F("Read: CodecStreamEnded Exception Caught\n");
    #endif // _DEBUG
            *iStreamEnded = TRUE;
            return S_OK;
        }
        catch(CodecStreamStopped&)
        {
    #ifdef _DEBUG
            DBUG_F("Read: CodecStreamStopped Exception Caught\n");
    #endif // _DEBUG
            *iStreamEnded = TRUE;
            return S_OK;
        }

        *aBytesRead = (ULONG)inputBuffer.Bytes();
    }

    iStreamPos += *aBytesRead;

    DBUG_F("Read: Bytes[%lu] Pos[%llu]\n", *aBytesRead, iStreamPos);

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

    iInAsyncRead = FALSE;

    hr = aResult->GetStatus();

    SafeRelease(&aResult);

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
            DBUG_F("Seek Origin: Offset [%llu]\n", aSeekOffset);
            break;
        case msoCurrent:
            DBUG_F("Seek Curent: Offset [%llu]\n", aSeekOffset);

            aSeekOffset += iStreamPos;
            break;
    }

    if (aSeekOffset >= iRecogCacheBytes)
    {
        // A seek on the physical stream is required.
        //
        // Unfortunately the SourceReader has the propensity to, on occasion,
        // execute seeks backwards from the current stream position then
        // forwards again to the current position when decoding the stream.
        //
        // For ease of integration the seeks are ignored.
        if (iIsRecogPhase || iRecogSeekOutwithCache || iSeekExpected)
        {
#if 0
            infile.clear();

            if (infile.seekg(aSeekOffset, way))
            {
                // If the recognition cache is active note that we have
                // seek'd outwith it.
                //
                // A physical seek will be required to prior to decoding to
                // get back to the start of the stream.
                //
                // This will not be required in LitePipe as we are automatically
                // returned to the start of the stream after recognition
                // is complete.
                if (iRecogCacheBytes >  0)
                {
                    iRecogSeekOutwithCache = TRUE;
                }

                *aCurrentPosition = aSeekOffset;

                DBUG_F("[%llu]:",  *aCurrentPosition);
                DBUG_F("Success [Physical]\n");

                // We don't track physical seeks after recognition.
                //
                // Not required in LitePipe.
                if (!iIsRecogPhase)
                {
                    iRecogSeekOutwithCache = FALSE;
                }

                // This seek was instigated via LitePipe and thus allowed.
                //
                // Reset things so seeks will be ignored, during decoding,
                // until the next LitePipe instigated seek.
                if (iSeekExpected)
                {
                    iSeekExpected = FALSE;
                }
            }
            else
            {
                DBUG_F("Failure\n");
                hr = E_FAIL;
            }
#endif
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

            DBUG_F("Seek: Codec instagated seek skipped\n");
        }
    }
    else
    {
        // The seek offset resides in the allocated recognition cache.
        //
        // Update the position to the required offset.
        *aCurrentPosition = aSeekOffset;

        DBUG_F("Seek Success [From Cache]\n");
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
    *aCapabilities = MFBYTESTREAM_IS_READABLE /*| MFBYTESTREAM_IS_SEEKABLE*/;

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
        DBUG_F("IsEndOfStream [%llu] [%llu] [TRUE]\n",
               iStreamPos, iStreamLength);
#endif

        *aIsEndOfStream = TRUE;
    }
    else
    {
#ifdef _DEBUG
        DBUG_F("IsEndOfStream [%llu] [%llu] [FALSE]\n",
               iStreamPos, iStreamLength);
#endif

        *aIsEndOfStream = FALSE;
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

#endif USE_IMFCODEC
