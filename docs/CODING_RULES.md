# Coding rules

These rules apply to R4nkine and to its code for libRealSpace (`contrib/librealspace`).

`.clang-format` sets the layout. Do `clang-format -i` on each file that you change. `.clang-tidy` does a check of the declarations and the braces.

- Put braces on each block: `if`, `else`, `for`, `while`, `switch`, functions and lambdas. Put the opening brace on the line of the statement.
  ```cpp
  if (condition) {
      effect();
  }
  ```
- Write one statement on each line. This rule also applies to the declarations. Write `int a = 0;` and then `int b = 0;`. Do not write `int a = 0, b = 0;` or `a = 1, b = 2;`.
- Do not break a line, also if it is long. Array initializers can keep their layout.
- Do not use an integer in the code for an identifier of a data set. Examples: a texture, a terrain type, an opcode of a file format. Use an enum for the identifier. Write its values in `UPPER_SNAKE_CASE`.
- Write few comments. A comment tells the function of a function or a type. A comment also tells the reason for a decision, if the code cannot show it. Do not describe the code line by line.
- Write the documentation in Simplified Technical English (ASD-STE100).
