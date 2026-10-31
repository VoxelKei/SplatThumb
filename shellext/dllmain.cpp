#include <windows.h>

#include <objbase.h>

#include "class_factory.h"
#include "module.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* ppv) {
    return splatthumb::shellext::get_class_object(clsid, iid, ppv);
}

STDAPI DllCanUnloadNow() {
    return splatthumb::shellext::module_in_use() ? S_FALSE : S_OK;
}
