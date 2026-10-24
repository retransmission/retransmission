// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentContextMenuProvider.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"TorrentContextMenu", @"")

@implementation TorrentContextMenuProvider

+ (NSString*)groupsMenuTitle
{
    return @"DynamicGroupsMenu";
}

+ (NSString*)shareMenuTitle
{
    return @"DynamicShareMenu";
}

+ (BOOL)isGroupsSubmenu:(NSMenu*)menu
{
    return [menu.title isEqualToString:[self groupsMenuTitle]];
}

+ (BOOL)isShareSubmenu:(NSMenu*)menu
{
    return [menu.title isEqualToString:[self shareMenuTitle]];
}

+ (NSMenuItem*)createItemWithTitle:(NSString*)title target:(id)target action:(SEL)action symbolName:(NSString*)symbolName
{
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    item.target = target;

    if (@available(macOS 26.0, *)) {
        if (symbolName.length > 0) {
            item.image = [NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:nil];
        }
    }

    return item;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundeclared-selector"

+ (NSMenu*)makeRowContextMenuWithTarget:(Controller*)target
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"ContextRowMenu"];

    // 1. Pause
    NSMenuItem* pauseItem = [self createItemWithTitle:LStr(@"Pause") target:target action:@selector(stopSelectedTorrents:)
                                           symbolName:@"pause"];
    [menu addItem:pauseItem];

    // 2. Start
    NSMenuItem* startItem = [self createItemWithTitle:LStr(@"Start") target:target action:@selector(resumeSelectedTorrents:)
                                           symbolName:@"arrow.clockwise"];
    [menu addItem:startItem];

    // 3. Start Now
    NSMenuItem* startNowItem = [self createItemWithTitle:LStr(@"Start Now") target:target
                                                  action:@selector(resumeSelectedTorrentsNoWait:)
                                              symbolName:@"arrow.clockwise.circle"];
    [menu addItem:startNowItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 4. Remove
    NSMenuItem* removeItem = [self createItemWithTitle:LStr(@"Remove") target:target action:@selector(removeNoDelete:)
                                            symbolName:@"trash"];
    [menu addItem:removeItem];

    // 5. Delete Files and Remove
    NSMenuItem* deleteItem = [self createItemWithTitle:LStr(@"Delete Files and Remove") target:target
                                                action:@selector(removeDeleteData:)
                                            symbolName:@"document.on.trash"];
    [menu addItem:deleteItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 6. Show in Finder
    NSMenuItem* finderItem = [self createItemWithTitle:LStr(@"Show in Finder") target:target action:@selector(revealFile:)
                                            symbolName:@"finder"];
    [menu addItem:finderItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 7. Group (with submenu)
    NSMenuItem* groupItem = [self createItemWithTitle:LStr(@"Group") target:nil action:nil symbolName:@"pin"];
    NSMenu* groupSubmenu = [[NSMenu alloc] initWithTitle:[self groupsMenuTitle]];
    groupSubmenu.delegate = (id<NSMenuDelegate>)target;
    groupItem.submenu = groupSubmenu;
    [menu addItem:groupItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 8. Set Location…
    NSMenuItem* locationItem = [self createItemWithTitle:LStr(@"Set Location…") target:target
                                                  action:@selector(moveDataFilesSelected:)
                                              symbolName:@"arrow.up.doc"];
    [menu addItem:locationItem];

    // 9. Rename…
    NSMenuItem* renameItem = [self createItemWithTitle:LStr(@"Rename…") target:target action:@selector(renameSelected:)
                                            symbolName:@"pencil"];
    [menu addItem:renameItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 10. Save Torrent File As…
    NSMenuItem* saveAsItem = [self createItemWithTitle:LStr(@"Save Torrent File As…") target:target
                                                action:@selector(copyTorrentFiles:)
                                            symbolName:@"arrow.down.doc"];
    [menu addItem:saveAsItem];

    // 11. Share Torrent File (with submenu)
    NSMenuItem* shareItem = [self createItemWithTitle:LStr(@"Share Torrent File") target:nil action:nil
                                           symbolName:@"square.and.arrow.up"];
    NSMenu* shareSubmenu = [[NSMenu alloc] initWithTitle:[self shareMenuTitle]];
    shareSubmenu.delegate = (id<NSMenuDelegate>)target;
    shareItem.submenu = shareSubmenu;
    [menu addItem:shareItem];

    // 12. Copy Magnet Link to Clipboard
    NSMenuItem* magnetItem = [self createItemWithTitle:LStr(@"Copy Magnet Link to Clipboard") target:target
                                                action:@selector(copyMagnetLinks:)
                                            symbolName:@""];
    [menu addItem:magnetItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 13. Ask Tracker for More Peers
    NSMenuItem* trackerItem = [self createItemWithTitle:LStr(@"Ask Tracker for More Peers") target:target
                                                 action:@selector(announceSelectedTorrents:)
                                             symbolName:@"megaphone"];
    [menu addItem:trackerItem];

    // 14. Verify Local Data
    NSMenuItem* verifyItem = [self createItemWithTitle:LStr(@"Verify Local Data") target:target
                                                action:@selector(verifySelectedTorrents:)
                                            symbolName:@"bandage"];
    [menu addItem:verifyItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 15. Show Inspector
    NSMenuItem* inspectorItem = [self createItemWithTitle:LStr(@"Show Inspector") target:target action:@selector(showInfo:)
                                               symbolName:@"info.circle"];
    [menu addItem:inspectorItem];

    return menu;
}

+ (NSMenu*)makeNoRowContextMenuWithTarget:(Controller*)target
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"ContextNoRowMenu"];

    // 1. Create Torrent File…
    NSMenuItem* createItem = [self createItemWithTitle:LStr(@"Create Torrent File…") target:target action:@selector(createFile:)
                                            symbolName:@"doc.badge.plus"];
    [menu addItem:createItem];

    // 2. Open…
    NSMenuItem* openItem = [self createItemWithTitle:LStr(@"Open…") target:target action:@selector(openShowSheet:)
                                          symbolName:@"folder"];
    [menu addItem:openItem];

    // 3. Open URL…
    NSMenuItem* openUrlItem = [self createItemWithTitle:LStr(@"Open URL…") target:target action:@selector(openURLShowSheet:)
                                             symbolName:@"globe"];
    [menu addItem:openUrlItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 4. Show Inspector
    NSMenuItem* inspectorItem = [self createItemWithTitle:LStr(@"Show Inspector") target:target action:@selector(showInfo:)
                                               symbolName:@"info.circle"];
    [menu addItem:inspectorItem];

    return menu;
}

#pragma clang diagnostic pop

@end
