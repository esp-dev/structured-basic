# BASIC 1.0 memory

The 8192-byte default arena stores symbols, code, four-byte globals and
the dynamically shared gap between the object heap and operand stack.
Values occupy five bytes on the operand stack. They carry a one-byte type
and a four-byte payload. A routine allocates an 18-byte frame and four
bytes per parameter or local. FOR and SELECT each store a 20-byte
control record. BASIC recursion stays within those frames.

Each nonempty string or allocated array has a six-byte object header.
Strings are immutable and reference counted. Arrays hold four-byte
elements and release their text references when the allocation is freed.
The heap splits suitable free blocks, merges adjacent free blocks and trims
the free high end. Live objects stay at stable offsets. Fragmentation can
therefore lead to a bounded memory error even when smaller holes collectively
contain enough space. A new execution clears all runtime storage.

The compiler uses bounded stacks, and the validator uses a bitset for
symbol and instruction boundaries. Neither structure retains memory in
the instance between runs. Check platform linker and stack reports for
the total MCU budget. See [EMBEDDING.md](EMBEDDING.md) for compile-time limits.
