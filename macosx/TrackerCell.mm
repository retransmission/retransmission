// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/web-utils.h> //tr_addressIsIP()

#import "CocoaCompatibility.h"

#import "TrackerCell.h"
#import "TrackerNode.h"
#import "L10n.h"

static CGFloat const kPaddingHorizontal = 3.0;
static CGFloat const kPaddingStatusHorizontal = 3.0;
static CGFloat const kIconSize = 16.0;
static CGFloat const kPaddingBetweenIconAndName = 4.0;
static CGFloat const kPaddingAboveIcon = 1.0;
static CGFloat const kPaddingAboveName = 1.0;
static CGFloat const kPaddingBetweenLines = 1.0;
static NSUInteger const kStatusRows = 3;

// make the favicons accessible to all tracker cells
static NSCache* fTrackerIconCache;
static NSMutableSet* fTrackerIconLoading;

@interface TrackerCell ()

@property(nonatomic, readonly) NSImage* favIcon;
@property(nonatomic, readonly) NSAttributedString* attributedName;
@property(nonatomic, readonly) NSMutableDictionary* fNameAttributes;
@property(nonatomic, readonly) NSMutableDictionary* fStatusAttributes;

@end

@implementation TrackerCell

+ (void)initialize
{
    if (self != [TrackerCell self])
        return;

    fTrackerIconCache = [[NSCache alloc] init];
    fTrackerIconLoading = [[NSMutableSet alloc] init];
}

- (instancetype)init
{
    if ((self = [super init])) {
        NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
        paragraphStyle.lineBreakMode = NSLineBreakByTruncatingTail;

        _fNameAttributes = [[NSMutableDictionary alloc]
            initWithObjectsAndKeys:[NSFont messageFontOfSize:12.0], NSFontAttributeName, paragraphStyle, NSParagraphStyleAttributeName, nil];

        _fStatusAttributes = [[NSMutableDictionary alloc]
            initWithObjectsAndKeys:[NSFont messageFontOfSize:9.5], NSFontAttributeName, paragraphStyle, NSParagraphStyleAttributeName, nil];
    }
    return self;
}

- (id)copyWithZone:(NSZone*)zone
{
    TrackerCell* copy = [super copyWithZone:zone];

    copy->_fNameAttributes = _fNameAttributes;
    copy->_fStatusAttributes = _fStatusAttributes;

    return copy;
}

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    //icon
    [self.favIcon drawInRect:[self imageRectForBounds:cellFrame] fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
                    fraction:1.0
              respectFlipped:YES
                       hints:nil];

    //set table colors
    NSColor *nameColor, *statusColor;
    if (self.backgroundStyle == NSBackgroundStyleEmphasized) {
        nameColor = statusColor = NSColor.whiteColor;
    } else {
        nameColor = NSColor.labelColor;
        statusColor = NSColor.secondaryLabelColor;
    }

    self.fNameAttributes[NSForegroundColorAttributeName] = nameColor;
    self.fStatusAttributes[NSForegroundColorAttributeName] = statusColor;

    TrackerNode* node = (TrackerNode*)self.objectValue;

    //name
    NSAttributedString* nameString = self.attributedName;
    NSRect const nameRect = [self rectForNameWithString:nameString inBounds:cellFrame];
    [nameString drawInRect:nameRect];

    //status strings
    // The cell has three rows. A tracker that has announced, has scraped and will scrape again has a fourth line,
    // which says when the next scrape is. That line isn't drawn.
    NSArray<NSString*>* const statusLines = node.statusLines;
    NSRect aboveRect = nameRect;
    for (NSUInteger i = 0; i < kStatusRows && i < statusLines.count; ++i) {
        NSAttributedString* statusString = [self attributedStatusWithString:statusLines[i]];
        aboveRect = [self rectForStatusWithString:statusString withAboveRect:aboveRect inBounds:cellFrame];
        [statusString drawInRect:aboveRect];
    }
}

#pragma mark - Private

- (NSImage*)favIcon
{
    id icon = nil;
    NSURL* address = [NSURL URLWithString:((TrackerNode*)self.objectValue).fullAnnounceAddress];
    NSString* host;
    if ((host = address.host)) {
        //don't try to parse ip address
        BOOL const isIP = tr_addressIsIP(host.UTF8String);
        NSArray* hostComponents = !isIP ? [host componentsSeparatedByString:@"."] : nil;

        if (!isIP && hostComponents.count >= 2) {
            NSString* domain = hostComponents[hostComponents.count - 2];
            NSString* tld = hostComponents[hostComponents.count - 1];
            NSString* baseAddress = [NSString stringWithFormat:@"%@.%@", domain, tld];

            icon = [fTrackerIconCache objectForKey:baseAddress];
            if (!icon) {
                [self loadTrackerIcon:baseAddress];
            }
        }
    }

    if ((icon && icon != [NSNull null])) {
        return icon;
    }

    NSImage* result = [NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil];
    [result lockFocus];
    [NSColor.textColor set];
    NSRect imageRect = { NSZeroPoint, result.size };
    NSRectFillUsingOperation(imageRect, NSCompositingOperationSourceIn);
    [result unlockFocus];
    return result;
}

- (void)loadTrackerIcon:(NSString*)baseAddress
{
    if ([fTrackerIconLoading containsObject:baseAddress]) {
        return;
    }
    [fTrackerIconLoading addObject:baseAddress];

    NSString* favIconUrl = [NSString stringWithFormat:@"https://icons.duckduckgo.com/ip3/%@.ico", baseAddress];

    NSURLRequest* request = [NSURLRequest requestWithURL:[NSURL URLWithString:favIconUrl] cachePolicy:NSURLRequestUseProtocolCachePolicy
                                         timeoutInterval:30.0];

    NSURLSessionDataTask* task = [NSURLSession.sharedSession
        dataTaskWithRequest:request completionHandler:^(NSData* iconData, NSURLResponse* response, NSError* error) {
            if (error) {
                NSLog(@"Unable to get tracker icon: task failed (%@)", error.localizedDescription);
                return;
            }
            BOOL ok = ((NSHTTPURLResponse*)response).statusCode == 200 ? YES : NO;
            if (!ok) {
                NSLog(@"Unable to get tracker icon: status code not OK (%ld)", (long)((NSHTTPURLResponse*)response).statusCode);
                return;
            }

            dispatch_async(dispatch_get_main_queue(), ^{
                NSImage* icon = [[NSImage alloc] initWithData:iconData];
                if (icon) {
                    [fTrackerIconCache setObject:icon forKey:baseAddress];

                    self.controlView.needsDisplay = YES;
                } else {
                    [fTrackerIconCache setObject:[NSNull null] forKey:baseAddress];
                }

                [fTrackerIconLoading removeObject:baseAddress];
            });
        }];
    [task resume];
}

- (NSRect)imageRectForBounds:(NSRect)bounds
{
    return NSMakeRect(NSMinX(bounds) + kPaddingHorizontal, NSMinY(bounds) + kPaddingAboveIcon, kIconSize, kIconSize);
}

- (NSRect)rectForNameWithString:(NSAttributedString*)string inBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + kPaddingHorizontal + kIconSize + kPaddingBetweenIconAndName;
    result.origin.y = NSMinY(bounds) + kPaddingAboveName;

    result.size.height = [string size].height;
    result.size.width = NSMaxX(bounds) - NSMinX(result) - kPaddingHorizontal;

    return result;
}

- (NSRect)rectForStatusWithString:(NSAttributedString*)string withAboveRect:(NSRect)aboveRect inBounds:(NSRect)bounds
{
    NSRect result;
    result.origin.x = NSMinX(bounds) + kPaddingStatusHorizontal;
    result.origin.y = NSMaxY(aboveRect) + kPaddingBetweenLines;

    result.size.height = [string size].height;
    result.size.width = NSMaxX(bounds) - kPaddingHorizontal - NSMinX(result);

    return result;
}

- (NSAttributedString*)attributedName
{
    NSString* name = ((TrackerNode*)self.objectValue).host;
    return [[NSAttributedString alloc] initWithString:name attributes:self.fNameAttributes];
}

- (NSAttributedString*)attributedStatusWithString:(NSString*)statusString
{
    return [[NSAttributedString alloc] initWithString:statusString attributes:self.fStatusAttributes];
}

@end
