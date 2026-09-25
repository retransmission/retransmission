#import "UTTypeAdditions.h"
#import "NSURLAdditions.h"

@implementation NSURL (Torrent)
- (BOOL)isTorrentFile
{
    UTType* contentType = nil;

    if ([self getResourceValue:&contentType forKey:NSURLContentTypeKey error:NULL] && contentType &&
        [contentType conformsToType:UTType.torrent]) {
        return YES;
    }

    // LaunchServices resolves .torrent to another declared type when a different
    // client owns it, and to a dynamic UTI when nothing is registered yet,
    // so the extension stays authoritative.
    return [self.pathExtension caseInsensitiveCompare:@"torrent"] == NSOrderedSame;
}
@end
