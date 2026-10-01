// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#import "InfoViewController.h"

@interface InfoOptionsViewController : NSViewController<InfoViewController>

- (NSRect)viewRect;
- (void)checkLayout;
- (void)checkWindowSize;
- (void)updateWindowLayout;

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents;
- (void)updateInfo;
- (void)updateOptions;

@property(nonatomic) IBOutlet NSView* fPriorityView;
@property(nonatomic) CGFloat oldHeight;

@end
