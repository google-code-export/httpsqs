/*
HTTP Simple Queue Service - httpsqs v1.2.20100311
Author: Zhang Yan (http://blog.s135.com), E-mail: net@s135.com
This is free software, and you are welcome to modify and redistribute it under the New BSD License
*/

#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <stdbool.h>

/* Enable threading please */
#include <pthread.h>

/* Include the tokyo tyrant library */
#include <tcbdb.h>

#include <err.h>
#include <event.h>
#include <evhttp.h>

#define VERSION "1.2"

/* Global vars */
TCBDB *httpsqs_db_tcbdb; /* Data store */
struct event resync_event;
struct timeval resync_timeout = {3,0};
int tcbdb_memory_cache_size = 104857600;
char* thread_stuff;
char* httpsqs_settings_datapath = NULL;
char* default_db = "default";
pthread_t db_creation_thread;
pthread_mutex_t db_create_lock ;

typedef struct stats {
int write_counts;
int read_counts;
} stats;

typedef struct tokyo_services {
TCBDB *db_tcbdb;
char queue_name[256];
int number;
pthread_mutex_t read_lock;
} tokyo_services;


struct stats mystats = {0,0};

struct tokyo_services db_service[10];

int db_pool_size = 1;
int service_num = -1;

/* Create multi-directory structure */
void create_multilayer_dir( char *muldir ) 
{
	int		i,len;
	char	str[512];
    
    strncpy( str, muldir, 512 );
    len=strlen(str);
    for( i=0; i<len; i++ )
    {
        if( str[i]=='/' )
        {
            str[i] = '\0';
            // If the directory does not exits, create
            if( access(str, F_OK)!=0 )
            {
                mkdir( str, 0777 );
            }
            str[i]='/';
        }
    }
    if( len>0 && access(str, F_OK)!=0 )
    {
        mkdir( str, 0777 );
    }

    return;
}

char *urldecode(char *input_str) 
{
		int len = strlen(input_str);
		char *str = strdup(input_str);
		
        char *dest = str; 
        char *data = str; 

        int value; 
        int c; 

        while (len--) { 
                if (*data == '+') { 
                        *dest = ' '; 
                } 
                else if (*data == '%' && len >= 2 && isxdigit((int) *(data + 1)) && isxdigit((int) *(data + 2))) 
                { 

                        c = ((unsigned char *)(data+1))[0]; 
                        if (isupper(c)) 
                                c = tolower(c); 
                        value = (c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10) * 16; 
                        c = ((unsigned char *)(data+1))[1]; 
                        if (isupper(c)) 
                                c = tolower(c); 
                                value += c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10; 

                        *dest = (char)value ; 
                        data += 2; 
                        len -= 2; 
                } else { 
                        *dest = *data; 
                } 
                data++; 
                dest++; 
        } 
        *dest = '\0'; 
        return str; 
}

static void show_help(void)
{
	char *b = "--------------------------------------------------------------------------------------------------\n"
		  "HTTP Simple Queue Service - httpsqs v" VERSION "\n\n"
		  "Author: Zhang Yan (http://blog.s135.com), E-mail: net@s135.com\n"
		  "This is free software, and you are welcome to modify and redistribute it under the New BSD License\n"
		  "\n"
		   "--listen -l <ip_addr>  interface to listen on, default is 0.0.0.0\n"
		   "--port -p <num>      TCP port number to listen on (default: 1218)\n"
		   "--datadir -x <path>     database directory (example: /opt/httpsqs/data)\n"
		   "--flush-interval -f <second>     Timeout for flushing data to disk when idle.\n"
		   "--memory -m <size>     database memory cache size in MB (default: 100)\n"
		   "--timeout -t <second>   timeout for an http request (default: 1)\n"		   
		   "--daemon -d            run as a daemon\n"
		   "--help -h            print this help and exit\n\n"
		   "Use command \"killall httpsqs\", \"pkill httpsqs\" and \"kill PID of httpsqs\" to stop httpsqs.\n"
		   "Please note that don't use the command \"pkill -9 httpsqs\" and \"kill -9 PID of httpsqs\"!\n"
		   "\n"
		   "Please visit \"http://code.google.com/p/httpsqs\" for more help information.\n\n"
		   "--------------------------------------------------------------------------------------------------\n"
		   "\n";
	fprintf(stderr, b, strlen(b));
}

static int httpsqs_sync_db()
{
	return tcbdbsync(httpsqs_db_tcbdb);
}

static void show_dbs() {
int i;
fprintf(stdout, "Service_num: %d\n", service_num);
for(i=0;i<=service_num;i++){
fprintf(stdout, "Got service number %d: %s (%d)\n",i,(char *)db_service[i].queue_name,strlen(db_service[i].queue_name));
}	
}

static void *start_db(void *arg){
	pthread_mutex_lock(&db_create_lock); fprintf(stdout, "Enter db create thread DB_CREATE MUTEX SET!\n");
	
	/* Database path */
	int httpsqs_settings_dataname_len = 1024;
	char *httpsqs_settings_dataname = (char *)malloc(httpsqs_settings_dataname_len);
	memset(httpsqs_settings_dataname, '\0', httpsqs_settings_dataname_len);
	
	int make_service; /* Holds key temporarily */
	
	/* Set database number before setting global datastore count */
	make_service = service_num+1;
	
	sprintf(httpsqs_settings_dataname, "%s/httpsqs_%d.db", httpsqs_settings_datapath, make_service); /* Database name is httpsqs_#.db */
	fprintf(stdout, "creating: %s\n", httpsqs_settings_dataname);
	
	fprintf(stdout, "Making new datastore number %d\n", make_service);
	
	/* Creating new read_lock mutex */
	pthread_mutex_init(&db_service[make_service].read_lock, NULL);
	/* Locking database while creating it */
	pthread_mutex_lock(&db_service[make_service].read_lock);
	
	/* Init Tokyo Cabinet into proper pointer? */
	db_service[make_service].db_tcbdb = tcbdbnew();
	
	/* Tuning parameters */
	/* 	1024 members in each leaf page */
	/* 	2024 members in each non-leaf page */
	/* 	50 mil elements in the bucket array (1 to 4 times number of pages to be stored) */
	/* 	8 record alignment by power of two, 2^8 = 256 */
	/* 	10 elements in the free block pool, by power of two, 2^10 = 1024 */
	/* 	opts, BDBTLARGE allows for 2G+ files by using 64bit bucket array */
	tcbdbtune(db_service[make_service].db_tcbdb, 1024, 2048, 50000000, 8, 10, BDBTLARGE);
	tcbdbsetxmsiz(db_service[make_service].db_tcbdb, tcbdb_memory_cache_size); /* Set memory cache size */
	
	/* Try opening the table */
	fprintf(stdout, "Opening DB... %s\n", httpsqs_settings_dataname);
	
	if(!tcbdbopen(db_service[make_service].db_tcbdb, httpsqs_settings_dataname, BDBOWRITER|BDBOCREAT)){
		fprintf(stderr, "Unable to open the database %s.\n\n", httpsqs_settings_dataname);		
		exit(1);
	}
	fprintf(stdout, "Done...\n\n");
	fprintf(stdout, "finished setting db service number %d\n\n", make_service);
	
	//sleep(10);
	
	/* Incrementing number of datastores to accomodate newly created one */
	service_num++;
	
	/* Unlock read mutex on current datastore */
	pthread_mutex_unlock(&db_service[make_service].read_lock);
	
	/* Free vars from memory */
	free(httpsqs_settings_dataname);
	
	/* Unlock table creation mutex */
	pthread_mutex_unlock(&db_create_lock);
	return NULL;
}

static TCBDB *get_db(const char* httpsqs_input_name) {
	int i=0;
	
	/* Iterate over available services searching for ours */
	fprintf(stdout, "Enter DB get thread trying to get %s\n", httpsqs_input_name);
	for(i=0;i<service_num;i++) {
		if(strcmp(db_service[i].queue_name, httpsqs_input_name) == 0) {
		fprintf(stdout, "got %s\n", db_service[i].queue_name);
			fprintf(stdout, "Getting %s queue number %d\n", db_service[i].queue_name, i);
			pthread_mutex_lock(&db_service[i].read_lock);
			pthread_mutex_unlock(&db_service[i].read_lock);
			return db_service[i].db_tcbdb;
		}
	}
	
	/* Trying to find an empty service to use */
	for(i=0;i<=service_num;i++) {
		fprintf(stdout, "Try empty service %d\n",i);
		if (strlen(db_service[i].queue_name) == 0) {
			/* Trying to acquire a read_lock on it, release it immediately. */
			pthread_mutex_lock(&db_service[i].read_lock);
			pthread_mutex_unlock(&db_service[i].read_lock);
			
			/* Name it like we want */
			strcpy(db_service[i].queue_name, httpsqs_input_name);
			
			/* Acquire a db create mutex and release it immediately */
			fprintf(stdout, "trying DB_CREATE MUTEX!\n");
			pthread_mutex_lock(&db_create_lock);
			fprintf(stdout, "GOT DB_CREATE MUTEX, starting creation thread!\n");
			pthread_create(&db_creation_thread, NULL, start_db, NULL);
		
			pthread_mutex_unlock(&db_create_lock);
			return db_service[i].db_tcbdb;
		}
	}
	
	return NULL;
}

/* Get writer cursor position */
static int httpsqs_read_putpos(const char* httpsqs_input_name)
{
	int queue_value = 0;
	char *queue_value_tmp;
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	memset(queue_name, '\0', 300);
	
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "putpos");
	
	
	
	queue_value_tmp = tcbdbget2(httpsqs_db_tcbdb, queue_name);
	if(queue_value_tmp){
		queue_value = atoi(queue_value_tmp);
		free(queue_value_tmp);
	}
	
	return queue_value;
}

/* Get reader cursor position */
static int httpsqs_read_getpos(const char* httpsqs_input_name)
{
	int queue_value = 0;
	char *queue_value_tmp;
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	memset(queue_name, '\0', 300);
	
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "getpos");
	
	queue_value_tmp = tcbdbget2(httpsqs_db_tcbdb, queue_name);
	if(queue_value_tmp){
		queue_value = atoi(queue_value_tmp);
		free(queue_value_tmp);
	}
	
	return queue_value;
}

/* 读取用于设置的最大队列数 */
static int httpsqs_read_maxqueue(const char* httpsqs_input_name)
{
	int queue_value = 0;
	char *queue_value_tmp;
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	memset(queue_name, '\0', 300);
	
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "maxqueue");
	
	queue_value_tmp = tcbdbget2(httpsqs_db_tcbdb, queue_name);
	if(queue_value_tmp){
		queue_value = atoi(queue_value_tmp);
		free(queue_value_tmp);
	} else {
		queue_value = 1000000; /* 默认队列长度为100万条 */
	}
	
	return queue_value;
}

/* 
Set the maximum queue length. 
*/
static int httpsqs_maxqueue(const char* httpsqs_input_name, int httpsqs_input_num)
{
	int queue_put_value = 0;
	int queue_get_value = 0;
	int queue_maxnum_int = 0;
	
	/* Read current write position */
	queue_put_value = httpsqs_read_putpos(httpsqs_input_name);
	
	/* Read current read position */
	queue_get_value = httpsqs_read_getpos(httpsqs_input_name);

	/* Set maximum number of queued messages */
	queue_maxnum_int = httpsqs_input_num;
	
	/* Set the max queue size if the queue is unwrapped and the maximum queue size is greater that the current write position, else fail. */
	if (queue_maxnum_int >= queue_put_value && queue_maxnum_int >= queue_get_value && queue_put_value >= queue_get_value) {
		char queue_name[300]; /* Total key name, first 256 is user queue name */
		char *queue_maxnum = (char *)malloc(16);
		memset(queue_name, '\0', 300);
		memset(queue_maxnum, '\0', 16);
		sprintf(queue_name, "%s:%s", httpsqs_input_name, "maxqueue");
		sprintf(queue_maxnum, "%d", queue_maxnum_int);
		tcbdbput2(httpsqs_db_tcbdb, queue_name, queue_maxnum);
		
		free(queue_maxnum);
		return queue_maxnum_int;
	}
	
	return 0;
}

/* Reset the queue, return 0 on success. */
static int httpsqs_reset(const char* httpsqs_input_name)
{
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	
	memset(queue_name, '\0', 300);
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "putpos");
	tcbdbout2(httpsqs_db_tcbdb, queue_name);

	memset(queue_name, '\0', 300);
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "getpos");
	tcbdbout2(httpsqs_db_tcbdb, queue_name);
	
	memset(queue_name, '\0', 300);
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "maxqueue");
	tcbdbout2(httpsqs_db_tcbdb, queue_name);
	
	return 0;
}

/* View content of a single queue message */
char *httpsqs_view(const char* httpsqs_input_name, int pos)
{
	char *queue_value;
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	memset(queue_name, '\0', 300);
	
	sprintf(queue_name, "%s:%d", httpsqs_input_name, pos);
	
	queue_value = tcbdbget2(httpsqs_db_tcbdb, queue_name);
	
	return queue_value;
}

/* Write to the current put position */
static int httpsqs_now_putpos(const char* httpsqs_input_name)
{
	int maxqueue_num = 0;
	int queue_put_value = 0;
	int queue_get_value = 0;
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	char *queue_input = (char *)malloc(32);
	
	/* Read the maximum queue length */
	maxqueue_num = httpsqs_read_maxqueue(httpsqs_input_name);
	
	/* Read the current put position */
	queue_put_value = httpsqs_read_putpos(httpsqs_input_name);
	
	/* Read the current read position */
	queue_get_value = httpsqs_read_getpos(httpsqs_input_name);	
	
	memset(queue_name, '\0', 300);
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "putpos");	
	
	/* Increment the put position */
	queue_put_value = queue_put_value + 1;
	if (queue_put_value == queue_get_value) { /* Write ID + 1 equals Read ID, this means queue is FULL! Refuse to write, return 0 */
		queue_put_value = 0;
	}
	else if (queue_put_value > maxqueue_num) { /* If queue is written at a position bigger than maxqueue_num, go to position 1, introduces major bug! */
		if(tcbdbput2(httpsqs_db_tcbdb, queue_name, "1")) {
			queue_put_value = 1;
		}
	} else { /* Write to queue location + 1 into the database */
		memset(queue_input, '\0', 32);
		sprintf(queue_input, "%d", queue_put_value);
		tcbdbput2(httpsqs_db_tcbdb, queue_name, queue_input);
		free(queue_input);
	}
	
	return queue_put_value;
}

/* Get the queue read point, if the return value is 0 then the queue is empty */
static int httpsqs_now_getpos(const char* httpsqs_input_name)
{
	int maxqueue_num = 0;
	int queue_put_value = 0;
	int queue_get_value = 0;
	char queue_name[300]; /* Total key name, first 256 is user queue name */

	/* Read the maximum queue length */
	maxqueue_num = httpsqs_read_maxqueue(httpsqs_input_name);
	
	/* Read the current put position */
	queue_put_value = httpsqs_read_putpos(httpsqs_input_name);
	
	/* Read the current read position */
	queue_get_value = httpsqs_read_getpos(httpsqs_input_name);
	
	memset(queue_name, '\0', 300);
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "getpos");
	/* If the get value doesn't exist set it to 1 */
	if (queue_get_value == 0 && queue_put_value > 0) {
		queue_get_value = 1;
		tcbdbput2(httpsqs_db_tcbdb, queue_name, "1");
	/* If the queue in unwrapped */
	} else if (queue_get_value < queue_put_value) {
		queue_get_value = queue_get_value + 1;
		char *queue_input = (char *)malloc(32);		
		memset(queue_input, '\0', 32);
		sprintf(queue_input, "%d", queue_get_value);
		tcbdbput2(httpsqs_db_tcbdb, queue_name, queue_input);
		free(queue_input);
	/* If the queue is wrapped */
	} else if (queue_get_value > queue_put_value && queue_get_value < maxqueue_num) {
		queue_get_value = queue_get_value + 1;
		char *queue_input = (char *)malloc(32);		
		memset(queue_input, '\0', 32);
		sprintf(queue_input, "%d", queue_get_value);
		tcbdbput2(httpsqs_db_tcbdb, queue_name, queue_input);
		free(queue_input);
	/* If we are reading the end of the queue, reset the read position */
	} else if (queue_get_value > queue_put_value && queue_get_value == maxqueue_num) {
		queue_get_value = 1;
		tcbdbput2(httpsqs_db_tcbdb, queue_name, "1");
	/* We have an unknown situation */
	} else {
		queue_get_value = 0;
	}
	
	return queue_get_value;
}

/* HTTP Request processor */
void httpsqs_handler(struct evhttp_request *req, void *arg)
{
        struct evbuffer *buf;
        buf = evbuffer_new();
		
		/* Parese URI */
		char *decode_uri = strdup((char*) evhttp_request_uri(req));
		struct evkeyvalq httpsqs_http_query;
		evhttp_parse_query(decode_uri, &httpsqs_http_query);
		free(decode_uri);
		
		/* Parse GET parameters */
		const char *httpsqs_input_name = evhttp_find_header (&httpsqs_http_query, "name"); /* Queue name, max 256 chars */
		const char *httpsqs_input_charset = evhttp_find_header (&httpsqs_http_query, "charset"); /* Charset */
		const char *httpsqs_input_opt = evhttp_find_header (&httpsqs_http_query, "opt"); /* Operation */
		const char *httpsqs_input_data = evhttp_find_header (&httpsqs_http_query, "data"); /* Data */
		const char *httpsqs_input_pos_tmp = evhttp_find_header (&httpsqs_http_query, "pos"); /* Location (get position) */
		const char *httpsqs_input_num_tmp = evhttp_find_header (&httpsqs_http_query, "num"); /* Length (set maxlen) */
		
		
		
		int httpsqs_input_pos = 0;
		int httpsqs_input_num = 0;
		if (httpsqs_input_pos_tmp != NULL) {
			httpsqs_input_pos = atoi(httpsqs_input_pos_tmp); /* Queue numeric position */
		}
		if (httpsqs_input_num_tmp != NULL) {
			httpsqs_input_num = atoi(httpsqs_input_num_tmp); /* Queue numeric size */
		}		
		
		/* Return user's header */
		if (httpsqs_input_charset != NULL && strlen(httpsqs_input_charset) <= 40) {
			char *content_type = (char *)malloc(64);
			memset(content_type, '\0', 64);
			sprintf(content_type, "text/plain; charset=%s", httpsqs_input_charset); /* Preserve user charset */
			evhttp_add_header(req->output_headers, "Content-Type", content_type);
			free(content_type);
		} else {
			evhttp_add_header(req->output_headers, "Content-Type", "text/plain"); /* Set default charset */
		}
		evhttp_add_header(req->output_headers, "Keep-Alive", "120");
		//evhttp_add_header(req->output_headers, "Cneonction", "close");
		
		/* Service logic */
		if (httpsqs_input_name != NULL && httpsqs_input_opt != NULL && strlen(httpsqs_input_name) <= 256) {
			/* Input */
			if (strcmp(httpsqs_input_opt, "put") == 0) {
				/* Recieve POST message */
				int buffer_data_len;
				buffer_data_len = EVBUFFER_LENGTH(req->input_buffer);
				if (buffer_data_len > 0) {
					int queue_put_value = httpsqs_now_putpos((char *)httpsqs_input_name);
					if (queue_put_value > 0) {
						char queue_name[300]; /* Total key name, first 256 is user queue name */
						memset(queue_name, '\0', 300);
						sprintf(queue_name, "%s:%d", httpsqs_input_name, queue_put_value);
						char *httpsqs_input_postbuffer;					
						char *buffer_data = (char *)malloc(buffer_data_len + 1);
						memset(buffer_data, '\0', buffer_data_len + 1);
						memcpy (buffer_data, EVBUFFER_DATA(req->input_buffer), buffer_data_len);
						httpsqs_input_postbuffer = urldecode(buffer_data);
						tcbdbput2(httpsqs_db_tcbdb, queue_name, httpsqs_input_postbuffer);
						memset(queue_name, '\0', 300);
						sprintf(queue_name, "%d", queue_put_value);
						evhttp_add_header(req->output_headers, "Pos", queue_name);
						evbuffer_add_printf(buf, "%s", "HTTPSQS_PUT_OK");
						if(resync_timeout.tv_sec != 0) {
							event_add(&resync_event, &resync_timeout);
						}
						free(httpsqs_input_postbuffer);
						free(buffer_data);
					} else {
						evbuffer_add_printf(buf, "%s", "HTTPSQS_PUT_END"); /* End of queue, how can this happen?! */
					}
				/* If there is no POST body, check the GET parameter data */
				} else if (httpsqs_input_data != NULL) {
					int queue_put_value = httpsqs_now_putpos((char *)httpsqs_input_name);
					if (queue_put_value > 0) {
						char queue_name[300]; /* Total key name, first 256 is user queue name */
						memset(queue_name, '\0', 300);
						sprintf(queue_name, "%s:%d", httpsqs_input_name, queue_put_value);				
						buffer_data_len = strlen(httpsqs_input_data);
						char *httpsqs_input_postbuffer;
						char *buffer_data = (char *)malloc(buffer_data_len + 1);
						memset(buffer_data, '\0', buffer_data_len + 1);
						memcpy (buffer_data, httpsqs_input_data, buffer_data_len);
						httpsqs_input_postbuffer = urldecode(buffer_data);
						tcbdbput2(httpsqs_db_tcbdb, queue_name, buffer_data);
						memset(queue_name, '\0', 300);
						sprintf(queue_name, "%d", queue_put_value);					
						evhttp_add_header(req->output_headers, "Pos", queue_name);
						evbuffer_add_printf(buf, "%s", "HTTPSQS_PUT_OK");
						if(resync_timeout.tv_sec != 0) {
							event_add(&resync_event, &resync_timeout);
						}
						free(httpsqs_input_postbuffer);
						free(buffer_data);
					} else {
						evbuffer_add_printf(buf, "%s", "HTTPSQS_PUT_END"); /* End of queue, how can this happen?! */
					}
				} else {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_PUT_ERROR");
				}
			}
			/* Read from queue */
			else if (strcmp(httpsqs_input_opt, "get") == 0) {
				int queue_get_value = 0;
				queue_get_value = httpsqs_now_getpos((char *)httpsqs_input_name);
				if (queue_get_value == 0) {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_GET_END");
				} else {
					char queue_name[300]; /* Total key name, first 256 is user queue name */
					memset(queue_name, '\0', 300);				
					sprintf(queue_name, "%s:%d", httpsqs_input_name, queue_get_value);
					char *httpsqs_output_value;
					httpsqs_output_value = tcbdbget2(httpsqs_db_tcbdb, queue_name);
					if (httpsqs_output_value) {
						memset(queue_name, '\0', 300);
						sprintf(queue_name, "%d", queue_get_value);	
						evhttp_add_header(req->output_headers, "Pos", queue_name);
						evbuffer_add_printf(buf, "%s", httpsqs_output_value);
						free(httpsqs_output_value);
					} else {
						evbuffer_add_printf(buf, "%s", "HTTPSQS_GET_END");
					}
				}
			}
			/* View queue status */
			else if (strcmp(httpsqs_input_opt, "status") == 0) {
				int maxqueue = httpsqs_read_maxqueue((char *)httpsqs_input_name); /* Get max queue length */
				int putpos = httpsqs_read_putpos((char *)httpsqs_input_name); /* Get queue write position */
				int getpos = httpsqs_read_getpos((char *)httpsqs_input_name); /* Get queue read position */
				int ungetnum;
				const char *put_times;
				const char *get_times;
				if (putpos >= getpos) {
					ungetnum = abs(putpos - getpos); /* Linear queue */
					put_times = "1st lap";
					get_times = "1st lap";
				} else if (putpos < getpos) {
					ungetnum = abs(maxqueue - getpos + putpos); /* Wrapped queue */
					put_times = "2nd lap";
					get_times = "1st lap";
				}
				evbuffer_add_printf(buf, "HTTP Simple Queue Service v%s\n", VERSION);
				evbuffer_add_printf(buf, "------------------------------\n");
				evbuffer_add_printf(buf, "Queue Name: %s\n", httpsqs_input_name);
				evbuffer_add_printf(buf, "Maximum number of queues: %d\n", maxqueue);
				evbuffer_add_printf(buf, "Put position of queue (%s): %d\n", put_times, putpos);
				evbuffer_add_printf(buf, "Get position of queue (%s): %d\n", get_times, getpos);
				evbuffer_add_printf(buf, "Number of unread queue: %d\n", ungetnum);
			}
			/* Get item from queue by pos */
			//else if (strcmp(httpsqs_input_opt, "view") == 0 && httpsqs_input_pos >= 1 && httpsqs_input_pos <= 1000000000) {
			else if (strcmp(httpsqs_input_opt, "view") == 0 && httpsqs_input_pos >= 1 && httpsqs_input_pos <= httpsqs_read_maxqueue((char *)httpsqs_input_name)) {
				char *httpsqs_output_value;
				httpsqs_output_value = httpsqs_view ((char *)httpsqs_input_name, httpsqs_input_pos);
				if (httpsqs_output_value) {
					evbuffer_add_printf(buf, "%s", httpsqs_output_value);
					free(httpsqs_output_value);
				}
			}
			/* Reset queue */
			else if (strcmp(httpsqs_input_opt, "reset") == 0) {
				int reset = httpsqs_reset((char *)httpsqs_input_name);
				if (reset == 0) {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_RESET_OK");
				} else {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_RESET_ERROR");
				}
			}
			/* Set the queue max length, top at 1 billion */
			else if (strcmp(httpsqs_input_opt, "maxqueue") == 0 && httpsqs_input_num >= 10 && httpsqs_input_num <= 1000000000) {
				if (httpsqs_maxqueue((char *)httpsqs_input_name, httpsqs_input_num) != 0) {
					/* Success */
					evbuffer_add_printf(buf, "%s", "HTTPSQS_MAXQUEUE_OK");
				} else {
					/* Fail */
					evbuffer_add_printf(buf, "%s", "HTTPSQS_MAXQUEUE_CANCEL");
				}
			/* Synchronize tokyop cabinet */
			} else if (strcmp(httpsqs_input_opt, "sync") == 0) {
				if(httpsqs_sync_db()) {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_TCBD_SYNC_OK");
				} else {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_TCBD_SYNC_ERROR");
				}
			} else if (strcmp(httpsqs_input_opt, "stop") == 0) {
				tcbdbsync(httpsqs_db_tcbdb);
				event_loopexit(NULL);
			} else if (strcmp(httpsqs_input_opt, "create") == 0) {
				get_db((char *)httpsqs_input_name);
			} else if (strcmp(httpsqs_input_opt, "list") == 0) {
				show_dbs();
			} else {
				/* Unrecognized request */
				evbuffer_add_printf(buf, "%s", "HTTPSQS_ERROR");				
			}
		} else {
			/* Invalid or incomplete parameters. */
			evbuffer_add_printf(buf, "%s", "HTTPSQS_ERROR");
		}
		
		/* Send output to client */
	        evhttp_send_reply(req, HTTP_OK, "OK", buf);
		
		/* Release memory */
		evhttp_clear_headers(&httpsqs_http_query);
		evbuffer_free(buf);
}

/* Signal processing */
static void kill_signal(const int sig) {
	/* Synchronize and close tables */
	tcbdbsync(httpsqs_db_tcbdb);
	tcbdbclose(httpsqs_db_tcbdb);
	//Try breaking from the event loop, this should free the evhttp and exit gracefully!
	event_loopexit(NULL);
//	tcbdbdel(httpsqs_db_tcbdb);
    exit(0);
}

static void run_resync_db(int fd, short events, void *arg) {
	show_dbs();
					event_add(&resync_event, &resync_timeout);
	//evhttp_set_timeout(httpd, httpsqs_settings_timeout);
	//fprintf(stdout, "Flushing db data to disk.\n");
	//fprintf(stdout, "Thread stuff %s.\n", thread_stuff);
	//httpsqs_sync_db();
}

void *create_db(void *arg)
{
   thread_stuff = "Threaded call.\n";
   fprintf(stdout, "Threaded function.\n");
   sleep(1);
   fprintf(stdout, "Threaded function.\n");
   sleep(1);
   fprintf(stdout, "Threaded function.\n");
   sleep(1);
   fprintf(stdout, "Threaded function.\n");
   sleep(1);
   fprintf(stdout, "Threaded function.\n");
   sleep(1);
   return NULL;
}

int main(int argc, char **argv)
{
	int c;
	/* Default parameter settings */
	char *httpsqs_settings_listen = "0.0.0.0";
	int httpsqs_settings_port = 1218;
//	char *httpsqs_settings_datapath = NULL;
	bool httpsqs_settings_daemon = false;
	int httpsqs_settings_timeout = 1; /* Set time-out in seconds */
	int i=0;
	
	//int https_settings_memory_cache_size = 104857600; /* Default Cabinet memory cache size is 100M */
	
	static struct option long_options[] = {
               {"listen", 1, NULL, 'l'},
               {"port",  1, NULL, 'p'},
               {"datadir", 1, NULL, 'x'},
               {"memory", 1, NULL, 'm'},
			   {"flush-interval", 1, NULL, 'f'},
               {"timeout", 1, NULL, 't'},
			   {"daemon", 0, NULL, 'd'},
			   {"help", 0, NULL, 'h'},
               {NULL, 0, NULL, 0}
             };
	
    fprintf(stdout, "HTTPSqS Starting.\n");

	/* process arguments */
    /* while ((c = getopt(argc, argv, "l:p:x:m:t:dh")) != -1) { */
	do {
		c = getopt_long (argc, argv, "l:p:x:m:f:t:dh", long_options, NULL);

		switch (c) {
		case 'l':
			httpsqs_settings_listen = strdup(optarg);
			break;
        case 'p':
            httpsqs_settings_port = atoi(optarg);
            break;
        case 'x':
            httpsqs_settings_datapath = strdup(optarg); /* httpsqs database storage path */
			if (access(httpsqs_settings_datapath, W_OK) != 0) { /* If the directory is not writable */
				if (access(httpsqs_settings_datapath, R_OK) == 0) { /* If readable */
					chmod(httpsqs_settings_datapath, S_IWOTH); /* Make dir writable */
				} else { /* If the directory does not exist */
					create_multilayer_dir(httpsqs_settings_datapath);
				}
				
				if (access(httpsqs_settings_datapath, W_OK) != 0) { /* If the directory is not writable */
					fprintf(stderr, "httpsqs database directory not writable\n");
				}
			}
            break;
		case 'm':
			tcbdb_memory_cache_size = atoi(optarg)*1024*1024;
			fprintf(stdout, "Memory cache size: %d M.\n", tcbdb_memory_cache_size/(1024*1024));
			break;
        case 't':
            httpsqs_settings_timeout = atoi(optarg);
			fprintf(stdout, "HTTP Session Timeout: %d s.\n", httpsqs_settings_timeout);
            break;
		case 'f':
			resync_timeout.tv_sec = atoi(optarg);
			fprintf(stdout, "Resync timeout is %ds, will pause on write activity.\n", atoi(optarg));
			break;
        case 'd':
            httpsqs_settings_daemon = true;
            break;
		case -1:    /* Done with options.  */
			break;
	case 'h':
        default:
            show_help();
			abort ();
            return 1;
        }
    }
	while (c != -1);
	
	/* If you forgot -x parameter */
	if (httpsqs_settings_datapath == NULL) {
		show_help();
		fprintf(stderr, "Attention: Please use the indispensable argument: --datadir(-x) <path>\n\n");		
		exit(1);
	}
	
	
//	fprintf(stdout, "Opening Tokyo Cabinet Table.\n");
	/* New B+ Tree Cabinet */
	//httpsqs_db_tcbdb = tcbdbnew();
	//httpsqs_db_tcbdb = start_db(default_db);
	//get_db(default_db);

				
				
				
				
				
				/* Threaded stuff */
				pthread_mutex_lock(&db_create_lock);
				pthread_create(&db_creation_thread, NULL, start_db, NULL);
				pthread_mutex_unlock(&db_create_lock);
				pthread_join(db_creation_thread, NULL);
				fprintf(stdout, "Trying to get queue: %s\n", default_db);
				httpsqs_db_tcbdb = get_db(default_db);
				
				
				
				
				
				
	
	/* Run as daemon if you have the -d parameter */
	if (httpsqs_settings_daemon == true){
        pid_t pid;

        /* Fork off the parent process */       
        pid = fork();
        if (pid < 0) {
                exit(EXIT_FAILURE);
        }
        /* If we got a good PID, then
           we can exit the parent process. */
        if (pid > 0) {
                exit(EXIT_SUCCESS);
        }
	}
	
	/* Ignore broken pipe signal */
	signal (SIGPIPE,SIG_IGN);
	
	/* Process kill signals */
	signal (SIGINT, kill_signal);
	signal (SIGKILL, kill_signal);
	signal (SIGQUIT, kill_signal);
	signal (SIGTERM, kill_signal);
	signal (SIGHUP, kill_signal);
	
	fprintf(stdout, "=============   Starting HTTP Service.   =============\n\n");
	/* Setup request processing */
	struct evhttp *httpd;
	struct event_base *ev_base = event_init();
	
	httpd = evhttp_start(httpsqs_settings_listen, httpsqs_settings_port);
	evhttp_set_timeout(httpd, httpsqs_settings_timeout);
	
	event_set(&resync_event, -1, 0, run_resync_db, NULL);
	if(resync_timeout.tv_sec != 0) {
		event_add(&resync_event, &resync_timeout);
	}
	

	/* Set a callback for requests to "/specific". */
	/* evhttp_set_cb(httpd, "/select", select_handler, NULL); */

	/* Set a callback for all other requests. */
	evhttp_set_gencb(httpd, httpsqs_handler, NULL);

	event_dispatch();

	/* Not reached in this code as it is now. */
	evhttp_free(httpd);
	
	for(i=0;i<service_num;i++) {
			fprintf(stdout, "Closing queue number %d\n", i);
			tcbdbclose(db_service[i].db_tcbdb);
	}
	
	
	//tcbdbclose(httpsqs_db_tcbdb);

	return 0;
}
