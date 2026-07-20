// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "libtransmission/macros.h"

#import "PowerManager.h"

#include <os/log.h>

#include <woke/woke.hpp>

@interface PowerManager ()

@property(nonatomic, readonly) os_log_t log;
@property(getter=isListening) BOOL listening;

@property(nonatomic) id<NSObject> noNapActivity;

- (void)systemWillSleep:(NSNotification*)notification;
- (void)systemDidWakeUp:(NSNotification*)notification;

- (void)powerStateDidChange:(NSNotification*)notification NS_AVAILABLE_MAC(12_0);

@end

@implementation PowerManager {
    // held while torrents are active and the "prevent sleep" default is on
    woke::SleepInhibitor _sleepInhibitor;
}

+ (instancetype)shared
{
    static PowerManager* sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[PowerManager alloc] init];
    });
    return sharedInstance;
}

- (instancetype)init
{
    if ((self = [super init])) {
        _log = os_log_create(TR_PROJ_DOMAIN_APEX_REVERSED, "power");
        _listening = NO;
    }

    return self;
}

- (void)dealloc
{
    [self stop];
}

- (void)start
{
    os_log_info(self.log, "Starting power manager");
    if (!self.isListening) {
        os_log_debug(self.log, "Registering sleep/wake/low power mode notifications");
        [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(systemWillSleep:)
                                                               name:NSWorkspaceWillSleepNotification
                                                             object:nil];
        [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(systemDidWakeUp:)
                                                               name:NSWorkspaceDidWakeNotification
                                                             object:nil];
        if (@available(macOS 12.0, *)) {
            [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(powerStateDidChange:)
                                                       name:NSProcessInfoPowerStateDidChangeNotification
                                                     object:nil];
        }
        self.listening = YES;
    }

    if (self.noNapActivity == nil) {
        os_log_debug(self.log, "Starting no-nap activity");
        self.noNapActivity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                                                                          reason:@TR_PROJ_APPNAME_CAPITALIZED ": Application is active"];
    }
}

- (void)stop
{
    os_log_info(self.log, "Stopping power manager");
    if (self.isListening) {
        os_log_debug(self.log, "Unregistering sleep/wake/low power mode notifications");
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceWillSleepNotification object:nil];
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceDidWakeNotification object:nil];
        if (@available(macOS 12.0, *)) {
            [NSNotificationCenter.defaultCenter removeObserver:self name:NSProcessInfoPowerStateDidChangeNotification object:nil];
        }
        self.listening = NO;
    }

    if (self.noNapActivity != nil) {
        os_log_debug(self.log, "Ending no-nap activity");
        [NSProcessInfo.processInfo endActivity:self.noNapActivity];
        self.noNapActivity = nil;
    }

    _sleepInhibitor.uninhibit();
}

- (void)systemWillSleep:(NSNotification*)notification
{
    os_log_info(self.log, "System will sleep notification received");
    [self.delegate systemWillSleep];
}

- (void)systemDidWakeUp:(NSNotification*)notification
{
    os_log_info(self.log, "System did wake up notification received");
    [self.delegate systemDidWakeUp];
}

- (void)powerStateDidChange:(NSNotification*)notification
{
    os_log_info(self.log, "Power state did change notification received");
    if (NSProcessInfo.processInfo.lowPowerModeEnabled) {
        os_log_info(self.log, "Low power mode enabled, disabling sleep prevention");
        self.shouldPreventSleep = NO;
    }
}

- (void)setShouldPreventSleep:(BOOL)shouldPreventSleep
{
    if (@available(macOS 12.0, *)) {
        if (shouldPreventSleep && NSProcessInfo.processInfo.lowPowerModeEnabled) {
            return;
        }
    }

    if (shouldPreventSleep) {
        _sleepInhibitor.inhibit(TR_PROJ_APPNAME_CAPITALIZED, "Torrents are active");
    } else {
        _sleepInhibitor.uninhibit();
    }
}

- (BOOL)shouldPreventSleep
{
    return _sleepInhibitor.active();
}

@end
