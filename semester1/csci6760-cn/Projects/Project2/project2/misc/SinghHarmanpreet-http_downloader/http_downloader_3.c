#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <signal.h>

#define BUFFER_SIZE 8192
#define MAX_RETRIES 3

// Define download_task datatype
typedef struct {
	char *url;
	int part;
	int start;
	int end;
	char *output_file;
} download_task;

// Function declarations
void parse_arguments(int arg_count, char *arg_val[], char **url, int *num_parts, char **output_file);
SSL_CTX *create_context();
int get_filesize(const char *url);
void *download_part(void *arg);
void start_download_threads(char *url, int num_parts, int file_size, char *output_file);
void reassemble_file(int num_parts, char *output_file);

// Parse arguments
void parse_arguments(int arg_count, char *arg_val[], char **url, int *num_parts, char **output_file) {
	int opt;
	//printf("parse_arguments...\n");
	while ((opt = getopt(arg_count, arg_val, "u:n:o:")) != -1) {
    	switch (opt) {
        	case 'u':
            	*url = optarg;
            	break;
        	case 'n':
            	*num_parts = atoi(optarg);
            	break;
        	case 'o':
            	*output_file = optarg;
            	break;
        	default:
            	fprintf(stderr, "Usage: %s -u URL -n NUM_PARTS -o OUTPUT_FILE\n", arg_val[0]);
            	exit(EXIT_FAILURE);
    	}
	}
	if (*url == NULL || *num_parts <= 0 || *output_file == NULL) {
    	fprintf(stderr, "Usage: %s -u  URL -n NUM_PARTS -o  OUTPUT_FILE\n", arg_val[0]);
    	exit(EXIT_FAILURE);
	}
}

// Create and configure a new SSL context
SSL_CTX *create_context() {
	const SSL_METHOD *method;
	SSL_CTX *ctx;
	//printf("create_context...\n");
	method = SSLv23_client_method();
	ctx = SSL_CTX_new(method);
	return ctx;
}

// Get file size from the server using HEAD request
int get_filesize(const char *url) {
	SSL_CTX *ctx;
	SSL *ssl;
	int server;
	struct hostent *host;
	struct sockaddr_in addr;
	char request[BUFFER_SIZE], response[BUFFER_SIZE];
	long file_size = -1;

	char hostname[256], path[256];
	sscanf(url, "https://%255[^/]/%255[^\n]", hostname, path);

	//printf("Connecting to server: %s\n", hostname);

	host = gethostbyname(hostname);
	server = socket(AF_INET, SOCK_STREAM, 0);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(443);
	addr.sin_addr.s_addr = *(long *)(host->h_addr);

	connect(server, (struct sockaddr*)&addr, sizeof(addr));

	ctx = create_context();
	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, server);
	SSL_connect(ssl);

	snprintf(request, sizeof(request),
         	"HEAD /%s HTTP/1.1\r\n"
         	"Host: %s\r\n"
         	"Connection: close\r\n\r\n",
         	path, hostname);
	SSL_write(ssl, request, strlen(request));

	//printf("get_file_size while loop...\n");

	while (SSL_read(ssl, response, sizeof(response)) > 0) {
  	//printf("response header %s ....", response);
    	if (strstr(response, "Content-Length:") != NULL) {
        	sscanf(strstr(response, "Content-Length:") + 16, "%ld", &file_size);
        	break;
    	}
	}
	//printf("filesize.... %ld\n",file_size);
	SSL_free(ssl);
	SSL_CTX_free(ctx);
	close(server);
	return file_size;
}

// Download a specific part of the file
void *download_part(void *arg) {
	download_task *task = (download_task *)arg;
	char request[BUFFER_SIZE], response[BUFFER_SIZE];
	SSL_CTX *ctx;
	SSL *ssl;
	int server;
	struct hostent *host;
	struct sockaddr_in addr;
	char *response_body;
	int header_length = 0;
	int retries = 0;

	char hostname[256], path[256];
	sscanf(task->url, "https://%255[^/]/%255[^\n]", hostname, path);

	while (retries < MAX_RETRIES) {
    	//printf("Downloading part %d (%d-%d) from %s (Attempt %d)\n", task->part, task->start, task->end, task->url, retries + 1);

    	host = gethostbyname(hostname);
    	server = socket(AF_INET, SOCK_STREAM, 0);
    	addr.sin_family = AF_INET;
    	addr.sin_port = htons(443);
    	addr.sin_addr.s_addr = *(long *)(host->h_addr);

    	if (connect(server, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        	perror("Connect failed");
        	close(server);
        	retries++;
        	continue;
    	}

    	ctx = create_context();
    	ssl = SSL_new(ctx);
    	SSL_set_fd(ssl, server);
    	if (SSL_connect(ssl) <= 0) {
        	perror("SSL connect failed");
        	SSL_free(ssl);
        	SSL_CTX_free(ctx);
        	close(server);
        	retries++;
        	continue;  
    	}

    	snprintf(request, sizeof(request),
             	"GET /%s HTTP/1.1\r\n"
             	"Host: %s\r\n"
             	"Range: bytes=%d-%d\r\n"
             	"Connection: close\r\n\r\n",
             	path, hostname, task->start, task->end);
    	if (SSL_write(ssl, request, strlen(request)) <= 0) {
        	perror("SSL write failed");
        	SSL_free(ssl);
        	SSL_CTX_free(ctx);
        	close(server);
        	retries++;
        	continue;  
    	}

    	char file_name[20];
    	sprintf(file_name, "part_%d", task->part);
    	FILE *part_file = fopen(file_name, "wb");

    	//printf("Writing part %d to file %s\n", task->part, file_name);

    	while ((header_length = SSL_read(ssl, response, sizeof(response) - 1)) > 0) {
        	response[header_length] = '\0';
        	response_body = strstr(response, "\r\n\r\n");
        	if (response_body) {
            	response_body += 4;
            	fwrite(response_body, 1, header_length - (response_body - response), part_file);
            	break;
        	}
    	}

    	while ((header_length = SSL_read(ssl, response, sizeof(response))) > 0) {
        	fwrite(response, 1, header_length, part_file);
    	}

    	fclose(part_file);

    	SSL_free(ssl);
    	SSL_CTX_free(ctx);
    	close(server);
    	//printf("Part %d downloaded successfully.\n", task->part);
    	return NULL;
	}

	fprintf(stderr, "Failed to download part %d after %d attempts.\n", task->part, MAX_RETRIES);
	return NULL;
}

// Start download threads
void start_download_threads(char *url, int num_parts, int file_size, char *output_file) {
	pthread_t threads[num_parts];
	download_task tasks[num_parts];
	int part_size = file_size / num_parts;

	//printf("Starting downloading the threads...\n");

	for (int i = 0; i < num_parts; i++) {
    	tasks[i].url = url;
    	tasks[i].part = i + 1;
    	tasks[i].start = i * part_size;
    	tasks[i].end = (i == num_parts - 1) ? file_size - 1 : (i + 1) * part_size - 1;
    	tasks[i].output_file = output_file;
    	pthread_create(&threads[i], NULL, download_part, &tasks[i]);
    	//printf(" created ...... %d \n",i);
	}

	for (int i = 0; i < num_parts; i++) {
    	//printf(" joining ...... %d \n",i);
    	pthread_join(threads[i], NULL);
	}

	//printf("All parts downloaded. Reassembling...\n");
}

// Reassemble the file from parts
void reassemble_file(int num_parts, char *output_file) {
	FILE *output = fopen(output_file, "wb");
	if (!output) {
    	perror("fopen");
    	exit(EXIT_FAILURE);
	}

	//printf("Reassembling file into %s...\n", output_file);

	for (int i = 1; i <= num_parts; i++) {
    	char file_name[20];
    	sprintf(file_name, "part_%d", i);

    	FILE *part = fopen(file_name, "rb");
    	if (!part) {
        	perror("fopen");
        	exit(EXIT_FAILURE);
    	}

    	char buffer[8192];
    	size_t bytes;
    	while ((bytes = fread(buffer, 1, sizeof(buffer), part)) > 0) {
        	fwrite(buffer, 1, bytes, output);
    	}
    	fclose(part);
	}
	fclose(output);
	//printf("File reassembled successfully.\n");
}

int main(int arg_count, char *arg_val[]) {
	char *url = NULL;
	int num_parts = 0;
	char *output_file = NULL;

	parse_arguments(arg_count, arg_val, &url, &num_parts, &output_file);

	// Ignore SIGPIPE globally
	signal(SIGPIPE, SIG_IGN);

	//printf("Initializing OpenSSL...\n");
	SSL_load_error_strings();
	OpenSSL_add_ssl_algorithms();

	int file_size = get_filesize(url);
	if (file_size == -1) {
    	fprintf(stderr, "Failed to get file size.\n");
    	exit(EXIT_FAILURE);
	}

	start_download_threads(url, num_parts, file_size, output_file);
	reassemble_file(num_parts, output_file);

	//printf("Cleaning up OpenSSL...\n");
	EVP_cleanup();

	return 0;
}