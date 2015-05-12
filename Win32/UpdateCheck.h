#pragma once

#include <windows.h>
#include "version.h"

namespace OpenHome {
namespace Media {

class UpdateChecker
{
public:
    static bool updateAvailable(Environment& aEnv, const char* aFeed, Bwx& aUrl);
private:
    static Brn ReadNextString(ReaderUntil& aReaderUntil);
    static Brn ReadValue(ReaderUntil& aReaderUntil, Brn& key);
    static bool GetWindowsVersion(DWORD& major, DWORD& minor);
    static bool isOlderVersion(TChar *version1, TChar *version2);
};

} // namespace Media
} // namespace OpenHome
