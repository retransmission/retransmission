// GroupRulesWindowController.mm
// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupRulesWindowController.h"
#import "PredicateEditorRowTemplateAny.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"GroupRules", @"")

@interface GroupRulesWindowController ()

@property(nonatomic) NSPredicateEditor* ruleEditor;
@property(nonatomic) NSLayoutConstraint* ruleEditorHeightConstraint;
@property(nonatomic) NSPredicate* initialPredicate;
@property(nonatomic, copy) void (^completionHandler)(BOOL, NSPredicate* _Nullable);

@end

@implementation GroupRulesWindowController

- (instancetype)initWithPredicate:(NSPredicate*)predicate
                completionHandler:(void (^)(BOOL, NSPredicate* _Nullable))completionHandler
{
    if ((self = [super initWithWindow:nil])) {
        _initialPredicate = predicate;
        _completionHandler = [completionHandler copy];
        [self loadWindow];
        [self setupUI];
    }
    return self;
}

- (void)loadWindow
{
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 471, 248) styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    self.window = window;
}

- (void)setupUI
{
    NSView* contentView = self.window.contentView;
    self.window.minSize = NSMakeSize(471, 248);

    NSScrollView* scrollView = [[NSScrollView alloc] init];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.autohidesScrollers = YES;
    scrollView.hasVerticalScroller = YES;
    scrollView.drawsBackground = NO;
    scrollView.borderType = NSBezelBorder;
    [contentView addSubview:scrollView];

    self.ruleEditor = [[NSPredicateEditor alloc] init];
    self.ruleEditor.translatesAutoresizingMaskIntoConstraints = NO;
    self.ruleEditor.nestingMode = NSRuleEditorNestingModeCompound;
    self.ruleEditor.rowHeight = 25;

    // Pass flat dictionary to the editor.
    self.ruleEditor.formattingDictionary = [self rulesDictionary];

    // 1. Template for AND/OR/NOT
    NSPredicateEditorRowTemplate* compoundTemplate = [[NSPredicateEditorRowTemplate alloc]
        initWithCompoundTypes:@[ @2, @1, @0 ]];

    // 2. Template for Name / Comment
    NSExpression* leftExpressionName = [NSExpression expressionForKeyPath:@"name"];
    NSExpression* leftExpressionComment = [NSExpression expressionForKeyPath:@"comment"];
    NSPredicateEditorRowTemplate* simpleTextTemplate = [[NSPredicateEditorRowTemplate alloc]
             initWithLeftExpressions:@[ leftExpressionName, leftExpressionComment ]
        rightExpressionAttributeType:NSStringAttributeType
                            modifier:NSDirectPredicateModifier
                           operators:@[ @99, @8, @9, @4, @5, @6 ]
                             options:NSCaseInsensitivePredicateOption | NSDiacriticInsensitivePredicateOption];

    // 3. Template for Any File / Any Tracker
    NSExpression* leftExpressionFiles = [NSExpression expressionForKeyPath:@"flatFileList.name"];
    NSExpression* leftExpressionTrackers = [NSExpression expressionForKeyPath:@"allTrackersFlat"];
    NSPredicateEditorRowTemplate* customTemplate = [[PredicateEditorRowTemplateAny alloc]
             initWithLeftExpressions:@[ leftExpressionFiles, leftExpressionTrackers ]
        rightExpressionAttributeType:NSStringAttributeType
                            modifier:NSDirectPredicateModifier
                           operators:@[ @99, @8, @9, @4, @6 ]
                             options:NSCaseInsensitivePredicateOption];

    self.ruleEditor.rowTemplates = @[ compoundTemplate, simpleTextTemplate, customTemplate ];

    scrollView.documentView = self.ruleEditor;

    NSButton* okButton = [[NSButton alloc] init];
    okButton.translatesAutoresizingMaskIntoConstraints = NO;
    okButton.title = LStr(@"OK");
    okButton.bezelStyle = NSBezelStyleRounded;
    okButton.keyEquivalent = @"\r";
    okButton.target = self;
    okButton.action = @selector(saveRules:);
    [contentView addSubview:okButton];

    NSButton* cancelButton = [[NSButton alloc] init];
    cancelButton.translatesAutoresizingMaskIntoConstraints = NO;
    cancelButton.title = LStr(@"Cancel");
    cancelButton.bezelStyle = NSBezelStyleRounded;
    cancelButton.keyEquivalent = @"\e";
    cancelButton.target = self;
    cancelButton.action = @selector(cancelRules:);
    [contentView addSubview:cancelButton];

    self.ruleEditorHeightConstraint = [scrollView.heightAnchor constraintEqualToConstant:167];

    [NSLayoutConstraint activateConstraints:@[
        [scrollView.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:20],
        [scrollView.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20],
        [scrollView.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],
        [scrollView.bottomAnchor constraintEqualToAnchor:okButton.topAnchor constant:-20],
        [scrollView.widthAnchor constraintGreaterThanOrEqualToConstant:430],
        self.ruleEditorHeightConstraint,

        [self.ruleEditor.topAnchor constraintEqualToAnchor:scrollView.contentView.topAnchor],
        [self.ruleEditor.leadingAnchor constraintEqualToAnchor:scrollView.contentView.leadingAnchor],
        [self.ruleEditor.trailingAnchor constraintEqualToAnchor:scrollView.contentView.trailingAnchor],

        [okButton.trailingAnchor constraintEqualToAnchor:scrollView.trailingAnchor],
        [okButton.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20],
        [okButton.leadingAnchor constraintEqualToAnchor:cancelButton.trailingAnchor constant:12],

        [cancelButton.firstBaselineAnchor constraintEqualToAnchor:okButton.firstBaselineAnchor],
        [cancelButton.widthAnchor constraintEqualToAnchor:okButton.widthAnchor]
    ]];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(ruleEditorRowsDidChange:)
                                               name:NSRuleEditorRowsDidChangeNotification
                                             object:self.ruleEditor];

    [contentView layoutSubtreeIfNeeded];
    self.ruleEditor.objectValue = self.initialPredicate;
    if (self.ruleEditor.numberOfRows == 0) {
        [self.ruleEditor addRow:nil];
    }
}

#pragma mark - Actions

- (void)saveRules:(id)sender
{
    if (self.completionHandler) {
        self.completionHandler(YES, self.ruleEditor.objectValue);
    }
}

- (void)cancelRules:(id)sender
{
    if (self.completionHandler) {
        self.completionHandler(NO, nil);
    }
}

#pragma mark - Notifications

- (void)ruleEditorRowsDidChange:(NSNotification*)notification
{
    NSScrollView* ruleEditorScrollView = self.ruleEditor.enclosingScrollView;
    CGFloat const rowHeight = self.ruleEditor.rowHeight;
    CGFloat const bordersHeight = ruleEditorScrollView.frame.size.height - ruleEditorScrollView.contentSize.height;
    CGFloat const requiredRowCount = self.ruleEditor.numberOfRows;
    CGFloat const maxVisibleRowCount = (long)((NSHeight(self.window.screen.visibleFrame) * 2 / 3) / rowHeight);

    self.ruleEditorHeightConstraint.constant = MIN(requiredRowCount, maxVisibleRowCount) * rowHeight + bordersHeight;
    ruleEditorScrollView.hasVerticalScroller = requiredRowCount > maxVisibleRowCount;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

#pragma mark - Generate Rules

/// Consider _(val) as localization function
/// Then, you need to configure special key value pair.
/// key contains raw values of predicates or keypaths.
/// value contains formatted string with includes localized strings.
///
/// Example:
///
/// - [Any, None, All] of the following are true
///
/// This predicate is expanded into three strings as you see.
///
/// - Any of the following are true
/// - None of the following are true
/// - All of the following are true
///
/// Localized versions will be:
///
/// - _(Any) _(of the following are true)
/// - _(None) _(of the following are true)
/// - _(All) _(of the following are true)
///
/// Result localized string will be
///
/// - [_(Any), _(None), _(All)] of the following are true
///
/// Key in rules dictionary will be:
///
/// - %[Any, None, All]@ of the following are true
///
/// Value in rules dictionary will be:
///
/// - %%1$[_(Any), _(None), _(All)]@ _(of the following are true)
///
/// Note this double percent (%%) interprets as raw "%".
/// 1$ is a placed parameter.
///
///
/// Complex example:
///
/// Consider you have keyPaths (name, comment) and operators (contains and matches).
///
/// For that you have a string
///
/// - [name, comment] [contains, matches] %@
///
/// As you see this string contains %@ because this string is a "format" or "prefix".
///
/// Result localized string will be
///
/// - [_(name), _(comment)] [_(contains), _(matches)] %@
///
/// Key in rules dictionary will be:
///
/// - %[name, comment]@ %[contains, matches]@ %@
///
/// Value in rules dictionary will be:
///
/// - %%1$[_(name), _(comment)]@ %%2$[_(contains), _(matches)]@ %%3$@
///
- (NSDictionary<NSString*, NSString*>*)rulesDictionary
{
    NSString* localizedQuantorValue = [NSString
        stringWithFormat:@"%%1$[%@, %@, %@]@ %@", LStr(@"Any"), LStr(@"None"), LStr(@"All"), LStr(@"of the following are true")];

    NSString* localizedKeyPathValue = [NSString stringWithFormat:@"%%1$[%@, %@, %@, %@]@ %%2$[%@, %@, %@, %@, %@, %@]@ %%3$@",
                                                                 LStr(@"Name"),
                                                                 LStr(@"Comment"),
                                                                 LStr(@"Any File"),
                                                                 LStr(@"Any Tracker"),

                                                                 LStr(@"is"),
                                                                 LStr(@"is not"),
                                                                 LStr(@"matches"),
                                                                 LStr(@"contains"),
                                                                 LStr(@"begins with"),
                                                                 LStr(@"ends with")];

    // Consider _(val) as localization function
    NSDictionary* dictionary = @{
        // %[Quantor] of the following are true = "%1$[_(Quantor)]@ _(of the following are true)"
        @"%[Any, None, All]@ of the following are true" : localizedQuantorValue,

        // "%[keyPath]@ %[op]@ %@" = "%1$[_(keyPath)]@ %2$[_(op)]@ %3$@";
        @"%[name, comment, flatFileList.name, allTrackersFlat]@ %[is, is not, matches, contains, begins with, ends with]@ %@" : localizedKeyPathValue
    };

    return dictionary;
}

@end
