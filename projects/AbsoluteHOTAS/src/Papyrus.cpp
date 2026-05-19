#include "Papyrus.h"
#include "AbsoluteGlobals.h"
#include "PapyrusNativeShim.h"
#include "RuntimePaths.h"
#include "ThrottleController.h"
#include "ThrottleHook.h"

#include <array>
#include <atomic>
#include <cstring>
#include <cstddef>
#include <format>
#include <new>
#include <string>
#include <string_view>

using namespace AbsoluteGlobals;

namespace Papyrus {
    namespace {
        void PapyrusLog(const std::string& msg) {
            RuntimePaths::AppendLog("[Papyrus]", msg);
        }

        struct StringEntry {
            StringEntry* left;
            union {
                std::uint32_t length;
                StringEntry* right;
            };
            volatile std::uint32_t refCount;
            std::uint8_t flags;
        };
        static_assert(sizeof(StringEntry) == 0x18);

        RE::BSFixedString MakePermanentFixedString(std::string_view a_value) {
            const auto length = std::min<std::size_t>(a_value.size(), 255);
            auto* const bytes = static_cast<std::byte*>(::operator new(sizeof(StringEntry) + length + 1));
            auto* const entry = reinterpret_cast<StringEntry*>(bytes);
            entry->left = nullptr;
            entry->length = static_cast<std::uint32_t>(length);
            entry->refCount = 0x40000000;
            entry->flags = 0;

            auto* const text = reinterpret_cast<char*>(entry + 1);
            std::memcpy(text, a_value.data(), length);
            text[length] = '\0';

            return RE::BSFixedString{ entry };
        }

        struct NativeVDescTable {
            void* entries{ nullptr };
            std::uint16_t paramCount{ 0 };
            std::uint16_t totalEntries{ 0 };
        };
        static_assert(sizeof(NativeVDescTable) == 0x10);

        class ZeroArgNativeFunction final : public RE::BSScript::IFunction {
        public:
            using Callback = void (*)();

            ZeroArgNativeFunction(std::string_view a_objectName, std::string_view a_functionName, Callback a_callback) :
                _callback(std::move(a_callback)) {
                _name = MakePermanentFixedString(a_functionName);
                _objectName = MakePermanentFixedString(a_objectName);
                _stateName = MakePermanentFixedString("");
                _docString = MakePermanentFixedString("");
                _sourceFilename = MakePermanentFixedString("");
                _returnType = RE::BSScript::TypeInfo::RawType::kNone;
            }

            static void* operator new(std::size_t a_size) { return ::operator new(a_size); }
            static void operator delete(void* a_ptr) noexcept { ::operator delete(a_ptr); }

            RE::BSFixedString& GetName() override { return _name; }
            RE::BSFixedString& GetObjectTypeName() override { return _objectName; }
            RE::BSFixedString& GetStateName() override { return _stateName; }

            RE::BSScript::TypeInfo* GetReturnType(RE::BSScript::TypeInfo* a_dst) override {
                *a_dst = RE::BSScript::TypeInfo::RawType::kNone;
                return a_dst;
            }

            std::uint64_t GetParamCount() override { return 0; }

            RE::BSScript::TypeInfo* GetParam(
                std::uint32_t,
                RE::BSFixedString* a_nameOut,
                RE::BSScript::TypeInfo* a_typeOut) override {
                (void)a_nameOut;
                if (a_typeOut) {
                    *a_typeOut = RE::BSScript::TypeInfo::RawType::kNone;
                }
                return a_typeOut;
            }

            std::uint64_t GetStackFrameSize() override { return 0; }
            bool GetIsNative() override { return true; }
            bool GetIsStatic() override { return true; }
            bool GetIsEmpty() override { return false; }
            FunctionType GetFunctionType() override { return FunctionType::kNormal; }
            std::uint32_t GetUserFlags() override { return 0; }
            RE::BSFixedString& GetDocString() override { return _docString; }
            void InsertLocals(std::uint32_t) override {}

            CallResult Call(
                const RE::BSTSmartPointer<RE::BSScript::Stack>& a_stack,
                RE::BSScript::ErrorLogger&,
                RE::BSScript::Internal::VirtualMachine&,
                RE::BSScript::StackFrame*) override {
                (void)a_stack;

                if (_callback) {
                    _callback();
                    return CallResult::kCompleted;
                }

                return CallResult::kFailedAbort;
            }

            RE::BSFixedString& GetSourceFilename() override { return _sourceFilename; }

            bool TranslateIPToLineNumber(std::uint32_t, std::uint32_t* r_lineNumber) override {
                if (r_lineNumber) {
                    *r_lineNumber = 0;
                }
                return false;
            }

            std::uint64_t* Unk_12(std::uint64_t* a_out) override {
                if (a_out) {
                    *a_out = 0;
                }
                return a_out;
            }

            Unk13* Unk_13(Unk13* a_out) override {
                if (a_out) {
                    a_out->unk00 = 0;
                    a_out->unk08 = 0;
                }
                return a_out;
            }

            bool GetVarNameForStackIndex(std::uint32_t, RE::BSFixedString&) override {
                return false;
            }

            void* Unk_15(std::uint64_t, std::uint64_t) override { return nullptr; }
            bool CanBeCalledFromTasklets() override { return _isCallableFromTasklet; }
            void SetCallableFromTasklets(bool a_taskletCallable) override { _isCallableFromTasklet = a_taskletCallable; }

            virtual bool HasStub() const { return _callback != nullptr; }

            virtual bool MarshallAndDispatch(
                RE::BSScript::Variable&,
                RE::BSScript::Internal::VirtualMachine&,
                std::uint32_t,
                RE::BSScript::Variable&,
                const RE::BSScript::StackFrame&) const {
                if (!_callback) {
                    return false;
                }

                _callback();
                return true;
            }

        public:
            RE::BSFixedString _name{};
            RE::BSFixedString _objectName{};
            RE::BSFixedString _stateName{};
            RE::BSScript::TypeInfo _returnType{};
            NativeVDescTable _params{};
            bool _isStatic{ true };
            bool _isCallableFromTasklet{ true };
            bool _isLatent{ false };
            std::uint32_t _userFlags{ 0 };
            RE::BSFixedString _docString{};

            RE::BSFixedString _sourceFilename{};
            Callback _callback;
        };
        static_assert(offsetof(ZeroArgNativeFunction, _name) == 0x10);
        static_assert(offsetof(ZeroArgNativeFunction, _objectName) == 0x18);
        static_assert(offsetof(ZeroArgNativeFunction, _stateName) == 0x20);
        static_assert(offsetof(ZeroArgNativeFunction, _returnType) == 0x28);
        static_assert(offsetof(ZeroArgNativeFunction, _params) == 0x30);
        static_assert(offsetof(ZeroArgNativeFunction, _isStatic) == 0x40);
        static_assert(offsetof(ZeroArgNativeFunction, _isCallableFromTasklet) == 0x41);
        static_assert(offsetof(ZeroArgNativeFunction, _isLatent) == 0x42);
        static_assert(offsetof(ZeroArgNativeFunction, _userFlags) == 0x44);
        static_assert(offsetof(ZeroArgNativeFunction, _docString) == 0x48);

        bool BindZeroArgNative(
            RE::BSScript::IVirtualMachine& a_vm,
            std::string_view a_objectName,
            std::string_view a_functionName,
            ZeroArgNativeFunction::Callback a_callback) {
            PapyrusLog("Constructing native function " + std::string(a_objectName) + "." + std::string(a_functionName) + ".");
            auto* nativeFunction = new ZeroArgNativeFunction(a_objectName, a_functionName, std::move(a_callback));
            PapyrusLog("Constructed native function " + std::string(a_objectName) + "." + std::string(a_functionName) + ".");
            const auto* const vtable = *reinterpret_cast<void* const* const*>(nativeFunction);
            PapyrusLog("Native function object=" + std::to_string(reinterpret_cast<std::uintptr_t>(nativeFunction)) +
                       " vtable=" + std::to_string(reinterpret_cast<std::uintptr_t>(vtable)));
            PapyrusLog("Binding native function " + std::string(a_objectName) + "." + std::string(a_functionName) + ".");
            if (!a_vm.BindNativeMethod(nativeFunction)) {
                PapyrusLog("Failed to bind native function " + std::string(a_objectName) + "." + std::string(a_functionName) + ".");
                return false;
            }

            PapyrusLog("Setting tasklet callable for " + std::string(a_objectName) + "." + std::string(a_functionName) + ".");
            a_vm.SetCallableFromTasklets(a_objectName.data(), a_functionName.data(), true);
            PapyrusLog("Bound native function " + std::string(a_objectName) + "." + std::string(a_functionName) + ".");
            return true;
        }
    }

    void SetPilotState(bool a_isPiloting) {
        const bool wasPiloting = g_isPilotState.exchange(a_isPiloting, std::memory_order_acq_rel);
        if (wasPiloting == a_isPiloting) {
            return;
        }

        if (a_isPiloting) {
            PapyrusLog("Pilot state entered.");
            if (ThrottleController::Initialize()) {
                ThrottleController::Start();
            } else {
                PapyrusLog("Controller startup skipped; AbsoluteHOTAS is disabled in config.");
            }
            return;
        }

        g_magicArmed = 0;
        g_isArmed = 0;
        g_lockedRDI = 0;
        g_lockedRCX = 0;
        g_capturedRDI = 0;
        g_capturedRCX = 0;
        ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
        ThrottleHook::SetSilenceEnabled(false);
        ThrottleHook::SetCaptureEnabled(false);
        ThrottleHook::ClearCandidates();
        PapyrusLog("Pilot state exited; pointers disarmed.");
    }

    void StartStateMonitor() {
        PapyrusLog("GlobalVariable monitor removed; using native Papyrus calls.");
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            PapyrusLog("RegisterFunctions failed: VM pointer is null.");
            return false;
        }

        PapyrusLog("RegisterFunctions entered.");
        const bool startBound = BindZeroArgNative(*a_vm, "AbsoluteHOTAS", "StartPilotMode", []() {
            PapyrusLog("Native StartPilotMode called.");
            SetPilotState(true);
        });
        const bool stopBound = BindZeroArgNative(*a_vm, "AbsoluteHOTAS", "StopPilotMode", []() {
            PapyrusLog("Native StopPilotMode called.");
            SetPilotState(false);
        });
        return startBound && stopBound;
    }
}
