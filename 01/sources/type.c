#include "type.h"
#include <string.h>

char* get_content_type(char* filename)
{
    char* dot;
    dot = strrchr(filename, '.');

    if (dot == NULL)
        return "text/plain; charset=UTF-8";
    if (0 == strcmp(dot, ".html"))
        return "text/html; charset=UTF-8";
    if (0 == strcmp(dot, ".css"))
        return "text/css";
    if (0 == strcmp(dot, ".js"))
        return "application/x-javascript";
    if (0 == strcmp(dot, ".jpg") || 0 == strcmp(dot, ".jpeg"))
        return "image/jpeg";
    if (0 == strcmp(dot, ".gif"))
        return "image/gif";
    if (0 == strcmp(dot, ".png"))
        return "image/png";
    if (0 == strcmp(dot, ".mp4"))
        return "video/mpeg4";
    if (0 == strcmp(dot, ".mp3"))
        return "audio/mp3";
    if (0 == strcmp(dot, ".wav"))
        return "audio/wav";

    return "text/plain; charset=UTF-8";
}