#include <iostream>
#include <boost/asio.hpp>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

#include "handler.cpp"

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

    bool have_meta = false;
    MetaHeader meta{};
    std::ofstream out;
    std::vector<bool> received;
    std::uint32_t received_count = 0;

    static constexpr std::size_t META_LEN = 27;
    static constexpr std::size_t DATA_LEN = 15;

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
            std::size_t len = dataSock.receive_from(
                boost::asio::buffer(buff, sizeof(buff)),
                senderDataEndpoint
            );
            if (len == 0) continue;

            // We need a byte-view for parseHeader (prototype: copy into vector)
            std::vector<std::uint8_t> pkt(buff, buff + len);

            const std::uint8_t t = pkt[0];

            if (t == static_cast<std::uint8_t>(Type::META)) {
                if (len < META_LEN) {
                    std::cout << "META too short: " << len << "\n";
                    continue;
                }

                Header h = parseHeader(pkt);
                meta = std::get<MetaHeader>(h);
                have_meta = true;

                received.assign(meta.totalChunks, false);
                received_count = 0;

                out.open("received_example.txt", std::ios::binary | std::ios::trunc);
                if (!out) {
                    std::cout << "Failed to open received_example.txt\n";
                    have_meta = false;
                    continue;
                }

                std::cout << "META received: transferID=" << meta.transferID
                          << " fileSize=" << meta.fileSize
                          << " chunkSize=" << meta.chunkSize
                          << " totalChunks=" << meta.totalChunks
                          << "\n";
                continue;
            }

            if (t == static_cast<std::uint8_t>(Type::DATA)) {
                if (!have_meta) {
                    std::cout << "DATA before META (ignored)\n";
                    continue;
                }
                if (len < DATA_LEN) {
                    std::cout << "DATA too short: " << len << "\n";
                    continue;
                }

                Header h = parseHeader(pkt);
                DataHeader dh = std::get<DataHeader>(h);

                if (dh.transferID != meta.transferID) continue;
                if (dh.chunkID >= meta.totalChunks) continue;

                const std::size_t payload_off = DATA_LEN;

                if (payload_off + dh.payloadLength > len) {
                    std::cout << "Malformed DATA: payloadLength=" << dh.payloadLength
                              << " len=" << len << "\n";
                    continue;
                }

                const std::uint64_t offset =
                    static_cast<std::uint64_t>(dh.chunkID) * meta.chunkSize;

                out.seekp(static_cast<std::streamoff>(offset));
                out.write(reinterpret_cast<char*>(buff + payload_off), dh.payloadLength);

                if (!received[dh.chunkID]) {
                    received[dh.chunkID] = true;
                    ++received_count;
                }

                if (received_count == meta.totalChunks) {
                    out.flush();
                    out.close();
                    std::cout << "Transfer complete: received_example.txt written\n";
                    break;
                }

                continue;
            }

            std::cout << "Unknown packet type: " << static_cast<int>(t) << "\n";
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
