#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Private/Printer.h>

#include <alsa/asoundlib.h>
#include <math.h>

#include "Volume.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// RebootLogger
void RebootLogger::Reboot(const Brx& aReason)
{
    Log::Print("\n\n\nRebootLogger::Reboot. Reason:\n%.*s\n\n\n",
               PBUF(aReason));
}

TUint VolumeProfile::VolumeMax() const
{
    return kVolumeMax;
}

TUint VolumeProfile::VolumeDefault() const
{
    return kVolumeDefault;
}

TUint VolumeProfile::VolumeUnity() const
{
    return kVolumeUnity;
}

TUint VolumeProfile::VolumeDefaultLimit() const
{
    return kVolumeDefaultLimit;
}

TUint VolumeProfile::VolumeStep() const
{
    return kVolumeStep;
}

TUint VolumeProfile::VolumeMilliDbPerStep() const
{
    return kVolumeMilliDbPerStep;
}

TUint VolumeProfile::ThreadPriority() const
{
	return kThreadPriority;
}

TUint VolumeProfile::BalanceMax() const
{
    return kBalanceMax;
}

TUint VolumeProfile::FadeMax() const
{
    return kFadeMax;
}

TUint VolumeProfile::OffsetMax() const
{
	return kOffsetMax;
}

TBool VolumeProfile::AlwaysOn() const
{
    return kAlwaysOn;
}

IVolumeProfile::StartupVolume VolumeProfile::StartupVolumeConfig() const
{
    return StartupVolume::LastUsed;
}


VolumeControl::VolumeControl()
{
    const TChar *CARD          = "default";
    const TChar *SELEM_NAMES[] = {"Digital", "PCM", "Master"};

    // Get the mixer element for the default sound card.
    snd_mixer_open(&iHandle, 0);
    snd_mixer_attach(iHandle, CARD);
    snd_mixer_selem_register(iHandle, NULL, NULL);
    snd_mixer_load(iHandle);

    // Get the mixer element for the most relevant control.
    snd_mixer_selem_id_t *iSid;

    snd_mixer_selem_id_alloca(&iSid);
    snd_mixer_selem_id_set_index(iSid, 0);

    int nelems = sizeof(SELEM_NAMES)/sizeof(TChar*);

    for (int i=0; i<nelems; i++)
    {
        snd_mixer_selem_id_set_name(iSid, SELEM_NAMES[i]);

        iElem = snd_mixer_find_selem(iHandle, iSid);

        // Quit the loop if control found.
        if (iElem > NULL)
        {
            break;
        }
    }

}

VolumeControl::~VolumeControl()
{
    snd_mixer_close(iHandle);
}

TBool VolumeControl::IsVolumeSupported()
{
    return (iElem != NULL);
}

void VolumeControl::SetVolume(TUint aVolume)
{
    const long  MAX_LINEAR_DB_SCALE = 24;
    const TUint MILLI_DB_PER_STEP   = 1024;
    double      volume;
    double      min_norm;
    long        min, max, value;
    TInt        err;

    // Sanity Check
    if (! IsVolumeSupported())
    {
        return;
    }

    volume = double((aVolume / MILLI_DB_PER_STEP)/100.0f);

    // Use the dB range to map the volume to a scale more in tune
    // with the human ear, if possible.
    err = snd_mixer_selem_get_playback_dB_range(iElem, &min, &max);

    if (err < 0 || min >= max) {
        // dB range not available, use a linear volume mapping.
        err = snd_mixer_selem_get_playback_volume_range(iElem, &min, &max);
        if (err < 0)
        {
            return;
        }

        value = lrint(floor(volume * (max - min))) + min;
        snd_mixer_selem_set_playback_volume_all(iElem, value);

        return;
    }

    if (max - min <= MAX_LINEAR_DB_SCALE * 100)
    {
        // dB range less than 24 dB, use a linear mapping
        value = lrint(floor(volume * (max - min))) + min;
        snd_mixer_selem_set_playback_dB_all(iElem, value, -1);

        return;
    }

    if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
        min_norm = exp10((min - max) / 6000.0);
        volume = volume * (1 - min_norm) + min_norm;
    }
    value = lrint(floor(6000.0 * log10(volume))) + max;
    snd_mixer_selem_set_playback_dB_all(iElem, value, -1);

    return;
}

void VolumeControl::SetBalance(TInt /*aBalance*/)
{
    // Not Implemented
}

void VolumeControl::SetFade(TInt /*aFade*/)
{
    // Not Implemented
}
