// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "libtransmission/transmission.h"

#import "BlocklistDownloaderViewController.h"
#import "NSStringAdditions.h"

@interface BlocklistDownloaderViewController ()

@property(nonatomic, assign) tr_session* fSessionHandle;
@property(nonatomic, strong) NSAlert* fAlert;
@property(nonatomic, strong) NSProgressIndicator* fProgressBar;
@property(nonatomic, copy, nullable) NSString* fError;

@end

@implementation BlocklistDownloaderViewController

- (instancetype)initWithSessionHandle:(tr_session*)sessionHandle
{
    if ((self = [super init])) {
        _fSessionHandle = sessionHandle;

        _fAlert = [[NSAlert alloc] init];
        _fAlert.messageText = [NSLocalizedString(@"Updating blocklist", @"Blocklist -> message") stringByAppendingEllipsis];
        _fAlert.alertStyle = NSAlertStyleInformational;

        NSButton* cancelButton = [_fAlert addButtonWithTitle:NSLocalizedString(@"Cancel", @"Blocklist -> cancel button")];
        cancelButton.target = self;
        cancelButton.action = @selector(cancelDownload:);
        cancelButton.keyEquivalent = @"\e"; // Esc

        _fProgressBar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 300, 16)];
        _fProgressBar.style = NSProgressIndicatorStyleBar;
        _fProgressBar.indeterminate = YES;
        _fProgressBar.usesThreadedAnimation = YES;

        _fAlert.accessoryView = _fProgressBar;
    }
    return self;
}

- (void)showAsSheetModalForWindow:(NSWindow*)parentWindow completionHandler:(void (^)(NSString* _Nullable error))handler
{
    [self.fProgressBar startAnimation:self];

    [self.fAlert beginSheetModalForWindow:parentWindow completionHandler:^(NSModalResponse /*returnCode*/) {
        handler(self.fError);
    }];

    [self startDownload];
}

- (void)didFinishDownload:(tr_blocklist_update_result const&)result
{
    tr_blocklist_update_status const status = result.status;
    NSString* const error = result.message.empty() ? nil : @(result.message.c_str());

    [self.fProgressBar stopAnimation:nil];

    if (status == tr_blocklist_update_status::Ok) {
        [self.fAlert.window.sheetParent endSheet:self.fAlert.window returnCode:NSModalResponseOK];
    } else {
        self.fError = error ?: NSLocalizedString(@"Couldn't update blocklist", @"Blocklist -> message");
        [self.fAlert.window.sheetParent endSheet:self.fAlert.window returnCode:NSModalResponseCancel];
    }
}

- (void)startDownload
{
    // tr_blocklistUpdate() fires its callback on the libtransmission thread,
    // so hop back to the main queue before touching any UI.
    // Capture weakly: tr_web holds this callback until the transfer finishes,
    // which can outlive the sheet and this controller.
    // Take the result by value: a block captures a C++ reference by address,
    // and libtransmission destroys the result when this callback returns.
    __weak BlocklistDownloaderViewController* weakSelf = self;
    tr_blocklistUpdate(self.fSessionHandle, [weakSelf](tr_blocklist_update_result result) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf didFinishDownload:result];
        });
    });
}

- (void)cancelDownload:(id)sender
{
    tr_blocklistUpdateCancel(self.fSessionHandle);
    [self.fProgressBar stopAnimation:self];

    [self.fAlert.window.sheetParent endSheet:self.fAlert.window returnCode:NSModalResponseCancel];
}

@end
