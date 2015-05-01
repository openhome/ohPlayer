#ifndef HEADER_AUDIO_SESSION_EVENTS
#define HEADER_AUDIO_SESSION_EVENTS

#include <Audiopolicy.h>

namespace OpenHome {
namespace Media {

class AudioSessionEvents : public IAudioSessionEvents
{
public:
    AudioSessionEvents(HWND hwnd, HANDLE audioSessionDisconnectedEvent);
private:
   STDMETHOD(OnDisplayNameChanged) (LPCWSTR /*NewDisplayName*/, LPCGUID /*EventContext*/) { return S_OK; };
    STDMETHOD(OnIconPathChanged) (LPCWSTR /*NewIconPath*/, LPCGUID /*EventContext*/) { return S_OK; };
    STDMETHOD(OnSimpleVolumeChanged) (float /*NewSimpleVolume*/, BOOL /*NewMute*/, LPCGUID /*EventContext*/) { return S_OK; }
    STDMETHOD(OnChannelVolumeChanged) (DWORD /*ChannelCount*/, float /*NewChannelVolumes*/[], DWORD /*ChangedChannel*/, LPCGUID /*EventContext*/) { return S_OK; };
    STDMETHOD(OnGroupingParamChanged) (LPCGUID /*NewGroupingParam*/, LPCGUID /*EventContext*/) {return S_OK; };
    STDMETHOD(OnStateChanged) (AudioSessionState /*NewState*/) { return S_OK; };
    STDMETHOD(OnSessionDisconnected) (AudioSessionDisconnectReason DisconnectReason);
    //
    //  IUnknown
    //
    STDMETHOD(QueryInterface)(REFIID iid, void **pvObject);

    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    LONG   _RefCount;
    HWND   _Hwnd;
    HANDLE _DisconnectedEvent;
};

} // Media
} // Openhome

#endif // HEADER_AUDIO_SESSION_EVENTS
