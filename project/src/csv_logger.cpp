#include <string.h>
#include "csv_logger.h"

extern "C" {

FRESULT csv_open(csv_logger_t* lg, const char* abs_path, const char* header) {
  if (!lg) return FR_INVALID_OBJECT;
  memset(lg, 0, sizeof(*lg));
  lg->flush_interval = 20;
  UINT bw = 0;

  FRESULT fr = f_open(&lg->file, abs_path, FA_WRITE | FA_CREATE_ALWAYS);
  if (fr != FR_OK) return fr;

  fr = f_write(&lg->file, header, (UINT)strlen(header), &bw);
  if (fr == FR_OK) {
    const char nl = '\n';
    fr = f_write(&lg->file, &nl, 1, &bw);
  }
  if (fr != FR_OK) { f_close(&lg->file); return fr; }

  lg->open = true;
  return f_sync(&lg->file);
}

FRESULT csv_append(csv_logger_t* lg, const char* line) {
  if (!lg || !lg->open) return FR_INVALID_OBJECT;
  UINT bw = 0;
  FRESULT fr = f_write(&lg->file, line, (UINT)strlen(line), &bw);
  if (fr != FR_OK) return fr;
  const char nl = '\n';
  fr = f_write(&lg->file, &nl, 1, &bw);
  if (fr != FR_OK) return fr;

  lg->lines_written++;
  if (lg->flush_interval && (lg->lines_written % lg->flush_interval) == 0)
    return f_sync(&lg->file);
  return FR_OK;
}

FRESULT csv_close(csv_logger_t* lg) {
  if (!lg || !lg->open) return FR_INVALID_OBJECT;
  FRESULT fr = f_sync(&lg->file);
  FRESULT fr2 = f_close(&lg->file);
  lg->open = false;
  return (fr != FR_OK) ? fr : fr2;
}

} // extern "C"

