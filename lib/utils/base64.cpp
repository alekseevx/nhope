#include <iterator>

#include "gsl/span"
#include "nhope/utils/base64.h"

namespace nhope {

namespace {

constexpr bool isBase64(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return true;
    }

    if (c >= 'a' && c <= 'z') {
        return true;
    }

    if (c >= '0' && c <= '9') {
        return true;
    }

    if (c == '+') {
        return true;
    }

    if (c == '/') {
        return true;
    }

    if (c == '=') {
        return true;
    }

    return false;
}

char encode(unsigned char uc)
{
    if (uc < 26) {
        return 'A' + static_cast<char>(uc);
    }

    if (uc < 52) {
        return 'a' + static_cast<char>(uc) - 26;
    }

    if (uc < 62) {
        return '0' + static_cast<char>(uc) - 52;
    }

    if (uc == 62) {
        return '+';
    }

    return '/';
}

unsigned char decode(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<unsigned char>(c - 'A');
    }

    if (c >= 'a' && c <= 'z') {
        return static_cast<unsigned char>(c - 'a' + 26);
    }

    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0' + 52);
    }

    if (c == '+') {
        return 62;
    }

    return 63;
}

}   // namespace

std::vector<uint8_t> fromBase64(std::string_view str)
{
    std::string newStr;

    newStr.reserve(str.size());

    for (char j : str) {
        if (isBase64(j)) {
            newStr += j;
        }
    }

    std::vector<unsigned char> retval;

    if (newStr.empty()) {
        return retval;
    }

    const auto newStrSize = newStr.size();

    // Note: This is how we were previously computing the size of the return
    //       sequence.  The method below is more efficient (and correct).
    // size_t lines = str.size() / 78;
    // size_t totalBytes = (lines * 76) + (((str.size() - (lines * 78)) * 3) / 4);

    // Figure out how long the final sequence is going to be.
    const size_t totalBytes = (newStrSize * 3 / 4) + 1;

    retval.reserve(totalBytes);

    unsigned char by1 = 0;
    unsigned char by2 = 0;
    unsigned char by3 = 0;
    unsigned char by4 = 0;

    char c1{};
    char c2{};
    char c3{};
    char c4{};

    for (size_t i = 0; i < newStrSize; i += 4) {
        c2 = 'A';
        c3 = 'A';
        c4 = 'A';

        c1 = newStr[i];

        if ((i + 1) < newStrSize) {
            c2 = newStr[i + 1];
        }

        if ((i + 2) < newStrSize) {
            c3 = newStr[i + 2];
        }

        if ((i + 3) < newStrSize) {
            c4 = newStr[i + 3];
        }

        by1 = decode(c1);
        by2 = decode(c2);
        by3 = decode(c3);
        by4 = decode(c4);

        retval.push_back(static_cast<unsigned char>(by1 << 2) | (by2 >> 4));

        if (c3 != '=') {
            retval.push_back(static_cast<unsigned char>((by2 & 0xf) << 4) | (by3 >> 2));
        }

        if (c4 != '=') {
            retval.push_back(static_cast<unsigned char>((by3 & 0x3) << 6) | by4);
        }
    }

    return retval;
}

std::string toBase64(gsl::span<const uint8_t> plainSeq)
{
    std::string retval;

    if (plainSeq.empty()) {
        return retval;
    }

    const auto plainSize = plainSeq.size();

    // Reserve enough space for the returned base64 string
    const size_t base64Bytes = (((plainSeq.size() * 4) / 3) + 1);
    const size_t newlineBytes = (((base64Bytes * 2) / 76) + 1);
    const size_t totalBytes = base64Bytes + newlineBytes;

    retval.reserve(totalBytes);

    unsigned char by1 = 0;
    unsigned char by2 = 0;
    unsigned char by3 = 0;
    unsigned char by4 = 0;
    unsigned char by5 = 0;
    unsigned char by6 = 0;
    unsigned char by7 = 0;

    for (size_t i = 0; i < plainSize; i += 3) {
        by1 = plainSeq[i];
        by2 = 0;
        by3 = 0;

        if ((i + 1) < plainSize) {
            by2 = plainSeq[i + 1];
        }

        if ((i + 2) < plainSize) {
            by3 = plainSeq[i + 2];
        }

        by4 = by1 >> 2;
        by5 = static_cast<unsigned char>((by1 & 0x3) << 4) | (by2 >> 4);
        by6 = static_cast<unsigned char>((by2 & 0xf) << 2) | (by3 >> 6);
        by7 = by3 & 0x3f;

        retval += encode(by4);
        retval += encode(by5);

        if ((i + 1) < plainSize) {
            retval += encode(by6);
        } else {
            retval += "=";
        }

        if ((i + 2) < plainSize) {
            retval += encode(by7);
        } else {
            retval += "=";
        }
    }

    std::string outString;
    outString.reserve(totalBytes);
    auto iter = retval.begin();

    while ((retval.end() - iter) > 76) {
        outString.insert(outString.end(), iter, iter + 76);
        outString += "\r\n";
        iter += 76;
    }

    outString.insert(outString.end(), iter, retval.end());
    return outString;
}

}   // namespace nhope
