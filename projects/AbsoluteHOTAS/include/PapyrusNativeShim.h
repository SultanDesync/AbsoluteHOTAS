#pragma once

#include <cstddef>
#include <cstdint>

namespace RE {
    struct BSFixedString {
        void* data{};
    };
    static_assert(sizeof(BSFixedString) == 0x8);

    template <class T>
    class BSTSmartPointer {
    public:
        T* ptr{};
    };
    static_assert(sizeof(BSTSmartPointer<void>) == 0x8);

    namespace BSScript {
        class ErrorLogger;
        class Stack;

        namespace Internal {
            class VirtualMachine;
        }

        class TypeInfo {
        public:
            enum class RawType : std::uint32_t {
                kNone,
                kObject,
                kString,
                kInt,
                kFloat,
                kBool,
                kVar,
                kStruct,
                kArrayStart = 10,
                kArrayObject,
                kArrayString,
                kArrayInt,
                kArrayFloat,
                kArrayBool,
                kArrayVar,
                kArrayStruct,
                kArrayEnd
            };

            TypeInfo& operator=(RawType a_type) noexcept {
                rawType = static_cast<std::uintptr_t>(a_type);
                return *this;
            }

            std::uintptr_t rawType{};
        };
        static_assert(sizeof(TypeInfo) == 0x8);

        class Variable {
        public:
            TypeInfo varType{};
            union {
                void* object;
                void* string;
                std::uint32_t unsignedInt;
                std::int32_t signedInt;
                float floatingPoint;
                bool boolean;
                Variable* variable;
                void* structObject;
                void* arrayObject;
            } value{};
        };
        static_assert(sizeof(Variable) == 0x10);

        class StackFrame {
        public:
            Stack* parent;
            StackFrame* previousFrame;
            void* owningFunction;
            void* owningObjectType;
            Variable self;
            std::uint32_t index;
            std::uint32_t ip;
            std::uint32_t size;
            bool instructionsValid;
        };
        static_assert(sizeof(StackFrame) == 0x40);

        class IFunction {
        public:
            IFunction() = default;
            virtual ~IFunction() = default;

            enum class CallResult : std::uint32_t {
                kCompleted,
                kSetupForVM,
                kInProgress,
                kFailedRetry,
                kFailedAbort
            };

            enum class FunctionType : std::uint32_t {
                kNormal,
                kPropertyGetter,
                kPropertySetter
            };

            struct Unk13 {
                std::uint64_t unk00;
                std::uint32_t unk08;
            };

            virtual BSFixedString& GetName() = 0;
            virtual BSFixedString& GetObjectTypeName() = 0;
            virtual BSFixedString& GetStateName() = 0;
            virtual TypeInfo* GetReturnType(TypeInfo* a_dst) = 0;
            virtual std::uint64_t GetParamCount() = 0;
            virtual TypeInfo* GetParam(std::uint32_t a_idx, BSFixedString* a_nameOut, TypeInfo* a_typeOut) = 0;
            virtual std::uint64_t GetStackFrameSize() = 0;
            virtual bool GetIsNative() = 0;
            virtual bool GetIsStatic() = 0;
            virtual bool GetIsEmpty() = 0;
            virtual FunctionType GetFunctionType() = 0;
            virtual std::uint32_t GetUserFlags() = 0;
            virtual BSFixedString& GetDocString() = 0;
            virtual void InsertLocals(std::uint32_t a_frame) = 0;
            virtual CallResult Call(
                const BSTSmartPointer<Stack>& a_stack,
                ErrorLogger& a_errorLogger,
                Internal::VirtualMachine& a_vm,
                StackFrame* a_frame) = 0;
            virtual BSFixedString& GetSourceFilename() = 0;
            virtual bool TranslateIPToLineNumber(std::uint32_t a_instructionPointer, std::uint32_t* r_lineNumber) = 0;
            virtual std::uint64_t* Unk_12(std::uint64_t* a_out) = 0;
            virtual Unk13* Unk_13(Unk13* a_out) = 0;
            virtual bool GetVarNameForStackIndex(std::uint32_t a_idx, BSFixedString& a_variableName) = 0;
            virtual void* Unk_15(std::uint64_t a_arg0, std::uint64_t a_arg1) = 0;
            virtual bool CanBeCalledFromTasklets() = 0;
            virtual void SetCallableFromTasklets(bool a_taskletCallable) = 0;

        protected:
            mutable volatile std::uint32_t refCount{};
        };
        static_assert(sizeof(IFunction) == 0x10);

        class IVirtualMachine {
        public:
            virtual ~IVirtualMachine() = default;
            virtual void Unk_01() = 0;
            virtual void Unk_02() = 0;
            virtual void Unk_03() = 0;
            virtual void Unk_04() = 0;
            virtual void Unk_05() = 0;
            virtual void Unk_06() = 0;
            virtual void Unk_07() = 0;
            virtual void Unk_08() = 0;
            virtual void Unk_09() = 0;
            virtual void Unk_0A() = 0;
            virtual void Unk_0B() = 0;
            virtual void Unk_0C() = 0;
            virtual void Unk_0D() = 0;
            virtual void Unk_0E() = 0;
            virtual void Unk_0F() = 0;
            virtual void Unk_10() = 0;
            virtual void Unk_11() = 0;
            virtual void Unk_12() = 0;
            virtual void Unk_13() = 0;
            virtual void Unk_14() = 0;
            virtual void Unk_15() = 0;
            virtual void Unk_16() = 0;
            virtual void Unk_17() = 0;
            virtual void Unk_18() = 0;
            virtual void Unk_19() = 0;
            virtual void Unk_1A() = 0;
            virtual void Unk_1B() = 0;
            virtual void Unk_1C() = 0;
            virtual void Unk_1D() = 0;
            virtual bool BindNativeMethod(IFunction* a_function) = 0;
            virtual void SetCallableFromTasklets(const char* a_objectName, const char* a_functionName, bool a_taskletCallable) = 0;
            virtual void SetCallableFromTasklets(const char* a_objectName, const char* a_stateName, const char* a_functionName, bool a_taskletCallable) = 0;

        protected:
            mutable volatile std::uint32_t refCount{};
        };
        static_assert(sizeof(IVirtualMachine) == 0x10);
    }
}
