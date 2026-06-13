#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H
#ifdef _WIN32
#define MINIZ_EXPORT __declspec(dllexport)
#else
#define MINIZ_EXPORT
#endif
#endif
