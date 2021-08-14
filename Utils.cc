#include "Utils.h"

#include "Interpreter.h"

const int OpLength[] = {
   1, //NOP, //No operation
   1, //HLT, //Halt the interpreter
   1, //RET, //Return from function, pop frame from call stack

//Call:
//  [object model]
   3, //callstatic, //<libName | typeName , funcName>  3 call method statically
   2, //callmem,    //<libName | typeName | funcName>  2 call method using vtable
//  [base]
   1, //call,       //(closure) 1 call closure
   2, //ldfn,       //<libName | typeName | funcName>(object)->closure 2 load method from vtable and current object
   3, //ldstaticfn, //<libName | typeName , funcName>->closure         3 load method statically(static env)


//NewObject:
//  [object model]
    2, //newobj,     //<libName | typeName>->object 2
////obj (type) -> object
    1, //copy,       //(object)->object             1
//ldnull->object

//Type manipulation
//  [object model]
////ldtype (object) -> type
    2, //cast,       //<libName | typeName>(object)->object 2 cast object to type, if can't, return null with casted type
////cast (type object) -> object
    2, //typecmp,    //<libName | typeName>(object)->u32  2 compare type using the inheritance chain
    1, //isnull,     //(obj) null: (reftype, obj == nullptr) or (type == nullptr) 1

    2, //PUSH,// <u32>     2 push N null value onto stack
    2, //PUSHIMM,// #val32  2 load 1 int32 immediate value onto stack
    1, //POP, //            1 Pop one element out of stack
    2, //POPI,// <u32>     2 Pop n elements out of stack
//Transfer
//  [object model]
    2, //ldmem, //<libName | typeName | fieldName>(src)->object 2  load member var onto stack
    2, //stmem, // <libName | typeName | fieldName>(dst, object) 2 store to member var
    3, //ldstatic, // <libName | typeName , fieldName>->object 3 load from static fields
    3, //ststatic, // <libName | typeName , fieldName>(object) 3 store to static fields
//  [base]
    2, //ldarg, // <u32>->object   2  load from frame bottom + u32
    2, //starg, // <u32>(object)   2  store to frame bottom + u32
    2, //ldi, // <u32>->object     2  load from stack top - u32
    2, //sti, // <u32>(object)     2  store to stack top - u32

    1, //ldthis, // ()->(object)  1  load this env onto stack

    1, //ldloc, //(u32)->object  1   same as ldarg
    1, //stloc, //(u32, object)  1   same as starg
    1, //ld, //(u32)->object     1   ...ldi
    1, //st, //(u32, object)     1   ...sti

    1,//jump (val32)
    2,//jmpi <val32>

//  1    2
    1, 2, //JZ,  JZI, //  *: (addr, cond) Jump to addr if cond == 0
    1, 2, //JNZ, JNZI, // *I: <i32>(cond)  Jump to addr if cond != 0

    1, 2, //JB,  JBI,
    1, 2, //JNB, JNBI,

    1, 2, //JA,  JAI,
    1, 2, //JNA, JNAI,
    //LastIndex

    //directives
    1, //d_embed
};

std::int32_t Read32(const std::uint8_t*& ptr)
{
    std::int32_t val = 0;
    val |= *ptr++;
    val |= (*ptr++) << 8;
    val |= (*ptr++) << 16;
    val |= (*ptr++) << 24;

    return val;
}

void Write32(std::int32_t val, std::uint8_t*& ptr)
{
    static const std::uint32_t mask = 0xFF;
    
    *ptr++ = val & mask;
    *ptr++ = (val >> 8) & mask;
    *ptr++ = (val >> 16) & mask;
    *ptr++ = (val >> 24) & mask;
    
}

std::string ReadStr(const std::uint8_t*& ptr,const std::uint8_t* end)
{
    int len = 0;
    auto start = ptr;
    while((*ptr++)!=0)
    {
        len++;
        if(ptr == end) break;
    }
    std::string str = std::string((char*)start,len);
    return str;
}


void WriteStr(const std::string& str, std::uint8_t*& ptr)
{
    int len = str.size() + 1;
    auto start = str.c_str();
    memcpy(ptr, start, len);
}

//0: no, 1: int, 2: string
enum class OpArgType
{
    None, Int32, String
};

OpArgType OpCodeArgumentType(OpCode opcode)
{
    switch (opcode)
    {
        //int32 imm
    case OpCode::PUSH:
    case OpCode::PUSHIMM:
    case OpCode::POPI:
    case OpCode::JAI:
    case OpCode::JBI:
    case OpCode::JNAI:
    case OpCode::JNBI:
    case OpCode::JZI:
    case OpCode::JNZI:
    case OpCode::ldarg:
    case OpCode::starg:
    case OpCode::ldi:
    case OpCode::sti:
        return OpArgType::Int32;
        break;
        //funcname
    case OpCode::callmem:
    case OpCode::ldfn:
        //Typename
    case OpCode::newobj:
    case OpCode::cast:
    case OpCode::typecmp:
        //fieldname
    case OpCode::ldmem:
    case OpCode::stmem:
        //2 immediate args: type, field
    case OpCode::ldstatic:
    case OpCode::ststatic:
        //2 immediate args: type, fnName:
    case OpCode::callstatic:
    case OpCode::ldstaticfn:
        //Compiler directives:
    case OpCode::d_embed:
        return OpArgType::String;
        break;
    default:return OpArgType::None;
    }
}

std::vector<Instruction> DecodeByteBuffer(const std::vector<std::uint8_t>& buffer)
{
    if(buffer.empty())return{};
    std::vector<Instruction> inst;
    const std::uint8_t* ptr = buffer.data(), *end = &buffer.back() + 1;
    while(ptr < end){
        inst.emplace_back();
        auto& I = inst.back();
        OpCode opcode = (OpCode)*ptr++;
        I.opcode = opcode;
        switch(OpCodeArgumentType(opcode))
        {
            //int32 imm
        case OpArgType::Int32:
            I.oprand = Read32(ptr);
            break;
        
        case OpArgType::String:
            I.oprand = ReadStr(ptr, end);
            break;
            default:break;
        }
    }
    return inst;
}

std::vector<std::uint8_t> EncodeInstList(const std::vector<Instruction>& list)
{
    std::vector<std::uint8_t> buffer;
    for(auto& I : list)
    {
        buffer.push_back((std::uint8_t)I.opcode);
        switch(OpCodeArgumentType(I.opcode))
        {
        case OpArgType::None: break;
        case OpArgType::Int32:
        {
            buffer.resize(buffer.size() + 4);
            std::uint8_t* start = &buffer.back() - 4;
            Write32(std::get<int>(I.oprand), start);
        }
        break;
        case OpArgType::String: 
        {
            auto str = std::get<std::string>(I.oprand);
            buffer.resize(buffer.size() + str.size() + 1);
            std::uint8_t* start = &buffer.back() - str.size() - 1;
            WriteStr(str, start);
        }
        break;
        default: ;
        }
    }
    return buffer;
}

ValueType& GetArg(Interpreter* intp, int idx)
{
    auto& stack = intp->valueStack;
    auto& frame = intp->callStack.back();
    int stackBottom = frame.sp;
    int addr = idx + stackBottom;
    assert(addr >= 0 && addr < (int)stack.size());
    return stack[addr];
}

ValueType& GetStackTop(Interpreter* intp, int idx)
{
    auto& stack = intp->valueStack;
    int len = stack.size() - 1;
    int addr = len - idx;
    if(addr < 0 || addr > len)
    {
        intp->ReportError(
        "Invalid stack address: " + std::to_string(addr) +
            ", Max address: " + std::to_string(len)
        );
        return ValueType::nullValue;
    }
    return stack[len - idx];
}
