// Test tool for scripts/hotplug-test.sh (ported from spikes/output). Creates a virtual display that macOS treats like a plugged-in monitor, until killed.
// Killing it is an unplug; starting it again with the same --serial is a replug of the same display.
// Uses CGVirtualDisplay, an undocumented CoreGraphics API (as DeskPad and Chromium's tests do).
// Test tooling only: never ship this in the app, since Apple can change it in any update.
//
//   virtual-display --name "Fake Stage" --serial 2 [--size 1920x1080] [--hz 60]
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <signal.h>

@class CGVirtualDisplay;

@interface CGVirtualDisplayDescriptor : NSObject
@property (retain, nonatomic) dispatch_queue_t queue;
@property (retain, nonatomic) NSString *name;
@property (nonatomic) unsigned int maxPixelsHigh;
@property (nonatomic) unsigned int maxPixelsWide;
@property (nonatomic) CGSize sizeInMillimeters;
@property (nonatomic) unsigned int serialNum;
@property (nonatomic) unsigned int productID;
@property (nonatomic) unsigned int vendorID;
@property (copy, nonatomic) void (^terminationHandler)(id, CGVirtualDisplay *);
@end

@interface CGVirtualDisplayMode : NSObject
- (instancetype)initWithWidth:(unsigned int)width height:(unsigned int)height refreshRate:(double)refreshRate;
@end

@interface CGVirtualDisplaySettings : NSObject
@property (retain, nonatomic) NSArray *modes;
@property (nonatomic) unsigned int hiDPI;
@end

@interface CGVirtualDisplay : NSObject
- (instancetype)initWithDescriptor:(CGVirtualDisplayDescriptor *)descriptor;
- (BOOL)applySettings:(CGVirtualDisplaySettings *)settings;
@property (readonly, nonatomic) unsigned int displayID;
@end

static void quit(int sig) { (void)sig; exit(0); } // releasing the display happens on process exit

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        NSString *name = @"Visualis Virtual Display";
        unsigned serial = 1, width = 1920, height = 1080;
        double hz = 60;
        for (int i = 1; i + 1 < argc; i += 2) {
            NSString *opt = @(argv[i]), *val = @(argv[i + 1]);
            if ([opt isEqualToString:@"--name"]) name = val;
            else if ([opt isEqualToString:@"--serial"]) serial = (unsigned)val.intValue;
            else if ([opt isEqualToString:@"--hz"]) hz = val.doubleValue;
            else if ([opt isEqualToString:@"--size"] && sscanf(argv[i + 1], "%ux%u", &width, &height) == 2) {}
            else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
        }

        CGVirtualDisplayDescriptor *desc = [CGVirtualDisplayDescriptor new];
        desc.queue = dispatch_get_main_queue();
        desc.name = name;
        desc.maxPixelsWide = width;
        desc.maxPixelsHigh = height;
        desc.sizeInMillimeters = CGSizeMake(width * 0.25, height * 0.25); // ~100 dpi
        desc.vendorID = 0x7669; // arbitrary, fixed so the display keeps its identity across runs
        desc.productID = 0x5031;
        desc.serialNum = serial;
        desc.terminationHandler = ^(id sender, CGVirtualDisplay *gone) { (void)sender; (void)gone; fprintf(stderr, "display terminated by system\n"); exit(1); };

        CGVirtualDisplay *display = [[CGVirtualDisplay alloc] initWithDescriptor:desc];
        if (!display) { fprintf(stderr, "CGVirtualDisplay init failed\n"); return 1; }

        CGVirtualDisplaySettings *settings = [CGVirtualDisplaySettings new];
        settings.hiDPI = 0;
        settings.modes = @[[[CGVirtualDisplayMode alloc] initWithWidth:width height:height refreshRate:hz]];
        if (![display applySettings:settings]) { fprintf(stderr, "applySettings failed\n"); return 1; }

        printf("display %u: %s %ux%u @ %.0f Hz serial %u\n", display.displayID, name.UTF8String, width, height, hz, serial);
        fflush(stdout);
        signal(SIGTERM, quit);
        signal(SIGINT, quit);
        dispatch_main();
    }
}
