// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "libtransmission/transmission.h"

#import "BlocklistDownloaderViewController.h"
#import "Controller.h"
#import "PrefsController.h"
#import "NSStringAdditions.h"

@interface BlocklistDownloaderViewController ()

@property(nonatomic) IBOutlet NSWindow* fStatusWindow;
@property(nonatomic) IBOutlet NSProgressIndicator* fProgressBar;
@property(nonatomic) IBOutlet NSTextField* fTextField;
@property(nonatomic) IBOutlet NSButton* fButton;

@property(nonatomic, readonly) PrefsController* fPrefsController;

@end

@implementation BlocklistDownloaderViewController

static BlocklistDownloaderViewController* fBLViewController = nil;
+ (void)downloadWithPrefsController:(PrefsController*)prefsController
{
    if (!fBLViewController) {
        fBLViewController = [[BlocklistDownloaderViewController alloc] initWithPrefsController:prefsController];
        [fBLViewController startDownload];
    }
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    self.fButton.title = NSLocalizedString(@"Cancel", "Blocklist -> cancel button");

    CGFloat const oldWidth = NSWidth(self.fButton.frame);
    [self.fButton sizeToFit];
    NSRect buttonFrame = self.fButton.frame;
    buttonFrame.size.width += 12.0; //sizeToFit sizes a bit too small
    buttonFrame.origin.x -= NSWidth(buttonFrame) - oldWidth;
    self.fButton.frame = buttonFrame;

    self.fProgressBar.usesThreadedAnimation = YES;
    [self.fProgressBar startAnimation:self];
}

- (void)cancelDownload:(id)sender
{
    tr_blocklistUpdateCancel(((Controller*)NSApp.delegate).sessionHandle);
    [self setFinished];
}

- (void)setStatusStarting
{
    self.fTextField.stringValue = [NSLocalizedString(@"Updating blocklist", "Blocklist -> message") stringByAppendingEllipsis];
    self.fProgressBar.indeterminate = YES;
}

- (void)setFinished
{
    [self.fPrefsController.window endSheet:self.fStatusWindow];

    fBLViewController = nil;
}

- (void)setFailed:(NSString*)error
{
    [self.fPrefsController.window endSheet:self.fStatusWindow];

    NSAlert* alert = [[NSAlert alloc] init];
    [alert addButtonWithTitle:NSLocalizedString(@"OK", "Blocklist -> button")];
    alert.messageText = NSLocalizedString(@"Download of the blocklist failed.", "Blocklist -> message");
    alert.alertStyle = NSAlertStyleWarning;

    alert.informativeText = error;

    [alert beginSheetModalForWindow:self.fPrefsController.window completionHandler:^(NSModalResponse /*returnCode*/) {
        fBLViewController = nil;
    }];
}

#pragma mark - Private

- (instancetype)initWithPrefsController:(PrefsController*)prefsController
{
    if ((self = [super init])) {
        _fPrefsController = prefsController;
    }

    return self;
}

- (void)startDownload
{
    //load window and show as sheet
    [NSBundle.mainBundle loadNibNamed:@"BlocklistStatusWindow" owner:self topLevelObjects:NULL];

    [self setStatusStarting]; //do before showing the sheet to ensure it doesn't slide out with placeholder text

    [self.fPrefsController.window beginSheet:self.fStatusWindow completionHandler:nil];

    // tr_blocklistUpdate() fires its callback on the libtransmission thread, so
    // hop back to the main queue before touching any UI. Capture weakly: tr_web
    // holds this callback until the transfer finishes, which can outlive the
    // sheet (and thus this controller), so it must not keep them alive or touch
    // them once they're gone.
    __weak BlocklistDownloaderViewController* weakSelf = self;
    __weak PrefsController* weakPrefsController = self.fPrefsController;
    tr_blocklistUpdate(((Controller*)NSApp.delegate).sessionHandle, [weakSelf, weakPrefsController](tr_blocklist_update_result const& result) {
        tr_blocklist_update_status const status = result.status;
        NSString* const error = result.error.empty() ? nil : @(result.error.c_str());
        dispatch_async(dispatch_get_main_queue(), ^{
            BlocklistDownloaderViewController* const strongSelf = weakSelf;
            if (strongSelf == nil) {
                return;
            }
            switch (status) {
            case tr_blocklist_update_status::Ok:
                [strongSelf setFinished];
                [weakPrefsController updateBlocklistFields];
                break;
            case tr_blocklist_update_status::Superseded:
                // a newer update took over; close the sheet quietly, no error
                [strongSelf setFinished];
                break;
            default:
                [strongSelf setFailed:error ?: NSLocalizedString(@"The blocklist could not be updated.", "Blocklist -> message")];
                break;
            }
        });
    });
}

@end
