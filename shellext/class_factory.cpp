#include "class_factory.h"

#include <new>

#include "clsid.h"
#include "module.h"
#include "thumbnail_provider.h"

namespace splatthumb::shellext {

HRESULT ClassFactory::create(REFIID iid, void** ppv) noexcept {
    if (ppv == nullptr) {
        return E_POINTER;
    }
    *ppv = nullptr;
    auto* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = factory->QueryInterface(iid, ppv);
    factory->Release();
    return hr;
}

ClassFactory::ClassFactory() noexcept {
    module_add_ref();
}

ClassFactory::~ClassFactory() {
    module_release();
}

IFACEMETHODIMP ClassFactory::QueryInterface(REFIID iid, void** ppv) noexcept {
    if (ppv == nullptr) {
        return E_POINTER;
    }
    if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(IClassFactory))) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ClassFactory::AddRef() noexcept {
    return refs_.fetch_add(1, std::memory_order_relaxed) + 1;
}

IFACEMETHODIMP_(ULONG) ClassFactory::Release() noexcept {
    const ULONG left = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (left == 0) {
        delete this;
    }
    return left;
}

IFACEMETHODIMP ClassFactory::CreateInstance(IUnknown* outer, REFIID iid, void** ppv) noexcept {
    if (ppv == nullptr) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (outer != nullptr) {
        return CLASS_E_NOAGGREGATION;
    }
    return ThumbnailProvider::create(iid, ppv);
}

IFACEMETHODIMP ClassFactory::LockServer(BOOL lock) noexcept {
    if (lock) {
        module_add_ref();
    } else {
        module_release();
    }
    return S_OK;
}

HRESULT get_class_object(REFCLSID clsid, REFIID iid, void** ppv) noexcept {
    if (ppv == nullptr) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, kThumbnailProviderClsid)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return ClassFactory::create(iid, ppv);
}

} // namespace splatthumb::shellext
