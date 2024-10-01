# Hold My Beer

This is a very simple JSON storage daemon implemented as a FastCGI application.  By all means use the built-ins in your webserver to authenticate and authorize as this daemon has no support for tokens or logins.

The daemon has one document in memory and supports returning document fragments when queried with a JSON Pointer in the URL path.

It supports PUT of an application/json data to the JSON Pointer (https://datatracker.ietf.org/doc/html/rfc6901) , as well as PATCH with application/merge-patch+json data.

Nodes in the document can be removed with the DELETE method.

Example of an nginx location directive:

  	location /holdmybeer {
	    include /etc/nginx/fastcgi.conf;
	    fastcgi_split_path_info ^(/holdmybeer)(/.+)$;
	    fastcgi_pass    unix:/var/run/holdmybeer.sock;
	    fastcgi_param   PATH_INFO   $fastcgi_path_info;
	}

## Mid-air collision prevention

An ETag is generated as a hash of the string-formatted resource when a resource is returned. 

A response of a "412 Precondition Failed" is sent and a PATCH modification is rejected if the "If-Match" header of the incoming PATCH doesn't match.

Note that using the "If-Unmodified-Since" header is not granular - the modification time is on the whole store so it doesn't really work with this project.

## Standards.

Supports the JSON Merge Patch standard: https://datatracker.ietf.org/doc/html/rfc7396 - merge patch documents have media type "application/merge-patch+json"

## Data persistance.

The deamon reads "holdmybeer.json" from the current directory at startup and writes the document in memory to the file when exiting or when receiving SIGHUP.

## Dependecies.

Uses libfcgi and libfcgi++ for the FastCGI interface. RapidJSON is used for JSON handling and is included. OpenSSL libcrypt is used for the MD5 hash in the ETags. If using newer OpenSSL libraries you may get a deprecation warning for the MD5 function.


## Examples.

An example of using jquery $.ajax against this daemon is is holdmybeer.html

## Copyright

Copyright (C) 2014,2024 Jóhann Þórir Jóhannsson. All rights reserved.
