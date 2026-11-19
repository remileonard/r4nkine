# Coding rules

They apply to R4nkine and to its code for libRealSpace (`contrib/librealspace`).
`.clang-format` enforces the layout: `clang-format -i` on every file changed.

- Every block has its braces, the opening one on the line of its statement:
  `if`, `else`, `for`, `while`, `switch`, functions and lambdas.
  ```cpp
  if (condition) {
      effect();
  }
  ```
- One statement per line, declarations included: `int a = 0;` then `int b = 0;`,
  never `int a = 0, b = 0;` nor `a = 1, b = 2;`.
- Lines are not broken, however long. Array initializers may keep their layout.
- No integer in the code for an identifier of a data set (a texture, a terrain
  type, an opcode of a file format): an enum names it. Its values are in
  `UPPER_SNAKE_CASE`.
- Comments are few: what a function or a type is for, or why something
  is done when the code cannot say it. The code is not described line by line.
