#include <windows.h>
#include <urlmon.h>
#include <string>
#include <ctime>

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Get the user's home directory (e.g., C:\Users\GALLEY)
    char userProfile[MAX_PATH];
    GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH);

    // 2. Create a dedicated folder in the Pictures directory
    std::string folderPath = std::string(userProfile) + "\\Pictures\\SavedWallpapers";
    CreateDirectoryA(folderPath.c_str(), NULL); // Silently fails if the folder already exists, which is perfect

    // 3. Generate a unique filename using the current Unix timestamp
    std::time_t currentTime = std::time(nullptr);
    std::string savePath = folderPath + "\\wallpaper_" + std::to_string(currentTime) + ".jpg";

    // 4. Download the file silently
    const char* url = "https://picsum.photos/4096/2160";
    HRESULT hr = URLDownloadToFileA(NULL, url, savePath.c_str(), 0, NULL);

    // 5. If successful, set it as the desktop wallpaper
    if (hr == S_OK) {
        SystemParametersInfoA(
            SPI_SETDESKWALLPAPER,
            0,
            (PVOID)savePath.c_str(),
            SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE
        );
    }

    return 0;
}