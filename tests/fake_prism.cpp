// External-backend fixture for testing the real Speech worker without restarting NVDA.
#pragma warning(disable:4309)
#include <prism.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
namespace {
std::atomic<bool> available{false}, fail_next{false}, speaking{false}, speaking_supported{true};
std::mutex guard;
std::string output, interruptions;
std::atomic<unsigned> speaking_queries{};
std::atomic<unsigned> stops{};
int context_token{},backend_token{};
}
extern "C" {
__declspec(dllexport) void test_set_available(bool state) {available=state;}
__declspec(dllexport) void test_fail_next() {fail_next=true;}
__declspec(dllexport) void test_speaking(bool state) {speaking=state;}
__declspec(dllexport) void test_speaking_supported(bool state) {speaking_supported=state;}
__declspec(dllexport) unsigned test_speaking_queries() {return speaking_queries.load();}
__declspec(dllexport) unsigned test_stops() {return stops.load();}
__declspec(dllexport) void test_interruptions(char* buffer,size_t length) {std::lock_guard lock(guard);strncpy_s(buffer,length,interruptions.c_str(),_TRUNCATE);}
__declspec(dllexport) void test_output(char* buffer,size_t length) {std::lock_guard lock(guard);strncpy_s(buffer,length,output.c_str(),_TRUNCATE);}
PrismConfig PRISM_CALL prism_config_init() {return {};}
PrismContext* PRISM_CALL prism_init(PrismConfig*) {return reinterpret_cast<PrismContext*>(&context_token);}
void PRISM_CALL prism_shutdown(PrismContext*) {}
PrismBackend* PRISM_CALL prism_registry_acquire_best(PrismContext*) {return available?reinterpret_cast<PrismBackend*>(&backend_token):nullptr;}
const char* PRISM_CALL prism_backend_name(PrismBackend*) {return "External test backend";}
PrismError PRISM_CALL prism_backend_initialize(PrismBackend*) {return PRISM_ERROR_ALREADY_INITIALIZED;}
PrismError PRISM_CALL prism_backend_speak(PrismBackend*,const char* text,bool interrupt) {
    if(!available || fail_next.exchange(false)) {available=false;return PRISM_ERROR_SPEAK_FAILURE;}
    std::lock_guard lock(guard);
    output+=text;output+='\n';
    if(interrupt){interruptions+=text;interruptions+='\n';}
    return PRISM_OK;
}
PrismError PRISM_CALL prism_backend_is_speaking(PrismBackend*,bool* out_speaking) {
    ++speaking_queries;
    if(!speaking_supported)return PRISM_ERROR_NOT_IMPLEMENTED;
    *out_speaking=speaking;
    return PRISM_OK;
}
PrismError PRISM_CALL prism_backend_stop(PrismBackend*) {++stops;speaking=false;return PRISM_OK;}
void PRISM_CALL prism_backend_free(PrismBackend*) {}
}
