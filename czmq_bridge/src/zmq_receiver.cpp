#include "zmq_receiver.hpp"

/* Generic includes */
#include <iostream>
#include <pthread.h>

/* ZMQ includes */
#include <czmq.h>


UBX_MODULE_LICENSE_SPDX(BSD-3-Clause)


void* receiverThread(void *arg);

/* define a structure for holding the block local state. By assigning an
 * instance of this struct to the block private_data pointer (see init), this
 * information becomes accessible within the hook functions.
 */
struct zmq_receiver_info
{
        /* add custom block local data here */
	// ZMQ subscriber
	zsock_t* subscriber;

	// Thread that does the work
	pthread_t workerThread;

        /* this is to have fast access to ports for reading and writing, without
         * needing a hash table lookup */
        struct zmq_receiver_port_cache ports;
};

/* init */
int zmq_receiver_init(ubx_block_t *b)
{
        int ret = -1;
        struct zmq_receiver_info *inf;
        unsigned int tmplen;
        char *connection_spec_str;
        std::string connection_spec;
	zsock_t* sub;
        /* allocate memory for the block local state */
        if ((inf = (struct zmq_receiver_info*)calloc(1, sizeof(struct zmq_receiver_info)))==NULL) {
                ERR("zmq_receiver: failed to alloc memory");
                ret=EOUTOFMEM;
                goto out;
        }
        b->private_data=inf;
        update_port_cache(b, &inf->ports);

        //try {

        	//inf->context = new zmq::context_t(1);
        	//inf->subscriber = new zmq::socket_t(*inf->context, ZMQ_SUB);

        	connection_spec_str = (char*) ubx_config_get_data_ptr(b, "connection_spec", &tmplen);
			connection_spec = std::string(connection_spec_str);
			std::cout << "ZMQ connection configuration for block " << b->name << " is " << connection_spec << std::endl;

        	//inf->subscriber->connect(connection_spec_str);
        	//inf->subscriber->setsockopt(ZMQ_SUBSCRIBE, "1", 0); // message filter options

		sub = zsock_new_sub(connection_spec_str, "1");
		if (!sub)
			goto out;
		inf->subscriber = sub;
	//	} catch (std::exception e) {
	//		std::cout << e.what() << " : " << zmq_strerror (errno) << std::endl;

	//		goto out;
	//	}


        ret=0;
out:
        return ret;
}

/* start */
int zmq_receiver_start(ubx_block_t *b)
{
        struct zmq_receiver_info *inf = (struct zmq_receiver_info*) b->private_data;

	/* The worker thread handles all incoming data */
	pthread_create(&inf->workerThread, NULL, receiverThread, b);

        int ret = 0;
        return ret;
}

/* stop */
void zmq_receiver_stop(ubx_block_t *b)
{
        /* struct zmq_receiver_info *inf = (struct zmq_receiver_info*) b->private_data; */
}

/* cleanup */
void zmq_receiver_cleanup(ubx_block_t *b)
{
		struct zmq_receiver_info *inf = (struct zmq_receiver_info*) b->private_data;
		//delete inf->subscriber;
		zsock_destroy(&inf->subscriber);
        free(b->private_data);
}

/* step */
void zmq_receiver_step(ubx_block_t *b)
{

        //struct zmq_receiver_info *inf = (struct zmq_receiver_info*) b->private_data;

}

void* receiverThread(void *arg) {
    ubx_block_t *b = (ubx_block_t *) arg;
    struct zmq_receiver_info *inf = (struct zmq_receiver_info*) b->private_data;
    std::cout << " receiverThread started." << std::endl;

    /* Receiver loop */
    while(true) {
    	//inf->subscriber->recv(&update);
    	zmsg_t *update = zsock_recv (inf->subscriber);
	assert (update);
	std::cout << "zmq_receiver: Received " << zmsg_size(update) << " frames and " << zmsg_content_size(update) << " bytes from ?" << std::endl;
    	if (zmsg_size(update) < 1) {
    		std::cout << "did not recv()" << std::endl;
    		break;
    	}


    	// move to step function?
	char *body = zmsg_popstr (update);
	std::string data;
	while(body != NULL) {
	  	data += body;
		body = zmsg_popstr (update);
	}
        ubx_type_t* type =  ubx_type_get(b->ni, "unsigned char");
	ubx_data_t msg;
	msg.data = (void *)data.c_str();
	msg.len = data.size();
	msg.type = type;

	//hexdump((unsigned char *)msg.data, msg.len, 16);
	__port_write(inf->ports.zmq_in, &msg);
		
	/* Inform potential observers ? */

	zmsg_destroy (&update);
    }

    /* Clean up */
    return 0;

}
