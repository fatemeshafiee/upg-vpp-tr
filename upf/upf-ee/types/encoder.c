
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
#include "encoder.h"
#include <vlib/vlib.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlib/unix/unix.h>

json_t * json_string_or_null(char* str){
  if(str == NULL) return json_null();
  return json_string(str);

}

json_t *time_to_json(time_t timestamp){
  char date_time[30];
  struct tm td_info;
  gmtime_r(&timestamp, &td_info);
  strftime(date_time, sizeof(date_time), "%Y-%m-%dT%H:%M:%SZ", &td_info);
  return json_string(date_time);
}
json_t *serialize_eth_flow_description(const EthFlowDescription *eth) {
  clib_warning("[encoder] in serialize_eth_flow_description the eth is %p\n", (void *)eth);
  if (eth == NULL){
    clib_warning("[encoder] in serialize_eth_flow_description in the if");
    return json_null();

  }
  json_t *obj = json_object();
  json_object_set_new(obj, "destMacAddr", json_string(eth->destMacAddr));
  json_object_set_new(obj, "ethType", json_string(eth->ethType));
  json_object_set_new(obj, "fDesc", json_string(eth->fDesc));
  json_object_set_new(obj, "fDir", json_string(getFlowDirectionString(eth->fDir)));
  json_object_set_new(obj, "sourceMacAddr", json_string(eth->sourceMacAddr));
  json_t * Vlan_array = json_null();
  if(eth->vlanTags){
    Vlan_array = json_array();
    clib_warning("[encoder] in serialize_eth_flow_description in the 2nd if");
    for (size_t i = 0; i < cvector_size(eth->vlanTags); i++){
      clib_warning("[encoder] in serialize_eth_flow_description in the for");
      json_array_append_new(Vlan_array, json_string(eth->vlanTags[i]));
    }

  }
  json_object_set_new(obj, "vlanTags", Vlan_array);
  json_object_set_new(obj, "srcMacAddrEnd", json_string(eth->srcMacAddrEnd));
  json_object_set_new(obj, "destMacAddrEnd", json_string(eth->destMacAddrEnd));
  return obj;
}

json_t *serialize_flow_information(const FlowInformation *flow) {
  clib_warning("[encoder] in serialize_flow_information");
  if (flow == NULL) return json_null();
  json_t *obj = json_object();
  json_object_set_new(obj, "flowDescription", json_string_or_null(flow->flowDescription));
  json_object_set_new(obj, "ethFlowDescription", serialize_eth_flow_description(flow->ethFlowDescription));
  json_object_set_new(obj, "packFiltId", json_string_or_null(flow->packFiltId));
  json_object_set_new(obj, "packetFilterUsage", json_boolean(flow->packetFilterUsage));
  json_object_set_new(obj, "tosTrafficClass", json_string_or_null(flow->tosTrafficClass));
  json_object_set_new(obj, "spi", json_string_or_null(flow->spi));
  json_object_set_new(obj, "flowLabel", json_string_or_null(flow->flowLabel));
  json_object_set_new(obj, "fDir", json_string_or_null(getFlowDirectionString(flow->fDir)));
  clib_warning("[encoder] end of serialize_flow_information");

  return obj;
}
json_t *serialize_suggestion_information(ReportingSuggestionInformation Sinfo){
  clib_warning("[encoder] in serialize_suggestion_information");
  json_t *obj = json_object();
  json_object_set_new(obj, "reportingUrgency", json_string(getReportingUrgencyString(Sinfo.reportingUrgency)));
  json_object_set_new(obj, "reportingTimeInfo", json_integer(Sinfo.reportingTimeInfo));
  return  obj;
}

json_t *serialize_upf_event(const UpfEvent *event) {
  clib_warning("[encoder] in serialize_upf_event");
  json_t *obj = json_object();
  json_object_set_new(obj, "type", json_string(getEventTypeString(event->type)));
  json_object_set_new(obj, "immediateFlag", json_boolean(event->immediateFlag));
  json_t *measurementTypes = json_null();
  if (event->measurementTypes){
    measurementTypes = json_array();
    for (size_t i = 0; i < cvector_size(event->measurementTypes); i++) {
      json_array_append_new(measurementTypes, json_string(getMeasurementTypeString(event->measurementTypes[i])));
    }
  }

  json_object_set_new(obj, "measurementTypes", measurementTypes);
  clib_warning("[encoder] in serialize_upf_event after measurementTypes");

  json_t *TrafficFilters = json_null();
  if(event->TrafficFilters){
    clib_warning("[encoder] in serialize_upf_event after in the if of event->TrafficFilters");

    TrafficFilters = json_array();
    for (size_t i = 0; i < cvector_size(event->TrafficFilters); i++) {
      json_array_append_new(TrafficFilters, serialize_flow_information(&(event->TrafficFilters[i])));
    }
  }

  json_object_set_new(obj, "TrafficFilters", TrafficFilters);
  clib_warning("[encoder] in serialize_upf_event after TrafficFilters");


  json_t *appIds = json_null();
  if (event->appIds) {
    appIds = json_array();
    for (size_t i = 0; i < cvector_size(event->appIds); i++){
      json_array_append_new(appIds,json_string(event->appIds[i]));
    }
  }
  json_object_set_new(obj, "appIds", appIds);
  clib_warning("[encoder] in serialize_upf_event after appIds");

  json_object_set_new(obj, "granularityOfMeasurement", json_string(getGranularityOfMeasurementString(event->granularityOfMeasurement)));
  json_object_set_new(obj, "reportingSuggestionInfo", serialize_suggestion_information(event->reportingSuggestionInfo));
  return obj;
}

json_t *serialize_event_reporting_mode(const EventReportingMode *mode) {
  json_t *obj = json_object();
  clib_warning("[encoder] in serialize_event_reporting_mode");
  json_object_set_new(obj, "trigger", json_string(getUpfEventTriggerString(mode->trigger)));
  json_object_set_new(obj, "maxReports", json_integer(mode->maxReports));
  json_object_set_new(obj, "expiry", json_string(mode->expiry));
  json_object_set_new(obj, "repPeriod", json_integer(mode->repPeriod));
  json_object_set_new(obj, "sampRatio", json_integer(mode->sampRatio));
  json_object_set_new(obj, "notifFlag", json_string(getNotificationFlagString(mode->notifFlag)));

  json_t *partitioningCriteria = json_null();
  if(mode->partitioningCriteria){
    partitioningCriteria = json_array();
    for (size_t i = 0; i < cvector_size(mode->partitioningCriteria); i++) {
      json_array_append_new(partitioningCriteria, json_string(getPartitioningCriteriaString(mode->partitioningCriteria[i])));
    }

  }


  json_object_set_new(obj, "partitioningCriteria", partitioningCriteria);
  json_t *mutingExcInstructions_json = json_object();
  json_object_set_new(mutingExcInstructions_json, "bufferedNotifs", json_string(getBufferedNotificationsActionString(mode->mutingExcInstructions.BufferedNotificationinstructions)));
  json_object_set_new(mutingExcInstructions_json, "subscription", json_string(getSubscriptionActionString(mode->mutingExcInstructions.Subscriptioninstructions)));
  json_object_set_new(obj, "mutingExcInstructions", mutingExcInstructions_json);


  return obj;

}
json_t *serialize_snssai(const Snssai snssai){
  clib_warning("[encoder] in serialize_snssai");
  json_t * Snssai_json = json_object();
  json_object_set_new(Snssai_json, "sst", json_integer(snssai.sst));
  json_object_set_new(Snssai_json, "sd", json_string(snssai.sd));
  return Snssai_json;
}
json_t *serialize_UeIpAddress(const UeIpAddressVersion ueIpAddressVersion, const char* ueIpAddress ){
  clib_warning("[encoder] in serialize_UeIpAddress");
  json_t *ue_json = json_object();
  if(ueIpAddressVersion == V4) json_object_set_new(ue_json, "ipv4Addr",json_string(ueIpAddress));
  else if(ueIpAddressVersion == V6) json_object_set_new(ue_json, "ipv6Addr",json_string(ueIpAddress));
  else if(ueIpAddressVersion == V6Prefix) json_object_set_new(ue_json, "ipv6Prefix",json_string(ueIpAddress));
  return ue_json;
}
json_t *serialize_upf_event_subscription(const UpfEventSubscription *subscription) {
  clib_warning("[encoder] in serialize_upf_event_subscription");
  json_t *obj = json_object();
  json_object_set_new(obj, "eventNotifyUri", json_string(subscription->eventNotifyUri));
  json_object_set_new(obj, "notifyCorrelationId", json_string(subscription->notifyCorrelationId));
  json_object_set_new(obj, "nfId", json_string(subscription->nfId));
  json_object_set_new(obj, "ueIpAddress", json_string(subscription->ueIpAddress));
  json_object_set_new(obj, "supi", json_string(subscription->supi));
  json_object_set_new(obj, "gpsi", json_string(subscription->gpsi));
  json_object_set_new(obj, "pei", json_string(subscription->pei));
  json_object_set_new(obj, "anyUe", json_boolean(subscription->anyUe));
  json_object_set_new(obj, "dnn", json_string(subscription->dnn));
  json_object_set_new(obj, "eventReportingMode", serialize_event_reporting_mode(&(subscription->eventReportingMode)));
  json_object_set_new(obj, "ueIpAddress", serialize_UeIpAddress(subscription->ueIpAddressVersion, subscription->ueIpAddress));
  json_t *events = json_array();
  for (size_t i = 0; i < cvector_size(subscription->EventList); i++) {
    json_array_append_new(events, serialize_upf_event(&(subscription->EventList[i])));
  }
  json_object_set_new(obj, "eventList", events);
  json_object_set_new(obj, "snssai", serialize_snssai(subscription->snssai));
  return obj;
}
json_t *serialize_created_response(const UpfEventSubscription *subscription,char * subscriptionId ) {
  json_t *obj = json_object();
  json_object_set_new(obj,"subscription", serialize_upf_event_subscription(subscription));
  json_object_set_new(obj, "subscriptionId", json_string(subscriptionId));
  return  obj;
}
json_t *serialize_DomainInformation(DomainInformation *domainInfo) {
  json_t *obj = json_object();
  json_object_set_new(obj, "domainName",json_string(domainInfo->domainName));
  json_object_set_new(obj, "domainNameProtocol",json_string(getDnProtocolString(domainInfo->domainNameProtocol)));

  return  obj;
}

json_t *serialize_VolumeMeasurement(VolumeMeasurement *volumeMeasurement) {
  if (volumeMeasurement == NULL) return json_null();
  clib_warning("[encoder] start of serialize_VolumeMeasurement");
  json_t *obj = json_object();

  clib_warning("[encoder] totalVolume serialize_VolumeMeasurement %s", volumeMeasurement->totalVolume);
  json_object_set_new(obj, "totalVolume",json_sprintf("%s",volumeMeasurement->totalVolume));


  clib_warning("[encoder] ulVolume serialize_VolumeMeasurement %s",volumeMeasurement->ulVolume );
  json_object_set_new(obj, "ulVolume",json_sprintf("%s",volumeMeasurement->ulVolume));

  clib_warning("[encoder] dlVolume serialize_VolumeMeasurement %s", volumeMeasurement->dlVolume);
  json_object_set_new(obj, "dlVolume",json_sprintf("%s",volumeMeasurement->dlVolume));

  clib_warning("[encoder] totalNbOfPackets serialize_VolumeMeasurement %d", volumeMeasurement->totalNbOfPackets);
  json_object_set_new(obj, "totalNbOfPackets", json_integer(volumeMeasurement->totalNbOfPackets));

  clib_warning("[encoder] ulNbOfPackets serialize_VolumeMeasurement %d", volumeMeasurement->ulNbOfPackets);
  json_object_set_new(obj, "ulNbOfPackets", json_integer(volumeMeasurement->ulNbOfPackets));

  clib_warning("[encoder] dlNbOfPackets serialize_VolumeMeasurement %d", volumeMeasurement->dlNbOfPackets);
  json_object_set_new(obj, "dlNbOfPackets", json_integer(volumeMeasurement->dlNbOfPackets));
  return  obj;
}
json_t *serialize_ThroughputMeasurement(ThroughputMeasurement *throughputMeasurement) {
  if (throughputMeasurement == NULL){
    clib_warning("[encoder] returning null of serialize_ThroughputMeasurement");
    return json_null();
  }
  clib_warning("[encoder] start of serialize_ThroughputMeasurement");

  json_t *obj = json_object();
  json_object_set_new(obj, "ulThroughput",json_string(throughputMeasurement->ulThroughput));
  json_object_set_new(obj, "dlThroughput",json_string(throughputMeasurement->dlThroughput));
  json_object_set_new(obj, "ulPacketThroughput",json_string(throughputMeasurement->ulPacketThroughput));
  json_object_set_new(obj, "dlPacketThroughput",json_string(throughputMeasurement->dlPacketThroughput));
  clib_warning("[encoder] end of serialize_ThroughputMeasurement");
  return  obj;
}
json_t *serialize_ApplicationRelatedInformation(ApplicationRelatedInformation *applicationRelatedInformation) {
  if (applicationRelatedInformation == NULL){
    clib_warning("[encoder] returning null of serialize_ApplicationRelatedInformation");
    return json_null();
  }
  clib_warning("[encoder] start of serialize_ApplicationRelatedInformation");

  json_t *obj = json_object();
  json_t * urls =json_null();
  if(applicationRelatedInformation->urls)
  {
    urls = json_array();
    for (size_t i = 0; i < cvector_size(applicationRelatedInformation->urls); i++){
      json_array_append_new(urls, json_string(applicationRelatedInformation->urls[i]));
    }
  }

  json_object_set_new(obj, "urls", urls);
  json_t * domainInfoList = json_null();
  if(applicationRelatedInformation->domainInfoList){
    domainInfoList = json_array();
    for (size_t i = 0; i < cvector_size(applicationRelatedInformation->domainInfoList); i++){
      json_array_append_new(domainInfoList, serialize_DomainInformation(applicationRelatedInformation->domainInfoList[i]));
    }
  }

//    json_object_set_new(obj, "urls", urls);
  json_object_set_new(obj, "domainInfoList", domainInfoList);
  clib_warning("[encoder] end of serialize_ApplicationRelatedInformation");
  return  obj;
}
json_t *serialize_ThroughputStatisticsMeasurement(ThroughputStatisticsMeasurement *throughputStatisticsMeasurement) {
  if (throughputStatisticsMeasurement == NULL){
    clib_warning("[encoder] returning null of serialize_ThroughputStatisticsMeasurement");
    return json_null();
  }
  json_t *obj = json_object();
  json_object_set_new(obj, "ulAverageThroughput",json_string(throughputStatisticsMeasurement->ulAverageThroughput));
  json_object_set_new(obj, "dlAverageThroughput",json_string(throughputStatisticsMeasurement->dlAverageThroughput));
  json_object_set_new(obj, "ulPeakThroughput",json_string(throughputStatisticsMeasurement->ulPeakThroughput));
  json_object_set_new(obj, "dlPeakThroughPut",json_string(throughputStatisticsMeasurement->dlPeakThroughput));
  json_object_set_new(obj, "ulAveragePacketThroughput",json_string(throughputStatisticsMeasurement->ulAveragePacketThroughput));
  json_object_set_new(obj, "dlAveragePacketThroughput",json_string(throughputStatisticsMeasurement->dlAveragePacketThroughput));
  json_object_set_new(obj, "ulPeakPacketThroughput",json_string(throughputStatisticsMeasurement->ulPeakPacketThroughput));
  json_object_set_new(obj, "dlPeakPacketThroughput",json_string(throughputStatisticsMeasurement->dlPeakPacketThroughput));

  return  obj;
}

json_t *serialize_UserDataUsageMeasurements(UserDataUsageMeasurements *userDataUsageMeasurements) {
  if(userDataUsageMeasurements == NULL){
    clib_warning("[encoder] returning NULL in serialize_UserDataUsageMeasurements");
    return json_null();
  }
  clib_warning("[encoder] in serialize_UserDataUsageMeasurements 300");
  json_t *obj = json_object();
  clib_warning("[encoder] in serialize_UserDataUsageMeasurements 301");
  json_object_set_new(obj, "appId",json_string(userDataUsageMeasurements->appID));
  clib_warning("[encoder] in serialize_UserDataUsageMeasurements 302");
  json_object_set_new(obj, "flowInfo", serialize_flow_information(userDataUsageMeasurements->flowInfo));
  clib_warning("[encoder] in serialize_UserDataUsageMeasurements 303");
  json_object_set_new(obj, "volumeMeasurement",serialize_VolumeMeasurement(userDataUsageMeasurements->volumeMeasurement));
  clib_warning("[encoder] in serialize_UserDataUsageMeasurements 304");
  json_object_set_new(obj, "throughputMeasurement", serialize_ThroughputMeasurement(userDataUsageMeasurements->throughputMeasurement));
  clib_warning("[encoder] in serialize_UserDataUsageMeasurements 305");
  json_object_set_new(obj, "applicationRelatedInformation",serialize_ApplicationRelatedInformation(userDataUsageMeasurements->applicationRelatedInformation));
  clib_warning("[encoder] in serialize_UserDataUsageMeasurements 306");
  json_object_set_new(obj, "throughputStatisticsMeasurement",serialize_ThroughputStatisticsMeasurement(userDataUsageMeasurements->throughputStatisticsMeasurement));
  clib_warning("[encoder] End of of serialize_UserDataUsageMeasurements");
  return  obj;
}

json_t *serialize_Notification_Item(NotificationItem *notificationItem) {
  clib_warning("[encoder] in serialize_Notification_Item, line 319");
  json_t *obj = json_object();
  clib_warning("[encoder] in serialize_Notification_Item, line 321");
  json_object_set_new(obj, "eventType", json_string(getEventTypeString(notificationItem->type)));
  clib_warning("[encoder] in serialize_Notification_Item, line 322, %s",notificationItem->ueIpv4Addr);
  json_object_set_new(obj,"ueIpv4Addr", json_string(notificationItem->ueIpv4Addr));
  clib_warning("[encoder] in serialize_Notification_Item, line 323");
  json_object_set_new(obj,"ueIpv6Prefix", json_string(notificationItem->ueIpv6Prefix));
  clib_warning("[encoder] in serialize_Notification_Item, line 324");
  json_object_set_new(obj,"ueMacAddr", json_string(notificationItem->ueMacAddr));
  clib_warning("[encoder] in serialize_Notification_Item, line 325");
  json_object_set_new(obj,"dnn", json_string(notificationItem->dnn));
  clib_warning("[encoder] in serialize_Notification_Item, line 326");
  json_object_set_new(obj,"snssai", serialize_snssai(notificationItem->snssai));
  clib_warning("[encoder] in serialize_Notification_Item, line 327");
  json_object_set_new(obj,"gpsi", json_string(notificationItem->gpsi));
  clib_warning("[encoder] in serialize_Notification_Item, line 328");
  json_object_set_new(obj,"supi", json_string(notificationItem->supi));
  clib_warning("[encoder] in serialize_Notification_Item, line 329");
  json_object_set_new(obj,"timeStamp",time_to_json(notificationItem->timeStamp));
  clib_warning("[encoder] in serialize_Notification_Item, line 330");
  json_object_set_new(obj,"startTime", time_to_json(notificationItem->startTime));
  clib_warning("[encoder] in serialize_Notification_Item, line 331");
  json_t * userMeasurements = json_null();
  if(notificationItem->userDataUsageMeasurements){
    userMeasurements = json_array();
    for (size_t i = 0; i < cvector_size(notificationItem->userDataUsageMeasurements); i++){
      clib_warning("[encoder]  in the loop of serialize_UserDataUsageMeasurements, in the for i: %d", i);
      json_array_append_new(userMeasurements, serialize_UserDataUsageMeasurements(notificationItem->userDataUsageMeasurements[i]));
    }
  }
  json_object_set_new(obj,"userDataUsageMeasurements",userMeasurements);
  clib_warning("[encoder] End of of serialize_Notification_Item");
  return  obj;
}
json_t *serialize_callBack(NotificationItem *notificationItem, const char *correlationId , int achievedSampRatio) {
  json_t *obj = json_object();
  json_t * notificationItems = json_array();
  clib_warning("[encoder] in serialize_callBack, line 345");
  json_array_append_new(notificationItems, serialize_Notification_Item(notificationItem));
  clib_warning("[encoder] in serialize_callBack, line 346");
  json_object_set_new(obj, "notificationItems",notificationItems);
  clib_warning("[encoder] in serialize_callBack, correlationId is %s",correlationId );
  json_object_set_new(obj,"correlationId", json_string(correlationId));
  clib_warning("[encoder] in serialize_callBack, achievedSampRatio is %d",achievedSampRatio );
  json_object_set_new(obj,"achievedSampRatio", json_integer(achievedSampRatio));
  clib_warning("[encoder] End of of serialize_callBack");

  return  obj;
}
