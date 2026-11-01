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
    {L"Right-click a .ply file in Explorer and choose \"Set view with SplatThumb\".",
     L"エクスプローラーで .ply ファイルを右クリックし、「SplatThumb で視点を設定」を選んでください。"},
    {L"SplatThumb - Settings", L"SplatThumb - 設定"},
    {L"Show SplatThumb thumbnails", L"SplatThumb のサムネイルを表示する"},
    {L"Quality", L"品質"},
    {L"Fast (up to 50,000 splats)", L"速さ優先(最大 5 万個)"},
    {L"Standard (up to 200,000 splats)", L"標準(最大 20 万個)"},
    {L"High (up to 400,000 splats)", L"高品質(最大 40 万個)"},
    {L"Changes are saved right away and apply to thumbnails made from now on. To update a "
     L"folder that is already shown, use \"Remake all existing thumbnails\" below.",
     L"変更はすぐに保存され、これから作られるサムネイルに反映されます。表示済みのフォルダーは、"
     L"下の「既存のサムネイルを全て再生成する」で更新できます。"},
    {L"Settings saved.", L"設定を保存しました。"},
    {L"The settings could not be saved.", L"設定を保存できませんでした。"},
    {L"Remake all existing thumbnails...", L"既存のサムネイルを全て再生成する..."},
    {L"Looks for .ply and .spgm files in the folder you choose and all folders inside it, and "
     L"remakes those that already have a thumbnail. Choose a drive such as K:\\ to cover all of it.",
     L"選んだフォルダーとその中のすべてのフォルダーから .ply と .spgm のファイルを探し、すでにサムネイルが"
     L"あるものを作り直します。K:\\ などのドライブを選ぶと、その全体が対象になります。"},
    {L"Looking for files... {n} found", L"ファイルを探しています... {n} 個見つかりました"},
    {L"Stop", L"中止"},
    {L"Remaking thumbnails...", L"サムネイルを作り直しています..."},
    {L"Remade the thumbnails of {n} files.", L"{n} 個のファイルのサムネイルを作り直しました。"},
    {L"Stopped.", L"中止しました。"},
    {L"No thumbnails to remake were found.", L"作り直すサムネイルは見つかりませんでした。"},
    {L"Quality of this file", L"このファイルの品質"},
    {L"Same as Settings: ", L"全体の設定に従う: "},
    {L"Quality changed. Click \"Use this view\" to use it for the thumbnail.",
     L"品質を変えました。「この視点をサムネイルにする」を押すとサムネイルに反映されます。"},
};

static_assert(std::size(kTexts) == static_cast<std::size_t>(Text::QualityChanged) + 1);

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
