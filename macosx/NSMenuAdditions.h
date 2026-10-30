// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

typedef NS_ENUM(NSUInteger, TRMenuItemRole) {
    TRMenuItemRoleOpenIgnoreDownloadFolder,
    TRMenuItemRoleNextFilter,
    TRMenuItemRoleNextInfo,
    TRMenuItemRolePrevInfo
};

@interface NSMenuItem (NSMenuItemAdditions)
+ (NSMenuItem*)createItemWithTitle:(NSString*)title target:(id)target action:(SEL)action symbolName:(NSString*)symbolName;
+ (NSMenuItem*)createItemWithTitle:(NSString*)title action:(SEL)action tag:(NSInteger)tag;
@property(nonatomic) NSString* symbolName;
@end

@interface NSMenu (NSMenuAdditions)
- (NSMenuItem*)addItemWithTitle:(NSString*)string target:(id)target action:(SEL)selector symbolName:(NSString*)symbolName;
@end

@interface NSMenu (Submenu)
+ (instancetype)makeGroupsSubmenu;
+ (instancetype)makeShareSubmenu;
@property(nonatomic, readonly) BOOL isGroupsSubmenu;
@property(nonatomic, readonly) BOOL isShareSubmenu;
@end

@interface NSMenuItem (TRMenuItemsRoles)
- (void)setMenuItemRole:(TRMenuItemRole)role;
@property(nonatomic, readonly) BOOL isOpenIgnoreDownloadFolderItem;
@property(nonatomic, readonly) BOOL isNextFilterItem;
@property(nonatomic, readonly) BOOL isNextInfoTabItem;
@property(nonatomic, readonly) BOOL isPrevInfoTabItem;
@end
