#include "media/mediaprobe.h"
#include "media/medialimits.h"

#include <QDeadlineTimer>

#import <AVFoundation/AVFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>

#include <memory>

namespace visualis {

namespace {

// AVFoundation loads asynchronously; this runs on an import worker, so it waits, but never
// past one deadline for the whole probe: a file that hangs the framework is reported, and
// the loading cancelled, not waited on (#97 review).
constexpr int kProbeTimeoutMs = 15'000;

QString text(NSError *error)
{
    return error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("unknown error");
}

bool waitUntil(dispatch_semaphore_t done, const QDeadlineTimer &deadline)
{
    const qint64 ms = qMax<qint64>(0, deadline.remainingTime());
    return dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, ms * NSEC_PER_MSEC)) == 0;
}

// Loads `keys` of an asset or track, within the deadline. Empty on success, else why not.
QString load(id<AVAsynchronousKeyValueLoading> object, NSArray<NSString *> *keys, const QDeadlineTimer &deadline)
{
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [object loadValuesAsynchronouslyForKeys:keys completionHandler:^{ dispatch_semaphore_signal(done); }];
    if (!waitUntil(done, deadline))
        return QStringLiteral("reading the video took too long");
    for (NSString *key in keys) {
        NSError *error = nil;
        if ([object statusOfValueForKey:key error:&error] != AVKeyValueStatusLoaded)
            return QStringLiteral("not a video Visualis can play (%1)").arg(text(error));
    }
    return {};
}

// What the thumbnail callback hands back. Shared with the block, so a frame that arrives
// after the deadline is still released, by whichever side lets go last.
struct FrameResult {
    CGImageRef image = nullptr;
    NSError *error = nil;   // ARC keeps it alive
    ~FrameResult()
    {
        if (image)
            CGImageRelease(image);
    }
};

QImage toQImage(CGImageRef cg)
{
    QImage image(int(CGImageGetWidth(cg)), int(CGImageGetHeight(cg)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(image.bits(), image.width(), image.height(), 8,
                                                 image.bytesPerLine(), space,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    if (context) {
        CGContextDrawImage(context, CGRectMake(0, 0, image.width(), image.height()), cg);
        CGContextRelease(context);
    }
    CGColorSpaceRelease(space);
    return context ? image : QImage();
}

} // namespace

MediaProbe probeVideo(const QString &path)
{
    MediaProbe out;
    const QDeadlineTimer deadline(kProbeTimeoutMs);
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:@{AVURLAssetPreferPreciseDurationAndTimingKey: @YES}];
        auto fail = [&](const QString &why) {
            [asset cancelLoading];
            out.error = why;
            return out;
        };

        if (QString why = load(asset, @[@"duration", @"tracks"], deadline); !why.isEmpty())
            return fail(why);
        // Loaded above, so the getters below don't block; they're deprecated only in favour of
        // Swift's async ones.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        const CMTime duration = asset.duration;
        AVAssetTrack *track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
#pragma clang diagnostic pop
        if (!track)
            return fail(QStringLiteral("no video in it"));
        if (QString why = load(track, @[@"naturalSize", @"preferredTransform", @"nominalFrameRate", @"formatDescriptions"],
                               deadline);
            !why.isEmpty())
            return fail(why);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        const CGSize natural = CGSizeApplyAffineTransform(track.naturalSize, track.preferredTransform);
        out.frameRate = track.nominalFrameRate;
        NSArray *formats = track.formatDescriptions;
#pragma clang diagnostic pop
        out.width = int(std::abs(natural.width));
        out.height = int(std::abs(natural.height));
        if (CMTIME_IS_NUMERIC(duration))
            out.durationMs = qint64(CMTimeGetSeconds(duration) * 1000.0 + 0.5);
        if (formats.count > 0) {
            const auto format = (__bridge CMFormatDescriptionRef)formats.firstObject;
            const FourCharCode codec = CMFormatDescriptionGetMediaSubType(format);
            out.codec = codecName(codec);
            if (!VTIsHardwareDecodeSupported(codec))
                out.warnings << QStringLiteral("%1 video can't be decoded in hardware on this Mac, so it may "
                                               "stutter as a background; H.264 or HEVC plays smoothly")
                                    .arg(out.codec);
        }

        // The first frame, scaled down, for the media pane.
        AVAssetImageGenerator *generator = [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
        generator.appliesPreferredTrackTransform = YES;
        generator.maximumSize = CGSizeMake(medialimits::kThumbnailPixels, medialimits::kThumbnailPixels);
        generator.requestedTimeToleranceBefore = kCMTimeZero;
        generator.requestedTimeToleranceAfter = CMTimeMake(1, 2);
        auto frame = std::make_shared<FrameResult>();
        dispatch_semaphore_t generated = dispatch_semaphore_create(0);
        [generator generateCGImageAsynchronouslyForTime:kCMTimeZero
                                      completionHandler:^(CGImageRef image, CMTime, NSError *e) {
                                          if (image)
                                              frame->image = CGImageRetain(image);
                                          frame->error = e;
                                          dispatch_semaphore_signal(generated);
                                      }];
        if (!waitUntil(generated, deadline)) {
            [generator cancelAllCGImageGeneration];
            return fail(QStringLiteral("reading the video's first frame took too long"));
        }
        if (!frame->image)
            return fail(QStringLiteral("its frames can't be decoded (%1)").arg(text(frame->error)));
        out.thumbnail = toQImage(frame->image);
    }
    return out;
}

} // namespace visualis
