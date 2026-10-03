// Unity cannot load a library on iOS to ask it for its effects, so a plugin there hands Unity the function itself. Not
// before Unity has made its plugin registry, which an earlier call writes through a null pointer, and not after the
// first scene has loaded its mixers. Each Xcode project type posts a notification in that window, which the other
// never posts: the Objective-C type's app controller as it begins launching, the Swift type's player once its runtime
// is up.

#import <Foundation/Foundation.h>

struct UnityAudioEffectDefinition;

extern "C" void UnityRegisterAudioPlugin(int (*getAudioEffectDefinitions)(UnityAudioEffectDefinition ***));
extern "C" int psx_reverb_get_audio_effect_definitions(UnityAudioEffectDefinition ***definitions);

@interface PSXReverbRegistration : NSObject
@end

@implementation PSXReverbRegistration

+ (void)load
{
    for (NSString *name in @[ @"kUnityWillFinishLaunchingWithOptions", @"UnityDidInitializeRuntime" ])
    {
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(registerWithUnity:)
                                                     name:name
                                                   object:nil];
    }
}

+ (void)registerWithUnity:(NSNotification *)notification
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    UnityRegisterAudioPlugin(psx_reverb_get_audio_effect_definitions);
}

@end
