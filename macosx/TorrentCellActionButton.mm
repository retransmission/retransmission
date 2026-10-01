// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCellActionButton.h"
#import "TorrentTableView.h"
#import "Torrent.h"
#import "TorrentCell.h"

@interface TorrentCellActionButton ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic) NSImage* fImage;
@property(nonatomic) NSImage* fAlternativeImage;
@property(nonatomic, readonly) TorrentTableView* torrentTableView;
@property(nonatomic) NSUserDefaults* fDefaults;
@end

@implementation TorrentCellActionButton

- (TorrentTableView*)torrentTableView
{
    return self.torrentCell.fTorrentTableView;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        self.fDefaults = NSUserDefaults.standardUserDefaults;
        self.fImage = [NSImage imageNamed:@"ActionHover"];

        // hide image by default and show only on hover
        self.fAlternativeImage = [[NSImage alloc] init];
        self.image = self.fAlternativeImage;

        // disable button click highlighting
        [self.cell setHighlightsBy:NSNoCellMask];
    }
    return self;
}

- (void)handleIsHovering:(BOOL)isHovering
{
    self.image = isHovering ? self.fImage : self.fAlternativeImage;
}

- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];

    [self handleIsHovering:YES];

    [self.torrentTableView hoverEventBeganForView:self];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];

    [self handleIsHovering:NO];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    //when filterbar is shown, we need to remove focus otherwise action fails
    [self.window makeFirstResponder:self.torrentTableView];

    [super mouseDown:event];

    BOOL minimal = [self.fDefaults boolForKey:@"SmallView"];
    if (!minimal) {
        [self.torrentTableView hoverEventEndedForView:self];
    }
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    if (self.fTrackingArea != nil) {
        [self removeTrackingArea:self.fTrackingArea];
    }

    NSTrackingAreaOptions options = (NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways);

    // Check if mouse is currently inside the bounds
    NSPoint mouseLocation = [self.window mouseLocationOutsideOfEventStream];
    NSPoint localPoint = [self convertPoint:mouseLocation fromView:nil];
    if (NSPointInRect(localPoint, self.bounds)) {
        options |= NSTrackingAssumeInside;
        [self handleIsHovering:YES];
    } else {
        [self handleIsHovering:NO];
    }

    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds options:options owner:self userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];
}

@end
