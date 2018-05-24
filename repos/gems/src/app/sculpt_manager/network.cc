/*
 * \brief  Sculpt network management
 * \author Norman Feske
 * \date   2018-04-30
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* local includes */
#include <network.h>


void Sculpt_manager::Network::handle_key_press(Codepoint code)
{
	enum { BACKSPACE = 8, ENTER = 10 };
	if (code.value == BACKSPACE)
		wpa_passphrase.remove_last_character();
	else if (code.value == ENTER)
		wifi_connect(dialog.selected_ap());
	else if (code.value != 0)
		wpa_passphrase.append_character(code);

	/*
	 * Keep updating the passphase when pressing keys after
	 * clicking the connect button once.
	 */
	if (_wifi_connection.state == Wifi_connection::CONNECTING)
		wifi_connect(_wifi_connection.bssid);

	_dialog_generator.generate_dialog();
}


void Sculpt_manager::Network::_generate_nic_router_config()
{
	if ((_nic_target.wired() && !_runtime_info.present_in_runtime("nic_drv"))
	 || (_nic_target.wifi()  && !_runtime_info.present_in_runtime("wifi_drv"))) {

		/* defer NIC router reconfiguration until the needed uplink is present */
		_nic_router_config_up_to_date = false;
		return;
	}

	_nic_router_config_up_to_date = true;

	/*
	 * If a manually managed 'config/nic_router' is provided, copy its
	 * content to the effective config at 'config/managed/nic_router'.
	 * Note that attributes of the top-level node are not copied but the
	 * 'verbose_domain_state' is set.
	 */
	if (_nic_target.manual()) {
		_nic_router_config.generate([&] (Xml_generator &xml) {
			xml.attribute("verbose_domain_state", "yes");
			Xml_node const manual_config = _manual_nic_router_config_rom.xml();
			if (manual_config.content_size())
				xml.append(manual_config.content_base(), manual_config.content_size());
		});
		return;
	}

	if (!_nic_target.nic_router_needed()) {
		_nic_router_config.generate([&] (Xml_generator &xml) {
			xml.attribute("verbose_domain_state", "yes"); });
		return;
	}

	_nic_router_config.generate([&] (Xml_generator &xml) {
		xml.attribute("verbose_domain_state", "yes");

		xml.node("report", [&] () {
			xml.attribute("interval_sec",    "5");
			xml.attribute("bytes",           "yes");
			xml.attribute("config",          "yes");
			xml.attribute("config_triggers", "yes");
		});

		xml.node("default-policy", [&] () {
			xml.attribute("domain", "default"); });

		if (_nic_target.type != Nic_target::LOCAL) {
			gen_named_node(xml, "domain", "uplink", [&] () {
				switch (_nic_target.type) {
				case Nic_target::WIRED: xml.attribute("label", "wired"); break;
				case Nic_target::WIFI:  xml.attribute("label", "wifi");  break;
				default: break;
				}
				xml.node("nat", [&] () {
					xml.attribute("domain",    "default");
					xml.attribute("tcp-ports", "1000");
					xml.attribute("udp-ports", "1000");
					xml.attribute("icmp-ids",  "1000");
				});
			});
		}

		gen_named_node(xml, "domain", "default", [&] () {
			xml.attribute("interface", "10.0.1.1/24");

			xml.node("dhcp-server", [&] () {
				xml.attribute("ip_first",        "10.0.1.2");
				xml.attribute("ip_last",         "10.0.1.200");
				xml.attribute("dns_server_from", "uplink"); });

			xml.node("tcp", [&] () {
				xml.attribute("dst", "0.0.0.0/0");
				xml.node("permit-any", [&] () {
					xml.attribute("domain", "uplink"); }); });

			xml.node("udp", [&] () {
				xml.attribute("dst", "0.0.0.0/0");
				xml.node("permit-any", [&] () {
					xml.attribute("domain", "uplink"); }); });

			xml.node("icmp", [&] () {
				xml.attribute("dst", "0.0.0.0/0");
				xml.attribute("domain", "uplink"); });
		});
	});
}


void Sculpt_manager::Network::_handle_wlan_accesspoints()
{
	bool const initial_scan = !_wlan_accesspoints_rom.xml().has_sub_node("accesspoint");

	_wlan_accesspoints_rom.update();

	/* suppress updating the list while the access-point list is hovered */
	if (!initial_scan && dialog.ap_list_hovered())
		return;

	Access_point_update_policy policy(_alloc);
	_access_points.update_from_xml(policy, _wlan_accesspoints_rom.xml());
	_dialog_generator.generate_dialog();
}


void Sculpt_manager::Network::_handle_wlan_state()
{
	_wlan_state_rom.update();
	_wifi_connection = Wifi_connection::from_xml(_wlan_state_rom.xml());
	_dialog_generator.generate_dialog();
}


void Sculpt_manager::Network::_handle_nic_router_state()
{
	_nic_router_state_rom.update();

	Nic_state const old_nic_state = _nic_state;
	_nic_state = Nic_state::from_xml(_nic_router_state_rom.xml());
	_dialog_generator.generate_dialog();

	/* if the nic state becomes ready, consider spawning the update subsystem */
	if (old_nic_state.ready() != _nic_state.ready())
		_runtime_config_generator.generate_runtime_config();
}


void Sculpt_manager::Network::_handle_manual_nic_router_config()
{
	_manual_nic_router_config_rom.update();

	Xml_node const config = _manual_nic_router_config_rom.xml();

	_nic_target.policy = config.has_type("empty")
	                   ? Nic_target::MANAGED : Nic_target::MANUAL;

	/* obtain uplink information from configuration */
	Nic_target::Type target = Nic_target::LOCAL;
	target = Nic_target::LOCAL;

	if (!config.has_sub_node("domain"))
		target = Nic_target::OFF;

	config.for_each_sub_node("domain", [&] (Xml_node domain) {

		/* skip non-uplink domains */
		if (domain.attribute_value("name", String<16>()) != "uplink")
			return;

		if (domain.attribute_value("label", String<16>()) == "wired")
			target = Nic_target::WIRED;

		if (domain.attribute_value("label", String<16>()) == "wifi")
			target = Nic_target::WIFI;
	});

	nic_target(target);
	_generate_nic_router_config();
	_runtime_config_generator.generate_runtime_config();
	_dialog_generator.generate_dialog();
}


void Sculpt_manager::Network::gen_runtime_start_nodes(Xml_generator &xml) const
{
	if (_use_nic_drv)
		xml.node("start", [&] () { gen_nic_drv_start_content(xml); });

	if (_use_wifi_drv)
		xml.node("start", [&] () { gen_wifi_drv_start_content(xml); });

	if (_nic_target.type != Nic_target::OFF)
		xml.node("start", [&] () {
			gen_nic_router_start_content(xml); });
}
