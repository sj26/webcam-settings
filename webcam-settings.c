// Only UVC power-line frequency is changed; capture formats and fps are untouched.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <os/log.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef IOUSBInterfaceInterface220 **USB;

static IOReturn frequency(USB usb, UInt8 interface, UInt8 unit,
                         UInt8 request, UInt8 *value) {
    IOUSBDevRequestTO control = {
        .bmRequestType = request == 0x01 ? 0x21 : 0xa1,
        .bRequest = request,
        .wValue = 0x0500, // PU_POWER_LINE_FREQUENCY_CONTROL
        .wIndex = (UInt16)((unit << 8) | interface),
        .wLength = 1,
        .pData = value,
        .noDataTimeout = 1000,
        .completionTimeout = 1000,
    };
    IOReturn result = (*usb)->ControlRequestTO(usb, 0, &control);
    if (result == kIOReturnSuccess && control.wLenDone != 1)
        return kIOReturnUnderrun;
    return result;
}

static CFMutableDictionaryRef cameraMatchingCreate(void) {
    CFMutableDictionaryRef match = IOServiceMatching("IOUSBHostInterface");
    CFMutableDictionaryRef properties = CFDictionaryCreateMutable(
        NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[] = {CFSTR("idVendor"),
        CFSTR("bInterfaceClass"), CFSTR("bInterfaceSubClass")};
    const int values[] = {0x046d, 14, 1}; // Logitech, Video, VideoControl
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        CFNumberRef number = CFNumberCreate(NULL, kCFNumberIntType, &values[i]);
        CFDictionarySetValue(properties, keys[i], number);
        CFRelease(number);
    }
    CFDictionarySetValue(match, CFSTR(kIOPropertyMatchKey), properties);
    CFRelease(properties);
    return match;
}

static IOReturn checkCamera(io_service_t service, bool apply, bool verbose) {
    IOCFPlugInInterface **plugin = NULL;
    USB usb = NULL;
    SInt32 score;
    IOReturn result = IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID,
        kIOCFPlugInInterfaceID, &plugin, &score);
    if (result != kIOReturnSuccess) return result;
    HRESULT query = (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID220), (LPVOID *)&usb);
    IODestroyPlugInInterface(plugin);
    if (query || !usb) return kIOReturnUnsupported;

    UInt8 interface = 0, unit = 0;
    bool opened = false;
    result = (*usb)->GetInterfaceNumber(usb, &interface);
    if (result != kIOReturnSuccess) goto done;
    const UInt8 *descriptor = NULL;
    while ((descriptor = (const UInt8 *)(*usb)->FindNextAssociatedDescriptor(
                usb, descriptor, 0x24))) {
        // Processing Unit, with power-line-frequency bit 10 in bmControls.
        if (descriptor[0] >= 10 && descriptor[2] == 0x05 &&
            descriptor[7] >= 2 && (descriptor[9] & 0x04)) {
            unit = descriptor[3];
            break;
        }
    }
    if (!unit) { result = kIOReturnUnsupported; goto done; }

    result = (*usb)->USBInterfaceOpen(usb);
    opened = result == kIOReturnSuccess;
    // macOS can own the video interface while class control requests still work.
    // Never seize it, detach the driver, or change its configuration.
    if (!opened && result != kIOReturnExclusiveAccess) goto done;

    UInt8 current = 0;
    result = frequency(usb, interface, unit, 0x81, &current); // GET_CUR
    if (result != kIOReturnSuccess) goto done;
    if (verbose) {
        UInt8 factory = 0;
        IOReturn def = frequency(usb, interface, unit, 0x87, &factory); // GET_DEF
        printf("  interface=%u unit=%u current=%u (1=50 Hz, 2=60 Hz)",
               interface, unit, current);
        if (def == kIOReturnSuccess) printf(" factory-default=%u", factory);
        else printf(" factory-default=unavailable (0x%08x)", def);
        putchar('\n');
    }
    if (apply && current != 1) {
        UInt8 desired = 1;
        result = frequency(usb, interface, unit, 0x01, &desired); // SET_CUR
        if (result != kIOReturnSuccess) goto done;
        result = frequency(usb, interface, unit, 0x81, &desired);
        if (result == kIOReturnSuccess && desired != 1) result = kIOReturnIOError;
        if (result == kIOReturnSuccess) {
            uint64_t entryID = 0;
            IORegistryEntryGetRegistryEntryID(service, &entryID);
            os_log(OS_LOG_DEFAULT, "Webcam 0x%llx: restored 50 Hz from value %u", entryID, current);
            if (verbose) puts("  Set and verified: 50 Hz");
        }
    }
done:
    if (opened) (*usb)->USBInterfaceClose(usb);
    (*usb)->Release(usb);
    return result;
}

static IOReturn check(bool apply, bool verbose) {
    io_iterator_t iterator = IO_OBJECT_NULL;
    IOReturn result = IOServiceGetMatchingServices(kIOMainPortDefault, cameraMatchingCreate(), &iterator);
    if (result != kIOReturnSuccess) return result;
    IOReturn failure = kIOReturnSuccess;
    unsigned count = 0;
    io_service_t service;
    while ((service = IOIteratorNext(iterator))) {
        ++count;
        if (verbose) {
            char name[256] = "Logitech webcam";
            CFTypeRef product = IORegistryEntryCreateCFProperty(service, CFSTR("USB Product Name"), NULL, 0);
            if (product) {
                if (CFGetTypeID(product) == CFStringGetTypeID())
                    CFStringGetCString(product, name, sizeof(name), kCFStringEncodingUTF8);
                CFRelease(product);
            }
            uint64_t entryID = 0;
            IORegistryEntryGetRegistryEntryID(service, &entryID);
            printf("%s (USB registry 0x%llx):\n", name, entryID);
        }
        result = checkCamera(service, apply, verbose);
        IOObjectRelease(service);
        // A missing control or inaccessible camera must not block other cameras.
        if (result != kIOReturnSuccess) {
            if (verbose) printf("  %s (0x%08x)\n", mach_error_string(result), result);
            if (failure == kIOReturnSuccess) failure = result;
        }
    }
    IOObjectRelease(iterator);
    return count ? failure : kIOReturnNoDevice;
}

static void tick(CFRunLoopTimerRef timer, void *context) {
    (void)timer;
    (void)context;
    static IOReturn previous = kIOReturnSuccess;
    IOReturn result = check(true, false);
    // Report transitions only; an unplugged camera must not flood the log.
    if (result != previous) {
        os_log(OS_LOG_DEFAULT, "Webcam settings: status 0x%08x", result);
        previous = result;
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--watch") == 0) {
        tick(NULL, NULL);
        CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL,
            CFAbsoluteTimeGetCurrent() + 2, 2, 0, 0, tick, NULL);
        CFRunLoopTimerSetTolerance(timer, 0.2);
        CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
        CFRelease(timer);
        CFRunLoopRun();
        return 0;
    }
    bool status = argc == 2 && strcmp(argv[1], "--status") == 0;
    if (argc != 1 && !status) {
        bool help = argc == 2 && strcmp(argv[1], "--help") == 0;
        fputs("Usage: webcam-settings [--status | --watch | --help]\n", help ? stdout : stderr);
        return help ? 0 : 64;
    }
    IOReturn result = check(!status, true);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Webcam settings: %s (0x%08x)\n", mach_error_string(result), result);
        return result == kIOReturnNoDevice ? 2 : 1;
    }
    return 0;
}
