/* ChuniF4 raw IO4 HID -> Segatools MU3 IO API 1.1.
 * Firmware layout: ChuniF4/Core/Src/main.c (64 bytes, report ID 1).
 * API contract: djhackersdev/segatools mu3io/mu3io.h and mu3hook/io4.c.
 * Reads input only. Does not write USB commands or change firmware.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define API __declspec(dllexport)
static SRWLOCK lock = SRWLOCK_INIT;
static HANDLE device = INVALID_HANDLE_VALUE;
static OVERLAPPED read_op;
static BOOL pending;
static uint8_t report[64];
static uint8_t left_buttons, right_buttons;
static int16_t lever_position;
static ULONGLONG last_report, last_search;
static uint32_t frames;

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

/* This exported decoder also permits deterministic report-replay tests. */
API BOOL chunif4_decode_report(const uint8_t *r, uint32_t n,
        uint8_t *left, uint8_t *right, int16_t *lever) {
    uint16_t a, b;
    if (!r || n != 64 || r[0] != 1 || !left || !right || !lever) return FALSE;
    a = u16(r + 29); b = u16(r + 31);
    *left = ((a & (1u << 0)) ? 1 : 0)
          | ((a & (1u << 5)) ? 2 : 0)
          | ((a & (1u << 4)) ? 4 : 0)
          | ((b & (1u << 15)) ? 8 : 0)
          | ((b & (1u << 14)) ? 16 : 0);
    *right = ((a & (1u << 1)) ? 1 : 0)
           | ((b & (1u << 0)) ? 2 : 0)
           | ((a & (1u << 15)) ? 4 : 0)
           | ((a & (1u << 14)) ? 8 : 0)
           | ((a & (1u << 13)) ? 16 : 0);
    /* Firmware: physical left=0, center=32768, right=65535.
     * MU3 API: physical left is negative, center=0, right is positive.
     * mu3hook then produces the cabinet ADC direction (right-to-left).
     * Subtract in 32 bits so both full-scale endpoints remain representable. */
    *lever = (int16_t)((int32_t)u16(r + 1) - 32768);
    return TRUE;
}

static void neutral(void) { left_buttons = right_buttons = 0; lever_position = 0; }

static void disconnect(void) {
    if (device != INVALID_HANDLE_VALUE) {
        if (pending) {
            DWORD ignored;
            CancelIoEx(device, &read_op);
            GetOverlappedResult(device, &read_op, &ignored, TRUE);
        }
        CloseHandle(device);
    }
    device = INVALID_HANDLE_VALUE;
    pending = FALSE;
    neutral();
}

/* Called only while holding lock. Never choose arbitrarily between two boards. */
static HRESULT connect_device(void) {
    GUID guid;
    HDEVINFO devices;
    SP_DEVICE_INTERFACE_DATA iface;
    wchar_t chosen[1024] = {0};
    DWORD i, matches = 0;
    HidD_GetHidGuid(&guid);
    devices = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    for (i = 0; ; ++i) {
        DWORD size = 0;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        HANDLE query;
        HIDD_ATTRIBUTES attributes;
        PHIDP_PREPARSED_DATA preparsed = NULL;
        HIDP_CAPS caps;
        BOOL valid = FALSE;
        memset(&iface, 0, sizeof(iface)); iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(devices, NULL, &guid, i, &iface)) break;
        SetupDiGetDeviceInterfaceDetailW(devices, &iface, NULL, 0, &size, NULL);
        if (size < sizeof(*detail)) continue;
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)calloc(1, size);
        if (!detail) { SetupDiDestroyDeviceInfoList(devices); return E_OUTOFMEMORY; }
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, size, NULL, NULL)) { free(detail); continue; }
        query = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL, OPEN_EXISTING, 0, NULL);
        if (query != INVALID_HANDLE_VALUE) {
            memset(&attributes, 0, sizeof(attributes)); attributes.Size = sizeof(attributes);
            if (HidD_GetAttributes(query, &attributes) && attributes.VendorID == 0x0CA3
                    && attributes.ProductID == 0x0021 && HidD_GetPreparsedData(query, &preparsed)) {
                memset(&caps, 0, sizeof(caps));
                valid = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS
                        && caps.InputReportByteLength == 64 && caps.OutputReportByteLength == 64;
                HidD_FreePreparsedData(preparsed);
            }
            CloseHandle(query);
        }
        if (valid) {
            ++matches;
            if (wcslen(detail->DevicePath) < 1024) wcscpy(chosen, detail->DevicePath);
            else chosen[0] = 0;
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (matches > 1) return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
    if (matches != 1 || !chosen[0]) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    device = CreateFileW(chosen, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (device == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    HidD_SetNumInputBuffers(device, 64);
    if (!read_op.hEvent) read_op.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!read_op.hEvent) { HRESULT hr = HRESULT_FROM_WIN32(GetLastError()); disconnect(); return hr; }
    pending = FALSE;
    last_report = GetTickCount64();
    return S_OK;
}

API uint16_t mu3_io_get_api_version(void) { return 0x0101; }

/* This ChuniF4 firmware has no LED output implementation. The installed
 * mu3hook still requires these entry points when LED board emulation is on.
 * Consume the callbacks without sending unsupported commands to the board.
 */
API HRESULT mu3_io_led_init(void) { return S_OK; }
API void mu3_io_led_set_colors(uint8_t board, const uint8_t *rgb) {
    (void)board; (void)rgb;
}

API HRESULT mu3_io_init(void) {
    HRESULT hr;
    AcquireSRWLockExclusive(&lock);
    disconnect();
    last_search = GetTickCount64();
    hr = connect_device();
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

API HRESULT mu3_io_poll(void) {
    unsigned i;
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    if (device == INVALID_HANDLE_VALUE && now - last_search >= 1000) {
        last_search = now;
        connect_device();
    }
    for (i = 0; device != INVALID_HANDLE_VALUE && i < 64; ++i) {
        DWORD got = 0, error;
        BOOL ready;
        if (pending) {
            ready = GetOverlappedResult(device, &read_op, &got, FALSE);
            if (!ready) {
                error = GetLastError();
                if (error == ERROR_IO_INCOMPLETE) break;
                disconnect(); break;
            }
            pending = FALSE;
        } else {
            ResetEvent(read_op.hEvent);
            read_op.Offset = read_op.OffsetHigh = 0;
            ready = ReadFile(device, report, sizeof(report), &got, &read_op);
            if (!ready) {
                error = GetLastError();
                if (error == ERROR_IO_PENDING) { pending = TRUE; break; }
                disconnect(); break;
            }
        }
        if (chunif4_decode_report(report, got, &left_buttons, &right_buttons, &lever_position)) {
            last_report = GetTickCount64();
            ++frames;
        }
    }
    if (GetTickCount64() - last_report > 100) neutral();
    ReleaseSRWLockExclusive(&lock);
    return S_OK;
}

API void mu3_io_get_opbtns(uint8_t *buttons) {
    if (buttons) *buttons = ((GetAsyncKeyState(VK_F1) & 0x8000) ? 1 : 0)
                          | ((GetAsyncKeyState(VK_F2) & 0x8000) ? 2 : 0)
                          | ((GetAsyncKeyState(VK_F3) & 0x8000) ? 4 : 0);
}
API void mu3_io_get_gamebtns(uint8_t *left, uint8_t *right) {
    AcquireSRWLockShared(&lock);
    if (left) *left = left_buttons;
    if (right) *right = right_buttons;
    ReleaseSRWLockShared(&lock);
}
API void mu3_io_get_lever(int16_t *position) {
    AcquireSRWLockShared(&lock);
    if (position) *position = lever_position;
    ReleaseSRWLockShared(&lock);
}
API uint32_t chunif4_get_frames(void) {
    uint32_t n;
    AcquireSRWLockShared(&lock); n = frames; ReleaseSRWLockShared(&lock);
    return n;
}
API void chunif4_shutdown(void) {
    AcquireSRWLockExclusive(&lock);
    disconnect();
    if (read_op.hEvent) { CloseHandle(read_op.hEvent); read_op.hEvent = NULL; }
    ReleaseSRWLockExclusive(&lock);
}
