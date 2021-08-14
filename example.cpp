#include <memory>
#include <iostream>

//#include "Utils.h"
#include "Interpreter.h"
#include "Interop.h"
#include "Library.h"
#include "RuntimeLibs.h"


class WrappedHostType :public RefCntHostObj
{
public:
    std::string someStr;
    int someInt;

    static WrappedHostType* Create()
    {
        auto res = new WrappedHostType();
        res->someStr = "Initial content";
        res->someInt = 0;
        return res;
    }

    void SetStr()
    {
        someStr = "Modded content";
    }

    void AddInt(int val)
    {
        someStr += std::to_string(val);
        someInt = val;
    }

    int GiveAInt()
    {
        return someInt;
    }
};

template<>
struct ArgCvt< WrappedHostType*>
{
    static std::string GetName() { return "|WrappedHostType"; }
    
    static WrappedHostType* FromVal(ValueType& val)
    {
        return (WrappedHostType *) val.data.inst->get();
    }
    
    static ValueType ToVal(
        WrappedHostType* val, 
        Interpreter* intp
    )
    {
        

    	ValueType ival(
        intp->libLoader.LookupType(GetName())
           );
    	ival.data.inst = intp->AcceptHeapObject(val);;
    	return ival;
    }
};


#define FOR_EACH_FN(V) \
    V(WrappedHostType, Create)\
    V(WrappedHostType, SetStr)\
    V(WrappedHostType, AddInt)\
    V(WrappedHostType, GiveAInt)

FOR_EACH_FN(HOST_METHOD_WRAPPER)


int main()
{
    std::cout << "Hello World!\n";

    Test();

    /*************Int Class***************/

    auto prog = (new LibraryInfo(""))
        ->Deps({ "Num", "Vec" })
        ->Class((new ClassInfo("Program"))
            ->StaticField(FieldInfo("a", "Num|Int"))
            ->StaticField(FieldInfo("b", "Num|Int"))
            ->StaticField(FieldInfo("c", "Num|Int"))
            ->StaticField(FieldInfo("V","Vec|Vec3"))
            ->Method((new ProgramMethod("main"))
                ->Static()
                ->Body({
                    {OpCode::ldstatic, "Program|a"},      //a_t = a           ;load a from static fields
                    {OpCode::callstatic, "Num|Int|inc"},//a_t += 1
                    {OpCode::ldstatic, "Program|b"},      //b_t = b
                    {OpCode::callstatic, "Num|Int|inc"},//b_t += 1
                    {OpCode::callstatic, "Num|Int|inc"},//b_t += 1          ;should be 2
                    {OpCode::callstatic, "Num|Int|add"},//c_t = b_t + a_t   ;should be 3
                    {OpCode::ststatic, "Program|c"},      //c = c_t    ;should be 3
                    {OpCode::RET},
                    {OpCode::HLT},
                })

            )
            ->Method((new ProgramMethod("vecTest"))
                ->Static()
                ->Body({
                    //{OpCode::PUSH, 1}, // One local variable
                    {OpCode::ldstatic, "Program|V"},
                    {OpCode::ldmem, "Vec|Vec3|x"},
                    {OpCode::d_embed, "Num|Float|inc"},
                    {OpCode::ldstatic, "Program|V"},
                    {OpCode::stmem, "Vec|Vec3|x"},

                    {OpCode::newobj, "Vec|Vec3"},//Stack should be zero
                    {OpCode::ldarg, 0},
                    {OpCode::ldmem, "Vec|Vec3|z"},
                    {OpCode::d_embed, "Num|Float|inc"},
                    {OpCode::d_embed, "Num|Float|inc"},
                    {OpCode::d_embed, "Num|Float|inc"},
                    {OpCode::ldarg, 0},
                    {OpCode::stmem, "Vec|Vec3|z"},

                    //{OpCode::ldstatic, "Program|V"},

                    {OpCode::ldstatic, "Program|V"},
                    {OpCode::d_embed, "Vec|Vec3|Add"},
                    {OpCode::ststatic, "Program|V"},

                    {OpCode::RET},
                    {OpCode::HLT},
                })

            )
            ->Method((new ProgramMethod("wrapTest"))
                ->Static()
                ->Body({
                    //{OpCode::PUSH, 1}, // One local variable
                    {OpCode::callstatic, "WrappedHostType|Create"},
                    {OpCode::ldarg, 0},
                    {OpCode::callmem, "WrappedHostType|SetStr"},
                    {OpCode::PUSHIMM, 7},
                    {OpCode::ldarg, 0},
                    {OpCode::callmem, "WrappedHostType|AddInt"},
                    {OpCode::ldarg, 0},
                    {OpCode::callmem, "WrappedHostType|GiveAInt"},
                    {OpCode::ststatic, "Program|a"},

                    {OpCode::RET},
                    {OpCode::HLT},
                    })

                    )
        )
        ->Class(WRAP_HOST_TYPE(WrappedHostType)
            FOR_EACH_FN(WRAP_HOST_METHOD)
        )
        ;

    auto Num = RuntimeLibs::Num();
    auto Vec = RuntimeLibs::Vec();

    /*************************************/

    

    /*************************************/
    WrapTest();
    
    //lib->name = "TestProg";
    //lib->types.emplace_back(mainCls);
    //lib->types.emplace_back(intCls);
    //
    Interpreter intp;
    intp.LoadLibrary(std::shared_ptr<LibraryInfo>(Num));
    intp.LoadLibrary(std::shared_ptr<LibraryInfo>(Vec));
    intp.LoadLibrary(std::shared_ptr<LibraryInfo>(prog));
    //
    auto result = intp.CompileProgram();
    //
    auto fn = intp.LookupFunction("Program|wrapTest");
    //
    auto& staticFields = (*fn.GetEnv()).memberVars;

    auto vec3type = intp.libLoader.FindTypeByName("Vec3","Vec");
    staticFields[3].data.inst = intp.NewRefTypeObject(vec3type);

    
    intp.CallFunction(fn);

    bool hasError = intp.status == Interpreter::ExecutionStatus::Error;
    if(hasError)
    {
        std::cout << "Error:" << intp.errMsg << std::endl;
    }
    else
    {
        std::cout << "Finished";
    }

}

