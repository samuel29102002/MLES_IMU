#pragma once

#include <stdbool.h>
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  FIL file;
  bool open;
  unsigned lines_written;
  unsigned flush_interval;
} csv_logger_t;

FRESULT csv_open(csv_logger_t* lg, const char* abs_path, const char* header);
FRESULT csv_append(csv_logger_t* lg, const char* line);
FRESULT csv_close(csv_logger_t* lg);

#ifdef __cplusplus
}
#endif

