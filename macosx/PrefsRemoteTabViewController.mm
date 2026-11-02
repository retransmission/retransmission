// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/macros.h>
#import <libtransmission/transmission.h>
#import <string_view>

#import "PrefsRemoteTabViewController.h"
#import "PrefsUI.h"
#import "BonjourController.h"
#import "NSStringAdditions.h"
#import "ColorTextField.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesRemoteTab", @"")

typedef NS_ENUM(NSUInteger, RPCIPSegment) {
    RPCIPSegmentAdd = 0,
    RPCIPSegmentRemove = 1,
};

static char const* const kRPCKeychainService = TR_PROJ_APPNAME_CAPITALIZED ":Remote";
static char const* const kRPCKeychainName = "Remote";

static NSString* const kWebUIURLFormat = @"http://localhost:%ld/";

@interface PrefsRemoteTabViewController ()<NSTextFieldDelegate, NSTableViewDataSource, NSTableViewDelegate>

@property(nonatomic) NSButton* fRPCEnabledButton;
@property(nonatomic) NSButton* fOpenWebClientButton;

@property(nonatomic) NSButton* fRPCAuthorizeButton;
@property(nonatomic) ColorTextField* fRPCUsernameTitle;
@property(nonatomic) NSTextField* fRPCUsernameField;
@property(nonatomic) ColorTextField* fRPCPasswordTitle;
@property(nonatomic) NSSecureTextField* fRPCPasswordField;
@property(nonatomic) ColorTextField* fRPCPortTitle;
@property(nonatomic) NSTextField* fRPCPortField;

@property(nonatomic) NSButton* fRPCUseWhitelistButton;
@property(nonatomic) NSTableView* fRPCWhitelistTable;
@property(nonatomic) NSScrollView* fRPCWhitelistScrollView;
@property(nonatomic) NSSegmentedControl* fRPCAddRemoveControl;
@property(nonatomic, readonly) NSMutableArray<NSString*>* fRPCWhitelistArray;

@property(nonatomic) NSButton* fRPCWebDiscoveryButton;

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;
@property(nonatomic, copy) NSString* fRPCPassword;
@property(nonatomic, copy) NSString* fInitialTextFieldString;

@end

@implementation PrefsRemoteTabViewController

- (instancetype)initWithHandle:(tr_session*)handle
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _fHandle = handle;
        _fDefaults = NSUserDefaults.standardUserDefaults;

        [self updateRPCPassword];
        _fRPCWhitelistArray = [NSMutableArray arrayWithArray:[self.fDefaults arrayForKey:@"RPCWhitelist"] ?: @[ @"127.0.0.1" ]];
        [self updateRPCWhitelist];
    }
    return self;
}

- (void)loadView
{
    NSView* containerView = [[NSView alloc] init];
    NSView* contentView = [[NSView alloc] init];
    contentView.translatesAutoresizingMaskIntoConstraints = NO;
    [containerView addSubview:contentView];

    [self createUIElements];
    [self setupLayoutInView:contentView];

    NSLayoutConstraint* preferredWidth = [contentView.widthAnchor constraintEqualToConstant:502];
    preferredWidth.priority = NSLayoutPriorityDefaultHigh;

    [NSLayoutConstraint activateConstraints:@[
        [contentView.centerXAnchor constraintEqualToAnchor:containerView.centerXAnchor],
        [contentView.topAnchor constraintEqualToAnchor:containerView.topAnchor constant:20],
        [contentView.bottomAnchor constraintEqualToAnchor:containerView.bottomAnchor constant:-40],
        [contentView.leadingAnchor constraintGreaterThanOrEqualToAnchor:containerView.leadingAnchor constant:20],
        [contentView.trailingAnchor constraintLessThanOrEqualToAnchor:containerView.trailingAnchor constant:-20],
        preferredWidth
    ]];

    [containerView setFrameSize:containerView.fittingSize];
    self.view = containerView;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self setupBindings];

    self.fRPCPasswordField.stringValue = self.fRPCPassword ?: @"";
    [self.fRPCWhitelistTable sizeToFit];

    [self updateSubviewsAvailability];
}

#pragma mark - UI Generation

- (void)createUIElements
{
    self.fRPCEnabledButton = [PrefsUI checkbox:LStr(@"Allow remote access") target:self action:@selector(setRPCEnabled:)];
    self.fOpenWebClientButton = [PrefsUI pushButton:LStr(@"Open Web Client") target:self action:@selector(linkWebUI:)];

    self.fRPCUsernameTitle = [ColorTextField labelWithString:LStr(@"Username:")];
    self.fRPCPasswordTitle = [ColorTextField labelWithString:LStr(@"Password:")];
    self.fRPCPortTitle = [ColorTextField labelWithString:LStr(@"HTTP port:")];

    self.fRPCPortField = [PrefsUI numberInputFieldWithWidth:60 minimum:1 maximum:65535];
    self.fRPCPortField.delegate = self;
    self.fRPCPortField.target = self;
    self.fRPCPortField.action = @selector(setRPCPort:);

    self.fRPCAuthorizeButton = [PrefsUI checkbox:LStr(@"Use authentication") target:self action:@selector(setRPCAuthorize:)];

    self.fRPCUsernameField = [[NSTextField alloc] init];
    self.fRPCUsernameField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fRPCUsernameField.cell.sendsActionOnEndEditing = YES;
    self.fRPCUsernameField.delegate = self;
    self.fRPCUsernameField.target = self;
    self.fRPCUsernameField.action = @selector(setRPCUsername:);
    [self.fRPCUsernameField.widthAnchor constraintEqualToConstant:184].active = YES;

    self.fRPCPasswordField = [[NSSecureTextField alloc] init];
    self.fRPCPasswordField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fRPCPasswordField.cell.sendsActionOnEndEditing = YES;
    self.fRPCPasswordField.delegate = self;
    self.fRPCPasswordField.target = self;
    self.fRPCPasswordField.action = @selector(setRPCPassword:);
    [self.fRPCPasswordField.widthAnchor constraintEqualToConstant:184].active = YES;

    self.fRPCUseWhitelistButton = [PrefsUI checkbox:LStr(@"Only allow these IP addresses:") target:self
                                             action:@selector(setRPCUseWhitelist:)];

    // Whitelist Table and ScrollView setup
    self.fRPCWhitelistTable = [[NSTableView alloc] init];
    self.fRPCWhitelistTable.dataSource = self;
    self.fRPCWhitelistTable.delegate = self;
    self.fRPCWhitelistTable.headerView = nil;
    self.fRPCWhitelistTable.usesAlternatingRowBackgroundColors = YES;

    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"IP"];
    column.width = 250;
    [self.fRPCWhitelistTable addTableColumn:column];

    NSScrollView* tableScrollView = [[NSScrollView alloc] init];
    tableScrollView.translatesAutoresizingMaskIntoConstraints = NO;
    tableScrollView.hasVerticalScroller = YES;
    tableScrollView.hasHorizontalScroller = NO;
    tableScrollView.autohidesScrollers = YES;
    tableScrollView.borderType = NSBezelBorder;
    tableScrollView.documentView = self.fRPCWhitelistTable;
    [tableScrollView.heightAnchor constraintEqualToConstant:73].active = YES;
    [tableScrollView.widthAnchor constraintEqualToConstant:308].active = YES;
    self.fRPCWhitelistScrollView = tableScrollView;

    self.fRPCAddRemoveControl = [[NSSegmentedControl alloc] init];
    self.fRPCAddRemoveControl.translatesAutoresizingMaskIntoConstraints = NO;
    self.fRPCAddRemoveControl.segmentCount = 2;
    self.fRPCAddRemoveControl.segmentStyle = NSSegmentStyleRoundRect;
    self.fRPCAddRemoveControl.trackingMode = NSSegmentSwitchTrackingMomentary;
    [self.fRPCAddRemoveControl setImage:[NSImage imageWithSystemSymbolName:@"plus" accessibilityDescription:nil] forSegment:0];
    [self.fRPCAddRemoveControl setImage:[NSImage imageWithSystemSymbolName:@"minus" accessibilityDescription:nil] forSegment:1];
    [self.fRPCAddRemoveControl setEnabled:NO forSegment:1];
    self.fRPCAddRemoveControl.target = self;
    self.fRPCAddRemoveControl.action = @selector(addRemoveRPCIP:);

    self.fRPCWebDiscoveryButton = [PrefsUI checkbox:LStr(@"Display the web interface with Bonjour") target:self
                                             action:@selector(setRPCWebUIDiscovery:)];
}

#pragma mark - Layout Setup

- (void)setupLayoutInView:(NSView*)parent
{
    NSTextField* remoteHint = [PrefsUI smallLabel:LStr(@"This enables the web interface and allows remote requests")];
    NSTextField* ipHint = [PrefsUI smallLabel:LStr(@"IP addresses may use wildcards, such as 192.168.*.*")];

    NSStackView* portStack = [NSStackView stackViewWithViews:@[ self.fRPCPortTitle, self.fRPCPortField ]];
    portStack.spacing = 8;
    portStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* userStack = [NSStackView stackViewWithViews:@[ self.fRPCUsernameTitle, self.fRPCUsernameField ]];
    userStack.spacing = 8;
    userStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* passStack = [NSStackView stackViewWithViews:@[ self.fRPCPasswordTitle, self.fRPCPasswordField ]];
    passStack.spacing = 8;
    passStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* whitelistControlStack = [NSStackView stackViewWithViews:@[ self.fRPCAddRemoveControl, ipHint ]];
    whitelistControlStack.spacing = 8;
    whitelistControlStack.alignment = NSLayoutAttributeCenterY;

    // Building a clean 2-column tabular list
    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        @[ self.fRPCEnabledButton ],
        @[ [PrefsUI indentedView:remoteHint] ],

        @[ [PrefsUI indentedView:self.fOpenWebClientButton] ],

        @[ [PrefsUI indentedView:self.fRPCAuthorizeButton] ],
        @[ [PrefsUI indentedView:userStack level:2] ],
        @[ [PrefsUI indentedView:passStack level:2] ],

        @[ [PrefsUI indentedView:portStack] ],

        @[ [PrefsUI indentedView:self.fRPCUseWhitelistButton] ],
        @[ [PrefsUI indentedView:self.fRPCWhitelistScrollView] ],
        @[ [PrefsUI indentedView:whitelistControlStack] ],

        @[ [PrefsUI indentedView:self.fRPCWebDiscoveryButton] ]
    ]];

    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 10;
    gridView.columnSpacing = 8;
    gridView.xPlacement = NSGridCellPlacementLeading;
    gridView.yPlacement = NSGridCellPlacementCenter;

    [gridView cellForView:self.fOpenWebClientButton].row.topPadding = 14;
    [gridView cellForView:self.fRPCAuthorizeButton].row.topPadding = 14;
    [gridView cellForView:portStack].row.topPadding = 14;
    [gridView cellForView:self.fRPCUseWhitelistButton].row.topPadding = 14;
    [gridView cellForView:self.fRPCWebDiscoveryButton].row.topPadding = 14;

    [parent addSubview:gridView];
    [NSLayoutConstraint activateConstraints:@[
        [gridView.topAnchor constraintEqualToAnchor:parent.topAnchor],
        [gridView.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor],
        [gridView.leadingAnchor constraintGreaterThanOrEqualToAnchor:parent.leadingAnchor],
        [gridView.trailingAnchor constraintLessThanOrEqualToAnchor:parent.trailingAnchor],
        [gridView.centerXAnchor constraintEqualToAnchor:parent.centerXAnchor]
    ]];
}

#pragma mark - Cocoa Bindings
- (void)setupBindings
{
    NSUserDefaultsController* defaultsController = [NSUserDefaultsController sharedUserDefaultsController];

    [self.fRPCEnabledButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RPC" options:nil];
    [self.fOpenWebClientButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.RPC" options:nil];

    [self.fRPCAuthorizeButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RPCAuthorize" options:nil];
    [self.fRPCAuthorizeButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.RPC" options:nil];

    [self.fRPCPortTitle bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.RPC" options:nil];

    [self.fRPCUsernameField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RPCUsername" options:nil];

    [self.fRPCPortField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RPCPort" options:nil];
    [self.fRPCPortField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.RPC" options:nil];

    [self.fRPCUseWhitelistButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RPCUseWhitelist" options:nil];
    [self.fRPCUseWhitelistButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.RPC" options:nil];

    [self.fRPCWebDiscoveryButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RPCWebDiscovery" options:nil];
    [self.fRPCWebDiscoveryButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.RPC" options:nil];
}

- (void)updateSubviewsAvailability
{
    BOOL const rpcEnabled = [self.fDefaults boolForKey:@"RPC"];
    BOOL const authEnabled = [self.fDefaults boolForKey:@"RPCAuthorize"];
    BOOL const whitelistEnabled = [self.fDefaults boolForKey:@"RPCUseWhitelist"];

    BOOL const fieldsShouldBeEnabled = rpcEnabled && authEnabled;
    BOOL const whitelistShouldBeEnabled = rpcEnabled && whitelistEnabled;

    self.fRPCUsernameTitle.enabled = fieldsShouldBeEnabled;
    self.fRPCUsernameField.enabled = fieldsShouldBeEnabled;
    self.fRPCPasswordTitle.enabled = fieldsShouldBeEnabled;
    self.fRPCPasswordField.enabled = fieldsShouldBeEnabled;

    self.fRPCWhitelistTable.enabled = whitelistShouldBeEnabled;
    self.fRPCAddRemoveControl.enabled = whitelistShouldBeEnabled;
}

#pragma mark - Transmission Logic & Actions
- (void)setRPCEnabled:(id)sender
{
    tr_sessionSetRPCEnabled(self.fHandle, [self.fDefaults boolForKey:@"RPC"]);
    [self setRPCWebUIDiscovery:nil];
    [self updateSubviewsAvailability];
}

- (void)linkWebUI:(id)sender
{
    NSString* urlString = [NSString stringWithFormat:kWebUIURLFormat, [self.fDefaults integerForKey:@"RPCPort"]];
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:urlString]];
}

- (void)setRPCAuthorize:(id)sender
{
    tr_sessionSetRPCPasswordEnabled(self.fHandle, [self.fDefaults boolForKey:@"RPCAuthorize"]);
    [self updateSubviewsAvailability];
}

- (void)setRPCUsername:(id)sender
{
    tr_sessionSetRPCUsername(self.fHandle, [self.fDefaults stringForKey:@"RPCUsername"].UTF8String);
}

- (void)setRPCPassword:(id)sender
{
    self.fRPCPassword = [sender stringValue];
    char const* password = self.fRPCPassword.UTF8String;
    [self setKeychainPassword:password];
    tr_sessionSetRPCPassword(self.fHandle, password);
}

- (void)setRPCPort:(id)sender
{
    int const port = [sender intValue];
    [self.fDefaults setInteger:port forKey:@"RPCPort"];
    tr_sessionSetRPCPort(self.fHandle, port);
    [self setRPCWebUIDiscovery:nil];
}

- (void)setRPCUseWhitelist:(id)sender
{
    tr_sessionSetRPCWhitelistEnabled(self.fHandle, [self.fDefaults boolForKey:@"RPCUseWhitelist"]);
    [self updateSubviewsAvailability];
}

- (void)setRPCWebUIDiscovery:(id)sender
{
    if ([self.fDefaults boolForKey:@"RPC"] && [self.fDefaults boolForKey:@"RPCWebDiscovery"]) {
        [BonjourController.defaultController startWithPort:static_cast<int>([self.fDefaults integerForKey:@"RPCPort"])];
    } else {
        if (BonjourController.defaultControllerExists) {
            [BonjourController.defaultController stop];
        }
    }
}

- (void)addRemoveRPCIP:(id)sender
{
    if (self.fRPCWhitelistTable.editedRow != -1) {
        return;
    }
    if ([sender selectedSegment] == RPCIPSegmentRemove) {
        [self.fRPCWhitelistArray removeObjectsAtIndexes:self.fRPCWhitelistTable.selectedRowIndexes];
        [self.fRPCWhitelistTable deselectAll:self];
        [self.fRPCWhitelistTable reloadData];
        [self.fDefaults setObject:self.fRPCWhitelistArray forKey:@"RPCWhitelist"];
        [self updateRPCWhitelist];
    } else {
        [self.fRPCWhitelistArray addObject:@""];
        [self.fRPCWhitelistTable reloadData];
        NSUInteger const row = self.fRPCWhitelistArray.count - 1;
        [self.fRPCWhitelistTable selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        [self.fRPCWhitelistTable editColumn:0 row:row withEvent:nil select:YES];
    }
}

#pragma mark - TableView DataSource & Delegate
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fRPCWhitelistArray.count;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    return self.fRPCWhitelistArray[row];
}

- (void)tableView:(NSTableView*)tableView
    setObjectValue:(id)object
    forTableColumn:(NSTableColumn*)tableColumn
               row:(NSInteger)row
{
    NSArray* components = [object componentsSeparatedByString:@"."];
    NSMutableArray* newComponents = [NSMutableArray arrayWithCapacity:4];
    BOOL valid = false;
    if (components.count == 4) {
        valid = true;
        for (NSString* component in components) {
            if ([component isEqualToString:@"*"]) {
                [newComponents addObject:component];
            } else {
                int num = component.intValue;
                if (num >= 0 && num < 256) {
                    [newComponents addObject:@(num).stringValue];
                } else {
                    valid = false;
                    break;
                }
            }
        }
    }
    NSString* newIP;
    if (valid) {
        newIP = [newComponents componentsJoinedByString:@"."];
        if ([self.fRPCWhitelistArray containsObject:newIP] && ![self.fRPCWhitelistArray[row] isEqualToString:newIP]) {
            valid = false;
        }
    }
    if (valid) {
        self.fRPCWhitelistArray[row] = newIP;
        [self.fRPCWhitelistArray sortUsingSelector:@selector(compareNumeric:)];
    } else {
        NSBeep();
        if ([self.fRPCWhitelistArray[row] isEqualToString:@""]) {
            [self.fRPCWhitelistArray removeObjectAtIndex:row];
        }
    }
    [self.fRPCWhitelistTable deselectAll:self];
    [self.fRPCWhitelistTable reloadData];
    [self.fDefaults setObject:self.fRPCWhitelistArray forKey:@"RPCWhitelist"];
    [self updateRPCWhitelist];
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self.fRPCAddRemoveControl setEnabled:self.fRPCWhitelistTable.numberOfSelectedRows > 0 forSegment:1];
}

#pragma mark - Keychain & Initialization Private Helpers
- (void)updateRPCPassword
{
    CFTypeRef data{};
    OSStatus result = SecItemCopyMatching((__bridge CFDictionaryRef)rpcKeychainQuery(@{ (NSString*)kSecReturnData : @YES }), &data);
    if (result == errSecItemNotFound) {
        return;
    }
    if (result != noErr) {
        NSLog(@"Problem accessing Keychain: %@", getOSStatusDescription(result));
        return;
    }

    NSData* const passwordData = (__bridge_transfer NSData*)data;
    NSString* const password = [[NSString alloc] initWithData:passwordData encoding:NSUTF8StringEncoding];
    if (!password) {
        NSLog(@"Problem accessing Keychain: password is not valid UTF-8");
        return;
    }

    tr_sessionSetRPCPassword(self.fHandle, std::string_view{ static_cast<char const*>(passwordData.bytes), passwordData.length });

    // NSString's UTF-8 decoding drops a leading byte order mark,
    // which is part of the password.
    auto const droppedBom = [password lengthOfBytesUsingEncoding:NSUTF8StringEncoding] < passwordData.length;
    self.fRPCPassword = droppedBom ? [@"\uFEFF" stringByAppendingString:password] : password;
}

- (void)setKeychainPassword:(char const*)password
{
    OSStatus result = SecItemCopyMatching((__bridge CFDictionaryRef)rpcKeychainQuery(), nil);
    if (result != noErr && result != errSecItemNotFound) {
        NSLog(@"Problem accessing Keychain: %@", getOSStatusDescription(result));
        return;
    }

    size_t passwordLength = strlen(password);
    if (result == noErr) {
        if (passwordLength > 0) // found and needed, so update it
        {
            result = SecItemUpdate((__bridge CFDictionaryRef)rpcKeychainQuery(), (CFDictionaryRef) @{
                (NSString*)kSecValueData : [NSData dataWithBytes:password length:passwordLength],
            });
            if (result != noErr) {
                NSLog(@"Problem updating Keychain item: %@", getOSStatusDescription(result));
            }
        } else // found and not needed, so remove it
        {
            result = SecItemDelete((__bridge CFDictionaryRef)rpcKeychainQuery());
            if (result != noErr) {
                NSLog(@"Problem removing Keychain item: %@", getOSStatusDescription(result));
            }
        }
    } else {
        if (passwordLength > 0) // not found and needed, so add it
        {
            result = SecItemAdd(
                (__bridge CFDictionaryRef)rpcKeychainQuery(@{
                    (NSString*)kSecValueData : [NSData dataWithBytes:password length:passwordLength],
                }),
                nil);
            if (result != noErr) {
                NSLog(@"Problem adding Keychain item: %@", getOSStatusDescription(result));
            }
        }
    }
}

- (void)updateRPCWhitelist
{
    NSString* string = [self.fRPCWhitelistArray componentsJoinedByString:@","];
    tr_sessionSetRPCWhitelist(self.fHandle, string.UTF8String);
}

static NSString* getOSStatusDescription(OSStatus errorCode)
{
    return [NSError errorWithDomain:NSOSStatusErrorDomain code:errorCode userInfo:NULL].description;
}

// The keychain item that holds the remote access password, plus the given attributes.
[[nodiscard]] static NSDictionary* rpcKeychainQuery(NSDictionary* const attributes = @{})
{
    NSMutableDictionary* const query = [attributes mutableCopy];
    query[(NSString*)kSecClass] = (NSString*)kSecClassGenericPassword;
    query[(NSString*)kSecAttrAccount] = @(kRPCKeychainName);
    query[(NSString*)kSecAttrService] = @(kRPCKeychainService);
    return query;
}

+ (BOOL)saveUnguessableRPCPassword
{
    // Deletes any saved item first, so the new password replaces it.
    // macOS refuses to delete an item another app owns, which may also be unreadable here, and then nothing is saved.
    OSStatus result = SecItemDelete((__bridge CFDictionaryRef)rpcKeychainQuery());
    if (result != noErr && result != errSecItemNotFound) {
        NSLog(@"Problem removing Keychain item: %@", getOSStatusDescription(result));
        return NO;
    }

    NSMutableData* const randomBytes = [NSMutableData dataWithLength:18];
    arc4random_buf(randomBytes.mutableBytes, randomBytes.length);
    NSData* const password = [[randomBytes base64EncodedStringWithOptions:0] dataUsingEncoding:NSUTF8StringEncoding];

    result = SecItemAdd((__bridge CFDictionaryRef)rpcKeychainQuery(@{ (NSString*)kSecValueData : password }), nil);
    if (result != noErr) {
        NSLog(@"Problem adding Keychain item: %@", getOSStatusDescription(result));
        return NO;
    }
    return YES;
}

#pragma mark - NSTextFieldDelegate
- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor
{
    self.fInitialTextFieldString = control.stringValue;
    return YES;
}

- (BOOL)control:(NSControl*)control didFailToFormatString:(NSString*)string errorDescription:(NSString*)error
{
    NSBeep();
    if (self.fInitialTextFieldString) {
        control.stringValue = self.fInitialTextFieldString;
        self.fInitialTextFieldString = nil;
    }
    return NO;
}
@end
