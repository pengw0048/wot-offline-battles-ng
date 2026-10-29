#include "stock_spam_filter.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
// Injectable classifications exercise the original instruction flow without
// claiming that a host's locale database reproduces the Windows CRT.
class FixtureCtype : public std::ctype<wchar_t> {
    int mode,index;
public:
    FixtureCtype(int value,int order):mode(value),index(order) {}
protected:
    bool do_is(mask kind,wchar_t ch) const override {
        const bool alpha=mode==1?((ch>=L'A'&&ch<=L'Z')||(ch>=L'a'&&ch<=L'z')):(ch==L'A'&&(mode!=3||index==1));
        const bool lower=mode==1?(ch>=L'a'&&ch<=L'z'):(mode==2&&index==0&&alpha);
        return kind==std::ctype_base::alpha?alpha:kind==std::ctype_base::lower?lower:false;
    }
    wchar_t do_tolower(wchar_t ch) const override {
        if(mode==1)return ch>=L'A'&&ch<=L'Z'?ch+L'a'-L'A':ch;
        return index==0?L'x':L'y';
    }
};
}
int main() {
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream input(line);int mode;std::string hex;
        if(!(input>>mode))return 2;input>>hex;
        if(hex.size()%4)return 2;
        std::u16string text;
        for(size_t offset=0;offset<hex.size();offset+=4)text.push_back(static_cast<char16_t>(std::stoul(hex.substr(offset,4),NULL,16)));
        std::vector<std::locale> locales;
        for(int index=0;index<(mode==0?0:mode==1?1:2);++index)locales.push_back(std::locale(std::locale::classic(),new FixtureCtype(mode,index)));
        const auto output=BW::StockSpamFilterDetail::removeSpam(std::move(text),locales);
        for(const auto ch:output)std::cout<<std::hex<<std::setfill('0')<<std::setw(4)<<static_cast<unsigned int>(ch);
        std::cout<<'\n';
    }
}
