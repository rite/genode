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

#ifndef _NETWORK_H_
#define _NETWORK_H_

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <os/reporter.h>

/* local includes */
#include <model/child_exit_state.h>
#include <view/network_dialog.h>
#include <gui.h>
#include <runtime.h>
#include <keyboard_focus.h>

namespace Sculpt_manager { struct Network; }


struct Sculpt_manager::Network : Network_dialog::Action
{
	Env &_env;

	Allocator &_alloc;

	Dialog::Generator &_dialog_generator;

	Runtime_config_generator &_runtime_config_generator;

	Runtime_info const &_runtime_info;

	Nic_target _nic_target { };
	Nic_state  _nic_state  { };

	bool _nic_router_config_up_to_date = false;

	Wpa_passphrase wpa_passphrase { };

	bool _use_nic_drv  = false;
	bool _use_wifi_drv = false;

	Attached_rom_dataspace _wlan_accesspoints_rom {
		_env, "report -> runtime/wifi_drv/wlan_accesspoints" };

	Attached_rom_dataspace _wlan_state_rom {
		_env, "report -> runtime/wifi_drv/wlan_state" };

	Attached_rom_dataspace _nic_router_state_rom {
		_env, "report -> runtime/nic_router/state" };

	Attached_rom_dataspace _manual_nic_router_config_rom {
		_env, "config -> nic_router" };

	Expanding_reporter _nic_router_config { _env, "config", "nic_router_config" };

	void _generate_nic_router_config();

	Access_points _access_points { };

	Wifi_connection _wifi_connection = Wifi_connection::disconnected_wifi_connection();

	void gen_runtime_start_nodes(Xml_generator &xml) const;

	bool ready() const { return _nic_target.ready() && _nic_state.ready(); }

	void handle_key_press(Codepoint);

	void _handle_wlan_accesspoints();
	void _handle_wlan_state();
	void _handle_nic_router_state();
	void _handle_manual_nic_router_config();

	Signal_handler<Network> _wlan_accesspoints_handler {
		_env.ep(), *this, &Network::_handle_wlan_accesspoints };

	Signal_handler<Network> _wlan_state_handler {
		_env.ep(), *this, &Network::_handle_wlan_state };

	Signal_handler<Network> _nic_router_state_handler {
		_env.ep(), *this, &Network::_handle_nic_router_state };

	Signal_handler<Network> _manual_nic_router_config_handler {
		_env.ep(), *this, &Network::_handle_manual_nic_router_config };

	Network_dialog dialog {
		_env, _dialog_generator, _nic_target, _access_points,
		_wifi_connection, _nic_state, wpa_passphrase };

	Expanding_reporter _wlan_config { _env, "selected_network", "wlan_config" };

	/**
	 * Network_dialog::Action interface
	 */
	void nic_target(Nic_target::Type const type) override
	{
		/*
		 * Start drivers on first use but never remove them to avoid
		 * driver-restarting issues.
		 */
		if (type == Nic_target::WIFI)  _use_wifi_drv = true;
		if (type == Nic_target::WIRED) _use_nic_drv  = true;

		if (type != _nic_target.type) {
			_nic_target.type = type;
			_generate_nic_router_config();
			_runtime_config_generator.generate_runtime_config();
			_dialog_generator.generate_dialog();
		}
	}

	void wifi_connect(Access_point::Bssid bssid) override
	{
		_access_points.for_each([&] (Access_point const &ap) {
			if (ap.bssid != bssid)
				return;

			_wifi_connection.ssid  = ap.ssid;
			_wifi_connection.bssid = ap.bssid;
			_wifi_connection.state = Wifi_connection::CONNECTING;

			_wlan_config.generate([&] (Xml_generator &xml) {
				xml.attribute("ssid", ap.ssid);
				if (ap.protection == Access_point::WPA_PSK) {
					xml.attribute("protection", "WPA-PSK");
					String<128> const psk(wpa_passphrase);
					xml.attribute("psk", psk);
				}
			});
		});
	}

	void wifi_disconnect() override
	{
		/*
		 * Reflect state change immediately to the user interface even
		 * if the wifi driver will take a while to perform the disconnect.
		 */
		_wifi_connection = Wifi_connection::disconnected_wifi_connection();

		_wlan_config.generate([&] (Xml_generator &xml) {

			/* generate attributes to ease subsequent manual tweaking */
			xml.attribute("ssid", "");
			xml.attribute("protection", "WPA-PSK");
			xml.attribute("psk", "");
		});

		_runtime_config_generator.generate_runtime_config();
	}

	Network(Env &env, Allocator &alloc, Dialog::Generator &dialog_generator,
	        Runtime_config_generator &runtime_config_generator,
	        Runtime_info const &runtime_info)
	:
		_env(env), _alloc(alloc), _dialog_generator(dialog_generator),
		_runtime_config_generator(runtime_config_generator),
		_runtime_info(runtime_info)
	{
		_generate_nic_router_config();

		/*
		 * Subscribe to reports
		 */
		_wlan_accesspoints_rom       .sigh(_wlan_accesspoints_handler);
		_wlan_state_rom              .sigh(_wlan_state_handler);
		_nic_router_state_rom        .sigh(_nic_router_state_handler);
		_manual_nic_router_config_rom.sigh(_manual_nic_router_config_handler);
	}
};

#endif /* _NETWORK_H_ */
