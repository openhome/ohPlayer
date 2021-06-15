#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>

#include "ConfigPersistentStore.h"
#import "Prefs.h"

using namespace OpenHome;
using namespace OpenHome::Configuration;

// Registry based implementation of the IStoreReadWrite interface

ConfigPersistentStore::ConfigPersistentStore()
    : iLock("RAMS")
{
    prefs = [[Prefs alloc] init];
    [prefs load];
}

ConfigPersistentStore::~ConfigPersistentStore()
{
    [prefs save];
}

void ConfigPersistentStore::Read(const Brx& /*aKey*/,
                                 IWriter& /*aWriter*/)
{ }

void ConfigPersistentStore::Read(const Brx& aKey, Bwx& aDest)
{
    AutoMutex a(iLock);
    Brhz aKeyZ(aKey);
    
    // Initialise the destination buffer.
    aDest.SetBytes(0);

    NSString *value = [prefs getPref: [NSString stringWithUTF8String: (const char *)aKeyZ.Ptr()]];
    
    // we have a hex string representing the bytes we want to use to fill aDest.
    // convert the data to bytes in aDest
    
    // first check if we can actually return the right
    if (value==nil)
        THROW(StoreKeyNotFound);
    
    // value is a hex encoded string with a null terminator
    // our actual byte count will be ([value length] / 2)
    if (([value length]/2) > (aDest.MaxBytes()))
        THROW(StoreReadBufferUndersized);
    
    const char *cvalue = [value cStringUsingEncoding:NSUTF8StringEncoding];
    if(cvalue == NULL)
        return;
    
    for(int i=0; i<value.length; i+=2)
    {
        int val;
        sscanf(cvalue+i, "%2x", &val);
        char c = val & 0xff;
        aDest.TryAppend(c);
    }
}

void ConfigPersistentStore::ResetToDefaults()
{
    
}

// Encode a preference buffer as a null-terminated hex string
// This allows us to simply handle storage of arbitrary data
// buffers using preference strings
static char * encodePref(const Brx& aPref)
{
    // allocate 2 bytes per source byte plus a null terminator
    size_t encodesize = (aPref.Bytes()*2) +1;
    char * buff = new char[encodesize];
    char * tmp = buff;
    
    if(!buff)
        return nil;
    
    for(int i=0; i < aPref.Bytes(); i++)
    {
        sprintf(tmp, "%.2x", aPref.At(i));
        tmp += 2;
    }
    
    // Now null terminate the buffer
    *tmp = 0;
    
    return buff;
}

void ConfigPersistentStore::Write(const Brx& aKey, const Brx& aSource)
{
    AutoMutex a(iLock);
    Brhz aKeyZ(aKey);
  
    // inefficient but straightforward rendering of property buffer as a prefs string
    // convert to hex representation
    NSString *_key=[NSString stringWithUTF8String:(const char *)aKeyZ.Ptr()];
    
    const char *encoded = encodePref(aSource);
    NSString *_value=[NSString stringWithUTF8String:encoded];
    delete[] encoded;
    
    [prefs setPref:_key value:_value];
}

void ConfigPersistentStore::Delete(const Brx& aKey)
{
    AutoMutex a(iLock);
    Brhz aKeyZ(aKey);

    NSString *_key = [NSString stringWithUTF8String: (const char *)aKeyZ.Ptr()];
    
    [prefs setPref:_key value:nil];
}
