#include "RuntimeLibs.h"

#include "Interpreter.h"
#include <cmath>

/* ValueType.data:
*       std::int32_t value;
        float fval;
        void* pval;//Dummy padding
        ExtRef<StaticFields>* obj;
 */
#define FLOAT(vt) (vt).data.fval
#define INT32(vt) (vt).data.value
#define PTR(vt) (vt).data.pval
#define INST(vt) (vt).data.obj

#define uname(type, name) _##type##_UN_##name
#define bname(type, name) _##type##_BIN_##name

#define UnaryMethod(type, name, op)      \
    void uname(type, name) (Interpreter* intp) { \
        auto& x = type(intp->valueStack.back()); \
        (op);\
    }
#define UNEXP_OP(op) (x = op x)
#define UNEXP_FN(op) (x = op(x))

#define UnaryOpMethod(type, name, op) UnaryMethod(type, name, UNEXP_OP(op))
#define UnaryFnMethod(type, op) UnaryMethod(type, op, UNEXP_FN(op))

#define BinaryMethod(type, name, op)      \
    void bname(type, name) (Interpreter* intp) { \
        auto last = intp->valueStack.end();\
        auto& x = type(*(last - 2)); \
        auto& y = type(*(last - 1)); \
        (op); \
        intp->valueStack.pop_back();\
    }

#define BINEXP_OP(op) (x = x op y)
#define BINEXP_FN(op) (x = op(x, y))

#define BinaryOpMethod(type, name, op) BinaryMethod(type, name, BINEXP_OP(op))
#define BinaryFnMethod(type, op) BinaryMethod(type, op, BINEXP_FN(op))

#define FloatBinOp(name, op) BinaryOpMethod(FLOAT, name, op)
#define FloatBinFn(name) BinaryFnMethod(FLOAT, name)
#define FloatUnOp(name, op) UnaryOpMethod(FLOAT, name, op)
#define FloatUnFn(name) UnaryFnMethod(FLOAT, name)


#define I32BinOp(name, op) BinaryOpMethod(INT32, name, op)
#define I32BinFn(name) BinaryFnMethod(INT32, name)
#define I32UnOp(name, op) UnaryOpMethod(INT32, name, op)
#define I32UnFn(name) UnaryFnMethod(INT32, name)

#define FOREACH_ARITH(V) \
    V(add, +) \
    V(sub, -) \
    V(mul, *) \
    V(div, /)

FOREACH_ARITH(FloatBinOp)
FOREACH_ARITH(I32BinOp)
FloatUnOp(neg, -)
FloatUnOp(dec, -1+)
FloatUnOp(inc, 1+)
I32UnOp(neg, -)
I32UnOp(dec, -1+)
I32UnOp(inc, 1+)
I32BinOp(mod, %)
I32BinOp(lsh, <<)
I32BinOp(rsh, >>)

//Logical
#define LOGICAL_BIN(V)\
    V(and, &&)\
    V(or,  ||)\
    V(bitwise_and,&)\
    V(bitwise_or,|)\
    V(bitwise_xor,^)\

LOGICAL_BIN(I32BinOp)
void bname(INT32, xor) (Interpreter* intp) {
    auto last = intp->valueStack.end();
    auto& x = INT32(*(last - 2));
    auto& y = INT32(*(last - 1));
    x = (!x) != (!y);
    intp->valueStack.pop_back();
}

#define LOGICAL_UN(V)\
    V(not,  !)\
    V(bitwise_not, ~)\

LOGICAL_UN(I32UnOp)



//Floating point arithmetic
//Trigonometry
#define FOREACH_TRIG(V) \
    V(sin)\
    V(sinh)\
    V(asin)\
    V(asinh)\
    V(cos)\
    V(cosh)\
    V(acos)\
    V(acosh)\
    V(tan)\
    V(tanh)\
    V(atan)\
    V(atanh)

FOREACH_TRIG(FloatUnFn)
FloatBinFn(atan2)

//Common
#define FOREACH_COMMON(V)\
    V(exp)\
    V(log)\
    V(log10)\
    V(sqrt)\
    V(cbrt)

FOREACH_COMMON(FloatUnFn)
FloatBinFn(pow)
FloatBinFn(fmod)



//Comparison
#define CmpMethod(type, name, op)      \
    void bname(type, name) (Interpreter* intp) { \
        auto last = intp->valueStack.end();\
        auto& x = type(*(last - 2)); \
        auto& y = type(*(last - 1)); \
        INT32(*(last - 2)) = (x)op(y); \
        intp->valueStack.pop_back(); \
    }

#define CMP(V) \
    V(greater_than, >)\
    V(less_than,    <)\
    V(equal,        ==)\
    V(not_greater,  <=)\
    V(not_less,     >=)\
    V(not_equal,    !=)

#define I32CmpOp(name, op) CmpMethod(INT32, name, op)
#define FloatCmpOp(name, op) CmpMethod(FLOAT, name, op)

CMP(I32CmpOp)
CMP(FloatCmpOp)

//Conversion
//    V(floor)\
//    V(ceil)\
//    V(round)\

//Comparison
#define F2ICvt(op)      \
    void uname(FLOAT, op) (Interpreter* intp) { \
        float x = FLOAT(intp->valueStack.back()); \
        INT32(intp->valueStack.back()) = op(x); \
        intp->valueStack.pop_back(); \
    }

#define F2I(V)\
    V(floor)\
    V(ceil)\
    V(round)

F2I(F2ICvt)

//Truncate
//F2I
void uname(FLOAT, truncate)(Interpreter* intp){
    float x = FLOAT(intp->valueStack.back());
    INT32(intp->valueStack.back()) = x;
    intp->valueStack.pop_back();
}
//I2f

void uname(INT32, to_float)(Interpreter* intp) {
    float x = INT32(intp->valueStack.back());
    FLOAT(intp->valueStack.back()) = x;
    intp->valueStack.pop_back();
}



#define I32BinWrapper(name, ...) \
    ->Method((new HostMethod(#name, &bname(INT32, name))) \
        ->Static()->Constant()\
        ->Arg("a", "Int") \
        ->Arg("b", "Int") \
        ->Return("res", "Int"))

#define I32UnWrapper(name, ...) \
    ->Method((new HostMethod(#name, &uname(INT32, name))) \
        ->Static()->Constant()\
        ->Arg("x", "Int") \
        ->Return("res", "Int"))

#define FloatBinWrapper(name, ...) \
    ->Method((new HostMethod(#name, &bname(FLOAT, name))) \
        ->Static()->Constant()\
        ->Arg("a", "Float") \
        ->Arg("b", "Float") \
        ->Return("res", "Float"))
#define FloatUnWrapper(name, ...) \
    ->Method((new HostMethod(#name, &uname(FLOAT, name))) \
        ->Static()->Constant()\
        ->Arg("x", "Float") \
        ->Return("res", "Float"))

#define FloatCmpWrapper(name, ...) \
    ->Method((new HostMethod(#name, &bname(FLOAT, name))) \
        ->Static()->Constant()\
        ->Arg("a", "Float") \
        ->Arg("b", "Float") \
        ->Return("res", "Int"))

#define F2IWrapper(name, ...) \
    ->Method((new HostMethod(#name, &uname(FLOAT, name))) \
        ->Static()->Constant()\
        ->Arg("from", "Float") \
        ->Return("to", "Int"))

#define I2FWrapper(name, ...) \
    ->Method((new HostMethod(#name, &uname(INT32, name))) \
        ->Static()->Constant()\
        ->Arg("from", "Int") \
        ->Return("to", "Float"))

std::shared_ptr<LibraryInfo> RuntimeLibs::Num()
{
    std::shared_ptr<LibraryInfo> numLib( (new LibraryInfo("Num"))
        ->Class((new ClassInfo("Float"))
            ->RefType(false)
            FloatBinWrapper(add)
            FloatBinWrapper(sub)
            FloatBinWrapper(mul)
            FloatBinWrapper(div)
            FloatBinWrapper(fmod)
            FloatUnWrapper(inc)
            FloatUnWrapper(dec)
            FloatUnWrapper(neg)
            FOREACH_TRIG(FloatUnWrapper)
            FOREACH_COMMON(FloatUnWrapper)
            FloatBinWrapper(atan2)
            FloatBinWrapper(pow)

            CMP(FloatCmpWrapper)

            //Conversion
            F2I(F2IWrapper)
            F2IWrapper(truncate)
        )
        ->Class((new ClassInfo("Int"))
            ->RefType(false)
            I32BinWrapper(add)
            I32BinWrapper(sub)
            I32BinWrapper(mul)
            I32BinWrapper(div)
            I32BinWrapper(mod)
            I32BinWrapper(lsh)
            I32BinWrapper(rsh)
            I32UnWrapper(inc)
            I32UnWrapper(dec)
            I32UnWrapper(neg)
            LOGICAL_BIN(I32BinWrapper)
            I32BinWrapper(xor)
            LOGICAL_UN(I32UnWrapper)
            CMP(I32BinWrapper)
            //to float
            I2FWrapper(to_float)
        )
        );
    return numLib;
}


template<int _Dim>
void VecLen(Interpreter* intp)
{
    auto x = (ValueType*)intp->valueStack.back().data.obj;
    auto fltType = x[0].type;

    float len = 0;
    for(int i = 0; i < _Dim; i++)
    {
        len += FLOAT(x[i]) * FLOAT(x[i]);
    }

    len = std::sqrt(len);
    intp->valueStack.back() = ValueType(fltType);
    intp->valueStack.back().data.fval = len;
}


template<int _Dim>
void VecLen2(Interpreter* intp)
{
    auto x = (ValueType*)intp->valueStack.back().data.obj;
    auto fltType = x[0].type;

    float len = 0;
    for (int i = 0; i < _Dim; i++)
    {
        len += FLOAT(x[i]) * FLOAT(x[i]);
    }

    intp->valueStack.back() = ValueType(fltType);
    intp->valueStack.back().data.fval = len;
}

#define VecBinaryMethodWrapper(name, body) \
void name(Interpreter* intp){ \
    auto last = intp->valueStack.end() - 1;\
    auto x = (ValueType*)(*(last - 1)).data.obj;\
    auto y = (ValueType*)(*(last)).data.obj;\
    /*auto fltType = x[0].type;*/\
    auto vecType = last->type;\
    auto newVec = intp->NewRefTypeObject(vecType);\
    auto r = (ValueType*)newVec.data.obj; \
    body \
    intp->valueStack.pop_back(); \
    intp->valueStack.back() = newVec; \
}

#define VecUnaryMethodWrapper(name, body) \
void name(Interpreter* intp){ \
    auto x = (ValueType*)intp->valueStack.back().data.obj;\
    /*auto fltType = x[0].type;*/\
    auto vecType = intp->valueStack.back().type;\
    auto newVec = intp->NewRefTypeObject(vecType);\
    auto r = (ValueType*)newVec.data.obj; \
    body \
    intp->valueStack.back() = newVec; \
}

#define VecBinaryOPExpr(op, a, b) (a) op (b)
#define VecBinaryFnExpr(op, a, b) op((a), (b))
#define VecUnaryOPExpr(op, a) op (a)
#define VecUnaryFnExpr(op, a) op((a))

#define Vec2BinaryBody(expr, op) \
    FLOAT(r[0]) = expr(op, FLOAT(x[0]), FLOAT(y[0]) ); \
    FLOAT(r[1]) = expr(op, FLOAT(x[1]), FLOAT(y[1]) );

#define Vec2UnaryBody(expr, op) \
    FLOAT(r[0]) = expr(op, FLOAT(x[0]) ); \
    FLOAT(r[1]) = expr(op, FLOAT(x[1]) );

#define Vec3BinaryBody(expr, op) \
    FLOAT(r[0]) = expr(op, FLOAT(x[0]), FLOAT(y[0]) ); \
    FLOAT(r[1]) = expr(op, FLOAT(x[1]), FLOAT(y[1]) ); \
    FLOAT(r[2]) = expr(op, FLOAT(x[2]), FLOAT(y[2]) );

#define Vec3UnaryBody(expr, op) \
    FLOAT(r[0]) = expr(op, FLOAT(x[0]) ); \
    FLOAT(r[1]) = expr(op, FLOAT(x[1]) ); \
    FLOAT(r[2]) = expr(op, FLOAT(x[2]) ); 

#define Vec4BinaryBody(expr, op) \
    FLOAT(r[0]) = expr(op, FLOAT(x[0]), FLOAT(y[0]) ); \
    FLOAT(r[1]) = expr(op, FLOAT(x[1]), FLOAT(y[1]) ); \
    FLOAT(r[2]) = expr(op, FLOAT(x[2]), FLOAT(y[2]) ); \
    FLOAT(r[3]) = expr(op, FLOAT(x[3]), FLOAT(y[3]) );

#define Vec4UnaryBody(expr, op) \
    FLOAT(r[0]) = expr(op, FLOAT(x[0]) ); \
    FLOAT(r[1]) = expr(op, FLOAT(x[1]) ); \
    FLOAT(r[2]) = expr(op, FLOAT(x[2]) ); \
    FLOAT(r[3]) = expr(op, FLOAT(x[3]) );

#define VecMethodName(dim, type, name) _FVEC##dim##_##type##_##name
#define VecBinMethodName(dim, name) VecMethodName(dim, BIN, name)
#define VecUnMethodName(dim, name) VecMethodName(dim, UN, name)

#define VecBinaryMethod(name, expr, op) \
    VecBinaryMethodWrapper(VecBinMethodName(2,name), Vec2BinaryBody(expr, op))\
    VecBinaryMethodWrapper(VecBinMethodName(3,name), Vec3BinaryBody(expr, op))\
    VecBinaryMethodWrapper(VecBinMethodName(4,name), Vec4BinaryBody(expr, op))

#define VecUnaryMethod(name, expr, op) \
    VecUnaryMethodWrapper(VecUnMethodName(2,name), Vec2UnaryBody(expr, op))\
    VecUnaryMethodWrapper(VecUnMethodName(3,name), Vec3UnaryBody(expr, op))\
    VecUnaryMethodWrapper(VecUnMethodName(4,name), Vec4UnaryBody(expr, op))

#define VecBinaryOP(name, op) VecBinaryMethod(name, VecBinaryOPExpr, op)
#define VecUnaryOP(name, op) VecUnaryMethod(name, VecUnaryOPExpr, op)


#define VecBinaryFn(name) VecBinaryMethod(name, VecBinaryFnExpr, name)
#define VecUnaryFn(name) VecUnaryMethod(name, VecUnaryFnExpr, name)


FOREACH_ARITH(VecBinaryOP)
VecUnaryOP(neg, -)

FOREACH_TRIG(VecUnaryFn)
FOREACH_COMMON(VecUnaryFn)

#define VecBinWrapper(dim, name, ...) \
    ->Method((new HostMethod(#name, &VecBinMethodName(dim, name))) \
        ->Static()->Constant()\
        ->Arg("a", "Vec|Vec"#dim) \
        ->Arg("b", "Vec|Vec"#dim) \
        ->Return("res", "Vec|Vec"#dim))

#define VecUnWrapper(dim, name, ...) \
    ->Method((new HostMethod(#name, &VecUnMethodName(dim, name))) \
        ->Static()->Constant()\
        ->Arg("x", "Vec|Vec"#dim) \
        ->Return("res", "Vec|Vec"#dim))

#define Vec2BinWrapper(name, ...) VecBinWrapper(2, name, ...)
#define Vec3BinWrapper(name, ...) VecBinWrapper(3, name, ...)
#define Vec4BinWrapper(name, ...) VecBinWrapper(4, name, ...)
#define Vec2UnWrapper(name, ...) VecUnWrapper(2, name, ...)
#define Vec3UnWrapper(name, ...) VecUnWrapper(3, name, ...)
#define Vec4UnWrapper(name, ...) VecUnWrapper(4, name, ...)

#define VecMethodInfos(dim) \
    FOREACH_ARITH(Vec##dim##BinWrapper)\
    Vec##dim##UnWrapper(neg)\
    FOREACH_TRIG(Vec##dim##UnWrapper)\
    FOREACH_COMMON(Vec##dim##UnWrapper)

std::shared_ptr<LibraryInfo> RuntimeLibs::Vec()
{
    static std::shared_ptr<LibraryInfo> vecLib ( (new LibraryInfo("Vec"))
        ->Class((new ClassInfo("Vec2"))
            ->RefType(true)
            ->Field(FieldInfo("x", "Num|Float"))
            ->Field(FieldInfo("y", "Num|Float"))
            VecMethodInfos(2)
            ->Method((new HostMethod("length", &VecLen<2>)) \
                ->Static()->Constant()\
                ->Arg("x", "Vec|Vec2") \
                ->Return("res", "Num|Float"))
            ->Method((new HostMethod("length2", &VecLen2<2>)) \
                ->Static()->Constant()\
                ->Arg("x", "Vec|Vec2") \
                ->Return("res", "Num|Float"))
        )
        ->Class((new ClassInfo("Vec3"))
            ->RefType(true)
            ->Field(FieldInfo("x", "Num|Float"))
            ->Field(FieldInfo("y", "Num|Float"))
            ->Field(FieldInfo("z", "Num|Float"))
            VecMethodInfos(3)
            ->Method((new HostMethod("length", &VecLen<3>)) \
                ->Static()->Constant()\
                ->Arg("x", "Vec|Vec3") \
                ->Return("res", "Num|Float"))
            ->Method((new HostMethod("length2", &VecLen2<3>)) \
                ->Static()->Constant()\
                ->Arg("x", "Vec|Vec3") \
                ->Return("res", "Num|Float"))
        )
        ->Class((new ClassInfo("Vec4"))
            ->RefType(true)
            ->Field(FieldInfo("x", "Num|Float"))
            ->Field(FieldInfo("y", "Num|Float"))
            ->Field(FieldInfo("z", "Num|Float"))
            ->Field(FieldInfo("w", "Num|Float"))
            VecMethodInfos(4)
            ->Method((new HostMethod("length", &VecLen<4>)) \
                ->Static()->Constant()\
                ->Arg("x", "Vec|Vec4") \
                ->Return("res", "Num|Float"))
            ->Method((new HostMethod("length2", &VecLen2<4>)) \
                ->Static()->Constant()\
                ->Arg("x", "Vec|Vec4") \
                ->Return("res", "Num|Float"))
        )
        );

    return vecLib;
}
