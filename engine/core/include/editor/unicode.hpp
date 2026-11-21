#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace editor {
// Standard UTF-8 <-> UTF-16, not JNI's modified UTF-8/CESU-8. Embedded U+0000
// is representable here; individual path/identifier adapters may reject it.
inline std::string utf8_from_utf16(std::span<const std::uint16_t> text) {
    std::string result;
    result.reserve(text.size());
    for(std::size_t index=0; index<text.size(); ++index) {
        std::uint32_t point=text[index];
        if(point >= 0xd800 && point <= 0xdbff) {
            if(index+1 == text.size() || text[index+1] < 0xdc00 || text[index+1] > 0xdfff)
                throw std::invalid_argument("invalid UTF-16 surrogate pair");
            point=0x10000+((point-0xd800)<<10)+(text[++index]-0xdc00);
        } else if(point >= 0xdc00 && point <= 0xdfff) throw std::invalid_argument("unpaired UTF-16 surrogate");
        auto byte=[&](std::uint32_t value) { result.push_back(static_cast<char>(value)); };
        if(point < 0x80) byte(point);
        else if(point < 0x800) { byte(0xc0|(point>>6)); byte(0x80|(point&0x3f)); }
        else if(point < 0x10000) { byte(0xe0|(point>>12)); byte(0x80|((point>>6)&0x3f)); byte(0x80|(point&0x3f)); }
        else { byte(0xf0|(point>>18)); byte(0x80|((point>>12)&0x3f)); byte(0x80|((point>>6)&0x3f)); byte(0x80|(point&0x3f)); }
    }
    return result;
}
inline std::vector<std::uint16_t> utf16_from_utf8(std::string_view text) {
    std::vector<std::uint16_t> result;
    result.reserve(text.size());
    for(std::size_t index=0; index<text.size();) {
        const auto first=static_cast<unsigned char>(text[index++]);
        std::uint32_t point=0, minimum=0;
        unsigned count=0;
        if(first < 0x80) point=first;
        else if(first >= 0xc2 && first <= 0xdf) { point=first&0x1f; minimum=0x80; count=1; }
        else if(first >= 0xe0 && first <= 0xef) { point=first&0x0f; minimum=0x800; count=2; }
        else if(first >= 0xf0 && first <= 0xf4) { point=first&0x07; minimum=0x10000; count=3; }
        else throw std::invalid_argument("invalid UTF-8 leading byte");
        if(count > text.size()-index) throw std::invalid_argument("truncated UTF-8 sequence");
        for(unsigned next=0; next<count; ++next) {
            const auto byte=static_cast<unsigned char>(text[index++]);
            if((byte&0xc0) != 0x80) throw std::invalid_argument("invalid UTF-8 continuation");
            point=(point<<6)|(byte&0x3f);
        }
        if(point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
            throw std::invalid_argument("invalid Unicode scalar value");
        if(point < 0x10000) result.push_back(static_cast<std::uint16_t>(point));
        else {
            point -= 0x10000;
            result.push_back(static_cast<std::uint16_t>(0xd800+(point>>10)));
            result.push_back(static_cast<std::uint16_t>(0xdc00+(point&0x3ff)));
        }
    }
    return result;
}
} // namespace editor
