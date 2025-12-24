#include <iostream>
#include <boost/asio.hpp>
#include <iomanip>
#include <string>

using udp = boost::asio::ip::udp;
using std::cout;
using std::endl;

void dump_hex(const unsigned char* buff, size_t len) {
    cout << std::hex << std::setfill('0');

    for (size_t i = 0; i < len; ++i) {
        cout << std::setw(2)
            << static_cast<int>(buff[i])
            << ' ';
    }

    cout << std::endl;

    cout << std::dec << std::setfill(' ');
}

int main() {
    boost::asio::io_context io;
    udp::socket sock(io);
    sock.open(udp::v4());
    sock.bind(udp::endpoint(udp::v4(), 40000));

    const std::string device_name = "Aevin-PC";
    const unsigned short service_port = 40000;

    unsigned char buff[2048];
    udp::endpoint sender_endpoint;

    while (true) {
        std::size_t len = sock.receive_from(boost::asio::buffer(buff, sizeof(buff)), sender_endpoint);

        if (len == 0) continue;

        std::string msg(reinterpret_cast<char*>(buff), len);

        if (msg == "DISCOVER") {
            std::cout << "HELLO BIATCH" << std::endl;
            std::string reply = "HERE " + device_name + " " + std::to_string(service_port);
            sock.send_to(boost::asio::buffer(reply), sender_endpoint);
            continue;
        }

        cout << "Sender port " << sender_endpoint.port() << endl;
        cout << "Sender address " << sender_endpoint.address().to_string() << endl;
        cout << "Received " << len << " bytes" << endl;

        dump_hex(buff, len);
    }
}

