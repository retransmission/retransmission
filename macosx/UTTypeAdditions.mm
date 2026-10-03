// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "UTTypeAdditions.h"

static NSString* const kTorrentFileType = @"org.bittorrent.torrent";

@implementation UTType (Torrent)
+ (UTType*)torrent
{
    static UTType* result = nil;

    static dispatch_once_t once;
    dispatch_once(&once, ^{
        result = [UTType exportedTypeWithIdentifier:kTorrentFileType conformingToType:UTTypeData];
    });

    return result;
}

+ (UTType*)contentTypeForFilenameExtension:(NSString*)fileExtension isFolder:(BOOL)isFolder
{
    if (isFolder) {
        return UTTypeFolder;
    }

    UTType* fileType = nil;
    if (fileExtension.length > 0) {
        fileType = [UTType typeWithFilenameExtension:fileExtension];
    }

    return fileType ?: UTTypeData;
}

+ (BOOL)isTorrentResponseWithMIMEType:(nullable NSString*)mimeType suggestedFilename:(nullable NSString*)suggestedFilename
{
    UTType* contentType = mimeType.length > 0 ? [UTType typeWithMIMEType:mimeType] : nil;
    NSString* suggestedExtension = suggestedFilename.pathExtension;
    UTType* fileType = suggestedExtension.length > 0 ? [UTType typeWithFilenameExtension:suggestedExtension] : nil;
    UTType* torrentType = UTType.torrent;

    return [suggestedExtension.lowercaseString isEqualToString:@"torrent"] || [contentType conformsToType:torrentType] ||
        [fileType conformsToType:torrentType];
}
@end
