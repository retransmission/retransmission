// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoGeneralViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"
#import "L10n.h"

@interface InfoGeneralViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) IBOutlet NSTextField* fSizeField;
@property(nonatomic) IBOutlet NSTextField* fHashField;
@property(nonatomic) IBOutlet NSTextField* fSecureField;
@property(nonatomic) IBOutlet NSTextField* fDataLocationField;
@property(nonatomic) IBOutlet NSTextField* fLastDataLocationField;
@property(nonatomic) IBOutlet NSTextField* fLastDataLabel;
@property(nonatomic) IBOutlet NSTextField* fCreatorField;
@property(nonatomic) IBOutlet NSTextField* fDateCreatedField;

@property(nonatomic) IBOutlet NSTextView* fCommentView;

@property(nonatomic) IBOutlet NSButton* fRevealDataButton;

@end

@implementation InfoGeneralViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:@"InfoGeneralView" bundle:nil])) {
        // Translators: Inspector view -> title
        self.title = TR_TEXT("Information");
    }

    return self;
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    //don't check if it's the same in case the metadata changed
    self.fTorrents = torrents;

    self.fSet = NO;
}

- (void)updateInfo
{
    if (!self.fSet) {
        [self setupInfo];
    }

    if (self.fTorrents.count != 1) {
        return;
    }

    Torrent* torrent = self.fTorrents[0];

    NSString* location = torrent.dataLocation;
    NSString* lastKnownDataLocation = torrent.lastKnownDataLocation;

    self.fDataLocationField.stringValue = location ? location.stringByAbbreviatingWithTildeInPath : @"";
    self.fDataLocationField.toolTip = location ? location : @"";

    self.fLastDataLabel.hidden = location ? YES : NO;
    self.fLastDataLocationField.hidden = location ? YES : NO;
    self.fLastDataLocationField.stringValue = location ? @"" : lastKnownDataLocation.stringByAbbreviatingWithTildeInPath ?: @"";
    self.fLastDataLocationField.toolTip = location ? @"" : lastKnownDataLocation;

    self.fRevealDataButton.hidden = location ? NO : YES;
}

- (IBAction)revealDataFile:(id)sender
{
    Torrent* torrent = self.fTorrents[0];
    NSString* location = torrent.dataLocation;
    if (!location) {
        return;
    }

    NSURL* file = [NSURL fileURLWithPath:location];
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ file ]];
}

#pragma mark - Private

- (void)setupInfo
{
    self.fLastDataLabel.hidden = YES;
    self.fLastDataLocationField.hidden = YES;

    if (self.fTorrents.count == 1) {
        Torrent* torrent = self.fTorrents[0];

        // "1.21 GB in 3 files (4,812 pieces @ 256 KB)", as in the other clients.
        // It takes two messages, because a plural form follows only one count.
        NSString* sizeString = @"";
        if (!torrent.magnet) {
            NSString* const size = TR_FORMAT_N(
                "{total_size} in {file_count:L} file",
                "{total_size} in {file_count:L} files",
                torrent.fileCount,
                TRArg("total_size", [NSString stringForFileSize:torrent.size]),
                TRArg("file_count", torrent.fileCount));
            NSString* const pieces = TR_FORMAT_N(
                "({piece_count:L} piece @ {piece_size})",
                "({piece_count:L} pieces @ {piece_size})",
                torrent.pieceCount,
                TRArg("piece_count", torrent.pieceCount),
                TRArg("piece_size", [NSString stringForFileSize:torrent.pieceSize]));
            sizeString = [NSString stringWithFormat:@"%@ %@", size, pieces];
        }
        self.fSizeField.stringValue = sizeString;

        NSString* hashString = torrent.hashString;
        self.fHashField.stringValue = hashString;
        self.fHashField.toolTip = hashString;
        self.fSecureField.stringValue = torrent.privateTorrent ? TR_TEXT("Private to this tracker -- DHT and PEX disabled") :
                                                                 // Translators: Inspector -> private torrent
                                                                 TR_TEXT("Public torrent");

        NSString* commentString = torrent.comment;
        self.fCommentView.string = commentString;

        NSString* creatorString = torrent.creator;
        self.fCreatorField.stringValue = creatorString;
        self.fDateCreatedField.objectValue = torrent.dateCreated;
    } else {
        self.fSizeField.stringValue = @"";
        self.fHashField.stringValue = @"";
        self.fHashField.toolTip = nil;
        self.fSecureField.stringValue = @"";
        self.fCommentView.string = @"";

        self.fCreatorField.stringValue = @"";
        self.fDateCreatedField.stringValue = @"";

        self.fDataLocationField.stringValue = @"";
        self.fDataLocationField.toolTip = nil;

        self.fRevealDataButton.hidden = YES;
    }

    self.fSet = YES;
}

@end
