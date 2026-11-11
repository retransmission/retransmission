// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "NSMenuAdditions.h"

@implementation NSMenuItem (NSMenuItemAdditions)
+ (NSMenuItem*)createItemWithTitle:(NSString*)title target:(id)target action:(SEL)action symbolName:(NSString*)symbolName
{
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    item.target = target;

    [item setSymbolName:symbolName];

    return item;
}

+ (NSMenuItem*)createItemWithTitle:(NSString*)title action:(SEL)action tag:(NSInteger)tag
{
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];

    item.tag = tag;

    return item;
}

- (NSString*)symbolName
{
    return nil;
}

- (void)setSymbolName:(NSString*)symbolName
{
    if (@available(macOS 26.0, *)) {
        if (symbolName.length > 0) {
            self.image = [NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:nil];
        }
    }
}
@end

@implementation NSMenu (NSMenuAdditions)
- (NSMenuItem*)addItemWithTitle:(NSString*)string
                         target:(id)target
                         action:(nullable SEL)selector
                     symbolName:(NSString*)symbolName
{
    NSMenuItem* item = [self addItemWithTitle:string action:selector keyEquivalent:@""];
    item.target = target;
    [item setSymbolName:symbolName];
    return item;
}
@end

static NSUserInterfaceItemIdentifier const TRGroupsSubmenuIdentifier = @"com.retransmission.menu.groups";
static NSUserInterfaceItemIdentifier const TRShareSubmenuIdentifier = @"com.retransmission.menu.share";
static NSUserInterfaceItemIdentifier const TRMenuItemRoleOpenIgnoreDownloadFolderIdentifier = @"com.retransmission.menuItem.OpenIgnoreDownloadFolderIdentifier";
static NSUserInterfaceItemIdentifier const TRMenuItemRoleNextFilterIdentifier = @"com.retransmission.menuItem.NextFilterIdentifier";
static NSUserInterfaceItemIdentifier const TRMenuItemRoleNextInfoIdentifier = @"com.retransmission.menuItem.NextInfoIdentifier";
static NSUserInterfaceItemIdentifier const TRMenuItemRolePrevInfoIdentifier = @"com.retransmission.menuItem.PrevInfoIdentifier";

@implementation NSMenu (Submenu)
+ (instancetype)makeGroupsSubmenu
{
    auto result = [[self alloc] initWithTitle:@""];
    result.identifier = TRGroupsSubmenuIdentifier;
    return result;
}

+ (instancetype)makeShareSubmenu
{
    auto result = [[self alloc] initWithTitle:@""];
    result.identifier = TRShareSubmenuIdentifier;
    return result;
}

- (BOOL)isGroupsSubmenu
{
    return [self.identifier isEqualToString:TRGroupsSubmenuIdentifier];
}

- (BOOL)isShareSubmenu
{
    return [self.identifier isEqualToString:TRShareSubmenuIdentifier];
}
@end

@implementation NSMenuItem (TRMenuItemsRoles)

- (void)setMenuItemRole:(TRMenuItemRole)role
{
    switch (role) {
    case TRMenuItemRoleOpenIgnoreDownloadFolder:
        self.identifier = TRMenuItemRoleOpenIgnoreDownloadFolderIdentifier;
        break;
    case TRMenuItemRoleNextFilter:
        self.identifier = TRMenuItemRoleNextFilterIdentifier;
        break;
    case TRMenuItemRoleNextInfo:
        self.identifier = TRMenuItemRoleNextInfoIdentifier;
        break;
    case TRMenuItemRolePrevInfo:
        self.identifier = TRMenuItemRolePrevInfoIdentifier;
        break;
    }
}

- (BOOL)isOpenIgnoreDownloadFolderItem
{
    return [self.identifier isEqualToString:TRMenuItemRoleOpenIgnoreDownloadFolderIdentifier];
}

- (BOOL)isNextFilterItem
{
    return [self.identifier isEqualToString:TRMenuItemRoleNextFilterIdentifier];
}

- (BOOL)isNextInfoTabItem
{
    return [self.identifier isEqualToString:TRMenuItemRoleNextInfoIdentifier];
}

- (BOOL)isPrevInfoTabItem
{
    return [self.identifier isEqualToString:TRMenuItemRolePrevInfoIdentifier];
}

@end
