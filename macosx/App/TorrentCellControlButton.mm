// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCellControlButton.h"
#import "NSImageAdditions.h"
#import "TorrentTableView.h"
#import "Torrent.h"
#import "TorrentCell.h"

typedef NS_ENUM(NSInteger, ControlButtonHoverState) {
    ControlButtonHoverStateOff,
    ControlButtonHoverStateHover,
    ControlButtonHoverStateOn
};

@interface TorrentCellControlButton ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic) ControlButtonHoverState buttonState;
@property(nonatomic, readonly) TorrentTableView* torrentTableView;
@end

@implementation TorrentCellControlButton

- (TorrentTableView*)torrentTableView
{
    return self.torrentCell.fTorrentTableView;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        self.buttonState = ControlButtonHoverStateOff;
        [self updateImage];
    }
    return self;
}

- (void)resetImage
{
    self.buttonState = ControlButtonHoverStateOff;
    [self updateImage];
}

- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];
    self.buttonState = ControlButtonHoverStateHover;
    [self updateImage];

    [self.torrentTableView hoverEventBeganForView:self];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];
    self.buttonState = ControlButtonHoverStateOff;
    [self updateImage];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    [self.window makeFirstResponder:self.torrentTableView];

    [super mouseDown:event];
    self.buttonState = ControlButtonHoverStateOn;
    [self updateImage];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)updateImage
{
    Torrent* torrent = [self.torrentTableView itemAtRow:[self.torrentTableView rowForView:self]];

    NSString* symbolName;
    if (torrent.active) {
        symbolName = @"pause.circle.fill";
    } else if (NSApp.currentEvent.modifierFlags & NSEventModifierFlagOption) {
        symbolName = @"forward.end.fill";
    } else if (torrent.waitingToStart) {
        symbolName = @"pause.circle.fill";
    } else {
        symbolName = @"play.circle.fill";
    }

    NSImage* controlImage = [NSImage re_symbolNamed:symbolName pointSize:15.0 weight:NSFontWeightRegular];

    self.image = controlImage;

    switch (self.buttonState) {
    case ControlButtonHoverStateOff:
        self.contentTintColor = [NSColor secondaryLabelColor];
        break;
    case ControlButtonHoverStateHover:
        self.contentTintColor = [NSColor labelColor];
        break;
    case ControlButtonHoverStateOn:
        self.contentTintColor = [NSColor controlAccentColor];
        break;
    }
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
