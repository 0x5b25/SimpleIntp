#pragma once

#include "Interpreter.h"
#include "Library.h"
#include "ArgCvt.h"



template<typename T>
struct ArgCvt<T, typename std::enable_if<
	std::is_convertible<T*, StaticFields*>::value>::type>
{
	static std::string GetName() { return ""; }

	static T& FromVal(ValueType& val) { return *((T*)val.data.obj); }

	static ValueType ToVal(int val, Interpreter* intp)
	{

	}
};

template <size_t... indices>
struct IndicesHolder {};

template <size_t requested_index, size_t... indices>
struct IndicesGenerator {
	using type = typename IndicesGenerator<requested_index - 1,
		requested_index - 1,
		indices...>::type;
};

template <size_t... indices>
struct IndicesGenerator<0, indices...> {
	using type = IndicesHolder<indices...>;
};


template <typename... ArgTypes>
std::vector<std::string> GetArgTypeNames()
{
    return { ArgCvt<ArgTypes>::GetName()...};
};

template <typename T>
class IndicesForSignature {};

template <typename ResultType, typename... ArgTypes>
struct IndicesForSignature<ResultType(*)(ArgTypes...)> {
	static const size_t ArgCount = sizeof...(ArgTypes);
	using type = typename IndicesGenerator<ArgCount>::type;
	using resultType = ResultType;
	static std::vector<std::string> ArgTypeNames(){return GetArgTypeNames<ArgTypes...>();}
};

template <typename C, typename ResultType, typename... ArgTypes>
struct IndicesForSignature<ResultType(C::*)(ArgTypes...)> {
	static const size_t ArgCount = sizeof...(ArgTypes);
	using type = typename IndicesGenerator<ArgCount>::type;
	using resultType = ResultType;
	static std::vector<std::string> ArgTypeNames() { return GetArgTypeNames<ArgTypes...>(); }


};

template <typename C, typename ResultType, typename... ArgTypes>
struct IndicesForSignature<ResultType(C::*)(ArgTypes...) const> {
	static const size_t ArgCount = sizeof...(ArgTypes);
	using type = typename IndicesGenerator<ArgCount>::type;
	using resultType = ResultType;
	static std::vector<std::string> ArgTypeNames() { return GetArgTypeNames<ArgTypes...>(); }

};

template<typename T>
void ReturnHostVal(T result, Interpreter* intp)
{
    intp->valueStack.back() = ArgCvt<T>::ToVal(result, intp);
}

template <typename IndicesType, typename T>
class ArgDispatcher {};


template <size_t... indices, typename ResultType, typename... ArgTypes>
struct ArgDispatcher<IndicesHolder<indices...>, ResultType(*)(ArgTypes...)>
	//: public ArgHolder<indices, ArgTypes>... 
{
	using FunctionPtr = ResultType(*)(ArgTypes...);
	static const size_t ArgCount = sizeof...(ArgTypes);
	static const bool HasReturn = true;
	static std::vector<std::string> ArgTypeNames() { return GetArgTypeNames<ArgTypes...>(); }
	static std::string RetTypeName() { return ArgCvt<ResultType>::GetName(); }
	static const bool IsStatic = true;

	static void Dispatch(FunctionPtr func, Interpreter* intp){
		static const std::size_t argCnt = sizeof...(indices);
		//Move into template arg expansion for safer operation on empty stack
	    //auto* idx0 = &intp->valueStack.back();
		ResultType result = (*func)(
		    ArgCvt<std::remove_reference_t<ArgTypes>>::FromVal(
		    *(&intp->valueStack.back() - indices))...
		);
		auto stackSize = intp->valueStack.size();
		intp->valueStack.resize(stackSize - argCnt + 1);
		ReturnHostVal<ResultType>(result, intp);
	}
};


template <size_t... indices, typename... ArgTypes>
struct ArgDispatcher<IndicesHolder<indices...>, void (*)(ArgTypes...)>
	//: public ArgHolder<indices, ArgTypes>... 
{
	using FunctionPtr = void (*)(ArgTypes...);

	static const size_t ArgCount = sizeof...(ArgTypes);
	static const bool HasReturn = false;
	static std::vector<std::string> ArgTypeNames() { return GetArgTypeNames<ArgTypes...>(); }
	static const bool IsStatic = true;

	static void Dispatch(FunctionPtr func, Interpreter* intp){
		static const std::size_t argCnt = sizeof...(indices);
		//auto* idx0 = &intp->valueStack.back();
		(*func)(ArgCvt<std::remove_reference_t<ArgTypes>>::FromVal(
			*(&intp->valueStack.back() - indices))...
		);
		auto stackSize = intp->valueStack.size();
		intp->valueStack.resize(stackSize - argCnt);
	}
};



template <typename C, size_t... indices, typename... ArgTypes>
struct ArgDispatcher<IndicesHolder<indices...>, void(C::*)(ArgTypes...)>
	//: public ArgHolder<indices, ArgTypes>... 
{
	using FunctionPtr = void(C::*)(ArgTypes...);
	static const size_t ArgCount = sizeof...(ArgTypes);
	static const bool HasReturn = false;
	static std::vector<std::string> ArgTypeNames() { return GetArgTypeNames<ArgTypes...>(); }
	static const bool IsStatic = false;


	static void Dispatch(FunctionPtr func, Interpreter* intp) {
		static const std::size_t argCnt = sizeof...(indices);
		//auto* idx0 = &intp->valueStack.back();
		C* thisPtr = (C*)intp->callStack.back().currEnv.data.obj;
		(thisPtr->*func)(ArgCvt<std::remove_reference_t<ArgTypes>>::FromVal(
			*(&intp->valueStack.back() - indices))...
		);
		auto stackSize = intp->valueStack.size();
		intp->valueStack.resize(stackSize - argCnt);
	}
};

template <typename C, size_t... indices, typename ResultType, typename... ArgTypes>
struct ArgDispatcher<IndicesHolder<indices...>, ResultType(C::*)(ArgTypes...)>
	//: public ArgHolder<indices, ArgTypes>... 
{
	using FunctionPtr = ResultType(C::*)(ArgTypes...);
	static const size_t ArgCount = sizeof...(ArgTypes);
	static const bool HasReturn = true;
	static std::vector<std::string> ArgTypeNames() { return GetArgTypeNames<ArgTypes...>(); }
	static std::string RetTypeName(){ return ArgCvt<ResultType>::GetName();}
	static const bool IsStatic = false;

	static void Dispatch(FunctionPtr func, Interpreter* intp) {

		static const std::size_t argCnt = sizeof...(indices);
		//auto* idx0 = &intp->valueStack.back();
		C* thisPtr = (C*)intp->callStack.back().currEnv.data.obj;
		ResultType result = (thisPtr->*func)(ArgCvt<std::remove_reference_t<ArgTypes>>::FromVal(
			*(&intp->valueStack.back() - indices))...
		);
		auto stackSize = intp->valueStack.size();
		intp->valueStack.resize(stackSize - argCnt + 1);
		ReturnHostVal<ResultType>(result, intp);
	}
};


template <typename Sig>
void CallHost(Sig func, Interpreter* intp) {
	using Indices = typename IndicesForSignature<Sig>::type;

	ArgDispatcher<Indices, Sig>::Dispatch(func, intp);
}

#define HM_WRAPNAME(CLASS, METHOD) CLASS##_##METHOD
#define HOST_METHOD_WRAPPER(CLASS, METHOD)                 \
  static void HM_WRAPNAME(CLASS, METHOD)(Interpreter* intp) { \
    CallHost(&CLASS::METHOD, intp);                  \
  }

#define Str(x) #x

template <typename Dispat, typename Enable = void>
void SetRetInfo(HostMethod* m)
{
	m->Return("return", Dispat::RetTypeName());
}

template <typename Dispat, bool En>
typename std::enable_if<En,void>::type SetRetInfo(HostMethod* m)
{
	m->Return("return", Dispat::RetTypeName());
}

template <typename Dispat, bool En>
typename std::enable_if<(!En), void>::type SetRetInfo(HostMethod* m)
{
}

template <typename Sig>
HostMethod* WrapHostMethod(Sig func,const std::string& name, InstFn wrapper)
{
	using Indices = typename IndicesForSignature<Sig>::type;
    using dispatcher = ArgDispatcher<Indices, Sig>;

	//static const size_t argCnt = dispatcher::ArgCount;
	static const bool hasRet = dispatcher::HasReturn;
	static const bool isStatic = dispatcher::IsStatic;

	static const std::vector<std::string> ArgTypes = dispatcher::ArgTypeNames();

	auto m = new HostMethod(name, wrapper);

	for(std::size_t i = 0; i < ArgTypes.size(); i++)
	{
	    m->Arg("arg"+std::to_string(i),ArgTypes[i]);
	}

	SetRetInfo<dispatcher, hasRet>(m);

	//if(hasRet)
	//{
	//    m->Return("return",dispatcher::RetTypeName());
	//}

	return m->Static(isStatic);
}

#define DO_WRAP_HOST_METHOD(CLASS, METHOD) \
    WrapHostMethod(\
        &CLASS::METHOD,\
        Str(METHOD),\
        &HM_WRAPNAME(CLASS, METHOD))

#define WRAP_HOST_TYPE(CLASS) \
	(new ClassInfo(#CLASS, false))->RefType()

#define WRAP_HOST_METHOD(CLASS, METHOD) \
	->Method(\
    DO_WRAP_HOST_METHOD(CLASS, METHOD)\
    )

//class Nat1
//{
//public:
//	int TestFunc(int a, float b, bool c)
//	{
//	    return 0;
//	}
//};



void WrapTest();
//auto vecLib = (new LibraryInfo("Vec"))
//->Class((new ClassInfo("Vec3"))
//    ->RefType(true)
//    ->Method((new HostMethod("Add", &AddVec))
//        ->Arg("a", "Vec3")
//        ->Arg("b", "Vec3")
//        ->Return("res", "Vec3")
//    )
//    ->Method((new HostMethod("Dec", &DecVec))
//        ->Arg("a", "Vec3")
//        ->Arg("b", "Vec3")
//        ->Return("res", "Vec3")
//    )
//)
//;

/*  FOREACH_HOST_METHOD(V) \
 *		V(class, fn1)\
 *		V(class, fn2)\
 *		V(class, fn3)\
 *		...
 *
 *	auto lib = (new LibraryInfo("Vec"))
 *		->Class((new HostTypeInfo<Typename>())
 *			->HostMethod()
 *		)
 *		;
 */

