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

## Settings

The daemon reads the settings from the json file /etc/holdmybeer/settings.json and expects an object with two members
* "port" - this is the name of the fastcgi port, either a unix port or a tcp port.
* "datafile" - path to the file for the persistance of the json document.

## Data persistance.

The deamon reads the json file whose path is in the "datafile" member of the settings document. The document is written to the file when exiting or when receiving SIGHUP.

## Dependecies.

Uses libfcgi and libfcgi++ for the FastCGI interface. RapidJSON is used for JSON handling and is included. OpenSSL libcrypt is used for the MD5 hash in the ETags. If using newer OpenSSL libraries you may get a deprecation warning for the MD5 function.


## Examples.

An example of using jquery $.ajax against this daemon is is example.html


## Build and install

This should do it:

	git clone https://github.com/johannthorir/holdmybeer.git
	cd holdmybeer
	sudo apt-get install libfcgi-dev openssl
	cmake .
	make
	sudo cp holdmybeer-fcgi /usr/sbin
	sudo install -v holdmybeer.conf /etc/nginx/snippets
	sudo install -v -D -t /etc/holdmybeer/ settings.json 
	sudo install -v -D -t /usr/share/holdmybeer/ data.json
	sudo install -v example.html /var/www/html
	sudo install holdmybeer.service /etc/systemd/system/
	sudo systemctl daemon-reload
	sudo systemctl enable holdmybeer.service
	sudo service holdmybeer start

Then add a line to the nginx config to 'include snippets/holdmybeer.conf;'

## Copyright

Copyright (C) 2014,2024 Jóhann Þórir Jóhannsson. All rights reserved.
