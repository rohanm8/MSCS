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

#define BUFFER_SIZE 8192

/*
CHANGELOG
1. SSL_CTX *create_context()
    removed 'if(!ctx)' error checking block
2. void *download_part(void *arg)
    removed 'if(!part_file)' error checking block
3. int 'get_file_size' changed to 'get_filesize'
4. In typedef struct, renamed the following elements
    - part_number to part
    - start_byte to start
    - end_byte to end
    - output_file to output_file
5. In void *download_part, changed the following:
    - header_len to header_length
6. Indentation and spacing changes
7. removed void init_openssl() and added it into the main function
8. removed void cleanup_openssl() and added it into the main function
*/

// Define a struct to store information about a download task
typedef struct {
    char *url;
    int part;
    int start;
    int end;
    char *output_file;
} download_task_t;

void parse_arguments(int arg_count, char *arg_val[], char **url, int *num_parts, char **output_file);
SSL_CTX *create_context();
int get_filesize(const char *url);
void *download_part(void *arg);
void start_download_threads(char *url, int num_parts, int file_size, char *output_file);
void reassemble_file(int num_parts, char *output_file);

// Parse command line arguments and assign values to the URL, number of parts and output file
void parse_arguments(int arg_count, char *arg_val[], char **url, int *num_parts, char **output_file) {
    int opt;        // Variable to store the current option being processed by getopt
    
    // Loop to process each command-line option passed to the program
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

    // Check if all the required arguments are provided. Exit with usage info if any are missing or invalid
    if (*url == NULL || *num_parts <= 0 || *output_file == NULL) {
        fprintf(stderr, "Usage: %s -u  URL -n NUM_PARTS -o  OUTPUT_FILE\n", arg_val[0]);
        exit(EXIT_FAILURE);
    }
}

// Create and configure a new SSL context for establishing TLS connections
SSL_CTX *create_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_client_method();
    ctx = SSL_CTX_new(method);
    return ctx;
}

int get_filesize(const char *url) {
    SSL_CTX *ctx;
    SSL *ssl;
    int server;
    struct hostent *host;
    struct sockaddr_in addr;
    char request[BUFFER_SIZE], response[BUFFER_SIZE];
    int file_size = -1;

    char hostname[256], path[256];
    sscanf(url, "https://%255[^/]/%255[^\n]", hostname, path);
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

    while (SSL_read(ssl, response, sizeof(response)) > 0) {
        if (strstr(response, "Content-Length:") != NULL) {
            sscanf(strstr(response, "Content-Length:") + 16, "%d", &file_size);
            break;
        }
    }

    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(server);
    return file_size;
}

void *download_part(void *arg) {
    download_task_t *task = (download_task_t *)arg;
    char request[BUFFER_SIZE], response[BUFFER_SIZE];
    SSL_CTX *ctx;
    SSL *ssl;
    int server;
    struct hostent *host;
    struct sockaddr_in addr;
    char *response_body;
    int header_length = 0;

    char hostname[256], path[256];
    sscanf(task->url, "https://%255[^/]/%255[^\n]", hostname, path);
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
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Range: bytes=%d-%d\r\n"
             "Connection: close\r\n\r\n", 
             path, hostname, task->start, task->end);
    SSL_write(ssl, request, strlen(request));

    char part_filename[20];
    sprintf(part_filename, "part_%d", task->part);
    FILE *part_file = fopen(part_filename, "wb");

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

    return NULL;
}

void start_download_threads(char *url, int num_parts, int file_size, char *output_file) {
    pthread_t threads[num_parts];
    download_task_t tasks[num_parts];
    int part_size = file_size / num_parts;
    for (int i = 0; i < num_parts; i++) {
        tasks[i].url = url;
        tasks[i].part = i + 1;
        tasks[i].start = i * part_size;
        tasks[i].end = (i == num_parts - 1) ? file_size - 1 : (i + 1) * part_size - 1;
        tasks[i].output_file = output_file;
        pthread_create(&threads[i], NULL, download_part, &tasks[i]);
    }
    for (int i = 0; i < num_parts; i++) {
        pthread_join(threads[i], NULL);
    }
}

void reassemble_file(int num_parts, char *output_file) {
    FILE *output = fopen(output_file, "wb");
    if (!output) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i <= num_parts; i++) {
        char part_filename[20];
        sprintf(part_filename, "part_%d", i);

        FILE *part = fopen(part_filename, "rb");
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
}

int main(int arg_count, char *arg_val[]) {
    char *url = NULL;
    int num_parts = 0;
    char *output_file = NULL;

    parse_arguments(arg_count, arg_val, &url, &num_parts, &output_file);
    // Initialize the OpenSSL library
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    int file_size = get_filesize(url);
    if (file_size == -1) {
        fprintf(stderr, "Failed to get file size.\n");
        exit(EXIT_FAILURE);
    }

    start_download_threads(url, num_parts, file_size, output_file);
    reassemble_file(num_parts, output_file);

    // Cleanup the OpenSSL library
    EVP_cleanup();
    return 0;
}