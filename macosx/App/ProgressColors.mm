// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ProgressColors.h"

@implementation ProgressColors

+ (NSColor*)progressWhiteColor
{
    return [self progressColorForAsset:@"ProgressWhite"];
}
+ (NSColor*)progressGrayColor
{
    return [self progressColorForAsset:@"ProgressGray"];
}
+ (NSColor*)progressLightGrayColor
{
    return [self progressColorForAsset:@"ProgressLightGray"];
}
+ (NSColor*)progressBlueColor
{
    return [self progressColorForAsset:@"ProgressBlue"];
}
+ (NSColor*)progressDarkBlueColor
{
    return [self progressColorForAsset:@"ProgressDarkBlue"];
}
+ (NSColor*)progressGreenColor
{
    return [self progressColorForAsset:@"ProgressGreen"];
}
+ (NSColor*)progressLightGreenColor
{
    return [self progressColorForAsset:@"ProgressLightGreen"];
}
+ (NSColor*)progressDarkGreenColor
{
    return [self progressColorForAsset:@"ProgressDarkGreen"];
}
+ (NSColor*)progressRedColor
{
    return [self progressColorForAsset:@"ProgressRed"];
}
+ (NSColor*)progressYellowColor
{
    return [self progressColorForAsset:@"ProgressYellow"];
}

#pragma mark - Private
+ (NSColor*)progressColorForAsset:(NSString*)assetName
{
    CGFloat const alpha = [NSUserDefaults.standardUserDefaults boolForKey:@"SmallView"] ? 0.27 : 1.0;
    return [[NSColor colorNamed:assetName] colorWithAlphaComponent:alpha];
}

@end
