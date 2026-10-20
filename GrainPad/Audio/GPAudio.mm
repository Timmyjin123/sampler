#import "GPAudio.h"
#import <AVFoundation/AVFoundation.h>
#include "GPEngine.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <cmath>

static const int kMaxRenderFrames = 4096;
static const double kMaxRecordSeconds = 120.0;
static const double kMaxFileSeconds = 600.0;

@implementation GPAudio {
    AVAudioEngine *_engine;
    AVAudioSourceNode *_source;
    BOOL _micTapInstalled;
    BOOL _outTapInstalled;
    double _sampleRate;

    std::mutex _recLock;
    std::vector<float> _recL, _recR;
    std::atomic<bool> _capturing;
    std::atomic<int> _recSource;
    std::atomic<float> _recLevel;
    double _recSampleRate;
    int _recChannels;
    std::vector<float> _scratchR;
}

+ (GPAudio *)shared {
    static GPAudio *s; static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [GPAudio new]; });
    return s;
}

- (instancetype)init {
    if ((self = [super init])) {
        _capturing.store(false); _recSource.store(0); _recLevel.store(0);
        _scratchR.assign(kMaxRenderFrames, 0.0f);
        _recSampleRate = 48000; _recChannels = 1;
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
        [nc addObserver:self selector:@selector(interruption:) name:AVAudioSessionInterruptionNotification object:nil];
        [nc addObserver:self selector:@selector(mediaReset:) name:AVAudioSessionMediaServicesWereResetNotification object:nil];
    }
    return self;
}

- (BOOL)running { return _engine.isRunning; }
- (double)sampleRate { return _sampleRate; }
- (BOOL)recording { return _capturing.load(); }
- (float)recordLevel { return _recLevel.load(); }

- (double)recordedSeconds {
    std::lock_guard<std::mutex> g(_recLock);
    return _recSampleRate > 0 ? _recL.size() / _recSampleRate : 0;
}

- (double)latencyMs {
    AVAudioSession *s = AVAudioSession.sharedInstance;
    return (s.outputLatency + s.IOBufferDuration) * 1000.0;
}

// ---------------------------------------------------------------------------
- (BOOL)configureSession:(NSError **)error {
    AVAudioSession *s = AVAudioSession.sharedInstance;
    AVAudioSessionCategoryOptions opts = AVAudioSessionCategoryOptionDefaultToSpeaker |
                                         AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    if (![s setCategory:AVAudioSessionCategoryPlayAndRecord mode:AVAudioSessionModeDefault options:opts error:error]) return NO;
    [s setPreferredSampleRate:48000 error:nil];
    [s setPreferredIOBufferDuration:0.005 error:nil];   // ~256 frames: low latency pads
    return [s setActive:YES error:error];
}

- (BOOL)start:(NSError **)error {
    if (_engine.isRunning) return YES;
    if (![self configureSession:error]) return NO;

    if (!_engine) {
        _engine = [AVAudioEngine new];
        _sampleRate = AVAudioSession.sharedInstance.sampleRate;
        if (_sampleRate < 8000) _sampleRate = 48000;
        gp_prepare(_sampleRate, kMaxRenderFrames);

        AVAudioFormat *fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:_sampleRate channels:2];
        float *scratch = _scratchR.data();
        _source = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:
            ^OSStatus(BOOL *isSilence, const AudioTimeStamp *ts, AVAudioFrameCount frameCount, AudioBufferList *abl) {
                float *L = (float *)abl->mBuffers[0].mData;
                float *R = abl->mNumberBuffers > 1 ? (float *)abl->mBuffers[1].mData : nullptr;
                int n = (int)frameCount;
                if (R) {
                    gp_render(L, R, n);
                } else {
                    int done = 0;
                    while (done < n) {
                        int c = std::min(n - done, kMaxRenderFrames);
                        gp_render(L + done, scratch, c);
                        for (int i = 0; i < c; ++i) L[done + i] = 0.5f * (L[done + i] + scratch[i]);
                        done += c;
                    }
                }
                return noErr;
            }];
        [_engine attachNode:_source];
        [_engine connect:_source to:_engine.mainMixerNode format:fmt];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(configChanged:)
                                                   name:AVAudioEngineConfigurationChangeNotification object:_engine];
    }
    [self installOutputTap];
    [_engine prepare];
    return [_engine startAndReturnError:error];
}

- (void)restart {
    if (!_engine) { [self start:nil]; return; }
    BOOL wasCapturing = _capturing.load();
    _capturing.store(false);
    [_engine stop];
    if (_micTapInstalled) { @try { [_engine.inputNode removeTapOnBus:0]; } @catch (NSException *e) {} }
    _micTapInstalled = NO;
    if (_outTapInstalled) { @try { [_engine.mainMixerNode removeTapOnBus:0]; } @catch (NSException *e) {} }
    (void)wasCapturing;
    _outTapInstalled = NO;
    [self start:nil];
}

- (void)configChanged:(NSNotification *)n {
    dispatch_async(dispatch_get_main_queue(), ^{ [self restart]; });
}
- (void)mediaReset:(NSNotification *)n {
    dispatch_async(dispatch_get_main_queue(), ^{
        self->_engine = nil; self->_source = nil;
        self->_micTapInstalled = NO; self->_outTapInstalled = NO;
        [self start:nil];
    });
}
- (void)interruption:(NSNotification *)n {
    NSUInteger type = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeEnded) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self restart]; });
    }
}

// ---------------------------------------------------------------------------
- (void)appendBuffer:(AVAudioPCMBuffer *)buf source:(int)src {
    if (!_capturing.load() || _recSource.load() != src) return;
    int n = (int)buf.frameLength;
    int ch = (int)buf.format.channelCount;
    if (n <= 0 || ch <= 0 || !buf.floatChannelData) return;
    const float *l = buf.floatChannelData[0];
    const float *r = ch > 1 ? buf.floatChannelData[1] : l;
    float pk = 0;
    for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
    _recLevel.store(std::max(pk, _recLevel.load() * 0.7f));
    std::lock_guard<std::mutex> g(_recLock);
    if (_recL.size() > (size_t)(kMaxRecordSeconds * buf.format.sampleRate)) return;
    _recSampleRate = buf.format.sampleRate;
    _recChannels = ch > 1 ? 2 : 1;
    _recL.insert(_recL.end(), l, l + n);
    if (ch > 1) _recR.insert(_recR.end(), r, r + n);
}

- (void)installOutputTap {
    if (_outTapInstalled) return;
    __weak GPAudio *weakSelf = self;
    AVAudioMixerNode *mixer = _engine.mainMixerNode;
    [mixer installTapOnBus:0 bufferSize:1024 format:[mixer outputFormatForBus:0]
                     block:^(AVAudioPCMBuffer *buf, AVAudioTime *when) {
        [weakSelf appendBuffer:buf source:GPRecordSourceResample];
    }];
    _outTapInstalled = YES;
}

- (BOOL)installMicTap {
    if (_micTapInstalled) return YES;
    [_engine stop];
    AVAudioInputNode *input = _engine.inputNode;
    AVAudioFormat *fmt = [input outputFormatForBus:0];
    if (fmt.channelCount == 0 || fmt.sampleRate <= 0) { [_engine prepare]; [_engine startAndReturnError:nil]; return NO; }
    __weak GPAudio *weakSelf = self;
    [input installTapOnBus:0 bufferSize:1024 format:fmt block:^(AVAudioPCMBuffer *buf, AVAudioTime *when) {
        [weakSelf appendBuffer:buf source:GPRecordSourceMic];
    }];
    _micTapInstalled = YES;
    [_engine prepare];
    return [_engine startAndReturnError:nil];
}

- (void)requestMicPermission:(void (^)(BOOL))completion {
    void (^done)(BOOL) = ^(BOOL ok) { dispatch_async(dispatch_get_main_queue(), ^{ completion(ok); }); };
    if (@available(iOS 17.0, *)) {
        switch (AVAudioApplication.sharedInstance.recordPermission) {
            case AVAudioApplicationRecordPermissionGranted: done(YES); return;
            case AVAudioApplicationRecordPermissionDenied: done(NO); return;
            default: [AVAudioApplication requestRecordPermissionWithCompletionHandler:done]; return;
        }
    } else {
        AVAudioSession *s = AVAudioSession.sharedInstance;
        switch (s.recordPermission) {
            case AVAudioSessionRecordPermissionGranted: done(YES); return;
            case AVAudioSessionRecordPermissionDenied: done(NO); return;
            default: [s requestRecordPermission:done]; return;
        }
    }
}

- (BOOL)beginRecording:(GPRecordSource)source {
    if (!_engine.isRunning && ![self start:nil]) return NO;
    if (source == GPRecordSourceMic && ![self installMicTap]) return NO;
    {
        std::lock_guard<std::mutex> g(_recLock);
        _recL.clear(); _recR.clear();
        _recL.reserve((size_t)(_sampleRate * 10));
    }
    _recLevel.store(0);
    _recSource.store((int)source);
    _capturing.store(true);
    return YES;
}

- (void)cancelRecording {
    _capturing.store(false);
    std::lock_guard<std::mutex> g(_recLock);
    _recL.clear(); _recR.clear();
}

- (void)finishRecordingIntoPad:(int)pad completion:(void (^)(int))completion {
    _capturing.store(false);
    int src = _recSource.load();
    std::vector<float> L, R; double sr; int ch;
    {
        std::lock_guard<std::mutex> g(_recLock);
        L.swap(_recL); R.swap(_recR); sr = _recSampleRate; ch = _recChannels;
    }
    if (ch > 1 && R.size() != L.size()) { size_t n = std::min(L.size(), R.size()); L.resize(n); R.resize(n); }
    int frames = 0;
    if (L.size() > (size_t)(sr * 0.02)) {
        frames = gp_load_pad_sample(pad, L.data(), ch > 1 ? R.data() : nullptr, (int)L.size(), sr,
                                    1, src == GPRecordSourceMic ? 1 : 0);
    }
    _recLevel.store(0);
    completion(frames);
}

// ---------------------------------------------------------------------------
static BOOL decodeFile(NSURL *url, std::vector<float> &L, std::vector<float> &R, double &sr) {
    BOOL scoped = [url startAccessingSecurityScopedResource];
    NSError *err = nil;
    AVAudioFile *file = [[AVAudioFile alloc] initForReading:url error:&err];
    BOOL ok = NO;
    if (file) {
        AVAudioFormat *fmt = file.processingFormat;
        sr = fmt.sampleRate;
        AVAudioFrameCount total = (AVAudioFrameCount)std::min<long long>(file.length, (long long)(kMaxFileSeconds * sr));
        AVAudioPCMBuffer *buf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:total];
        if (buf && total > 0 && [file readIntoBuffer:buf frameCount:total error:&err] && buf.floatChannelData) {
            int n = (int)buf.frameLength;
            int ch = (int)fmt.channelCount;
            L.assign(buf.floatChannelData[0], buf.floatChannelData[0] + n);
            if (ch > 1) R.assign(buf.floatChannelData[1], buf.floatChannelData[1] + n);
            ok = n > 0;
        }
    }
    if (scoped) [url stopAccessingSecurityScopedResource];
    return ok;
}

- (void)loadFile:(NSURL *)url intoPad:(int)pad autoTrim:(BOOL)trim normalize:(BOOL)normalize
      completion:(void (^)(int))completion {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto L = std::make_shared<std::vector<float>>();
        auto R = std::make_shared<std::vector<float>>();
        double sr = 48000;
        BOOL ok = decodeFile(url, *L, *R, sr);
        dispatch_async(dispatch_get_main_queue(), ^{
            int frames = 0;
            if (ok) frames = gp_load_pad_sample(pad, L->data(), R->empty() ? nullptr : R->data(),
                                                (int)L->size(), sr, trim ? 1 : 0, normalize ? 1 : 0);
            completion(frames);
        });
    });
}

- (int)loadFileNow:(NSURL *)url intoPad:(int)pad {
    std::vector<float> L, R; double sr = 48000;
    if (!decodeFile(url, L, R, sr)) return 0;
    return gp_load_pad_sample(pad, L.data(), R.empty() ? nullptr : R.data(), (int)L.size(), sr, 0, 0);
}

static NSDictionary *wavSettings(double sr, int ch) {
    return @{
        AVFormatIDKey: @(kAudioFormatLinearPCM),
        AVSampleRateKey: @(sr),
        AVNumberOfChannelsKey: @(ch),
        AVLinearPCMBitDepthKey: @24,
        AVLinearPCMIsFloatKey: @NO,
        AVLinearPCMIsBigEndianKey: @NO,
        AVLinearPCMIsNonInterleaved: @NO,
    };
}

- (BOOL)bouncePattern:(int)pattern loops:(int)loops toURL:(NSURL *)url error:(NSError **)error {
    double sr = 48000.0;
    int frames = gp_bounce_frames(pattern, loops, 2.0f, sr);
    if (frames <= 0) return NO;
    AVAudioFormat *fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:sr
                                                            channels:2 interleaved:NO];
    AVAudioPCMBuffer *buf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:(AVAudioFrameCount)frames];
    if (!buf) return NO;
    int n = gp_bounce(pattern, loops, 2.0f, sr, buf.floatChannelData[0], buf.floatChannelData[1], frames);
    if (n <= 0) return NO;
    buf.frameLength = (AVAudioFrameCount)n;
    AVAudioFile *file = [[AVAudioFile alloc] initForWriting:url settings:wavSettings(sr, 2)
                                               commonFormat:AVAudioPCMFormatFloat32 interleaved:NO error:error];
    if (!file) return NO;
    return [file writeFromBuffer:buf error:error];
}

- (BOOL)writePad:(int)pad toURL:(NSURL *)url error:(NSError **)error {
    int frames = gp_pad_frames(pad);
    int ch = gp_pad_channels(pad);
    double sr = gp_pad_sample_rate(pad);
    if (frames <= 0 || ch <= 0) return NO;
    NSDictionary *settings = @{
        AVFormatIDKey: @(kAudioFormatLinearPCM),
        AVSampleRateKey: @(sr),
        AVNumberOfChannelsKey: @(ch),
        AVLinearPCMBitDepthKey: @24,
        AVLinearPCMIsFloatKey: @NO,
        AVLinearPCMIsBigEndianKey: @NO,
        AVLinearPCMIsNonInterleaved: @NO,
    };
    AVAudioFile *file = [[AVAudioFile alloc] initForWriting:url settings:settings
                                               commonFormat:AVAudioPCMFormatFloat32 interleaved:NO error:error];
    if (!file) return NO;
    AVAudioFormat *fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:sr
                                                            channels:(AVAudioChannelCount)ch interleaved:NO];
    AVAudioPCMBuffer *buf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:(AVAudioFrameCount)frames];
    if (!buf) return NO;
    buf.frameLength = (AVAudioFrameCount)frames;
    gp_pad_copy_audio(pad, buf.floatChannelData[0], ch > 1 ? buf.floatChannelData[1] : nullptr, frames);
    return [file writeFromBuffer:buf error:error];
}

@end
