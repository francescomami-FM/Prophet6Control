#include "ProtocolCore.h"

#include <algorithm>

namespace p6
{
    namespace
    {
        constexpr std::uint8_t kSysExStart = 0xF0;
        constexpr std::uint8_t kSysExEnd = 0xF7;
        constexpr std::uint8_t kDsiId = 0x01;
        constexpr std::uint8_t kProphet6Id = 0x2D;
        constexpr std::size_t kUnpackedProgramBytes = 1024;
        constexpr std::size_t kPackedProgramBytes = 1171;
    }

    std::array<ControlChange, 4> encodeNrpn(int nrpn, int value)
    {
        const int safeNumber = std::clamp(nrpn, 0, 16383);
        const int safeValue = std::clamp(value, 0, 16383);

        return {{
            {99, (safeNumber >> 7) & 0x7f},
            {98, safeNumber & 0x7f},
            {6, (safeValue >> 7) & 0x7f},
            {38, safeValue & 0x7f}
        }};
    }

    bool NrpnDecoder::processCC(int channelZeroBased, int controller, int value, DecodedValue& decoded)
    {
        if (channelZeroBased < 0 || channelZeroBased >= static_cast<int>(states.size()))
            return false;

        auto& state = states[static_cast<std::size_t>(channelZeroBased)];
        const int safeValue = std::clamp(value, 0, 127);

        switch (controller)
        {
            case 99:
                state.numberMsb = safeValue;
                state.hasDataMsb = false;
                state.hasDataLsb = false;
                return false;
            case 98:
                state.numberLsb = safeValue;
                state.hasDataMsb = false;
                state.hasDataLsb = false;
                return false;
            case 6:
                state.dataMsb = safeValue;
                state.hasDataMsb = true;
                state.hasDataLsb = false;
                break;
            case 38:
                state.dataLsb = safeValue;
                state.hasDataLsb = true;
                break;
            case 96:
            case 97:
                if (state.numberMsb < 0 || state.numberLsb < 0)
                    return false;
                if (!state.hasDataMsb)
                    state.dataMsb = 0;
                {
                    int current = (state.dataMsb << 7) | (state.hasDataLsb ? state.dataLsb : 0);
                    current += (controller == 96 ? 1 : -1) * std::max(1, safeValue);
                    current = std::clamp(current, 0, 16383);
                    state.dataMsb = (current >> 7) & 0x7f;
                    state.dataLsb = current & 0x7f;
                    state.hasDataMsb = true;
                    state.hasDataLsb = true;
                }
                break;
            case 100:
            case 101:
                state.numberMsb = -1;
                state.numberLsb = -1;
                state.hasDataMsb = false;
                state.hasDataLsb = false;
                return false;
            default:
                return false;
        }

        if (state.numberMsb < 0 || state.numberLsb < 0 || !state.hasDataMsb || !state.hasDataLsb)
            return false;

        decoded.channel = channelZeroBased + 1;
        decoded.nrpn = (state.numberMsb << 7) | state.numberLsb;
        decoded.value = (state.dataMsb << 7) | state.dataLsb;
        return true;
    }

    void NrpnDecoder::reset()
    {
        states = {};
        for (auto& s : states)
        {
            s.numberMsb = -1;
            s.numberLsb = -1;
        }
    }

    bool unpackProgramData(const std::vector<std::uint8_t>& packed,
                           std::vector<std::uint8_t>& unpacked)
    {
        // Reject aliasing and clear the destination on every failure path.
        if (&packed == &unpacked)
            return false;
        unpacked.clear();
        if (packed.size() != kPackedProgramBytes)
            return false;
        if (std::any_of(packed.begin(), packed.end(), [](std::uint8_t byte) { return byte > 0x7f; }))
            return false;

        unpacked.reserve(kUnpackedProgramBytes);
        std::size_t pos = 0;
        while (pos < packed.size() && unpacked.size() < kUnpackedProgramBytes)
        {
            const std::uint8_t msbFlags = packed[pos++];
            for (int i = 0; i < 7 && pos < packed.size() && unpacked.size() < kUnpackedProgramBytes; ++i)
            {
                const std::uint8_t low = packed[pos++] & 0x7f;
                const std::uint8_t high = static_cast<std::uint8_t>(((msbFlags >> i) & 0x01) << 7);
                unpacked.push_back(static_cast<std::uint8_t>(low | high));
            }
        }
        return unpacked.size() == kUnpackedProgramBytes;
    }

    std::vector<std::uint8_t> packProgramData(const std::vector<std::uint8_t>& unpacked)
    {
        if (unpacked.size() != kUnpackedProgramBytes)
            return {};

        std::vector<std::uint8_t> packed;
        packed.reserve(kPackedProgramBytes);
        for (std::size_t i = 0; i < unpacked.size(); i += 7)
        {
            const std::size_t count = std::min<std::size_t>(7, unpacked.size() - i);
            std::uint8_t msbFlags = 0;
            for (std::size_t j = 0; j < count; ++j)
                msbFlags |= static_cast<std::uint8_t>(((unpacked[i + j] >> 7) & 0x01) << j);
            packed.push_back(msbFlags);
            for (std::size_t j = 0; j < count; ++j)
                packed.push_back(static_cast<std::uint8_t>(unpacked[i + j] & 0x7f));
        }
        return packed;
    }

    bool parseProphet6SysEx(const std::vector<std::uint8_t>& bytes, Prophet6SysEx& parsed)
    {
        parsed = {};
        if (bytes.size() < 5 || bytes.front() != kSysExStart || bytes.back() != kSysExEnd
            || bytes[1] != kDsiId || bytes[2] != kProphet6Id)
            return false;

        parsed.bytes = bytes;
        const auto command = bytes[3];

        if (command == 0x03)
        {
            if (bytes.size() != 4 + kPackedProgramBytes + 1)
                return false;
            const auto begin = bytes.begin() + 4;
            const auto end = bytes.end() - 1;
            if (std::any_of(begin, end, [](std::uint8_t byte) { return byte > 0x7f; }))
                return false;
            parsed.kind = SysExKind::EditBufferDump;
            parsed.packedProgramData.assign(begin, end);
            return true;
        }

        if (command == 0x02)
        {
            if (bytes.size() != 6 + kPackedProgramBytes + 1)
                return false;
            parsed.bank = bytes[4];
            parsed.program = bytes[5];
            if (parsed.bank > 9 || parsed.program > 99)
                return false;
            const auto begin = bytes.begin() + 6;
            const auto end = bytes.end() - 1;
            if (std::any_of(begin, end, [](std::uint8_t byte) { return byte > 0x7f; }))
                return false;
            parsed.kind = SysExKind::ProgramDump;
            parsed.packedProgramData.assign(begin, end);
            return true;
        }

        if (command == 0x0f)
        {
            if (std::any_of(bytes.begin() + 4, bytes.end() - 1,
                            [](std::uint8_t byte) { return byte > 0x7f; }))
                return false;
            parsed.kind = SysExKind::GlobalDump;
            return true;
        }

        return false;
    }

    std::vector<std::uint8_t> makeEditBufferDumpRequest()
    {
        return { kSysExStart, kDsiId, kProphet6Id, 0x06, kSysExEnd };
    }

    std::vector<std::uint8_t> makeProgramDumpRequest(int bank, int program)
    {
        if (bank < 0 || bank > 9 || program < 0 || program > 99)
            return {};
        return { kSysExStart, kDsiId, kProphet6Id, 0x05,
                 static_cast<std::uint8_t>(bank), static_cast<std::uint8_t>(program), kSysExEnd };
    }

    std::vector<std::uint8_t> programDumpToEditBuffer(const Prophet6SysEx& programDump)
    {
        if (programDump.kind != SysExKind::ProgramDump
            || programDump.packedProgramData.size() != kPackedProgramBytes
            || std::any_of(programDump.packedProgramData.begin(), programDump.packedProgramData.end(),
                           [](std::uint8_t byte) { return byte > 0x7f; }))
            return {};

        std::vector<std::uint8_t> editBuffer;
        editBuffer.reserve(4 + kPackedProgramBytes + 1);
        editBuffer.insert(editBuffer.end(), { kSysExStart, kDsiId, kProphet6Id, 0x03 });
        editBuffer.insert(editBuffer.end(), programDump.packedProgramData.begin(), programDump.packedProgramData.end());
        editBuffer.push_back(kSysExEnd);
        return editBuffer;
    }
}
