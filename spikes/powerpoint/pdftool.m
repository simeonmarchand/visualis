// P4 helper: render PDF pages to PNG at output size, and check which fonts this Mac has.
//
//   pdftool render <file.pdf> <out dir> [--width 1920]     one PNG per page, letterboxed on black
//   pdftool fonts "Gill Sans" "League Gothic Regular" ...  installed or not, and what replaces it
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <mach/mach_time.h>

static int render(NSString *pdfPath, NSString *outDir, size_t width)
{
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:pdfPath]);
    if (!doc) {
        fprintf(stderr, "can't open %s\n", pdfPath.UTF8String);
        return 1;
    }
    [[NSFileManager defaultManager] createDirectoryAtPath:outDir withIntermediateDirectories:YES attributes:nil error:nil];
    size_t height = width * 9 / 16;
    CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    uint64_t t0 = mach_absolute_time();
    size_t pages = CGPDFDocumentGetNumberOfPages(doc);
    for (size_t i = 1; i <= pages; i++) {
        CGPDFPageRef page = CGPDFDocumentGetPage(doc, i);
        CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
        CGContextRef ctx = CGBitmapContextCreate(NULL, width, height, 8, 0, rgb, kCGImageAlphaPremultipliedLast);
        CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
        CGContextFillRect(ctx, CGRectMake(0, 0, width, height));
        // Fit the page inside the output, centred: a 4:3 deck gets black bars on a 16:9 screen.
        double scale = MIN(width / box.size.width, height / box.size.height);
        CGContextTranslateCTM(ctx, (width - box.size.width * scale) / 2, (height - box.size.height * scale) / 2);
        CGContextScaleCTM(ctx, scale, scale);
        CGContextTranslateCTM(ctx, -box.origin.x, -box.origin.y);
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextDrawPDFPage(ctx, page);
        CGImageRef img = CGBitmapContextCreateImage(ctx);
        NSString *path = [outDir stringByAppendingPathComponent:[NSString stringWithFormat:@"slide-%02zu.png", i]];
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],
                                                                    CFSTR("public.png"), 1, NULL);
        CGImageDestinationAddImage(dst, img, NULL);
        CGImageDestinationFinalize(dst);
        CFRelease(dst);
        CGImageRelease(img);
        CGContextRelease(ctx);
        if (i == 1)
            printf("page size %.0fx%.0f pt (aspect %.3f)\n", box.size.width, box.size.height, box.size.width / box.size.height);
    }
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    printf("rendered %zu pages at %zux%zu in %.0f ms\n", pages, width, height,
           (double)(mach_absolute_time() - t0) * tb.numer / tb.denom / 1e6);
    CGColorSpaceRelease(rgb);
    CGPDFDocumentRelease(doc);
    return 0;
}

static void fonts(NSArray<NSString *> *names)
{
    for (NSString *name in names) {
        // CoreText always returns some font; it's the requested one only if a name matches.
        CTFontRef f = CTFontCreateWithName((__bridge CFStringRef)name, 12, NULL);
        NSString *ps = CFBridgingRelease(CTFontCopyPostScriptName(f));
        NSString *full = CFBridgingRelease(CTFontCopyFullName(f));
        NSString *family = CFBridgingRelease(CTFontCopyFamilyName(f));
        BOOL ok = [@[ps, full, family] indexOfObjectPassingTest:^BOOL(NSString *n, NSUInteger i, BOOL *stop) {
            return [n caseInsensitiveCompare:name] == NSOrderedSame;
        }] != NSNotFound;
        printf("  %-26s %s\n", name.UTF8String,
               ok ? "installed" : [NSString stringWithFormat:@"MISSING (macOS would use %@)", full].UTF8String);
        CFRelease(f);
    }
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        NSArray<NSString *> *a = NSProcessInfo.processInfo.arguments;
        if (a.count >= 4 && [a[1] isEqualToString:@"render"]) {
            NSUInteger w = [a indexOfObject:@"--width"];
            return render(a[2], a[3], w != NSNotFound && w + 1 < a.count ? (size_t)a[w + 1].integerValue : 1920);
        }
        if (a.count >= 3 && [a[1] isEqualToString:@"fonts"]) {
            fonts([a subarrayWithRange:NSMakeRange(2, a.count - 2)]);
            return 0;
        }
        fprintf(stderr, "usage: pdftool render <pdf> <outdir> [--width N] | pdftool fonts <name>...\n");
        return 2;
    }
}
