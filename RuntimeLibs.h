#pragma once

#include "Library.h"
#include "Interop.h"
#include "ArgCvt.h"

template<>
struct ArgCvt<int>
{
	static std::string GetName() { return "Num|Int"; }

	static int FromVal(ValueType& val) { return val.data.value; }

	static ValueType ToVal(int val, Interpreter* intp)
	{
		ValueType ival(intp->libLoader.LookupType("Num|Int")
		);
		ival.data.value = val;
		return ival;
	}
};

template<>
struct ArgCvt<bool>
{
	static std::string GetName() { return "Num|Int"; }

	static bool FromVal(ValueType& val) { return val.data.value; }

	static ValueType ToVal(int val, Interpreter* intp)
	{
		ValueType ival(intp->libLoader.LookupType("Num|Int")
		);
		ival.data.value = val;
		return ival;
	}
};
template<>
struct ArgCvt<float>
{
	static std::string GetName() { return "Num|Float"; }

	static float FromVal(ValueType& val) { return val.data.fval; }

	static ValueType ToVal(float val, Interpreter* intp)
	{
		ValueType ival(intp->libLoader.LookupType("Num|Float")
		);
		ival.data.fval = val;
		return ival;
	}
};

struct RuntimeLibs
{
    static std::shared_ptr<LibraryInfo> Num();

    static std::shared_ptr<LibraryInfo> Vec();
};

