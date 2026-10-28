// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "MenuProvider.h"
#import "NSMenuAdditions.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"AppMenu", @"")

@interface MenuProvider ()
@property(weak, nonatomic) id controller;
@end

@implementation MenuProvider
- (instancetype)initWithController:(id)controller
{
    if (self = [super init]) {
        self.controller = controller;
    }
    return self;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundeclared-selector"

- (NSMenu*)buildMainMenu:(id)sparkleController
{
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@""];

    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:[self buildAppMenu:sparkleController]];
    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:[self buildFileMenu]];
    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:[self buildEditMenu]];
    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:[self buildViewMenu]];
    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:[self buildTorrentMenu]];
    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:[self buildWindowMenu]];
    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:[self buildHelpMenu]];

    return mainMenu;
}

- (NSMenu*)buildAppMenu:(id)sparkleController
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"Retransmission")];

    [menu addItemWithTitle:LStr(@"About Retransmission") action:@selector(showAboutWindow:) keyEquivalent:@""];
    [[menu addItemWithTitle:LStr(@"Check for Update…") action:@selector(checkForUpdates:)
              keyEquivalent:@""] setTarget:sparkleController];
    [[menu addItemWithTitle:LStr(@"Donate") action:@selector(linkDonate:) keyEquivalent:@""] setSymbolName:@"heart"];

    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Preferences…") action:@selector(showPreferenceWindow:) keyEquivalent:@","];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenu* services = [[NSMenu alloc] initWithTitle:LStr(@"Services")];
    [[menu addItemWithTitle:LStr(@"Services") action:nil keyEquivalent:@""] setSubmenu:services];
    [NSApp setServicesMenu:services];

    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Hide Retransmission") action:@selector(hide:) keyEquivalent:@"h"];
    [[menu addItemWithTitle:LStr(@"Hide Others") action:@selector(hideOtherApplications:)
              keyEquivalent:@"h"] setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    [menu addItemWithTitle:LStr(@"Show All") action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];

    [menu addItemWithTitle:LStr(@"Quit Retransmission") action:@selector(terminate:) keyEquivalent:@"q"];

    return menu;
}

- (NSMenu*)buildFileMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"File")];

    NSMenuItem* create = [menu addItemWithTitle:LStr(@"Create Torrent File…") action:@selector(createFile:) keyEquivalent:@"n"];
    create.symbolName = @"doc.badge.plus";

    NSMenuItem* open = [menu addItemWithTitle:LStr(@"Open…") action:@selector(openShowSheet:) keyEquivalent:@"o"];
    open.symbolName = @"folder";

    NSMenuItem* openOpt = [menu addItemWithTitle:LStr(@"Open With Options Window…") action:@selector(openShowSheet:)
                                   keyEquivalent:@"o"];
    [openOpt setMenuItemRole:TRMenuItemRoleOpenIgnoreDownloadFolder];
    openOpt.symbolName = @"folder.badge.gear";
    [openOpt setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    openOpt.alternate = YES;

    NSMenuItem* openURL = [menu addItemWithTitle:LStr(@"Open URL…") action:@selector(openURLShowSheet:) keyEquivalent:@"u"];
    openURL.symbolName = @"globe";

    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Close Window") action:@selector(performClose:) keyEquivalent:@"w"];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* ql = [menu addItemWithTitle:LStr(@"Quick Look") action:@selector(toggleQuickLook:) keyEquivalent:@"y"];
    ql.symbolName = @"eye";
    ql.target = self.controller;
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* setLoc = [menu addItemWithTitle:LStr(@"Set Location…") action:@selector(moveDataFilesSelected:) keyEquivalent:@""];
    setLoc.symbolName = @"arrow.forward.folder";

    NSMenuItem* rename = [menu addItemWithTitle:LStr(@"Rename…") action:@selector(renameSelected:) keyEquivalent:@""];
    rename.symbolName = @"pencil";
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* saveAs = [menu addItemWithTitle:LStr(@"Save Torrent File As…") action:@selector(copyTorrentFiles:)
                                  keyEquivalent:@"s"];
    saveAs.symbolName = @"arrow.down.doc";

    NSMenu* shareSubmenu = [NSMenu makeShareSubmenu];
    shareSubmenu.delegate = (id<NSMenuDelegate>)self.controller;
    NSMenuItem* shareItem = [menu addItemWithTitle:LStr(@"Share Torrent File") action:nil keyEquivalent:@""];
    shareItem.submenu = shareSubmenu;
    shareItem.symbolName = @"square.and.arrow.up";

    [menu addItemWithTitle:LStr(@"Copy Magnet Link to Clipboard") action:@selector(copyMagnetLinks:) keyEquivalent:@""];

    return menu;
}

- (NSMenu*)buildEditMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"Edit")];
    [menu addItemWithTitle:LStr(@"Undo") action:@selector(undo:) keyEquivalent:@"z"];
    [menu addItemWithTitle:LStr(@"Redo") action:@selector(redo:) keyEquivalent:@"Z"];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Cut") action:@selector(cut:) keyEquivalent:@"x"];
    [menu addItemWithTitle:LStr(@"Copy") action:@selector(copy:) keyEquivalent:@"c"];
    [menu addItemWithTitle:LStr(@"Paste") action:@selector(paste:) keyEquivalent:@"v"];
    [menu addItemWithTitle:LStr(@"Delete") action:@selector(delete:) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Select All") action:@selector(selectAll:) keyEquivalent:@"a"];
    return menu;
}

- (NSMenu*)buildSortMenu
{
    // Tags are Controller.mm's SortTag values, which setSort: and validateMenuItem: switch on.
    NSMenu* sortMenu = [[NSMenu alloc] initWithTitle:LStr(@"Sort Torrents By")];

    // Always first
    NSMenuItem* order = [NSMenuItem createItemWithTitle:LStr(@"Queue Order") action:@selector(setSort:) tag:SortTagOrder];

    NSArray* sortItems = @[
        [NSMenuItem createItemWithTitle:LStr(@"Date Added") action:@selector(setSort:) tag:SortTagDate],
        [NSMenuItem createItemWithTitle:LStr(@"Name") action:@selector(setSort:) tag:SortTagName],
        [NSMenuItem createItemWithTitle:LStr(@"Progress") action:@selector(setSort:) tag:SortTagProgress],
        [NSMenuItem createItemWithTitle:LStr(@"State") action:@selector(setSort:) tag:SortTagState],
        [NSMenuItem createItemWithTitle:LStr(@"Tracker") action:@selector(setSort:) tag:SortTagTracker],
        [NSMenuItem createItemWithTitle:LStr(@"Activity") action:@selector(setSort:) tag:SortTagActivity],
        [NSMenuItem createItemWithTitle:LStr(@"Size") action:@selector(setSort:) tag:SortTagSize],
        [NSMenuItem createItemWithTitle:LStr(@"ETA") action:@selector(setSort:) tag:SortTagETA],
    ];

    sortItems = [sortItems sortedArrayUsingDescriptors:@[ [NSSortDescriptor sortDescriptorWithKey:@"title" ascending:YES
                                                                                         selector:@selector(localizedCompare:)] ]];

    [sortMenu addItem:order];
    for (NSMenuItem* item in sortItems) {
        [sortMenu addItem:item];
    }

    [sortMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* asc = [sortMenu addItemWithTitle:LStr(@"Ascending") action:@selector(setSortReverse:) keyEquivalent:@""];
    asc.image = [NSImage imageWithSystemSymbolName:@"arrow.up" accessibilityDescription:nil];

    NSMenuItem* desc = [sortMenu addItemWithTitle:LStr(@"Descending") action:@selector(setSortReverse:) keyEquivalent:@""];
    desc.image = [NSImage imageWithSystemSymbolName:@"arrow.down" accessibilityDescription:nil];
    desc.tag = SortOrderTagDescending;

    return sortMenu;
}

- (NSMenu*)buildViewMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"View")];

    NSMenuItem* compact = [menu addItemWithTitle:LStr(@"Compact View") action:@selector(toggleSmallView:) keyEquivalent:@"t"];
    compact.symbolName = @"arrow.down.forward.and.arrow.up.backward";

    NSMenuItem* pieces = [menu addItemWithTitle:LStr(@"Pieces Bar") action:@selector(togglePiecesBar:) keyEquivalent:@"t"];
    pieces.symbolName = @"puzzlepiece";
    [pieces setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];

    NSMenuItem* avail = [menu addItemWithTitle:LStr(@"Availability") action:@selector(toggleAvailabilityBar:) keyEquivalent:@""];
    avail.symbolName = @"hand.thumbsdown.hand.thumbsup";
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* groups = [menu addItemWithTitle:LStr(@"Use Groups") action:@selector(setSortByGroup:) keyEquivalent:@"g"];
    groups.symbolName = @"pin";

    NSMenuItem* sortContainer = [menu addItemWithTitle:LStr(@"Sort Torrents By") action:nil keyEquivalent:@""];
    sortContainer.symbolName = @"arrow.up.arrow.down";

    NSMenu* sortMenu = [self buildSortMenu];
    [sortContainer setSubmenu:sortMenu];

    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* inspector = [menu addItemWithTitle:LStr(@"Show Inspector") action:@selector(showInfo:) keyEquivalent:@"i"];
    inspector.symbolName = @"info.circle";
    [menu addItem:[NSMenuItem separatorItem]];

    [menu addItemWithTitle:LStr(@"Hide Status Bar") action:@selector(toggleStatusBar:) keyEquivalent:@"b"];
    [menu addItemWithTitle:LStr(@"Hide Filter Bar") action:@selector(toggleFilterBar:) keyEquivalent:@"f"];
    [menu addItem:[NSMenuItem separatorItem]];

    [[menu addItemWithTitle:LStr(@"Hide Toolbar") action:@selector(toggleToolbarShown:) keyEquivalent:@""] setTarget:self.controller];
    [menu addItemWithTitle:LStr(@"Customize Toolbar…") action:@selector(runToolbarCustomizationPalette:) keyEquivalent:@""];

    return menu;
}

- (NSMenu*)buildTorrentMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"Torrent")];
    unichar backspace = NSBackspaceCharacter;
    NSMenuItem* altSpeed = [menu addItemWithTitle:LStr(@"Alternative Speed Limits") action:@selector(toggleSpeedLimit:)
                                    keyEquivalent:@"l"];
    altSpeed.symbolName = @"tortoise";
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* pause = [menu addItemWithTitle:LStr(@"Pause") action:@selector(stopSelectedTorrents:) keyEquivalent:@"."];
    pause.symbolName = @"pause";
    NSMenuItem* start = [menu addItemWithTitle:LStr(@"Start") action:@selector(resumeSelectedTorrents:) keyEquivalent:@"/"];
    start.symbolName = @"arrow.clockwise";
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* remove = [menu addItemWithTitle:LStr(@"Remove") action:@selector(removeNoDelete:)
                                  keyEquivalent:[NSString stringWithCharacters:&backspace length:1]];
    remove.symbolName = @"trash";
    NSMenuItem* delFiles = [menu addItemWithTitle:LStr(@"Delete Files and Remove") action:@selector(removeDeleteData:)
                                    keyEquivalent:[NSString stringWithCharacters:&backspace length:1]];
    delFiles.symbolName = @"document.on.trash";
    [delFiles setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    NSMenuItem* clearComp = [menu addItemWithTitle:LStr(@"Remove All Completed From List") action:@selector(clearCompleted:)
                                     keyEquivalent:@""];
    clearComp.symbolName = @"trash.circle.fill";
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* reveal = [menu addItemWithTitle:LStr(@"Show in Finder") action:@selector(revealFile:) keyEquivalent:@"r"];
    reveal.symbolName = @"finder";
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* pauseAll = [menu addItemWithTitle:LStr(@"Pause All") action:@selector(stopAllTorrents:) keyEquivalent:@"."];
    pauseAll.symbolName = @"pause.circle.fill";
    [pauseAll setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    NSMenuItem* startAll = [menu addItemWithTitle:LStr(@"Start All") action:@selector(resumeAllTorrents:) keyEquivalent:@"/"];
    startAll.symbolName = @"arrow.clockwise.circle.fill";
    [startAll setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* startNow = [menu addItemWithTitle:LStr(@"Start Now") action:@selector(resumeSelectedTorrentsNoWait:)
                                    keyEquivalent:@""];
    startNow.symbolName = @"arrow.clockwise.circle";
    NSMenuItem* startAllQ = [menu addItemWithTitle:LStr(@"Start All Queued Now") action:@selector(resumeWaitingTorrents:)
                                     keyEquivalent:@""];
    startAllQ.symbolName = @"stopwatch";
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenu* groupSubmenu = [NSMenu makeGroupsSubmenu];
    groupSubmenu.delegate = (id<NSMenuDelegate>)self.controller;
    NSMenuItem* groupItem = [menu addItemWithTitle:LStr(@"Group") action:nil keyEquivalent:@""];
    groupItem.submenu = groupSubmenu;
    groupItem.symbolName = @"pin";
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* askTracker = [menu addItemWithTitle:LStr(@"Ask Tracker for More Peers") action:@selector(announceSelectedTorrents:)
                                      keyEquivalent:@""];
    askTracker.symbolName = @"megaphone";
    NSMenuItem* verify = [menu addItemWithTitle:LStr(@"Verify Local Data") action:@selector(verifySelectedTorrents:)
                                  keyEquivalent:@""];
    verify.symbolName = @"bandage";
    return menu;
}
- (NSMenu*)buildWindowMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"Window")];
    [NSApp setWindowsMenu:menu];
    [menu addItemWithTitle:LStr(@"Minimize") action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [menu addItemWithTitle:LStr(@"Zoom") action:@selector(performZoom:) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* nextInfoTab = [menu addItemWithTitle:LStr(@"Next Inspector Tab") action:@selector(setInfoTab:)
                                       keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSRightArrowFunctionKey]];
    [nextInfoTab setMenuItemRole:TRMenuItemRoleNextInfo];
    NSMenuItem* prevInfoTab = [menu addItemWithTitle:LStr(@"Previous Inspector Tab") action:@selector(setInfoTab:)
                                       keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey]];
    [prevInfoTab setMenuItemRole:TRMenuItemRolePrevInfo];

    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* nextFilter = [menu addItemWithTitle:LStr(@"Next Filter Button") action:@selector(switchFilter:)
                                      keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSRightArrowFunctionKey]];
    nextFilter.target = self.controller;
    [nextFilter setMenuItemRole:TRMenuItemRoleNextFilter];
    [nextFilter setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];

    NSMenuItem* prevFilter = [menu addItemWithTitle:LStr(@"Previous Filter Button") action:@selector(switchFilter:)
                                      keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey]];
    prevFilter.target = self.controller;

    [prevFilter setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* winRetrans = [menu addItemWithTitle:LStr(@"Retransmission") action:@selector(showMainWindow:) keyEquivalent:@"1"];
    winRetrans.symbolName = @"house";
    NSMenuItem* winStats = [menu addItemWithTitle:LStr(@"Statistics") action:@selector(showStatsWindow:) keyEquivalent:@"2"];
    winStats.symbolName = @"chart.bar";
    NSMenuItem* winLog = [menu addItemWithTitle:LStr(@"Message Log") action:@selector(showMessageWindow:) keyEquivalent:@"3"];
    winLog.symbolName = @"text.bubble";
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Bring All to Front") action:@selector(arrangeInFront:) keyEquivalent:@""];
    return menu;
}

- (NSMenu*)buildHelpMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"Help")];
    [menu addItemWithTitle:LStr(@"Retransmission Help") action:@selector(showHelp:) keyEquivalent:@"?"];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* home = [menu addItemWithTitle:LStr(@"Retransmission Homepage") action:@selector(linkHomepage:) keyEquivalent:@""];
    home.symbolName = @"globe";
    NSMenuItem* forum = [menu addItemWithTitle:LStr(@"Forums") action:@selector(linkForums:) keyEquivalent:@""];
    forum.symbolName = @"bubble.left.and.bubble.right";
    NSMenuItem* github = [menu addItemWithTitle:LStr(@"Support & Development") action:@selector(linkGitHub:) keyEquivalent:@""];
    github.symbolName = @"keyboard";
    return menu;
}

#pragma clang diagnostic pop

@end
