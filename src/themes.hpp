#pragma once
#include "render.hpp"
#include "json.hpp"
#include <filesystem>
#include <fstream>

// Theme packages contain data only: a JSON file and an optional sibling image.
namespace themes {
namespace fs = std::filesystem;
struct Entry {
    std::wstring key, name, description;
    fs::path image,video;
    ui::Theme palette;
    int base=0;
    float imageOpacity=0.75f;
    std::string videoFit="auto";
};
inline std::vector<Entry> entries=[] {
    std::vector<Entry> result;
    for(int i=0;i<ui::THEME_COUNT;++i){Entry e;e.name=ui::themeName(i);e.description=ui::themeDescription(i);e.palette=ui::themePreset(i);e.base=i;result.push_back(e);}
    return result;
}();
inline bool safeLeaf(const std::wstring& value) {
    return !value.empty() && value!=L"." && value!=L".." &&
        value.find_first_of(L"/\\:<>\"|?*")==std::wstring::npos && value.back()!=L'.' && value.back()!=L' ';
}
inline bool color(const std::string& value, ui::Color& out) {
    if((value.size()!=7 && value.size()!=9) || value[0]!='#')return false;
    unsigned bytes[4]={0,0,0,255};
    for(size_t i=1;i<value.size();++i) {
        char c=value[i];int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(n<0)return false;
        size_t at=(i-1)/2;if(i%2)bytes[at]=n*16;else bytes[at]+=n;
    }
    out=ui::Color(bytes[0],bytes[1],bytes[2],bytes[3]);return true;
}
inline bool load(const fs::path& path, Entry& result) {
    try {
        if(!fs::is_regular_file(path) || fs::file_size(path)>32768)return false;
        std::ifstream file(path,std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(file)),{});
        int depth=0;bool quoted=false,escaped=false;
        for(char c:data) {
            if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}
            else if(c=='"')quoted=true;
            else if(c=='{' || c=='['){if(++depth>16)return false;}
            else if(c=='}' || c==']'){if(--depth<0)return false;}
        }
        if(depth || quoted)return false;
        auto root=sj::Parser(data).parseStrict();
        if(!root.isObj() || root.getStr("format")!="sidedeck-theme" || root.getInt("version")!=1)return false;
        Entry e; e.name=ui::utf8(root.getStr("name"));e.description=ui::utf8(root.getStr("description"));
        if(e.name.empty() || e.name.size()>48 || e.description.size()>100)return false;
        auto base=root.find("base");
        if(base && (!base->isNum() || !std::isfinite(base->num) || base->num<0 || base->num>=ui::THEME_COUNT || floor(base->num)!=base->num))return false;
        e.base=root.getInt("base",0);e.palette=ui::themePreset(e.base);
        if(auto colors=root.find("colors")) {
            if(!colors->isObj())return false;
            for(auto& pair:colors->obj) {
                if(!pair.second.isStr())return false;
                ui::Color* target=nullptr;
#define FIELD(name) if(pair.first==#name)target=&e.palette.name;
                FIELD(panel) FIELD(panelTop) FIELD(text) FIELD(textDim) FIELD(textFaint)
                FIELD(accentA) FIELD(accentB) FIELD(selBg) FIELD(sep) FIELD(chip)
                FIELD(shadow) FIELD(glassTop) FIELD(glassBottom) FIELD(rim) FIELD(reader)
#undef FIELD
                if(!target || !color(pair.second.str,*target))return false;
            }
        }
        for(auto field:{"backdropOpacity","imageOpacity"})if(auto v=root.find(field)) {
            if(!v->isNum() || !std::isfinite(v->num) || v->num<0 || v->num>1)return false;
            if(std::string(field)=="backdropOpacity")e.palette.backdropOpacity=(float)v->num;else e.imageOpacity=(float)v->num;
        }
        auto image=ui::utf8(root.getStr("background"));
        if(!image.empty()) {
            if(!safeLeaf(image))return false;
            auto extension=fs::path(image).extension().wstring();
            std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
            if(extension!=L".png" && extension!=L".jpg" && extension!=L".jpeg")return false;
            e.image=path.parent_path()/image;
            if(!fs::is_regular_file(e.image) || fs::file_size(e.image)>16*1024*1024)return false;
            // Check dimensions before loading the full bitmap into the active theme.
            Gdiplus::Bitmap probe(e.image.c_str());
            if(probe.GetLastStatus()!=Gdiplus::Ok || !probe.GetWidth() || !probe.GetHeight() || probe.GetWidth()>4096 || probe.GetHeight()>4096)return false;
        }
        if(auto fit=root.find("videoFit")) {
            if(!fit->isStr() || (fit->str!="auto" && fit->str!="contain" && fit->str!="cover"))return false;
            e.videoFit=fit->str;
        }
        auto video=ui::utf8(root.getStr("video"));
        if(!video.empty()) {
            if(!safeLeaf(video) || fs::path(video).extension()!=L".mp4")return false;
            e.video=path.parent_path()/video;
            if(!fs::is_regular_file(e.video) || fs::file_size(e.video)>256*1024*1024)return false;
        }
        result=std::move(e);return true;
    } catch(...) {return false;}
}
inline void reload(const fs::path& directory) {
    entries.clear();
    for(int i=0;i<ui::THEME_COUNT;++i) {
        Entry e;e.name=ui::themeName(i);e.description=ui::themeDescription(i);e.palette=ui::themePreset(i);e.base=i;entries.push_back(e);
    }
    std::error_code ec;std::vector<fs::path> files;
    for(fs::directory_iterator it(directory,ec),end;!ec && it!=end;it.increment(ec)) {
        if(files.size()>=128)break;
        auto path=it->path();
        if(it->is_directory(ec))path/=L"theme.json";
        if(path.extension()==L".json" && fs::is_regular_file(path,ec))files.push_back(path);
    }
    std::sort(files.begin(),files.end());
    for(auto& path:files) {Entry e;if(load(path,e)){e.key=path.lexically_relative(directory).generic_wstring();entries.push_back(std::move(e));}}
}
inline int count(){return (int)entries.size();}
inline const Entry& get(int index){return entries.at((size_t)std::clamp(index,0,count()-1));}
inline int selected(int base,const std::wstring& key) {
    for(int i=ui::THEME_COUNT;i<count();++i)if(entries[i].key==key)return i;
    return std::clamp(base,0,ui::THEME_COUNT-1);
}
inline bool importVideo(const fs::path& source,const fs::path& directory,std::wstring& key) {
    try {
        if(!fs::is_regular_file(source) || fs::file_size(source)>256*1024*1024)return false;
        std::error_code ec;fs::create_directories(directory,ec);if(ec)return false;
        fs::path dest;
        for(int i=1;i<=1000;++i){dest=directory/(L"video-"+std::to_wstring(i));if(fs::create_directory(dest,ec))break;if(ec)return false;dest.clear();}
        if(dest.empty())return false;
        fs::copy_file(source,dest/L"wallpaper.mp4",fs::copy_options::none,ec);
        if(ec){fs::remove(dest,ec);return false;}
        sj::Value value(sj::Type::Object);value["format"]="sidedeck-theme";value["version"]=1;
        value["name"]=ui::narrow(source.stem().wstring().substr(0,48));value["description"]="MP4 动态玻璃";
        value["base"]=0;value["video"]="wallpaper.mp4";value["imageOpacity"]=0.88;
        std::ofstream file(dest/L"theme.json",std::ios::binary);file<<sj::dump(value);file.close();
        if(!file){fs::remove(dest/L"theme.json",ec);fs::remove(dest/L"wallpaper.mp4",ec);fs::remove(dest,ec);return false;}
        key=(dest/L"theme.json").lexically_relative(directory).generic_wstring();return true;
    }catch(...){return false;}
}
inline bool import(const fs::path& source,const fs::path& directory,std::wstring& key) {
    auto extension=source.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
    if(extension==L".mp4")return importVideo(source,directory,key);
    Entry e;if(!load(source,e))return false;
    std::error_code ec;fs::create_directories(directory,ec);if(ec)return false;
    auto stem=source.stem().wstring();if(!safeLeaf(stem))stem=L"custom";
    fs::path dest;
    for(int i=1;i<=1000;++i){dest=directory/(stem+L"-"+std::to_wstring(i));if(fs::create_directory(dest,ec))break;if(ec)return false;dest.clear();}
    if(dest.empty())return false;
    auto jsonDest=dest/L"theme.json";
    // Reserve theme.json for metadata, so image copying cannot overwrite it.
    if(!e.image.empty())fs::copy_file(e.image,dest/e.image.filename(),fs::copy_options::none,ec);
    if(!ec && !e.video.empty())fs::copy_file(e.video,dest/e.video.filename(),fs::copy_options::none,ec);
    if(!ec)fs::copy_file(source,jsonDest,fs::copy_options::none,ec);
    if(ec){std::error_code ignored;fs::remove(jsonDest,ignored);if(!e.image.empty())fs::remove(dest/e.image.filename(),ignored);if(!e.video.empty())fs::remove(dest/e.video.filename(),ignored);fs::remove(dest,ignored);return false;}
    key=jsonDest.lexically_relative(directory).generic_wstring();return true;
}
}
