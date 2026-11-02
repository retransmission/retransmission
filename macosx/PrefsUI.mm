// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PrefsUI.h"

static CGFloat const defaultPadding = 19;

@implementation PrefsUI

+ (NSTextField*)sectionTitle:(NSString*)text
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.font = [NSFont preferredFontForTextStyle:NSFontTextStyleTitle3 options:@{}];
    return label;
}

+ (NSTextField*)smallLabel:(NSString*)text
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    label.textColor = [NSColor disabledControlTextColor];
    return label;
}

+ (NSButton*)checkbox:(NSString*)title target:(id)target action:(SEL)action
{
    NSButton* checkbox = [NSButton checkboxWithTitle:title target:target action:action];
    checkbox.translatesAutoresizingMaskIntoConstraints = NO;
    checkbox.imagePosition = NSImageLeft;

    checkbox.cell.lineBreakMode = NSLineBreakByWordWrapping;
    checkbox.cell.usesSingleLineMode = NO;
    [checkbox setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];

    return checkbox;
}

+ (NSButton*)pushButton:(NSString*)title target:(id)target action:(SEL)action
{
    NSButton* button = [NSButton buttonWithTitle:title target:target action:action];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.bezelStyle = NSBezelStyleRounded;
    return button;
}

+ (NSPopUpButton*)popUpButton
{
    NSPopUpButton* button = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.bezelStyle = NSBezelStyleRounded;
    button.autoenablesItems = NO;
    return button;
}

+ (NSTextField*)numberInputFieldWithWidth:(CGFloat)width minimum:(NSInteger)min maximum:(NSInteger)max
{
    NSTextField* textField = [[NSTextField alloc] init];
    textField.translatesAutoresizingMaskIntoConstraints = NO;
    textField.cell.sendsActionOnEndEditing = YES;
    textField.focusRingType = NSFocusRingTypeNone;
    textField.drawsBackground = YES;

    [textField.widthAnchor constraintEqualToConstant:width].active = YES;

    NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
    formatter.numberStyle = NSNumberFormatterNoStyle;
    formatter.allowsFloats = NO;
    formatter.minimum = @(min);
    formatter.maximum = @(max);
    textField.formatter = formatter;

    return textField;
}

+ (NSView*)indentedView:(NSView*)view
{
    return [self indentedView:view level:1];
}

+ (NSView*)indentedView:(NSView*)view level:(NSInteger)level
{
    NSStackView* const stack = [NSStackView stackViewWithViews:@[ view ]];
    stack.edgeInsets = { 0, defaultPadding * level, 0, 0 };
    return stack;
}

@end
