#include <windows.h>
#include <urlmon.h>
#include <string>
#include <ctime>
#include <filesystem>
#include <vector>
#include <random>

namespace fs = std::filesystem;

// Helper function to generate a random number between min and max
int getRandomInt(int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(min, max);
    return distr(gen);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    char userProfile[MAX_PATH];
    GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH);

    std::string folderPath = std::string(userProfile) + "\\Pictures\\SavedWallpapers";
    CreateDirectoryA(folderPath.c_str(), NULL);

    std::time_t currentTime = std::time(nullptr);
    std::string savePath = folderPath + "\\wallpaper_" + std::to_string(currentTime) + ".jpg";

    // 1. 4K API Pool with Bing Daily Wallpaper included
    std::vector<std::string> apis = {
        "https://picsum.photos/3840/2160",                       // Random 4K Photo
        "https://loremflickr.com/3840/2160/wallpaper",           // Random 4K Flickr tag
        "https://loremflickr.com/3840/2160/nature,landscape",    // Random 4K Nature
        "https://bing.biturl.top/?resolution=UHD&format=image&index=random&mkt=en-US" // Random Bing 4K
    };

    std::string selectedUrl = apis[getRandomInt(0, apis.size() - 1)];

    // 2. Attempt to download the image
    HRESULT hr = URLDownloadToFileA(NULL, selectedUrl.c_str(), savePath.c_str(), 0, NULL);

    std::string finalWallpaperPath = savePath;

    // 3. Offline Fallback Logic
    if (hr != S_OK) {
        std::vector<std::string> savedFiles;

        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.path().extension() == ".jpg" || entry.path().extension() == ".png") {
                savedFiles.push_back(entry.path().string());
            }
        }

        if (!savedFiles.empty()) {
            finalWallpaperPath = savedFiles[getRandomInt(0, savedFiles.size() - 1)];
        } else {
            return 0;
        }
    }

    // 4. Apply the chosen wallpaper
    SystemParametersInfoA(
        SPI_SETDESKWALLPAPER,
        0,
        (PVOID)finalWallpaperPath.c_str(),
        SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE
    );

    return 0;
}