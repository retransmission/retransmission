// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <libtransmission/types.h>

@class Torrent;

@interface TrackerNode : NSObject

@property(nonatomic, weak, readonly) Torrent* torrent;

- (instancetype)initWithTrackerView:(tr_tracker_view const*)stat torrent:(Torrent*)torrent;

- (BOOL)isEqual:(id)object;

@property(nonatomic, readonly) NSString* host;
@property(nonatomic, readonly) NSString* fullAnnounceAddress;

@property(nonatomic, readonly) NSInteger tier;

@property(nonatomic, readonly) NSUInteger identifier;

/// What the tracker's last announce and scrape got and when it is asked again, in the other clients' words.
@property(nonatomic, readonly) NSArray<NSString*>* statusLines;

@end
