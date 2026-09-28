// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>

#import "ProgressBarView.h"
#import "ProgressColors.h"
#import "TorrentTableView.h"
#import "Torrent.h"

static CGFloat const kPiecesTotalPercent = 0.6;
static NSInteger const kMaxPieces = 18 * 18;

@interface ProgressBarView ()

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, strong) NSBitmapImageRep* fPiecesBitmap;

@property(nonatomic, readonly) NSColor* fBarBorderColor;
@property(nonatomic, readonly) NSColor* fBluePieceColor;
@property(nonatomic, readonly) NSColor* fBarMinimalBorderColor;

@end

@implementation ProgressBarView

+ (ProgressBarView*)sharedInstance
{
    static ProgressBarView* sSharedInstance = [[ProgressBarView alloc] init];
    return sSharedInstance;
}

- (instancetype)init
{
    if ((self = [super init])) {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        _fPiecesBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:kMaxPieces pixelsHigh:1
                                                              bitsPerSample:8
                                                            samplesPerPixel:4
                                                                   hasAlpha:YES
                                                                   isPlanar:NO
                                                             colorSpaceName:NSCalibratedRGBColorSpace
                                                                bytesPerRow:0
                                                               bitsPerPixel:0];

        _fBluePieceColor = [NSColor colorWithCalibratedRed:0.0 green:0.4 blue:0.8 alpha:1.0];
        _fBarBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.2];
        _fBarMinimalBorderColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.015];
    }
    return self;
}

- (void)drawBarInRect:(NSRect)barRect forTableView:(TorrentTableView*)tableView withTorrent:(Torrent*)torrent
{
    BOOL const minimal = [self.fDefaults boolForKey:@"SmallView"];

    CGFloat const piecesBarPercent = tableView.piecesBarPercent;
    if (piecesBarPercent > 0.0) {
        NSRect piecesBarRect, regularBarRect;
        NSDivideRect(barRect, &piecesBarRect, &regularBarRect, floor(NSHeight(barRect) * kPiecesTotalPercent * piecesBarPercent), NSMaxYEdge);

        [self drawRegularBar:regularBarRect forTorrent:torrent];
        [self drawPiecesBar:piecesBarRect forTorrent:torrent minimal:minimal];
    } else {
        torrent.previousFinishedPieces = nil;

        [self drawRegularBar:barRect forTorrent:torrent];
    }

    NSColor* borderColor = minimal ? self.fBarMinimalBorderColor : self.fBarBorderColor;
    [borderColor set];
    [NSBezierPath strokeRect:NSInsetRect(barRect, 0.5, 0.5)];
}

- (void)drawRegularBar:(NSRect)barRect forTorrent:(Torrent*)torrent
{
    NSRect haveRect, missingRect;
    NSDivideRect(barRect, &haveRect, &missingRect, round(torrent.progress.to_fraction() * NSWidth(barRect)), NSMinXEdge);

    if (!NSIsEmptyRect(haveRect)) {
        if (torrent.active) {
            if (torrent.checking) {
                [ProgressColors.progressYellowColor set];
                NSRectFillUsingOperation(haveRect, NSCompositingOperationSourceOver);
            } else if (torrent.seeding) {
                NSRect ratioHaveRect, ratioRemainingRect;
                NSDivideRect(haveRect, &ratioHaveRect, &ratioRemainingRect, round(torrent.progressStopRatio.to_fraction() * NSWidth(haveRect)), NSMinXEdge);

                [ProgressColors.progressGreenColor set];
                NSRectFillUsingOperation(ratioHaveRect, NSCompositingOperationSourceOver);

                [ProgressColors.progressLightGreenColor set];
                NSRectFillUsingOperation(ratioRemainingRect, NSCompositingOperationSourceOver);
            } else {
                [ProgressColors.progressBlueColor set];
                NSRectFillUsingOperation(haveRect, NSCompositingOperationSourceOver);
            }
        } else {
            if (torrent.waitingToStart) {
                if (torrent.allDownloaded) {
                    [ProgressColors.progressDarkGreenColor set];
                    NSRectFillUsingOperation(haveRect, NSCompositingOperationSourceOver);
                } else {
                    [ProgressColors.progressDarkBlueColor set];
                    NSRectFillUsingOperation(haveRect, NSCompositingOperationSourceOver);
                }
            } else {
                [ProgressColors.progressGrayColor set];
                NSRectFillUsingOperation(haveRect, NSCompositingOperationSourceOver);
            }
        }
    }

    if (!torrent.allDownloaded) {
        CGFloat const widthRemaining = round(NSWidth(barRect) * torrent.progressLeft);

        NSRect wantedRect;
        NSDivideRect(missingRect, &wantedRect, &missingRect, widthRemaining, NSMinXEdge);

        //not-available section
        if (torrent.active && !torrent.checking && !torrent.availableDesired.is_complete() &&
            [self.fDefaults boolForKey:@"DisplayProgressBarAvailable"]) {
            NSRect unavailableRect;
            NSDivideRect(wantedRect, &wantedRect, &unavailableRect, round(NSWidth(wantedRect) * torrent.availableDesired.to_fraction()), NSMinXEdge);

            [ProgressColors.progressRedColor set];
            NSRectFillUsingOperation(unavailableRect, NSCompositingOperationSourceOver);
        }

        //remaining section
        [ProgressColors.progressWhiteColor set];
        NSRectFillUsingOperation(wantedRect, NSCompositingOperationSourceOver);
    }

    //unwanted section
    if (!NSIsEmptyRect(missingRect)) {
        if (!torrent.magnet) {
            [ProgressColors.progressLightGrayColor set];
            NSRectFillUsingOperation(missingRect, NSCompositingOperationSourceOver);
        } else {
            [ProgressColors.progressRedColor set];
            NSRectFillUsingOperation(missingRect, NSCompositingOperationSourceOver);
        }
    }
}

- (void)drawPiecesBar:(NSRect)barRect forTorrent:(Torrent*)torrent minimal:(BOOL)minimal
{
    BOOL const darkMode = [[NSAppearance.currentDrawingAppearance
        bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] isEqualToString:NSAppearanceNameDarkAqua];
    // Fill a solid color bar for magnet links
    if (torrent.magnet) {
        if (darkMode) {
            [NSColor.controlColor set];
        } else {
            [[NSColor colorWithCalibratedWhite:1.0 alpha:minimal ? 0.25 : 1.0] set];
        }
        NSRectFillUsingOperation(barRect, NSCompositingOperationSourceOver);
        return;
    }

    int const pieceCount = static_cast<int>(MIN(torrent.pieceCount, kMaxPieces));
    if (pieceCount <= 0) {
        torrent.previousFinishedPieces = nil;
        return;
    }

    auto piecesPercent = std::array<float, kMaxPieces>{};
    [torrent getAmountFinished:piecesPercent.data() size:pieceCount];

    NSBitmapImageRep* bitmap = self.fPiecesBitmap;

    NSIndexSet* previousFinishedIndexes = torrent.previousFinishedPieces;
    NSMutableIndexSet* finishedIndexes = [NSMutableIndexSet indexSet];

    NSColor* const pieceBgColor = darkMode ? NSColor.controlColor : NSColor.whiteColor;

    for (int i = 0; i < pieceCount; i++) {
        NSColor* pieceColor;
        if (piecesPercent[i] == 1.0F) {
            if (previousFinishedIndexes && ![previousFinishedIndexes containsIndex:i]) {
                pieceColor = NSColor.orangeColor;
            } else {
                pieceColor = self.fBluePieceColor;
            }
            [finishedIndexes addIndex:i];
        } else {
            pieceColor = [pieceBgColor blendedColorWithFraction:piecesPercent[i] ofColor:self.fBluePieceColor];
        }

        //it's faster to just set color instead of checking previous color
        // faster and non-broken alternative to `[bitmap setColor:pieceColor atX:i y:0]`
        unsigned char* data = bitmap.bitmapData + (i << 2);
        data[0] = pieceColor.redComponent * 255;
        data[1] = pieceColor.greenComponent * 255;
        data[2] = pieceColor.blueComponent * 255;
        data[3] = pieceColor.alphaComponent * 255;
    }

    torrent.previousFinishedPieces = finishedIndexes.count > 0 ? finishedIndexes : nil; //don't bother saving if none are complete

    //actually draw image
    [bitmap drawInRect:barRect fromRect:NSMakeRect(0.0, 0.0, pieceCount, 1.0) operation:NSCompositingOperationSourceOver
              fraction:minimal ? 0.25 : 1.0
        respectFlipped:YES
                 hints:nil];
}

@end
