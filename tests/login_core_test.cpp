#include "login_core.h"
#include "login_legacy_vectors.h"
#include <cstdlib>
#include <iostream>
void check(bool ok,const char* message) {if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    for(const auto& v:legacy_vectors) {
        std::wstring plain;
        check(login::decrypt_legacy(v.stored,plain),"independent .NET ciphertext decrypts");
        check(plain==v.decoded,"independent .NET plaintext matches, including BOM and non-BMP");
        if(!plain.empty() && login::trim(plain)==plain) {
            check(login::password_matches(v.stored,L"\u3000"+plain+L"\u00a0"),".NET whitespace trimmed at input");
            check(!login::password_matches(v.stored,plain+L"x"),"wrong password rejected");
        }
        check(!login::password_matches(v.stored,L" \u3000\t"),"empty input never accepted");
        auto broken=v.stored;broken[0]=L'!';
        check(!login::decrypt_legacy(broken,plain),"malformed base64 rejected");
    }
    check(login::trim(L"\u1680\u2007\u202f\u3000 A\u3000B \u0085")==L"A\u3000B","Unicode trim preserves internal whitespace");
    check(login::trim(L"\u200bA\ufeff")==L"\u200bA\ufeff","non-whitespace format characters preserved");
    check(login::valid_code(L"T123") && !login::valid_code(L"T'123") && !login::valid_code(L"T;123") && !login::valid_code(L"T--123"),"login name validation");
    const auto& ascii=legacy_vectors[3];
    check(!login::password_matches(ascii.stored,L"abc123!"),"password comparison remains case-sensitive");
    int seconds=-1;
    check(login::parse_lock_seconds(L" 60 ",seconds)&&seconds==60,"configured sixty seconds");
    check(login::parse_lock_seconds(L"0",seconds)&&seconds==0,"disabled lock interval");
    for(const auto& s:{L"",L"-1",L"abc",L"60x",L"2147483648"})check(!login::parse_lock_seconds(s,seconds),"bad config rejected");
    std::wstring plain=L"old";
    check(!login::decrypt_legacy(L"1234",plain)&&plain.empty(),"short corrupt storage rejected");
    std::cout<<legacy_vectors.size()<<" .NET legacy vectors and login policy tests passed\n";
}
