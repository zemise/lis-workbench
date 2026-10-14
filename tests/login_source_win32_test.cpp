#include "login_source_win32.h"
#include <iostream>
int main(int argc,char**){
    if(argc==1){
        auto s=login::scan_sources(L"Z:\\NoSuchInstallationForSourceTest",[]{return false;});
        if(s.complete||!s.sources.empty())return 1;
        std::cout<<"Unverified installation cannot produce source identity\n";return 0;
    }
    auto s=login::scan_sources(L"C:\\LIS\\创星医疗管理系统（标准）",[]{return false;});
    std::cout<<"{\"complete\":"<<(s.complete?"true":"false")<<",\"mains\":"<<s.mains.size()<<",\"sources\":"<<s.sources.size()<<"}\n";
    return s.complete?0:1;
}
