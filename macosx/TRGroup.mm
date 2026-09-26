#import "TRGroup.h"

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
