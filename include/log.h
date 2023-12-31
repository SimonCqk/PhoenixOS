#pragma once

#include <stdio.h>

#define POS_PRINT_ERROR 1
#define POS_PRINT_WARN 1
#define POS_PRINT_LOG 1
#define POS_PRINT_DEBUG 0

#define POS_PRINT_WITH_COLOR 1

#define _POS_PRINT_POSITION(stream)                                                 \
{                                                                                   \
fprintf(stream, "\
\033[33mfile:\033[0m       %s;\n\
\033[33mfunction:\033[0m   %s;\n\
\033[33mline:\033[0m       %d;\n", __FILE__, __PRETTY_FUNCTION__, __LINE__);\
}

#ifdef __GNUG__
#include <cstdlib>
#include <memory>
#include <cxxabi.h>

static std::string demangle(const char* name) {

    int status = -4; // some arbitrary value to eliminate the compiler warning

    // enable c++11 by passing the flag -std=c++11 to g++
    std::unique_ptr<char, void(*)(void*)> res {
        abi::__cxa_demangle(name, NULL, NULL, &status),
        std::free
    };

    return (status==0) ? res.get() : name;
}

#else

// does nothing if not g++
static std::string demangle(const char* name) {
    return name;
}

#endif

/**************************** POS_ERROR ****************************/

#if POS_PRINT_ERROR
/*!
    * \brief   print error message (internal-used)
    */
#define _POS_ERROR(...)                                         \
{                                                               \
if constexpr(POS_PRINT_WITH_COLOR)                              \
    fprintf(stdout, "\033[101m\033[97m POS Error \033[0m ");    \
else                                                            \
    fprintf(stdout, "|-POS Error-| ");                          \
fprintf(stdout, __VA_ARGS__);                                   \
fprintf(stdout, "\n");                                          \
}

/*!
    * \brief   print error message with class info (internal-used)
    * \note    this macro should only be expanded within c++ class
    */
#define _POS_ERROR_C(...)                                   \
{                                                           \
if constexpr(POS_PRINT_WITH_COLOR)                          \
    fprintf(stdout,                                         \
        "\033[101m\033[97m POS Error \033[0m [%s @ %p]\n",  \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
else                                                        \
    fprintf(stdout,                                         \
        "|-POS Error-| [%s @ %p]\n",                        \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
fprintf(stdout, __VA_ARGS__);                               \
fprintf(stdout, "\n");                                      \
}

/*!
    * \brief   print error message, then fatally quit
    */
#define POS_ERROR(...)          \
{                               \
_POS_ERROR(__VA_ARGS__)         \
throw "SHUTDOWN BY POS ERROR";  \
}

/*!
    * \brief   print error message with class info, then fatally quit
    * \note    this macro should only be expanded within c++ class
    */
#define POS_ERROR_C(...)        \
{                               \
_POS_ERROR_C(__VA_ARGS__)       \
throw "SHUTDOWN BY POS ERROR";  \
}

/*!
    * \brief   print error message with specific code position, then fatally quit
    */
#define POS_ERROR_DETAIL(...)   \
{                               \
_POS_ERROR(__VA_ARGS__)         \
_POS_PRINT_POSITION(stderr)     \
throw "SHUTDOWN BY POS ERROR";  \
}

/*!
    * \brief   print error message with class info and specific code position,
    *          then fatally quit
    */
#define POS_ERROR_C_DETAIL(...) \
{                               \
_POS_ERROR_C(__VA_ARGS__)       \
_POS_PRINT_POSITION(stderr)     \
throw "SHUTDOWN BY POS ERROR";  \
}
#else
#define POS_ERROR(...)
#define POS_ERROR_C(...)
#define POS_ERROR_DETAIL(...)
#define POS_ERROR_C_DETAIL(...)
#endif

/**************************** POS_WARN *******************************/

#if POS_PRINT_WARN
/*!
    * \brief   print warn message
    */
#define POS_WARN(...)                                           \
{                                                               \
if constexpr(POS_PRINT_WITH_COLOR)                              \
    fprintf(stdout, "\033[103m\033[97m POS Warn \033[0m ");     \
else                                                            \
    fprintf(stdout, "|-POS Warn-| ");                           \
fprintf(stdout, __VA_ARGS__);                                   \
fprintf(stdout, "\n");                                          \
}

/*!
    * \brief   print warn message with class info
    * \note    this macro should only be expanded within c++ class
    */
#define POS_WARN_C(...)                                     \
{                                                           \
if constexpr(POS_PRINT_WITH_COLOR)                          \
    fprintf(stdout,                                         \
        "\033[103m\033[97m POS Warn \033[0m [%s @ %p]\n",   \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
else                                                        \
    fprintf(stdout,                                         \
        "|-POS Warn-| [%s @ %p]\n",                         \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
fprintf(stdout, __VA_ARGS__);                               \
fprintf(stdout, "\n");                                      \
}

/*!
    * \brief   print warn message with class info and specific code position
    */
#define POS_WARN_DETAIL(...)    \
{                               \
POS_WARN(__VA_ARGS__)           \
_POS_PRINT_POSITION(stderr)     \
}

/*!
    * \brief   print warn message with class info and specific code position
    * \note    this macro should only be expanded within c++ class
    */
#define POS_WARN_C_DETAIL(...)  \
{                               \
POS_WARN_C(__VA_ARGS__)         \
_POS_PRINT_POSITION(stderr)     \
}
#else
#define POS_WARN(...)
#define POS_WARN_C(...)
#define POS_WARN_DETAIL(...)
#define POS_WARN_C_DETAIL(...)
#endif

/***************************** POS_LOG *****************************/

#if POS_PRINT_LOG
/*!
    * \brief   print log message
    */
#define POS_LOG(...)                                        \
{                                                           \
if constexpr(POS_PRINT_WITH_COLOR)                          \
    fprintf(stdout, "\033[104m\033[97m POS Log \033[0m ");  \
else                                                        \
    fprintf(stdout, "|-POS Log-| ");                        \
fprintf(stdout, __VA_ARGS__);                               \
fprintf(stdout, "\n");                                      \
}

/*!
    * \brief   print log message with class info
    * \note    this macro should only be expanded within c++ class
    */
#define POS_LOG_C(...)                                      \
{                                                           \
if constexpr(POS_PRINT_WITH_COLOR)                          \
    fprintf(stdout,                                         \
        "\033[104m\033[97m POS Log \033[0m [%s @ %p]\n",    \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
else                                                        \
    fprintf(stdout,                                         \
        "|-POS Log-| [%s @ %p]\n",                          \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
fprintf(stdout, __VA_ARGS__);                               \
fprintf(stdout, "\n");                                      \
}

/*!
    * \brief   print log message with class info and specific code position
    */
#define POS_LOG_DETAIL(...)     \
{                               \
POS_LOG(__VA_ARGS__)            \
_POS_PRINT_POSITION(stdout)     \
}

/*!
    * \brief   print log message with class info and specific code position
    * \note    this macro should only be expanded within c++ class
    */
#define POS_LOG_C_DETAIL(...)   \
{                               \
POS_LOG_C(__VA_ARGS__)          \
_POS_PRINT_POSITION(stdout)     \
}
#else
#define POS_LOG(...)
#define POS_LOG_C(...)
#define POS_LOG_DETAIL(...)
#define POS_LOG_C_DETAIL(...)
#endif

/**************************** POS_DEBUG ****************************/

#if POS_PRINT_DEBUG
/*!
    * \brief   print debug message
    */
#define POS_DEBUG(...)                                          \
{                                                               \
if constexpr(POS_PRINT_WITH_COLOR)                              \
    fprintf(stdout, "\033[104m\033[42m POS Debug \033[0m ");    \
else                                                            \
    fprintf(stdout, "|-POS Debug-| ");                          \
fprintf(stdout, __VA_ARGS__);                                   \
fprintf(stdout, "\n");                                          \
}

/*!
    * \brief   print debug message with class info
    * \note    this macro should only be expanded within c++ class
    */
#define POS_DEBUG_C(...)                                    \
{                                                           \
if constexpr(POS_PRINT_WITH_COLOR)                          \
    fprintf(stdout,                                         \
        "\033[104m\033[42m POS Debug \033[0m [%s @ %p]\n",  \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
else                                                        \
    fprintf(stdout,                                         \
        "|-POS Debug-| [%s @ %p]\n",                        \
        demangle(typeid(*this).name()).c_str(), this        \
    );                                                      \
fprintf(stdout, __VA_ARGS__);                               \
fprintf(stdout, "\n");                                      \
}

/*!
    * \brief   print debug message with class info and specific code position
    */
#define POS_DEBUG_DETAIL(...)   \
{                               \
POS_DEBUG(__VA_ARGS__)          \
_POS_PRINT_POSITION(stdout)     \
}

/*!
    * \brief   print debug message with class info and specific code position
    * \note    this macro should only be expanded within c++ class
    */
#define POS_DEBUG_C_DETAIL(...) \
{                               \
POS_DEBUG_C(__VA_ARGS__)        \
_POS_PRINT_POSITION(stdout)     \
}
#else
#define POS_DEBUG(...)
#define POS_DEBUG_C(...)
#define POS_DEBUG_DETAIL(...)
#define POS_DEBUG_C_DETAIL(...)
#endif