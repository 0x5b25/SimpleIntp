#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <queue>
#include <string>
#include <variant>
#include <chrono>

//#include "Utils.h"
#include <iostream>

#include "Library.h"
#include "GC.h"

#ifdef NDEBUG
    #define DEL_POINTER(ptr) (delete (ptr))
#else
    #define DEL_POINTER(ptr) \
        do{\
            delete ptr; ptr = nullptr;\
        }while (0)
#endif

class ValueType
{
public:
    static ValueType nullValue;
    union {
        std::int32_t value;
        float fval;
        void* obj;
    } data;

    TypeTable* type;

    bool IsRef() const
    {
        return type != nullptr && type->IsReferenceType() && data.obj != nullptr;
    }
    bool IsNull() const {
        return type == nullptr ||
            (type->IsReferenceType() && data.obj == nullptr);
    }

    ValueType() { type = nullptr; memset(&data, 0, sizeof(data)); }
    ValueType(TypeTable* type) :type(type) {

        {
            memset(&data, 0, sizeof(data));
        }
    }

    ValueType(const ValueType& other) {
        data = other.data;
        type = other.type;

    }
    //ValueType(ValueType&& other)
    //{
    //    data = (other.data);
    //    type = (other.type);
    //    other.type = nullptr; memset(&(other.data), 0, sizeof(data));
    //}
    ~ValueType()
    {

        //type = nullptr;
    }


    ValueType& operator=(const ValueType& other) {

        data = other.data;
        type = other.type;

        return *this;
    }

    //ValueType& operator=(ValueType&& other) {
    //    data = (other.data);
    //    type = (other.type);
    //    other.type = nullptr; memset(&(other.data), 0, sizeof(data));
    //    return *this;
    //}
};

class ExternalRefReg;
class ExternalRefBase
{
public:
    int refCnt = 0;
    int idx;
    ValueType handle;
    ExternalRefReg* reg;

    ExternalRefBase(
        ExternalRefReg* reg,
        int idx,
        const ValueType& val
    ):idx(idx), handle(val), reg(reg)
    {}

    void Unref();

    void Ref()
    {
        refCnt++;
    }
};

class ExtRef
{
    friend class ExternalRefReg;
    ExternalRefBase *base = nullptr;
    ExtRef(ExternalRefBase* base) :base(base) { base->Ref(); }

public:

    ExtRef() :base(nullptr){}

    //Cuz handle value changes during gc moving, we return pointer to handle
    //to ensure handle value is always valid
    const ValueType* get() const { return base == nullptr?nullptr:&(base->handle); }
    ValueType* get() { return base == nullptr ? nullptr : &(base->handle); }

    ExtRef(const ExtRef& other):ExtRef() { *this = other; }
    ExtRef(ExtRef&& other) :ExtRef() {*this = std::move(other);}

    ExtRef& operator=(const ExtRef& other)
    {
        if(base !=nullptr)
        {
            base->Unref();
        }
        base = other.base;
        if(base != nullptr){
            base-> Ref();
        }
        return *this;
    }

    ExtRef& operator=(ExtRef&& other)
    {
        if (base != nullptr)
        {
            base->Unref();
        }
        base = other.base;
        other.base = nullptr;
        return *this;
    }

    void Reset()
    {
        if (base != nullptr)
        {
            base->Unref();
        }
        base = nullptr;
    }

    ~ExtRef()
    {
        Reset();
    }
};


class ExternalRefReg
{
public:
    std::vector<std::unique_ptr<ExternalRefBase>> refs;

    
    ExtRef NewExtRef(const ValueType& val)
    {
        refs.emplace_back(new ExternalRefBase(this, refs.size(), val));
        return ExtRef(refs.back().get());
    }

    void RemoveRef(std::size_t idx)
    {
        //swap with back
        if(refs.size() > idx + 1)
        {
            std::swap(refs[idx], refs.back());
            refs[idx]->idx = idx;
        }
        refs.pop_back();
    }

    ~ExternalRefReg()
    {
        assert(refs.empty());
    }

};


class StaticFields;
struct CallStackFrame;
class TypeTable;

//union DataType { int32_t i; float f; void* p; ObjectInfo* inf;};



//Object information for "Type" object, really ment for a dummy type to
//Check if the type is a "Type"
class TypeObjInfo:public TypeTable
{
public:
    TypeObjInfo() { name = "Type"; isReferenceType = false; }
    virtual ~TypeObjInfo() {}
};

//Type for engine created integers
class IntpObjInfo :public TypeTable
{
public:
    IntpObjInfo() { name = "Engine type"; isReferenceType = false; }
    virtual ~IntpObjInfo() {}
};

//Type representing a closure
class ClosureObjInfo :public TypeTable
{
public:
    ClosureObjInfo(){ name = "Closure"; isReferenceType = true; fields = {nullptr, nullptr};}
    virtual ~ClosureObjInfo(){}
};


class StaticFields
{
public:
    StaticFields(TypeTable* type):type(type){}

    StaticFields(StaticFields&&) = default;
    StaticFields& operator=(StaticFields&& other) = default;

    TypeTable* type = nullptr;

    std::vector<ValueType> memberVars;
};

//We need a dummy instruction to load function handles from strings
//Also a specific type for function handles
//LDFN #name type [opcode]{[char][char]...[null]} -> function info



struct CallStackFrame
{
    //Local stack frame base
    //And caller position
    int sp;
    IL* ip;
    ValueType currEnv;
    MethodBlock* currentFn;

    CallStackFrame(
        int sp, 
        IL* ip, 
        const ValueType& env,
        MethodBlock* fn
    ):
        sp(sp), 
        ip(ip), 
        currEnv(env),
        currentFn(fn)
        {}

    CallStackFrame(const CallStackFrame& other) = default;
    CallStackFrame(CallStackFrame&& other) = default;
};


class Interpreter
{

    static TypeObjInfo _typeObjInfo;
    static ClosureObjInfo _closureObjInfo;
    static IntpObjInfo _intpObjInfo;

public:
    enum class ExecutionStatus{Finished, Halted, Error, Running} status;
    enum class ErrorCode
    {
        None,
        UnknownOpcode,
        LocalAddrOutOfRange,
        GlobalAddrOutOfRange,
        PopOnEmptyStack,
        NotEnoughArgument,
        JumpAddrOutOfRange,
        RuntimeError,
    } error;

    std::string errMsg;

    /*Environment management*/
    //Make sure the library is destroyed last
    LibraryLoader libLoader;

    GarbageCollector gc;
    std::vector<ValueType> valueStack;
    std::vector<CallStackFrame> callStack;

    //std::vector<std::unique_ptr<StaticFields>> heap;
    //std::queue<int> freeIdx;
    ExternalRefReg extRefs;
    std::unordered_map<TypeTable*, ValueType> staticPool;

    IL* ip;

    int gcManagedFreq = 2, gcMajorHeapFreq = 2, gcMatureGen = 2;
    //int gcTick = 0, gcCurrTick = 0;
    void NotifyGC()
    {
        //gcCurrTick++;
        //if(gcCurrTick % gcTick == 0)
        {
            GC_SweepManaged();
            GC_SweepHeap();
        }
    }

    std::vector<ValueType*> FindRoot(bool fullSweep)
    {
        std::vector<ValueType*> roots;
        //static fields:
        for(auto& kv : staticPool)
        {
            auto cb = GetCtrlBlk(kv.second.data.obj);
            if(cb->isInNursery | fullSweep)
                roots.push_back(&kv.second);
        }

        //Value stack
        for(auto& v : valueStack)
        {
            if(!v.IsRef()) continue;
            auto cb = GetCtrlBlk(v.data.obj);
            if (cb->isInNursery | fullSweep)
                roots.push_back(&v);
        }

        //Call stack

        for(auto& s : callStack)
        {
            auto& v = s.currEnv;
            if (!v.IsRef()) continue;
            auto cb = GetCtrlBlk(v.data.obj);
            if (cb->isInNursery | fullSweep)
                roots.push_back(&v);
        }

        //External references
        for(auto& e : extRefs.refs)
        {
            auto& v = e->handle;
            if (!v.IsRef()) continue;
            auto cb = GetCtrlBlk(v.data.obj);
            if (cb->isInNursery | fullSweep)
                roots.push_back(&v);
        }

        return roots;
    }

    std::chrono::steady_clock::time_point prevPrtTime = std::chrono::steady_clock::now();

    int gcManTh = 256;//1024*1024;
    void GC_SweepManaged()
    {
        if(gc.allocManaged < gcManTh) return;
        //Mark roots
        auto roots = FindRoot(false);
        gc.SweepManaged(roots);

        //auto curTime = std::chrono::steady_clock::now();
        //auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - prevPrtTime);
        //prevPrtTime = curTime;
        //std::cout << "[ +"<< delta.count() << "ms ]>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << std::endl;
        //std::cout << "New object space delta:" << gcManTh - gc.allocManaged << "bytes" << std::endl;

        gcManTh = gc.allocManaged * gcManagedFreq;
        //std::cout<< "NewObj threshold:" << gcManTh << "bytes" <<std::endl;
        //std::cout << gc.PrintAllocStat();
    }

    int gcHeapTh = 1024;//1024 * 1024 * 4;
    void GC_SweepHeap()
    {
        if (gc.allocMajorHeap < gcHeapTh) return;
        auto roots = FindRoot(true);
        gc.SweepMajorHeap(roots);

        //auto curTime = std::chrono::steady_clock::now();
        //auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - prevPrtTime);
        //prevPrtTime = curTime;
        //std::cout << "[ +" << delta.count() << "ms ]>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << std::endl;
        //std::cout << "Heap object space delta:" << gcHeapTh - gc.allocMajorHeap << "bytes" << std::endl;

        gcHeapTh = gc.allocMajorHeap * gcMajorHeapFreq;

        //std::cout << "NewHeap threshold:" << gcHeapTh << "bytes" << std::endl;
        //std::cout << gc.PrintAllocStat();
    }

    ValueType NewRefTypeObject(TypeTable* ty)
    {
        assert(ty->IsReferenceType());
        //TODO: run scheduled GC tasks
        NotifyGC();

        if (ty == nullptr) return nullptr;
        int fieldCnt = ty->fields.size();

        auto inst = gc.AllocateRawObject(fieldCnt);
        auto cb = GetCtrlBlk(inst);
        cb->vptr = ty;
        cb->debugInfo = "Instance of " + ty->name;
        ValueType hndl(ty);
        hndl.data.obj = inst;

        for (std::size_t i = 0; i < ty->fields.size(); i++)
        {
            auto typeInfo = ty->fields[i];
            new (inst + i)ValueType(typeInfo);
        }
        return hndl;
    }


    template<typename T, typename ...ArgTypes>
    T* NewExtTypeObject(ArgTypes... args)
    {
        NotifyGC();
        auto inst = gc.AllocateObject<T, ArgTypes...>(args...);
        return (T*)(inst);
    }

    
    ValueType& FindStaticFields(TypeTable* typeInfo) {
        //static ValueType nullEnv;
        //value types don't have static fields
        //if(!typeInfo->IsReferenceType()) return nullEnv;
        auto iter = staticPool.find(typeInfo);
        if(iter != staticPool.end())
            return ((*iter).second);

        //Not found. maybe first time use of type?
        auto& staticFields = typeInfo->staticFields;
        //Allocate object
        auto inst = gc.AllocateRawObject(staticFields.size());
        auto cb = GetCtrlBlk(inst);
        cb->vptr = typeInfo;
        cb->debugInfo = "StaticFields of "+typeInfo->name;
        ValueType staticFieldEnv(typeInfo);
        staticFieldEnv.data.obj = inst;

        for(std::size_t i = 0; i < staticFields.size();i++)
        {
            auto typeInfo = staticFields[i];
            new (inst + i)ValueType(typeInfo);
        }

        staticPool[typeInfo] =(staticFieldEnv);

        return staticPool[typeInfo];
    }

    

public:
    Interpreter():
        status(ExecutionStatus::Finished),
        error(ErrorCode::None),
        ip(nullptr)
    {
        gc.matureGen = gcMatureGen;
    }

public:
#undef LoadLibrary
    void LoadLibrary(std::shared_ptr<LibraryInfo> lib)
    {
        libLoader.libs.push_back(lib);
    }


    class Closure
    {
    public:

        ExtRef closureObjRef;

        Closure(ExtRef&& obj):closureObjRef(obj)
        {
            //assert(closureObjRef!=nullptr);
        }

        Closure():closureObjRef(){}

        ~Closure()
        {
        }

        bool IsValid()const{return closureObjRef.get()!=nullptr;}
        void Reset(){closureObjRef.Reset();}

        const MethodBlock* GetMethod() const
        {
            if (!IsValid()) return nullptr;

            //Push special callstack frame to notify return
            auto inst = (ValueType*)closureObjRef.get()->data.obj;
            //this->function
            // [0]    [1]
            auto fnInfo = (MethodBlock*)inst[1].data.obj;

            return fnInfo;
        }

        ValueType GetEnv() const
        {
            if(!IsValid()) return nullptr;

            //Push special callstack frame to notify return
            auto inst = (ValueType*)closureObjRef.get()->data.obj;
            //this->function
            // [0]    [1]
            //ValueType* tgtenv = (ValueType*)inst[0].data.obj;

            return inst[0];
        }
    };

    //Can only lookup static functions
    Closure LookupFunction(std::string name) {

        auto blk = libLoader.LookupFunction(name);
        auto tyBlk = std::get<0>(blk);
        auto fnBlk = std::get<1>(blk);
        if (fnBlk == nullptr || !fnBlk->isStatic)
            return Closure();

        auto clos = CreateStaticClosure(fnBlk, tyBlk);
        return Closure{extRefs.NewExtRef(clos)};
    }

    //TODO:Create new call env to support multithreading

    void CallClosure(const Closure& fn)
    {
        if(!fn.IsValid())
        {
            return;
        }

        //Assert is env setup

        //this->function
        // [0]    [1]
        
        auto currIP = ip;

        //Let's Run!!!

        //Utilize the call and ret systems
        static IL dummyBody[2] = {
            {(void*)&Op_CALL},
            {(void*)&Op_HLT}
        };

        valueStack.push_back(*fn.closureObjRef.get());
        ip = dummyBody;
        status = ExecutionStatus::Running;
        while(ip!= dummyBody + 1)
        {
            if(status != ExecutionStatus::Running) break;
            //CHECK_IP(ip);
            InstFn fn = (InstFn)((*ip++).inst);
            (*fn)(this);
            
        }

        ip = currIP;
    }

    void Call(MethodBlock* fn, const ValueType& env);

    LibraryLoader::_StatReg CompileProgram()
    {
        return libLoader.Compile();
    }

    void Reset()
    {
        this->status = ExecutionStatus::Finished;
        this->error = ErrorCode::None;
        this->errMsg = "";
        //Clear value stack
        valueStack.clear();
        //and static fields
        staticPool.clear();
        //remaining call stack frames
        callStack.clear();
    }
        
    void ClearLib()
    {
        libLoader.ClearCompiled();
        libLoader.libs.clear();
    }
    
    const static InstFn opcodeEntry[];

    void ReportError(const std::string& errmsg){
        status = ExecutionStatus::Error;
        error = ErrorCode::RuntimeError;
        this->errMsg = errmsg;
    }

    ValueType CreateStaticClosure(MethodBlock* fnInfo, TypeTable* thisType) {
        //if(thisType->IsReferenceType()){
            //Find static field
            auto& inst = FindStaticFields(thisType);
            //Since static fields can be moved by GC during closure creation
            //(closure object allocation may trigger gc), we pass field by reference
            return CreateClosure(fnInfo, inst);
        //}else
        //{
        //    ValueType ty(thisType);
        //    return CreateClosure(fnInfo, ty);
        //}
    }


    ValueType CreateClosure(MethodBlock* fnInfo, const ValueType& thisPtr) {
        auto closureRef = NewRefTypeObject(&_closureObjInfo);

        ValueType* closureInst = (ValueType*)closureRef.data.obj;
        //ValueType fnInfoSlot(fnInfo);


        //ValueType thisEnvSlot(thisPtr->get()->type);
        //thisEnvSlot.data.obj = new ExtRef<StaticFields>(thisPtr);
        //This pointer
        closureInst[0].type = thisPtr.type;
        gc.WriteField(thisPtr, closureRef, 0);

        //Function entry
        closureInst[1].type = (&_intpObjInfo);
        closureInst[1].data.obj = fnInfo;

        return closureRef;
    }

private:

    static void Op_HLT(Interpreter* intp){intp->status = ExecutionStatus::Halted;}
    //()->void     Return from function, pop frame from call stack
    static void Op_RET(Interpreter* intp);
    // (Closure)->void
    static void Op_CALL(Interpreter* intp);

    static void _op_callstatic(Interpreter* intp);

    static void _op_callmem(Interpreter* intp);

    //(Type/Obj)->Obj
    static void Op_NEW(Interpreter* intp);

    

    //(Type/Obj),imm.funcInfo->Closure
    static void _op_ldfn(Interpreter* intp);

    //(Type/Obj),imm.str->Closure
    static void _op_ldstaticfn(Interpreter* intp);
    
    //Load member
    //(Obj),imm->Obj
    static void _op_ldmem(Interpreter* intp);

    //Store member
    //(Obj(cls), Obj(val)),imm->-1
    static void _op_stmem(Interpreter* intp);

    // addr    Init a syscall into c++ land
    static void NOP(Interpreter* intp)
    {
        
    }
    // (),imm -> null[imm]      Effectively increase stack size by n
    static void Op_PUSH(Interpreter* intp)
    {
        auto imm = (intp->ip++)->i;
        auto currStackSize = intp->valueStack.size();
        int tgtSize = imm + currStackSize;
        intp->valueStack.resize(tgtSize);
    }
    static void Op_PUSHIMM(Interpreter* intp)
    {
        auto imm = (intp->ip++)->i;
        intp->valueStack.emplace_back(&_intpObjInfo);
        intp->valueStack.back().data.value = imm;
    }
    //()-> -1        Pop one element out of stack
    static void Op_POP(Interpreter* intp);
    // (),imm -> -imm      Pop n elements out of stack
    static void Op_POPI(Interpreter* intp);
    // (u32)->Obj        Load from address 64
    static void _op_ldstatic(Interpreter* intp);
    // (u32, Obj)-> -1  Store to address 64, val stays put
    static void _op_ststatic(Interpreter* intp);
    // (),imm->Obj        Load from address 64
    static void Op_LDI(Interpreter* intp);
    // (Obj),imm-> 0  Store to address 64, val stays put
    static void Op_STI(Interpreter* intp);
    // (u32)->Obj         Load from local + addr
    static void _op_ldarg(Interpreter* intp);
    // (u32, Obj)-> -1   Store to local + addr, val stays put
    static void _op_starg(Interpreter* intp);
    // (u32)->Obj         Load from local + addr
    static void _op_ldloc(Interpreter* intp);
    // (u32, Obj)-> -1   Store to local + addr, val stays put
    static void _op_stloc(Interpreter* intp);
    // (u32)->Obj         Load from local + addr
    static void _op_ld(Interpreter* intp);
    // (u32, Obj)-> -1   Store to local + addr, val stays put
    static void _op_st(Interpreter* intp);

    static void _op_ldthis(Interpreter* intp);

    //Copy currently does nothing. maybe remove in future?
    static void _op_copy(Interpreter* intp);
    static void _op_cast(Interpreter* intp);
    static void _op_typecmp(Interpreter* intp);
    static void _op_isnull(Interpreter* intp);

    //JZ,  // addr, cond  Jump to addr if cond == 0
    //JNZ, // addr, cond  Jump to addr if cond != 0

    //JB,
    //JNB,

    //JA,
    //JNA,



    
    


};
