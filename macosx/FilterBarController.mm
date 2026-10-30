// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterBarController.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"

FilterType const FilterTypeNone = @"None";
FilterType const FilterTypeActive = @"Active";
FilterType const FilterTypeDownload = @"Download";
FilterType const FilterTypeSeed = @"Seed";
FilterType const FilterTypePause = @"Pause";
FilterType const FilterTypeError = @"Error";

FilterSearchType const FilterSearchTypeName = @"Name";
FilterSearchType const FilterSearchTypeTracker = @"Tracker";

NSInteger const kGroupFilterAllTag = -2;

typedef NS_ENUM(NSInteger, FilterTypeTag) {
    FilterTypeTagName = 401,
    FilterTypeTagTracker = 402,
};

@interface FilterBarController ()

@property(nonatomic, strong) NSButton* fNoFilterButton;
@property(nonatomic, strong) NSButton* fActiveFilterButton;
@property(nonatomic, strong) NSButton* fDownloadFilterButton;
@property(nonatomic, strong) NSButton* fSeedFilterButton;
@property(nonatomic, strong) NSButton* fPauseFilterButton;
@property(nonatomic, strong) NSButton* fErrorFilterButton;

@property(nonatomic, strong) NSSearchField* fSearchField;
@property(nonatomic, strong) NSLayoutConstraint* fSearchFieldMinWidthConstraint;

@property(nonatomic, strong) NSPopUpButton* fGroupsButton;

@end

@implementation FilterBarController

- (instancetype)init
{
    self = [super initWithNibName:nil bundle:nil];
    return self;
}

- (void)loadView
{
    NSView* mainView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 500, 24)];
    mainView.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    self.view = mainView;

    NSBox* topSeparator = [[NSBox alloc] init];
    topSeparator.translatesAutoresizingMaskIntoConstraints = NO;
    topSeparator.boxType = NSBoxSeparator;
    [mainView addSubview:topSeparator];

    self.fGroupsButton = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:YES];
    self.fGroupsButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fGroupsButton.controlSize = NSControlSizeMini;
    self.fGroupsButton.bezelStyle = NSBezelStyleInline;
    self.fGroupsButton.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeMini]];
    self.fGroupsButton.bordered = NO;
    [self.fGroupsButton.cell setArrowPosition:NSPopUpNoArrow];
    [self.fGroupsButton.widthAnchor constraintEqualToConstant:26].active = YES;

    NSMenu* groupsMenu = [[NSMenu alloc] initWithTitle:@"OtherViews"];
    groupsMenu.delegate = self;

    NSMenuItem* hiddenItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    hiddenItem.tag = -999;
    hiddenItem.hidden = YES;
    [groupsMenu addItem:hiddenItem];

    NSMenuItem* allGroupsItem = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"All Groups", "Filter Bar -> group filter menu")
                                                           action:@selector(setGroupFilter:)
                                                    keyEquivalent:@""];
    allGroupsItem.tag = kGroupFilterAllTag;
    allGroupsItem.target = self;
    [groupsMenu addItem:allGroupsItem];

    [groupsMenu addItem:[NSMenuItem separatorItem]];
    self.fGroupsButton.menu = groupsMenu;
    [mainView addSubview:self.fGroupsButton];

    NSBox* verticalSeparator = [[NSBox alloc] init];
    verticalSeparator.translatesAutoresizingMaskIntoConstraints = NO;
    verticalSeparator.boxType = NSBoxSeparator;
    [mainView addSubview:verticalSeparator];

    self.fNoFilterButton = [[NSButton alloc] init];
    self.fActiveFilterButton = [[NSButton alloc] init];
    self.fDownloadFilterButton = [[NSButton alloc] init];
    self.fSeedFilterButton = [[NSButton alloc] init];
    self.fPauseFilterButton = [[NSButton alloc] init];
    self.fErrorFilterButton = [[NSButton alloc] init];

    NSArray<NSButton*>* buttons = @[
        self.fNoFilterButton,
        self.fActiveFilterButton,
        self.fDownloadFilterButton,
        self.fSeedFilterButton,
        self.fPauseFilterButton,
        self.fErrorFilterButton
    ];

    NSArray<NSString*>* titles = @[
        NSLocalizedString(@"All", "Filter Bar -> filter button"),
        NSLocalizedString(@"Active", "Filter Bar -> filter button"),
        NSLocalizedString(@"Downloading", "Filter Bar -> filter button"),
        NSLocalizedString(@"Seeding", "Filter Bar -> filter button"),
        NSLocalizedString(@"Paused", "Filter Bar -> filter button"),
        NSLocalizedString(@"Error", "Filter Bar -> filter button")
    ];

    for (NSUInteger i = 0; i < buttons.count; i++) {
        NSButton* button = buttons[i];
        button.translatesAutoresizingMaskIntoConstraints = NO;
        button.showsBorderOnlyWhileMouseInside = YES;
        button.buttonType = NSButtonTypePushOnPushOff;
        button.bezelStyle = NSBezelStyleRecessed;
        button.controlSize = NSControlSizeMini;
        button.font = [NSFont boldSystemFontOfSize:[NSFont smallSystemFontSize]];
        button.title = titles[i];
        button.target = self;
        button.action = @selector(setFilter:);
        [button setContentCompressionResistancePriority:NSLayoutPriorityDefaultHigh
                                         forOrientation:NSLayoutConstraintOrientationHorizontal];
    }

    NSStackView* stackView = [NSStackView stackViewWithViews:buttons];
    stackView.translatesAutoresizingMaskIntoConstraints = NO;
    stackView.distribution = NSStackViewDistributionFill;
    stackView.spacing = 1;
    stackView.detachesHiddenViews = NO;
    [mainView addSubview:stackView];

    self.fSearchField = [[NSSearchField alloc] init];
    self.fSearchField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fSearchField.controlSize = NSControlSizeMini;
    self.fSearchField.focusRingType = NSFocusRingTypeNone;

    self.fSearchField.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeMini]];

    self.fSearchField.delegate = self;
    self.fSearchField.action = @selector(setSearchText:);
    self.fSearchField.target = self;

    NSMenu* searchMenu = [[NSMenu alloc] initWithTitle:@"SearchFilterMenu"];
    NSMenuItem* nameItem = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Name", "Filter Bar -> filter menu")
                                                      action:@selector(setSearchType:)
                                               keyEquivalent:@""];
    nameItem.tag = FilterTypeTagName;
    nameItem.target = self;
    NSMenuItem* trackerItem = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Tracker", "Filter Bar -> filter menu")
                                                         action:@selector(setSearchType:)
                                                  keyEquivalent:@""];
    trackerItem.tag = FilterTypeTagTracker;
    trackerItem.target = self;
    [searchMenu addItem:nameItem];
    [searchMenu addItem:trackerItem];
    self.fSearchField.searchMenuTemplate = searchMenu;
    [mainView addSubview:self.fSearchField];

    [self.fSearchField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                                forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.fSearchFieldMinWidthConstraint = [self.fSearchField.widthAnchor constraintGreaterThanOrEqualToConstant:48];
    NSLayoutConstraint* searchFieldPreferredWidth = [self.fSearchField.widthAnchor constraintEqualToConstant:80];
    searchFieldPreferredWidth.priority = NSLayoutPriorityDefaultLow;
    searchFieldPreferredWidth.active = YES;

    [NSLayoutConstraint activateConstraints:@[
        [topSeparator.topAnchor constraintEqualToAnchor:mainView.topAnchor],
        [topSeparator.leadingAnchor constraintEqualToAnchor:mainView.leadingAnchor],
        [topSeparator.trailingAnchor constraintEqualToAnchor:mainView.trailingAnchor],

        [self.fGroupsButton.leadingAnchor constraintEqualToAnchor:mainView.leadingAnchor constant:10],
        [self.fGroupsButton.centerYAnchor constraintEqualToAnchor:mainView.centerYAnchor],

        [verticalSeparator.leadingAnchor constraintEqualToAnchor:self.fGroupsButton.trailingAnchor constant:8],
        [verticalSeparator.widthAnchor constraintEqualToConstant:1],
        [verticalSeparator.topAnchor constraintEqualToAnchor:mainView.topAnchor constant:4],
        [verticalSeparator.bottomAnchor constraintEqualToAnchor:mainView.bottomAnchor constant:-4],

        [stackView.leadingAnchor constraintEqualToAnchor:verticalSeparator.trailingAnchor constant:10],
        [stackView.centerYAnchor constraintEqualToAnchor:mainView.centerYAnchor],

        [self.fSearchField.trailingAnchor constraintEqualToAnchor:mainView.safeAreaLayoutGuide.trailingAnchor constant:-12],
        [self.fSearchField.centerYAnchor constraintEqualToAnchor:mainView.centerYAnchor],
        [self.fSearchField.heightAnchor constraintEqualToConstant:16],
        self.fSearchFieldMinWidthConstraint,

        [self.fSearchField.leadingAnchor constraintGreaterThanOrEqualToAnchor:stackView.trailingAnchor constant:12]
    ]];
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    NSString* filterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* currentFilterButton;
    if ([filterType isEqualToString:FilterTypeActive]) {
        currentFilterButton = self.fActiveFilterButton;
    } else if ([filterType isEqualToString:FilterTypePause]) {
        currentFilterButton = self.fPauseFilterButton;
    } else if ([filterType isEqualToString:FilterTypeSeed]) {
        currentFilterButton = self.fSeedFilterButton;
    } else if ([filterType isEqualToString:FilterTypeDownload]) {
        currentFilterButton = self.fDownloadFilterButton;
    } else if ([filterType isEqualToString:FilterTypeError]) {
        currentFilterButton = self.fErrorFilterButton;
    } else {
        //safety
        if (![filterType isEqualToString:FilterTypeNone]) {
            [NSUserDefaults.standardUserDefaults setObject:FilterTypeNone forKey:@"Filter"];
        }
        currentFilterButton = self.fNoFilterButton;
    }
    currentFilterButton.state = NSControlStateValueOn;

    //set filter search type
    NSString* filterSearchType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

    NSMenu* filterSearchMenu = self.fSearchField.searchMenuTemplate;
    NSString* filterSearchTypeTitle;
    if ([filterSearchType isEqualToString:FilterSearchTypeTracker]) {
        filterSearchTypeTitle = [filterSearchMenu itemWithTag:FilterTypeTagTracker].title;
    } else {
        //safety
        if (![filterType isEqualToString:FilterSearchTypeName]) {
            [NSUserDefaults.standardUserDefaults setObject:FilterSearchTypeName forKey:@"FilterSearchType"];
        }
        filterSearchTypeTitle = [filterSearchMenu itemWithTag:FilterTypeTagName].title;
    }
    self.fSearchField.placeholderString = filterSearchTypeTitle;

    NSString* searchString;
    if ((searchString = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchString"])) {
        self.fSearchField.stringValue = searchString;
    }

    [self updateGroupsButton];

    // update when groups change
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateGroups:) name:@"UpdateGroups" object:nil];
}

- (void)setFilter:(id)sender
{
    NSString* oldFilterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* prevFilterButton;
    if ([oldFilterType isEqualToString:FilterTypePause]) {
        prevFilterButton = self.fPauseFilterButton;
    } else if ([oldFilterType isEqualToString:FilterTypeActive]) {
        prevFilterButton = self.fActiveFilterButton;
    } else if ([oldFilterType isEqualToString:FilterTypeSeed]) {
        prevFilterButton = self.fSeedFilterButton;
    } else if ([oldFilterType isEqualToString:FilterTypeDownload]) {
        prevFilterButton = self.fDownloadFilterButton;
    } else if ([oldFilterType isEqualToString:FilterTypeError]) {
        prevFilterButton = self.fErrorFilterButton;
    } else {
        prevFilterButton = self.fNoFilterButton;
    }

    if (sender != prevFilterButton) {
        prevFilterButton.state = NSControlStateValueOff;
        [sender setState:NSControlStateValueOn];

        FilterType filterType;
        if (sender == self.fActiveFilterButton) {
            filterType = FilterTypeActive;
        } else if (sender == self.fDownloadFilterButton) {
            filterType = FilterTypeDownload;
        } else if (sender == self.fPauseFilterButton) {
            filterType = FilterTypePause;
        } else if (sender == self.fSeedFilterButton) {
            filterType = FilterTypeSeed;
        } else if (sender == self.fErrorFilterButton) {
            filterType = FilterTypeError;
        } else {
            filterType = FilterTypeNone;
        }

        [NSUserDefaults.standardUserDefaults setObject:filterType forKey:@"Filter"];
    } else {
        [sender setState:NSControlStateValueOn];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)switchFilter:(BOOL)right
{
    NSString* filterType = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];

    NSButton* button;
    if ([filterType isEqualToString:FilterTypeNone]) {
        button = right ? self.fActiveFilterButton : self.fErrorFilterButton;
    } else if ([filterType isEqualToString:FilterTypeActive]) {
        button = right ? self.fDownloadFilterButton : self.fNoFilterButton;
    } else if ([filterType isEqualToString:FilterTypeDownload]) {
        button = right ? self.fSeedFilterButton : self.fActiveFilterButton;
    } else if ([filterType isEqualToString:FilterTypeSeed]) {
        button = right ? self.fPauseFilterButton : self.fDownloadFilterButton;
    } else if ([filterType isEqualToString:FilterTypePause]) {
        button = right ? self.fErrorFilterButton : self.fSeedFilterButton;
    } else if ([filterType isEqualToString:FilterTypeError]) {
        button = right ? self.fNoFilterButton : self.fPauseFilterButton;
    } else {
        button = self.fNoFilterButton;
    }

    [self setFilter:button];
}

- (void)setSearchText:(id)sender
{
    [NSUserDefaults.standardUserDefaults setObject:self.fSearchField.stringValue forKey:@"FilterSearchString"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)focusSearchField
{
    [self.view.window makeFirstResponder:self.fSearchField];
}

- (BOOL)isFocused
{
    NSTextView* textView = (NSTextView*)self.fSearchField.window.firstResponder;
    return [self.fSearchField.window.firstResponder isKindOfClass:NSTextView.class] &&
        [self.fSearchField.window fieldEditor:NO forObject:nil] != nil && [self.fSearchField isEqualTo:textView.delegate];
}

- (void)searchFieldDidStartSearching:(NSSearchField*)sender
{
    [self.fSearchFieldMinWidthConstraint animator].constant = 95;
}

- (void)searchFieldDidEndSearching:(NSSearchField*)sender
{
    [self.fSearchFieldMinWidthConstraint animator].constant = 48;
}

- (void)setSearchType:(id)sender
{
    NSString* oldFilterType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

    NSInteger prevTag, currentTag = [sender tag];
    if ([oldFilterType isEqualToString:FilterSearchTypeTracker]) {
        prevTag = FilterTypeTagTracker;
    } else {
        prevTag = FilterTypeTagName;
    }

    if (currentTag != prevTag) {
        FilterSearchType filterType;
        if (currentTag == FilterTypeTagTracker) {
            filterType = FilterSearchTypeTracker;
        } else {
            filterType = FilterSearchTypeName;
        }

        [NSUserDefaults.standardUserDefaults setObject:filterType forKey:@"FilterSearchType"];

        self.fSearchField.placeholderString = [sender title];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)setGroupFilter:(id)sender
{
    [NSUserDefaults.standardUserDefaults setInteger:[sender tag] forKey:@"FilterGroup"];
    [self updateGroupsButton];

    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)reset
{
    [NSUserDefaults.standardUserDefaults setInteger:kGroupFilterAllTag forKey:@"FilterGroup"];

    [self updateGroupsButton];

    [self setFilter:self.fNoFilterButton];

    self.fSearchField.stringValue = @"";
    [self setSearchText:self.fSearchField];
}

- (NSArray<NSString*>*)searchStrings
{
    return [self.fSearchField.stringValue nonEmptyComponentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}

- (void)setCountAll:(NSUInteger)all
             active:(NSUInteger)active
        downloading:(NSUInteger)downloading
            seeding:(NSUInteger)seeding
             paused:(NSUInteger)paused
              error:(NSUInteger)error
{
    NSArray<NSButton*>* buttons = @[
        self.fNoFilterButton,
        self.fActiveFilterButton,
        self.fDownloadFilterButton,
        self.fSeedFilterButton,
        self.fPauseFilterButton,
        self.fErrorFilterButton
    ];
    NSArray<NSNumber*>* counts = @[ @(all), @(active), @(downloading), @(seeding), @(paused), @(error) ];
    for (NSUInteger i = 0; i < buttons.count; i++) {
        NSButton* btn = buttons[i];
        NSUInteger count = [counts[i] unsignedIntegerValue];
        btn.toolTip = (count == 1) ?
            NSLocalizedString(@"1 torrent", "Filter Button -> tool tip") :
            [NSString localizedStringWithFormat:NSLocalizedString(@"%lu torrents", "Filter Bar Button -> tool tip"), count];
    }
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if (menu == self.fGroupsButton.menu) {
        //remove all items except first three
        for (NSInteger i = menu.numberOfItems - 1; i >= 3; i--) {
            [menu removeItemAtIndex:i];
        }

        NSMenu* groupMenu = [GroupsController.groups groupMenuWithTarget:self action:@selector(setGroupFilter:) isSmall:YES];

        NSInteger const groupMenuCount = groupMenu.numberOfItems;
        for (NSInteger i = 0; i < groupMenuCount; i++) {
            NSMenuItem* item = [groupMenu itemAtIndex:0];
            [groupMenu removeItemAtIndex:0];
            [menu addItem:item];
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;

    //check proper filter search item
    if (action == @selector(setSearchType:)) {
        NSString* filterType = [NSUserDefaults.standardUserDefaults stringForKey:@"FilterSearchType"];

        BOOL state;
        if (menuItem.tag == FilterTypeTagTracker) {
            state = [filterType isEqualToString:FilterSearchTypeTracker];
        } else {
            state = [filterType isEqualToString:FilterSearchTypeName];
        }

        menuItem.state = state ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }

    if (action == @selector(setGroupFilter:)) {
        menuItem.state = menuItem.tag == [NSUserDefaults.standardUserDefaults integerForKey:@"FilterGroup"] ? NSControlStateValueOn :
                                                                                                              NSControlStateValueOff;
        return YES;
    }

    return YES;
}

#pragma mark - Private

- (void)updateGroupsButton
{
    NSInteger const groupIndex = [NSUserDefaults.standardUserDefaults integerForKey:@"FilterGroup"];

    NSImage* icon;
    NSString* toolTip;
    if (groupIndex == kGroupFilterAllTag) {
        icon = [NSImage imageNamed:@"PinTemplate"];
        toolTip = NSLocalizedString(@"All Groups", "Groups -> Button");
    } else {
        icon = [GroupsController.groups imageForIndex:groupIndex];
        NSString* groupName = groupIndex != -1 ? [GroupsController.groups nameForIndex:groupIndex] :
                                                 NSLocalizedString(@"None", "Groups -> Button");
        toolTip = [NSLocalizedString(@"Group", "Groups -> Button") stringByAppendingFormat:@": %@", groupName];
    }

    [self.fGroupsButton.menu itemAtIndex:0].image = icon;
    self.fGroupsButton.toolTip = toolTip;
}

- (void)updateGroups:(NSNotification*)notification
{
    [self updateGroupsButton];
}

@end
