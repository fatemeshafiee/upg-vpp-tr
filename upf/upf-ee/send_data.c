//
// Created by Fatemeh Shafiei Ardestani on 2024-07-13.
//
//
// Created by Fatemeh Shafiei Ardestani on 2024-07-13.
//

#include "send_data.h"

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  printf("%.*s", (int)realsize, (char *)contents); // Print the response data
  return realsize;
}

void parse_time(const char* date_time, struct  tm* tm){
  int year, month, day, hour, minute, second;
  memset(tm, 0, sizeof(struct tm));

  if (sscanf(date_time, "%d-%d-%dT%d:%d:%dZ", &year, &month, &day, &hour, &minute, &second) != 6) {
    fprintf(stderr, "Failed to parse date-time string\n");
    return;
  }

  tm->tm_year = year - 1900;
  tm->tm_mon = month - 1;
  tm->tm_mday = day;
  tm->tm_hour = hour;
  tm->tm_min = minute;
  tm->tm_sec = second;
  tm->tm_isdst = -1;


  return;
}

void fillNotificationItem(UpfEventSubscription upfSub,NotificationItem *item,EventType type ) {
  if(type==USER_DATA_USAGE_MEASURES){
    item->type = USER_DATA_USAGE_MEASURES;
    struct tm tm;
    time_t current_time;
    time(&current_time);
    parse_time(current_time,  &tm);
    item->timeStamp = mktime(&tm);
//    item->startTime = mktime(&tm);
    usage_report_per_flow_t* rep;
    cvector(UserDataUsageMeasurements) userDataMeasurements = NULL;
    pthread_mutex_lock(&ee_lock);
    vec_foreach(rep, usage_report_per_flow_vector){
      UserDataUsageMeasurements *usage = malloc(sizeof (UserDataUsageMeasurements));
      usage->volumeMeasurement.totalNbOfPackets = rep->src_pkts + rep->dst_pkts;
      usage->volumeMeasurement.totalVolume = rep->src_bytes + rep->dst_bytes;
      // TODO: make sure the uplink and downlink are right.
      usage->volumeMeasurement.dlNbOfPackets = rep->src_pkts;
      usage->volumeMeasurement.dlVolume = rep->src_bytes;
      usage->volumeMeasurement.ulNbOfPackets = rep->dst_pkts;
      usage->volumeMeasurement.ulVolume = rep->dst_bytes;
      char buffer[INET6_ADDRSTRLEN];
      json_t *obj = json_object();
      json_object_set_new(obj,"SeId", json_integer(rep->seid));
      inet_ntop(AF_INET, &(rep->src_ip), buffer, sizeof(buffer));
      json_object_set_new(obj,"SrcIp", json_string(buffer));
      inet_ntop(AF_INET, &(rep->dst_ip), buffer, sizeof(buffer));
      json_object_set_new(obj,"DstIp", json_string(buffer));
      json_object_set_new(obj,"SrcPort", json_integer(rep->src_port));
      json_object_set_new(obj,"DstPort", json_integer(rep->dst_port));
      usage->flowInfo.flowDescription = json_dumps(obj, JSON_INDENT(2));
      cvector_push_back(userDataMeasurements, *usage);
    }
    item->userDataUsageMeasurements = userDataMeasurements;
  }
  pthread_mutex_unlock(&ee_lock);

}
const char * create_custom_report(UpfEventSubscription upfSub,EventType type){
  if(type == USER_DATA_USAGE_TRENDS){
    NotificationItem *notificationItem = malloc(sizeof (NotificationItem));
    fillNotificationItem(upfSub, notificationItem, type);
    json_t* callBack_Report= serialize_callBack(*notificationItem, upfSub.notifyCorrelationId, 0);
    if (!callBack_Report) {
      fprintf(stdout, "The json_t object is NULL.\n");
    }
    char *json_str = json_dumps(callBack_Report, JSON_INDENT(2));
    return json_str;
  }
}
void send_report(UpfEventSubscription upfSub,EventType type){
  // we Assume that we have the upf raw data hare
  // we call the function that can customized the needed measurement
  const char *json_data = create_custom_report(upfSub, type);

  CURL *curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();
  if (curl) {

    curl_easy_setopt(curl, CURLOPT_URL, upfSub.eventNotifyUri);
    fprintf(stdout,"the URI is %s\n", upfSub.eventNotifyUri);
    fprintf(stdout, "dat: %s\n", json_data);
    fflush(stdout);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L); // To set the size dependent to json

    struct curl_slist *headers = NULL;

    headers = curl_slist_append(headers, "Expect:"); // To Disable 100 continue
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    res = curl_easy_perform(curl);
    // Check for errors
    if (res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }

}
//UpfEventSubscription*
void* EventReport_UDUT() {
//  while (true){
    time_t current_time;
    time(&current_time);
    uint32_t eventType = (uint32_t) USER_DATA_USAGE_TRENDS;
//    void*  element = hashmap_get(&hashmap,"USER_DATA_USAGE_TRENDS" , strlen("USER_DATA_USAGE_TRENDS"));
//    void*  element = hashmap_get(&hashmap,&eventType , sizeof(uint32_t));
    clib_warning("[EventReport_UDUT] in the Event report.");
    cvector_vector_type(UpfEventSubscription*) retrieved_vec = hmget(subHash, USER_DATA_USAGE_TRENDS);
    if (retrieved_vec!=NULL) {
//      retrieved_vec = (cvector_vector_type(UpfEventSubscription*))element;
      clib_warning("[EventReport_UDUT] retrived vector was != NULL");
      for (int i = 0; i < cvector_size(retrieved_vec); i++) {
        if(retrieved_vec[i]->eventReportingMode.trigger == PERIODIC){
          if(retrieved_vec[i]->eventReportingMode.TimeOfLastReport == ((time_t)-1)){
            if (current_time - retrieved_vec[i]->eventReportingMode.TimeOfSubscription >=retrieved_vec[i]->eventReportingMode.repPeriod){
              send_report(*retrieved_vec[i], USER_DATA_USAGE_TRENDS); // dummy
              retrieved_vec[i]->eventReportingMode.TimeOfLastReport = current_time;
              retrieved_vec[i]->eventReportingMode.sent_reports += 1;
            }
          }
          else if (current_time - retrieved_vec[i]->eventReportingMode.TimeOfLastReport >=retrieved_vec[i]->eventReportingMode.repPeriod){
            send_report(*retrieved_vec[i], USER_DATA_USAGE_TRENDS); // dummy
            retrieved_vec[i]->eventReportingMode.TimeOfLastReport = current_time;
            retrieved_vec[i]->eventReportingMode.sent_reports += 1;
          }
          // TODO
//        if(retrieved_vec[i].eventReportingMode.maxReports == eventReportingMode.sent_reports){
//          //deactivate the subscription
//          continue;
//        }

        }

      }
    }
//    struct timespec req = {0};
//    req.tv_sec = 0;
//    req.tv_nsec = 500000000L; // 500 million nanoseconds (0.5 seconds)
//    nanosleep(&req, NULL);
//  }
  return NULL;
}

