//
// Implementation of the IAudioSessionEvents interface.
//
// This allows us to receive notifications to any changes to the
// multimedia endpoint.
//

#include <Audiopolicy.h>

#include <OpenHome/Private/Printer.h>

#include "AudioSessionEvents.h"
#include "CustomMessages.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
   #ifndef DBG_NEW
      #define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
      #define new DBG_NEW
   #endif
#endif  // _DEBUG

using namespace OpenHome;
using namespace OpenHome::Media;

AudioSessionEvents::AudioSessionEvents(HWND hwnd,
                                       HANDLE audioSessionDisconnectedEvent) :
    _RefCount(1),
    _Hwnd(hwnd),
    _DisconnectedEvent(audioSessionDisconnectedEvent)
{
}

HRESULT AudioSessionEvents::OnSessionDisconnected (AudioSessionDisconnectReason /*DisconnectReason*/)
{
    Log::Print("Audio Session Terminated\n");

    // Notify the render thread of the disconnection.
    SetEvent(_DisconnectedEvent);

    // Notify user of audio session disconnection.
    PostMessage(_Hwnd, WM_APP_AUDIO_DISCONNECTED, NULL, NULL);

    return S_OK;
}

//
//  IUnknown
//
HRESULT AudioSessionEvents::QueryInterface(REFIID Iid, void **Object)
{
    if (Object == NULL)
    {
        return E_POINTER;
    }
    *Object = NULL;

    if (Iid == IID_IUnknown)
    {
        *Object =
            static_cast<IUnknown *>(static_cast<IAudioSessionEvents *>(this));

        AddRef();
    }
    else if (Iid == __uuidof(IAudioSessionEvents))
    {
        *Object = static_cast<IAudioSessionEvents *>(this);
        AddRef();
    }
    else
    {
        return E_NOINTERFACE;
    }
    return S_OK;
}

ULONG AudioSessionEvents::AddRef()
{
    return InterlockedIncrement(&_RefCount);
}

ULONG AudioSessionEvents::Release()
{
    ULONG returnValue = InterlockedDecrement(&_RefCount);
    if (returnValue == 0)
    {
        delete this;
    }
    return returnValue;
}
