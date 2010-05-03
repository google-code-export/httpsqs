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

#include <tcbdb.h>

#include <err.h>
#include <event.h>
#include <evhttp.h>

#define VERSION "1.2"

/* Global vars */
TCBDB *httpsqs_db_tcbdb; /* Data store */
struct event resync_event;
struct timeval resync_timeout = {3,0};

/* Create multi-directory structure */
void create_multilayer_dir( char *muldir ) 
{
    int    i,len;
    char    str[512];
    
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
                else if (*data == '%' && len >= 2 && isxdigit((int) *(data + 1)) 
  && isxdigit((int) *(data + 2))) 
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

/* 设置最大的队列数量，返回值为设置的队列数量。如果返回值为0，则表示设置取消（取消原因为：设置的最大的队列数量小于”当前队列写入位置点“和”当前队列读取位置点“，或者”当前队列写入位置点“小于”当前队列的读取位置点） */
static int httpsqs_maxqueue(const char* httpsqs_input_name, int httpsqs_input_num)
{
	int queue_put_value = 0;
	int queue_get_value = 0;
	int queue_maxnum_int = 0;
	
	/* 读取当前队列写入位置点 */
	queue_put_value = httpsqs_read_putpos(httpsqs_input_name);
	
	/* 读取当前队列读取位置点 */
	queue_get_value = httpsqs_read_getpos(httpsqs_input_name);

	/* 设置最大的队列数量，最小值为10条，最大值为10亿条 */
	queue_maxnum_int = httpsqs_input_num;
	
	/* 设置的最大的队列数量必须大于等于”当前队列写入位置点“和”当前队列读取位置点“，并且”当前队列写入位置点“必须大于等于”当前队列读取位置点“ */
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

/* 重置队列，0表示重置成功 */
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

/* 查看单条队列内容 */
char *httpsqs_view(const char* httpsqs_input_name, int pos)
{
	char *queue_value;
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	memset(queue_name, '\0', 300);
	
	sprintf(queue_name, "%s:%d", httpsqs_input_name, pos);
	
	queue_value = tcbdbget2(httpsqs_db_tcbdb, queue_name);
	
	return queue_value;
}

/* 获取本次“入队列”操作的队列写入点 */
static int httpsqs_now_putpos(const char* httpsqs_input_name)
{
	int maxqueue_num = 0;
	int queue_put_value = 0;
	int queue_get_value = 0;
	char queue_name[300]; /* Total key name, first 256 is user queue name */
	char *queue_input = (char *)malloc(32);
	
	/* 获取最大队列数量 */
	maxqueue_num = httpsqs_read_maxqueue(httpsqs_input_name);
	
	/* 读取当前队列写入位置点 */
	queue_put_value = httpsqs_read_putpos(httpsqs_input_name);
	
	/* 读取当前队列读取位置点 */
	queue_get_value = httpsqs_read_getpos(httpsqs_input_name);	
	
	memset(queue_name, '\0', 300);
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "putpos");	
	
	/* 队列写入位置点加1 */
	queue_put_value = queue_put_value + 1;
	if (queue_put_value == queue_get_value) { /* Write ID + 1 equals Read ID, this means queue is FULL! Refuse to write, return 0 */
		queue_put_value = 0;
	}
	else if (queue_put_value > maxqueue_num) { /* If queue is written at a position bigger than maxqueue_num, go to position 1, introtuces major bug! */
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

/* Get the queue read point, if the return value is 0 then the queue is emty */
static int httpsqs_now_getpos(const char* httpsqs_input_name)
{
	int maxqueue_num = 0;
	int queue_put_value = 0;
	int queue_get_value = 0;
	char queue_name[300]; /* Total key name, first 256 is user queue name */

	/* 获取最大队列数量 */
	maxqueue_num = httpsqs_read_maxqueue(httpsqs_input_name);
	
	/* 读取当前队列写入位置点 */
	queue_put_value = httpsqs_read_putpos(httpsqs_input_name);
	
	/* 读取当前队列读取位置点 */
	queue_get_value = httpsqs_read_getpos(httpsqs_input_name);
	
	/* 如果queue_get_value的值不存在，重置队列读取位置点为1 */
	memset(queue_name, '\0', 300);
	sprintf(queue_name, "%s:%s", httpsqs_input_name, "getpos");
	/* 如果queue_get_value的值不存在，重置为1 */
	if (queue_get_value == 0 && queue_put_value > 0) {
		queue_get_value = 1;
		tcbdbput2(httpsqs_db_tcbdb, queue_name, "1");
	/* 如果队列的读取值（出队列）小于队列的写入值（入队列） */
	} else if (queue_get_value < queue_put_value) {
		queue_get_value = queue_get_value + 1;
		char *queue_input = (char *)malloc(32);		
		memset(queue_input, '\0', 32);
		sprintf(queue_input, "%d", queue_get_value);
		tcbdbput2(httpsqs_db_tcbdb, queue_name, queue_input);
		free(queue_input);
	/* 如果队列的读取值（出队列）大于队列的写入值（入队列），并且队列的读取值（出队列）小于最大队列数量 */
	} else if (queue_get_value > queue_put_value && queue_get_value < maxqueue_num) {
		queue_get_value = queue_get_value + 1;
		char *queue_input = (char *)malloc(32);		
		memset(queue_input, '\0', 32);
		sprintf(queue_input, "%d", queue_get_value);
		tcbdbput2(httpsqs_db_tcbdb, queue_name, queue_input);
		free(queue_input);
	/* 如果队列的读取值（出队列）大于队列的写入值（入队列），并且队列的读取值（出队列）等于最大队列数量 */
	} else if (queue_get_value > queue_put_value && queue_get_value == maxqueue_num) {
		queue_get_value = 1;
		tcbdbput2(httpsqs_db_tcbdb, queue_name, "1");
	/* 队列的读取值（出队列）等于队列的写入值（入队列），即队列中的数据已全部读出 */
	} else {
		queue_get_value = 0;
	}
	
	return queue_get_value;
}

/* 处理模块 */
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
				if(httpsqs_sync_db()) {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_TCBD_SYNC_OK");
				} else {
					evbuffer_add_printf(buf, "%s", "HTTPSQS_TCBD_SYNC_ERROR");
				}
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
	fprintf(stdout, "Flushing db data to disk.\n");
	httpsqs_sync_db();
}

int main(int argc, char **argv)
{
	int c;
	/* Default parameter settings */
	char *httpsqs_settings_listen = "0.0.0.0";
	int httpsqs_settings_port = 1218;
	char *httpsqs_settings_datapath = NULL;
	bool httpsqs_settings_daemon = false;
	int httpsqs_settings_timeout = 1; /* Set time-out in seconds */
	int https_settings_memory_cache_size = 104857600; /* Default Cabinet memory cache size is 100M */
	
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
			https_settings_memory_cache_size = atoi(optarg)*1024*1024;
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
	
	/* Database path */
	int httpsqs_settings_dataname_len = 1024;
	char *httpsqs_settings_dataname = (char *)malloc(httpsqs_settings_dataname_len);
	memset(httpsqs_settings_dataname, '\0', httpsqs_settings_dataname_len);
	sprintf(httpsqs_settings_dataname, "%s/httpsqs.db", httpsqs_settings_datapath); /* Database name is httpsqs.db */

	fprintf(stdout, "Opening Tokyo Cabinet Table.\n");
	/* New B+ Tree Cabinet */
	httpsqs_db_tcbdb = tcbdbnew();

	/* Tuning parameters */
	/* 	1024 members in each leaf page */
	/* 	2024 members in each non-leaf page */
	/* 	50 mil elements in the bucket array (1 to 4 times number of pages to be stored) */
	/* 	8 record alignment by power of two, 2^8 = 256 */
	/* 	10 elements in the free block pool, by power of two, 2^10 = 1024 */
	/* 	opts, BDBTLARGE allows for 2G+ files by using 64bit bucket array */
	tcbdbtune(httpsqs_db_tcbdb, 1024, 2048, 50000000, 8, 10, BDBTLARGE);
	fprintf(stdout, "Memory cache size: %d M.\n", https_settings_memory_cache_size/(1024*1024));
	tcbdbsetxmsiz(httpsqs_db_tcbdb, https_settings_memory_cache_size); /* Set memory cache size */
				
	/* Try opening the table */
	if(!tcbdbopen(httpsqs_db_tcbdb, httpsqs_settings_dataname, BDBOWRITER|BDBOCREAT)){
		show_help();
		fprintf(stderr, "Attention: Unable to open the database.\n\n");		
		exit(1);
	}
	fprintf(stdout, "Done...\n\n");
	
	/* Free vars from memory */
	free(httpsqs_settings_dataname);
	
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
	
	fprintf(stdout, "Starting HTTP Service.\n\n");
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

    return 0;
}
