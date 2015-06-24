#pragma once

#include "version.h"

namespace OpenHome {
namespace Media {

class UpdateChecker
{
public:
    static bool updateAvailable(Environment& aEnv, const TChar* aFeed, Bwx& aUrl);
private:
    static Brn ReadNextString(ReaderUntil& aReaderUntil);
    static Brn ReadValue(ReaderUntil& aReaderUntil, Brn& key);
    static bool isOlderVersion(const TChar *version1, const TChar *version2);
};

} // namespace Media
} // namespace OpenHome
