// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class Torrent;

typedef struct {
    CGFloat ratio;
    CGFloat uploadRate;
    CGFloat downloadRate;
} TorrentGroupData;

@interface TorrentGroup : NSObject

- (instancetype)initWithGroup:(NSInteger)group;

@property(nonatomic, readonly) NSInteger groupIndex;
@property(nonatomic, readonly) NSInteger groupOrderValue;
@property(nonatomic, readonly) NSMutableArray<Torrent*>* torrents;

@property(nonatomic, readonly) TorrentGroupData aggregatedData;

@end
