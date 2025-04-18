
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
#include "response_builder.h"
//#include "user_handler.h"
#include <setjmp.h>


void get_current_time(char *buffer, size_t buffer_size);
void log_api(const char *url, const char *method);
struct PostData {
    char *data;
    size_t size;
    size_t capacity;
};


enum MHD_Result default_handler(void *cls, struct MHD_Connection *connection, const char *url,
                                const char *method, const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls);