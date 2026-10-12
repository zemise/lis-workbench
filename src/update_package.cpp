#include "update_package.h"
#include "update_manifest.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace lis_update {
namespace {
unsigned u16(const unsigned char* p) { return p[0] | (unsigned(p[1]) << 8); }
std::uint32_t u32(const unsigned char* p) { return u16(p) | (std::uint32_t(u16(p+2)) << 16); }
std::string json_string(const std::string& s) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + "\"";
}
}
std::string serialize_update_manifest(const UpdateManifest& m) {
    std::ostringstream out;
    out << "{\"appId\":" << json_string(m.app_id) << ",\"version\":" << json_string(m.version)
        << ",\"channel\":" << json_string(m.channel) << ",\"minUpdaterVersion\":" << json_string(m.min_updater_version)
        << ",\"publishedAt\":" << json_string(m.published_at) << ",\"package\":{\"file\":"
        << json_string(m.package.file) << ",\"size\":" << m.package.size
        << ",\"sha256\":" << json_string(m.package.sha256) << "}}";
    return out.str();
}
bool validate_update_zip(const std::wstring& path, UpdateZipInfo& info, std::string& error) {
    info = {};
    auto fail = [&]() { error = "invalid ZIP or unsafe/protected package entry"; return false; };
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return fail();
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 22 || std::uint64_t(end) > kMaxUpdatePackageSize) return fail();
    const size_t tail_size = size_t((std::min)(std::uint64_t(end), std::uint64_t(65557)));
    std::vector<unsigned char> tail(tail_size);
    file.seekg(end - std::streamoff(tail_size));
    file.read(reinterpret_cast<char*>(tail.data()), tail.size());
    size_t eocd = tail.size();
    for (size_t i=tail.size()-22;; --i) {
        if (u32(&tail[i]) == 0x06054b50 && i + 22 + u16(&tail[i+20]) == tail.size()) { eocd=i; break; }
        if (!i) break;
    }
    if (eocd == tail.size()) return fail();
    const auto* e = &tail[eocd];
    const unsigned count = u16(e+10);
    const std::uint64_t directory_size = u32(e+12), offset = u32(e+16);
    if (u16(e+4) || u16(e+6) || u16(e+8) != count || count < 2 || count > 1000 ||
        directory_size > 1024*1024 || offset + directory_size != std::uint64_t(end)-tail_size+eocd) return fail();
    std::vector<unsigned char> directory(size_t(directory_size), 0);
    file.seekg(std::streamoff(offset));
    file.read(reinterpret_cast<char*>(directory.data()), directory.size());
    if (!file) return fail();
    size_t cursor=0;
    std::set<std::string> names;
    for (unsigned n=0; n<count; ++n) {
        if (cursor+46 > directory.size()) return fail();
        const auto* p=&directory[cursor];
        unsigned len=u16(p+28), extra=u16(p+30), comment=u16(p+32);
        if (u32(p)!=0x02014b50 || cursor+46+len+extra+comment > directory.size() ||
            (u16(p+8)&1) || (u16(p+10)!=0 && u16(p+10)!=8) || u16(p+34) ||
            ((u32(p+38)>>16)&0170000)==0120000) return fail();
        std::string name(reinterpret_cast<const char*>(p+46), len);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return char(std::tolower(c)); });
        if (name.empty() || !std::all_of(name.begin(),name.end(),[](unsigned char c){
                return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.';
            }) || name.find("..")!=std::string::npos ||
            (name.size()<4 || (name.substr(name.size()-4)!=".exe" && name.substr(name.size()-4)!=".dll")) ||
            !names.insert(name).second) return fail();
        const auto stem=name.substr(0,name.find('.'));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||
           (stem.size()==4&&(stem.substr(0,3)=="com"||stem.substr(0,3)=="lpt")&&stem[3]>='1'&&stem[3]<='9')) return fail();
        const std::uint64_t local=u32(p+42), compressed=u32(p+20);
        unsigned char header[30];
        file.seekg(std::streamoff(local)); file.read(reinterpret_cast<char*>(header),30);
        if (!file || u32(header)!=0x04034b50 || u16(header+26)!=len ||
            u16(header+6)!=u16(p+8) || u16(header+8)!=u16(p+10) ||
            local+30+len+u16(header+28)+compressed>offset) return fail();
        std::string local_name(len,'\0'); file.read(&local_name[0],len);
        if (!file || local_name!=std::string(reinterpret_cast<const char*>(p+46),len)) return fail();
        info.expanded_bytes += u32(p+24); ++info.files;
        if (info.expanded_bytes>512ULL*1024*1024) return fail();
        cursor += 46+len+extra+comment;
    }
    if (cursor!=directory.size() || !names.count("lis_workbench.exe") || !names.count("updater.exe")) return fail();
    error.clear(); return true;
}
}
