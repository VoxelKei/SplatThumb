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
    TrainingCamera,
    Automatic,
    Save,
    OpenSettings,
    Close,
    Saved,
    ResetDone,
    NotSplat,
    CannotOpen,
    CannotLoad,
    CannotSave,
    Usage,
    SettingsTitle,
    EnableThumbnails,
    BackgroundLabel,
    BackgroundGray,
    BackgroundTransparent,
    BackgroundWhite,
    BackgroundBlack,
    QualityLabel,
    QualityFast,
    QualityStandard,
    QualityHigh,
    SettingsNote,
    SaveSettings,
    SettingsSaved,
    CannotSaveSettings,
    SavedViews,
    DeleteViews,
    ConfirmDeleteViews,
    ViewsDeleted,
    UseTrainingCameras,
    RemakeFolder,
    Stop,
    Remaking,
    Remade,
    Stopped,
    NoPlyFiles,
};

const wchar_t* text(Text id) noexcept;

// text(id) with every "{n}" replaced by n.
std::wstring text(Text id, unsigned long long n);

} // namespace splatthumb::app
