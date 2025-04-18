/*
 * upf_cli.c - 3GPP TS 29.244 GTP-U UP plug-in for vpp
 *
 * Copyright (c) 2017-2019 Travelping GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Modified by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.

#include <math.h>
#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <vpp/app/version.h>
#include <vnet/dpo/lookup_dpo.h>
#include <vnet/fib/ip4_fib.h>
#include <vnet/fib/ip6_fib.h>
#include <vnet/ip/ip6_hop_by_hop.h>

#include <upf/upf.h>
#include <upf/upf_pfcp.h>
#include <upf/pfcp.h>
#include <upf/upf_pfcp_server.h>
#include <upf/upf_proxy.h>

/* Action function shared between message handler and debug CLI */
#include <upf/flowtable.h>
#include <upf/upf_app_db.h>

#if CLIB_DEBUG > 1
#define upf_debug clib_warning
#else
#define upf_debug(...)                          \
  do { } while (0)
#endif

static clib_error_t *
upf_pfcp_endpoint_ip_add_del_command_fn (vlib_main_t * vm,
					 unformat_input_t * main_input,
					 vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t *error = NULL;
  u32 fib_index = 0;
  ip46_address_t ip;
  u8 addr_set = 0;
  u32 vrf = ~0;
  u8 add = 1;
  int rv;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "del"))
	add = 0;
      else if (unformat (line_input, "add"))
	add = 1;
      else
	if (unformat
	    (line_input, "%U", unformat_ip46_address, &ip, IP46_TYPE_ANY))
	addr_set = 1;
      else if (unformat (line_input, "vrf %u", &vrf))
	;
      else
	{
	  error = unformat_parse_error (line_input);
	  goto done;
	}
    }

  if (!addr_set)
    {
      error = clib_error_return (0, "endpoint IP be specified");
      goto done;
    }

  if (vrf != ~0)
    {
      fib_index =
	fib_table_find (fib_ip_proto (!ip46_address_is_ip4 (&ip)), vrf);
      if (fib_index == ~0)
	{
	  error = clib_error_return (0, "nonexistent vrf %d", vrf);
	  goto done;
	}
    }

  rv = vnet_upf_pfcp_endpoint_add_del (&ip, fib_index, add);

  switch (rv)
    {
    case 0:
      break;

    case VNET_API_ERROR_NO_SUCH_ENTRY:
      error = clib_error_return (0, "network instance does not exist...");
      break;

    default:
      error = clib_error_return (0, "vnet_upf_pfcp_endpoint_add_del %d", rv);
      break;
    }

done:
  unformat_free (line_input);
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_pfcp_endpoint_ip_add_del_command, static) =
{
  .path = "upf pfcp endpoint ip",
  .short_help =
  "upf pfcp endpoint ip <address> [vrf <table-id>] [del]",
  .function = upf_pfcp_endpoint_ip_add_del_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_pfcp_show_endpoint_command_fn (vlib_main_t * vm,
				   unformat_input_t * main_input,
				   vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  upf_main_t *gtm = &upf_main;
  clib_error_t *error = NULL;
  ip46_address_fib_t *key;
  uword *v;

  if (unformat_user (main_input, unformat_line_input, line_input))
    {
      while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
	{
	  error = unformat_parse_error (line_input);
	  unformat_free (line_input);
	  goto done;
	}

      unformat_free (line_input);
    }

  vlib_cli_output (vm, "Endpoints: %d\n",
		   mhash_elts (&gtm->pfcp_endpoint_index));

  /* *INDENT-OFF* */
  mhash_foreach(key, v, &gtm->pfcp_endpoint_index,
  ({
    vlib_cli_output (vm, "  %U: %u\n", format_pfcp_endpoint_key, key, *v);
  }));
  /* *INDENT-ON* */

done:
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_pfcp_show_endpoint_command, static) =
{
  .path = "show upf pfcp endpoint",
  .short_help =
  "show upf pfcp endpoint",
  .function = upf_pfcp_show_endpoint_command_fn,
};
/* *INDENT-ON* */

/**
 * Translate "foo.com" into "0x3 f o o 0x3 c o m 0x0"
 * A historical / hysterical micro-TLV scheme. DGMS.
 */
static u8 *
upf_name_to_labels (u8 * name)
{
  int i;
  int last_label_index;
  u8 *rv;

  rv = vec_dup (name);

  /* punch in space for the first length */
  vec_insert (rv, 1, 0);
  last_label_index = 0;
  i = 1;

  while (i < vec_len (rv))
    {
      if (rv[i] == '.')
	{
	  rv[last_label_index] = (i - last_label_index) - 1;
	  if ((i - last_label_index) > 63)
	    clib_warning ("stupid name, label length %d",
			  i - last_label_index);
	  last_label_index = i;
	  rv[i] = 0;
	}
      i++;
    }
  /* Set the last real label length */
  rv[last_label_index] = (i - last_label_index) - 1;

  return rv;
}

static clib_error_t *
upf_ueip_pool_add_del_command_fn (vlib_main_t * vm,
				  unformat_input_t * main_input,
				  vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t *error = NULL;
  u8 *name = 0;
  u8 *nwi_s = 0;
  u8 *nwi_name;
  int rc = 0;
  int is_add = 1;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "id %_%v%_", &name))
	;
      else if (unformat (line_input, "del"))
	is_add = 0;
      else if (unformat (line_input, "nwi %_%v%_", &nwi_s))
	;
      else
	{
	  error = unformat_parse_error (line_input);
	  goto done;
	}
    }

  nwi_name = upf_name_to_labels (nwi_s);
  vec_free (nwi_s);

  rc = vnet_upf_ue_ip_pool_add_del (name, nwi_name, is_add);

done:
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_ueip_pool_add_del_command, static) =
{
  .path = "upf ueip pool",
  .short_help =
  "upf ueip pool nwi <nwi-name> id <identity> [del]",
  .function = upf_ueip_pool_add_del_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_nat_pool_add_del_command_fn (vlib_main_t * vm,
				 unformat_input_t * main_input,
				 vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t *error = NULL;
  u8 *name = 0;
  u8 *nwi_name;
  u8 *nwi_s = 0;
  ip4_address_t start, end;
  u16 min_port;
  u16 max_port;
  u16 port_block_size;
  u8 is_add = 1;
  int rv;

  min_port = UPF_NAT_MIN_PORT;
  max_port = UPF_NAT_MAX_PORT;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "%U - %U",
		    unformat_ip4_address, &start, unformat_ip4_address, &end))
	;
      else if (unformat (line_input, "block_size %u", &port_block_size))
	;
      else if (unformat (line_input, "nwi %_%v%_", &nwi_s))
	;
      else if (unformat (line_input, "name %_%v%_", &name))
	;
      else if (unformat (line_input, "del"))
	is_add = 0;
      else
	{
	  error = unformat_parse_error (line_input);
	  goto done;
	}
    }

  nwi_name = upf_name_to_labels (nwi_s);
  vec_free (nwi_s);

  rv =
    vnet_upf_nat_pool_add_del (nwi_name, start, end, name, port_block_size,
			       min_port, max_port, is_add);

  if (rv)
    error = clib_error_return (0, "Unable to create NAT Pool");

done:
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_nat_pool_add_del_command, static) =
{
  .path = "upf nat pool",
  .short_help =
  "upf nat pool nwi <nwi-name> start <ip4-addr> end <ip4-addr> min_port <min-port> max_port <max-port> block_size <port-block-size> name <name> [del]",
  .function = upf_nat_pool_add_del_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_nwi_add_del_command_fn (vlib_main_t * vm,
			    unformat_input_t * main_input,
			    vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t *error = NULL;
  u8 *name = NULL;
  u8 *s;
  u32 table_id = 0;
  u8 add = 1;
  int rv;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "del"))
	add = 0;
      else if (unformat (line_input, "add"))
	add = 1;
      else if (unformat (line_input, "name %_%v%_", &s))
	{
	  name = upf_name_to_labels (s);
	  vec_free (s);
	}
      else if (unformat (line_input, "table %u", &table_id))
	;
      else if (unformat (line_input, "vrf %u", &table_id))
	;
      else
	{
	  error = unformat_parse_error (line_input);
	  goto done;
	}
    }

  if (!name)
    {
      error = clib_error_return (0, "name or label must be specified!");
      goto done;
    }

  if (~0 == fib_table_find (FIB_PROTOCOL_IP4, table_id))
    clib_warning ("table %d not (yet) defined for IPv4", table_id);
  if (~0 == fib_table_find (FIB_PROTOCOL_IP6, table_id))
    clib_warning ("table %d not (yet) defined for IPv6", table_id);

  rv = vnet_upf_nwi_add_del (name, table_id, table_id, add);

  switch (rv)
    {
    case 0:
      break;

    case VNET_API_ERROR_IF_ALREADY_EXISTS:
      error = clib_error_return (0, "Network instance '%v' already exists...",name);
      break;

    case VNET_API_ERROR_NO_SUCH_ENTRY:
      error = clib_error_return (0, "network instance does not exist...");
      break;

    default:
      error = clib_error_return (0, "vnet_upf_nwi_add_del returned %d", rv);
      break;
    }

done:
  vec_free (name);
  unformat_free (line_input);
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_nwi_add_del_command, static) =
{
  .path = "upf nwi",
  .short_help =
  "upf nwi name <name> [table <table-id>] [del]",
  .function = upf_nwi_add_del_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_nwi_command_fn (vlib_main_t * vm,
			 unformat_input_t * main_input,
			 vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  upf_main_t *gtm = &upf_main;
  clib_error_t *error = NULL;
  upf_nwi_t *nwi;
  u8 *name = NULL;
  u8 *s;

  if (unformat_user (main_input, unformat_line_input, line_input))
    {
      while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
	{
	  if (unformat (line_input, "name %_%v%_", &s))
	    {
	      name = upf_name_to_labels (s);
	      vec_free (s);
	    }
	  else
	    {
	      error = unformat_parse_error (line_input);
	      unformat_free (line_input);
	      goto done;
	    }
	}

      unformat_free (line_input);
    }

  pool_foreach (nwi, gtm->nwis)
  {
    if (name && !vec_is_equal (name, nwi->name))
      continue;

    vlib_cli_output (vm, "%U, ip4-fib-index %u, ip6-fib-index %u\n",
		     format_dns_labels, nwi->name,
		     nwi->fib_index[FIB_PROTOCOL_IP4],
		     nwi->fib_index[FIB_PROTOCOL_IP6]);
  }

done:
  vec_free (name);
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_nwi_command, static) =
{
  .path = "show upf nwi",
  .short_help =
  "show upf nwi",
  .function = upf_show_nwi_command_fn,
};
/* *INDENT-ON* */

#if 0
static void
vtep_ip4_ref (ip4_address_t * ip, u8 ref)
{
  uword *vtep = hash_get (upf_main.vtep4, ip->as_u32);
  if (ref)
    {
      if (vtep)
	++(*vtep);
      else
	hash_set (upf_main.vtep4, ip->as_u32, 1);
    }
  else
    {
      if (!vtep)
	return;

      if (--(*vtep) == 0)
	hash_unset (upf_main.vtep4, ip->as_u32);
    }
}

static void
vtep_ip6_ref (ip6_address_t * ip, u8 ref)
{
  uword *vtep = hash_get_mem (upf_main.vtep6, ip);
  if (ref)
    {
      if (vtep)
	++(*vtep);
      else
	hash_set_mem_alloc (&upf_main.vtep6, ip, 1);
    }
  else
    {
      if (!vtep)
	return;

      if (--(*vtep) == 0)
	hash_unset_mem_free (&upf_main.vtep6, ip);
    }
}

static void
vtep_if_address_add_del (u32 sw_if_index, u8 add)
{
  ip_lookup_main_t *lm4 = &ip4_main.lookup_main;
  ip_lookup_main_t *lm6 = &ip6_main.lookup_main;
  ip_interface_address_t *ia = 0;
  ip4_address_t *ip4;
  ip6_address_t *ip6;

  /* *INDENT-OFF* */
  foreach_ip_interface_address (lm4, ia, sw_if_index, 1 /* unnumbered */ ,
  ({
    ip4 = ip_interface_address_get_address (lm4, ia);
    vtep_ip4_ref(ip4, add);
  }));
  foreach_ip_interface_address (lm6, ia, sw_if_index, 1 /* unnumbered */ ,
  ({
    ip6 = ip_interface_address_get_address (lm6, ia);
    vtep_ip6_ref(ip6, add);
  }));
  /* *INDENT-ON* */
}
#endif

static clib_error_t *
upf_tdf_ul_table_add_del_command_fn (vlib_main_t * vm,
				     unformat_input_t * main_input,
				     vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t *error = NULL;
  fib_protocol_t fproto = FIB_PROTOCOL_IP4;
  u32 table_id = ~0;
  u32 vrf = 0;
  u8 add = 1;
  int rv;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "del"))
	add = 0;
      else if (unformat (line_input, "add"))
	add = 1;
      else if (unformat (line_input, "vrf %u", &vrf))
	;
      else if (unformat (line_input, "ip4"))
	fproto = FIB_PROTOCOL_IP4;
      else if (unformat (line_input, "ip6"))
	fproto = FIB_PROTOCOL_IP6;
      else if (unformat (line_input, "table-id %u", &table_id))
	;
      else
	{
	  error = unformat_parse_error (line_input);
	  goto done;
	}
    }

  if (table_id == ~0)
    return clib_error_return (0, "table-id must be specified");

  rv = vnet_upf_tdf_ul_table_add_del (vrf, fproto, table_id, add);

  switch (rv)
    {
    case 0:
      break;

    case VNET_API_ERROR_NO_SUCH_FIB:
      error = clib_error_return (0, "TDF UL lookup table already exists...");
      break;

    case VNET_API_ERROR_NO_SUCH_ENTRY:
      error = clib_error_return (0, "In VRF instance does not exist...");
      break;

    default:
      error = clib_error_return (0, "vvnet_upf_tdf_ul_table_add_del %d", rv);
      break;
    }

done:
  unformat_free (line_input);
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_tdf_ul_table_add_del_command, static) =
{
  .path = "upf tdf ul table",
  .short_help =
  "upf tdf ul table vrf <table-id> [ip4|ip6] table-id <src-lookup-table-id> [del]",
  .function = upf_tdf_ul_table_add_del_command_fn,
};
/* *INDENT-ON* */

static u32
upf_table_id_from_fib_index (fib_protocol_t fproto, u32 fib_index)
{
  return (fproto == FIB_PROTOCOL_IP4) ?
    ip4_fib_get (fib_index)->table_id : ip6_fib_get (fib_index)->table_id;
};

static clib_error_t *
upf_tdf_ul_table_show_fn (vlib_main_t * vm,
			  unformat_input_t * input, vlib_cli_command_t * cmd)
{
  upf_main_t *gtm = &upf_main;
  fib_protocol_t fproto;
  u32 ii;

  vlib_cli_output (vm, "UPF TDF UpLink VRF to fib-index mappings:");
  FOR_EACH_FIB_IP_PROTOCOL (fproto)
  {
    vlib_cli_output (vm, " %U", format_fib_protocol, fproto);
    vec_foreach_index (ii, gtm->tdf_ul_table[fproto])
    {
      if (~0 != vec_elt (gtm->tdf_ul_table[fproto], ii))
	{
	  u32 vrf_table_id = upf_table_id_from_fib_index (fproto, ii);
	  u32 fib_table_id = upf_table_id_from_fib_index (fproto,
							  vec_elt
							  (gtm->tdf_ul_table
							   [fproto],
							   ii));

	  vlib_cli_output (vm, "  %u -> %u", vrf_table_id, fib_table_id);
	}
    }
  }
  return (NULL);
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_tdf_ul_table_show_command, static) = {
  .path = "show upf tdf ul tables",
  .short_help = "Show UPF TDF UpLink tables",
  .function = upf_tdf_ul_table_show_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_tdf_ul_enable_command_fn (vlib_main_t * vm,
			      unformat_input_t * main_input,
			      vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  fib_protocol_t fproto = FIB_PROTOCOL_IP4;
  vnet_main_t *vnm = vnet_get_main ();
  u32 sw_if_index = ~0;
  u8 enable = 1;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "%U", unformat_vnet_sw_interface,
		    vnm, &sw_if_index))
	;
      else if (unformat (line_input, "enable"))
	enable = 1;
      else if (unformat (line_input, "disable"))
	enable = 0;
      else if (unformat (line_input, "ip4"))
	fproto = FIB_PROTOCOL_IP4;
      else if (unformat (line_input, "ip6"))
	fproto = FIB_PROTOCOL_IP6;
      else
	break;
    }

  if (~0 == sw_if_index)
    return clib_error_return (0, "interface must be specified");

  vnet_upf_tdf_ul_enable_disable (fproto, sw_if_index, enable);

  return NULL;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_tdf_ul_enable_command, static) = {
    .path = "upf tdf ul enable",
    .short_help = "UPF TDF UpLink [enable|disable] [ip4|ip6] <interface>",
    .function = upf_tdf_ul_enable_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_spec_release_command_fn (vlib_main_t * vm,
			     unformat_input_t * main_input,
			     vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  upf_main_t *gtm = &upf_main;
  u32 spec_version = 0;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "release %u", &spec_version))
	break;
      else
	return 0;
    }

  gtm->pfcp_spec_version = spec_version;
  return NULL;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_spec_release_command, static) = {
    .path = "upf specification",
    .short_help = "upf specification release [MAJOR.MINOR.PATCH]",
    .function = upf_spec_release_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_spec_release_command_fn (vlib_main_t * vm,
				  unformat_input_t * main_input,
				  vlib_cli_command_t * cmd)
{
  upf_main_t *gtm = &upf_main;
  vlib_cli_output (vm, "PFCP version: %u", gtm->pfcp_spec_version);
  return NULL;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_spec_release_command, static) =
{
  .path = "show upf specification release",
  .short_help =
  "show upf specification release",
  .function = upf_show_spec_release_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_node_id_command_fn (vlib_main_t * vm,
			unformat_input_t * main_input,
			vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t *error = NULL;
  u8 *fqdn = 0;
  pfcp_node_id_t node_id = {.type = (u8) ~ 0 };

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "fqdn %_%v%_", &fqdn))
	{
	  node_id.type = NID_FQDN;
	  node_id.fqdn = upf_name_to_labels (fqdn);
	  vec_free (fqdn);
	}
      else if (unformat (line_input, "ip4 %U",
			 unformat_ip46_address, &node_id.ip, IP46_TYPE_ANY))
	{
	  node_id.type = NID_IPv4;
	}
      else if (unformat (line_input, "ip6 %U",
			 unformat_ip46_address, &node_id.ip, IP46_TYPE_ANY))
	{
	  node_id.type = NID_IPv6;
	}
      else
	{
	  error = unformat_parse_error (line_input);
	  return error;
	}
    }

  if ((u8) ~ 0 == node_id.type)
    return clib_error_return (0, "A valid node id must be specified");

  vnet_upf_node_id_set (&node_id);

  return NULL;

}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_node_id_command, static) = {
    .path = "upf node-id",
    .short_help = "upf node-id ( fqdn <fqdn> | ip4 <ip4-addr> | ip6 <ip6-addr> )",
    .function = upf_node_id_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_node_id_command_fn (vlib_main_t * vm,
			     unformat_input_t * main_input,
			     vlib_cli_command_t * cmd)
{
  upf_main_t *gtm = &upf_main;
  u8 *type = 0;
  vec_reset_length (type);
  vlib_cli_output (vm, "Node ID: %U", format_node_id, &gtm->node_id);
  return NULL;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_node_id_command, static) =
{
  .path = "show upf node-id",
  .short_help =
  "show upf node-id",
  .function = upf_show_node_id_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_gtpu_endpoint_add_del_command_fn (vlib_main_t * vm,
				      unformat_input_t * main_input,
				      vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  u32 teid = 0, mask = 0, teidri = 0;
  clib_error_t *error = NULL;
  ip6_address_t ip6 = ip6_address_initializer;
  ip4_address_t ip4 = ip4_address_initializer;
  u8 ip_set = 0;
  u8 *name = NULL;
  u8 intf = INTF_INVALID;
  u8 add = 1;
  int rv;
  u8 *s;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "del"))
	add = 0;
      else if (unformat (line_input, "add"))
	add = 1;
      else if (unformat (line_input, "ip %U", unformat_ip4_address, &ip4))
	ip_set |= 1;
      else if (unformat (line_input, "ip6 %U", unformat_ip6_address, &ip6))
	ip_set |= 2;
      else if (unformat (line_input, "nwi %_%v%_", &s))
	{
	  name = upf_name_to_labels (s);
	  vec_free (s);
	}
      else if (unformat (line_input, "intf access"))
	intf = SRC_INTF_ACCESS;
      else if (unformat (line_input, "intf core"))
	intf = SRC_INTF_CORE;
      else if (unformat (line_input, "intf sgi"))
	/*
	 * WTF: the specification does permit that,
	 *      but what does that mean in terms
	 *      of the UPIP IE?
	 */
	intf = SRC_INTF_SGI_LAN;
      else if (unformat (line_input, "intf cp"))
	intf = SRC_INTF_CP;
      else if (unformat (line_input, "teid %u/%u", &teid, &teidri))
	{
	  if (teidri > 7)
	    {
	      error =
		clib_error_return (0,
				   "TEID Range Indication to large (%d > 7)",
				   teidri);
	      goto done;
	    }
	  mask = 0xfe000000 << (7 - teidri);
	}
      else if (unformat (line_input, "teid 0x%x/%u", &teid, &teidri))
	{
	  if (teidri > 7)
	    {
	      error =
		clib_error_return (0,
				   "TEID Range Indication to large (%d > 7)",
				   teidri);
	      goto done;
	    }
	  mask = 0xfe000000 << (7 - teidri);
	}
      else
	{
	  error = unformat_parse_error (line_input);
	  goto done;
	}
    }

  if (!ip_set)
    {
      error = clib_error_return (0, "ip or ip6 need to be set");
      goto done;
    }

  rv = vnet_upf_upip_add_del (&ip4, &ip6, name, intf, teid, mask, add);

  switch (rv)
    {
    case 0:
      break;

    case VNET_API_ERROR_NO_SUCH_ENTRY:
      error =
	clib_error_return (0, "network instance or entry does not exist...");
      break;

    default:
      error = clib_error_return
	(0, "vnet_upf_nwi_set_intf_role returned %d", rv);
      break;
    }

done:
  vec_free (name);
  unformat_free (line_input);
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_gtpu_endpoint_command, static) =
{
  .path = "upf gtpu endpoint",
  .short_help =
  "upf gtpu endpoint [ip <v4 address>] [ip6 <v6 address>] [nwi <name>]"
  " [src access | core | sgi | cp] [teid <teid>/<mask>] [del]",
  .function = upf_gtpu_endpoint_add_del_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_gtpu_endpoint_command_fn (vlib_main_t * vm,
				   unformat_input_t * main_input,
				   vlib_cli_command_t * cmd)
{
  upf_main_t *gtm = &upf_main;
  clib_error_t *error = NULL;
  upf_upip_res_t *res;

  /* TBD....
     if (unformat_user (main_input, unformat_line_input, line_input))
     {
     while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
     {
     if (unformat (line_input, "name %_%v%_", &s))
     {
     name = upf_name_to_labels (s);
     vec_free (s);
     }
     else
     {
     error = unformat_parse_error (line_input);
     unformat_free (line_input);
     goto done;
     }
     }

     unformat_free (line_input);
     }
   */

  pool_foreach (res, gtm->upip_res)
  {
    vlib_cli_output (vm, "[%d]: %U", res - gtm->upip_res,
		     format_gtpu_endpoint, res);
  }

  //done:
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_gtpu_endpoint_command, static) =
{
  .path = "show upf gtpu endpoint",
  .short_help =
  "show upf gtpu endpoint",
  .function = upf_show_gtpu_endpoint_command_fn,
};
/* *INDENT-ON* */

static int
upf_flows_out_cb (BVT (clib_bihash_kv) * kvp, void *arg)
{
  flowtable_main_t *fm = &flowtable_main;
  vlib_main_t *vm = (vlib_main_t *) arg;
  flow_entry_t *flow;

  clib_warning("[FATEMEH] Got packet: %d", 10000006);

  flow = pool_elt_at_index (fm->flows, kvp->value);
  vlib_cli_output (vm, "%U", format_flow, flow);

  return (BIHASH_WALK_CONTINUE);
}

static clib_error_t *
upf_show_session_command_fn (vlib_main_t * vm,
			     unformat_input_t * main_input,
			     vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  upf_main_t *gtm = &upf_main;
  clib_error_t *error = NULL;
  u64 cp_seid, up_seid;
  ip46_address_t cp_ip;
  u8 has_cp_f_seid = 0, has_up_seid = 0;
  upf_session_t *sess = NULL;
  int debug = 0;
#if FLOWTABLE_TODO
  u8 has_flows = 0;
#endif

  if (unformat_user (main_input, unformat_line_input, line_input))
    {
      while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
	{
	  if (unformat (line_input, "cp %U seid %lu",
			unformat_ip46_address, &cp_ip, IP46_TYPE_ANY,
			&cp_seid))
	    has_cp_f_seid = 1;
	  else if (unformat (line_input, "cp %U seid 0x%lx",
			     unformat_ip46_address, &cp_ip, IP46_TYPE_ANY,
			     &cp_seid))
	    has_cp_f_seid = 1;
	  else if (unformat (line_input, "up seid %lu", &up_seid))
	    has_up_seid = 1;
	  else if (unformat (line_input, "up seid 0x%lx", &up_seid))
	    has_up_seid = 1;
	  else if (unformat (line_input, "debug"))
	    debug = 1;
#if FLOWTABLE_TODO
	  else if (unformat (line_input, "%lu flows", &up_seid))
	    has_flows = 1;
	  else if (unformat (line_input, "0x%lx flows", &up_seid))
	    has_flows = 1;
#endif
	  else
	    {
	      error = unformat_parse_error (line_input);
	      unformat_free (line_input);
	      goto done;
	    }
	}

      unformat_free (line_input);
    }

#if FLOWTABLE_TODO
  if (has_flows)
    {
      if (!(sess = pfcp_lookup (up_seid)))
	{
	  error = clib_error_return (0, "Sessions 0x%lx not found", up_seid);
	  goto done;
	}

      BV (clib_bihash_foreach_key_value_pair)
	(&sess->fmt.flows_ht, upf_flows_out_cb, vm);
      goto done;
    }
#endif

  if (has_cp_f_seid)
    {
      error = clib_error_return (0, "CP F_SEID is not supported, yet");
      goto done;
    }

  if (has_up_seid)
    {
      if (!(sess = pfcp_lookup (up_seid)))
	{
	  error = clib_error_return (0, "Sessions %d not found", up_seid);
	  goto done;
	}

      vlib_cli_output (vm, "%U", format_pfcp_session, sess, PFCP_ACTIVE,
		       debug);
    }
  else
    {
      pool_foreach (sess, gtm->sessions)
      {
	vlib_cli_output (vm, "%U", format_pfcp_session, sess, PFCP_ACTIVE,
			 debug);
      }
    }

done:
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_session_command, static) =
{
  .path = "show upf session",
  .short_help =
  "show upf session",
  .function = upf_show_session_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_assoc_command_fn (vlib_main_t * vm,
			   unformat_input_t * main_input,
			   vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  upf_main_t *gtm = &upf_main;
  clib_error_t *error = NULL;
  u8 has_ip = 0, has_fqdn = 0;
  ip46_address_t node_ip;
  upf_node_assoc_t *node;
  u8 verbose = 0;
  u8 *fqdn = 0;

  if (unformat_user (main_input, unformat_line_input, line_input))
    {
      while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
	{
	  if (unformat (line_input, "ip %U",
			unformat_ip46_address, &node_ip, IP46_TYPE_ANY))
	    has_ip = 1;
	  else if (unformat (line_input, "fqdn %_%v%_", &fqdn))
	    has_fqdn = 1;
	  if (unformat (line_input, "verbose"))
	    verbose = 1;
	  else
	    {
	      error = unformat_parse_error (line_input);
	      unformat_free (line_input);
	      goto done;
	    }
	}

      unformat_free (line_input);
    }

  if (has_ip && has_fqdn)
    {
      error =
	clib_error_return (0,
			   "Only one selector is allowed, eith ip or fqdn");
      goto done;
    }

  if (has_ip && has_fqdn)
    {
      pfcp_node_id_t node_id;

      if (has_ip)
	{
	  node_id.type = ip46_address_is_ip4 (&node_ip) ? NID_IPv4 : NID_IPv6;
	  node_id.ip = node_ip;
	}
      if (has_fqdn)
	{
	  node_id.type = NID_FQDN;
	  node_id.fqdn = upf_name_to_labels (fqdn);
	}

      node = pfcp_get_association (&node_id);

      if (node_id.type == NID_FQDN)
	vec_free (node_id.fqdn);

      if (!node)
	{
	  error = clib_error_return (0, "Association not found");
	  goto done;
	}

      vlib_cli_output (vm, "%U", format_pfcp_node_association, node, verbose);
    }
  else
    {
      pool_foreach (node, gtm->nodes)
      {
	vlib_cli_output (vm, "%U", format_pfcp_node_association, node,
			 verbose);
      }
    }

done:
  if (fqdn)
    vec_free (fqdn);

  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_assoc_command, static) =
{
  .path = "show upf association",
  .short_help =
  "show upf association",
  .function = upf_show_assoc_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_flows_command_fn (vlib_main_t * vm,
			   unformat_input_t * input, vlib_cli_command_t * cmd)
{
  u32 cpu_index;
  flowtable_main_t *fm = &flowtable_main;

  clib_warning("[FATEMEH] Got packet: %d", 10000007);
  vlib_thread_main_t *tm = vlib_get_thread_main ();

  for (cpu_index = 0; cpu_index < tm->n_vlib_mains; cpu_index++)
    {
      flowtable_main_per_cpu_t *fmt = &fm->per_cpu[cpu_index];
      BV (clib_bihash_foreach_key_value_pair)
	(&fmt->flows_ht, upf_flows_out_cb, vm);
    }

  return NULL;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_flows_command, static) =
{
  .path = "show upf flows",
  .short_help = "show upf flows",
  .function = upf_show_flows_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_bihash_command_fn (vlib_main_t * vm,
			    unformat_input_t * main_input,
			    vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t *error = NULL;
  upf_main_t *sm = &upf_main;
  int verbose = 0;
  int hash = 0;

  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "detail"))
	verbose = 1;
      else if (unformat (line_input, "verbose"))
	verbose = 2;
      else if (unformat (line_input, "v4-tunnel-by-key"))
	hash = 1;
      else if (unformat (line_input, "v6-tunnel-by-key"))
	hash = 2;
      else if (unformat (line_input, "qer-by-id"))
	hash = 3;
      else if (unformat (line_input, "peer-index-by-ip"))
	hash = 4;
      else
	{
	  error = unformat_parse_error (line_input);
	  goto done;
	}
    }

  switch (hash)
    {
    case 1:
      vlib_cli_output (vm, "%U", format_bihash_8_8, &sm->v4_tunnel_by_key,
		       verbose);
      break;
    case 2:
      vlib_cli_output (vm, "%U", format_bihash_24_8, &sm->v6_tunnel_by_key,
		       verbose);
      break;
    case 3:
      vlib_cli_output (vm, "%U", format_bihash_8_8, &sm->qer_by_id, verbose);
      break;
    case 4:
      vlib_cli_output (vm, "%U", format_bihash_24_8, &sm->peer_index_by_ip,
		       verbose);
      break;
    default:
      error = clib_error_return (0, "Please specify an hash...");
      break;
    }

done:
  unformat_free (line_input);
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_bihash_command, static) =
{
  .path = "show upf bihash",
  .short_help =
  "show upf bihash <v4-tunnel-by-key | v6-tunnel-by-key | qer-by-id | peer-index-by-ip> [detail|verbose]",
  .function = upf_show_bihash_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_proxy_set_command_fn (vlib_main_t * vm, unformat_input_t * input,
			  vlib_cli_command_t * cmd)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
#define _(type, name) type name;
  foreach_upf_proxy_config_fields
#undef _
    u32 tmp32;

#define _(type, name) name = pm->name;
  foreach_upf_proxy_config_fields
#undef _
    while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "mss %d", &tmp32))
	mss = (u16) tmp32;
      else if (unformat (input, "fifo-size %U",
			 unformat_memory_size, &fifo_size))
	;
      else if (unformat (input, "max-fifo-size %U",
			 unformat_memory_size, &max_fifo_size))
	;
      else if (unformat (input, "high-watermark %d", &tmp32))
	high_watermark = (u8) tmp32;
      else if (unformat (input, "low-watermark %d", &tmp32))
	low_watermark = (u8) tmp32;
      else if (unformat (input, "prealloc-fifos %d", &prealloc_fifos))
	;
      else if (unformat (input, "private-segment-count %d",
			 &private_segment_count))
	;
      else if (unformat (input, "private-segment-size %U",
			 unformat_memory_size, &private_segment_size))
	;
      else
	return clib_error_return (0, "unknown input `%U'",
				  format_unformat_error, input);
    }

#define _(type, name) pm->name = name;
  foreach_upf_proxy_config_fields
#undef _
    return 0;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_proxy_set_command, static) =
{
  .path = "set upf proxy",
  .short_help = "set upf proxy [mss <nn>] [fifo-size <nn>[k|m]]"
      "[max-fifo-size <nn>[k|m]][high-watermark <nn>]"
      "[low-watermark <nn>][prealloc-fifos <nn>]"
      "[private-segment-size <mem>][private-segment-count <nn>]",
  .function = upf_proxy_set_command_fn,
};
/* *INDENT-ON* */

static clib_error_t *
upf_show_proxy_command_fn (vlib_main_t * vm,
			   unformat_input_t * main_input,
			   vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  upf_proxy_main_t *pm = &upf_proxy_main;
  clib_error_t *error = NULL;

  if (unformat_user (main_input, unformat_line_input, line_input))
    {
      while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
	{
	  error = unformat_parse_error (line_input);
	  unformat_free (line_input);
	  goto done;
	}

      unformat_free (line_input);
    }

  vlib_cli_output (vm, "MSS: %u\n"
		   "FIFO Size: %U\n"
		   "Max FIFO Size: %U\n"
		   "Hi/Lo Watermark: %u %% / %u %%\n"
		   "Prealloc FIFOs: %u\n"
		   "Private Segment Count: %u\n"
		   "Private Segment Size: %U\n",
		   pm->mss,
		   format_memory_size, pm->fifo_size,
		   format_memory_size, pm->max_fifo_size,
		   pm->high_watermark, pm->low_watermark,
		   pm->prealloc_fifos,
		   pm->private_segment_count,
		   format_memory_size, pm->private_segment_size);

done:
  return error;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_proxy_command, static) =
{
  .path = "show upf proxy",
  .short_help = "show upf proxy",
  .function = upf_show_proxy_command_fn,
};

/* *INDENT-ON* */

static clib_error_t *
upf_show_proxy_session_command_fn (vlib_main_t * vm,
				   unformat_input_t * main_input,
				   vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  upf_proxy_main_t *pm = &upf_proxy_main;
  clib_error_t *error = NULL;
  upf_proxy_session_t *ps;

  if (unformat_user (main_input, unformat_line_input, line_input))
    {
      while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
	{
	  error = unformat_parse_error (line_input);
	  unformat_free (line_input);
	  goto done;
	}

      unformat_free (line_input);
    }

  pool_foreach (ps, pm->sessions)
  {
    vlib_cli_output (vm, "%U\n", format_upf_proxy_session, ps);
  }

done:
  return error;
}


/* *INDENT-OFF* */
VLIB_CLI_COMMAND (upf_show_proxy_session_command, static) =
{
  .path = "show upf proxy sessions",
  .short_help = "show upf proxy sessions",
  .function = upf_show_proxy_session_command_fn,
};
/* *INDENT-ON* */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
