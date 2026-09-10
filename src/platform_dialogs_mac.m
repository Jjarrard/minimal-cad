#include "platform_dialogs.h"

#import <Cocoa/Cocoa.h>

#include <stdio.h>

static bool copyPanelPath(NSURL* url, char* outPath, size_t outPathSize) {
    if (!outPath || outPathSize == 0 || !url) {
        return false;
    }

    const char* selectedPath = [[url path] UTF8String];
    if (!selectedPath || selectedPath[0] == '\0') {
        return false;
    }

    snprintf(outPath, outPathSize, "%s", selectedPath);
    return outPath[0] != '\0';
}

static void configureSavePanel(NSSavePanel* panel, NSString* title, NSString* extension, const char* suggestedPath) {
    [panel setTitle:title];
    [panel setCanCreateDirectories:YES];
    [panel setExtensionHidden:NO];
    [panel setAllowedFileTypes:@[extension]];

    NSString* fallbackName = [@"untitled" stringByAppendingPathExtension:extension];
    NSString* path = (suggestedPath && suggestedPath[0] != '\0')
        ? [NSString stringWithUTF8String:suggestedPath]
        : fallbackName;
    NSString* filename = [path lastPathComponent];
    NSString* directory = [path stringByDeletingLastPathComponent];

    if ([filename length] == 0) {
        filename = fallbackName;
    }
    [panel setNameFieldStringValue:filename];

    if ([directory length] > 0 && ![directory isEqualToString:@"."]) {
        NSURL* directoryUrl = [NSURL fileURLWithPath:directory isDirectory:YES];
        [panel setDirectoryURL:directoryUrl];
    }
}

static void configureOpenPanel(NSOpenPanel* panel, NSString* title, NSString* extension, const char* suggestedPath) {
    [panel setTitle:title];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setAllowedFileTypes:@[extension]];

    if (suggestedPath && suggestedPath[0] != '\0') {
        NSString* path = [NSString stringWithUTF8String:suggestedPath];
        NSString* directory = [path stringByDeletingLastPathComponent];
        if ([directory length] > 0 && ![directory isEqualToString:@"."]) {
            NSURL* directoryUrl = [NSURL fileURLWithPath:directory isDirectory:YES];
            [panel setDirectoryURL:directoryUrl];
        }
    }
}

bool showNativeStlSaveDialog(const char* suggestedPath, char* outPath, size_t outPathSize) {
    @autoreleasepool {
        if (!outPath || outPathSize == 0) {
            return false;
        }

        NSSavePanel* panel = [NSSavePanel savePanel];
        configureSavePanel(panel, @"Export STL", @"stl", suggestedPath);

        if ([panel runModal] != NSModalResponseOK) {
            return false;
        }

        return copyPanelPath([panel URL], outPath, outPathSize);
    }
}

bool showNativeProjectSaveDialog(const char* suggestedPath, char* outPath, size_t outPathSize) {
    @autoreleasepool {
        if (!outPath || outPathSize == 0) {
            return false;
        }

        NSSavePanel* panel = [NSSavePanel savePanel];
        configureSavePanel(panel, @"Save Project", @"scad", suggestedPath);
        if ([panel runModal] != NSModalResponseOK) {
            return false;
        }

        return copyPanelPath([panel URL], outPath, outPathSize);
    }
}

bool showNativeProjectOpenDialog(const char* suggestedPath, char* outPath, size_t outPathSize) {
    @autoreleasepool {
        if (!outPath || outPathSize == 0) {
            return false;
        }

        NSOpenPanel* panel = [NSOpenPanel openPanel];
        configureOpenPanel(panel, @"Open Project", @"scad", suggestedPath);
        if ([panel runModal] != NSModalResponseOK) {
            return false;
        }

        return copyPanelPath([[panel URLs] firstObject], outPath, outPathSize);
    }
}