// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <ctime>

#include <libtransmission/string-utils.h>

#include <libtransmission-app/formatters.h>

#import "TrackerNode.h"
#import "L10n.h"

@interface TrackerNode ()

@property(nonatomic, readonly) tr_tracker_view fStat;

@end

@implementation TrackerNode

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

- (NSArray<NSString*>*)statusLines
{
    if (self.fStat.isBackup) {
        return @[ TR_TEXT("Tracker will be used as a backup") ];
    }

    // The cell draws plain text, so the lines take no markup and escape nothing.
    static constexpr auto PlainText = tr::app::TrackerStatusMarkup{ .pending_begin = {}, .pending_end = {}, .escape = false };
    auto const lines = tr::app::tracker_status_lines(self.fStat, time(nullptr), true, PlainText);

    NSMutableArray<NSString*>* const strings = [NSMutableArray arrayWithCapacity:std::size(lines)];
    for (auto const& line : lines) {
        [strings addObject:tr_strv_to_utf8_nsstring(line)];
    }
    return strings;
}

@end
