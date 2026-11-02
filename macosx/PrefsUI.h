// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface PrefsUI : NSObject

+ (NSTextField*)sectionTitle:(NSString*)text;

+ (NSTextField*)smallLabel:(NSString*)text;

+ (NSButton*)checkbox:(NSString*)title target:(id)target action:(SEL)action;

+ (NSButton*)pushButton:(NSString*)title target:(id)target action:(SEL)action;

+ (NSPopUpButton*)popUpButton;

+ (NSTextField*)numberInputFieldWithWidth:(CGFloat)width minimum:(NSInteger)min maximum:(NSInteger)max;

/// Views modifiers
/// Wrapped into stack with default padding.
+ (NSView*)indentedView:(NSView*)view;
+ (NSView*)indentedView:(NSView*)view level:(NSInteger)level;

@end
