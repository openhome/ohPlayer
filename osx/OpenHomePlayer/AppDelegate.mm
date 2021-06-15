//
//  AppDelegate.m
//  Sample app delegate
//
//  Copyright (c) 2015 OpenHome Limited. All rights reserved.
//

#import "AppDelegate.h"

#include "version.h"
#include "MediaPlayerIF.h"
#include "ExampleMediaPlayer.h"
#include "UpdateCheck.h"

@interface AppDelegate ()

- (IBAction)saveAction:(id)sender;

@end

@implementation AppDelegate

using namespace OpenHome::Av::Example;

NSTimer* _updateTimer = nil;

OpenHome::Av::Example::MediaPlayerIF *samplePlayer = nil;

std::vector<MediaPlayerIF::SubnetRecord*> *subnetList = nil;
NSMenuItem *subnetItem =  nil;

- (void)getOSVersion:(long *)major minor:(long *)minor
{
    NSProcessInfo *pInfo = [NSProcessInfo processInfo];
    NSOperatingSystemVersion osVer = [pInfo operatingSystemVersion];

    if(major)
        *major = osVer.majorVersion;
    if(minor)
        *minor = osVer.minorVersion;
}

- (void)checkUpdates:(id)sender
{
    if(samplePlayer != nil)
    {
        // Read the current version from application Info.plist
        NSDictionary *dict    = [[NSBundle mainBundle] infoDictionary];
        NSString     *version = [dict objectForKey:@"CFBundleShortVersionString"];

        long _minor = 0;
        long _major = 0;
        [self getOSVersion:&_major minor:&_minor];
        char *uri = samplePlayer->checkForUpdate(_major & 0xffff,
                                                 _minor & 0xffff,
                                                 [version UTF8String]);

        if(uri !=nil)
        {
            NSString * uriString = [NSString stringWithUTF8String:uri];
            delete uri;
            [self showUpdateNotification:self updateUri:uriString];
        }
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // we have finished launching the app so create our status menu
    // The app has been configured without a main window by setting
    // the 'Application is agent' property in info.plist
    // and not having a main window or view controller
    [self setupStatusItem];

    // schedule a repeating timer to periodically check for updates
    // to mediaplayer
    TIpAddress initialAddress = {
        kFamilyV4,  // iFamily
        0xFFFFFF,   // iV4
        { 255 },    // iV6
    };
    
    samplePlayer = new OpenHome::Av::Example::MediaPlayerIF( initialAddress );
    _updateTimer = [NSTimer scheduledTimerWithTimeInterval:CHECK_INTERVAL target:self selector:@selector(checkUpdates:) userInfo:nil repeats:YES];

    // schedule an initial (short) timer to check for updates
    [NSTimer scheduledTimerWithTimeInterval:INITIAL_CHECK_INTERVAL target:self selector:@selector(checkUpdates:) userInfo:nil repeats:NO];

    // register self to receive UserNotification actions
    [[NSUserNotificationCenter defaultUserNotificationCenter] setDelegate:self];
    
    [self fileNotifications];
}

- (void)updateAvailable:(NSString *)uri
{
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Update available"];
    [alert setInformativeText:@"An update is available for the OpenHome Sample Application. Do you want to download and install it?"];
    [alert addButtonWithTitle:@"Yes"];
    [alert addButtonWithTitle:@"No"];

    NSInteger answer = [alert runModal];

    if (answer == NSAlertFirstButtonReturn) {
        // download the new app
        NSURL *url = [NSURL URLWithString:uri];
        [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification
{
    if( [notification.title isEqual:@"OpenHome OSX Player"] )
    {
        NSDictionary *dict = [notification userInfo];
        NSString *uri = [dict valueForKey:@"uri"];
        [self updateAvailable:uri];
    }
    [center removeDeliveredNotification: notification];
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center didDeliverNotification:(NSUserNotification *)notification
{
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification
{
    return YES;
}

- (IBAction)showUpdateNotification:(id)sender updateUri:(NSString *)uri{
    NSUserNotification *notification = [[NSUserNotification alloc] init];
    notification.title = @"OpenHome OSX Player";
    notification.informativeText = @"An update is available for your player. Click this message to download it.";
    notification.soundName = NSUserNotificationDefaultSoundName;
    NSMutableDictionary *dict = [[NSMutableDictionary alloc] init];
    [dict setValue:uri forKey:@"uri"];
    notification.userInfo = dict;

    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification ];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    delete samplePlayer;
}

- (void) receiveSleepNote: (NSNotification*) note
{
    if(samplePlayer)
    {
        NSLog(@"ohPlayer Suspend: %@", [note name]);
        samplePlayer->NotifySuspended();
    }
}

- (void) receiveWakeNote: (NSNotification*) note
{
    if(samplePlayer)
    {
        NSLog(@"ohPlayer Resume: %@", [note name]);
        samplePlayer->NotifyResumed();
    }
}

- (void) fileNotifications
{
    //These notifications are filed on NSWorkspace's notification center, not the default
    // notification center. You will not receive sleep/wake notifications if you file
    //with the default notification center.

    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver: self
            selector: @selector(receiveSleepNote:)
            name: NSWorkspaceWillSleepNotification object: NULL];

    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver: self
            selector: @selector(receiveWakeNote:)
            name: NSWorkspaceDidWakeNotification object: NULL];
}

- (void)setupStatusItem
{
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    _statusItem.title = @"";
    _statusItem.image = [NSImage imageNamed:@"menu"];
    [_statusItem.image setTemplate:true];
    _statusItem.alternateImage = [NSImage imageNamed:@"menu-hilite"];
    [_statusItem.alternateImage setTemplate:true];
    _statusItem.highlightMode = YES;

    [self initMenu];
}

- (void)initMenu
{
    // create a menu
    NSMenu *menu = [[NSMenu alloc] init];
    [menu addItemWithTitle:@"Check for updates" action:@selector(checkUpdates:) keyEquivalent:@""];
    subnetItem = [menu addItemWithTitle:@"Subnet" action:@selector(subnet:) keyEquivalent:@""];

    [menu addItemWithTitle:@"About" action:@selector(about:) keyEquivalent:@""];

    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Play" action:@selector(lp_play:) keyEquivalent:@""];
    [menu addItemWithTitle:@"Pause" action:@selector(lp_pause:) keyEquivalent:@""];
    [menu addItemWithTitle:@"Stop" action:@selector(lp_stop:) keyEquivalent:@""];

    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit OpenHome" action:@selector(terminate:) keyEquivalent:@""];

    self.statusItem.menu = menu;
}

- (void)createSubnetPopup
{
    if (samplePlayer && subnetItem)
    {
        // create a submenu for the subnet lists
        NSMenu *submenu = [[NSMenu alloc] init];

        // get subnet list
        if(subnetList)
            samplePlayer->FreeSubnets(subnetList);
        subnetList = samplePlayer->GetSubnets();

        int index = 0;
        std::vector<MediaPlayerIF::SubnetRecord*>::iterator it;

        // Put each subnet in our popup menu.
        for (it=subnetList->begin(); it < subnetList->end(); it++)
        {
            NSString  *str = [NSString stringWithUTF8String: (*it)->menuString->c_str() ];

            // add a subnet menu entry for each subnet
            NSMenuItem *item = [submenu addItemWithTitle:str action:@selector(selectNet:) keyEquivalent:@""];

            // set the item to represent the current index value
            NSNumber *num = [NSNumber numberWithInt:index];
            [item setRepresentedObject:num];

            // If this is the subnet we are currently using disable it's selection.
            if ((*it)->isCurrent)
                [item setTag:0];
            else
                [item setTag:1];

            // Add the menu item.
            index++;
        }

        // add the submenu to our main menu
        [subnetItem setSubmenu:submenu];
    }
}

- (BOOL)validateUserInterfaceItem:(id <NSValidatedUserInterfaceItem>)anItem
{
    SEL theAction = [anItem action];

    // enable our player menu items based on the current mediaplayer state
    // note that we must check with the player as it may have been paused/played
    // by external controlpoints, and the mediaplayer monitors the playback
    // state directly so should always be up to date
    OpenHome::Av::Example::ExampleMediaPlayer * mp = samplePlayer ? samplePlayer->mediaPlayer() : NULL;
    if (theAction == @selector(lp_pause:)) {
        return( (mp && mp->CanPause()) ? YES : NO );
    }
    else if (theAction == @selector(lp_play:)) {
        return( (mp && mp->CanPlay()) ? YES : NO );
    }
    else if (theAction == @selector(lp_stop:)) {
        return( (mp && mp->CanHalt()) ? YES : NO );
    }
    else if (theAction == @selector(subnet:)) {
        [self createSubnetPopup];
        return( mp ? YES : NO );
    }
    else if (theAction == @selector(selectNet:)) {
        return( ([anItem tag]==1) ? YES : NO );
    }

    // all our other menu items should always be enabled
    return YES;
}

- (void)subnet:(id)sender
{
}

- (void)selectNet:(id)sender
{
    if(!subnetList)
        return;

    // retrieve the desired subnet index in the list
    NSMenuItem *item = (NSMenuItem *)sender;
    NSNumber *num = [item representedObject];

    int index = [num intValue];
    if(index < subnetList->size())
    {
        MediaPlayerIF::SubnetRecord *rec = subnetList->at(index);

        if(rec)
        {
            delete samplePlayer;
            samplePlayer = new OpenHome::Av::Example::MediaPlayerIF( rec->subnet );
        }
    }
}

- (void)lp_play:(id)sender
{
    if(samplePlayer != nil)
        samplePlayer->PlayPipeLine();
}

- (void)lp_pause:(id)sender
{
    if(samplePlayer != nil)
        samplePlayer->PausePipeLine();
}

- (void)lp_stop:(id)sender
{
    if(samplePlayer != nil)
        samplePlayer->StopPipeLine();
}

- (void)about:(id) sender
{
    // invoke the standard Cocoa about box using details
    // pulled from the app resources
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp orderFrontStandardAboutPanel:self];
}


#pragma mark - Core Data stack

@synthesize persistentStoreCoordinator = _persistentStoreCoordinator;
@synthesize managedObjectModel = _managedObjectModel;
@synthesize managedObjectContext = _managedObjectContext;


- (NSURL *)applicationDocumentsDirectory {
    // The directory the application uses to store the Core Data store file. This code uses a directory named "com.openhome.mediaplayer" in the user's Application Support directory.
    NSURL *appSupportURL = [[[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask] lastObject];
    return [appSupportURL URLByAppendingPathComponent:@"com.openhome.player"];
}

- (NSManagedObjectModel *)managedObjectModel {
    // The managed object model for the application. It is a fatal error for the application not to be able to find and load its model.
    if (_managedObjectModel) {
        return _managedObjectModel;
    }

    NSURL *modelURL = [[NSBundle mainBundle] URLForResource:@"sample" withExtension:@"momd"];
    _managedObjectModel = [[NSManagedObjectModel alloc] initWithContentsOfURL:modelURL];
    return _managedObjectModel;
}

- (NSPersistentStoreCoordinator *)persistentStoreCoordinator {
    // The persistent store coordinator for the application. This implementation creates and return a coordinator, having added the store for the application to it. (The directory for the store is created, if necessary.)
    if (_persistentStoreCoordinator) {
        return _persistentStoreCoordinator;
    }

    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSURL *applicationDocumentsDirectory = [self applicationDocumentsDirectory];
    BOOL shouldFail = NO;
    NSError *error = nil;
    NSString *failureReason = @"There was an error creating or loading the application's saved data.";

    // Make sure the application files directory is there
    NSDictionary *properties = [applicationDocumentsDirectory resourceValuesForKeys:@[NSURLIsDirectoryKey] error:&error];
    if (properties) {
        if (![properties[NSURLIsDirectoryKey] boolValue]) {
            failureReason = [NSString stringWithFormat:@"Expected a folder to store application data, found a file (%@).", [applicationDocumentsDirectory path]];
            shouldFail = YES;
        }
    } else if ([error code] == NSFileReadNoSuchFileError) {
        error = nil;
        [fileManager createDirectoryAtPath:[applicationDocumentsDirectory path] withIntermediateDirectories:YES attributes:nil error:&error];
    }

    if (!shouldFail && !error) {
        NSPersistentStoreCoordinator *coordinator = [[NSPersistentStoreCoordinator alloc] initWithManagedObjectModel:[self managedObjectModel]];
        NSURL *url = [applicationDocumentsDirectory URLByAppendingPathComponent:@"OSXCoreDataObjC.storedata"];
        if (![coordinator addPersistentStoreWithType:NSXMLStoreType configuration:nil URL:url options:nil error:&error]) {
            coordinator = nil;
        }
        _persistentStoreCoordinator = coordinator;
    }

    if (shouldFail || error) {
        // Report any error we got.
        NSMutableDictionary *dict = [NSMutableDictionary dictionary];
        dict[NSLocalizedDescriptionKey] = @"Failed to initialize the application's saved data";
        dict[NSLocalizedFailureReasonErrorKey] = failureReason;
        if (error) {
            dict[NSUnderlyingErrorKey] = error;
        }
        error = [NSError errorWithDomain:@"Init Error" code:9999 userInfo:dict];
        [[NSApplication sharedApplication] presentError:error];
    }
    return _persistentStoreCoordinator;
}

- (NSManagedObjectContext *)managedObjectContext {
    // Returns the managed object context for the application (which is already bound to the persistent store coordinator for the application.)
    if (_managedObjectContext) {
        return _managedObjectContext;
    }

    NSPersistentStoreCoordinator *coordinator = [self persistentStoreCoordinator];
    if (!coordinator) {
        return nil;
    }
    _managedObjectContext = [[NSManagedObjectContext alloc] initWithConcurrencyType:NSMainQueueConcurrencyType];
    [_managedObjectContext setPersistentStoreCoordinator:coordinator];

    return _managedObjectContext;
}

#pragma mark - Core Data Saving and Undo support

- (IBAction)saveAction:(id)sender {
    // Performs the save action for the application, which is to send the save: message to the application's managed object context. Any encountered errors are presented to the user.
    if (![[self managedObjectContext] commitEditing]) {
        NSLog(@"%@:%@ unable to commit editing before saving", [self class], NSStringFromSelector(_cmd));
    }

    NSError *error = nil;
    if ([[self managedObjectContext] hasChanges] && ![[self managedObjectContext] save:&error]) {
        [[NSApplication sharedApplication] presentError:error];
    }
}

- (NSUndoManager *)windowWillReturnUndoManager:(NSWindow *)window {
    // Returns the NSUndoManager for the application. In this case, the manager returned is that of the managed object context for the application.
    return [[self managedObjectContext] undoManager];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    // Save changes in the application's managed object context before the application terminates.

    if (!_managedObjectContext) {
        return NSTerminateNow;
    }

    if (![[self managedObjectContext] commitEditing]) {
        NSLog(@"%@:%@ unable to commit editing to terminate", [self class], NSStringFromSelector(_cmd));
        return NSTerminateCancel;
    }

    if (![[self managedObjectContext] hasChanges]) {
        return NSTerminateNow;
    }

    NSError *error = nil;
    if (![[self managedObjectContext] save:&error]) {

        // Customize this code block to include application-specific recovery steps.
        BOOL result = [sender presentError:error];
        if (result) {
            return NSTerminateCancel;
        }

        NSString *question = NSLocalizedString(@"Could not save changes while quitting. Quit anyway?", @"Quit without saves error question message");
        NSString *info = NSLocalizedString(@"Quitting now will lose any changes you have made since the last successful save", @"Quit without saves error question info");
        NSString *quitButton = NSLocalizedString(@"Quit anyway", @"Quit anyway button title");
        NSString *cancelButton = NSLocalizedString(@"Cancel", @"Cancel button title");
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:question];
        [alert setInformativeText:info];
        [alert addButtonWithTitle:quitButton];
        [alert addButtonWithTitle:cancelButton];

        NSInteger answer = [alert runModal];

        if (answer == NSAlertFirstButtonReturn) {
            return NSTerminateCancel;
        }
    }

    return NSTerminateNow;
}

@end
