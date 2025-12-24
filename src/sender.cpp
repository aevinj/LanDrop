#include <boost/asio.hpp>
#include <string>
#include <regex>
#include <vector>
#include <tuple>
#include <chrono>
#include <thread>
#include <iostream>

using udp = boost::asio::ip::udp;

int main() {
    boost::asio::io_context io;
    udp::socket sock(io);
    sock.open(udp::v4());

    sock.set_option(boost::asio::socket_base::broadcast(true));

    udp::endpoint broadcast_endpoint(boost::asio::ip::address_v4::broadcast(), 40000);
    const std::string bMsg = "DISCOVER";
    sock.send_to(boost::asio::buffer(bMsg), broadcast_endpoint);

    sock.non_blocking(true);

    unsigned char buff[2048];

    // Capture: device_name (non-space), port (digits)
    const std::regex r(R"(HERE\s(\S)+\s(\d+)\s*)");

    // device name, port number, sender address (from UDP endpoint, not payload)
    std::vector<std::tuple<std::string, unsigned short, std::string>> discovered_devices;

    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(3000);

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
        if (!std::regex_match(reply, m, r)) {
            std::cout << "failed to match" << std::endl;
            continue;
        }

        const std::string dev_name = m[1].str();
        const std::string port_str = m[2].str();

        unsigned long port_ul = 0;
        try {
            port_ul = std::stoul(port_str);
        } catch (...) {
            continue;
        }
        if (port_ul == 0 || port_ul > 65535) continue;

        const auto addr_str = from.address().to_string();
        const auto port_us  = static_cast<unsigned short>(port_ul);

        bool seen = false;
        for (const auto& d : discovered_devices) {
            if (std::get<2>(d) == addr_str && std::get<1>(d) == port_us) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            discovered_devices.emplace_back(dev_name, port_us, addr_str);
        }
    }

    if (discovered_devices.empty()) {
        std::cerr << "No devices discovered.\n";
        return 1;
    }

    const auto& top = discovered_devices.front();
    const std::string& addr_str = std::get<2>(top);
    const unsigned short port   = std::get<1>(top);

    boost::system::error_code ec_addr;
    auto addr = boost::asio::ip::make_address(addr_str, ec_addr);
    if (ec_addr) {
        std::cerr << "Invalid discovered address: " << addr_str << "\n";
        return 1;
    }

    udp::endpoint receiver(addr, port);

    const std::string msg = "aevin";
    sock.send_to(boost::asio::buffer(msg), receiver);

    return 0;
}

