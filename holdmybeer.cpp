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

#include <fcgio.h>
#include <fcgiapp.h>

#include <openssl/md5.h>

#include <sys/stat.h>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/pointer.h"

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


static const std::string FCGI_PORT = "/var/run/holdmybeer.sock";
static const std::string jsonFileName = "holdmybeer.json";

volatile sig_atomic_t powerSwitch = 1;

time_point lastModified;
rapidjson::Document doc;
std::mutex docMutex;



// -----------------------------------------------------------------------------

rapidjson::Value &JsonMergePatch(rapidjson::Value &target, rapidjson::Value &patch)
{ 
    if(!patch.IsObject()) 
        return patch;

    if(!target.IsObject()) 
        target.SetObject();

    for(auto p = patch.MemberBegin(); p != patch.MemberEnd(); ++p) 
        if(p->value.IsNull())
            target.RemoveMember(p->name);
        else if(target.HasMember(p->name)) 
            target[p->name] = JsonMergePatch(target[p->name], p->value);
        else
            target.AddMember(p->name, p->value, doc.GetAllocator());
    return target;
}

// -----------------------------------------------------------------------------

bool UnSerializeFromFile() 
{
    const std::lock_guard<std::mutex> lock(docMutex);
    std::ifstream in(jsonFileName);
    if(in.is_open()) 
    {
        rapidjson::IStreamWrapper isw(in);        
        doc.ParseStream(isw);
        if(doc.HasParseError()) 
        {
            std::cerr << "Parse errors in jsonstore.json" << std::endl;
            return false;
        }
        lastModified =  local_clock::now();
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------

bool SerializeToFile()
{
    const std::lock_guard<std::mutex> lock(docMutex);
    std::ofstream ofs(jsonFileName);
    if(ofs.is_open()) 
    {
        rapidjson::OStreamWrapper osw(ofs);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
        doc.Accept(writer);
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

void AddETagFromBuffer(rapidjson::StringBuffer &buffer)
{
    unsigned char result[MD5_DIGEST_LENGTH];    
    MD5((const unsigned char*)(buffer.GetString()), buffer.GetSize(), result);
    std::cout <<  "ETag: \"";
    base64_encode(result, MD5_DIGEST_LENGTH, std::cout);
    std::cout << "\"\r\n";
}

// -----------------------------------------------------------------------------

void AddJsonFromBuffer(rapidjson::StringBuffer &buffer)
{
    std::cout << CONTENT_DISPOSITION_HEADER << JSON_HEADER << END_HEADERS << buffer.GetString();
}


// -----------------------------------------------------------------------------

void HandleFCGIGet(const char *path, FCGX_Request &req) 
{
    // let's get the document 
    const std::lock_guard<std::mutex> lock(docMutex);
    AddLastModifiedHeader();

    // first try to find the node:
    rapidjson::Pointer ptr(path);
    rapidjson::Value *currentNode = rapidjson::GetValueByPointer(doc, ptr);
    if(currentNode && !currentNode->IsNull()) 
    {
        try 
        {
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);                
            currentNode->Accept(writer);
            AddETagFromBuffer(buffer);
            AddJsonFromBuffer(buffer);
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when dumping a node." << e.what() << std::endl;
        }      
    }
    else 
    {
        try 
        {
            std::cout << NOT_FOUND_HEADER << END_HEADERS;
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when returning Not Found." << e.what() << std::endl;
        }      
    }
}

// -----------------------------------------------------------------------------


void HandleFCGIPatch(const char *path, FCGX_Request &req) 
{
   
    // Let's get the document 
    const std::lock_guard<std::mutex> lock(docMutex);

    // check the content type:
    std::string contentType(FCGX_GetParam("CONTENT_TYPE", req.envp));
    
    bool isJson           = (contentType == "application/json");
    bool isJsonMergePatch = (contentType == "application/merge-patch+json");

    if(!isJson and !isJsonMergePatch) 
    {
        // RETURN PARSE ERROR HEADERS.
        std::cerr << "PATCH Input is neither json or merge-patch json but '" << contentType << "'" << std::endl;
        try 
        {
            std::cout << INCORRECT_PATCH_MEDIA_TYPE << END_HEADERS;
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when returning Client Error." << e.what() << std::endl;
        }      
        return;        
    }

    rapidjson::Document incoming(&doc.GetAllocator());
    rapidjson::IStreamWrapper isw(std::cin);
    incoming.ParseStream(isw); 
    
    if(incoming.HasParseError()) 
    {
        // RETURN PARSE ERROR HEADERS.
        try 
        {
            std::cout << CLIENT_ERROR_HEADER << END_HEADERS;
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when returning Client Error." << e.what() << std::endl;
        }      
        return;

    }
    // Try to find the node:
    rapidjson::Pointer ptr(path);
    rapidjson::Value *currentNode = rapidjson::GetValueByPointer(doc, ptr);
    if(currentNode) 
    {
        try 
        {
            // Mid-air collision prevention:
            const char *http_if_match = FCGX_GetParam("HTTP_IF_MATCH", req.envp);
            if(http_if_match) 
            {
                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);                
                currentNode->Accept(writer);
                unsigned char result[MD5_DIGEST_LENGTH];    
                MD5((const unsigned char*)(buffer.GetString()), buffer.GetSize(), result);

                std::stringstream oss;    
                oss << "\"";
                base64_encode(result, MD5_DIGEST_LENGTH, oss);
                oss << "\"";
                
                if(std::string(http_if_match) != oss.str())
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
                JsonMergePatch(*currentNode, incoming);
            else
                currentNode->Swap(incoming);    
            
            lastModified =  local_clock::now();
            AddLastModifiedHeader();
            
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);                
            currentNode->Accept(writer);
            AddETagFromBuffer(buffer);
            AddJsonFromBuffer(buffer);
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when dumping a node." << e.what() << std::endl;
        }
        return;
    }

    try 
    {
        std::cout << NOT_FOUND_HEADER << END_HEADERS;
    }
    catch(std::exception const &e)  
    {
        std::cerr << "Exception when returning Not Found." << e.what() << std::endl;
    }      

}


// -----------------------------------------------------------------------------
void HandleFCGIPut(const char *path, FCGX_Request &req) 
{
    // Let's get the document 
    const std::lock_guard<std::mutex> lock(docMutex);
    
    // check the content type:
    std::string contentType(FCGX_GetParam("CONTENT_TYPE", req.envp));    
    bool isJson           = (contentType == "application/json");

    if(!isJson)
    {
        // RETURN PARSE ERROR HEADERS.
        std::cerr << "PUT Input is not json but '" << contentType << "'" << std::endl;
        try 
        {
            std::cout << INCORRECT_PATCH_MEDIA_TYPE << END_HEADERS;
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when returning Client Error." << e.what() << std::endl;
        }      
        return;        
    }
    // Retrieve the payload
    std::string temp = ReadRequestInput(&req);
    rapidjson::Document incoming(&doc.GetAllocator());
    incoming.Parse(rapidjson::StringRef(temp.c_str()));
    if (incoming.HasParseError()) 
    {
        std::cerr << "PUT input to '" << std::string(path) << "' has parse errors: '" << temp << "'" << std::endl;

        // RETURN PARSE ERROR HEADERS.
        try 
        {
            std::cout << CLIENT_ERROR_HEADER << END_HEADERS;
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when returning Client Error." << e.what() << std::endl;
        }      
    }
    else 
    {
        // Try to find the node:
        rapidjson::Pointer ptr(path);
        rapidjson::Value &currentNode = rapidjson::SetValueByPointer(doc, ptr, incoming);    
        lastModified =  local_clock::now();            
        try 
        {            
            AddLastModifiedHeader();

            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);                
            currentNode.Accept(writer);
            AddETagFromBuffer(buffer);
            AddJsonFromBuffer(buffer);
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when dumping a node." << e.what() << std::endl;
        }
    }
}

// -----------------------------------------------------------------------------

void HandleFCGIDelete(const char *path, FCGX_Request &req)
{
    // Let's get the document 
    const std::lock_guard<std::mutex> lock(docMutex);
    rapidjson::Pointer ptr(path);    
    
    if(rapidjson::EraseValueByPointer(doc, ptr))
    {
        lastModified =  local_clock::now();
        AddLastModifiedHeader();
        std::cout << JSON_HEADER << END_HEADERS << "true";
    } 
    else         
    {
        std::cout << NOT_FOUND_HEADER << END_HEADERS;
    }    
}

// -----------------------------------------------------------------------------

void HandleFCGIHead(const char *path, FCGX_Request &req)
{
    // let's get the document 
    const std::lock_guard<std::mutex> lock(docMutex);
    
    AddLastModifiedHeader();

    // first try to find the node:
    rapidjson::Pointer ptr(path);
    rapidjson::Value *currentNode = rapidjson::GetValueByPointer(doc, ptr);
    if(currentNode && !currentNode->IsNull()) 
    {
        try 
        {
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);                
            currentNode->Accept(writer);
            AddETagFromBuffer(buffer);
            std::cout << CONTENT_DISPOSITION_HEADER << JSON_HEADER << END_HEADERS;
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when dumping a node." << e.what() << std::endl;
        }      
    }
    else 
    {
        try 
        {
            std::cout << NOT_FOUND_HEADER << END_HEADERS;
        }
        catch(std::exception const &e)  
        {
            std::cerr << "Exception when returning Not Found." << e.what() << std::endl;
        }      
    }
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

int main(void)
{
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
    int sock = FCGX_OpenSocket(FCGI_PORT.c_str(), 128);

    

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

        // char **env = req.envp; while (*(++env)) puts(*env);

        if     (method == "GET"   )  HandleFCGIGet(pi, request);
        else if(method == "PATCH" )  HandleFCGIPatch(pi, request);
        else if(method == "PUT"   )  HandleFCGIPut(pi, request);                    
        else if(method == "DELETE")  HandleFCGIDelete(pi, request);
        else if(method == "HEAD"  )  HandleFCGIHead(pi, request);
        else  
        {
            std::cout << METHOD_ERROR_HEADER << JSON_HEADER << END_HEADERS << METHOD_ERROR_BODY;
            std::cerr << "Method " << method << " not allowed from " << std::string(FCGX_GetParam("REMOTE_ADDR", request.envp));
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
