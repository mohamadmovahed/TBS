// DVB SI string decoding (character table selectors, control codes).

#include <tbsbbproc/DvbText.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)tbs::bb::dvbTextToUtf8(data, size);
    if (size > 0)
        (void)tbs::bb::dvbTextToUtf8(data + 1, size - 1);
    return 0;
}
