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
    {L"Drag to turn, right-drag to move, wheel to zoom.",
     L"ドラッグで回転、右ドラッグで移動、ホイールで拡大と縮小。"},
    {L"Turn left 90°", L"左に 90°"},
    {L"Turn right 90°", L"右に 90°"},
    {L"Training camera", L"学習時のカメラ"},
    {L"Automatic", L"自動に戻す"},
    {L"Use this view", L"この視点をサムネイルにする"},
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
};

static_assert(std::size(kTexts) == static_cast<std::size_t>(Text::Usage) + 1);

bool japanese() noexcept {
    static const bool ja = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
    return ja;
}

} // namespace

const wchar_t* text(Text id) noexcept {
    const Entry& e = kTexts[static_cast<std::size_t>(id)];
    return japanese() ? e.ja : e.en;
}

} // namespace splatthumb::app
