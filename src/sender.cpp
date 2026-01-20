#include <boost/asio.hpp>
#include <string>
#include <regex>
#include <vector>
#include <tuple>
#include <chrono>
#include <thread>
#include <iostream>
#include <optional>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <unordered_map>

#include "headers.hpp"

using udp = boost::asio::ip::udp;

class Sender {
private:
    boost::asio::io_context io;
    udp::socket dataTransferSock{io}, ackSock{io};
    static constexpr unsigned short receiverDiscoveryPort = 40000;
    static constexpr unsigned short ackPort = 40002;
    unsigned char buff[2048];
    const std::regex r{R"(HERE\s(\S+)\s(\d+)\s*)"};

    std::ifstream file;
    std::string extension;
    std::string inputPath;

    static constexpr std::uint16_t chunkSize = 1200;
    static constexpr std::size_t WINDOW = 300;
    using Clock = std::chrono::steady_clock;
    static constexpr auto RTO = std::chrono::milliseconds{50};

    std::vector<std::uint8_t> payload = std::vector<std::uint8_t>(chunkSize);

    // device name, port number, sender address (from UDP endpoint, not payload)
    std::vector<std::tuple<std::string, unsigned short, std::string>> discovered_devices;

    std::optional<unsigned short> validPort(const std::string &port_str) {
        unsigned long port_ul = 0;
        try {
            port_ul = std::stoul(port_str);
        } catch (...) {
            return std::nullopt;
        }

        if (port_ul == 0 || port_ul > 65535) {
            return std::nullopt;
        } else {
            return static_cast<unsigned short>(port_ul);
        }
    }

    void addReceiver(std::smatch&& m, const std::string addr_str) {
        const std::string dev_name = m[1].str();
        const std::string port_str = m[2].str();

        if (auto port_us = validPort(port_str)) { 
            bool seen = false;
            for (const auto& d : discovered_devices) {
                if (std::get<2>(d) == addr_str && std::get<1>(d) == port_us.value()) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                discovered_devices.emplace_back(dev_name, port_us.value(), addr_str);
            }
        } 
    }

    void findReceiver() {
        const std::string bMsg = "DISCOVER";
        udp::endpoint broadcast_endpoint(boost::asio::ip::address_v4::broadcast(), receiverDiscoveryPort);
        dataTransferSock.send_to(boost::asio::buffer(bMsg), broadcast_endpoint);

        using clock = std::chrono::steady_clock;
        auto deadline = clock::now() + std::chrono::milliseconds(1000);

        while (clock::now() < deadline) {
            udp::endpoint from;
            boost::system::error_code ec;

            std::size_t len = 
                dataTransferSock.receive_from(boost::asio::buffer(buff, sizeof(buff)), from, 0, ec);
            if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (ec || len == 0) continue;

            std::string reply(reinterpret_cast<char*>(buff), len);

            std::smatch m;
            if (std::regex_match(reply, m, r)) {
                addReceiver(std::move(m), from.address().to_string());
            }
        }
    }

    udp::endpoint formReceiver(int idx) {
        const auto& choice          = discovered_devices[idx];
        const std::string& addr_str = std::get<2>(choice);
        const unsigned short port   = std::get<1>(choice);

        boost::system::error_code ec_addr;
        auto addr = boost::asio::ip::make_address(addr_str, ec_addr);
        if (ec_addr) {
            throw "Could not form receiver";
        }

        return udp::endpoint(addr, port);
    }

    void sendReceiverChosen(udp::endpoint receiver) {
        receiver.port(receiverDiscoveryPort);
        std::string msg("CHOSEN"); // not inlining since string literal trails with null terminator
        dataTransferSock.send_to(boost::asio::buffer(msg), receiver);
    }

    udp::endpoint getDesiredDiscoveredDevice() {
        int i = 0;

        std::cout << "Discovered Devices:" << std::endl << std::endl;
        for (const auto dev : discovered_devices) {
            std::cout << ++i  << ") " << std::get<0>(dev) << " port: " << std::get<1>(dev) << std::endl;
        }

        std::size_t choice = 0;
        while (true) {
            std::cout << std::endl << "Enter choice: " << std::endl;
            if (std::cin >> choice && choice > 0 && choice <= discovered_devices.size()) {
                udp::endpoint receiver = formReceiver(choice - 1);
                sendReceiverChosen(receiver);
                return receiver;
            }
        }
    }

    std::uint64_t nextTransferID() {
        static std::uint64_t currTransferID = 0;
        return currTransferID++;
    }

    std::optional<std::pair<std::uint64_t, std::vector<std::uint32_t>>> receiveAckBatch() {
        udp::endpoint from;
        boost::system::error_code ec;

        std::size_t n = ackSock.receive_from(boost::asio::buffer(buff, sizeof(buff)), from, 0, ec);
        if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again || ec) {
            return std::nullopt;
        }
        if (n < ACK_BATCH_LEN) return std::nullopt;

        std::span<const std::uint8_t> pkt(reinterpret_cast<const std::uint8_t*>(buff), n);

        if (pkt[0] != static_cast<std::uint8_t>(Type::ACK_BATCH)) return std::nullopt;

        std::size_t off = 1;
        std::uint64_t tid = readX<std::uint64_t>(pkt, off); off += 8;
        std::uint16_t count = readX<std::uint16_t>(pkt, off); off += 2;

        std::size_t need = ACK_BATCH_LEN + static_cast<std::size_t>(count) * 4;
        if (n < need) return std::nullopt;

        std::vector<std::uint32_t> ids;
        ids.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            ids.push_back(readX<std::uint32_t>(pkt, off));
            off += 4;
        }

        return std::make_optional(std::make_pair(tid, std::move(ids)));
    }

    void sendChunk(const std::uint32_t chunkID, const std::uint64_t transferID, udp::endpoint &receiver) {
        const std::uint64_t offset = static_cast<std::uint64_t>(chunkID) * chunkSize;

        file.seekg(static_cast<std::streamoff>(offset));
        file.read(reinterpret_cast<char*>(payload.data()), payload.size());

        std::streamsize got = file.gcount();
        if (got <= 0) return;

        DataHeader dh{};
        dh.transferID    = transferID;
        dh.chunkID       = chunkID;
        dh.payloadLength = static_cast<std::uint16_t>(got);

        auto dh_bytes = serialiseHeader(dh);

        std::array<boost::asio::const_buffer, 2> bufs{
            boost::asio::buffer(dh_bytes),
            boost::asio::buffer(payload.data(), static_cast<std::size_t>(got))
        };

        dataTransferSock.send_to(bufs, receiver);
    }

public:
    Sender(std::ifstream&& file_, std::string inputPath_, std::string extension_)
    : file(std::move(file_)), 
    inputPath(inputPath_), 
    extension(extension_) {
        dataTransferSock.open(udp::v4());
        dataTransferSock.set_option(boost::asio::socket_base::broadcast(true));
        dataTransferSock.non_blocking(true);

        ackSock.open(udp::v4());
        ackSock.bind(udp::endpoint(udp::v4(), ackPort));
        ackSock.non_blocking(true);
    }

    void sendMsg() {
        findReceiver();

        if (discovered_devices.empty()) {
            std::cerr << "No devices discovered.\n";
            return;
        }

        udp::endpoint receiver = getDesiredDiscoveredDevice();
        
        const std::uint64_t fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(std::filesystem::path(inputPath)));
        const std::uint32_t totalChunks = static_cast<std::uint32_t>((fileSize + chunkSize - 1) / chunkSize);

        MetaHeader mh;
        mh.fileSize = fileSize;
        mh.chunkSize = chunkSize;
        mh.totalChunks = totalChunks;
        mh.transferID = nextTransferID();

        int i = 0;
        for (; i < std::min(7, static_cast<int>(extension.size())); ++i) {
            mh.ext[i] = extension[i];
        }
        mh.ext[i] = '\0';

        auto metaBytes = serialiseHeader(mh);
        dataTransferSock.send_to(boost::asio::buffer(metaBytes), receiver);

        // ----- Data -------

        std::vector<bool> acked(totalChunks, false);
        std::unordered_map<std::uint32_t, Clock::time_point> inFlight;
        std::uint32_t doneCount = 0;
        std::uint32_t nextToSend = 0;

        std::uint32_t lastPrintedPercent = 0;

        while (doneCount < totalChunks) {
            // process any incoming ACKS
            while (true) {
                auto batchOpt = receiveAckBatch();
                if (!batchOpt) break;

                auto& [tid, ids] = batchOpt.value();
                if (tid != mh.transferID) continue;

                for (auto id : ids) {
                    if (id < totalChunks && !acked[id]) {
                        acked[id] = true;
                        ++doneCount;
                        inFlight.erase(id);

                        const std::uint32_t percent =
                            static_cast<std::uint32_t>((static_cast<std::uint64_t>(doneCount) * 100) / totalChunks);
                        
                        if (percent > lastPrintedPercent) {
                            // If we jumped multiple % due to batched ACKs, print each missing step once.
                            for (std::uint32_t p = lastPrintedPercent + 1; p <= percent; ++p) {
                                std::cout << "Progress: " << p << "% (" << doneCount << "/" << totalChunks
                                        << " chunks acked)\n";
                            }
                            lastPrintedPercent = percent;
                        }
                    }
                }
            }

            // fill window
            while (inFlight.size() < WINDOW && nextToSend < totalChunks) {
                sendChunk(nextToSend, mh.transferID, receiver);
                inFlight[nextToSend++] = Clock::now();
            }

            // retransmit timed out chunks
            for (auto& [chunk, time] : inFlight) {
                if (Clock::now() - time > RTO) {
                    sendChunk(chunk, mh.transferID, receiver);
                    time = Clock::now();
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

std::string extractExtension(const std::string &path) {
    std::size_t fullStop = path.find_last_of(".");
    return path.substr(fullStop + 1);
}

int main(int argc, char *argv[]) {
    if (argc == 1 || argc > 2) {
        std::cerr << "Invalid input arguments" << std::endl;
        return 1;
    }

    std::string pathName(argv[1]);
    std::ifstream file(pathName, std::ios::binary);
    if (!file) {
        std::cerr << "Could not find input file" << std::endl;
        return 1;
    }

    Sender sender(std::move(file), pathName, extractExtension(pathName));
    sender.sendMsg();
}
