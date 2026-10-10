// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

NS_ASSUME_NONNULL_BEGIN

@interface UTType (UTTypeAdditions)
@property(class, readonly, strong, nonnull) UTType* torrent;
+ (UTType*)contentTypeForFilenameExtension:(NSString*)fileExtension isFolder:(BOOL)isFolder;

+ (BOOL)isTorrentResponseWithMIMEType:(nullable NSString*)mimeType suggestedFilename:(nullable NSString*)suggestedFilename;
@end

NS_ASSUME_NONNULL_END
