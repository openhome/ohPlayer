//
// Check for updates, supplying the location Uri of any found to the
// caller.
//

#include <windows.h>
#include <lm.h>
#pragma comment(lib, "netapi32.lib")

#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>

#include "MemoryCheck.h"
#include "UpdateCheck.h"
#include "version.h"

using namespace OpenHome;
using namespace OpenHome::Media;

//
// Compare two version numbers of the form 'x.y.z' or a substring of.
//
// Returns true if version1 < version2. False otherwise.
//
bool UpdateChecker::isOlderVersion(TChar *version1, TChar *version2)
{
    TUint major1, minor1, build1;
    TUint major2, minor2, build2;
    TInt  params1 = 0;
    TInt  params2 = 0;

    major1 = minor1 = build1 = 0;
    major2 = minor2 = build2 = 0;

    params1 = sscanf_s(version1, "%u.%u.%u", &major1, &minor1, &build1);
    params2 = sscanf_s(version2, "%u.%u.%u", &major2, &minor2, &build2);

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

bool UpdateChecker::GetWindowsVersion(DWORD& major, DWORD& minor)
{
    LPBYTE pinfoRawData;

    if (NERR_Success == NetWkstaGetInfo(NULL, 100, &pinfoRawData))
    {
        WKSTA_INFO_100 * pworkstationInfo = (WKSTA_INFO_100 *)pinfoRawData;

        major = pworkstationInfo->wki100_ver_major;
        minor = pworkstationInfo->wki100_ver_minor;

        NetApiBufferFree(pinfoRawData);

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
                                     Bwx& /*aUrl*/)
{
    /*
    const TUint        versionLen = 16;

    HttpReader         client(aEnv);
    ReaderUntilS<1024> readerUntil(client);
    Brn                urlBuf(aFeed);
    Uri                feed(urlBuf);

    // Obtain the version of windows we are running as a version string.
    DWORD major, minor;
    TChar platformVersion[versionLen+1];

    if (GetWindowsVersion(major, minor))
    {
        if (sprintf_s(platformVersion, versionLen, "%u.%u", major, minor) == -1)
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    // Connect to the server.
    if (!client.Connect(feed))
    {
        return false;
    }

    // Process the feed.
    try
    {
        for (;;)
        {
            // Check this release record is for our platform.
            Brn jsonKey = Brn("platform");
            if (ReadValue(readerUntil, jsonKey) == Brn("win32"))
            {

                // Check this release record is valid for our platform version.
                jsonKey    = Brn("platformMinVersion");
                Brn minVer = ReadValue(readerUntil, jsonKey);

                if (isOlderVersion(platformVersion, (TChar *)(minVer.Ptr())))
                {
                    continue;
                }

                // Check if the release record offers an update to our
                // current version.
                jsonKey     = Brn("version");
                Brn version = ReadValue(readerUntil, jsonKey);

                if (isOlderVersion(CURRENT_VERSION, (TChar *)(version.Ptr())))
                {
                    jsonKey = Brn("uri");
                    Brn uri = ReadValue(readerUntil, jsonKey);

                    // This record represents a valid update.
                    // Return the update location.
                    aUrl.Replace(uri);
                    return true;
                }
            }
        }
    }
    // Catch the end of stream exception.
    catch (ReaderError&)
    {
    }
    */

    return false;
}
