# Code Conventions

Consistent code choices and style guidelines means an easier time searching for things and more predictable behaviour. Please try to adhere. This is a living document.

- Don't forget to use `clang-format`.

## Universal

- I'm a Canadian who has generally worked with Europeans and other Canadians on software, so preference is given to Commonwealth English. This means "grey" over "gray", "colour" over "colour", and "initialise" over "initialize".
- Indentation comes in the form of 4-space-wide tabs, except where `clang-format` needs to perform alignments.
- Line endings are LF.

## C++

- Namespace, variable, and function names are in `snake_case`.
- Type names (classes, structs, enums) are in `PascalCase`.
- Symbolic constants (including enum values) and preprocessor symbols are in `CAPITAL_SNAKE_CASE`.
- Prefer `std::string::length()` over `std::string::size()` since the former is more common in other languages and only one of the two should be used.
- Use Javadoc style for Doxygen comments: open with `/**` and add special fields with `@`.
- In source files with static functions which "help" any functions exported via a header, prefer to forward declare these functions at the top of the source file and implement them at the bottom. This keeps the functions which are less likely to change out of the way.

# Glossary

Many symbols in Machinate are given very terse names to keep lines short. Most should be clear if you know enough English to know C and C++, but some are Machinate-specific, and if you ever find yourself wondering what an abbrevation or acronym might stand for, check here.

- `attr` -> attribute (generally as in vertex attributes)
- `bp` -> blueprint
- `buf` -> buffer
- `cb` -> callback
- `ci` -> create info (as in Vulkan's `CreateInfo` archetype of structs)
- `cli` -> client
- `clr` -> colour
- `cmd` -> command
- `condvar` -> condition variable
- `ctxt` -> context
- `dbg` -> debug
- `elem` -> element (generally as in vertex elements)
- `feats` -> features
- `fs` -> filesystem
- `gfx` -> graphics
- `hmap`/`htmap` -> heightmap
- `idx` -> index
- `img` -> image
- `impl` -> implementation
- `lbl` -> label
- `loc` -> localise/localisation (should never be used to refer to "location")
- `mnf` -> manifest (as in game package manifest)
- `mtx` -> mutex
- `opt` -> option/optional
- `pfs` -> PhysicsFS (likely in reference to a `PHYSFS_File` struct)
- `pos` -> position
- `ppl` -> pipeline (generally as in graphics pipeline)
- `pres` -> present (as in frame presentation)
- `proj` -> projection
- `props` -> properties
- `q` -> queue
- `repr` -> represent/representation
- `req` -> requirement
- `res` -> resolution
- `sema` -> semaphore
- `srf` -> surface
- `sz` -> size
- `tab` -> table (generally as in Lua tables)
- `trans` -> transfer
- `ts` -> thread-safe
- `vfs` -> virtual file system

If the letters `w` and `h` are seen together, they likely refer to width and height.

Also be aware that any symbol ending with a `_c`, e.g. `format_c`, is likely to be referring to a "count" variable (in this case, "format count"). This is meant to be akin to the `argc` parameter name of C's `int main()`.
