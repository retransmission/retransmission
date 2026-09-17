#include "libtransmission/macros.h"

#import "BonjourController.h"
#import <Network/Network.h>
#import <SystemConfiguration/SystemConfiguration.h>

static NSUInteger const kBonjourServiceNameMaxLength = 63;

@interface BonjourController ()

@property(nonatomic, strong) nw_listener_t fListener;

@end

@implementation BonjourController

- (void)startWithPort:(int)port
{
    [self stop];

    // 1. Get the localized computer name (Computer Name) instead of using NSHost
    NSString* computerName = nil;
    CFStringRef computerNameRef = SCDynamicStoreCopyComputerName(NULL, NULL);
    if (computerNameRef != NULL) {
        computerName = CFBridgingRelease(computerNameRef);
    } else {
        computerName = [[NSProcessInfo processInfo] hostName];
    }

    NSMutableString* serviceName = [NSMutableString stringWithFormat:@TR_PROJ_APPNAME_CAPITALIZED " (%@ - %@)", NSUserName(), computerName];

    if (serviceName.length > kBonjourServiceNameMaxLength) {
        [serviceName deleteCharactersInRange:NSMakeRange(kBonjourServiceNameMaxLength, serviceName.length - kBonjourServiceNameMaxLength)];
    }

    // 2. Configure parameters for standard unencrypted TCP (HTTP)
    // The first argument disables TLS; the second applies default TCP configurations
    nw_parameters_t parameters = nw_parameters_create_secure_tcp(NW_PARAMETERS_DISABLE_PROTOCOL, NW_PARAMETERS_DEFAULT_CONFIGURATION);

    // 3. Format the port into a string (6-byte buffer fits ports up to 65535)
    char portString[6];
    snprintf(portString, sizeof(portString), "%d", port);

    // Create a network listener on the specified port
    self.fListener = nw_listener_create_with_port(portString, parameters);
    if (self.fListener == NULL) {
        NSLog(@"Failed to create network listener on port %s", portString);
        return;
    }

    // 4. Configure the Bonjour advertisement descriptor (without trailing dots for type and domain)
    nw_advertise_descriptor_t advertiseDescriptor = nw_advertise_descriptor_create_bonjour_service([serviceName UTF8String], "_http._tcp", "local");

    nw_listener_set_advertise_descriptor(self.fListener, advertiseDescriptor);
    nw_listener_set_queue(self.fListener, dispatch_get_main_queue());

    // 5. Set up the listener state change handler
    nw_listener_set_state_changed_handler(self.fListener, ^(nw_listener_state_t state, nw_error_t error) {
        switch (state) {
        case nw_listener_state_ready:
            // Service successfully published to the Bonjour network
            break;

        case nw_listener_state_failed:
            {
                int errorCode = error ? nw_error_get_error_code(error) : 0;
                NSLog(@"Network listener failed on port %d with error code: %d", port, errorCode);
                break;
            }

        case nw_listener_state_cancelled:
            break;

        case nw_listener_state_waiting:
            break;

        case nw_listener_state_invalid:
        default:
            break;
        }
    });

    // 6. Handle incoming connections
    nw_listener_set_new_connection_handler(self.fListener, ^(nw_connection_t connection) {
        // Since libtransmission's built-in HTTP server handles the actual traffic,
        // we cancel the system connection here. This listener is used only for Bonjour advertisement.
        nw_connection_cancel(connection);
    });

    // 7. Start the listener
    nw_listener_start(self.fListener);
}

- (void)stop
{
    if (self.fListener != NULL) {
        nw_listener_cancel(self.fListener);
        self.fListener = NULL;
    }
}

@end
