// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <libtransmission/log.h>
#include <libtransmission/file.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/types.h>

#import "MessageWindowController.h"
#import "Controller.h"
#import "NSImageAdditions.h"
#import "NSMutableArrayAdditions.h"
#import "NSStringAdditions.h"
#import "MessageLogsViews.h"

typedef NS_ENUM(NSUInteger, LevelButtonLevel) {
    LevelButtonLevelError = 0,
    LevelButtonLevelWarn = 1,
    LevelButtonLevelInfo = 2,
    LevelButtonLevelDebug = 3,
    // we do not support LevelButtonLevelTrace, as it would overwhelm everything (#4233)
};

static NSTimeInterval const kUpdateSeconds = 0.75;

// Maximum number of messages retained in the message window.
static NSUInteger const kMaxQueueLength = 10000U;

@interface MessageWindowController ()<NSWindowRestoration, NSMenuItemValidation, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate>

@property(nonatomic) NSTableView* fMessageTable;

@property(nonatomic) NSPopUpButton* fLevelButton;
@property(nonatomic) NSButton* fSaveButton;
@property(nonatomic) NSButton* fClearButton;
@property(nonatomic) NSSearchField* fFilterField;

@property(nonatomic) NSDateFormatter* fDateFormatter;

@property(nonatomic) NSMutableArray<NSDictionary*>* fMessages;
@property(nonatomic) NSMutableArray<NSDictionary*>* fDisplayedMessages;

@property(nonatomic) NSTimer* fTimer;

@property(nonatomic) NSLock* fLock;

- (void)updateLog;

@end

@implementation MessageWindowController

- (instancetype)init
{
    self = [super initWithWindow:nil];
    if (self) {
        [self setupWindowAndLayout];
    }
    return self;
}

- (void)setupWindowAndLayout
{
    NSPanel* panel = [[NSPanel alloc]
        initWithContentRect:NSMakeRect(320, 502, 611, 328)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:YES];
    panel.identifier = @"MessageWindow";
    panel.title = NSLocalizedString(@"Message Log", "Message window -> title");
    panel.minSize = NSMakeSize(550, 200);
    panel.releasedWhenClosed = NO;
    panel.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
    panel.hidesOnDeactivate = NO;
    panel.tabbingMode = NSWindowTabbingModeDisallowed;

    self.window = panel;
    panel.delegate = self;
    NSView* contentView = panel.contentView;

    NSBox* separator = [[NSBox alloc] initWithFrame:NSZeroRect];
    separator.translatesAutoresizingMaskIntoConstraints = NO;
    separator.boxType = NSBoxSeparator;
    [contentView addSubview:separator];

    self.fLevelButton = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.fLevelButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fLevelButton.bezelStyle = NSBezelStyleRounded;
    [self.fLevelButton addItemsWithTitles:@[
        NSLocalizedString(@"Error", "Message window -> level string"),
        NSLocalizedString(@"Warning", "Message window -> level string"),
        NSLocalizedString(@"Information", "Message window -> level string"),
        NSLocalizedString(@"Debug", "Message window -> level string")
    ]];
    [[self.fLevelButton itemAtIndex:LevelButtonLevelError] setImage:[self.class iconForLevel:TR_LOG_ERROR]];
    [[self.fLevelButton itemAtIndex:LevelButtonLevelWarn] setImage:[self.class iconForLevel:TR_LOG_WARN]];
    [[self.fLevelButton itemAtIndex:LevelButtonLevelInfo] setImage:[self.class iconForLevel:TR_LOG_INFO]];
    [[self.fLevelButton itemAtIndex:LevelButtonLevelDebug] setImage:[self.class iconForLevel:TR_LOG_DEBUG]];
    self.fLevelButton.target = self;
    self.fLevelButton.action = @selector(changeLevel:);
    [contentView addSubview:self.fLevelButton];

    self.fSaveButton = [NSButton buttonWithTitle:[NSLocalizedString(@"Save", "Message window -> save button") stringByAppendingEllipsis]
                                          target:self
                                          action:@selector(writeToFile:)];
    self.fSaveButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fSaveButton.bezelStyle = NSBezelStyleRounded;
    [contentView addSubview:self.fSaveButton];

    self.fClearButton = [NSButton buttonWithTitle:NSLocalizedString(@"Clear", "Message window -> save button") target:self
                                           action:@selector(clearLog:)];
    self.fClearButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fClearButton.bezelStyle = NSBezelStyleRounded;
    [contentView addSubview:self.fClearButton];

    self.fFilterField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    self.fFilterField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fFilterField.placeholderString = NSLocalizedString(@"Filter", "Message window -> filter field");
    self.fFilterField.target = self;
    self.fFilterField.action = @selector(changeFilter:);
    [contentView addSubview:self.fFilterField];

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.hasVerticalScroller = YES;
    scrollView.hasHorizontalScroller = NO;
    scrollView.autohidesScrollers = YES;
    [contentView addSubview:scrollView];

    self.fMessageTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    self.fMessageTable.allowsExpansionToolTips = YES;
    self.fMessageTable.usesAlternatingRowBackgroundColors = YES;
    self.fMessageTable.focusRingType = NSFocusRingTypeNone;
    self.fMessageTable.allowsColumnReordering = NO;
    self.fMessageTable.allowsMultipleSelection = YES;
    self.fMessageTable.autosaveName = @"LogTableView";
    self.fMessageTable.rowHeight = 18;
    self.fMessageTable.usesAutomaticRowHeights = YES;
    self.fMessageTable.style = NSTableViewStyleFullWidth;
    self.fMessageTable.intercellSpacing = NSMakeSize(3, 2);

    NSTableColumn* colLevel = [[NSTableColumn alloc] initWithIdentifier:@"Level"];
    colLevel.title = @"";
    colLevel.width = 14;
    colLevel.minWidth = 14;
    colLevel.maxWidth = 14;
    colLevel.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:@"Level" ascending:YES selector:@selector(compare:)];
    [self.fMessageTable addTableColumn:colLevel];

    NSTableColumn* colDate = [[NSTableColumn alloc] initWithIdentifier:@"Date"];
    colDate.title = NSLocalizedString(@"Time", "Message window -> table column");
    colDate.width = 92;
    colDate.minWidth = 40;
    colDate.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:@"Index" ascending:YES selector:@selector(compare:)];
    [self.fMessageTable addTableColumn:colDate];

    NSTableColumn* colName = [[NSTableColumn alloc] initWithIdentifier:@"Name"];
    colName.title = NSLocalizedString(@"Name", "Message window -> table column");
    colName.width = 100;
    colName.minWidth = 54;
    colName.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:@"Name" ascending:YES
                                                                     selector:@selector(localizedStandardCompare:)];
    [self.fMessageTable addTableColumn:colName];

    NSTableColumn* colMessage = [[NSTableColumn alloc] initWithIdentifier:@"Message"];
    colMessage.title = NSLocalizedString(@"Message", "Message window -> table column");
    colMessage.width = 367;
    colMessage.minWidth = 200;
    colMessage.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
    colMessage.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:@"Message" ascending:YES
                                                                        selector:@selector(localizedStandardCompare:)];
    [self.fMessageTable addTableColumn:colMessage];
    self.fMessageTable.autosaveTableColumns = YES;

    self.fDateFormatter = [[NSDateFormatter alloc] init];
    self.fDateFormatter.dateStyle = NSDateFormatterShortStyle;
    self.fDateFormatter.timeStyle = NSDateFormatterShortStyle;

    scrollView.documentView = self.fMessageTable;
    self.fMessageTable.dataSource = self;
    self.fMessageTable.delegate = self;

    [NSLayoutConstraint activateConstraints:@[
        [scrollView.topAnchor constraintEqualToAnchor:contentView.topAnchor],
        [scrollView.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor],
        [scrollView.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor],
        [scrollView.bottomAnchor constraintEqualToAnchor:separator.topAnchor],

        [separator.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor],
        [separator.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor],
        [separator.bottomAnchor constraintEqualToAnchor:self.fLevelButton.topAnchor constant:-8],

        [self.fLevelButton.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20],
        [self.fLevelButton.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-10],

        [self.fSaveButton.leadingAnchor constraintEqualToAnchor:self.fLevelButton.trailingAnchor constant:8],
        [self.fSaveButton.centerYAnchor constraintEqualToAnchor:self.fLevelButton.centerYAnchor],

        [self.fClearButton.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],
        [self.fClearButton.centerYAnchor constraintEqualToAnchor:self.fLevelButton.centerYAnchor],

        [self.fFilterField.trailingAnchor constraintEqualToAnchor:self.fClearButton.leadingAnchor constant:-8],
        [self.fFilterField.centerYAnchor constraintEqualToAnchor:self.fLevelButton.centerYAnchor],
        [self.fFilterField.widthAnchor constraintEqualToConstant:184],
        [self.fFilterField.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.fSaveButton.trailingAnchor constant:8]
    ]];

    panel.frameAutosaveName = @"MessageWindowFrame";
    [panel setFrameUsingName:@"MessageWindowFrame"];
    panel.restorationClass = [self class];

    switch ([NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"]) {
    case TR_LOG_ERROR:
        [self.fLevelButton selectItemAtIndex:LevelButtonLevelError];
        break;
    case TR_LOG_WARN:
        [self.fLevelButton selectItemAtIndex:LevelButtonLevelWarn];
        break;
    case TR_LOG_INFO:
        [self.fLevelButton selectItemAtIndex:LevelButtonLevelInfo];
        break;
    case TR_LOG_DEBUG:
    case TR_LOG_TRACE:
        [self.fLevelButton selectItemAtIndex:LevelButtonLevelDebug];
        break;
    default:
        [NSUserDefaults.standardUserDefaults setInteger:TR_LOG_ERROR forKey:@"MessageLevel"];
        [self.fLevelButton selectItemAtIndex:LevelButtonLevelError];
    }

    self.fMessages = [[NSMutableArray alloc] init];
    self.fDisplayedMessages = [[NSMutableArray alloc] init];
    self.fLock = [[NSLock alloc] init];
}

- (void)dealloc
{
    [_fTimer invalidate];
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    if (!self.fTimer) {
        __weak __auto_type weakSelf = self;
        self.fTimer = [NSTimer scheduledTimerWithTimeInterval:kUpdateSeconds repeats:YES block:^(NSTimer* _Nonnull) {
            [weakSelf updateLog];
        }];
        [self updateLog];
    }
}

- (void)windowWillClose:(id)sender
{
    [self.fTimer invalidate];
    self.fTimer = nil;
}

+ (void)restoreWindowWithIdentifier:(NSString*)identifier
                              state:(NSCoder*)state
                  completionHandler:(void (^)(NSWindow*, NSError*))completionHandler
{
    NSAssert1([identifier isEqualToString:@"MessageWindow"], @"Trying to restore unexpected identifier %@", identifier);

    NSWindow* window = ((Controller*)NSApp.delegate).messageWindowController.window;
    completionHandler(window, nil);
}

- (void)window:(NSWindow*)window didDecodeRestorableState:(NSCoder*)coder
{
    [self.fTimer invalidate];
    __weak __auto_type weakSelf = self;
    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:kUpdateSeconds repeats:YES block:^(NSTimer* _Nonnull) {
        [weakSelf updateLog];
    }];
    [self updateLog];
}

+ (NSImage*)iconForLevel:(NSInteger)level
{
    NSColor* color;
    switch (level) {
    case TR_LOG_CRITICAL:
    case TR_LOG_ERROR:
        color = NSColor.systemRedColor;
        break;

    case TR_LOG_WARN:
        color = NSColor.systemOrangeColor;
        break;

    case TR_LOG_INFO:
        color = NSColor.systemGreenColor;
        break;

    case TR_LOG_DEBUG:
        color = NSColor.systemTealColor;
        break;

    case TR_LOG_TRACE:
        color = NSColor.systemPurpleColor;
        break;

    default:
        NSAssert1(NO, @"Unknown message log level: %ld", level);
        return nil;
    }

    // cache dictionary
    static NSMutableDictionary<NSColor*, NSImage*>* icons = [NSMutableDictionary dictionary];
    NSImage* icon;
    if ((icon = icons[color])) {
        return icon;
    }
    icon = [NSImage discIconWithColor:color insetFactor:0.5];
    icons[color] = icon;
    return icon;
}

- (void)updateLog
{
    auto const messages = tr_logGetQueue();
    if (messages.empty()) {
        return;
    }

    [self.fLock lock];

    static NSUInteger currentIndex = 0;

    NSScroller* scroller = self.fMessageTable.enclosingScrollView.verticalScroller;
    BOOL const shouldScroll = currentIndex == 0 || scroller.floatValue == 1.0 || scroller.hidden || scroller.knobProportion == 1.0;

    NSInteger const maxLevel = [NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"];
    NSString* filterString = self.fFilterField.stringValue;

    BOOL changed = NO;

    for (auto const& currentMessage : messages) {
        NSString* name = !std::empty(currentMessage.name) ? @(currentMessage.name.c_str()) : NSProcessInfo.processInfo.processName;

        auto const basename = tr_sys_path_basename(currentMessage.file);
        NSString* file = [tr_strv_to_utf8_nsstring(basename) stringByAppendingFormat:@":%ld", currentMessage.line];

        auto const secs_since_1970 = std::chrono::system_clock::to_time_t(currentMessage.when);
        NSDictionary* message = @{
            @"Message" : [NSString convertedStringFromCString:currentMessage.message.c_str()],
            @"Date" : [NSDate dateWithTimeIntervalSince1970:secs_since_1970],
            @"Index" : @(currentIndex++), //more accurate when sorting by date
            @"Level" : @(currentMessage.level),
            @"Name" : name,
            @"File" : file
        };
        [self.fMessages addObject:message];

        if (currentMessage.level <= maxLevel && [self shouldIncludeMessageForFilter:filterString message:message]) {
            [self.fDisplayedMessages addObject:message];
            changed = YES;
        }
    }

    if (self.fMessages.count > kMaxQueueLength) {
        NSUInteger const oldCount = self.fDisplayedMessages.count;

        NSIndexSet* removeIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fMessages.count - kMaxQueueLength)];
        NSArray* itemsToRemove = [self.fMessages objectsAtIndexes:removeIndexes];

        [self.fMessages removeObjectsAtIndexes:removeIndexes];
        [self.fDisplayedMessages removeObjectsInArray:itemsToRemove];

        changed |= oldCount > self.fDisplayedMessages.count;
    }

    if (changed) {
        [self.fDisplayedMessages sortUsingDescriptors:self.fMessageTable.sortDescriptors];

        NSIndexSet* selectedIndexes = self.fMessageTable.selectedRowIndexes;

        [self.fMessageTable reloadData];

        if (selectedIndexes.count > 0) {
            [self.fMessageTable selectRowIndexes:selectedIndexes byExtendingSelection:NO];
        }

        if (shouldScroll) {
            [self.fMessageTable layoutSubtreeIfNeeded];
            [self.fMessageTable scrollRowToVisible:self.fMessageTable.numberOfRows - 1];
        }
    }

    [self.fLock unlock];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fDisplayedMessages.count;
}

- (void)tableView:(NSTableView*)tableView didAddRowView:(NSTableRowView*)rowView forRow:(NSInteger)row
{
    NSDictionary* message = self.fDisplayedMessages[row];
    NSString* tooltip = message[@"File"];
    rowView.toolTip = tooltip;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    NSString* ident = tableColumn.identifier;
    NSDictionary* message = self.fDisplayedMessages[row];

    if ([ident isEqualToString:@"Level"]) {
        MessageLogLevelCellView* cellView = [tableView makeViewWithIdentifier:ident owner:self];
        if (!cellView) {
            cellView = [[MessageLogLevelCellView alloc] initWithFrame:NSZeroRect];
            cellView.identifier = ident;
        }

        NSInteger const level = [message[@"Level"] integerValue];
        [cellView setImage:[self.class iconForLevel:level]];

        return cellView;
    }

    MessageLogTextCellView* cellView = [tableView makeViewWithIdentifier:ident owner:self];
    if (!cellView) {
        cellView = [[MessageLogTextCellView alloc] initWithFrame:NSZeroRect];
        cellView.identifier = ident;
        [cellView configureForMultiline:[ident isEqualToString:@"Message"]];
    }

    NSString* text = @"";

    if ([ident isEqualToString:@"Date"]) {
        text = [self.fDateFormatter stringFromDate:message[@"Date"]];
    } else if ([ident isEqualToString:@"Name"]) {
        text = message[@"Name"];
    } else {
        text = message[@"Message"];
    }

    [cellView setText:text];
    return cellView;
}

- (void)tableView:(NSTableView*)tableView sortDescriptorsDidChange:(NSArray*)oldDescriptors
{
    [self.fDisplayedMessages sortUsingDescriptors:self.fMessageTable.sortDescriptors];
    [self.fMessageTable reloadData];
}

- (void)tableViewColumnDidResize:(NSNotification*)notification
{
    [self.fMessageTable
        noteHeightOfRowsWithIndexesChanged:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fMessageTable.numberOfRows)]];
}

- (CGFloat)tableView:(NSTableView*)tableView sizeToFitWidthOfColumn:(NSInteger)column
{
    NSTableColumn* tableColumn = tableView.tableColumns[column];
    NSRange visibleRows = [tableView rowsInRect:tableView.visibleRect];

    if (visibleRows.length == 0) {
        return tableColumn.minWidth;
    }

    CGFloat maxWidth = tableColumn.minWidth;

    for (NSUInteger row = visibleRows.location; row < NSMaxRange(visibleRows); row++) {
        NSView* view = [tableView viewAtColumn:column row:row makeIfNecessary:YES];
        if (!view) {
            continue;
        }

        NSSize fittingSize = [view fittingSize];

        if (fittingSize.width > maxWidth) {
            maxWidth = fittingSize.width;
        }
    }

    return MIN(maxWidth, tableColumn.maxWidth);
}

- (void)copy:(id)sender
{
    NSIndexSet* indexes = self.fMessageTable.selectedRowIndexes;
    NSMutableArray* messageStrings = [NSMutableArray arrayWithCapacity:indexes.count];

    for (NSDictionary* message in [self.fDisplayedMessages objectsAtIndexes:indexes]) {
        [messageStrings addObject:[self stringForMessage:message]];
    }

    NSString* messageString = [messageStrings componentsJoinedByString:@"\n"];

    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    [pb writeObjects:@[ messageString ]];
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL action = menuItem.action;

    if (action == @selector(copy:)) {
        return self.fMessageTable.numberOfSelectedRows > 0;
    }

    return YES;
}

- (void)changeLevel:(id)sender
{
    NSInteger level;
    switch (self.fLevelButton.indexOfSelectedItem) {
    case LevelButtonLevelError:
        level = TR_LOG_ERROR;
        break;
    case LevelButtonLevelWarn:
        level = TR_LOG_WARN;
        break;
    case LevelButtonLevelInfo:
        level = TR_LOG_INFO;
        break;
    case LevelButtonLevelDebug:
        level = TR_LOG_DEBUG;
        break;
    default:
        NSAssert1(NO, @"Unknown message log level: %ld", [self.fLevelButton indexOfSelectedItem]);
        level = TR_LOG_INFO;
    }

    if ([NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"] == level) {
        return;
    }

    [NSUserDefaults.standardUserDefaults setInteger:level forKey:@"MessageLevel"];

    [self.fLock lock];

    [self updateListForFilter];

    [self.fLock unlock];
}

- (void)changeFilter:(id)sender
{
    [self.fLock lock];

    [self updateListForFilter];

    [self.fLock unlock];
}

- (void)clearLog:(id)sender
{
    [self.fLock lock];

    [self.fMessages removeAllObjects];

    [self.fMessageTable beginUpdates];
    [self.fMessageTable removeRowsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fDisplayedMessages.count)]
                              withAnimation:NSTableViewAnimationSlideLeft];

    [self.fDisplayedMessages removeAllObjects];

    [self.fMessageTable endUpdates];

    [self.fLock unlock];
}

- (void)writeToFile:(id)sender
{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = @[ UTTypePlainText ];
    panel.canSelectHiddenExtension = YES;

    panel.nameFieldStringValue = NSLocalizedString(@"untitled", "Save log panel -> default file name");

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            //make the array sorted by date
            NSSortDescriptor* descriptor = [NSSortDescriptor sortDescriptorWithKey:@"Index" ascending:YES];
            NSArray* descriptors = @[ descriptor ];
            NSArray* sortedMessages = [self.fDisplayedMessages sortedArrayUsingDescriptors:descriptors];

            //create the text to output
            NSMutableArray* messageStrings = [NSMutableArray arrayWithCapacity:sortedMessages.count];
            for (NSDictionary* message in sortedMessages) {
                [messageStrings addObject:[self stringForMessage:message]];
            }

            NSString* fileString = [messageStrings componentsJoinedByString:@"\n"];

            if (![fileString writeToFile:panel.URL.path atomically:YES encoding:NSUTF8StringEncoding error:nil]) {
                NSAlert* alert = [[NSAlert alloc] init];
                [alert addButtonWithTitle:NSLocalizedString(@"OK", "Save log alert panel -> button")];
                alert.messageText = NSLocalizedString(@"Log Could Not Be Saved", "Save log alert panel -> title");
                alert.informativeText = [NSString
                    stringWithFormat:NSLocalizedString(@"There was a problem creating the file \"%@\".", "Save log alert panel -> message"),
                                     panel.URL.path.lastPathComponent];
                alert.alertStyle = NSAlertStyleWarning;

                [alert runModal];
            }
        }
    }];
}

#pragma mark - Private

- (BOOL)shouldIncludeMessageForFilter:(NSString*)filterString message:(NSDictionary*)message
{
    if ([filterString isEqualToString:@""]) {
        return YES;
    }

    NSStringCompareOptions const searchOptions = NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch;
    return [message[@"Name"] rangeOfString:filterString options:searchOptions].location != NSNotFound ||
        [message[@"Message"] rangeOfString:filterString options:searchOptions].location != NSNotFound;
}

- (void)updateListForFilter
{
    NSInteger const level = [NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"];
    NSString* filterString = self.fFilterField.stringValue;

    NSIndexSet* indexes = [self.fMessages
        indexesOfObjectsWithOptions:NSEnumerationConcurrent passingTest:^BOOL(NSDictionary* message, NSUInteger /*idx*/, BOOL* /*stop*/) {
            return [message[@"Level"] integerValue] <= level && [self shouldIncludeMessageForFilter:filterString message:message];
        }];

    NSArray* tempMessages = [[self.fMessages objectsAtIndexes:indexes] sortedArrayUsingDescriptors:self.fMessageTable.sortDescriptors];

    [self.fMessageTable beginUpdates];

    //figure out which rows were added/moved
    NSUInteger currentIndex = 0, totalCount = 0;
    NSMutableArray* itemsToAdd = [NSMutableArray array];
    NSMutableIndexSet* itemsToAddIndexes = [NSMutableIndexSet indexSet];

    for (NSDictionary* message in tempMessages) {
        NSUInteger const previousIndex = [self.fDisplayedMessages
            indexOfObject:message
                  inRange:NSMakeRange(currentIndex, self.fDisplayedMessages.count - currentIndex)];
        if (previousIndex == NSNotFound) {
            [itemsToAdd addObject:message];
            [itemsToAddIndexes addIndex:totalCount];
        } else {
            if (previousIndex != currentIndex) {
                [self.fDisplayedMessages moveObjectAtIndex:previousIndex toIndex:currentIndex];
                [self.fMessageTable moveRowAtIndex:previousIndex toIndex:currentIndex];
            }
            ++currentIndex;
        }

        ++totalCount;
    }

    //remove trailing items - those are the unused
    if (currentIndex < self.fDisplayedMessages.count) {
        NSRange const removeRange = NSMakeRange(currentIndex, self.fDisplayedMessages.count - currentIndex);
        [self.fDisplayedMessages removeObjectsInRange:removeRange];
        [self.fMessageTable removeRowsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:removeRange]
                                  withAnimation:NSTableViewAnimationSlideDown];
    }

    //add new items
    [self.fDisplayedMessages insertObjects:itemsToAdd atIndexes:itemsToAddIndexes];
    [self.fMessageTable insertRowsAtIndexes:itemsToAddIndexes withAnimation:NSTableViewAnimationSlideUp];

    [self.fMessageTable endUpdates];

    NSAssert2([self.fDisplayedMessages isEqualToArray:tempMessages], @"Inconsistency between message arrays! %@ %@", self.fDisplayedMessages, tempMessages);
}

- (NSString*)stringForMessage:(NSDictionary*)message
{
    NSString* levelString;
    NSInteger const level = [message[@"Level"] integerValue];
    switch (level) {
    case TR_LOG_ERROR:
        levelString = NSLocalizedString(@"Error", "Message window -> level");
        break;
    case TR_LOG_WARN:
        levelString = NSLocalizedString(@"Warning", "Message window -> level");
        break;
    case TR_LOG_INFO:
        levelString = NSLocalizedString(@"Information", "Message window -> level");
        break;
    case TR_LOG_DEBUG:
        levelString = NSLocalizedString(@"Debug", "Message window -> level");
        break;
    case TR_LOG_TRACE:
        levelString = NSLocalizedString(@"Trace", "Message window -> level");
        break;
    default:
        NSAssert1(NO, @"Unknown message log level: %ld", level);
        levelString = @"?";
    }

    return [NSString
        stringWithFormat:@"%@ %@ [%@] %@: %@", message[@"Date"], message[@"File"], levelString, message[@"Name"], message[@"Message"], nil];
}

@end
