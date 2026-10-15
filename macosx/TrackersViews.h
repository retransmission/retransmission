// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class TrackerNode;

@interface TrackerView : NSTableCellView
- (void)configureWithNode:(TrackerNode*)node;
@end

@interface TrackerTierView : NSTableCellView
- (void)setTier:(NSString*)tier;
@end

@class TrackerInputView;

@protocol TrackerInputViewDelegate<NSObject>
- (void)trackerInputView:(TrackerInputView*)inputView didCommitAddress:(NSString*)address;
- (void)trackerInputViewDidCancel:(TrackerInputView*)inputView;
@end

@interface TrackerInputView : NSView

@property(nonatomic, weak) id<TrackerInputViewDelegate> delegate;
@property(nonatomic, readonly) NSTextField* textFieldView;

@end
