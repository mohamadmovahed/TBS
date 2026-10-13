# Writes OUTPUT as a C++ header holding INPUT as a NUL-terminated `const char *NAME`.
file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "// Generated from ${INPUT}\n#pragma once\nstatic const unsigned char ${NAME}Bytes[] = {\n${bytes}0x00};\n"
     "static const char *const ${NAME} = reinterpret_cast<const char *>(${NAME}Bytes);\n")
