#include "SPIFFSEditor.h"
#include <FS.h>
#include "embeds_data.h"
#define SPIFFS_MAXLENGTH_FILEPATH 32

/* Exclusion list feature not needed and omitted */

// WEB HANDLER IMPLEMENTATION

SPIFFSEditor::SPIFFSEditor(const fs::FS& fs, const String& username, const String& password, const String& uri)
  :_fs(fs)
  , _username(username)
  , _password(password)
  , _uri(uri)
  , _authenticated(false)
  , _startTime(0) {}

#ifdef ESP8266
SPIFFSEditor::SPIFFSEditor(const String& username, const String& password, const fs::FS& fs) : SPIFFSEditor(fs, username, password) {};
#endif

bool SPIFFSEditor::canHandle(AsyncWebServerRequest* request) {
  // Embedded JS assets used by edit.htm — match regardless of the configured _uri
  if (request->method() == HTTP_GET) {
    const String& url = request->url();
    if (url.equals(F("/ace.js")) ||
        url.equals(F("/mode-html.js")) ||
        url.equals(F("/mode-json.js")) ||
        url.equals(F("/worker-html.js")) ||
        url.equals(F("/worker-json.js"))) {
      return true;
    }
  }
  if (request->url().equalsIgnoreCase(_uri)) {
    if (request->method() == HTTP_GET) {
      if (request->hasParam("list"))
        return true;
      if (request->hasParam("edit")) {
        if (request->arg("edit").indexOf("wsec") > -1) return false; //make sure wsec.json is not served
        request->_tempFile = _fs.open(request->arg("edit"), "r");
        if (!request->_tempFile) {
          return false;
          }
        #ifdef ESP32
        if (request->_tempFile.isDirectory()) {
          request->_tempFile.close();
          return false;
          }
        #endif
        }
      if (request->hasParam(F("download"))) {
        if (request->arg(F("download")).indexOf("wsec") > -1) return false; //make sure wsec.json is not served
        request->_tempFile = _fs.open(request->arg(F("download")), "r");
        if (!request->_tempFile) {
          return false;
          }
        #ifdef ESP32
        if (request->_tempFile.isDirectory()) {
          request->_tempFile.close();
          return false;
          }
        #endif
        }
      request->addInterestingHeader(F("If-Modified-Since"));
      return true;
      } else if (request->method() == HTTP_POST)
        return true;
      else if (request->method() == HTTP_DELETE)
      return true;
      else if (request->method() == HTTP_PUT)
      return true;

    }
  return false;
  }


void SPIFFSEditor::handleRequest(AsyncWebServerRequest* request) {
  if (_username.length() && _password.length() && !request->authenticate(_username.c_str(), _password.c_str()))
    return request->requestAuthentication();

  if (request->method() == HTTP_GET) {
    // Embedded JS assets used by edit.htm (Ace editor + HTML/JSON modes and workers)
    const String& url = request->url();
    const byte* data = nullptr;
    size_t len = 0;
    if      (url.equals(F("/ace.js")))         { data = ace_js_gz;         len = ace_js_gz_len; }
    else if (url.equals(F("/mode-html.js")))   { data = mode_html_js_gz;   len = mode_html_js_gz_len; }
    else if (url.equals(F("/mode-json.js")))   { data = mode_json_js_gz;   len = mode_json_js_gz_len; }
    else if (url.equals(F("/worker-html.js"))) { data = worker_html_js_gz; len = worker_html_js_gz_len; }
    else if (url.equals(F("/worker-json.js"))) { data = worker_json_js_gz; len = worker_json_js_gz_len; }
    if (data) {
      AsyncWebServerResponse* r = request->beginResponse_P(200, FPSTR(CONTENT_TYPE_JAVASCRIPT), data, len);
      r->addHeader(F("Content-Encoding"), F("gzip"));
      request->send(r);
      return;
    }
    if (request->hasParam("list")) {
      String path = request->getParam("list")->value();
      #ifdef ESP32
      File dir = _fs.open(path);
      #else
      Dir dir = _fs.openDir(path);
      #endif
      path = String();
      String output = "[";
      #ifdef ESP32
      File entry = dir.openNextFile();
      while (entry) {
        #else
      while (dir.next()) {
        fs::File entry = dir.openFile("r");
        #endif
        String fname = entry.name();
        if (fname.indexOf("wsec") == -1) {
          if (output != "[") output += ',';
          #ifdef ESP32
          if (entry.isDirectory()) {
            output += F("{\"type\":\"dir\",\"name\":\"");
            } else
            #endif
            {
            output += F("{\"type\":\"file\",\"name\":\"");
            }
            if (fname[0] != '/') output += '/';
            output += fname;
            output += F("\",\"size\":");
            output += String(entry.size());
            output += '}';
          }
        #ifdef ESP32
        entry = dir.openNextFile();
        #else
        entry.close();
        #endif
        }
      #ifdef ESP32
      dir.close();
      #endif
      output += ']';
      request->send(200, FPSTR(CONTENT_TYPE_JSON), output);
      output = String();
        } else if (request->hasParam("edit") || request->hasParam(F("download"))) {
          request->send(request->_tempFile, request->_tempFile.name(), String(), request->hasParam(F("download")));
          } else {
          const char* buildTime = __DATE__ " " __TIME__ " GMT";
          if (request->header(F("If-Modified-Since")).equals(buildTime)) {
            request->send(304);
            } else {
            AsyncWebServerResponse* response = request->beginResponse_P(200, FPSTR(CONTENT_TYPE_HTML), edit_htm_gz, edit_htm_gz_len);
            response->addHeader(F("Content-Encoding"), F("gzip"));
            response->addHeader(F("Last-Modified"), buildTime);
            request->send(response);
            }
          }
      } else if (request->method() == HTTP_DELETE) {
        if (request->hasParam("path", true)) {
          _fs.remove(request->getParam("path", true)->value());
          request->send(200, "", "DELETE: " + request->getParam("path", true)->value());
          } else
          request->send(404);
        } else if (request->method() == HTTP_POST) {
          if (request->hasParam("data", true, true) && _fs.exists(request->getParam("data", true, true)->value()))
            request->send(200, "", "UPLOADED: " + request->getParam("data", true, true)->value());
          else
            request->send(500);
          } else if (request->method() == HTTP_PUT) {
            if (request->hasParam("path", true)) {
              String filename = request->getParam("path", true)->value();
              if (_fs.exists(filename)) {
                request->send(200);
                } else {
                fs::File f = _fs.open(filename, "w");
                if (f) {
                  f.write((uint8_t)0x00);
                  f.close();
                  request->send(200, "", "CREATE: " + filename);
                  } else {
                  request->send(500);
                  }
                }
              } else
              request->send(400);
            }
    }

void SPIFFSEditor::handleUpload(AsyncWebServerRequest * request, const String & filename, size_t index, uint8_t * data, size_t len, bool final) {
  if (!index) {
    if (!_username.length() || request->authenticate(_username.c_str(), _password.c_str())) {
      _authenticated = true;
      request->_tempFile = _fs.open(filename, "w");
      _startTime = millis();
      }
    }
  if (_authenticated && request->_tempFile) {
    if (len) {
      request->_tempFile.write(data, len);
      }
    if (final) {
      request->_tempFile.close();
      }
    }
  }
