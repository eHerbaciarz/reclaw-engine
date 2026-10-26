#include "RaceNet.h"

#include <algorithm>
#include <cstring>

#include "../SharedDefines.h"

#ifdef __EMSCRIPTEN__

// No UDP in the browser: network races are not offered there.
namespace RaceNet
{
    bool Open(bool) { return false; }
    void Close() { }
    bool IsOpen() { return false; }
    void SetPeer(const std::string&) { }
    const std::string& GetPeer() { static std::string s; return s; }
    std::string LocalAddress() { return ""; }
    bool SendToPeer(MessageType, const std::vector<uint8_t>&) { return false; }
    bool SendTo(const std::string&, MessageType, const std::vector<uint8_t>&) { return false; }
    void Broadcast(MessageType, const std::vector<uint8_t>&) { }
    bool Receive(Message&) { return false; }
}

#else

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#endif
#if defined(__SWITCH__)
#include <switch.h>
#elif defined(__vita__)
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <cstdlib>
#elif defined(__3DS__)
#include <3ds.h>
#include <malloc.h>
#elif defined(__PSL1GHT__)
#include <net/net.h>
#endif

namespace
{
    const uint8_t kMagic[4] = { 'C', 'L', 'W', 'R' };
    const uint8_t kVersion = 1;

    int g_Socket = -1;
    std::string g_Peer;
    bool g_SocketsInitialized = false;

    // Brings up the platform's network stack (once).
    bool InitSockets()
    {
        if (g_SocketsInitialized) return true;
#if defined(_WIN32)
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
#elif defined(__SWITCH__)
        if (R_FAILED(socketInitializeDefault())) return false;
#elif defined(__vita__)
        sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
        static void* s_NetMemory = malloc(1024 * 1024);
        SceNetInitParam param;
        param.memory = s_NetMemory;
        param.size = 1024 * 1024;
        param.flags = 0;
        int rc = sceNetInit(&param);
        if (rc < 0 && rc != (int)0x80410110 /* already initialized */) return false;
        sceNetCtlInit();
#elif defined(__3DS__)
        static u32* s_SocBuffer = (u32*)memalign(0x1000, 0x100000);
        if (!s_SocBuffer || R_FAILED(socInit(s_SocBuffer, 0x100000))) return false;
#elif defined(__PSL1GHT__)
        if (netInitialize() < 0) return false;
#endif
        g_SocketsInitialized = true;
        return true;
    }

    void SetNonBlocking(int socket)
    {
#if defined(_WIN32)
        u_long on = 1;
        ioctlsocket(socket, FIONBIO, &on);
#elif defined(__PSL1GHT__)
        int on = 1;
        setsockopt(socket, SOL_SOCKET, SO_NBIO, &on, sizeof(on));
#else
        int flags = fcntl(socket, F_GETFL, 0);
        fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    bool ParseAddress(const std::string& address, sockaddr_in& out)
    {
        memset(&out, 0, sizeof(out));
        out.sin_family = AF_INET;
        std::string host = address;
        uint16_t port = RaceNet::kPort;
        size_t colon = address.find(':');
        if (colon != std::string::npos)
        {
            host = address.substr(0, colon);
            port = (uint16_t)atoi(address.c_str() + colon + 1);
        }
        out.sin_port = htons(port);
        return inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1;
    }

    std::string FormatAddress(const sockaddr_in& addr)
    {
        char buf[INET_ADDRSTRLEN] = { 0 };
        inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    bool SendRaw(const sockaddr_in& addr, RaceNet::MessageType type, const std::vector<uint8_t>& payload)
    {
        if (g_Socket < 0) return false;
        std::vector<uint8_t> packet(kMagic, kMagic + 4);
        packet.push_back(kVersion);
        packet.push_back((uint8_t)type);
        packet.insert(packet.end(), payload.begin(), payload.end());
        return (int)sendto(g_Socket, (const char*)packet.data(), (int)packet.size(), 0,
            (const sockaddr*)&addr, sizeof(addr)) == (int)packet.size();
    }
}

namespace RaceNet
{
    bool Open(bool host)
    {
        Close();
        if (!InitSockets())
        {
            LOG_ERROR("Race: network not available");
            return false;
        }
        g_Socket = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (g_Socket < 0)
        {
            LOG_ERROR("Race: socket() failed: " + std::to_string(errno));
            return false;
        }
        int yes = 1;
        setsockopt(g_Socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
        setsockopt(g_Socket, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(host ? kPort : 0);
        if (bind(g_Socket, (const sockaddr*)&addr, sizeof(addr)) != 0)
        {
            LOG_ERROR("Race: bind() failed: " + std::to_string(errno));
            Close();
            return false;
        }
        SetNonBlocking(g_Socket);
        LOG(std::string("Race: socket open as ") + (host ? "host" : "joiner"));
        return true;
    }

    void Close()
    {
        if (g_Socket >= 0)
        {
            CLOSE_SOCKET(g_Socket);
        }
        g_Socket = -1;
        g_Peer.clear();
    }

    bool IsOpen() { return g_Socket >= 0; }

    void SetPeer(const std::string& address) { g_Peer = address; }
    const std::string& GetPeer() { return g_Peer; }

    std::string LocalAddress()
    {
#ifdef __SWITCH__
        struct in_addr a;
        a.s_addr = (in_addr_t)gethostid();
        if (a.s_addr == 0 || a.s_addr == htonl(INADDR_LOOPBACK)) return "";
        char buf[INET_ADDRSTRLEN] = { 0 };
        inet_ntop(AF_INET, &a, buf, sizeof(buf));
        return buf;
#else
        // The address the system would use to reach the local network: a UDP
        // socket "connected" to a private address (no packet is sent).
        if (!InitSockets()) return "";
        int s = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) return "";
        sockaddr_in target;
        memset(&target, 0, sizeof(target));
        target.sin_family = AF_INET;
        target.sin_port = htons(9);
        inet_pton(AF_INET, "10.255.255.255", &target.sin_addr);
        std::string result;
        if (connect(s, (const sockaddr*)&target, sizeof(target)) == 0)
        {
            sockaddr_in local;
            socklen_t len = sizeof(local);
            if (getsockname(s, (sockaddr*)&local, &len) == 0 && local.sin_addr.s_addr != 0)
            {
                char buf[INET_ADDRSTRLEN] = { 0 };
                inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
                result = buf;
            }
        }
        CLOSE_SOCKET(s);
        return result;
#endif
    }

    bool SendToPeer(MessageType type, const std::vector<uint8_t>& payload)
    {
        return !g_Peer.empty() && SendTo(g_Peer, type, payload);
    }

    bool SendTo(const std::string& address, MessageType type, const std::vector<uint8_t>& payload)
    {
        sockaddr_in addr;
        if (!ParseAddress(address, addr)) return false;
        return SendRaw(addr, type, payload);
    }

    void Broadcast(MessageType type, const std::vector<uint8_t>& payload)
    {
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        SendRaw(addr, type, payload);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        SendRaw(addr, type, payload);

        // Directed broadcast of the local /24 network too (some routers and
        // the Switch drop 255.255.255.255).
        std::string local = LocalAddress();
        size_t dot = local.rfind('.');
        if (dot != std::string::npos)
        {
            SendTo(local.substr(0, dot) + ".255:" + std::to_string(kPort), type, payload);
        }
    }

    bool Receive(Message& out)
    {
        if (g_Socket < 0) return false;
        for (;;)
        {
            uint8_t buf[1500];
            sockaddr_in from;
            socklen_t fromLen = sizeof(from);
            int n = (int)recvfrom(g_Socket, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
            if (n <= 0) return false;
            if (n < 6 || memcmp(buf, kMagic, 4) != 0 || buf[4] != kVersion) continue;
            out.type = (MessageType)buf[5];
            out.from = FormatAddress(from);
            out.payload.assign(buf + 6, buf + n);
            return true;
        }
    }
}

#endif

namespace RaceNet
{
    void PutU32(std::vector<uint8_t>& b, uint32_t v)
    {
        for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
    }

    void PutF32(std::vector<uint8_t>& b, float v)
    {
        uint32_t u;
        memcpy(&u, &v, 4);
        PutU32(b, u);
    }

    void PutStr(std::vector<uint8_t>& b, const std::string& s)
    {
        uint8_t n = (uint8_t)std::min<size_t>(s.size(), 255);
        b.push_back(n);
        b.insert(b.end(), s.begin(), s.begin() + n);
    }

    bool GetU32(const std::vector<uint8_t>& b, size_t& pos, uint32_t& v)
    {
        if (pos + 4 > b.size()) return false;
        v = 0;
        for (int i = 0; i < 4; i++) v |= (uint32_t)b[pos + i] << (8 * i);
        pos += 4;
        return true;
    }

    bool GetF32(const std::vector<uint8_t>& b, size_t& pos, float& v)
    {
        uint32_t u;
        if (!GetU32(b, pos, u)) return false;
        memcpy(&v, &u, 4);
        return true;
    }

    bool GetStr(const std::vector<uint8_t>& b, size_t& pos, std::string& s)
    {
        if (pos >= b.size()) return false;
        uint8_t n = b[pos++];
        if (pos + n > b.size()) return false;
        s.assign((const char*)b.data() + pos, n);
        pos += n;
        return true;
    }
}
