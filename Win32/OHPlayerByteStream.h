// IMFByteStream implementation that interacts with the CodecController.
#include <OpenHome/Media/Codec/CodecController.h>

#include <Mfidl.h>
#include <Windows.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class OHPlayerByteStream : public IMFByteStream, public IWriter
{
// From IMFByteStream
public:
    OHPlayerByteStream(ICodecController *controller,
                       TBool            *streamStart,
                       TBool            *streamEnded);
    ~OHPlayerByteStream();

    // IUnknown Methods.
    STDMETHODIMP QueryInterface(REFIID aIId, LPVOID *aInterface);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMFByteStream Methods.
    STDMETHODIMP BeginRead(BYTE             *aBuffer,
                           ULONG             aLength,
                           IMFAsyncCallback *aCallback,
                           IUnknown         *aCallerState);

    STDMETHODIMP Read(BYTE  *aBuffer,
                      ULONG  aLength,
                      ULONG *aBytesRead);

    STDMETHODIMP EndRead(IMFAsyncResult *aResult,
                         ULONG          *aBytesRead);

    STDMETHODIMP SetCurrentPosition(QWORD aPosition);
    STDMETHODIMP Seek(MFBYTESTREAM_SEEK_ORIGIN  aSeekOrigin,
                      LONGLONG                  aSeekOffset,
                      DWORD                     aSeekFlags,
                      QWORD                    *aCurrentPosition);

    STDMETHODIMP GetCapabilities(DWORD *aCapabilities);
    STDMETHODIMP GetCurrentPosition(QWORD *aPosition);
    STDMETHODIMP GetLength(QWORD *aLength);

    STDMETHODIMP IsEndOfStream(BOOL *aIsEndOfStream);
    STDMETHODIMP Close();

    // Unimplemented IMFByteStream Methods.
    STDMETHODIMP BeginWrite(const BYTE       *aBuffer,
                            ULONG             aLength,
                            IMFAsyncCallback *aCallback,
                            IUnknown         *aCallerState);

    STDMETHODIMP Write(const BYTE *aBuffer,
                       ULONG       aLength,
                       ULONG      *aBytesWritten);

    STDMETHODIMP EndWrite(IMFAsyncResult *aResult,
                          ULONG          *aBytesWritten);

    STDMETHODIMP SetLength(QWORD aLength);

    STDMETHODIMP Flush();

// From IWriter
public:
    void Write(TByte aValue) override;
    void Write(const Brx& aBuffer) override;
    void WriteFlush() override;

// Integration Extras
public:

    // Disable the stream format recognition cache.
    void DisableRecogCache(TBool revertStreamPos);

    void RecognitionComplete(); // Note the completion of format recognition.
    void ExpectExternalSeek();  // Act on the next seek request.

private:
    // Recognition cache size. This matches the size of the initial read
    // on SourceReader initialisation.
    static const ULONG iCacheSize = 64 * 1024;

    ULONG            *iRefCount;              // Object reference count.
    LONGLONG          iStreamLength;          // Stream length
    LONGLONG          iStreamPos;             // Current stream position.
    TBool             iInAsyncRead;           // Currently in begin/end read
                                              // sequence.
    TBool             iIsRecogPhase;          // Recognising stream format.
    Bws<iCacheSize>   iRecogCache;            // Recognition cache
    LONGLONG          iRecogCachePos;         // Stream position of recog cache.
    TBool             iSeekExpected;          // Honour the next seek request.

    ICodecController *iController;            // Codec Controller.
    TBool            *iStreamStart;
    TBool            *iStreamEnded;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome
