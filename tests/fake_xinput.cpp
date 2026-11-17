#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <array>
#include <mutex>
namespace {std::mutex mutex;std::array<XINPUT_STATE,4> states{};std::array<bool,4> connected{};}
extern "C" __declspec(noinline) DWORD WINAPI fake_get_state(DWORD index,XINPUT_STATE* result) {
    std::lock_guard lock(mutex);
    if(index>=states.size()||!connected[index])return ERROR_DEVICE_NOT_CONNECTED;
    if(!result)return ERROR_BAD_ARGUMENTS;*result=states[index];return ERROR_SUCCESS;
}
extern "C" void fake_set_state(DWORD index,BOOL present,const XINPUT_STATE* value) {
    std::lock_guard lock(mutex);if(index>=states.size())return;connected[index]=present!=FALSE;
    states[index]=value?*value:XINPUT_STATE{};
}
