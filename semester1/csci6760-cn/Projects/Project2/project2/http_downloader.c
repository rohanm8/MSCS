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

// Define a struct to store information about a download task
typedef struct {
    char *url;          // URL of the file to download
    int part_number;    // Part number of the download task
    int start_byte;     // Start byte position for the part to download
    int end_byte;       // End byte position for the part to download
    char *output_file;  // Output file name
} download_task_t;

void parse_arguments(int argc, char *argv[], char **url, int *num_parts, char **output_file);
void init_openssl();
void cleanup_openssl();
SSL_CTX *create_context();
int get_file_size(const char *url);
void *download_part(void *arg);
void start_download_threads(char *url, int num_parts, int file_size, char *output_file);
void reassemble_file(int num_parts, char *output_file);

// Parse command line arguments and assign values to the URL, number of parts and output file
void parse_arguments(int argc, char *argv[], char **url, int *num_parts, char **output_file) {
    int opt;        // Variable to store the current option being processed by getopt
    
    // Loop to process each command-line option passed to the program
    while ((opt = getopt(argc, argv, "u:n:o:")) != -1) {
        switch (opt) {
            case 'u':       // -u option specifies the URL of the file to download
                *url = optarg;    // Assign the URL argument to the url pointer
                break;
            case 'n':       // -n option specifies the number of parts to download the file in
                *num_parts = atoi(optarg);  // Convert the argument to an integer and assign it to the num_parts pointer
                break;
            case 'o':       // -o option specifies the output file name
                *output_file = optarg;  // Assign the argument to the output_file pointer
                break;
            default:        // If an invalid option is passed, print the usage message and exit
                fprintf(stderr, "Usage: %s -u URL -n NUM_PARTS -o OUTPUT_FILE\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Check if all the required arguments are provided. Exit with usage info if any are missing or invalid
    if (*url == NULL || *num_parts <= 0 || *output_file == NULL) {
        fprintf(stderr, "Usage: %s -u URL -n NUM_PARTS -o OUTPUT_FILE\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

// Initialize openssl library for TLS connection
void init_openssl() {
    SSL_load_error_strings();       // Load error messages for TLS errors
    OpenSSL_add_ssl_algorithms();   // Initialize SSL algorithms from OpenSSL
}

// Cleanup openssl library to free allocated resources
void cleanup_openssl() {
    EVP_cleanup();    // Cleanup and free all resources used by OpenSSL library
}

// Create and configure a new SSL context for establishing TLS connections
SSL_CTX *create_context() {
    const SSL_METHOD *method;   // Variable to store the TLS method to use for the context
    SSL_CTX *ctx;               // Variable to store the SSL context

    method = SSLv23_client_method();    // Use SSLv23 method, it is compatible with TLS
    ctx = SSL_CTX_new(method);          // Create a new SSL context using the method
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);    // Print openssl specific errors details to stderr
        exit(EXIT_FAILURE);             // Exit if SSL context creation fails
    }
    return ctx;     // Return the created SSL context
}

int get_file_size(const char *url) {
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
    int header_len = 0;

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
             path, hostname, task->start_byte, task->end_byte);
    SSL_write(ssl, request, strlen(request));

    char part_filename[20];
    sprintf(part_filename, "part_%d", task->part_number);
    FILE *part_file = fopen(part_filename, "wb");
    if (!part_file) {
        perror("fopen");
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(server);
        return NULL;
    }

    while ((header_len = SSL_read(ssl, response, sizeof(response) - 1)) > 0) {
        response[header_len] = '\0';
        response_body = strstr(response, "\r\n\r\n");
        if (response_body) {
            response_body += 4;
            fwrite(response_body, 1, header_len - (response_body - response), part_file);
            break;
        }
    }

    while ((header_len = SSL_read(ssl, response, sizeof(response))) > 0) {
        fwrite(response, 1, header_len, part_file);
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
        tasks[i].part_number = i + 1;
        tasks[i].start_byte = i * part_size;
        tasks[i].end_byte = (i == num_parts - 1) ? file_size - 1 : (i + 1) * part_size - 1;
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

int main(int argc, char *argv[]) {
    char *url = NULL;
    int num_parts = 0;
    char *output_file = NULL;

    parse_arguments(argc, argv, &url, &num_parts, &output_file);
    init_openssl();

    int file_size = get_file_size(url);
    if (file_size == -1) {
        fprintf(stderr, "Failed to get file size.\n");
        exit(EXIT_FAILURE);
    }

    start_download_threads(url, num_parts, file_size, output_file);
    reassemble_file(num_parts, output_file);

    cleanup_openssl();
    return 0;
}