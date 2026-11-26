# embed.cmake - turn a binary file into a C array (cmake -DIN=... -DOUT=... -DNAME=... -P).
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" n)
math(EXPR size "${n} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUT}" "/* Generated from ${IN} by cmake/embed.cmake. */\n#include <stddef.h>\n"
     "const unsigned char ${NAME}[] = {\n${bytes}\n};\nconst size_t ${NAME}_size = ${size};\n")
