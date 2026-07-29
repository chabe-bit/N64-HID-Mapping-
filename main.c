#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <hidusage.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    BOOL is_down;
    BOOL is_pressed;
    BOOL is_released;
} digitalbutton;

typedef struct {
    float threshold;
    float value;
    BOOL is_down;
    BOOL is_pressed;
    BOOL is_released;
} analogbutton;

typedef struct {
    float threshold;
    struct {
        float x, y;
    } axis;
} controllerstick;

typedef struct {
    digitalbutton a,
                  b,
                  z,
                  start;

    digitalbutton d_up,
                  d_down,
                  d_left,
                  d_right;

    digitalbutton c_up,
                  c_down,
                  c_left,
                  c_right;

    digitalbutton lshoulder,
                  rshoulder;

    controllerstick stick;
} n64gamepad;

typedef struct {
    USAGE usages[32];
    ULONG usage_count;
    float stick_x,
          stick_y;
    ULONG hat;
    n64gamepad gamepad;
} api;

static api g_api;
static BOOL quit = FALSE;

// USAGE button mappings 
#define N64_GAMEPAD_A          2
#define N64_GAMEPAD_B          1
#define N64_GAMEPAD_Z          5

#define N64_GAMEPAD_CPAD_UP    9
#define N64_GAMEPAD_CPAD_DOWN  4
#define N64_GAMEPAD_CPAD_LEFT  3
#define N64_GAMEPAD_CPAD_RIGHT 10

#define N64_GAMEPAD_START      6
#define N64_GAMEPAD_LSHOULDER  7
#define N64_GAMEPAD_RSHOULDER  8

#define N64_GAMEPAD_STICK_THRESHOLD 0.12f

void
update_digitalbuttons(digitalbutton *button, BOOL is_down)
{
    BOOL was_down;

    was_down = button->is_down;
    button->is_down = is_down;
    button->is_pressed = !was_down && is_down;
    button->is_released = was_down && !is_down;
}

void
update_analogbuttons(analogbutton *analog, float value)
{
    BOOL was_down;

    was_down = analog->is_down;
    analog->value = value;
    analog->is_down = (value >= analog->threshold);
    analog->is_pressed = !was_down && analog->is_down;
    analog->is_released = was_down && !analog->is_down;
}

void
update_stick(controllerstick *stick, float x, float y)
{
    if (fabsf(x) <= stick->threshold) {
        x = 0.0f;
    }

    if (fabsf(y) <= stick->threshold) {
        y = 0.0f;
    }

    stick->axis.x = x;
    stick->axis.y = y;
}

static BOOL
hid_usage_is_down(USAGE usage)
{
    ULONG i;
    for (i = 0; i < g_api.usage_count; ++i) {
        if (g_api.usages[i] == usage) {
            return TRUE;
        }
    }

    return FALSE;
}

static float
normalize_hid_axis(ULONG value, LONG logical_min, LONG logical_max)
{
#if 0
    float t;
    float result;

    if (logical_max == logical_min) {
        return 0.0f;
    }

    t = ((float)((LONG)value - logical_min)) /
        ((float)(logical_max - logical_min));

    result = (t * 2.0f) - 1.0f;

    if (result < -1.0f) result = -1.0f;
    if (result >  1.0f) result =  1.0f;

    return result;
#endif 

    if (logical_max == logical_min) {
        return 0.0f;
    }

    if (value < logical_min) {
        value = logical_min;
    } else if (value > logical_max) {
        value = logical_max;
    }

    double range = (double)logical_max - (double)logical_min;
    double offset = (double)value - (double)logical_min;

    float result = (float)((2.0 * offset / range) - 1.0);        
    return result;
}

static void
pull_gamepad(float stick_x, float stick_y, ULONG hat)
{
    update_digitalbuttons(&g_api.gamepad.a,
                          hid_usage_is_down(N64_GAMEPAD_A));

    update_digitalbuttons(&g_api.gamepad.b,
                          hid_usage_is_down(N64_GAMEPAD_B));

    update_digitalbuttons(&g_api.gamepad.z,
                          hid_usage_is_down(N64_GAMEPAD_Z));

    update_digitalbuttons(&g_api.gamepad.start,
                          hid_usage_is_down(N64_GAMEPAD_START));

    update_digitalbuttons(&g_api.gamepad.lshoulder,
                          hid_usage_is_down(N64_GAMEPAD_LSHOULDER));

    update_digitalbuttons(&g_api.gamepad.rshoulder,
                          hid_usage_is_down(N64_GAMEPAD_RSHOULDER));

    update_digitalbuttons(&g_api.gamepad.c_up,
                          hid_usage_is_down(N64_GAMEPAD_CPAD_UP));

    update_digitalbuttons(&g_api.gamepad.c_down,
                          hid_usage_is_down(N64_GAMEPAD_CPAD_DOWN));

    update_digitalbuttons(&g_api.gamepad.c_left,
                          hid_usage_is_down(N64_GAMEPAD_CPAD_LEFT));

    update_digitalbuttons(&g_api.gamepad.c_right,
                          hid_usage_is_down(N64_GAMEPAD_CPAD_RIGHT));

    /*
        Hat switch:
        0 = up
        1 = up/right
        2 = right
        3 = down/right
        4 = down
        5 = down/left
        6 = left
        7 = up/left

    */
    update_digitalbuttons(&g_api.gamepad.d_up,
                          hat == 0 || hat == 1 || hat == 7);

    update_digitalbuttons(&g_api.gamepad.d_right,
                          hat == 1 || hat == 2 || hat == 3);

    update_digitalbuttons(&g_api.gamepad.d_down,
                          hat == 3 || hat == 4 || hat == 5);

    update_digitalbuttons(&g_api.gamepad.d_left,
                          hat == 5 || hat == 6 || hat == 7);

    update_stick(&g_api.gamepad.stick, stick_x, stick_y);
}

static void
print_gamepad_debug(void)
{
    if (g_api.gamepad.a.is_pressed) {
        printf("N64 A pressed\n");
    }

    if (g_api.gamepad.b.is_pressed) {
        printf("N64 B pressed\n");
    }

    if (g_api.gamepad.z.is_pressed) {
        printf("N64 Z pressed\n");
    }

    if (g_api.gamepad.start.is_pressed) {
        printf("N64 START pressed\n");
    }

    if (g_api.gamepad.c_up.is_pressed) {
        printf("N64 C-UP pressed\n");
    }

    if (g_api.gamepad.c_down.is_pressed) {
        printf("N64 C-DOWN pressed\n");
    }

    if (g_api.gamepad.c_left.is_pressed) {
        printf("N64 C-LEFT pressed\n");
    }

    if (g_api.gamepad.c_right.is_pressed) {
        printf("N64 C-RIGHT pressed\n");
    }

    if (g_api.gamepad.d_up.is_pressed) {
        printf("N64 D-UP pressed\n");
    }

    if (g_api.gamepad.d_down.is_pressed) {
        printf("N64 D-DOWN pressed\n");
    }

    if (g_api.gamepad.d_left.is_pressed) {
        printf("N64 D-LEFT pressed\n");
    }

    if (g_api.gamepad.d_right.is_pressed) {
        printf("N64 D-RIGHT pressed\n");
    }

    if (g_api.gamepad.lshoulder.is_pressed) {
        printf("N64 L pressed\n");
    }

    if (g_api.gamepad.rshoulder.is_pressed) {
        printf("N64 R pressed\n");
    }
    printf("stick: %.2f, %.2f\n",
           g_api.gamepad.stick.axis.x,
           g_api.gamepad.stick.axis.y);
}

void
query_hid_info(HANDLE hid_handle)
{
    RID_DEVICE_INFO *rdi;
    UINT rdi_count = 0;
    HANDLE hheap = GetProcessHeap();

    if (GetRawInputDeviceInfoA(hid_handle, RIDI_DEVICEINFO, NULL, &rdi_count) == (UINT)-1) {
        return;
    }
    
    rdi = (RID_DEVICE_INFO *)HeapAlloc(hheap, HEAP_ZERO_MEMORY, rdi_count * sizeof(RID_DEVICE_INFO));
    assert(rdi);

    if (GetRawInputDeviceInfoA(hid_handle, RIDI_DEVICEINFO, rdi, &rdi_count) == 0) {
        return;
    }

    if (rdi->dwType == RIM_TYPEHID) {
        printf("vendor_id: %lu product_id: %lu usage_page: 0x%04x usage: 0x%04x\n",
               rdi->hid.dwVendorId,
               rdi->hid.dwProductId,
               rdi->hid.usUsagePage,
               rdi->hid.usUsage);
    }

    HeapFree(hheap, 0, rdi);
}

void 
print_rawinput_data(LPARAM lparam)
{
    PHIDP_PREPARSED_DATA ppd;
    HIDP_BUTTON_CAPS *b_caps;
    HIDP_VALUE_CAPS  *v_caps;
    HIDP_CAPS caps = {0};
    RAWINPUT *ri;
    USAGE *usages;
    UINT ppd_count = 0, 
         ri_count = 0;
    USHORT b_caps_count = 0, 
           v_caps_count = 0;
    HANDLE hheap = GetProcessHeap();
    BOOL status;

    if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, NULL, &ri_count, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        return;
    }

    ri = (RAWINPUT *)HeapAlloc(hheap, HEAP_ZERO_MEMORY, ri_count);
    assert(ri);
    
    if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, ri, &ri_count, sizeof(RAWINPUTHEADER)) == 0) {
        return;
    }
   
    if (ri->header.dwType == RIM_TYPEHID) {
        if (GetRawInputDeviceInfoA(ri->header.hDevice, RIDI_PREPARSEDDATA, NULL, &ppd_count) == (UINT)-1) {
            return;
        }

        ppd = (PHIDP_PREPARSED_DATA)HeapAlloc(hheap, HEAP_ZERO_MEMORY, ppd_count);
        assert(ppd);

        if (GetRawInputDeviceInfoA(ri->header.hDevice, RIDI_PREPARSEDDATA, ppd, &ppd_count) == 0) {
            return;
        }

        if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS) {
            v_caps_count = caps.NumberInputValueCaps;
            b_caps_count = caps.NumberInputButtonCaps;
            
            g_api.stick_x = g_api.gamepad.stick.axis.x;
            g_api.stick_y = g_api.gamepad.stick.axis.y;
            g_api.hat = 0xFFFFFFFF;

            g_api.usage_count = 0;

            v_caps = (HIDP_VALUE_CAPS *)HeapAlloc(hheap, HEAP_ZERO_MEMORY, v_caps_count * sizeof(HIDP_VALUE_CAPS));
            assert(v_caps);

            if (HidP_GetValueCaps(HidP_Input, v_caps, &v_caps_count, ppd) == HIDP_STATUS_SUCCESS) {
                USHORT i;
                for (i = 0; i < v_caps_count; i++) {
                    ULONG value;
                    USAGE usage;

                    if (v_caps[i].IsRange)
                        usage = v_caps[i].Range.UsageMin;
                    else 
                        usage = v_caps[i].NotRange.Usage;

                    if (HidP_GetUsageValue(HidP_Input, 
                                           v_caps[i].UsagePage,
                                           0,
                                           usage,
                                           &value,
                                           ppd,
                                           (PCHAR)ri->data.hid.bRawData,
                                           ri->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) 
                    {
                        continue;
                    }
        
                    switch (usage) {
                    case HID_USAGE_GENERIC_X:
                        g_api.stick_x = normalize_hid_axis(value, v_caps[i].LogicalMin, v_caps[i].LogicalMax);
                        break;
                    case HID_USAGE_GENERIC_Y:
                        g_api.stick_y = normalize_hid_axis(value, v_caps[i].LogicalMin, v_caps[i].LogicalMax);
                        g_api.stick_y = -g_api.stick_y;
                        break;
                    case HID_USAGE_GENERIC_HATSWITCH:
                        g_api.hat = value;
                        break;
                    }
                }
            }
        
            b_caps = (HIDP_BUTTON_CAPS *)HeapAlloc(hheap, HEAP_ZERO_MEMORY, b_caps_count * sizeof(HIDP_BUTTON_CAPS));
            assert(b_caps);

            if (HidP_GetButtonCaps(HidP_Input, b_caps, &b_caps_count, ppd) == HIDP_STATUS_SUCCESS) {
                USHORT i;
                for (i = 0; i < b_caps_count; i++) {
                    ULONG usage_cap, usage_count;
            
                    if (b_caps[i].IsRange) 
                        usage_cap = b_caps[i].Range.UsageMax - b_caps[i].Range.UsageMin + 1;
                    else 
                        usage_cap = 1;

                    if (usage_cap == 0)
                        continue;

                    usages = (USAGE *)HeapAlloc(hheap, HEAP_ZERO_MEMORY, usage_cap * sizeof(USAGE));
                    assert(usages);
                    usage_count = usage_cap;

                    if (HidP_GetUsages(HidP_Input,
                                       b_caps[i].UsagePage,
                                       0,
                                       usages,
                                       &usage_count,
                                       ppd,
                                       (PCHAR)ri->data.hid.bRawData,
                                       ri->data.hid.dwSizeHid) == HIDP_STATUS_SUCCESS);
                    {
                        ULONG i;
                        for (i = 0; i < usage_count; i++) {
                            USAGE usage;
                            usage = usages[i];
                            if (g_api.usage_count < ARRAY_SIZE(g_api.usages)) {
                                g_api.usages[g_api.usage_count++] = usage;
                            }
                        }
                    }

                }
            }
        }
    }
    
    HeapFree(hheap, 0, ppd);
    HeapFree(hheap, 0, v_caps);
    HeapFree(hheap, 0, b_caps);
    HeapFree(hheap, 0, usages);
    HeapFree(hheap, 0, ri);
}

static USHORT device_usages[] = {
    HID_USAGE_GENERIC_GAMEPAD,
    HID_USAGE_GENERIC_JOYSTICK
};

static void
register_rawinput_devices(HWND hwnd)
{
    RAWINPUTDEVICE rid[ARRAY_SIZE(device_usages)];
    UINT i;

    for (i = 0; i < ARRAY_SIZE(device_usages); i++) {
        rid[i].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid[i].usUsage = device_usages[i];
        rid[i].dwFlags = RIDEV_DEVNOTIFY | RIDEV_INPUTSINK;
        rid[i].hwndTarget = hwnd;
    }

    if (!RegisterRawInputDevices(rid, ARRAY_SIZE(rid), sizeof(RAWINPUTDEVICE))) {
        return;
    }

    printf("Registered Raw Input devices.\n");
}

static void
pending_message(void)
{
    MSG message;
    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
        switch (message.message) {
        case WM_QUIT:
            quit = TRUE;
            break;
        default:
            TranslateMessage(&message);
            DispatchMessageA(&message);
            break;
        }
    }
}


static LRESULT
main_wndproc(HWND hwnd, 
                UINT message, 
                WPARAM wparam, 
                LPARAM lparam)
{
    LRESULT result = 0;

    switch (message) {
    case WM_CREATE:
        register_rawinput_devices(hwnd);
        break;
    case WM_INPUT:
        print_rawinput_data(lparam);
        break;
    case WM_CLOSE:
        quit = TRUE;
        break;
    case WM_DESTROY:
        quit = TRUE;
        break;
    default:
        result = DefWindowProcA(hwnd, message, wparam, lparam);
        break;
    }

    return result;
}

void 
console_attach(void)
{
    AttachConsole(ATTACH_PARENT_PROCESS);
    if (GetConsoleWindow() != NULL) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

int APIENTRY
WinMain(HINSTANCE inst,
        HINSTANCE prev_inst,
        LPSTR cmdline,
        int cmdshow)
{
    WNDCLASSA wc = {0};
    HWND hwnd;

    console_attach();
    g_api.gamepad.stick.threshold = N64_GAMEPAD_STICK_THRESHOLD;

    wc.hInstance = inst;
    wc.lpfnWndProc = main_wndproc;
    wc.lpszClassName = "N64";
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

    if (!RegisterClassA(&wc)) {
        printf("RegisterClassA failed: %lu\n", GetLastError());
        return 1;
    }

    hwnd = CreateWindowExA(0, wc.lpszClassName, "Main", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, inst, 0);
    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (!quit) {
        pending_message();

        pull_gamepad(g_api.stick_x, g_api.stick_y, g_api.hat);

        print_gamepad_debug(); 

    }

    return 0;
}
