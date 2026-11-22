#include "media/mediaprobe.h"
#include "media/medialimits.h"

#import <AVFoundation/AVFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>

namespace visualis {

namespace {

// AVFoundation's loads are asynchronous; this runs on an import worker, so it waits, but
// never forever: a file that hangs the framework is reported, not waited on.
constexpr int64_t kLoadTimeoutNs = 15LL * NSEC_PER_SEC;

QString text(NSError *error)
{
    return error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("unknown error");
}

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
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:@{AVURLAssetPreferPreciseDurationAndTimingKey: @YES}];

        __block NSArray<AVAssetTrack *> *tracks = nil;
        __block NSError *loadError = nil;
        dispatch_semaphore_t loaded = dispatch_semaphore_create(0);
        [asset loadTracksWithMediaType:AVMediaTypeVideo
                     completionHandler:^(NSArray<AVAssetTrack *> *t, NSError *e) {
                         tracks = t;
                         loadError = e;
                         dispatch_semaphore_signal(loaded);
                     }];
        if (dispatch_semaphore_wait(loaded, dispatch_time(DISPATCH_TIME_NOW, kLoadTimeoutNs)) != 0) {
            out.error = QStringLiteral("reading the video took too long");
            return out;
        }
        if (loadError) {
            out.error = QStringLiteral("not a video Visualis can play (%1)").arg(text(loadError));
            return out;
        }
        if (tracks.count == 0) {
            out.error = QStringLiteral("no video in it");
            return out;
        }
        AVAssetTrack *track = tracks.firstObject;

        // The properties below are loaded with the tracks for a local file; the synchronous
        // getters are deprecated only in favour of Swift's async ones.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        const CGSize natural = CGSizeApplyAffineTransform(track.naturalSize, track.preferredTransform);
        out.width = int(std::abs(natural.width));
        out.height = int(std::abs(natural.height));
        out.frameRate = track.nominalFrameRate;
        const CMTime duration = asset.duration;
        NSArray *formats = track.formatDescriptions;
#pragma clang diagnostic pop
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
        __block CGImageRef frame = nullptr;
        __block NSError *frameError = nil;
        dispatch_semaphore_t generated = dispatch_semaphore_create(0);
        [generator generateCGImageAsynchronouslyForTime:kCMTimeZero
                                      completionHandler:^(CGImageRef image, CMTime, NSError *e) {
                                          if (image)
                                              frame = CGImageRetain(image);
                                          frameError = e;
                                          dispatch_semaphore_signal(generated);
                                      }];
        if (dispatch_semaphore_wait(generated, dispatch_time(DISPATCH_TIME_NOW, kLoadTimeoutNs)) != 0) {
            [generator cancelAllCGImageGeneration];
            out.error = QStringLiteral("reading the video's first frame took too long");
            return out;
        }
        if (!frame) {
            out.error = QStringLiteral("its frames can't be decoded (%1)").arg(text(frameError));
            return out;
        }
        out.thumbnail = toQImage(frame);
        CGImageRelease(frame);
    }
    return out;
}

} // namespace visualis
