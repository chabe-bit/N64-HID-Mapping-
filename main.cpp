#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

#include <hidusage.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static constexpr float n64_stick_threshold = 0.12f;
static constexpr ULONG n64_hat_neutral = 0xFFFFFFFFUL;
static constexpr std::size_t n64_max_active_usages = 32;
static constexpr char window_class_name[] = "N64HidCppWindow";

enum class n64_usage : USAGE {
    b = 1,
    a = 2,
    c_left = 3,
    c_down = 4,
    z = 5,
    start = 6,
    l_shoulder = 7,
    r_shoulder = 8,
    c_up = 9,
    c_right = 10,
};

typedef struct {
    bool is_down;
    bool is_pressed;
    bool is_released;
} digital_button;

typedef struct {
    float x;
    float y;
} axis2;

typedef struct {
    float threshold;
    axis2 axis;
} controller_stick;

typedef struct {
    digital_button a;
    digital_button b;
    digital_button z;
    digital_button start;

    digital_button d_up;
    digital_button d_down;
    digital_button d_left;
    digital_button d_right;

    digital_button c_up;
    digital_button c_down;
    digital_button c_left;
    digital_button c_right;

    digital_button l_shoulder;
    digital_button r_shoulder;

    controller_stick stick;
} n64_gamepad;

typedef struct {
    HANDLE device;
    std::array<USAGE, n64_max_active_usages> active_usages;
    std::size_t active_usage_count;
    float stick_x;
    float stick_y;
    ULONG hat;
    n64_gamepad gamepad;
} n64_controller;

typedef struct {
    HINSTANCE instance;
    HWND window;
    bool window_class_registered;
    bool quit;
    axis2 last_printed_stick;
    n64_controller controller;
} application;

static application g_app = {};

static void
n64_print_gamepad_events(void);

static void
digital_button_update(digital_button *button, bool is_down)
{
    bool was_down = button->is_down;

    button->is_down = is_down;
    button->is_pressed = !was_down && is_down;
    button->is_released = was_down && !is_down;
}

static void
controller_stick_update(controller_stick *stick, float x, float y)
{
    if (std::fabs(x) <= stick->threshold) {
        x = 0.0f;
    }

    if (std::fabs(y) <= stick->threshold) {
        y = 0.0f;
    }

    stick->axis.x = x;
    stick->axis.y = y;
}

static void
n64_controller_begin_report(n64_controller *controller)
{
    controller->active_usage_count = 0;
    controller->stick_x = controller->gamepad.stick.axis.x;
    controller->stick_y = controller->gamepad.stick.axis.y;
    controller->hat = n64_hat_neutral;
}

static void
n64_controller_init(n64_controller *controller)
{
    *controller = n64_controller();
    controller->hat = n64_hat_neutral;
    controller->gamepad.stick.threshold = n64_stick_threshold;
}

static void
n64_controller_add_usage(n64_controller *controller, USAGE usage)
{
    if (controller->active_usage_count <
        controller->active_usages.size()) {
        controller->active_usages[controller->active_usage_count++] = usage;
    }
}

static LONG
n64_decode_logical_value(ULONG raw_value,
                         LONG logical_min,
                         USHORT bit_size)
{
    if (logical_min >= 0 || bit_size == 0 || bit_size > 32) {
        return static_cast<LONG>(raw_value);
    }

    std::uint64_t modulus = std::uint64_t(1) << bit_size;
    std::uint64_t value =
        static_cast<std::uint64_t>(raw_value) & (modulus - 1);
    std::uint64_t sign_bit = modulus >> 1;

    if ((value & sign_bit) == 0) {
        return static_cast<LONG>(value);
    }

    std::int64_t signed_value =
        static_cast<std::int64_t>(value) -
        static_cast<std::int64_t>(modulus);

    return static_cast<LONG>(signed_value);
}

static float
n64_normalize_axis(LONG value, LONG logical_min, LONG logical_max)
{
    if (logical_max == logical_min) {
        return 0.0f;
    }

    float t =
        (static_cast<float>(value) -
         static_cast<float>(logical_min)) /
        (static_cast<float>(logical_max) -
         static_cast<float>(logical_min));

    float result = (t * 2.0f) - 1.0f;

    if (result < -1.0f) {
        result = -1.0f;
    }

    if (result > 1.0f) {
        result = 1.0f;
    }

    return result;
}

static bool
n64_usage_is_down(const n64_controller *controller, n64_usage usage)
{
    USAGE raw_usage = static_cast<USAGE>(usage);

    for (std::size_t i = 0;
         i < controller->active_usage_count;
         ++i) {
        if (controller->active_usages[i] == raw_usage) {
            return true;
        }
    }

    return false;
}

static void
n64_controller_set_value(n64_controller *controller,
                         USAGE usage_page,
                         USAGE usage,
                         ULONG raw_value,
                         LONG logical_min,
                         LONG logical_max,
                         USHORT bit_size)
{
    if (usage_page != HID_USAGE_PAGE_GENERIC) {
        return;
    }

    LONG value =
        n64_decode_logical_value(raw_value, logical_min, bit_size);

    switch (usage) {
    case HID_USAGE_GENERIC_X:
        controller->stick_x =
            n64_normalize_axis(value, logical_min, logical_max);
        break;

    case HID_USAGE_GENERIC_Y:
        controller->stick_y =
            -n64_normalize_axis(value, logical_min, logical_max);
        break;

    case HID_USAGE_GENERIC_HATSWITCH:
        if (value >= 0) {
            controller->hat = static_cast<ULONG>(value);
        } else {
            controller->hat = n64_hat_neutral;
        }
        break;
    }
}

static void
n64_controller_update(n64_controller *controller)
{
    n64_gamepad *gamepad = &controller->gamepad;

    digital_button_update(&gamepad->a,
                          n64_usage_is_down(controller, n64_usage::a));
    digital_button_update(&gamepad->b,
                          n64_usage_is_down(controller, n64_usage::b));
    digital_button_update(&gamepad->z,
                          n64_usage_is_down(controller, n64_usage::z));
    digital_button_update(&gamepad->start,
                          n64_usage_is_down(controller, n64_usage::start));

    digital_button_update(
        &gamepad->l_shoulder,
        n64_usage_is_down(controller, n64_usage::l_shoulder));
    digital_button_update(
        &gamepad->r_shoulder,
        n64_usage_is_down(controller, n64_usage::r_shoulder));

    digital_button_update(&gamepad->c_up,
                          n64_usage_is_down(controller, n64_usage::c_up));
    digital_button_update(&gamepad->c_down,
                          n64_usage_is_down(controller, n64_usage::c_down));
    digital_button_update(&gamepad->c_left,
                          n64_usage_is_down(controller, n64_usage::c_left));
    digital_button_update(&gamepad->c_right,
                          n64_usage_is_down(controller, n64_usage::c_right));

    digital_button_update(
        &gamepad->d_up,
        controller->hat == 0 ||
        controller->hat == 1 ||
        controller->hat == 7);

    digital_button_update(
        &gamepad->d_right,
        controller->hat == 1 ||
        controller->hat == 2 ||
        controller->hat == 3);

    digital_button_update(
        &gamepad->d_down,
        controller->hat == 3 ||
        controller->hat == 4 ||
        controller->hat == 5);

    digital_button_update(
        &gamepad->d_left,
        controller->hat == 5 ||
        controller->hat == 6 ||
        controller->hat == 7);

    controller_stick_update(&gamepad->stick,
                            controller->stick_x,
                            controller->stick_y);
}

static bool
hid_report_id_matches(UCHAR report_id, PCHAR report)
{
    if (report_id == 0) {
        return true;
    }

    return static_cast<UCHAR>(report[0]) == report_id;
}

static void
hid_read_value_caps(n64_controller *controller,
                    const std::vector<HIDP_VALUE_CAPS> &caps,
                    PHIDP_PREPARSED_DATA preparsed_data,
                    PCHAR report,
                    ULONG report_size)
{
    for (std::size_t i = 0; i < caps.size(); ++i) {
        const HIDP_VALUE_CAPS *cap = &caps[i];

        if (!hid_report_id_matches(cap->ReportID, report)) {
            continue;
        }

        USAGE first_usage;
        USAGE last_usage;

        if (cap->IsRange) {
            first_usage = cap->Range.UsageMin;
            last_usage = cap->Range.UsageMax;
        } else {
            first_usage = cap->NotRange.Usage;
            last_usage = cap->NotRange.Usage;
        }

        for (ULONG raw_usage = first_usage;
             raw_usage <= last_usage;
             ++raw_usage) {
            USAGE usage = static_cast<USAGE>(raw_usage);
            ULONG value = 0;

            if (HidP_GetUsageValue(HidP_Input,
                                   cap->UsagePage,
                                   cap->LinkCollection,
                                   usage,
                                   &value,
                                   preparsed_data,
                                   report,
                                   report_size) != HIDP_STATUS_SUCCESS) {
                continue;
            }

            n64_controller_set_value(controller,
                                     cap->UsagePage,
                                     usage,
                                     value,
                                     cap->LogicalMin,
                                     cap->LogicalMax,
                                     cap->BitSize);
        }
    }
}

static bool
hid_read_button_caps(n64_controller *controller,
                     const std::vector<HIDP_BUTTON_CAPS> &caps,
                     PHIDP_PREPARSED_DATA preparsed_data,
                     PCHAR report,
                     ULONG report_size)
{
    bool found_button_report = false;

    for (std::size_t i = 0; i < caps.size(); ++i) {
        const HIDP_BUTTON_CAPS *cap = &caps[i];

        if (cap->UsagePage != HID_USAGE_PAGE_BUTTON) {
            continue;
        }

        if (!hid_report_id_matches(cap->ReportID, report)) {
            continue;
        }

        found_button_report = true;

        ULONG usage_capacity;

        if (cap->IsRange) {
            usage_capacity =
                cap->Range.UsageMax -
                cap->Range.UsageMin + 1;
        } else {
            usage_capacity = 1;
        }

        if (usage_capacity == 0) {
            continue;
        }

        std::vector<USAGE> usages(usage_capacity);
        ULONG usage_count = usage_capacity;

        if (HidP_GetUsages(HidP_Input,
                           cap->UsagePage,
                           cap->LinkCollection,
                           usages.data(),
                           &usage_count,
                           preparsed_data,
                           report,
                           report_size) != HIDP_STATUS_SUCCESS) {
            return false;
        }

        for (ULONG usage_index = 0;
             usage_index < usage_count;
             ++usage_index) {
            n64_controller_add_usage(controller, usages[usage_index]);
        }
    }

    return found_button_report;
}

static void
hid_parse_reports(n64_controller *controller, const RAWINPUT *raw_input)
{
    UINT preparsed_size = 0;

    if (raw_input->data.hid.dwSizeHid == 0 ||
        raw_input->data.hid.dwCount == 0) {
        return;
    }

    if (GetRawInputDeviceInfoA(raw_input->header.hDevice,
                               RIDI_PREPARSEDDATA,
                               nullptr,
                               &preparsed_size) ==
        static_cast<UINT>(-1)) {
        return;
    }

    if (preparsed_size == 0) {
        return;
    }

    std::size_t preparsed_word_count =
        (static_cast<std::size_t>(preparsed_size) +
         sizeof(ULONG_PTR) - 1) /
        sizeof(ULONG_PTR);

    std::vector<ULONG_PTR> preparsed_storage(preparsed_word_count);
    PHIDP_PREPARSED_DATA preparsed_data =
        reinterpret_cast<PHIDP_PREPARSED_DATA>(
            preparsed_storage.data());

    if (GetRawInputDeviceInfoA(raw_input->header.hDevice,
                               RIDI_PREPARSEDDATA,
                               preparsed_data,
                               &preparsed_size) ==
        static_cast<UINT>(-1)) {
        return;
    }

    HIDP_CAPS caps = {};

    if (HidP_GetCaps(preparsed_data, &caps) != HIDP_STATUS_SUCCESS) {
        return;
    }

    USHORT value_caps_count = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> value_caps(value_caps_count);

    if (value_caps_count > 0) {
        if (HidP_GetValueCaps(HidP_Input,
                              value_caps.data(),
                              &value_caps_count,
                              preparsed_data) != HIDP_STATUS_SUCCESS) {
            value_caps.clear();
        } else {
            value_caps.resize(value_caps_count);
        }
    }

    USHORT button_caps_count = caps.NumberInputButtonCaps;
    std::vector<HIDP_BUTTON_CAPS> button_caps(button_caps_count);

    if (button_caps_count > 0) {
        if (HidP_GetButtonCaps(HidP_Input,
                               button_caps.data(),
                               &button_caps_count,
                               preparsed_data) != HIDP_STATUS_SUCCESS) {
            button_caps.clear();
        } else {
            button_caps.resize(button_caps_count);
        }
    }

    const char *report_data =
        reinterpret_cast<const char *>(
            raw_input->data.hid.bRawData);

    for (DWORD report_index = 0;
         report_index < raw_input->data.hid.dwCount;
         ++report_index) {
        n64_controller_begin_report(controller);

        PCHAR report = const_cast<PCHAR>(
            report_data +
            (report_index * raw_input->data.hid.dwSizeHid));

        hid_read_value_caps(controller,
                            value_caps,
                            preparsed_data,
                            report,
                            raw_input->data.hid.dwSizeHid);

        bool buttons_parsed =
            hid_read_button_caps(controller,
                                 button_caps,
                                 preparsed_data,
                                 report,
                                 raw_input->data.hid.dwSizeHid);

        if (buttons_parsed) {
            n64_controller_update(controller);
            n64_print_gamepad_events();
        }
    }
}

static void
win32_read_raw_input(HRAWINPUT raw_input_handle)
{
    UINT input_size = 0;

    if (GetRawInputData(raw_input_handle,
                        RID_INPUT,
                        nullptr,
                        &input_size,
                        sizeof(RAWINPUTHEADER)) ==
        static_cast<UINT>(-1)) {
        return;
    }

    if (input_size < sizeof(RAWINPUTHEADER)) {
        return;
    }

    std::size_t input_word_count =
        (static_cast<std::size_t>(input_size) +
         sizeof(ULONG_PTR) - 1) /
        sizeof(ULONG_PTR);

    std::vector<ULONG_PTR> input_storage(input_word_count);
    RAWINPUT *raw_input =
        reinterpret_cast<RAWINPUT *>(input_storage.data());

    UINT bytes_read =
        GetRawInputData(raw_input_handle,
                        RID_INPUT,
                        raw_input,
                        &input_size,
                        sizeof(RAWINPUTHEADER));

    if (bytes_read == static_cast<UINT>(-1) ||
        bytes_read < sizeof(RAWINPUTHEADER)) {
        return;
    }

    if (raw_input->header.dwType != RIM_TYPEHID) {
        return;
    }

    if (!g_app.controller.device) {
        g_app.controller.device = raw_input->header.hDevice;
    } else if (g_app.controller.device !=
               raw_input->header.hDevice) {
        return;
    }

    std::size_t report_size =
        static_cast<std::size_t>(raw_input->data.hid.dwSizeHid);
    std::size_t report_count =
        static_cast<std::size_t>(raw_input->data.hid.dwCount);

    if (report_size == 0 ||
        report_count >
            std::numeric_limits<std::size_t>::max() / report_size) {
        return;
    }

    const unsigned char *input_begin =
        reinterpret_cast<const unsigned char *>(raw_input);
    const unsigned char *report_begin =
        reinterpret_cast<const unsigned char *>(
            raw_input->data.hid.bRawData);

    std::size_t report_offset =
        static_cast<std::size_t>(report_begin - input_begin);
    std::size_t report_bytes = report_count * report_size;

    if (report_offset > bytes_read ||
        report_bytes >
            static_cast<std::size_t>(bytes_read) - report_offset) {
        return;
    }

    hid_parse_reports(&g_app.controller, raw_input);
}

static bool
win32_register_raw_input(HWND window)
{
    USHORT device_usages[] = {
        HID_USAGE_GENERIC_GAMEPAD,
        HID_USAGE_GENERIC_JOYSTICK,
    };

    RAWINPUTDEVICE devices[ARRAY_COUNT(device_usages)] = {};
    UINT device_count =
        static_cast<UINT>(ARRAY_COUNT(device_usages));

    for (UINT i = 0; i < device_count; ++i) {
        devices[i].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[i].usUsage = device_usages[i];
        devices[i].dwFlags = RIDEV_DEVNOTIFY | RIDEV_INPUTSINK;
        devices[i].hwndTarget = window;
    }

    BOOL registered =
        RegisterRawInputDevices(devices,
                                device_count,
                                sizeof(RAWINPUTDEVICE));

    if (registered) {
        std::puts("Registered Raw Input gamepad and joystick devices.");
    }

    return registered == TRUE;
}

static void
win32_print_hid_device_info(HANDLE device)
{
    RID_DEVICE_INFO info = {};
    UINT info_size = sizeof(info);

    info.cbSize = sizeof(info);

    if (GetRawInputDeviceInfoA(device,
                               RIDI_DEVICEINFO,
                               &info,
                               &info_size) ==
        static_cast<UINT>(-1)) {
        return;
    }

    if (info.dwType == RIM_TYPEHID) {
        std::printf(
            "HID connected: vendor=0x%04lx product=0x%04lx "
            "usage_page=0x%04x usage=0x%04x\n",
            info.hid.dwVendorId,
            info.hid.dwProductId,
            info.hid.usUsagePage,
            info.hid.usUsage);
    }
}

static void
print_button_event(const char *name, const digital_button *button)
{
    if (button->is_pressed) {
        std::printf("%s pressed\n", name);
    } else if (button->is_released) {
        std::printf("%s released\n", name);
    }
}

static void
n64_print_gamepad_events(void)
{
    const n64_gamepad *gamepad = &g_app.controller.gamepad;

    print_button_event("A", &gamepad->a);
    print_button_event("B", &gamepad->b);
    print_button_event("Z", &gamepad->z);
    print_button_event("Start", &gamepad->start);
    print_button_event("L", &gamepad->l_shoulder);
    print_button_event("R", &gamepad->r_shoulder);

    print_button_event("C-Up", &gamepad->c_up);
    print_button_event("C-Down", &gamepad->c_down);
    print_button_event("C-Left", &gamepad->c_left);
    print_button_event("C-Right", &gamepad->c_right);

    print_button_event("D-Up", &gamepad->d_up);
    print_button_event("D-Down", &gamepad->d_down);
    print_button_event("D-Left", &gamepad->d_left);
    print_button_event("D-Right", &gamepad->d_right);

    const float print_epsilon = 0.01f;
    const axis2 *stick = &gamepad->stick.axis;

    if (std::fabs(stick->x - g_app.last_printed_stick.x) >
            print_epsilon ||
        std::fabs(stick->y - g_app.last_printed_stick.y) >
            print_epsilon) {
        std::printf("Stick: %.2f, %.2f\n", stick->x, stick->y);
        g_app.last_printed_stick = *stick;
    }
}

static void
win32_process_pending_messages(void)
{
    MSG message = {};

    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            g_app.quit = true;
            break;
        }

        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static LRESULT CALLBACK
win32_window_proc(HWND window,
                  UINT message,
                  WPARAM wparam,
                  LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        if (!win32_register_raw_input(window)) {
            std::fprintf(stderr,
                         "RegisterRawInputDevices failed: %lu\n",
                         GetLastError());
        }
        return 0;

    case WM_INPUT:
        win32_read_raw_input(reinterpret_cast<HRAWINPUT>(lparam));

        if (GET_RAWINPUT_CODE_WPARAM(wparam) == RIM_INPUT) {
            return DefWindowProcA(window, message, wparam, lparam);
        }
        return 0;

    case WM_INPUT_DEVICE_CHANGE:
        if (wparam == GIDC_ARRIVAL) {
            win32_print_hid_device_info(
                reinterpret_cast<HANDLE>(lparam));
        } else if (wparam == GIDC_REMOVAL) {
            HANDLE device = reinterpret_cast<HANDLE>(lparam);

            if (g_app.controller.device == device) {
                n64_controller_begin_report(&g_app.controller);
                g_app.controller.stick_x = 0.0f;
                g_app.controller.stick_y = 0.0f;
                n64_controller_update(&g_app.controller);
                n64_print_gamepad_events();

                n64_controller_init(&g_app.controller);
                g_app.last_printed_stick.x = 0.0f;
                g_app.last_printed_stick.y = 0.0f;
            }
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        g_app.window = nullptr;
        g_app.quit = true;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(window, message, wparam, lparam);
}

static bool
win32_create_window(void)
{
    WNDCLASSA window_class = {};

    window_class.hInstance = g_app.instance;
    window_class.lpfnWndProc = win32_window_proc;
    window_class.lpszClassName = window_class_name;
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

    if (RegisterClassA(&window_class)) {
        g_app.window_class_registered = true;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::fprintf(stderr,
                     "RegisterClassA failed: %lu\n",
                     GetLastError());
        return false;
    }

    g_app.window =
        CreateWindowExA(0,
                        window_class_name,
                        "N64 HID Mapping",
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                        CW_USEDEFAULT,
                        CW_USEDEFAULT,
                        640,
                        360,
                        nullptr,
                        nullptr,
                        g_app.instance,
                        nullptr);

    if (!g_app.window) {
        std::fprintf(stderr,
                     "CreateWindowExA failed: %lu\n",
                     GetLastError());
        return false;
    }

    ShowWindow(g_app.window, SW_SHOW);
    UpdateWindow(g_app.window);

    return true;
}

static void
win32_attach_console(void)
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }

    FILE *stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

static void
win32_shutdown(void)
{
    if (g_app.window) {
        DestroyWindow(g_app.window);
        g_app.window = nullptr;
    }

    if (g_app.window_class_registered) {
        UnregisterClassA(window_class_name, g_app.instance);
        g_app.window_class_registered = false;
    }
}

int APIENTRY
WinMain(HINSTANCE instance,
        HINSTANCE previous_instance,
        LPSTR command_line,
        int show_command)
{
    win32_attach_console();

    g_app.instance = instance;
    n64_controller_init(&g_app.controller);

    if (!win32_create_window()) {
        win32_shutdown();
        return 1;
    }

    while (!g_app.quit) {
        win32_process_pending_messages();
        Sleep(1);
    }

    win32_shutdown();

    return 0;
}
