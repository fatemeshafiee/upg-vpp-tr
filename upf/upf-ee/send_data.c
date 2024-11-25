//
// Created by Fatemeh Shafiei Ardestani on 2024-07-13.
//
//userDataMeasurements
// Created by Fatemeh Shafiei Ardestani on 2024-07-13.
//
#include <stdio.h>
#include <string.h>
#include "send_data.h"
#include <vlib/vlib.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlib/unix/unix.h>
void get_current_time_send(char *buffer, size_t buffer_size) {

    struct timeval tv;
    struct tm local_tm;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &local_tm);
    int millisec = tv.tv_usec / 1000;
    snprintf(buffer, buffer_size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
             local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, millisec);
//  time_t now = time(NULL);
//  struct tm local_tm;
//  localtime_r(&now, &local_tm);
//  strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &local_tm);

}
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  printf("%.*s", (int)realsize, (char *)contents); // Print the response data
  return realsize;
}

void parse_time(const char* date_time, struct  tm* tm){
  int year, month, day, hour, minute, second;
  memset(tm, 0, sizeof(struct tm));

  if (sscanf(date_time, "%d-%d-%dT%d:%d:%dZ", &year, &month, &day, &hour, &minute, &second) != 6) {
    clib_warning(stderr, "Failed to parse date-time string\n");
    return;
  }

  tm->tm_year = year - 1900;
  tm->tm_mon = month - 1;
  tm->tm_mday = day;
  tm->tm_hour = hour;
  tm->tm_min = minute;
  tm->tm_sec = second;
  tm->tm_isdst = -1;
  clib_warning("[parse_time] succeed to parse date-time string\n");

  return;
}
void key_to_string(flow_key* key, const char * result){
  json_t *obj = json_object();
  json_object_set_new(obj,"SeId", json_integer(key->seid));
  json_object_set_new(obj,"SrcIp", json_string(key->src_ip));
  json_object_set_new(obj,"DstIp", json_string(key->dst_ip));
  json_object_set_new(obj,"SrcPort", json_integer(key->src_port));
  json_object_set_new(obj,"DstPort", json_integer(key->dst_port));
  result = json_dumps(obj, JSON_INDENT(2));

}
// TODO:we should have different notification Items reports sent for different UEs
void fillNotificationItemPerPacket(UpfEventSubscription upfSub,cvector_vector_type(NotificationItem **) Notifvec,EventType type){
  if(type==USER_DATA_USAGE_TRENDS){
    pthread_mutex_lock(&ee_lock);
    struct {char* key; NotificationItem * value;} * ue_to_notif = NULL;
    sh_new_strdup(ue_to_notif);
    shdefault(ue_to_notif, NULL);
    size_t hash_length = shlen(usage_packet_hash);
    clib_warning("[send_data] fillNotificationItemPerPacket, the hash size is %d",hash_length);
    for(size_t i=0;i<hash_length;i++){
      usage_report_per_packet_t* usage_report_per_packet_vector = usage_packet_hash[i].value;
      clib_warning("[send_data_len] the length of the vector is %d", vec_len(usage_report_per_packet_vector));
      usage_report_per_packet_t * rep;
      UserDataUsageMeasurements *usage = malloc(sizeof (UserDataUsageMeasurements));
      usage->volumeMeasurement = malloc(sizeof (VolumeMeasurement));
      usage->flowInfo = malloc(sizeof (FlowInformation));
      key_to_string(usage_packet_hash[i].key,  usage->flowInfo->packFiltId);
      json_t * packet_info = json_array();
      bool downlink;
      int count = 0;
      int volume = 0;
      vec_foreach(rep, usage_report_per_packet_vector){
        if(rep == NULL)
          continue;
        volume += rep->packet_length;
        count += 1;
        downlink = rep->is_reverse;
        json_t *obj = json_object();
        json_object_set_new(obj,"packet_time",time_to_json(rep->packet_time));
        json_object_set_new(obj,"packet_length", json_integer(rep->packet_length));
        json_object_set_new(obj,"highest_layer", json_string(rep->highest_layer));
        json_object_set_new(obj,"ip_flags", json_integer(rep->ip_flags));
        json_object_set_new(obj,"tcp_length", json_integer(rep->tcp_length));
        json_object_set_new(obj,"tcp_ack", json_integer(rep->tcp_ack));
        json_object_set_new(obj,"tcp_window_size", json_integer(rep->tcp_window_size));
        json_object_set_new(obj,"udp_length", json_integer(rep->udp_length));
        json_object_set_new(obj,"ICMP_type", json_integer(rep->ICMP_type));
        json_array_append_new(packet_info, obj);
      }
      usage->flowInfo->flowDescription = json_dumps(packet_info, JSON_INDENT(4));
      usage->volumeMeasurement->totalNbOfPackets = count;
      char *strVolume = malloc(20 + 1);
      sprintf(strVolume, "%dB", volume);
      usage->volumeMeasurement->totalVolume = strVolume;
      char * ue_ip = malloc(sizeof(char )* 20);
      if(downlink){
        usage->volumeMeasurement->dlNbOfPackets = count;
        usage->volumeMeasurement->dlVolume = strVolume;
        usage->volumeMeasurement->ulNbOfPackets = 0;
        usage->volumeMeasurement->ulVolume = "0B";
        usage->flowInfo->flowDescription = DOWNLINK;
        strcpy(ue_ip, usage_packet_hash[i].key->dst_ip);
      }
      else{
        usage->volumeMeasurement->dlNbOfPackets = 0;
        usage->volumeMeasurement->dlVolume = "0B";
        usage->volumeMeasurement->ulNbOfPackets = count;
        usage->volumeMeasurement->ulVolume = strVolume;
        usage->flowInfo->flowDescription = UPLINK;
        strcpy(ue_ip, usage_packet_hash[i].key->src_ip);
      }

      clib_warning("The UE IP is", ue_ip);



      NotificationItem *item = shget(ue_to_notif, ue_ip);
      if(item == NULL){

        item = malloc(sizeof(NotificationItem));
        item->ueIpv4Addr = ue_ip;
        clib_warning("item->ueIpv4Addr", item->ueIpv4Addr );
        item->type = USER_DATA_USAGE_TRENDS;
        struct tm* tm = malloc(sizeof(struct tm));
        time_t current_time;
        time(&current_time);
        item->timeStamp = current_time;
        item->startTime = upfSub.eventReportingMode.TimeOfSubscription;
        item->snssai.sst = 0;
        item->snssai.sd[0] = "\0";
        // when UE is the source it is uplink
        item->dnn = NULL;
        item->gpsi = NULL;
        item->supi = NULL;
        item->ueMacAddr = NULL;
        item->ueIpv6Prefix = NULL;
        cvector(UserDataUsageMeasurements *) userDataMeasurements = NULL;
        cvector_push_back(userDataMeasurements, usage);
        shput(ue_to_notif, ue_ip, item);
      }
      else{
        cvector_push_back(item->userDataUsageMeasurements, usage);
      }


    }
    size_t ue_to_notif_len = shlen(ue_to_notif);
    for(int i = 0; i<ue_to_notif_len; i++){
      cvector_push_back(*Notifvec, ue_to_notif[i].value);

    }
    usage_packet_hash = NULL;
    pthread_mutex_unlock(&ee_lock);

  }
}
void fillNotificationItem(UpfEventSubscription upfSub,cvector_vector_type(NotificationItem **) Notifvec,EventType type) {
  if(type==USER_DATA_USAGE_TRENDS){
    pthread_mutex_lock(&ee_lock);
    clib_warning("[EventReport_UDUT] After locking the mutex");
    if (usage_hash == NULL){
      clib_warning("[EventReport_UDUT] There is no data to report");
      pthread_mutex_unlock(&ee_lock);
      return;
    }
    size_t hash_length = shlen(usage_hash);
    clib_warning("[send_data] fillNotificationItem, the hash size is %d",hash_length);
    for(size_t i=0;i<hash_length;i++){
      NotificationItem *item = malloc(sizeof(NotificationItem));
      item->type = USER_DATA_USAGE_TRENDS;
      struct tm* tm = malloc(sizeof(struct tm));
      time_t current_time;
      time(&current_time);
      item->timeStamp = current_time;
      item->startTime = upfSub.eventReportingMode.TimeOfSubscription;
      item->snssai.sst = 0;
      item->snssai.sd[0] = "\0";
      item->ueIpv4Addr = usage_hash[i].key;
      item->dnn = NULL;
      item->gpsi = NULL;
      item->supi = NULL;
      item->ueMacAddr = NULL;
      item->ueIpv6Prefix = NULL;
      clib_warning("assianing the IPadd which is in the item %s", item->ueIpv4Addr);
      clib_warning("assianing the IPadd which is in tha hash %s", usage_hash[i].key);
      cvector(UserDataUsageMeasurements *) userDataMeasurements = NULL;
      usage_report_per_flow_t* usage_report_per_flow_vector = usage_hash[i].value;
      clib_warning("[send_data_len] the length of the vector is %d", vec_len(usage_report_per_flow_vector));
//      for (int j = 0; j < vec_len(usage_report_per_flow_vector); j++)
      usage_report_per_flow_t* rep;

      vec_foreach(rep, usage_report_per_flow_vector){
        if(rep == NULL){
          clib_warning("[EventReport_UDUT] There is no data to report, fill notification");
          continue;
        }
        // TODO: make sure the uplink and downlink are right.
        int volume = rep->src_bytes + rep->dst_bytes;
        clib_warning("the volume is %d %d %d", volume,rep->src_bytes , rep->dst_bytes);
        char *totalVolume = malloc(20 + 1);
        sprintf(totalVolume, "%dB", volume);
        clib_warning("the volume is %d", volume);
        UserDataUsageMeasurements *usage = malloc(sizeof (UserDataUsageMeasurements));
        usage->volumeMeasurement = malloc(sizeof (VolumeMeasurement));
        usage->volumeMeasurement->totalVolume = totalVolume;
        usage->volumeMeasurement->totalNbOfPackets = rep->src_pkts + rep->dst_pkts;
        usage->volumeMeasurement->dlNbOfPackets = rep->dst_pkts;
        usage->volumeMeasurement->ulNbOfPackets = rep->src_pkts;
        volume = rep->dst_bytes;
        char *dlVolume = malloc(20 + 1);
        sprintf(dlVolume, "%dB", volume);
        clib_warning("%d%d",volume,rep->dst_bytes);
        usage->volumeMeasurement->dlVolume = dlVolume;
        clib_warning("the dl volume is %s",dlVolume);
        volume = rep->src_bytes;
        char *ulVolume = malloc(20 + 1);
        sprintf(ulVolume, "%dB", volume);
        clib_warning("the ul volume is %s",ulVolume);
        usage->volumeMeasurement->ulVolume = ulVolume;
        clib_warning("the src Ip is %s", rep->src_ip);
        clib_warning("the Dst Ip is %s", rep->dst_ip);
        json_t *obj = json_object();
        json_object_set_new(obj,"SeId", json_integer(rep->seid));
        json_object_set_new(obj,"SrcIp", json_string(rep->src_ip));
        json_object_set_new(obj,"DstIp", json_string(rep->dst_ip));
        json_object_set_new(obj,"SrcPort", json_integer(rep->src_port));
        json_object_set_new(obj,"DstPort", json_integer(rep->dst_port));
        usage->flowInfo = malloc(sizeof (FlowInformation));
        usage->flowInfo->flowDescription = json_dumps(obj, JSON_INDENT(2));
        usage->throughputMeasurement = NULL;
        usage->applicationRelatedInformation = NULL;
        usage->throughputStatisticsMeasurement = NULL;
        usage->flowInfo->ethFlowDescription = NULL;
        usage->flowInfo->fDir = BIDIRECTIONAL;
        usage->flowInfo->flowLabel = NULL;
        usage->flowInfo->packFiltId = NULL;
        usage->flowInfo->packetFilterUsage = false;
        usage->flowInfo->spi = NULL;
        usage->flowInfo->tosTrafficClass = NULL;
        usage->appID = "\0";
        cvector_push_back(userDataMeasurements, usage);
        clib_warning("[send_data] fillNotificationItem, in the loop 113. %p\n", usage->flowInfo->ethFlowDescription);
      }
      item->userDataUsageMeasurements = userDataMeasurements;
      cvector_push_back(*Notifvec, item);
      clib_warning("[send_data] fillNotificationItem, the Noitve_size %d\n", cvector_size(*Notifvec));

    }
    pthread_mutex_unlock(&ee_lock);
  }
  clib_warning("[send_data] fillNotificationItem, end of function");
}
void create_send_report(UpfEventSubscription upfSub,EventType type){
  if(type == USER_DATA_USAGE_TRENDS){
    clib_warning("[EventReport_UDUT] in create_custom_report");
    cvector_vector_type(NotificationItem *) Notifvec = NULL;
    clib_warning("[EventReport_UDUT] after creating the notif vec");
    fillNotificationItem(upfSub, &Notifvec, type);
    if(Notifvec == NULL){
      clib_warning("[EventReport_UDUT] There is no data to report");
      return;
    }
    clib_warning("[EventReport_UDUT] fillNotificationItem, the Noitve_size %d\n", cvector_size(Notifvec));
    for(size_t i = 0; i < cvector_size(Notifvec); i++){
      clib_warning("[EventReport_UDUT] in the for");
      json_t* callBack_Report = serialize_callBack(Notifvec[i], upfSub.notifyCorrelationId, 0);
      if (!callBack_Report) {
        clib_warning("The json_t object is NULL.\n");
      }
      char *json_str = json_dumps(callBack_Report, JSON_INDENT(2));
      send_report (json_str, upfSub, type);
    }
    for (size_t i = 0; i < cvector_size(Notifvec); i++) {
      if (Notifvec[i] != NULL) {
        clib_warning("[1|free] hash Notifvec[i]");
        free(Notifvec[i]);
      }
    }
    clib_warning("[2|free] free notifs cvector ");
    cvector_free(Notifvec);

    clib_warning("[EventReport_UDUT] End of create_send_report");
  }
}
void send_report(char *json_data,UpfEventSubscription upfSub,EventType type){
  // we Assume that we have the upf raw data hare
  // we call the function that can customized the needed measurements
  CURL *curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();
  if (curl) {
    int report_num = upfSub.eventReportingMode.sent_reports;
    char report_num_str[10];
    char report_num_head[50] = "Report_number: ";
    snprintf(report_num_str, sizeof(report_num_str), "%d", report_num);
    strcat(report_num_head, report_num_str);
    curl_easy_setopt(curl, CURLOPT_URL, upfSub.eventNotifyUri);
    fprintf(stdout,"the URI is %s\n", upfSub.eventNotifyUri);
    char c_time[50];
    get_current_time_send(c_time, sizeof(c_time));
    clib_warning("[DSN_Latency]the report number sent and the time is %d, %s\n", report_num, c_time);
//    fprintf(stdout,);
    fprintf(stdout, "dat: %s\n", json_data);
    fflush(stdout);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L); // To set the size dependent to json

    struct curl_slist *headers = NULL;

    headers = curl_slist_append(headers, "Expect:"); // To Disable 100 continue
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, report_num_head);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    res = curl_easy_perform(curl);

    // Check for errors
    if (res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    clib_warning("[EventReport_UDUT] in send_report 184");

  }

}
//UpfEventSubscription*
void* EventReport_UDUT() {
    time_t current_time;
    time(&current_time);
    uint32_t eventType = (uint32_t) USER_DATA_USAGE_TRENDS;

    clib_warning("[EventReport_UDUT] in the Event report.");
    cvector_vector_type(UpfEventSubscription*) retrieved_vec = hmget(subHash, USER_DATA_USAGE_TRENDS);
    if (retrieved_vec!=NULL) {
      clib_warning("[EventReport_UDUT] retrived vector was != NULL");
      for (int i = 0; i < cvector_size(retrieved_vec); i++) {
        if(retrieved_vec[i]->eventReportingMode.trigger == PERIODIC){
          if(retrieved_vec[i]->eventReportingMode.TimeOfLastReport == 0){
            if (current_time - retrieved_vec[i]->eventReportingMode.TimeOfSubscription >=retrieved_vec[i]->eventReportingMode.repPeriod){
              clib_warning("[EventReport_UDUT] CALLING SEND report function");
              create_send_report(*retrieved_vec[i], USER_DATA_USAGE_TRENDS); // dummy
              clib_warning("[EventReport_UDUT|207] after SENDing the report.");
              retrieved_vec[i]->eventReportingMode.TimeOfLastReport = current_time;
              retrieved_vec[i]->eventReportingMode.sent_reports += 1;
            }
          }
          else if (current_time - retrieved_vec[i]->eventReportingMode.TimeOfLastReport >=retrieved_vec[i]->eventReportingMode.repPeriod){
            clib_warning("[EventReport_UDUT] CALLING SEND report function");
            create_send_report(*retrieved_vec[i], USER_DATA_USAGE_TRENDS); // dummy
            clib_warning("EventReport_UDUT|207] after SENDing the report.");
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

  return NULL;
}

