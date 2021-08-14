#include "Library.h"

#include "Interpreter.h"

const std::string LibraryLoader::hostWrapperLibName = "HostTypes";


static std::vector<std::string> SplitFullName(
    const std::string& name,
    const std::string& delim = "|"
) {
    std::size_t p = 0;// std::min(name.size(),name.find(delim));
    auto s = name;
    std::vector<std::string> parts;
    while ((p = s.find(delim)) != std::string::npos) {
        auto part = s.substr(0, p);
        parts.push_back(part);
        s.erase(0, p + delim.size());
    }

    parts.push_back(s);

    return parts;
}

LibraryInfo* Test()
{
    auto lib = (new LibraryInfo("TestLib"))->Class(

        (new ClassInfo("Cls1"))
        ->Field(FieldInfo("a", "t"))
        ->Field(FieldInfo("b", "t"))
        ->Method(
            (new ProgramMethod("Fn1"))
            ->Arg("a", "t")
            ->Arg("a", "t")
            ->Arg("a", "t")
            ->Return("a", "t")
        )

    );

    return lib;
}

FieldInfo::FieldInfo(const std::string& name, const std::string& type): cls(nullptr),name(name),type(type)
{
    auto n = SplitFullName(type);
    if (n.size() < 2)
    {
        isFullType = false;
    }else
    {
        isFullType = true;
    }
}

std::string FieldInfo::GetFullType() const
{
    if(isFullType) return type;
    std::string libName = "_ERR_FIELD_NO_LIB";
    if(cls != nullptr)
    {
        libName = cls->GetLibName();
    }
    return libName + "|" + type;
}

bool FieldInfo::IsComplete() const
{
    return cls != nullptr && cls->IsComplete();
}

void FieldInfo::SetType(const std::string& type)
{
    this->type = type;
    auto n = SplitFullName(type);
    if (n.size() < 2)
    {
        isFullType = false;
    }
    else
    {
        isFullType = true;
    }
}

bool MethodInfoBase::IsComplete() const
{
    return cls != nullptr && cls->IsComplete();
}

void MethodInfoBase::MendFields(ClassInfo* cls)
{
    this->cls = cls;
    for (auto& f : args)
    {
        f.cls = cls;
    }
    for (auto& f : rets)
    {
        f.cls = cls;
    }
}

std::string MethodInfoBase::GetThisType() const
{
    if(cls == nullptr) return "_ERR_MTD_NO_CLASS";
    return cls->GetFullName();
}

std::string ClassInfo::GetFullName() const
{
    return GetLibName() + "|" + name;
}

std::string ClassInfo::GetParentName() const
{
    if(parent.empty()) return "";
    auto n = SplitFullName(parent);
    if (n.size() < 2)
    {
        return GetLibName() + "|" + parent;
    }else
    {
        return parent;
    }
}

std::string ClassInfo::GetLibName() const
{
    if(lib == nullptr) return "_ERR_CLS_NO_LIB";
    return lib->name;
}


TypeTable* LibraryLoader::FindTypeByName(const std::string& name, const std::string& libName)
{
    LibBlock* lib = nullptr;
    for(auto& l:compiledLibs)
    {
        if(l.name == libName){
            lib = &l;
            break;
        }
    }
    if (lib == nullptr) return nullptr;

    for (auto& typeInfo : lib->types)
    {
        auto typeName = typeInfo->name;
        if (name == typeName)
            return typeInfo.get();
    }

    return nullptr;
}

ClassInfo* LibraryLoader::FindClassByName(const std::string& name, const std::string& libName)
{
    LibraryInfo* lib = nullptr;
    for (auto& l : libs)
    {
        if (l->name == libName) {
            lib = l.get();
            break;
        }
    }
    if (lib == nullptr) return nullptr;

    for (auto& typeInfo : lib->types)
    {
        auto typeName = typeInfo->name;
        if (name == typeName)
            return typeInfo.get();
    }

    return nullptr;
}

TypeTable* LibraryLoader::_ResolveTypeName(const std::string& name, const std::string& thisLib)
{
    //libName|typeName
    auto parts = SplitFullName(name);
    if(parts.empty()) return nullptr;
    std::string typeName, libName;
    if (parts.size() > 1)
    {
        typeName = parts[1];
        libName = parts[0];
    }
    else
    {
        typeName = parts[0];
        libName = thisLib;
    }

    //return FindTypeByName(typeName, libName);
    return LookupType(libName + "|" + typeName);
}


std::tuple<TypeTable*, int> LibraryLoader::_ResolveFnName(const std::string& name, const std::string& thisLib)
{
    //libName|typeName|fnName or typeName|fnName
    auto parts = SplitFullName(name);
    if (parts.size() <= 1)return { nullptr,-1 };
    std::string typeName, libName;
    if (parts.size() > 2)
    {
        typeName = parts[1];
        libName = parts[0];
    }
    else
    {
        typeName = parts[0];
        libName = thisLib;
    }

    auto fnName = parts.back();
    auto table = FindTypeByName(typeName, libName);
    if (table == nullptr)
        return { nullptr,-1 };
    auto typeInfo = tlut[table];
    if (typeInfo == nullptr)
        return { nullptr,-1 };

    for(std::size_t i = 0; i < typeInfo->methods.size();i++)
    {
        auto& m = typeInfo->methods[i];
        if(m->name == fnName)
            return { table,i };
    }

    return { nullptr,-1 };
}

int LibraryLoader::_ResolveMemberName(const std::string& name, const std::string& thisLib)
{
    //libName|typeName|memberName or typeName|memberName
    auto parts = SplitFullName(name);
    if (parts.size() <= 1)return -1;
    std::string typeName, libName;
    if (parts.size() > 2)
    {
        typeName = parts[1];
        libName = parts[0];
    }
    else
    {
        typeName = parts[0];
        libName = thisLib;
    }

    auto typeInfo = FindClassByName(typeName, libName);
    if (typeInfo == nullptr)
        return -1;

    auto fieldName = parts.back();
    for (std::size_t i = 0; i < typeInfo->fields.size(); i++)
    {
        auto& m = typeInfo->fields[i];
        if (m.name == fieldName)
            return i;
    }

    return -1;
}

std::tuple<TypeTable*, int> LibraryLoader::_ResolveStaticMemberName(const std::string& name, const std::string& thisLib)
{
    //libName|typeName|memberName or typeName|memberName
    auto parts = SplitFullName(name);
    if (parts.size() <= 1)return {nullptr,-1};
    std::string typeName, libName;
    if (parts.size() > 2)
    {
        typeName = parts[1];
        libName = parts[0];
    }
    else
    {
        typeName = parts[0];
        libName = thisLib;
    }

    

    auto table = FindTypeByName(typeName, libName);
    if (table == nullptr)
        return { nullptr,-1 };
    auto typeInfo = tlut[table];
    if (typeInfo == nullptr)
        return { nullptr,-1 };
    auto fieldName = parts.back();
    for (std::size_t i = 0; i < typeInfo->staticFields.size(); i++)
    {
        auto& m = typeInfo->staticFields[i];
        if (m.name == fieldName)
            return { table,i };
    }

    return { nullptr,-1 };
}

std::vector<IL> LibraryLoader::TranslateFn(
    MethodInfoBase* fnInfo, 
    std::vector<std::string>& libs,
    LibraryLoader::_StatReg& reg)
{
    //msg = "";
    std::vector<std::string> dynamicBindingNames;
    return fnInfo->TranslateByteCode(
        [&](std::vector<Instruction> bytecode)
        {
            std::vector<IL> translated;
            auto hltFn = (void*)Interpreter::opcodeEntry[0];

            //Build line numbers using opcode length
            std::vector<int> lineNum(bytecode.size());
            int currLine = 0;
            for (std::size_t i = 0; i < bytecode.size(); i++)
            {
                auto& line = bytecode[i];
                std::uint8_t opFnID = (std::uint8_t)line.opcode;
                int opLen = OpLength[opFnID];
                lineNum[i] = currLine;
                currLine += opLen;
            }

            for (std::size_t i = 0; i < bytecode.size(); i++)
            {
                auto& line = bytecode[i];
                std::uint8_t opFnID = (std::uint8_t)line.opcode;
                void* opFn = (void*)Interpreter::opcodeEntry[0];
                if (opFnID <= (int)OpCode::LastIndex)
                {
                    //msg = "Unknown opcode";
                    //return std::vector<IL>();
                    opFn = (void*)Interpreter::opcodeEntry[opFnID];

                }else
                {
                    reg.Log("Encountered compiler directive:"+std::to_string(opFnID));
                }
                switch (line.opcode)
                {
                    //Relative jumps
                case OpCode::JMPI:
                case OpCode::JAI:
                case OpCode::JBI:
                case OpCode::JNAI:
                case OpCode::JNBI:
                case OpCode::JZI:
                case OpCode::JNZI:
                    {
                        auto relAdd = std::get<std::int32_t>(line.oprand);
                        int addr = i+relAdd;
                        if(addr < 0 || addr >= (int)lineNum.size())
                        {
                            reg.RegisterIfError("Jump out of range: line " 
                                + std::to_string(i) + "->" + std::to_string(addr));
                            translated.push_back(IL{ hltFn });
                            translated.push_back(IL{hltFn});
                        }else{
                            int currLine = lineNum[i];
                            int tgtLine = lineNum[i+relAdd];
                            int jmpDelta = tgtLine - currLine;
                            IL immIL, instIL;
                            immIL.i = jmpDelta;
                            instIL.inst = opFn;
                            translated.push_back(std::move(instIL));
                            translated.push_back(std::move(immIL));
                        }
                    }break;
                    //int32 imm
                case OpCode::PUSH:
                case OpCode::PUSHIMM:
                case OpCode::POPI:
                case OpCode::ldarg:
                case OpCode::starg:
                case OpCode::ldi:
                case OpCode::sti:
                    {
                        auto imm = std::get<std::int32_t>(line.oprand);
                        IL immIL, instIL;
                        immIL.i = imm;
                        instIL.inst = opFn;
                        translated.push_back(std::move(instIL));
                        translated.push_back(std::move(immIL));
                    }
                    break;
                //funcname
                case OpCode::callmem:
                case OpCode::ldfn:
                    {
                        auto name = std::get<std::string>(line.oprand);
                        auto idx = std::get<1>(ResolveFnName(name, libs));
                        IL immIL, instIL;
                        immIL.i = idx;
                        if (idx < 0)
                        {
                            reg.RegisterIfError( "Function not found: " + name);
                            return std::vector<IL>();
                        }
                        instIL.inst = opFn;
                        translated.push_back(std::move(instIL));
                        translated.push_back(std::move(immIL));
                    }
                    break;
                //Typename
                case OpCode::newobj:
                case OpCode::cast:
                case OpCode::typecmp:
                    {
                        auto& tyName = std::get<std::string>(line.oprand);
                        auto tyInfo = ResolveTypeName(tyName, libs);
                        
                        if (tyInfo == nullptr)
                        {
                            reg.RegisterIfError("Type not found: " + tyName);
                            return std::vector<IL>();
                        }
                        IL immIL, instIL;
                        immIL.inst = tyInfo;
                        instIL.inst = opFn;
                        translated.push_back(std::move(instIL));
                        translated.push_back(std::move(immIL));
                    }
                    break;
                //fieldname
                case OpCode::ldmem:
                case OpCode::stmem:
                    {
                    auto& fdName = std::get<std::string>(line.oprand);
                    auto idx = ResolveMemberName(fdName, libs);

                    if (idx < 0)
                    {
                        reg.RegisterIfError("Field not found: " + fdName);
                        return std::vector<IL>();
                    }
                    IL immIL, instIL;
                    immIL.i = idx;
                    instIL.inst = opFn;
                    translated.push_back(std::move(instIL));
                    translated.push_back(std::move(immIL));
                    }
                break;
                //2 immediate args: type, field
                case OpCode::ldstatic:
                case OpCode::ststatic:
                {
                    auto& fdName = std::get<std::string>(line.oprand);
                    auto res = ResolveStaticMemberName(fdName, libs);
                    auto table = std::get<0>(res);
                    auto idx = std::get<1>(res);

                    if (table == nullptr)
                    {
                        reg.RegisterIfError("Static field not found: " + fdName);
                        return std::vector<IL>();
                    }
                    IL immIL, imm2IL, instIL;
                    immIL.inst = table;
                    imm2IL.i = idx;
                    instIL.inst = opFn;
                    translated.push_back(std::move(instIL)); //Op
                    translated.push_back(std::move(immIL));  //Type
                    translated.push_back(std::move(imm2IL)); //Idx
                }
                    break;

                //2 immediate args: type, fnName:
                case OpCode::callstatic:
                case OpCode::ldstaticfn:
                    {
                    auto name = std::get<std::string>(line.oprand);
                    auto res = ResolveFnName(name, libs);
                    auto table = std::get<0>(res);
                    auto idx = std::get<1>(res);
                    if (idx < 0)
                    {
                        reg.RegisterIfError("Function not found: " + name);
                        return std::vector<IL>();
                    }
                    IL immIL, imm2IL, instIL;
                    immIL.inst = table;
                    imm2IL.i = idx;
                    instIL.inst = opFn;
                    translated.push_back(std::move(instIL));  //Op
                    translated.push_back(std::move(immIL));   //Type
                    translated.push_back(std::move(imm2IL));  //Idx
                    }
                    break;

                //Compiler directives:
                case OpCode::d_embed:
                {
                    auto name = std::get<std::string>(line.oprand);
                    auto res = ResolveFnName(name, libs);
                    reg.Log("Embedding function:" + name);
                    auto table = std::get<0>(res);
                    auto idx = std::get<1>(res);
                    if (idx < 0)
                    {
                        reg.RegisterIfError("Function not found: " + name);
                        return std::vector<IL>();
                    }

                    auto fnBlk = table->methodTable[idx];
                    if(!fnBlk->isEmbeddable)
                    {
                        reg.RegisterIfError("Function not embeddable: " + name);
                        return std::vector<IL>();
                    }
                    //IL immIL = fnBlk->body[0];
                    translated.push_back(fnBlk->body[0]);
                }
                    break;

                default:
                    IL il;
                    il.inst = opFn;
                    translated.push_back(std::move(il));
                    break;
                }
            }

            //msg = "Success";
            return translated;
        }
    );
}

std::tuple<TypeTable*, MethodBlock*> LibraryLoader::LookupFunction(const std::string& name)
{
    //libName|typeName|fnName or typeName|fnName
    auto parts = SplitFullName(name);
    if (parts.size() <= 1)return { nullptr, nullptr };
    std::string typeName, libName;
    if (parts.size() > 2)
    {
        typeName = parts[1];
        libName = parts[0];
    }
    else
    {
        typeName = parts[0];
        libName = "";
    }

    auto fnName = parts.back();
    //LibBlock* lib = nullptr;
    //for (auto& l : compiledLibs)
    //{
    //    if (l.name == libName) {
    //        lib = &l;
    //        break;
    //    }
    //}
    //if (lib == nullptr) return { nullptr, nullptr };

    auto tyFullName = libName + "|" + typeName;
    TypeTable* ty = LookupType(libName + "|" + typeName);
    //for (auto& typeInfo : lib->types)
    //{
    //    if (typeName == typeInfo->name){
    //        ty = typeInfo.get();
    //        break;
    //    }
    //}

    if(ty == nullptr) return { nullptr, nullptr };

    //for (auto& fn : ty->methodTable)
    //{
    //    if (fn->name == fnName)
    //        return {ty,fn};
    //}
    auto fnFullName = tyFullName + "|" + fnName;
    MethodBlock* fn = LookupMethod(fnFullName);

    if (fn == nullptr) return { nullptr, nullptr };

    return {ty, fn};
}
