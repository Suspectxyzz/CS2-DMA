/* QR Code generator - single-header C++ library
 *
 * Derived from the public-domain reference implementation by Project Nayuki:
 * https://github.com/nayuki/QR-Code-generator/tree/master/cpp
 *
 * Combines qrcodegen.hpp + qrcodegen.cpp into one header with `inline`
 * qualifiers so it can be included from a single translation unit.
 * GF(256) exp/log tables are computed at runtime to avoid transcription errors.
 *
 * Usage:
 *   qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("hello",
 *                              qrcodegen::QrCode::Ecc::MEDIUM);
 *   int  size = qr.getSize();          // edge length in modules
 *   bool mod = qr.getModule(x, y);     // true = dark module
 */
#pragma once

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qrcodegen {

// Forward declarations
class QrSegment;
class QrCode;

// ---- GF(256) tables (computed once, generator polynomial 0x11D) ----

inline const std::array<uint8_t, 256>& gfExp() {
    static std::array<uint8_t, 256> table = []() {
        std::array<uint8_t, 256> t{};
        uint16_t x = 1;
        for (int i = 0; i < 256; i++) {
            t[i] = static_cast<uint8_t>(x);
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        return t;
    }();
    return table;
}

inline const std::array<uint8_t, 256>& gfLog() {
    static std::array<uint8_t, 256> table = []() {
        std::array<uint8_t, 256> t{};
        const auto& e = gfExp();
        for (int i = 0; i < 255; i++)
            t[e[i]] = static_cast<uint8_t>(i);
        t[0] = 0; // log(0) is undefined; leave 0
        return t;
    }();
    return table;
}

inline uint8_t gfMultiply(uint8_t x, uint8_t y) {
    if (x == 0 || y == 0) return 0;
    const auto& e = gfExp();
    const auto& l = gfLog();
    int idx = static_cast<int>(l[x]) + static_cast<int>(l[y]);
    if (idx >= 255) idx -= 255;
    return e[idx];
}

inline void appendBits(std::vector<bool>& bb, unsigned val, int len) {
    if (len < 0 || len > 31 || (len < 32 && (val >> len) != 0))
        throw std::invalid_argument("Value out of range");
    for (int i = len - 1; i >= 0; i--)
        bb.push_back(((val >> i) & 1u) != 0);
}


/*---- Class QrSegment ----*/

class QrSegment {
public:
    class Mode {
    public:
        static const Mode NUMERIC;
        static const Mode ALPHANUMERIC;
        static const Mode BYTE;
        static const Mode KANJI;
        static const Mode ECI;
        int getModeBits() const { return modeBits; }
        int getNumCharCountBits(int ver) const {
            if      (1 <= ver && ver <=  9) return numCharCountBits[0];
            else if (10 <= ver && ver <= 26) return numCharCountBits[1];
            else if (27 <= ver && ver <= 40) return numCharCountBits[2];
            else throw std::invalid_argument("Version out of range");
        }
    private:
        int modeBits;
        int numCharCountBits[3];
        Mode(int mb, int cc0, int cc1, int cc2) :
            modeBits(mb), numCharCountBits{cc0, cc1, cc2} {}
    };

    static QrSegment makeBytes(const void* data, size_t len) {
        std::vector<bool> bb;
        for (size_t i = 0; i < len; i++)
            appendBits(bb, static_cast<const uint8_t*>(data)[i], 8);
        return QrSegment(Mode::BYTE, static_cast<int>(len), std::move(bb));
    }

    QrSegment(Mode md, int numCh, std::vector<bool>&& dat) :
        mode(md), numChars(numCh), data(std::move(dat)) {}

    Mode getMode() const { return mode; }
    int getNumChars() const { return numChars; }
    const std::vector<bool>& getData() const { return data; }

    static int getTotalBits(const std::vector<QrSegment>& segs, int version) {
        int result = 0;
        for (const QrSegment& seg : segs) {
            int ccBits = seg.mode.getNumCharCountBits(version);
            if (seg.numChars >= (1L << ccBits))
                return -1;
            result += 4 + ccBits + static_cast<int>(seg.data.size());
        }
        return result;
    }

private:
    Mode mode;
    int numChars;
    std::vector<bool> data;
};

const QrSegment::Mode QrSegment::Mode::NUMERIC     (0x1, 10, 12, 14);
const QrSegment::Mode QrSegment::Mode::ALPHANUMERIC(0x2,  9, 11, 13);
const QrSegment::Mode QrSegment::Mode::BYTE        (0x4,  8, 16, 16);
const QrSegment::Mode QrSegment::Mode::KANJI       (0x8,  8, 10, 12);
const QrSegment::Mode QrSegment::Mode::ECI         (0x7,  0,  0,  0);


/*---- Class QrCode ----*/

class QrCode {
public:
    enum class Ecc {
        LOW      = 0,
        MEDIUM   = 1,
        QUARTILE = 2,
        HIGH     = 3,
    };

    static QrCode encodeText(const char* text, Ecc ecl) {
        std::vector<QrSegment> segs;
        segs.push_back(QrSegment::makeBytes(text, std::strlen(text)));
        return encodeSegments(segs, ecl);
    }

    static QrCode encodeBinary(const std::vector<uint8_t>& data, Ecc ecl) {
        std::vector<QrSegment> segs;
        segs.push_back(QrSegment::makeBytes(data.data(), data.size()));
        return encodeSegments(segs, ecl);
    }

    static QrCode encodeSegments(const std::vector<QrSegment>& segs, Ecc ecl,
                                 int minVersion = 1, int maxVersion = 40,
                                 int mask = -1, bool boostEcl = true) {
        if (!(1 <= minVersion && minVersion <= maxVersion && maxVersion <= 40)
                || mask < -1 || mask > 7)
            throw std::invalid_argument("Invalid value");

        // Find smallest version that fits
        int version = -1;
        int dataUsedBits = 0;
        bool found = false;
        {
            int bitsUsed = QrSegment::getTotalBits(segs, minVersion);
            if (bitsUsed != -1 && bitsUsed <= getNumDataCodewords(minVersion, ecl) * 8) {
                version = minVersion;
                dataUsedBits = bitsUsed;
                found = true;
            }
        }
        if (!found) {
            for (version = maxVersion; version >= minVersion; version--) {
                int bitsUsed = QrSegment::getTotalBits(segs, version);
                if (bitsUsed != -1 && bitsUsed <= getNumDataCodewords(version, ecl) * 8) {
                    dataUsedBits = bitsUsed;
                    found = true;
                    break;
                }
            }
            if (!found)
                throw std::invalid_argument("Data too long");
        }

        // Boost error correction level if it still fits
        if (boostEcl) {
            if (dataUsedBits <= getNumDataCodewords(version, Ecc::MEDIUM) * 8)   ecl = Ecc::MEDIUM;
            if (dataUsedBits <= getNumDataCodewords(version, Ecc::QUARTILE) * 8) ecl = Ecc::QUARTILE;
            if (dataUsedBits <= getNumDataCodewords(version, Ecc::HIGH) * 8)     ecl = Ecc::HIGH;
        }

        // Concatenate all segments into one bit buffer
        std::vector<bool> bb;
        for (const QrSegment& seg : segs) {
            appendBits(bb, static_cast<unsigned>(seg.getMode().getModeBits()), 4);
            appendBits(bb, static_cast<unsigned>(seg.getNumChars()),
                       seg.getMode().getNumCharCountBits(version));
            const std::vector<bool>& sd = seg.getData();
            bb.insert(bb.end(), sd.begin(), sd.end());
        }
        // Pad to fill data capacity
        size_t dataCapacityBits = static_cast<size_t>(getNumDataCodewords(version, ecl)) * 8;
        if (bb.size() > dataCapacityBits)
            throw std::logic_error("Bits exceed capacity");
        appendBits(bb, 0, std::min(4, static_cast<int>(dataCapacityBits - bb.size())));
        appendBits(bb, 0, (8 - static_cast<int>(bb.size() % 8)) % 8);
        for (uint8_t pad = 0xEC; bb.size() < dataCapacityBits; pad ^= 0xEC ^ 0x11)
            appendBits(bb, pad, 8);

        return QrCode(version, ecl, std::move(bb), mask);
    }

    int getVersion() const { return version; }
    int getSize() const { return size; }
    Ecc getErrorCorrectionLevel() const { return errorCorrectionLevel; }
    int getMask() const { return mask; }

    bool getModule(int x, int y) const {
        return 0 <= x && x < size && 0 <= y && y < size && module(x, y);
    }

    QrCode(int ver, Ecc ecl, std::vector<bool>&& dataCodewords, int msk) :
            version(ver), errorCorrectionLevel(ecl) {
        size = version * 4 + 17;
        mask = msk;
        modules    = std::vector<bool>(static_cast<size_t>(size) * size, false);
        isFunction = std::vector<bool>(static_cast<size_t>(size) * size, false);

        drawFunctionPatterns();
        std::vector<bool> allCodewords;
        addEccAndInterleave(dataCodewords, allCodewords);
        drawCodewords(allCodewords);
        if (mask == -1)
            mask = handleConstructorMasking();
        else {
            drawFormatBits(mask);
            applyMask(mask);
        }
        isFunction.clear();
        isFunction.shrink_to_fit();
    }

private:
    int version;
    int size;
    Ecc errorCorrectionLevel;
    int mask;
    std::vector<bool> modules;
    std::vector<bool> isFunction;

    bool module(int x, int y) const {
        return modules.at(static_cast<size_t>(y) * size + x);
    }
    void setModule(int x, int y, bool isDark) {
        modules.at(static_cast<size_t>(y) * size + x) = isDark;
    }
    void setFunctionModule(int x, int y, bool isDark) {
        size_t idx = static_cast<size_t>(y) * size + x;
        modules.at(idx) = isDark;
        isFunction.at(idx) = true;
    }

    void drawFunctionPatterns() {
        // Dark module at (8, size-8)
        setFunctionModule(8, size - 8, true);

        drawFinderPattern(3, 3);
        drawFinderPattern(size - 4, 3);
        drawFinderPattern(3, size - 4);

        std::vector<int> alignPatPos;
        getAlignmentPatternPositions(version, alignPatPos);
        size_t numAlign = alignPatPos.size();
        for (size_t i = 0; i < numAlign; i++) {
            for (size_t j = 0; j < numAlign; j++) {
                if (!(i == 0 && j == 0 || i == 0 && j == numAlign - 1 || i == numAlign - 1 && j == 0))
                    drawAlignmentPattern(alignPatPos[i], alignPatPos[j]);
            }
        }

        // Timing patterns
        for (int i = 0; i < size; i++) {
            setFunctionModule(6, i, i % 2 == 0);
            setFunctionModule(i, 6, i % 2 == 0);
        }

        drawFormatBits(mask);
        drawVersion();
    }

    void drawFormatBits(int msk) {
        int data;
        switch (errorCorrectionLevel) {
            case Ecc::LOW:      data = 1; break;
            case Ecc::MEDIUM:   data = 0; break;
            case Ecc::QUARTILE: data = 3; break;
            case Ecc::HIGH:     data = 2; break;
            default: throw std::logic_error("Assertion error");
        }
        data = (data << 3) | msk;
        int rem = data;
        for (int i = 0; i < 10; i++)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        data = (data << 10) | rem;
        data ^= 0x5412;

        for (int i = 0; i <= 5; i++)
            setFunctionModule(8, i, ((data >> i) & 1) != 0);
        setFunctionModule(8, 7, ((data >> 6) & 1) != 0);
        setFunctionModule(8, 8, ((data >> 7) & 1) != 0);
        setFunctionModule(7, 8, ((data >> 8) & 1) != 0);
        for (int i = 9; i < 15; i++)
            setFunctionModule(14 - i, 8, ((data >> i) & 1) != 0);

        for (int i = 0; i <= 7; i++)
            setFunctionModule(size - 1 - i, 8, ((data >> i) & 1) != 0);
        for (int i = 8; i < 15; i++)
            setFunctionModule(8, size - 15 + i, ((data >> i) & 1) != 0);
    }

    void drawVersion() {
        if (version < 7) return;
        int rem = version;
        for (int i = 0; i < 12; i++)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long data = (static_cast<long>(version) << 12) | rem;
        for (int i = 0; i < 18; i++) {
            bool bit = ((data >> i) & 1) != 0;
            int a = size / 3 - 2 + i % 6;
            int b = i / 3;
            setFunctionModule(a, b, bit);
            setFunctionModule(b, a, bit);
        }
    }

    void drawFinderPattern(int x, int y) {
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                int dist = std::max(std::abs(dx), std::abs(dy));
                int xx = x + dx, yy = y + dy;
                if (0 <= xx && xx < size && 0 <= yy && yy < size)
                    setFunctionModule(xx, yy, dist != 2 && dist != 4);
            }
        }
    }

    void drawAlignmentPattern(int x, int y) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++)
                setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
        }
    }

    void addEccAndInterleave(const std::vector<bool>& data, std::vector<bool>& result) {
        int eclIdx = static_cast<int>(errorCorrectionLevel);
        int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[eclIdx][version];
        int blockEccLen = ECC_CODEWORDS_PER_BLOCK[eclIdx][version];
        int rawCodewords = getNumRawDataModules(version) / 8;
        int numShortBlocks = numBlocks - rawCodewords % numBlocks;
        int shortBlockDataLen = rawCodewords / numBlocks - blockEccLen;

        // Convert bit vector to bytes
        std::vector<uint8_t> dataBytes;
        dataBytes.reserve(data.size() / 8);
        for (size_t i = 0; i + 7 < data.size(); i += 8) {
            uint8_t b = 0;
            for (int j = 0; j < 8; j++)
                b = (b << 1) | (data[i + j] ? 1 : 0);
            dataBytes.push_back(b);
        }

        // Reed-Solomon generator
        std::vector<uint8_t> rsGen;
        getReedSolomonGenerator(blockEccLen, rsGen);

        // Split into blocks, compute ECC for each
        std::vector<std::vector<uint8_t>> blocks;
        std::vector<std::vector<uint8_t>> eccs;
        size_t k = 0;
        for (int i = 0; i < numBlocks; i++) {
            int datLen = shortBlockDataLen + (i < numShortBlocks ? 0 : 1);
            std::vector<uint8_t> blk(dataBytes.begin() + k, dataBytes.begin() + k + datLen);
            k += datLen;
            std::vector<uint8_t> ecc;
            getReedSolomonRemainder(blk, rsGen, ecc);
            blocks.push_back(std::move(blk));
            eccs.push_back(std::move(ecc));
        }

        // Interleave: data first (column by column), then ECC
        result.clear();
        int maxDataLen = shortBlockDataLen + 1;
        for (int i = 0; i < maxDataLen; i++) {
            for (size_t j = 0; j < blocks.size(); j++) {
                if (i < static_cast<int>(blocks[j].size())) {
                    uint8_t b = blocks[j][i];
                    for (int bit = 7; bit >= 0; bit--)
                        result.push_back(((b >> bit) & 1) != 0);
                }
            }
        }
        for (int i = 0; i < blockEccLen; i++) {
            for (size_t j = 0; j < eccs.size(); j++) {
                uint8_t b = eccs[j][i];
                for (int bit = 7; bit >= 0; bit--)
                    result.push_back(((b >> bit) & 1) != 0);
            }
        }
    }

    void drawCodewords(const std::vector<bool>& data) {
        size_t i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; vert++) {
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? size - 1 - vert : vert;
                    size_t idx = static_cast<size_t>(y) * size + x;
                    if (!isFunction.at(idx) && i < data.size()) {
                        modules.at(idx) = data.at(i);
                        i++;
                    }
                }
            }
        }
    }

    void applyMask(int msk) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                size_t idx = static_cast<size_t>(y) * size + x;
                if (isFunction.at(idx)) continue;
                bool invert;
                switch (msk) {
                    case 0: invert = (x + y) % 2 == 0; break;
                    case 1: invert = y % 2 == 0; break;
                    case 2: invert = x % 3 == 0; break;
                    case 3: invert = (x + y) % 3 == 0; break;
                    case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: invert = (x * y) % 2 + (x * y) % 3 == 0; break;
                    case 6: invert = ((x * y) % 2 + (x * y) % 3) % 2 == 0; break;
                    case 7: invert = ((x + y) % 2 + (x * y) % 3) % 2 == 0; break;
                    default: throw std::logic_error("Assertion error");
                }
                if (invert)
                    modules.at(idx) = !modules.at(idx);
            }
        }
    }

    int handleConstructorMasking() {
        int minPenalty = INT_MAX;
        int bestMask = 0;
        for (int i = 0; i < 8; i++) {
            drawFormatBits(i);
            applyMask(i);
            int penalty = computePenalty();
            if (penalty < minPenalty) {
                minPenalty = penalty;
                bestMask = i;
            }
            applyMask(i); // undo
        }
        drawFormatBits(bestMask);
        applyMask(bestMask);
        return bestMask;
    }

    int computePenalty() {
        int penalty = 0;
        // Rule 1: runs of 5+ same-color modules in rows/cols
        for (int y = 0; y < size; y++) {
            int runColor = -1;
            int runLen = 0;
            for (int x = 0; x < size; x++) {
                int c = module(x, y) ? 1 : 0;
                if (c == runColor) {
                    runLen++;
                    if (runLen == 5) penalty += 3;
                    else if (runLen > 5) penalty += 1;
                } else {
                    runColor = c;
                    runLen = 1;
                }
            }
        }
        for (int x = 0; x < size; x++) {
            int runColor = -1;
            int runLen = 0;
            for (int y = 0; y < size; y++) {
                int c = module(x, y) ? 1 : 0;
                if (c == runColor) {
                    runLen++;
                    if (runLen == 5) penalty += 3;
                    else if (runLen > 5) penalty += 1;
                } else {
                    runColor = c;
                    runLen = 1;
                }
            }
        }
        // Rule 2: 2x2 blocks of same color
        for (int y = 0; y < size - 1; y++) {
            for (int x = 0; x < size - 1; x++) {
                bool m = module(x, y);
                if (module(x + 1, y) == m && module(x, y + 1) == m && module(x + 1, y + 1) == m)
                    penalty += 3;
            }
        }
        // Rule 3: finder-like patterns (1011101 with 4-light border on one side)
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (x + 6 < size &&
                    module(x, y) && !module(x+1, y) && module(x+2, y) &&
                    module(x+3, y) && module(x+4, y) && !module(x+5, y) && module(x+6, y) &&
                    ((x + 10 < size && !module(x+7, y) && !module(x+8, y) && !module(x+9, y) && !module(x+10, y)) ||
                     (x - 4 >= 0 && !module(x-1, y) && !module(x-2, y) && !module(x-3, y) && !module(x-4, y))))
                    penalty += 40;
                if (y + 6 < size &&
                    module(x, y) && !module(x, y+1) && module(x, y+2) &&
                    module(x, y+3) && module(x, y+4) && !module(x, y+5) && module(x, y+6) &&
                    ((y + 10 < size && !module(x, y+7) && !module(x, y+8) && !module(x, y+9) && !module(x, y+10)) ||
                     (y - 4 >= 0 && !module(x, y-1) && !module(x, y-2) && !module(x, y-3) && !module(x, y-4))))
                    penalty += 40;
            }
        }
        // Rule 4: dark module proportion
        int dark = 0;
        for (size_t j = 0; j < modules.size(); j++) if (modules[j]) dark++;
        int total = static_cast<int>(modules.size());
        int percent = dark * 100 / total;
        int k = std::abs(percent - 50) / 5;
        penalty += k * 10;
        return penalty;
    }

    static void getAlignmentPatternPositions(int ver, std::vector<int>& result) {
        if (ver == 1) { result.clear(); return; }
        int numAlign = ver / 7 + 2;
        int step = (ver == 32) ? 26 :
                   (ver + 7) / (numAlign - 1) * 2 - (ver == 32 ? 0 : 2);
        result.resize(numAlign);
        int pos = ver * 4 + 10;
        for (int i = numAlign - 1; i >= 1; i--, pos -= step)
            result[i] = pos;
        result[0] = 6;
    }

    static int getNumRawDataModules(int ver) {
        if (ver < 1 || ver > 40) throw std::invalid_argument("Version out of range");
        int result = (16 * ver + 128) * ver + 8;
        std::vector<int> alignPatPos;
        getAlignmentPatternPositions(ver, alignPatPos);
        int numAlign = static_cast<int>(alignPatPos.size());
        result -= 25 * numAlign - 10;
        if (ver >= 7) result -= 36;
        return result;
    }

    static int getNumDataCodewords(int ver, Ecc ecl) {
        int eclIdx = static_cast<int>(ecl);
        return getNumRawDataModules(ver) / 8
             - ECC_CODEWORDS_PER_BLOCK[eclIdx][ver]
             * NUM_ERROR_CORRECTION_BLOCKS[eclIdx][ver];
    }

    static void getReedSolomonGenerator(int degree, std::vector<uint8_t>& result) {
        result.assign(1, 1);
        for (int i = 0; i < degree; i++) {
            // Multiply result by (x - alpha^i) = (x + alpha^i) in GF(2)
            std::vector<uint8_t> next(result.size() + 1, 0);
            uint8_t coeff = gfExp()[i];
            for (size_t j = 0; j < result.size(); j++) {
                next[j] ^= result[j];                         // shift
                next[j + 1] ^= gfMultiply(result[j], coeff);  // multiply by coeff
            }
            result = std::move(next);
        }
    }

    static void getReedSolomonRemainder(const std::vector<uint8_t>& data,
                                        const std::vector<uint8_t>& gen,
                                        std::vector<uint8_t>& result) {
        int degree = static_cast<int>(gen.size()) - 1;
        std::vector<uint8_t> rem(degree, 0);
        for (uint8_t b : data) {
            uint8_t factor = b ^ rem.front();
            rem.erase(rem.begin());
            rem.push_back(0);
            if (factor != 0) {
                for (size_t i = 0; i < gen.size(); i++)
                    rem[i] ^= gfMultiply(gen[i], factor);
            }
        }
        result = std::move(rem);
    }

    // ECC tables: [ecc level][version 1..40], index 0 unused
    static const int ECC_CODEWORDS_PER_BLOCK[4][41];
    static const int NUM_ERROR_CORRECTION_BLOCKS[4][41];
};

inline const int QrCode::ECC_CODEWORDS_PER_BLOCK[4][41] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};

inline const int QrCode::NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

} // namespace qrcodegen
