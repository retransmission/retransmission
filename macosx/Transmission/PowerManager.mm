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

- (void)systemWillSleep:(NSNotification*)notification;
- (void)systemDidWakeUp:(NSNotification*)notification;

- (void)powerStateDidChange:(NSNotification*)notification NS_AVAILABLE_MAC(12_0);

@end

@implementation PowerManager {
    // held for the app's lifetime so macOS doesn't App-Nap the process
    woke::NapInhibitor _napInhibitor;

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
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(powerStateDidChange:)
                                                   name:NSProcessInfoPowerStateDidChangeNotification
                                                 object:nil];
        self.listening = YES;
    }

    _napInhibitor.inhibit(TR_PROJ_APPNAME_CAPITALIZED, "Application is running");
}

- (void)stop
{
    os_log_info(self.log, "Stopping power manager");
    if (self.isListening) {
        os_log_debug(self.log, "Unregistering sleep/wake/low power mode notifications");
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceWillSleepNotification object:nil];
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self name:NSWorkspaceDidWakeNotification object:nil];
        [NSNotificationCenter.defaultCenter removeObserver:self name:NSProcessInfoPowerStateDidChangeNotification object:nil];
        self.listening = NO;
    }

    _napInhibitor.uninhibit();
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
    if (shouldPreventSleep && NSProcessInfo.processInfo.lowPowerModeEnabled) {
        return;
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
