#pragma once

// UDP messaging for network races (two consoles on the same Wi-Fi / LAN, or
// two emulators). The host listens on kPort; a joining console broadcasts a
// discovery request (and also tries 127.0.0.1, for two emulators on one PC),
// the host answers, and from then on both sides talk directly.
//
// Unreliable by design: state packets are sent many times per second and the
// few one-off messages (join, ready, go, finish) are repeated until answered.

#include <cstdint>
#include <string>
#include <vector>

namespace RaceNet
{
    const uint16_t kPort = 47811;

    enum MessageType : uint8_t
    {
        Msg_Discover = 1,   // joiner -> broadcast
        Msg_Offer,          // host -> joiner: level, host name
        Msg_Join,           // joiner -> host
        Msg_Accept,         // host -> joiner: level
        Msg_Ready,          // both: level loaded
        Msg_Go,             // host -> joiner: countdown starts
        Msg_State,          // both: position / frame of the Captain
        Msg_Finish,         // both: reached the end (race time)
        Msg_Leave,          // both: left the race
    };

    struct Message
    {
        MessageType type = Msg_Discover;
        std::string from;            // sender address "a.b.c.d:port"
        std::vector<uint8_t> payload;
    };

    // Opens the socket. Host: bound to kPort. Joiner: any free port.
    bool Open(bool host);
    void Close();
    bool IsOpen();

    // Peer the messages go to (set once known).
    void SetPeer(const std::string& address);
    const std::string& GetPeer();

    // This console's address on the local network (for display), or "".
    std::string LocalAddress();

    bool SendToPeer(MessageType type, const std::vector<uint8_t>& payload = {});
    bool SendTo(const std::string& address, MessageType type, const std::vector<uint8_t>& payload = {});
    // Discovery request to the whole local network (and 127.0.0.1).
    void Broadcast(MessageType type, const std::vector<uint8_t>& payload = {});

    // Next received message; false when none is waiting.
    bool Receive(Message& out);

    // Little helpers for payloads.
    void PutU32(std::vector<uint8_t>& b, uint32_t v);
    void PutF32(std::vector<uint8_t>& b, float v);
    void PutStr(std::vector<uint8_t>& b, const std::string& s);
    bool GetU32(const std::vector<uint8_t>& b, size_t& pos, uint32_t& v);
    bool GetF32(const std::vector<uint8_t>& b, size_t& pos, float& v);
    bool GetStr(const std::vector<uint8_t>& b, size_t& pos, std::string& s);
}
