#ifndef HEXDUMP_HPP
#define HEXDUMP_HPP

#include <sstream>
#include <string>

#ifdef WIN32
#define snprintf _snprintf
#endif

namespace hexdump{


inline char translate(char c)
{
    // show the '.' for non-printable chars
    char ch = '.';
    if (c >= 0x20 && c <= 0x7E)
        ch = c;
    return ch;
}

inline std::string dump(const char *buf, int len, int bytesPerRow=16)
{
    std::stringstream output;

    // make the total multiple of bytesPerRow
    int total = len;
    if (total%bytesPerRow != 0)
    {
        // increment total
        total += (bytesPerRow - total%bytesPerRow);
    }

    std::stringstream bytes; // text output in the format '41 42 43'
    std::stringstream chars; // text output in the format 'ABC'

    for (int i = 0; i < total; i++)
    {
        // consume all bytes in the buffer first
        if (i < len){
            int value = (int)buf[i] & 0xff;
            char byte[4] = {0};
            snprintf(byte, sizeof(byte), "%02x ", value);

            bytes << byte; // saved the number in hex
            chars << translate(buf[i]); // save the filtered chars (dont print \n, \r, etc...)

        // now fill the rest of the line with empty data
        } else {
            bytes << "   "; // necessary to keep the chars aligned
        }

        // decide when to break the line and join the bytes and chars ("41 42 43 | ABC")
        if (((i + 1) % bytesPerRow == 0) ||
            ((i + 1) == total)) // prints if it is the last item also
        {
            output << bytes.str() << "| " << chars.str() << "\n";

            // clear to move to the next line
            bytes.str("");
            chars.str("");
        }
    }

    return output.str();
}

}

#endif // HEXDUMP_HPP
