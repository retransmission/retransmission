// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#if __has_feature(modules)
@import AppKit;
#else
#import <AppKit/AppKit.h>
#endif

#include <libtransmission/utils.h>

#import "Controller.h"

// Shows the launch alerts and then loads the main nib, while the app finishes launching.
// The nib's objects read the settings an import changes, so the nib loads after the alerts.
// Alerts shown before NSApplication runs would keep every window the app opens afterwards off screen.
@interface LaunchDelegate : NSObject<NSApplicationDelegate>

// Kept for the app's lifetime, as NSApplicationMain keeps a main nib's objects.
@property(nonatomic) NSArray* mainNibObjects;

@end

@implementation LaunchDelegate

- (void)applicationWillFinishLaunching:(NSNotification*)notification
{
    // Names this app in the menu bar during the alerts; the main nib's menu replaces it.
    NSMenu* const launchMenu = [[NSMenu alloc] init];
    [launchMenu addItemWithTitle:@"" action:nil keyEquivalent:@""].submenu = [[NSMenu alloc] init];
    NSApp.mainMenu = launchMenu;

    [Controller prepareForLaunch];

    NSArray* objects = nil;
    [NSBundle.mainBundle loadNibNamed:@"MainMenu" owner:NSApp topLevelObjects:&objects];
    self.mainNibObjects = objects;

    // The nib made Controller the app's delegate after this notification went out.
    [NSApp.delegate applicationWillFinishLaunching:notification];
}

@end

int main()
{
    tr_lib_init();

    tr_locale_set_global("");

    // NSApplication holds its delegate weakly.
    static LaunchDelegate* launchDelegate;
    @autoreleasepool {
        launchDelegate = [[LaunchDelegate alloc] init];
        [NSApplication sharedApplication].delegate = launchDelegate;
    }

    // Runs the app directly, because NSApplicationMain would also load a nib: with no NSMainNibFile, an arbitrary one.
    [NSApp run];
    return 0;
}
