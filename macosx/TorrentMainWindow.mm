// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentMainWindow.h"

@implementation TorrentMainWindow

- (instancetype)initWindowWithContentView:(TorrentMainView*)contentView
{
    self = [super initWithContentRect:NSMakeRect(71, 712, 480, 269)
                            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                                       NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView)
                              backing:NSBackingStoreBuffered
                                defer:NO];

    if (self) {
        _mainView = contentView;
        self.title = @"Retransmission";
        self.frameAutosaveName = @"TransmissionWindow";
        self.minSize = NSMakeSize(350, 5);
        self.tabbingMode = NSWindowTabbingModeDisallowed;
        self.releasedWhenClosed = NO;
        self.restorable = NO;
        self.contentView = _mainView;
    }
    return self;
}

@end
