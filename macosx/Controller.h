// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class AddMagnetWindowController;
@class AddWindowController;
@class MessageWindowController;
@class PrefsController;
@class Torrent;

struct tr_session;

/// Public because ShareTorrentFileHelper sets Controller as each NSSharingService's delegate.
/// TODO: Move NSSharingServiceDelegate to implementation once macOS 12 support is dropped.
/// Look at modern way to show NSSharingServicePicker.
@interface Controller : NSObject<NSSharingServiceDelegate>

/// Quits if another copy is running, registers the default settings, and on the first launch offers the import from Transmission.
/// Runs while the app finishes launching, before the main nib loads, because the nib's objects read the settings as they load.
+ (void)prepareForLaunch;

// In StatsWindowController / CreatorWindowController
@property(nonatomic, readonly) tr_session* sessionHandle;

// In PrefsController
@property(nonatomic, readonly) PrefsController* prefsController;

// In MessageWindowController
@property(nonatomic, readonly) MessageWindowController* messageWindowController;

// In ShareTorrentFileHelper
@property(nonatomic, readonly) NSArray<Torrent*>* selectedTorrents;

// In AddWindowController/AddMagnetWindowController
- (void)askOpenConfirmed:(AddWindowController*)addController add:(BOOL)add;
- (void)askOpenMagnetConfirmed:(AddMagnetWindowController*)addController add:(BOOL)add;

// In TorrentTableView
- (void)openPasteboard;
- (void)resumeTorrents:(NSArray<Torrent*>*)torrents;
- (void)resumeTorrentsNoWait:(NSArray<Torrent*>*)torrents;
- (void)stopTorrents:(NSArray<Torrent*>*)torrents;
- (void)toggleQuickLook;
- (void)showInfo;
- (void)focusFilterField;

@end
