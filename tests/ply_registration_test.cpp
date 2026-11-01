#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

#include "ply_registration.h"

namespace {

using namespace splatthumb::installer;

// A fresh key under HKCU\Software\SplatThumbTests that stands in for HKEY_CURRENT_USER,
// deleted again after each test.
class PlyRegistrationTest : public ::testing::Test {
  protected:
    void SetUp() override {
        path_ = L"Software\\SplatThumbTests\\" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64());
        ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, path_.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                                  nullptr, &root_, nullptr),
                  ERROR_SUCCESS);
    }

    void TearDown() override {
        if (root_ != nullptr) {
            RegCloseKey(root_);
        }
        RegDeleteTreeW(HKEY_CURRENT_USER, path_.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\SplatThumbTests"); // only if empty
    }

    HKEY create(const wchar_t* path) {
        HKEY key = nullptr;
        EXPECT_EQ(
            RegCreateKeyExW(root_, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr),
            ERROR_SUCCESS);
        return key;
    }

    void set_string(const wchar_t* path, const wchar_t* name, const std::wstring& value) {
        HKEY key = create(path);
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }

    void set_raw(const wchar_t* path, const wchar_t* name, DWORD type,
                 const std::vector<BYTE>& data) {
        HKEY key = create(path);
        RegSetValueExW(key, name, 0, type, data.data(), static_cast<DWORD>(data.size()));
        RegCloseKey(key);
    }

    bool exists(const wchar_t* path) const {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root_, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
            return false;
        }
        RegCloseKey(key);
        return true;
    }

    std::wstring handler() const {
        wchar_t value[128] = {};
        DWORD size = sizeof value - sizeof(wchar_t);
        if (RegGetValueW(root_, kHandlerKey, nullptr, RRF_RT_REG_SZ, nullptr, value, &size) !=
            ERROR_SUCCESS) {
            return L"(none)";
        }
        return value;
    }

    // Everything under root, as text: key paths, value names, types and bytes.
    std::wstring dump() const {
        return dump_key(root_, L"");
    }

    static std::wstring dump_key(HKEY key, const std::wstring& path) {
        std::wstring out = L"[" + path + L"]\n";
        for (DWORD i = 0;; ++i) {
            wchar_t name[256];
            DWORD name_size = 256;
            BYTE data[1024];
            DWORD data_size = sizeof data;
            DWORD type = 0;
            if (RegEnumValueW(key, i, name, &name_size, nullptr, &type, data, &data_size) !=
                ERROR_SUCCESS) {
                break;
            }
            out += std::wstring(name) + L"|" + std::to_wstring(type) + L"|";
            for (DWORD b = 0; b < data_size; ++b) {
                out += std::to_wstring(data[b]) + L",";
            }
            out += L"\n";
        }
        for (DWORD i = 0;; ++i) {
            wchar_t name[256];
            DWORD name_size = 256;
            if (RegEnumKeyExW(key, i, name, &name_size, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS) {
                break;
            }
            HKEY sub = nullptr;
            if (RegOpenKeyExW(key, name, 0, KEY_READ, &sub) == ERROR_SUCCESS) {
                out += dump_key(sub, path + L"\\" + name);
                RegCloseKey(sub);
            }
        }
        return out;
    }

    std::wstring path_;
    HKEY root_ = nullptr;
};

TEST_F(PlyRegistrationTest, NothingBeforeLeavesNothingAfter) {
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS);
    EXPECT_EQ(handler(), kClsid);
    EXPECT_TRUE(exists(kStateKey));
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_FALSE(exists(kPlyKey));
    EXPECT_FALSE(exists(kStateKey));
    EXPECT_EQ(dump().find(L".ply"), std::wstring::npos);
}

TEST_F(PlyRegistrationTest, KeepsAnEmptyPlyKeyThatExistedBefore) {
    RegCloseKey(create(kPlyKey));
    const std::wstring before = dump();
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS);
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_TRUE(exists(kPlyKey));
    EXPECT_FALSE(exists(kShellExKey));
    // The rest of the test root (Software\SplatThumb) is the MSI's to remove.
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);
}

TEST_F(PlyRegistrationTest, KeepsOtherValuesOfThePlyKey) {
    set_string(kPlyKey, nullptr, L"SomeApp.ply");
    set_string(kPlyKey, L"Content Type", L"model/ply");
    const std::wstring before = dump();
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS);
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);
}

TEST_F(PlyRegistrationTest, RestoresAnotherHandlerExactly) {
    set_string(kHandlerKey, nullptr, L"{11111111-2222-3333-4444-555555555555}");
    set_raw(kHandlerKey, L"Number", REG_DWORD, {5, 0, 0, 0});
    set_raw(kHandlerKey, L"Bytes", REG_BINARY, {1, 2, 3});
    set_raw(kHandlerKey, L"Lines", REG_MULTI_SZ, {'a', 0, 0, 0, 'b', 0, 0, 0, 0, 0});
    set_string(L"Software\\Classes\\.ply\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}\\Sub",
               L"x", L"y");
    const std::wstring before = dump();

    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS);
    EXPECT_EQ(handler(), kClsid);
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_EQ(handler(), L"{11111111-2222-3333-4444-555555555555}");
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);
}

TEST_F(PlyRegistrationTest, RegisteringAgainKeepsTheFirstSavedState) {
    set_string(kHandlerKey, nullptr, L"{11111111-2222-3333-4444-555555555555}");
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS);
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS); // a repair must not save our own value
    EXPECT_EQ(handler(), kClsid);
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_EQ(handler(), L"{11111111-2222-3333-4444-555555555555}");
}

TEST_F(PlyRegistrationTest, LeavesAHandlerThatAnotherAppSetLater) {
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS);
    set_string(kHandlerKey, nullptr, L"{66666666-7777-8888-9999-000000000000}");
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_EQ(handler(), L"{66666666-7777-8888-9999-000000000000}");
    EXPECT_FALSE(exists(kStateKey));
}

TEST_F(PlyRegistrationTest, RestoreWithoutSavedStateChangesNothing) {
    set_string(kHandlerKey, nullptr, kClsid);
    const std::wstring before = dump();
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_EQ(dump(), before);
}

TEST_F(PlyRegistrationTest, InterruptedStateIsDiscarded) {
    set_string(kHandlerKey, nullptr, L"{11111111-2222-3333-4444-555555555555}");
    set_raw(kStateKey, L"PlyExisted", REG_DWORD, {0, 0, 0, 0}); // no "Complete"
    const std::wstring handler_before = handler();

    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_FALSE(exists(kStateKey));
    EXPECT_EQ(handler(), handler_before);

    set_raw(kStateKey, L"PlyExisted", REG_DWORD, {0, 0, 0, 0});
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS); // saves afresh
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_EQ(handler(), handler_before);
    EXPECT_TRUE(exists(kPlyKey)); // it existed before this register
}

constexpr const wchar_t* kExe =
    L"C:\\Users\\Test User\\AppData\\Local\\Programs\\SplatThumb\\SplatThumb.exe";

TEST_F(PlyRegistrationTest, AddsAndRemovesTheRightClickItem) {
    RegCloseKey(create(L"Software\\Classes")); // always there in a real HKEY_CURRENT_USER
    const std::wstring before = dump();
    ASSERT_EQ(register_ply(root_, kExe), ERROR_SUCCESS);

    wchar_t value[512] = {};
    DWORD size = sizeof value - sizeof(wchar_t);
    ASSERT_EQ(RegGetValueW(root_, kVerbCommandKey, nullptr, RRF_RT_REG_SZ, nullptr, value, &size),
              ERROR_SUCCESS);
    EXPECT_EQ(std::wstring(value), L"\"" + std::wstring(kExe) + L"\" --view \"%1\"");
    size = sizeof value - sizeof(wchar_t);
    ASSERT_EQ(RegGetValueW(root_, kVerbKey, L"MUIVerb", RRF_RT_REG_SZ, nullptr, value, &size),
              ERROR_SUCCESS);
    EXPECT_EQ(std::wstring(value), L"@" + std::wstring(kExe) + L",-102");
    size = sizeof value - sizeof(wchar_t);
    ASSERT_EQ(RegGetValueW(root_, kVerbKey, L"Icon", RRF_RT_REG_SZ, nullptr, value, &size),
              ERROR_SUCCESS);
    EXPECT_EQ(std::wstring(value), std::wstring(kExe) + L",-1");

    ASSERT_EQ(register_ply(root_, kExe), ERROR_SUCCESS); // repair
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_FALSE(exists(kAssociationsKey));
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);
}

TEST_F(PlyRegistrationTest, KeepsOtherRightClickItemsAndEmptyParents) {
    set_string(L"Software\\Classes\\SystemFileAssociations\\.ply\\shell\\Other\\command", nullptr,
               L"other.exe \"%1\"");
    RegCloseKey(create(L"Software\\Classes\\SystemFileAssociations\\.jpg"));
    const std::wstring before = dump();
    ASSERT_EQ(register_ply(root_, kExe), ERROR_SUCCESS);
    EXPECT_TRUE(exists(kVerbKey));
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);

    RegDeleteTreeW(root_, L"Software\\Classes\\SystemFileAssociations\\.ply\\shell\\Other");
    const std::wstring empty_shell = dump(); // an empty shell key that existed before
    ASSERT_EQ(register_ply(root_, kExe), ERROR_SUCCESS);
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_TRUE(exists(kAssociationsShellKey));
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), empty_shell);
}

TEST_F(PlyRegistrationTest, RepairOfAnOlderStateRecordsTheItemsParents) {
    // A state saved by v0.1: no values for the right-click item's parents.
    set_raw(kStateKey, L"PlyExisted", REG_DWORD, {0, 0, 0, 0});
    set_raw(kStateKey, L"ShellExExisted", REG_DWORD, {0, 0, 0, 0});
    set_raw(kStateKey, L"Complete", REG_DWORD, {1, 0, 0, 0});
    set_string(kHandlerKey, nullptr, kClsid);
    ASSERT_EQ(register_ply(root_, kExe), ERROR_SUCCESS);
    EXPECT_TRUE(exists(kVerbKey));
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_FALSE(exists(kAssociationsKey));
    EXPECT_FALSE(exists(kPlyKey));
}

TEST_F(PlyRegistrationTest, RestoreOfAnOlderStateKeepsTheParents) {
    set_raw(kStateKey, L"PlyExisted", REG_DWORD, {0, 0, 0, 0});
    set_raw(kStateKey, L"ShellExExisted", REG_DWORD, {0, 0, 0, 0});
    set_raw(kStateKey, L"Complete", REG_DWORD, {1, 0, 0, 0});
    set_string(kHandlerKey, nullptr, kClsid);
    RegCloseKey(create(kAssociationsShellKey));
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    EXPECT_TRUE(exists(kAssociationsShellKey)); // not recorded, so not ours to remove
    EXPECT_FALSE(exists(kStateKey));
}

TEST_F(PlyRegistrationTest, RemovesUserSettingsAndTheEmptyParent) {
    set_raw(kUserSettingsKey, L"Background", REG_DWORD, {1, 0, 0, 0});
    ASSERT_EQ(remove_user_settings(root_), ERROR_SUCCESS);
    EXPECT_FALSE(exists(kUserKey));
    ASSERT_EQ(remove_user_settings(root_), ERROR_SUCCESS); // nothing left to do

    set_raw(kUserSettingsKey, L"Quality", REG_DWORD, {2, 0, 0, 0});
    set_raw(kUserKey, L"Other", REG_DWORD, {1, 0, 0, 0});
    ASSERT_EQ(remove_user_settings(root_), ERROR_SUCCESS);
    EXPECT_FALSE(exists(kUserSettingsKey));
    EXPECT_TRUE(exists(kUserKey)); // not empty, so left for its owner
}

// A second extension, registered without the right-click item, with its own saved state.
constexpr Extension kOther{L".spgmtest", L"Software\\SplatThumb\\Previous.spgmtest", false};
constexpr const wchar_t* kOtherHandlerKey =
    L"Software\\Classes\\.spgmtest\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}";

TEST_F(PlyRegistrationTest, SecondExtensionGetsTheHandlerButNoRightClickItem) {
    RegCloseKey(create(L"Software\\Classes"));
    const std::wstring before = dump();
    ASSERT_EQ(register_ply(root_, kExe), ERROR_SUCCESS);
    ASSERT_EQ(register_extension(root_, kOther, kExe), ERROR_SUCCESS);

    wchar_t value[128] = {};
    DWORD size = sizeof value - sizeof(wchar_t);
    ASSERT_EQ(RegGetValueW(root_, kOtherHandlerKey, nullptr, RRF_RT_REG_SZ, nullptr, value, &size),
              ERROR_SUCCESS);
    EXPECT_EQ(std::wstring(value), kClsid);
    EXPECT_FALSE(exists(L"Software\\Classes\\SystemFileAssociations\\.spgmtest"));
    EXPECT_TRUE(exists(kVerbKey));

    ASSERT_EQ(restore_extension(root_, kOther), ERROR_SUCCESS);
    EXPECT_FALSE(exists(L"Software\\Classes\\.spgmtest"));
    EXPECT_EQ(handler(), kClsid); // .ply is untouched
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS);
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);
}

TEST_F(PlyRegistrationTest, EachExtensionRestoresItsOwnHandlerInAnyOrder) {
    set_string(kHandlerKey, nullptr, L"{11111111-2222-3333-4444-555555555555}");
    set_string(kOtherHandlerKey, nullptr, L"{AAAAAAAA-2222-3333-4444-555555555555}");
    set_raw(kOtherHandlerKey, L"Number", REG_DWORD, {7, 0, 0, 0});
    set_string(L"Software\\Classes\\.spgmtest", nullptr, L"SomeApp.Project");
    const std::wstring before = dump();

    ASSERT_EQ(register_extension(root_, kOther), ERROR_SUCCESS);
    ASSERT_EQ(register_ply(root_), ERROR_SUCCESS);
    ASSERT_EQ(restore_ply(root_), ERROR_SUCCESS); // .ply first this time
    ASSERT_EQ(restore_extension(root_, kOther), ERROR_SUCCESS);
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);
}

TEST_F(PlyRegistrationTest, RegisterAllAndRestoreAllRoundTrip) {
    RegCloseKey(create(L"Software\\Classes"));
    set_string(kHandlerKey, nullptr, L"{11111111-2222-3333-4444-555555555555}");
    const std::wstring before = dump();
    ASSERT_EQ(register_all(root_, kExe), ERROR_SUCCESS);
    for (const Extension& ext : kExtensions) {
        const std::wstring key = std::wstring(L"Software\\Classes\\") + ext.name +
                                 L"\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}";
        wchar_t value[128] = {};
        DWORD size = sizeof value - sizeof(wchar_t);
        ASSERT_EQ(RegGetValueW(root_, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &size),
                  ERROR_SUCCESS);
        EXPECT_EQ(std::wstring(value), kClsid) << ext.name;
    }
    ASSERT_EQ(register_all(root_, kExe), ERROR_SUCCESS); // repair
    ASSERT_EQ(restore_all(root_), ERROR_SUCCESS);
    RegDeleteKeyW(root_, L"Software\\SplatThumb");
    EXPECT_EQ(dump(), before);
}

TEST(PlyRegistration, ExtensionListStartsWithPlyAndUsesDistinctStateKeys) {
    ASSERT_GE(std::size(kExtensions), 1u);
    EXPECT_STREQ(kExtensions[0].name, L".ply");
    EXPECT_STREQ(kExtensions[0].state_key, kStateKey);
    for (std::size_t i = 0; i < std::size(kExtensions); ++i) {
        for (std::size_t j = i + 1; j < std::size(kExtensions); ++j) {
            EXPECT_STRNE(kExtensions[i].name, kExtensions[j].name);
            EXPECT_STRNE(kExtensions[i].state_key, kExtensions[j].state_key);
        }
    }
}

TEST(PlyRegistration, SpgmIsRegisteredWithoutTheRightClickItem) {
    bool found = false;
    for (const Extension& ext : kExtensions) {
        if (std::wstring(ext.name) == L".spgm") {
            found = true;
            EXPECT_FALSE(ext.view_item);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(PlyRegistrationTest, RegisterAllAddsNoSpgmRightClickItem) {
    ASSERT_EQ(register_all(root_, kExe), ERROR_SUCCESS);
    HKEY key = nullptr;
    EXPECT_NE(RegOpenKeyExW(root_, L"Software\\Classes\\SystemFileAssociations\\.spgm", 0,
                            KEY_READ, &key),
              ERROR_SUCCESS);
    if (key != nullptr) {
        RegCloseKey(key);
    }
    ASSERT_EQ(restore_all(root_), ERROR_SUCCESS);
}

} // namespace
