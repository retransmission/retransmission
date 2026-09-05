// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#if __has_feature(modules)
@import AppKit;
#else
#import <AppKit/AppKit.h>
#endif

#import "ExpandedPathToIconTransformer.h"
#import "DefaultAppHelper.h"

@implementation ExpandedPathToIconTransformer

+ (Class)transformedValueClass
{
    return [NSImage class];
}

+ (BOOL)allowsReverseTransformation
{
    return NO;
}

- (id)transformedValue:(id)value
{
    if (!value) {
        return nil;
    }

    NSString* path = [value stringByExpandingTildeInPath];
    NSImage* icon;
    if ([NSFileManager.defaultManager fileExistsAtPath:path]) {
        icon = [NSWorkspace.sharedWorkspace iconForFile:path];
    } else {
        //show a folder icon if the path has no extension
        auto isFolder = path.pathExtension.length == 0;
        auto contentType = [UTType contentTypeForFilenameExtension:path.pathExtension isFolder:isFolder];
        icon = [NSWorkspace.sharedWorkspace iconForContentType:contentType];
    }

    icon = [icon copy];
    icon.size = NSMakeSize(16.0, 16.0);

    return icon;
}

@end
