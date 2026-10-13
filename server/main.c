// Copyright (c) 2020 Cesanta Software Limited
// All rights reserved

#include <signal.h>
#include "mongoose.h"

static int s_debug_level = MG_LL_INFO;
static const char *s_root_dir = ".";
static const char *s_listening_addr = "http://0.0.0.0:8000";
static const char *s_enable_hexdump = "no";

// Changed from #.html so normal HTML files are not treated as SSI
static const char *s_ssi_pattern = "#.shtml";

static const char *s_upload_dir = NULL;  // File uploads disabled by default

// -------------------------------------------------------
// Chronometer data stored by the server
// -------------------------------------------------------
static char s_chrono_state[16] = "WAITING";
static long s_elapsed_ms = 0;

// TLS certificates. For self signed certificates, see
// https://github.com/cesanta/mongoose/blob/master/test/certs/generate.sh
static const char *s_ca_path = NULL;                  // TLS CA file
static const char *s_crt_path = "certs/server.crt";   // TLS cert file
static const char *s_key_path = "certs/server.key";   // TLS key file

static struct mg_str s_ca, s_crt, s_key;

// Handle interrupts, like Ctrl-C
static int s_signo;

static void signal_handler(int signo) {
  s_signo = signo;
}


// -------------------------------------------------------
// Main HTTP event handler
// -------------------------------------------------------
static void cb(struct mg_connection *c, int ev, void *ev_data) {

  // Initialise TLS if HTTPS is being used
  if (ev == MG_EV_ACCEPT && c->is_tls) {
    struct mg_tls_opts opts;

    memset(&opts, 0, sizeof(opts));

    opts.cert = s_crt;
    opts.key = s_key;
    opts.ca = s_ca;

    mg_tls_init(c, &opts);
  }


  // Handle HTTP requests
  if (ev == MG_EV_HTTP_MSG) {

    struct mg_http_message *hm = ev_data;


    // ===================================================
    // CHRONOMETER API
    //
    // GET  /api/chronometer
    //      Returns current chronometer data
    //
    // POST /api/chronometer
    //      Updates chronometer data
    // ===================================================

    if (mg_match(hm->uri, mg_str("/api/chronometer"), NULL)) {


      // -------------------------------------------------
      // POST - update chronometer
      // -------------------------------------------------
      if (mg_match(hm->method, mg_str("POST"), NULL)) {

        // Read values from JSON body
        char *new_state =
            mg_json_get_str(hm->body, "$.state");

        long new_elapsed =
            mg_json_get_long(hm->body, "$.elapsed_ms", -1);


        // Check that both values were received correctly
        if (new_state != NULL && new_elapsed >= 0) {

          // Save state
          mg_snprintf(
              s_chrono_state,
              sizeof(s_chrono_state),
              "%s",
              new_state
          );

          // Save elapsed time
          s_elapsed_ms = new_elapsed;


          // Send response back to client
          mg_http_reply(
              c,
              200,
              "Content-Type: application/json\r\n",
              "{"
              "\"message\":\"updated\","
              "\"state\":\"%s\","
              "\"elapsed_ms\":%ld"
              "}\n",
              s_chrono_state,
              s_elapsed_ms
          );

        } else {

          // Invalid JSON or missing values
          mg_http_reply(
              c,
              400,
              "Content-Type: application/json\r\n",
              "{"
              "\"error\":\"invalid JSON\","
              "\"expected\":\"state and elapsed_ms\""
              "}\n"
          );
        }


        // mg_json_get_str() allocates memory,
        // so free it after we are finished
        mg_free(new_state);
      }


      // -------------------------------------------------
      // GET - return current chronometer data
      // -------------------------------------------------
      else if (mg_match(hm->method, mg_str("GET"), NULL)) {

        mg_http_reply(
            c,
            200,
            "Content-Type: application/json\r\n",
            "{"
            "\"state\":\"%s\","
            "\"elapsed_ms\":%ld"
            "}\n",
            s_chrono_state,
            s_elapsed_ms
        );
      }


      // -------------------------------------------------
      // Anything other than GET or POST
      // -------------------------------------------------
      else {

        mg_http_reply(
            c,
            405,
            "Content-Type: application/json\r\n",
            "{"
            "\"error\":\"method not allowed\""
            "}\n"
        );
      }
    }


    // ===================================================
    // FILE UPLOAD
    // ===================================================

    else if (mg_match(hm->uri, mg_str("/upload"), NULL)) {

      // Serve file upload
      if (s_upload_dir == NULL) {

        mg_http_reply(
            c,
            403,
            "",
            "Denied: file upload directory not set\n"
        );

      } else {

        struct mg_http_part part;

        size_t pos = 0;
        size_t total_bytes = 0;
        size_t num_files = 0;


        while ((pos =
                    mg_http_next_multipart(
                        hm->body,
                        pos,
                        &part)) > 0) {

          char path[MG_PATH_MAX];


          MG_INFO((
              "Chunk name: [%.*s] filename: [%.*s] length: %lu bytes",
              part.name.len,
              part.name.buf,
              part.filename.len,
              part.filename.buf,
              part.body.len
          ));


          mg_snprintf(
              path,
              sizeof(path),
              "%s/%.*s",
              s_upload_dir,
              part.filename.len,
              part.filename.buf
          );


          if (mg_path_is_sane(mg_str(path))) {

            mg_file_write(
                &mg_fs_posix,
                path,
                part.body.buf,
                part.body.len
            );

            total_bytes += part.body.len;
            num_files++;

          } else {

            MG_ERROR((
                "Rejecting dangerous path %s",
                path
            ));
          }
        }


        mg_http_reply(
            c,
            200,
            "",
            "Uploaded %lu files, %lu bytes\n",
            num_files,
            total_bytes
        );
      }
    }


    // ===================================================
    // STATIC WEB FILES
    // ===================================================

    else {

      struct mg_http_serve_opts opts = {0};

      opts.root_dir = s_root_dir;
      opts.ssi_pattern = s_ssi_pattern;

      mg_http_serve_dir(c, hm, &opts);
    }


    // ---------------------------------------------------
    // Log every HTTP request
    // ---------------------------------------------------

    MG_INFO((
        "%.*s %.*s %lu -> %.*s %lu",
        hm->method.len,
        hm->method.buf,
        hm->uri.len,
        hm->uri.buf,
        hm->body.len,
        3,
        c->send.buf + 9,
        c->send.len
    ));
  }
}


// -------------------------------------------------------
// Command-line help
// -------------------------------------------------------

static void usage(const char *prog) {

  fprintf(
      stderr,

      "Mongoose v.%s\n"
      "Usage: %s OPTIONS\n"
      "  -H yes|no - enable traffic hexdump, default: '%s'\n"
      "  -S PAT    - SSI filename pattern, default: '%s'\n"
      "  -d DIR    - directory to serve, default: '%s'\n"
      "  -l ADDR   - listening HTTP address, default: '%s'\n"
      "  -u DIR    - file upload directory, default: unset\n"
      "  -v LEVEL  - debug level, from 0 to 4, default: %d\n",

      MG_VERSION,
      prog,
      s_enable_hexdump,
      s_ssi_pattern,
      s_root_dir,
      s_listening_addr,
      s_debug_level
  );

  exit(EXIT_FAILURE);
}


// -------------------------------------------------------
// Main program
// -------------------------------------------------------

int main(int argc, char *argv[]) {

  char path[MG_PATH_MAX] = ".";

  struct mg_mgr mgr;
  struct mg_connection *c;

  int i;


  // -----------------------------------------------------
  // Parse command-line flags
  // -----------------------------------------------------

  for (i = 1; i < argc; i++) {

    if (strcmp(argv[i], "-d") == 0) {

      s_root_dir = argv[++i];

    } else if (strcmp(argv[i], "-H") == 0) {

      s_enable_hexdump = argv[++i];

    } else if (strcmp(argv[i], "-S") == 0) {

      s_ssi_pattern = argv[++i];

    } else if (strcmp(argv[i], "-l") == 0) {

      s_listening_addr = argv[++i];

    } else if (strcmp(argv[i], "-u") == 0) {

      s_upload_dir = argv[++i];

    } else if (strcmp(argv[i], "-v") == 0) {

      s_debug_level = atoi(argv[++i]);

    } else {

      usage(argv[0]);
    }
  }


  // -----------------------------------------------------
  // Load TLS certificates
  // -----------------------------------------------------

  s_ca = mg_file_read(
      &mg_fs_posix,
      s_ca_path
  );

  s_crt = mg_file_read(
      &mg_fs_posix,
      s_crt_path
  );

  s_key = mg_file_read(
      &mg_fs_posix,
      s_key_path
  );


  if (s_crt.len == 0 || s_key.len == 0) {
    MG_ERROR(("TLS cert+key not loaded"));
  }


  // -----------------------------------------------------
  // Make root directory absolute
  // -----------------------------------------------------

  if (strchr(s_root_dir, ',') == NULL) {

    realpath(
        s_root_dir,
        path
    );

    s_root_dir = path;
  }


  // -----------------------------------------------------
  // Initialise Mongoose
  // -----------------------------------------------------

  signal(
      SIGINT,
      signal_handler
  );

  signal(
      SIGTERM,
      signal_handler
  );


  // Switch off output buffering
  setvbuf(
      stdout,
      NULL,
      _IONBF,
      0
  );


  mg_log_set(
      s_debug_level
  );


  mg_mgr_init(
      &mgr
  );


  // -----------------------------------------------------
  // Start HTTP server
  // -----------------------------------------------------

  if ((c =
           mg_http_listen(
               &mgr,
               s_listening_addr,
               cb,
               NULL)) == NULL) {

    MG_ERROR((
        "Cannot listen on %s. Use http://ADDR:PORT or :PORT",
        s_listening_addr
    ));

    exit(EXIT_FAILURE);
  }


  if (mg_casecmp(
          s_enable_hexdump,
          "yes") == 0) {

    c->is_hexdumping = 1;
  }


  // -----------------------------------------------------
  // Server information
  // -----------------------------------------------------

  MG_INFO((
      "Mongoose version : v%s",
      MG_VERSION
  ));

  MG_INFO((
      "Listening on     : %s",
      s_listening_addr
  ));

  MG_INFO((
      "Web root         : %s",
      s_root_dir
  ));

  MG_INFO((
      "Upload dir       : %s",
      s_upload_dir
          ? s_upload_dir
          : "(unset)"
  ));


  // -----------------------------------------------------
  // Main event loop
  // -----------------------------------------------------

  while (s_signo == 0) {

    mg_mgr_poll(
        &mgr,
        1000
    );
  }


  // -----------------------------------------------------
  // Cleanup
  // -----------------------------------------------------

  mg_mgr_free(
      &mgr
  );


  MG_INFO((
      "Exiting on signal %d",
      s_signo
  ));


  return 0;
}