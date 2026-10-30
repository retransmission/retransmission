// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TRGroup.h"

#pragma mark - Auto-assign rules

// Rules are archived as property lists because evaluating a decoded NSPredicate can send any message its expressions
// name. ruleFromPlist() builds only the predicate shapes that GroupRules.xib's editor produces:
//   { Type = Compound; Compound = NOT|AND|OR; Subpredicates = (rule, ...); }  one rule for NOT, one or more for AND, OR
//   { Type = Comparison; KeyPath; Operator; Modifier; Options; Value = string; }
// Transmission 4.1.4 and later save rules in this form too.
// Each conversion visits at most kMaxRuleNodes nodes, which bounds the work a crafted plist can cause.

static NSUInteger const kMaxRuleNodes = 256;

static id _Nullable objectOfClass(id _Nullable object, Class cls)
{
    return [object isKindOfClass:cls] ? object : nil;
}

static BOOL isRuleCompound(NSUInteger compoundType, NSUInteger subruleCount)
{
    switch (compoundType) {
    case NSNotPredicateType:
        return subruleCount == 1;
    case NSAndPredicateType:
    case NSOrPredicateType:
        return subruleCount > 0;
    default:
        return NO;
    }
}

// Accepts the key paths, operators, modifiers, and options of GroupRules.xib's row templates.
static BOOL isRuleComparison(NSString* keyPath, NSUInteger operatorType, NSUInteger modifier, NSUInteger options)
{
    static NSSet<NSString*>* const keyPaths = [NSSet setWithArray:@[
        @"name",
        @"comment",
        @"flatFileList.name",
        @"allTrackersFlat",
    ]];
    if (![keyPaths containsObject:keyPath]) {
        return NO;
    }

    switch (operatorType) {
    case NSEqualToPredicateOperatorType:
    case NSNotEqualToPredicateOperatorType:
    case NSMatchesPredicateOperatorType:
    case NSBeginsWithPredicateOperatorType:
    case NSEndsWithPredicateOperatorType:
    case NSContainsPredicateOperatorType:
        break;
    default:
        return NO;
    }

    NSUInteger const allowedOptions = NSCaseInsensitivePredicateOption | NSDiacriticInsensitivePredicateOption;
    return (modifier == NSDirectPredicateModifier || modifier == NSAnyPredicateModifier) && (options & ~allowedOptions) == 0;
}

// Reads only rule's structure, so it's safe on a predicate decoded straight from an archive.
static NSDictionary* _Nullable plistFromRule(id _Nullable rule, NSUInteger& nodesLeft)
{
    if (nodesLeft == 0) {
        return nil;
    }
    --nodesLeft;

    if ([rule isKindOfClass:NSCompoundPredicate.class]) {
        NSCompoundPredicate* const compound = rule;
        NSMutableArray<NSDictionary*>* const subplists = [NSMutableArray array];
        for (id subrule in compound.subpredicates) {
            NSDictionary* const subplist = plistFromRule(subrule, nodesLeft);
            if (subplist == nil) {
                return nil;
            }
            [subplists addObject:subplist];
        }
        return @{@"Type" : @"Compound", @"Compound" : @(compound.compoundPredicateType), @"Subpredicates" : subplists};
    }

    if ([rule isKindOfClass:NSComparisonPredicate.class]) {
        NSComparisonPredicate* const comparison = rule;
        NSExpression* const left = comparison.leftExpression;
        NSExpression* const right = comparison.rightExpression;
        if (left.expressionType != NSKeyPathExpressionType || left.operand.expressionType != NSEvaluatedObjectExpressionType ||
            right.expressionType != NSConstantValueExpressionType) {
            return nil;
        }

        NSString* const keyPath = objectOfClass(left.keyPath, NSString.class);
        NSString* const value = objectOfClass(right.constantValue, NSString.class);
        if (keyPath == nil || value == nil) {
            return nil;
        }

        return @{
            @"Type" : @"Comparison",
            @"KeyPath" : keyPath,
            @"Operator" : @(comparison.predicateOperatorType),
            @"Modifier" : @(comparison.comparisonPredicateModifier),
            @"Options" : @(comparison.options),
            @"Value" : value,
        };
    }

    return nil;
}

static NSPredicate* _Nullable ruleFromPlist(id _Nullable plist, NSUInteger& nodesLeft)
{
    NSDictionary* const dict = objectOfClass(plist, NSDictionary.class);
    if (dict == nil || nodesLeft == 0) {
        return nil;
    }
    --nodesLeft;

    NSString* const type = objectOfClass(dict[@"Type"], NSString.class);

    if ([type isEqualToString:@"Compound"]) {
        NSNumber* const compoundType = objectOfClass(dict[@"Compound"], NSNumber.class);
        NSArray* const subplists = objectOfClass(dict[@"Subpredicates"], NSArray.class);
        if (compoundType == nil || subplists == nil || !isRuleCompound(compoundType.unsignedIntegerValue, subplists.count)) {
            return nil;
        }

        NSMutableArray<NSPredicate*>* const subrules = [NSMutableArray array];
        for (id subplist in subplists) {
            NSPredicate* const subrule = ruleFromPlist(subplist, nodesLeft);
            if (subrule == nil) {
                return nil;
            }
            [subrules addObject:subrule];
        }
        return [[NSCompoundPredicate alloc] initWithType:(NSCompoundPredicateType)compoundType.unsignedIntegerValue
                                           subpredicates:subrules];
    }

    if ([type isEqualToString:@"Comparison"]) {
        NSString* const keyPath = objectOfClass(dict[@"KeyPath"], NSString.class);
        NSNumber* const operatorType = objectOfClass(dict[@"Operator"], NSNumber.class);
        NSNumber* const modifier = objectOfClass(dict[@"Modifier"], NSNumber.class);
        NSNumber* const options = objectOfClass(dict[@"Options"], NSNumber.class);
        NSString* const value = objectOfClass(dict[@"Value"], NSString.class);
        if (keyPath == nil || operatorType == nil || modifier == nil || options == nil || value == nil ||
            !isRuleComparison(keyPath, operatorType.unsignedIntegerValue, modifier.unsignedIntegerValue, options.unsignedIntegerValue)) {
            return nil;
        }

        return [NSComparisonPredicate predicateWithLeftExpression:[NSExpression expressionForKeyPath:keyPath]
                                                  rightExpression:[NSExpression expressionForConstantValue:value]
                                                         modifier:(NSComparisonPredicateModifier)modifier.unsignedIntegerValue
                                                             type:(NSPredicateOperatorType)operatorType.unsignedIntegerValue
                                                          options:options.unsignedIntegerValue];
    }

    return nil;
}

static NSDictionary* _Nullable plistFromRule(NSPredicate* _Nullable rule)
{
    NSUInteger nodesLeft = kMaxRuleNodes;
    return plistFromRule(rule, nodesLeft);
}

static NSPredicate* _Nullable ruleFromPlist(id _Nullable plist)
{
    NSUInteger nodesLeft = kMaxRuleNodes;
    return ruleFromPlist(plist, nodesLeft);
}

@implementation TRGroup

- (void)setName:(nullable NSString*)name
{
    _name = name ? [name copy] : @"";
}

- (void)setColor:(nullable NSColor*)color
{
    _color = color ?: NSColor.systemGrayColor;
}

- (void)setAutoGroupRules:(nullable NSPredicate*)autoGroupRules
{
    _autoGroupRules = ruleFromPlist(plistFromRule(autoGroupRules));
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

- (nullable instancetype)initWithGroupDictsEntry:(id)entry
{
    NSDictionary* const dict = objectOfClass(entry, NSDictionary.class);
    NSNumber* const index = objectOfClass(dict[@"Index"], NSNumber.class);
    if (index == nil) {
        return nil;
    }

    if ((self = [self initWithIndex:index.integerValue name:objectOfClass(dict[@"Name"], NSString.class)
                              color:objectOfClass(dict[@"Color"], NSColor.class)])) {
        _usesCustomDownloadLocation = [objectOfClass(dict[@"UsesCustomDownloadLocation"], NSNumber.class) boolValue];
        _customDownloadLocation = objectOfClass(dict[@"CustomDownloadLocation"], NSString.class);
        // Transmission 4.1.4 and later save the plist form; earlier versions save the NSPredicate itself.
        if (id const plist = dict[@"AutoGroupRulesSchema"]) {
            _autoGroupRules = ruleFromPlist(plist);
        } else {
            self.autoGroupRules = objectOfClass(dict[@"AutoGroupRules"], NSPredicate.class);
        }
        _usesAutoGroupRules = [objectOfClass(dict[@"UsesAutoGroupRules"], NSNumber.class) boolValue] && _autoGroupRules != nil;
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
    [coder encodeObject:plistFromRule(self.autoGroupRules) forKey:@"AutoGroupRulesSchema"];
}

- (instancetype)initWithCoder:(NSCoder*)coder
{
    auto groupIndex = [coder decodeIntegerForKey:@"Index"];
    auto name = (NSString*)[coder decodeObjectOfClass:NSString.class forKey:@"Name"];
    auto color = (NSColor*)[coder decodeObjectOfClass:NSColor.class forKey:@"Color"];

    if ((self = [self initWithIndex:groupIndex name:name color:color])) {
        _usesCustomDownloadLocation = [coder decodeBoolForKey:@"UsesCustomDownloadLocation"];
        _customDownloadLocation = [coder decodeObjectOfClass:NSString.class forKey:@"CustomDownloadLocation"];
        if ([coder containsValueForKey:@"AutoGroupRulesSchema"]) {
            _autoGroupRules = ruleFromPlist([coder decodePropertyListForKey:@"AutoGroupRulesSchema"]);
        } else {
            // Archives from before AutoGroupRulesSchema hold the NSPredicate itself.
            self.autoGroupRules = [coder decodeObjectOfClass:NSPredicate.class forKey:@"AutoGroupRules"];
        }
        _usesAutoGroupRules = [coder decodeBoolForKey:@"UsesAutoGroupRules"] && _autoGroupRules != nil;
    }
    return self;
}

@end
