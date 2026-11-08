// splatthumb-icon: option B spike. Renders 3DGS PLY files with the shared core and sets the
// result as each file's custom Finder icon (stored in the file's extended attributes; the
// PLY data itself is not touched).
//
//   splatthumb-icon set <file-or-folder>...     set or refresh icons
//   splatthumb-icon remove <file-or-folder>...  remove icons this tool set
//   splatthumb-icon watch <folder>              set icons now, then keep them up to date
#import <Foundation/Foundation.h>

#include <cstdio>
#include <cstring>

#import "STIconSetter.h"

namespace {

int usage() {
    std::fprintf(stderr, "usage: splatthumb-icon set|remove <file-or-folder>...\n"
                         "       splatthumb-icon watch <folder>\n");
    return 2;
}

void print(NSString* s) {
    std::printf("%s\n", s.UTF8String);
}

} // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        if (argc < 3) {
            return usage();
        }
        setvbuf(stdout, nullptr, _IOLBF, 0);
        const bool set = std::strcmp(argv[1], "set") == 0;
        const bool remove = std::strcmp(argv[1], "remove") == 0;
        const bool watch = std::strcmp(argv[1], "watch") == 0 && argc == 3;
        if (!set && !remove && !watch) {
            return usage();
        }
        for (int i = 2; i < argc; ++i) {
            for (NSString* f in [STIconSetter plyFilesAt:@(argv[i])]) {
                print(remove ? [STIconSetter removeIconForFile:f]
                             : [STIconSetter setIconForFile:f]);
            }
        }
        if (watch) {
            NSString* folder = @(argv[2]);
            STFolderWatcher* watcher =
                [[STFolderWatcher alloc] initWithFolder:folder
                                                 settle:3.0
                                                handler:^(NSString* path) {
                                                    print([STIconSetter setIconForFile:path]);
                                                }];
            [watcher start];
            print([NSString stringWithFormat:@"watching %@ (Ctrl+C to stop)", folder]);
            dispatch_main();
        }
    }
    return 0;
}
