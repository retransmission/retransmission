// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

NS_ASSUME_NONNULL_BEGIN

@interface UTType (Torrent)
@property(class, readonly, strong, nonnull) UTType* torrent;
+ (UTType*)contentTypeForFilenameExtension:(NSString*)fileExtension isFolder:(BOOL)isFolder;
@end

@interface NSURL (Torrent)
@property(nonatomic, readonly) BOOL isTorrentFile;
@end

@interface DefaultAppHelper : NSObject

- (BOOL)isDefaultForTorrentFiles;
- (void)setDefaultForTorrentFiles:(void (^_Nullable)())completionHandler;

- (BOOL)isDefaultForMagnetURLs;
- (void)setDefaultForMagnetURLs:(void (^_Nullable)())completionHandler;

@end

NS_ASSUME_NONNULL_END
