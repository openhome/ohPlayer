//
// Check for updates, supplying the location Uri of any found to the
// caller.
//

#include <stdio.h>
#include <string.h>

#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>

#include "UpdateCheck.h"
#include "version.h"

using namespace OpenHome;
using namespace OpenHome::Media;

//
// Compare two version numbers of the form 'x.y.z' or a substring of.
//
// Returns true if version1 < version2. False otherwise.
//
bool UpdateChecker::isOlderVersion(const TChar *version1, const TChar *version2)
{
    TUint major1, minor1, build1;
    TUint major2, minor2, build2;
    TInt  params1 = 0;
    TInt  params2 = 0;

    major1 = minor1 = build1 = 0;
    major2 = minor2 = build2 = 0;

    params1 = sscanf(version1, "%u.%u.%u", &major1, &minor1, &build1);
    params2 = sscanf(version2, "%u.%u.%u", &major2, &minor2, &build2);

    // If we have be unable to parse either version we can't make a
    // decision. Assume we've got the latest.
    if (params1 < 1 || params2 < 1)
    {
        return true;
    }

    if (major1 < major2)
    {
        return true;
    }

    if (major1 > major2)
    {
        return false;
    }

    if (minor1 < minor2)
    {
        return true;
    }

    if (minor1 > minor2)
    {
        return false;
    }

    if (build1 < build2)
    {
        return true;
    }

    return false;
}


Brn UpdateChecker::ReadNextString(ReaderUntil& aReaderUntil)
{
    (void)aReaderUntil.ReadUntil('\"');
    return aReaderUntil.ReadUntil('\"');
}

Brn UpdateChecker::ReadValue(ReaderUntil& aReaderUntil, Brn& key)
{
    // Read until we find the requested key.
    while (ReadNextString(aReaderUntil) != key)
    {
        ;
    }

    // Return the next string, which will be the value.
    return ReadNextString(aReaderUntil);
}

// Check a given feed for a valid update returning the Url if found.
TBool UpdateChecker::updateAvailable(Environment& /*aEnv*/,
                                     const TChar* /*aFeed*/,
                                     Bwx& /*aUrl*/,
                                     const TChar* /*currentVersion*/,
                                     unsigned int /*aOsMajor*/,
                                     unsigned int /*aOsMinor*/)
{
    // Disabled for now. Requires rework as original used components no longer
    // available inside ohNet.
    return false;
}
