#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/UnrealApi.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <climits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace resonance::unreal {
namespace {
// Steam build 24250053. These native views are enabled only after the
// executable hash and the game-thread metadata anchors have been verified.
constexpr std::uintptr_t name_find_rva = 0x1371d00;
constexpr std::uintptr_t name_to_string_rva = 0x1382f90;
constexpr std::uintptr_t allocator_rva = 0x9c95350;
constexpr std::uintptr_t object_chunks_rva = 0x9d84dd0;
constexpr std::uintptr_t process_event_rva = 0x1592e30;
constexpr std::size_t process_event_slot = 76;
constexpr std::size_t free_slot = 9;
constexpr std::size_t copy_slot = 30;
constexpr std::size_t destroy_slot = 39;
constexpr std::size_t initialize_slot = 42;
constexpr std::uint64_t cast_enum = 0x4, cast_struct = 0x8, cast_script_struct = 0x10, cast_class = 0x20;
constexpr std::uint64_t cast_function = 0x80000, cast_package = 0x400000000;
constexpr std::uint32_t unavailable_item_flags = 0xb0200000u;
constexpr std::uint32_t unavailable_object_flags = 0x40018000u;
constexpr std::size_t chunk_size = 65536;
constexpr int max_type_depth = 128;
constexpr int max_fields = 4096;

std::atomic<std::uintptr_t> image_base{};

template<class T, class Object>
T& member(Object* object, std::size_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

template<class Function, class Object>
Function virtual_function(Object* object, std::size_t slot) {
    return reinterpret_cast<Function>(member<void**>(object, 0)[slot]);
}

template<class T>
T image_address(std::uintptr_t rva) {
    const auto base = image_base.load(std::memory_order_acquire);
    return base ? reinterpret_cast<T>(base + rva) : nullptr;
}

std::uint64_t cast_flags(const UClass* type) {
    return type ? member<std::uint64_t>(type, 0xd8) : 0;
}

bool class_is(UObject* object, std::uint64_t kind) {
    return object && (cast_flags(object->GetClassPrivate()) & kind) != 0;
}

UStruct* struct_for(UObject* object) {
    if (class_is(object, cast_struct)) return static_cast<UStruct*>(object);
    return object->GetClassPrivate();
}

std::wstring object_path(UObject* object) {
    std::array<UObject*, 64> outers{};
    std::size_t depth = 0;
    for (auto* outer = object; outer; outer = outer->GetOuterPrivate()) {
        if (depth == outers.size()) return {};
        outers[depth++] = outer;
    }

    std::wstring result;
    while (depth) {
        auto* current = outers[--depth];
        if (!result.empty()) {
            auto* outer = current->GetOuterPrivate();
            const bool subobject = !class_is(outer, cast_package) &&
                class_is(outer->GetOuterPrivate(), cast_package);
            result.push_back(subobject ? L':' : L'.');
        }
        result += current->GetNamePrivate().ToString();
    }
    return result;
}

struct ObjectItem {
    UObject* object;
    std::uint32_t flags;
    std::int32_t cluster, serial, references;
};
static_assert(sizeof(ObjectItem) == 0x18);

struct ObjectChunks {
    ObjectItem** chunks;
    ObjectItem* preallocated;
    int capacity, count, chunk_capacity, chunk_count;
};
static_assert(sizeof(ObjectChunks) == 0x20);

const ObjectChunks* object_chunks() {
    const auto* table = image_address<const ObjectChunks*>(object_chunks_rva);
    if (!table || !table->chunks || table->count <= 0 || table->count > table->capacity ||
        table->chunk_count <= 0 || table->chunk_count > table->chunk_capacity ||
        static_cast<std::size_t>(table->count) > chunk_size * static_cast<std::size_t>(table->chunk_count))
        return nullptr;
    return table;
}

const ObjectItem* object_item(const ObjectChunks* table, int index) {
    if (!table || index < 0 || index >= table->count) return nullptr;
    const auto n = static_cast<std::size_t>(index);
    const auto* chunk = table->chunks[n / chunk_size];
    return chunk ? chunk + n % chunk_size : nullptr;
}

bool live_item(const ObjectItem* item) {
    return item && item->object && !(item->flags & unavailable_item_flags) &&
        !(static_cast<std::uint32_t>(item->object->GetObjectFlags()) & unavailable_object_flags);
}

bool registered(UObject* object) {
    if (!object) return false;
    const auto* item = object_item(object_chunks(), member<int>(object, 0xc));
    return live_item(item) && item->object == object;
}

template<class Visitor>
void visit_objects(const Visitor& visit) {
    const auto* table = object_chunks();
    if (!table) return;
    const auto count = static_cast<std::size_t>(table->count);
    for (std::size_t start = 0; start < count; start += chunk_size) {
        const auto* chunk = table->chunks[start / chunk_size];
        if (!chunk) continue;
        const auto entries = std::min(chunk_size, count - start);
        for (std::size_t i = 0; i < entries; ++i) {
            const auto* item = &chunk[i];
            if (live_item(item) && visit(item->object)) return;
        }
    }
}

struct IndexedObject {
    UObject* pointer;
    int index, serial;
    UObject* get() const {
        const auto* item = object_item(object_chunks(), index);
        return live_item(item) && item->object == pointer && item->serial == serial ? pointer : nullptr;
    }
};

using ClassIndex = std::unordered_map<UClass*, std::vector<UObject*>>;
thread_local ClassIndex class_index;
thread_local bool indexed{};
thread_local unsigned frame_depth{};
thread_local std::unordered_map<std::wstring, IndexedObject> named_objects;

const ClassIndex& indexed_classes() {
    if (!indexed) {
        visit_objects([](UObject* object) {
            if (auto* type = object->GetClassPrivate()) class_index[type].push_back(object);
            return false;
        });
        indexed = true;
    }
    return class_index;
}

bool type_matches(UClass* type, FName name) {
    auto* cls = static_cast<UStruct*>(type);
    for (int depth = 0; cls && depth < max_type_depth; ++depth, cls = cls->GetSuperStruct()) {
        if (cls->GetNamePrivate() == name) return true;
    }
    return false;
}

FProperty* first_property(UStruct* type) {
    return member<FProperty*>(type, 0x50);
}

FProperty* next_property(FProperty* property) {
    return member<FProperty*>(property, 0x18);
}

FProperty* own_property(UStruct* type, FName name) {
    auto* property = first_property(type);
    for (int n = 0; property && n < max_fields; ++n, property = next_property(property)) {
        if (member<FName>(property, 0x20) == name) return property;
    }
    return nullptr;
}

struct NativeString {
    wchar_t* data{};
    int count{}, capacity{};

    ~NativeString() { if (data) FMemory::Free(data); }
    std::wstring value() const {
        if (!data || count <= 0 || count > 65536 || capacity < count) return {};
        return std::wstring(data, static_cast<std::size_t>(count - (data[count - 1] == L'\0')));
    }
};
static_assert(sizeof(NativeString) == 16);

struct TextConversion {
    UObject* library{};
    UFunction* function{};
    FProperty* input{};
    FProperty* output{};
};

TextConversion text_conversion() {
    auto* library = UObjectGlobals::StaticFindObject(L"/Script/Engine.Default__KismetTextLibrary");
    if (!library) return {};
    auto* function = library->GetFunctionByNameInChain(L"Conv_TextToString");
    if (!function || !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400)) ||
        function->GetParmsSize() != 32 || function->GetReturnValueOffset() != 16 ||
        function->GetPropertiesSize() != 32) return {};
    auto* input = function->FindProperty(FName(L"InText", FNAME_Find));
    auto* output = function->GetReturnProperty();
    if (!input || !output || input->GetOffset_Internal() != 0 || input->GetSize() != 16 ||
        output->GetOffset_Internal() != 16 || output->GetSize() != 16 ||
        !input->field_class() || !output->field_class() ||
        input->field_class()->GetName() != L"TextProperty" || output->field_class()->GetName() != L"StrProperty")
        return {};
    return {library, function, input, output};
}
}

void bind_exact_image(std::uintptr_t image) {
    image_base.store(image, std::memory_order_release);
}

ObjectFrame::ObjectFrame() {
    if (frame_depth++ == 0) {
        class_index.clear();
        indexed = false;
    }
}

ObjectFrame::~ObjectFrame() {
    if (--frame_depth == 0) {
        class_index.clear();
        indexed = false;
    }
}

bool verify_live_bindings() {
    const ObjectFrame frame;
    auto* meta = UObjectGlobals::StaticFindObject(L"/Script/CoreUObject.Class");
    if (!meta || meta->GetClassPrivate() != meta || !class_is(meta, cast_class) ||
        meta->GetFullName() != L"Class /Script/CoreUObject.Class")
        return false;
    const auto conversion = text_conversion();
    if (!conversion.function || !registered(conversion.library) || !registered(conversion.function))
        return false;
    using Event = void(*)(UObject*, UFunction*, void*);
    return virtual_function<Event>(conversion.library, process_event_slot) ==
        image_address<Event>(process_event_rva);
}

FName::FName(const wchar_t* name, EFindName mode) {
    using Find = FName*(*)(FName*, const wchar_t*, int);
    if (const auto find = image_address<Find>(name_find_rva); find && name) find(this, name, mode);
}

std::wstring FName::ToString() const {
    using Convert = NativeString*(*)(const FName*, NativeString*);
    const auto convert = image_address<Convert>(name_to_string_rva);
    if (!convert) return {};

    thread_local std::unordered_map<std::uint64_t, std::wstring> cache;
    const std::uint64_t key = (static_cast<std::uint64_t>(Number) << 32) | ComparisonIndex;
    if (const auto found = cache.find(key); found != cache.end()) return found->second;

    NativeString result;
    convert(this, &result);
    auto name = result.value();
    if (cache.size() >= 8192) cache.clear();
    cache.emplace(key, name);
    return name;
}

UClass*& UObjectBase::GetClassPrivate() { return member<UClass*>(this, 0x10); }
UObject*& UObjectBase::GetOuterPrivate() { return member<UObject*>(this, 0x20); }
EObjectFlags& UObjectBase::GetObjectFlags() { return member<EObjectFlags>(this, 8); }
FName& UObjectBase::GetNamePrivate() { return member<FName>(this, 0x18); }

std::wstring UObject::GetFullName() const {
    auto* type = member<UClass*>(this, 0x10);
    if (!type) return {};
    auto path = object_path(const_cast<UObject*>(this));
    if (path.empty()) return {};
    return type->GetNamePrivate().ToString() + L" " + path;
}

UFunction* UObject::GetFunctionByNameInChain(const wchar_t* name) {
    const FName field(name, FNAME_Find);
    if (field == FName{}) return nullptr;
    auto* cls = struct_for(this);
    for (int depth = 0; cls && depth < max_type_depth; ++depth, cls = cls->GetSuperStruct()) {
        auto* child = member<UField*>(cls, 0x48);
        for (int n = 0; child && n < max_fields; ++n, child = member<UField*>(child, 0x28)) {
            if (child->GetNamePrivate() == field && class_is(child, cast_function))
                return static_cast<UFunction*>(child);
        }
    }
    return nullptr;
}

FProperty* UObject::GetPropertyByNameInChain(const wchar_t* name) {
    auto* type = struct_for(this);
    const FName field(name, FNAME_Find);
    return type && field != FName{} ? type->FindProperty(field) : nullptr;
}

void* UObject::GetValuePtrByPropertyNameInChain(const wchar_t* name) {
    auto* property = GetPropertyByNameInChain(name);
    if (!property || property->GetOffset_Internal() < 0) return nullptr;
    return &member<std::byte>(this, static_cast<std::size_t>(property->GetOffset_Internal()));
}

void UObject::ProcessEvent(UFunction* function, void* params) {
    using Event = void(*)(UObject*, UFunction*, void*);
    virtual_function<Event>(this, process_event_slot)(this, function, params);
}

UStruct*& UStruct::GetSuperStruct() { return member<UStruct*>(this, 0x40); }
int& UStruct::GetPropertiesSize() { return member<int>(this, 0x58); }

FProperty* UStruct::FindProperty(FName name) {
    auto* type = this;
    for (int depth = 0; type && depth < max_type_depth; ++depth, type = type->GetSuperStruct()) {
        if (auto* property = own_property(type, name)) return property;
    }
    return nullptr;
}

unsigned short& UFunction::GetParmsSize() { return member<unsigned short>(this, 0xb6); }
unsigned short& UFunction::GetReturnValueOffset() { return member<unsigned short>(this, 0xb8); }

FProperty* UFunction::GetReturnProperty() {
    auto* property = first_property(this);
    for (int n = 0; property && n < max_fields; ++n, property = next_property(property)) {
        if (property->HasAnyPropertyFlags(0x400)) return property;
    }
    return nullptr;
}

bool UFunction::HasAnyFunctionFlags(EFunctionFlags flags) {
    return (member<std::uint32_t>(this, 0xb0) & static_cast<std::uint32_t>(flags)) != 0;
}

std::wstring FFieldClass::GetName() const { return member<FName>(this, 0).ToString(); }
FFieldClass* FField::field_class() { return member<FFieldClass*>(this, 8); }
int& FProperty::GetOffset_Internal() { return member<int>(this, 0x44); }

int FProperty::GetSize() const {
    const auto dim = member<int>(this, 0x30);
    const auto element = member<int>(this, 0x34);
    if (dim <= 0 || element <= 0 || dim > INT_MAX / element) return 0;
    return dim * element;
}

bool FProperty::HasAnyPropertyFlags(std::uint64_t flags) const {
    return (member<std::uint64_t>(this, 0x38) & flags) != 0;
}

void FProperty::InitializeValue(void* destination) const {
    if (HasAnyPropertyFlags(0x200)) {
        std::memset(destination, 0, GetSize());
    } else {
        using Initialize = void(*)(const FProperty*, void*);
        virtual_function<Initialize>(this, initialize_slot)(this, destination);
    }
}

void FProperty::DestroyValue(void* destination) const {
    if (!HasAnyPropertyFlags(0x1000000000)) {
        using Destroy = void(*)(const FProperty*, void*);
        virtual_function<Destroy>(this, destroy_slot)(this, destination);
    }
}

void FProperty::CopyCompleteValue(void* destination, const void* source) const {
    if (destination == source) return;
    if (HasAnyPropertyFlags(0x40000000)) {
        std::memcpy(destination, source, GetSize());
    } else {
        using Copy = void(*)(const FProperty*, void*, const void*, int);
        virtual_function<Copy>(this, copy_slot)(this, destination, source, member<int>(this, 0x30));
    }
}

TObjectPtr<UScriptStruct>& FStructProperty::GetStruct() { return member<TObjectPtr<UScriptStruct>>(this, 0x70); }
FProperty*& FArrayProperty::GetInner() { return member<FProperty*>(this, 0x78); }

UObject* FWeakObjectPtr::Get() const {
    const auto serial = member<int>(this, 4);
    if (!serial) return nullptr;
    const auto* item = object_item(object_chunks(), member<int>(this, 0));
    return live_item(item) && serial == item->serial ? item->object : nullptr;
}

std::wstring FText::ToString() const {
    if (!member<void*>(this, 0)) return {};
    const auto conversion = text_conversion();
    if (!conversion.function) return {};

    alignas(16) std::array<std::byte, 32> params{};
    // The reflected input is a const reference and stays alive for this
    // synchronous call. The resulting FString owns its allocation.
    std::memcpy(params.data(), this, 16);
    conversion.library->ProcessEvent(conversion.function, params.data());
    NativeString output;
    std::memcpy(&output.data, params.data() + 16, sizeof output.data);
    std::memcpy(&output.count, params.data() + 24, sizeof output.count);
    std::memcpy(&output.capacity, params.data() + 28, sizeof output.capacity);
    return output.value();
}

void FMemory::Free(void* pointer) {
    auto* allocator_slot = image_address<void**>(allocator_rva);
    if (pointer && allocator_slot && *allocator_slot) {
        using Free = void(*)(void*, void*);
        virtual_function<Free>(*allocator_slot, free_slot)(*allocator_slot, pointer);
    }
}

namespace UObjectGlobals {
UObject* StaticFindObject(const wchar_t* name) {
    if (!name || !*name) return nullptr;
    const std::wstring key(name);
    if (auto found = named_objects.find(key); found != named_objects.end()) {
        if (auto* object = found->second.get(); object && object_path(object) == key) return object;
        named_objects.erase(found);
    }

    const auto split = key.find_last_of(L".:");
    const FName leaf(split == std::wstring::npos ? key.c_str() : key.c_str() + split + 1, FNAME_Find);
    if (leaf == FName{}) return nullptr;

    UObject* match = nullptr;
    visit_objects([&](UObject* candidate) {
        if (candidate->GetNamePrivate() != leaf || object_path(candidate) != key) return false;
        match = candidate;
        return true;
    });
    if (match) {
        const auto index = member<int>(match, 0xc);
        const auto* item = object_item(object_chunks(), index);
        if (item && item->object == match) named_objects.emplace(key, IndexedObject{match, index, item->serial});
    }
    return match;
}

void FindAllOf(const wchar_t* name, std::vector<UObject*>& result) {
    const FName wanted(name, FNAME_Find);
    if (wanted == FName{}) return;
    const ObjectFrame frame;
    const auto first = result.size();
    for (const auto& [type, objects] : indexed_classes()) {
        if (!type_matches(type, wanted)) continue;
        for (auto* object : objects) {
            if (IsValidObjectForFindXOf(object)) result.push_back(object);
        }
    }
    std::sort(result.begin() + static_cast<std::ptrdiff_t>(first), result.end(), [](UObject* a, UObject* b) {
        return member<int>(a, 0xc) < member<int>(b, 0xc);
    });
}

bool IsValidObjectForFindXOf(UObject* object) {
    if (!registered(object) || !object->GetClassPrivate()) return false;
    constexpr auto metadata = cast_class | cast_function | cast_package | cast_enum | cast_script_struct;
    return !class_is(object, metadata);
}
}
}
