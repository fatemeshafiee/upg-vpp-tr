#include "response_builder.h"
#include "user_handler.h"
#include <setjmp.h>


void log_api(const char *url, const char *method);
struct PostData {
    char *data;
    size_t size;
    size_t capacity;
};

enum MHD_Result default_handler(void *cls, struct MHD_Connection *connection, const char *url,
                                const char *method, const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls);