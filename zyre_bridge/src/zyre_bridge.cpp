#include "zyre_bridge.hpp"
#include <zyre.h>
#include <jansson.h>
#include <vector>
#include <string>

UBX_MODULE_LICENSE_SPDX(BSD-3-Clause)

static void zyre_bridge_actor(zsock_t *pipe, void *args);

/* define a structure for holding the block local state. By assigning an
 * instance of this struct to the block private_data pointer (see init), this
 * information becomes accessible within the hook functions.
 */
struct zyre_bridge_info
{
        /* add custom block local data here */
		// max number of msg to send per step
		int max_send;
		// max number of msg to send per step
		unsigned long max_msg_length;
//        // Msg buffer for input port
//		unsigned char* msg_buffer;

		//list of msg types that are accepted by the zyre bridge and handed to the RSG and length of list
		std::vector<std::string> input_type_list ;
		//list of msg types that coming from the RSG and will be broadcasted to local components
		std::vector<std::string> output_type_list ;

		// zyre node
		zyre_t *node;
		// zyre groups
		char *group;

        // actor running in separate thread to receive the messages
		zactor_t* actor;

        /* this is to have fast access to ports for reading and writing, without
         * needing a hash table lookup */
        struct zyre_bridge_port_cache ports;
};


char* send_request(const char *uid, const char *local_req, json_t *recipients, int timeout, const char* payload_type, json_t *msg_payload) {
	/**
	 * creates a query to the mediator to send a msg
	 *
	 * @param uid that is used to identify the answer to the query
	 * @param list of recipients as json array; msg is always broadcasted, but recipients on this list need to acknowledge the msg
	 * @param timeout after which mediator will stop resending msg
	 * @param payload_type as string that identifies payload
	 * @param payload as json object
	 *
	 * @return the string encoded JSON msg that can be sent directly via zyre. Must be freed by user! Returns NULL if wrong json types are passed in.
	 */

	if (!json_is_array(recipients)) {
		ERR("ERROR: Recipients are not a json array! \n");
		return NULL;
	}
	if (!json_is_object(msg_payload)) {
		ERR("ERROR: Payload is not a json object! \n");
		return NULL;
	}
    json_t *root;
    root = json_object();
    json_object_set_new(root, "metamodel", json_string("sherpa_mgs"));
    json_object_set_new(root, "model", json_string("http://kul/send_request.json"));
    json_object_set_new(root, "type", json_string("send_request"));
    json_t *payload;
    payload = json_object();
    json_object_set_new(payload, "UID", json_string(uid));
    json_object_set_new(payload, "local_requester", json_string(local_req));
    json_object_set_new(payload, "recipients", recipients);
    json_object_set_new(payload, "timeout", json_integer(timeout));
    json_object_set_new(payload, "payload_type", json_string(payload_type));
    json_object_set(payload, "payload", msg_payload);
    json_object_set_new(root, "payload", payload);
    char* ret = json_dumps(root, JSON_ENCODE_ANY);
    json_decref(root);
    return ret;
}

/* init */
int zyre_bridge_init(ubx_block_t *b)
{
        int ret = -1;
        struct zyre_bridge_info *inf;
        unsigned int tmplen;
	
        /* allocate memory for the block local state */
        if ((inf = (struct zyre_bridge_info*)calloc(1, sizeof(struct zyre_bridge_info)))==NULL) {
                ERR("zyre_bridge: failed to alloc memory");
                ret=EOUTOFMEM;
                return ret;
        }

        update_port_cache(b, &inf->ports);

        //If the following code is used, do not forget to also use the code freeing the memory in the cleanup. And also to put it into the bridge_info struct
//        inf->msg_buffer = (unsigned char*) malloc(inf->max_msg_length*sizeof(unsigned char*));
//        if (!inf->msg_buffer){
//        	printf("%s: Could not allocate memory for msg buffer. \n", b->name);
//        	goto out;
//        }

		int *max_msg_length;
        max_msg_length = (int*) ubx_config_get_data_ptr(b, "max_msg_length", &tmplen);
		printf("max_msg_length value for block %s is %d\n", b->name, *max_msg_length);
		inf->max_msg_length = *max_msg_length;
		if (inf->max_msg_length <=0){
			printf("ERR: %s: max_msg_length must be >0!\n",b->name);
			return ret;
		}

        int *max_send;
        max_send = (int*) ubx_config_get_data_ptr(b, "max_send", &tmplen);
        printf("max_send value for block %s is %d\n", b->name, *max_send);
        inf->max_send = *max_send;

//        ///TODO: need to get a list of type names in here
//        char *type_list;
//        type_list = (char*) ubx_config_get_data_ptr(b, "type_list", &tmplen);
//        printf("List of types for block %s is %s\n", b->name, type_list);
//        inf->type_list = type_list;
        ///TODO: for now hardcoded list -> fix, read from comma separated string
        inf->input_type_list.push_back("RSGUpdate_global");
        inf->input_type_list.push_back("RSGUpdate_local");
        inf->input_type_list.push_back("RSGUpdate_both");
        inf->input_type_list.push_back("RSGUpdate");
        inf->input_type_list.push_back("RSGQuery");
        inf->input_type_list.push_back("RSGFunctionBlock");
        inf->output_type_list.push_back("RSGUpdate"); //updates generated for updating other RSG agents, will be mapped to RSGUpdate_global
        inf->output_type_list.push_back("RSGUpdateResult");
        inf->output_type_list.push_back("RSGQueryResult");
        inf->output_type_list.push_back("RSGFunctionBlockResult");

        int major, minor, patch;
        zyre_version (&major, &minor, &patch);
        if (major != ZYRE_VERSION_MAJOR)
        	return ret;
        if (minor != ZYRE_VERSION_MINOR)
        	return ret;
        if (patch != ZYRE_VERSION_PATCH)
        	return ret;

        char *wm_name;
        wm_name = (char*) ubx_config_get_data_ptr(b, "wm_name", &tmplen);
        printf("zyre name for block %s is %s\n", b->name, wm_name);
        zyre_t *node;
        node = zyre_new (wm_name);
        if (!node){
        	printf("Could not create a zyre node!");
        	return ret;
        }
        inf->node = node;

        int rc;
        char *loc_ep;
        char *gos_ep;
        ///TODO: remove superfluous local endpoint
        loc_ep = (char*) ubx_config_get_data_ptr(b, "local_endpoint", &tmplen);
        gos_ep = (char*) ubx_config_get_data_ptr(b, "gossip_endpoint", &tmplen);
        //printf("local and gossip endpoint for block %s is %s and %s\n", b->name, loc_ep, gos_ep);
		rc = zyre_set_endpoint (node, "%s", loc_ep);
		assert (rc == 0);
        // check if zyre or gossip shall be used
        ubx_data_t *tmp;
        tmp = ubx_config_get_data(b, "gossip_flag");
        int gossip_flag;
        gossip_flag = *(int*) tmp->data;
        if (gossip_flag == 1){

        	//  Set up gossip network for this node
			ubx_data_t *dmy;
			dmy = ubx_config_get_data(b, "bind");
			int bind;
			bind = *(int*) dmy->data;
			if (bind == 1) {
				printf("%s: This block will bind to gossip endpoint '%s'\n", b->name,gos_ep);
				zyre_gossip_bind (node, "%s", gos_ep);
			} else if (bind == 0) {
				printf("%s: This block will connect to gossip endpoint '%s' \n", b->name,gos_ep);
				zyre_gossip_connect (node, "%s",gos_ep);
			} else {
				printf("%s: Wrong value for bind configuration. Must be 0 or 1. \n", b->name);
				return ret;
			}
        } else if (gossip_flag == 0) {
        	//nothing to do here. if no gossip port was set, zyre will use UDP beacons automatically
        } else {
        	printf("%s: Wrong value for gossip_flag configuration. Must be 0 or 1. \n", b->name);
        	return ret;
        }
		rc = zyre_start (node);
		assert (rc == 0);

		///TODO: enable list of group names
		char *group;
		group = (char*) ubx_config_get_data_ptr(b, "group", &tmplen);
		zyre_join (node, group);
		inf->group = group;

		//  Give time for them to interconnect
		zclock_sleep (100);

        b->private_data=inf;
        ret=0;
        return ret;
}

/* start */
int zyre_bridge_start(ubx_block_t *b)
{
        struct zyre_bridge_info *inf = (struct zyre_bridge_info*) b->private_data;
	
        // incoming data is handled by the actor thread
        zactor_t* actor = zactor_new (zyre_bridge_actor, b);
        assert(actor);
        inf->actor = actor;

        int ret = 0;
        return ret;
}

/* stop */
void zyre_bridge_stop(ubx_block_t *b)
{
         struct zyre_bridge_info *inf = (struct zyre_bridge_info*) b->private_data;
		// clean up actor thread
		zactor_destroy(&inf->actor);
		printf("Destroyed zactor!\n");
}

/* cleanup */
void zyre_bridge_cleanup(ubx_block_t *b)
{
		struct zyre_bridge_info *inf = (struct zyre_bridge_info*) b->private_data;

		//free(inf->msg_buffer);
		//zyre_t *node = inf->node;
		zyre_stop (inf->node);
		zclock_sleep (100);
		zyre_destroy (&inf->node);

		free(b->private_data);
		printf("Cleanup successful.\n");
}

/* step */
void zyre_bridge_step(ubx_block_t *b)
{
    struct zyre_bridge_info *inf = (struct zyre_bridge_info*) b->private_data;

	/* Read data from port */
	ubx_port_t* port = inf->ports.zyre_out;
	assert(port != 0);

	char * tmp_str = (char*) malloc(inf->max_msg_length*sizeof(char*));

	ubx_data_t msg;
	checktype(port->block->ni, port->in_type, "unsigned char", port->name, 1);
	msg.type = port->in_type;
	msg.len = inf->max_msg_length;
	msg.data = tmp_str;
	//msg.data = inf->msg_buffer;

    int counter = 0;
    while (counter < inf->max_send) {
    	int read_bytes = __port_read(port, &msg);

    	//printf("zyrebridge: read bytes: %d\n",read_bytes);
    	//printf("step: read strlen: %lu\n",strlen((char*) msg.data));

    	if (read_bytes <= 0) {
    		//printf("zyre_bridge: No data recieved from port\n");
    		free(tmp_str);
    		return;
    	}
//    	printf("zyrebridge: read bytes: %d\n",read_bytes);
    	// port_read returns byte array. Need to add 0 termination manually to the string.
    	tmp_str[read_bytes] = '\0';

    	// create json object and...
    	json_t *pl;
		json_error_t error;
		pl= json_loads(tmp_str,0,&error);
		if(!pl) {
			ERR("Error parsing JSON payload! line %d: %s\n", error.line, error.text);
			json_decref(pl);
			free(tmp_str);
			return;
		}
//    	printf("[zyrebridge] retrieving msg: %s\n", json_dumps(pl, JSON_ENCODE_ANY));
		// ...check for its type and embed it into msg envelope
		json_t *new_msg;
		new_msg = json_object();
		json_object_set(new_msg, "payload", pl);
		json_object_set_new(new_msg, "metamodel", json_string("SHERPA"));
		if(json_object_get(pl, "@worldmodeltype") == 0) {
			DBG("[zyrebridge] retrieving msg: %s\n", json_dumps(pl, JSON_ENCODE_ANY));
			ERR("[zyrebridge] Error parsing RSG payload! @worldmodeltype is missing.\n");
			json_decref(pl);
			free(tmp_str);
			return;
		}

		std::string tmp_type = json_string_value(json_object_get(pl, "@worldmodeltype")); //can segfault
		char *send_msg = NULL;
		int found = 0;
		for (int i=0; i < inf->output_type_list.size();i++)
		{
			if (tmp_type.compare(inf->output_type_list[i])) {
				found = 1;
				// need to handle exception for updates generated from RSG due to local updates
				if (tmp_type.compare("RSGUpdate") == 0) {
					json_object_set(new_msg, "model", json_string("RSGUpdate"));
					json_object_set(new_msg, "type", json_string("RSGUpdate_global"));

					//  If used with mediator, add send_request envelope
					ubx_data_t *dmy;
					dmy = ubx_config_get_data(b, "mediator");
					int mediator;
					mediator = *(int*) dmy->data;
					if (mediator == 1) {
						zuuid_t *query_uuid = zuuid_new ();
						assert (query_uuid);
						json_t *recip = json_array();
						assert((recip)&&(json_array_size(recip)==0));
						send_msg = send_request(zuuid_str(query_uuid),zyre_uuid(inf->node),recip,1000,"send_remote",new_msg);
					}
					else {
						send_msg = json_dumps(new_msg, JSON_ENCODE_ANY);
					}
				} else {
					json_object_set_new(new_msg, "model", json_string(tmp_type.c_str()));
					json_object_set_new(new_msg, "type", json_string(tmp_type.c_str()));
					send_msg = json_dumps(new_msg, JSON_ENCODE_ANY);
				}
				break;
			}
		}
		if (found == 0) {
			DBG("[zyre_bridge] WARNING: Unknown output type: %s!\n",tmp_type.c_str());
		}

		DBG("[zyrebridge] sending msg: %s\n", send_msg);
    	zyre_shouts(inf->node, inf->group, "%s", send_msg);
    	counter++;

    	json_decref(pl);
    	json_decref(new_msg);
    	free(send_msg);

    }

    free(tmp_str);
    return;
}


static void
zyre_bridge_actor (zsock_t *pipe, void *args)
{

    // initialization
    ubx_block_t *b = (ubx_block_t *) args;
    struct zyre_bridge_info *inf = (struct zyre_bridge_info*) b->private_data;
    printf("[zyre_bridge]: actor started.\n");
    // send signal on pipe socket to acknowledge initialization
    zsock_signal (pipe, 0);

    bool terminated = false;
	zpoller_t *poller = zpoller_new (pipe, zyre_socket (inf->node), NULL);
	while (!terminated) {
		void *which = zpoller_wait (poller, -1);
		// handle msgs from main thread
		if (which == pipe) {
			zmsg_t *msg = zmsg_recv (which);
			if (!msg)
				break;              //  Interrupted
			// only react to TERM signal
			char *command = zmsg_popstr (msg);
			if (streq (command, "$TERM"))
				terminated = true;
			else {
				puts ("Invalid pipe message to actor");
			}
			free (command);
			zmsg_destroy (&msg);
		} else
		// handle msgs from zyre network
		if (which == zyre_socket (inf->node)) {
			zmsg_t *msg = zmsg_recv (which);
			if (!msg) {
				printf("[zyre_bridge]: interrupted!\n");
			}
			char *event = zmsg_popstr (msg);
			char *peer = zmsg_popstr (msg);
			char *name = zmsg_popstr (msg);

			if (streq (event, "ENTER"))
				printf ("[zyre_bridge]: %s has entered\n", name);
			else
			if (streq (event, "EXIT"))
				printf ("[zyre_bridge]: %s has exited\n", name);
			else
			if (streq (event, "SHOUT")) {
				DBG ("[zyre_bridge]: SHOUT received from %s.\n", name);

				char *group = zmsg_popstr (msg);
				char *message = zmsg_popstr (msg);

				// load JSON msg
		    	json_t *m;
				json_error_t error;
				m= json_loads(message,0,&error);
				if(!m) {
					ERR("Error parsing JSON payload! line %d: %s\n", error.line, error.text);
					json_decref(m);
				} else {
					DBG("%s\n",message);
					if (json_object_get(m, "type")) {
						std::string type = json_dumps(json_object_get(m, "type"), JSON_ENCODE_ANY);
						type = type.substr(1, type.size()-2); // get rid of " characters
						DBG("type: %s\n",json_dumps(json_object_get(m, "type"), JSON_ENCODE_ANY));

						for (int i=0; i < inf->input_type_list.size();i++)
						{
							//if (json_string_value(json_object_get(m, "type")) == inf->input_type_list[i]){
							//printf("type list, type : %s, %s \n", inf->input_type_list[i].c_str(), type.c_str());
							if (inf->input_type_list[i].compare(type) == 0) {
								ubx_type_t* type =  ubx_type_get(b->ni, "unsigned char");
								ubx_data_t ubx_msg;
								char * payload =  json_dumps(json_object_get(m, "payload"), JSON_ENCODE_ANY);
								ubx_msg.data = (void *)payload;
								DBG("message: %s\n", payload);
								ubx_msg.len = strlen(payload);
								ubx_msg.type = type;
								__port_write(inf->ports.zyre_in, &ubx_msg);
								free(payload);
							}
						}
					} else {
						ERR("Error parsing JSON string! Does not conform to msg model.\n");
					}
					json_decref(m);
				}
				free (group);
				free (message);
			}
			else
			if (streq (event, "WHISPER")){
				char *message = zmsg_popstr (msg);
				printf ("%s: WHISPER \n%s\n", name, message);
				free (message);
			}
			else
			if (streq (event, "EVASIVE"))
				printf ("[zyre_bridge]: %s is being evasive\n", name);

			free (event);
			free (peer);
			free (name);
			zmsg_destroy (&msg);
		}
	}
	zpoller_destroy (&poller);
	//TODO: make parametrizable
	zclock_sleep (100);
}

