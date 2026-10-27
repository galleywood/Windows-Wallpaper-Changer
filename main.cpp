#include <windows.h>
#include <winhttp.h>
#include <urlmon.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <random>
#include <filesystem>
#include <ctime>

// Ensure Shell32 and Ole32 are linked alongside Winhttp and Urlmon
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

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

// First "key":"value" in the JSON, with JSON escapes undone
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

// Pool of search queries
const std::vector<std::string> categories = {"cars", "anime", "hacking", "animal", "tech", "game", "linux"};

Photo fetchUnsplashPhoto(const std::string& accessKey) {
    std::string randomQuery = categories[getRandomInt(0, (int)categories.size() - 1)];

    std::string headers = "Authorization: Client-ID " + accessKey + "\r\nAccept-Version: v1\r\n";
    std::string json = httpGet("api.unsplash.com",
        "/photos/random?orientation=landscape&query=" + randomQuery,
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

// Dynamically resolves Pictures\SavedWallpapers even when redirected by OneDrive
fs::path getSavedWallpapersDir() {
    PWSTR pszPath = nullptr;
    fs::path folderPath;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pszPath))) {
        folderPath = pszPath;
        CoTaskMemFree(pszPath);
    } else {
        wchar_t userProfile[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
            folderPath = fs::path(userProfile) / L"Pictures";
        }
    }

    folderPath /= L"SavedWallpapers";

    std::error_code ec;
    fs::create_directories(folderPath, ec); // Creates parent folders if missing, never throws
    return folderPath;
}

// Checks that a file is physically present on disk and not an unhydrated OneDrive cloud stub
bool isFileAvailableOffline(const fs::path& p) {
    DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;

    const DWORD cloudStubFlags = FILE_ATTRIBUTE_OFFLINE |
                                 FILE_ATTRIBUTE_RECALL_ON_OPEN |
                                 FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
    return (attrs & cloudStubFlags) == 0;
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    fs::path folderPath = getSavedWallpapersDir();
    fs::path savePath = folderPath / (L"wallpaper_" + std::to_wstring(std::time(nullptr)) + L".jpg");

    bool downloaded = false;
    char key[256];
    if (GetEnvironmentVariableA("UNSPLASH_ACCESS_KEY", key, sizeof(key)) > 0) {
        Photo photo = fetchUnsplashPhoto(key);
        if (!photo.imageUrl.empty()) {
            std::wstring wUrl = widen(photo.imageUrl);
            downloaded = (URLDownloadToFileW(nullptr, wUrl.c_str(), savePath.c_str(), 0, nullptr) == S_OK);
            if (!downloaded) {
                DeleteFileW(savePath.c_str());
            } else if (!photo.downloadPath.empty()) {
                // Unsplash guideline: register the download once the photo is actually used
                httpGet("api.unsplash.com", photo.downloadPath,
                        "Authorization: Client-ID " + std::string(key) + "\r\nAccept-Version: v1\r\n");
            }
        }
    }

    fs::path finalWallpaperPath = savePath;

    // Offline fallback: reuse a previously saved local wallpaper
    if (!downloaded) {
        std::vector<fs::path> savedFiles;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folderPath, ec)) {
            if (entry.is_regular_file(ec)) {
                auto ext = entry.path().extension();
                if ((ext == L".jpg" || ext == L".png") && isFileAvailableOffline(entry.path())) {
                    savedFiles.push_back(entry.path());
                }
            }
        }
        if (savedFiles.empty()) return 0;
        finalWallpaperPath = savedFiles[getRandomInt(0, (int)savedFiles.size() - 1)];
    }

    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)finalWallpaperPath.c_str(),
                          SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
    return 0;
}