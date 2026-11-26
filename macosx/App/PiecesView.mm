// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <QuartzCore/QuartzCore.h>
#import "PiecesView.h"
#import "Torrent.h"
#import "InfoWindowController.h"

static NSInteger const kMaxAcross = 18;
static NSInteger const kMaxCells = kMaxAcross * kMaxAcross;

static CGFloat const kBetweenPadding = 1.0;

static int8_t const kHighPeers = 10;

static NSColor* const DoneColor = NSColor.systemBlueColor;
static NSColor* const BlinkColor = NSColor.systemOrangeColor;
static NSColor* const HighColor = NSColor.systemGreenColor;

typedef struct PieceInfo {
    int8_t available[kMaxCells];
    float complete[kMaxCells];
} PieceInfo;

@implementation PiecesView {
    PieceInfo fPieceInfo;
    NSString* fRenderedHashString;
    NSArray<CALayer*>* fCellLayers;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if ((self = [super initWithFrame:frameRect])) {
        [self setupLayers];
    }
    return self;
}

- (void)setupLayers
{
    self.wantsLayer = YES;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;

    NSDictionary<NSString*, id<CAAction>>* disabledActions = @{
        @"backgroundColor" : [NSNull null],
        @"bounds" : [NSNull null],
        @"position" : [NSNull null],
        @"hidden" : [NSNull null]
    };

    NSMutableArray<CALayer*>* layers = [NSMutableArray arrayWithCapacity:kMaxCells];
    for (NSInteger i = 0; i < kMaxCells; ++i) {
        CALayer* cellLayer = [CALayer layer];
        cellLayer.hidden = YES;
        cellLayer.actions = disabledActions;
        [self.layer addSublayer:cellLayer];
        [layers addObject:cellLayer];
    }
    fCellLayers = [layers copy];

    [self updateLayerAppearance];
}

#pragma mark - Appearance & Colors

- (BOOL)isDarkMode
{
    NSAppearanceName bestMatch = [self.effectiveAppearance
        bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
    return [bestMatch isEqualToString:NSAppearanceNameDarkAqua];
}

- (NSColor*)backgroundColor
{
    return self.isDarkMode ? NSColor.blackColor : NSColor.whiteColor;
}

- (void)updateLayerAppearance
{
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
        self.layer.backgroundColor = [NSColor.controlTextColor colorWithAlphaComponent:0.2].CGColor;
    }];
}

- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    [self updateLayerAppearance];

    fRenderedHashString = nil;
    [self updateView];
}

- (BOOL)isCompletenessDone:(float)val
{
    return val >= 1.0F;
}

- (BOOL)isCompletenessNone:(float)val
{
    return val <= 0.0F;
}

- (NSColor*)completenessColor:(float)oldVal newVal:(float)newVal noBlink:(BOOL)noBlink
{
    if ([self isCompletenessDone:newVal]) {
        return noBlink || [self isCompletenessDone:oldVal] ? DoneColor : BlinkColor;
    }

    if ([self isCompletenessNone:newVal]) {
        return noBlink || [self isCompletenessNone:oldVal] ? [self backgroundColor] : BlinkColor;
    }

    return [[self backgroundColor] blendedColorWithFraction:newVal ofColor:DoneColor];
}

- (BOOL)isAvailabilityDone:(uint8_t)val
{
    return val == (uint8_t)-1;
}

- (BOOL)isAvailabilityNone:(uint8_t)val
{
    return val == 0;
}

- (BOOL)isAvailabilityHigh:(uint8_t)val
{
    return val >= kHighPeers;
}

- (NSColor*)availabilityColor:(int8_t)oldVal newVal:(int8_t)newVal noBlink:(bool)noBlink
{
    if ([self isAvailabilityDone:newVal]) {
        return noBlink || [self isAvailabilityDone:oldVal] ? DoneColor : BlinkColor;
    }

    if ([self isAvailabilityNone:newVal]) {
        return noBlink || [self isAvailabilityNone:oldVal] ? [self backgroundColor] : BlinkColor;
    }

    if ([self isAvailabilityHigh:newVal]) {
        return noBlink || [self isAvailabilityHigh:oldVal] ? HighColor : BlinkColor;
    }

    CGFloat percent = CGFloat(newVal) / kHighPeers;
    return [[self backgroundColor] blendedColorWithFraction:percent ofColor:HighColor];
}

#pragma mark - Layout & Data Updates

- (void)layout
{
    [super layout];

    if (!self.torrent) {
        return;
    }

    int const numCells = static_cast<int>(MIN(_torrent.pieceCount, kMaxCells));
    if (numCells <= 0) {
        return;
    }

    int const across = static_cast<int>(ceil(sqrt(numCells)));
    CGFloat const fullWidth = self.bounds.size.width;
    if (fullWidth <= 0) {
        return;
    }

    NSInteger const cellWidth = (NSInteger)((fullWidth - (across + 1) * kBetweenPadding) / across);
    NSInteger const extraBorder = (NSInteger)((fullWidth - ((cellWidth + kBetweenPadding) * across + kBetweenPadding)) / 2);

    for (int index = 0; index < numCells; index++) {
        int const row = index / across;
        int const col = index % across;

        fCellLayers[index].frame = CGRectMake(
            col * (cellWidth + kBetweenPadding) + kBetweenPadding + extraBorder,
            fullWidth - (row + 1) * (cellWidth + kBetweenPadding) - extraBorder,
            MAX(0, cellWidth),
            MAX(0, cellWidth));
    }
}

- (void)setTorrent:(Torrent*)torrent
{
    _torrent = (torrent && !torrent.magnet) ? torrent : nil;
    [self clearView];

    if (_torrent) {
        self.needsLayout = YES;
    }
}

- (void)clearView
{
    fRenderedHashString = nil;
    memset(&fPieceInfo, 0, sizeof(PieceInfo));

    for (CALayer* layer in fCellLayers) {
        layer.hidden = YES;
    }
}

- (void)updateView
{
    if (!self.torrent) {
        return;
    }

    PieceInfo const oldInfo = fPieceInfo;
    BOOL const first = ![self.torrent.hashString isEqualToString:fRenderedHashString];

    BOOL const showAvailability = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
    int const numCells = static_cast<int>(MIN(_torrent.pieceCount, kMaxCells));
    PieceInfo info;
    [self.torrent getAvailability:info.available size:numCells];
    [self.torrent getAmountFinished:info.complete size:numCells];

    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
        for (int index = 0; index < kMaxCells; index++) {
            CALayer* cellLayer = fCellLayers[index];

            if (index >= numCells) {
                cellLayer.hidden = YES;
                continue;
            }

            NSColor* color = showAvailability ?
                [self availabilityColor:oldInfo.available[index] newVal:info.available[index] noBlink:first] :
                [self completenessColor:oldInfo.complete[index] newVal:info.complete[index] noBlink:first];

            cellLayer.hidden = NO;
            cellLayer.backgroundColor = color.CGColor;
        }
    }];

    fPieceInfo = info;
    fRenderedHashString = self.torrent.hashString;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    return YES;
}

@end
