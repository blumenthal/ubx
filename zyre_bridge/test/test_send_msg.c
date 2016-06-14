#include <zyre.h>
#include <jansson.h>
#include <uuid/uuid.h>
#include <string.h>

typedef struct _json_msg_t {
    char *metamodel;
    char *model;
    char *type;
    char *payload;
} json_msg_t;

typedef struct _query_t {
    char *UID;
    char *msg;
} query_t;

json_t * load_config_file(char* file) {
    json_error_t error;
    json_t * root;
    root = json_load_file(file, JSON_ENSURE_ASCII, &error);
    printf("[%s] config file: %s\n", json_string_value(json_object_get(root, "short-name")), json_dumps(root, JSON_ENCODE_ANY));
    if(!root) {
   	printf("Error parsing JSON file! line %d: %s\n", error.line, error.text);
    	return NULL;
    }
    return root;
}

int decode_json(char* message, json_msg_t *result) {
	/**
	 * decodes a received msg to json_msg types
	 *
	 * @param received msg as char*
	 * @param json_msg_t* at which the result is stored
	 *
	 * @return returns 0 if successful and -1 if an error occurred
	 */
    json_t *root;
    json_error_t error;
    root = json_loads(message, 0, &error);

    if(!root) {
    	printf("Error parsing JSON string! line %d: %s\n", error.line, error.text);
    	return -1;
    }

    if (json_object_get(root, "metamodel")) {
    	result->metamodel = strdup(json_string_value(json_object_get(root, "metamodel")));
    } else {
    	printf("Error parsing JSON string! Does not conform to msg model.\n");
    	return -1;
    }
    if (json_object_get(root, "model")) {
		result->model = strdup(json_string_value(json_object_get(root, "model")));
	} else {
		printf("Error parsing JSON string! Does not conform to msg model.\n");
		return -1;
	}
    if (json_object_get(root, "type")) {
		result->type = strdup(json_string_value(json_object_get(root, "type")));
	} else {
		printf("Error parsing JSON string! Does not conform to msg model.\n");
		return -1;
	}
    if (json_object_get(root, "payload")) {
    	result->payload = strdup(json_dumps(json_object_get(root, "payload"), JSON_ENCODE_ANY));
	} else {
		printf("Error parsing JSON string! Does not conform to msg model.\n");
		return -1;
	}
    json_decref(root);
    return 0;
}

char* send_msg() {
	/**
	 * creates a world model update msg (fixed for now)
	 *
	 * @return the string encoded JSON msg that can be sent directly via zyre. Must be freed by user! Returns NULL if wrong json types are passed in.
	 */

    json_t *root;
    root = json_object();
    json_object_set(root, "@worldmodeltype", json_string("RSGUpdate"));
    json_object_set(root, "operation", json_string("CREATE"));
	json_object_set(root, "parentId", json_string("e379121f-06c6-4e21-ae9d-ae78ec1986a1"));
    json_t *node;
    node = json_object();
    json_object_set(node, "@graphtype", json_string("Node"));
	zuuid_t *uuid = zuuid_new ();
	assert(uuid);
	//printf("UUID: %s\n",zuuid_str_canonical(uuid));
    json_object_set(node, "id", json_string(zuuid_str_canonical(uuid)));
	json_t *attributes;
    attributes = json_array();
	json_t *kv;
	kv = json_object();
	json_object_set(kv, "key", json_string("name"));
	json_object_set(kv, "value", json_string("myNode"));
	json_array_append_new(attributes, kv);
	kv = json_object();
	json_object_set(kv, "key", json_string("comment"));
	json_object_set(kv, "value", json_string("Some human readable comment on the node."));
	json_array_append_new(attributes, kv);
	json_object_set(node, "attributes", attributes);
    json_object_set(root, "node", node);
    char* ret = json_dumps(root, JSON_ENCODE_ANY);
	
	json_decref(kv);
	json_decref(attributes);
	json_decref(node);
    json_decref(root);
    return ret;
}

int main(int argc, char *argv[]) {

    int major, minor, patch;
    zyre_version (&major, &minor, &patch);
    assert (major == ZYRE_VERSION_MAJOR);
    assert (minor == ZYRE_VERSION_MINOR);
    assert (patch == ZYRE_VERSION_PATCH);

    // load configuration file
    json_t * config = load_config_file("test_config.json");
    if (config == NULL) {
      return -1;
    }
    const char *self = json_string_value(json_object_get(config, "short-name"));
    assert(self);
    bool verbose = json_is_true(json_object_get(config, "verbose"));
    int timeout = json_integer_value(json_object_get(config, "timeout"));
    assert(timeout > 0);

    //  Create local gossip node
    zyre_t *local = zyre_new (self);
    assert (local);
    printf("[%s] my local UUID: %s\n", self, zyre_uuid(local));
    /* config is a JSON object */
    // set values for config file as zyre header.
    const char *key;
    json_t *value;
    json_object_foreach(config, key, value) {
        /* block of code that uses key and value */
    	const char *header_value;
    	if(json_is_string(value)) {
    		header_value = json_string_value(value);
    	} else {
    		header_value = json_dumps(value, JSON_ENCODE_ANY);
    	}
    	printf("header key value pair\n");
    	printf("%s %s\n",key,header_value);
    	zyre_set_header(local, key, "%s", header_value);
    }

    int rc;
    if(!json_is_null(json_object_get(config, "gossip_endpoint"))) {
    	rc = zyre_set_endpoint (local, "%s", json_string_value(json_object_get(config, "local_endpoint")));
    	assert (rc == 0);
    	printf("[%s] using gossip with local endpoint %s\n", self, json_string_value(json_object_get(config, "local_endpoint")));
    	//  Set up gossip network for this node
    	zyre_gossip_connect (local, "%s", json_string_value(json_object_get(config, "gossip_endpoint")));
    	printf("[%s] using gossip with gossip hub '%s' \n", self,json_string_value(json_object_get(config, "gossip_endpoint")));
    } else {
    	printf("[%s] WARNING: no local gossip communication is set! \n", self);
    }
    rc = zyre_start (local);
    assert (rc == 0);
    char* localgroup = strdup("local");
    zyre_join (local, localgroup);
    //  Give time for them to connect
    zclock_sleep (1000);

    if (verbose)
        zyre_dump (local);

    //create a list to store queries...
    zlist_t *query_list = zlist_new();
    assert (query_list);
    assert (zlist_size (query_list) == 0);

    int alive = 1; //will be used to quit program after answer to query is received
    zpoller_t *poller =  zpoller_new (zyre_socket(local), NULL);

    //check if there is at least one component connected
    zlist_t * tmp = zlist_new ();
    assert (tmp);
    assert (zlist_size (tmp) == 0);
    // timestamp for timeout
    struct timespec ts = {0,0};
    if (clock_gettime(CLOCK_MONOTONIC,&ts)) {
		printf("[%s] Could not assign time stamp!\n",self);
	}
    struct timespec curr_time = {0,0};
    while (true) {
    	printf("[%s] Checking for connected peers.\n",self);
    	zlist_t * tmp = zyre_peers(local);
    	printf("[%s] %zu peers connected.\n", self,zlist_size(tmp));
    	if (zlist_size (tmp) > 0)
    		break;
		if (!clock_gettime(CLOCK_MONOTONIC,&curr_time)) {
			// if timeout, stop component
			double curr_time_msec = curr_time.tv_sec*1.0e3 +curr_time.tv_nsec*1.0e-6;
			double ts_msec = ts.tv_sec*1.0e3 +ts.tv_nsec*1.0e-6;
			if (curr_time_msec - ts_msec > timeout) {
				printf("[%s] Timeout! Could not connect to other peers.\n",self);
				alive = 0;
				break;
			}
		} else {
			printf ("[%s] could not get current time\n", self);
		}
    	zclock_sleep (1000);
    }
    zlist_destroy(&tmp);

    while(!zsys_interrupted && alive == 1) {


		
		
    		//query mediator to send a fire and forget msg
    		printf("\n");
    		printf("#########################################\n");
    		printf("[%s] Sending a msg without recipients (aka fire and forget msg)\n",self);
    		printf("#########################################\n");
    		printf("\n");
			zuuid_t *query_uuid = zuuid_new ();
			assert (query_uuid);
			json_t *recip = json_array();
			assert((recip)&&(json_array_size(recip)==0));
			char *msg = send_msg();
			if (msg) {
				zyre_shouts(local, localgroup, "%s", msg);
				printf("[%s] Sent msg: %s \n",self,msg);
				zstr_free(&msg);
			} else {
				alive = false;
			}
			zuuid_destroy (&query_uuid);


    	void *which = zpoller_wait (poller, ZMQ_POLL_MSEC);
    	if (which) {
			printf("[%s] local data received!\n", self);
			zmsg_t *msg = zmsg_recv (which);
			if (!msg) {
				printf("[%s] interrupted!\n", self);
				return -1;
			}
			//reset timeout
			if (clock_gettime(CLOCK_MONOTONIC,&ts)) {
				printf("[%s] Could not assign time stamp!\n",self);
			}
			char *event = zmsg_popstr (msg);
/*			if (streq (event, "WHISPER")) {
                assert (zmsg_size(msg) == 3);
                char *peerid = zmsg_popstr (msg);
                char *name = zmsg_popstr (msg);
                char *message = zmsg_popstr (msg);
                printf ("[%s] %s %s %s %s\n", self, event, peerid, name, message);
                //printf("[%s] Received: %s from %s\n",self, event, name);
				json_msg_t *result = (json_msg_t *) zmalloc (sizeof (json_msg_t));
				if (decode_json(message, result) == 0) {
					// load the payload as json
					json_t *payload;
					json_error_t error;
					payload= json_loads(result->payload,0,&error);
					if(!payload) {
						printf("Error parsing JSON send_remote! line %d: %s\n", error.line, error.text);
					} else {
						const char *uid = json_string_value(json_object_get(payload,"UID"));
						//TODO:does this string need to be freed?
						if (!uid){
							printf("[%s] Received msg without UID!\n", self);
						} else {
							// search through stored list of queries and check if this query corresponds to one we have sent
							query_t *it = zlist_first(query_list);
							int found_UUID = 0;
							while (it != NULL) {
								if streq(it->UID, uid) {
									printf("[%s] Received reply to query %s.\n", self, uid);
									if (streq(result->type,"peer-list")){
										printf("Received peer list: %s\n",result->payload);
										//TODO: search list for a wasp
										alive = 0;

									} else if (streq(result->type,"communication_report")){
										printf("Received communication_report: %s\n",result->payload);
										/////////////////////////////////////////////////
										//Do something with the report
										if (json_is_true(json_object_get(payload,"success"))){
											printf("Yeay! All recipients have received the msg.\n");
										} else {
											printf("Sending msg was not successful because of: %s\n",json_string_value(json_object_get(payload,"error")));
										}
										/////////////////////////////////////////////////

										if (streq(json_string_value(json_object_get(payload,"error")),"Unknown recipients")){
											//This is really not how coordination in a program should be done -> TODO: clean up
						
										}

									}
									found_UUID = 1;
									zlist_remove(query_list,it);
									//TODO: make sure the data of that query is properly freed
								}
								it = zlist_next(query_list);
							}
							if (found_UUID == 0) {
								printf("[%s] Received a msg with an unknown UID!\n", self);
							}
						}
						json_decref(payload);
					}



				} else {
					printf ("[%s] message could not be decoded\n", self);
				}
				free(result);
				zstr_free(&peerid);
				zstr_free(&name);
				zstr_free(&message);
			} else {*/
				printf ("[%s] received %s msg\n", self, event);
	//		}
			zstr_free (&event);
			zmsg_destroy (&msg);
    	} else {
			if (!clock_gettime(CLOCK_MONOTONIC,&curr_time)) {
				// if timeout, stop component
				double curr_time_msec = curr_time.tv_sec*1.0e3 +curr_time.tv_nsec*1.0e-6;
				double ts_msec = ts.tv_sec*1.0e3 +ts.tv_nsec*1.0e-6;
				if (curr_time_msec - ts_msec > timeout) {
					printf("[%s] Timeout! No msg received for %i msec.\n",self,timeout);
					break;
				}
			} else {
				printf ("[%s] could not get current time\n", self);
			}
    		printf ("[%s] waiting for a reply. Could execute other code now.\n", self);
    		zclock_sleep (1000);
    	}
    }

    //free memory of all items from the query list
    query_t *it;
    while(zlist_size (query_list) > 0){
    	it = (query_t*) zlist_pop(query_list);
    	free(it->UID);
    	free(it->msg);
    }

    zyre_stop (local);
	printf ("[%s] Stopping...", self);
	zclock_sleep (100);
	printf (" done\n");
    zyre_destroy (&local);
    //  @end
    printf ("[%s] SHUTDOWN\n", self);
    return 0;
}

