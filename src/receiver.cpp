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
    udp::socket discoverySock{io};
    udp::socket dataSock{io};

    const string device_name = "Aevin-PC";
    const unsigned short discoveryPort = 40000;
    const unsigned short dataPort = 40001;

    unsigned char buff[2048];
    udp::endpoint senderDiscoveryEndpoint;
    udp::endpoint senderDataEndpoint;

    bool handleDiscovery(const string &msg) {
        if (msg == "DISCOVER") {
            string reply = "HERE " + device_name + " " + std::to_string(dataPort);
            discoverySock.send_to(boost::asio::buffer(reply), senderDiscoveryEndpoint);
            return false;
        } else if (msg == "CHOSEN") {
            return true;
        }
        return false;
    }

    void outputPayload(const std::size_t len, const string &msg) {
        cout << "Sender port " << senderDataEndpoint.port() << endl;
        cout << "Sender address " << senderDataEndpoint.address().to_string() << endl;
        cout << "Received " << len << " bytes" << endl;
        cout << "Message: " << msg << endl;
        cout << endl;
    }

    void receiveData() {
        while (true) {
            std::size_t len = dataSock.receive_from(boost::asio::buffer(buff, sizeof(buff)), senderDataEndpoint);

            if (len == 0) {
                continue;
            }

            string msg(reinterpret_cast<char*>(buff), len);

            if (msg == "aevin") {
                outputPayload(len, msg);
            } else {
                cout << "Unknown data: " << msg << endl;
            }
        }
    }

public:
    Receiver() {
        discoverySock.open(udp::v4());
        discoverySock.bind(udp::endpoint(udp::v4(), discoveryPort));

        dataSock.open(udp::v4());
        dataSock.bind(udp::endpoint(udp::v4(), dataPort));
    }

    void listen() {
        while (true) {
            std::size_t len = discoverySock.receive_from(boost::asio::buffer(buff, sizeof(buff)), senderDiscoveryEndpoint);

            if (len == 0) {
                continue;
            }

            string msg(reinterpret_cast<char*>(buff), len);

            if (handleDiscovery(msg)) {
                break;
            }
        }
        receiveData();
    }
};

int main() {
    Receiver receiver;
    receiver.listen();
}
