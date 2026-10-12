// Runs a fuzz target over files or directories without libFuzzer (any compiler).
// Used by ctest to replay the seed corpus and the saved crash reproducers.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int runFile(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(buf.data(), buf.size());
    return 1;
}

int main(int argc, char **argv)
{
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path p(argv[i]);
        if (std::filesystem::is_directory(p)) {
            for (const auto &e : std::filesystem::recursive_directory_iterator(p))
                if (e.is_regular_file())
                    n += runFile(e.path());
        } else if (std::filesystem::exists(p)) {
            n += runFile(p);
        }
    }
    std::printf("%d inputs executed without crashing\n", n);
    return 0;
}
