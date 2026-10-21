// energy.mm - the power state the energy policy reacts to (mods/display_settings.cpp):
// Low Power Mode, a hot device and running on battery. Read at most once a second by
// the policy; each read is a handful of property loads.
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif

extern "C" int host_power_state(void) {
    int state = 0;
    NSProcessInfo *info = [NSProcessInfo processInfo];
    if (@available(macOS 12.0, iOS 9.0, *)) {
        if (info.lowPowerModeEnabled)
            state |= 1;
    }
    if (info.thermalState >= NSProcessInfoThermalStateSerious)
        state |= 2;
#if TARGET_OS_IPHONE
    // Battery monitoring is switched on once, on the main thread; until then the
    // state reads as unknown, which counts as plugged in.
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      dispatch_async(dispatch_get_main_queue(), ^{
        [UIDevice currentDevice].batteryMonitoringEnabled = YES;
      });
    });
    if ([UIDevice currentDevice].batteryState == UIDeviceBatteryStateUnplugged)
        state |= 4;
#else
    if (CFTypeRef blob = IOPSCopyPowerSourcesInfo()) {
        if (CFStringRef type = IOPSGetProvidingPowerSourceType(blob)) {
            if (CFStringCompare(type, CFSTR(kIOPMBatteryPowerKey), 0) == kCFCompareEqualTo)
                state |= 4;
        }
        CFRelease(blob);
    }
#endif
    return state;
}

// How hot the device is, as the system grades it: 0 nominal, 1 fair, 2 serious, 3
// critical (NSProcessInfo.thermalState). The render quality's automatic choice draws
// the 3D smaller as it rises (host/gpu/metal/d3d9_metal.mm update_dynamic_scale).
extern "C" int host_thermal_level(void) {
    switch ([NSProcessInfo processInfo].thermalState) {
    case NSProcessInfoThermalStateFair:
        return 1;
    case NSProcessInfoThermalStateSerious:
        return 2;
    case NSProcessInfoThermalStateCritical:
        return 3;
    default:
        return 0;
    }
}
