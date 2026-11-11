// On-demand MoltenVK capture without Accessibility input-monitoring access.
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

static NSString *capturePipe;
static NSString *outputPath;
static int frameCount;

static OSStatus Capture(EventHandlerCallRef next, EventRef event, void *context) {
    if ([[NSFileManager defaultManager] fileExistsAtPath:outputPath]) {
        fprintf(stderr, "Trace already exists; start another capture session.\n");
        NSBeep();
        return noErr;
    }
    int fd = open(capturePipe.fileSystemRepresentation, O_WRONLY | O_NONBLOCK);
    char frames[120] = {0};
    ssize_t written = fd < 0 ? -1 : write(fd, frames, frameCount);
    if (fd >= 0) close(fd);
    if (written != frameCount) {
        perror("Capture request failed");
        NSBeep();
        return noErr;
    }
    printf("Requested %d frames at %s\n", frameCount, outputPath.UTF8String);
    fflush(stdout);
    NSBeep();
    return noErr;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc != 5) return 2;
        capturePipe = [NSString stringWithUTF8String:argv[1]];
        outputPath = [NSString stringWithUTF8String:argv[2]];
        frameCount = atoi(argv[3]);
        pid_t gamePID = atoi(argv[4]);
        if (frameCount < 1 || frameCount > 120 || gamePID <= 0) return 2;
        signal(SIGPIPE, SIG_IGN);
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        EventTypeSpec type = {kEventClassKeyboard, kEventHotKeyPressed};
        OSStatus installed = InstallApplicationEventHandler(&Capture, 1, &type, NULL, NULL);
        EventHotKeyRef hotkey;
        EventHotKeyID ident = {'LoSc', 1};
        OSStatus registered = RegisterEventHotKey(kVK_ANSI_B, controlKey | optionKey,
                                                 ident, GetApplicationEventTarget(), 0, &hotkey);
        if (installed != noErr || registered != noErr) {
            fprintf(stderr, "Could not register Control+Option+B (%d, %d)\n",
                    (int)installed, (int)registered);
            return 1;
        }
        [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {
            if (kill(gamePID, 0) != 0) [NSApp terminate:nil];
        }];
        puts("Ready: Control+Option+B captures the next frames.");
        fflush(stdout);
        [NSApp run];
    }
}
