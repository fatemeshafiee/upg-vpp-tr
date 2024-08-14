#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *executeQueryToJson(const char *query);
char *executeQueryToJson(const char *query) {
  return "{\"ok\": true}";
}
