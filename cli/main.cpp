#include <cstdio>
#include <string_view>

#include "splatthumb/version.h"

namespace {

void print_usage() {
    std::puts("usage: splatthumb-cli --version");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        const std::string_view v = splatthumb::version();
        std::printf("splatthumb-cli %.*s\n", static_cast<int>(v.size()), v.data());
        return 0;
    }
    print_usage();
    return 2;
}
