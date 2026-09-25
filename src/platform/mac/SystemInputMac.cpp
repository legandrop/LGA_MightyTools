#include "platform/SystemInput.h"

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

// Sin compilar todavia en un Mac: la primera compilacion en macOS la valida.

namespace SystemInput {

bool primaryButtonDown()
{
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
}

bool escapeDown()
{
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Escape);
}

bool accessibilityTrusted(bool prompt)
{
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {prompt ? kCFBooleanTrue : kCFBooleanFalse};
    CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFCopyStringDictionaryKeyCallBacks,
                                                 &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    return trusted;
}

bool needsAccessibilityPermission()
{
    return true;
}

} // namespace SystemInput
