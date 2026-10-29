// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TrackerNode.h"
#import "NSStringAdditions.h"

@implementation TrackerNode {
    tr_tracker_view _fStat;
}

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
    return @(_fStat.host_and_port);
}

- (NSString*)fullAnnounceAddress
{
    return @(_fStat.announce.c_str());
}

- (NSInteger)tier
{
    return _fStat.tier;
}

- (NSUInteger)identifier
{
    return _fStat.id;
}

- (NSInteger)totalSeeders
{
    return _fStat.seederCount;
}

- (NSInteger)totalLeechers
{
    return _fStat.leecherCount;
}

- (NSInteger)totalDownloaded
{
    return _fStat.downloadCount;
}

- (NSString*)lastAnnounceStatusString
{
    NSString* dateString;
    if (_fStat.hasAnnounced) {
        dateString = [self.class.dateFormatter stringFromDate:[NSDate dateWithTimeIntervalSince1970:_fStat.lastAnnounceTime]];
    } else {
        dateString = NSLocalizedString(@"N/A", "Tracker last announce");
    }

    NSString* baseString;
    if (_fStat.hasAnnounced && _fStat.lastAnnounceTimedOut) {
        baseString = [NSLocalizedString(@"Announce timed out", "Tracker last announce") stringByAppendingFormat:@": %@", dateString];
    } else if (_fStat.hasAnnounced && !_fStat.lastAnnounceSucceeded) {
        baseString = NSLocalizedString(@"Announce error", "Tracker last announce");

        NSString* errorString = @(_fStat.lastAnnounceResult);
        if ([errorString isEqualToString:@""]) {
            baseString = [baseString stringByAppendingFormat:@": %@", dateString];
        } else {
            baseString = [baseString stringByAppendingFormat:@": %@ - %@", errorString, dateString];
        }
    } else {
        baseString = [NSLocalizedString(@"Last Announce", "Tracker last announce") stringByAppendingFormat:@": %@", dateString];
        if (_fStat.hasAnnounced && _fStat.lastAnnounceSucceeded && _fStat.lastAnnouncePeerCount > 0) {
            NSString* peerString;
            if (_fStat.lastAnnouncePeerCount == 1) {
                peerString = NSLocalizedString(@"got 1 peer", "Tracker last announce");
            } else {
                peerString = [NSString localizedStringWithFormat:NSLocalizedString(@"got %zu peers", "Tracker last announce"),
                                                                 (size_t)_fStat.lastAnnouncePeerCount];
            }
            baseString = [baseString stringByAppendingFormat:@" (%@)", peerString];
        }
    }

    return baseString;
}

- (NSString*)nextAnnounceStatusString
{
    switch (_fStat.announceState) {
    case TR_TRACKER_ACTIVE:
        return [NSLocalizedString(@"Announce in progress", "Tracker next announce") stringByAppendingEllipsis];

    case TR_TRACKER_WAITING:
        {
            NSTimeInterval const nextAnnounceTimeLeft = _fStat.nextAnnounceTime - [NSDate date].timeIntervalSince1970;

            static NSDateComponentsFormatter* formatter;
            static dispatch_once_t onceToken;
            dispatch_once(&onceToken, ^{
                formatter = [NSDateComponentsFormatter new];
                formatter.unitsStyle = NSDateComponentsFormatterUnitsStyleAbbreviated;
                formatter.zeroFormattingBehavior = NSDateComponentsFormatterZeroFormattingBehaviorDropLeading;
                formatter.collapsesLargestUnit = YES;
            });

            NSString* timeString = [formatter stringFromTimeInterval:nextAnnounceTimeLeft];
            return [NSString stringWithFormat:NSLocalizedString(@"Next announce in %@", "Tracker next announce"), timeString];
        }
    case TR_TRACKER_QUEUED:
        return [NSLocalizedString(@"Queued to ask for more peers", "Tracker next announce") stringByAppendingEllipsis];

    case TR_TRACKER_INACTIVE:
        return _fStat.isBackup ? NSLocalizedString(@"Tracker will be used as a backup", "Tracker next announce") :
                                 NSLocalizedString(@"No updates scheduled", "Tracker next announce");

    default:
        NSAssert1(NO, @"unknown announce state: %d", _fStat.announceState);
        return nil;
    }
}

- (NSString*)lastScrapeStatusString
{
    NSString* dateString;
    if (_fStat.hasScraped) {
        dateString = [self.class.dateFormatter stringFromDate:[NSDate dateWithTimeIntervalSince1970:_fStat.lastScrapeTime]];
    } else {
        dateString = NSLocalizedString(@"N/A", "Tracker last scrape");
    }

    NSString* baseString;
    if (_fStat.hasScraped && _fStat.lastScrapeTimedOut) {
        baseString = [NSLocalizedString(@"Scrape timed out", "Tracker last scrape") stringByAppendingFormat:@": %@", dateString];
    } else if (_fStat.hasScraped && !_fStat.lastScrapeSucceeded) {
        baseString = NSLocalizedString(@"Scrape error", "Tracker last scrape");

        NSString* errorString = @(_fStat.lastScrapeResult);
        if ([errorString isEqualToString:@""]) {
            baseString = [baseString stringByAppendingFormat:@": %@", dateString];
        } else {
            baseString = [baseString stringByAppendingFormat:@": %@ - %@", errorString, dateString];
        }
    } else {
        baseString = [NSLocalizedString(@"Last Scrape", "Tracker last scrape") stringByAppendingFormat:@": %@", dateString];
    }

    return baseString;
}

@end
