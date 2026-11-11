// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/web-utils.h> //tr_addressIsIP()
#import "TrackerIconLoader.h"

@interface TrackerIconLoader ()
@property(nonatomic, readonly) NSCache<NSString*, NSImage*>* iconCache;
@property(nonatomic, readonly) NSMutableSet<NSString*>* loadingAddresses;
@end

@implementation TrackerIconLoader

+ (instancetype)sharedInstance
{
    static TrackerIconLoader* shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[TrackerIconLoader alloc] init];
    });
    return shared;
}

- (instancetype)init
{
    if (self = [super init]) {
        _iconCache = [[NSCache alloc] init];
        _iconCache.countLimit = 100;
        _loadingAddresses = [[NSMutableSet alloc] init];
    }
    return self;
}

- (void)onDidLoadIcon:(NSImage*)icon baseAddress:(NSString*)baseAddress completion:(void (^)(NSImage* image))completion
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (icon) {
            [self.iconCache setObject:icon forKey:baseAddress];
        } else {
            [self.iconCache setObject:(NSImage*)NSNull.null forKey:baseAddress];
        }
        [self.loadingAddresses removeObject:baseAddress];

        completion(icon ?: [NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil]);
    });
}

- (void)fetchIconForAddress:(NSString*)addressString completion:(void (^)(NSImage* image))completion
{
    NSURL* address = [NSURL URLWithString:addressString];
    NSString* host = address.host;
    if (!host || tr_addressIsIP(host.UTF8String)) {
        completion([NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil]);
        return;
    }
    NSArray<NSString*>* hostComponents = [host componentsSeparatedByString:@"."];
    if (hostComponents.count < 2) {
        completion([NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil]);
        return;
    }

    NSString* baseAddress = [NSString
        stringWithFormat:@"%@.%@", hostComponents[hostComponents.count - 2], hostComponents[hostComponents.count - 1]];

    NSImage* cachedIcon = [_iconCache objectForKey:baseAddress];
    if (cachedIcon) {
        completion(((id)cachedIcon == NSNull.null) ? [NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil] : cachedIcon);
        return;
    }

    completion([NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil]);

    if ([_loadingAddresses containsObject:baseAddress]) {
        return;
    }

    [_loadingAddresses addObject:baseAddress];

    NSString* favIconUrl = [NSString stringWithFormat:@"https://icons.duckduckgo.com/ip3/%@.ico", baseAddress];
    NSURLRequest* request = [NSURLRequest requestWithURL:[NSURL URLWithString:favIconUrl] cachePolicy:NSURLRequestUseProtocolCachePolicy
                                         timeoutInterval:15.0];

    __weak TrackerIconLoader* weakSelf = self;
    [[NSURLSession.sharedSession dataTaskWithRequest:request completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
        NSImage* icon = nil;
        if (!error && ((NSHTTPURLResponse*)response).statusCode == 200) {
            icon = [[NSImage alloc] initWithData:data];
        }

        [weakSelf onDidLoadIcon:icon baseAddress:baseAddress completion:completion];
    }] resume];
}
@end
