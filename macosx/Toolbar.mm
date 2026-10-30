// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "Toolbar.h"

@interface Toolbar ()
@property(nonatomic) BOOL isRunningCustomizationPalette;
@end

@implementation Toolbar

- (void)setVisible:(BOOL)visible
{
    // This notification allows the controller to update and hide toolbar item bezels during the toggle.
    [NSNotificationCenter.defaultCenter postNotificationName:@"ToolbarDidChange" object:nil];

    super.visible = visible;
}

- (void)runCustomizationPalette:(nullable id)sender
{
    _isRunningCustomizationPalette = YES;
    [super runCustomizationPalette:sender];
    _isRunningCustomizationPalette = NO;
}

@end
