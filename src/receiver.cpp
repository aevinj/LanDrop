#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <span>
#include <chrono>

#include "headers.hpp"

using udp = boost::asio::ip::udp;
using std::string;

class Receiver {
private:
    boost::asio::io_context io;
    udp::socket discoverySock{io};
    udp::socket dataSock{io};
    udp::socket ackSock{io};

    const string device_name = "Aevin-PC";
    static constexpr unsigned short discoveryPort = 40000;
    static constexpr unsigned short dataPort = 40001;
    static constexpr unsigned short senderAckPort = 40002;

    std::uint8_t buff[2048];
    udp::endpoint senderDiscoveryEndpoint;
    udp::endpoint senderDataEndpoint;

    bool have_meta = false;
    MetaHeader meta{};
    std::ofstream out;
    std::vector<bool> received;
    std::uint32_t received_count = 0;

    static constexpr std::size_t MAX_ACK_IDS = 256;
    static constexpr auto ACK_INTERVAL = std::chrono::milliseconds(5);
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastAckSend = Clock::now();
    std::vector<std::uint32_t> pendingAcks;

    std::optional<boost::asio::ip::address> chosenSenderAddr;

    bool handleDiscovery(const string &msg) {
        if (msg == "DISCOVER") {
            string reply = "HERE " + device_name + " " + std::to_string(dataPort);
            discoverySock.send_to(boost::asio::buffer(reply), senderDiscoveryEndpoint);
            return false;
        } else if (msg == "CHOSEN") {
            chosenSenderAddr = senderDiscoveryEndpoint.address();
            return true;
        }
        return false;
    }

    void flushAckBatch() {
        if (pendingAcks.empty() || !have_meta) return;

        std::vector<std::uint8_t> msg;
        msg.reserve(ACK_BATCH_LEN + pendingAcks.size() * 4);

        msg.push_back(static_cast<std::uint8_t>(Type::ACK_BATCH));
        pushX<std::uint64_t>(msg, meta.transferID);
        pushX<std::uint16_t>(msg, static_cast<std::uint16_t>(pendingAcks.size()));

        for (auto id : pendingAcks) {
            pushX<std::uint32_t>(msg, id);
        }

        ackSock.send_to(
            boost::asio::buffer(msg),
            udp::endpoint(senderDataEndpoint.address(), senderAckPort)
        );

        pendingAcks.clear();
        lastAckSend = Clock::now();
    }

    void receiveData() {
        while (true) {
            std::size_t len = dataSock.receive_from(
                boost::asio::buffer(buff, sizeof(buff)),
                senderDataEndpoint
            );
            if (len == 0) continue;

            if (chosenSenderAddr.has_value() && chosenSenderAddr.value() != senderDataEndpoint.address()) continue;

            std::span<const std::uint8_t> pkt(buff, buff + len);
            const std::uint8_t t = pkt[0];

            if (t == static_cast<std::uint8_t>(Type::META)) {
                if (len < META_LEN) continue;

                Header h = parseHeader(pkt);
                meta = std::get<MetaHeader>(h);
                have_meta = true;

                received.assign(meta.totalChunks, false);
                received_count = 0;

                std::string outputFile = "received_file." + std::string(meta.ext);

                out.open(outputFile, std::ios::binary | std::ios::trunc);
                if (!out) {
                    have_meta = false;
                    continue;
                }
                continue;
            }

            if (t == static_cast<std::uint8_t>(Type::DATA)) {
                if (!have_meta) continue;
                if (len < DATA_LEN) continue;

                Header h = parseHeader(pkt);
                DataHeader dh = std::get<DataHeader>(h);

                if (dh.transferID != meta.transferID) continue;
                if (dh.chunkID >= meta.totalChunks) continue;

                const std::size_t payload_off = DATA_LEN;
                if (payload_off + dh.payloadLength > len) continue;

                if (received[dh.chunkID]) {
                    // still ACK it (idempotent), but don't rewrite
                    pendingAcks.push_back(dh.chunkID);
                } else {
                    const std::uint64_t offset = static_cast<std::uint64_t>(dh.chunkID) * meta.chunkSize;

                    out.seekp(static_cast<std::streamoff>(offset));
                    out.write(reinterpret_cast<char*>(buff + payload_off), dh.payloadLength);

                    received[dh.chunkID] = true;
                    ++received_count;

                    pendingAcks.push_back(dh.chunkID);
                }

                if (pendingAcks.size() >= MAX_ACK_IDS || (Clock::now() - lastAckSend) >= ACK_INTERVAL) {
                    flushAckBatch();
                }

                if (received_count == meta.totalChunks) {
                    out.flush();
                    out.close();
                    flushAckBatch();
                    break;
                }

                continue;
            }
        }
    }

public:
    Receiver() {
        discoverySock.open(udp::v4());
        discoverySock.bind(udp::endpoint(udp::v4(), discoveryPort));

        dataSock.open(udp::v4());
        dataSock.bind(udp::endpoint(udp::v4(), dataPort));

        ackSock.open(udp::v4());
    }

    void listen() {
        while (true) {
            std::size_t len = discoverySock.receive_from(
                boost::asio::buffer(buff, sizeof(buff)),
                senderDiscoveryEndpoint
            );
            if (len == 0) continue;

            string msg(reinterpret_cast<char*>(buff), len);
            if (handleDiscovery(msg)) break;
        }
        receiveData();
    }
};

int main() {
    Receiver receiver;
    receiver.listen();
}
