//
// Implementation of the IAudioSessionEvents interface.
//
// This allows us to receive notifications to any changes to the
// multimedia endpoint.
//

#include <OpenHome/Private/Printer.h>

#include "AudioSessionEvents.h"
#include "CustomMessages.h"
#include "MemoryCheck.h"

using namespace OpenHome;
using namespace OpenHome::Media;

AudioSessionEvents::AudioSessionEvents(HWND   hwnd,
                                       HANDLE audioSessionDisconnectedEvent) :
    iRefCount(1),
    iHwnd(hwnd),
    iDisconnectedEvent(audioSessionDisconnectedEvent)
{
}

HRESULT AudioSessionEvents::OnSessionDisconnected (AudioSessionDisconnectReason /*DisconnectReason*/)
{
    Log::Print("Audio Session Terminated\n");

    // Notify the audio render thread of the disconnection.
    SetEvent(iDisconnectedEvent);

    // Notify the user of audio session disconnection.
    PostMessage(iHwnd, WM_APP_AUDIO_DISCONNECTED, NULL, NULL);

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
    return InterlockedIncrement(&iRefCount);
}

ULONG AudioSessionEvents::Release()
{
    ULONG returnValue = InterlockedDecrement(&iRefCount);

    if (returnValue == 0)
    {
        delete this;
    }

    return returnValue;
}
