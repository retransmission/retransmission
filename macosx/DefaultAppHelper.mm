// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "DefaultAppHelper.h"

#import <AppKit/AppKit.h>
#import "UTTypeAdditions.h"

static NSString* const kMagnetURLScheme = @"magnet";

@interface DefaultAppHelper ()

@property(nonatomic, readonly) NSString* bundleIdentifier;

@end

@implementation DefaultAppHelper

- (instancetype)init
{
    if (self = [super init]) {
        _bundleIdentifier = NSBundle.mainBundle.bundleIdentifier;
    }
    return self;
}

- (BOOL)isDefaultForTorrentFiles
{
    auto fileType = UTType.torrent;
    NSURL* appUrl = [NSWorkspace.sharedWorkspace URLForApplicationToOpenContentType:fileType];
    if (!appUrl) {
        return NO;
    }

    NSString* bundleId = [NSBundle bundleWithURL:appUrl].bundleIdentifier;

    if ([self.bundleIdentifier isEqualToString:bundleId]) {
        return YES;
    }

    return NO;
}

- (void)setDefaultForTorrentFiles:(void (^_Nullable)())completionHandler
{
    auto fileType = UTType.torrent;
    NSURL* appUrl = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:self.bundleIdentifier];
    [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:appUrl toOpenContentType:fileType completionHandler:^(NSError* error) {
        if (error) {
            NSLog(@"Failed setting default torrent file handler: %@", error.localizedDescription);
        }
        if (completionHandler != nil) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completionHandler();
            });
        }
    }];
}

- (BOOL)isDefaultForMagnetURLs
{
    NSURL* schemeUrl = [NSURL URLWithString:[kMagnetURLScheme stringByAppendingString:@":"]];
    NSURL* appUrl = [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:schemeUrl];
    if (!appUrl) {
        return NO;
    }

    NSString* bundleId = [NSBundle bundleWithURL:appUrl].bundleIdentifier;

    if ([self.bundleIdentifier isEqualToString:bundleId]) {
        return YES;
    }

    return NO;
}

- (void)setDefaultForMagnetURLs:(void (^_Nullable)())completionHandler
{
    NSURL* appUrl = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:self.bundleIdentifier];
    [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:appUrl toOpenURLsWithScheme:kMagnetURLScheme
                                          completionHandler:^(NSError* error) {
                                              if (error) {
                                                  NSLog(@"Failed setting default magnet link handler: %@", error.localizedDescription);
                                              }
                                              if (completionHandler != nil) {
                                                  dispatch_async(dispatch_get_main_queue(), ^{
                                                      completionHandler();
                                                  });
                                              }
                                          }];
}

@end
