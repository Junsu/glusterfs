/*
   Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <fnmatch.h>
#include <sys/socket.h>
#include <netdb.h>
#include "authenticate.h"
#include "dict.h"

#define ADDR_DELIMITER " ,"
#define PRIVILEGED_PORT_CEILING 1024

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

/* TODO: duplicate declaration */
typedef struct peer_info {
        struct sockaddr_storage sockaddr;
        socklen_t sockaddr_len;
        char identifier[UNIX_PATH_MAX];
}peer_info_t;

auth_result_t 
gf_auth (dict_t *input_params, dict_t *config_params)
{
  int   ret = 0;
  char *name = NULL;
  char *searchstr = NULL;
  char peer_addr[UNIX_PATH_MAX];
  data_t *peer_info_data = NULL;
  peer_info_t *peer_info = NULL;
  data_t *allow_addr = NULL, *reject_addr = NULL;
  char is_inet_sdp = 0;
  char *type = NULL;
  gf_boolean_t allow_insecure = _gf_false;

  name = data_to_str (dict_get (input_params, "remote-subvolume"));
  if (!name) {
    gf_log ("authenticate/addr",
	    GF_LOG_ERROR,
	    "remote-subvolume not specified");
    return AUTH_DONT_CARE;
  }
  
  ret = asprintf (&searchstr, "auth.addr.%s.allow", name);
  if (-1 == ret) {
          gf_log ("auth/addr", GF_LOG_ERROR,
                  "asprintf failed while setting search string");
          return AUTH_DONT_CARE;
  }
  allow_addr = dict_get (config_params,
			 searchstr);
  free (searchstr);

  ret = asprintf (&searchstr, "auth.addr.%s.reject", name);
  if (-1 == ret) {
          gf_log ("auth/addr", GF_LOG_ERROR,
                  "asprintf failed while setting search string");
          return AUTH_DONT_CARE;
  }
  reject_addr = dict_get (config_params,
			  searchstr);
  free (searchstr);

  if (!allow_addr) {
	  /* TODO: backword compatibility */
	  ret = asprintf (&searchstr, "auth.ip.%s.allow", name);
          if (-1 == ret) {
                  gf_log ("auth/addr", GF_LOG_ERROR,
                          "asprintf failed while setting search string");
                  return AUTH_DONT_CARE;
          }
	  allow_addr = dict_get (config_params, searchstr);
	  free (searchstr);
  }

  if (!(allow_addr || reject_addr)) {
    gf_log ("auth/addr",  GF_LOG_DEBUG,
	    "none of the options auth.addr.%s.allow or "
	    "auth.addr.%s.reject specified, returning auth_dont_care", 
	    name, name);
    return AUTH_DONT_CARE;
  }

  peer_info_data = dict_get (input_params, "peer-info");
  if (!peer_info_data) {
    gf_log ("authenticate/addr",
	    GF_LOG_ERROR,
	    "peer-info not present");
    return AUTH_DONT_CARE;
  }
  
  peer_info = data_to_ptr (peer_info_data);

  switch (((struct sockaddr *) &peer_info->sockaddr)->sa_family) 
    {
    case AF_INET_SDP:
      is_inet_sdp = 1;
      ((struct sockaddr *) &peer_info->sockaddr)->sa_family = AF_INET;

    case AF_INET:
    case AF_INET6:
      {
	char *service;
	uint16_t peer_port;
	strcpy (peer_addr, peer_info->identifier);
	service = strrchr (peer_addr, ':');
	*service = '\0';
	service ++;

	if (is_inet_sdp) {
	  ((struct sockaddr *) &peer_info->sockaddr)->sa_family = AF_INET_SDP;
	}

        ret = dict_get_str (config_params, "rpc-auth-allow-insecure",
                            &type);
        if (ret == 0) {
                ret = gf_string2boolean (type, &allow_insecure);
                if (ret < 0) {
                        gf_log ("auth/addr", GF_LOG_WARNING,
                                "rpc-auth-allow-insecure option %s "
                                "is not a valid bool option", type);
                        return AUTH_DONT_CARE;
                }
        }

	peer_port = atoi (service);
	if (peer_port >= PRIVILEGED_PORT_CEILING && !allow_insecure) {
	  gf_log ("auth/addr", GF_LOG_ERROR,
		  "client is bound to port %d which is not privileged",
		  peer_port);
	  return AUTH_DONT_CARE;
	}
	break;

      case AF_UNIX:
	strcpy (peer_addr, peer_info->identifier);
	break;

      default:
	gf_log ("authenticate/addr", GF_LOG_ERROR,
		"unknown address family %d", 
		((struct sockaddr *) &peer_info->sockaddr)->sa_family);
	return AUTH_DONT_CARE;
      }
    }

  if (reject_addr) {
    char *addr_str = NULL;
    char *tmp;
    char *addr_cpy = strdup (reject_addr->data);
      
    addr_str = strtok_r (addr_cpy, ADDR_DELIMITER, &tmp);
	
    while (addr_str) {
      char negate = 0,  match =0;
      gf_log (name,  GF_LOG_DEBUG,
	      "rejected = \"%s\", received addr = \"%s\"",
	      addr_str, peer_addr);
      if (addr_str[0] == '!') {
	negate = 1;
	addr_str++;
      }

      match = fnmatch (addr_str,
		       peer_addr,
		       0);
      if (negate ? match : !match) {
	free (addr_cpy);
	return AUTH_REJECT;
      }
      addr_str = strtok_r (NULL, ADDR_DELIMITER, &tmp);
    }
    free (addr_cpy);
  }      

  if (allow_addr) {
    char *addr_str = NULL;
    char *tmp;
    char *addr_cpy = strdup (allow_addr->data);
    
    addr_str = strtok_r (addr_cpy, ADDR_DELIMITER, &tmp);
      
    while (addr_str) {
      char negate = 0, match = 0;
      gf_log (name,  GF_LOG_DEBUG,
	      "allowed = \"%s\", received addr = \"%s\"",
	      addr_str, peer_addr);
      if (addr_str[0] == '!') {
	negate = 1;
	addr_str++;
      }

      match = fnmatch (addr_str,
		       peer_addr,
		       0);

      if (negate ? match : !match) {
	free (addr_cpy);
	return AUTH_ACCEPT;
      }
      addr_str = strtok_r (NULL, ADDR_DELIMITER, &tmp);
    }
    free (addr_cpy);
  }      
  
  return AUTH_DONT_CARE;
}

struct volume_options options[] = {
 	{ .key   = {"auth.addr.*.allow"}, 
	  .type  = GF_OPTION_TYPE_ANY 
	},
 	{ .key   = {"auth.addr.*.reject"}, 
	  .type  = GF_OPTION_TYPE_ANY 
	},
	/* Backword compatibility */
 	{ .key   = {"auth.ip.*.allow"}, 
	  .type  = GF_OPTION_TYPE_ANY 
	},
	{ .key = {NULL} }
};
