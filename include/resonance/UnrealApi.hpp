#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace resonance::unreal {
class UClass;
class UObject;
class UFunction;
class FProperty;
class UScriptStruct;
template<class T> struct TObjectPtr { T* pointer; };

// Opaque native views for the supported shipping executable. Only FName is
// constructed by the reader. The other views refer to live game storage.
enum EFindName { FNAME_Find = 0 };
class FName {
public:
    FName() = default;
    FName(const wchar_t*, EFindName);
    std::wstring ToString() const;
    bool operator==(const FName&) const = default;
    std::uint32_t ComparisonIndex{}, Number{};
};
static_assert(sizeof(FName) == 8);

enum class EObjectFlags : std::uint32_t {};
class UObjectBase {
public:
    UClass*& GetClassPrivate();
    UObject*& GetOuterPrivate();
    EObjectFlags& GetObjectFlags();
    FName& GetNamePrivate();
};
class UObject : public UObjectBase {
public:
    std::wstring GetFullName() const;
    UFunction* GetFunctionByNameInChain(const wchar_t*);
    FProperty* GetPropertyByNameInChain(const wchar_t*);
    void* GetValuePtrByPropertyNameInChain(const wchar_t*);
    void ProcessEvent(UFunction*, void*);
};
class UField : public UObject {};
class UStruct : public UField {
public:
    UStruct*& GetSuperStruct();
    int& GetPropertiesSize();
    FProperty* FindProperty(FName);
};
class UClass : public UStruct {};
class UScriptStruct : public UStruct {};
enum class EFunctionFlags : std::uint32_t {};
class UFunction : public UStruct {
public:
    unsigned short& GetParmsSize();
    unsigned short& GetReturnValueOffset();
    FProperty* GetReturnProperty();
    bool HasAnyFunctionFlags(EFunctionFlags);
};

class FFieldClass { public: std::wstring GetName() const; };
class FField { public: FFieldClass* field_class(); };
class FProperty : public FField {
public:
    int& GetOffset_Internal();
    int GetSize() const;
    void InitializeValue(void*) const;
    void DestroyValue(void*) const;
    void CopyCompleteValue(void*, const void*) const;
    bool HasAnyPropertyFlags(std::uint64_t) const;
};
class FStructProperty : public FProperty {
public:
    TObjectPtr<UScriptStruct>& GetStruct();
};
class FArrayProperty : public FProperty {
public:
    FProperty*& GetInner();
};
class FWeakObjectPtr { public: UObject* Get() const; };
class FText { public: std::wstring ToString() const; };
class FMemory { public: static void Free(void*); };
class UEngine;

namespace UObjectGlobals {
    UObject* StaticFindObject(const wchar_t*);
    void FindAllOf(const wchar_t*, std::vector<UObject*>&);
    bool IsValidObjectForFindXOf(UObject*);
}

// Bind only after the executable digest matches. The snapshot is refreshed on
// the game thread and is usable only within a post-tick reader pass.
void bind_exact_image(std::uintptr_t image);
bool verify_live_bindings();
class ObjectFrame {
public:
    ObjectFrame();
    ~ObjectFrame();
    ObjectFrame(const ObjectFrame&) = delete;
    ObjectFrame& operator=(const ObjectFrame&) = delete;
};
}
