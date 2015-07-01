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
}

ConfigPersistentStore::~ConfigPersistentStore()
{
    [prefs save];
}


void ConfigPersistentStore::Read(const Brx& aKey, Bwx& aDest)
{
    AutoMutex a(iLock);
    
    NSString *value = [prefs getPref: [NSString stringWithUTF8String: (const char *)aKey.Ptr()]];
    
    // we have a hex string representing the bytes we want to use to fill aDest.
    // convert the data to bytes in aDest
    
    // first check if we can actually return the right
    if (value==nil)
        THROW(StoreKeyNotFound);
    
    if ([value length] > (aDest.MaxBytes()+1))
        THROW(StoreReadBufferUndersized);
    
    const char *cvalue = [value cStringUsingEncoding:NSUTF8StringEncoding];
    if(cvalue == NULL)
        return;
    
    for(int i=0; i<(value.length / 2); i+=2)
    {
        int val;
        sscanf(cvalue+i, "%2x", &val);
        char c = val & 0xff;
        aDest.TryAppend(c);
    }
}

static char * encodePref(const Brx& aPref)
{
    char * buff = new char((aPref.Bytes()+1) * 2);
    char * tmp = buff;
    
    if(!buff)
        return nil;
    
    for(int i=0; i < aPref.Bytes(); i++)
        sprintf(tmp, "%.2x", aPref.At(i));
    
    return buff;
}

void ConfigPersistentStore::Write(const Brx& aKey, const Brx& aSource)
{
    AutoMutex a(iLock);
    
    // inefficient but straightforward rendering of property buffer as a prefs string
    // convert to hex representation
    NSString *_key=[NSString stringWithUTF8String:encodePref(aKey)];
    NSString *_value=[NSString stringWithUTF8String:encodePref(aSource)];
    
    [prefs setPref:_key value:_value];
}

void ConfigPersistentStore::Delete(const Brx& aKey)
{
    AutoMutex a(iLock);

    NSString *_key = [NSString stringWithUTF8String: (const char *)aKey.Ptr()];
    
    [prefs setPref:_key value:nil];
}
