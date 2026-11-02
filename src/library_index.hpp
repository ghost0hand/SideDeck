#pragma once
#include "library.hpp"
namespace libraryindex {
inline std::wstring identity(int kind,std::wstring target) {
    if(target.find(L"://")==std::wstring::npos) {std::replace(target.begin(),target.end(),L'/',L'\\');target=lib::toLowerCp(target);}
    return std::to_wstring(kind)+L"|"+target;
}
inline std::wstring identity(const lib::Item& item) {
    return identity(item.kind,item.target);
}
inline wchar_t initial(const std::wstring& title) {
    for(size_t i=0;i<title.size();++i) {
        wchar_t ch=title[i];
        if(ch>=L'a'&&ch<=L'z')return ch-L'a'+L'A';
        if(ch>=L'A'&&ch<=L'Z')return ch;
        if(ch>=L'0'&&ch<=L'9')return L'#';
        if(ch>=0x3400&&ch<=0x9fff) {
            char encoded[4]{};BOOL substituted=FALSE;
            int bytes=WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,&ch,1,encoded,4,nullptr,&substituted);
            static const int boundaries[]={-20319,-20284,-19776,-19219,-18711,-18527,-18240,-17923,-17418,-16475,-16213,-15641,-15166,-14923,-14915,-14631,-14150,-14091,-13319,-12839,-12557,-11848,-11056};
            static const wchar_t* alphabet=L"ABCDEFGHJKLMNOPQRSTWXYZ";
            int code=bytes==2 ? (unsigned char)encoded[0]*256+(unsigned char)encoded[1]-65536 : 0;
            if(!substituted && code>=boundaries[0] && code<=-10247) {
                for(int group=22;group>=0;--group)if(code>=boundaries[group])return alphabet[group];
            }
            // Windows' zh-CN collation includes pinyin ordering for Han
            // characters outside GB2312 too. Compare against each syllable group.
            static const wchar_t* starts=L"阿芭擦搭蛾发噶哈击喀垃妈拿哦啪期然撒塌挖昔压匝";
            static const wchar_t* letters=L"ABCDEFGHJKLMNOPQRSTWXYZ";
            wchar_t result=L'A';
            for(int group=0;starts[group];++group) {
                int compared=CompareStringEx(L"zh-CN",NORM_IGNORECASE,&ch,1,starts+group,1,nullptr,nullptr,0);
                if(compared==CSTR_LESS_THAN)break;
                if(compared==0)return L'#';
                result=letters[group];
            }
            return result;
        }
    }
    return L'#';
}
inline bool favorite(const lib::Config& config,const lib::Item& item) {
    return std::find(config.favorites.begin(),config.favorites.end(),identity(item))!=config.favorites.end();
}
inline bool toggle(lib::Config& config,const lib::Item& item) {
    auto key=identity(item);auto found=std::find(config.favorites.begin(),config.favorites.end(),key);
    if(found!=config.favorites.end()) {config.favorites.erase(found);return false;}
    config.favorites.push_back(key);return true;
}
inline bool removed(const lib::Config& config,const lib::Item& item) {
    auto key=identity(item);
    return std::find(config.removed.begin(),config.removed.end(),key)!=config.removed.end();
}
inline void remember(std::vector<std::wstring>& list,const std::wstring& key) {
    if(key.empty()||std::find(list.begin(),list.end(),key)!=list.end())return;
    list.push_back(key);
}
inline void forget(std::vector<std::wstring>& list,const std::wstring& key) {
    list.erase(std::remove(list.begin(),list.end(),key),list.end());
}
inline int find(const std::vector<lib::Item>& items,wchar_t letter,int after=-1) {
    for(size_t step=1;step<=items.size();++step) {
        int index=(after+(int)step)%(int)items.size();
        if(initial(items[index].title)==letter)return index;
    }
    return -1;
}
} // namespace libraryindex
