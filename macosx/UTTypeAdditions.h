#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

NS_ASSUME_NONNULL_BEGIN

@interface UTType (Torrent)
@property(class, readonly, strong, nonnull) UTType* torrent;
+ (UTType*)contentTypeForFilenameExtension:(NSString*)fileExtension isFolder:(BOOL)isFolder;

+ (BOOL)isTorrentResponseWithMIMEType:(nullable NSString *)mimeType
                    suggestedFilename:(nullable NSString *)suggestedFilename;
@end

NS_ASSUME_NONNULL_END
