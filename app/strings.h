#pragma once

#include <string>

namespace splatthumb::app {

// Every piece of text the app shows. Japanese when the Windows display language is
// Japanese, English otherwise.
enum class Text {
    AppName,
    ViewerTitle,
    Loading,
    Hint,
    Automatic,
    Save,
    OpenSettings,
    Close,
    ResetDone,
    NotSplat,
    CannotOpen,
    CannotLoad,
    CannotSave,
    Usage,
    SettingsTitle,
    EnableThumbnails,
    QualityLabel,
    QualityFast,
    QualityStandard,
    QualityHigh,
    SettingsNote,
    SettingsSaved,
    CannotSaveSettings,
    RemakeAll,
    RemakeAllNote,
    Searching,
    Stop,
    Remaking,
    Remade,
    Stopped,
    NothingToRemake,
    QualityFollowSettings,
    TotalSplats,
    QualityChanged,
};

const wchar_t* text(Text id) noexcept;

// text(id) with every "{n}" replaced by n.
std::wstring text(Text id, unsigned long long n);

} // namespace splatthumb::app
