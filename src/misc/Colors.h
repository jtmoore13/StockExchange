#pragma once
#include <fmt/core.h>
#include <string>
#include <iostream>

namespace Colors {
    [[maybe_unused]] constexpr const char* Black            = "\033[30m";
    [[maybe_unused]] constexpr const char* Red              = "\033[31m";
    [[maybe_unused]] constexpr const char* Green            = "\033[32m";
    [[maybe_unused]] constexpr const char* Yellow           = "\033[33m";
    [[maybe_unused]] constexpr const char* Blue             = "\033[34m";
    [[maybe_unused]] constexpr const char* Magenta          = "\033[35m";
    [[maybe_unused]] constexpr const char* Cyan             = "\033[36m";
    [[maybe_unused]] constexpr const char* White            = "\033[37m";
    [[maybe_unused]] constexpr const char* Grey             = "\033[90m";
    [[maybe_unused]] constexpr const char* LightGrey        = "\033[37m";
    [[maybe_unused]] constexpr const char* BoldWhite        = "\033[1;97m";

    // blues
    [[maybe_unused]] constexpr const char* LightBlue        = "\033[38;5;117m";
    [[maybe_unused]] constexpr const char* SkyBlue          = "\033[38;5;111m";
    [[maybe_unused]] constexpr const char* PowderBlue       = "\033[38;5;153m";
    [[maybe_unused]] constexpr const char* PaleBlue         = "\033[38;5;189m";
    [[maybe_unused]] constexpr const char* SteelBlue        = "\033[38;5;67m";
    [[maybe_unused]] constexpr const char* IceBlue          = "\033[38;5;123m";

    // greens
    [[maybe_unused]] constexpr const char* DarkGreen         = "\033[38;2;80;0;0m";
    [[maybe_unused]] constexpr const char* ForestGreen       = "\033[38;5;28m";
    [[maybe_unused]] constexpr const char* EmeraldGreen      = "\033[38;5;34m";
    [[maybe_unused]] constexpr const char* LeafGreen         = "\033[38;5;40m";
    [[maybe_unused]] constexpr const char* LimeGreen         = "\033[38;5;46m";
    [[maybe_unused]] constexpr const char* MintGreen         = "\033[38;5;120m";
    [[maybe_unused]] constexpr const char* PaleGreen         = "\033[38;5;151m";

    // bold greens
    [[maybe_unused]] constexpr const char* BoldGreen         = "\033[1;38;5;34m";
    [[maybe_unused]] constexpr const char* BoldBrightGreen   = "\033[1;92m";
    [[maybe_unused]] constexpr const char* BoldForestGreen   = "\033[1;38;5;28m";
    [[maybe_unused]] constexpr const char* BoldEmeraldGreen  = "\033[1;38;5;34m";
    [[maybe_unused]] constexpr const char* BoldLeafGreen     = "\033[1;38;5;40m";
    [[maybe_unused]] constexpr const char* BoldLimeGreen     = "\033[1;38;5;46m";
    [[maybe_unused]] constexpr const char* BoldMintGreen     = "\033[1;38;5;120m";
    [[maybe_unused]] constexpr const char* BoldPaleGreen     = "\033[1;38;5;151m";

    // reds
    [[maybe_unused]] constexpr const char* DarkRed          = "\033[38;2;110;0;0m";
    [[maybe_unused]] constexpr const char* PaleRed          = "\033[91m";
    [[maybe_unused]] constexpr const char* CrimsonRed       = "\033[38;5;124m";
    [[maybe_unused]] constexpr const char* FireRed          = "\033[38;5;160m";
    [[maybe_unused]] constexpr const char* ScarletRed       = "\033[38;5;196m";
    [[maybe_unused]] constexpr const char* CoralRed         = "\033[38;5;203m";
    [[maybe_unused]] constexpr const char* SalmonRed        = "\033[38;5;210m";
    [[maybe_unused]] constexpr const char* RoseRed          = "\033[38;5;211m";

    // bold reds
    [[maybe_unused]] constexpr const char* BoldRed          = "\033[1;31m";
    [[maybe_unused]] constexpr const char* BoldBrightRed    = "\033[1;91m";
    [[maybe_unused]] constexpr const char* BoldCrimsonRed   = "\033[1;38;5;124m";
    [[maybe_unused]] constexpr const char* BoldFireRed      = "\033[1;38;5;160m";
    [[maybe_unused]] constexpr const char* BoldScarletRed   = "\033[1;38;5;196m";
    [[maybe_unused]] constexpr const char* BoldCoralRed     = "\033[1;38;5;203m";
    [[maybe_unused]] constexpr const char* BoldSalmonRed    = "\033[1;38;5;210m";
    [[maybe_unused]] constexpr const char* BoldRoseRed      = "\033[1;38;5;211m";

    // purples
    [[maybe_unused]] constexpr const char* LavenderPurple    = "\033[38;5;183m";
    [[maybe_unused]] constexpr const char* OrchidPurple      = "\033[38;5;171m";
    [[maybe_unused]] constexpr const char* AmethystPurple    = "\033[38;5;135m";
    [[maybe_unused]] constexpr const char* GrapePurple       = "\033[38;5;91m";
    [[maybe_unused]] constexpr const char* DeepPurple        = "\033[38;5;54m";

    // bold purples
    [[maybe_unused]] constexpr const char* BoldLavenderPurple = "\033[1;38;5;183m";
    [[maybe_unused]] constexpr const char* BoldOrchidPurple   = "\033[1;38;5;171m";
    [[maybe_unused]] constexpr const char* BoldAmethystPurple = "\033[1;38;5;135m";
    [[maybe_unused]] constexpr const char* BoldGrapePurple    = "\033[1;38;5;91m";
    [[maybe_unused]] constexpr const char* BoldDeepPurple     = "\033[1;38;5;54m";

    // others
    [[maybe_unused]] constexpr const char* Orange = "\033[38;5;208m";
    [[maybe_unused]] constexpr const char* Peach = "\033[38;5;216m";
    [[maybe_unused]] constexpr const char* LightPink = "\033[38;5;217m";
    [[maybe_unused]] constexpr const char* Gold = "\033[38;5;214m";
    [[maybe_unused]] constexpr const char* Teal = "\033[38;5;30m";

    [[maybe_unused]] constexpr const char* Reset = "\033[0m";
}


namespace ClassColors {
    constexpr const char* exchangeServer = Colors::LavenderPurple;
    constexpr const char* exchange       = Colors::LavenderPurple;
    constexpr const char* broker         = Colors::LavenderPurple;
    constexpr const char* serverSocket   = Colors::Grey;
    constexpr const char* clientSocket   = Colors::Grey;
    constexpr const char* matchingEngine = Colors::Grey;
}

namespace LoggingPrefix {
    [[maybe_unused]] constexpr const char* timeColor           = Colors::Grey;
    [[maybe_unused]] inline const std::string serverSocket     = fmt::format("{}{}{}", ClassColors::serverSocket, "ServerSocket", Colors::Reset);
    [[maybe_unused]] inline const std::string clientSocket     = fmt::format("{}{}{}", ClassColors::clientSocket, "ClientSocket", Colors::Reset);
    [[maybe_unused]] inline const std::string broker           = fmt::format("{}{}{}", ClassColors::broker, "Broker", Colors::Reset);
    [[maybe_unused]] inline const std::string exchangeServer   = fmt::format("{}{}{}", ClassColors::exchangeServer, "ExchangeServer", Colors::Reset);
    [[maybe_unused]] inline const std::string exchange         = fmt::format("{}{}{}", ClassColors::exchange, "Exchange", Colors::Reset);
    [[maybe_unused]] inline const std::string matchingEngine   = fmt::format("{}{}{}", ClassColors::matchingEngine, "MatchingEngine", Colors::Reset);
}

namespace TradingColors {
    [[maybe_unused]] constexpr const char* orderId     = Colors::White;
    [[maybe_unused]] constexpr const char* buy         = Colors::Green;
    [[maybe_unused]] constexpr const char* sell        = Colors::Red;
}


namespace ColorUtils {
    inline std::string Wrap(const std::string& str, const char* color)
    {
        return fmt::format("{}{}{}", color, str, Colors::Reset);
    }

    constexpr void PrintAll() {
        constexpr std::pair<const char*, const char*> colorList[] = {
            {"Black", Colors::Black}, {"Red", Colors::Red}, {"Green", Colors::Green}, {"Yellow", Colors::Yellow},
            {"Blue",  Colors::Blue},  {"Magenta", Colors::Magenta}, {"Cyan",  Colors::Cyan}, {"White", Colors::White},
            {"Grey",  Colors::Grey},  {"BoldWhite", Colors::BoldWhite},

            {"LightBlue",  Colors::LightBlue},  {"SkyBlue",     Colors::SkyBlue},     {"PowderBlue", Colors::PowderBlue},
            {"PaleBlue",   Colors::PaleBlue},   {"SteelBlue",   Colors::SteelBlue},   {"IceBlue",    Colors::IceBlue},

            {"DarkGreen",       Colors::DarkGreen},       {"ForestGreen",       Colors::ForestGreen},
            {"EmeraldGreen",    Colors::EmeraldGreen},    {"LeafGreen",         Colors::LeafGreen},
            {"LimeGreen",       Colors::LimeGreen},       {"MintGreen",         Colors::MintGreen},
            {"PaleGreen",       Colors::PaleGreen},
            {"BoldGreen",       Colors::BoldGreen},       {"BoldBrightGreen",   Colors::BoldBrightGreen},
            {"BoldForestGreen", Colors::BoldForestGreen}, {"BoldEmeraldGreen",  Colors::BoldEmeraldGreen},
            {"BoldLeafGreen",   Colors::BoldLeafGreen},   {"BoldLimeGreen",     Colors::BoldLimeGreen},
            {"BoldMintGreen",   Colors::BoldMintGreen},   {"BoldPaleGreen",     Colors::BoldPaleGreen},

            {"DarkRed",       Colors::DarkRed},       {"PaleRed",       Colors::PaleRed},
            {"CrimsonRed",    Colors::CrimsonRed},    {"FireRed",       Colors::FireRed},
            {"ScarletRed",    Colors::ScarletRed},    {"CoralRed",      Colors::CoralRed},
            {"SalmonRed",     Colors::SalmonRed},     {"RoseRed",       Colors::RoseRed},
            {"BoldRed",       Colors::BoldRed},       {"BoldBrightRed",    Colors::BoldBrightRed},
            {"BoldCrimsonRed",Colors::BoldCrimsonRed},{"BoldFireRed",      Colors::BoldFireRed},
            {"BoldScarletRed",Colors::BoldScarletRed},{"BoldCoralRed",     Colors::BoldCoralRed},
            {"BoldSalmonRed", Colors::BoldSalmonRed}, {"BoldRoseRed",      Colors::BoldRoseRed},

            {"LavenderPurple",      Colors::LavenderPurple},      {"OrchidPurple",      Colors::OrchidPurple},
            {"AmethystPurple",      Colors::AmethystPurple},      {"GrapePurple",       Colors::GrapePurple},
            {"DeepPurple",          Colors::DeepPurple},

            {"BoldLavenderPurple",  Colors::BoldLavenderPurple},  {"BoldOrchidPurple",  Colors::BoldOrchidPurple},
            {"BoldAmethystPurple",  Colors::BoldAmethystPurple},  {"BoldGrapePurple",   Colors::BoldGrapePurple},
            {"BoldDeepPurple",      Colors::BoldDeepPurple},

            {"Orange", Colors::Orange}, {"Peach", Colors::Peach},
            {"LightPink", Colors::LightPink}, {"Gold", Colors::Gold},
            {"Teal", Colors::Teal}
        };

        for (const auto& [name, color] : colorList) {
            std::cout << Wrap("Hello World! 10 AAPL @ 99.99 ", color) << "    " << name << std::endl;
        }
    }
}