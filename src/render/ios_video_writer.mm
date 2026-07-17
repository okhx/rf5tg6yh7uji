#include "ios_video_writer.hpp"

#ifdef GEODE_IS_IOS

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <dispatch/dispatch.h>

#include <cstring>

namespace {
std::string errorText(NSError* error, const char* fallback) {
    if (!error) return fallback;
    const char* text = error.localizedDescription.UTF8String;
    return text ? std::string(text) : std::string(fallback);
}
}

struct IOSVideoWriter::Impl {
    __strong AVAssetWriter* writer = nil;
    __strong AVAssetWriterInput* input = nil;
    __strong AVAssetWriterInput* audioInput = nil;
    __strong AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
    int width = 0;
    int height = 0;
    int fps = 60;
    int sampleRate = 48000;
    int channels = 2;
    int64_t frame = 0;
    int64_t audioFrame = 0;
    bool finished = false;
};

IOSVideoWriter::IOSVideoWriter() : m_impl(std::make_unique<Impl>()) {}

IOSVideoWriter::~IOSVideoWriter() {
    if (m_impl && m_impl->writer && !m_impl->finished) {
        [m_impl->input markAsFinished];
        if (m_impl->audioInput) [m_impl->audioInput markAsFinished];
        [m_impl->writer cancelWriting];
    }
}

geode::Result<> IOSVideoWriter::open(const std::filesystem::path& output,
                                     int width, int height, int fps,
                                     uint32_t bitrate, int sampleRate,
                                     int channels, bool includeAudio) {
    if (width <= 0 || height <= 0 || fps <= 0)
        return geode::Err("Invalid iOS render settings");

    @autoreleasepool {
        NSString* path = [NSString
            stringWithUTF8String:output.string().c_str()];
        if (!path) return geode::Err("Invalid iOS render output path");
        NSURL* url = [NSURL fileURLWithPath:path];
        [[NSFileManager defaultManager] removeItemAtURL:url error:nullptr];

        NSError* error = nil;
        m_impl->writer = [[AVAssetWriter alloc]
            initWithURL:url fileType:AVFileTypeMPEG4 error:&error];
        if (!m_impl->writer)
            return geode::Err(errorText(error, "Unable to create MP4 writer"));

        NSDictionary* compression = @{
            AVVideoAverageBitRateKey : @(bitrate),
            AVVideoExpectedSourceFrameRateKey : @(fps),
            AVVideoMaxKeyFrameIntervalKey : @(fps * 2)
        };
        NSDictionary* settings = @{
            AVVideoCodecKey : AVVideoCodecTypeH264,
            AVVideoWidthKey : @(width),
            AVVideoHeightKey : @(height),
            AVVideoCompressionPropertiesKey : compression
        };
        m_impl->input = [AVAssetWriterInput
            assetWriterInputWithMediaType:AVMediaTypeVideo
                            outputSettings:settings];
        m_impl->input.expectsMediaDataInRealTime = NO;

        NSDictionary* attributes = @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey :
                @(kCVPixelFormatType_32BGRA),
            (__bridge NSString*)kCVPixelBufferWidthKey : @(width),
            (__bridge NSString*)kCVPixelBufferHeightKey : @(height),
            (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{}
        };
        m_impl->adaptor = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:m_impl->input
                                       sourcePixelBufferAttributes:attributes];
        if (![m_impl->writer canAddInput:m_impl->input])
            return geode::Err("iOS encoder rejected the video settings");
        [m_impl->writer addInput:m_impl->input];

        if (includeAudio && sampleRate > 0 && channels > 0) {
            NSDictionary* audioSettings = @{
                AVFormatIDKey : @(kAudioFormatMPEG4AAC),
                AVSampleRateKey : @(sampleRate),
                AVNumberOfChannelsKey : @(channels),
                AVEncoderBitRateKey : @(192000)
            };
            m_impl->audioInput = [AVAssetWriterInput
                assetWriterInputWithMediaType:AVMediaTypeAudio
                                outputSettings:audioSettings];
            m_impl->audioInput.expectsMediaDataInRealTime = NO;
            if (![m_impl->writer canAddInput:m_impl->audioInput])
                return geode::Err("iOS encoder rejected the audio settings");
            [m_impl->writer addInput:m_impl->audioInput];
        } else {
            m_impl->audioInput = nil;
        }
        if (![m_impl->writer startWriting]) {
            return geode::Err(errorText(m_impl->writer.error,
                                        "Unable to start iOS video encoder"));
        }
        [m_impl->writer startSessionAtSourceTime:kCMTimeZero];
        m_impl->width = width;
        m_impl->height = height;
        m_impl->fps = fps;
        m_impl->sampleRate = sampleRate;
        m_impl->channels = channels;
        m_impl->frame = 0;
        m_impl->audioFrame = 0;
        m_impl->finished = false;
    }
    return geode::Ok();
}

geode::Result<bool> IOSVideoWriter::appendAudio(
    const std::vector<float>& pcm) {
    if (!m_impl->audioInput || pcm.empty()) return geode::Ok(true);
    if (!m_impl->writer || m_impl->finished)
        return geode::Err("iOS video writer is not active");

    const size_t frames = pcm.size() / static_cast<size_t>(m_impl->channels);
    if (frames == 0) return geode::Ok(true);
    const size_t bytes = frames * static_cast<size_t>(m_impl->channels) *
                         sizeof(float);

    if (m_impl->writer.status == AVAssetWriterStatusFailed)
        return geode::Err(errorText(m_impl->writer.error,
                                    "iOS audio encoder failed"));
    if (!m_impl->audioInput.readyForMoreMediaData)
        return geode::Ok(false);

    AudioStreamBasicDescription format{};
    format.mSampleRate = m_impl->sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat |
                          kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = sizeof(float) * m_impl->channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float) * m_impl->channels;
    format.mChannelsPerFrame = m_impl->channels;
    format.mBitsPerChannel = 32;

    CMAudioFormatDescriptionRef description = nullptr;
    if (CMAudioFormatDescriptionCreate(
            kCFAllocatorDefault, &format, 0, nullptr, 0, nullptr, nullptr,
            &description) != noErr) {
        return geode::Err("Unable to describe captured iOS audio");
    }

    CMBlockBufferRef block = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault, nullptr,
        0, bytes, 0, &block);
    if (status == kCMBlockBufferNoErr) {
        status = CMBlockBufferReplaceDataBytes(pcm.data(), block, 0, bytes);
    }
    if (status != kCMBlockBufferNoErr) {
        if (block) CFRelease(block);
        CFRelease(description);
        return geode::Err("Unable to allocate captured iOS audio");
    }

    CMSampleTimingInfo timing{
        .duration = CMTimeMake(1, m_impl->sampleRate),
        .presentationTimeStamp =
            CMTimeMake(m_impl->audioFrame, m_impl->sampleRate),
        .decodeTimeStamp = kCMTimeInvalid,
    };
    CMSampleBufferRef sample = nullptr;
    status = CMSampleBufferCreateReady(
        kCFAllocatorDefault, block, description,
        static_cast<CMItemCount>(frames), 1, &timing, 0,
        nullptr, &sample);
    CFRelease(block);
    CFRelease(description);
    if (status != noErr || !sample)
        return geode::Err("Unable to create an iOS audio sample");

    const BOOL appended = [m_impl->audioInput appendSampleBuffer:sample];
    CFRelease(sample);
    if (!appended)
        return geode::Err(errorText(m_impl->writer.error,
                                    "Unable to append iOS audio"));
    m_impl->audioFrame += static_cast<int64_t>(frames);
    return geode::Ok(true);
}

geode::Result<bool> IOSVideoWriter::appendRGB(
    const std::vector<uint8_t>& rgb) {
    if (!m_impl->writer || m_impl->finished)
        return geode::Err("iOS video writer is not active");
    const size_t expected = static_cast<size_t>(m_impl->width) *
                            static_cast<size_t>(m_impl->height) * 3;
    if (rgb.size() != expected)
        return geode::Err("Captured frame has an invalid size");

    if (m_impl->writer.status == AVAssetWriterStatusFailed)
        return geode::Err(errorText(m_impl->writer.error,
                                    "iOS video encoder failed"));
    if (!m_impl->input.readyForMoreMediaData) return geode::Ok(false);

    CVPixelBufferRef pixel = nullptr;
    const CVReturn created = CVPixelBufferPoolCreatePixelBuffer(
        kCFAllocatorDefault, m_impl->adaptor.pixelBufferPool, &pixel);
    if (created != kCVReturnSuccess || !pixel)
        return geode::Err("Unable to allocate an iOS video frame");

    CVPixelBufferLockBaseAddress(pixel, 0);
    auto* destination = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixel));
    if (!destination) {
        CVPixelBufferUnlockBaseAddress(pixel, 0);
        CVPixelBufferRelease(pixel);
        return geode::Err("Unable to access the iOS video frame");
    }
    const size_t stride = CVPixelBufferGetBytesPerRow(pixel);
    for (int y = 0; y < m_impl->height; ++y) {
        const uint8_t* source = rgb.data() +
            static_cast<size_t>(m_impl->height - 1 - y) * m_impl->width * 3;
        uint8_t* row = destination + static_cast<size_t>(y) * stride;
        for (int x = 0; x < m_impl->width; ++x) {
            row[x * 4 + 0] = source[x * 3 + 2];
            row[x * 4 + 1] = source[x * 3 + 1];
            row[x * 4 + 2] = source[x * 3 + 0];
            row[x * 4 + 3] = 255;
        }
    }
    CVPixelBufferUnlockBaseAddress(pixel, 0);

    const CMTime time = CMTimeMake(m_impl->frame++, m_impl->fps);
    const BOOL appended = [m_impl->adaptor appendPixelBuffer:pixel
                                        withPresentationTime:time];
    CVPixelBufferRelease(pixel);
    if (!appended)
        return geode::Err(errorText(m_impl->writer.error,
                                    "Unable to append iOS video frame"));
    return geode::Ok(true);
}

geode::Result<> IOSVideoWriter::finish() {
    if (!m_impl->writer || m_impl->finished) return geode::Ok();
    m_impl->finished = true;
    [m_impl->input markAsFinished];
    if (m_impl->audioInput) [m_impl->audioInput markAsFinished];

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    [m_impl->writer finishWritingWithCompletionHandler:^{
        dispatch_semaphore_signal(semaphore);
    }];
    const long timedOut = dispatch_semaphore_wait(
        semaphore, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
    if (timedOut != 0)
        return geode::Err("Timed out finalizing the iOS video");
    if (m_impl->writer.status != AVAssetWriterStatusCompleted) {
        return geode::Err(errorText(m_impl->writer.error,
                                    "Failed to finalize the iOS video"));
    }
    return geode::Ok();
}

#endif
