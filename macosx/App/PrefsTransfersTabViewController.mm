// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PrefsTransfersTabViewController.h"
#import "PrefsTransfersAddingViewController.h"
#import "PrefsTransfersManagementViewController.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesTransfersTab", @"")

// Standard tab view chrome height padding (tabs height + default margins)
static CGFloat const kTabViewChromeHeightPadding = 48.0;

@interface PrefsTransfersTabViewController ()<NSTabViewDelegate>

@property(nonatomic) PrefsTransfersAddingViewController* addingViewController;
@property(nonatomic) PrefsTransfersManagementViewController* managementViewController;

@end

@implementation PrefsTransfersTabViewController

- (instancetype)initWithHandle:(tr_session*)handle
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _addingViewController = [[PrefsTransfersAddingViewController alloc] initWithHandle:handle];
        _managementViewController = [[PrefsTransfersManagementViewController alloc] initWithHandle:handle];
    }
    return self;
}

- (void)loadView
{
    NSTabView* tabView = [[NSTabView alloc] init];
    tabView.translatesAutoresizingMaskIntoConstraints = NO;
    tabView.font = [NSFont systemFontOfSize:NSFont.systemFontSize];

    NSTabViewItem* addingTab = [NSTabViewItem tabViewItemWithViewController:self.addingViewController];
    addingTab.label = LStr(@"Adding");
    [tabView addTabViewItem:addingTab];

    NSTabViewItem* managementTab = [NSTabViewItem tabViewItemWithViewController:self.managementViewController];
    managementTab.label = LStr(@"Management");
    [tabView addTabViewItem:managementTab];

    NSView* containerView = [[NSView alloc] init];
    [containerView addSubview:tabView];

    NSLayoutConstraint* preferredWidth = [tabView.widthAnchor constraintEqualToConstant:462];
    preferredWidth.priority = NSLayoutPriorityDefaultHigh;

    [NSLayoutConstraint activateConstraints:@[
        preferredWidth,
        [tabView.centerXAnchor constraintEqualToAnchor:containerView.centerXAnchor],
        [tabView.topAnchor constraintEqualToAnchor:containerView.topAnchor constant:10],
        [tabView.bottomAnchor constraintEqualToAnchor:containerView.bottomAnchor constant:-20],
        [tabView.leadingAnchor constraintGreaterThanOrEqualToAnchor:containerView.leadingAnchor constant:20],
        [tabView.trailingAnchor constraintLessThanOrEqualToAnchor:containerView.trailingAnchor constant:-20],
    ]];

    [containerView setFrameSize:NSMakeSize(containerView.fittingSize.width, self.addingViewController.view.fittingSize.height + kTabViewChromeHeightPadding)];
    self.view = containerView;

    tabView.delegate = self;
}

- (void)viewWillAppear
{
    [super viewWillAppear];

    // Size the window for whichever tab is selected
    NSTabView* tabView = self.view.subviews.firstObject;
    [self resizeWindowToFitViewController:tabView.selectedTabViewItem.viewController animated:NO];
}

#pragma mark - NSTabViewDelegate

- (void)tabView:(NSTabView*)tabView didSelectTabViewItem:(NSTabViewItem*)tabViewItem
{
    // Safe size extraction directly from the child view controllers, breaking any cycle
    NSViewController* selectedVC = tabViewItem.viewController;
    if (selectedVC) {
        [self resizeWindowToFitViewController:selectedVC animated:YES];
    }
}

#pragma mark - Private Resize Helper

- (void)resizeWindowToFitViewController:(NSViewController*)viewController animated:(BOOL)animated
{
    NSWindow* window = self.view.window;
    if (!window)
        return;

    // Calculate the target frame using child's layout properties independently
    NSRect windowRect = window.frame;
    CGFloat const currentContentHeight = NSHeight(window.contentView.frame);

    // Child's fitting height + static chrome/padding constant
    CGFloat const targetContentHeight = [viewController.view fittingSize].height + kTabViewChromeHeightPadding;
    CGFloat const difference = targetContentHeight - currentContentHeight;

    windowRect.origin.y -= difference;
    windowRect.size.height += difference;

    [window setFrame:windowRect display:YES];
}

@end
