// Protocol fuzz harness for length-prefixed frame decoding (milestone 35 scaffold).
//
// Standalone replay (default build):
//   cmake -S . -B build -DTCP_SERVER_BUILD_FUZZ=ON && cmake --build build --target fuzz_decoder
//   ./build/fuzz_decoder path/to/corpus.bin
//   ./build/fuzz_decoder -     # read stdin (binary)
//
// LLVM libFuzzer (Clang/GCC; CMake option TCP_SERVER_FUZZ_LIBFUZZER=ON):
//   cmake ... -DTCP_SERVER_BUILD_FUZZ=ON -DTCP_SERVER_FUZZ_LIBFUZZER=ON
//   cmake --build build --target fuzz_decoder
//   ./build/fuzz_decoder -runs=1000 corpus_dir/

#include <tcp_server/protocol/frame_decoder.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

using tcp_server::protocol::try_decode_frame;

// Matches typical server limits; decoder caps internally to UINT32_MAX payload.
constexpr std::uint64_t k_max_payload_bytes = 65536;

[[nodiscard]] auto read_all_binary(std::istream& in) -> std::vector<std::byte> {
    std::string chunk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>{});
    std::vector<std::byte> out(chunk.size());
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(chunk[static_cast<std::string::size_type>(i)]));
    }
    return out;
}

void run_decode(std::span<const std::byte> buffer) {
    (void)try_decode_frame(buffer, k_max_payload_bytes);
}

}  // namespace

#if defined(TCP_SERVER_LIBFUZZER)

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
    run_decode({reinterpret_cast<const std::byte*>(data), size});  // NOLINT
    return 0;
}

#else

auto main(int argc, char** argv) -> int {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <file|- (stdin)>\n";
        return 1;
    }

    const std::string_view arg{argv[1]};
    std::vector<std::byte> buf;
    if (arg == "-") {
        buf = read_all_binary(std::cin);
    } else {
        std::ifstream in(std::string{arg}, std::ios::binary);
        if (!in) {
            std::cerr << "failed to open: " << arg << '\n';
            return 2;
        }
        buf = read_all_binary(in);
    }

    run_decode(buf);
    return 0;
}

#endif
