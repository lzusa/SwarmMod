#include "../src/main.cpp"
#include <iostream>
int wmain(int argc,wchar_t **argv) {
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH,temp);
    debugPath=std::wstring(temp)+L"SwarmMod-curl-probe.log";
    if(argc==3) {
        configPath=argv[1];
        return publish(settings(),utf8(argv[2]))==200 ? 0 : 1;
    }
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) return 2;
    auto http=std::unique_ptr<CURL,decltype(&curl_easy_cleanup)>(curl_easy_init(),curl_easy_cleanup);
    if(!http) return 3;
    std::string trust=windowsRoots();
    if(trust.empty()) return 4;
    curl_blob roots{trust.data(),trust.size(),CURL_BLOB_COPY};
    const std::wstring endpoint=argc==2 ? argv[1] : L"https://konni.delthas.fr/games";
    const auto url=utf8(endpoint);
    const auto proxy=userProxy(endpoint);
    char error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(http.get(),CURLOPT_URL,url.c_str());
    curl_easy_setopt(http.get(),CURLOPT_NOBODY,1L);
    curl_easy_setopt(http.get(),CURLOPT_CAINFO_BLOB,&roots);
    curl_easy_setopt(http.get(),CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(http.get(),CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(http.get(),CURLOPT_TIMEOUT_MS,20000L);
    curl_easy_setopt(http.get(),CURLOPT_ERRORBUFFER,error);
    if(!proxy.proxy.empty()) curl_easy_setopt(http.get(),CURLOPT_PROXY,proxy.proxy.c_str());
    curl_easy_setopt(http.get(),CURLOPT_NOPROXY,"localhost,127.0.0.1,::1");
    const auto result=curl_easy_perform(http.get());
    long status=0;
    curl_easy_getinfo(http.get(),CURLINFO_RESPONSE_CODE,&status);
    std::cout<<curl_version()<<"\ntrusted roots bytes="<<trust.size()<<" result="<<result<<" HTTP="<<status<<" error="<<error<<"\n";
    return result==CURLE_OK ? 0 : 1;
}
