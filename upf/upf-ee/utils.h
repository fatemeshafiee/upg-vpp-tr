// Added by: Fatemeh Shafiei Ardestani
// Date: 2025-04-06
//  See Git history for complete list of changes.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

typedef enum {
  OK = 200,
  CREATED = 201,
  BAD_REQUEST = 400,
  NOT_FOUND = 404,
  INTERNAL_SERVER_ERROR = 500,
  NOT_IMPLEMENTED = 501
} HTTP_status;

typedef struct {
  char *body;
  HTTP_status status;
} HTTP_response;

char *simple_message(const char *message_str);
bool validate_route(const char *url, char *route);
bool validate_method(const char *method, char *valid_method);
HTTP_response validate_result(char *result);