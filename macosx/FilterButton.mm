// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterButton.h"
#import "NSStringAdditions.h"
#import "L10n.h"

@implementation FilterButton

- (instancetype)initWithCoder:(NSCoder*)coder
{
    if ((self = [super initWithCoder:coder])) {
        _count = NSNotFound;
    }
    return self;
}

- (void)setCount:(NSUInteger)count
{
    if (count == _count) {
        return;
    }

    _count = count;

    self.toolTip = [NSString stringForTorrentCount:count];
}

@end
