// P2 MIDI spike (plan 6.3): what arrives, how fast, in what order, and what happens when the
// sending app restarts. CoreMIDI directly, MIDI 1.0 protocol via Universal MIDI Packets.
//
//   midi-spike list                          sources and destinations
//   midi-spike listen [--seconds N]          virtual destination "Visualis Cues" plus every source;
//                                            logs each message (CSV on stdout); follows sources that
//                                            appear and disappear
//   midi-spike send <destination|--virtual> [--count N] [--interval-ms N]
//                                            sends a numbered Note On pattern: to a named destination
//                                            (e.g. "IAC Driver Bus 1", "Visualis Cues"), or from a
//                                            virtual source "Visualis Test Sender"
//   midi-spike loopback [--count N] [--interval-ms N] [--via <IAC bus name>]
//                                            sends to itself and reports latency and ordering
#import <CoreMIDI/CoreMIDI.h>
#import <Foundation/Foundation.h>
#include <mach/mach_time.h>

static mach_timebase_info_data_t tb;
static double ms(uint64_t ticks) { return (double)ticks * tb.numer / tb.denom / 1e6; }
static uint64_t start_ticks;

static NSString *name_of(MIDIObjectRef obj)
{
    CFStringRef s = NULL;
    if (MIDIObjectGetStringProperty(obj, kMIDIPropertyDisplayName, &s) != noErr || !s)
        MIDIObjectGetStringProperty(obj, kMIDIPropertyName, &s);
    return s ? CFBridgingRelease(s) : @"?";
}

static SInt32 unique_id(MIDIObjectRef obj)
{
    SInt32 id = 0;
    MIDIObjectGetIntegerProperty(obj, kMIDIPropertyUniqueID, &id);
    return id;
}

static void check(OSStatus err, const char *what)
{
    if (err != noErr) {
        fprintf(stderr, "%s failed: %d\n", what, (int)err);
        exit(1);
    }
}

// MIDI 1.0 channel voice message as a 32-bit UMP word: type 2, group 0.
static UInt32 ump(UInt8 status, UInt8 d1, UInt8 d2) { return 0x20000000u | (status << 16) | (d1 << 8) | d2; }

static NSString *describe(UInt32 w)
{
    UInt8 type = w >> 28, status = (w >> 16) & 0xFF, d1 = (w >> 8) & 0x7F, d2 = w & 0x7F;
    if (type != 2)
        return [NSString stringWithFormat:@"ump type %u 0x%08x", type, w];
    int ch = (status & 0x0F) + 1;
    switch (status & 0xF0) {
    case 0x90: return d2 ? [NSString stringWithFormat:@"note_on,%d,%u,%u", ch, d1, d2]
                         : [NSString stringWithFormat:@"note_off,%d,%u,0", ch, d1];
    case 0x80: return [NSString stringWithFormat:@"note_off,%d,%u,%u", ch, d1, d2];
    case 0xB0: return [NSString stringWithFormat:@"cc,%d,%u,%u", ch, d1, d2];
    case 0xC0: return [NSString stringWithFormat:@"program_change,%d,%u,", ch, d1];
    default:   return [NSString stringWithFormat:@"status_0x%02x,%d,%u,%u", status, ch, d1, d2];
    }
}

static void list(void)
{
    printf("sources (things that send to us):\n");
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); i++) {
        MIDIEndpointRef e = MIDIGetSource(i);
        SInt32 offline = 0;
        MIDIObjectGetIntegerProperty(e, kMIDIPropertyOffline, &offline);
        printf("  %-40s id=%d%s\n", name_of(e).UTF8String, unique_id(e), offline ? " (offline)" : "");
    }
    printf("destinations (things we can send to):\n");
    for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); i++) {
        MIDIEndpointRef e = MIDIGetDestination(i);
        printf("  %-40s id=%d\n", name_of(e).UTF8String, unique_id(e));
    }
}

// --- listen ----------------------------------------------------------------------------------

static MIDIPortRef in_port;
static NSMutableDictionary<NSNumber *, NSString *> *connected; // unique id -> name
static MIDIEndpointRef own_virtual_dest;

static void log_event(const char *kind, NSString *detail)
{
    printf("%.3f,%s,%s\n", ms(mach_absolute_time() - start_ticks), kind, detail.UTF8String);
    fflush(stdout);
}

static void connect_all_sources(void)
{
    NSMutableSet *present = [NSMutableSet set];
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); i++) {
        MIDIEndpointRef src = MIDIGetSource(i);
        NSNumber *uid = @(unique_id(src));
        [present addObject:uid];
        if (connected[uid])
            continue;
        if (MIDIPortConnectSource(in_port, src, (void *)(intptr_t)uid.intValue) == noErr) {
            connected[uid] = name_of(src);
            log_event("connected", connected[uid]);
        }
    }
    for (NSNumber *uid in connected.allKeys) {
        if (![present containsObject:uid]) {
            log_event("disconnected", connected[uid]);
            [connected removeObjectForKey:uid];
        }
    }
}

static void handle_events(const MIDIEventList *list, NSString *from)
{
    uint64_t now = mach_absolute_time();
    const MIDIEventPacket *p = &list->packet[0];
    for (UInt32 i = 0; i < list->numPackets; i++) {
        // timeStamp 0 means "now"; otherwise it's the sender's host time for the event.
        double age = p->timeStamp && p->timeStamp <= now ? ms(now - p->timeStamp) : -1;
        for (UInt32 w = 0; w < p->wordCount; w++)
            printf("%.3f,message,%s,%s,%.3f\n", ms(now - start_ticks), from.UTF8String,
                   describe(p->words[w]).UTF8String, age);
        p = MIDIEventPacketNext(p);
    }
    fflush(stdout);
}

static void listen_for(double seconds)
{
    MIDIClientRef client;
    connected = [NSMutableDictionary dictionary];
    check(MIDIClientCreateWithBlock(CFSTR("Visualis MIDI spike"), &client, ^(const MIDINotification *n) {
        if (n->messageID == kMIDIMsgSetupChanged)
            connect_all_sources();
    }), "MIDIClientCreate");
    check(MIDIInputPortCreateWithProtocol(client, CFSTR("in"), kMIDIProtocol_1_0, &in_port,
                                          ^(const MIDIEventList *list, void *ref) {
        NSString *from = connected[@((int)(intptr_t)ref)] ?: @"?";
        handle_events(list, from);
    }), "MIDIInputPortCreate");
    check(MIDIDestinationCreateWithProtocol(client, CFSTR("Visualis Cues"), kMIDIProtocol_1_0, &own_virtual_dest,
                                            ^(const MIDIEventList *list, void *ref) {
        handle_events(list, @"Visualis Cues (virtual destination)");
    }), "MIDIDestinationCreate");
    printf("time_ms,event,source,message,channel,data1,data2,age_ms\n");
    log_event("listening", @"virtual destination 'Visualis Cues' plus all sources");
    connect_all_sources();
    if (seconds > 0)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
    else
        [[NSRunLoop currentRunLoop] run];
}

// --- send ------------------------------------------------------------------------------------

static MIDIEndpointRef find_destination(NSString *name)
{
    for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); i++) {
        MIDIEndpointRef e = MIDIGetDestination(i);
        if ([name_of(e) isEqualToString:name] || [name_of(e) hasPrefix:name])
            return e;
    }
    return 0;
}

static MIDIEndpointRef find_source(NSString *name)
{
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); i++) {
        MIDIEndpointRef e = MIDIGetSource(i);
        if ([name_of(e) isEqualToString:name] || [name_of(e) hasPrefix:name])
            return e;
    }
    return 0;
}

// Sends one message, stamped with the current host time so the receiver can measure delivery.
static void send_one(MIDIPortRef port, MIDIEndpointRef dest, MIDIEndpointRef virt, UInt32 word)
{
    MIDIEventList list;
    MIDIEventPacket *p = MIDIEventListInit(&list, kMIDIProtocol_1_0);
    p = MIDIEventListAdd(&list, sizeof(list), p, mach_absolute_time(), 1, &word);
    if (virt)
        MIDIReceivedEventList(virt, &list);
    else
        MIDISendEventList(port, dest, &list);
}

static void send_pattern(NSString *target, int count, double interval_ms)
{
    MIDIClientRef client;
    MIDIPortRef out = 0;
    MIDIEndpointRef dest = 0, virt = 0;
    check(MIDIClientCreateWithBlock(CFSTR("Visualis MIDI sender"), &client, NULL), "MIDIClientCreate");
    if ([target isEqualToString:@"--virtual"]) {
        check(MIDISourceCreateWithProtocol(client, CFSTR("Visualis Test Sender"), kMIDIProtocol_1_0, &virt),
              "MIDISourceCreate");
        [NSThread sleepForTimeInterval:0.5]; // let listeners see the new source and connect
    } else {
        check(MIDIOutputPortCreate(client, CFSTR("out"), &out), "MIDIOutputPortCreate");
        dest = find_destination(target);
        if (!dest) {
            fprintf(stderr, "no destination named '%s' (try: midi-spike list)\n", target.UTF8String);
            exit(2);
        }
    }
    for (int i = 0; i < count; i++) {
        // Note number counts up so the receiver can check order: 0..127 then wraps, channel 16.
        send_one(out, dest, virt, ump(0x9F, i % 128, 100));
        if (interval_ms > 0)
            [NSThread sleepForTimeInterval:interval_ms / 1000.0];
    }
    [NSThread sleepForTimeInterval:0.2];
    printf("sent %d notes to %s\n", count, virt ? "virtual source 'Visualis Test Sender'" : name_of(dest).UTF8String);
}

// --- loopback --------------------------------------------------------------------------------

static void loopback(int count, double interval_ms, NSString *via)
{
    MIDIClientRef client;
    MIDIPortRef in, out = 0;
    MIDIEndpointRef virt = 0, dest = 0, src = 0;
    NSMutableArray<NSNumber *> *lat = [NSMutableArray array];
    NSMutableArray<NSNumber *> *notes = [NSMutableArray array];
    check(MIDIClientCreateWithBlock(CFSTR("Visualis MIDI loopback"), &client, NULL), "MIDIClientCreate");
    check(MIDIInputPortCreateWithProtocol(client, CFSTR("in"), kMIDIProtocol_1_0, &in,
                                          ^(const MIDIEventList *list, void *ref) {
        uint64_t now = mach_absolute_time();
        const MIDIEventPacket *p = &list->packet[0];
        for (UInt32 i = 0; i < list->numPackets; i++) {
            for (UInt32 w = 0; w < p->wordCount; w++) {
                if ((p->words[w] >> 28) != 2)
                    continue;
                @synchronized (lat) {
                    [lat addObject:@(p->timeStamp ? ms(now - p->timeStamp) : -1)];
                    [notes addObject:@((p->words[w] >> 8) & 0x7F)];
                }
            }
            p = MIDIEventPacketNext(p);
        }
    }), "MIDIInputPortCreate");

    if (via) { // out through a real driver bus (IAC), back in from the same bus
        check(MIDIOutputPortCreate(client, CFSTR("out"), &out), "MIDIOutputPortCreate");
        dest = find_destination(via);
        src = find_source(via);
        if (!dest || !src) {
            fprintf(stderr, "no IAC bus named '%s'. Turn on the IAC Driver in Audio MIDI Setup (see README).\n",
                    via.UTF8String);
            exit(2);
        }
    } else {   // a virtual source in this process, listened to by our own input port
        check(MIDISourceCreateWithProtocol(client, CFSTR("Visualis Loopback"), kMIDIProtocol_1_0, &virt),
              "MIDISourceCreate");
        src = virt;
    }
    check(MIDIPortConnectSource(in, src, NULL), "MIDIPortConnectSource");
    [NSThread sleepForTimeInterval:0.3];

    for (int i = 0; i < count; i++) {
        send_one(out, dest, virt, ump(0x9F, i % 128, 100));
        if (interval_ms > 0)
            [NSThread sleepForTimeInterval:interval_ms / 1000.0];
    }
    [NSThread sleepForTimeInterval:0.5];

    NSArray *sorted;
    NSUInteger received, reordered = 0;
    @synchronized (lat) {
        received = lat.count;
        sorted = [lat sortedArrayUsingSelector:@selector(compare:)];
        for (NSUInteger i = 1; i < notes.count; i++)
            if ((notes[i].intValue - notes[i - 1].intValue + 128) % 128 != 1)
                reordered++;
    }
    double (^pct)(double) = ^(double q) { return [sorted[(NSUInteger)((sorted.count - 1) * q)] doubleValue]; };
    printf("loopback via %s, %d messages every %.1f ms: received %lu, out of order %lu\n",
           via ? via.UTF8String : "an in-process virtual source", count, interval_ms,
           (unsigned long)received, (unsigned long)reordered);
    if (sorted.count)
        printf("  latency ms: min %.3f  median %.3f  p99 %.3f  max %.3f\n", pct(0), pct(0.5), pct(0.99), pct(1));
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        mach_timebase_info(&tb);
        start_ticks = mach_absolute_time();
        NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
        NSString *cmd = args.count > 1 ? args[1] : @"list";
        NSString * (^opt)(NSString *, NSString *) = ^NSString *(NSString *flag, NSString *def) {
            NSUInteger i = [args indexOfObject:flag];
            return i != NSNotFound && i + 1 < args.count ? args[i + 1] : def;
        };
        if ([cmd isEqualToString:@"list"])
            list();
        else if ([cmd isEqualToString:@"listen"])
            listen_for(opt(@"--seconds", @"0").doubleValue);
        else if ([cmd isEqualToString:@"send"] && args.count > 2)
            send_pattern(args[2], opt(@"--count", @"10").intValue, opt(@"--interval-ms", @"100").doubleValue);
        else if ([cmd isEqualToString:@"loopback"])
            loopback(opt(@"--count", @"1000").intValue, opt(@"--interval-ms", @"1").doubleValue, opt(@"--via", nil));
        else {
            fprintf(stderr, "usage: midi-spike list | listen [--seconds N] | send <destination|--virtual> "
                            "[--count N] [--interval-ms N] | loopback [--count N] [--interval-ms N] [--via bus]\n");
            return 2;
        }
    }
    return 0;
}
