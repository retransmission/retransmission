// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/utils.h> // tr_getRatio()

#import "TorrentGroup.h"
#import "GroupsController.h"
#import "Torrent.h"

@interface TorrentGroupData ()
- (instancetype)initWithRatio:(CGFloat)ratio uploadRate:(CGFloat)uploadRate downloadRate:(CGFloat)downloadRate;
@end

@implementation TorrentGroupData
- (instancetype)initWithRatio:(CGFloat)ratio uploadRate:(CGFloat)uploadRate downloadRate:(CGFloat)downloadRate
{
    self = [super init];
    if (self) {
        _ratio = ratio;
        _uploadRate = uploadRate;
        _downloadRate = downloadRate;
    }
    return self;
}
@end

@implementation TorrentGroup

- (instancetype)initWithGroup:(NSInteger)group
{
    if ((self = [super init])) {
        _groupIndex = group;
        _torrents = [[NSMutableArray alloc] init];
    }
    return self;
}

- (NSString*)description
{
    return [NSString stringWithFormat:@"Torrent Group %ld: %@", self.groupIndex, self.torrents];
}

- (NSInteger)groupOrderValue
{
    return [GroupsController.groups rowValueForIndex:self.groupIndex];
}

- (CGFloat)ratio
{
    uint64_t uploaded = 0, total_size = 0;
    for (Torrent* torrent in self.torrents) {
        uploaded += torrent.uploadedTotal;
        total_size += torrent.totalSizeSelected;
    }

    return tr_getRatio(uploaded, total_size);
}

- (CGFloat)uploadRate
{
    CGFloat rate = 0.0;
    for (Torrent* torrent in self.torrents) {
        rate += torrent.uploadRate;
    }

    return rate;
}

- (CGFloat)downloadRate
{
    CGFloat rate = 0.0;
    for (Torrent* torrent in self.torrents) {
        rate += torrent.downloadRate;
    }

    return rate;
}

- (TorrentGroupData*)aggregatedData
{
    uint64_t uploaded = 0;
    uint64_t total_size = 0;

    CGFloat uploadRate = 0.0;

    CGFloat downloadRate = 0.0;

    for (Torrent* torrent in self.torrents) {
        uploaded += torrent.uploadedTotal;
        total_size += torrent.totalSizeSelected;

        downloadRate += torrent.downloadRate;
        uploadRate += torrent.uploadRate;
    }

    CGFloat ratio = tr_getRatio(uploaded, total_size);

    auto result = [[TorrentGroupData alloc] initWithRatio:ratio uploadRate:uploadRate downloadRate:downloadRate];
    return result;
}

@end
