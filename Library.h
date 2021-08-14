#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "Utils.h"

class ClassInfo;
class LibraryInfo;
class FieldInfo;
class MethodInfoBase;


LibraryInfo* Test();

// +-------------------------+
// | parent tree 0 . field 0 |    <-- slot 0
// |               . field n |
// +-------------------------+
// | parent tree 1 . field 0 |
//   ...
//
// |                         |
// +-------------------------+
// | this          . field 0 |
// |               . field n |
// +-------------------------+
//


class FieldInfo
{
public:
    ClassInfo* cls;

    std::string name;
    std::string type;

    bool isFullType;

    FieldInfo(const std::string& name, const std::string& type);

    std::string GetFullType() const;

    bool IsComplete() const;
    void SetType(const std::string& type);
};

class ProgramMethod;


class MethodInfoBase
{
public:
    std::string name;
    bool isStatic;
    bool isConstant;
    bool isEmbeddable;
    std::vector<FieldInfo> rets, args;

    ClassInfo* cls;

    MethodInfoBase(const std::string& name) :
        name(name),
        isStatic(false),
        isConstant(false),
        isEmbeddable(false),
        cls(nullptr) {}
    virtual ~MethodInfoBase()
    {

    }

    bool IsComplete() const;

    virtual std::vector<IL> TranslateByteCode(TrnFn tranFn) = 0;

    void MendFields(ClassInfo* cls);

    std::string GetThisType() const;
};
template<typename SubClass>
class MethodInfo:public MethodInfoBase
{
public:
    
    MethodInfo(const std::string& name):MethodInfoBase(name){}
    virtual ~MethodInfo()
    {

    }
    //virtual std::vector<IL> TranslateByteCode(TrnFn tranFn) override = 0;


    SubClass* Return(const std::string& name, const std::string& type)
    {
        rets.push_back({name, type});
        return (SubClass*)this;
    }
    SubClass* Arg(const std::string& name, const std::string& type)
    {
        args.push_back({ name, type });
        return (SubClass*)this;
    }
    SubClass* Static(bool st = true) { isStatic = st; return (SubClass*)this; }
    SubClass* Constant(bool ct = true) { isConstant = ct; return (SubClass*)this; }

};

class ProgramMethod:public MethodInfo<ProgramMethod>
{
public:
    std::vector<Instruction> body;
    ProgramMethod(const std::string& name):MethodInfo(name)
    {
        //Program methods are not embeddable
        isEmbeddable = false;
    }

    std::vector<IL> TranslateByteCode(TrnFn tranFn) override
    {
        return tranFn(body);
    }

    ProgramMethod* Body(std::vector<Instruction>&& body){ this->body = std::move(body); return this;}
};

class HostMethod :public MethodInfo<HostMethod>
{
public:
    HostMethod(const std::string& name, InstFn fn) :MethodInfo(name), hostFunc(fn)
    {
        isEmbeddable = true;
    }
    InstFn hostFunc;

    virtual std::vector<IL> TranslateByteCode(TrnFn tranFn) override
    {
        //We just need to translate RET
        std::vector<IL> body;
        static const std::vector<Instruction> dummyRet = { {OpCode::RET,0} };
        auto tran = tranFn(dummyRet);
        IL il;
        il.inst = (void*)hostFunc;
        body.push_back(std::move(il));
        body.insert(body.end(), tran.begin(), tran.end());
        return body;
    }
};

class ClassInfo
{

public:

    std::string name;
    std::string parent;
    bool isReferenceType;
    bool isImplicitConstructable;
    //todo: interface maps..
    //std::vector<std::string> ifaces;
    std::vector<std::shared_ptr<MethodInfoBase>> methods;
    std::vector<FieldInfo> fields, staticFields;

    LibraryInfo* lib;

    ClassInfo(const std::string& name, bool implicitCtor = true)
        :name(name), isReferenceType(false), isImplicitConstructable(implicitCtor), lib(nullptr){}
    ~ClassInfo()
    {
        for(auto& f : methods)
        {
            f->cls = nullptr;
        
            for(auto& arg : f->args)
            {
                arg.cls = nullptr;
            }

            for (auto& ret : f->rets)
            {
                ret.cls = nullptr;
            }
        }
    }

    bool IsComplete()const{return lib!= nullptr;}

    std::string GetFullName() const;
    std::string GetParentName() const;

    ClassInfo* Field(FieldInfo&& field){ field.cls = this; fields.push_back(field); return this;}
    ClassInfo* StaticField(FieldInfo&& field) { field.cls = this; staticFields.push_back(field); return this; }

    void AddMethod(std::shared_ptr<MethodInfoBase> method)
    {
        method->cls = this;
        method->MendFields(this);
        methods.push_back(method);
    }
    ClassInfo* Method(MethodInfoBase* method)
    {
        method->cls = this;
        method->MendFields(this);
        methods.emplace_back(method); return this;
    }
    ClassInfo* StaticMethod(MethodInfoBase* method)
    {
        method->isStatic = true;
        method->MendFields(this);
        return Method(method);
    }
    ClassInfo* RefType(bool isRefType = true){isReferenceType = isRefType; return this;}
    std::string GetLibName() const;
};

class LibraryInfo
{
public:
    std::string name;

    std::vector<std::shared_ptr<ClassInfo>> types;
    std::vector<std::string> deps;

    std::vector<std::string> GetDeps(){auto d =deps; d.push_back(name); return d;}

    LibraryInfo(const std::string& name):name(name){}
    ~LibraryInfo()
    {
        for(auto& ty : types)
        {
            ty->lib = nullptr;
        }
    }

    void AddClass(std::shared_ptr<ClassInfo> cls)
    {
        cls->lib = this; types.push_back(cls);
    }

    LibraryInfo* Class(ClassInfo* cls) {cls->lib = this; types.emplace_back(cls); return this; }
    LibraryInfo* Deps(std::initializer_list<std::string> dep) { deps = dep; return this; }
    LibraryInfo* Dep(const std::string& dep){deps.push_back(dep);return this;}
};


class TypeTable;
class MethodBlock;

class LibBlock
{
public:
    std::string name;
    std::vector<std::unique_ptr<TypeTable>> types;
};
//The "compiled" types
class TypeTable
{
public:
    std::string name;
    TypeTable* parentType;
    bool isReferenceType;
    bool isImplicitConstructable;
    virtual bool IsReferenceType()const{return isReferenceType;}
    //Interface map
    std::vector<MethodBlock*> methodTable;
    std::vector<TypeTable*> fields, staticFields;

    virtual ~TypeTable(){}
};
//The "compiled" methods
class MethodBlock
{
public:
    std::string name;
    bool isStatic;
    bool isEmbeddable;
    std::vector<TypeTable*> args, rets;
    std::vector<IL> body;
};


//Handling type tables and others...
class LibraryLoader
{
public:
    const static std::string hostWrapperLibName;
    LibraryLoader()
    {
        hostWrapper.name = hostWrapperLibName;
    }

    std::vector<std::shared_ptr<LibraryInfo>> libs;
    std::vector<LibBlock> compiledLibs;
    std::vector<std::unique_ptr<MethodBlock>> compiledMethods;
    LibBlock hostWrapper;

    class _StatReg
    {
    public:

        std::vector<std::tuple<
            int,         //Severity 0:log 1:warning 2:error
            std::string  //Message
            >> reg;
        bool hasError = false;
        bool HasError() const { return hasError; }
        void RegisterIfError(const std::string& msg) {
            if (msg == "")return; reg.push_back(std::make_tuple(2, msg)); hasError = true;
        }
        void Log(const std::string& msg) { reg.push_back(std::make_tuple(0, msg)); }
    };

    /**
     * \brief Resolve type info from name
     * \param name lib search format: libName|typeName; prog search format: typeName
     */
    TypeTable* FindTypeByName(const std::string& name, const std::string& libName = "");
    ClassInfo* FindClassByName(const std::string& name, const std::string& libName = "");

    TypeTable* _ResolveTypeName(const std::string& name, const std::string& thisLib);
    TypeTable* ResolveTypeName(const std::string& name, std::vector<std::string>& libs)
    {
        for(auto& lib : libs)
        {
            if(TypeTable* ty = _ResolveTypeName(name, lib))
            {
                return ty;
            }
        }

        return nullptr;
    }

    //Function table for types

    std::tuple<TypeTable*, int> _ResolveFnName(const std::string& name, const std::string& thisLib);
    std::tuple<TypeTable*, int> ResolveFnName(const std::string& name, std::vector<std::string>& libs)
    {
        for (auto& lib : libs)
        {
            auto res = _ResolveFnName(name, lib);
            if (std::get<0>(res) != nullptr)
            {
                return res;
            }
        }

        return {nullptr, -1};
    }


    //Function table for types

    int _ResolveMemberName(const std::string& name, const std::string& thisLib);
    int ResolveMemberName(const std::string& name, std::vector<std::string>& libs)
    {
        for (auto& lib : libs)
        {
            auto res = _ResolveMemberName(name, lib);
            if (res >= 0)
            {
                return res;
            }
        }

        return -1;
    }


    std::tuple<TypeTable*, int> _ResolveStaticMemberName(const std::string& name, const std::string& thisLib);
    std::tuple<TypeTable*, int> ResolveStaticMemberName(const std::string& name, std::vector<std::string>& libs)
    {
        for (auto& lib : libs)
        {
            auto res = _ResolveStaticMemberName(name, lib);
            if (std::get<0>(res) != nullptr)
            {
                return res;
            }
        }

        return { nullptr, -1 };
    }


    std::vector<IL> TranslateFn(MethodInfoBase* fnInfo,
                                std::vector<std::string>& libs, LibraryLoader::_StatReg& reg);

    using TypeInfoLUT = std::unordered_map<TypeTable*, ClassInfo*>;
    using MethodBlockLUT = std::unordered_map<MethodInfoBase*, MethodBlock*>;

    TypeInfoLUT tlut; MethodBlockLUT mlut;

    std::unordered_map<std::string, TypeTable*> typeMap;
    std::unordered_map<std::string, MethodBlock*> methodMap;
    std::unordered_map<std::string, int> fieldMap, staticFieldMap;

    void ClearCompiled()
    {
        //All luts
        tlut.clear(); mlut.clear();

        //Maps
        typeMap.clear(); methodMap.clear();
        fieldMap.clear(); staticFieldMap.clear();

        //Compiled bins
        compiledMethods.clear();
        compiledLibs.clear();
    }

    void RegisterAllTypes()
    {
        for(auto& lib:libs)
        {
            compiledLibs.emplace_back();

            LibBlock& block = compiledLibs.back();
            block.name = lib->name;
            for(auto& type : lib->types)
            {
                TypeTable* table = new TypeTable();
                table->name = type->name;
                table->isReferenceType = type->isReferenceType;
                table->isImplicitConstructable = type->isImplicitConstructable;
                block.types.emplace_back(table);

                auto typeName = lib->name + "|" + type->name;

                tlut[table] = type.get();
                typeMap[typeName] = table;

                for(auto& fn: type->methods)
                {
                    MethodBlock* fnBlk = new MethodBlock();
                    compiledMethods.emplace_back(fnBlk);
                    fnBlk->name = fn->name;
                    fnBlk->isStatic = fn->isStatic;
                    fnBlk->isEmbeddable = fn->isEmbeddable;
                    fnBlk->args.resize(fn->args.size());
                    fnBlk->rets.resize(fn->rets.size());

                    mlut[fn.get()] = fnBlk;

                }
            }
        }

    }

    TypeTable* LookupType(const std::string& name) const
    {
        auto tyIter = typeMap.find(name);
        if(tyIter == typeMap.end())
        {
            return nullptr;
        }

        return tyIter->second;
    }


    MethodBlock* LookupMethod(const std::string& name) const
    {
        auto fnIter = methodMap.find(name);
        if (fnIter == methodMap.end())
        {
            return nullptr;
        }

        return fnIter->second;
    }


    int LookupFieldIndex(const std::string& name) const
    {
        auto fdIter = fieldMap.find(name);
        if (fdIter == fieldMap.end())
        {
            return -1;
        }

        return fdIter->second;
    }


    int LookupStaticFieldIndex(const std::string& name) const
    {
        auto fdIter = staticFieldMap.find(name);
        if (fdIter == staticFieldMap.end())
        {
            return -1;
        }

        return fdIter->second;
    }

    void PopulateFields(
        TypeTable* src,
        TypeTable* dst,
        const std::string& thisLib,
        _StatReg& sreg,
        std::unordered_map<std::string, int>& fieldLut,
        std::unordered_map<std::string, int>& sfieldLut
    )
    {
        if (src->parentType != nullptr){
            PopulateFields(src->parentType, dst, thisLib, sreg, fieldLut, sfieldLut);
        }

        auto info = tlut[src];

        //Populate fields
        for (auto& field : info->fields)
        {
            auto& typeName = field.type;
            auto typTabl = _ResolveTypeName(typeName, thisLib);
            if(typTabl == nullptr) sreg.RegisterIfError("Unknown type:" + typeName);
            int idx = dst->fields.size();
            fieldLut[field.name] = idx;
            dst->fields.push_back(typTabl);
        }
        //And static fields
        for (auto& field : info->staticFields)
        {
            auto& typeName = field.type;
            auto typTabl = _ResolveTypeName(typeName, thisLib);
            if (typTabl == nullptr) sreg.RegisterIfError("Unknown type:" + typeName);
            int idx = dst->fields.size();
            sfieldLut[field.name] = idx;
            dst->staticFields.push_back(typTabl);
        }
    }

    void PopulateMethodTable(
        TypeTable* src,
        TypeTable* dst,
        const std::string& thisLib,
        std::unordered_map<std::string, int>& tableLut
    )
    {
        if (src->parentType != nullptr) {
            PopulateMethodTable(src->parentType, dst, thisLib, tableLut);
        }

        auto info = tlut[src];

        for (auto& fn: info->methods)
        {
            auto& fnName = fn->name;
            auto fnBlk = mlut[fn.get()];

            auto fnIdxIter = tableLut.find(fnName);
            if(fnIdxIter != tableLut.end())
            {
                auto fnIdx = (*fnIdxIter).second;
                dst->methodTable[fnIdx] = fnBlk;
            }else
            {
                auto id = dst->methodTable.size();
                dst->methodTable.push_back(fnBlk);
                tableLut[fnName] = id;
                //TODO:Is argtype really needed?
                //std::string errMsg;
                //cmpFn->body = TranslateFn(origFn.get(), origLib->name, errMsg);
            }

            
        }
    }

    _StatReg CompileRegistered()
    {
        _StatReg sreg;
        //Populate tables
        for(decltype(libs.size()) il = 0; il < libs.size(); il++)
        {
            auto& origLib = libs[il];
            auto& cmpLib = compiledLibs[il];
            sreg.Log("Entering library:" + origLib->name);

            for(decltype(origLib->types.size()) it = 0; it < origLib->types.size(); it++)
            {
                auto origType = origLib->types[it].get();
                auto& cmpType = cmpLib.types[it];

                auto typeName = origLib->name + "|" + origType->name;
                sreg.Log("Registering class:" + origType->name);

                //parent info
                auto parentType = _ResolveTypeName(origType->parent, origLib->name);
                cmpType->parentType = parentType;

                ////Populate fields
                std::unordered_map<std::string, int> fieldIdx, sfieldIdx;

                PopulateFields(cmpType.get(), cmpType.get(), origLib->name, sreg, fieldIdx, sfieldIdx);

                //Populate tables
                for (auto& kv : fieldIdx)
                {
                    auto fdName = typeName + "|" + kv.first;
                    auto fdIdx = kv.second;
                    fieldMap[fdName] = fdIdx;
                }

                for (auto& kv : sfieldIdx)
                {
                    auto fdName = typeName + "|" + kv.first;
                    auto fdIdx = kv.second;
                    staticFieldMap[fdName] = fdIdx;
                }

                std::unordered_map<std::string, int> methodTableIdx;
                //And functions
                PopulateMethodTable(cmpType.get(), cmpType.get(), origLib->name, methodTableIdx);

                //Populate tables
                for(auto& kv:methodTableIdx)
                {
                    auto fnName = typeName + "|" + kv.first;
                    auto fnIdx = kv.second;
                    auto fnBlk = cmpType->methodTable[fnIdx];
                    methodMap[fnName] = fnBlk;
                }
            }
        }

        //Compile functions
        for (auto& lib : libs)
        {
            for (auto& type : lib->types)
            {

                for (auto& fn : type->methods)
                {
                    sreg.Log("Compiling method:" + lib->name + "|" + type->name + "|" + fn->name);

                    MethodBlock* fnBlk = mlut[fn.get()];

                    std::string errMsg;
                    auto deps = lib->GetDeps();
                    fnBlk->body = TranslateFn(fn.get(),deps ,sreg);
                    sreg.RegisterIfError(errMsg);

                }
            }
        }

        return sreg;
    }

    _StatReg Compile()
    {
        //Allocate holders
        RegisterAllTypes();

        //Populate holders
        return CompileRegistered();
    }

    std::tuple<TypeTable*, MethodBlock*> LookupFunction(const std::string& name);


    void RegisterHostWrapper(
        const std::string& name, 
        std::vector<std::tuple<const std::string,InstFn, int, int>> methods
    )
    {
        //TypeTable* table = new TypeTable();
        //table->name = name;
        //table->isReferenceType = true;
        //
        //auto typeName = hostWrapperLibName + "|" + name;
        //
        //typeMap[typeName] = table;
        //
        //for (auto& fn : methods)
        //{
        //    auto fnName = std::get<0>(fn);
        //    auto fnPtr = std::get<1>(fn);
        //    auto fnArgCnt = std::get<2>(fn);
        //    auto fnRetCnt = std::get<3>(fn);
        //
        //    auto fnFullName = typeName + "|" + fnName;
        //    MethodBlock* fnBlk = new MethodBlock();
        //    compiledMethods.emplace_back(fnBlk);
        //
        //    fnBlk->name = fnName;
        //    fnBlk.
        //}
    }
};
