# SimpleIntp

A simple interpreter using [direct call threading](http://www.cs.toronto.edu/~matz/dissertation/matzDissertation-latex2html/node6.html). This project aims at easy host intergration and expanding. The runtime itself only have boolean compare capabilities, everything else including integer types and arithmetic functions are provided by host. The rumtime also implements a OOP object model and type system.

The interpreter is stack based. Available opcodes can be found [here](opcodes.txt).
