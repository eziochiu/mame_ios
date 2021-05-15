
//============================================================
//
//  clipboard.mm - iOS clipboard handler
//
//============================================================

// MAME headers
#include "emu.h"

// IOS headers
#include "iososd.h"

#import <UIKit/UIPasteboard.h>
#include <AvailabilityMacros.h>

//============================================================
//  osd_get_clipboard_text
//============================================================

std::string osd_get_clipboard_text(void)
{
#if TARGET_OS_IOS   // no clipboard on tvOS
    @autoreleasepool {
        if (UIPasteboard.generalPasteboard.hasStrings)
            return std::string(UIPasteboard.generalPasteboard.string.UTF8String);
        else
            return std::string();
    }
#else
    return std::string();
#endif
}
