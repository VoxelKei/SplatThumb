#pragma once

#include <windows.h>

#include <unknwn.h>

#include <atomic>

namespace splatthumb::shellext {

// Creates ThumbnailProvider objects.
class ClassFactory final : public IClassFactory {
public:
    static HRESULT create(REFIID iid, void** ppv) noexcept;

    IFACEMETHODIMP QueryInterface(REFIID iid, void** ppv) noexcept override;
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override;
    IFACEMETHODIMP_(ULONG) Release() noexcept override;

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** ppv) noexcept override;
    IFACEMETHODIMP LockServer(BOOL lock) noexcept override;

private:
    ClassFactory() noexcept;
    ~ClassFactory();
    ClassFactory(const ClassFactory&) = delete;
    ClassFactory& operator=(const ClassFactory&) = delete;

    std::atomic<ULONG> refs_{1};
};

// What DllGetClassObject does, without the export, so tests can call it.
HRESULT get_class_object(REFCLSID clsid, REFIID iid, void** ppv) noexcept;

} // namespace splatthumb::shellext
