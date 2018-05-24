/*
 * \brief  Sculpt system manager
 * \author Norman Feske
 * \date   2018-04-30
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <os/reporter.h>
#include <nitpicker_session/connection.h>

/* included from depot_deploy tool */
#include <children.h>

/* local includes */
#include <model/child_exit_state.h>
#include <model/discovery_state.h>
#include <view/storage_dialog.h>
#include <view/network_dialog.h>
#include <gui.h>
#include <nitpicker.h>
#include <runtime.h>
#include <keyboard_focus.h>

namespace Sculpt_manager { struct Main; }

struct Sculpt_manager::Main : Input_event_handler,
                              Dialog::Generator,
                              Storage_dialog::Action,
                              Network_dialog::Action
{
	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Nitpicker::Connection _nitpicker { _env, "input" };

	Signal_handler<Main> _input_handler {
		_env.ep(), *this, &Main::_handle_input };

	void _handle_input()
	{
		_nitpicker.input()->for_each_event([&] (Input::Event const &ev) {
			handle_input_event(ev); });
	}

	Expanding_reporter _fonts_config { _env, "config", "fonts_config" };

	Signal_handler<Main> _nitpicker_mode_handler {
		_env.ep(), *this, &Main::_handle_nitpicker_mode };

	void _handle_nitpicker_mode();

	Attached_rom_dataspace _nitpicker_hover { _env, "nitpicker_hover" };

	Signal_handler<Main> _nitpicker_hover_handler {
		_env.ep(), *this, &Main::_handle_nitpicker_hover };

	void _handle_nitpicker_hover();


	/***************************
	 ** Configuration loading **
	 ***************************/

	Prepare_version _prepare_version   { 0 };
	Prepare_version _prepare_completed { 0 };

	bool _prepare_in_progress() const
	{
		return _prepare_version.value != _prepare_completed.value;
	}


	/*************
	 ** Storage **
	 *************/

	Attached_rom_dataspace _block_devices_rom { _env, "report -> drivers/block_devices" };

	Attached_rom_dataspace _usb_active_config_rom { _env, "report -> drivers/usb_active_config" };

	Storage_devices _storage_devices { };

	Ram_fs_state _ram_fs_state { };

	Storage_target _sculpt_partition { };

	Discovery_state _discovery_state { };

	File_browser_version _file_browser_version { 0 };

	Storage_dialog _storage_dialog {
		_env, *this, _storage_devices, _ram_fs_state, _sculpt_partition };

	void _handle_storage_devices_update();

	Signal_handler<Main> _storage_device_update_handler {
		_env.ep(), *this, &Main::_handle_storage_devices_update };

	template <typename FN>
	void _apply_partition(Storage_target const &target, FN const &fn)
	{
		_storage_devices.for_each([&] (Storage_device &device) {

			if (target.device != device.label)
				return;

			device.for_each_partition([&] (Partition &partition) {

				bool const whole_device = !target.partition.valid()
				                       && !partition.number.valid();

				bool const partition_matches = (device.label == target.device)
				                            && (partition.number == target.partition);

				if (whole_device || partition_matches) {
					fn(partition);
					_generate_runtime_config();
				}
			});
		});
	}


	/**
	 * Storage_dialog::Action interface
	 */
	void format(Storage_target const &target) override
	{
		_apply_partition(target, [&] (Partition &partition) {
			partition.format_in_progress = true; });
	}

	void cancel_format(Storage_target const &target) override
	{
		_apply_partition(target, [&] (Partition &partition) {

			if (partition.format_in_progress) {
				partition.file_system.type   = File_system::UNKNOWN;
				partition.format_in_progress = false;
			}
			_storage_dialog.reset_operation();
		});
	}

	void expand(Storage_target const &target) override
	{
		_apply_partition(target, [&] (Partition &partition) {
			partition.gpt_expand_in_progress = true; });
	}

	void cancel_expand(Storage_target const &target) override
	{
		_apply_partition(target, [&] (Partition &partition) {

			if (partition.expand_in_progress()) {
				partition.file_system.type       = File_system::UNKNOWN;
				partition.gpt_expand_in_progress = false;
				partition.fs_resize_in_progress  = false;
			}
			_storage_dialog.reset_operation();
		});
	}

	void check(Storage_target const &target) override
	{
		_apply_partition(target, [&] (Partition &partition) {
			partition.check_in_progress = true; });
	}

	void toggle_file_browser(Storage_target const &target) override
	{
		File_browser_version const orig_version = _file_browser_version;

		if (target.ram_fs()) {
			_ram_fs_state.inspected = !_ram_fs_state.inspected;
			_file_browser_version.value++;
		}

		_apply_partition(target, [&] (Partition &partition) {
			partition.file_system_inspected = !partition.file_system_inspected;
			_file_browser_version.value++;
		});

		if (orig_version.value == _file_browser_version.value)
			return;

		_generate_runtime_config();
	}

	void toggle_default_storage_target(Storage_target const &target) override
	{
		_apply_partition(target, [&] (Partition &partition) {
			partition.toggle_default_label(); });
	}

	void use(Storage_target const &target) override
	{
		_sculpt_partition = target;

		/* trigger loading of the configuration from the sculpt partition */
		_prepare_version.value++;

		/* ignore stale query results */
		_query_version.value++;

		_children.apply_config(Xml_node("<config/>"));

		_generate_runtime_config();
	}

	void reset_ram_fs() override
	{
		_ram_fs_state.ram_quota = Ram_fs_state::initial_ram();
		_ram_fs_state.version.value++;

		_storage_dialog.reset_operation();
		_generate_runtime_config();
	}


	/*************
	 ** Network **
	 *************/

	Nic_target _nic_target { };
	Nic_state  _nic_state  { };

	bool _nic_router_config_up_to_date = false;

	Wpa_passphrase _wpa_passphrase { };

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

	void _handle_wlan_accesspoints();
	void _handle_wlan_state();
	void _handle_nic_router_state();
	void _handle_manual_nic_router_config();

	Signal_handler<Main> _wlan_accesspoints_handler {
		_env.ep(), *this, &Main::_handle_wlan_accesspoints };

	Signal_handler<Main> _wlan_state_handler {
		_env.ep(), *this, &Main::_handle_wlan_state };

	Signal_handler<Main> _nic_router_state_handler {
		_env.ep(), *this, &Main::_handle_nic_router_state };

	Signal_handler<Main> _manual_nic_router_config_handler {
		_env.ep(), *this, &Main::_handle_manual_nic_router_config };

	Network_dialog _network_dialog {
		_env, *this, _nic_target, _access_points, _wifi_connection, _nic_state,
		_wpa_passphrase };

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
			_generate_runtime_config();
			generate_dialog();
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
					String<128> const psk(_wpa_passphrase);
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

		_generate_runtime_config();
	}


	/************
	 ** Update **
	 ************/

	Attached_rom_dataspace _update_state_rom {
		_env, "report -> runtime/update/state" };

	void _handle_update_state();

	Signal_handler<Main> _update_state_handler {
		_env.ep(), *this, &Main::_handle_update_state };

	Expanding_reporter _installation_reporter { _env, "installation", "installation"};


	/************
	 ** Deploy **
	 ************/

	typedef String<16> Arch;
	Arch _arch { };

	struct Query_version { unsigned value; } _query_version { 0 };

	struct Depot_rom_state { Ram_quota ram_quota; };

	Depot_rom_state _depot_rom_state { 32*1024*1024 };

	Attached_rom_dataspace _manual_deploy_rom { _env, "config -> deploy" };

	Attached_rom_dataspace _blueprint_rom { _env, "report -> runtime/depot_query/blueprint" };

	Expanding_reporter _depot_query_reporter { _env, "query", "depot_query"};

	Depot_deploy::Children _children { _heap };

	void _handle_deploy();

	void _handle_manual_deploy()
	{
		_manual_deploy_rom.update();
		_query_version.value++;
		_handle_deploy();
	}

	void _handle_blueprint()
	{
		_blueprint_rom.update();
		_handle_deploy();
	}

	Signal_handler<Main> _manual_deploy_handler {
		_env.ep(), *this, &Main::_handle_manual_deploy };

	Signal_handler<Main> _blueprint_handler {
		_env.ep(), *this, &Main::_handle_blueprint };


	/************
	 ** Global **
	 ************/

	Gui _gui { _env };

	Expanding_reporter _dialog_reporter { _env, "dialog", "storage_dialog" };

	Attached_rom_dataspace _hover_rom { _env, "storage_view_hover" };

	Signal_handler<Main> _hover_handler {
		_env.ep(), *this, &Main::_handle_hover };

	struct Hovered { enum Dialog { NONE, STORAGE, NETWORK } value; };

	Hovered::Dialog _hovered_dialog { Hovered::NONE };

	template <typename FN>
	void _apply_to_hovered_dialog(Hovered::Dialog dialog, FN const &fn)
	{
		if (dialog == Hovered::STORAGE) fn(_storage_dialog);
		if (dialog == Hovered::NETWORK) fn(_network_dialog);
	}

	void _handle_hover();

	void _gen_download_status(Xml_generator &, Xml_node update_state) const;

	/**
	 * Dialog::Generator interface
	 */
	void generate_dialog() override
	{
		_dialog_reporter.generate([&] (Xml_generator &xml) {

			xml.node("vbox", [&] () {
				gen_named_node(xml, "frame", "logo", [&] () {
					xml.node("float", [&] () {
						xml.node("frame", [&] () {
							xml.attribute("style", "logo"); }); }); });

				_storage_dialog.generate(xml);
				_network_dialog.generate(xml);

				gen_named_node(xml, "frame", "runtime", [&] () {
					xml.node("vbox", [&] () {
						gen_named_node(xml, "label", "title", [&] () {
							xml.attribute("text", "Runtime");
							xml.attribute("font", "title/regular");
						});

						Xml_node state = _update_state_rom.xml();
						if (state.has_sub_node("archive"))
							_gen_download_status(xml, state);
					});
				});
			});
		});
	}

	Attached_rom_dataspace _runtime_state { _env, "report -> runtime/state" };

	/**
	 * Return true if specified child is present in the runtime subsystem
	 */
	bool _present_in_runtime(Start_name const &name)
	{
		bool present = false;
		_runtime_state.xml().for_each_sub_node("child", [&] (Xml_node child) {
			if (child.attribute_value("name", Start_name()) == name)
				present = true; });
		return present;
	}

	Expanding_reporter _runtime_config { _env, "config", "runtime_config" };

	inline void _generate_runtime_config(Xml_generator &) const;

	void _generate_runtime_config()
	{
		_runtime_config.generate([&] (Xml_generator &xml) {
			_generate_runtime_config(xml); });
	}

	Signal_handler<Main> _runtime_state_handler {
		_env.ep(), *this, &Main::_handle_runtime_state };

	void _handle_runtime_state();

	Keyboard_focus _keyboard_focus { _env, _network_dialog, _wpa_passphrase };

	/**
	 * Input_event_handler interface
	 */
	void handle_input_event(Input::Event const &ev) override
	{
		if (ev.key_press(Input::BTN_LEFT)) {
			if (_hovered_dialog == Hovered::STORAGE) _storage_dialog.click(*this);
			if (_hovered_dialog == Hovered::NETWORK) _network_dialog.click(*this);
		}

		if (ev.key_release(Input::BTN_LEFT))
			_storage_dialog.clack(*this);

		/*
		 * Insert WPA passphrase
		 */
		if (_keyboard_focus.target == Keyboard_focus::WPA_PASSPHRASE) {
			ev.handle_press([&] (Input::Keycode, Codepoint code) {

				enum { BACKSPACE = 8, ENTER = 10 };
				if (code.value == BACKSPACE)
					_wpa_passphrase.remove_last_character();
				else if (code.value == ENTER)
					wifi_connect(_network_dialog.selected_ap());
				else if (code.value != 0)
					_wpa_passphrase.append_character(code);

				/*
				 * Keep updating the passphase when pressing keys after
				 * clicking the connect button once.
				 */
				if (_wifi_connection.state == Wifi_connection::CONNECTING)
					wifi_connect(_wifi_connection.bssid);

				generate_dialog();
			});
		}

		if (ev.press())
			_keyboard_focus.update();
	}

	Nitpicker::Root _gui_nitpicker { _env, _heap, *this };

	Main(Env &env) : _env(env)
	{
		_runtime_state.sigh(_runtime_state_handler);

		_nitpicker.input()->sigh(_input_handler);
		_nitpicker.mode_sigh(_nitpicker_mode_handler);

		/*
		 * Adjust GUI parameters to initial nitpicker mode
		 */
		_handle_nitpicker_mode();

		/*
		 * Generate initial configurations
		 */
		wifi_disconnect();
		_generate_nic_router_config();

		/*
		 * Subscribe to reports
		 */
		_block_devices_rom           .sigh(_storage_device_update_handler);
		_usb_active_config_rom       .sigh(_storage_device_update_handler);
		_wlan_accesspoints_rom       .sigh(_wlan_accesspoints_handler);
		_wlan_state_rom              .sigh(_wlan_state_handler);
		_nic_router_state_rom        .sigh(_nic_router_state_handler);
		_update_state_rom            .sigh(_update_state_handler);
		_manual_deploy_rom           .sigh(_manual_deploy_handler);
		_blueprint_rom               .sigh(_blueprint_handler);
		_nitpicker_hover             .sigh(_nitpicker_hover_handler);
		_hover_rom                   .sigh(_hover_handler);
		_manual_nic_router_config_rom.sigh(_manual_nic_router_config_handler);

		/*
		 * Import initial report content
		 */
		_handle_nitpicker_hover();
		_handle_storage_devices_update();
		_handle_deploy();
		_handle_manual_nic_router_config();

		_generate_runtime_config();

		generate_dialog();

		_gui.generate_config();
	}
};


void Sculpt_manager::Main::_gen_download_status(Xml_generator &xml, Xml_node state) const
{
	gen_named_node(xml, "frame", "downloads", [&] () {
		xml.node("vbox", [&] () {

			xml.node("label", [&] () {
				xml.attribute("text", "Download"); });

			unsigned count = 0;
			state.for_each_sub_node("archive", [&] (Xml_node archive) {
				gen_named_node(xml, "hbox", String<10>(count++), [&] () {
					gen_named_node(xml, "float", "left", [&] () {
						xml.attribute("west", "yes");
						typedef String<50> Path;
						xml.node("label", [&] () {
							xml.attribute("text", archive.attribute_value("path", Path()));
							xml.attribute("font", "annotation/regular");
						});
					});

					typedef String<16> Info;

					Info        info  = archive.attribute_value("state", Info());
					float const total = archive.attribute_value("total", 0.0);
					float const now   = archive.attribute_value("now",   0.0);

					if (info == "download") {
						if (total > 0.0)
							info = Info((unsigned)((100*now)/total), "%");
						else
							info = Info("fetch");
					}

					gen_named_node(xml, "float", "right", [&] () {
						xml.attribute("east", "yes");
						xml.node("label", [&] () {
							xml.attribute("text", info);
							xml.attribute("font", "annotation/regular");
						});
					});
				});
			});
		});
	});
}


void Sculpt_manager::Main::_handle_nitpicker_mode()
{
	Framebuffer::Mode const mode = _nitpicker.mode();

	_fonts_config.generate([&] (Xml_generator &xml) {
		xml.node("vfs", [&] () {
			gen_named_node(xml, "rom", "Vera.ttf");
			gen_named_node(xml, "rom", "VeraMono.ttf");
			gen_named_node(xml, "dir", "fonts", [&] () {

				auto gen_ttf_dir = [&] (char const *dir_name,
				                        char const *ttf_path, float size_px) {

					gen_named_node(xml, "dir", dir_name, [&] () {
						gen_named_node(xml, "ttf", "regular", [&] () {
							xml.attribute("path",    ttf_path);
							xml.attribute("size_px", size_px);
							xml.attribute("cache",   "256K");
						});
					});
				};

				float const text_size = (float)mode.height() / 60.0;

				gen_ttf_dir("title",      "/Vera.ttf",     text_size*1.25);
				gen_ttf_dir("text",       "/Vera.ttf",     text_size);
				gen_ttf_dir("annotation", "/Vera.ttf",     text_size*0.8);
				gen_ttf_dir("monospace",  "/VeraMono.ttf", text_size);
			});
		});
		xml.node("default-policy", [&] () { xml.attribute("root", "/fonts"); });
	});

	_gui.version.value++;
	_gui.generate_config();
}


void Sculpt_manager::Main::_handle_hover()
{
	_hover_rom.update();
	Xml_node const hover = _hover_rom.xml();

	Hovered::Dialog const orig_hovered_dialog = _hovered_dialog;

	typedef String<32> Top_level_frame;
	Top_level_frame const top_level_frame =
		query_attribute<Top_level_frame>(hover, "dialog", "vbox", "frame", "name");

	_hovered_dialog = Hovered::NONE;
	if (top_level_frame == "network") _hovered_dialog = Hovered::NETWORK;
	if (top_level_frame == "storage") _hovered_dialog = Hovered::STORAGE;

	if (orig_hovered_dialog != _hovered_dialog)
		_apply_to_hovered_dialog(orig_hovered_dialog, [&] (Dialog &dialog) {
			dialog.hover(Xml_node("<hover/>")); });

	_apply_to_hovered_dialog(_hovered_dialog, [&] (Dialog &dialog) {
		dialog.hover(hover.sub_node("dialog").sub_node("vbox")
		                                     .sub_node("frame")); });
}


void Sculpt_manager::Main::_handle_nitpicker_hover()
{
	if (!_discovery_state.discovery_in_progress())
		return;

	_nitpicker_hover.update();

	if (_nitpicker_hover.xml().attribute_value("active", false) == true)
		_discovery_state.user_intervention = true;
}


void Sculpt_manager::Main::_handle_storage_devices_update()
{
	bool reconfigure_runtime = false;
	{
		_block_devices_rom.update();
		Block_device_update_policy policy(_env, _heap, _storage_device_update_handler);
		_storage_devices.update_block_devices_from_xml(policy, _block_devices_rom.xml());

		_storage_devices.block_devices.for_each([&] (Block_device &dev) {

			dev.process_part_blk_report();

			if (dev.state == Storage_device::UNKNOWN) {
				reconfigure_runtime = true; };
		});
	}

	{
		_usb_active_config_rom.update();
		Usb_storage_device_update_policy policy(_env, _heap, _storage_device_update_handler);
		Xml_node const config = _usb_active_config_rom.xml();
		Xml_node const raw = config.has_sub_node("raw")
		                   ? config.sub_node("raw") : Xml_node("<raw/>");

		_storage_devices.update_usb_storage_devices_from_xml(policy, raw);

		_storage_devices.usb_storage_devices.for_each([&] (Usb_storage_device &dev) {

			dev.process_driver_report();
			dev.process_part_blk_report();

			if (dev.state == Storage_device::UNKNOWN) {
				reconfigure_runtime = true; };
		});
	}

	if (!_sculpt_partition.valid()) {

		Storage_target const default_target =
			_discovery_state.detect_default_target(_storage_devices);

		if (default_target.valid())
			use(default_target);
	}

	generate_dialog();

	if (reconfigure_runtime)
		_generate_runtime_config();
}


void Sculpt_manager::Main::_generate_nic_router_config()
{
	if ((_nic_target.wired() && !_present_in_runtime("nic_drv"))
	 || (_nic_target.wifi()  && !_present_in_runtime("wifi_drv"))) {

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
			xml.attribute("interval_sec", "5");
			xml.attribute("bytes",        "yes");
			xml.attribute("config",       "yes");
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


void Sculpt_manager::Main::_handle_wlan_accesspoints()
{
	bool const initial_scan = !_wlan_accesspoints_rom.xml().has_sub_node("accesspoint");

	_wlan_accesspoints_rom.update();

	/* suppress updating the list while the access-point list is hovered */
	if (!initial_scan && _network_dialog.ap_list_hovered())
		return;

	Access_point_update_policy policy(_heap);
	_access_points.update_from_xml(policy, _wlan_accesspoints_rom.xml());
	generate_dialog();
}


void Sculpt_manager::Main::_handle_wlan_state()
{
	_wlan_state_rom.update();
	_wifi_connection = Wifi_connection::from_xml(_wlan_state_rom.xml());
	generate_dialog();
}


void Sculpt_manager::Main::_handle_nic_router_state()
{
	_nic_router_state_rom.update();

	Nic_state const old_nic_state = _nic_state;
	_nic_state = Nic_state::from_xml(_nic_router_state_rom.xml());
	generate_dialog();

	/* if the nic state becomes ready, consider spawning the update subsystem */
	if (old_nic_state.ready() != _nic_state.ready())
		_generate_runtime_config();
}


void Sculpt_manager::Main::_handle_manual_nic_router_config()
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
	_generate_runtime_config();
	generate_dialog();
}


void Sculpt_manager::Main::_handle_update_state()
{
	_update_state_rom.update();
	generate_dialog();

	bool const installation_complete =
		!_update_state_rom.xml().has_sub_node("archive");

	if (installation_complete) {
		_children.reset_incomplete();
		_handle_deploy();
	}
}


void Sculpt_manager::Main::_handle_runtime_state()
{
	_runtime_state.update();

	Xml_node state = _runtime_state.xml();

	bool reconfigure_runtime = false;

	/* check for completed storage operations */
	_storage_devices.for_each([&] (Storage_device &device) {

		device.for_each_partition([&] (Partition &partition) {

			Storage_target const target { device.label, partition.number };

			if (partition.check_in_progress) {
				String<64> name(target.label(), ".fsck.ext2");
				Child_exit_state exit_state(state, name);

				if (exit_state.exited) {
					if (exit_state.code != 0)
						error("file-system check failed");
					if (exit_state.code == 0)
						log("file-system check succeeded");

					partition.check_in_progress = 0;
					reconfigure_runtime = true;
					_storage_dialog.reset_operation();
				}
			}

			if (partition.format_in_progress) {
				String<64> name(target.label(), ".mkfs.ext2");
				Child_exit_state exit_state(state, name);

				if (exit_state.exited) {
					if (exit_state.code != 0)
						error("file-system creation failed");

					partition.format_in_progress = false;
					partition.file_system.type = File_system::EXT2;

					if (partition.whole_device())
						device.rediscover();

					reconfigure_runtime = true;
					_storage_dialog.reset_operation();
				}
			}

			/* respond to completion of file-system resize operation */
			if (partition.fs_resize_in_progress) {
				Child_exit_state exit_state(state, Start_name(target.label(), ".resize2fs"));
				if (exit_state.exited) {
					partition.fs_resize_in_progress = false;
					reconfigure_runtime = true;
					device.rediscover();
					_storage_dialog.reset_operation();
				}
			}

		}); /* for each partition */

		/* respond to completion of GPT relabeling */
		if (device.relabel_in_progress()) {
			Child_exit_state exit_state(state, device.relabel_start_name());
			if (exit_state.exited) {
				device.rediscover();
				reconfigure_runtime = true;
				_storage_dialog.reset_operation();
			}
		}

		/* respond to completion of GPT expand */
		if (device.gpt_expand_in_progress()) {
			Child_exit_state exit_state(state, device.expand_start_name());
			if (exit_state.exited) {

				/* kick off resize2fs */
				device.for_each_partition([&] (Partition &partition) {
					if (partition.gpt_expand_in_progress) {
						partition.gpt_expand_in_progress = false;
						partition.fs_resize_in_progress  = true;
					}
				});

				reconfigure_runtime = true;
				_storage_dialog.reset_operation();
			}
		}

	}); /* for each device */

	/* remove prepare subsystem when finished */
	{
		Child_exit_state exit_state(state, "prepare");
		if (exit_state.exited) {
			_prepare_completed = _prepare_version;

			/* trigger deployment */
			_handle_deploy();

			/* trigger update and deploy */
			reconfigure_runtime = true;
		}
	}

	/* upgrade ram_fs quota on demand */
	state.for_each_sub_node("child", [&] (Xml_node child) {

		if (child.attribute_value("name", String<16>()) == "ram_fs"
		 && child.has_sub_node("ram")
		 && child.sub_node("ram").has_attribute("requested")) {

			_ram_fs_state.ram_quota.value *= 2;
			reconfigure_runtime = true;
			generate_dialog();
		}
	});

	/* upgrade depot_rom quota on demand */
	state.for_each_sub_node("child", [&] (Xml_node child) {

		if (child.attribute_value("name", String<16>()) == "depot_rom"
		 && child.has_sub_node("ram")
		 && child.sub_node("ram").has_attribute("requested")) {

			_depot_rom_state.ram_quota.value *= 2;
			reconfigure_runtime = true;
		}
	});

	/*
	 * Re-attempt NIC-router configuration as the uplink may have become
	 * available in the meantime.
	 */
	if (_nic_target.nic_router_needed() && !_nic_router_config_up_to_date)
		_generate_nic_router_config();

	if (reconfigure_runtime)
		_generate_runtime_config();
}


void Sculpt_manager::Main::_generate_runtime_config(Xml_generator &xml) const
{
	xml.attribute("verbose", "yes");

	xml.node("report", [&] () {
		xml.attribute("init_ram",   "yes");
		xml.attribute("init_caps",  "yes");
		xml.attribute("child_ram",  "yes");
		xml.attribute("delay_ms",   4*500);
		xml.attribute("buffer",     "64K");
	});

	xml.node("parent-provides", [&] () {
		gen_parent_service<Rom_session>(xml);
		gen_parent_service<Cpu_session>(xml);
		gen_parent_service<Pd_session>(xml);
		gen_parent_service<Rm_session>(xml);
		gen_parent_service<Log_session>(xml);
		gen_parent_service<Timer::Session>(xml);
		gen_parent_service<Report::Session>(xml);
		gen_parent_service<Platform::Session>(xml);
		gen_parent_service<Block::Session>(xml);
		gen_parent_service<Usb::Session>(xml);
		gen_parent_service<::File_system::Session>(xml);
		gen_parent_service<Nitpicker::Session>(xml);
		gen_parent_service<Rtc::Session>(xml);
	});

	xml.node("start", [&] () {
		gen_ram_fs_start_content(xml, _ram_fs_state); });

	auto part_blk_needed_for_use = [&] (Storage_device const &dev) {
		return (_sculpt_partition.device == dev.label)
		     && _sculpt_partition.partition.valid(); };

	_storage_devices.block_devices.for_each([&] (Block_device const &dev) {
	
		if (dev.part_blk_needed_for_discovery()
		 || dev.part_blk_needed_for_access()
		 || part_blk_needed_for_use(dev))

			xml.node("start", [&] () {
				Storage_device::Label const parent { };
				dev.gen_part_blk_start_content(xml, parent); }); });

	_storage_devices.usb_storage_devices.for_each([&] (Usb_storage_device const &dev) {

		if (dev.usb_block_drv_needed() || _sculpt_partition.device == dev.label)
			xml.node("start", [&] () {
				dev.gen_usb_block_drv_start_content(xml); });

		if (dev.part_blk_needed_for_discovery()
		 || dev.part_blk_needed_for_access()
		 || part_blk_needed_for_use(dev))

			xml.node("start", [&] () {
				Storage_device::Label const driver = dev.usb_block_drv_name();
				dev.gen_part_blk_start_content(xml, driver);
		});
	});

	_storage_devices.for_each([&] (Storage_device const &device) {

		device.for_each_partition([&] (Partition const &partition) {

			Storage_target const target { device.label, partition.number };

			if (partition.check_in_progress) {
				xml.node("start", [&] () {
					gen_fsck_ext2_start_content(xml, target); }); }

			if (partition.format_in_progress) {
				xml.node("start", [&] () {
					gen_mkfs_ext2_start_content(xml, target); }); }

			if (partition.fs_resize_in_progress) {
				xml.node("start", [&] () {
					gen_resize2fs_start_content(xml, target); }); }

			if (partition.file_system.type != File_system::UNKNOWN) {
				if (partition.file_system_inspected || target == _sculpt_partition)
					xml.node("start", [&] () {
						gen_fs_start_content(xml, target, partition.file_system.type); });

				/*
				 * Create alias so that the default file system can be referred
				 * to as "default_fs_rw" without the need to know the name of the
				 * underlying storage target.
				 */
				if (target == _sculpt_partition)
					gen_named_node(xml, "alias", "default_fs_rw", [&] () {
						xml.attribute("child", target.fs()); });
			}

		}); /* for each partition */

		/* relabel partitions if needed */
		if (device.relabel_in_progress())
			xml.node("start", [&] () {
				gen_gpt_relabel_start_content(xml, device); });

		/* expand partitions if needed */
		if (device.expand_in_progress())
			xml.node("start", [&] () {
				gen_gpt_expand_start_content(xml, device); });

	}); /* for each device */

	if (_sculpt_partition.ram_fs())
		gen_named_node(xml, "alias", "default_fs_rw", [&] () {
			xml.attribute("child", "ram_fs"); });

	/*
	 * Determine whether showing the file-system browser or not
	 */
	bool any_file_system_inspected = _ram_fs_state.inspected;
	_storage_devices.for_each([&] (Storage_device const &device) {
		device.for_each_partition([&] (Partition const &partition) {
			any_file_system_inspected |= partition.file_system_inspected; }); });

	/*
	 * Load configuration and update depot config on the sculpt partition
	 */
	if (_sculpt_partition.valid() && _prepare_in_progress())
		xml.node("start", [&] () {
			gen_prepare_start_content(xml, _prepare_version); });

	if (any_file_system_inspected)
		gen_file_browser(xml, _storage_devices, _ram_fs_state, _file_browser_version);

	/*
	 * Spawn chroot instances for accessing '/depot' and '/public'. The
	 * chroot instances implicitly refer to the 'default_fs_rw'.
	 */
	if (_sculpt_partition.valid()) {

		auto chroot = [&] (Start_name const &name, Path const &path, Writeable w) {
			xml.node("start", [&] () {
				gen_chroot_start_content(xml, name, path, w); }); };

		chroot("depot_rw",  "/depot",  WRITEABLE);
		chroot("depot",     "/depot",  READ_ONLY);
		chroot("public_rw", "/public", WRITEABLE);
	}

	/*
	 * Network drivers and NIC router
	 */
	if (_use_nic_drv)
		xml.node("start", [&] () { gen_nic_drv_start_content(xml); });

	if (_use_wifi_drv)
		xml.node("start", [&] () { gen_wifi_drv_start_content(xml); });

	if (_nic_target.type != Nic_target::OFF)
		xml.node("start", [&] () {
			gen_nic_router_start_content(xml); });

	/*
	 * Update subsystem
	 */
	bool const network_connectivity = (_nic_target.type == Nic_target::WIRED)
	                               || (_nic_target.type == Nic_target::WIFI);

	if (_sculpt_partition.valid() && !_prepare_in_progress()
	 && _nic_state.ready() && network_connectivity)

		xml.node("start", [&] () {
			gen_update_start_content(xml); });

	/*
	 * Deployment infrastructure
	 */
	if (_sculpt_partition.valid() && !_prepare_in_progress()) {

		xml.node("start", [&] () {
			gen_fs_rom_start_content(xml, "depot_rom", "depot",
			                         _depot_rom_state.ram_quota); });

		xml.node("start", [&] () {
			gen_depot_query_start_content(xml); });

		Xml_node const manual_deploy = _manual_deploy_rom.xml();

		/* insert content of '<static>' node as is */
		if (manual_deploy.has_sub_node("static")) {
			Xml_node static_config = manual_deploy.sub_node("static");
			xml.append(static_config.content_base(), static_config.content_size());
		}

		/* generate start nodes for deployed packages */
		if (manual_deploy.has_sub_node("common_routes"))
			_children.gen_start_nodes(xml, manual_deploy.sub_node("common_routes"),
			                          "depot_rom");
	}
}


void Sculpt_manager::Main::_handle_deploy()
{
	Xml_node const manual_deploy = _manual_deploy_rom.xml();

	/* determine CPU architecture of deployment */
	_arch = manual_deploy.attribute_value("arch", Arch());
	if (!_arch.valid())
		warning("manual deploy config lacks 'arch' attribute");

	try { _children.apply_config(manual_deploy); }
	catch (...) {
		error("spurious exception during deploy update (apply_config)"); }

	/* update query for blueprints of all unconfigured start nodes */
	if (_arch.valid()) {
		_depot_query_reporter.generate([&] (Xml_generator &xml) {
			xml.attribute("arch",    _arch);
			xml.attribute("version", _query_version.value);
			_children.gen_queries(xml);
		});
	}

	/*
	 * Apply blueprint after 'gen_queries'. Otherwise the application of a
	 * stale blueprint may flag children whose pkgs have been installed in the
	 * meanwhile as incomplete, suppressing their respective queries.
	 */
	try {
		Xml_node const blueprint = _blueprint_rom.xml();

		/* apply blueprint, except when stale */
		typedef String<32> Version;
		Version const version = blueprint.attribute_value("version", Version());
		if (version == Version(_query_version.value))
			_children.apply_blueprint(_blueprint_rom.xml());
	}
	catch (...) {
		error("spurious exception during deploy update (apply_blueprint)"); }

	/* feed missing packages to installation queue */
	_installation_reporter.generate([&] (Xml_generator &xml) {
		xml.attribute("arch", _arch);
		_children.gen_installation_entries(xml); });

	_generate_runtime_config();
}


void Component::construct(Genode::Env &env)
{
	static Sculpt_manager::Main main(env);
}

