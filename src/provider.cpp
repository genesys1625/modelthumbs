// provider.cpp - Windows Gezgini (Explorer) küçük resim sağlayıcısı: STL ve 3MF.
// IInitializeWithStream + IThumbnailProvider COM nesnesi. Asıl iş core.hpp'de.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <propsys.h>
#include <shlobj.h>
#include <thumbcache.h>

#include <new>

#include "core.hpp"

// {7E3C5A1D-4B2F-4C9E-9A6B-2F3D8E1A4C70}
static const CLSID CLSID_ModelThumb = {0x7e3c5a1d, 0x4b2f, 0x4c9e, {0x9a, 0x6b, 0x2f, 0x3d, 0x8e, 0x1a, 0x4c, 0x70}};
static const wchar_t* kClsidStr = L"{7E3C5A1D-4B2F-4C9E-9A6B-2F3D8E1A4C70}";
// IThumbnailProvider {E357FCCD-A995-4576-B01F-234630154E96}
static const IID kIID_IThumbnailProvider = {0xe357fccd, 0xa995, 0x4576, {0xb0, 0x1f, 0x23, 0x46, 0x30, 0x15, 0x4e, 0x96}};
// IInitializeWithStream {B824B49D-22AC-4161-AC8A-9916E8FA3F7F}
static const IID kIID_IInitializeWithStream = {0xb824b49d, 0x22ac, 0x4161, {0xac, 0x8a, 0x99, 0x16, 0xe8, 0xfa, 0x3f, 0x7f}};
static const wchar_t* kThumbHandlerKey = L"{E357FCCD-A995-4576-B01F-234630154E96}";

#ifndef SELFREG_E_CLASS
#define SELFREG_E_CLASS ((HRESULT)0x80040201L)  // olectl.h
#endif

static HINSTANCE g_hInst = nullptr;
static volatile LONG g_objects = 0;
static volatile LONG g_locks = 0;

static const size_t kMaxFileSize = (size_t)900 << 20;  // 900 MB üstünü okuma

// ------------------------------------------------------------------ küçük resim nesnesi
class Provider : public IInitializeWithStream, public IThumbnailProvider {
    LONG ref_ = 1;
    std::vector<uint8_t> data_;
    bool inited_ = false;

   public:
    Provider() { InterlockedIncrement(&g_objects); }
    ~Provider() { InterlockedDecrement(&g_objects); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_IInitializeWithStream))
            *ppv = static_cast<IInitializeWithStream*>(this);
        else if (IsEqualIID(riid, kIID_IThumbnailProvider))
            *ppv = static_cast<IThumbnailProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = (ULONG)InterlockedDecrement(&ref_);
        if (!c) delete this;
        return c;
    }

    // IInitializeWithStream
    STDMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (!stream) return E_INVALIDARG;
        if (inited_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        try {
            data_.clear();
            STATSTG st = {};
            if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart > 0) {
                if (st.cbSize.QuadPart > (ULONGLONG)kMaxFileSize) return E_OUTOFMEMORY;
                data_.reserve((size_t)st.cbSize.QuadPart);
            }
            LARGE_INTEGER zero = {};
            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
            std::vector<uint8_t> chunk(1 << 20);
            for (;;) {
                ULONG got = 0;
                HRESULT hr = stream->Read(chunk.data(), (ULONG)chunk.size(), &got);
                if (FAILED(hr)) return hr;
                if (got == 0) break;
                if (data_.size() + got > kMaxFileSize) return E_OUTOFMEMORY;
                data_.insert(data_.end(), chunk.begin(), chunk.begin() + got);
            }
        } catch (...) {
            return E_OUTOFMEMORY;
        }
        inited_ = true;
        return S_OK;
    }

    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_UNKNOWN;
        if (!inited_ || data_.empty()) return E_UNEXPECTED;
        try {
            mt::Image img;
            if (!mt::make_thumbnail(data_.data(), data_.size(), (int)cx, img)) return E_FAIL;
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = img.w;
            bi.bmiHeader.biHeight = -img.h;  // yukarıdan aşağıya
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP h = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!h || !bits) return E_OUTOFMEMORY;
            memcpy(bits, img.bgra.data(), img.bgra.size());
            *phbmp = h;
            *pdwAlpha = WTSAT_RGB;  // opak, beyaz zeminli
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }
};

// ------------------------------------------------------------------ sınıf fabrikası
class Factory : public IClassFactory {
    LONG ref_ = 1;

   public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = (ULONG)InterlockedDecrement(&ref_);
        if (!c) delete this;
        return c;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        Provider* p = new (std::nothrow) Provider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_locks);
        else InterlockedDecrement(&g_locks);
        return S_OK;
    }
};

// ------------------------------------------------------------------ kayıt defteri
static bool SetValue(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY k;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static std::wstring GetValue(HKEY root, const std::wstring& path) {
    HKEY k;
    wchar_t buf[256] = {};
    DWORD sz = sizeof(buf) - sizeof(wchar_t), type = 0;
    std::wstring r;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW(k, nullptr, nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) r = buf;
        RegCloseKey(k);
    }
    return r;
}

static const wchar_t* kExts[] = {L".stl", L".3mf"};

static HRESULT Register(bool perUser) {
    wchar_t dll[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, dll, MAX_PATH)) return E_FAIL;
    HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    std::wstring base = L"Software\\Classes\\";
    bool ok = true;
    ok &= SetValue(root, base + L"CLSID\\" + kClsidStr, nullptr, L"Model Thumbnail Provider (STL/3MF)");
    ok &= SetValue(root, base + L"CLSID\\" + kClsidStr + L"\\InprocServer32", nullptr, dll);
    ok &= SetValue(root, base + L"CLSID\\" + kClsidStr + L"\\InprocServer32", L"ThreadingModel", L"Both");
    for (const wchar_t* ext : kExts) {
        ok &= SetValue(root, base + ext + L"\\ShellEx\\" + kThumbHandlerKey, nullptr, kClsidStr);
        ok &= SetValue(root, base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" + kThumbHandlerKey, nullptr, kClsidStr);
    }
    if (!perUser)
        SetValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", kClsidStr,
                 L"Model Thumbnail Provider (STL/3MF)");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok ? S_OK : SELFREG_E_CLASS;
}

static void DeleteIfOurs(HKEY root, const std::wstring& handlerKeyPath) {
    if (GetValue(root, handlerKeyPath) == kClsidStr) RegDeleteTreeW(root, handlerKeyPath.c_str());
}

static HRESULT Unregister(bool perUser) {
    HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    std::wstring base = L"Software\\Classes\\";
    for (const wchar_t* ext : kExts) {
        DeleteIfOurs(root, base + ext + L"\\ShellEx\\" + kThumbHandlerKey);
        DeleteIfOurs(root, base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" + kThumbHandlerKey);
    }
    RegDeleteTreeW(root, (base + L"CLSID\\" + kClsidStr).c_str());
    if (!perUser) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", 0, KEY_SET_VALUE, &k) ==
            ERROR_SUCCESS) {
            RegDeleteValueW(k, kClsidStr);
            RegCloseKey(k);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// ------------------------------------------------------------------ DLL giriş noktaları
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}

extern "C" {
__declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_ModelThumb)) return CLASS_E_CLASSNOTAVAILABLE;
    Factory* f = new (std::nothrow) Factory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}
__declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(void) { return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE; }
__declspec(dllexport) HRESULT __stdcall DllRegisterServer(void) { return Register(false); }
__declspec(dllexport) HRESULT __stdcall DllUnregisterServer(void) { return Unregister(false); }
// regsvr32 /n /i:user ModelThumbs.dll  -> yönetici izni gerektirmeyen, sadece bu kullanıcı için kurulum
__declspec(dllexport) HRESULT __stdcall DllInstall(BOOL install, PCWSTR cmd) {
    bool perUser = cmd && lstrcmpiW(cmd, L"user") == 0;
    return install ? Register(perUser) : Unregister(perUser);
}
}
