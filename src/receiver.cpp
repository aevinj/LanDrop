#include <iostream>
#include <boost/asio.hpp>
#include <iomanip>
#include <string>

using udp = boost::asio::ip::udp;
using std::cout;
using std::endl;
using std::string;

class Receiver {
private:
    boost::asio::io_context io;
    udp::socket sock{io};

    const string device_name = "Aevin-PC";
    const unsigned short port = 40000;

    unsigned char buff[2048];
    udp::endpoint sender_endpoint;

    bool handleDiscovery(const string &msg) {
        if (msg == "DISCOVER") {
            string reply = "HERE " + device_name + " " + std::to_string(port);
            sock.send_to(boost::asio::buffer(reply), sender_endpoint);
            return true;
        }
        return false;
    }

    void outputPayload(const std::size_t len, const string &msg) {
        cout << "Sender port " << sender_endpoint.port() << endl;
        cout << "Sender address " << sender_endpoint.address().to_string() << endl;
        cout << "Received " << len << " bytes" << endl;
        cout << "Message: " << msg << endl;
        cout << endl;
    }

public:
    Receiver() {
        sock.open(udp::v4());
        sock.bind(udp::endpoint(udp::v4(), 40000));
    }

    void listen() {
        while (true) {
            std::size_t len = sock.receive_from(boost::asio::buffer(buff, sizeof(buff)), sender_endpoint);

            if (len == 0) {
                continue;
            }

            string msg(reinterpret_cast<char*>(buff), len);

            if (!handleDiscovery(msg)) {
                outputPayload(len, msg);
            }
        }
    }
};

int main() {
    Receiver receiver;
    receiver.listen();
}
