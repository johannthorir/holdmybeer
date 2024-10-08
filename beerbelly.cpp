#include <unistd.h>
#include <signal.h>
#include <cstdlib>
#include <utility>
#include <cctype>
#include <string>
#include <map>
#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <mutex>
#include <iomanip>
#include <iterator>

#include <fcgio.h>
#include <fcgiapp.h>

#include <openssl/md5.h>

#include <sys/stat.h>

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpointer/jsonpointer.hpp>
#include <jsoncons_ext/jsonpath/jsonpath.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>
#include <jsoncons_ext/mergepatch/mergepatch.hpp>

#include "ClockSetup.h"
#include "base64.h"

static const std::string JSON_HEADER = 
    "Status: 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "Expires: 0\r\n";


static const std::string NOT_FOUND_HEADER = 
    "Status: 404 Not Found\r\n";

static const std::string CLIENT_ERROR_HEADER = 
    "Status: 400 Bad Request\r\n";

static const std::string INCORRECT_PATCH_MEDIA_TYPE = 
    "Status: 415 Unsupported Media\r\n"
    "Accept-Patch: application/json, application/merge-patch+json\r\n";

static const std::string PRECONDITION_FAILED_HEADER = 
    "Status: 412 Precondition Failed\r\n";

static const std::string METHOD_ERROR_HEADER = 
    "Status: 405 Method Not Allowed\r\n"
    "Allow: GET\r\n";

static const std::string CONTENT_DISPOSITION_HEADER = 
    "Content-disposition: inline; filename=\"hmb.json\"\r\n";

static const std::string METHOD_ERROR_BODY = 
    "{ \"error\" : \"Method Not Allowed\" }";

static const std::string END_HEADERS = "\r\n";

static const std::string PID_FILE      = "/var/run/beerbelly-fcgi.pid";
static const std::string FCGI_PORT     = "/var/run/beerbelly.sock";

volatile sig_atomic_t powerSwitch = 1;

time_point     lastModified;
jsoncons::json jdoc;
jsoncons::json jsettings;
std::recursive_mutex docMutex;


// -----------------------------------------------------------------------------

bool ReadSettingsFromFile(const std::string &file) 
{   
    std::ifstream in(file);
    if(in.is_open()) 
    {
        try 
        {
            jsettings = jsoncons::json::parse(in);
        }
        catch( const jsoncons::ser_error& e) 
        {            
            std::cerr << "Parse errors in the settings json file at line: " << e.line() << " col: " << e.column() 
                  << ", category: " <<e.code().category().name() 
                  << ", code: " << e.code().value() 
                  << " and message " << e.what() << std::endl;            
            return false;
        }
        
        return true;
    }
    std::cerr << "Can't open settings file " << file << std::endl;
    return false;
}


// -----------------------------------------------------------------------------

bool UnSerializeFromFile() 
{
    const std::lock_guard<std::recursive_mutex> lock(docMutex);
    std::string filename = jsettings["datafile"].as_string();
    std::ifstream in(filename);
    
    if(in.is_open()) 
    {
        try 
        {
            jdoc = jsoncons::json::parse(in);
        }
        catch(const jsoncons::ser_error& e) 
        {
            std::cerr << "Parse errors in the data json file at line: " << e.line() << " col: " << e.column() 
                  << ", category: " <<e.code().category().name() 
                  << ", code: " << e.code().value() 
                  << " and message " << e.what() << std::endl;            
            return false;
        }

        lastModified =  local_clock::now();
        std::cout << "Read in " << filename << std::endl;
        return true;
    }
    std::cerr << "Can't read datafile " << filename << std::endl;

    return false;
}

// -----------------------------------------------------------------------------

bool SerializeToFile()
{
    const std::lock_guard<std::recursive_mutex> lock(docMutex);
    std::ofstream ofs(jsettings["datafile"].as_string());
    if(ofs.is_open()) 
    {
        ofs << jsoncons::pretty_print(jdoc);
        return true;
    }
    
    return false;
}


// -----------------------------------------------------------------------------

void AddLastModifiedHeader()
{
    std::time_t t = local_clock::to_time_t(lastModified);
    std::cout << std::put_time(std::gmtime(&t), "Last-Modified: %a, %d  %b %Y %H:%M:%S %Z\r\n");
}

// -----------------------------------------------------------------------------

std::string GetETag(const std::string &buffer)
{
    unsigned char result[MD5_DIGEST_LENGTH];    

    // printf("Buffer size is %lu\n", buffer.size());
    MD5((const unsigned char*)(buffer.c_str()), buffer.size(), result);

    std::stringstream oss;    
    oss << "\"";
    base64_encode(result, MD5_DIGEST_LENGTH, oss);
    oss << "\"";

    return oss.str();
}

// -----------------------------------------------------------------------------

void AddETagFromBuffer(const std::string& buffer)
{
    std::cout << "ETag: " << GetETag(buffer) << "\r\n";
}


// -----------------------------------------------------------------------------

void AddJsonFromBuffer(const std::string &buffer)
{
    std::cout << CONTENT_DISPOSITION_HEADER << JSON_HEADER << END_HEADERS << buffer << std::endl;
}


// -----------------------------------------------------------------------------

void HandleFCGIGet(const char *path, FCGX_Request &req) 
{

    // let's get the document 
    const std::lock_guard<std::recursive_mutex> lock(docMutex);
    AddLastModifiedHeader();
        
    std::istreambuf_iterator<char> begin(std::cin), end;
    std::string query(begin, end);

    std::error_code ec;
    const jsoncons::json& currentNode = jsoncons::jsonpointer::get(jdoc, path, ec);
    if (ec)
    {
        std::cout << NOT_FOUND_HEADER << END_HEADERS;
    }
    else
    {
        std::string buffer;
        currentNode.dump(buffer, jsoncons::indenting::indent);
        AddETagFromBuffer(buffer);
        AddJsonFromBuffer(buffer);
    }
}


void HandleFCGIPost(const char *path, FCGX_Request &req) 
{

    // let's get the document 
    const std::lock_guard<std::recursive_mutex> lock(docMutex);
    AddLastModifiedHeader();
        
    std::istreambuf_iterator<char> begin(std::cin), end;
    std::string query(begin, end);
    std::error_code ec;
    const jsoncons::json& currentNode = jsoncons::jsonpointer::get(jdoc, path, ec);
    if (ec)
    {
        std::cout << NOT_FOUND_HEADER << END_HEADERS;
    }
    else
    {
        if(query.length() > 0)
        {
            // ok, we have a jsonpath or a jmespath
            if(query.at(0) == '$')
            {
                // this is a jsonpath
                try 
                {
                    auto res = jsoncons::jsonpath::json_query(currentNode, query);
                    std::string buffer;                    
                    res.dump(buffer, jsoncons::indenting::indent);                
                    AddETagFromBuffer(buffer);             
                    AddJsonFromBuffer(buffer);
                }
                catch(const jsoncons::jsonpath::jsonpath_error &e)
                {
                    std::cerr << e.what();
                    std::cout << CLIENT_ERROR_HEADER << END_HEADERS;                    
                }
            }
            else
            { 
                // interpret as JMESPath.
                try 
                {
                    auto res = jsoncons::jmespath::search(currentNode, query);
                    std::string buffer;                    
                    res.dump(buffer, jsoncons::indenting::indent);                
                    AddETagFromBuffer(buffer);             
                    AddJsonFromBuffer(buffer);
                }
                catch(const jsoncons::jmespath::jmespath_error &e)
                {
                    std::cerr << e.what();
                    std::cout << CLIENT_ERROR_HEADER << END_HEADERS;                    
                }
            }
        }
        else
        {
            std::string buffer;
            currentNode.dump(buffer, jsoncons::indenting::indent);
            AddETagFromBuffer(buffer);
            AddJsonFromBuffer(buffer);
        }
    }
}


// -----------------------------------------------------------------------------


void HandleFCGIPatch(const char *path, FCGX_Request &req) 
{
     // Let's get the document 
    const std::lock_guard<std::recursive_mutex> lock(docMutex);

    // check the content type:
    std::string contentType(FCGX_GetParam("CONTENT_TYPE", req.envp));
    
    bool isJson           = (contentType == "application/json");
    bool isJsonMergePatch = (contentType == "application/merge-patch+json");

    if(!isJson and !isJsonMergePatch) 
    {
        // RETURN PARSE ERROR HEADERS.
        std::cerr << std::string("PATCH Input is neither json or merge-patch json but ") + contentType;
        std::cout << INCORRECT_PATCH_MEDIA_TYPE << END_HEADERS;
        return;        
    }

    jsoncons::json incoming;

    try 
    {
        incoming = jsoncons::json::parse(std::cin);
    }
    catch(const jsoncons::ser_error& e) 
    {
        std::cout << CLIENT_ERROR_HEADER << END_HEADERS;
        std::stringstream oss;
        oss << "Parse errors in the data json file at line: " << e.line() << " col: " << e.column() 
                << ", category: " <<e.code().category().name() 
                << ", code: " << e.code().value() 
                << " and message " << e.what() << std::endl;
        std::cerr << oss.str();
        return;
    }

    std::error_code ec;
    jsoncons::json& currentNode = jsoncons::jsonpointer::get(jdoc, path, ec);
    if (ec)
    {
        std::cout << NOT_FOUND_HEADER << END_HEADERS;            
        return;    
    }
    
    // Mid-air collision prevention:
    const char *http_if_match = FCGX_GetParam("HTTP_IF_MATCH", req.envp);    
    if(http_if_match) 
    {
        // the client wants us to verify that this has not changed...
        std::string buffer;
        currentNode.dump(buffer, jsoncons::indenting::indent);
        
        std::string etag = GetETag(buffer);
        
        if(std::string(http_if_match) != etag)
        {
            std::cout << PRECONDITION_FAILED_HEADER << END_HEADERS;
            return;
        }
    }

    // if the If-Unmodified-Since header is present, we must:
    //   convert the incoming header to a time_point 
    //   reject with 412 Precondition Failed if our modified time is newer.
    //   NOTE: the modified timestamp is not granular - it is for the whole store.
    
    if(isJsonMergePatch) 
        jsoncons::mergepatch::apply_merge_patch(currentNode, incoming);                
    else
        currentNode.swap(incoming);    
    
    lastModified =  local_clock::now();
    
    if(jsettings["alwayssave"].as_bool()) 
        SerializeToFile();

    AddLastModifiedHeader();
    
    const jsoncons::json& updated = jsoncons::jsonpointer::get(jdoc, path);

    std::string buffer;
    updated.dump(buffer, jsoncons::indenting::indent);
    AddETagFromBuffer(buffer);
    AddJsonFromBuffer(buffer);
}

// -----------------------------------------------------------------------------

void HandleFCGIPut(const char *path, FCGX_Request &req) 
{
    // Let's get the document 
    const std::lock_guard<std::recursive_mutex> lock(docMutex);
    
    // check the content type:
    std::string contentType(FCGX_GetParam("CONTENT_TYPE", req.envp));    
    bool isJson = (contentType == "application/json");

    if(!isJson)
    {
        // RETURN PARSE ERROR HEADERS.
        std::stringstream oss;
        oss << "PUT Input is not json but '" << contentType << "'";
        std::cerr << oss.str();
        std::cout << INCORRECT_PATCH_MEDIA_TYPE << END_HEADERS;
        return;        
    }

    jsoncons::json incoming;

    try 
    {
        incoming = jsoncons::json::parse(std::cin);
    }
    catch(const jsoncons::ser_error& e) 
    {
        std::cout << CLIENT_ERROR_HEADER << END_HEADERS;
        std::stringstream oss;    
    
        oss << "Parse errors in the incoming json at line: " << e.line() << " col: " << e.column() 
                << ", category: " <<e.code().category().name() 
                << ", code: " << e.code().value() 
                << " and message " << e.what() << std::endl;            
        std::cerr << oss.str();
        return;
    }
    
    std::error_code ec;
    if(std::string(path) == "") 
    {
        jdoc = incoming;
    }
    else 
    {
        jsoncons::jsonpointer::add(jdoc, path, incoming, true, ec);
    }
    
    if (ec)
    {
        std::stringstream oss;    
        oss << "Error in replace: " 
                << ", category: " <<ec.category().name() 
                << ", code: " << ec.value() 
                << ", message " << ec.message() << std::endl;          
        std::cerr << oss.str();
        std::cout << NOT_FOUND_HEADER << END_HEADERS;            
        return;    
    }

    
    lastModified =  local_clock::now();
    if(jsettings["alwayssave"].as_bool()) 
        SerializeToFile();
    AddLastModifiedHeader();
    
    std::string buffer;
    incoming.dump(buffer, jsoncons::indenting::indent);
    AddETagFromBuffer(buffer);
    AddJsonFromBuffer(buffer);
}

// -----------------------------------------------------------------------------

void HandleFCGIDelete(const char *path, FCGX_Request &req)
{
    // Let's get the document 
    const std::lock_guard<std::recursive_mutex> lock(docMutex);
    
    std::error_code ec;
    jsoncons::jsonpointer::remove(jdoc, path, ec);
    if (ec)
    {
        std::cout << NOT_FOUND_HEADER << END_HEADERS;
        return;
    }
  
    lastModified =  local_clock::now();
    if(jsettings["alwayssave"].as_bool()) 
        SerializeToFile();
    AddLastModifiedHeader();
    std::cout << JSON_HEADER << END_HEADERS << "true" << std::endl;
}

// -----------------------------------------------------------------------------

void HandleFCGIHead(const char *path, FCGX_Request &req)
{
    // let's get the document 
    const std::lock_guard<std::recursive_mutex> lock(docMutex);
    AddLastModifiedHeader();

    // Using error codes to report errors
    std::error_code ec;
    const jsoncons::json& currentNode = jsoncons::jsonpointer::get(jdoc, path, ec);
    if(ec)
    {
        std::cout << NOT_FOUND_HEADER << END_HEADERS;
        return;
    }

    std::string buffer;
    currentNode.dump(buffer, jsoncons::indenting::indent);
    AddETagFromBuffer(buffer);
    std::cout << CONTENT_DISPOSITION_HEADER << JSON_HEADER << END_HEADERS;        
}

// -----------------------------------------------------------------------------

extern "C" void sighandler(int sig_no)
{   
    switch(sig_no) {
        case SIGHUP: 
            SerializeToFile();
            break;
        case SIGINT:
        case SIGTERM:
            powerSwitch = 0;    
            FCGX_ShutdownPending();
            break;
    }
}


// -----------------------------------------------------------------------------

int main(int argc, char **argv)
{

    std::string configfile = argc == 2 ? argv[1] : "/etc/beerbelly.json";

    ReadSettingsFromFile(configfile);

    std::ofstream pidfile;
    pidfile.open(jsettings["pidfile"].as_string());
    pidfile << getpid();
    pidfile.close();

    struct sigaction new_action, old_action;    
    new_action.sa_handler = sighandler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGINT, &new_action, &old_action);
    sigaction(SIGTERM, &new_action, &old_action);
    sigaction(SIGHUP, &new_action, &old_action);

    UnSerializeFromFile(); 

    std::streambuf * cin_streambuf  = std::cin.rdbuf();
    std::streambuf * cout_streambuf = std::cout.rdbuf();
    std::streambuf * cerr_streambuf = std::cerr.rdbuf();

    FCGX_Request request;
    int res = 0;

    if((res = FCGX_Init()) != 0)
        std::cerr << "FCGX_Init fail: " << res << std::endl;

    umask(0);
    int sock = FCGX_OpenSocket(jsettings["port"].as_string().c_str(), 128);

    if((res = FCGX_InitRequest(&request, sock, 0)) != 0)
        std::cerr << "FCGX_InitRequest fail: " << res << std::endl;

    while ((res = FCGX_Accept_r(&request)) == 0 && powerSwitch) 
    {
        fcgi_streambuf cin_fcgi_streambuf(request.in);
        fcgi_streambuf cout_fcgi_streambuf(request.out);
        fcgi_streambuf cerr_fcgi_streambuf(request.err);

        std::cin.rdbuf(&cin_fcgi_streambuf);
        std::cout.rdbuf(&cout_fcgi_streambuf);
        std::cerr.rdbuf(&cerr_fcgi_streambuf);

        char *pi = FCGX_GetParam("PATH_INFO", request.envp);
        std::string method(FCGX_GetParam("REQUEST_METHOD", request.envp));

        if     (method == "GET"   )  HandleFCGIGet(pi, request);
        else if(method == "PATCH" )  HandleFCGIPatch(pi, request);
        else if(method == "PUT"   )  HandleFCGIPut(pi, request);                    
        else if(method == "DELETE")  HandleFCGIDelete(pi, request);
        else if(method == "HEAD"  )  HandleFCGIHead(pi, request);        
        else if(method == "POST"  )  HandleFCGIPost(pi, request);
        else  
        {
            std::cout << METHOD_ERROR_HEADER << JSON_HEADER << END_HEADERS << METHOD_ERROR_BODY;
            std::stringstream oss;
            oss << "Method " << method << " not allowed from " << std::string(FCGX_GetParam("REMOTE_ADDR", request.envp)) << std::endl;
            std::cerr << oss.str();

        }
        
        FCGX_Finish_r(&request);
    }
    close(sock);

    // restore stdio streambufs
    std::cin.rdbuf(cin_streambuf);
    std::cout.rdbuf(cout_streambuf);
    std::cerr.rdbuf(cerr_streambuf);
  
    std::cerr << "FCGI loop exited, res is " << res << std::endl;

    SerializeToFile();
    
}
