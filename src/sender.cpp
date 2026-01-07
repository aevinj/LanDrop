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

#include "headers.hpp"

using udp = boost::asio::ip::udp;

class Sender {
private:
    boost::asio::io_context io;
    udp::socket sock{io};
    unsigned char buff[2048];
    const std::regex r{R"(HERE\s(\S+)\s(\d+)\s*)"};

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
        udp::endpoint broadcast_endpoint(boost::asio::ip::address_v4::broadcast(), 40000);
        sock.send_to(boost::asio::buffer(bMsg), broadcast_endpoint);

        using clock = std::chrono::steady_clock;
        auto deadline = clock::now() + std::chrono::milliseconds(1000);

        while (clock::now() < deadline) {
            udp::endpoint from;
            boost::system::error_code ec;

            std::size_t len = sock.receive_from(
                boost::asio::buffer(buff, sizeof(buff)),
                from,
                /*flags=*/0,
                ec
            );

            if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (ec) {
                // Real error; ignore for discovery loop
                continue;
            }
            if (len == 0) continue;

            std::string reply(reinterpret_cast<char*>(buff), len);

            std::smatch m;
            if (std::regex_match(reply, m, r)) {
                addReceiver(std::move(m), from.address().to_string());
            }
        }
    }

    udp::endpoint formReceiver(int idx) {
        const auto& choice = discovered_devices[idx];
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
        receiver.port(40000);
        std::string msg("CHOSEN"); // not inlining since string literal trails with null terminator
        sock.send_to(boost::asio::buffer(msg), receiver);
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

public:
    Sender() {
        sock.open(udp::v4());
        sock.set_option(boost::asio::socket_base::broadcast(true));
        sock.non_blocking(true);
    }

    void sendMsg() {
        findReceiver();

        if (discovered_devices.empty()) {
            std::cerr << "No devices discovered.\n";
            return;
        }

        udp::endpoint receiver = getDesiredDiscoveredDevice();
        
        std::ifstream file("example.txt", std::ios::binary);
        if (!file) {
            throw std::runtime_error("Couldn't find input file");
        }

        constexpr std::uint16_t chunk_size = 1200;
        const std::uint64_t file_size = static_cast<std::uint64_t>(std::filesystem::file_size("example.txt"));
        const std::uint32_t total_chunks = static_cast<std::uint32_t>((file_size + chunk_size - 1) / chunk_size);

        MetaHeader mh;
        mh.fileSize = file_size;
        mh.chunkSize = chunk_size;
        mh.totalChunks = total_chunks;
        mh.transferID = nextTransferID();

        mh.ext[0] = 't';
        mh.ext[1] = 'x';
        mh.ext[2] = 't';
        mh.ext[3] = '\0';

        auto metaBytes = serialiseHeader(mh);
        sock.send_to(boost::asio::buffer(metaBytes), receiver);

        std::vector<std::uint8_t> payload(chunk_size);      

        for (std::uint32_t chunk_id = 0; chunk_id < total_chunks; ++chunk_id) {
            file.read(reinterpret_cast<char*>(payload.data()), payload.size());
            std::streamsize got = file.gcount();
            if (got <= 0) break;

            DataHeader dh{};
            dh.transferID    = mh.transferID;
            dh.chunkID       = chunk_id;
            dh.payloadLength = static_cast<std::uint16_t>(got);

            auto dh_bytes = serialiseHeader(dh);

            std::array<boost::asio::const_buffer, 2> bufs{
                boost::asio::buffer(dh_bytes),
                boost::asio::buffer(payload.data(), static_cast<std::size_t>(got))
            };

            sock.send_to(bufs, receiver);
        }

    }
};

int main() {
    Sender sender;
    sender.sendMsg();
}
