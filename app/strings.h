#pragma once

namespace splatthumb::app {

// Every piece of text the app shows. Japanese when the Windows display language is
// Japanese, English otherwise.
enum class Text {
    AppName,
    ViewerTitle,
    Loading,
    Hint,
    RotateLeft,
    RotateRight,
    TrainingCamera,
    Automatic,
    Save,
    Close,
    Saved,
    ResetDone,
    NotSplat,
    CannotOpen,
    CannotLoad,
    CannotSave,
    Usage,
};

const wchar_t* text(Text id) noexcept;

} // namespace splatthumb::app
