#pragma once
#include <locale>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace BW { namespace StockSpamFilterDetail {

inline void eraseUnits(std::u16string& text,size_t start,size_t count) {
    // #1513's 0x006e09a0 shrinks by count even when wcsncmp's NUL semantics
    // make the nominal deleted range extend past the end of the string.
    if(start>text.size()||count>text.size())throw std::length_error("Invalid WGSpamFilter deletion range");
    if(count<text.size()-start)
        for(size_t source=start+count;source<text.size();++source)text[source-count]=text[source];
    text.resize(text.size()-count);
}

// #1513's 0x006e06b0 operates on UTF-16 units, including embedded NULs.
inline bool sameChunk(const std::u16string& text, size_t first, size_t second, size_t width) {
    if(first>text.size()||second>text.size())throw std::length_error("Invalid WGSpamFilter comparison range");
    for(size_t offset=0;offset<width;++offset) {
        if(offset>text.size()-first||offset>text.size()-second)throw std::length_error("Invalid WGSpamFilter comparison range");
        const char16_t a=text[first+offset],b=text[second+offset];
        if(a!=b)return false;
        // The original uses wcsncmp, rather than a length-aware comparison.
        if(a==0)return true;
    }
    return true;
}

inline std::u16string removeSpam(std::u16string text,const std::vector<std::locale>& locales) {
    size_t capitals=0;
    for(const auto ch:text)for(const auto& locale:locales) {
        const auto& type=std::use_facet<std::ctype<wchar_t>>(locale);
        if(type.is(std::ctype_base::alpha,static_cast<wchar_t>(ch))&&
           !type.is(std::ctype_base::lower,static_cast<wchar_t>(ch))) {++capitals;break;}
    }
    if(capitals>text.size()/2)for(auto& ch:text)for(const auto& locale:locales) {
        const auto& type=std::use_facet<std::ctype<wchar_t>>(locale);
        if(type.is(std::ctype_base::alpha,static_cast<wchar_t>(ch))) {
            ch=static_cast<char16_t>(type.tolower(static_cast<wchar_t>(ch)));break;
        }
    }
    for(size_t start=0;start<text.size();++start) {
        size_t end=start+1;
        while(end<text.size()&&text[end]==text[start])++end;
        if(end-start>3)eraseUnits(text,start+3,end-start-3);
    }
    for(size_t start=0;start<text.size();++start) {
        if(text[start]==u' '||text[start]==u'\t'||text[start]==u'\n')continue;
        for(size_t width=2;width<=6;++width) {
            size_t count=1,next=start+width;
            while(next<text.size()&&sameChunk(text,start,next,width)) {++count;next+=width;}
            if(count>3)eraseUnits(text,start+2*width,(count-2)*width);
        }
    }
    return text;
}

struct Filter {
    std::vector<std::locale> locales;
    Filter() {
        // #1513's 0x006e0530 skips unavailable names; it has no fallback locale.
        for(const auto* name:{"English_US","German_Germany","Russian_Russia"}) {
            try {locales.push_back(std::locale(name));}
            catch(const std::runtime_error&) {}
        }
    }
    std::u16string apply(std::u16string text) const {return removeSpam(std::move(text),locales);}
};

} }
