// tools/fake_server.cpp - stands in for llama-server during UI verification.
//
// Prints the one line the launcher parses for the KV split and then holds a
// working set large enough to be visible on a bar scaled to the whole machine.
#include <cstdio>
#include <vector>
#include <windows.h>

int main() {
    std::printf("build: 11026 (fake)\n");
    std::printf("load_tensors: offloaded 41/48 layers to GPU\n");
    std::fflush(stdout);

    std::vector<char> ballast(800ull * 1024 * 1024, 1);
    for (size_t i = 0; i < ballast.size(); i += 4096) ballast[i] = 1;
    std::printf("llama server listening on http://127.0.0.1:8080\n");
    std::fflush(stdout);

    ::Sleep(600000);
    return 0;
}
