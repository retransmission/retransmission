// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCellRevealButton.h"
#import "NSImageAdditions.h"
#import "TorrentTableView.h"
#import "TorrentCell.h"

@interface TorrentCellRevealButton ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic, readonly) TorrentTableView* torrentTableView;
@end

@implementation TorrentCellRevealButton

- (TorrentTableView*)torrentTableView
{
    return self.torrentCell.fTorrentTableView;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        NSImage* revealImage = [NSImage re_symbolNamed:@"magnifyingglass" pointSize:15.0 weight:NSFontWeightRegular];
        self.image = revealImage;

        self.contentTintColor = [NSColor secondaryLabelColor];
    }
    return self;
}

- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];
    self.contentTintColor = [NSColor labelColor];

    [self.torrentTableView hoverEventBeganForView:self];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];
    self.contentTintColor = [NSColor secondaryLabelColor];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    //when filterbar is shown, we need to remove focus otherwise action fails
    [self.window makeFirstResponder:self.torrentTableView];

    [super mouseDown:event];
    self.contentTintColor = [NSColor controlAccentColor];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    if (self.fTrackingArea != nil) {
        [self removeTrackingArea:self.fTrackingArea];
    }

    NSTrackingAreaOptions options = (NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways);
    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds options:options owner:self userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];
}

@end
