/* livejq - project-wide primitive typedefs. */
#ifndef LIVEJQ_DEFINES_H
#define LIVEJQ_DEFINES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t usz;

typedef float f32;
typedef double f64;

typedef u8 b8;
typedef u16 b16;
typedef u32 b32;
typedef u64 b64;

#define UNUSED(x)    ((void)(x))
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

#endif /* LIVEJQ_DEFINES_H */
