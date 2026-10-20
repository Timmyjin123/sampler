// Owns the AVAudioSession / AVAudioEngine and feeds the C++ engine.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, GPRecordSource) {
    GPRecordSourceMic = 0,
    GPRecordSourceResample = 1,   // records the app's own output
};

@interface GPAudio : NSObject

@property (class, nonatomic, readonly) GPAudio *shared;

/// Configures the audio session and starts the engine. Safe to call again.
- (BOOL)start:(NSError * _Nullable * _Nullable)error;
@property (nonatomic, readonly) BOOL running;
@property (nonatomic, readonly) double sampleRate;
/// Round-trip latency estimate in milliseconds (output + input + IO buffer).
@property (nonatomic, readonly) double latencyMs;

// ---- recording ----
- (void)requestMicPermission:(void (^)(BOOL granted))completion;
- (BOOL)beginRecording:(GPRecordSource)source;
/// Stops and loads the take into a pad (auto-trimmed and normalized for the mic).
/// Calls back on the main queue with the number of frames kept (0 = nothing usable).
- (void)finishRecordingIntoPad:(int)pad completion:(void (^)(int frames))completion;
- (void)cancelRecording;
@property (nonatomic, readonly) BOOL recording;
@property (nonatomic, readonly) double recordedSeconds;
/// Peak level of the current recording source, 0..1.
@property (nonatomic, readonly) float recordLevel;

// ---- files ----
/// Decodes any audio file AVAudioFile can read and loads it into a pad.
/// Completion runs on the main queue with frames kept (0 on failure).
- (void)loadFile:(NSURL *)url intoPad:(int)pad autoTrim:(BOOL)trim normalize:(BOOL)normalize
      completion:(void (^)(int frames))completion;
/// Synchronous variant for project loading (call on main thread).
- (int)loadFileNow:(NSURL *)url intoPad:(int)pad;
/// Renders a pattern offline (loops passes + 2 s tail) and writes it as a 24-bit stereo WAV.
- (BOOL)bouncePattern:(int)pattern loops:(int)loops toURL:(NSURL *)url error:(NSError * _Nullable * _Nullable)error;
/// Writes the pad's audio as a 24-bit WAV.
- (BOOL)writePad:(int)pad toURL:(NSURL *)url error:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END
