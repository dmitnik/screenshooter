#include "app/application.h"
#include "platform/com_apartment.h"

#include <windows.h>

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int show_command
) {
    const screenshooter::platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    if (FAILED(apartment.result())) {
        return 1;
    }

    screenshooter::app::Application application(instance);
    return application.Run(show_command);
}
