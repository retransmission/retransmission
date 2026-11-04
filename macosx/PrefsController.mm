// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/string-utils.h>
#include <libtransmission/transmission.h>

#import "PrefsController.h"
#import "Controller.h"
#import "DefaultAppHelper.h"
#import "NSImageAdditions.h"
#import "NSStringAdditions.h"
#import "PrefsGeneralTabViewController.h"
#import "PrefsTransfersTabViewController.h"
#import "PrefsBandwidthTabViewController.h"
#import "PrefsPeersTabViewController.h"
#import "PrefsNetworkTabViewController.h"
#import "PrefsRemoteTabViewController.h"

typedef NSString* ToolbarTab NS_TYPED_EXTENSIBLE_ENUM;

static ToolbarTab const ToolbarTabGeneral = @"TOOLBAR_GENERAL";
static ToolbarTab const ToolbarTabTransfers = @"TOOLBAR_TRANSFERS";
static ToolbarTab const ToolbarTabGroups = @"TOOLBAR_GROUPS";
static ToolbarTab const ToolbarTabBandwidth = @"TOOLBAR_BANDWIDTH";
static ToolbarTab const ToolbarTabPeers = @"TOOLBAR_PEERS";
static ToolbarTab const ToolbarTabNetwork = @"TOOLBAR_NETWORK";
static ToolbarTab const ToolbarTabRemote = @"TOOLBAR_REMOTE";

@interface PrefsController ()<NSWindowRestoration>

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;
@property(nonatomic) BOOL fHasLoaded;

@property(nonatomic) PrefsGeneralTabViewController* generalViewController;
@property(nonatomic) PrefsTransfersTabViewController* transfersViewController;
@property(nonatomic) PrefsBandwidthTabViewController* bandwidthViewController;
@property(nonatomic) PrefsPeersTabViewController* peersViewController;
@property(nonatomic) PrefsNetworkTabViewController* networkViewController;
@property(nonatomic) PrefsRemoteTabViewController* remoteViewController;
@property(nonatomic) IBOutlet NSView* fGroupsView;

@end

@implementation PrefsController

- (instancetype)initWithHandle:(tr_session*)handle
{
    if ((self = [super initWithWindowNibName:@"PrefsWindow"])) {
        _fHandle = handle;

        _fDefaults = NSUserDefaults.standardUserDefaults;

        //save a new random port
        if ([_fDefaults boolForKey:@"RandomPort"]) {
            [_fDefaults setInteger:tr_sessionGetPeerPort(_fHandle) forKey:@"BindPort"];
        }

        _generalViewController = [[PrefsGeneralTabViewController alloc] init];
        _transfersViewController = [[PrefsTransfersTabViewController alloc] initWithHandle:handle];
        _bandwidthViewController = [[PrefsBandwidthTabViewController alloc] initWithHandle:handle];
        _peersViewController = [[PrefsPeersTabViewController alloc] initWithHandle:handle];
        _networkViewController = [[PrefsNetworkTabViewController alloc] initWithHandle:handle];
        _remoteViewController = [[PrefsRemoteTabViewController alloc] initWithHandle:handle];

        [_peersViewController configureEncryptionMode];
    }

    return self;
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    self.fHasLoaded = YES;

    self.window.restorationClass = [self class];

    //disable fullscreen support
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;

    NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"Preferences Toolbar"];
    toolbar.delegate = self;
    toolbar.allowsUserCustomization = NO;
    toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
    toolbar.sizeMode = NSToolbarSizeModeRegular;
    toolbar.selectedItemIdentifier = ToolbarTabGeneral;
    self.window.toolbar = toolbar;

    [self setWindowSize];
    [self.window center];

    [self setPrefView:nil];
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar itemForItemIdentifier:(NSString*)ident willBeInsertedIntoToolbar:(BOOL)flag
{
    NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:ident];

    if ([ident isEqualToString:ToolbarTabGeneral]) {
        item.label = NSLocalizedString(@"General", "Preferences -> toolbar item title");
        item.image = [NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(setPrefView:);
        item.autovalidates = NO;
    } else if ([ident isEqualToString:ToolbarTabTransfers]) {
        item.label = NSLocalizedString(@"Torrents", "Preferences -> toolbar item title");
        item.image = [NSImage imageWithSystemSymbolName:@"arrow.up.arrow.down" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(setPrefView:);
        item.autovalidates = NO;
    } else if ([ident isEqualToString:ToolbarTabGroups]) {
        item.label = NSLocalizedString(@"Groups", "Preferences -> toolbar item title");
        item.image = [NSImage imageWithSystemSymbolName:@"pin" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(setPrefView:);
        item.autovalidates = NO;
    } else if ([ident isEqualToString:ToolbarTabBandwidth]) {
        item.label = NSLocalizedString(@"Speed", "Preferences -> toolbar item title");
        item.image = [NSImage imageWithSystemSymbolName:@"speedometer" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(setPrefView:);
        item.autovalidates = NO;
    } else if ([ident isEqualToString:ToolbarTabPeers]) {
        item.label = NSLocalizedString(@"Peers", "Preferences -> toolbar item title");
        item.image = [NSImage imageWithSystemSymbolName:@"person.2" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(setPrefView:);
        item.autovalidates = NO;
    } else if ([ident isEqualToString:ToolbarTabNetwork]) {
        item.label = NSLocalizedString(@"Network", "Preferences -> toolbar item title");
        item.image = [NSImage imageWithSystemSymbolName:@"network" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(setPrefView:);
        item.autovalidates = NO;
    } else if ([ident isEqualToString:ToolbarTabRemote]) {
        item.label = NSLocalizedString(@"Remote", "Preferences -> toolbar item title");
        item.image = [NSImage imageWithSystemSymbolName:@"antenna.radiowaves.left.and.right" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(setPrefView:);
        item.autovalidates = NO;
    } else {
        return nil;
    }

    return item;
}

- (NSArray*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar
{
    return @[
        ToolbarTabGeneral,
        ToolbarTabTransfers,
        ToolbarTabGroups,
        ToolbarTabBandwidth,
        ToolbarTabPeers,
        ToolbarTabNetwork,
        ToolbarTabRemote
    ];
}

- (NSArray*)toolbarSelectableItemIdentifiers:(NSToolbar*)toolbar
{
    return [self toolbarAllowedItemIdentifiers:toolbar];
}

- (NSArray*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar
{
    return [self toolbarAllowedItemIdentifiers:toolbar];
}

+ (void)restoreWindowWithIdentifier:(NSString*)identifier
                              state:(NSCoder*)state
                  completionHandler:(void (^)(NSWindow*, NSError*))completionHandler
{
    NSWindow* window = ((Controller*)NSApp.delegate).prefsController.window;
    completionHandler(window, nil);
}

- (void)setWindowSize
{
    //set window width with localised value
    NSRect windowRect = self.window.frame;
    NSString* sizeString = NSLocalizedString(@"PrefWindowSize", nil);
    if ([sizeString isEqualToString:@"PrefWindowSize"]) {
        sizeString = @"640";
    }
    windowRect.size.width = [sizeString floatValue];
    [self.window setFrame:windowRect display:YES animate:NO];
}

+ (int)dateToTimeSum:(NSDate*)date
{
    NSCalendar* calendar = NSCalendar.currentCalendar;
    NSDateComponents* components = [calendar components:NSCalendarUnitHour | NSCalendarUnitMinute fromDate:date];
    return static_cast<int>(components.hour * 60 + components.minute);
}

+ (NSDate*)timeSumToDate:(NSInteger)sum
{
    NSDateComponents* comps = [[NSDateComponents alloc] init];
    comps.hour = sum / 60;
    comps.minute = sum % 60;

    return [NSCalendar.currentCalendar dateFromComponents:comps];
}

- (void)rpcUpdatePrefs
{
    //encryption
    tr_encryption_mode const encryptionMode = tr_sessionGetEncryption(self.fHandle);
    [self.fDefaults setBool:encryptionMode != TR_CLEAR_PREFERRED forKey:@"EncryptionPrefer"];
    [self.fDefaults setBool:encryptionMode == TR_ENCRYPTION_REQUIRED forKey:@"EncryptionRequire"];

    //download directory
    NSString* downloadLocation = tr_strv_to_utf8_nsstring(tr_sessionGetDownloadDir(self.fHandle)).stringByStandardizingPath;
    [self.fDefaults setObject:downloadLocation forKey:@"DownloadFolder"];

    NSString* incompleteLocation = tr_strv_to_utf8_nsstring(tr_sessionGetIncompleteDir(self.fHandle)).stringByStandardizingPath;
    [self.fDefaults setObject:incompleteLocation forKey:@"IncompleteDownloadFolder"];

    BOOL const useIncomplete = tr_sessionIsIncompleteDirEnabled(self.fHandle);
    [self.fDefaults setBool:useIncomplete forKey:@"UseIncompleteDownloadFolder"];

    BOOL const usePartialFileRenaming = tr_sessionIsIncompleteFileNamingEnabled(self.fHandle);
    [self.fDefaults setBool:usePartialFileRenaming forKey:@"RenamePartialFiles"];

    //utp
    BOOL const utp = tr_sessionIsUTPEnabled(self.fHandle);
    [self.fDefaults setBool:utp forKey:@"UTPGlobal"];

    //peers
    uint16_t const peersTotal = tr_sessionGetPeerLimit(self.fHandle);
    [self.fDefaults setInteger:peersTotal forKey:@"PeersTotal"];

    uint16_t const peersTorrent = tr_sessionGetPeerLimitPerTorrent(self.fHandle);
    [self.fDefaults setInteger:peersTorrent forKey:@"PeersTorrent"];

    //pex
    BOOL const pex = tr_sessionIsPexEnabled(self.fHandle);
    [self.fDefaults setBool:pex forKey:@"PEXGlobal"];

    //dht
    BOOL const dht = tr_sessionIsDHTEnabled(self.fHandle);
    [self.fDefaults setBool:dht forKey:@"DHTGlobal"];

    //lpd
    BOOL const lpd = tr_sessionIsLPDEnabled(self.fHandle);
    [self.fDefaults setBool:lpd forKey:@"LocalPeerDiscoveryGlobal"];

    //auto start
    BOOL const autoStart = !tr_sessionGetPaused(self.fHandle);
    [self.fDefaults setBool:autoStart forKey:@"AutoStartDownload"];

    //port
    auto const port = tr_sessionGetPeerPort(self.fHandle);
    [self.fDefaults setInteger:port forKey:@"BindPort"];

    BOOL const nat = tr_sessionIsPortForwardingEnabled(self.fHandle);
    [self.fDefaults setBool:nat forKey:@"NatTraversal"];

    BOOL const randomPort = tr_sessionGetPeerPortRandomOnStart(self.fHandle);
    [self.fDefaults setBool:randomPort forKey:@"RandomPort"];

    //speed limit - down
    BOOL const downLimitEnabled = tr_sessionIsSpeedLimited(self.fHandle, tr_direction::Down);
    [self.fDefaults setBool:downLimitEnabled forKey:@"CheckDownload"];

    auto const downLimit = tr_sessionGetSpeedLimit_KBps(self.fHandle, tr_direction::Down);
    [self.fDefaults setInteger:downLimit forKey:@"DownloadLimit"];

    //speed limit - up
    BOOL const upLimitEnabled = tr_sessionIsSpeedLimited(self.fHandle, tr_direction::Up);
    [self.fDefaults setBool:upLimitEnabled forKey:@"CheckUpload"];

    auto const upLimit = tr_sessionGetSpeedLimit_KBps(self.fHandle, tr_direction::Up);
    [self.fDefaults setInteger:upLimit forKey:@"UploadLimit"];

    //alt speed limit enabled
    BOOL const useAltSpeed = tr_sessionUsesAltSpeed(self.fHandle);
    [self.fDefaults setBool:useAltSpeed forKey:@"SpeedLimit"];

    //alt speed limit - down
    auto const downLimitAlt = tr_sessionGetAltSpeed_KBps(self.fHandle, tr_direction::Down);
    [self.fDefaults setInteger:downLimitAlt forKey:@"SpeedLimitDownloadLimit"];

    //alt speed limit - up
    auto const upLimitAlt = tr_sessionGetAltSpeed_KBps(self.fHandle, tr_direction::Up);
    [self.fDefaults setInteger:upLimitAlt forKey:@"SpeedLimitUploadLimit"];

    //alt speed limit schedule
    BOOL const useAltSpeedSched = tr_sessionUsesAltSpeedTime(self.fHandle);
    [self.fDefaults setBool:useAltSpeedSched forKey:@"SpeedLimitAuto"];

    NSDate* limitStartDate = [PrefsController timeSumToDate:tr_sessionGetAltSpeedBegin(self.fHandle)];
    [self.fDefaults setObject:limitStartDate forKey:@"SpeedLimitAutoOnDate"];

    NSDate* limitEndDate = [PrefsController timeSumToDate:tr_sessionGetAltSpeedEnd(self.fHandle)];
    [self.fDefaults setObject:limitEndDate forKey:@"SpeedLimitAutoOffDate"];

    int const limitDay = tr_sessionGetAltSpeedDay(self.fHandle);
    [self.fDefaults setInteger:limitDay forKey:@"SpeedLimitAutoDay"];

    //blocklist
    BOOL const blocklist = tr_blocklistIsEnabled(self.fHandle);
    [self.fDefaults setBool:blocklist forKey:@"BlocklistNew"];

    NSString* blocklistURL = tr_strv_to_utf8_nsstring(tr_blocklistGetURL(self.fHandle));
    [self.fDefaults setObject:blocklistURL forKey:@"BlocklistURL"];

    //seed ratio
    BOOL const ratioLimited = tr_sessionIsRatioLimited(self.fHandle);
    [self.fDefaults setBool:ratioLimited forKey:@"RatioCheck"];

    float const ratioLimit = tr_sessionGetRatioLimit(self.fHandle);
    [self.fDefaults setFloat:ratioLimit forKey:@"RatioLimit"];

    //idle seed limit
    BOOL const idleLimited = tr_sessionIsIdleLimited(self.fHandle);
    [self.fDefaults setBool:idleLimited forKey:@"IdleLimitCheck"];

    NSUInteger const idleLimitMin = tr_sessionGetIdleLimit(self.fHandle);
    [self.fDefaults setInteger:idleLimitMin forKey:@"IdleLimitMinutes"];

    //queue
    BOOL const downloadQueue = tr_sessionGetQueueEnabled(self.fHandle, tr_direction::Down);
    [self.fDefaults setBool:downloadQueue forKey:@"Queue"];

    size_t const downloadQueueNum = tr_sessionGetQueueSize(self.fHandle, tr_direction::Down);
    [self.fDefaults setInteger:downloadQueueNum forKey:@"QueueDownloadNumber"];

    BOOL const seedQueue = tr_sessionGetQueueEnabled(self.fHandle, tr_direction::Up);
    [self.fDefaults setBool:seedQueue forKey:@"QueueSeed"];

    size_t const seedQueueNum = tr_sessionGetQueueSize(self.fHandle, tr_direction::Up);
    [self.fDefaults setInteger:seedQueueNum forKey:@"QueueSeedNumber"];

    BOOL const checkStalled = tr_sessionGetQueueStalledEnabled(self.fHandle);
    [self.fDefaults setBool:checkStalled forKey:@"CheckStalled"];

    NSInteger const stalledMinutes = tr_sessionGetQueueStalledMinutes(self.fHandle);
    [self.fDefaults setInteger:stalledMinutes forKey:@"StalledMinutes"];

    //done script
    BOOL const doneScriptEnabled = tr_sessionIsScriptEnabled(self.fHandle, TR_SCRIPT_ON_TORRENT_DONE);
    [self.fDefaults setBool:doneScriptEnabled forKey:@"DoneScriptEnabled"];

    NSString* doneScriptPath = tr_strv_to_utf8_nsstring(tr_sessionGetScript(self.fHandle, TR_SCRIPT_ON_TORRENT_DONE));
    [self.fDefaults setObject:doneScriptPath forKey:@"DoneScriptPath"];

    //update gui if loaded
    if (self.fHasLoaded) {
        //encryption handled by bindings

        //download directory handled by bindings

        //utp handled by bindings

        //pex handled by bindings

        //dht handled by bindings

        //lpd handled by bindings

        //port forwarding (nat) handled by bindings
        //random port handled by bindings

        //speed limit schedule handled by bindings

        //speed limit schedule times and day handled by bindings

        [self.peersViewController updateBlocklistButton];
        [self.peersViewController updateBlocklistFields];

        //ratio limit enabled handled by bindings

        //idle limit enabled handled by bindings

        //queues enabled handled by bindings

        //check stalled handled by bindings
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

#pragma mark - Private

- (void)setPrefView:(id)sender
{
    NSString* identifier;
    if (sender) {
        identifier = [sender itemIdentifier];
        [NSUserDefaults.standardUserDefaults setObject:identifier forKey:@"SelectedPrefView"];
    } else {
        identifier = [NSUserDefaults.standardUserDefaults stringForKey:@"SelectedPrefView"];
    }

    NSView* view;
    if ([identifier isEqualToString:ToolbarTabTransfers]) {
        view = self.transfersViewController.view;
    } else if ([identifier isEqualToString:ToolbarTabGroups]) {
        view = self.fGroupsView;
    } else if ([identifier isEqualToString:ToolbarTabBandwidth]) {
        view = self.bandwidthViewController.view;
    } else if ([identifier isEqualToString:ToolbarTabPeers]) {
        view = self.peersViewController.view;
    } else if ([identifier isEqualToString:ToolbarTabNetwork]) {
        view = self.networkViewController.view;
    } else if ([identifier isEqualToString:ToolbarTabRemote]) {
        view = self.remoteViewController.view;
    } else {
        identifier = ToolbarTabGeneral; //general view is the default selected
        view = self.generalViewController.view;
    }

    self.window.toolbar.selectedItemIdentifier = identifier;

    NSWindow* window = self.window;
    if (window.contentView == view) {
        return;
    }

    NSRect windowRect = window.frame;
    CGFloat const difference = NSHeight(view.frame) - NSHeight(window.contentView.frame);
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;

    view.hidden = YES;
    window.contentView = view;

    [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
        context.allowsImplicitAnimation = YES;
        [window setFrame:windowRect display:YES];
    } completionHandler:^{
        view.hidden = NO;
    }];

    //set title label
    if (sender) {
        window.title = [sender label];
    } else {
        NSToolbar* toolbar = window.toolbar;
        NSString* itemIdentifier = toolbar.selectedItemIdentifier;
        for (NSToolbarItem* item in toolbar.items) {
            if ([item.itemIdentifier isEqualToString:itemIdentifier]) {
                window.title = item.label;
                break;
            }
        }
    }
}

@end
