#include "Interpreter.h"

#define ARITH_FN(name, type) _arith_##name##_##type
#define CMP_FN(name, type) _cmp_##name##_##type
#define AI(name) & ARITH_FN(name, i)
#define CI(name) & CMP_FN(name, i)
#define AF(name) & ARITH_FN(name, f)
#define CF(name) & CMP_FN(name, f)
//InstFn rst[]={nullptr};


TypeObjInfo Interpreter::_typeObjInfo;
ClosureObjInfo Interpreter::_closureObjInfo;
IntpObjInfo Interpreter::_intpObjInfo;

ValueType ValueType::nullValue = ValueType();

#define ENSURE_ARG_NUM(vm, cnt) \
    do{\
        if (vm->valueStack.size() < cnt)\
        {\
            vm->status = Interpreter::ExecutionStatus::Error;\
            vm->error = Interpreter::ErrorCode::NotEnoughArgument;\
            return;\
        }\
    }while(0)

#define CHECK_INST_ADDR(addr) \
    do{\
        if (addr >= program.size() || addr < 0)\
        {\
            status = ExecutionStatus::Error;\
            error = ErrorCode::JumpAddrOutOfRange;\
            return;\
        }\
    }while(0)

#define CHECK_GLOBAL_ADDR(addr)\
    do {\
        if(addr >= staticPool.size() || addr < 0)\
        {\
            status = ExecutionStatus::Error;\
            error = ErrorCode::GlobalAddrOutOfRange;\
            return;\
        }\
    }while (0)

#define CHECK_LOCAL_ADDR(addr)\
    do {\
        if (addr >= valueStack.size() - 1 || addr < 0)\
        {\
            status = ExecutionStatus::Error;\
            error = ErrorCode::LocalAddrOutOfRange;\
            return;\
        }\
    }while (0)

#define CHECK_IP(ip)\
    do {\
        if(!isInstruction[ip])\
        {\
            status = ExecutionStatus::Error;\
            error = ErrorCode::UnknownOpcode;\
            break;\
        }\
    }while (0)

void ExternalRefBase::Unref()
{
    if (refCnt <= 1)
    {
        reg->RemoveRef(idx);
    }
    else
    {
        refCnt--;
    }
}


void CallOpBase(Interpreter* intp, MethodBlock* fn, const ValueType& thisHndl)
{

    //if (thisHndl.data.obj == nullptr)
    //{
    //    intp->ReportError("Call function on null");
    //    return;
    //}

    //we need this pointer and fn desc

    //Use the same block for arg and rets to avoid memcpy
    int argCnt = fn->args.size();
    //int retCnt = fn->rets.size();
    //int exchangeBlkSize = std::max(argCnt, retCnt);

    auto currSP = intp->valueStack.size();
    //include arguments
    currSP -= argCnt;
    //auto tgtStackSize = currSP + exchangeBlkSize;
    //intp->valueStack.resize(tgtStackSize);

    //Don't pop to retain the closure handle for keeping object alive
    //And record stack position - 1 to automatically delete closure handle
    intp->callStack.emplace_back(currSP, intp->ip, thisHndl, fn);

    //Copy arg value
    //+----------- <- sp
    //| closure
    //| arg 0
    //| arg 1
    //| ...
    //| arg n

    //ENSURE_ARG_NUM(intp, argCnt + 1);

    //Load function body
    auto entryPt = fn->body.data();

    //Jump to entrypoint
    intp->ip = entryPt;
}

void Interpreter::Call(MethodBlock* fn, const ValueType& env)
{
    auto currIP = ip;

    //Let's Run!!!

    //Utilize the call and ret systems
    static IL dummyBody[1] = {
        //{(void*)&Op_CALL},
        {(void*)&Op_HLT}
    };

    ip = dummyBody;
    status = ExecutionStatus::Running;
    CallOpBase(this, fn, env);
    while (ip != dummyBody)
    {
        if (status != ExecutionStatus::Running) break;
        //CHECK_IP(ip);
        InstFn fn = (InstFn)((*ip++).inst);
        (*fn)(this);

    }

    ip = currIP;
}

void Interpreter::Op_RET(Interpreter* intp)
{
    if (intp->callStack.empty())
    {
        intp->status = ExecutionStatus::Finished;
        return;
    }

    //Restore stackframe
    auto& frame = intp->callStack.back();

    MethodBlock* fn = frame.currentFn;
    int retCnt = fn->rets.size();

    //int dst = frame.sp;
    //int src = intp->valueStack.size() - retCnt;

    int stackTgtSize = frame.sp + retCnt;
    intp->valueStack.resize(stackTgtSize);

    //if (src < dst)
    //{
    //    //stack count error! the function pop too much!
    //    intp->ReportError("Stack imbalance!, the function: " + fn->name + " pop too much!");
    //    return;
    //}
    //
    //if (src == dst)
    //{
    //    //landed just right, no need to move ret value.
    //}
    //else
    //{
    //    //Move from low to high to address overlapping
    //    for (int i = 0; i < retCnt; i++)
    //    {
    //        intp->valueStack[dst + i] =
    //            intp->valueStack[src + i];
    //    }
    //    auto end = intp->valueStack.end();
    //    auto begin = intp->valueStack.begin();
    //    intp->valueStack.erase(begin + dst + retCnt, end);
    //}

    intp->ip = frame.ip;
    intp->callStack.pop_back();
}


void Interpreter::Op_CALL(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    //lookup function defs

    auto& closureVal = intp->valueStack.back();
    

    if (closureVal.type != &_closureObjInfo)
    {
        intp->ReportError("Call object is not a closure");
        return;
    }

    ValueType* closureObj = (ValueType*)closureVal.data.obj;
    MethodBlock* fnInfo = (MethodBlock*)closureObj[1].data.obj;
    auto& tgtenv = closureObj[0];

    intp->valueStack.pop_back();
    CallOpBase(intp, fnInfo, tgtenv);
}

void Interpreter::_op_callstatic(Interpreter* intp)
{
    //Index correctness is checked by compiler 
    auto ty = (TypeTable*)(intp->ip++)->inst;
    auto fnIdx = (intp->ip++)->i;
    auto fn = ty->methodTable[fnIdx];
    auto thisEnv = intp->FindStaticFields(ty);
    //ValueType thisHndl(ty);
    //thisHndl.data.obj = thisEnv;
    CallOpBase(intp, fn, thisEnv);
}

void Interpreter::_op_callmem(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    //lookup function defs

    auto& thisObj = intp->valueStack.back();

    auto fnIdx = (intp->ip++)->i;
    auto fn = thisObj.type->methodTable[fnIdx];

    intp->valueStack.pop_back();
    CallOpBase(intp, fn, thisObj);
}


void Interpreter::Op_NEW(Interpreter* intp)
{
    auto ty = (TypeTable*)(intp->ip++)->inst;
    if(ty == nullptr)
    {
        intp->ReportError("Instantiate null type");
        return;
    }

    if(ty->IsReferenceType()){
        auto newHndl = intp->NewRefTypeObject(ty);
        intp->valueStack.push_back(newHndl);

    }else
    {
        intp->valueStack.emplace_back(ty);
    }
    //Add closure reference to stack
}

void Interpreter::_op_ldfn(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    auto fnInfo = (MethodBlock*)(intp->ip++)->inst;
    auto& thisObj = intp->valueStack.back();

    

    if (fnInfo->isStatic)
    {
        intp->ReportError("Binding static function to instance");
        return;
    }
   
    if (!thisObj.type->IsReferenceType())
    {
        intp->ReportError("Binding function to non-reference types");
        return;
    }
    //auto inst = thisObj.data.obj;
    assert(thisObj.data.obj != nullptr);
    auto closureRef = intp->CreateClosure(fnInfo, thisObj);

    

    //Add closure reference to stack
    
    thisObj = std::move(closureRef);
}

void Interpreter::_op_ldstaticfn(Interpreter* intp)
{
    auto ty = (TypeTable*)(intp->ip++)->inst;
    auto fn = (MethodBlock*)(intp->ip++)->inst;
    auto thisEnv = intp->FindStaticFields(ty);

    

    //Allocate closure object
    auto closureRef = intp->CreateStaticClosure(fn, ty);

    if (!fn->isStatic)
    {
        intp->ReportError("Binding non static function to static fields");
        return;
    }


    //Add closure reference to stack
    intp->valueStack.push_back(closureRef);
    
}

void Interpreter::_op_ldmem(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);

    auto memIdx = (intp->ip++)->u;
    auto& thisObj = intp->valueStack.back();

    if (!thisObj.type->IsReferenceType())
    {
        intp->ReportError("Accessing member from non-reference type");
        return;
    }

    auto thisInst = (ValueType*)thisObj.data.obj;

    if(thisInst == nullptr)
    {
        intp->ReportError("Accessing member from null reference");
        return;
    }

    //Sadly we don't have this check..
    //if (memIdx >= thisInst->memberVars.size())
    //{
    //    intp->ReportError("Accessing member out of range");
    //    return;
    //}

    thisObj = thisInst[memIdx];
}

void Interpreter::_op_stmem(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 2);
    //Function name is stored in immediate values
    auto memIdx = (intp->ip++)->u;
    auto& thisObj = intp->valueStack.back();
    auto& val = *(intp->valueStack.end() - 2);

    //if (!thisObj.type->IsReferenceType())
    //{
    //    intp->ReportError("Accessing member from non-reference type"); return;
    //}
    //
    //auto thisInst = (ValueType*)thisObj.data.obj;

    //if (memIdx >= thisInst->memberVars.size())
    //{
    //    intp->ReportError("Accessing member out of range");
    //}
    if(!intp->gc.WriteField(val,thisObj, memIdx))
    {
        intp->ReportError("Writing to invalid member:" 
            + thisObj.type->name + "." + std::to_string(memIdx)); return;
    }

    //thisInst[memIdx] = val;
    intp->valueStack.pop_back();
    intp->valueStack.pop_back();

}

void Interpreter::Op_POP(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    intp->valueStack.pop_back();
}

void Interpreter::Op_POPI(Interpreter* intp)
{
    auto cnt = (intp->ip++)->u;
    ENSURE_ARG_NUM(intp, cnt);
    intp->valueStack.erase(
        intp->valueStack.end() - cnt,
        intp->valueStack.end()
    );
}

void Interpreter::_op_ldstatic(Interpreter* intp)
{
    auto ty = (TypeTable*)(intp->ip++)->inst;
    auto idx = (intp->ip++)->u;
    auto thisEnv = intp->FindStaticFields(ty);


    if (idx >= ty->staticFields.size())
    {
        intp->ReportError("Env value address out of range");
        return;
    }

    intp->valueStack.push_back(((ValueType*)(thisEnv.data.obj))[idx]);
}

void Interpreter::_op_ststatic(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    auto& val = intp->valueStack.back();

    auto ty = (TypeTable*)(intp->ip++)->inst;
    auto idx = (intp->ip++)->u;
    auto thisEnv = intp->FindStaticFields(ty);


    if (idx >= ty->staticFields.size())
    {
        intp->ReportError("Env value address: " + std::to_string(idx) + " out of range");
        return;
    }

    if(!intp->gc.WriteField(val, thisEnv, idx))
    {
        intp->ReportError("Writing to invalid static field:"
            + thisEnv.type->name + "." + std::to_string(idx)); return;
    }

    //((ValueType*)(thisEnv.data.obj))[idx] = val;

    intp->valueStack.pop_back();

}

void Interpreter::Op_LDI(Interpreter* intp)
{
    std::uint32_t addr = (intp->ip++)->u;

    auto currSP = intp->valueStack.size() - 1;
    addr = currSP - addr;

    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Env value address out of range");
        return;
    }

    intp->valueStack.push_back(intp->valueStack[addr]);
}

void Interpreter::Op_STI(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    std::uint32_t addr = (intp->ip++)->u;
    auto currSP = intp->valueStack.size() - 1;
    addr = currSP - addr;

    ValueType& val = intp->valueStack.back();


    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Env value address: " + std::to_string(addr) + " out of range");
        return;
    }

    intp->valueStack[addr] = val;
    intp->valueStack.pop_back();

}

void Interpreter::_op_ldarg(Interpreter* intp)
{
    std::uint32_t addr = (intp->ip++)->u;
    addr += intp->callStack.back().sp; //slot 0 is current closure

    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Local value address: " + std::to_string(addr) + " out of range");
        return;
    }

    intp->valueStack.push_back(intp->valueStack[addr]);
}

void Interpreter::_op_starg(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    std::uint32_t addr = (intp->ip++)->u;

    addr += intp->callStack.back().sp; //slot 0 is current closure

    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Local value address: " + std::to_string(addr) + " out of range");
        return;
    }

    intp->valueStack[addr] = intp->valueStack.back();
    intp->valueStack.pop_back();
}

void Interpreter::_op_ldloc(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    std::uint32_t addr = intp->valueStack.back().data.value;
    addr += intp->callStack.back().sp; //slot 0 is current closure

    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Local value address: " + std::to_string(addr) + " out of range");
        return;
    }

    intp->valueStack.back() = intp->valueStack[addr];
}

void Interpreter::_op_stloc(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 2);
    std::uint32_t addr = intp->valueStack.back().data.value;

    addr += intp->callStack.back().sp; //slot 0 is current closure

    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Local value address: " + std::to_string(addr) + " out of range");
        return;
    }

    intp->valueStack.pop_back();
    intp->valueStack[addr] = intp->valueStack.back();
    intp->valueStack.pop_back();
}


void Interpreter::_op_ld(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    std::uint32_t addr = intp->valueStack.back().data.value;

    auto currSP = intp->valueStack.size() - 1;
    addr = currSP - addr;

    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Env value address out of range");
        return;
    }

    intp->valueStack.back() = (intp->valueStack[addr]);
}

void Interpreter::_op_st(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 2);
    std::uint32_t addr = intp->valueStack.back().data.value;

    auto currSP = intp->valueStack.size() - 1;
    addr = currSP - addr;


    if (addr >= intp->valueStack.size())
    {
        intp->ReportError("Env value address: " + std::to_string(addr) + " out of range");
        return;
    }
    intp->valueStack.pop_back();

    ValueType& val = intp->valueStack.back();

    intp->valueStack[addr] = val;
    intp->valueStack.pop_back();

}

void Interpreter::_op_ldthis(Interpreter* intp)
{
    auto& cstack = intp->callStack;
    if(cstack.empty())
    {
        intp->ReportError("Loading environment from empty callstack");
    }
    auto& frame = cstack.back();
    intp->valueStack.push_back(frame.currEnv);
}

void Interpreter::_op_copy(Interpreter* intp)
{
    
}

void Interpreter::_op_cast(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);

    auto ty = (TypeTable*)(intp->ip++)->inst;
    assert(ty != nullptr);
    auto& tgtObj = intp->valueStack.back();
    auto thisTy = intp->valueStack.back().type;

    int eq = 0;

    while (thisTy != nullptr)
    {
        if (thisTy == ty)
        {
            eq = 1;
            break;
        }
        thisTy = thisTy->parentType;
    }

    if(eq)
        tgtObj.type = ty;
    else
    {
        tgtObj = ValueType(ty);
    }
}

void Interpreter::_op_typecmp(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp,1);
    auto ty = (TypeTable*)(intp->ip++)->inst;
    auto thisTy = intp->valueStack.back().type;

    int eq = 0;

    do
    {
        if(thisTy == ty)
        {
            eq = 1;
            break;
        }
    }while (
        (thisTy != nullptr) && 
        ((thisTy = thisTy->parentType) != nullptr)
    );


    //Add closure reference to stack
    ValueType v(&_intpObjInfo);
    v.data.value = eq;
    intp->valueStack.emplace_back(&_intpObjInfo);
    intp->valueStack.back() = v;
}

void Interpreter::_op_isnull(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    ValueType isnull(&Interpreter::_intpObjInfo);
    bool isValNul = intp->valueStack.back().IsNull();
    isnull.data.value = isValNul;
    intp -> valueStack.back() = isnull;
}

//(i32(offset), i32(cond))->-1 // jump if zero
#define JMPL(condition, name)                             \
    static void Op_##name (Interpreter* intp)   \
    {                                                              \
        ENSURE_ARG_NUM(intp, 2);                                   \
        std::int32_t offset = intp->valueStack.back().data.value;  \
        intp->valueStack.pop_back();                               \
        std::int32_t cond = intp->valueStack.back().data.value;    \
        intp->valueStack.pop_back();                               \
                                                                   \
        if (!(condition)) return;                                  \
                                                                   \
        auto tgtIP = intp->ip - 1 + offset;                        \
        auto currFn = intp->callStack.back().currentFn;            \
        auto& body = currFn->body;                                  \
                                                                   \
        auto entry = &body.front();                                \
        auto exit = &body.back();                                  \
        if (tgtIP > exit || tgtIP < entry) {                       \
            intp->ReportError("Jump out of range");                \
            return;                                                \
        }                                                          \
                                                                   \
        intp->ip = tgtIP;                                          \
    }

    //JMP(cond == 0,JZ)

static void Op_JZ(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 2);
    std::int32_t offset = intp->valueStack.back().data.value;
    intp->valueStack.pop_back();
    std::int32_t cond = intp->valueStack.back().data.value;
    intp->valueStack.pop_back();

    if (cond != 0) return;

    auto tgtIP = intp->ip - 1 + offset;
    auto currFn = intp->callStack.back().currentFn;
    auto& body = currFn->body;

    auto entry = &body.front();
    auto exit = &body.back();
    if (tgtIP > exit || tgtIP < entry) {
        intp->ReportError("Jump out of range");
        return;
    }

    intp->ip = tgtIP;
}

#define JMPI(condition, name)                              \
    static void Op_##name##I(Interpreter* intp)    \
    {                                                              \
        ENSURE_ARG_NUM(intp, 1);                                   \
        std::int32_t offset = (intp->ip++)->i;                     \
        std::int32_t cond = intp->valueStack.back().data.value;    \
        intp->valueStack.pop_back();                               \
                                                                   \
        if (!(condition)) return;                                  \
                                                                   \
        auto tgtIP = intp->ip - 2 + offset;                        \
        auto currFn = intp->callStack.back().currentFn;            \
        auto& body = currFn->body;                                  \
                                                                   \
        auto entry = &body.front();                                \
        auto exit = &body.back();                                  \
        if (tgtIP > exit || tgtIP < entry) {                       \
            intp->ReportError("Jump out of range");                \
            return;                                                \
        }                                                          \
                                                                   \
        intp->ip = tgtIP;                                          \
    }

//(u32(cond)), imm->-1 // jump if zero
static void Op_JZI(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    std::int32_t cond = intp->valueStack.back().data.value;

    std::int32_t offset = (intp->ip++)->i;

    intp->valueStack.pop_back();

    if (cond != 0) return;

    auto tgtIP = intp->ip - 2 + offset;

    auto currFn = intp->callStack.back().currentFn;
    auto& body = currFn->body;

    auto entry = &body.front();
    auto exit = &body.back();
    if (tgtIP > exit || tgtIP < entry) {
        intp->ReportError("Jump out of range");
        return;
    }

    intp->ip = tgtIP;
}

#define JMP(condition, name) \
    JMPL(condition, J##name)\
    JMPI(condition, J##name)\
    JMPL(!(condition), JN##name)\
    JMPI(!(condition), JN##name)\

JMP(cond > 0, A) // jump above/jump not above
JMP(cond < 0, B) // jump below/jump not below

JMPL(cond != 0, JNZ)
static void Op_JNZI(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    std::int32_t offset = (intp->ip++)->i;
    std::int32_t cond = intp->valueStack.back().data.value;
    intp->valueStack.pop_back();
    if (!(cond != 0)) return;
    auto tgtIP = intp->ip - 2 + offset;
    auto currFn = intp->callStack.back().currentFn;
    auto& body = currFn->body;
    auto entry = &body.front();
    auto exit = &body.back();
    if (tgtIP > exit || tgtIP < entry)
    {
        intp->ReportError("Jump out of range"); return;
    }
    intp->ip = tgtIP;
}


static void Op_J(Interpreter* intp)
{
    ENSURE_ARG_NUM(intp, 1);
    std::int32_t offset = intp->valueStack.back().data.value;
    intp->valueStack.pop_back();

    auto tgtIP = intp->ip - 1 + offset;
    auto currFn = intp->callStack.back().currentFn;
    auto& body = currFn->body;

    auto entry = &body.front();
    auto exit = &body.back();
    if (tgtIP > exit || tgtIP < entry) {
        intp->ReportError("Jump out of range");
        return;
    }

    intp->ip = tgtIP;
}

static void Op_JI(Interpreter* intp)
{
    std::int32_t offset = offset = (intp->ip++)->i;
    //2 word instruction
    auto tgtIP = intp->ip - 2 + offset;
    auto currFn = intp->callStack.back().currentFn;
    auto& body = currFn->body;

    auto entry = &body.front();
    auto exit = &body.back();
    if (tgtIP > exit || tgtIP < entry) {
        intp->ReportError("Jump out of range");
        return;
    }

    intp->ip = tgtIP;
}


const InstFn Interpreter::opcodeEntry[] = {
    &NOP,
    //HLT, //Halt the interpreter
    &Op_HLT,
    //RET,    //         Return from function, pop frame from call stack
    &Op_RET,
//Call:
//  [object model]
  //callstatic, //<libName | typeName | funcName>
  //callmem,    //<libName | typeName | funcName>
    &_op_callstatic, &_op_callmem,
//  [base]
  //call,       //(closure)
  //ldfn,       //<libName | typeName | funcName>(object)->closure
  //ldstaticfn, //<libName | typeName | funcName>->closure
    &Op_CALL, &_op_ldfn, &_op_ldstaticfn,

//NewObject:
//  [object model]
  //newobj,     //<libName | typeName>->object
    &Op_NEW,
////obj (type) -> object
  //copy,       //(object)->object
    &_op_copy,
//Type manipulation
//  [object model]
////ldtype (object) -> type
  //cast,       //<libName | typeName>(object)->object
    &_op_cast,
////cast (type object) -> object
  //typecmp,    //<libName | typeName>(object)->u32
    & _op_typecmp,
    &_op_isnull,
    //PUSH,// #val32      Load immediate 64 onto stack
    //POP, //             Pop one element out of stack
    //POPI,// #val32      Pop n elements out of stack
    &Op_PUSH,& Op_PUSHIMM,&Op_POP, &Op_POPI,
//Transfer
//  [object model]
  //ldmem, //<libName | typeName | fieldName>->object
  //stmem, // <libName | typeName | fieldName>(object)
  //ldstatic, // <libName | typeName | fieldName>->object
  //ststatic, // <libName | typeName | fieldName>(object)
    &_op_ldmem, &_op_stmem, &_op_ldstatic, &_op_ststatic,
//  [base]
  //ldarg, // <u32>->object     load from frame bottom + u32
  //starg, // <u32>(object)      store to frame bottom + u32
  //ldi, // <u32>->object       load from stack top - u32
  //sti, // <u32>(object)        store to stack top - u32
    &_op_ldarg, &_op_starg, &Op_LDI, &Op_STI,
    & _op_ldthis,
  //ldloc, //(u32)->object     same as ldarg
  //stloc, //(u32, object)       same as starg
  //ld, //(u32)->object        ldi
  //st, //(u32, object)          sti
    &_op_ldloc, &_op_stloc, &_op_ld, &_op_st,

    &Op_J, //(val32)
    &Op_JI,//<val32>

    //JZ, JZI, // addr, cond  Jump to addr if cond == 0
    //JNZ, JNZI, // addr, cond  Jump to addr if cond != 0
    & Op_JZ,& Op_JZI,
    &Op_JNZ, &Op_JNZI,
    //JB,JBI,
    //JNB,JNBI,
    & Op_JB,& Op_JBI,
    & Op_JNB,& Op_JNBI,
    //JA,JAI,
    //JNA,JNAI,
    & Op_JA,& Op_JAI,
    & Op_JNA,& Op_JNAI,

};
