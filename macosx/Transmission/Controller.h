// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
#import <Quartz/Quartz.h>

#import <Sparkle/SPUUpdaterDelegate.h>

#include <libtransmission/transmission.h>

#import "VDKQueue.h"

@class AddMagnetWindowController;
@class AddWindowController;
@class MessageWindowController;
@class PrefsController;
@class Torrent;

typedef NS_ENUM(NSUInteger, AddType) { //
    AddTypeManual,
    AddTypeAuto,
    AddTypeShowOptions,
    AddTypeURL,
    AddTypeCreated
};

@interface Controller
    : NSObject<NSApplicationDelegate, NSMenuItemValidation, NSPopoverDelegate, NSSharingServiceDelegate, NSSharingServicePickerDelegate, NSToolbarDelegate, NSToolbarItemValidation, NSWindowDelegate, QLPreviewPanelDataSource, QLPreviewPanelDelegate, VDKQueueDelegate, SPUUpdaterDelegate>

- (void)openFiles:(NSArray<NSString*>*)filenames addType:(AddType)type forcePath:(NSString*)path;

- (void)askOpenConfirmed:(AddWindowController*)addController add:(BOOL)add;
- (void)openCreatedFile:(NSNotification*)notification;
- (void)openFilesWithDict:(NSDictionary*)dictionary;

- (void)openMagnet:(NSString*)address;
- (void)askOpenMagnetConfirmed:(AddMagnetWindowController*)addController add:(BOOL)add;

- (void)invalidOpenAlert:(NSString*)filename;
- (void)invalidOpenMagnetAlert:(NSString*)address;
- (void)duplicateOpenAlert:(NSString*)name;
- (void)duplicateOpenMagnetAlert:(NSString*)address transferName:(NSString*)name;

- (void)openURL:(NSString*)urlString;

- (void)openPasteboard;

@property(nonatomic, readonly) tr_session* sessionHandle;

- (void)resumeTorrents:(NSArray<Torrent*>*)torrents;

- (void)resumeTorrentsNoWait:(NSArray<Torrent*>*)torrents;

- (void)stopTorrents:(NSArray<Torrent*>*)torrents;

- (void)removeTorrents:(NSArray<Torrent*>*)torrents deleteData:(BOOL)deleteData;
- (void)confirmRemoveTorrents:(NSArray<Torrent*>*)torrents deleteData:(BOOL)deleteData;

- (void)moveDataFiles:(NSArray<Torrent*>*)torrents;

- (void)copyTorrentFileForTorrents:(NSMutableArray<Torrent*>*)torrents;

- (void)verifyTorrents:(NSArray<Torrent*>*)torrents;

@property(nonatomic, readonly) NSArray<Torrent*>* selectedTorrents;

@property(nonatomic, readonly) PrefsController* prefsController;

- (void)resetInfo;

@property(nonatomic, readonly) MessageWindowController* messageWindowController;

- (void)updateUI;
- (void)fullUpdateUI;

- (void)setBottomCountText:(BOOL)filtering;

- (Torrent*)torrentForHash:(NSString*)hash;
- (Torrent*)torrentForId:(tr_torrent_id_t)id;

- (void)torrentFinishedDownloading:(NSNotification*)notification;
- (void)torrentRestartedDownloading:(NSNotification*)notification;
- (void)torrentFinishedSeeding:(NSNotification*)notification;

- (void)updateTorrentHistory;

- (void)applyFilter;

- (void)sortTorrentsAndIncludeQueueOrder:(BOOL)includeQueueOrder;
- (void)sortTorrentsCallUpdates:(BOOL)callUpdates includeQueueOrder:(BOOL)includeQueueOrder;
- (void)rearrangeTorrentTableArray:(NSMutableArray*)rearrangeArray
                         forParent:(id)parent
               withSortDescriptors:(NSArray*)descriptors
                  beganTableUpdate:(BOOL*)beganTableUpdate;

// In TorrentTableView
- (void)toggleQuickLook;
- (void)showInfo;

- (void)setGroup:(id)sender; //used by delegate-generated menu items

- (void)altSpeedToggledCallbackIsLimited:(NSDictionary*)dict;

- (void)changeAutoImport;
- (void)checkAutoImportDirectory;

- (void)beginCreateFile:(NSNotification*)notification;

@property(nonatomic, readonly) VDKQueue* fileWatcherQueue;

- (void)torrentTableViewSelectionDidChange:(NSNotification*)notification;

- (void)focusFilterField;

- (void)allToolbarClicked:(id)sender;
- (void)selectedToolbarClicked:(id)sender;

- (void)updateMainWindow;

- (void)setWindowSizeToFit;
- (void)updateForAutoSize;
- (void)updateWindowAfterToolbarChange;
- (void)removeHeightConstraints;
@property(nonatomic, readonly) CGFloat minScrollViewHeightAllowed;
@property(nonatomic, readonly) CGFloat toolbarHeight;
@property(nonatomic, readonly) CGFloat mainWindowComponentHeight;
@property(nonatomic, readonly) CGFloat scrollViewHeight;
@property(nonatomic, getter=isFullScreen, readonly) BOOL fullScreen;

- (void)updateForExpandCollapse;

- (void)rpcCallback:(tr_rpc_callback_type)type forTorrentId:(std::optional<tr_torrent_id_t>)torrentId;
- (void)rpcAddTorrentStruct:(struct tr_torrent*)torrentStruct;
- (void)rpcRemoveTorrent:(Torrent*)torrent deleteData:(BOOL)deleteData;
- (void)rpcStartedStoppedTorrent:(Torrent*)torrent;
- (void)rpcChangedTorrent:(Torrent*)torrent;
- (void)rpcMovedTorrent:(Torrent*)torrent;
- (void)rpcUpdateQueue;

@end
