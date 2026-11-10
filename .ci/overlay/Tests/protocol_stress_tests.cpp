#include "ProtocolCore.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        std::mt19937_64 rng(0x5036436f6e74726fULL);
        std::uniform_int_distribution<int> nrpnDist(-20000, 40000);
        std::uniform_int_distribution<int> valueDist(-20000, 40000);
        std::uniform_int_distribution<int> channelDist(0, 15);
        std::uniform_int_distribution<int> ccDist(0, 127);
        std::uniform_int_distribution<int> byteDist(0, 255);

        for (int i = 0; i < 250000; ++i)
        {
            const int number = nrpnDist(rng);
            const int value = valueDist(rng);
            const auto messages = p6::encodeNrpn(number, value);
            for (const auto& message : messages)
                require(message.controller <= 127 && message.value <= 127, "invalid encoded MIDI data byte");
            const int n = std::clamp(number, 0, 16383);
            const int v = std::clamp(value, 0, 16383);
            require(messages[0].value == ((n >> 7) & 0x7f), "NRPN MSB mismatch");
            require(messages[1].value == (n & 0x7f), "NRPN LSB mismatch");
            require(messages[2].value == ((v >> 7) & 0x7f), "data MSB mismatch");
            require(messages[3].value == (v & 0x7f), "data LSB mismatch");
        }

        {
            p6::NrpnDecoder decoder;
            p6::NrpnDecoder::DecodedValue decoded;
            for (int i = 0; i < 1000000; ++i)
            {
                const int channel = channelDist(rng);
                const int controller = ccDist(rng);
                const int value = ccDist(rng);
                if (decoder.processCC(channel, controller, value, decoded))
                {
                    require(decoded.channel >= 1 && decoded.channel <= 16, "decoded channel outside MIDI range");
                    require(decoded.nrpn >= 0 && decoded.nrpn <= 16383, "decoded NRPN outside 14-bit range");
                    require(decoded.value >= 0 && decoded.value <= 16383, "decoded value outside 14-bit range");
                }
            }
        }

        {
            p6::NrpnDecoder decoder;
            p6::NrpnDecoder::DecodedValue decoded;
            for (int channel = 0; channel < 16; ++channel)
            {
                const int address = 127 + channel * 719;
                const int value = (channel * 811) & 0x3fff;
                const auto encoded = p6::encodeNrpn(address, value);
                for (const auto& cc : encoded)
                    decoder.processCC(channel, cc.controller, cc.value, decoded);
            }
            for (int channel = 0; channel < 16; ++channel)
            {
                const int value = (channel * 97) & 0x3fff;
                const auto update = p6::encodeNrpn(127 + channel * 719, value);
                decoder.processCC(channel, update[2].controller, update[2].value, decoded);
                require(decoder.processCC(channel, update[3].controller, update[3].value, decoded), "running NRPN update missing");
                require(decoded.channel == channel + 1 && decoded.nrpn == 127 + channel * 719
                        && decoded.value == value, "NRPN channel selector contamination");
            }
        }

        for (int iteration = 0; iteration < 20000; ++iteration)
        {
            std::vector<std::uint8_t> raw(1024);
            for (auto& byte : raw)
                byte = static_cast<std::uint8_t>(byteDist(rng));
            const auto packed = p6::packProgramData(raw);
            require(packed.size() == 1171, "packed program has wrong size");
            require(std::all_of(packed.begin(), packed.end(), [](std::uint8_t b) { return b <= 0x7f; }),
                    "packed program contains high-bit byte");
            std::vector<std::uint8_t> unpacked;
            require(p6::unpackProgramData(packed, unpacked), "valid packed program rejected");
            require(unpacked == raw, "program round trip mismatch");
        }

        {
            std::vector<std::uint8_t> output(1024, 0x55);
            require(!p6::unpackProgramData(std::vector<std::uint8_t>(1170, 0), output), "short packed input accepted");
            require(output.empty(), "failed unpack left stale output");
            std::vector<std::uint8_t> invalid(1171, 0);
            invalid[811] = 0x80;
            require(!p6::unpackProgramData(invalid, output), "non 7-bit packed input accepted");
            require(output.empty(), "invalid packed input left stale output");
            require(!p6::unpackProgramData(invalid, invalid), "aliased input/output accepted");
        }

        for (int iteration = 0; iteration < 10000; ++iteration)
        {
            std::vector<std::uint8_t> raw(1024);
            for (auto& byte : raw)
                byte = static_cast<std::uint8_t>(byteDist(rng));
            const auto packed = p6::packProgramData(raw);
            std::vector<std::uint8_t> program{0xF0, 0x01, 0x2D, 0x02,
                static_cast<std::uint8_t>(iteration % 10), static_cast<std::uint8_t>(iteration % 100)};
            program.insert(program.end(), packed.begin(), packed.end());
            program.push_back(0xF7);
            p6::Prophet6SysEx parsed;
            require(p6::parseProphet6SysEx(program, parsed), "valid program dump rejected");
            const auto edit = p6::programDumpToEditBuffer(parsed);
            require(edit.size() == 1176 && edit[3] == 0x03 && edit.back() == 0xF7,
                    "program-to-edit conversion failed");
            p6::Prophet6SysEx parsedEdit;
            require(p6::parseProphet6SysEx(edit, parsedEdit), "valid edit buffer rejected");

            auto malformed = edit;
            malformed[100] = 0x80;
            require(!p6::parseProphet6SysEx(malformed, parsedEdit), "non 7-bit SysEx payload accepted");
            malformed = program;
            malformed[4] = 0x80;
            require(!p6::parseProphet6SysEx(malformed, parsed), "high-bit bank selector accepted");
            malformed = program;
            malformed[5] = 0x80;
            require(!p6::parseProphet6SysEx(malformed, parsed), "high-bit program selector accepted");
        }

        {
            p6::Prophet6SysEx parsed;
            for (int iteration = 0; iteration < 300000; ++iteration)
            {
                const int length = (iteration % 3 == 0) ? (iteration % 1200) : (iteration % 64);
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
                for (auto& byte : bytes)
                    byte = static_cast<std::uint8_t>(byteDist(rng));
                (void) p6::parseProphet6SysEx(bytes, parsed);
                std::vector<std::uint8_t> unpacked;
                (void) p6::unpackProgramData(bytes, unpacked);
            }
        }

        std::cout << "Passed: 250k NRPN encodes, 1m decoder events, 20k patch round trips, "
                     "10k SysEx cases, 300k malformed parser inputs.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Stress test failure: " << error.what() << "\n";
        return 1;
    }
}
