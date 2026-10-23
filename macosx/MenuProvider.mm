// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "MenuProvider.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"AppMenu", @"")

@implementation MenuProvider

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
    [[menu addItemWithTitle:LStr(@"Donate") action:@selector(linkDonate:)
              keyEquivalent:@""] setImage:[NSImage imageWithSystemSymbolName:@"heart" accessibilityDescription:nil]];

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
    create.image = [NSImage imageWithSystemSymbolName:@"doc.badge.plus" accessibilityDescription:nil];

    NSMenuItem* open = [menu addItemWithTitle:LStr(@"Open…") action:@selector(openShowSheet:) keyEquivalent:@"o"];
    open.image = [NSImage imageWithSystemSymbolName:@"folder" accessibilityDescription:nil];

    NSMenuItem* openOpt = [menu addItemWithTitle:LStr(@"Open With Options Window…") action:@selector(openShowSheet:)
                                   keyEquivalent:@"o"];
    openOpt.image = [NSImage imageWithSystemSymbolName:@"folder.badge.gear" accessibilityDescription:nil];
    [openOpt setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];

    NSMenuItem* openURL = [menu addItemWithTitle:LStr(@"Open URL…") action:@selector(openURLShowSheet:) keyEquivalent:@"u"];
    openURL.image = [NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil];

    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Close Window") action:@selector(performClose:) keyEquivalent:@"w"];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* ql = [menu addItemWithTitle:LStr(@"Quick Look") action:@selector(toggleQuickLook:) keyEquivalent:@"y"];
    ql.image = [NSImage imageWithSystemSymbolName:@"eye" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* setLoc = [menu addItemWithTitle:LStr(@"Set Location…") action:@selector(moveDataFilesSelected:) keyEquivalent:@""];
    setLoc.image = [NSImage imageWithSystemSymbolName:@"arrow.forward.folder" accessibilityDescription:nil];

    NSMenuItem* rename = [menu addItemWithTitle:LStr(@"Rename…") action:@selector(renameSelected:) keyEquivalent:@""];
    rename.image = [NSImage imageWithSystemSymbolName:@"pencil" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* saveAs = [menu addItemWithTitle:LStr(@"Save Torrent File As…") action:@selector(copyTorrentFiles:)
                                  keyEquivalent:@"s"];
    saveAs.image = [NSImage imageWithSystemSymbolName:@"arrow.down.doc" accessibilityDescription:nil];

    NSMenu* share = [[NSMenu alloc] initWithTitle:LStr(@"Share Torrent File")];
    [[menu addItemWithTitle:LStr(@"Share Torrent File") action:nil keyEquivalent:@""] setSubmenu:share];
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

- (NSMenu*)buildViewMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"View")];

    NSMenuItem* compact = [menu addItemWithTitle:LStr(@"Compact View") action:@selector(toggleSmallView:) keyEquivalent:@"t"];
    compact.image = [NSImage imageWithSystemSymbolName:@"arrow.down.forward.and.arrow.up.backward" accessibilityDescription:nil];

    NSMenuItem* pieces = [menu addItemWithTitle:LStr(@"Pieces Bar") action:@selector(togglePiecesBar:) keyEquivalent:@"t"];
    pieces.image = [NSImage imageWithSystemSymbolName:@"puzzlepiece" accessibilityDescription:nil];
    [pieces setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];

    NSMenuItem* avail = [menu addItemWithTitle:LStr(@"Availability") action:@selector(toggleAvailabilityBar:) keyEquivalent:@""];
    avail.image = [NSImage imageWithSystemSymbolName:@"hand.thumbsdown.hand.thumbsup" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* groups = [menu addItemWithTitle:LStr(@"Use Groups") action:@selector(setSortByGroup:) keyEquivalent:@"g"];
    groups.image = [NSImage imageWithSystemSymbolName:@"pin" accessibilityDescription:nil];

    // Вложенная сортировка
    NSMenuItem* sortContainer = [menu addItemWithTitle:LStr(@"Sort Torrents By") action:nil keyEquivalent:@""];
    sortContainer.image = [NSImage imageWithSystemSymbolName:@"arrow.up.arrow.down" accessibilityDescription:nil];

    NSMenu* sortMenu = [[NSMenu alloc] initWithTitle:LStr(@"Sort Torrents By")];
    [sortMenu addItemWithTitle:LStr(@"Queue Order") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"Activity") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"Date Added") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"Name") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"Progress") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"ETA") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"Size") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"State") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItemWithTitle:LStr(@"Tracker") action:@selector(setSort:) keyEquivalent:@""];
    [sortMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* asc = [sortMenu addItemWithTitle:LStr(@"Ascending") action:@selector(setSortReverse:) keyEquivalent:@""];
    asc.image = [NSImage imageWithSystemSymbolName:@"arrow.up" accessibilityDescription:nil];

    NSMenuItem* desc = [sortMenu addItemWithTitle:LStr(@"Descending") action:@selector(setSortReverse:) keyEquivalent:@""];
    desc.image = [NSImage imageWithSystemSymbolName:@"arrow.down" accessibilityDescription:nil];

    [sortContainer setSubmenu:sortMenu];

    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* inspector = [menu addItemWithTitle:LStr(@"Show Inspector") action:@selector(showInfo:) keyEquivalent:@"i"];
    inspector.image = [NSImage imageWithSystemSymbolName:@"info.circle" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];

    [menu addItemWithTitle:LStr(@"Hide Status Bar") action:@selector(toggleStatusBar:) keyEquivalent:@"b"];
    [menu addItemWithTitle:LStr(@"Hide Filter Bar") action:@selector(toggleFilterBar:) keyEquivalent:@"f"];
    [menu addItem:[NSMenuItem separatorItem]];

    [menu addItemWithTitle:LStr(@"Hide Toolbar") action:@selector(toggleToolbarShown:) keyEquivalent:@""];
    [menu addItemWithTitle:LStr(@"Customize Toolbar…") action:@selector(runToolbarCustomizationPalette:) keyEquivalent:@""];

    return menu;
}

- (NSMenu*)buildTorrentMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"Torrent")];
    unichar backspace = NSBackspaceCharacter;
    NSMenuItem* altSpeed = [menu addItemWithTitle:LStr(@"Alternative Speed Limits") action:@selector(toggleSpeedLimit:)
                                    keyEquivalent:@"l"];
    altSpeed.image = [NSImage imageWithSystemSymbolName:@"tortoise" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* pause = [menu addItemWithTitle:LStr(@"Pause") action:@selector(stopSelectedTorrents:) keyEquivalent:@"."];
    pause.image = [NSImage imageWithSystemSymbolName:@"pause" accessibilityDescription:nil];
    NSMenuItem* start = [menu addItemWithTitle:LStr(@"Start") action:@selector(resumeSelectedTorrents:) keyEquivalent:@"/"];
    start.image = [NSImage imageWithSystemSymbolName:@"arrow.clockwise" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* remove = [menu addItemWithTitle:LStr(@"Remove") action:@selector(removeNoDelete:)
                                  keyEquivalent:[NSString stringWithCharacters:&backspace length:1]];
    remove.image = [NSImage imageWithSystemSymbolName:@"trash" accessibilityDescription:nil];
    NSMenuItem* delFiles = [menu addItemWithTitle:LStr(@"Delete Files and Remove") action:@selector(removeDeleteData:)
                                    keyEquivalent:[NSString stringWithCharacters:&backspace length:1]];
    delFiles.image = [NSImage imageWithSystemSymbolName:@"document.on.trash" accessibilityDescription:nil];
    [delFiles setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    NSMenuItem* clearComp = [menu addItemWithTitle:LStr(@"Remove All Completed From List") action:@selector(clearCompleted:)
                                     keyEquivalent:@""];
    clearComp.image = [NSImage imageWithSystemSymbolName:@"trash.circle.fill" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* reveal = [menu addItemWithTitle:LStr(@"Show in Finder") action:@selector(revealFile:) keyEquivalent:@"r"];
    reveal.image = [NSImage imageWithSystemSymbolName:@"finder" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* pauseAll = [menu addItemWithTitle:LStr(@"Pause All") action:@selector(stopAllTorrents:) keyEquivalent:@"."];
    pauseAll.image = [NSImage imageWithSystemSymbolName:@"pause.circle.fill" accessibilityDescription:nil];
    [pauseAll setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    NSMenuItem* startAll = [menu addItemWithTitle:LStr(@"Start All") action:@selector(resumeAllTorrents:) keyEquivalent:@"/"];
    startAll.image = [NSImage imageWithSystemSymbolName:@"arrow.clockwise.circle.fill" accessibilityDescription:nil];
    [startAll setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* startNow = [menu addItemWithTitle:LStr(@"Start Now") action:@selector(resumeSelectedTorrentsNoWait:)
                                    keyEquivalent:@""];
    startNow.image = [NSImage imageWithSystemSymbolName:@"arrow.clockwise.circle" accessibilityDescription:nil];
    NSMenuItem* startAllQ = [menu addItemWithTitle:LStr(@"Start All Queued Now") action:@selector(resumeWaitingTorrents:)
                                     keyEquivalent:@""];
    startAllQ.image = [NSImage imageWithSystemSymbolName:@"stopwatch" accessibilityDescription:nil];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenu* groups = [[NSMenu alloc] initWithTitle:LStr(@"Group")];
    [[menu addItemWithTitle:LStr(@"Group") action:nil keyEquivalent:@""] setSubmenu:groups];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* askTracker = [menu addItemWithTitle:LStr(@"Ask Tracker for More Peers") action:@selector(announceSelectedTorrents:)
                                      keyEquivalent:@""];
    askTracker.image = [NSImage imageWithSystemSymbolName:@"megaphone" accessibilityDescription:nil];
    NSMenuItem* verify = [menu addItemWithTitle:LStr(@"Verify Local Data") action:@selector(verifySelectedTorrents:)
                                  keyEquivalent:@""];
    verify.image = [NSImage imageWithSystemSymbolName:@"bandage" accessibilityDescription:nil];
    return menu;
}
- (NSMenu*)buildWindowMenu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:LStr(@"Window")];
    [NSApp setWindowsMenu:menu];
    [menu addItemWithTitle:LStr(@"Minimize") action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [menu addItemWithTitle:LStr(@"Zoom") action:@selector(performZoom:) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:LStr(@"Next Inspector Tab") action:@selector(setInfoTab:)
             keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSRightArrowFunctionKey]];
    [menu addItemWithTitle:LStr(@"Previous Inspector Tab") action:@selector(setInfoTab:)
             keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey]];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* nextFilter = [menu addItemWithTitle:LStr(@"Next Filter Button") action:@selector(switchFilter:)
                                      keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSRightArrowFunctionKey]];
    [nextFilter setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    NSMenuItem* prevFilter = [menu addItemWithTitle:LStr(@"Previous Filter Button") action:@selector(switchFilter:)
                                      keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey]];
    [prevFilter setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* winRetrans = [menu addItemWithTitle:LStr(@"Retransmission") action:@selector(showMainWindow:) keyEquivalent:@"1"];
    winRetrans.image = [NSImage imageWithSystemSymbolName:@"house" accessibilityDescription:nil];
    NSMenuItem* winStats = [menu addItemWithTitle:LStr(@"Statistics") action:@selector(showStatsWindow:) keyEquivalent:@"2"];
    winStats.image = [NSImage imageWithSystemSymbolName:@"chart.bar" accessibilityDescription:nil];
    NSMenuItem* winLog = [menu addItemWithTitle:LStr(@"Message Log") action:@selector(showMessageWindow:) keyEquivalent:@"3"];
    winLog.image = [NSImage imageWithSystemSymbolName:@"text.bubble" accessibilityDescription:nil];
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
    home.image = [NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil];
    NSMenuItem* forum = [menu addItemWithTitle:LStr(@"Forums") action:@selector(linkForums:) keyEquivalent:@""];
    forum.image = [NSImage imageWithSystemSymbolName:@"bubble.left.and.bubble.right" accessibilityDescription:nil];
    NSMenuItem* github = [menu addItemWithTitle:LStr(@"Support & Development") action:@selector(linkGitHub:) keyEquivalent:@""];
    github.image = [NSImage imageWithSystemSymbolName:@"keyboard" accessibilityDescription:nil];
    return menu;
}

@end
