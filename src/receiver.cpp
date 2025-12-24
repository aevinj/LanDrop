#include <iostream>
#include <boost/asio.hpp>
#include <iomanip>

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

    cout << std::endl; // for flushing output

    cout << std::dec << std::setfill(' ');
}

int main() {
    boost::asio::io_context io;
    udp::socket sock(io);
    sock.open(udp::v4());
    sock.bind(udp::endpoint(udp::v4(), 40000));

    // define a mutable buffer asio::flat_buffer?
    // enter some loop now
    // use receive_from but where does it get the sender_endpoint from?

    unsigned char buff[2048];
    udp::endpoint sender_endpoint; 

    while (true) {
        std::size_t len = sock.receive_from(boost::asio::buffer(buff, sizeof(buff)), sender_endpoint);
        
        if (len > 0) {
            cout << "Sender port " << sender_endpoint.port() << endl;
            cout << "Sender address " << sender_endpoint.address().to_string() << endl;
            cout << "Received " << len << " bytes" << endl;

            dump_hex(buff, len);
        }
    }
}
