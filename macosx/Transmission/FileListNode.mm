// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FileListNode.h"

@interface FileListNode () {
  @protected
    uint64_t _size;
    NSIndexSet* _indexesInternal;
}
- (instancetype)initWithName:(NSString*)name
                        path:(NSString*)path
                     torrent:(Torrent*)torrent
                        size:(uint64_t)size
                     indexes:(NSIndexSet*)indexes;
@end

@interface FileListFileNode : FileListNode
- (instancetype)initWithFileName:(NSString*)name
                            path:(NSString*)path
                            size:(uint64_t)size
                           index:(NSUInteger)index
                         torrent:(Torrent*)torrent;
@end

@interface FileListFolderNode : FileListNode {
    NSMutableArray<FileListNode*>* _childrenInternal;
}

- (instancetype)initWithFolderName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent;
@end

@implementation FileListNode

- (NSMutableArray<FileListNode*>*)children
{
    return nil;
}

- (void)insertChild:(FileListNode*)child
{
    [self doesNotRecognizeSelector:_cmd];
}

- (void)insertIndex:(NSUInteger)index withSize:(uint64_t)size
{
    [self doesNotRecognizeSelector:_cmd];
}

- (id)copyWithZone:(NSZone*)zone
{
    //this object is essentially immutable after initial setup
    return self;
}

- (NSString*)description
{
    return @"";
}

- (BOOL)isFolder
{
    return NO;
}

- (NSIndexSet*)indexes
{
    return _indexesInternal;
}

- (BOOL)updateFromOldName:(NSString*)oldName toNewName:(NSString*)newName inPath:(NSString*)path
{
    NSParameterAssert(oldName != nil);
    NSParameterAssert(newName != nil);
    NSParameterAssert(path != nil);

    NSArray* lookupPathComponents = path.pathComponents;
    NSArray* thesePathComponents = self.path.pathComponents;

    if ([lookupPathComponents isEqualToArray:thesePathComponents]) //this node represents what's being renamed
    {
        if ([oldName isEqualToString:self.name]) {
            _name = [newName copy];
            return YES;
        }
    } else if (lookupPathComponents.count < thesePathComponents.count) //what's being renamed is part of this node's path
    {
        lookupPathComponents = [lookupPathComponents arrayByAddingObject:oldName];
        BOOL const allSame = NSNotFound ==
            [lookupPathComponents indexOfObjectWithOptions:NSEnumerationConcurrent
                                               passingTest:^BOOL(NSString* name, NSUInteger idx, BOOL* /*stop*/) {
                                                   return ![name isEqualToString:thesePathComponents[idx]];
                                               }];

        if (allSame) {
            NSString* oldPathPrefix = [path stringByAppendingPathComponent:oldName];
            NSString* newPathPrefix = [path stringByAppendingPathComponent:newName];

            _path = [_path stringByReplacingCharactersInRange:NSMakeRange(0, oldPathPrefix.length) withString:newPathPrefix];
            return YES;
        }
    }

    return NO;
}

#pragma mark - Private

- (instancetype)initWithName:(NSString*)name
                        path:(NSString*)path
                     torrent:(Torrent*)torrent
                        size:(uint64_t)size
                     indexes:(NSIndexSet*)indexes
{
    if ((self = [super init])) {
        _name = [name copy];
        _path = [path copy];
        _torrent = torrent;
        _size = size;
        _indexesInternal = indexes;
    }

    return self;
}

@end

@implementation FileListFileNode

- (instancetype)initWithFileName:(NSString*)name
                            path:(NSString*)path
                            size:(uint64_t)size
                           index:(NSUInteger)index
                         torrent:(Torrent*)torrent
{
    return [self initWithName:name path:path torrent:torrent size:size indexes:[NSIndexSet indexSetWithIndex:index]];
}

- (NSString*)description
{
    return [NSString stringWithFormat:@"%@ (%ld)", self.name, _indexesInternal.firstIndex];
}

@end

@implementation FileListFolderNode

- (instancetype)initWithFolderName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent
{
    if ((self = [super initWithName:name path:path torrent:torrent size:0 indexes:[[NSMutableIndexSet alloc] init]])) {
        _childrenInternal = [[NSMutableArray alloc] init];
    }
    return self;
}

- (BOOL)isFolder
{
    return YES;
}

- (NSMutableArray<FileListNode*>*)children
{
    return _childrenInternal;
}

- (void)insertChild:(FileListNode*)child
{
    [_childrenInternal addObject:child];
}

- (void)insertIndex:(NSUInteger)index withSize:(uint64_t)size
{
    [(NSMutableIndexSet*)_indexesInternal addIndex:index];
    _size += size;
}

- (NSString*)description
{
    return [NSString stringWithFormat:@"%@ (folder: %@)", self.name, _indexesInternal];
}

@end

@implementation FileListNode (Creation)
+ (instancetype)createWithFolderName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent
{
    return [[FileListFolderNode alloc] initWithFolderName:name path:path torrent:torrent];
}

+ (instancetype)createWithFileName:(NSString*)name
                              path:(NSString*)path
                              size:(uint64_t)size
                             index:(NSUInteger)index
                           torrent:(Torrent*)torrent
{
    return [[FileListFileNode alloc] initWithFileName:name path:path size:size index:index torrent:torrent];
}
@end
