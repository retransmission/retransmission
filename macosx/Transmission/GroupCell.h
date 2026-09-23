// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
#import "TorrentTableView.h"

@interface GroupCell : NSTableCellView

@property(nonatomic) NSImageView* fGroupIndicatorView;
@property(nonatomic) NSTextField* fGroupTitleField;

@property(nonatomic) NSImageView* fGroupDownloadView;
@property(nonatomic) NSImageView* fGroupUploadAndRatioView;
@property(nonatomic) NSTextField* fGroupDownloadField;
@property(nonatomic) NSTextField* fGroupUploadAndRatioField;

@end
