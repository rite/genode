/*
 * \brief  Reflects an effective domain configuration node
 * \author Martin Stein
 * \date   2016-08-19
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _DOMAIN_H_
#define _DOMAIN_H_

/* local includes */
#include <forward_rule.h>
#include <transport_rule.h>
#include <nat_rule.h>
#include <ip_rule.h>
#include <arp_cache.h>
#include <port_allocator.h>
#include <pointer.h>
#include <ipv4_config.h>
#include <dhcp_server.h>
#include <interface.h>

/* Genode includes */
#include <util/avl_string.h>
#include <util/reconstructible.h>

namespace Genode {

	class Xml_generator;
	class Allocator;
}

namespace Net {

	class Interface;
	class Configuration;
	class Domain_avl_member;
	class Domain_base;
	class Domain;
	class Domain_tree;
	using Domain_name = Genode::String<160>;
}


class Net::Domain_avl_member : public Genode::Avl_string_base
{
	private:

		Domain &_domain;

	public:

		Domain_avl_member(Domain_name const &name,
		                  Domain            &domain);


		/***************
		 ** Accessors **
		 ***************/

		Domain &domain() const { return _domain; }
};


class Net::Domain_base
{
	protected:

		Domain_name const _name;

		Domain_base(Genode::Xml_node const node);
};


class Net::Domain : public Domain_base,
                    public List<Domain>::Element
{
	private:

		Domain_avl_member                     _avl_member;
		Configuration                        &_config;
		Genode::Xml_node                      _node;
		Genode::Allocator                    &_alloc;
		Ip_rule_list                          _ip_rules             { };
		Forward_rule_tree                     _tcp_forward_rules    { };
		Forward_rule_tree                     _udp_forward_rules    { };
		Transport_rule_list                   _tcp_rules            { };
		Transport_rule_list                   _udp_rules            { };
		Ip_rule_list                          _icmp_rules           { };
		Port_allocator                        _tcp_port_alloc       { };
		Port_allocator                        _udp_port_alloc       { };
		Port_allocator                        _icmp_port_alloc      { };
		Nat_rule_tree                         _nat_rules            { };
		Interface_list                        _interfaces           { };
		unsigned long                         _interface_cnt        { 0 };
		Pointer<Dhcp_server>                  _dhcp_server          { };
		Genode::Reconstructible<Ipv4_config>  _ip_config;
		bool                            const _ip_config_dynamic    { !ip_config().valid };
		List<Domain>                          _ip_config_dependents { };
		Arp_cache                             _arp_cache            { *this };
		Arp_waiter_list                       _foreign_arp_waiters  { };
		Link_side_tree                        _tcp_links            { };
		Link_side_tree                        _udp_links            { };
		Link_side_tree                        _icmp_links           { };
		Genode::size_t                        _tx_bytes             { 0 };
		Genode::size_t                        _rx_bytes             { 0 };
		bool                            const _verbose_packets      { false };
		Genode::Session_label           const _label;

		void _read_forward_rules(Genode::Cstring  const &protocol,
		                         Domain_tree            &domains,
		                         Genode::Xml_node const  node,
		                         char             const *type,
		                         Forward_rule_tree      &rules);

		void _read_transport_rules(Genode::Cstring  const &protocol,
		                           Domain_tree            &domains,
		                           Genode::Xml_node const  node,
		                           char             const *type,
		                           Transport_rule_list    &rules);

		void __FIXME__dissolve_foreign_arp_waiters();

	public:

		struct Invalid          : Genode::Exception { };
		struct Ip_config_static : Genode::Exception { };
		struct No_next_hop      : Genode::Exception { };

		Domain(Configuration          &config,
		       Genode::Xml_node const  node,
		       Genode::Allocator      &alloc);

		~Domain();

		void init(Domain_tree &domains);

		Ipv4_address const &next_hop(Ipv4_address const &ip) const;

		void ip_config(Ipv4_config const &ip_config);

		void ip_config(Ipv4_address ip,
		               Ipv4_address subnet_mask,
		               Ipv4_address gateway,
		               Ipv4_address dns_server);

		void discard_ip_config();

		void try_reuse_ip_config(Domain const &domain);

		Link_side_tree &links(L3_protocol const protocol);

		void attach_interface(Interface &interface);

		void detach_interface(Interface &interface);

		void raise_rx_bytes(Genode::size_t bytes) { _rx_bytes += bytes; }

		void raise_tx_bytes(Genode::size_t bytes) { _tx_bytes += bytes; }

		void report(Genode::Xml_generator &xml);


		/*********
		 ** log **
		 *********/

		void print(Genode::Output &output) const;


		/***************
		 ** Accessors **
		 ***************/

		bool                         verbose_packets() const { return _verbose_packets; }
		Genode::Session_label const &label()           const { return _label; }
		Ipv4_config           const &ip_config()       const { return *_ip_config; }
		List<Domain>                &ip_config_dependents()  { return _ip_config_dependents; }
		Domain_name           const &name()            const { return _name; }
		Ip_rule_list                &ip_rules()              { return _ip_rules; }
		Forward_rule_tree           &tcp_forward_rules()     { return _tcp_forward_rules; }
		Forward_rule_tree           &udp_forward_rules()     { return _udp_forward_rules; }
		Transport_rule_list         &tcp_rules()             { return _tcp_rules; }
		Transport_rule_list         &udp_rules()             { return _udp_rules; }
		Ip_rule_list                &icmp_rules()            { return _icmp_rules; }
		Nat_rule_tree               &nat_rules()             { return _nat_rules; }
		Interface_list              &interfaces()            { return _interfaces; }
		Configuration               &config()          const { return _config; }
		Domain_avl_member           &avl_member()            { return _avl_member; }
		Dhcp_server                 &dhcp_server();
		Arp_cache                   &arp_cache()             { return _arp_cache; }
		Arp_waiter_list             &foreign_arp_waiters()   { return _foreign_arp_waiters; }
		Link_side_tree              &tcp_links()             { return _tcp_links; }
		Link_side_tree              &udp_links()             { return _udp_links; }
		Link_side_tree              &icmp_links()            { return _icmp_links; }
};


struct Net::Domain_tree : Genode::Avl_tree<Genode::Avl_string_base>
{
	using Avl_tree = Genode::Avl_tree<Genode::Avl_string_base>;

	struct No_match : Genode::Exception { };

	static Domain &domain(Genode::Avl_string_base const &node);

	Domain &find_by_name(Domain_name name);

	template <typename FUNC>
	void for_each(FUNC && functor) const {
		Avl_tree::for_each([&] (Genode::Avl_string_base const &node) {
			functor(domain(node));
		});
	}

	void insert(Domain &domain) { Avl_tree::insert(&domain.avl_member()); }

	void destroy_each(Genode::Deallocator &dealloc);
};

#endif /* _DOMAIN_H_ */
