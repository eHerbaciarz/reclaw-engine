#include "RaceLobby.h"

#include "RaceNet.h"
#include "RaceUi.h"

#include "../GameApp/BaseGameApp.h"
#include "../UserInterface/Lang.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace
{
    const char* kLevelNames[15] = { "",
        "La Roca", "Battlements", "Thief's Forest", "Dark Woods", "Town",
        "Puerto De Lobo", "Docks", "Shipyards", "Pirate's Cove", "Cliffs",
        "Caverns", "Undersea Caves", "Tiger Island", "Temple" };

    std::string LevelLabel(int level)
    {
        return std::string(Lang::Tr("Poziom ", "Level ")) + std::to_string(level) + " - " + kLevelNames[level];
    }

    // 1..14, or 0 when the player went back.
    int PickLevel(SDL_Renderer* r, int initial)
    {
        RaceUi::ListScreen s;
        s.title = Lang::Tr("Wybierz poziom", "Choose a level");
        for (int i = 1; i <= 14; ++i) s.items.push_back(LevelLabel(i));
        s.selected = std::max(0, initial - 1);
        s.footer = { Lang::Tr("A - zatwierdź, B - wróć", "A - confirm, B - back") };
        int result = RaceUi::Run(r, s);
        return result < 0 ? 0 : result + 1;
    }

#ifdef __SWITCH__
    // System screen to connect a controller for each player.
    bool ConnectTwoControllers()
    {
        HidLaControllerSupportArg args;
        hidLaCreateControllerSupportArg(&args);
        args.hdr.player_count_min = 2;
        args.hdr.player_count_max = 2;
        args.hdr.enable_single_mode = 0;
        args.hdr.enable_take_over_connection = 1;
        args.hdr.enable_permit_joy_dual = 1;
        hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);
        HidLaControllerSupportResultInfo info;
        memset(&info, 0, sizeof(info));
        Result rc = hidLaShowControllerSupport(&info, &args);
        LOG("Race: controller applet result " + std::to_string((int)rc) + ", players " + std::to_string((int)info.player_count));
        return R_SUCCEEDED(rc) && info.player_count >= 2;
    }
#endif

    bool SplitScreenSetup(SDL_Renderer* r, Race::Setup& out)
    {
#ifdef __SWITCH__
        // System screen to connect player 2's controller. Not fatal when it
        // fails (emulators may not show it): the second pad works if connected.
        if (!ConnectTwoControllers())
        {
            LOG_WARNING("Race: controller screen did not confirm two players");
        }
#endif
        int level = PickLevel(r, 1);
        if (level == 0) return false;
        out.mode = Race::Mode::SplitScreen;
        out.level = level;
        out.host = true;
        return true;
    }

    bool HostNetworkRace(SDL_Renderer* r, Race::Setup& out)
    {
        int level = PickLevel(r, 1);
        if (level == 0) return false;
        if (!RaceNet::Open(true))
        {
            RaceUi::ShowMessage(r, Lang::Tr("Wyścig przez sieć", "Network race"),
                { Lang::Tr("Nie udało się uruchomić sieci.", "Could not start networking."),
                  Lang::Tr("Sprawdź połączenie z Wi-Fi.", "Check the Wi-Fi connection.") });
            return false;
        }

        std::string local = RaceNet::LocalAddress();
        RaceUi::ListScreen s;
        s.title = Lang::Tr("Czekam na rywala", "Waiting for an opponent");
        s.items = { Lang::Tr("Anuluj", "Cancel") };
        s.footer = {
            LevelLabel(level),
            std::string(Lang::Tr("Adres tej konsoli: ", "This console: ")) + (local.empty() ? "?" : local),
            Lang::Tr("Na drugiej konsoli wybierz: Dołącz do wyścigu", "On the other console choose: Join a race"),
        };
        std::string joined;
        // Identifies this race when the joiner hears it on several addresses.
        const uint32_t session = (uint32_t)SDL_GetTicks() * 2654435761u ^ (uint32_t)(uintptr_t)&s;
        s.tick = [&](RaceUi::ListScreen&) -> int
        {
            RaceNet::Message m;
            while (RaceNet::Receive(m))
            {
                if (m.type == RaceNet::Msg_Discover)
                {
                    std::vector<uint8_t> p;
                    RaceNet::PutU32(p, (uint32_t)level);
                    RaceNet::PutStr(p, local);
                    RaceNet::PutU32(p, session);
                    RaceNet::SendTo(m.from, RaceNet::Msg_Offer, p);
                }
                else if (m.type == RaceNet::Msg_Join)
                {
                    joined = m.from;
                    std::vector<uint8_t> p;
                    RaceNet::PutU32(p, (uint32_t)level);
                    for (int i = 0; i < 3; ++i) RaceNet::SendTo(m.from, RaceNet::Msg_Accept, p);
                    return 1;
                }
            }
            return -2;
        };
        int result = RaceUi::Run(r, s);
        if (result != 1 || joined.empty())
        {
            RaceNet::Close();
            return false;
        }
        RaceNet::SetPeer(joined);
        LOG("Race: " + joined + " joined");
        out.mode = Race::Mode::Network;
        out.level = level;
        out.host = true;
        return true;
    }

    struct FoundHost
    {
        std::string address;
        int level = 0;
        uint32_t session = 0;
    };

    bool JoinNetworkRace(SDL_Renderer* r, Race::Setup& out)
    {
        if (!RaceNet::Open(false))
        {
            RaceUi::ShowMessage(r, Lang::Tr("Wyścig przez sieć", "Network race"),
                { Lang::Tr("Nie udało się uruchomić sieci.", "Could not start networking."),
                  Lang::Tr("Sprawdź połączenie z Wi-Fi.", "Check the Wi-Fi connection.") });
            return false;
        }

        std::vector<FoundHost> hosts;
        RaceUi::ListScreen s;
        s.title = Lang::Tr("Szukam wyścigów", "Looking for races");
        Uint32 lastDiscover = 0;
        auto rebuild = [&](RaceUi::ListScreen& screen)
        {
            screen.items.clear();
            for (const FoundHost& h : hosts)
            {
                std::string ip = h.address.substr(0, h.address.find(':'));
                screen.items.push_back(LevelLabel(h.level) + "  (" + ip + ")");
            }
            screen.items.push_back(Lang::Tr("Anuluj", "Cancel"));
            screen.footer = { hosts.empty()
                ? Lang::Tr("Na drugiej konsoli wybierz: Utwórz wyścig", "On the other console choose: Create a race")
                : Lang::Tr("Wybierz wyścig i naciśnij A", "Choose a race and press A"),
                Lang::Tr("Obie konsole muszą być w tej samej sieci Wi-Fi.", "Both consoles must be on the same Wi-Fi.") };
        };
        rebuild(s);
        s.tick = [&](RaceUi::ListScreen& screen) -> int
        {
            if (SDL_GetTicks() - lastDiscover > 500)
            {
                lastDiscover = SDL_GetTicks();
                RaceNet::Broadcast(RaceNet::Msg_Discover);
            }
            RaceNet::Message m;
            bool changed = false;
            while (RaceNet::Receive(m))
            {
                if (m.type != RaceNet::Msg_Offer) continue;
                size_t pos = 0;
                uint32_t level = 0, session = 0;
                std::string hostLocal;
                if (!RaceNet::GetU32(m.payload, pos, level) || level < 1 || level > 14) continue;
                RaceNet::GetStr(m.payload, pos, hostLocal);
                RaceNet::GetU32(m.payload, pos, session);
                bool known = false;
                for (FoundHost& h : hosts)
                {
                    // The same race heard on another address (loopback / broadcast)
                    if (h.address == m.from || (session != 0 && h.session == session)) { known = true; h.level = (int)level; }
                }
                if (!known)
                {
                    hosts.push_back(FoundHost{ m.from, (int)level, session });
                    changed = true;
                }
            }
            if (changed) rebuild(screen);
            return -2;
        };

        for (;;)
        {
            int choice = RaceUi::Run(r, s);
            if (choice < 0 || choice >= (int)hosts.size())
            {
                RaceNet::Close();
                return false;
            }

            // Ask to join until the host accepts (or 5 s pass).
            const FoundHost host = hosts[choice];
            int acceptedLevel = 0;
            Uint32 start = SDL_GetTicks(), lastJoin = 0;
            while (SDL_GetTicks() - start < 5000 && acceptedLevel == 0)
            {
                if (SDL_GetTicks() - lastJoin > 250)
                {
                    lastJoin = SDL_GetTicks();
                    RaceNet::SendTo(host.address, RaceNet::Msg_Join);
                }
                RaceNet::Message m;
                while (RaceNet::Receive(m))
                {
                    size_t pos = 0;
                    uint32_t level = 0;
                    if (m.type == RaceNet::Msg_Accept && RaceNet::GetU32(m.payload, pos, level) && level >= 1 && level <= 14)
                    {
                        acceptedLevel = (int)level;
                    }
                }
                SDL_Delay(10);
            }
            if (acceptedLevel == 0)
            {
                RaceUi::ShowMessage(r, Lang::Tr("Wyścig przez sieć", "Network race"),
                    { Lang::Tr("Druga konsola nie odpowiada.", "The other console does not answer.") });
                hosts.clear();
                rebuild(s);
                continue;
            }
            RaceNet::SetPeer(host.address);
            out.mode = Race::Mode::Network;
            out.level = acceptedLevel;
            out.host = false;
            return true;
        }
    }

    bool NetworkSetup(SDL_Renderer* r, Race::Setup& out)
    {
        RaceUi::ListScreen s;
        s.title = Lang::Tr("Wyścig przez sieć", "Network race");
        s.items = { Lang::Tr("Utwórz wyścig", "Create a race"),
                    Lang::Tr("Dołącz do wyścigu", "Join a race"),
                    Lang::Tr("Powrót", "Back") };
        s.footer = { Lang::Tr("Dwie konsole w tej samej sieci Wi-Fi.", "Two consoles on the same Wi-Fi."),
                     Lang::Tr("Rywal jest widoczny jako duch.", "The opponent is shown as a ghost.") };
        for (;;)
        {
            int choice = RaceUi::Run(r, s);
            if (choice == 0 && HostNetworkRace(r, out)) return true;
            if (choice == 1 && JoinNetworkRace(r, out)) return true;
            if (choice < 0 || choice == 2) return false;
        }
    }
}

namespace RaceLobby
{
    bool Run(SDL_Renderer* pRenderer, Race::Setup& out)
    {
        RaceUi::ListScreen s;
        s.title = Lang::Tr("Wyścig", "Race");
        s.items = { Lang::Tr("Podzielony ekran (jedna konsola)", "Split screen (one console)") };
#ifndef __EMSCRIPTEN__
        s.items.push_back(Lang::Tr("Przez sieć (dwie konsole)", "Network (two consoles)"));
#endif
        s.items.push_back(Lang::Tr("Powrót", "Back"));
        s.footer = { Lang::Tr("Kto pierwszy dotrze do końca poziomu, wygrywa.", "First to reach the end of the level wins.") };

        for (;;)
        {
            int choice = RaceUi::Run(pRenderer, s);
            int backIndex = (int)s.items.size() - 1;
            if (choice < 0 || choice == backIndex) return false;
            bool ok = (choice == 0) ? SplitScreenSetup(pRenderer, out) : NetworkSetup(pRenderer, out);
            if (ok) return true;
        }
    }
}
