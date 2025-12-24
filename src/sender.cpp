#include <boost/asio.hpp>

using udp = boost::asio::ip::udp;

int main() {
    boost::asio::io_context io;
    udp::socket sock(io);
    sock.open(udp::v4());

    auto addr = boost::asio::ip::make_address("127.0.0.1");
    udp::endpoint dst(addr, 40000);

    std::string msg = "aevin";
    sock.send_to(boost::asio::buffer(msg), dst);

}
