// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupsController.h"
#import "NSImageAdditions.h"
#import "NSMutableArrayAdditions.h"

static CGFloat const kIconWidth = 16.0;
static CGFloat const kBorderWidth = 1.25;
static CGFloat const kIconWidthSmall = 12.0;

@interface TRGroup : NSObject<NSSecureCoding>

@property(nonatomic, assign) NSInteger groupIndex;
@property(nonatomic, copy, null_resettable) NSString* name;
@property(nonatomic, strong, null_resettable) NSColor* color;

/** Runtime-only cache for the rendered group icon. Ignored during serialization. */
@property(nonatomic, strong, nullable) NSImage* icon;

@property(nonatomic, assign) BOOL usesCustomDownloadLocation;
@property(nonatomic, copy, nullable) NSString* customDownloadLocation;

@property(nonatomic, assign) BOOL usesAutoGroupRules;
@property(nonatomic, strong, nullable) NSPredicate* autoGroupRules;

- (instancetype)initWithIndex:(NSInteger)index name:(nullable NSString*)name color:(nullable NSColor*)color;

@end

@implementation TRGroup

- (void)setName:(nullable NSString*)name
{
    _name = name ? [name copy] : @"";
}

- (void)setColor:(nullable NSColor*)color
{
    _color = color ?: NSColor.systemGrayColor;
}

+ (BOOL)supportsSecureCoding
{
    return YES;
}

- (instancetype)initWithIndex:(NSInteger)index name:(nullable NSString*)name color:(nullable NSColor*)color
{
    if ((self = [super init])) {
        _groupIndex = index;
        self.name = name;
        self.color = color;
    }
    return self;
}

#pragma mark - NSSecureCoding

- (void)encodeWithCoder:(NSCoder*)coder
{
    [coder encodeInteger:self.groupIndex forKey:@"Index"];
    [coder encodeObject:self.name forKey:@"Name"];
    [coder encodeObject:self.color forKey:@"Color"];
    [coder encodeBool:self.usesCustomDownloadLocation forKey:@"UsesCustomDownloadLocation"];
    [coder encodeObject:self.customDownloadLocation forKey:@"CustomDownloadLocation"];
    [coder encodeBool:self.usesAutoGroupRules forKey:@"UsesAutoGroupRules"];
    [coder encodeObject:self.autoGroupRules forKey:@"AutoGroupRules"];
}

- (instancetype)initWithCoder:(NSCoder*)coder
{
    auto groupIndex = [coder decodeIntegerForKey:@"Index"];
    auto name = (NSString*)[coder decodeObjectOfClass:NSString.class forKey:@"Name"];
    auto color = (NSColor*)[coder decodeObjectOfClass:NSColor.class forKey:@"Color"];

    if ((self = [self initWithIndex:groupIndex name:name color:color])) {
        _usesCustomDownloadLocation = [coder decodeBoolForKey:@"UsesCustomDownloadLocation"];
        _customDownloadLocation = [coder decodeObjectOfClass:NSString.class forKey:@"CustomDownloadLocation"];
        _usesAutoGroupRules = [coder decodeBoolForKey:@"UsesAutoGroupRules"];
        _autoGroupRules = [coder decodeObjectOfClass:NSPredicate.class forKey:@"AutoGroupRules"];
    }
    return self;
}

@end

@interface GroupsController ()

/** Backing array storing strongly-typed TRGroup model objects. */
@property(nonatomic, strong) NSMutableArray<TRGroup*>* fGroups;

/** Fast lookup map (Group ID -> Array Index) providing O(1) row queries. */
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, NSNumber*>* fIndexMap;

- (nullable TRGroup*)groupForIndex:(NSInteger)index;
- (void)rebuildIndexMap;

- (void)saveGroups;
- (void)saveGroupsAndNotify;

- (BOOL)torrent:(Torrent*)torrent doesMatchRulesForGroup:(TRGroup*)group;

- (nonnull NSImage*)imageForGroupNone;
- (nonnull NSImage*)imageForGroup:(TRGroup*)group;

@end

@implementation GroupsController

+ (GroupsController*)groups
{
    static GroupsController* fGroupsInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        fGroupsInstance = [[GroupsController alloc] init];
    });
    return fGroupsInstance;
}

- (instancetype)init
{
    if ((self = [super init])) {
        _fIndexMap = [[NSMutableDictionary alloc] init];
        NSData* data;

        // 1. Try to load modern strongly-typed TRGroup objects
        if ((data = [NSUserDefaults.standardUserDefaults dataForKey:@"GroupObjects"])) {
            _fGroups = [NSKeyedUnarchiver
                unarchivedObjectOfClasses:[NSSet setWithObjects:NSMutableArray.class, TRGroup.class, NSColor.class, NSPredicate.class, nil]
                                 fromData:data
                                    error:nil];
        }
        // 2. Backward compatibility: Migrate old untyped dictionaries to TRGroup objects
        else if ((data = [NSUserDefaults.standardUserDefaults dataForKey:@"GroupDicts"])) {
            NSError* error;
            NSArray* oldDicts = [NSKeyedUnarchiver unarchivedObjectOfClasses:[NSSet setWithObjects:NSMutableArray.class,
                                                                                                   NSMutableDictionary.class,
                                                                                                   NSNumber.class,
                                                                                                   NSColor.class,
                                                                                                   NSString.class,
                                                                                                   NSPredicate.class,
                                                                                                   nil]
                                                                    fromData:data
                                                                       error:&error];

            if (error != nil) {
                NSLog(@"Got error in groups decoding: %@", error);
            }

            if (oldDicts != nil) {
                _fGroups = [[NSMutableArray alloc] init];
                for (NSDictionary* dict in oldDicts) {
                    TRGroup* g = [[TRGroup alloc] initWithIndex:[dict[@"Index"] integerValue] name:dict[@"Name"]
                                                          color:dict[@"Color"]];
                    g.usesCustomDownloadLocation = [dict[@"UsesCustomDownloadLocation"] boolValue];
                    g.customDownloadLocation = dict[@"CustomDownloadLocation"];
                    g.usesAutoGroupRules = [dict[@"UsesAutoGroupRules"] boolValue];
                    g.autoGroupRules = dict[@"AutoGroupRules"];
                    [_fGroups addObject:g];
                }
                [self saveGroups];
                [NSUserDefaults.standardUserDefaults removeObjectForKey:@"GroupDicts"];
            }
        }

        // 3. Fallback: Initialize with default color-coded groups if no saved data exists
        if (_fGroups == nil) {
            _fGroups = [[NSMutableArray alloc]
                initWithObjects:[[TRGroup alloc] initWithIndex:0 name:NSLocalizedString(@"Red", "Groups -> Name")
                                                         color:NSColor.systemRedColor],
                                [[TRGroup alloc] initWithIndex:1 name:NSLocalizedString(@"Orange", "Groups -> Name")
                                                         color:NSColor.systemOrangeColor],
                                [[TRGroup alloc] initWithIndex:2 name:NSLocalizedString(@"Yellow", "Groups -> Name")
                                                         color:NSColor.systemYellowColor],
                                [[TRGroup alloc] initWithIndex:3 name:NSLocalizedString(@"Green", "Groups -> Name")
                                                         color:NSColor.systemGreenColor],
                                [[TRGroup alloc] initWithIndex:4 name:NSLocalizedString(@"Blue", "Groups -> Name")
                                                         color:NSColor.systemBlueColor],
                                [[TRGroup alloc] initWithIndex:5 name:NSLocalizedString(@"Purple", "Groups -> Name")
                                                         color:NSColor.systemPurpleColor],
                                [[TRGroup alloc] initWithIndex:6 name:NSLocalizedString(@"Gray", "Groups -> Name")
                                                         color:NSColor.systemGrayColor],
                                nil];
            [self saveGroups];
        }

        [self rebuildIndexMap];
    }
    return self;
}

/** Rebuilds the O(1) hash map cache. Must be called whenever the array mutates or reorders. */
- (void)rebuildIndexMap
{
    [self.fIndexMap removeAllObjects];
    for (NSUInteger i = 0; i < self.fGroups.count; i++) {
        self.fIndexMap[@(self.fGroups[i].groupIndex)] = @(i);
    }
}

- (void)saveGroups
{
    NSError* error = nil;
    // Runtime-only .icon property is naturally skipped since it's omitted in TRGroup's encodeWithCoder
    NSData* data = [NSKeyedArchiver archivedDataWithRootObject:self.fGroups requiringSecureCoding:YES error:&error];
    if (data && !error) {
        [NSUserDefaults.standardUserDefaults setObject:data forKey:@"GroupObjects"];
    } else {
        NSLog(@"[GroupsController] Serialization failed: %@", error.localizedDescription);
    }
}

- (void)saveGroupsAndNotify
{
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGroups" object:self];
    [self saveGroups];
}

#pragma mark - Data Accessors

- (NSInteger)numberOfGroups
{
    return self.fGroups.count;
}

/** Resolves table row index from a unique Group ID. Optimized to O(1) via hash map. */
- (NSInteger)rowValueForIndex:(NSInteger)index
{
    if (index == -1 || index == NSNotFound)
        return -1;
    NSNumber* row = self.fIndexMap[@(index)];
    return row ? [row integerValue] : -1;
}

- (NSInteger)indexForRow:(NSInteger)row
{
    return self.fGroups[row].groupIndex;
}

- (nullable TRGroup*)groupForIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    return orderIndex != -1 ? self.fGroups[orderIndex] : nil;
}

- (NSString*)nameForIndex:(NSInteger)index
{
    return [self groupForIndex:index].name;
}

- (void)setName:(nullable NSString*)name forIndex:(NSInteger)index
{
    TRGroup* group = [self groupForIndex:index];

    if (group == nil) {
        return;
    }

    group.name = name;
    [self saveGroupsAndNotify];
}

- (NSColor*)colorForIndex:(NSInteger)index
{
    return [self groupForIndex:index].color;
}

- (void)setColor:(nullable NSColor*)color forIndex:(NSInteger)index
{
    TRGroup* group = [self groupForIndex:index];

    if (group == nil) {
        return;
    }

    group.icon = nil; // Invalidate cached icon to force re-render with the new color
    group.color = color;
    [self saveGroupsAndNotify];
}

#pragma mark - Download Location Management

- (BOOL)usesCustomDownloadLocationForIndex:(NSInteger)index
{
    if (![self customDownloadLocationForIndex:index])
        return NO;

    return [self groupForIndex:index].usesCustomDownloadLocation;
}

- (void)setUsesCustomDownloadLocation:(BOOL)useCustomLocation forIndex:(NSInteger)index
{
    TRGroup* group = [self groupForIndex:index];

    if (group == nil) {
        return;
    }

    group.usesCustomDownloadLocation = useCustomLocation;
    [self saveGroups];
}

- (NSString*)customDownloadLocationForIndex:(NSInteger)index
{
    return [self groupForIndex:index].customDownloadLocation;
}

- (void)setCustomDownloadLocation:(NSString*)location forIndex:(NSInteger)index
{
    TRGroup* group = [self groupForIndex:index];

    if (group == nil) {
        return;
    }

    group.customDownloadLocation = location;
    [self saveGroups];
}

#pragma mark - Smart Rules (NSPredicate Matching)

- (BOOL)usesAutoAssignRulesForIndex:(NSInteger)index
{
    return [self groupForIndex:index].usesAutoGroupRules;
}

- (void)setUsesAutoAssignRules:(BOOL)useAutoAssignRules forIndex:(NSInteger)index
{
    TRGroup* group = [self groupForIndex:index];

    if (group == nil) {
        return;
    }

    group.usesAutoGroupRules = useAutoAssignRules;
    [self saveGroups];
}

- (NSPredicate*)autoAssignRulesForIndex:(NSInteger)index
{
    return [self groupForIndex:index].autoGroupRules;
}

- (void)setAutoAssignRules:(NSPredicate*)predicate forIndex:(NSInteger)index
{
    TRGroup* group = [self groupForIndex:index];

    if (group == nil) {
        return;
    }

    if (predicate != nil) {
        group.autoGroupRules = predicate;
        [self saveGroups];
    } else {
        group.autoGroupRules = nil;
        [self setUsesAutoAssignRules:NO forIndex:index];
    }
}

#pragma mark - Group Management Mutators

- (void)addNewGroup
{
    // Find the lowest available unique Group ID index
    NSMutableIndexSet* candidates = [NSMutableIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fGroups.count + 1)];
    for (TRGroup* group in self.fGroups) {
        [candidates removeIndex:group.groupIndex];
    }

    NSInteger const index = candidates.firstIndex;
    NSColor* defaultBlue = [NSColor colorWithCalibratedRed:0.0 green:0.65 blue:1.0 alpha:1.0];

    TRGroup* newGroup = [[TRGroup alloc] initWithIndex:index name:@"" color:defaultBlue];
    [self.fGroups addObject:newGroup];

    [self rebuildIndexMap];

    [self saveGroupsAndNotify];
}

- (void)removeGroupWithRowIndex:(NSInteger)row
{
    if (row < 0 || row >= self.fGroups.count) {
        return;
    }

    NSInteger index = self.fGroups[row].groupIndex;
    [self.fGroups removeObjectAtIndex:row];
    [self rebuildIndexMap];

    [NSNotificationCenter.defaultCenter postNotificationName:@"GroupValueRemoved" object:self
                                                    userInfo:@{ @"Index" : @(index) }];

    // Reset UI group filter if the currently filtered group was deleted
    if (index == [NSUserDefaults.standardUserDefaults integerForKey:@"FilterGroup"]) {
        [NSUserDefaults.standardUserDefaults setInteger:-2 forKey:@"FilterGroup"];
    }

    [self saveGroupsAndNotify];
}

- (void)moveGroupAtRow:(NSInteger)oldRow toRow:(NSInteger)newRow
{
    if (oldRow < 0 || oldRow >= self.fGroups.count || newRow < 0 || newRow >= self.fGroups.count) {
        return;
    }

    [self.fGroups moveObjectAtIndex:oldRow toIndex:newRow];
    [self rebuildIndexMap];

    [self saveGroupsAndNotify];
}

#pragma mark - UI Menu & Torrent Matching

- (NSMenu*)groupMenuWithTarget:(id)target action:(SEL)action isSmall:(BOOL)small
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    void (^addItemWithTitleTagIcon)(NSString*, NSInteger, NSImage*) = ^void(NSString* title, NSInteger tag, NSImage* icon) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
        item.target = target;
        item.tag = tag;
        if (small) {
            NSImage* smallIcon = [icon copy];
            smallIcon.size = NSMakeSize(kIconWidthSmall, kIconWidthSmall);
            item.image = smallIcon;
        } else {
            item.image = icon;
        }
        [menu addItem:item];
    };

    // Add the default placeholder item "None" (-1)
    addItemWithTitleTagIcon(NSLocalizedString(@"None", "Groups -> Menu"), -1, [self imageForGroupNone]);

    for (TRGroup* group in self.fGroups) {
        addItemWithTitleTagIcon(group.name, group.groupIndex, [self imageForGroup:group]);
    }

    return menu;
}

- (NSInteger)groupIndexForTorrent:(Torrent*)torrent
{
    // Evaluate smart-rules linearly to find the first matching category
    for (TRGroup* group in self.fGroups) {
        if ([self torrent:torrent doesMatchRulesForGroup:group]) {
            return group.groupIndex;
        }
    }
    return -1;
}

- (BOOL)torrent:(Torrent*)torrent doesMatchRulesForGroup:(TRGroup*)group
{
    if (group.usesAutoGroupRules == NO) {
        return NO;
    }

    NSPredicate* predicate = group.autoGroupRules;
    [predicate allowEvaluation];
    BOOL eval = NO;
    @try {
        eval = [predicate evaluateWithObject:torrent];
    } @catch (NSException* exception) {
        NSLog(@"[GroupsController] Predicate evaluation failed (%@) - %@", predicate, exception);
    } @finally {
        return eval;
    }
}

#pragma mark - Icon Graphics Rendering

- (nonnull NSImage*)imageForGroupNone
{
    static NSImage* icon;
    if (icon)
        return icon;
    icon = [NSImage imageWithSize:NSMakeSize(kIconWidth, kIconWidth) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        rect = NSInsetRect(rect, kBorderWidth / 2, kBorderWidth / 2);
        NSBezierPath* bp = [NSBezierPath bezierPathWithOvalInRect:rect];
        bp.lineWidth = kBorderWidth;
        [NSColor.controlTextColor setStroke];
        [bp stroke];
        return YES;
    }];
    [icon setTemplate:YES];
    return icon;
}

- (nonnull NSImage*)imageForGroup:(TRGroup*)group
{
    if (group.icon) {
        return group.icon;
    }

    // Render the crisp round badge matching the group color and cache it inside the model instance
    NSImage* icon = [NSImage discIconWithColor:group.color insetFactor:0];
    group.icon = icon;

    return icon;
}

- (nonnull NSImage*)imageForIndex:(NSInteger)index
{
    TRGroup* group = [self groupForIndex:index];

    if (group == nil) {
        return [self imageForGroupNone];
    }

    return [self imageForGroup:group];
}

@end
