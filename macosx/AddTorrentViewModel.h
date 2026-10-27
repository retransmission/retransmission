// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

@interface AddTorrentViewModel : NSObject

@property(nonatomic, readonly) Torrent* torrent;
@property(nonatomic, readonly) NSString* destinationPath;
@property(nonatomic, readonly) NSString* guardedDestinationPath; // return @"" if destinationPath is nil
@property(nonatomic, readonly) NSImage* locationIcon;
@property(nonatomic, readonly) NSInteger groupValue;
@property(nonatomic, readonly) NSInteger priorityIndex;
@property(nonatomic, readonly) BOOL lockDestination;

@property(nonatomic, copy) void (^onDataUpdate)(AddTorrentViewModel* viewModel);
@property(nonatomic, copy) void (^onGroupsUpdate)(AddTorrentViewModel* viewModel);
@property(nonatomic, copy) void (^onCancelTrigger)(void);
@property(nonatomic, copy) void (^onSuccessTrigger)(void);

- (void)triggerDataUpdate;
- (void)triggerGroupsUpdate;

- (instancetype)initWithTorrent:(Torrent*)torrent destination:(NSString*)path lockDestination:(BOOL)lockDestination;

- (void)selectDestinationModalForWindow:(NSWindow*)window;
- (void)changePriorityToIndex:(NSInteger)index;
- (void)changeGroupValueToTag:(NSInteger)tag;
- (NSMenu*)createGroupsMenuWithTarget:(id)target action:(SEL)action;

- (void)confirmAddWithWindow:(NSWindow*)window;

@end
