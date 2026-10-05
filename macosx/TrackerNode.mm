// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TrackerNode.h"
#import "NSStringAdditions.h"
#import "L10n.h"

@interface TrackerNode ()

@property(nonatomic, readonly) tr_tracker_view fStat;

@end

@implementation TrackerNode

+ (NSDateFormatter*)dateFormatter
{
    static NSDateFormatter* formatter = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        formatter = [[NSDateFormatter alloc] init];
        formatter.dateStyle = NSDateFormatterFullStyle;
        formatter.timeStyle = NSDateFormatterShortStyle;
        formatter.doesRelativeDateFormatting = YES;
        formatter.timeZone = NSTimeZone.localTimeZone;
    });
    return formatter;
}

- (instancetype)initWithTrackerView:(tr_tracker_view const*)stat torrent:(Torrent*)torrent
{
    if ((self = [super init])) {
        _fStat = *stat;
        _torrent = torrent; //weak reference
    }

    return self;
}

- (NSString*)description
{
    return [@"Tracker: " stringByAppendingString:self.fullAnnounceAddress];
}

- (id)copyWithZone:(NSZone*)zone
{
    //this object is essentially immutable after initial setup
    return self;
}

- (BOOL)isEqual:(id)object
{
    if (self == object) {
        return YES;
    }

    if (![object isKindOfClass:[self class]]) {
        return NO;
    }

    auto other = static_cast<decltype(self)>(object);
    if (self.torrent != other.torrent) {
        return NO;
    }

    return self.tier == other.tier && [self.fullAnnounceAddress isEqualToString:other.fullAnnounceAddress];
}

- (NSString*)host
{
    return @(self.fStat.host_and_port);
}

- (NSString*)fullAnnounceAddress
{
    return @(self.fStat.announce.c_str());
}

- (NSInteger)tier
{
    return self.fStat.tier;
}

- (NSUInteger)identifier
{
    return self.fStat.id;
}

- (NSInteger)totalSeeders
{
    return self.fStat.seederCount;
}

- (NSInteger)totalLeechers
{
    return self.fStat.leecherCount;
}

- (NSInteger)totalDownloaded
{
    return self.fStat.downloadCount;
}

- (NSString*)lastAnnounceStatusString
{
    NSString* dateString;
    if (self.fStat.hasAnnounced) {
        dateString = [self.class.dateFormatter stringFromDate:[NSDate dateWithTimeIntervalSince1970:self.fStat.lastAnnounceTime]];
    } else {
        // Translators: Tracker last announce
        dateString = TR_TEXT("N/A");
    }

    NSString* baseString;
    if (self.fStat.hasAnnounced && self.fStat.lastAnnounceTimedOut) {
        // Translators: Tracker last announce
        baseString = [TR_TEXT("Announce timed out") stringByAppendingFormat:@": %@", dateString];
    } else if (self.fStat.hasAnnounced && !self.fStat.lastAnnounceSucceeded) {
        // Translators: Tracker last announce
        baseString = TR_TEXT("Announce error");

        NSString* errorString = @(self.fStat.lastAnnounceResult);
        if ([errorString isEqualToString:@""]) {
            baseString = [baseString stringByAppendingFormat:@": %@", dateString];
        } else {
            baseString = [baseString stringByAppendingFormat:@": %@ - %@", errorString, dateString];
        }
    } else {
        // Translators: Tracker last announce
        baseString = [TR_TEXT("Last Announce") stringByAppendingFormat:@": %@", dateString];
        if (self.fStat.hasAnnounced && self.fStat.lastAnnounceSucceeded && self.fStat.lastAnnouncePeerCount > 0) {
            auto const peerCount = self.fStat.lastAnnouncePeerCount;
            // Translators: Tracker last announce
            NSString* peerString = TR_FORMAT_N("got {count:L} peer", "got {count:L} peers", peerCount, TRArg("count", peerCount));
            baseString = [baseString stringByAppendingFormat:@" (%@)", peerString];
        }
    }

    return baseString;
}

- (NSString*)nextAnnounceStatusString
{
    switch (self.fStat.announceState) {
    case TR_TRACKER_ACTIVE:
        // Translators: Tracker next announce
        return [TR_TEXT("Announce in progress") stringByAppendingEllipsis];

    case TR_TRACKER_WAITING:
        {
            NSTimeInterval const nextAnnounceTimeLeft = self.fStat.nextAnnounceTime - [NSDate date].timeIntervalSince1970;

            static NSDateComponentsFormatter* formatter;
            static dispatch_once_t onceToken;
            dispatch_once(&onceToken, ^{
                formatter = [NSDateComponentsFormatter new];
                formatter.unitsStyle = NSDateComponentsFormatterUnitsStyleAbbreviated;
                formatter.zeroFormattingBehavior = NSDateComponentsFormatterZeroFormattingBehaviorDropLeading;
                formatter.collapsesLargestUnit = YES;
            });

            NSString* timeString = [formatter stringFromTimeInterval:nextAnnounceTimeLeft];
            return TR_FORMAT("Next announce in {time_span}", TRArg("time_span", timeString));
        }
    case TR_TRACKER_QUEUED:
        return [TR_TEXT("Queued to ask for more peers") stringByAppendingEllipsis];

    case TR_TRACKER_INACTIVE:
        return self.fStat.isBackup ? TR_TEXT("Tracker will be used as a backup") :
                                     // Translators: Tracker next announce
                                     TR_TEXT("No updates scheduled");

    default:
        NSAssert1(NO, @"unknown announce state: %d", self.fStat.announceState);
        return nil;
    }
}

- (NSString*)lastScrapeStatusString
{
    NSString* dateString;
    if (self.fStat.hasScraped) {
        dateString = [self.class.dateFormatter stringFromDate:[NSDate dateWithTimeIntervalSince1970:self.fStat.lastScrapeTime]];
    } else {
        // Translators: Tracker last scrape
        dateString = TR_TEXT("N/A");
    }

    NSString* baseString;
    if (self.fStat.hasScraped && self.fStat.lastScrapeTimedOut) {
        // Translators: Tracker last scrape
        baseString = [TR_TEXT("Scrape timed out") stringByAppendingFormat:@": %@", dateString];
    } else if (self.fStat.hasScraped && !self.fStat.lastScrapeSucceeded) {
        // Translators: Tracker last scrape
        baseString = TR_TEXT("Scrape error");

        NSString* errorString = @(self.fStat.lastScrapeResult);
        if ([errorString isEqualToString:@""]) {
            baseString = [baseString stringByAppendingFormat:@": %@", dateString];
        } else {
            baseString = [baseString stringByAppendingFormat:@": %@ - %@", errorString, dateString];
        }
    } else {
        // Translators: Tracker last scrape
        baseString = [TR_TEXT("Last Scrape") stringByAppendingFormat:@": %@", dateString];
    }

    return baseString;
}

@end
