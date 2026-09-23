// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#include <libtransmission/transmission.h>

@interface CreatorWindowController : NSWindowController

+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle;
+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle forFile:(NSURL*)file;

- (instancetype)initWithHandle:(tr_session*)handle path:(NSURL*)path;

- (void)copy:(id)sender;
- (void)paste:(id)sender;

@end
