#include "login_core.h"
#include <algorithm>
#include <array>
#include <climits>
#include <codecvt>
#include <locale>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonCryptor.h>
#else
#include <openssl/evp.h>
#endif

namespace login {
namespace {
bool whitespace(wchar_t c) {
    return (c>=9 && c<=13) || c==0x20 || c==0x85 || c==0xa0 || c==0x1680 ||
           (c>=0x2000 && c<=0x200a) || c==0x2028 || c==0x2029 ||
           c==0x202f || c==0x205f || c==0x3000;
}
bool base64(const std::wstring& text, std::vector<unsigned char>& out) {
    std::wstring s;
    for(auto c:text) { if(c==L' '||c==L'\t'||c==L'\r'||c==L'\n')continue; s+=c; }
    if(s.empty() || s.size()%4) return false;
    const std::wstring alphabet=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(std::size_t i=0;i<s.size();i+=4) {
        unsigned v=0; int padding=0;
        for(int j=0;j<4;++j) {
            if(s[i+j]==L'=') { if(i+4!=s.size() || j<2)return false; ++padding; v<<=6; }
            else { if(padding)return false; auto n=alphabet.find(s[i+j]);if(n==std::wstring::npos)return false;v=(v<<6)|unsigned(n); }
        }
        out.push_back(static_cast<unsigned char>(v>>16));
        if(padding<2)out.push_back(static_cast<unsigned char>(v>>8));
        if(!padding)out.push_back(static_cast<unsigned char>(v));
    }
    return true;
}
bool aes(const std::vector<unsigned char>& input, const std::string& key,
         const std::string& iv, std::vector<unsigned char>& output) {
    if(input.empty() || input.size()%16 || input.size()>4096)return false;
    output.resize(input.size()+16);
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_KEY_HANDLE handle=nullptr;
    bool ok=false; DWORD size=0,used=0; std::vector<unsigned char> object;
    std::array<unsigned char,16> mutable_iv{};
    std::copy(iv.begin(),iv.end(),mutable_iv.begin());
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_AES_ALGORITHM,nullptr,0)>=0 &&
       BCryptSetProperty(alg,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),sizeof(BCRYPT_CHAIN_MODE_CBC),0)>=0 &&
       BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&used,0)>=0) {
        object.resize(size);
        if(BCryptGenerateSymmetricKey(alg,&handle,object.data(),size,reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),ULONG(key.size()),0)>=0 &&
           BCryptDecrypt(handle,const_cast<PUCHAR>(input.data()),ULONG(input.size()),nullptr,mutable_iv.data(),16,output.data(),ULONG(output.size()),&used,BCRYPT_BLOCK_PADDING)>=0) {
            output.resize(used);ok=true;
        }
    }
    if(handle)BCryptDestroyKey(handle);
    if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(!object.empty())SecureZeroMemory(object.data(),object.size());
    return ok;
#elif defined(__APPLE__)
    std::size_t used=0;
    const auto rc=CCCrypt(kCCDecrypt,kCCAlgorithmAES,kCCOptionPKCS7Padding,
                         key.data(),key.size(),iv.data(),input.data(),input.size(),
                         output.data(),output.size(),&used);
    if(rc!=kCCSuccess)return false;
    output.resize(used);return true;
#else
    auto* ctx=EVP_CIPHER_CTX_new();if(!ctx)return false;
    int n=0,last=0;
    const bool ok=EVP_DecryptInit_ex(ctx,EVP_aes_256_cbc(),nullptr,
        reinterpret_cast<const unsigned char*>(key.data()),reinterpret_cast<const unsigned char*>(iv.data()))==1 &&
        EVP_DecryptUpdate(ctx,output.data(),&n,input.data(),int(input.size()))==1 &&
        EVP_DecryptFinal_ex(ctx,output.data()+n,&last)==1;
    EVP_CIPHER_CTX_free(ctx);if(ok)output.resize(n+last);return ok;
#endif
}
void wipe(std::vector<unsigned char>& bytes) { volatile unsigned char* p=bytes.data();for(std::size_t i=0;i<bytes.size();++i)p[i]=0; }
}
std::wstring trim(const std::wstring& text) {
    auto first=std::find_if_not(text.begin(),text.end(),whitespace);
    auto last=std::find_if_not(text.rbegin(),text.rend(),whitespace).base();
    return first<last ? std::wstring(first,last) : std::wstring{};
}
bool valid_code(const std::wstring& code) {
    return !code.empty() && code.size()<=50 && code.find_first_of(L"';")==std::wstring::npos && code.find(L"--")==std::wstring::npos;
}
bool decrypt_legacy(const std::wstring& stored, std::wstring& plaintext) {
    plaintext.clear();
    if(stored.size()<5 || stored.size()>4096)return false;
    std::string r;
    for(int i=0;i<5;++i) {
        int c=int(stored[stored.size()-5+i])+(i%2 ? -2 : 2);
        if(c<'0'||c>'9')return false;
        r+=char(c);
    }
    std::string key="Guz(%"+r.substr(0,1)+"&h7H$y"+r.substr(1,1)+"04~FtT5&"+r.substr(2,1)+"]@"+r.substr(3,1)+"6*/["+r.substr(4,1)+")7*/";
    std::string iv="E"+r.substr(0,1)+"r"+r.substr(1,1)+"%r"+r.substr(2,1)+"1$"+r.substr(3,1)+"U^#@"+r.substr(4,1)+"*";
    key.resize(32,' ');iv.resize(16,' ');
    std::vector<unsigned char> cipher,bytes;
    try {
        if(!base64(stored.substr(0,stored.size()-5),cipher) || !aes(cipher,key,iv,bytes)) {wipe(bytes);return false;}
        std::string utf8(bytes.begin(),bytes.end());
        // Encrypto writes UTF-8; StreamReader recognizes its optional UTF-8 BOM.
        if(utf8.compare(0,3,"\xef\xbb\xbf")==0)utf8.erase(0,3);
        if constexpr(sizeof(wchar_t)==2) {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
            plaintext=convert.from_bytes(utf8);
        } else {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
            plaintext=convert.from_bytes(utf8);
        }
        volatile char* data=utf8.data();for(std::size_t i=0;i<utf8.size();++i)data[i]=0;
        wipe(bytes);return true;
    } catch(...) {wipe(bytes);plaintext.clear();return false;}
}
bool password_matches(const std::wstring& stored,const std::wstring& input) {
    const auto normalized=trim(input);if(normalized.empty())return false;
    std::wstring plain;
    const bool ok=decrypt_legacy(stored,plain) && plain==normalized;
    volatile wchar_t* data=plain.data();for(std::size_t i=0;i<plain.size();++i)data[i]=0;
    return ok;
}
bool parse_lock_seconds(const std::wstring& value,int& seconds) {
    const auto s=trim(value);if(s.empty())return false;
    unsigned v=0;
    for(auto c:s) {if(c<L'0'||c>L'9')return false;unsigned n=unsigned(c-L'0');if(v>(unsigned(INT_MAX)-n)/10)return false;v=v*10+n;}
    seconds=int(v);return true;
}
const wchar_t* message(Status s) {
    switch(s) {
    case Status::success:return L"登录成功";
    case Status::invalid_credentials:return L"用户名或者密码不正确！";
    case Status::locked:return L"该登录名被锁定，不能登录系统！";
    case Status::temporary_locked:return L"该账号处于错误次数限时锁定期间，请稍后重试。";
    case Status::expired:return L"该账户已经过有效期，请切换其他账户登录。";
    default:return L"登录验证失败，请检查连接或联系管理员。";
    }
}
}
