#include <windows.h>
#include <winhttp.h>
#include <urlmon.h>
#include <string>
#include <vector>
#include <random>
#include <filesystem>
#include <ctime>

namespace fs = std::filesystem;

int getRandomInt(int min, int max) {
    static std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<> distr(min, max);
    return distr(gen);
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n - 1);
    return w;
}

// HTTPS GET with optional extra headers, returns the body (empty on failure)
std::string httpGet(const std::string& host, const std::string& path, const std::string& headers) {
    std::string body;
    HINTERNET hSession = WinHttpOpen(L"WallpaperChanger/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return body;

    HINTERNET hConnect = WinHttpConnect(hSession, widen(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = hConnect ? WinHttpOpenRequest(hConnect, L"GET", widen(path).c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;

    std::wstring wHeaders = widen(headers);
    if (hRequest &&
        WinHttpSendRequest(hRequest,
            wHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wHeaders.c_str(),
            wHeaders.empty() ? 0 : (DWORD)-1L,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {

        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

        if (status == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0) break;
                body.append(chunk, 0, read);
            }
        }
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return body;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// First  "key":"value"  in the JSON, with JSON escapes undone
std::string extractFirst(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    std::string v = json.substr(pos, end - pos);
    v = replaceAll(v, "\\u0026", "&");
    v = replaceAll(v, "\\/", "/");
    return v;
}

struct Photo {
    std::string imageUrl;
    std::string downloadPath; // path on api.unsplash.com used to register the download
};

Photo fetchUnsplashPhoto(const std::string& accessKey) {
    std::string headers = "Authorization: Client-ID " + accessKey + "\r\nAccept-Version: v1\r\n";
    std::string json = httpGet("api.unsplash.com",
        "/photos/random?orientation=landscape&query=landscape+nature&content_filter=high",
        headers);

    Photo p;
    std::string raw = extractFirst(json, "raw");
    if (raw.empty()) return p;

    // "raw" is the original; ask Unsplash's image server for a 4K JPEG crop
    p.imageUrl = raw + "&w=3840&h=2160&fit=crop&fm=jpg&q=85";

    const std::string prefix = "https://api.unsplash.com";
    std::string dl = extractFirst(json, "download_location");
    if (dl.rfind(prefix, 0) == 0) p.downloadPath = dl.substr(prefix.size());
    return p;
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char userProfile[MAX_PATH];
    GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH);

    std::string folderPath = std::string(userProfile) + "\\Pictures\\SavedWallpapers";
    CreateDirectoryA(folderPath.c_str(), NULL);

    std::string savePath = folderPath + "\\wallpaper_" + std::to_string(std::time(nullptr)) + ".jpg";

    bool downloaded = false;
    char key[256];
    if (GetEnvironmentVariableA("UNSPLASH_ACCESS_KEY", key, sizeof(key)) > 0) {
        Photo photo = fetchUnsplashPhoto(key);
        if (!photo.imageUrl.empty()) {
            downloaded = (URLDownloadToFileA(NULL, photo.imageUrl.c_str(), savePath.c_str(), 0, NULL) == S_OK);
            if (!downloaded) {
                DeleteFileA(savePath.c_str());
            } else if (!photo.downloadPath.empty()) {
                // Unsplash guideline: register the download once the photo is actually used
                httpGet("api.unsplash.com", photo.downloadPath,
                        "Authorization: Client-ID " + std::string(key) + "\r\nAccept-Version: v1\r\n");
            }
        }
    }

    std::string finalWallpaperPath = savePath;

    // Offline fallback: reuse a previously saved wallpaper
    if (!downloaded) {
        std::vector<std::string> savedFiles;
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            auto ext = entry.path().extension();
            if (ext == ".jpg" || ext == ".png") savedFiles.push_back(entry.path().string());
        }
        if (savedFiles.empty()) return 0;
        finalWallpaperPath = savedFiles[getRandomInt(0, (int)savedFiles.size() - 1)];
    }

    SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, (PVOID)finalWallpaperPath.c_str(),
                          SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
    return 0;
}