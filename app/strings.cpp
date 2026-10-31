#include "strings.h"

#include <windows.h>

#include <cstddef>
#include <iterator>

namespace splatthumb::app {

namespace {

struct Entry {
    const wchar_t* en;
    const wchar_t* ja;
};

// Indexed by Text.
constexpr Entry kTexts[] = {
    {L"SplatThumb", L"SplatThumb"},
    {L"SplatThumb - Thumbnail view", L"SplatThumb - サムネイルの視点"},
    {L"Loading...", L"読み込み中..."},
    {L"Drag to turn, right-drag to move, Ctrl+drag to tilt, wheel to zoom.",
     L"ドラッグで回転、右ドラッグで移動、Ctrl+ドラッグで傾き、ホイールで拡大と縮小。"},
    {L"Reset", L"リセット"},
    {L"Use this view", L"この視点をサムネイルにする"},
    {L"Settings...", L"全体の設定..."},
    {L"Close", L"閉じる"},
    {L"Saved. The thumbnail has been updated.", L"保存しました。サムネイルを作り直しました。"},
    {L"Back to the automatic view. The thumbnail has been updated.",
     L"自動の視点に戻しました。サムネイルを作り直しました。"},
    {L"This file is not a 3D Gaussian Splatting PLY, so SplatThumb does not draw it.",
     L"このファイルは 3D Gaussian Splatting の PLY ではないため、SplatThumb は描きません。"},
    {L"The file could not be opened.", L"ファイルを開けませんでした。"},
    {L"The file could not be read, or it has nothing to draw.",
     L"ファイルを読み込めないか、描くものがありません。"},
    {L"The view could not be saved.", L"視点を保存できませんでした。"},
    {L"Right-click a .ply file in Explorer and choose \"SplatThumb: set thumbnail view\".",
     L"エクスプローラーで .ply ファイルを右クリックし、「SplatThumb で視点を設定」を選んでください。"},
    {L"SplatThumb - Settings", L"SplatThumb - 設定"},
    {L"Show SplatThumb thumbnails for 3DGS PLY files",
     L"3DGS の PLY ファイルに SplatThumb のサムネイルを表示する"},
    {L"Quality", L"品質"},
    {L"Fast (up to 50,000 splats)", L"速さ優先(最大 5 万個)"},
    {L"Standard (up to 200,000 splats)", L"標準(最大 20 万個)"},
    {L"High (up to 400,000 splats)", L"高品質(最大 40 万個)"},
    {L"Changes are saved right away and apply to thumbnails made from now on. To update a "
     L"folder that is already shown, use \"Remake thumbnails in a folder\" below.",
     L"変更はすぐに保存され、これから作られるサムネイルに反映されます。表示済みのフォルダーは、"
     L"下の「フォルダーのサムネイルを作り直す」で更新できます。"},
    {L"Settings saved.", L"設定を保存しました。"},
    {L"The settings could not be saved.", L"設定を保存できませんでした。"},
    {L"Remake thumbnails in a folder...", L"フォルダーのサムネイルを作り直す..."},
    {L"Stop", L"中止"},
    {L"Remaking thumbnails...", L"サムネイルを作り直しています..."},
    {L"Remade the thumbnails of {n} files.", L"{n} 個のファイルのサムネイルを作り直しました。"},
    {L"Stopped.", L"中止しました。"},
    {L"There are no .ply files in this folder.", L"このフォルダーには .ply ファイルがありません。"},
};

static_assert(std::size(kTexts) == static_cast<std::size_t>(Text::NoPlyFiles) + 1);

bool japanese() noexcept {
    static const bool ja = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
    return ja;
}

} // namespace

const wchar_t* text(Text id) noexcept {
    const Entry& e = kTexts[static_cast<std::size_t>(id)];
    return japanese() ? e.ja : e.en;
}

std::wstring text(Text id, unsigned long long n) {
    std::wstring s = text(id);
    const std::wstring value = std::to_wstring(n);
    for (std::size_t at = s.find(L"{n}"); at != std::wstring::npos; at = s.find(L"{n}", at)) {
        s.replace(at, 3, value);
        at += value.size();
    }
    return s;
}

} // namespace splatthumb::app
