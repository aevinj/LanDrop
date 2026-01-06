#include <stdexcept>
#include <type_traits>
#include <cstdint>
#include <vector>
#include <variant>
#include <array>
#include <iostream>
#include <span>

using uint64 = std::uint64_t;
using uint32 = std::uint32_t;
using uint16 = std::uint16_t;
using uint8 = std::uint8_t;
using std::vector;

enum class Type : uint8 {
    META = 1,
    DATA = 2,
    TERMINATION = 3
};

struct MetaHeader {
   Type type = Type::META;
   uint64 transferID;
   uint64 fileSize;
   uint16 chunkSize;
   uint32 totalChunks;
   std::array<char, 4> ext;
};

struct DataHeader {
    Type type = Type::DATA;
    uint64 transferID;
    uint32 chunkID;
    uint16 payloadLength;
};

using Header = std::variant<MetaHeader, DataHeader>;

template <typename T>
inline void pushX(vector<uint8>& res, T v) {
    static_assert(std::is_same_v<T, uint16> ||
                    std::is_same_v<T, uint32> ||
                    std::is_same_v<T, uint64>, 
                    "pushX only supports uint16/uint32/uint64");
    constexpr std::size_t bits = sizeof(T) * 8;

    for (int shift = bits - 8; shift >= 0; shift -= 8) {
        res.push_back(static_cast<uint8>((v >> shift) & 0xFF));
    }
}

template <typename T>
T readX(std::span<const uint8> buf, std::size_t off) {
    static_assert(std::is_same_v<T, uint16> ||
                    std::is_same_v<T, uint32> ||
                    std::is_same_v<T, uint64>,
                    "readX only supports uint16/uint32/uint64");
    constexpr std::size_t N = sizeof(T);

    if (off + N > buf.size()) {
        throw std::runtime_error("readX out of range");
    }

    T value = 0;
    for (std::size_t i = 0; i < N; ++i) {
        value = static_cast<T>((value << 8) | buf[off + i]);
    }

    return value;
}

vector<uint8> serialiseHeader(const Header& header) {
    vector<uint8> res;

    if (std::holds_alternative<MetaHeader>(header)) {
        res.reserve(27);
        const auto& h = std::get<MetaHeader>(header);
        res.push_back(static_cast<uint8>(h.type));
        pushX<uint64>(res, h.transferID);
        pushX<uint64>(res, h.fileSize);
        pushX<uint16>(res, h.chunkSize);
        pushX<uint32>(res, h.totalChunks);
        for (char c : h.ext) res.push_back(static_cast<uint8>(c));
    } else {
        res.reserve(15);
        const auto& h = std::get<DataHeader>(header);
        res.push_back(static_cast<uint8>(h.type));
        pushX<uint64>(res, h.transferID);
        pushX<uint32>(res, h.chunkID);
        pushX<uint16>(res, h.payloadLength);
    }

    return res;
}

Header parseHeader(std::span<const uint8> buf) {
    if (buf.size() < 1) throw std::runtime_error("Empty buffer");

    const uint8 type = buf.front();

    if (type == static_cast<uint8>(Type::META)) {
        constexpr std::size_t META_LEN = 27;
        if (buf.size() < META_LEN) throw std::runtime_error("Invalid Meta header");

        MetaHeader mh; 
        std::size_t off = 1;

        mh.transferID  = readX<uint64>(buf, off); off += 8;
        mh.fileSize    = readX<uint64>(buf, off); off += 8; 
        mh.chunkSize   = readX<uint16>(buf, off); off += 2;
        mh.totalChunks = readX<uint32>(buf, off); off += 4;

        for (std::size_t i = 0; i < mh.ext.size(); ++i) {
            mh.ext[i] = static_cast<char>(buf[off + i]);
        }

        return mh;
    } else if (type == static_cast<uint8>(Type::DATA)) {
        constexpr std::size_t DATA_LEN = 15;
        if (buf.size() < DATA_LEN) throw std::runtime_error("Invalid Data header");

        DataHeader dh;
        std::size_t off = 1;

        dh.transferID    = readX<uint64>(buf, off); off += 8;
        dh.chunkID       = readX<uint32>(buf, off); off += 4;
        dh.payloadLength = readX<uint16>(buf, off); off += 2;

        return dh;
    }

    throw std::runtime_error("Unknown packet type");
}
